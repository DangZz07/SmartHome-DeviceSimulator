#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib,"ws2_32.lib")

#define PORT 5000
#define BUFFER_SIZE 1024
#define MAX_TOKENS 100
#define TOKEN_LENGTH 32

// Token management structure
typedef struct {
    char app_id[50];
    char token[TOKEN_LENGTH+1];
    time_t created_at;
} TOKEN_ENTRY;

TOKEN_ENTRY tokens[MAX_TOKENS];
int token_count = 0;

typedef struct {
    SOCKET client_sock;
    char username[50]; // token owner
    char token[TOKEN_LENGTH+1];
    int logged_in;
} THREAD_PARAM;

// ------------------- Token Management -------------------

// Generate random token
void generate_token(char* token) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    srand((unsigned int)time(NULL) + rand());
    for(int i = 0; i < TOKEN_LENGTH; i++) {
        token[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    token[TOKEN_LENGTH] = '\0';
}

// Create and store token for app_id
char* create_token(const char* app_id) {
    if(token_count >= MAX_TOKENS) {
        // Remove oldest token
        for(int i = 0; i < MAX_TOKENS - 1; i++) {
            tokens[i] = tokens[i+1];
        }
        token_count--;
    }
    
    generate_token(tokens[token_count].token);
    strcpy(tokens[token_count].app_id, app_id);
    tokens[token_count].created_at = time(NULL);
    token_count++;
    
    return tokens[token_count-1].token;
}

// Validate token
int validate_token(const char* token) {
    for(int i = 0; i < token_count; i++) {
        if(strcmp(tokens[i].token, token) == 0) {
            // Token expires after 24 hours
            if(time(NULL) - tokens[i].created_at < 86400) {
                return 1;
            }
        }
    }
    return 0;
}

// ------------------- Power Consumption Calculation -------------------

// Calculate power consumption for Light (Watts)
double calculate_light_power(const char* state) {
    if(strcmp(state, "OFF") == 0) return 0.0;
    return 60.0; // 60W when ON
}

// Calculate power consumption for Fan (Watts)
double calculate_fan_power(const char* state, int speed) {
    if(strcmp(state, "OFF") == 0) return 0.0;
    // Speed 1: 30W, Speed 2: 50W, Speed 3: 75W
    switch(speed) {
        case 1: return 30.0;
        case 2: return 50.0;
        case 3: return 75.0;
        default: return 0.0;
    }
}

// Calculate power consumption for AC (Watts)
double calculate_ac_power(const char* state, const char* mode, int temp) {
    if(strcmp(state, "OFF") == 0) return 0.0;
    
    double base_power = 0.0;
    // Base power by mode
    if(strcmp(mode, "COOL") == 0) base_power = 1500.0;
    else if(strcmp(mode, "HEAT") == 0) base_power = 2000.0;
    else if(strcmp(mode, "DRY") == 0) base_power = 800.0;
    
    // Temperature adjustment: lower temp = more power for cooling, higher temp = more power for heating
    if(strcmp(mode, "COOL") == 0) {
        // Lower temperature = more power (target 25C is baseline)
        base_power += (25 - temp) * 50.0;
    } else if(strcmp(mode, "HEAT") == 0) {
        // Higher temperature = more power (target 25C is baseline)
        base_power += (temp - 25) * 50.0;
    }
    
    return base_power;
}

// ------------------- User/Device -------------------

// Helper function to get file path (for writing)
void get_file_path(const char* filename, char* path) {
    // Try ../python_simulator/filename
    sprintf(path, "../python_simulator/%s", filename);
    FILE* test = fopen(path, "r");
    if(test) { fclose(test); return; }
    // Try python_simulator/filename
    sprintf(path, "python_simulator/%s", filename);
    test = fopen(path, "r");
    if(test) { fclose(test); return; }
    // Use filename in current directory
    strcpy(path, filename);
}

// Helper function to open file with multiple path attempts
FILE* open_file(const char* filename, const char* mode) {
    char path[256];
    // Try ../python_simulator/filename
    sprintf(path, "../python_simulator/%s", filename);
    FILE* f = fopen(path, mode);
    if(f) return f;
    // Try python_simulator/filename
    sprintf(path, "python_simulator/%s", filename);
    f = fopen(path, mode);
    if(f) return f;
    // Try filename in current directory
    f = fopen(filename, mode);
    return f;
}

void trim(char* str) {
    int i;
    // Loại bỏ \r \n cuối
    for(i=strlen(str)-1;i>=0;i--) {
        if(str[i]=='\n' || str[i]=='\r' || str[i]==' ' || str[i]=='\t') str[i]=0;
        else break;
    }
}

int check_user_login(const char* username, const char* password) {
    FILE* f = open_file("users.txt", "r");
    if(!f) return 0;
    char line[200];
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        char u[50], p[50];
        if(sscanf(line,"%49s %49s", u,p)==2) {
            trim(u); trim(p);
            if(strcmp(u,username)==0 && strcmp(p,password)==0) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

// Return devices of owner as string
void scan_devices(const char* owner, char* result) {
    FILE* f = open_file("devices.txt", "r");
    if(!f) { strcpy(result,"NO_DEVICES\n"); return; }

    char id[50], type[50], dev_owner[50], pass[50], state[20], extra1[20], extra2[20], power_str[20], timer_time[20], timer_action[20];
    result[0]='\0';

    char line[500];
    while(fgets(line, sizeof(line), f)) {
        // Try to read with extended format, but accept minimum 6 fields
        int fields = sscanf(line, "%s %s %s %s %s %s %s %s %s %s", id,type,dev_owner,pass,state,extra1,extra2,power_str,timer_time,timer_action);
        if(fields >= 6 && strcmp(dev_owner,owner)==0) {
            char device_line[100];
            sprintf(device_line,"%s %s\n", id, type);
            strcat(result,device_line);
        }
    }
    fclose(f);
}

// Connect device - returns device info
int connect_device(const char* device_id, const char* password, char* device_info) {
    FILE* f = open_file("devices.txt", "r");
    if(!f) return 0;
    char id[50], type[50], dev_owner[50], pass[50], state[20], extra1[20], extra2[20], power_str[20], timer_time[20], timer_action[20];
    
    char line[500];
    while(fgets(line, sizeof(line), f)) {
        int fields = sscanf(line, "%s %s %s %s %s %s %s %s %s %s", id,type,dev_owner,pass,state,extra1,extra2,power_str,timer_time,timer_action);
        if(fields >= 6 && strcmp(id,device_id)==0 && strcmp(pass,password)==0) {
            // Initialize defaults if missing
            if(fields < 8) strcpy(power_str, "0.0");
            
            // Format device info: ID TYPE STATE EXTRA1 EXTRA2 POWER
            sprintf(device_info, "ID:%s TYPE:%s STATE:%s", id, type, state);
            if(strcmp(type,"Fan")==0) {
                sprintf(device_info + strlen(device_info), " SPEED:%s", extra1);
            } else if(strcmp(type,"AC")==0) {
                sprintf(device_info + strlen(device_info), " MODE:%s TEMP:%s", extra1, extra2);
            }
            sprintf(device_info + strlen(device_info), " POWER:%.2fW", atof(power_str));
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

// Read device info by ID (extended format with backward compatibility)
int read_device(const char* device_id, char* type, char* owner, char* state, char* extra1, char* extra2, char* power_str, char* timer_time, char* timer_action) {
    FILE* f = open_file("devices.txt", "r");
    if(!f) return 0;
    char id[50], t[50], o[50], p[50], s[20], e1[20], e2[20], pw[20], tt[20], ta[20];
    
    // Initialize defaults
    strcpy(power_str, "0.0");
    strcpy(timer_time, "-1");
    strcpy(timer_action, "NONE");
    
    // Read line by line to handle both old and new formats
    char line[500];
    while(fgets(line, sizeof(line), f)) {
        // Try to read with extended format (10 fields)
        int fields = sscanf(line, "%s %s %s %s %s %s %s %s %s %s", id,t,o,p,s,e1,e2,pw,tt,ta);
        if(fields >= 6) {
            if(strcmp(id,device_id)==0) {
                strcpy(type,t); strcpy(owner,o); strcpy(state,s); 
                strcpy(extra1,e1); strcpy(extra2,e2);
                // Handle power field
                if(fields >= 8 && strlen(pw) > 0) {
                    strcpy(power_str, pw);
                } else {
                    // Calculate power if not in file
                    double power = 0.0;
                    if(strcmp(t,"Light")==0) {
                        power = calculate_light_power(s);
                    } else if(strcmp(t,"Fan")==0) {
                        int speed = atoi(e1);
                        if(speed < 1) speed = 1;
                        if(speed > 3) speed = 3;
                        power = calculate_fan_power(s, speed);
                    } else if(strcmp(t,"AC")==0) {
                        int temp = atoi(e2);
                        if(temp < 18) temp = 18;
                        if(temp > 30) temp = 30;
                        power = calculate_ac_power(s, e1, temp);
                    }
                    sprintf(power_str, "%.2f", power);
                }
                // Handle timer fields
                if(fields >= 9 && strlen(tt) > 0 && strcmp(tt, "-1") != 0) {
                    strcpy(timer_time, tt);
                }
                if(fields >= 10 && strlen(ta) > 0 && strcmp(ta, "NONE") != 0) {
                    strcpy(timer_action, ta);
                }
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

// Update device info by ID (with power calculation)
int update_device(const char* device_id, const char* state, const char* extra1, const char* extra2, const char* timer_time, const char* timer_action) {
    // Find the correct path for devices.txt
    char devices_path[256];
    FILE* f = fopen("../python_simulator/devices.txt", "r");
    if(f) {
        strcpy(devices_path, "../python_simulator/devices.txt");
    } else {
        f = fopen("python_simulator/devices.txt", "r");
        if(f) {
            strcpy(devices_path, "python_simulator/devices.txt");
        } else {
            f = fopen("devices.txt", "r");
            if(f) {
                strcpy(devices_path, "devices.txt");
            } else {
                return 0;
            }
        }
    }
    
    // Create tmp file in same directory
    char tmp_path[256];
    sprintf(tmp_path, "%s.tmp", devices_path);
    FILE* tmp = fopen(tmp_path, "w");
    if(!tmp) { fclose(f); return 0; }
    
    char id[50], type[50], owner[50], pass[50], s[20], e1[20], e2[20], pw[20], tt[20], ta[20];
    int updated = 0;

    char line[500];
    while(fgets(line, sizeof(line), f)) {
        // Read with backward compatibility
        int fields = sscanf(line, "%s %s %s %s %s %s %s %s %s %s", id,type,owner,pass,s,e1,e2,pw,tt,ta);
        if(fields >= 6) {
            // Initialize defaults if missing
            if(fields < 8) strcpy(pw, "0.0");
            if(fields < 9) strcpy(tt, "-1");
            if(fields < 10) strcpy(ta, "NONE");
            
            if(strcmp(id,device_id)==0) {
                if(state) strcpy(s,state);
                if(extra1) strcpy(e1,extra1);
                if(extra2) strcpy(e2,extra2);
                if(timer_time) strcpy(tt, timer_time);
                if(timer_action) strcpy(ta, timer_action);
                
                // Calculate power consumption
                double power = 0.0;
                if(strcmp(type,"Light")==0) {
                    power = calculate_light_power(s);
                } else if(strcmp(type,"Fan")==0) {
                    int speed = atoi(e1);
                    if(speed < 1) speed = 1;
                    if(speed > 3) speed = 3;
                    power = calculate_fan_power(s, speed);
                } else if(strcmp(type,"AC")==0) {
                    int temp = atoi(e2);
                    if(temp < 18) temp = 18;
                    if(temp > 30) temp = 30;
                    power = calculate_ac_power(s, e1, temp);
                }
                sprintf(pw, "%.2f", power);
                
                updated = 1;
            }
            // Write in extended format
            fprintf(tmp,"%s %s %s %s %s %s %s %s %s %s\n",id,type,owner,pass,s,e1,e2,pw,tt,ta);
        }
    }

    fclose(f); fclose(tmp);
    
    // Remove old file and rename tmp
    remove(devices_path);
    rename(tmp_path, devices_path);
    
    return updated;
}

// Check and execute timer actions
void check_timers() {
    FILE* f = open_file("devices.txt", "r");
    if(!f) return;
    
    time_t current_time = time(NULL);
    char id[50], type[50], owner[50], pass[50], s[20], e1[20], e2[20], pw[20], tt[20], ta[20];
    
    char line[500];
    while(fgets(line, sizeof(line), f)) {
        int fields = sscanf(line, "%s %s %s %s %s %s %s %s %s %s", id,type,owner,pass,s,e1,e2,pw,tt,ta);
        if(fields >= 6) {
            // Initialize defaults if missing
            if(fields < 9) strcpy(tt, "-1");
            if(fields < 10) strcpy(ta, "NONE");
            
            if(strcmp(tt, "-1") != 0 && strcmp(ta, "NONE") != 0) {
                time_t timer_t = (time_t)atol(tt);
                if(timer_t > 0 && current_time >= timer_t) {
                    // Execute timer action
                    if(strcmp(ta, "ON") == 0 && strcmp(s, "OFF") == 0) {
                        update_device(id, "ON", NULL, NULL, "-1", "NONE");
                    } else if(strcmp(ta, "OFF") == 0 && strcmp(s, "ON") == 0) {
                        update_device(id, "OFF", NULL, NULL, "-1", "NONE");
                    }
                }
            }
        }
    }
    fclose(f);
}

// ---------------- Client Handler ----------------
DWORD WINAPI client_handler(LPVOID arg) {
    THREAD_PARAM* param = (THREAD_PARAM*)arg;
    SOCKET client_sock = param->client_sock;
    param->logged_in=0;
    param->token[0]='\0';
    char buffer[BUFFER_SIZE];
    int bytes;

    while((bytes=recv(client_sock,buffer,BUFFER_SIZE-1,0))>0) {
        buffer[bytes]='\0';
        printf("Received: %s\n", buffer);

        // Check timers periodically
        check_timers();

        // LOGIN
        if(strncmp(buffer,"LOGIN ",6)==0) {
            char username[50], password[50], app_id[50];
            int fields = sscanf(buffer+6,"%49s %49s %49s", username,password,app_id);
            if(fields < 2) {
                send(client_sock,"400 BAD_REQUEST\r\n",17,0);
                continue;
            }
            trim(username); trim(password);
            if(fields == 3) trim(app_id);
            else sprintf(app_id, "%s_%ld", username, time(NULL));
            
            if(check_user_login(username,password)) {
                param->logged_in=1;
                strcpy(param->username,username);
                char* token = create_token(app_id);
                strcpy(param->token, token);
                char resp[BUFFER_SIZE];
                sprintf(resp,"200 OK Token=%s AppID=%s\r\n", token, app_id);
                send(client_sock, resp, strlen(resp),0);
            } else {
                send(client_sock,"401 WRONG_PASS\r\n",17,0);
            }
        }

        // SCAN
        else if(strncmp(buffer,"SCAN ",5)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char resp[BUFFER_SIZE];
            scan_devices(param->username, resp);
            send(client_sock, resp, strlen(resp),0);
        }

        // CONNECT
        else if(strncmp(buffer,"CONNECT ",8)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50], password[50], device_info[BUFFER_SIZE];
            sscanf(buffer+8,"%s %s", dev_id,password);
            if(connect_device(dev_id,password,device_info)) {
                char resp[BUFFER_SIZE];
                sprintf(resp,"200 OK CONNECTED %s\r\n", device_info);
                send(client_sock, resp, strlen(resp),0);
            } else {
                send(client_sock,"401 WRONG_PASS\r\n",17,0);
            }
        }

        // POWER
        else if(strncmp(buffer,"POWER ",6)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50], state[10], type[50], owner[50], e1[20], e2[20], pw[20], tt[20], ta[20];
            sscanf(buffer+6,"%s %s", dev_id,state);
            if(!read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta)) { 
                send(client_sock,"404 NOT_FOUND\r\n",15,0); 
                continue; 
            }
            if(strcmp(owner,param->username)!=0) { 
                send(client_sock,"403 FORBIDDEN\r\n",16,0); 
                continue; 
            }
            if(strcmp(state,"ON")!=0 && strcmp(state,"OFF")!=0) { 
                send(client_sock,"400 INVALID_VALUE\r\n",20,0); 
                continue; 
            }
            update_device(dev_id,state,NULL,NULL,NULL,NULL);
            // Read updated power
            read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta);
            char resp[BUFFER_SIZE];
            sprintf(resp,"200 OK POWER %s POWER:%.2fW\r\n", state, atof(pw));
            send(client_sock, resp, strlen(resp),0);
        }

        // SPEED
        else if(strncmp(buffer,"SPEED ",6)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50]; int level;
            char type[50], owner[50], state[20], e1[20], e2[20], pw[20], tt[20], ta[20];
            sscanf(buffer+6,"%s %d", dev_id,&level);
            read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta);
            if(strcmp(owner,param->username)!=0) { 
                send(client_sock,"403 FORBIDDEN\r\n",16,0); 
                continue; 
            }
            if(strcmp(type,"Fan")!=0) { 
                send(client_sock,"405 NOT_SUPPORTED\r\n",20,0); 
                continue; 
            }
            if(level<1 || level>3) { 
                send(client_sock,"400 INVALID_VALUE\r\n",20,0); 
                continue; 
            }
            char lev[20]; sprintf(lev,"%d",level);
            update_device(dev_id,NULL,lev,NULL,NULL,NULL);
            // Read updated power
            read_device(dev_id,type,owner,state,lev,e2,pw,tt,ta);
            char resp[BUFFER_SIZE];
            sprintf(resp,"200 OK SPEED %d POWER:%.2fW\r\n", level, atof(pw));
            send(client_sock, resp, strlen(resp),0);
        }

        // MODE
        else if(strncmp(buffer,"MODE ",5)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50], mode[20]; 
            char type[50], owner[50], state[20], e1[20], e2[20], pw[20], tt[20], ta[20];
            sscanf(buffer+5,"%s %s", dev_id,mode);
            read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta);
            if(strcmp(owner,param->username)!=0) { 
                send(client_sock,"403 FORBIDDEN\r\n",16,0); 
                continue; 
            }
            if(strcmp(type,"AC")!=0) { 
                send(client_sock,"405 NOT_SUPPORTED\r\n",20,0); 
                continue; 
            }
            if(strcmp(mode,"COOL")!=0 && strcmp(mode,"HEAT")!=0 && strcmp(mode,"DRY")!=0) { 
                send(client_sock,"400 INVALID_VALUE\r\n",20,0); 
                continue; 
            }
            update_device(dev_id,NULL,mode,NULL,NULL,NULL);
            // Read updated power
            read_device(dev_id,type,owner,state,mode,e2,pw,tt,ta);
            char resp[BUFFER_SIZE];
            sprintf(resp,"200 OK MODE %s POWER:%.2fW\r\n", mode, atof(pw));
            send(client_sock, resp, strlen(resp),0);
        }

        // TEMP
        else if(strncmp(buffer,"TEMP ",5)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50]; int temp;
            char type[50], owner[50], state[20], e1[20], e2[20], pw[20], tt[20], ta[20];
            sscanf(buffer+5,"%s %d", dev_id,&temp);
            read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta);
            if(strcmp(owner,param->username)!=0) { 
                send(client_sock,"403 FORBIDDEN\r\n",16,0); 
                continue; 
            }
            if(strcmp(type,"AC")!=0) { 
                send(client_sock,"405 NOT_SUPPORTED\r\n",20,0); 
                continue; 
            }
            if(temp<18 || temp>30) { 
                send(client_sock,"400 INVALID_VALUE\r\n",20,0); 
                continue; 
            }
            char t[20]; sprintf(t,"%d",temp);
            update_device(dev_id,NULL,e1,t,NULL,NULL);
            // Read updated power
            read_device(dev_id,type,owner,state,e1,t,pw,tt,ta);
            char resp[BUFFER_SIZE];
            sprintf(resp,"200 OK TEMP %d POWER:%.2fW\r\n", temp, atof(pw));
            send(client_sock, resp, strlen(resp),0);
        }

        // TIMER - Set timer for device
        else if(strncmp(buffer,"TIMER ",6)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50], action[20], timer_str[50];
            int seconds;
            sscanf(buffer+6,"%s %s %d", dev_id, action, &seconds);
            char type[50], owner[50], state[20], e1[20], e2[20], pw[20], tt[20], ta[20];
            if(!read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta)) { 
                send(client_sock,"404 NOT_FOUND\r\n",15,0); 
                continue; 
            }
            if(strcmp(owner,param->username)!=0) { 
                send(client_sock,"403 FORBIDDEN\r\n",16,0); 
                continue; 
            }
            if(strcmp(action,"ON")!=0 && strcmp(action,"OFF")!=0) { 
                send(client_sock,"400 INVALID_VALUE\r\n",20,0); 
                continue; 
            }
            if(seconds < 0) { 
                send(client_sock,"400 INVALID_VALUE\r\n",20,0); 
                continue; 
            }
            
            time_t timer_time = time(NULL) + seconds;
            sprintf(timer_str, "%ld", timer_time);
            update_device(dev_id,NULL,NULL,NULL,timer_str,action);
            char resp[BUFFER_SIZE];
            sprintf(resp,"200 OK TIMER SET %s in %d seconds\r\n", action, seconds);
            send(client_sock, resp, strlen(resp),0);
        }

        // STATUS
        else if(strncmp(buffer,"STATUS ",7)==0) {
            if(!param->logged_in || !validate_token(param->token)) { 
                send(client_sock,"401 INVALID_TOKEN\r\n",20,0); 
                continue; 
            }
            char dev_id[50], type[50], owner[50], state[20], e1[20], e2[20], pw[20], tt[20], ta[20];
            sscanf(buffer+7,"%s", dev_id);
            if(!read_device(dev_id,type,owner,state,e1,e2,pw,tt,ta)) { 
                send(client_sock,"404 NOT_FOUND\r\n",15,0); 
                continue; 
            }
            if(strcmp(owner,param->username)!=0) { 
                send(client_sock,"403 FORBIDDEN\r\n",16,0); 
                continue; 
            }
            char resp[BUFFER_SIZE];
            sprintf(resp,"200 OK STATUS %s STATE:%s",dev_id,state);
            if(strcmp(type,"Fan")==0) {
                sprintf(resp + strlen(resp), " SPEED:%s", e1);
            } else if(strcmp(type,"AC")==0) {
                sprintf(resp + strlen(resp), " MODE:%s TEMP:%s", e1, e2);
            }
            sprintf(resp + strlen(resp), " POWER:%.2fW", atof(pw));
            if(strcmp(tt, "-1") != 0 && strcmp(ta, "NONE") != 0) {
                time_t timer_t = atol(tt);
                time_t remaining = timer_t - time(NULL);
                if(remaining > 0) {
                    sprintf(resp + strlen(resp), " TIMER:%s in %ld seconds", ta, remaining);
                } else {
                    sprintf(resp + strlen(resp), " TIMER:EXPIRED");
                }
            }
            strcat(resp, "\r\n");
            send(client_sock,resp,strlen(resp),0);
        }

        else {
            send(client_sock,"UNKNOWN_CMD\r\n",12,0);
        }
    }

    closesocket(client_sock);
    free(param);
    return 0;
}

// ---------------- Main ----------------
int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);

    SOCKET server_sock = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr=INADDR_ANY;
    server.sin_port=htons(PORT);

    bind(server_sock,(struct sockaddr*)&server,sizeof(server));
    listen(server_sock,5);

    printf("Server running on port %d...\n",PORT);
    printf("Features: Power consumption, Timer, Random tokens, Device info on connect\n");

    while(1) {
        struct sockaddr_in client;
        int c = sizeof(client);
        SOCKET client_sock = accept(server_sock,(struct sockaddr*)&client,&c);
        printf("Client connected\n");

        THREAD_PARAM* param = malloc(sizeof(THREAD_PARAM));
        param->client_sock = client_sock;
        param->logged_in = 0;
        param->token[0] = '\0';

        CreateThread(NULL,0,client_handler,param,0,NULL);
    }

    closesocket(server_sock);
    WSACleanup();
    return 0;
}
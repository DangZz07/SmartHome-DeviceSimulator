#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024

/* ================= SCREEN STATE ================= */
typedef enum
{
    SCREEN_HOME,
    SCREEN_SCAN,
    SCREEN_DEVICE,
    SCREEN_EXIT
} Screen;

Screen current_screen = SCREEN_HOME;

/* ===== DEVICE STATE ===== */
char current_device_id[64] = "";
char current_token[64] = "";
int device_connected = 0;

/* ===== SCAN TOKEN CACHE ===== */
typedef struct
{
    char id[64];
    char token[64];
} DeviceToken;

DeviceToken scan_tokens[64];
int scan_token_count = 0;

/* ================= RESPONSE CODE ================= */
void interpret_response(const char *code)
{
    if (strcmp(code, "100") == 0)
        printf("→ CONNECTED TO SERVER\n");
    else if (strcmp(code, "200") == 0)
        printf("→ SUCCESS\n");
    else if (strcmp(code, "201") == 0)
        printf("→ WRONG PASSWORD\n");
    else if (strcmp(code, "202") == 0)
        printf("→ DEVICE NOT FOUND\n");
    else if (strcmp(code, "203") == 0)
        printf("→ INVALID FORMAT\n");
    else if (strcmp(code, "210") == 0)
        printf("→ PASSWORD CHANGED OK\n");
    else if (strcmp(code, "211") == 0)
        printf("→ WRONG OLD PASSWORD\n");
    else if (strcmp(code, "300") == 0)
        printf("→ UNKNOWN COMMAND\n");
    else if (strcmp(code, "500") == 0)
        printf("500. BAD_REQUEST\n");
    else if (strcmp(code, "400") == 0)
        printf("400. INVALID_VALUE\n");
    else if (strcmp(code, "401") == 0)
        printf("401. INVALID TOKEN\n");
    else if (strcmp(code, "221") == 0)
        printf("221. ALREADY SET/CANCEL\n");
    else if (strcmp(code, "405") == 0)
        printf("→ DEVICE NOT SUPPORT SPEED\n");
    else if (strcmp(code, "502") == 0)
        printf("→ DEVICE IS OFF\n");
}

/* ================= RECEIVE LINE (CRLF SAFE) ================= */
static char message[8192];
static int message_len = 0;

int receive_line(int sock, char *buffer, size_t size)
{
char temp_buff[BUF_SIZE];
    
    while (1) {
        // 1. Kiểm tra xem trong 'message' hiện tại đã có dòng nào chưa (\r\n)
        char *pos = strstr(message, "\r\n");
        if (pos != NULL) {
            int line_len = (int)(pos - message);
            
            // Chỉ copy tối đa 'size - 1' để tránh tràn bộ đệm 'buffer'
            int copy_len = (line_len < (int)size - 1) ? line_len : (int)size - 1;
            
            memcpy(buffer, message, copy_len);
            buffer[copy_len] = '\0';

            // Dịch chuyển phần dư lên đầu
            int consumed = line_len + 2; // +2 là cho \r\n
            int remaining = message_len - consumed;

            if (remaining > 0) {
                memmove(message, message + consumed, remaining);
            }
            message_len = remaining;
            message[message_len] = '\0'; // Luôn kết thúc chuỗi để strstr chạy đúng lần sau
            
            return 1; 
        }

        // 2. Nhận thêm một khối dữ liệu lớn
        int n = recv(sock, temp_buff, sizeof(temp_buff) - 1, 0);
        if (n <= 0) return 0;

        // 3. Nối vào bộ đệm tích lũy bằng memcpy (an toàn và nhanh hơn strcat)
        if (message_len + n < (int)sizeof(message) - 1) {
            memcpy(message + message_len, temp_buff, n);
            message_len += n;
            message[message_len] = '\0'; // Cực kỳ quan trọng để strstr tìm thấy điểm dừng
        } else {
            // Buffer quá đầy mà không thấy \r\n, reset để tránh treo
            message_len = 0;
            message[0] = '\0';
        }
    }
}

/* ================= SCAN MULTI LINE ================= */
int receive_scan_until_end(int sock)
{
    char line[BUF_SIZE];
    while (1)
    {
        if (!receive_line(sock, line, sizeof(line)))
            return 0;
        printf("%s\n", line);
        if (strcmp(line, "END") == 0)
            break;
    }
    return 1;
}

int receive_scan_until_end2(int sock)
{
    char line[BUF_SIZE];
    int in_my = 0;

    scan_token_count = 0; // reset mỗi lần scan

    while (1)
    {
        if (!receive_line(sock, line, sizeof(line)))
            return 0;

        if (strcmp(line, "MY DEVICES") == 0)
        {
            in_my = 1;
            printf("MY DEVICES\n");
            continue;
        }

        if (strcmp(line, "NEW DEVICES") == 0)
        {
            in_my = 0;
            printf("NEW DEVICES\n");
            continue;
        }

        if (strcmp(line, "END") == 0)
        {
            printf("END\n");
            break;
        }

        if (in_my)
        {
            char id[64], type[32], name[128], token[64];

            if (sscanf(line,
                       "%63s || %31s || %127s || %63s",
                       id, type, name, token) == 4) {

                // lưu token
                strcpy(scan_tokens[scan_token_count].id, id);
                strcpy(scan_tokens[scan_token_count].token, token);
                scan_token_count++;

                // in KHÔNG token
                printf("%s || %s || %s\n", id, type, name);
            }
        }
        else
        {
            // NEW DEVICE
            printf("%s\n", line);
        }
    }
    return 1;
}

char *find_scan_token(const char *id)
{
    for (int i = 0; i < scan_token_count; i++)
    {
        if (strcmp(scan_tokens[i].id, id) == 0)
            return scan_tokens[i].token;
    }
    return NULL;
}

/* ================= HOME ================= */
void show_home_menu()
{
    printf("\n========== HOME ==========\n");
    printf("1. Show Home\n");
    printf("2. Show Room\n");
    printf("3. Scan Devices\n");
    printf("0. Exit\n");
    printf("==========================\n");
    printf("Choose: ");
}

void handle_home(int sock)
{
    char c[8];
    fgets(c, sizeof(c), stdin);

    if (c[0] == '1')
    {
        send(sock, "SHOW HOME\r\n", 11, 0);
        receive_scan_until_end(sock);
    }
    else if (c[0] == '2')
    {
        char room[64], cmd[128];
        printf("Enter Room ID: ");
        fgets(room, sizeof(room), stdin);
        room[strcspn(room, "\n")] = 0;

        snprintf(cmd, sizeof(cmd),
                 "SHOW ROOM %s\r\n", room);
        send(sock, cmd, strlen(cmd), 0);
        receive_scan_until_end(sock);
    }
    else if (c[0] == '3')
    {
        current_screen = SCREEN_SCAN;
    }
    else if (c[0] == '0')
    {
        current_screen = SCREEN_EXIT;
    }
}

/* ================= SCAN ================= */
void show_scan(int sock)
{
    printf("\n========== SCAN ==========\n");
    send(sock, "SCAN\r\n", 6, 0);
    receive_scan_until_end2(sock);
    printf("\nEnter device ID (or 0 to back): ");
}

void handle_scan()
{
    char id[64];
    fgets(id, sizeof(id), stdin);
    id[strcspn(id, "\n")] = 0;

    if (strcmp(id, "0") == 0)
    {
        current_screen = SCREEN_HOME;
        return;
    }

    strcpy(current_device_id, id);

    char *tk = find_scan_token(id);
    if (tk)
    {
        // MY DEVICE
        strcpy(current_token, tk);
        device_connected = 1;
    }
    else
    {
        // NEW DEVICE
        current_token[0] = '\0';
        device_connected = 0;
    }

    current_screen = SCREEN_DEVICE;
}

/* ================= DEVICE MENU ================= */
void show_device_menu()
{
    printf("\n====== DEVICE ======\n");
    printf("Device: %s\n", current_device_id);

    if (!device_connected)
    {
        printf("1. Connect\n");
    }
    else
    {
        printf("2. Power\n");
        printf("3. Change Password\n");
        printf("4. Timer\n");
        printf("5. Speed\n");
    }

    printf("0. Back\n");
    printf("====================\n");
    printf("Choose: ");
}

/* ================= CONNECT ================= */
void handle_connect(int sock)
{
    char pass[64], cmd[256], line[BUF_SIZE];

    printf("Enter password: ");
    fgets(pass, sizeof(pass), stdin);
    pass[strcspn(pass, "\n")] = 0;

    snprintf(cmd, sizeof(cmd),
             "CONNECT %s %s\r\n",
             current_device_id, pass);

    send(sock, cmd, strlen(cmd), 0);

    receive_line(sock, line, sizeof(line));
    printf("%s\n", line);

    if (strncmp(line, "200", 3) == 0)
    {
        sscanf(line, "200 %*s %63s", current_token);
        device_connected = 1;
    }
}

/* ================= POWER ================= */
void handle_power(int sock)
{
    char c[8], cmd[128], line[BUF_SIZE];
    printf("1. ON\n2. OFF\nChoose: ");
    fgets(c, sizeof(c), stdin);

    snprintf(cmd, sizeof(cmd),
             "POWER %s %s\r\n",
             current_token,
             (c[0] == '1') ? "ON" : "OFF");

    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);
}

/* ================= TIMER ================= */
void handle_timer(int sock)
{
  
    char minutes[16];
    char action[8];
    char cmd[256];
    char response[BUF_SIZE];
    char choice[8];

  
    printf("Nhap so phut hen gio: ");
    fgets(minutes, sizeof(minutes), stdin);
    minutes[strcspn(minutes, "\n")] = 0;

  
    printf("Hanh dong khi het gio:\n");
    printf("1. Bat (ON)\n");
    printf("2. Tat (OFF)\n");
    printf("Chon: ");
    fgets(choice, sizeof(choice), stdin);

    if (choice[0] == '1')
    {
        strcpy(action, "ON");
    }
    else
    {
        strcpy(action, "OFF");
    }

 
    sprintf(cmd, "TIMER %s %s %s\r\n", current_token, minutes, action);

    send(sock, cmd, strlen(cmd), 0);


    receive_line(sock, response, sizeof(response));

 
    interpret_response(response);
}

void handle_speed(int sock)
{

    char speed[16];
    char cmd[256];
    char response[BUF_SIZE];

    printf("Nhap toc do (0-3): ");
    fgets(speed, sizeof(speed), stdin);
    speed[strcspn(speed, "\n")] = 0;

    sprintf(cmd, "SPEED %s %s\r\n", current_token, speed);
    
    send(sock, cmd, strlen(cmd), 0);
 
    receive_line(sock, response, sizeof(response));
   
    interpret_response(response);
}
/* ================= CHANGE PASSWORD ================= */
void handle_change_password(int sock)
{
    char newpass[64], cmd[256], line[BUF_SIZE];

    printf("Enter new password: ");
    fgets(newpass, sizeof(newpass), stdin);
    newpass[strcspn(newpass, "\n")] = 0;

    snprintf(cmd, sizeof(cmd),
             "PASS %s %s\r\n",
             current_token, newpass);

    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);

 
    current_token[0] = '\0';
    device_connected = 0;
}

/* ================= MAIN ================= */
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server.sin_addr);

    connect(sock, (struct sockaddr *)&server, sizeof(server));

    char greet[BUF_SIZE];
    receive_line(sock, greet, sizeof(greet));
    printf("Server: %s\n", greet);

    while (current_screen != SCREEN_EXIT)
    {
        switch (current_screen)
        {

        case SCREEN_HOME:
            show_home_menu();
            handle_home(sock);
            break;

        case SCREEN_SCAN:
            show_scan(sock);
            handle_scan();
            break;

        case SCREEN_DEVICE:
        {
            show_device_menu();
            char c[8];
            fgets(c, sizeof(c), stdin);

            if (!device_connected)
            {
                if (c[0] == '1')
                    handle_connect(sock);
                else if (c[0] == '0')
                    current_screen = SCREEN_HOME;
            }
            else
            {
                if (c[0] == '2')
                    handle_power(sock);
                else if (c[0] == '3')
                    handle_change_password(sock);
                else if (c[0] == '4')
                    handle_timer(sock);
                else if (c[0] == '5')
                    handle_speed(sock);
                else if (c[0] == '0')
                    current_screen = SCREEN_HOME;
            }
            break;
        }

        default:
            current_screen = SCREEN_HOME;
        }
    }

    close(sock);
    return 0;
}

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
    SCREEN_SHOW_HOME,
    SCREEN_SHOW_ROOM,
    SCREEN_SCAN,
    SCREEN_DEVICE,
    SCREEN_EXIT
} Screen;

Screen current_screen = SCREEN_HOME;
Screen previous_screen = SCREEN_HOME; // chi dung cho back tu device ve show room
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
    else if (strcmp(code, "231") == 0)
        printf("→ FULL \n");
    else if (strcmp(code, "300") == 0)
        printf("→ UNKNOWN COMMAND\n");
    else if (strcmp(code, "500") == 0)
        printf("→ BAD_REQUEST\n");
    else if (strcmp(code, "400") == 0)
        printf("→ INVALID_VALUE\n");
    else if (strcmp(code, "401") == 0)
        printf("→ INVALID TOKEN\n");
    else if (strcmp(code, "221") == 0)
        printf("→ ALREADY SET/CANCEL\n");
    else if (strcmp(code, "222") == 0)
        printf("→ ALREADY EXISTS\n");
    else if (strcmp(code, "405") == 0)
        printf("→ DEVICE NOT SUPPORT SPEED\n");
    else if (strcmp(code, "406") == 0)
        printf("→ DEVICE NOT SUPPORT TEMPERATURE\n");
    else if (strcmp(code, "407") == 0)
        printf("→ DEVICE NOT SUPPORT MODE\n");
    else if (strcmp(code, "502") == 0)
        printf("→ DEVICE IS OFF\n");
    else if (strcmp(code, "404") == 0)
        printf("→ NOT FOUND\n");
}

/* ================= RECEIVE LINE (CRLF SAFE) ================= */
static char message[8192];
static int message_len = 0;

int receive_line(int sock, char *buffer, size_t size)
{
    char temp_buff[BUF_SIZE];

    while (1)
    {
        // 1. Kiểm tra xem trong 'message' hiện tại đã có dòng nào chưa (\r\n)
        char *pos = strstr(message, "\r\n");
        if (pos != NULL)
        {
            int line_len = (int)(pos - message);

            // Chỉ copy tối đa 'size - 1' để tránh tràn bộ đệm 'buffer'
            int copy_len = (line_len < (int)size - 1) ? line_len : (int)size - 1;

            memcpy(buffer, message, copy_len);
            buffer[copy_len] = '\0';

            // Dịch chuyển phần dư lên đầu
            int consumed = line_len + 2; // +2 là cho \r\n
            int remaining = message_len - consumed;

            if (remaining > 0)
            {
                memmove(message, message + consumed, remaining);
            }
            message_len = remaining;
            message[message_len] = '\0'; // Luôn kết thúc chuỗi để strstr chạy đúng lần sau

            return 1;
        }

        // 2. Nhận thêm một khối dữ liệu lớn
        int n = recv(sock, temp_buff, sizeof(temp_buff) - 1, 0);
        if (n <= 0)
            return 0;

        // 3. Nối vào bộ đệm tích lũy bằng memcpy (an toàn và nhanh hơn strcat)
        if (message_len + n < (int)sizeof(message) - 1)
        {
            memcpy(message + message_len, temp_buff, n);
            message_len += n;
            message[message_len] = '\0'; // Cực kỳ quan trọng để strstr tìm thấy điểm dừng
        }
        else
        {
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
                       id, type, name, token) == 4)
            {

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

void receive_scan_until_end3(int sock) // dung khi trong enter show home
{
    char line[BUF_SIZE];
    int in_my = 0;
    scan_token_count = 0; // reset mỗi lần scan
    while (1)
    {
        if (!receive_line(sock, line, sizeof(line)))
            return;
        if (strcmp(line, "MY DEVICES") == 0)
        {
            in_my = 1;
            continue;
        }
        if (strcmp(line, "NEW DEVICES") == 0)
        {
            in_my = 0;
            continue;
        }
        if (strcmp(line, "END") == 0)
        {
            break;
        }
        if (in_my)
        {
            char id[64], type[32], name[128], token[64];
            if (sscanf(line,
                       "%63s || %31s || %127s || %63s",
                       id, type, name, token) == 4)
            {
                // lưu token
                strcpy(scan_tokens[scan_token_count].id, id);
                strcpy(scan_tokens[scan_token_count].token, token);
                scan_token_count++;
            }
        }
    }
    return;
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
void handle_add_device(int sock, char *roomId)
{
    char cmd[256];
    char line[BUF_SIZE];
    char device_id[64];

    printf("Enter Device ID to add (or 0 to back): ");
    fgets(device_id, sizeof(device_id), stdin);
    device_id[strcspn(device_id, "\n")] = 0;
    if (device_id[0] == '\0' || strcmp(device_id, "0") == 0)
    {
        current_screen = SCREEN_SHOW_HOME;
        return;
    }
    snprintf(cmd, sizeof(cmd), "ADD DEVICE %s %s\r\n", device_id, roomId);
    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);
}
void handle_delete_device(int sock)
{
    char cmd[256];
    char line[BUF_SIZE];

    snprintf(cmd, sizeof(cmd), "DELETE DEVICE %s\r\n", current_device_id);
    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);

    // sau khi xoa ve show room
    current_screen = SCREEN_SHOW_ROOM;
}
void handle_select_home_name(int sock)
{
    char homeName[128];
    char cmd[256];
    char line[BUF_SIZE];

    printf("Enter Home name to select (no spaces, use _): ");
    fgets(homeName, sizeof(homeName), stdin);
    homeName[strcspn(homeName, "\n")] = 0;

    if (homeName[0] == '\0')
    {
        printf("Invalid value!\n");
        return;
    }

    snprintf(cmd, sizeof(cmd), "SET HOME %s\r\n", homeName);
    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);
}

void handle_add_home(int sock)
{
    char homeName[128];
    char cmd[256];
    char line[BUF_SIZE];

    printf("Enter new Home name (no spaces, use _): ");
    fgets(homeName, sizeof(homeName), stdin);
    homeName[strcspn(homeName, "\n")] = 0;

    if (homeName[0] == '\0')
    {
        printf("Invalid value!\n");
        return;
    }

    snprintf(cmd, sizeof(cmd), "ADD HOME %s\r\n", homeName);
    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);
}

void handle_add_room(int sock)
{
    char roomId[64];
    char roomName[128];
    char cmd[256];
    char line[BUF_SIZE];

    printf("Enter Room ID (no spaces, e.g. ROOM_02): ");
    fgets(roomId, sizeof(roomId), stdin);
    roomId[strcspn(roomId, "\n")] = 0;

    printf("Enter Room Name (no spaces, use _): ");
    fgets(roomName, sizeof(roomName), stdin);
    roomName[strcspn(roomName, "\n")] = 0;

    if (roomId[0] == '\0' || roomName[0] == '\0')
    {
        printf("Invalid value!\n");
        return;
    }

    snprintf(cmd, sizeof(cmd), "ADD ROOM %s %s\r\n", roomId, roomName);
    send(sock, cmd, strlen(cmd), 0);
    receive_line(sock, line, sizeof(line));
    interpret_response(line);
}
void handle_init_device(int sock)
{
    char id[64], pass[64], type[32];
    char cmd[256], response[BUF_SIZE];

    printf("\n----- Initialize New Device -----\n");

    printf("Enter Device ID (e.g., L001, F002): ");
    fgets(id, sizeof(id), stdin);
    id[strcspn(id, "\n")] = 0;

    printf("Set Password: ");
    fgets(pass, sizeof(pass), stdin);
    pass[strcspn(pass, "\n")] = 0;

    printf("Enter Device Type (LIGHT / FAN / AC): ");
    fgets(type, sizeof(type), stdin);
    type[strcspn(type, "\n")] = 0;

    // Tạo lệnh theo protocol: INIT <ID> <PW> <TYPE>\r\n
    snprintf(cmd, sizeof(cmd), "INIT %s %s %s\r\n", id, pass, type);

    // Gửi đến Server
    send(sock, cmd, strlen(cmd), 0);

    // Nhận phản hồi dùng hàm receive_line (buffer-based) đã sửa ở trên
    if (receive_line(sock, response, sizeof(response)))
    {
        interpret_response(response); // In ra "SUCCESS" hoặc lỗi dựa trên mã code
    }
}

/* ================= HOME ================= */
void show_home_menu()
{
    printf("\n========== HOME ==========\n");
    printf("1. Show Home\n");
    // printf("2. Show Room\n");
    printf("2. Scan Devices\n");
    printf("3. Initialize Devices\n");
    printf("0. Exit\n");
    printf("==========================\n");
    printf("Choose: ");
}
// khi vao SHOW ROOM
void enter_show_room(int sock)
{
    char device_id[64];

    printf("\n====== MANAGE DEVICE ======\n");
    printf("Enter Device ID (or 0 to Back): ");
    fgets(device_id, sizeof(device_id), stdin);
    device_id[strcspn(device_id, "\n")] = 0;

    if (strcmp(device_id, "0") == 0)
    {
        current_screen = SCREEN_SHOW_HOME;
        return;
    }

    char *tk = find_scan_token(device_id);
    if (!tk)
    {
        printf("Device not found or not connected!\n");
        current_screen = SCREEN_SHOW_HOME;
        return;
    }

    // thiet bi hop le
    strcpy(current_token, tk);
    strcpy(current_device_id, device_id);
    device_connected = 1;

    previous_screen = SCREEN_SHOW_ROOM;
    current_screen = SCREEN_DEVICE;
}

// khi vao SHOW HOME
void enter_show_home(int sock)
{
    char choice[8];

    // Scan device truoc
    send(sock, "SCAN\r\n", 6, 0);
    receive_scan_until_end3(sock);

    printf("\n====== HOME ======\n");
    printf("0. Back\n");
    printf("1. Show Room\n");
    printf("2. Select Home\n");
    printf("3. Add Room (to current home)\n");
    printf("4. Add Home\n");
    printf("Choose: ");
    fgets(choice, sizeof(choice), stdin);

    if (choice[0] == '0')
    {
        current_screen = SCREEN_HOME;
        return;
    }
    if (choice[0] == '2')
    {
        handle_select_home_name(sock);
        send(sock, "SHOW HOME\r\n", 11, 0);
        receive_scan_until_end(sock);
        current_screen = SCREEN_SHOW_HOME;
        return;
    }
    if (choice[0] == '3')
    {
        handle_add_room(sock);
        send(sock, "SHOW HOME\r\n", 11, 0);
        receive_scan_until_end(sock);
        current_screen = SCREEN_SHOW_HOME;
        return;
    }
    if (choice[0] == '4')
    {
        handle_add_home(sock);
        send(sock, "SHOW HOME\r\n", 11, 0);
        receive_scan_until_end(sock);
        current_screen = SCREEN_SHOW_HOME;
        return;
    }
    if (choice[0] != '1')
    {
        printf("Invalid choice!\n");
        current_screen = SCREEN_SHOW_HOME;
        return;
    }

    // neu chon 1
    char roomId[64], cmd[128];
    printf("\nEnter Room ID (0 to back): ");
    fgets(roomId, sizeof(roomId), stdin);
    roomId[strcspn(roomId, "\n")] = 0;

    snprintf(cmd, sizeof(cmd), "SHOW ROOM %s\r\n", roomId);
    if(roomId[0] == '\0'|| strcmp(roomId, "0") == 0){
        current_screen = SCREEN_SHOW_HOME;
        return;
    }
    send(sock, cmd, strlen(cmd), 0);
    receive_scan_until_end(sock);

    printf("\n====== ROOM ======\n");
    printf("0. Back\n");
    printf("1. Manage Device\n");
    printf("2. Add Device\n");
    printf("Choose: ");
    fgets(choice, sizeof(choice), stdin);

    if (choice[0] == '0')
    {
        current_screen = SCREEN_SHOW_HOME;
    }
    else if (choice[0] == '1')
    {
        current_screen = SCREEN_SHOW_ROOM;
        enter_show_room(sock);
    }
    else if (choice[0] == '2')
    {
        handle_add_device(sock, roomId);
    }
    else
    {
        printf("Invalid choice!\n");
        current_screen = SCREEN_SHOW_HOME;
    }
}

void handle_home(int sock)
{
    char c[8];
    fgets(c, sizeof(c), stdin);

    if (c[0] == '1')
    {
        printf("\n========== SHOW HOME ==========\n");
        send(sock, "SHOW HOME\r\n", 11, 0);
        receive_scan_until_end(sock);
        enter_show_home(sock);
    }
    // else if (c[0] == '2')
    // {
    //     char room[64], cmd[128];
    //     printf("Enter Room ID: ");
    //     fgets(room, sizeof(room), stdin);
    //     room[strcspn(room, "\n")] = 0;

    //     snprintf(cmd, sizeof(cmd),
    //              "SHOW ROOM %s\r\n", room);
    //     send(sock, cmd, strlen(cmd), 0);
    //     receive_scan_until_end(sock);
    // }
    else if (c[0] == '2')
    {
        current_screen = SCREEN_SCAN;
    }
    else if (c[0] == '3')
    {
        handle_init_device(sock);
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
        printf("6. Temperature\n");
        printf("7. Mode(Only on AC)\n");
        printf("8. Power Usage\n");
        printf("9. Show information\n");
        printf("a. Delete Device\n");
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
    interpret_response(line);

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
// ================= SPEED ================= */
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
/* ================= TEMPERATURE ================= */
void handle_temperature(int sock)
{
    char temperature[16];
    char cmd[256];
    char response[BUF_SIZE];

    printf("Nhap nhiet do mong muon: ");
    fgets(temperature, sizeof(temperature), stdin);
    temperature[strcspn(temperature, "\n")] = 0;

    sprintf(cmd, "TEMPERATURE %s %s\r\n", current_token, temperature);

    send(sock, cmd, strlen(cmd), 0);

    receive_line(sock, response, sizeof(response));

    interpret_response(response);
}
/* ================= MODE ================= */
void handle_mode(int sock)
{
    char mode[16];
    char cmd[256];
    char response[BUF_SIZE];
    char choice[8];
    printf("Nhap che do: ");
    printf("1. COOL❄️\n");
    printf("2. FAN🌀\n");
    printf("3. DRY💧\n");
    printf("Chon: ");
    fgets(choice, sizeof(choice), stdin);
    if (choice[0] == '1')
    {
        strcpy(mode, "COOL");
    }
    else if (choice[0] == '2')
    {
        strcpy(mode, "FAN");
    }
    else if (choice[0] == '3')
    {
        strcpy(mode, "DRY");
    }
    else
    {
        printf("Invalid choice.\n");
        return;
    }

    sprintf(cmd, "MODE %s %s\r\n", current_token, mode);

    send(sock, cmd, strlen(cmd), 0);

    receive_line(sock, response, sizeof(response));

    interpret_response(response);
}
/* ================= POWER USAGE ================= */
void handle_power_usage(int sock)
{
    char cmd[256];
    char response[BUF_SIZE];

    sprintf(cmd, "POWER USAGE %s\r\n", current_token);

    send(sock, cmd, strlen(cmd), 0);

    receive_scan_until_end(sock);
}
/* ================= SHOW INFORMATION ================= */
void handle_show_information(int sock)
{
    char cmd[256];
    char response[BUF_SIZE];

    sprintf(cmd, "SHOW INFO %s\r\n", current_token);

    send(sock, cmd, strlen(cmd), 0);

    receive_scan_until_end(sock);
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
    if (inet_pton(AF_INET, argv[1], &server.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server IP: %s\n", argv[1]);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) != 0)
    {
        perror("connect");
        fprintf(stderr, "Failed to connect to %s:%s (check server is running and port is open)\n", argv[1], argv[2]);
        close(sock);
        return 1;
    }

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

        case SCREEN_SHOW_HOME:
            enter_show_home(sock);
            break;

        case SCREEN_SHOW_ROOM:
            enter_show_room(sock);
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
                {
                    if (previous_screen == SCREEN_SHOW_ROOM)
                        current_screen = SCREEN_SHOW_ROOM;
                    else
                        current_screen = SCREEN_HOME;
                }
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
                else if (c[0] == '6')
                    handle_temperature(sock);
                else if (c[0] == '7')
                    handle_mode(sock);
                else if (c[0] == '8')
                    handle_power_usage(sock);
                else if (c[0] == '9')
                    handle_show_information(sock);
                else if (c[0] == 'a')
                    handle_delete_device(sock);
                else if (c[0] == '0')
                {
                    if (previous_screen == SCREEN_SHOW_ROOM)
                        current_screen = SCREEN_SHOW_ROOM;
                    else
                        current_screen = SCREEN_HOME;
                }
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

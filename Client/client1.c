#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cJSON.h>

#define BUF_SIZE 1024

void interpret_response(const char *code) {
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
        printf("400. INVALID_VALUE.\n");
    else if (strcmp(code, "401") == 0)
        printf("401. INVALID TOKEN.\n");
    else if (strcmp(code, "221") == 0)
        printf("221. ALREADY SET/CANCEL.\n");
}


static char recvbuf[4096];     // buffer nhận tạm - STATIC để giữ dữ liệu giữa các lần gọi
static int recv_len = 0;       // số byte hiện có trong recvbuf - STATIC

int receive_line(int sock, char *buffer, size_t size) {
    int i;

    while (1) {

        // 1) Tìm chuỗi '\r\n' (CRLF) trong recvbuf
        // Lặp đến recv_len - 1 vì cần 2 byte để tìm '\r' và '\n'
        for (i = 0; i < recv_len - 1; i++) { 
            // Điều kiện tìm thấy CRLF
            if (recvbuf[i] == '\r' && recvbuf[i+1] == '\n') {

                // Độ dài của dòng là từ đầu đến '\r' (không bao gồm '\r\n')
                int linelen = i; 
                int total_extracted_len = linelen + 2; // Độ dài dòng + \r\n

                // Nếu dòng (không kể \r\n) dài quá size → cắt
                if (linelen >= size) {
                    linelen = size - 1; 
                }
                
                // Copy ra buffer (kết quả) (chỉ copy đến trước \r\n)
                memcpy(buffer, recvbuf, linelen);
                buffer[linelen] = '\0'; // Kết thúc chuỗi

                // dời phần còn lại lên đầu recvbuf
                // Bắt đầu dời từ sau '\n' (tức là recvbuf + i + 2)
                memmove(recvbuf, recvbuf + total_extracted_len, recv_len - total_extracted_len);
                recv_len -= total_extracted_len;

                return linelen;// trả về số byte của dòng
            }
        }

        // 2) Nếu chưa thấy '\r\n', nhận thêm dữ liệu
        int n = recv(sock, recvbuf + recv_len, sizeof(recvbuf) - recv_len, 0);
        if (n <= 0)
            return 0;// mất kết nối hoặc lỗi

        recv_len += n;

        // 3) Nếu recvbuf đầy mà vẫn không có '\r\n' (xử lý dòng quá dài hoặc không có dấu phân cách)
        // Trong giao thức CRLF, lỗi này ít phổ biến hơn và thường được coi là lỗi giao thức.
        if (recv_len >= sizeof(recvbuf)) {
            // Xử lý bằng cách cắt một phần (size - 1) để trả về
            int linelen = size - 1;
            memcpy(buffer, recvbuf, linelen);
            buffer[linelen] = '\0';
            
            // Dời phần còn lại (nếu có) lên đầu.
            // Nếu linelen < recv_len, vẫn còn dữ liệu
            int remaining = recv_len - linelen;
            memmove(recvbuf, recvbuf + linelen, remaining);
            recv_len = remaining;
            
            return linelen;
        }
    }
}


// ============================ SCAN MULTI-LINE ============================
int receive_scan_until_end(int sock) {
    char buffer[BUF_SIZE];

    while (1) {
        if (!receive_line(sock, buffer, sizeof(buffer)))
            return 0;

        printf("%s\n", buffer);

        if (strcmp(buffer, "END") == 0)
            break;
    }

    return 1;
}


// ============================ PRINT MENU ============================
void print_menu() {
    printf("\n==================================\n");
    printf("         SMART HOME CLIENT        \n");
    printf("==================================\n");
    printf("Available commands:\n");
    printf("  SCAN\n");
    printf("     → Scan all devices\n\n");

    printf("  CONNECT <deviceId> <password>\n");
    printf("     → Connect to a device\n\n");

    printf("  CHANGE_PASS <deviceId> <oldPass> <newPass>\n");
    printf("     → Change device password\n\n");

    printf("  POWER <token> <ON|OFF>\n");
    printf("     → Control device power\n\n");

    printf("  TIMER <token> <minutes> <ON|OFF>\n");
    printf("     → Set device timer\n\n");

    printf("==================================\n");
}

int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    int sock;
    struct sockaddr_in server;
    char buffer[BUF_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket failed");
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server.sin_addr);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect failed");
        return 1;
    }

    printf(" Connected to server %s:%s\n", argv[1], argv[2]);

    // Greeting
    if (receive_line(sock, buffer, sizeof(buffer)))
        printf(" Server: %s\n", buffer);

    // ================= MAIN LOOP =================
    while (1) {

        print_menu();
        printf(" Enter command: ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin))
            break;

        buffer[strcspn(buffer, "\n")] = '\0';

        send(sock, buffer, strlen(buffer), 0);
        send(sock, "\r\n", 2, 0);

        if (strcmp(buffer, "SCAN") == 0) {
            receive_scan_until_end(sock);
            continue;
        }
        if ((strncmp(buffer, "CONNECT", 7)) == 0  ){
            if (!receive_line(sock, buffer, sizeof(buffer))) {
                printf("Connection closed by server.\n");
                break;
            }
            printf("%s\n", buffer);
            continue;
        }
        if ((strncmp(buffer, "SHOW HOME", 9)) == 0  ||
            (strncmp(buffer, "SHOW ROOM", 9)) == 0 ) {
            receive_scan_until_end(sock);
            continue;
        }
        if (receive_line(sock, buffer, sizeof(buffer))) {
            interpret_response(buffer);
        } else {
            printf("Connection closed by server.\n");
            break;
        }
    }

    close(sock);
    return 0;
}

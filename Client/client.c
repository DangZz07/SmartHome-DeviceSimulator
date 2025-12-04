#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 2048
void interpret_response(const char *code) {
    if (strcmp(code, "100") == 0) 
        printf("→ CONNECTED TO SERVER\n");
    else if (strcmp(code, "200") == 0) 
        printf("→ CONNECT OK\n");
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
}
// ============================ Read EXACT 1 LINE ============================
int receive_line(int sock, char *buffer, size_t size) {
    size_t idx = 0;
    char c;

    while (idx < size - 1) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return 0;

        if (c == '\n') break;   // end of line
        if (c == '\r') continue; // skip CR

        buffer[idx++] = c;
    }

    buffer[idx] = '\0';
    return 1;
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

        if (strcasecmp(buffer, "SCAN") == 0) {
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

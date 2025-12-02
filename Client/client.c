#include <stdio.h>


#include <stdio.h>






void interpret_response(const char *code) {
    if (strcmp(code, "100") == 0)
        printf("100. Connected to server successfully.\n");
    else if (strcmp(code, "110") == 0)
        printf("110. Login successful! You are now logged in.\n");
    else if (strcmp(code, "120") == 0)
        printf("120. Post successful!\n");
    else if (strcmp(code, "130") == 0)
        printf("130. Logout successful. Session ended.\n");
    else if (strcmp(code, "211") == 0)
        printf("211. Account is locked. Please contact administrator.\n");
    else if (strcmp(code, "212") == 0)
        printf("212.  Account does not exist.\n");
    else if (strcmp(code, "213") == 0)
        printf("213.  You are already logged in on this client.\n");
    else if (strcmp(code, "214") == 0)
        printf("214. This account is already logged in on another client.\n");
    else if (strcmp(code, "221") == 0)
        printf("221. You must log in before using this command.\n");
    else if (strcmp(code, "300") == 0)
        printf("300. Unknown command or server error.\n");
    else
        printf("Server: %s\n", code);
}

/**
 * @brief Receive a message from the server and print it nicely.
 *
 * @param sock The socket file descriptor connected to the server.
 * @return int 0 if connection closed, 1 otherwise
 */
int receive_message(int sock) {
    char buffer[BUFF_SIZE];
    int len = recv(sock, buffer, BUFF_SIZE - 1, 0);

    if (len <= 0)
        return 0;

    buffer[len] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';

    interpret_response(buffer);

    // End session if BYE (130)
    if (strcmp(buffer, "130") == 0)
        return 0;

    return 1;
}
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Server_IP> <Port>\n", argv[0]);
        exit(1);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int client_sock;
    struct sockaddr_in server_addr;

    /* Create TCP socket */
    if ((client_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror(" Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror(" Invalid address");
        close(client_sock);
        exit(1);
    }

    /* Connect to server */
    if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror(" Connection failed");
        close(client_sock);
        exit(1);
    }

    printf(" Connected to server %s:%d\n", server_ip, port);

    /* Receive initial greeting */
    if (!receive_message(client_sock)) {
        close(client_sock);
        return 0;
    }

    char buff[BUFF_SIZE];

    while (1) {
        show_menu();
        printf(" Enter command: ");
        fgets(buff, BUFF_SIZE, stdin);
        buff[strcspn(buff, "\n")] = '\0';  // Remove newline

        if (strlen(buff) == 0)
            continue;

        /* Prepare message with CRLF */
        char msg[BUFF_SIZE];
        snprintf(msg, sizeof(msg), "%.*s\r\n", (int)(sizeof(msg) - 3), buff);

        /* Send to server */
        if (send(client_sock, msg, strlen(msg), 0) <= 0) {
            printf("  Failed to send data. Connection may be closed.\n");
            break;
        }

        /* Receive and interpret response */
        if (!receive_message(client_sock))
            break;  // Session ended (BYE or disconnect)
    }

    printf(" Connection closed.\n");
    close(client_sock);
    return 0;
}
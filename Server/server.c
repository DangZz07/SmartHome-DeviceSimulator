
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>




void trim_end(char *s) {
    int i = (int)strlen(s) - 1;
    while (i >= 0 && (s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t')) {
        s[i] = '\0';
        i--;
    }
}

/* Safely receive one line (ending with '\n') */
ssize_t recv_line(int sock, char *buf, size_t maxlen) {
    size_t n = 0;
    char c;
    ssize_t r;

    while (n + 1 < maxlen) {
        r = recv(sock, &c, 1, 0);
        if (r == 1) {
            buf[n++] = c;
            if (c == '\n') break;
        } else if (r == 0) {
            if (n == 0) return 0;
            break;
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }
    buf[n] = '\0';
    return (ssize_t)n;
}

/* Send a reply code to the client */
void send_reply(int sock, const char *code) {
    char out[64];
    snprintf(out, sizeof(out), "%s\r\n", code);
    send(sock, out, strlen(out), 0);
}

void *client_thread(void *arg) {
    int sockfd = *(int *)arg;
    free(arg);
    pthread_detach(pthread_self());

    char line[BUF_SIZE];
    bool logged_in = false;
    Account current = {"", 0};

    send_reply(sockfd, "100");  /* Greeting */

    while (1) {
        ssize_t r = recv_line(sockfd, line, sizeof(line));
        if (r == 0) {
            printf("[thread %lu] client disconnected.\n", (unsigned long)pthread_self());
            break;
        } else if (r < 0) {
            perror("[thread] recv_line");
            break;
        }

        trim_end(line);
        if (line[0] == '\0') continue;


        /* ---- Unknown command ---- */
        else {
            send_reply(sockfd, "300"); /* unknown command */
        }
    }

    if (logged_in) {
        remove_active_user(current.username);
    }

    close(sockfd);
    printf("[thread %lu] connection closed.\n", (unsigned long)pthread_self());
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc == 2) port = atoi(argv[1]);

    int listenfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size = sizeof(client_addr);
    pthread_t tid;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) == -1) {
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d.\n", port);

    while (1) {
        int *connfd = malloc(sizeof(int));
        if (!connfd) continue;

        *connfd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size);
        if (*connfd == -1) {
            perror("accept error");
            free(connfd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        printf("New connection from %s:%d\n", client_ip, client_port);

        pthread_create(&tid, NULL, client_thread, connfd);
    }

    close(listenfd);
    return 0;
}
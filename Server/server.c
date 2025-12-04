
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
#include "cJSON.h"

#define PORT 5550
#define BACKLOG 20
#define BUF_SIZE 1024


typedef struct {
    double day;
    double month;
    double year;
} UsageHistory;

typedef struct {
    int currentWatt;
    UsageHistory usageHistory;
} PowerUsage;

typedef struct {
    char time[64];
    char action[128];
} LogEntry;

typedef struct {
    char password[64];
    int connected;
    char token[64];
} AuthInfo;

typedef struct {
    char power[16];
    int timer;
    int speed;
    char mode[32];
    int temperature;
} DeviceState;

typedef struct {
    char deviceId[64];
    char type[32];
    char name[128];

    AuthInfo auth;
    DeviceState state;
    PowerUsage powerUsage;

    LogEntry logs[64];
    int logCount;
} Device;

typedef struct {
    char roomId[64];
    char roomName[128];

    char deviceIds[64][64];
    int deviceCount;
} Room;

typedef struct {
    char homeName[128];
    Room rooms[32];
    int roomCount;

    Device devices[128];
    int deviceCount;
} Database;

typedef struct {
    int sockfd;
    Database *db;
} ThreadArgs;


int load_database(Database *db)
{
    FILE *f = fopen("DB.json", "r");
    if (!f) {
        printf("Cannot open DB.json\n");
        return 0;
    }

    // đọc toàn file
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);

    // parse JSON
    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        printf("JSON Error: %s\n", cJSON_GetErrorPtr());
        free(buffer);
        return 0;
    }

    // -------- HOME --------
    cJSON *home = cJSON_GetObjectItem(root, "home");
    strcpy(db->homeName, cJSON_GetObjectItem(home, "homeName")->valuestring);

    // -------- ROOMS --------
    cJSON *rooms = cJSON_GetObjectItem(home, "rooms");
    db->roomCount = cJSON_GetArraySize(rooms);

    for (int i = 0; i < db->roomCount; i++) {
        cJSON *room = cJSON_GetArrayItem(rooms, i);

        Room *r = &db->rooms[i];
        strcpy(r->roomId, cJSON_GetObjectItem(room, "roomId")->valuestring);
        strcpy(r->roomName, cJSON_GetObjectItem(room, "roomName")->valuestring);

        cJSON *dList = cJSON_GetObjectItem(room, "devices");
        r->deviceCount = cJSON_GetArraySize(dList);

        for (int j = 0; j < r->deviceCount; j++) {
            strcpy(r->deviceIds[j], cJSON_GetArrayItem(dList, j)->valuestring);
        }
    }

    // -------- DEVICES --------
    cJSON *devArr = cJSON_GetObjectItem(root, "devices");
    db->deviceCount = cJSON_GetArraySize(devArr);

    for (int i = 0; i < db->deviceCount; i++) {
        cJSON *d = cJSON_GetArrayItem(devArr, i);
        Device *dev = &db->devices[i];

        strcpy(dev->deviceId, cJSON_GetObjectItem(d, "deviceId")->valuestring);
        strcpy(dev->type,     cJSON_GetObjectItem(d, "type")->valuestring);
        strcpy(dev->name,     cJSON_GetObjectItem(d, "name")->valuestring);

        // AUTH
        cJSON *auth = cJSON_GetObjectItem(d, "auth");
        strcpy(dev->auth.password, cJSON_GetObjectItem(auth, "password")->valuestring);
        dev->auth.connected = cJSON_IsTrue(cJSON_GetObjectItem(auth, "connected"));
        cJSON *tk = cJSON_GetObjectItem(auth, "Token");
        strcpy(dev->auth.token, tk && tk->valuestring ? tk->valuestring : "");

        // STATE
        cJSON *st = cJSON_GetObjectItem(d, "state");
        strcpy(dev->state.power, cJSON_GetObjectItem(st, "power")->valuestring);

        dev->state.timer = cJSON_GetObjectItem(st, "timer")->valueint;

        cJSON *sp = cJSON_GetObjectItem(st, "speed");
        dev->state.speed = cJSON_IsNumber(sp) ? sp->valueint : -1;

        cJSON *md = cJSON_GetObjectItem(st, "mode");
        strcpy(dev->state.mode, (md && md->valuestring) ? md->valuestring : "");

        cJSON *tmp = cJSON_GetObjectItem(st, "temperature");
        dev->state.temperature = cJSON_IsNumber(tmp) ? tmp->valueint : -1;

        // POWER USAGE
        cJSON *pu = cJSON_GetObjectItem(d, "powerUsage");
        dev->powerUsage.currentWatt = cJSON_GetObjectItem(pu, "currentWatt")->valueint;

        cJSON *hist = cJSON_GetObjectItem(pu, "usageHistory");
        dev->powerUsage.usageHistory.day   = cJSON_GetObjectItem(hist, "day")->valuedouble;
        dev->powerUsage.usageHistory.month = cJSON_GetObjectItem(hist, "month")->valuedouble;
        dev->powerUsage.usageHistory.year  = cJSON_GetObjectItem(hist, "year")->valuedouble;

        // LOGS
        cJSON *logs = cJSON_GetObjectItem(d, "logs");
        dev->logCount = cJSON_GetArraySize(logs);

        for (int j = 0; j < dev->logCount; j++) {
            cJSON *lg = cJSON_GetArrayItem(logs, j);
            strcpy(dev->logs[j].time,   cJSON_GetObjectItem(lg, "time")->valuestring);
            strcpy(dev->logs[j].action, cJSON_GetObjectItem(lg, "action")->valuestring);
        }
    }

    // cleanup
    cJSON_Delete(root);
    free(buffer);

    return 1;
}

int save_database(Database *db)
{
    cJSON *root = cJSON_CreateObject();

    // HOME
    cJSON *home = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "home", home);
    cJSON_AddStringToObject(home, "homeName", db->homeName);

    cJSON *rooms = cJSON_CreateArray();
    cJSON_AddItemToObject(home, "rooms", rooms);

    for (int i = 0; i < db->roomCount; i++) {
        Room *r = &db->rooms[i];
        cJSON *room = cJSON_CreateObject();
        cJSON_AddItemToArray(rooms, room);

        cJSON_AddStringToObject(room, "roomId", r->roomId);
        cJSON_AddStringToObject(room, "roomName", r->roomName);

        cJSON *dArr = cJSON_CreateArray();
        cJSON_AddItemToObject(room, "devices", dArr);
        for (int j = 0; j < r->deviceCount; j++) {
            cJSON_AddItemToArray(dArr, cJSON_CreateString(r->deviceIds[j]));
        }
    }

    // DEVICES
    cJSON *devices = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devices);

    for (int i = 0; i < db->deviceCount; i++) {

        Device *d = &db->devices[i];
        cJSON *dv = cJSON_CreateObject();
        cJSON_AddItemToArray(devices, dv);

        cJSON_AddStringToObject(dv, "deviceId", d->deviceId);
        cJSON_AddStringToObject(dv, "type", d->type);
        cJSON_AddStringToObject(dv, "name", d->name);

        // AUTH
        cJSON *auth = cJSON_CreateObject();
        cJSON_AddItemToObject(dv, "auth", auth);
        cJSON_AddStringToObject(auth, "password", d->auth.password);
        cJSON_AddBoolToObject(auth, "connected", d->auth.connected);
        cJSON_AddStringToObject(auth, "Token", d->auth.token);

        // STATE
        cJSON *st = cJSON_CreateObject();
        cJSON_AddItemToObject(dv, "state", st);
        cJSON_AddStringToObject(st, "power", d->state.power);
        cJSON_AddNumberToObject(st, "timer", d->state.timer);
        if (d->state.speed != -1)
            cJSON_AddNumberToObject(st, "speed", d->state.speed);
        else
            cJSON_AddNullToObject(st, "speed");

        if (strlen(d->state.mode) > 0)
            cJSON_AddStringToObject(st, "mode", d->state.mode);
        else
            cJSON_AddNullToObject(st, "mode");

        if (d->state.temperature != -1)
            cJSON_AddNumberToObject(st, "temperature", d->state.temperature);
        else
            cJSON_AddNullToObject(st, "temperature");

        // POWER USAGE
        cJSON *pu = cJSON_CreateObject();
        cJSON_AddItemToObject(dv, "powerUsage", pu);
        cJSON_AddNumberToObject(pu, "currentWatt", d->powerUsage.currentWatt);

        cJSON *hist = cJSON_CreateObject();
        cJSON_AddItemToObject(pu, "usageHistory", hist);
        cJSON_AddNumberToObject(hist, "day", d->powerUsage.usageHistory.day);
        cJSON_AddNumberToObject(hist, "month", d->powerUsage.usageHistory.month);
        cJSON_AddNumberToObject(hist, "year", d->powerUsage.usageHistory.year);

        // LOGS
        cJSON *logs = cJSON_CreateArray();
        cJSON_AddItemToObject(dv, "logs", logs);

        for (int j = 0; j < d->logCount; j++) {
            cJSON *lg = cJSON_CreateObject();
            cJSON_AddItemToArray(logs, lg);

            cJSON_AddStringToObject(lg, "time", d->logs[j].time);
            cJSON_AddStringToObject(lg, "action", d->logs[j].action);
        }
    }

    char *jsonStr = cJSON_Print(root);

    FILE *f = fopen("DB.json", "w");
    if (!f) {
        free(jsonStr);
        cJSON_Delete(root);
        return 0;
    }

    fwrite(jsonStr, 1, strlen(jsonStr), f);
    fclose(f);

    free(jsonStr);
    cJSON_Delete(root);

    return 1;
}


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
void handle_scan_struct(int sock, Database *db)
{
    char out[256];

    send(sock, "MY DEVICES\r\n", 12, 0);
    for (int i = 0; i < db->deviceCount; i++) {
        if (db->devices[i].auth.connected) {
            snprintf(out, sizeof(out), "%s\r\n", db->devices[i].deviceId);
            send(sock, out, strlen(out), 0);
        }
    }

    send(sock, "NEW DEVICES\r\n", 13, 0);
    for (int i = 0; i < db->deviceCount; i++) {
        if (!db->devices[i].auth.connected) {
            snprintf(out, sizeof(out), "%s\r\n", db->devices[i].deviceId);
            send(sock, out, strlen(out), 0);
        }
    }

    send(sock, "END\r\n", 5, 0);
}


Device* find_device(Database *db, const char *id)
{
    for (int i = 0; i < db->deviceCount; i++) {
        if (strcmp(db->devices[i].deviceId, id) == 0)
            return &db->devices[i];
    }
    return NULL;
}

void *client_thread(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    int sockfd = args->sockfd;
    Database *db = args->db;
    free(args);

    pthread_detach(pthread_self());

    char line[BUF_SIZE];
    char id[64], pass[64];

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

        else if (strcmp(line, "SCAN") == 0) {
            handle_scan_struct(sockfd, db);
        }
        else if (strncmp(line, "CONNECT ", 8) == 0) {


        if (sscanf(line + 8, "%63s %63s", id, pass) != 2) {
            send_reply(sockfd, "ERROR Invalid format");
            continue;
        }

        Device *d = find_device(db, id);
        if (!d) {
            send_reply(sockfd, "ERROR Not found");
            continue;
        }

        if (strcmp(d->auth.password, pass) != 0) {
            send_reply(sockfd, "ERROR Wrong password");
            continue;
        }

        d->auth.connected = 1;
        save_database(db);
        send_reply(sockfd, "OK");
    }
else if (strncmp(line, "CHANGE_PASS ", 12) == 0) {
    char id[64], oldp[64], newp[64];

    if (sscanf(line + 12, "%63s %63s %63s", id, oldp, newp) != 3) {
        send_reply(sockfd, "ERROR Invalid format");
        continue;
    }

    Device *d = find_device(db, id);
    if (!d) {
        send_reply(sockfd, "ERROR Not found");
        continue;
    }

    if (strcmp(d->auth.password, oldp) != 0) {
        send_reply(sockfd, "ERROR Wrong password");
        continue;
    }

    strcpy(d->auth.password, newp);
    save_database(db);
    send_reply(sockfd, "OK");
}

        /* ---- Unknown command ---- */
        else {
            send_reply(sockfd, "300"); /* unknown command */
        }
    }


    close(sockfd);
    printf("[thread %lu] connection closed.\n", (unsigned long)pthread_self());
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc == 2) port = atoi(argv[1]);

    Database db;
    if (!load_database(&db)) {
        printf("Failed to load database!\n");
        return 1;
    }   
    printf("Database loaded: %d devices\n", db.deviceCount);


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

        int clientSock = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size);
        if (clientSock == -1) {
            perror("accept error");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        printf("New connection from %s:%d\n", client_ip, client_port);

        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->sockfd = clientSock;   // socket của client
        args->db = &db;             // trỏ tới database đã load

        pthread_create(&tid, NULL, client_thread, args);
    }

    close(listenfd);
    return 0;

}

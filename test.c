#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "cJSON.h"

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

int load_database(Database *db)
{
    FILE *f = fopen("DB.json", "r");
    if (!f) {
        printf("Không mở được DB.json\n");
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
        printf("Lỗi parse JSON\n");
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
void print_database(const Database *db)
{
    printf("=== HOME: %s ===\n", db->homeName);

    for (int i = 0; i < db->roomCount; i++) {
        const Room *r = &db->rooms[i];

        printf("\nROOM %s - %s\n", r->roomId, r->roomName);
        printf("Devices:\n");
        for (int j = 0; j < r->deviceCount; j++)
            printf(" - %s\n", r->deviceIds[j]);
    }

    printf("\n=== DEVICE DETAILS ===\n");

    for (int i = 0; i < db->deviceCount; i++) {
        const Device *d = &db->devices[i];

        printf("\n[%s] %s (%s)\n", d->deviceId, d->name, d->type);
        printf("Connected: %d | Token: %s\n", d->auth.connected, d->auth.token);
        printf("Power: %s | Timer: %d | Speed: %d | Temp: %d\n",
               d->state.power, d->state.timer, d->state.speed, d->state.temperature);

        printf("Usage Day: %.1f  Month: %.1f  Year: %.1f\n",
               d->powerUsage.usageHistory.day,
               d->powerUsage.usageHistory.month,
               d->powerUsage.usageHistory.year);

        printf("Logs (%d):\n", d->logCount);
        for (int j = 0; j < d->logCount; j++)
            printf(" - [%s] %s\n", d->logs[j].time, d->logs[j].action);
    }
}
int main() {
    Database db;

    if (!load_database(&db)) {
        printf("Load thất bại!\n");
        return 1;
    }

    print_database(&db);
    return 0;
}

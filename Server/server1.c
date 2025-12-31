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
#include <cJSON.h>

#define PORT 5550
#define BACKLOG 20
#define BUF_SIZE 1024

typedef struct
{
    double day;
    double month;
    double year;
} UsageHistory;

typedef struct
{
    int currentWatt;
    UsageHistory usageHistory;
    int lastDay;
    int lastMonth;
    int lastYear;
} PowerUsage;

typedef struct
{
    char time[64];
    char action[128];
} LogEntry;

typedef struct
{
    char password[64];
    int connected;
    char token[64];
} AuthInfo;

typedef struct
{
    char power[16];
    int timer;
    int speed;
    char mode[32];
    int temperature;
} DeviceState;

typedef struct
{
    char deviceId[64];
    char type[32];
    char name[128];

    AuthInfo auth;
    DeviceState state;
    PowerUsage powerUsage;

    LogEntry logs[64];
    int logCount;
} Device;

typedef struct
{
    char roomId[64];
    char roomName[128];

    char deviceIds[64][64];
    int deviceCount;
} Room;

typedef struct
{
    char homeName[128];
    Room rooms[32];
    int roomCount;
} Home;

typedef struct
{
    Home homes[16];
    int homeCount;
    int currentHome;

    Device devices[128];
    int deviceCount;
} Database;

typedef struct
{
    char internal_buf[4096];
    size_t internal_len;
} RecvContext;

typedef struct
{
    int sockfd;
    Database *db;
    RecvContext recv_ctx;
} ThreadArgs;

// Global mutex để bảo vệ database
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

int load_database(Database *db)
{
    FILE *f = fopen("DB.json", "r");
    if (!f)
    {
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
    if (!root)
    {
        printf("JSON Error: %s\n", cJSON_GetErrorPtr());
        free(buffer);
        return 0;
    }

    // Reset db before loading
    memset(db, 0, sizeof(*db));
    db->currentHome = 0;

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    int today = tm_now ? tm_now->tm_mday : 1;
    int thisMonth = tm_now ? tm_now->tm_mon + 1 : 1;
    int thisYear = tm_now ? tm_now->tm_year + 1900 : 1970;

    // -------- HOME(S) --------
    cJSON *homeNode = cJSON_GetObjectItem(root, "home");
    int homeN = cJSON_GetArraySize(homeNode);
    int maxHomes = (int)(sizeof(db->homes) / sizeof(db->homes[0]));
    if (homeN > maxHomes)
        homeN = maxHomes;

    db->homeCount = homeN;
    for (int hi = 0; hi < homeN; hi++)
    {
        cJSON *homeObj = cJSON_GetArrayItem(homeNode, hi);
        Home *h = &db->homes[hi];

        cJSON *hn = cJSON_GetObjectItem(homeObj, "homeName");
        if (hn && cJSON_IsString(hn) && hn->valuestring)
            strncpy(h->homeName, hn->valuestring, sizeof(h->homeName) - 1);

        cJSON *rooms = cJSON_GetObjectItem(homeObj, "rooms");
        if (!cJSON_IsArray(rooms))
            continue;

        int roomN = cJSON_GetArraySize(rooms);
        int maxRooms = (int)(sizeof(h->rooms) / sizeof(h->rooms[0]));
        if (roomN > maxRooms)
            roomN = maxRooms;

        h->roomCount = roomN;
        for (int i = 0; i < roomN; i++)
        {
            cJSON *room = cJSON_GetArrayItem(rooms, i);
            Room *r = &h->rooms[i];

            cJSON *rid = cJSON_GetObjectItem(room, "roomId");
            cJSON *rname = cJSON_GetObjectItem(room, "roomName");
            if (rid && cJSON_IsString(rid) && rid->valuestring)
                strncpy(r->roomId, rid->valuestring, sizeof(r->roomId) - 1);
            if (rname && cJSON_IsString(rname) && rname->valuestring)
                strncpy(r->roomName, rname->valuestring, sizeof(r->roomName) - 1);

            cJSON *dList = cJSON_GetObjectItem(room, "devices");
            if (!cJSON_IsArray(dList))
                continue;

            int devN = cJSON_GetArraySize(dList);
            int maxDev = (int)(sizeof(r->deviceIds) / sizeof(r->deviceIds[0]));
            if (devN > maxDev)
                devN = maxDev;

            r->deviceCount = devN;
            for (int j = 0; j < devN; j++)
            {
                cJSON *dv = cJSON_GetArrayItem(dList, j);
                if (dv && cJSON_IsString(dv) && dv->valuestring)
                    strncpy(r->deviceIds[j], dv->valuestring, sizeof(r->deviceIds[j]) - 1);
            }
        }
    }

    if (db->homeCount <= 0)
    {
        db->homeCount = 1;
        strncpy(db->homes[0].homeName, "Home_1", sizeof(db->homes[0].homeName) - 1);
        db->homes[0].roomCount = 0;
        db->currentHome = 0;
    }

    // -------- DEVICES --------
    cJSON *devArr = cJSON_GetObjectItem(root, "devices");
    db->deviceCount = cJSON_GetArraySize(devArr);

    for (int i = 0; i < db->deviceCount; i++)
    {
        cJSON *d = cJSON_GetArrayItem(devArr, i);
        Device *dev = &db->devices[i];

        strcpy(dev->deviceId, cJSON_GetObjectItem(d, "deviceId")->valuestring);
        strcpy(dev->type, cJSON_GetObjectItem(d, "type")->valuestring);
        strcpy(dev->name, cJSON_GetObjectItem(d, "name")->valuestring);

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
        dev->powerUsage.usageHistory.day = cJSON_GetObjectItem(hist, "day")->valuedouble;
        dev->powerUsage.usageHistory.month = cJSON_GetObjectItem(hist, "month")->valuedouble;
        dev->powerUsage.usageHistory.year = cJSON_GetObjectItem(hist, "year")->valuedouble;
        dev->powerUsage.lastDay = today;
        dev->powerUsage.lastMonth = thisMonth;
        dev->powerUsage.lastYear = thisYear;

        // LOGS
        cJSON *logs = cJSON_GetObjectItem(d, "logs");
        dev->logCount = cJSON_GetArraySize(logs);

        for (int j = 0; j < dev->logCount; j++)
        {
            cJSON *lg = cJSON_GetArrayItem(logs, j);
            strcpy(dev->logs[j].time, cJSON_GetObjectItem(lg, "time")->valuestring);
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

    // HOME(S)
    cJSON *homes = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "home", homes);

    for (int hi = 0; hi < db->homeCount; hi++)
    {
        Home *h = &db->homes[hi];
        cJSON *home = cJSON_CreateObject();
        cJSON_AddItemToArray(homes, home);
        cJSON_AddStringToObject(home, "homeName", h->homeName);

        cJSON *rooms = cJSON_CreateArray();
        cJSON_AddItemToObject(home, "rooms", rooms);

        for (int i = 0; i < h->roomCount; i++)
        {
            Room *r = &h->rooms[i];
            cJSON *room = cJSON_CreateObject();
            cJSON_AddItemToArray(rooms, room);

            cJSON_AddStringToObject(room, "roomId", r->roomId);
            cJSON_AddStringToObject(room, "roomName", r->roomName);

            cJSON *dArr = cJSON_CreateArray();
            cJSON_AddItemToObject(room, "devices", dArr);
            for (int j = 0; j < r->deviceCount; j++)
            {
                cJSON_AddItemToArray(dArr, cJSON_CreateString(r->deviceIds[j]));
            }
        }
    }

    // DEVICES
    cJSON *devices = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devices);

    for (int i = 0; i < db->deviceCount; i++)
    {

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

        for (int j = 0; j < d->logCount; j++)
        {
            cJSON *lg = cJSON_CreateObject();
            cJSON_AddItemToArray(logs, lg);

            cJSON_AddStringToObject(lg, "time", d->logs[j].time);
            cJSON_AddStringToObject(lg, "action", d->logs[j].action);
        }
    }

    char *jsonStr = cJSON_Print(root);

    FILE *f = fopen("DB.json", "w");
    if (!f)
    {
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

Device *find_device_byId(Database *db, const char *id)
{
    for (int i = 0; i < db->deviceCount; i++)
    {
        if (strcmp(db->devices[i].deviceId, id) == 0)
            return &db->devices[i];
    }
    return NULL;
}
Device *find_device_byToken(Database *db, const char *token)
{
    for (int i = 0; i < db->deviceCount; i++)
    {
        if (strcmp(db->devices[i].auth.token, token) == 0)
            return &db->devices[i];
    }
    return NULL;
}
// ====== ADD LOG ======
void add_log(Device *dev, const char *text)
{
    if (dev->logCount >= 64)
    {
        // Nếu đã đầy, xóa log cũ nhất
        for (int i = 1; i < 64; i++)
        {
            dev->logs[i - 1] = dev->logs[i];
        }
        dev->logCount--;
    }

    time_t now = time(NULL);
    strftime(dev->logs[dev->logCount].time, sizeof(dev->logs[dev->logCount].time), "%Y-%m-%d %H:%M:%S", localtime(&now));
    strncpy(dev->logs[dev->logCount].action, text, sizeof(dev->logs[dev->logCount].action) - 1);
    dev->logs[dev->logCount].action[sizeof(dev->logs[dev->logCount].action) - 1] = '\0';
    dev->logCount++;
}

// ====== TIMER THREAD AND POWER USAGE THREAD =======
void *timer_thread(void *arg)
{
    Database *db = (Database *)arg;
    pthread_detach(pthread_self());

    printf("[Timer Thread] Started - checking every 60 seconds\n");
    printf("[Power Usage Thread] Started - updating every 60 seconds\n");
    while (1)
    {
        sleep(60); // Ngủ 60 giây

        pthread_mutex_lock(&db_mutex);
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        int today = tm_now ? tm_now->tm_mday : -1;
        int thisMonth = tm_now ? tm_now->tm_mon + 1 : -1;
        int thisYear = tm_now ? tm_now->tm_year + 1900 : -1;
        // timer countdown
        // int changed = 0;
        for (int i = 0; i < db->deviceCount; i++)
        {
            Device *dev = &db->devices[i];

            if (dev->state.timer > 0)
            {
                dev->state.timer--;
                // changed = 1;

                printf("[Timer] %s: %d minutes remaining\n",
                       dev->deviceId, dev->state.timer);

                // Nếu timer về 0, thực hiện hành động
                if (dev->state.timer == 0)
                {
                    printf("[Timer] %s: Timer expired! Action completed.\n",
                           dev->deviceId);
                    // Tắt hoặc bật thiết bị khi timer hết
                    if (strcmp(dev->state.power, "OFF") == 0)
                        strcpy(dev->state.power, "ON");
                    else
                    {
                        strcpy(dev->state.power, "OFF");
                    }
                    char logbuf[128];
                    snprintf(logbuf, sizeof(logbuf), "POWER OFF SPEED %d MODE %s TEMPERATURE %d", dev->state.speed, dev->state.mode, dev->state.temperature); // add log
                    add_log(dev, logbuf);
                }
            }
        }

        // if (changed)
        // {
        //     save_database(db);
        // }
        // power usage update
        for (int i = 0; i < db->deviceCount; i++)
        {
            Device *dev = &db->devices[i];

            if (thisYear != -1 && dev->powerUsage.lastYear != thisYear)
            {
                dev->powerUsage.usageHistory.year = 0;
                dev->powerUsage.lastYear = thisYear;
            }
            if (thisMonth != -1 && dev->powerUsage.lastMonth != thisMonth)
            {
                dev->powerUsage.usageHistory.month = 0;
                dev->powerUsage.lastMonth = thisMonth;
            }
            if (today != -1 && dev->powerUsage.lastDay != today)
            {
                dev->powerUsage.usageHistory.day = 0;
                dev->powerUsage.lastDay = today;
            }

            if (strcmp(dev->state.power, "ON") == 0)
            {
                // Nếu thiết bị đang bật
                if (strcmp(dev->type, "LIGHT") == 0 || strcmp(dev->type, "light") == 0)
                {
                    dev->powerUsage.currentWatt = 100; 
                }
                else if (strcmp(dev->type, "FAN") == 0 || strcmp(dev->type, "fan") == 0)
                {
                    if (dev->state.speed == 1)
                        dev->powerUsage.currentWatt = 200; 
                    else if (dev->state.speed == 2)
                        dev->powerUsage.currentWatt = 350; 
                    else if (dev->state.speed == 3)
                        dev->powerUsage.currentWatt = 500; 
                }
                else if (strcmp(dev->type, "AC") == 0 || strcmp(dev->type, "ac") == 0)
                {
                    if (strcmp(dev->state.mode, "FAN") == 0)
                    {
                        dev->powerUsage.currentWatt = 900; 
                    }
                    else if (strcmp(dev->state.mode, "COOL") == 0)
                    {
                        dev->powerUsage.currentWatt = 1800;
                    }
                    else if (strcmp(dev->state.mode, "DRY") == 0)
                    {
                        dev->powerUsage.currentWatt = 2000; 
                    }
                }
            }
            else
            {
               
                dev->powerUsage.currentWatt = 0;
            }

            // Cộng dồn vào lịch sử sử dụng công thức kWh= (Wattxt/60)/1000 với t tính theo phút
            dev->powerUsage.usageHistory.day += (double)dev->powerUsage.currentWatt / 60000.0;   // kWh
            dev->powerUsage.usageHistory.month += (double)dev->powerUsage.currentWatt / 60000.0; // kWh
            dev->powerUsage.usageHistory.year += (double)dev->powerUsage.currentWatt / 60000.0;  // kWh
        }
        save_database(db);
        pthread_mutex_unlock(&db_mutex);
    }

    return NULL;
}

void generate_token(char *token, int len)
{
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < len; i++)
        token[i] = hex[rand() % 16];
    token[len] = '\0';
}

void trim_end(char *s)
{
    int i = (int)strlen(s) - 1;
    while (i >= 0 && (s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t'))
    {
        s[i] = '\0';
        i--;
    }
}

/* Send a reply code to the client */
void send_reply(int sock, const char *code)
{
    char out[64];
    snprintf(out, sizeof(out), "%s\r\n", code);
    send(sock, out, strlen(out), 0);
}
// ====== GET CURRENT HOME ======
Home *get_current_home(Database *db)
{
    if (!db || db->homeCount <= 0)
        return NULL;
    if (db->currentHome < 0 || db->currentHome >= db->homeCount)
        db->currentHome = 0;
    return &db->homes[db->currentHome];
}

// ====== SHOW HOME ======
void handle_show_home(Database *db, int sock)
{
    char out[256];

    pthread_mutex_lock(&db_mutex);
    Home *cur = get_current_home(db);
    if (!cur)
    {
        pthread_mutex_unlock(&db_mutex);
        send(sock, "END\r\n", 5, 0);
        return;
    }

    snprintf(out, sizeof(out), "CURRENT HOME: %s\r\n", cur->homeName);
    send(sock, out, strlen(out), 0);

    for (int i = 0; i < db->homeCount; i++)
    {
        Home *h = &db->homes[i];
        snprintf(out, sizeof(out), "HOME: %s\r\n", h->homeName);
        send(sock, out, strlen(out), 0);

        for (int j = 0; j < h->roomCount; j++)
        {
            Room *r = &h->rooms[j];
            snprintf(out, sizeof(out), "ROOM: %s (%s)\r\n", r->roomName, r->roomId);
            send(sock, out, strlen(out), 0);
        }
    }

    pthread_mutex_unlock(&db_mutex);
    send(sock, "END\r\n", 5, 0);
}
// ====== ADD DEVICE ======
void handle_add_device(Database *db, const char *deviceId, const char *roomId, int sockfd)
{
    if (!deviceId || deviceId[0] == '\0' || !roomId || roomId[0] == '\0')
    {
        send_reply(sockfd, "400");
        return;
    }

    pthread_mutex_lock(&db_mutex);

    Home *cur = get_current_home(db);
    if (!cur)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "500");
        return;
    }

    Device *dev = find_device_byId(db, deviceId);
    if (!dev)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "404"); // device not found in device list
        return;
    }

    Room *targetRoom = NULL;
    for (int i = 0; i < cur->roomCount; i++)
    {
        if (strcmp(cur->rooms[i].roomId, roomId) == 0)
        {
            targetRoom = &cur->rooms[i];
            break;
        }
    }
    if (!targetRoom)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "404");
        return;
    }

    if (targetRoom->deviceCount >= (int)(sizeof(targetRoom->deviceIds) / sizeof(targetRoom->deviceIds[0])))
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "231"); // full
        return;
    }
    for (int i = 0; i < cur->roomCount; i++)
    {
        Room *r = &cur->rooms[i];
        for (int j = 0; j < r->deviceCount; j++)
        {
            if (strcmp(r->deviceIds[j], deviceId) == 0)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "222"); // already exists in another room
                return;
            }
        }
    }
    strncpy(targetRoom->deviceIds[targetRoom->deviceCount], deviceId, sizeof(targetRoom->deviceIds[targetRoom->deviceCount]) - 1);
    targetRoom->deviceIds[targetRoom->deviceCount][sizeof(targetRoom->deviceIds[targetRoom->deviceCount]) - 1] = '\0';
    targetRoom->deviceCount++;

    save_database(db);
    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, "200");
}
void handle_delete_device(Database *db, const char *deviceId, int sockfd)
{
    pthread_mutex_lock(&db_mutex);
    // Xoa device khoi Devices list
    int devIndex = -1;
    for (int i = 0; i < db->deviceCount; i++)
    {
        if (strcmp(db->devices[i].deviceId, deviceId) == 0)
        {
            devIndex = i;
            break;
        }
    }
    if (devIndex != -1)
    {
        for (int i = devIndex; i < db->deviceCount - 1; i++)
        {
            db->devices[i] = db->devices[i + 1];
        }
        db->deviceCount--;
    }
    // xoa device khoi room
    Home *cur = get_current_home(db);
    Room *targetRoom = NULL;
    int deviceIndex = -1;
    for (int i = 0; i < cur->roomCount; i++)
    {
        Room *r = &cur->rooms[i];
        for (int j = 0; j < r->deviceCount; j++)
        {
            if (strcmp(r->deviceIds[j], deviceId) == 0)
            {
                targetRoom = r;
                deviceIndex = j;
                break;
            }
        }
        if (targetRoom)
            break;
    }
    if (!targetRoom || deviceIndex == -1)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "404");
        return;
    }

    // Xoa deviceId khoi room bang cach copy cac phan tu sau len truoc
    for (int j = deviceIndex; j < targetRoom->deviceCount - 1; j++)
    {
        strcpy(targetRoom->deviceIds[j], targetRoom->deviceIds[j + 1]);
    }
    targetRoom->deviceCount--;
    save_database(db);
    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, "200");
}
// ====== SET CURRENT HOME ======
void handle_set_home(Database *db, const char *homeName, int sockfd)
{
    pthread_mutex_lock(&db_mutex);
    int found = -1;
    for (int i = 0; i < db->homeCount; i++)
    {
        if (strcmp(db->homes[i].homeName, homeName) == 0)
        {
            found = i;
            break;
        }
    }
    if (found < 0)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "404");
        return;
    }
    db->currentHome = found;
    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, "200");
}

// ====== ADD HOME ======
void handle_add_home(Database *db, const char *homeName, int sockfd)
{
    pthread_mutex_lock(&db_mutex);

    int maxHomes = (int)(sizeof(db->homes) / sizeof(db->homes[0]));
    if (db->homeCount >= maxHomes)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "231");
        return;
    }

    for (int i = 0; i < db->homeCount; i++)
    {
        if (strcmp(db->homes[i].homeName, homeName) == 0)
        {
            pthread_mutex_unlock(&db_mutex);
            send_reply(sockfd, "222");
            return;
        }
    }

    Home *h = &db->homes[db->homeCount];
    memset(h, 0, sizeof(*h));
    strncpy(h->homeName, homeName, sizeof(h->homeName) - 1);
    h->roomCount = 0;

    db->currentHome = db->homeCount;
    db->homeCount++;
    save_database(db);
    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, "200");
}

// ====== ADD ROOM ======
void handle_add_room(Database *db, const char *roomId, const char *roomName, int sockfd)
{
    pthread_mutex_lock(&db_mutex);

    Home *cur = get_current_home(db);
    if (!cur)
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "404");
        return;
    }

    if (cur->roomCount >= (int)(sizeof(cur->rooms) / sizeof(cur->rooms[0])))
    {
        pthread_mutex_unlock(&db_mutex);
        send_reply(sockfd, "231"); // full
        return;
    }

    for (int i = 0; i < cur->roomCount; i++)
    {
        if (strcmp(cur->rooms[i].roomId, roomId) == 0)
        {
            pthread_mutex_unlock(&db_mutex);
            send_reply(sockfd, "222"); // already exists
            return;
        }
    }

    Room *r = &cur->rooms[cur->roomCount];
    strncpy(r->roomId, roomId, sizeof(r->roomId) - 1);
    r->roomId[sizeof(r->roomId) - 1] = '\0';
    strncpy(r->roomName, roomName, sizeof(r->roomName) - 1);
    r->roomName[sizeof(r->roomName) - 1] = '\0';
    r->deviceCount = 0;

    cur->roomCount++;
    save_database(db);
    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, "200");
}

// ====== SHOW ROOM ======
void handle_show_room(Database *db, const char *roomId, int sock)
{
    char out[512];

    pthread_mutex_lock(&db_mutex);
    Home *cur = get_current_home(db);
    if (!cur)
    {
        pthread_mutex_unlock(&db_mutex);
        send(sock, "END\r\n", 5, 0);
        return;
    }

    for (int i = 0; i < cur->roomCount; i++)
    {
        Room *r = &cur->rooms[i];
        if (strcmp(r->roomId, roomId) == 0)
        {
            snprintf(out, sizeof(out), "ROOM: %s (%s)\r\n", r->roomName, r->roomId);
            send(sock, out, strlen(out), 0);

            for (int j = 0; j < r->deviceCount; j++)
            {
                const char *devId = r->deviceIds[j];
                Device *dev = NULL;
                for (int k = 0; k < db->deviceCount; k++)
                {
                    if (strcmp(db->devices[k].deviceId, devId) == 0)
                    {
                        dev = &db->devices[k];
                        break;
                    }
                }
                if (dev)
                {
                    snprintf(out, sizeof(out), "DEVICE: %s || %s || %s\r\n", dev->deviceId, dev->type, dev->name);
                    send(sock, out, strlen(out), 0);
                }
            }
            send(sock, "END\r\n", 5, 0);
            pthread_mutex_unlock(&db_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    send(sock, "1000\r\n", 6, 0); // room not found
    send(sock, "END\r\n", 5, 0);
}
// ====== CHECK TYPE DEVICE ======
int is_device_type(Database *db, const char *token)
{
    int result = 0;
    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            if (strcmp(dev->type, "FAN") == 0 || strcmp(dev->type, "fan") == 0)
                result = 1;
            else if (strcmp(dev->type, "AC") == 0 || strcmp(dev->type, "ac") == 0)
                result = 2;
            else
                result = 3; // light
            return result;
        }
    }

    return 0; // token not found
}
// ====== POWER DEVICE ======
void handle_power_device(Database *db, const char *token, const char *action)
{
    char logbuf[128];
    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            if (strcmp(action, "ON") == 0 || strcmp(action, "OFF") == 0)
            {
                strcpy(dev->state.power, action);
                snprintf(logbuf, sizeof(logbuf), "POWER %s SPEED %d MODE %s TEMPERATURE %d TIMER %d", action, dev->state.speed, dev->state.mode, dev->state.temperature, dev->state.timer); // ghi log
                add_log(dev, logbuf);
            }
            pthread_mutex_unlock(&db_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&db_mutex);
}
// ====== SPEED DEVICE ======
void handle_speed_device(Database *db, const char *token, int speed, int sockfd)
{
    char logbuf[128];
    pthread_mutex_lock(&db_mutex);
    int result = 0;
    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            int res = is_device_type(db, token);
            if (res != 1)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "405"); // device not support speed
                return;
            }
            else if (speed == 0)
            {
                dev->state.speed = speed;
                strcpy(dev->state.power, "OFF");
                snprintf(logbuf, sizeof(logbuf), "POWER %s SPEED %d MODE %s TEMPERATURE %d TIMER %d", dev->state.power, speed, dev->state.mode, dev->state.temperature, dev->state.timer); // add log
                add_log(dev, logbuf);
            }
            else if (speed < 0 || speed > 3)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "400"); // invalid speed
                return;
            }
            else if (strcmp(dev->state.power, "OFF") == 0)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "502"); // device is off
                return;
            }
            dev->state.speed = speed;
            snprintf(logbuf, sizeof(logbuf), "POWER %s SPEED %d MODE %s TEMPERATURE %d TIMER %d", dev->state.power, speed, dev->state.mode, dev->state.temperature, dev->state.timer); // add log
            add_log(dev, logbuf);
            pthread_mutex_unlock(&db_mutex);
            send_reply(sockfd, "200"); // success
            return;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "202"); // not found
    return;
}
// ====== MODE DEVICE ======
void handle_mode_device(Database *db, const char *token, const char *mode, int sockfd)
{
    pthread_mutex_lock(&db_mutex);
    int result = 0;
    char logbuf[128];
    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            int res = is_device_type(db, token);
            if (res != 2)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "407"); // device not support mode
                return;
            }
            else if (strcmp(dev->state.power, "OFF") == 0)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "502"); // device is off
                return;
            }
            strcpy(dev->state.mode, mode);
            snprintf(logbuf, sizeof(logbuf), "POWER %s SPEED %d MODE %s TEMPERATURE %d TIMER %d", dev->state.power, dev->state.speed, mode, dev->state.temperature, dev->state.timer); // add log
            add_log(dev, logbuf);
            pthread_mutex_unlock(&db_mutex);
            send_reply(sockfd, "200"); // success
            return;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "202"); // not found
    return;
}
// ====== TEMPERATURE DEVICE ======
void handle_temperature_device(Database *db, const char *token, int temperature, int sockfd)
{
    pthread_mutex_lock(&db_mutex);
    char logbuf[128];
    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            int result = is_device_type(db, token);
            if (result != 2)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "406"); // device not support temperature
                return;
            }
            if (strcmp(dev->state.power, "OFF") == 0)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "502"); // device is off
                return;
            }
            if (temperature < 16 || temperature > 30)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "400"); // invalid value
                return;                    // invalid temperature
            }
            dev->state.temperature = temperature;
            snprintf(logbuf, sizeof(logbuf), "POWER %s SPEED %d MODE %s TEMPERATURE %d TIMER %d", dev->state.power, dev->state.speed, dev->state.mode, temperature, dev->state.timer); // add log
            add_log(dev, logbuf);
            pthread_mutex_unlock(&db_mutex);
            send_reply(sockfd, "200"); // success
            return;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "202"); // not found
    return;
}
// ====== TIMER DEVICE ======

void handle_timer_device(Database *db, const char *token, const char *minuteStr, const char *action, int sockfd)
{
    int minutes = atoi(minuteStr);
    if (minutes <= 0)
    {
        send_reply(sockfd, "400"); // invalid minute
        return;
    }
    char logbuf[128];
    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            if (strcmp(dev->state.power, action) == 0)
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "221"); // already set/cancel
                return;
            }
            if (strcmp(action, "ON") == 0 || strcmp(action, "OFF") == 0)
            {
                dev->state.timer = minutes;
                snprintf(logbuf, sizeof(logbuf), "POWER %s SPEED %d MODE %s TEMPERATURE %d TIMER %d", dev->state.power, dev->state.speed, dev->state.mode, dev->state.temperature, minutes); // add log
                add_log(dev, logbuf);
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "200"); // success
                return;
            }
            else
            {
                pthread_mutex_unlock(&db_mutex);
                send_reply(sockfd, "500"); // invalid action
                return;
            }
        }
    }

    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "202"); // device not found
    return;
}
// ====== POWER USAGE DEVICE ======
void handle_power_usage_device(Database *db, const char *token, int sockfd)
{
    pthread_mutex_lock(&db_mutex);

    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            char out[256];
            snprintf(out, sizeof(out),
                     "CURRENT WATT: %d W\r\nDAILY USAGE: %.2f kWh\r\nMONTHLY USAGE: %.2f kWh\r\nYEARLY USAGE: %.2f kWh\r\n",
                     dev->powerUsage.currentWatt,
                     dev->powerUsage.usageHistory.day,
                     dev->powerUsage.usageHistory.month,
                     dev->powerUsage.usageHistory.year);
            pthread_mutex_unlock(&db_mutex);
            send(sockfd, out, strlen(out), 0);
            send(sockfd, "END\r\n", 5, 0);
            return;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "202"); // device not found
    send(sockfd, "END\r\n", 5, 0);
    return;
}
// ====== SHOW INFORMATION DEVICE ======
void handle_show_information_device(Database *db, const char *token, int sockfd)
{
    pthread_mutex_lock(&db_mutex);
    int result = 0;
    for (int i = 0; i < db->deviceCount; i++)
    {
        Device *dev = &db->devices[i];
        if (strcmp(dev->auth.token, token) == 0)
        {
            result = is_device_type(db, token);
            char out[512];
            if (result == 1) // fan
                snprintf(out, sizeof(out),
                         "DEVICE ID: %s\r\nTYPE: %s\r\nNAME: %s\r\nPOWER: %s\r\nTIMER: %d minutes\r\nSPEED: %d\r\n",
                         dev->deviceId,
                         dev->type,
                         dev->name,
                         dev->state.power,
                         dev->state.timer,
                         dev->state.speed);
            else if (result == 2) // ac
                snprintf(out, sizeof(out),
                         "DEVICE ID: %s\r\nTYPE: %s\r\nNAME: %s\r\nPOWER: %s\r\nTIMER: %d minutes\r\nMODE: %s\r\nTEMPERATURE: %d °C\r\n",
                         dev->deviceId,
                         dev->type,
                         dev->name,
                         dev->state.power,
                         dev->state.timer,
                         dev->state.mode,
                         dev->state.temperature);
            else // light
                snprintf(out, sizeof(out),
                         "DEVICE ID: %s\r\nTYPE: %s\r\nNAME: %s\r\nPOWER: %s\r\nTIMER: %d minutes\r\n",
                         dev->deviceId,
                         dev->type,
                         dev->name,
                         dev->state.power,
                         dev->state.timer);
            pthread_mutex_unlock(&db_mutex);
            send(sockfd, out, strlen(out), 0);
            send(sockfd, "END\r\n", 5, 0);
            return;
        }
    }

    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "202"); // device not found
    send(sockfd, "END\r\n", 5, 0);
    return;
}
/* Safely receive one line (ending with '\r\n') - Buffer-based version */
ssize_t recv_line(int sock, RecvContext *ctx, char *buf, size_t maxlen)
{
    while (1)
    {

        ctx->internal_buf[ctx->internal_len] = '\0';

        char *pos = strstr(ctx->internal_buf, "\r\n");

        if (pos != NULL)
        {
            size_t linelen = pos - ctx->internal_buf;
            size_t total_extracted_len = linelen + 2;
            size_t copy_len = (linelen < maxlen - 1) ? linelen : maxlen - 1;
            memcpy(buf, ctx->internal_buf, copy_len);
            buf[copy_len] = '\0';

            size_t remain = ctx->internal_len - total_extracted_len;

            if (remain > 0)
            {
                memmove(ctx->internal_buf, ctx->internal_buf + total_extracted_len, remain);
            }

            ctx->internal_len = remain;
            ctx->internal_buf[ctx->internal_len] = '\0';

            return (ssize_t)copy_len;
        }

        ssize_t n = recv(sock,
                         ctx->internal_buf + ctx->internal_len,
                         sizeof(ctx->internal_buf) - ctx->internal_len - 1,
                         0);

        if (n == 0)
        {
            return 0;
        }
        if (n < 0)
        {
            return -1;
        }

        ctx->internal_len += n;
    }
}
void handle_scan_struct(int sock, Database *db)
{
    char out[512];

    send(sock, "MY DEVICES\r\n", 12, 0);
    for (int i = 0; i < db->deviceCount; i++)
    {
        if (db->devices[i].auth.connected)
        {
            snprintf(out, sizeof(out), "%s || %s || %s || %s \r\n", db->devices[i].deviceId, db->devices[i].type, db->devices[i].name, db->devices[i].auth.token);
            send(sock, out, strlen(out), 0);
        }
    }

    send(sock, "NEW DEVICES\r\n", 13, 0);
    for (int i = 0; i < db->deviceCount; i++)
    {
        if (!db->devices[i].auth.connected)
        {
            snprintf(out, sizeof(out), "%s\r\n", db->devices[i].deviceId);
            send(sock, out, strlen(out), 0);
        }
    }

    send(sock, "END\r\n", 5, 0);
}

void handle_connect(int sockfd, Database *db, const char *line)
{
    char id[64], pass[64];

    if (sscanf(line + 8, "%63s %63s", id, pass) != 2)
    {
        send_reply(sockfd, "203"); // invalid params
        return;
    }
    pthread_mutex_lock(&db_mutex);
    Device *d = find_device_byId(db, id);
    if (!d)
    {
        send_reply(sockfd, "202"); // device not found
        pthread_mutex_unlock(&db_mutex);
        return;
    }

    if (strcmp(d->auth.password, pass) != 0)
    {
        send_reply(sockfd, "201"); // wrong password
        pthread_mutex_unlock(&db_mutex);
        return;
    }

    // ---- Authentication success ----

    // If no token → generate new
    if (strlen(d->auth.token) == 0)
    {
        char newToken[32];
        generate_token(newToken, 16);

        strcpy(d->auth.token, newToken);
        d->auth.connected = true;

        save_database(db); // save db with new token
    }

    // reply format: 200 <device ID> <token>
    char reply[256];
    snprintf(reply, sizeof(reply),
             "200 %s %s",
             d->deviceId,
             d->auth.token);
    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, reply);
}

void handle_pass(int sockfd, Database *db, const char *line)
{
    char token[64], oldp[64], newp[64], logbuf[64];

    if (sscanf(line + 5, "%63s %63s", token, newp) != 2)
    {
        send_reply(sockfd, "203"); // invalid params
        return;
    }
    pthread_mutex_lock(&db_mutex);

    Device *d = find_device_byToken(db, token);
    if (!d)
    {
        send_reply(sockfd, "401"); // invalid token
        pthread_mutex_unlock(&db_mutex);
        return;
    }

    // ---- Authentication success ----

    strcpy(d->auth.password, newp);
    snprintf(logbuf, sizeof(logbuf), "PASSWORD CHANGED"); // add log
    add_log(d, logbuf);
    save_database(db);
    pthread_mutex_unlock(&db_mutex);
    send_reply(sockfd, "210"); // Success - password changed
}

void handle_init(int sockfd, Database *db, const char *line)
{
    char id[64], pass[64], type[32], name[128];

    // INIT <ID> <PASS> <TYPE> <NAME>
    if (sscanf(line + 5, "%63s %63s %31s %127s",
               id, pass, type, name) != 4)
    {
        send_reply(sockfd, "203"); // Invalid format
        return;
    }

    pthread_mutex_lock(&db_mutex);

    // 1. Check duplicate ID
    for (int i = 0; i < db->deviceCount; i++)
    {
        if (strcmp(db->devices[i].deviceId, id) == 0)
        {
            send_reply(sockfd, "221");
            pthread_mutex_unlock(&db_mutex);
            return;
        }
    }

    Device *new_dev = &db->devices[db->deviceCount];

    strcpy(new_dev->deviceId, id);
    strcpy(new_dev->type, type);
    strcpy(new_dev->name, name); //

    // Auth
    strcpy(new_dev->auth.password, pass);
    new_dev->auth.connected = false;
    new_dev->auth.token[0] = '\0';

    // Power default
    strcpy(new_dev->state.power, "OFF");
    new_dev->state.timer = 0;

    // Speed
    if (strcasecmp(type, "FAN") == 0)
        new_dev->state.speed = 1;
    else
        new_dev->state.speed = -1;

    // AC
    if (strcasecmp(type, "AC") == 0)
    {
        new_dev->state.temperature = 24;
        strcpy(new_dev->state.mode, "cool");
    }
    else
    {
        new_dev->state.temperature = -1;
        strcpy(new_dev->state.mode, "null");
    }

    // Power usage
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    new_dev->powerUsage.currentWatt = 0;
    new_dev->powerUsage.usageHistory.day = 0;
    new_dev->powerUsage.usageHistory.month = 0;
    new_dev->powerUsage.usageHistory.year = 0;

    new_dev->powerUsage.lastDay = tm_now ? tm_now->tm_mday : 1;
    new_dev->powerUsage.lastMonth = tm_now ? tm_now->tm_mon + 1 : 1;
    new_dev->powerUsage.lastYear = tm_now ? tm_now->tm_year + 1900 : 1970;

    db->deviceCount++;
    save_database(db);

    pthread_mutex_unlock(&db_mutex);

    send_reply(sockfd, "200");
    printf("Server: Created device %s (%s) name=%s\n",
           id, type, name);
}

void *client_thread(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    int sockfd = args->sockfd;
    Database *db = args->db;
    RecvContext recv_ctx = args->recv_ctx; // Copy recv context
    free(args);

    pthread_detach(pthread_self());

    char line[BUF_SIZE];

    send_reply(sockfd, "100"); /* Greeting */

    while (1)
    {
        ssize_t r = recv_line(sockfd, &recv_ctx, line, sizeof(line));
        printf("DEBUG recv_line return = %zd, cmd = [%s]\n", r, line);
        if (r == 0)
        {
            printf("[thread %lu] client disconnected.\n", (unsigned long)pthread_self());
            break;
        }
        else if (r < 0)
        {
            perror("[thread] recv_line");
            break;
        }

        trim_end(line);
        if (line[0] == '\0')
            continue;

        else if (strncmp(line, "SCAN", 4) == 0)
        {
            handle_scan_struct(sockfd, db);
        }
        else if (strncmp(line, "CONNECT ", 8) == 0)
        {
            handle_connect(sockfd, db, line);
        }
        else if (strncmp(line, "PASS ", 5) == 0)
        {
            handle_pass(sockfd, db, line);
        }
        else if (strncmp(line, "INIT ", 5) == 0)
        {
            handle_init(sockfd, db, line);
        }
        else if (strncmp(line, "ADD HOME ", 9) == 0)
        {
            char homeName[128];
            if (sscanf(line, "ADD HOME %127s", homeName) == 1)
            {
                handle_add_home(db, homeName, sockfd);
            }
            else
            {
                send_reply(sockfd, "203");
            }
        }
        else if (strncmp(line, "SET HOME ", 9) == 0)
        {
            char homeName[128];
            if (sscanf(line, "SET HOME %127s", homeName) == 1)
            {
                handle_set_home(db, homeName, sockfd);
            }
            else
            {
                send_reply(sockfd, "203");
            }
        }
        else if (strncmp(line, "ADD ROOM ", 9) == 0)
        {
            char roomId[64];
            char roomName[128];
            if (sscanf(line, "ADD ROOM %63s %127s", roomId, roomName) == 2)
            {
                handle_add_room(db, roomId, roomName, sockfd);
            }
            else
            {
                send_reply(sockfd, "203");
            }
        }
        else if (strncmp(line, "ADD DEVICE", 10) == 0)
        {
            char deviceId[64];
            char roomId[64];
            if (sscanf(line, "ADD DEVICE %63s %63s", deviceId, roomId) == 2)
            {
                handle_add_device(db, deviceId, roomId, sockfd);
            }
            else
            {
                send_reply(sockfd, "203");
            }
        }
        else if (strcmp(line, "SHOW HOME") == 0)
        {
            handle_show_home(db, sockfd);
        }
        else if (strncmp(line, "SHOW ROOM ", 10) == 0)
        {
            char roomId[64];
            if (sscanf(line, "SHOW ROOM %63s", roomId) == 1)
            {
                handle_show_room(db, roomId, sockfd);
            }
            else
            {
                send_reply(sockfd, "400"); // invalid format
            }
        }
        else if (strncmp(line, "POWER USAGE", 11) == 0)
        {
            char token[128];
            if (sscanf(line, "POWER USAGE %127s", token) == 1)
            {
                handle_power_usage_device(db, token, sockfd);
            }
            else
            {
                send_reply(sockfd, "203"); // invalid format
            }
        }
        else if (strncmp(line, "POWER", 5) == 0)
        {
            char token[128];
            char action[16];
            if (sscanf(line, "POWER %127s %16s", token, action) == 2)
            {
                handle_power_device(db, token, action);
                save_database(db);
                send_reply(sockfd, "200");
            }
            else
            {
                send_reply(sockfd, "203"); // invalid format
            }
        }
        else if (strncmp(line, "TIMER", 5) == 0)
        {
            char token[128];
            char minuteStr[16];
            char action[16];
            if (sscanf(line, "TIMER %127s %15s %16s", token, minuteStr, action) == 3)
            {
                handle_timer_device(db, token, minuteStr, action, sockfd);
                save_database(db);
            }
        }
        else if (strncmp(line, "SPEED", 5) == 0)
        {
            char token[128];
            int speed;
            if (sscanf(line, "SPEED %127s %d", token, &speed) == 2)
            {
                handle_speed_device(db, token, speed, sockfd);
                save_database(db);
            }
            else
            {
                send_reply(sockfd, "203"); // invalid format
            }
        }
        else if (strncmp(line, "TEMPERATURE", 11) == 0)
        {
            char token[128];
            int temperature;
            if (sscanf(line, "TEMPERATURE %127s %d", token, &temperature) == 2)
            {
                handle_temperature_device(db, token, temperature, sockfd);
                save_database(db);
            }
            else
            {
                send_reply(sockfd, "203"); // invalid format
            }
        }
        else if (strncmp(line, "MODE", 4) == 0)
        {
            char token[128];
            char mode[32];
            if (sscanf(line, "MODE %127s %31s", token, mode) == 2)
            {
                handle_mode_device(db, token, mode, sockfd);
                save_database(db);
            }
            else
            {
                send_reply(sockfd, "203"); // invalid format
            }
        }
        else if (strncmp(line, "SHOW INFO", 9) == 0)
        {
            char token[128];
            if (sscanf(line, "SHOW INFO %127s", token) == 1)
            {
                handle_show_information_device(db, token, sockfd);
            }
            else
            {
                send_reply(sockfd, "203"); // invalid format
            }
        }
        else if (strncmp(line, "DELETE DEVICE", 13) == 0)
        {
            char deviceId[64];
            char roomId[64];
            if (sscanf(line, "DELETE DEVICE %63s", deviceId) == 1)
            {
                handle_delete_device(db, deviceId, sockfd);
            }
            else
            {
                send_reply(sockfd, "203");
            }
        }
        /* ---- Unknown command ---- */
        else
        {
            send_reply(sockfd, "500"); /* unknown command */
        }
    }

    close(sockfd);
    printf("[thread %lu] connection closed.\n", (unsigned long)pthread_self());
    return NULL;
}

int main(int argc, char *argv[])
{
    int port = PORT;
    if (argc == 2)
        port = atoi(argv[1]);
    srand(time(NULL));

    Database db;
    if (!load_database(&db))
    {
        printf("Failed to load database!\n");
        return 1;
    }
    printf("Database loaded: %d devices\n", db.deviceCount);

    // Khởi động timer thread
    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, timer_thread, &db);

    int listenfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size = sizeof(client_addr);
    pthread_t tid;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) == -1)
    {
        perror("listen error");
        exit(EXIT_FAILURE);
    }
    printf("Server started on port %d.\n", port);

    while (1)
    {
        int clientSock = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size);
        if (clientSock == -1)
        {
            perror("accept error");
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        printf("New connection from %s:%d\n", client_ip, client_port);

        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->sockfd = clientSock;                          // socket của client
        args->db = &db;                                     // trỏ tới database đã load
        memset(&args->recv_ctx, 0, sizeof(args->recv_ctx)); // initialize per-connection recv context

        pthread_create(&tid, NULL, client_thread, args);
    }
    close(listenfd);
    return 0;
}

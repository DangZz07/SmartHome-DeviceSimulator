#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct {
    char deviceId[64];
    int connected;
} DeviceInfo;

typedef struct {
    DeviceInfo list[64];
    int count;
} DeviceList;

void skip_spaces(const char **p) {
    while (**p==' ' || **p=='\n' || **p=='\t' || **p=='\r')
        (*p)++;
}

void parse_devices(const char *json, DeviceList *out) {
    out->count = 0;
    const char *p = json;

    while ((p = strstr(p, "\"deviceId\"")) != NULL) {

        DeviceInfo dev;
        memset(&dev, 0, sizeof(dev));

        // ===== PARSE deviceId =====
        const char *colon = strchr(p, ':');
        colon++;
        skip_spaces(&colon);

        if (*colon != '"') { p++; continue; }
        colon++;

        const char *endQuote = strchr(colon, '"');
        int length = endQuote - colon;
        strncpy(dev.deviceId, colon, length);
        dev.deviceId[length] = '\0';

        // ===== GIỚI HẠN BLOCK CỦA THIẾT BỊ NÀY =====
        const char *blockEnd = strchr(endQuote, '}');  
        if (!blockEnd) break;

        // ===== PARSE connected trong block này =====
        const char *c = strstr(endQuote, "\"connected\"");
        if (c && c < blockEnd) {
            c = strchr(c, ':');
            c++;
            skip_spaces(&c);
            dev.connected = (strncmp(c, "true", 4) == 0);
        }

        out->list[out->count++] = dev;

        p = blockEnd;
    }
}

int main() {
    char json[20000];
    FILE *f = fopen("DB.json", "r");
    fread(json, 1, sizeof(json)-1, f);
    fclose(f);

    DeviceList list;
    parse_devices(json, &list);

    printf("=== CONNECTED DEVICES ===\n");
    for (int i = 0; i < list.count; i++)
        if (list.list[i].connected)
            printf(" - %s\n", list.list[i].deviceId);

    printf("\n=== NEW DEVICES (not connected) ===\n");
    for (int i = 0; i < list.count; i++)
        if (!list.list[i].connected)
            printf(" - %s\n", list.list[i].deviceId);

    return 0;
}

#include "core_process_t.h"
#include "helper.h"
#include "string.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include "time_t.h"
#include <mosquitto.h>
#include "define.h"

extern struct mosquitto * mosq;
extern uint8_t SERVICE_ID;
extern bool g_printLog;

void printInfo(const char* formatedString, ...) {
    if (g_printLog) {
        char* currentTime = get_localtime_now();
        va_list args;
        va_start(args, formatedString);
        int len = vsnprintf(NULL, 0, formatedString, args);
        if (len > 0) {
            char* message = (char*)malloc(len + 2);
            vsprintf(message, formatedString, args);
            printf(message);
            printf("\n\r");
            // Publish to CFG service to write to log files
            char topic[50];
            sprintf(topic, "LOG_REPORT/%d", SERVICE_ID);
            mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
            free(message);
        }
        va_end(args);
    }
}

void logInfo(const char* formatedString, ...) {
    if (g_printLog) {
        char* currentTime = get_localtime_now();
        va_list args;
        va_start(args, formatedString);
        int len = vsnprintf(NULL, 0, formatedString, args);
        if (len > 0) {
            char* message = (char*)malloc(len + 2);
            vsprintf(message, formatedString, args);
            char* fullLog = (char*)malloc(len + 50);    // Adding [INFO] and time
            sprintf(fullLog, "[INFO] [%s] %s", currentTime, message);
            if (StringLength(fullLog) < 4095) {
                printf(fullLog);
            }
            printf("\n\r");
            // Publish to CFG service to write to log files
            char topic[50];
            sprintf(topic, "LOG_REPORT/%d", SERVICE_ID);
            mosquitto_publish(mosq, NULL, topic, strlen(fullLog), fullLog, 0, false);
            free(fullLog);
            free(message);
        }
        va_end(args);
    }
}

void logError(const char* formatedString, ...) {
    char* currentTime = get_localtime_now();
    va_list args;
    va_start(args, formatedString);
    int len = vsnprintf(NULL, 0, formatedString, args);
    if (len > 0) {
        char* message = (char*)malloc(len + 2);
        vsprintf(message, formatedString, args);
        char* fullLog = (char*)malloc(len + 50);    // Adding [ERROR] and time
        sprintf(fullLog, "[ERROR][%s] %s", currentTime, message);
        if (StringLength(fullLog) < 4095) {
            printf(fullLog);
        }
        printf("\n\r");
        // Publish to CFG service to write to log files
        char topic[50];
        sprintf(topic, "LOG_REPORT/%d", SERVICE_ID);
        mosquitto_publish(mosq, NULL, topic, strlen(fullLog), fullLog, 0, false);
        free(fullLog);
        free(message);
    }
    va_end(args);
}

char* StringCopy(char* dest, const char* src) {
    if (dest != NULL && src != NULL) {
        return strcpy(dest, src);
    }
    return NULL;
}

char* StringAppend(char* dest, const char* src) {
    if (dest != NULL && src != NULL) {
        int len = StringLength(dest);
        char* d = strcpy((char*)(dest + len), src);
        return d;
    }
    return NULL;
}

bool StringCompare(const char* str1, const char* str2) {
    if (str1 && str2) {
        if (strcmp(str1, str2) == 0) {
            return true;
        }
    }
    return false;
}

bool StringContains(const char* mainString, const char* subString) {
    if (mainString && subString){
        char *p = strstr(mainString, subString);
        if (p != NULL) {
            return true;
        } else {
            return false;
        }
    }
    return false;
}


List* List_Create() {
    List* l = malloc(sizeof(List));
    l->count = 0;
    return l;
}

void List_Delete(List* l) {
    if (l) {
        for (int i = 0; i < l->count; i++) {
            if (l->items[i]) {
                free(l->items[i]);
            }
        }
        free(l);
    }
}

int List_PushString(List* l, const char* item) {
    char* newItem = malloc(strlen(item) + 1);
    StringCopy(newItem, item);
    l->items[l->count++] = newItem;
    return (l->count - 1);
}

int List_Push(List* l, const void* item, size_t size) {
    char* newItem = malloc(size);
    memcpy(newItem, item, size);
    l->items[l->count++] = newItem;
    return (l->count - 1);
}

void List_ToString(List* l, const char* separator, char* resultStr) {
    resultStr[0] = 0;
    for (size_t i = 0; i < l->count; i++) {
        StringAppend(resultStr, l->items[i]);
        if (separator && i < l->count - 1) {
            StringAppend(resultStr, separator);
        }
    }
}

List* String_Split(const char* str, const char* delim) {
    List* resultList = List_Create();
    if (str && delim) {
        // Copy to temp string before split because strtok will modify the original string
        char* tmpString = malloc(strlen(str) + 1);
        StringCopy(tmpString, str);
        // Split string
        char * token = strtok(tmpString, delim);
        while (token != NULL) {
            List_PushString(resultList, token);
            token = strtok(NULL, delim);
        }
        free(tmpString);
    }
    return resultList;
}


// char* JSON_GetFirstObjectName(JSON* obj) {
//     char* name = NULL;
//     JSON_ForEach(o, obj) {
//         if (cJSON_IsObject(o)) {
//             return o->string;
//         }
//     }
// }


void PlayAudio(const char* audioName) {
    char str[100];
    SPEAKER_ENABLE;
    sprintf(str, "aplay /home/szbaijie/audio/%s.wav", audioName);
    system(str);
    SPEAKER_DISABLE;
}

void ExecCommand(const char* command, char* output) {
    FILE* fp = popen(command, "r");
    if (output) {
        while (fgets(output, StringLength(output), fp) != NULL);
    }
    fclose(fp);
}

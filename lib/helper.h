#ifndef HELPER_H
#define HELPER_H

#include "parson.h"
#include "stdbool.h"
#include "gpio.h"

typedef struct {
    void* items[1000];
    size_t count;
} List;

#define SPEAKER_PIN          (PF + 2)
#define SPEAKER_ENABLE       pinWrite(SPEAKER_PIN, 0)
#define SPEAKER_DISABLE      pinWrite(SPEAKER_PIN, 1)

void logInfo(const char* formatedString, ...);
void logError(const char* formatedString, ...);
void printInfo(const char* formatedString, ...);

#define StringLength(str)   (str == NULL? 0 : strlen(str))
char* StringCopy(char* dest, const char* src);
char* StringAppend(char* dest, const char* src);
bool StringCompare(const char* str1, const char* str2);
bool StringContains(const char* mainString, const char* subString);

JSON_Object* JArray_FindStringValue(JSON_Array* array, const char* key, const char* value);
JSON_Object* JArray_FindNumberValue(JSON_Array* array, const char* key, double value);

List* List_Create();
void List_Delete(List* list);
int List_PushString(List* l, const char* item);
int List_Push(List* l, const void* item, size_t size);
void List_ToString(List* l, const char* separator, char* resultStr);
List* String_Split(const char* str, const char* delim);

// char* JSON_GetFirstObjectName(JSON* obj);

void PlayAudio(const char* audioName);
void ExecCommand(const char* command, char* output);

#endif   /* HELPER_H */

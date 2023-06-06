#ifndef HELPER_H
#define HELPER_H

#include "parson.h"
#include "stdbool.h"

typedef struct {
    void* items[1000];
    size_t count;
} list_t;

#define StringLength(str)   (str == NULL? 0 : strlen(str))
char* StringCopy(char* dest, const char* src);
bool StringCompare(const char* str1, const char* str2);
bool StringContains(const char* mainString, const char* subString);

JSON_Object* JArray_FindStringValue(JSON_Array* array, const char* key, const char* value);
JSON_Object* JArray_FindNumberValue(JSON_Array* array, const char* key, double value);

list_t* List_Create();
void List_Delete(list_t* list);
int List_PushString(list_t* l, const char* item);
int List_Push(list_t* l, const void* item, size_t size);
void List_ToString(list_t* l, const char* separator, char* resultStr);
list_t* String_Split(const char* str, const char* delim);

// char* JSON_GetFirstObjectName(JSON* obj);

void PlayAudio(const char* audioName);

#endif   /* HELPER_H */

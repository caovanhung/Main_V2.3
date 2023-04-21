#include "core_process_t.h"
#include "helper.h"
#include "string.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char* StringCopy(char* dest, const char* src) {
    if (dest != NULL && src != NULL) {
        return strcpy(dest, src);
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


JSON_Object* JArray_FindStringValue(JSON_Array* array, const char* key, const char* value) {
    int arrayCount = json_array_get_count(array);
    for (int i = 0; i < arrayCount; i++) {
        JSON_Object* arrayItem = json_array_get_object(array, i);
        if (isMatchString(value, json_object_get_string(arrayItem, key)))
        {
            return arrayItem;
        }
    }
    return NULL;
}


JSON_Object* JArray_FindNumberValue(JSON_Array* array, const char* key, double value) {
    int arrayCount = json_array_get_count(array);
    for (int i = 0; i < arrayCount; i++) {
        JSON_Object* arrayItem = json_array_get_object(array, i);
        if (value == json_object_get_number(arrayItem, key))
        {
            return arrayItem;
        }
    }
    return NULL;
}

list_t* List_Create() {
    list_t* l = malloc(sizeof(list_t));
    l->count = 0;
    return l;
}

void List_Delete(list_t* l) {
    if (l) {
        for (int i = 0; i < l->count; i++) {
            if (l->items[i]) {
                free(l->items[i]);
            }
        }
        free(l);
    }
}

int List_PushString(list_t* l, const char* item) {
    char* newItem = malloc(strlen(item) + 1);
    strcpy(newItem, item);
    l->items[l->count++] = newItem;
    return (l->count - 1);
}

int List_Push(list_t* l, const void* item, size_t size) {
    char* newItem = malloc(size);
    memcpy(newItem, item, size);
    l->items[l->count++] = newItem;
    return (l->count - 1);
}

void List_ToString(list_t* l, const char* separator, char* resultStr) {
    resultStr[0] = 0;
    for (size_t i = 0; i < l->count; i++) {
        strcat(resultStr, l->items[i]);
        if (separator && i < l->count - 1) {
            strcat(resultStr, separator);
        }
    }
}

list_t* String_Split(const char* str, const char* delim) {
    list_t* resultList = List_Create();
    if (str && delim) {
        // Copy to temp string before split because strtok will modify the original string
        char* tmpString = malloc(strlen(str) + 1);
        strcpy(tmpString, str);
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
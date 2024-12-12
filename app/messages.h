#ifndef MESSAGES_H
#define MESSAGES_H

#include "define.h"
#include "stdint.h"
#include "string.h"

typedef enum {
    LANG_VI,
    LANG_EN,
    LANGUAGE_COUNT      // Number of languages
} language_t;

typedef enum {
    MSG_ADD_GROUP_NORMAL,
    MSG_UPDATE_GROUP_NORMAL,
    MSG_ADD_GROUP_LINK,
    MSG_UPDATE_GROUP_LINK,
    MESSAGE_COUNT       // Number of messages
} msg_t;

typedef enum {
    MSG_TYPE_FAILED,
    MSG_TYPE_SUCCESS
} msg_type_t;


void setLanguage(language_t language);
const char* msgById(msg_t msgId);
const char* msgByReqType(int reqType);

#endif  // MESSAGES_H

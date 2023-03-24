#include "messages.h"

typedef struct {
    msg_type_t msgType;
    int reqType;
    int msgId;
} request_msg_t;

static language_t g_language = LANG_VI;

static char* g_messages[MESSAGE_COUNT][LANGUAGE_COUNT] = {
    { "TẠO NHÓM ĐÈN THÀNH CÔNG", "ADD LIGHT GROUP SUCCESSFULLY" },
    { "THIẾT BỊ LỖI", "FAILED DEVICES" },
    { "TẠO NHÓM CÔNG TẮC THÀNH CÔNG", "ADD SWITCH GROUP SUCCESSFULLY" },
    { "THIẾT BỊ LỖI", "FAILED DEVICES" },
};

static request_msg_t g_messageRequests[] = {
    { MSG_TYPE_SUCCESS, TYPE_ADD_GROUP_NORMAL, MSG_ADD_GROUP_SUCCESS },
    { MSG_TYPE_FAILED, TYPE_ADD_GROUP_NORMAL, MSG_ADD_GROUP_FAILED },
    { MSG_TYPE_SUCCESS, TYPE_ADD_GROUP_LINK, MSG_ADD_SOFT_LINK_SUCCESS },
    { MSG_TYPE_FAILED, TYPE_ADD_GROUP_LINK, MSG_ADD_SOFT_LINK_FAILED },
};


void setLanguage(language_t language) {
    if (language < LANGUAGE_COUNT) {
        g_language = language;
    }
}

const char* msg(msg_t msgId) {
    if (msgId < MESSAGE_COUNT) {
        return g_messages[msgId][g_language];
    }
}

const char* msgByReqType(int reqType, msg_type_t msgType) {
    int count = sizeof(g_messageRequests) / sizeof(request_msg_t);  // Number items of g_messageRequests
    for (int i = 0; i < count; i++) {
        if (g_messageRequests[i].reqType == reqType && g_messageRequests[i].msgType == msgType) {
            return msg(g_messageRequests[i].msgId);
        }
    }
    return NULL;
}
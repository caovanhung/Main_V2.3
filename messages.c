#include "messages.h"

typedef struct {
    // msg_type_t msgType;
    int reqType;
    int msgId;
} request_msg_t;

static language_t g_language = LANG_VI;

static char* g_messages[MESSAGE_COUNT][LANGUAGE_COUNT] = {
    { "TẠO NHÓM ĐÈN", "ADD LIGHT GROUP" },
    { "CHỈNH SỬA NHÓM ĐÈN", "UPDATE LIGHT GROUP" },
    { "TẠO NHÓM CÔNG TẮC", "ADD SWITCH GROUP" },
    { "CHỈNH SỬA NHÓM CÔNG TẮC", "UPDATE SWITCH GROUP" },
};

// static request_msg_t g_messageRequests[] = {
//     { MSG_TYPE_SUCCESS, TYPE_ADD_GROUP_NORMAL, MSG_ADD_GROUP_SUCCESS },
//     { MSG_TYPE_FAILED, TYPE_ADD_GROUP_NORMAL, MSG_ADD_GROUP_FAILED },
//     { MSG_TYPE_SUCCESS, TYPE_ADD_GROUP_LINK, MSG_ADD_SOFT_LINK_SUCCESS },
//     { MSG_TYPE_FAILED, TYPE_ADD_GROUP_LINK, MSG_ADD_SOFT_LINK_FAILED },
// };

static request_msg_t g_messageRequests[] = {
    { TYPE_ADD_GROUP_NORMAL, MSG_ADD_GROUP_NORMAL },
    { TYPE_UPDATE_GROUP_NORMAL, MSG_UPDATE_GROUP_NORMAL },
    { TYPE_ADD_GROUP_NORMAL, MSG_ADD_GROUP_LINK },
    { TYPE_UPDATE_GROUP_NORMAL, MSG_UPDATE_GROUP_LINK },
};


void setLanguage(language_t language) {
    if (language < LANGUAGE_COUNT) {
        g_language = language;
    }
}

const char* msgById(msg_t msgId) {
    if (msgId < MESSAGE_COUNT) {
        return g_messages[msgId][g_language];
    }
}

// const char* msgByReqType(int reqType, msg_type_t msgType) {
//     int count = sizeof(g_messageRequests) / sizeof(request_msg_t);  // Number items of g_messageRequests
//     for (int i = 0; i < count; i++) {
//         if (g_messageRequests[i].reqType == reqType && g_messageRequests[i].msgType == msgType) {
//             return msg(g_messageRequests[i].msgId);
//         }
//     }
//     return NULL;
// }

const char* msgByReqType(int reqType) {
    int count = sizeof(g_messageRequests) / sizeof(request_msg_t);  // Number items of g_messageRequests
    for (int i = 0; i < count; i++) {
        if (g_messageRequests[i].reqType == reqType) {
            return msgById(g_messageRequests[i].msgId);
        }
    }
    return NULL;
}
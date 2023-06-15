
#include <mosquitto.h>
#include "aws_mosquitto.h"
#include "time_t.h"
#include "helper.h"
#include "database.h"

extern const char* SERVICE_NAME;
char* g_dbgFileName;
int g_dbgLineNumber;
extern struct mosquitto * mosq;

bool sendPacketToFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, JSON* packet) {
    char* payload = cJSON_PrintUnformatted(packet);
    bool ret = sendToServiceFunc(mosq, serviceToSend, typeAction, payload, true);
    free(payload);
    return ret;
}

bool sendToServiceFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, const char* payload, bool printDebug) {
    bool ret = false;
    char* layerService;
    char topic[100];

    if (StringCompare(serviceToSend, SERVICE_AWS)) {
        layerService = MOSQ_LayerService_App;
    } else if (StringCompare(serviceToSend, SERVICE_CORE)) {
        layerService = MOSQ_LayerService_Core;
    } else if (StringCompare(serviceToSend, SERVICE_BLE)) {
        layerService = MOSQ_LayerService_Device;
    } else if (StringCompare(serviceToSend, SERVICE_TUYA)) {
        layerService = MOSQ_LayerService_Device;
    } else {
        layerService = "Unknown";
    }

    if (StringCompare(layerService, "Unknown")) {
        sprintf(topic, "%s/%d", serviceToSend, typeAction);
    } else {
        sprintf(topic, "%s/%s/%d", layerService, serviceToSend, typeAction);
    }

    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    // json_object_set_string(root_object, MOSQ_LayerService, layerService);
    json_object_set_string(root_object, MOSQ_NameService, SERVICE_NAME);
    json_object_set_number(root_object, MOSQ_ActionType, typeAction);
    json_object_set_number(root_object, MOSQ_TimeCreat, timeInMilliseconds());
    json_object_set_string(root_object, MOSQ_Payload, payload);

    char *message = json_serialize_to_string_pretty(root_value);
    int reponse = mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
    if (MOSQ_ERR_SUCCESS == reponse) {
        if (printDebug) {
            logInfo("Sent to service %s (topic %s), data: %s", serviceToSend, topic, message);
        }
        ret = true;
    } else {
        logInfo("Failed to publish to local topic: %s", topic);
    }
    json_free_serialized_string(message);
    json_value_free(root_value);
    return ret;
}

bool sendToServicePageIndexFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, int pageIndex, const char* payload) {
    bool ret = false;
    char* layerService;
    char topic[100];

    if (StringCompare(serviceToSend, SERVICE_AWS)) {
        layerService = MOSQ_LayerService_App;
    } else if (StringCompare(serviceToSend, SERVICE_CORE)) {
        layerService = MOSQ_LayerService_Core;
    } else if (StringCompare(serviceToSend, SERVICE_BLE)) {
        layerService = MOSQ_LayerService_Device;
    } else if (StringCompare(serviceToSend, SERVICE_TUYA)) {
        layerService = MOSQ_LayerService_Device;
    } else {
        layerService = "Unknown";
    }

    if (StringCompare(layerService, "Unknown")) {
        sprintf(topic, "%s/%d", serviceToSend, typeAction);
    } else {
        sprintf(topic, "%s/%s/%d", layerService, serviceToSend, typeAction);
    }

    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    // json_object_set_string(root_object, MOSQ_LayerService, layerService);
    json_object_set_string(root_object, MOSQ_NameService, SERVICE_NAME);
    json_object_set_number(root_object, MOSQ_ActionType, typeAction);
    json_object_set_number(root_object, MOSQ_TimeCreat, timeInMilliseconds());
    json_object_set_number(root_object, "pageIndex", pageIndex);
    json_object_set_string(root_object, MOSQ_Payload, payload);

    char *message = json_serialize_to_string_pretty(root_value);
    int reponse = mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
    if (MOSQ_ERR_SUCCESS == reponse) {
        logInfo("Sent to service %s (topic %s), data: %s", serviceToSend, topic, message);
        ret = true;
    } else {
        logInfo("Failed to publish to local topic: %s", topic);
    }
    json_free_serialized_string(message);
    json_value_free(root_value);
    return ret;
}



void Aws_updateGroupState(const char* groupAddr, int state)
{
    char payload[200];
    sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"%s\":%d}}}}", TYPE_UPDATE_GROUP_NORMAL, SENDER_HC_VIA_CLOUD, groupAddr, KEY_STATE, state);
    sendToService(SERVICE_AWS, GW_RESPONSE_ADD_GROUP_NORMAL, payload);
}

void Aws_updateGroupDevices(const char* groupAddr, const list_t* devices, const list_t* failedDevices) {
    int pageIndex = 1;
    char tmp[50];
    char* str = malloc((devices->count + failedDevices->count) * 50);
    JSON_Value* jsonValue = json_value_init_object();
    JSON_Object* obj = json_object(jsonValue);
    json_object_dotset_number(obj, "state.reported.type", TYPE_UPDATE_GROUP_NORMAL);
    json_object_dotset_number(obj, "state.reported.sender", SENDER_HC_VIA_CLOUD);
    if (devices->count > 0) {
        sprintf(tmp, "state.reported.%s.devices", groupAddr);
        List_ToString(devices, "|", str);
        json_object_dotset_string(obj, tmp, str);
        sprintf(tmp, "state.reported.%s.state", groupAddr);
        json_object_dotset_number(obj, tmp, AWS_STATUS_SUCCESS);
    } else {
        sprintf(tmp, "state.reported.%s.state", groupAddr);
        json_object_dotset_number(obj, tmp, AWS_STATUS_FAILED);
    }
    if (failedDevices->count > 0) {
        sprintf(tmp, "state.reported.%s.failed", groupAddr);
        List_ToString(failedDevices, "|", str);
        json_object_dotset_string(obj, tmp, str);
    }
    char* payload = json_serialize_to_string(jsonValue);
    sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_ADD_GROUP_NORMAL, pageIndex, payload);
    json_free_serialized_string(payload);
    free(str);
}


void sendNotiToUser(const char* message, bool isRealTime) {
    char* payload = malloc(strlen(message) + 200);
    if (isRealTime) {
        sprintf(payload, "{\"type\": %d, \"sender\":%d,\"data\": %s }", TYPE_REALTIME_STATUS_FB, SENDER_HC_VIA_CLOUD, message);
    } else {
        sprintf(payload, "{\"type\": %d, \"sender\":%d,\"message\": \"%s\" }", TYPE_NOTIFI_REPONSE, SENDER_HC_VIA_CLOUD, message);
    }
    sendToService(SERVICE_AWS, TYPE_NOTIFI_REPONSE, payload);
    free(payload);
}





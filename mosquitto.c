
#include <mosquitto.h>
#include "mosquitto.h"
#include "time_t.h"
#include "helper.h"


char* g_dbgFileName;
int g_dbgLineNumber;
extern struct mosquitto * mosq;

bool sendPacketToFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, JSON* packet) {
    char* payload = cJSON_PrintUnformatted(packet);
    bool ret = sendToServiceFunc(mosq, serviceToSend, typeAction, payload);
    free(payload);
    return ret;
}

bool sendToServiceFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, const char* payload) {
    bool ret = false;
    char* layerService;
    char topic[100];

    if (strcmp(serviceToSend, SERVICE_AWS) == 0) {
        layerService = MOSQ_LayerService_App;
    } else if (strcmp(serviceToSend, SERVICE_CORE) == 0) {
        layerService = MOSQ_LayerService_Core;
    } else if (strcmp(serviceToSend, SERVICE_BLE) == 0) {
        layerService = MOSQ_LayerService_Device;
    } else if (strcmp(serviceToSend, SERVICE_TUYA) == 0) {
        layerService = MOSQ_LayerService_Device;
    } else {
        layerService = "Unknown";
    }

    sprintf(topic, "%s/%s/%d", layerService, serviceToSend, typeAction);

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
        myLogInfo("Sent to service %s (topic %s), data: %s", serviceToSend, topic, message);
        ret = true;
    } else {
        myLogInfo("Failed to publish to local topic: %s", topic);
    }
    json_free_serialized_string(message);
    json_value_free(root_value);
    return ret;
}

bool sendToServicePageIndexFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, int pageIndex, const char* payload) {
    bool ret = false;
    char* layerService;
    char topic[100];

    if (strcmp(serviceToSend, SERVICE_AWS) == 0) {
        layerService = MOSQ_LayerService_App;
    } else if (strcmp(serviceToSend, SERVICE_CORE) == 0) {
        layerService = MOSQ_LayerService_Core;
    } else if (strcmp(serviceToSend, SERVICE_BLE) == 0) {
        layerService = MOSQ_LayerService_Device;
    } else if (strcmp(serviceToSend, SERVICE_TUYA) == 0) {
        layerService = MOSQ_LayerService_Device;
    } else {
        layerService = "Unknown";
    }

    sprintf(topic, "%s/%s/%d", layerService, serviceToSend, typeAction);

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
        myLogInfo("Sent to service %s (topic %s), data: %s", serviceToSend, topic, message);
        ret = true;
    } else {
        myLogInfo("Failed to publish to local topic: %s", topic);
    }
    json_free_serialized_string(message);
    json_value_free(root_value);
    return ret;
}


bool get_topic(char **result_topic,const char * layer_service,const char * service,int type_action,const char * extend)
{
    char *type_action_ = (char*)calloc(10,sizeof(char));
    Int2String(type_action,type_action_);
    if(layer_service != NULL && service != NULL && extend !=NULL)
    {
        *result_topic = malloc(1000);
        memset(*result_topic ,'\0',1000);
        strcpy(*result_topic,layer_service);
        strcat(*result_topic,"/");
        strcat(*result_topic,service);
        strcat(*result_topic,"/");
        strcat(*result_topic,type_action_);
        strcat(*result_topic,"/");
        strcat(*result_topic,extend);
        free(type_action_);
        return true;
    }
    else
    {
        free(type_action_);
        return false;
    }
}


bool getFormTranMOSQ(char **ResultTemplate,const char * layer_service,const char * service,int type_action,const char * extend,const char *Id,long long TimeCreat,const char* payload)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, MOSQ_LayerService,layer_service);
    json_object_set_string(root_object, MOSQ_NameService,service);
    json_object_set_number(root_object, MOSQ_ActionType,type_action);
    json_object_set_string(root_object, MOSQ_Extend,extend);

    json_object_set_string(root_object, MOSQ_Id,Id);
    json_object_set_number(root_object, MOSQ_TimeCreat,TimeCreat);
    

    json_object_set_string(root_object, MOSQ_Payload,payload);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


void Aws_DeleteDevice(const char* deviceId, int pageIndex) {
    ASSERT(deviceId);
    char payload[200];
    sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": null}}}", GW_RESPONSE_DEVICE_KICKOUT, SENDER_HC_VIA_CLOUD, deviceId);
    sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_DEVICE_KICKOUT, pageIndex, payload);
}

void Aws_SaveDeviceState(const char* deviceId, int state, int pageIndex) {
    ASSERT(deviceId);
    char payload[200];
    sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"state\":%d}}}}", TYPE_UPDATE_DEVICE, SENDER_HC_VIA_CLOUD, deviceId, state);
    sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_DEVICE_STATE, pageIndex, payload);
}

void Aws_SaveDpValue(const char* deviceId, int dpId, int value, int pageIndex) {
    ASSERT(deviceId);
    char payload[200];
    sprintf(payload,"{\"deviceId\":\"%s\", \"state\":2, \"dpId\":%d, \"dpValue\":%d}", deviceId, dpId, value);
    sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_DEVICE_CONTROL, pageIndex, payload);
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


void Ble_SetTTL(const char* deviceAddr, uint8_t ttl) {
    JSON* p = JSON_CreateObject();
    JSON_SetText(p, "deviceAddr", deviceAddr);
    JSON_SetNumber(p, "ttl", ttl);
    sendPacketTo(SERVICE_BLE, TYPE_SET_DEVICE_TTL, p);
    JSON_Delete(p);
}

void sendNotiToUser(const char* message) {
    char* payload = malloc(strlen(message) + 200);
    sprintf(payload, "{\"type\": %d, \"sender\":%d,\"%s\": \"%s\" }", TYPE_NOTIFI_REPONSE, SENDER_HC_VIA_CLOUD, KEY_MESSAGE, message);
    sendToService(SERVICE_AWS, TYPE_NOTIFI_REPONSE, payload);
    free(payload);
}


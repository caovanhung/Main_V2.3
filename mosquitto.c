
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

    if (strcmp(serviceToSend, SERVICE_CORE) == 0) {
        layerService = MOSQ_LayerService_Core;
    } else if (strcmp(serviceToSend, SERVICE_CMD) == 0) {
        layerService = MOSQ_LayerService_Core;
    } else if (strcmp(serviceToSend, SERVICE_BLE) == 0) {
        layerService = MOSQ_LayerService_Device;
    } else if (strcmp(serviceToSend, SERVICE_AWS) == 0) {
        layerService = MOSQ_LayerService_App;
    } else if (strcmp(serviceToSend, MOSQ_NameService_Support_Rule_Schedule) == 0) {
        layerService = MOSQ_LayerService_Support;
    } else if (strcmp(serviceToSend, MOSQ_NameService_Support_Rule_Schedule) == 0) {
        layerService = MOSQ_LayerService_Support;
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


bool get_inf_topic_mos(Info_topic_mosquitto* inf_topic_mos,char *topic)
{
    if(topic == NULL)
    {
        return false;
    }
    else
    {
        int size_device_inf = 0;
        char** str = str_split(topic,'/',&size_device_inf);

        if(size_device_inf > 0)
        {
            inf_topic_mos->layer_service = malloc(100);
            inf_topic_mos->name_service = malloc(100);
            inf_topic_mos->type_action = malloc(100);
            inf_topic_mos->extend = malloc(100);

            strcpy(inf_topic_mos->layer_service,*(str + 0));
            strcpy(inf_topic_mos->name_service,*(str + 1));
            strcpy(inf_topic_mos->type_action,*(str + 2));
            strcpy(inf_topic_mos->extend,*(str + 3));
            free_fields(str,size_device_inf);
        }
        return true;
    }
}

bool free_Info_topic_mosquitto(Info_topic_mosquitto* inf_topic_mos)
{
    if(inf_topic_mos->layer_service != NULL)
    {
        free(inf_topic_mos->layer_service);
    }
    if(inf_topic_mos->name_service != NULL)
    {
        free(inf_topic_mos->name_service);
    }
    if(inf_topic_mos->type_action != NULL)
    {
        free(inf_topic_mos->type_action);
    }
    if(inf_topic_mos->extend != NULL)
    {
        free(inf_topic_mos->extend);
    }
    free(inf_topic_mos);
    return true;
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

bool creatPayloadString(char** ResultTemplate,char *Key, char *Value)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    json_object_dotset_string(root_object, Key,Value);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool creatPayloadNumber(char** ResultTemplate,char *Key, int Value)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    json_object_dotset_number(root_object, Key,Value);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool replaceInfoFormTranMOSQ(char **ResultTemplate, char * NewLayerService,char * NewService,int NewTypeCction,char * NewExtend, char* payload)
{
    JSON_Value *root_value = NULL;
    char *serialized_string = NULL;

    root_value = json_parse_string(payload);

    json_object_set_string(json_object(root_value), MOSQ_LayerService,NewLayerService);
    json_object_set_string(json_object(root_value), MOSQ_NameService,NewService);
    json_object_set_number(json_object(root_value), MOSQ_ActionType,NewTypeCction);
    json_object_set_string(json_object(root_value), MOSQ_Extend,NewExtend);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool replaceValuePayloadTranMOSQ(char **ResultTemplate, char *Key,char *NewValue, char *payload)
{
    JSON_Value *root_value = NULL;
    char *serialized_string = NULL;

    root_value = json_parse_string(payload);


    json_object_dotset_string(json_object(root_value),Key,NewValue);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool insertObjectToPayloadTranMOSQ(char **ResultTemplate, char *Key,char *Value, char *payload)
{
    JSON_Value *root_value = NULL;
    char *serialized_string = NULL;

    root_value = json_parse_string(payload);

    json_object_dotset_value(json_object(root_value), Key,json_parse_string(Value));

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool insertStringToPayloadTranMOSQ(char **ResultTemplate, char *Key,char *Value, char *payload)
{
    JSON_Value *root_value = NULL;
    char *serialized_string = NULL;

    root_value = json_parse_string(payload);


    json_object_dotset_string(json_object(root_value),Key,Value);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool insertIntToPayloadTranMOSQ(char **ResultTemplate, char *Key,int Value, char *payload)
{
    JSON_Value *root_value = NULL;
    char *serialized_string = NULL;

    root_value = json_parse_string(payload);


    json_object_dotset_number(json_object(root_value),Key,Value);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
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

bool getPayloadReponseMOSQ(char *result,char type_reponse, char *deviceID,char *dpid,char *value,long long TimeCreat)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_number(root_object, MOSQ_TimeCreat,TimeCreat);
    json_object_set_number(root_object, TYPE_REPONSE,type_reponse);
    json_object_set_string(root_object, KEY_DEVICE_ID,deviceID);
    json_object_set_string(root_object, KEY_DP_ID,dpid);
    json_object_set_string(root_object, MOSQ_ResponseValue,value);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    strcpy(result, serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool getPayloadDeleteDeviceToBLE(char** ResultTemplate,char *deviceID)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_DEVICE_ID,deviceID);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true; 
}

bool getPayloadReponseAWS(char** ResultTemplate,int sender,int type, char *ID,int state)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    char *temp_str;
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_SENDER);
    json_object_dotset_number(root_object, temp_str,sender);

    free(temp_str);
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_TYPE);
    json_object_dotset_number(root_object, temp_str,type);

    free(temp_str);
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s.%s",KEY_STATE,KEY_REPORT,ID,KEY_STATE);   
    json_object_dotset_number(root_object, temp_str,state);
    free(temp_str);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool getPayloadReponseErrorAWS(char** ResultTemplate,int sender,int type, char *ID,char *failed)
{
    if(failed == NULL)
    {
        return false;
    }
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;
    char *temp_str;
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_SENDER);
    json_object_dotset_number(root_object, temp_str,sender);

    free(temp_str);
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_TYPE);
    json_object_dotset_number(root_object, temp_str,type);

    free(temp_str);
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_GROUPS,ID,KEY_FAILED);   
    json_object_dotset_string(root_object, temp_str,failed);
    free(temp_str);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool getPayloadReponseDeleteDeviceAWS(char** result,char *deviceID)
{
    char *serialized_string = malloc(1000);
    sprintf(serialized_string,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": null }}}",TYPE_DEL_DEVICE,SENDER_HC_VIA_CLOUD,deviceID);
    printf("serialized_string %s\n",serialized_string );
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    free(serialized_string);
    return true;    
}

bool getPayloadReponseDeleteGroupAWS(char** result,char *groupAddress)
{
    char *serialized_string = malloc(1000);
    sprintf(serialized_string,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\":{\"%s\": null }}}}",TYPE_DEL_GROUP_LINK,SENDER_HC_VIA_CLOUD,KEY_GROUPS,groupAddress);
    printf("serialized_string %s\n",serialized_string );
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    free(serialized_string);
    return true;      
}

bool getPayloadReponseDeleteSceneAWS(char** result,char *sceneID)
{
    char *serialized_string = malloc(1000);
    sprintf(serialized_string,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": null }}}",TYPE_DEL_SCENE,SENDER_HC_VIA_CLOUD,sceneID);
    printf("serialized_string %s\n",serialized_string );
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    free(serialized_string);
    return true;
}

char* getPayloadReponseStateDeviceAWS(char *deviceID, int  state)
{
    char *serialized_string = malloc(1000);
    sprintf(serialized_string,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"%s\":%d}}}}",TYPE_UPDATE_DEVICE,SENDER_HC_VIA_CLOUD,deviceID,KEY_STATE,state);
    int size_t = strlen(serialized_string);
    char* result = malloc(size_t+1);
    memset(result,'\0',size_t+1);
    strcpy(result, serialized_string);
    free(serialized_string);
    return result;
}

bool getPayloadReponseValueSceneAWS(char** result,int sender,int type,char *value)
{

    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;
    char *temp_str;
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_SENDER);
    json_object_dotset_number(root_object, temp_str,sender);

    free(temp_str);
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s.%s",KEY_STATE,KEY_REPORT,KEY_TYPE);
    json_object_dotset_number(root_object, temp_str,type);

    free(temp_str);
    temp_str = calloc(100,sizeof(char));
    sprintf(temp_str,"%s.%s",KEY_STATE,KEY_REPORT);   
    json_object_dotset_value(root_object, temp_str,json_parse_string(value));
    free(temp_str);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    free(serialized_string);
    return true;       
}

void Aws_updateGroupState(const char* groupAddr, int state)
{
    char payload[200];
    sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"groups\":{\"%s\": {\"%s\":%d}}}}}", TYPE_UPDATE_GROUP_NORMAL, SENDER_HC_VIA_CLOUD, groupAddr, KEY_STATE, state);
    sendToService(SERVICE_AWS, GW_RESPONSE_ADD_GROUP_NORMAL, payload);
}

void Aws_updateGroupDevices(const char* groupAddr, const list_t* devices, const list_t* failedDevices) {
    char tmp[50];
    char str[3000];
    // char notification[3000];
    JSON_Value* jsonValue = json_value_init_object();
    JSON_Object* obj = json_object(jsonValue);
    json_object_dotset_number(obj, "state.reported.type", TYPE_UPDATE_GROUP_NORMAL);
    json_object_dotset_number(obj, "state.reported.sender", SENDER_HC_VIA_CLOUD);
    if (devices->count > 0) {
        sprintf(tmp, "state.reported.groups.%s.devices", groupAddr);
        List_ToString(devices, "|", str);
        json_object_dotset_string(obj, tmp, str);
        sprintf(tmp, "state.reported.groups.%s.state", groupAddr);
        json_object_dotset_number(obj, tmp, AWS_STATUS_SUCCESS);
    } else {
        sprintf(tmp, "state.reported.groups.%s.state", groupAddr);
        json_object_dotset_number(obj, tmp, AWS_STATUS_FAILED);
    }
    if (failedDevices->count > 0) {
        sprintf(tmp, "state.reported.groups.%s.failed", groupAddr);
        List_ToString(failedDevices, "|", str);
        json_object_dotset_string(obj, tmp, str);
        // sprintf(notification, "%s: %s", MESSAGE_DEVICE_ERROR, str);
        // sendNotiToUser(notification);
    }
    char* payload = json_serialize_to_string(jsonValue);
    sendToService(SERVICE_AWS, GW_RESPONSE_ADD_GROUP_NORMAL, payload);
    json_free_serialized_string(payload);
}

bool getPayloadReponseDevicesGroupAWS(char** result,char *deviceID,char*  devices)
{
    if(devices == NULL)
    {
        return false;
    }
    char *serialized_string = malloc(1000);
    sprintf(serialized_string,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"groups\":{\"%s\": {\"%s\":\"%s\"}}}}}",TYPE_UPDATE_GROUP_NORMAL,SENDER_HC_VIA_CLOUD,deviceID,DEVICES_INF,devices);
    printf("serialized_string %s\n",serialized_string );
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    free(serialized_string);
    return true;    
}

void sendNotiToUser(const char* message) {
    char payload[200];
    sprintf(payload, "{\"type\": %d, \"sender\":%d,\"%s\": \"%s\" }", TYPE_NOTIFI_REPONSE, SENDER_HC_VIA_CLOUD, KEY_MESSAGE, message);
    sendToService(SERVICE_AWS, TYPE_NOTIFI_REPONSE, payload);
}


bool insertObjectReponseMOSQ(char **ResultTemplate,long long TimeCreat,char *deviceID, char *dictDPs)
{
    JSON_Value *root_value = json_parse_string(*ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    char *temp = malloc(50);
    memset(temp,'\0',50*sizeof(char));
    sprintf(temp,"%lld.%s",TimeCreat,KEY_DEVICE_ID);
    json_object_dotset_string(root_object, temp, deviceID);

    memset(temp,'\0',50*sizeof(char));
    sprintf(temp,"%lld.%s",TimeCreat,KEY_DICT_DPS);
    json_object_dotset_value(root_object, temp,json_parse_string(dictDPs));

    free(temp);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;

}

bool removeObjectReponseMOSQ(char **ResultTemplate,long long TimeCreat)
{
    JSON_Value *root_value = json_parse_string(*ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    char *temp = malloc(20);
    memset(temp,'\0',20*sizeof(char));
    sprintf(temp,"%lld",TimeCreat);
    json_object_remove(root_object, temp);
    free(temp);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);    
    return true;
}

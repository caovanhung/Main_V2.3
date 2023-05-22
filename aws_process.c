#include "aws_process.h"
#include "time_t.h"


static char* g_homeId;
static char* g_thingId;

void getHcInformation() {
    myLogInfo("Getting HC parameters");
    FILE* f = fopen("app.json", "r");
    char buff[1000];
    fread(buff, sizeof(char), 1000, f);
    fclose(f);
    JSON* setting = JSON_Parse(buff);
    char* homeId = JSON_GetText(setting, "homeId");
    char* thingId = JSON_GetText(setting, "thingId");
    free(g_homeId);
    free(g_thingId);
    g_homeId = malloc(StringLength(homeId));
    g_thingId = malloc(StringLength(thingId));
    StringCopy(g_homeId, homeId);
    StringCopy(g_thingId, thingId);
    myLogInfo("HomeId: %s, ThingId: %s", g_homeId, g_thingId);
}

char* Aws_GetTopic(AwsPageType pageType, int pageIndex, AwsTopicType topicType) {
    char* topicTypeStr = NULL, *pageTypeStr = NULL, *topic = NULL;

    if (pageType == PAGE_MAIN) {
        pageTypeStr = "accountInfo";
    } else if (pageType == PAGE_ANY) {
        pageTypeStr = "+";
    } else if (pageType == PAGE_DEVICE) {
        pageTypeStr = "d";
    } else if (pageType == PAGE_SCENE) {
        pageTypeStr = "s";
    } else if (pageType == PAGE_GROUP) {
        pageTypeStr = "g";
    } else {
        pageTypeStr = "";
    }

    if (topicType == TOPIC_GET_PUB) {
        topicTypeStr = "get";
    } else if (topicType == TOPIC_GET_SUB) {
        topicTypeStr = "get/accepted";
    } else if (topicType == TOPIC_UPD_PUB) {
        topicTypeStr = "update";
    } else if (topicType == TOPIC_UPD_SUB) {
        topicTypeStr = "update/accepted";
    } else if (topicType == TOPIC_NOTI_PUB || topicType == TOPIC_NOTI_SUB) {
        topicTypeStr = "notify";
    } else if (topicType == TOPIC_REJECT) {
        topicTypeStr = "reject";
    }

    if (topicTypeStr && pageTypeStr) {
        topic = (char*)malloc(strlen(g_thingId) + strlen(g_homeId) + strlen(topicTypeStr) + 50);
        if (pageType == PAGE_MAIN || pageType == PAGE_ANY) {
            sprintf(topic, "$aws/things/%s/shadow/name/%s/%s", g_thingId, pageTypeStr, topicTypeStr);
        } else if (pageType == PAGE_NONE) {
            sprintf(topic, "$aws/things/%s/shadow/name/%s", g_thingId, topicTypeStr);
        } else {
            sprintf(topic, "$aws/things/%s/shadow/name/%s_%d/%s", g_thingId, pageTypeStr, pageIndex, topicTypeStr);
        }
    }
    return topic;
}

/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_short_message_received(char *message)
{
    JSON_Value *schema = NULL;
    if(message != NULL)
    {
        schema = json_parse_string(message);
        json_object_remove(json_object(schema),"metadata");
        json_object_remove(json_object(schema),"version");
        json_object_remove(json_object(schema),"timestamp");
        memset(message,'\0',MAX_SIZE_ELEMENT_QUEUE);
        char* str = json_serialize_to_string(schema);
        strcpy(message, str);
        free(str);
        json_value_free(schema);
        return true;
    }
    else
    {
        return false;
    }
}


/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_pre_detect_message_received(Pre_parse *var,char *mess)
{
    JSON_Value *schema = NULL;
    const char *temp = NULL;
    JSON_Object *object = NULL;
    int i = 0;

    if(mess!= NULL)
    {
        schema = json_parse_string(mess);
        var->JS_value = schema;
        int size_chema = json_object_get_count(json_object(schema));
        for(i = 0;i<size_chema;i++)
        {
            temp = json_object_get_name(json_object(schema),i);
            if(isMatchString(temp,KEY_STATE))
            {
                object =  json_object_get_object(json_object(schema),temp);
                temp = json_object_get_name(object,0);
                //check value into reported
                if(isMatchString(temp,KEY_REPORT))
                {
                    object =  json_object_get_object(object,KEY_REPORT);
                    int size_reported = json_object_get_count(object);
                    //check object have "type"?
                    if(!json_object_has_value(object,KEY_TYPE))
                    {
                        return false;
                    }

                    //get value into struct
                    for(i = 0; i<size_reported; i++)
                    {
                        temp = json_object_get_name(object,i);
                        if(isMatchString(temp,KEY_TYPE))
                        {
                            var->type = (int)json_object_get_number(object,KEY_TYPE);
                        }
                        if(isMatchString(temp,KEY_STATE))
                        {
                            var->state = (int)json_object_get_number(object,KEY_STATE);
                        }
                        else if(isMatchString(temp,KEY_SENDER))
                        {
                            var->sender = json_object_get_number(object,KEY_SENDER);
                        }
                        else if(isMatchString(temp,KEY_PROVIDER))
                        {
                            var->provider = (int)json_object_get_number(object,KEY_PROVIDER);
                            // Ignore packets for wifi devices except TYPE_ADD_DEVICE and TYPE_DEL_DEVICE
                            if (var->provider != 2 && (var->type != TYPE_ADD_DEVICE || var->type != TYPE_DEL_DEVICE)) {
                                return false;
                            }
                        }
                        else if(isMatchString(temp,KEY_PAGE_INDEX))
                        {
                            var->pageIndex = (int)json_object_get_number(object,KEY_PAGE_INDEX);
                        }
                        else if(isMatchString(temp,KEY_LOOP_PRECONDITION))
                        {
                            var->loops = (int)json_object_get_number(object,KEY_LOOP_PRECONDITION);
                        }
                        else if(isMatchString(temp,KEY_DELAY))
                        {
                            var->delay = (int)json_object_get_number(object,KEY_DELAY);
                        }
                        else if(isMatchString(temp,KEY_SETTING))
                        {
                            return false;
                        }
                        else if(isMatchString(temp,KEY_SENDER_ID))
                        {
                            var->senderId = (char *)json_object_get_string(object,KEY_SENDER_ID);
                        }
                        else if(!isMatchString(temp,KEY_TYPE) && !isMatchString(temp,KEY_SENDER) && !isMatchString(temp,KEY_SENDER_ID) && !isMatchString(temp,KEY_SETTING) && !isMatchString(temp,KEY_PAGE_INDEX) && !isMatchString(temp,"maxGroupAddress") && !isMatchString(temp,"maxSceneAddress"))
                        {
                            var->object = (char *)temp;
                            var->JS_object = json_object_get_object(object,temp);
                            // myLogInfo(json_serialize_to_string(var->JS_object));
                        }

                        if (var->pageIndex == 0) {
                            var->pageIndex = 1;
                        }
                    }
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    else
    {
        return false;
    }
    return true;
}

/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_detect_message_received_for_update(Pre_parse *var,char *mess)
{
    JSON_Value *schema = NULL;

    if(mess!= NULL)
    {
        schema = json_parse_string(mess);
        var->type = (int)json_object_get_number(json_object(schema),KEY_TYPE);
        var->sender = (int)json_object_get_number(json_object(schema),KEY_SENDER);
    }
    else
    {
        return false;
    }
    return true;
}

/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_get_info_device(Info_device *inf_device,Pre_parse *pre_detect)
{
    char *device_inf =  (char*)json_object_get_string(pre_detect->JS_object, DEVICES_INF);

    if(pre_detect->object == NULL || device_inf == NULL)
    {
        return false;
    }
    inf_device->deviceID        = pre_detect->object;
    inf_device->IDgateway       = (char*)json_object_get_string(pre_detect->JS_object, "gateWay");
    inf_device->pageIndex       = pre_detect->pageIndex;
    inf_device->name            = (char*)json_object_get_string(pre_detect->JS_object, KEY_NAME);
    inf_device->created         = 11111;
    inf_device->modified        = 11111;
    inf_device->firmware        = "1.0";
    inf_device->last_updated    = 11111;
    inf_device->State           = 1;
    inf_device->Service         = "Export";

    char *device_inf_ = malloc(500);
    strcpy(device_inf_, device_inf);

    inf_device->provider = String2Int(strtok(device_inf_, "|"));
    printf("provider %d\n",inf_device->provider);
    if(inf_device->provider  == HOMEGY_BLE)
    {
        int size_device_inf = 0;
        char** str = str_split(device_inf,'|',&size_device_inf);
        inf_device->pid       =   *(str + 1);
        inf_device->MAC       =   *(str + 2);
        inf_device->Unicast   =   *(str + 3);
        inf_device->deviceKey =   *(str + 4);

        inf_device->dictMeta =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_META));
        inf_device->dictDPs  =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_DPS));
        inf_device->dictName =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_NAME));
    }
    else if(inf_device->provider == HOMEGY_WIFI)
    {
        inf_device->pid       =   NULL;
        inf_device->MAC       =   NULL;
        inf_device->Unicast   =   NULL;
        inf_device->deviceKey =   NULL;

        inf_device->dictMeta =  NULL;

        inf_device->dictDPs  =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_DPS));
        inf_device->dictName =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_NAME));
    }
    else if(inf_device->provider == HANET_CAMERA)
    {
        inf_device->pid = CAM_HANET;
        inf_device->MAC       =   NULL;
        inf_device->Unicast   =   NULL;
        inf_device->deviceKey =   NULL;
        inf_device->dictMeta =  NULL;
        inf_device->dictName =  NULL;
        inf_device->dictDPs  =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_DPS));
    }
    // free(device_inf_);
    return true;
}

bool MOSQ_getTemplateAddDevice(char **result,Info_device *inf_device)
{
    JSON_Value *root_value = json_value_init_object();

    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    json_object_set_string(root_object, "deviceID",inf_device->deviceID);
    json_object_dotset_number(root_object, "provider",inf_device->provider);
    json_object_set_string(root_object, "name",inf_device->name);
    json_object_set_string(root_object, "Service",inf_device->Service);
    json_object_set_number(root_object, "created", 234565665);
    json_object_set_number(root_object, "modified", 1111);
    json_object_set_string(root_object, "firmware", "1.0");
    json_object_set_number(root_object, "last_updated", 2222);
    json_object_set_number(root_object, "state", TYPE_DEVICE_ONLINE);
    json_object_set_number(root_object, "pageIndex", inf_device->pageIndex);


    json_object_dotset_number(root_object, "protocol_para.provider",    inf_device->provider );
    json_object_dotset_string(root_object, "protocol_para.pid",         inf_device->pid );
    json_object_dotset_string(root_object, "protocol_para.MAC",         inf_device->MAC );
    json_object_dotset_string(root_object, "protocol_para.Unicast",     inf_device->Unicast);
    json_object_dotset_string(root_object, "protocol_para.IDgateway",   inf_device->IDgateway);
    json_object_dotset_string(root_object, "protocol_para.deviceKey",   inf_device->deviceKey);
    json_object_dotset_value(root_object, "protocol_para.dictMeta",json_parse_string(inf_device->dictMeta));
    json_object_dotset_value(root_object, "protocol_para.dictDPs",json_parse_string(inf_device->dictDPs));
    json_object_dotset_value(root_object, "protocol_para.dictName",json_parse_string(inf_device->dictName));

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_getInfoScene(Info_scene *inf_scene,Pre_parse *pre_detect)
{
    inf_scene->sceneId              = pre_detect->object;
    inf_scene->name                 = (char*)json_object_get_string(pre_detect->JS_object, KEY_NAME);
    inf_scene->sceneState           = 1;
    inf_scene->sceneControl         = 0;
    char *scene_inf =  (char*)json_object_get_string(pre_detect->JS_object, SCENE_INF);
    char sceneType_[3];
    memset(sceneType_, '\0', sizeof(sceneType_));
    strncpy(sceneType_, scene_inf, 1);
    if(isMatchString(sceneType_,"0"))
    {
        inf_scene->sceneType = "0";
    }
    else if(isMatchString(sceneType_,"1"))
    {
        inf_scene->sceneType = "1";
    }
    else if(isMatchString(sceneType_,"2"))
    {
        inf_scene->sceneType = "2";
    }
    inf_scene->isLocal      =  json_object_get_boolean(pre_detect->JS_object, KEY_IS_LOCAL);
    inf_scene->actions      =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_ACTIONS));
    inf_scene->conditions   =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_CONDITIONS));
    // if(inf_scene->isLocal)
    // {
    //  if(inf_scene->conditions ==NULL)
    //  {
    //      return false;
    //  }
    //  else
    //      return true;
    // }
    return true;
}

bool MOSQ_getTemplateAddScene(char **result,Info_scene *inf_scene)
{
    JSON_Value *root_value = json_value_init_object();

    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, "Id",inf_scene->sceneId);
    json_object_set_string(root_object, "name",inf_scene->name);
    json_object_set_number(root_object, "state",inf_scene->sceneState);
    json_object_set_number (root_object,    "isLocal",inf_scene->isLocal);
    json_object_set_string(root_object, "sceneType",inf_scene->sceneType);
    json_object_set_value (root_object, "actions",json_parse_string(inf_scene->actions));
    json_object_set_value (root_object, "conditions",json_parse_string(inf_scene->conditions));


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool MOSQ_getTemplateDeleteScene(char **result,const char* sceneId)
{
    JSON_Value *root_value = json_value_init_object();

    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, "Id",sceneId);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}
/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_getInfoGateway(InfoProvisonGateway *InfoProvisonGateway_t,Pre_parse *pre_detect)
{
    JSON_Object *object = NULL;
    char *add_GW = (char *)json_object_get_name(pre_detect->JS_object,0);
    object =  json_object_get_object(pre_detect->JS_object,add_GW);

    InfoProvisonGateway_t->appkey = json_object_get_string(object, KEY_APP_KEY);
    InfoProvisonGateway_t->ivIndex = json_object_get_string(object, KEY_IV_INDEX);
    InfoProvisonGateway_t->netkeyIndex = json_object_get_string(object, KEY_NETKEY_INDEX);
    InfoProvisonGateway_t->netkey = json_object_get_string(object, KEY_NETKEY);
    InfoProvisonGateway_t->appkeyIndex = json_object_get_string(object, KEY_APP_KEY_INDEX);
    InfoProvisonGateway_t->deviceKey = json_object_get_string(object, KEY_DEVICE_KEY);
    InfoProvisonGateway_t->address1 = json_object_get_string(object, "gateway1");
    InfoProvisonGateway_t->address2 = json_object_get_string(object, "gateway2");
    return true;
}

bool MOSQ_getTemplateAddGateway(char **result,InfoProvisonGateway *InfoProvisonGateway_t)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_APP_KEY,InfoProvisonGateway_t->appkey);
    json_object_set_string(root_object, KEY_IV_INDEX,InfoProvisonGateway_t->ivIndex);
    json_object_set_string(root_object, KEY_NETKEY_INDEX,InfoProvisonGateway_t->netkeyIndex);
    json_object_set_string(root_object, KEY_NETKEY,InfoProvisonGateway_t->netkey);
    json_object_set_string(root_object, KEY_APP_KEY_INDEX,InfoProvisonGateway_t->appkeyIndex);
    json_object_set_string(root_object, KEY_DEVICE_KEY,InfoProvisonGateway_t->deviceKey);
    json_object_set_string(root_object, "address1", InfoProvisonGateway_t->address1);
    json_object_set_string(root_object, "address2", InfoProvisonGateway_t->address2);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool AWS_getInfoControlSecene(Info_scene *inf_scene,Pre_parse *pre_detect)
{
    inf_scene->sceneId = pre_detect->object;
    inf_scene->sceneControl = json_object_get_boolean(pre_detect->JS_object, KEY_STATE);
    return true;
}

bool MOSQ_getTemplateControSecene(char **result,Info_scene *inf_scene)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_ID_SCENE,inf_scene->sceneId);
    json_object_set_number(root_object, KEY_STATE,inf_scene->sceneControl);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_getInfoControlDevice(Info_device *inf_device,Pre_parse *pre_detect)
{
    inf_device->deviceID = pre_detect->object;
    inf_device->provider = pre_detect->provider;
    inf_device->senderId = pre_detect->senderId;
    inf_device->dictDPs  =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_DPS));
    return true;
}

bool MOSQ_getTemplateControlDevice(char **result,Info_device *inf_device)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_DEVICE_ID,inf_device->deviceID);
    json_object_set_string(root_object, "senderId", inf_device->senderId);
    json_object_set_number(root_object, KEY_PROVIDER,inf_device->provider);
    json_object_set_value(root_object, KEY_DICT_DPS,json_parse_string(inf_device->dictDPs));



    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool AWS_getInfoDimLedDevice(Info_device *inf_device,Pre_parse *pre_detect)
{
    inf_device->deviceID = pre_detect->object;
    inf_device->provider = pre_detect->provider;
    inf_device->ledDim  =  json_object_get_number(pre_detect->JS_object, KEY_LED);
    return true;
}

bool MOSQ_getTemplateDimLedDevice(char **result,Info_device *inf_device)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;


    json_object_set_string(root_object, KEY_DEVICE_ID,inf_device->deviceID);
    json_object_set_number(root_object, KEY_PROVIDER,inf_device->provider);
    json_object_set_number(root_object, KEY_LED,inf_device->ledDim);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool AWS_getInfoLockDevice(Info_device *inf_device,Pre_parse *pre_detect)
{
    inf_device->deviceID = pre_detect->object;
    inf_device->provider = pre_detect->provider;
    inf_device->lock   =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_LOCK));
    return true;
}

bool MOSQ_getTemplateLockDevice(char **result,Info_device *inf_device)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;


    json_object_set_string(root_object, KEY_DEVICE_ID,inf_device->deviceID);
    json_object_set_number(root_object, KEY_PROVIDER,inf_device->provider);
    json_object_set_value(root_object,  KEY_LOCK,json_parse_string(inf_device->lock));


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


bool MOSQ_getTemplateUpdateService(char **result,Pre_parse *pre_detect)
{
    if(pre_detect->object == NULL)
    {
        return false;
    }
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;


    json_object_set_string(root_object, KEY_MESSAGE,json_object_get_string(pre_detect->JS_object,KEY_MESSAGE));
    json_object_set_number(root_object, KEY_STATE,TYPE_DEVICE_REPONSE_ADD_FROM_APP);



    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}



bool AWS_getInfoControlDevice_DEBUG(Info_device_Debug *inf_device_Debug,Pre_parse *pre_detect)
{
    inf_device_Debug->deviceID  = pre_detect->object;
    inf_device_Debug->provider  = pre_detect->provider;
    inf_device_Debug->loops     = pre_detect->loops;
    inf_device_Debug->delay     = pre_detect->delay;
    inf_device_Debug->dictDPs   =  (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_DPS));
    return true;
}

bool MOSQ_getTemplateControlDevice_DEBUG(char **result,Info_device_Debug *inf_device_Debug)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_DEVICE_ID,inf_device_Debug->deviceID);
    json_object_set_number(root_object, KEY_PROVIDER,inf_device_Debug->provider);
    json_object_set_value(root_object, KEY_DICT_DPS,json_parse_string(inf_device_Debug->dictDPs));
    json_object_set_number(root_object, KEY_LOOP_PRECONDITION,inf_device_Debug->loops);
    json_object_set_number(root_object, KEY_DELAY,inf_device_Debug->delay);



    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_getInfoDeleteDevice(Info_device *inf_device,Pre_parse *pre_detect)
{
    inf_device->deviceID    = pre_detect->object;
    inf_device->State       =  TYPE_DEVICE_RESETED;
    return true;
}

bool MOSQ_getTemplateDeleteDevice(char **result,Info_device *inf_device)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_DEVICE_ID,inf_device->deviceID);
    json_object_set_number(root_object, KEY_STATE,inf_device->State);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_getInfoAddGroupNormal(Info_group *info_group_t,Pre_parse *pre_detect)
{
    info_group_t->name      =   json_object_get_string(pre_detect->JS_object, KEY_NAME);
    info_group_t->groupID   =   pre_detect->object;
    info_group_t->devices   =   json_object_get_string(pre_detect->JS_object, KEY_DEVICES_GROUP);
    info_group_t->pid       =   json_object_get_string(pre_detect->JS_object, KEY_PID);
    info_group_t->state     =   json_object_get_number(pre_detect->JS_object, KEY_STATE);
    return true;
}

bool MOSQ_getTemplateAddGroupNormal(char **result,Info_group *info_group_t)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_NAME,info_group_t->name);
    json_object_set_string(root_object, KEY_DEVICES_GROUP,info_group_t->devices);
    json_object_set_string(root_object, KEY_PID,info_group_t->pid);
    json_object_set_string(root_object, KEY_ADDRESS_GROUP,info_group_t->groupID);
    json_object_set_number(root_object, KEY_STATE,info_group_t->state);



    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool AWS_getInfoControlGroupNormal(Info_group *info_group_t,Pre_parse *pre_detect)
{
    JSON_Object *object = NULL;
    char *AddGroup = (char *)json_object_get_name(pre_detect->JS_object,0);
    object =  json_object_get_object(pre_detect->JS_object,AddGroup);

    info_group_t->groupID       =   pre_detect->object;
    info_group_t->provider      =   pre_detect->provider;
    info_group_t->senderId      =   pre_detect->senderId;
    info_group_t->dictDPs       =   (char*)json_serialize_to_string_pretty(json_object_get_value(pre_detect->JS_object, KEY_DICT_DPS));
    return true;
}

bool MOSQ_getTemplateControlGroupNormal(char **result,Info_group *info_group_t)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_ADDRESS_GROUP,info_group_t->groupID);
    json_object_set_number(root_object, KEY_PROVIDER,info_group_t->provider);
    json_object_set_string(root_object, "senderId", info_group_t->senderId);
    json_object_set_value(root_object, KEY_DICT_DPS,json_parse_string(info_group_t->dictDPs));


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

/*
Note: Have optimite *result = malloc(max_size_message_received);
*/
bool AWS_getInfoDeleteGroupNormal(Info_group *info_group_t,Pre_parse *pre_detect)
{
    info_group_t->groupID   =   pre_detect->object;
    info_group_t->state     =   TYPE_DEVICE_RESETED;
    return true;
}

bool MOSQ_getTemplateDeleteGroupNormal(char **result,Info_group *info_group_t)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    json_object_set_string(root_object, KEY_ADDRESS_GROUP,info_group_t->groupID);
    json_object_set_number(root_object, KEY_STATE,info_group_t->state);


    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}


JSON* Aws_CreateCloudPacket(JSON* localPacket)
{
    char packetStr[1000];
    char* deviceId = JSON_GetText(localPacket, "deviceId");
    int dpId = JSON_GetNumber(localPacket, "dpId");
    double dpValue = JSON_GetNumber(localPacket, "dpValue");
    sprintf(packetStr,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"%s\":{\"%d\": %f},\"state\":%d }}}}", TYPE_UPDATE_DEVICE, SENDER_HC_VIA_CLOUD, deviceId, KEY_DICT_DPS, dpId, dpValue, TYPE_DEVICE_ONLINE);
    return JSON_Parse(packetStr);
}



#include "aws_process.h"
#include "time_t.h"
#include "helper.h"
#include "aws_mosquitto.h"

char* g_homeId;
static char* g_thingId;

void getHcInformation() {
    logInfo("Getting HC parameters");
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
    logInfo("HomeId: %s, ThingId: %s", g_homeId, g_thingId);
    JSON_Delete(setting);
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

JSON* Aws_GetShadow(const char* thingName, const char* shadowName) {
    char result[100000];
    char request[500];
    char url[200];
    sprintf(url, "https://a2376tec8bakos-ats.iot.ap-southeast-1.amazonaws.com:8443/things/%s/shadow?name=%s", thingName, shadowName);
    char* certName = "c8f9a13dc7c253251b9e250439897bc010f501edd780348ecc1c2e91add22237";
    sprintf(request, "curl --connect-timeout 10 --tlsv1.2 --cacert /usr/bin/AmazonRootCA1.pem --cert /usr/bin/%s-certificate.pem.crt --key /usr/bin/%s-private.pem.key  %s", certName, certName, url);
    printInfo(request);
    printInfo("\n");
    FILE* fp = popen(request, "r");
    while (fgets(result, sizeof(result), fp) != NULL);
    fclose(fp);
    if (result) {
        // printf(result);
        JSON* obj = JSON_Parse(result);
        if (obj && JSON_HasKey(obj, "state")) {
            JSON_RemoveKey(obj, "metadata");
            JSON* state = JSON_GetObject(obj, "state");
            return JSON_GetObject(state, "reported");
            return obj;
        }
    }
    return NULL;
}

void Aws_SyncDatabase() {
    bool syncOK = true;
    JSON* syncingDevices = JSON_CreateArray();
    JSON* syncingGroups = JSON_CreateArray();
    JSON* syncingScenes = JSON_CreateArray();
    JSON* gatewayInfo;

    // Get number of pages
    JSON* accountInfo = Aws_GetShadow(g_thingId, "accountInfo");
    if (accountInfo) {
        int devicePages = JSON_HasKey(accountInfo, "pageIndex0")? JSON_GetNumber(accountInfo, "pageIndex0") : 1;
        int groupPages = JSON_HasKey(accountInfo, "pageIndex3")? JSON_GetNumber(accountInfo, "pageIndex3") : 1;
        int scenePages = JSON_HasKey(accountInfo, "pageIndex2")? JSON_GetNumber(accountInfo, "pageIndex2") : 1;

        // Sync gateways
        gatewayInfo = JSON_Clone(JSON_GetObject(accountInfo, "gateWay"));
        JSON_Delete(accountInfo);

        // Sync devices from aws
        for (int i = 1; i <= devicePages; i++) {
            char str[10];
            sprintf(str, "d_%d", i);
            JSON* devices = Aws_GetShadow(g_thingId, str);
            if (devices) {
                // Add devices from cloud to syncingDevices array
                int pageIndex = JSON_HasKey(devices, "pageIndex")? JSON_GetNumber(devices, "pageIndex") : 1;
                JSON_ForEach(d, devices) {
                    if (cJSON_IsObject(d)) {
                        JSON* device = JSON_Clone(d);
                        JSON_SetText(device, "deviceId", d->string);
                        JSON_SetNumber(device, "pageIndex", pageIndex);
                        JArr_AddObject(syncingDevices, device);
                        printf("deviceId: %s\n", d->string);
                    }
                }
            } else {
                syncOK = false;
            }
            JSON_Delete(devices);
        }

        // Sync groups from aws
        for (int i = 1; i <= groupPages; i++) {
            char str[10];
            sprintf(str, "g_%d", i);
            JSON* groups = Aws_GetShadow(g_thingId, str);
            if (groups) {
                // Add groups from cloud to syncingGroups array
                int pageIndex = JSON_HasKey(groups, "pageIndex")? JSON_GetNumber(groups, "pageIndex") : 1;
                JSON_ForEach(g, groups) {
                    if (cJSON_IsObject(g)) {
                        if (JSON_HasKey(g, "devices")) {
                            JSON* group = JSON_Clone(g);
                            JSON_SetText(group, "groupAddr", g->string);
                            JSON_SetNumber(group, "pageIndex", pageIndex);
                            JArr_AddObject(syncingGroups, group);
                        }
                    }
                }
                JSON_Delete(groups);
            } else {
                syncOK = false;
            }
        }

        // Sync scenes from aws
        for (int i = 1; i <= scenePages; i++) {
            char str[10];
            sprintf(str, "s_%d", i);
            JSON* scenes = Aws_GetShadow(g_thingId, str);
            if (scenes) {
                // Add scenes from cloud to syncingScenes array
                int pageIndex = JSON_HasKey(scenes, "pageIndex")? JSON_GetNumber(scenes, "pageIndex") : 1;
                JSON_ForEach(s, scenes) {
                    if (cJSON_IsObject(s)) {
                        if (JSON_HasKey(s, "name")) {
                            JSON* scene = JArr_CreateObject(syncingScenes);
                            JSON_SetText(scene, "id", s->string);
                            JSON_SetText(scene, "name", JSON_GetText(s, "name"));
                            JSON_SetNumber(scene, "state", JSON_GetNumber(s, "state"));
                            JSON_SetNumber(scene, "isLocal", JSON_GetNumber(s, "isLocal"));
                            JSON_SetNumber(scene, "pageIndex", pageIndex);
                            char sceneType[100];
                            StringCopy(sceneType, JSON_GetText(s, "scenes"));
                            sceneType[1] = 0;
                            JSON_SetText(scene, "sceneType", sceneType);
                            JSON* actions = JSON_Clone(JSON_GetObject(s, "actions"));
                            JSON* conditions = JSON_Clone(JSON_GetObject(s, "conditions"));
                            JSON_SetObject(scene, "actions", actions);
                            JSON_SetObject(scene, "conditions", conditions);
                        } else {
                            logError("Error to parse sene %s", s->string);
                        }
                    }
                }
                JSON_Delete(scenes);
            } else {
                syncOK = false;
            }
        }
    } else {
        syncOK = false;
    }

    if (syncOK) {
        sendPacketTo(SERVICE_CORE, TYPE_RESET_DATABASE, gatewayInfo);
        sleep(2);
        JSON_ForEach(gw, gatewayInfo) {
            if (cJSON_IsObject(gw)) {
                JSON_SetNumber(gw, "needToConfig", 0);
            }
        }
        sendPacketTo(SERVICE_CORE, TYPE_ADD_GW, gatewayInfo);
        JSON_Delete(gatewayInfo);

        if (JArr_Count(syncingDevices) > 0) {
            sendPacketTo(SERVICE_CORE, TYPE_SYNC_DB_DEVICES, syncingDevices);
        }
        if (JArr_Count(syncingGroups) > 0) {
            sendPacketTo(SERVICE_CORE, TYPE_SYNC_DB_GROUPS, syncingGroups);
        }
        if (JArr_Count(syncingScenes) > 0) {
            sendPacketTo(SERVICE_CORE, TYPE_SYNC_DB_SCENES, syncingScenes);
        }
        int currentHour = get_hour_today();
        if (currentHour >= 6 && currentHour < 21) {
            PlayAudio("ready");
        }
    } else {
        PlayAudio("cannot_connect_server");
    }

    JSON_Delete(syncingDevices);
    JSON_Delete(syncingGroups);
    JSON_Delete(syncingScenes);
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


JSON* Aws_CreateCloudPacket(JSON* localPacket)
{
    char packetStr[1000];
    char* deviceId = JSON_GetText(localPacket, "deviceId");
    int dpId = JSON_GetNumber(localPacket, "dpId");
    double dpValue = JSON_GetNumber(localPacket, "dpValue");
    sprintf(packetStr,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"%s\":{\"%d\": %f},\"state\":%d }}}}", TYPE_UPDATE_DEVICE, SENDER_HC_TO_CLOUD, deviceId, KEY_DICT_DPS, dpId, dpValue, TYPE_DEVICE_ONLINE);
    return JSON_Parse(packetStr);
}



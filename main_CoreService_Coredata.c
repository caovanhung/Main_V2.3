#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>     // string function definitions
#include <fcntl.h>  // File control definitions
#include <errno.h>  // Error number definitions
#include <termios.h>    // POSIX terminal control definitionss
#include <sys/time.h>   // time calls
#include <sys/un.h>
#include <netdb.h> /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <mosquitto.h>
#include "database_common.h"
#include "define.h"
#include "logging_stack.h"
#include "time_t.h"
#include "queue.h"
#include "parson.h"
#include "database.h"
#include "mosquitto.h"
#include "core_data_process.h"
#include "helper.h"
#include "messages.h"
#include "cJSON.h"

const char* SERVICE_NAME = SERVICE_CORE;
FILE *fptr;
struct mosquitto * mosq;
sqlite3 *db;

struct Queue *queue_received;

pthread_mutex_t mutex_lock_t            = PTHREAD_MUTEX_INITIALIZER;

Scene* g_sceneList;
int g_sceneCount = 0;
JSON_Value *g_checkRespList;

JSON* parseGroupLinkDevices(const char* devices);
JSON* parseGroupNormalDevices(const char* devices);
JSON* ConvertToLocalPacket(int reqType, const char* cloudPacket);
bool sendInfoDeviceFromDatabase();
bool sendInfoSceneFromDatabase();
// Add device that need to check response to response list
void addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr);
// Check and get the JSON_Object of request that is in response list
JSON_Object* requestIsInRespList(int reqType, const char* itemId);
// Update device status in response list
void updateDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr, int status);
// Get number of response in response list
int getRespNumber();
// Get response status of a device according to a command
int getDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr);
bool Scene_GetFullInfo(JSON* packet);
void SyncDevicesState();

bool compareSceneEntity(JSON* entity1, JSON* entity2) {
    if (entity1 && entity2) {
        int dpId1 = JSON_GetNumber(entity1, "dpId");
        int dpValue1 = JSON_GetNumber(entity1, "dpValue");
        char* dpAddr1 = JSON_GetText(entity1, "dpAddr");
        int dpId2 = JSON_GetNumber(entity2, "dpId");
        int dpValue2 = JSON_GetNumber(entity2, "dpValue");
        char* dpAddr2 = JSON_GetText(entity2, "dpAddr");
        if (dpId1 == dpId2 && dpValue1 == dpValue2 && StringCompare(dpAddr1, dpAddr2)) {
            return true;
        }
    }
    return false;
}

bool compareDp(JSON* dp1, JSON* dp2) {
    if (dp1 && dp2) {
        char* deviceId1 = JSON_GetText(dp1, "deviceId");
        int dpId1 = JSON_GetNumber(dp1, "dpId");
        char* deviceId2 = JSON_GetText(dp2, "deviceId");
        int dpId2 = JSON_GetNumber(dp2, "dpId");
        if (dpId1 == dpId2 && StringCompare(deviceId1, deviceId2)) {
            return true;
        }
    }
    return false;
}

bool compareDevice(JSON* device1, JSON* device2) {
    if (device1 && device2) {
        char* deviceAddr1 = JSON_GetText(device1, "deviceAddr");
        char* deviceAddr2 = JSON_GetText(device2, "deviceAddr");
        if (StringCompare(deviceAddr1, deviceAddr2)) {
            return true;
        }
    }
    return false;
}

void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if(rc)
    {
        LogError((get_localtime_now()),("Error with result code: %d\n", rc));
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_CORE_DATA, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    int reponse = 0;
    bool check_flag =false;

    pthread_mutex_lock(&mutex_lock_t);
    int size_queue = get_sizeQueue(queue_received);
    if (size_queue < QUEUE_SIZE) {
        enqueue(queue_received,(char *) msg->payload);
        pthread_mutex_unlock(&mutex_lock_t);
    } else {
       pthread_mutex_unlock(&mutex_lock_t);
    }
}

void* RUN_MQTT_LOCAL(void* p)
{
    int rc = 0;
    mosquitto_lib_init();
    mosq = mosquitto_new(MQTT_MOSQUITTO_CIENT_ID, true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
    }
    LogInfo((get_localtime_now()),("We are now connected to the broker!"));
    while(1)
    {
        rc = mosquitto_loop(mosq, -1, 1);
        //LogInfo( (get_localtime_now()),( "rc %d.",rc ) );
        if(rc != 0)
        {
            LogError( (get_localtime_now()),( "rc %d.",rc ) );
            fptr = fopen("/usr/bin/log.txt","a");
            fprintf(fptr,"[%s]CORE  error %d connected mosquitto\n",get_localtime_now(),rc);
            fclose(fptr);
            // break;
        }
        usleep(100);
    }
}

void CHECK_REPONSE_RESULT()
{
    bool check_result = false;
    unsigned char size_ = 0,reuestValueCount,i=0,j=0,k=0,leng=0,reponse=0;
    long long int currentTime = timeInMilliseconds();
    JSON_Array* respArray = json_value_get_array(g_checkRespList);
    int respCount = json_array_get_count(respArray);

    // Loop through all request that need to check response
    for (i = 0; i < respCount; i++) {
        JSON_Object* respItem = json_array_get_object(respArray, i);
        long long int createdTime  = json_object_get_number(respItem, "createdTime");
        JSON_Array* devices   = json_object_get_array(respItem, "devices");
        int deviceCount       = json_array_get_count(devices);
        char reqId[20];
        strcpy(reqId, json_object_get_string(respItem, "reqType"));   // reqId = reqType.itemId
        int reqType  = atoi(strtok(reqId, "."));
        char* itemId = strtok(NULL, ".");
        if (currentTime - createdTime < TIMEOUT_ADD_GROUP_MS) {
            // Check status of all devices in the response object
            int successCount = 0;
            char* deviceAddr;
            for (int d = 0; d < deviceCount; d++) {
                JSON_Object* device = json_array_get_object(devices, d);
                int deviceStatus = json_object_get_number(device, "status");
                deviceAddr = json_object_get_string(device, "addr");
                if (deviceStatus == 1) {
                    successCount++;
                }
            }

            if (successCount == deviceCount) {
                Aws_updateGroupState(itemId, AWS_STATUS_SUCCESS);
                sendNotiToUser(msgByReqType(reqType, MSG_TYPE_SUCCESS));
                // Remove this respItem from response list
                json_array_remove(respArray, i);
            }
        } else {
            logError("Check Response Timeout. reqType: %d", reqType);
            list_t successDevices = {0}, failedDevices = {0};
            DeviceInfo deviceInfo;
            for (int d = 0; d < deviceCount; d++) {
                JSON_Object* device = json_array_get_object(devices, d);
                int length = Db_FindDeviceByAddr(&deviceInfo, json_object_get_string(device, "addr"));
                if (length == 1) {
                    int deviceStatus = json_object_get_number(device, "status");
                    if (deviceStatus == 1) {
                        List_PushString(&successDevices, deviceInfo.id);
                    } else {
                        List_PushString(&failedDevices, deviceInfo.id);
                    }
                }
            }
            Aws_updateGroupDevices(itemId, &successDevices, &failedDevices);
            // Remove this respItem from response list
            json_array_remove(respArray, i);
        }
    }
}

void markSceneToRun(Scene* scene) {
    // Just setting the runningActionIndex vatiable to 0 and the actions of scene will be executed
    // in executeScenes() function
    scene->delayStart = timeInMilliseconds();
    scene->runningActionIndex = 0;
}

// Check if a scene is need to be executed or not
void checkSceneCondition(Scene* scene) {
    // Don't check condition for manual scene
    if (scene->type == SceneTypeManual) {
        markSceneToRun(scene);
        return;
    }

    // Check effective time
    bool effectTime = false;
    if (scene->effectFrom > 0 || scene->effectTo > 0) {
        time_t rawtime; struct tm *info; time( &rawtime ); info = localtime(&rawtime);
        int todayMinutes = info->tm_hour * 60 + info->tm_min;
        if (scene->effectFrom < scene->effectTo) {
            // effectFrom and effectTo are in the same day
            uint8_t weekdayMark = (0x40 >> info->tm_mday) & scene->effectRepeat;
            if (weekdayMark && todayMinutes >= scene->effectFrom && todayMinutes > scene->effectTo) {
                effectTime = true;
            }
        } else {
            // effectFrom and effectTo are in the different days
            uint8_t todayMark = (0x40 >> info->tm_mday) & scene->effectRepeat;
            uint8_t tomorrowMark = (scene->effectRepeat & 0x01)? scene->effectRepeat | 0x80 : scene->effectRepeat;
            tomorrowMark = (0x40 >> info->tm_mday) & (tomorrowMark >> 1);
            if ((todayMark && todayMinutes >= scene->effectFrom) ||
                (tomorrowMark && todayMinutes <= scene->effectTo)) {
                effectTime = true;
            }
        }
    } else {
        effectTime = true;
    }

    // Only check conditions of scene if current time is effective and scene is enabled
    if (effectTime && scene->isEnable) {
        if (scene->type == SceneTypeOneOfConds) {
            for (int i = 0; i < scene->conditionCount; i++) {
                if (scene->conditions[i].conditionType == EntitySchedule) {
                    if (scene->conditions[i].timeReached) {
                        scene->conditions[i].timeReached = -1;
                        markSceneToRun(scene);
                    }
                } else {
                    dp_info_t dpInfo;
                    int foundDps = Db_FindDp(&dpInfo, scene->conditions[i].entityId, scene->conditions[i].dpId);
                    if (foundDps == 1 && dpInfo.value == scene->conditions[i].dpValue) {
                        markSceneToRun(scene);
                        break;
                    }
                }
            }
        } else {
            int i = 0;
            for (i = 0; i < scene->conditionCount; i++) {
                if (scene->conditions[i].conditionType == EntitySchedule) {
                    if (scene->conditions[i].timeReached == 0) {
                        break;
                    }
                } else {
                    dp_info_t dpInfo;
                    int foundDps = Db_FindDp(&dpInfo, scene->conditions[i].entityId, scene->conditions[i].dpId);
                    if (foundDps == 0 || dpInfo.value != scene->conditions[i].dpValue) {
                        break;
                    }
                }
            }
            if (i == scene->conditionCount) {
                scene->conditions[i].timeReached = -1;
                markSceneToRun(scene);
            }
        }
    }
}


// Check if there are scenes need to be executed if status of a device is changed
void checkSceneForDevice(const char* deviceId, int dpId) {
    // Find all scenes that this device is in conditions
    for (int i = 0; i < g_sceneCount; i++) {
        Scene* scene = &g_sceneList[i];
        for (int c = 0; c < scene->conditionCount; c++) {
            if (scene->conditions[c].conditionType == EntityDevice &&
                (strcmp(scene->conditions[c].entityId, deviceId) == 0) &&
                (scene->conditions[c].dpId == dpId)) {
                checkSceneCondition(scene);
            }
        }
    }
}

/*
 * Loop through all HC scene and execute them is they need to execute
 * This function has to be called constantly in main loop
 */
void executeScenes() {
    for (int i = 0; i < g_sceneCount; i++) {
        Scene* scene = &g_sceneList[i];
        if (!scene->isLocal && scene->runningActionIndex >= 0) {
            SceneAction* runningAction = &scene->actions[scene->runningActionIndex];
            if (runningAction->actionType == EntityDevice) {
                // Action type is "control a device"
                JSON* packet = JSON_CreateObject();
                JSON_SetText(packet, "pid", runningAction->pid);
                JSON* dpArray = JSON_AddArrayToObject(packet, "dictDPs");
                JSON* dpItem = JSON_ArrayAddObject(dpArray);
                JSON_SetNumber(dpItem, "id", runningAction->dpId);
                JSON_SetText(dpItem, "addr", runningAction->dpAddr);
                JSON_SetNumber(dpItem, "value", runningAction->dpValue);
                sendPacketTo(SERVICE_BLE, TYPE_CTR_DEVICE, packet);
                JSON_Delete(packet);
                // Move to next action
                scene->delayStart = timeInMilliseconds();
                scene->runningActionIndex++;
                if (scene->runningActionIndex >= scene->actionCount) {
                    scene->runningActionIndex = -1;     // No other actions need to execute, move scene to idle state
                }
            } else if (runningAction->actionType == EntityScene) {
                // Action type is "run another scene"
                for (int s = 0; s < g_sceneCount; s++) {
                    if (strcmp(g_sceneList[s].id, runningAction->entityId) == 0) {
                        markSceneToRun(&g_sceneList[s]);
                        break;
                    }
                }
                // Move to next action
                scene->delayStart = timeInMilliseconds();
                scene->runningActionIndex++;
                if (scene->runningActionIndex >= scene->actionCount) {
                    scene->runningActionIndex = -1;     // No other actions need to execute, move scene to idle state
                }
            } else if (runningAction->actionType == EntityDelay) {
                // Action type is delay a given time
                long long currentTime = timeInMilliseconds();
                if (currentTime - scene->delayStart > runningAction->delaySeconds * 1000) {
                    // Move to next action
                    scene->delayStart = timeInMilliseconds();
                    scene->runningActionIndex++;
                    if (scene->runningActionIndex >= scene->actionCount) {
                        scene->runningActionIndex = -1;     // No other actions need to execute, move scene to idle state
                    }
                }
            }
        }

        // Check condition scheduler
        if (!scene->isLocal) {
            for (int i = 0; i < scene->conditionCount; i++) {
                SceneCondition* cond = &scene->conditions[i];
                if (cond->conditionType == EntitySchedule) {
                    // Get current date time
                    time_t rawtime; struct tm *info; time( &rawtime ); info = localtime(&rawtime);
                    int todayMinutes = info->tm_hour * 60 + info->tm_min;
                    if ((0x40 >> info->tm_mday) & cond->repeat) {
                        // After scene is executed, timeReached will be -1
                        if (cond->timeReached == 0 && todayMinutes == cond->schMinutes) {
                            cond->timeReached = 1;
                            checkSceneCondition(scene);     // Check other conditions of this scene
                        } else if (todayMinutes > cond->schMinutes) {
                            cond->timeReached = 0;
                        }
                        // When scene was created, if no repeating, repeat will be set to 0xFF
                        // So after scene is executed, we have to set repeat to 0 to disable any checking later
                        if (cond->repeat == 0xFF && cond->timeReached == -1) {
                            cond->repeat = 0;
                            Db_SaveSceneCondRepeat(scene->id, i, cond->repeat);
                            break;
                        }
                    }
                }
            }
        }
    }
}

int main( int argc,char ** argv )
{
    g_checkRespList   = json_value_init_array();
    int size_queue = 0;
    bool check_flag = false;
    pthread_t thr[2];
    int err,xRun = 1;
    int rc[2];
    queue_received = newQueue(QUEUE_SIZE);

    //Open database
    check_flag = open_database(VAR_DATABASE,&db);
    if(!check_flag)
    {
        LogInfo((get_localtime_now()),("sqlite3_open is success."));
    }

    rc[0]=pthread_create(&thr[0],NULL,RUN_MQTT_LOCAL,NULL);
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }

    Db_LoadSceneToRam();
    sleep(1);
    SyncDevicesState();
    while(xRun!=0) {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if (size_queue > 0) {
            int reponse = 0;int leng =  0,number = 0, size_ = 0,i=0;
            long long TimeCreat = 0;
            char *val_input = (char*)malloc(10000);
            strcpy(val_input,(char *)dequeue(queue_received));

            JSON_Value *schema = NULL;
            JSON_Value *object = NULL;
            JSON_Object *object_tmp = NULL;
            JSON_Array *actions_array_json = NULL;
            JSON_Array *condition_array_json = NULL;
            schema = json_parse_string(val_input);
            const char *NameService     = json_object_get_string(json_object(schema),MOSQ_NameService);
            int type_action_t           = json_object_get_number(json_object(schema),MOSQ_ActionType);
            TimeCreat                   = json_object_get_number(json_object(schema),MOSQ_TimeCreat);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            object = json_parse_string(object_string);
            int TypeReponse_t           = json_object_get_number(json_object(object),TYPE_REPONSE);
            printf("\n\r");
            logInfo("Received message: %s", val_input);
            JSON* recvPacket = JSON_Parse(val_input);
            JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));
            int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
            if (isMatchString(NameService, SERVICE_BLE)) {
                switch (reqType) {
                    case GW_RESPONSE_DEVICE_CONTROL: {
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        double dpValue = JSON_GetNumber(payload, "dpValue");
                        dp_info_t dpInfo;
                        int foundDps = Db_FindDpByAddr(&dpInfo, dpAddr);
                        if (foundDps == 1) {
                            JSON_SetText(payload, "deviceId", dpInfo.deviceId);
                            JSON_SetNumber(payload, "dpId", dpInfo.id);
                            JSON_SetNumber(payload, "statusType", GW_RESPONSE_DEVICE_CONTROL);
                            // Send packet to AWS
                            sendPacketTo(SERVICE_AWS, reqType, payload);
                            Db_SaveDpValue(dpAddr, dpInfo.id, dpValue);
                            Db_SaveDeviceState(dpInfo.deviceId, STATE_ONLINE);
                            Db_AddDeviceHistory(payload);
                            checkSceneForDevice(dpInfo.deviceId, dpInfo.id);     // Check and run scenes for this device if any
                        }
                        break;
                    }
                    case GW_RESPONSE_DEVICE_STATE: {
                        JSON* devicesArray = JSON_GetObject(payload, "devices");
                        JSON_ForEach(arrayItem, devicesArray) {
                            char* deviceAddr = JSON_GetText(arrayItem, "deviceAddr");
                            int deviceState = JSON_GetNumber(arrayItem, "deviceState");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDeviceByAddr(&deviceInfo, deviceAddr);
                            if (foundDevices == 1) {
                                JSON_SetText(arrayItem, "deviceId", deviceInfo.id);
                                Db_SaveDeviceState(deviceInfo.id, deviceState);
                            }
                        }
                        sendPacketTo(SERVICE_AWS, GW_RESPONSE_DEVICE_STATE, payload);
                        break;
                    }
                    case GW_RESPONSE_SENSOR_BATTERY:
                    case GW_RESPONSE_SMOKE_SENSOR:
                    case GW_RESPONSE_SENSOR_PIR_DETECT:
                    case GW_RESPONSE_SENSOR_PIR_LIGHT:
                    case GW_RESPONSE_SENSOR_ENVIRONMENT:
                    case GW_RESPONSE_SENSOR_DOOR_DETECT:
                    case GW_RESPONSE_SENSOR_DOOR_ALARM: {
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        int dpId = JSON_GetNumber(payload, "dpId");
                        int dpValue = JSON_GetNumber(payload, "dpValue");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDeviceByAddr(&deviceInfo, deviceAddr);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceId", deviceInfo.id);
                            sendPacketTo(SERVICE_AWS, reqType, payload);
                            Db_SaveDpValue(deviceAddr, dpId, dpValue);
                            JSON_SetNumber(payload, "causeType", 0);
                            JSON_SetText(payload, "causeId", "");
                            JSON_SetNumber(payload, "statusType", reqType);
                            Db_AddDeviceHistory(payload);
                            checkSceneForDevice(deviceInfo.id, dpId);     // Check and run scenes for this device if any
                        }

                        break;
                    }
                    case GW_RESPONSE_ADD_GROUP_LIGHT:
                    {
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        if (requestIsInRespList(TYPE_ADD_GROUP_NORMAL, groupAddr)) {
                            updateDeviceRespStatus(TYPE_ADD_GROUP_NORMAL, groupAddr, deviceAddr, 1);
                        } else if (requestIsInRespList(TYPE_ADD_GROUP_LINK, groupAddr)) {
                            updateDeviceRespStatus(TYPE_ADD_GROUP_LINK, groupAddr, deviceAddr, 1);
                        }
                        break;
                    }
                    case TYPE_SYNC_DEVICE_STATE: {
                        SyncDevicesState();
                        break;
                    }
                }
            } else if (isMatchString(NameService, SERVICE_AWS)) {
                switch (reqType) {
                    case TYPE_GET_DEVICE_HISTORY: {
                        long long currentTime = timeInMilliseconds();
                        long long startTime = JSON_GetNumber(payload, "start");
                        long long endTime = JSON_GetNumber(payload, "end");
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        int dpId = atoi(JSON_GetText(payload, "dpId"));
                        int pageIndex = JSON_GetNumber(payload, "pageIndex");
                        startTime = startTime == 0? currentTime - 86400000 : startTime;
                        JSON* histories = Db_FindDeviceHistories(startTime, endTime, deviceId, dpId, pageIndex);
                        sendPacketTo(SERVICE_AWS, reqType, histories);
                        break;
                    }
                    case TYPE_CTR_DEVICE:
                    case TYPE_CTR_GROUP_NORMAL: {
                        char* senderId = JSON_GetText(payload, "senderId");
                        JSON* originDPs = JSON_GetObject(payload, "dictDPs");
                        DeviceInfo deviceInfo;
                        char* deviceId;
                        int foundDevices = 0;
                        if (type_action_t == TYPE_CTR_DEVICE) {
                            deviceId = JSON_GetText(payload, "deviceId");
                            foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        } else {
                            deviceId = JSON_GetText(payload, "groupAdress");
                            deviceInfo.pid[0] = 0;
                            foundDevices = 1;
                        }

                        if (foundDevices == 1) {
                            JSON* packet = JSON_CreateObject();
                            JSON_SetText(packet, "pid", deviceInfo.pid);
                            JSON_SetText(packet, "senderId", senderId);
                            cJSON* dictDPs = cJSON_AddArrayToObject(packet, "dictDPs");
                            JSON_ForEach(o, originDPs) {
                                int dpId = atoi(o->string);
                                dp_info_t dpInfo;
                                int dpFound = Db_FindDp(&dpInfo, deviceId, dpId);
                                if (dpFound) {
                                    cJSON* dp = JSON_CreateObject();
                                    JSON_SetNumber(dp, "id", dpId);
                                    JSON_SetText(dp, "addr", dpInfo.addr);
                                    JSON_SetNumber(dp, "value", o->valueint);
                                    cJSON_AddItemToArray(dictDPs, dp);
                                }
                            }
                            sendPacketTo(SERVICE_BLE, type_action_t, packet);
                            JSON_Delete(packet);
                        }
                        break;
                    }
                    case TYPE_CTR_SCENE: {
                        char* sceneId = JSON_GetText(payload, "sceneId");
                        int state = JSON_GetNumber(payload, "state");

                        // Check if this scene is HC or local
                        bool isLocal = true;
                        int sceneType = 0;
                        for (int i = 0; i < g_sceneCount; i++) {
                            if (strcmp(g_sceneList[i].id, sceneId) == 0) {
                                isLocal = g_sceneList[i].isLocal;
                                sceneType = g_sceneList[i].type;
                                JSON_SetNumber(payload, "sceneType", g_sceneList[i].type);
                                break;
                            }
                        }
                        if (sceneType != SceneTypeManual) {
                            if (state)  { logInfo("Enabling scene %s", sceneId); }
                            else        { logInfo("Disabling scene %s", sceneId); }
                            Db_EnableScene(sceneId, state);
                        } else {
                            logInfo("Executing HC scene %s", sceneId);
                            if (!isLocal) {
                                for (int i = 0; i < g_sceneCount; i++) {
                                    if (strcmp(g_sceneList[i].id, sceneId) == 0) {
                                        markSceneToRun(&g_sceneList[i]);
                                        break;
                                    }
                                }
                            }
                        }
                        if (isLocal) {
                            sendPacketTo(SERVICE_BLE, TYPE_CTR_SCENE, payload);
                        }
                        break;
                    }
                    case TYPE_ADD_DEVICE: {
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        char* deviceId = JSON_GetText(localPacket, "deviceId");
                        int provider = JSON_GetNumber(localPacket, "provider");
                        logInfo("Adding device: %s", deviceId);

                        // Delete device from database if exist
                        logInfo("Delete device %s from database if exist", deviceId);
                        Db_DeleteDevice(deviceId);

                        if (provider == HOMEGY_BLE) {
                            // Send packet to BLE to save device information in to gateway
                            sendPacketTo(SERVICE_BLE, TYPE_ADD_DEVICE, localPacket);
                            JSON_Delete(localPacket);
                        }

                        // Insert device to database
                        addNewDevice(&db, object_string);
                        break;
                    }
                    case TYPE_DEL_DEVICE: {
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        char* deviceId = JSON_GetText(localPacket, "deviceId");
                        if (deviceId) {
                            sendPacketTo(SERVICE_BLE, TYPE_DEL_DEVICE, localPacket);
                            Db_DeleteDevice(deviceId);
                            logInfo("Delete deviceId: %s", deviceId);
                        } else {
                            logError("deviceId is not existed in the packet: %s", object_string);
                        }
                        JSON_Delete(localPacket);
                        break;
                    }
                    case TYPE_ADD_GW: {
                        // addGateway(&db,object_string);

                        // get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_ADD_GW,MOSQ_ActResponse);
                        // replaceInfoFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_ADD_GW,MOSQ_ActResponse,val_input);
                        // mqttLocalPublish(topic, message);
                        // if(topic != NULL) free(topic);
                        // if(message != NULL) free(message);
                        break;
                    }

                    case TYPE_ADD_SCENE: {
                        int isLocal = JSON_GetNumber(payload, "isLocal");
                        Scene_GetFullInfo(payload);
                        Db_AddScene(payload);  // Insert scene into database
                        if (isLocal) {
                            sendPacketTo(SERVICE_BLE, TYPE_ADD_SCENE, payload);
                        }
                        break;
                    }
                    case TYPE_DEL_SCENE: {
                        char* sceneId = JSON_GetText(payload, "Id");
                        JSON* sceneInfo = Db_FindScene(sceneId);
                        if (sceneInfo) {
                            int isLocal = JSON_GetNumber(sceneInfo, "isLocal");
                            if (isLocal) {
                                sendPacketTo(SERVICE_BLE, TYPE_DEL_SCENE, sceneInfo);   // Send packet to BLE
                            }
                            Db_DeleteScene(sceneId);    // Delete scene from database
                        }
                        JSON_Delete(sceneInfo);
                        break;
                    }
                    case TYPE_UPDATE_SCENE: {
                        JSON* newScene = payload;
                        Scene_GetFullInfo(newScene);
                        char* sceneId = JSON_GetText(newScene, "Id");
                        JSON* oldScene = Db_FindScene(sceneId);
                        if (oldScene) {
                            int isLocal = JSON_GetNumber(oldScene, "isLocal");
                            if (isLocal) {
                                JSON* oldActionsArray = JSON_GetObject(oldScene, "actions");
                                JSON* newActionsArray = JSON_GetObject(newScene, "actions");
                                // Find all actions that are need to be removed
                                JSON* actionsNeedRemove = JSON_CreateArray();
                                JSON_ForEach(oldAction, oldActionsArray) {
                                    bool found = false;
                                    JSON_ForEach(newAction, newActionsArray) {
                                        if (compareSceneEntity(oldAction, newAction)) {
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found) {
                                        cJSON_AddItemReferenceToArray(actionsNeedRemove, oldAction);
                                    }
                                }

                                // Find all actions that are need to be added
                                JSON* actionsNeedAdd = JSON_CreateArray();
                                JSON_ForEach(newAction, newActionsArray) {
                                    bool found = false;
                                    JSON_ForEach(oldAction, oldActionsArray) {
                                        if (compareSceneEntity(oldAction, newAction)) {
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found) {
                                        cJSON_AddItemReferenceToArray(actionsNeedAdd, newAction);
                                    }
                                }

                                // Check if condition is need to change
                                bool conditionNeedChange = true;
                                JSON* oldConditionsArray = JSON_GetObject(oldScene, "conditions");
                                JSON* newConditionsArray = JSON_GetObject(newScene, "conditions");
                                JSON* oldCondition = JSON_ArrayGetObject(oldConditionsArray, 0);
                                JSON* newCondition = JSON_ArrayGetObject(newConditionsArray, 0);
                                if (compareSceneEntity(oldCondition, newCondition)) {
                                    conditionNeedChange = false;
                                }

                                // Send updated packet to BLE
                                JSON* packet = JSON_CreateObject();
                                JSON_SetText(packet, "sceneId", sceneId);
                                JSON_SetObject(packet, "actionsNeedRemove", actionsNeedRemove);
                                JSON_SetObject(packet, "actionsNeedAdd", actionsNeedAdd);
                                if (conditionNeedChange) {
                                    JSON_SetObject(packet, "conditionNeedRemove", oldCondition);
                                    JSON_SetObject(packet, "conditionNeedAdd", newCondition);
                                }
                                sendPacketTo(SERVICE_BLE, type_action_t, packet);
                                JSON_Delete(packet);
                            }
                            // Save new scene to database
                            Db_DeleteScene(sceneId);
                            sendNotiToUser("Cập nhật kịch bản thành công");
                        } else {
                            logInfo("Scene %s is not found", sceneId);
                        }

                        break;
                    }
                    case TYPE_ADD_GROUP_NORMAL: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        sendPacketTo(SERVICE_BLE, type_action_t, localPacket);
                        JSON* devicesArray = JSON_GetObject(localPacket, "devices");
                        JSON* srcObj = JSON_Parse(object_string);
                        // Insert group information to database
                        Db_AddGroup(groupAddr, JSON_GetText(srcObj, "name"), JSON_GetText(srcObj, "devices"), true);
                        // Add this request to response list for checking response
                        JSON_ForEach(device, devicesArray) {
                            addDeviceToRespList(type_action_t, groupAddr, JSON_GetText(device, "deviceAddr"));
                        }
                        JSON_Delete(localPacket);
                        JSON_Delete(srcObj);
                        break;
                    }
                    case TYPE_DEL_GROUP_NORMAL: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        sendPacketTo(SERVICE_BLE, type_action_t, localPacket);
                        // Delete group information from database
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        Db_DeleteGroup(groupAddr);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_NORMAL: {
                        logInfo("TYPE_UPDATE_GROUP_NORMAL");
                        JSON* srcObj = payload;
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        char* devicesStr = Db_FindDevicesInGroup(groupAddr);
                        if (devicesStr) {
                            JSON* oldDevices = parseGroupNormalDevices(devicesStr);
                            char* t1 = cJSON_PrintUnformatted(oldDevices);
                            JSON* newDevices = JSON_GetObject(localPacket, "devices");
                            char* t2 = cJSON_PrintUnformatted(newDevices);
                            // Find all devices that are need to be removed
                            JSON* dpsNeedRemove = JSON_CreateArray();
                            JSON_ForEach(oldDevice, oldDevices) {
                                bool found = false;
                                JSON_ForEach(newDevice, newDevices) {
                                    if (compareDevice(oldDevice, newDevice)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    cJSON_AddItemReferenceToArray(dpsNeedRemove, oldDevice);
                                }
                            }

                            // Find all actions that are need to be added
                            JSON* dpsNeedAdd = JSON_CreateArray();
                            JSON_ForEach(newDevice, newDevices) {
                                bool found = false;
                                JSON_ForEach(oldDevice, oldDevices) {
                                    if (compareDevice(oldDevice, newDevice)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    cJSON_AddItemReferenceToArray(dpsNeedAdd, newDevice);
                                }
                            }

                            // Send updated packet to BLE
                            JSON* packet = JSON_CreateObject();
                            JSON_SetText(packet, "groupAddr", groupAddr);
                            JSON_SetObject(packet, "dpsNeedRemove", dpsNeedRemove);
                            JSON_SetObject(packet, "dpsNeedAdd", dpsNeedAdd);
                            sendPacketTo(SERVICE_BLE, type_action_t, packet);
                            JSON_Delete(packet);
                            Db_SaveGroupDevices(groupAddr, JSON_GetText(srcObj, "devices"));
                        }
                        JSON_Delete(localPacket);
                        free(devicesStr);
                        break;
                    }
                    case TYPE_ADD_GROUP_LINK: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        sendPacketTo(SERVICE_BLE, type_action_t, localPacket);
                        JSON* devicesArray = JSON_GetObject(localPacket, "devices");
                        JSON* srcObj = payload;
                        // Insert group information to database
                        Db_AddGroup(groupAddr, JSON_GetText(srcObj, "name"), JSON_GetText(srcObj, "devices"), false);
                        // Add this request to response list for checking response
                        JSON_ForEach(device, devicesArray) {
                            addDeviceToRespList(type_action_t, groupAddr, JSON_GetText(device, "deviceAddr"));
                        }
                        JSON_Delete(localPacket);
                        break;
                    }
                    case TYPE_DEL_GROUP_LINK: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        sendPacketTo(SERVICE_BLE, TYPE_DEL_GROUP_LINK, localPacket);
                        // Delete group information from database
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        Db_DeleteGroup(groupAddr);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_LINK: {
                        logInfo("TYPE_UPDATE_GROUP_LINK");
                        JSON* srcObj = payload;
                        JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        char* devicesStr = Db_FindDevicesInGroup(groupAddr);
                        if (devicesStr) {
                            JSON* oldDps = parseGroupLinkDevices(devicesStr);
                            JSON* newDps = JSON_GetObject(localPacket, "devices");
                            // Find all dps that are need to be removed
                            JSON* dpsNeedRemove = JSON_CreateArray();
                            JSON_ForEach(oldDp, oldDps) {
                                bool found = false;
                                JSON_ForEach(newDp, newDps) {
                                    if (compareDp(oldDp, newDp)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    cJSON_AddItemReferenceToArray(dpsNeedRemove, oldDp);
                                }
                            }

                            // Find all actions that are need to be added
                            JSON* dpsNeedAdd = JSON_CreateArray();
                            JSON_ForEach(newDp, newDps) {
                                bool found = false;
                                JSON_ForEach(oldDp, oldDps) {
                                    if (compareDp(oldDp, newDp)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    cJSON_AddItemReferenceToArray(dpsNeedAdd, newDp);
                                }
                            }

                            // Send updated packet to BLE
                            JSON* packet = JSON_CreateObject();
                            JSON_SetText(packet, "groupAddr", groupAddr);
                            JSON_SetObject(packet, "dpsNeedRemove", dpsNeedRemove);
                            JSON_SetObject(packet, "dpsNeedAdd", dpsNeedAdd);
                            sendPacketTo(SERVICE_BLE, type_action_t, packet);
                            JSON_Delete(packet);
                            Db_SaveGroupDevices(groupAddr, JSON_GetText(srcObj, "devices"));
                        }
                        JSON_Delete(localPacket);
                        free(devicesStr);
                        break;
                    }
                    case TYPE_LOCK_AGENCY: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            JSON* lock = JSON_GetObject(payload, "lock");
                            JSON_ForEach(l, lock) {
                                JSON_SetNumber(payload, "value", l->valueint - 2);
                                break;
                            }
                            sendPacketTo(SERVICE_BLE, type_action_t, payload);
                        }
                        break;
                    }
                    case TYPE_LOCK_KIDS: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            sendPacketTo(SERVICE_BLE, type_action_t, payload);
                        }
                        break;
                    }
                    case GW_RESPONSE_DIM_LED_SWITCH_HOMEGY: {
                        break;
                    }
                    case GW_RESPONSE_SCENE_LC_WRITE_INTO_DEVICE: {
                        break;
                    }
                    case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE: {
                        break;
                    }
                    case GW_RESPONSE_DEVICE_KICKOUT: {
                        break;
                    }
                }
            }

            JSON_Delete(recvPacket);
            JSON_Delete(payload);
        } else if(size_queue == QUEUE_SIZE) {
            pthread_mutex_unlock(&mutex_lock_t);
        }
        pthread_mutex_unlock(&mutex_lock_t);
        CHECK_REPONSE_RESULT();
        executeScenes();
        usleep(1000);
    }
    return 0;
}

// Add device that need to check response to response list
void addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr) {
    JSON_Object* item = requestIsInRespList(reqType, itemId);
    if (item == NULL) {
        char reqTypeStr[10];
        sprintf(reqTypeStr, "%d.%s", reqType, itemId);
        JSON_Array *rootArray = json_value_get_array(g_checkRespList);
        JSON_Value *newItem = json_value_init_object();
        JSON_Object *newItemObj = json_value_get_object(newItem);
        json_object_set_string(newItemObj, "reqType", reqTypeStr);
        json_object_dotset_number(newItemObj, "createdTime", timeInMilliseconds());
        JSON_Value *devices = json_value_init_array();
        json_object_set_value(newItemObj, "devices", devices);
        json_array_append_value(rootArray, newItem);
        item = json_object(newItem);
    }
    JSON_Array* devices = json_object_get_array(item, "devices");
    JSON_Value *device = json_value_init_object();
    JSON_Object *deviceObj = json_value_get_object(device);
    json_object_set_string(deviceObj, "addr", deviceAddr);
    json_object_set_number(deviceObj, "status", 0);
    json_array_append_value(devices, device);
}

JSON_Object* requestIsInRespList(int reqType, const char* itemId) {
    char reqTypeStr[10];
    sprintf(reqTypeStr, "%d.%s", reqType, itemId);
    JSON_Array *rootArray = json_value_get_array(g_checkRespList);
    JSON_Object* item = JArray_FindStringValue(rootArray, "reqType", reqTypeStr);
    return item;
}

void updateDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr, int status) {
    JSON_Object* item = requestIsInRespList(reqType, itemId);
    if (item) {
        JSON_Array* devices = json_object_get_array(item, "devices");
        JSON_Object* device = JArray_FindStringValue(devices, "addr", deviceAddr);
        if (device) {
            json_object_set_number(device, "status", status);
        }
    }
}

int getRespNumber() {
    return json_array_get_count(json_value_get_array(g_checkRespList));
}

int getDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr) {
    JSON_Object* item = requestIsInRespList(reqType, itemId);
    if (item) {
        JSON_Array* devices = json_object_get_array(item, "devices");
        JSON_Object* device = JArray_FindStringValue(devices, "addr", deviceAddr);
        if (device) {
            return (int)json_object_get_number(device, "status");
        }
    }
    return 0;
}


bool Scene_GetFullInfo(JSON* packet) {
    JSON* actionsArray = JSON_GetObject(packet, "actions");
    JSON_ForEach(action, actionsArray) {
        JSON* executorProperty = JSON_GetObject(action, "executorProperty");
        char* actionExecutor = JSON_GetText(action, "actionExecutor");
        char* entityId = JSON_GetText(action, "entityId");
        int delaySeconds = 0;
        if (strcmp(entityId, "delay") == 0) {
            int minutes = atoi(JSON_GetText(executorProperty, "minutes"));
            delaySeconds = atoi(JSON_GetText(executorProperty, "seconds"));
            delaySeconds = minutes * 60 + delaySeconds;
            JSON_SetNumber(action, "actionType", EntityDelay);
        } else if (strcmp(actionExecutor, "ruleTrigger") == 0) {
            JSON_SetNumber(action, "actionType", EntityScene);
        } else {
            JSON_SetNumber(action, "actionType", EntityDevice);
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, entityId);
            if (foundDevices == 1) {
                JSON_SetText(action, "pid", deviceInfo.pid);
                JSON_SetText(action, "entityAddr", deviceInfo.addr);
                int dpId = 0, dpValue = 0;
                JSON_ForEach(o, executorProperty) {
                    dpId = atoi(o->string);
                    dpValue = o->valueint;
                }
                dp_info_t dpInfo;
                int foundDps = Db_FindDp(&dpInfo, entityId, dpId);
                if (foundDps == 1) {
                    JSON_SetNumber(action, "dpId", dpId);
                    JSON_SetText(action, "dpAddr", dpInfo.addr);
                    JSON_SetNumber(action, "dpValue", dpValue);
                }
            }
        }
        JSON_SetNumber(action, "delaySeconds", delaySeconds);
    }
    JSON* conditionsArray = JSON_GetObject(packet, "conditions");
    JSON_ForEach(condition, conditionsArray) {
        int schMinutes = 0;
        uint8_t repeat = 0;
        JSON* exprArray = JSON_GetObject(condition, "expr");
        char* entityId = JSON_GetText(condition, "entityId");
        if (strcmp(entityId, "timer") == 0) {
            repeat = atoi(JSON_GetText(exprArray, "loops"));
            char* time = JSON_GetText(exprArray, "time");
            list_t* timeItems = String_Split(time, ":");
            if (timeItems->count == 2) {
                schMinutes = atoi(timeItems->items[0]) * 60 + atoi(timeItems->items[1]);
            }
            List_Delete(timeItems);
            JSON_SetNumber(condition, "conditionType", EntitySchedule);
        } else {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, entityId);
            if (foundDevices == 1) {
                int dpId = atoi(JSON_ArrayGetText(exprArray, 0) + 3);   // Template is "$dp1"
                int dpValue = JSON_ArrayGetNumber(exprArray, 2);
                dp_info_t dpInfo;
                int foundDps = Db_FindDp(&dpInfo, entityId, dpId);
                if (foundDps == 1) {
                    JSON_SetText(condition, "pid", deviceInfo.pid);
                    JSON_SetText(condition, "entityAddr", deviceInfo.addr);
                    JSON_SetNumber(condition, "dpId", dpId);
                    JSON_SetText(condition, "dpAddr", dpInfo.addr);
                    JSON_SetNumber(condition, "dpValue", dpValue);
                }
            }
            JSON_SetNumber(condition, "conditionType", EntityDevice);
        }
        JSON_SetNumber(condition, "schMinutes", schMinutes);
        JSON_SetNumber(condition, "repeat", repeat);
    }
    return true;
}


Scene* Scene_ParseJson(const char* json) {
    JSON* packet = JSON_Parse(json);
    Scene* scene = malloc(sizeof(Scene));
    StringCopy(scene->id, JSON_GetText(packet, "name"));
    scene->isLocal = JSON_GetNumber(packet, "isLocal");
    scene->isEnable = JSON_GetNumber(packet, "state");
    scene->type = atoi(JSON_GetText(packet, "sceneType"));
    scene->effectFrom = 0;
    scene->effectTo = 0;
    scene->effectRepeat = 0;
    scene->runningActionIndex = 0;
    scene->delayStart = 0;
    scene->actionCount = 0;
    scene->conditionCount = 0;

    // Parse actions
    JSON* actionsArray = JSON_GetObject(packet, "actions");
    JSON_ForEach(action, actionsArray) {
        SceneAction* sceneAction = &scene->actions[scene->actionCount];
        JSON* executorProperty = JSON_GetObject(action, "executorProperty");
        StringCopy(sceneAction->entityId, JSON_GetText(action, "entityId"));
        int delaySeconds = 0;
        if (strcmp(sceneAction->entityId, "delay") == 0) {
            int minutes = atoi(JSON_GetText(executorProperty, "minutes"));
            delaySeconds = atoi(JSON_GetText(executorProperty, "seconds"));
            delaySeconds = minutes * 60 + delaySeconds;
            sceneAction->actionType = EntityDelay;
        } else {
            sceneAction->actionType = EntityDevice;
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, sceneAction->entityId);
            if (foundDevices == 1) {
                StringCopy(sceneAction->pid, deviceInfo.pid);
                StringCopy(sceneAction->entityAddr, deviceInfo.addr);
                int dpId = 0, dpValue = 0;
                JSON_ForEach(o, executorProperty) {
                    dpId = atoi(o->string);
                    dpValue = o->valueint;
                }
                dp_info_t dpInfo;
                int foundDps = Db_FindDp(&dpInfo, sceneAction->entityId, dpId);
                if (foundDps == 1) {
                    sceneAction->dpId = dpId;
                    sceneAction->dpValue = dpValue;
                    StringCopy(sceneAction->dpAddr, dpInfo.addr);
                }
            }
        }
        sceneAction->delaySeconds = delaySeconds;
        scene->actionCount++;
    }

    // Parse conditions
    JSON* conditionsArray = JSON_GetObject(packet, "conditions");
    JSON_ForEach(condition, conditionsArray) {
        SceneCondition* sceneCondition = &scene->conditions[scene->conditionCount];
        int schMinutes = 0;
        uint8_t repeat = 0;
        JSON* exprArray = JSON_GetObject(condition, "expr");
        char* entityId = JSON_GetText(condition, "entityId");
        StringCopy(sceneCondition->entityId, JSON_GetText(condition, "entityId"));
        if (strcmp(sceneCondition->entityId, "timer") == 0) {
            repeat = atoi(JSON_GetText(exprArray, "loops"));
            char* time = JSON_GetText(exprArray, "time");
            list_t* timeItems = String_Split(time, ":");
            if (timeItems->count == 2) {
                schMinutes = atoi(timeItems->items[0]) * 60 + atoi(timeItems->items[1]);
            }
            List_Delete(timeItems);
            sceneCondition->conditionType = EntitySchedule;
        } else {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, entityId);
            if (foundDevices == 1) {
                int dpId = atoi(JSON_ArrayGetText(exprArray, 0) + 3);   // Template is "$dp1"
                int dpValue = JSON_ArrayGetNumber(exprArray, 2);
                dp_info_t dpInfo;
                int foundDps = Db_FindDp(&dpInfo, entityId, dpId);
                if (foundDps == 1) {
                    StringCopy(sceneCondition->pid, deviceInfo.pid);
                    StringCopy(sceneCondition->entityAddr, deviceInfo.addr);
                    StringCopy(sceneCondition->dpAddr, dpInfo.addr);
                    sceneCondition->dpId = dpId;
                    sceneCondition->dpValue = dpValue;
                }
            }
            sceneCondition->conditionType = EntityDevice;
        }
        sceneCondition->schMinutes = schMinutes;
        sceneCondition->repeat = repeat;
        scene->conditionCount++;
    }
    JSON_Delete(packet);
    return scene;
}


JSON* Scene_ToJson(Scene* scene) {
    JSON* root = cJSON_CreateObject();
    JSON_SetText(root, "name", scene->id);
    JSON_SetNumber(root, "isLocal", scene->isLocal);
    JSON_SetNumber(root, "state", scene->isEnable);
    JSON_SetNumber(root, "sceneType", scene->type);

    // Add actions
    JSON* actionsArray = JSON_AddArrayToObject(root, "actions");
    for (int i = 0; i < scene->actionCount; i++) {
        SceneAction* sceneAction = &scene->actions[i];
        JSON* action = JSON_ArrayAddObject(actionsArray);
        JSON_SetText(action, "entityId", sceneAction->entityId);
        JSON_SetNumber(action, "actionType", sceneAction->actionType);
        JSON_SetText(action, "pid", sceneAction->pid);
        JSON_SetText(action, "entityAddr", sceneAction->entityAddr);
        JSON_SetNumber(action, "dpId", sceneAction->dpId);
        JSON_SetNumber(action, "dpValue", sceneAction->dpValue);
        JSON_SetText(action, "dpAddr", sceneAction->dpAddr);
        JSON_SetNumber(action, "delaySeconds", sceneAction->delaySeconds);
    }

    // Add conditions
    JSON* conditionsArray = JSON_AddArrayToObject(root, "conditions");
    for (int i = 0; i < scene->conditionCount; i++) {
        SceneCondition* sceneCondition = &scene->conditions[i];
        JSON* condition = JSON_ArrayAddObject(conditionsArray);
        JSON_SetText(condition, "entityId", sceneCondition->entityId);
        JSON_SetNumber(condition, "conditionType", sceneCondition->conditionType);
        JSON_SetText(condition, "pid", sceneCondition->pid);
        JSON_SetText(condition, "entityAddr", sceneCondition->entityAddr);
        JSON_SetNumber(condition, "dpId", sceneCondition->dpId);
        JSON_SetNumber(condition, "dpValue", sceneCondition->dpValue);
        JSON_SetText(condition, "dpAddr", sceneCondition->dpAddr);
        JSON_SetNumber(condition, "schMinutes", sceneCondition->schMinutes);
        JSON_SetNumber(condition, "repeat", sceneCondition->repeat);
        JSON_SetNumber(condition, "timeReached", sceneCondition->timeReached);
    }
}


JSON* ConvertToLocalPacket(int reqType, const char* cloudPacket) {
    JSON* srcObj = JSON_Parse(cloudPacket);
    JSON* destObj = JSON_CreateObject();
    JSON_SetNumber(destObj, "reqType", reqType);
    if (JSON_HasObjectItem(srcObj, "provider")) {
        JSON_SetNumber(destObj, "provider", JSON_GetNumber(srcObj, "provider"));
    }
    if (JSON_HasObjectItem(srcObj, "deviceId")) {
        char* deviceId = JSON_GetText(srcObj, "deviceId");
        JSON_SetText(destObj, "deviceId", deviceId);
        // Find device information from database
        DeviceInfo deviceInfo;
        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
        if (foundDevices) {
            JSON_SetText(destObj, "deviceAddr", deviceInfo.addr);
            JSON_SetText(destObj, "devicePid", deviceInfo.pid);
        }
    }
    if (JSON_HasObjectItem(srcObj, "sceneId")) {
        char* sceneId = JSON_GetText(srcObj, "sceneId");
        JSON_SetText(destObj, "sceneId", sceneId);
    }
    if (JSON_HasObjectItem(srcObj, "groupAdress")) {
        char* groupAddr = JSON_GetText(srcObj, "groupAdress");
        JSON_SetText(destObj, "groupAddr", groupAddr);
        // Find devices in this grou from database or from source object if has
        char* deviceIds = NULL;
        bool hasDevicesInSrc = false;   // "devices" key is exist in the root level srcObj or not
        if (!JSON_HasObjectItem(srcObj, "devices")) {
            deviceIds = Db_FindDevicesInGroup(groupAddr);
        } else {
            hasDevicesInSrc = true;
            deviceIds = JSON_GetText(srcObj, "devices");
        }
        if (deviceIds) {
            // Split deviceIds and make devices array in dest object
            list_t* splitList = String_Split(deviceIds, "|");
            JSON* devicesArray = JSON_AddArrayToObject(destObj, "devices");
            if ((reqType == TYPE_ADD_GROUP_LINK || reqType == TYPE_DEL_GROUP_LINK || reqType == TYPE_UPDATE_GROUP_LINK) && splitList->count % 2 == 0) {
                JSON* arrayItem;
                for (int i = 0; i < splitList->count; i++) {
                    if (i % 2 == 0) {
                        arrayItem = NULL;
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, splitList->items[i]);
                        if (foundDevices) {
                            arrayItem = JSON_CreateObject();
                            JSON_SetText(arrayItem, "deviceId", splitList->items[i]);
                            JSON_SetText(arrayItem, "deviceAddr", deviceInfo.addr);
                        }
                    } else if (arrayItem != NULL) {
                        int dpId = atoi(splitList->items[i]);
                        dp_info_t dpInfo;
                        int dpFounds = Db_FindDp(&dpInfo, JSON_GetText(arrayItem, "deviceId"), dpId);
                        if (dpFounds == 1) {
                            JSON_SetNumber(arrayItem, "dpId", dpId);
                            JSON_SetText(arrayItem, "dpAddr", dpInfo.addr);
                            cJSON_AddItemToArray(devicesArray, arrayItem);
                        } else {
                            JSON_Delete(arrayItem);
                        }
                    }
                }
            } else if (reqType == TYPE_ADD_GROUP_NORMAL || reqType == TYPE_DEL_GROUP_NORMAL || reqType == TYPE_UPDATE_GROUP_NORMAL) {
                for (int i = 0; i < splitList->count; i++) {
                    JSON* arrayItem = JSON_CreateObject();
                    DeviceInfo deviceInfo;
                    int foundDevices = Db_FindDevice(&deviceInfo, splitList->items[i]);
                    if (foundDevices) {
                        JSON_SetText(arrayItem, "deviceAddr", deviceInfo.addr);
                    }
                    cJSON_AddItemToArray(devicesArray, arrayItem);
                }
            }
            List_Delete(splitList);
            if (!hasDevicesInSrc) {
                free(deviceIds);
            }
        }
    }
    if (JSON_HasObjectItem(srcObj, "protocol_para")) {
        JSON* protParam = cJSON_GetObjectItem(srcObj, "protocol_para");
        if (JSON_HasObjectItem(protParam, "pid")) {
            char* devicePid = JSON_GetText(protParam, "pid");
            JSON_SetText(destObj, "devicePid", devicePid);
        }

        if (JSON_HasObjectItem(protParam, "deviceKey")) {
            char* deviceKey = JSON_GetText(protParam, "deviceKey");
            JSON_SetText(destObj, "deviceKey", deviceKey);
        }

        if (JSON_HasObjectItem(protParam, "Unicast")) {
            char* deviceAddr = JSON_GetText(protParam, "Unicast");
            JSON_SetText(destObj, "deviceAddr", deviceAddr);
        }
    }

    JSON_Delete(srcObj);
    return destObj;
}


JSON* parseGroupLinkDevices(const char* devices) {
    JSON* devicesArray = cJSON_CreateArray();
    list_t* splitList = String_Split(devices, "|");
    if (splitList->count % 2 == 0) {
        JSON* arrayItem;
        for (int i = 0; i < splitList->count; i++) {
            if (i % 2 == 0) {
                arrayItem = NULL;
                DeviceInfo deviceInfo;
                int foundDevices = Db_FindDevice(&deviceInfo, splitList->items[i]);
                if (foundDevices) {
                    arrayItem = JSON_CreateObject();
                    JSON_SetText(arrayItem, "deviceId", splitList->items[i]);
                    JSON_SetText(arrayItem, "deviceAddr", deviceInfo.addr);
                }
            } else if (arrayItem != NULL) {
                int dpId = atoi(splitList->items[i]);
                dp_info_t dpInfo;
                int dpFounds = Db_FindDp(&dpInfo, JSON_GetText(arrayItem, "deviceId"), dpId);
                if (dpFounds == 1) {
                    JSON_SetNumber(arrayItem, "dpId", dpId);
                    JSON_SetText(arrayItem, "dpAddr", dpInfo.addr);
                    cJSON_AddItemToArray(devicesArray, arrayItem);
                } else {
                    JSON_Delete(arrayItem);
                }
            }
        }
    }
    return devicesArray;
}

JSON* parseGroupNormalDevices(const char* devices) {
    JSON* devicesArray = cJSON_CreateArray();
    list_t* splitList = String_Split(devices, "|");
    for (int i = 0; i < splitList->count; i++) {
        JSON* arrayItem = JSON_CreateObject();
        DeviceInfo deviceInfo;
        int foundDevices = Db_FindDevice(&deviceInfo, splitList->items[i]);
        if (foundDevices) {
            JSON_SetText(arrayItem, "deviceAddr", deviceInfo.addr);
        }
        cJSON_AddItemToArray(devicesArray, arrayItem);
    }
    return devicesArray;
}

void SyncDevicesState() {
    JSON* packet = JSON_CreateArray();
    char sqlCmd[300];
    sprintf(sqlCmd, "SELECT address FROM devices \
                    JOIN devices_inf ON devices.deviceId = devices_inf.deviceId \
                    WHERE instr('%s', pid) > 0 OR dpId='20';", HG_BLE);
    Sql_Query(sqlCmd, row) {
        char* dpAddr = sqlite3_column_text(row, 0);
        JSON_ArrayAddText(packet, dpAddr);
    }
    sendPacketTo(SERVICE_BLE, TYPE_SYNC_DEVICE_STATE, packet);
}
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

#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <unistd.h> /* read, write, close */
#include <string.h> /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h> /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <sys/un.h>
#include <pthread.h>

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX includes. */
#include <unistd.h>

#include <mosquitto.h>
#include <unistd.h>

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
pthread_cond_t dataUpdate_Queue         = PTHREAD_COND_INITIALIZER;


pthread_mutex_t mutex_lock_dataReponse_t    = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t UpdateReponse_Queue          = PTHREAD_COND_INITIALIZER;

Scene* g_sceneList;
int g_sceneCount = 0;

JSON_Value *Json_Value_checkReponses;
JSON_Value *Json_Value_InfoActionNeedReponses;
JSON_Value *g_checkRespList;
JSON* ConvertToLocalPacket(int reqType, const char* cloudPacket);
bool sendInfoDeviceFromDatabase();
bool sendInfoSceneFromDatabase();
bool add_JsonValueCheckReponses(JSON_Value *Json_Value_checkReponses,int type_action,char* Id,char* address_, long long int TimeCreat);
bool IsHaveAction_JsonValueCheckReponses(JSON_Value *Json_Value_checkReponses,char* type_action);
bool IsHaveAddress_Json_Value_InfoActionNeedReponses(JSON_Value *Json_Value_InfoActionNeedReponses,char* type_action,char* address);
bool IsHaveActionAndValue_JsonValueCheckReponses(JSON_Value *Json_Value_checkReponses,char* type_action,char* values);
bool add_JsonValueInfoActionNeedReponses(JSON_Value *Json_Value_InfoActionNeedReponses,int type_action,char* addressDevice, long long int TimeCreat,int status);
bool update_JsonValueInfoDevicesReponses(JSON_Value *Json_Value_InfoActionNeedReponses,int type_action,char* addressDevice, char* Key,int status);
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

bool append_Id2ListId(char** ListId, char* Id_append);
bool remove_Id2ListId(char** ListId, char* Id_remove);
bool Scene_GetFullInfo(JSON* packet);

bool compareSceneEntity(JSON* entity1, JSON* entity2) {
    if (entity1 && entity2) {
        int dpId1 = JSON_GetNumber(entity1, "dpId");
        int dpValue1 = JSON_GetNumber(entity1, "dpValue");
        char* dpAddr1 = JSON_GetText(entity1, "dpAddr");
        int dpId2 = JSON_GetNumber(entity2, "dpId");
        int dpValue2 = JSON_GetNumber(entity2, "dpValue");
        char* dpAddr2 = JSON_GetText(entity2, "dpAddr");
        if (dpId1 == dpId2 && dpValue1 == dpValue2 && strcmp(dpAddr1, dpAddr2) == 0) {
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
        pthread_cond_broadcast(&dataUpdate_Queue);
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
    Json_Value_checkReponses            = json_value_init_object();
    Json_Value_InfoActionNeedReponses   = json_value_init_object();
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
    if (pthread_mutex_init(&mutex_lock_dataReponse_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }

    Db_LoadSceneToRam();
    while(xRun!=0)
    {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if (size_queue > 0)
        {
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
            const char *Id              = json_object_get_string(json_object(schema),MOSQ_Id);
            const char *Extern          = json_object_get_string(json_object(schema),MOSQ_Extend);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            long long int TimeCreat_end = 0;
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
                }
            }

            JSON_Delete(recvPacket);
            JSON_Delete(payload);

            switch(type_action_t)
            {
                bool check_flag = false;
                char *topic;
                char *message;
                char *payload;

                char **andressDevice;
                char **typeDevice;
                char **actions;
                char **conditions;
                char **name;
                char **sceneType;
                int state = 0;
                int isLocal = 0;

                char **GroupListIntoDB;
                char **devicesIntoDB;
                char **typeDeviceIntoDB;
                char **actionsIntoDB;
                char **conditionsIntoDB;
                char **nameIntoDB = NULL;
                char **sceneTypeIntoDB;
                char **pidIntoDB;
                int stateIntoDB = 0;
                int isLocalIntoDB = 0;                

                char *SceneList;
                char *GroupList;
                char *groupAndress;
                char *devices;
                char *deviceID;
                char *dpID;
                char *dpValue;
                char *Key;
                char *pid;
                char *tmp,*tmp_devices;
                int dpValue_int = 0;
                unsigned char provider;

                case TYPE_CTR_DEVICE:
                case TYPE_CTR_GROUP_NORMAL:
                {
                    cJSON* originObj = cJSON_Parse(object_string);
                    char* senderId = JSON_GetText(originObj, "senderId");
                    cJSON* originDPs = cJSON_GetObjectItem(originObj, "dictDPs");
                    DeviceInfo deviceInfo;
                    char* deviceId;
                    int foundDevices = 0;
                    if (type_action_t == TYPE_CTR_DEVICE) {
                        deviceId = JSON_GetText(originObj, "deviceId");
                        foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                    } else {
                        deviceId = JSON_GetText(originObj, "groupAdress");
                        // foundDevices = Db_FindGroupByAddr(&deviceInfo, deviceId);
                    }

                    if (foundDevices == 1) {
                        cJSON* packet = cJSON_CreateObject();
                        cJSON_AddStringToObject(packet, "pid", deviceInfo.pid);
                        JSON_SetText(packet, "senderId", senderId);
                        cJSON* dictDPs = cJSON_AddArrayToObject(packet, "dictDPs");
                        JSON_ForEach(o, originDPs) {
                            int dpId = atoi(o->string);
                            dp_info_t dpInfo;
                            int dpFound = Db_FindDp(&dpInfo, deviceId, dpId);
                            if (dpFound) {
                                cJSON* dp = cJSON_CreateObject();
                                JSON_SetNumber(dp, "id", dpId);
                                JSON_SetText(dp, "addr", dpInfo.addr);
                                JSON_SetNumber(dp, "value", o->valueint);
                                cJSON_AddItemToArray(dictDPs, dp);
                            }
                        }
                        sendPacketTo(SERVICE_BLE, type_action_t, packet);
                        cJSON_Delete(packet);
                    }
                    cJSON_Delete(originObj);
                    break;
                }
                case TYPE_CTR_SCENE:
                {
                    JSON* packet = JSON_Parse(object_string);
                    char* sceneId = JSON_GetText(packet, "sceneId");
                    int state = JSON_GetNumber(packet, "state");

                    // Check if this scene is HC or local
                    bool isLocal = true;
                    int sceneType = 0;
                    for (int i = 0; i < g_sceneCount; i++) {
                        if (strcmp(g_sceneList[i].id, sceneId) == 0) {
                            isLocal = g_sceneList[i].isLocal;
                            sceneType = g_sceneList[i].type;
                            JSON_SetNumber(packet, "sceneType", g_sceneList[i].type);
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
                        sendPacketTo(SERVICE_BLE, TYPE_CTR_SCENE, packet);
                    }
                    cJSON_Delete(packet);
                    break;
                }
                case TYPE_ADD_GW:
                {
                    // addGateway(&db,object_string);

                    // get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_ADD_GW,MOSQ_ActResponse);
                    // replaceInfoFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_ADD_GW,MOSQ_ActResponse,val_input);
                    // mqttLocalPublish(topic, message);
                    // if(topic != NULL) free(topic);
                    // if(message != NULL) free(message);
                    break;
                }
                case TYPE_ADD_DEVICE:
                {
                    JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                    char* deviceId = JSON_GetText(localPacket, "deviceId");
                    int provider = JSON_GetNumber(localPacket, "provider");
                    logInfo("Adding device: %s", deviceId);

                    // Delete device from database if exist
                    logInfo("Delete device %s from database if exist", Id);
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
                case TYPE_DEL_DEVICE:
                {
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
                case TYPE_ADD_SCENE:
                {
                    JSON* packet = JSON_Parse(object_string);
                    int isLocal = JSON_GetNumber(packet, "isLocal");
                    Scene_GetFullInfo(packet);
                    Db_AddScene(packet);  // Insert scene into database
                    if (isLocal) {
                        sendPacketTo(SERVICE_BLE, TYPE_ADD_SCENE, packet);
                    }
                    JSON_Delete(packet);
                    break;
                }
                case TYPE_DEL_SCENE:
                {
                    JSON* packet = JSON_Parse(object_string);
                    char* sceneId = JSON_GetText(packet, "Id");
                    // Get sceneInfo from database to send to BLE
                    JSON* sceneInfo = Db_FindScene(sceneId);
                    if (sceneInfo) {
                        int isLocal = JSON_GetNumber(sceneInfo, "isLocal");
                        if (isLocal) {
                            sendPacketTo(SERVICE_BLE, TYPE_DEL_SCENE, sceneInfo);   // Send packet to BLE
                        }
                        Db_DeleteScene(sceneId);    // Delete scene from database
                    }
                    JSON_Delete(packet);
                    JSON_Delete(sceneInfo);
                    break;
                }
                case TYPE_UPDATE_SCENE:
                {
                    JSON* newScene = JSON_Parse(object_string);
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
                        }
                        // Save new scene to database
                        Db_DeleteScene(sceneId);
                        Db_AddScene(newScene);
                        sendNotiToUser("Cập nhật kịch bản thành công");
                    } else {
                        logInfo("Scene %s is not found", sceneId);
                    }

                    break;
                }
                case TYPE_ADD_GROUP_NORMAL:
                {
                    cJSON* originObj = cJSON_Parse(object_string);
                    devices = JSON_GetText(originObj, "devices");
                    char* groupAddr = JSON_GetText(originObj, "groupAdress");
                    cJSON* root = cJSON_CreateObject();
                    JSON_SetNumber(root, "reqType", type_action_t);
                    JSON_SetText(root, "groupAddr", groupAddr);
                    cJSON* devicesArray = cJSON_AddArrayToObject(root, "devices");
                    list_t* splitList = String_Split(devices, "|");
                    for (int i = 0; i < splitList->count; i++) {
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, (char*)splitList->items[i]);
                        if (foundDevices > 0) {
                            JSON_ArrayAddText(devicesArray, deviceInfo.addr);
                        }
                    }
                    sendPacketTo(SERVICE_BLE, TYPE_ADD_GROUP_NORMAL, root);
                    List_Delete(splitList);
                    cJSON_Delete(root);
                    cJSON_Delete(originObj);
                    //add inf group normal into TABLE_GROUP_INF and TABLE_DEVICES database
                    logInfo("Insert group data to database");
                    addNewGroupNormal(&db, object_string);

                    //add info reponse
                    pthread_mutex_lock(&mutex_lock_dataReponse_t);
                    devices         = json_object_get_string(json_object(object),KEY_DEVICES_GROUP);
                    groupAndress    = json_object_get_string(json_object(object),KEY_ADDRESS_GROUP);
                    size_ = 0;
                    name = str_split(devices,'|',&size_);
                    for (i = 0; i < (size_ - 1); i++)
                    {
                        leng = 0;
                        andressDevice = sql_getValueWithCondition(&db, &leng, KEY_UNICAST, TABLE_DEVICES_INF, KEY_DEVICE_ID, *(name+i));
                        if(leng > 0)
                        {
                            addDeviceToRespList(TYPE_ADD_GROUP_NORMAL, groupAndress, andressDevice[0]);
                            free_fields(andressDevice,leng);
                        }
                        else
                        {
                            LogError((get_localtime_now()),("Failed to get addressDevice form DeviceID\n"));
                        }
                    }
                    if(size_ > 0)
                    {
                        free_fields(name,size_);
                    }
                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
               
                    break;
                }
                case TYPE_DEL_GROUP_NORMAL:
                {
                    groupAndress = json_object_get_string(json_object(object),KEY_ADDRESS_GROUP);
                    devicesIntoDB = sql_getValueWithCondition(&db, &leng, KEY_DEVICES_GROUP, TABLE_GROUP_INF, KEY_ADDRESS_GROUP, groupAndress);
                    logInfo("Found %d devices of group %s in table %s", leng, groupAndress, TABLE_GROUP_INF);
                    if (leng > 0)
                    {
                        //push message for BLE-DEVICE
                        cJSON* originObj = cJSON_Parse(object_string);
                        char* groupAddr = JSON_GetText(originObj, "groupAdress");
                        char* devices = Db_FindDevicesInGroup(groupAddr);
                        cJSON* root = cJSON_CreateObject();
                        JSON_SetNumber(root, "reqType", type_action_t);
                        JSON_SetText(root, "groupAddr", groupAddr);
                        cJSON* devicesArray = cJSON_AddArrayToObject(root, "devices");
                        list_t* splitList = String_Split(devices, "|");
                        for (int i = 0; i < splitList->count; i++) {
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDevice(&deviceInfo, splitList->items[i]);
                            if (foundDevices > 0) {
                                JSON_ArrayAddText(devicesArray, deviceInfo.addr);
                            }
                        }
                        char* payload = cJSON_PrintUnformatted(root);
                        mqttLocalPublish("DEVICE_SERVICES/Ble/0", payload);
                        free(payload);
                        free(devices);
                        List_Delete(splitList);
                        cJSON_Delete(root);
                        cJSON_Delete(originObj);


                        //update state of group in GROUP_INF
                        sql_updateStateInTableWithCondition(&db,TABLE_GROUP_INF,KEY_STATE,TYPE_DEVICE_RESETED,KEY_ADDRESS_GROUP,groupAndress);
                        //update state of group in DEVICES  
                        sql_updateStateInTableWithCondition(&db,TABLE_DEVICES,KEY_STATE,TYPE_DEVICE_RESETED,KEY_ADDRESS,groupAndress);

                        //add info reponse
                        pthread_mutex_lock(&mutex_lock_dataReponse_t);
                        // groupAndress    = json_object_get_string(json_object(object),KEY_ADDRESS_GROUP);
                        size_ = 0;
                        name = str_split(devicesIntoDB[0],'|',&size_);
                        free_fields(devicesIntoDB,leng);
                        LogInfo((get_localtime_now()),("name[0] = %s\n",name[0]));
                        for(i=0;i<(size_-1);i++)
                        {
                            leng = 0;
                            andressDevice = sql_getValueWithCondition(&db,&leng,KEY_UNICAST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(name+i));
                            if(leng > 0)
                            {
                                LogInfo((get_localtime_now()),("andressDevice[0] = %s\n",andressDevice[0]));
                                add_JsonValueInfoActionNeedReponses(Json_Value_InfoActionNeedReponses,TYPE_DEL_GROUP_NORMAL,andressDevice[0],TimeCreat,TYPE_DEVICE_REPONSE_ADD_FROM_APP);
                                add_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_GROUP_NORMAL,groupAndress,andressDevice[0],TimeCreat);
                                free_fields(andressDevice,leng);                            
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get addressDevice form DeviceID\n"));
                            }

                            //remove groupAddress in to GroupList of each DeviceID
                            GroupListIntoDB = sql_getValueWithCondition(&db,&leng,KEY_GROUP_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(name+i));
                            
                            if(leng > 0)
                            {
                                check_flag = remove_Id2ListId(&GroupListIntoDB[0],groupAndress);
                                if(!check_flag)
                                {
                                    LogError((get_localtime_now()),("Failed to remove GroupAddress into  GroupList\n"));
                                }
                                else
                                {
                                    LogInfo((get_localtime_now()),("    GroupListIntoDB[0] after remove = %s\n",GroupListIntoDB[0]));
                                    sql_updateValueInTableWithCondition(&db,TABLE_DEVICES_INF,KEY_GROUP_LIST,GroupListIntoDB[0],KEY_DEVICE_ID,*(name+i));
                                }                       
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get GroupList form DeviceID\n"));
                            }
                        }   
                        pthread_mutex_unlock(&mutex_lock_dataReponse_t); 
                    }
                    else
                    {
                        LogError((get_localtime_now()),("TYPE_DEL_GROUP_NORMAL Failed\n"));
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_NORMAL:
                {
                    groupAndress = json_object_get_string(json_object(object),KEY_ADDRESS_GROUP);
                    if(groupAndress == NULL)
                    {
                        LogError((get_localtime_now()),("groupAndress is NULL\n"));
                        break;
                    }
                    LogInfo((get_localtime_now()),("groupAndress = %s\n",groupAndress));
                    devicesIntoDB   = sql_getValueWithCondition(&db,&leng,KEY_DEVICES_GROUP,TABLE_GROUP_INF,KEY_ADDRESS_GROUP,groupAndress);
                    if(leng < 1)
                    {
                        LogError((get_localtime_now()),("Failed to get devices of group from Database!!!"));
                        break;
                    }
                    LogInfo((get_localtime_now()),("devicesIntoDB = %s, size = %d\n",devicesIntoDB[0],leng));
                    devices         = json_object_get_string(json_object(object),KEY_DEVICES_GROUP);
                    dpValue = (char*)calloc(strlen(devices)+1,sizeof(char));
                    strcpy(dpValue,devices);

                    if(devices == NULL)
                    {
                        LogError((get_localtime_now()),("Failed to get devices of group from APP!!"));
                        break;
                    }
                    LogInfo((get_localtime_now()),("devices = %s\n",devices));

                    //get list device add
                    LogWarn((get_localtime_now()),("Start get list device need add!!!\n"));
                    size_ = 0;
                    name = str_split(dpValue,'|',&size_);
                    tmp = calloc(1,sizeof(char));
                    LogInfo((get_localtime_now()),("    size_ %d\n",size_));
                    for(i=0;i<(size_-1);i++)
                    {
                        LogInfo((get_localtime_now()),("    name[%d] = %s\n",i,*(name+i)));
                        if(*(name+i) == NULL)
                        {
                            LogError((get_localtime_now()),("Failed to get DeviceID from list devices from APP!\n"));
                            break;
                        }
                        if(!isContainString(devicesIntoDB[0],*(name+i)))// The deviceID not in the list devices from DB
                        {

                            //get list add to BLE
                            LogInfo((get_localtime_now()),("    name[%d] = %s\n",i,*(name+i)));
                            append_String2String(&tmp,*(name+i));
                            append_String2String(&tmp,"|");

                            //add reponse check add group normal
                            pthread_mutex_lock(&mutex_lock_dataReponse_t);
                            leng = 0;
                            andressDevice = sql_getValueWithCondition(&db,&leng,KEY_UNICAST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(name+i));
                            LogWarn((get_localtime_now()),("debug 1\n"));
                            if(leng > 0)
                            {
                                LogInfo((get_localtime_now()),("andressDevice[0] = %s\n",andressDevice[0]));
                                add_JsonValueInfoActionNeedReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_GROUP_NORMAL,andressDevice[0],TimeCreat,TYPE_DEVICE_REPONSE_ADD_FROM_APP);
                                LogWarn((get_localtime_now()),("debug 1\n"));
                                add_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_ADD_GROUP_NORMAL,groupAndress,andressDevice[0],TimeCreat);
                                LogWarn((get_localtime_now()),("debug 1\n"));
                                free_fields(andressDevice,leng); 
                                LogWarn((get_localtime_now()),("debug 1\n"));                           
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get addressDevice form DeviceID\n"));
                            }
                            pthread_mutex_unlock(&mutex_lock_dataReponse_t);

                            //add groupAddress in to GroupList of each DeviceID
                            GroupListIntoDB = sql_getValueWithCondition(&db,&leng,KEY_GROUP_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(name+i));
                            LogInfo((get_localtime_now()),("    leng = %d\n",leng));
                            LogInfo((get_localtime_now()),("    GroupListIntoDB[0] befor add = %s\n",GroupListIntoDB[0]));
                            LogInfo((get_localtime_now()),("    groupAndress = %s\n",groupAndress));
                            
                            if(leng > 0)
                            {
                                check_flag = append_Id2ListId(&GroupListIntoDB[0],groupAndress);
                                if(!check_flag)
                                {
                                    LogError((get_localtime_now()),("Failed to add GroupAddress into  GroupList\n"));
                                }
                                else
                                {
                                    LogInfo((get_localtime_now()),("    GroupListIntoDB[0] after add %s\n",GroupListIntoDB[0]));
                                    sql_updateValueInTableWithCondition(&db,TABLE_DEVICES_INF,KEY_GROUP_LIST,GroupListIntoDB[0],KEY_DEVICE_ID,*(name+i));
                                }                       
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get GroupList form DeviceID\n"));
                            }

                        }
                        // free(*(name+i));
                    }
                    free_fields(name,size_);
                    // free(dpValue);
                    //push message to BLE service for add
                    if(strlen(tmp) != 0)
                    {
                        LogInfo((get_localtime_now()),("    list device add = %s\n",tmp));
                        LogInfo((get_localtime_now()),("    strlen(tmp) = %ld\n",strlen(tmp)));
                        //push message to BLE_service
                        replaceValuePayloadTranMOSQ(&payload, KEY_DEVICES_GROUP,tmp,object_string);
                        LogWarn((get_localtime_now()),("payload = %s\n",payload));
                        get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_ADD_GROUP_NORMAL,Extern);
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_ADD_GROUP_NORMAL,MOSQ_ActResponse,Id,TimeCreat,payload);
                        mqttLocalPublish(topic, message);
                    }
                    else
                    {
                        LogError((get_localtime_now()),("    list device add is empty\n"));
                    }
                    free(tmp);
                    tmp = calloc(1,sizeof(char));
                    LogWarn((get_localtime_now()),("End get list device need add!!!\n"));


                    //get list device delete
                    size_ = 0;
                    name = str_split(devicesIntoDB[0],'|',&size_);
                    // devices         = json_object_get_string(json_object(object),KEY_DEVICES_GROUP);
                    LogWarn((get_localtime_now()),("Start get list device need delete!!!\n"));
                    LogInfo((get_localtime_now()),("    size_ %d\n",size_));

                    for(i=0;i<(size_-1);i++)
                    {
                        if(*(name+i) == NULL)
                        {
                            LogError((get_localtime_now()),("Failed to get DeviceID from list devices from APP!\n"));
                            break;
                        }
                        if(!isContainString(devices,*(name+i)))// no have into DB
                        {
                            append_String2String(&tmp,*(name+i));
                            append_String2String(&tmp,"|");

                            //add reponse check delete group normal
                            pthread_mutex_lock(&mutex_lock_dataReponse_t);
                            leng = 0;
                            andressDevice = sql_getValueWithCondition(&db,&leng,KEY_UNICAST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(name+i));
                            if(leng > 0)
                            {
                                LogInfo((get_localtime_now()),("andressDevice[0] = %s\n",andressDevice[0]));
                                add_JsonValueInfoActionNeedReponses(Json_Value_InfoActionNeedReponses,TYPE_DEL_GROUP_NORMAL,andressDevice[0],TimeCreat,TYPE_DEVICE_REPONSE_ADD_FROM_APP);
                                add_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_GROUP_NORMAL,groupAndress,andressDevice[0],TimeCreat);
                                free_fields(andressDevice,leng);
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get addressDevice form DeviceID\n"));
                            }

                            pthread_mutex_unlock(&mutex_lock_dataReponse_t);


                            //del groupAddress in to GroupList of each DeviceID
                            GroupListIntoDB = sql_getValueWithCondition(&db,&leng,KEY_GROUP_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(name+i));
                            LogInfo((get_localtime_now()),("    leng = %d\n",leng));
                            LogInfo((get_localtime_now()),("    GroupListIntoDB[0] befor remove = %s\n",GroupListIntoDB[0]));
                            LogInfo((get_localtime_now()),("    groupAndress = %s\n",groupAndress));
                            
                            if(leng > 0)
                            {
                                check_flag = remove_Id2ListId(&GroupListIntoDB[0],groupAndress);
                                if(!check_flag)
                                {
                                    LogError((get_localtime_now()),("Failed to remove GroupAddress into  GroupList\n"));
                                }
                                else
                                {
                                    LogInfo((get_localtime_now()),("    GroupListIntoDB[0] after remove %s\n",GroupListIntoDB[0]));
                                    sql_updateValueInTableWithCondition(&db,TABLE_DEVICES_INF,KEY_GROUP_LIST,GroupListIntoDB[0],KEY_DEVICE_ID,*(name+i));
                                }                       
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get GroupList form DeviceID\n"));
                            }

                        }

                        // free(*(name+i));
                    }
                    free_fields(name,size_);
                    //push message to BLE service for delete
                    if(strlen(tmp) != 0)
                    {
                        LogInfo((get_localtime_now()),("    list device delete = %s\n",tmp));
                        LogInfo((get_localtime_now()),("    strlen(tmp) = %ld\n",strlen(tmp)));
                        //push message to BLE_service
                        replaceValuePayloadTranMOSQ(&payload, KEY_DEVICES_GROUP,tmp,object_string);
                        LogWarn((get_localtime_now()),("payload = %s\n",payload));
                        get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_DEL_GROUP_NORMAL,Extern);
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_DEL_GROUP_NORMAL,MOSQ_ActResponse,Id,TimeCreat,payload);
                        mqttLocalPublish(topic, message);
                    }
                    else
                    {
                        LogError((get_localtime_now()),("    list device delete is empty\n"));
                    }


                    free(tmp);
                    LogWarn((get_localtime_now()),("Stop get list device need delete!!!\n\n"));


                    //update data devices in GROUP_INF
                    check_flag =  sql_updateValueInTableWithCondition(&db,TABLE_GROUP_INF,KEY_DEVICES_GROUP,devices,KEY_ADDRESS_GROUP,groupAndress);
                    if(check_flag)
                    {
                        LogInfo((get_localtime_now()),("Success to update devices in GROUP_INF\n"));
                    }
                    else
                    {
                        LogError((get_localtime_now()),("Failed to update devices in GROUP_INF\n"));
                    }
                    break;
                }
                case TYPE_ADD_GROUP_LINK:
                {
                    // Send request to BLE
                    JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                    char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                    sendPacketTo(SERVICE_BLE, type_action_t, localPacket);
                    JSON* devicesArray = JSON_GetObject(localPacket, "devices");
                    JSON* srcObj = JSON_Parse(object_string);
                    // Insert group information to database
                    Db_AddGroup(groupAddr, JSON_GetText(srcObj, "name"), JSON_GetText(srcObj, "devices"), false);
                    // Add this request to response list for checking response
                    JSON_ForEach(device, devicesArray) {
                        addDeviceToRespList(type_action_t, groupAddr, JSON_GetText(device, "deviceAddr"));
                    }
                    JSON_Delete(localPacket);
                    JSON_Delete(srcObj);
                    break;
                }
                case TYPE_DEL_GROUP_LINK:
                {
                    // Send request to BLE
                    JSON* localPacket = ConvertToLocalPacket(type_action_t, object_string);
                    sendPacketTo(SERVICE_BLE, TYPE_DEL_GROUP_LINK, localPacket);
                    // Delete group information from database
                    char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                    Db_DeleteGroup(groupAddr);
                    break;
                }
                case TYPE_UPDATE_GROUP_LINK:
                {
                    LogInfo((get_localtime_now()),("TYPE_UPDATE_GROUP_LINK \n"));
                    //push message to BLE_service
                    groupAndress    = json_object_get_string(json_object(object),KEY_ADDRESS_GROUP);
                    LogInfo((get_localtime_now()),("groupAndress = %s\n",groupAndress));
                    devices         = json_object_get_string(json_object(object),KEY_DEVICES_GROUP);
                    LogInfo((get_localtime_now()),("devices = %s\n",devices));
                    devicesIntoDB   = sql_getValueWithCondition(&db,&leng,KEY_DEVICES_GROUP,TABLE_GROUP_INF,KEY_ADDRESS_GROUP,groupAndress);
                    LogInfo((get_localtime_now()),("devicesIntoDB = %s \n",devicesIntoDB[0]));
                    dpValue = calloc(strlen(devices)|+1,sizeof(char));

                    strcpy(dpValue,devices);

                    //get list device add
                    LogWarn((get_localtime_now()),("Start get list device need add!!!\n"));
                    size_ = 0;
                    name = str_split(dpValue,'|',&size_);
                    tmp = calloc(1,sizeof(char));
                    
                    LogInfo((get_localtime_now()),("    size_ %d\n",size_));

                    for(i=0;i<(size_-1);i = i + 2)
                    {
                        tmp_devices = calloc(LENGTH_DEVICE_ID+4,sizeof(char));
                        LogInfo((get_localtime_now()),("    name[%d] = %s\n",i,*(name+i)));
                        LogInfo((get_localtime_now()),("    name[%d] = %s\n",i+1,*(name+i+1)));
                        append_String2String(&tmp_devices,*(name+i));
                        append_String2String(&tmp_devices,"|");
                        append_String2String(&tmp_devices,*(name+i+1));
                        append_String2String(&tmp_devices,"|");

                        LogInfo((get_localtime_now()),("    tmp_devices[%d] = %s\n",i,tmp_devices));
                        if(tmp_devices == NULL)
                        {
                            LogError((get_localtime_now()),("Failed to get DeviceID from list devices from APP!\n"));
                            break;
                        }
                        if(!isContainString(devicesIntoDB[0],tmp_devices))// The deviceID not in the list devices from DB
                        {
                            //get list add to BLE
                            LogInfo((get_localtime_now()),("    name[%d] = %s\n",i,*(name+i)));
                            append_String2String(&tmp,tmp_devices);

                            //add reponse check add group normal
                            pthread_mutex_lock(&mutex_lock_dataReponse_t);
                            leng = 0;
                            andressDevice = sql_getValueWithMultileCondition(&db,&leng,KEY_ADDRESS,TABLE_DEVICES,KEY_DEVICE_ID,*(name+i),KEY_DP_ID,*(name+i+1));
                            LogWarn((get_localtime_now()),("debug 1\n"));
                            if(leng > 0)
                            {
                                LogInfo((get_localtime_now()),("andressDevice[0] = %s\n",andressDevice[0]));
                                add_JsonValueInfoActionNeedReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_GROUP_LINK,andressDevice[0],TimeCreat,TYPE_DEVICE_REPONSE_ADD_FROM_APP);
                                LogWarn((get_localtime_now()),("debug 1\n"));
                                add_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_ADD_GROUP_LINK,groupAndress,andressDevice[0],TimeCreat);
                                LogWarn((get_localtime_now()),("debug 1\n"));
                                free_fields(andressDevice,leng); 
                                LogWarn((get_localtime_now()),("debug 1\n"));                           
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get addressDevice form DeviceID\n"));
                            }
                            pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                        }
                        free(tmp_devices);
                    }
                    
                    //push message to BLE service for add
                    if(strlen(tmp) != 0)
                    {
                        LogInfo((get_localtime_now()),("    list device add = %s\n",tmp));
                        LogInfo((get_localtime_now()),("    strlen(tmp) = %ld\n",strlen(tmp)));
                        //push message to BLE_service
                        replaceValuePayloadTranMOSQ(&payload, KEY_DEVICES_GROUP,tmp,object_string);
                        LogWarn((get_localtime_now()),("payload = %s\n",payload));
                        get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_ADD_GROUP_LINK,Extern);
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_ADD_GROUP_LINK,MOSQ_ActResponse,Id,TimeCreat,payload);
                        mqttLocalPublish(topic, message);
                    }
                    else
                    {
                        LogError((get_localtime_now()),("    list device add is empty\n"));
                    }

                    free_fields(name,size_);
                    free(dpValue);
                    free(tmp);
                    LogWarn((get_localtime_now()),("End get list device need add!!!\n"));


                    //get list device delete
                    size_ = 0;
                    LogInfo((get_localtime_now()),("    devicesIntoDB[0] %s\n",devicesIntoDB[0]));
                    tmp = calloc(1,sizeof(char));
                    size_ = 0;
                    name = str_split(devicesIntoDB[0],'|',&size_);
               

                    LogWarn((get_localtime_now()),("Start get list device need delete!!!\n"));
                    LogInfo((get_localtime_now()),("    size_ %d\n",size_));

                    for(i=0;i< (size_-1);i=i+2)
                    {

                        LogInfo((get_localtime_now()),("    name[%d] %s\n",i,*(name+i)));
                        LogInfo((get_localtime_now()),("    name[%d] %s\n",i+1,*(name+i+1)));
                        tmp_devices = calloc(LENGTH_DEVICE_ID+4,sizeof(char)); 
                        append_String2String(&tmp_devices,*(name+i));
                        append_String2String(&tmp_devices,"|");
                        append_String2String(&tmp_devices,*(name+i+1));
                        append_String2String(&tmp_devices,"|");
                        if(tmp_devices == NULL)
                        {
                            LogError((get_localtime_now()),("Failed to get DeviceID from list devices from APP!\n"));
                            break;
                        }
                        LogInfo((get_localtime_now()),("    devices[%d] %s\n",i,devices));
                        LogInfo((get_localtime_now()),("    tmp_devices[%d] %s\n",i,tmp_devices));
                        if(!isContainString(devices,tmp_devices))// no have into DB
                        {
                            LogInfo((get_localtime_now()),("    tmp_devices not into  devices\n"));
                            append_String2String(&tmp,tmp_devices);
                            LogInfo((get_localtime_now()),("    tmp[%d] %s\n",i,tmp));

                            //add reponse check delete group normal
                            pthread_mutex_lock(&mutex_lock_dataReponse_t);                           
                            leng = 0;
                            LogInfo((get_localtime_now()),("    name[%d] %s\n",i,*(name+i)));
                            LogInfo((get_localtime_now()),("    dpid[%d] %s\n",i,*(name+i+1)));
                            andressDevice = sql_getValueWithMultileCondition(&db,&leng,KEY_ADDRESS,TABLE_DEVICES,KEY_DEVICE_ID,*(name+i),KEY_DP_ID,*(name+i+1));
                            if(leng > 0)
                            {
                                LogInfo((get_localtime_now()),("andressDevice[0] = %s\n",andressDevice[0]));
                                add_JsonValueInfoActionNeedReponses(Json_Value_InfoActionNeedReponses,TYPE_DEL_GROUP_LINK,andressDevice[0],TimeCreat,TYPE_DEVICE_REPONSE_ADD_FROM_APP);
                                add_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_GROUP_LINK,groupAndress,andressDevice[0],TimeCreat);
                                free_fields(andressDevice,leng);                            
                            }
                            else
                            {
                                LogError((get_localtime_now()),("Failed to get addressDevice form DeviceID\n"));
                            }
                            pthread_mutex_unlock(&mutex_lock_dataReponse_t);

                            //remove GroupAddress into GroupList of Table DEVICES_INF in database




                        }
                        else
                        {
                            LogInfo((get_localtime_now()),("    tmp_devices into  devices\n"));
                        }
                        free(tmp_devices);
                    }   
                   
                    //push message to BLE service for delete
                    if(strlen(tmp) != 0)
                    {
                        LogInfo((get_localtime_now()),("    list device delete = %s\n",tmp));
                        LogInfo((get_localtime_now()),("    strlen(tmp) = %ld\n",strlen(tmp)));
                        //push message to BLE_service
                        replaceValuePayloadTranMOSQ(&payload, KEY_DEVICES_GROUP,tmp,object_string);
                        LogWarn((get_localtime_now()),("payload = %s\n",payload));
                        get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_DEL_GROUP_LINK,Extern);
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_DEL_GROUP_LINK,MOSQ_ActResponse,Id,TimeCreat,payload);
                        mqttLocalPublish(topic, message);
                    }
                    else
                    {
                        LogError((get_localtime_now()),("    list device delete is empty\n"));
                    }
                    if(size_ > 0) free_fields(name,size_);
                    free(tmp);
                    LogWarn((get_localtime_now()),("Stop get list device need delete!!!\n\n"));

                    //update data devices in GROUP_INF
                    sql_updateValueInTableWithCondition(&db,TABLE_GROUP_INF,KEY_DEVICES_GROUP,devices,KEY_ADDRESS_GROUP,groupAndress);
                    break;
                }
                case TYPE_MANAGER_PING_ON_OFF:
                {
                    get_topic(&topic,MOSQ_LayerService_Manager,MOSQ_NameService_Manager_ServieceManager,TYPE_MANAGER_PING_ON_OFF,Extern);
                    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_MANAGER_PING_ON_OFF,MOSQ_Reponse,Id,TimeCreat,object_string);
                    mqttLocalPublish(topic, message);
                    break;
                }
                case GW_RESPONSE_DIM_LED_SWITCH_HOMEGY:
                {
                    break;
                }
                case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_SUCCESS:
                {
                    pthread_mutex_lock(&mutex_lock_dataReponse_t);                   
                    update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_DEVICE,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_ACTIVE,TYPE_DEVICE_ADD_SUCCES_HC);
                    pthread_cond_broadcast(&UpdateReponse_Queue);
                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                    break;                        
                }
                case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_UNSUCCESS:
                {
                    pthread_mutex_lock(&mutex_lock_dataReponse_t);                   
                    update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_DEVICE,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_ACTIVE,TYPE_DEVICE_ADD_UNSUCCES_HC);
                    pthread_cond_broadcast(&UpdateReponse_Queue);
                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                    break;                        
                }
                case GW_RESPONSE_SAVE_GATEWAY_RD:
                case GW_RESPONSE_SAVE_GATEWAY_HG:
                {
                    pthread_mutex_lock(&mutex_lock_dataReponse_t);
                    update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_DEVICE,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_SAVE_GATEWAY,TYPE_DEVICE_ADD_SUCCES_HC);
                    pthread_cond_broadcast(&UpdateReponse_Queue);
                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                    break;
                }
                case GW_RESPONSE_SCENE_LC_WRITE_INTO_DEVICE:
                {
                    pthread_mutex_lock(&mutex_lock_dataReponse_t);

                    if(IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_ADD_SCENE_STRING) && !IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_SCENE_STRING))
                    {           
                        update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_SCENE_LC_ACTIONS,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_CREATE_SCENE_LC,TYPE_DEVICE_ADD_SUCCES_HC);
                        pthread_cond_broadcast(&UpdateReponse_Queue);
                    }
                    else if(IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_SCENE_STRING) && !IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_ADD_SCENE_STRING))
                    {           
                        update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_DEL_SCENE_LC_ACTIONS,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_DEL_SCENE_LC,TYPE_DEVICE_ADD_SUCCES_HC);
                        pthread_cond_broadcast(&UpdateReponse_Queue);
                    }
                    else
                    {
                        LogError((get_localtime_now()),("Error "));
                    }

                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                    break;  
                }
                case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE:
                {
                    pthread_mutex_lock(&mutex_lock_dataReponse_t); 

                    if(IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_ADD_SCENE_STRING) && !IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_SCENE_STRING))
                    {           
                        update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_ADD_SCENE_LC_CONDITIONS,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_SET_SCENE_LC,TYPE_DEVICE_ADD_SUCCES_HC);
                        pthread_cond_broadcast(&UpdateReponse_Queue);
                    }
                    else if(IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_DEL_SCENE_STRING) && !IsHaveAction_JsonValueCheckReponses(Json_Value_checkReponses,TYPE_ADD_SCENE_STRING))
                    {           
                        update_JsonValueInfoDevicesReponses(Json_Value_InfoActionNeedReponses,TYPE_DEL_SCENE_LC_CONDITIONS,json_object_get_string(json_object(object),KEY_DEVICE_ID),KEY_DEL_SCENE_LC,TYPE_DEVICE_ADD_SUCCES_HC);
                        pthread_cond_broadcast(&UpdateReponse_Queue);
                    }
                    else
                    {
                        LogError((get_localtime_now()),("Error "));
                    }
                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                    break;  
                }
                case GW_RESPONSE_DEVICE_KICKOUT:
                {
                    deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);
                    if(deviceID == NULL)
                    {
                        LogError((get_localtime_now()),("deviceID is empty!\n"));
                        break;
                    }
                    leng   = sql_getNumberRowWithCondition(&db,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
                    LogInfo((get_localtime_now()),("    leng = %d\n",leng));
                    if(leng == -1)
                    {
                        LogError((get_localtime_now()),("Get deviceID failed to database.\n"));
                        break; 
                    }
                    else
                    {
                        //delete info device into scenes
                        SceneList = delDeviceIntoScenes(&db,deviceID);
                        if(strlen(SceneList) == 0)
                        {
                            LogError((get_localtime_now()),("Failed to delete info deviceID %s into scenes\n",deviceID));
                        }
                        else
                        {
                            //update info sceneID to AWS
                            LogInfo((get_localtime_now()),("    SceneList need update = %s\n",SceneList));
                            name = str_split(SceneList,'|',&size_);
                            for(i=0;i<size_-1;i++)
                            {
                                //check info Scene after update  ->>> delete scene or retype scene if need
                                LogInfo((get_localtime_now()),("    SceneID[%d]  = %s\n",i,*(name+i)));
                                check_flag = checkTypeSceneWithInfoIntoDatabase(&db,*(name+i));
                                if(check_flag)
                                {
                                    number = sql_getNumberRowWithCondition(&db,TABLE_SCENE_INF,KEY_ID_SCENE,*(name+i));
                                    LogInfo((get_localtime_now()),("    number  = %d\n",number));
                                    if(number == 0)
                                    {
                                        payload = NULL;
                                        message = NULL;
                                        topic = NULL;
                                        getPayloadReponseDeleteSceneAWS(&payload,*(name+i));
                                        get_topic(&topic,MOSQ_LayerService_App,SERVICE_AWS,GW_RESPONSE_DEL_SCENE_HC,MOSQ_ActResponse);
                                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,GW_RESPONSE_DEL_SCENE_HC,MOSQ_ActResponse,*(name+i),TimeCreat,payload);
                                        LogInfo((get_localtime_now()),("    message = %s\n",message));
                                        mqttLocalPublish(topic, message);
                                    }
                                    else
                                    {
                                        //push update AWS
                                        getInfoSceneIdFromDatabase(&db,&dpValue,*(name+i));
                                        LogInfo((get_localtime_now()),("    dpValue = %s\n",dpValue));
                                        getPayloadReponseValueSceneAWS(&payload,SENDER_HC_VIA_CLOUD, TYPE_UPDATE_SCENE, dpValue);
                                        LogInfo((get_localtime_now()),("    payload = %s\n",payload));

                                        get_topic(&topic,MOSQ_LayerService_App,SERVICE_AWS,GW_RESPONSE_UPDATE_SCENE,MOSQ_ActResponse);
                                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,GW_RESPONSE_UPDATE_SCENE,MOSQ_ActResponse,*(name+i),TimeCreat,payload);
                                        LogInfo((get_localtime_now()),("    message = %s\n",message));
                                        mqttLocalPublish(topic, message);
                                    }                               
                                }
                                else
                                {
                                    LogError((get_localtime_now()),("Failed to check info scene in database!\n"));
                                }
                            }
                            free_fields(name,size_);
                        }

                        //delete info device into group
                        GroupList =   delDeviceIntoGroups(&db,deviceID);
                        if(strlen(GroupList) == 0)
                        {
                            LogError((get_localtime_now()),("Failed to delete info deviceID %s into scenes\n",deviceID));
                        }
                        else
                        {
                            //update info sceneID to AWS
                            LogInfo((get_localtime_now()),("    GroupList need update = %s\n",GroupList));
                            name = str_split(GroupList,'|',&size_);
                            LogInfo((get_localtime_now()),("    size_ = %d\n",size_));
                            for(i=0;i<size_-1;i++)
                            {
                                payload = NULL;
                                //push update AWS

                                devicesIntoDB = sql_getValueWithCondition(&db,&leng,KEY_DEVICES_GROUP,TABLE_GROUP_INF,KEY_ADDRESS_GROUP,*(name+i));

                                if(strlen(devicesIntoDB[0]) == 0)
                                {
                                    //delete group
                                    getPayloadReponseDeleteGroupAWS(&payload,*(name+i));
                                    LogInfo((get_localtime_now()),("    payload = %s\n",payload));

                                    get_topic(&topic,MOSQ_LayerService_App,SERVICE_AWS,GW_RESPONSE_DEL_GROUP_LINK,MOSQ_ActResponse);
                                    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,GW_RESPONSE_DEL_GROUP_LINK,MOSQ_ActResponse,*(name+i),TimeCreat,payload);
                                    LogInfo((get_localtime_now()),("    message = %s\n",message));
                                    mqttLocalPublish(topic, message);
                                }
                                else
                                {
                                    //update info group
                                    getPayloadReponseDevicesGroupAWS(&payload,*(name+i),devicesIntoDB[0]);
                                    LogInfo((get_localtime_now()),("    payload = %s\n",payload));

                                    get_topic(&topic,MOSQ_LayerService_App,SERVICE_AWS,GW_RESPONSE_UPDATE_GROUP,MOSQ_ActResponse);
                                    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,GW_RESPONSE_UPDATE_GROUP,MOSQ_ActResponse,*(name+i),TimeCreat,payload);
                                    LogInfo((get_localtime_now()),("    message = %s\n",message));
                                    mqttLocalPublish(topic, message);
                                }
                            }
                            free_fields(name,size_);
                        }


                        //get state Device into Database
                        state = sql_getNumberWithCondition(&db,&leng,KEY_STATE,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
                        LogInfo((get_localtime_now()),("    state = %d\n",state));

                        if(state == TYPE_DEVICE_ONLINE || state == TYPE_DEVICE_OFFLINE)
                        {
                            //push massege to AWS for delete device in Cloud
                            get_topic(&topic,MOSQ_LayerService_App,SERVICE_AWS,GW_RESPONSE_DEVICE_KICKOUT,MOSQ_ActResponse);
                            getPayloadReponseDeleteDeviceAWS(&payload,deviceID);
                            LogInfo((get_localtime_now()),("    payload = %s\n",payload));
                            getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,GW_RESPONSE_DEVICE_KICKOUT,MOSQ_ActResponse,deviceID,TimeCreat,payload);
                            LogInfo((get_localtime_now()),("    message = %s\n",message));
                            mqttLocalPublish(topic, message);
                        }
                        
                        //delete device into database
                        check_flag = delDevice(&db,deviceID);
                        if(!check_flag)
                        {
                            LogError((get_localtime_now()),("Failed to delete  deviceID %s into database\n",deviceID));
                        }
                        sendInfoDeviceFromDatabase();
                        //push message for Notifile
                        topic = NULL;
                        get_topic(&topic,MOSQ_LayerService_Support,MOSQ_NameService_Support_Notifile,type_action_t,MOSQ_Reponse);
                        mqttLocalPublish(topic, message);
                                        
                        //push message to Rule
                        topic = NULL;
                        get_topic(&topic,MOSQ_LayerService_Support,MOSQ_NameService_Support_Rule_Schedule,TYPE_DEL_DEVICE,Extern);
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_DEL_DEVICE,MOSQ_ActResponse,Id,TimeCreat,object_string);
                        mqttLocalPublish(topic, message);

                        if(topic != NULL) free(topic);
                        if(message != NULL) free(message);
                    }
                    break;  
                }
                case GW_RESPONSE_ADD_GROUP_LIGHT:
                {
                    deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);
                    char* groupAddr = json_object_get_string(json_object(object), "value");
                    if(deviceID == NULL)
                    {
                        break;
                    }
                    pthread_mutex_lock(&mutex_lock_dataReponse_t);
                    if (requestIsInRespList(TYPE_ADD_GROUP_NORMAL, groupAddr)) {
                        updateDeviceRespStatus(TYPE_ADD_GROUP_NORMAL, groupAddr, deviceID, 1);
                    } else if (requestIsInRespList(TYPE_ADD_GROUP_LINK, groupAddr)) {
                        updateDeviceRespStatus(TYPE_ADD_GROUP_LINK, groupAddr, deviceID, 1);
                    }
                    pthread_cond_broadcast(&UpdateReponse_Queue);
                    pthread_mutex_unlock(&mutex_lock_dataReponse_t);
                    break;
                }
            }
        } else if(size_queue == QUEUE_SIZE) {
            pthread_mutex_unlock(&mutex_lock_t);
        }
        pthread_mutex_unlock(&mutex_lock_t);
        CHECK_REPONSE_RESULT();
        executeScenes();
        usleep(10000);
    }
    return 0;
}

bool sendInfoDeviceFromDatabase()
{
    int reponse = 0;
    long long int TimeCreat;
    char *payload, *message, *topic;
    TimeCreat = timeInMilliseconds();
    getInfoDeviceFromDatabase(&db, &payload);
    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_DEVICES,MOSQ_ActResponse,"object",TimeCreat,payload);
    get_topic(&topic,MOSQ_LayerService_Support,MOSQ_NameService_Support_Rule_Schedule,TYPE_GET_INF_DEVICES,MOSQ_Reponse);
    mqttLocalPublish(topic, message);

    usleep(100);
    topic = NULL;
    get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_GET_INF_DEVICES,MOSQ_Reponse);
    mqttLocalPublish(topic, message);
             
    usleep(100);
    topic = NULL;
    get_topic(&topic,MOSQ_LayerService_Support,MOSQ_NameService_Support_Tool,TYPE_GET_INF_DEVICES,MOSQ_Reponse);
    mqttLocalPublish(topic, message);

    if(topic    != NULL) free(topic);
    if(message  != NULL) free(message);
    if(payload  != NULL) free(payload);
}

bool sendInfoSceneFromDatabase()
{
    int reponse;
    char *payload;
    char *message;
    char *topic;
    long long int TimeCreat = timeInMilliseconds();
    getInfoTypeSceneFromDatabase(&db,&payload,KEY_SCENE_TYPE_HC);

    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_SCENES,MOSQ_ActResponse,"object",TimeCreat,payload);
    get_topic(&topic,MOSQ_LayerService_Support,MOSQ_NameService_Support_Rule_Schedule,TYPE_GET_INF_SCENES,MOSQ_Reponse);
    mqttLocalPublish(topic, message);
    
    if(topic    != NULL) free(topic);
    if(message  != NULL) free(message);
    if(payload  != NULL) free(payload);

    return true;
}

bool add_JsonValueCheckReponses(JSON_Value *Json_Value_checkReponses,int type_action,char* Id,char* address_, long long int TimeCreat)
{
    char *serialized_string = NULL;
    char *temp;

    if(type_action == TYPE_ADD_DEVICE)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,address_,KEY_TIMECREAT);
        json_object_dotset_number(json_object(Json_Value_checkReponses),temp,TimeCreat);
        serialized_string = json_serialize_to_string_pretty(Json_Value_checkReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;
    }
    else if(type_action == TYPE_ADD_SCENE_LC_ACTIONS || type_action == TYPE_ADD_SCENE_LC_CONDITIONS)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",TYPE_ADD_SCENE,Id,KEY_TIMECREAT);
        json_object_dotset_number(json_object(Json_Value_checkReponses),temp,TimeCreat);
        free(temp);

        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%d",TYPE_ADD_SCENE,Id,type_action);

        JSON_Array * root_array        = json_object_dotget_array(json_object(Json_Value_checkReponses),temp);
        if(root_array == NULL)
        {
            JSON_Value *root_value    = json_value_init_array();
            json_array_append_string(json_array(root_value),address_);
            json_object_dotset_value(json_object(Json_Value_checkReponses),temp,root_value);        
        }
        else
        {
            int size_array = json_array_get_count(root_array);
            int i  = 0;
            for(i=0;i<size_array;i++)
            {
                char *checkAddress = json_array_get_string(root_array,i);
                if(isMatchString(checkAddress,address_))
                {
                    free(temp);
                    return false;
                }
            }
            json_array_append_string(root_array,address_);
        }
        free(temp);
        serialized_string = json_serialize_to_string_pretty(Json_Value_checkReponses);
        json_free_serialized_string(serialized_string);
        return true;
    }
    else if(type_action == TYPE_DEL_SCENE_LC_ACTIONS || type_action == TYPE_DEL_SCENE_LC_CONDITIONS)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",TYPE_DEL_SCENE_LC,Id,KEY_TIMECREAT);
        json_object_dotset_number(json_object(Json_Value_checkReponses),temp,TimeCreat);
        free(temp);

        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%d",TYPE_DEL_SCENE_LC,Id,type_action);

        JSON_Array * root_array        = json_object_dotget_array(json_object(Json_Value_checkReponses),temp);
        if(root_array == NULL)
        {
            JSON_Value *root_value    = json_value_init_array();
            json_array_append_string(json_array(root_value),address_);
            json_object_dotset_value(json_object(Json_Value_checkReponses),temp,root_value);        
        }
        else
        {
            int size_array = json_array_get_count(root_array);
            int i  = 0;
            for(i=0;i<size_array;i++)
            {
                char *checkAddress = (char*)json_array_get_string(root_array,i);
                if(isMatchString(checkAddress,address_))
                {
                    free(temp);
                    return false;
                }
            }
            json_array_append_string(root_array,address_);
        }
        free(temp);
        serialized_string = json_serialize_to_string_pretty(Json_Value_checkReponses);
        json_free_serialized_string(serialized_string);
        return true;
    }    
    
    else if(type_action == TYPE_ADD_GROUP_NORMAL || type_action == TYPE_DEL_GROUP_NORMAL)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,Id,KEY_TIMECREAT);
        json_object_dotset_number(json_object(Json_Value_checkReponses),temp,TimeCreat);
        free(temp);

        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%d",type_action,Id,type_action);

        JSON_Array * root_array        = json_object_dotget_array(json_object(Json_Value_checkReponses),temp);
        if(root_array == NULL)
        {
            JSON_Value *root_value    = json_value_init_array();
            json_array_append_string(json_array(root_value),address_);
            json_object_dotset_value(json_object(Json_Value_checkReponses),temp,root_value);        
        }
        else
        {
            int size_array = json_array_get_count(root_array);
            int i  = 0;
            for(i=0;i<size_array;i++)
            {
                char *checkAddress = json_array_get_string(root_array,i);
                if(isMatchString(checkAddress,address_))
                {
                    free(temp);
                    return false;
                }
            }
            json_array_append_string(root_array,address_);
        }
        free(temp);
        return true;
    } 
    else if(type_action == TYPE_CTR_DEVICE )
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,Id,KEY_TIMECREAT);
        json_object_dotset_number(json_object(Json_Value_checkReponses),temp,TimeCreat);
        free(temp);

        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,Id,KEY_VALUE);
        json_object_dotset_string(json_object(Json_Value_checkReponses),temp,address_);
        free(temp);

        serialized_string = json_serialize_to_string_pretty(Json_Value_checkReponses);
        json_free_serialized_string(serialized_string);
        return true;
    }
    else if (type_action == TYPE_ADD_GROUP_LINK || type_action == TYPE_DEL_GROUP_LINK)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,Id,KEY_TIMECREAT);
        json_object_dotset_number(json_object(Json_Value_checkReponses),temp,TimeCreat);
        free(temp);

        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%d",type_action,Id,type_action);

        JSON_Array * root_array        = json_object_dotget_array(json_object(Json_Value_checkReponses),temp);
        if(root_array == NULL)
        {
            JSON_Value *root_value    = json_value_init_array();
            json_array_append_string(json_array(root_value),address_);
            json_object_dotset_value(json_object(Json_Value_checkReponses),temp,root_value);        
        }
        else
        {
            int size_array = json_array_get_count(root_array);
            int i  = 0;
            for(i=0;i<size_array;i++)
            {
                char *checkAddress = json_array_get_string(root_array,i);
                if(isMatchString(checkAddress,address_))
                {
                    free(temp);
                    return false;
                }
            }
            json_array_append_string(root_array,address_);
        }
        free(temp);
        serialized_string = json_serialize_to_string_pretty(Json_Value_checkReponses);
        json_free_serialized_string(serialized_string);
        return true;        
    }
    return false;
}

bool IsHaveAction_JsonValueCheckReponses(JSON_Value *Json_Value_checkReponses,char* type_action)
{
    if(type_action == NULL || Json_Value_checkReponses == NULL)
    {
        LogError((get_localtime_now()),("type_action or Json_Value_checkReponses is NULL"));
        return false;
    }
    if(json_object_has_value(json_object(Json_Value_checkReponses),type_action))
    {
        LogInfo((get_localtime_now()),("type_action = %s is have into Json_Value_checkReponses",type_action));
        return true;
    }
    else
    {
        LogError((get_localtime_now()),("type_action = %s is not have into Json_Value_checkReponses",type_action));
        return false;
    }
}

bool IsHaveAddress_Json_Value_InfoActionNeedReponses(JSON_Value *Json_Value_InfoActionNeedReponses, char* type_action, char* address)
{
    if(type_action == NULL || Json_Value_InfoActionNeedReponses == NULL || address == NULL)
    {
        LogError((get_localtime_now()),("type_action or Json_Value_InfoActionNeedReponses or address is NULL"));
        return false;
    }
    char *tmp = (char*)calloc(50,sizeof(char));
    sprintf(tmp, "%s.%s", type_action, address);
    if(tmp == NULL)
    {
        return false;
    }
    if(json_object_dothas_value(json_object(Json_Value_InfoActionNeedReponses), tmp))
    {
        LogInfo((get_localtime_now()),(" %s is have into Json_Value_InfoActionNeedReponses",tmp));
        free(tmp);
        return true;
    }
    else
    {
        LogError((get_localtime_now()),(" %s is not have into Json_Value_InfoActionNeedReponses",tmp));
        free(tmp);
        return false;
    }
}

bool IsHaveActionAndValue_JsonValueCheckReponses(JSON_Value *Json_Value_checkReponses,char* type_action,char* values)
{
    if(type_action == NULL || Json_Value_checkReponses == NULL || values == NULL)
    {
        LogError((get_localtime_now()),("type_action or values or Json_Value_checkReponses is NULL"));
        return false;
    }

    char *tmp = calloc(100,sizeof(char));
    sprintf(tmp,"%s.%s",type_action,values);

    if(json_object_dothas_value(json_object(Json_Value_checkReponses),tmp))
    {
        free(tmp);
        return true;
    }
    else
    {
        free(tmp);
        return false;
    }
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

bool add_JsonValueInfoActionNeedReponses(JSON_Value *Json_Value_InfoActionNeedReponses, int type_action, char* addressDevice, long long int TimeCreat, int status)
{
    char *serialized_string = NULL;
    char *temp;

    if(type_action == TYPE_ADD_DEVICE)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_SAVE_GATEWAY);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status); 

        free(temp);
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_ACTIVE);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;        
    }
    else if(type_action == TYPE_ADD_SCENE_LC_ACTIONS)
    { 
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_CREATE_SCENE_LC);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;         
    }
    else if(type_action == TYPE_ADD_SCENE_LC_CONDITIONS)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_SET_SCENE_LC);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;         
    }
    else if(type_action == TYPE_DEL_SCENE_LC_ACTIONS)
    { 
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_DEL_SCENE_LC);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;         
    }
    else if(type_action == TYPE_DEL_SCENE_LC_CONDITIONS)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_DEL_SCENE_LC);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;         
    }    
    else if(type_action == TYPE_ADD_GROUP_NORMAL)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s", type_action, addressDevice, KEY_ADD_GROUP_NORMAL);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses), temp, status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;            
    }
    else if(type_action == TYPE_DEL_GROUP_NORMAL)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_DEL_GROUP_NORMAL);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;            
    }
    else if(type_action == TYPE_CTR_DEVICE)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_CTL);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;            
    }
    else if(type_action == TYPE_ADD_GROUP_LINK)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_ADD_GROUP_LINK);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;          
    }
    else if(type_action == TYPE_DEL_GROUP_LINK)
    {
        temp = calloc(50,sizeof(char));
        sprintf(temp,"%d.%s.%s",type_action,addressDevice,KEY_DEL_GROUP_LINK);
        json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);
        serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
        json_free_serialized_string(serialized_string);
        free(temp);
        return true;          
    }
    return false;
}

bool update_JsonValueInfoDevicesReponses(JSON_Value *Json_Value_InfoActionNeedReponses,int type_action,char* addressDevice, char* Key,int status)
{
    char *serialized_string = NULL;

    char *temp = calloc(50,sizeof(char));
    sprintf(temp,"%d.%s.%s",type_action,addressDevice,Key);
    json_object_dotset_number(json_object(Json_Value_InfoActionNeedReponses),temp,status);

    serialized_string = json_serialize_to_string_pretty(Json_Value_InfoActionNeedReponses);
    json_free_serialized_string(serialized_string);
    return true;    
}

bool append_Id2ListId(char** ListId, char* Id_append)
{
    bool check_result = false;
    if(Id_append == NULL)
    {
        return false;
    }
    LogInfo((get_localtime_now()),("remove_Id2ListId"));
    LogInfo((get_localtime_now()),("    ListId = %s",*ListId));
    LogInfo((get_localtime_now()),("    Id_append = %s",Id_append));
    JSON_Value *Object          =   json_parse_string_with_comments(*ListId);
    LogInfo((get_localtime_now()),("    json_parse_string_with_comments is success"));
    JSON_Array * root_array     =   json_value_get_array(Object);
    int count = json_array_get_count(root_array);
    LogInfo((get_localtime_now()),("    count root_array = %d",count));
    if(count <= 0)
    {
        check_result = false;
    }
    else
    {
        char *serialized_string = NULL;
        int i  = 0;
        for(i=0;i<count;i++)
        {
            if(!isMatchString(Id_append,json_array_get_string(root_array,i)))
            {
                check_result = true;
            }
        }
        if(check_result)
        {
            json_array_append_string(root_array,Id_append);
            serialized_string = json_serialize_to_string_pretty(Object);
            int size_t = strlen(serialized_string);
            *ListId = malloc(size_t+1);
            memset(*ListId,'\0',size_t+1);
            strcpy(*ListId,serialized_string);    
            json_free_serialized_string(serialized_string);

        }
    json_value_free(Object);    
    }
    return check_result;
}

bool remove_Id2ListId(char** ListId, char* Id_remove)
{
    bool check_result = false;
    if(Id_remove == NULL)
    {
        return false;
    }
    JSON_Value *Object          =   json_parse_string_with_comments(*ListId);
    JSON_Array * root_array     =   json_value_get_array(Object);
    int count = json_array_get_count(root_array);
    if(count <= 0)
    {
        check_result = false;
    }
    else
    {
        char *serialized_string = NULL;
        int i  = 0;
        for(i=0;i<count;i++)
        {
            if(isMatchString(Id_remove,json_array_get_string(root_array,i)))
            {
                json_array_remove(root_array,i);
                check_result = true;
                break;
            }
        }
        serialized_string = json_serialize_to_string_pretty(Object);
        int size_t = strlen(serialized_string);
        *ListId = malloc(size_t+1);
        memset(*ListId,'\0',size_t+1);
        strcpy(*ListId,serialized_string);    
        json_free_serialized_string(serialized_string);
    }
    return check_result;
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
            if ((reqType == TYPE_ADD_GROUP_LINK || reqType == TYPE_DEL_GROUP_LINK) && splitList->count % 2 == 0) {
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
            } else if (reqType == TYPE_ADD_GROUP_NORMAL || reqType == TYPE_DEL_GROUP_NORMAL) {
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


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
JSON *g_checkRespList;
static bool g_mosqIsConnected = false;

JSON* parseGroupLinkDevices(const char* devices);
JSON* parseGroupNormalDevices(const char* devices);
JSON* ConvertToLocalPacket(int reqType, const char* cloudPacket);
bool sendInfoDeviceFromDatabase();
bool sendInfoSceneFromDatabase();
// Add device that need to check response to response list
void addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr);
// Check and get the JSON_Object of request that is in response list
JSON* requestIsInRespList(int reqType, const char* itemId);
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

bool compareDeviceById(JSON* device1, JSON* device2) {
    if (device1 && device2) {
        char* deviceId1 = JSON_GetText(device1, "deviceId");
        char* deviceId2 = JSON_GetText(device2, "deviceId");
        if (StringCompare(deviceId1, deviceId2)) {
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

void Mosq_Init() {
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
        return;
    }
    logInfo("Mosq_Init() done");
    g_mosqIsConnected = true;
}

void Mosq_ProcessLoop() {
    if (g_mosqIsConnected) {
        int rc = mosquitto_loop(mosq, 5, 1);
        if (rc != 0) {
            logError("mosquitto_loop error: %d.", rc);
            g_mosqIsConnected = false;
        }
    } else {
        mosquitto_destroy(mosq);
        Mosq_Init();
    }
}

void checkResponseLoop() {
    long long int currentTime = timeInMilliseconds();
    int respCount = JArr_Count(g_checkRespList);

    // Loop through all request that need to check response
    for (int i = 0; i < respCount; i++) {
        JSON* respItem = JArr_GetObject(g_checkRespList, i);
        long long int createdTime  = JSON_GetNumber(respItem, "createdTime");
        JSON* devices   = JSON_GetObject(respItem, "devices");
        int deviceCount       = JArr_Count(devices);
        // Get status of all devices in the response object
        int successCount = 0, failedCount = 0, notRespCount = 0;
        for (int d = 0; d < deviceCount; d++) {
            JSON* device = JArr_GetObject(devices, d);
            int deviceStatus = JSON_GetNumber(device, "status");
            if (deviceStatus == 0) {
                successCount++;
            } else if (deviceStatus > 0) {
                failedCount++;
            } else {
                notRespCount++;
            }
        }
        char reqId[20];
        strcpy(reqId, JSON_GetText(respItem, "reqType"));   // reqId = reqType.itemId
        int reqType  = atoi(strtok(reqId, "."));
        char* itemId = strtok(NULL, ".");
        // Calculate timeout based on number of successed and failed devices
        int addgroupTimeout = deviceCount * 1500;
        bool checkDone = false;

        if (currentTime - createdTime < addgroupTimeout) {
            if (successCount + failedCount == deviceCount) {
                checkDone = true;
            }
        } else {
            logError("Check Response Timeout. reqType: %d", reqType);
            checkDone = true;
        }

        if (checkDone) {
            list_t* successDevices = List_Create();
            list_t* failedDevices = List_Create();
            DeviceInfo deviceInfo;
            DpInfo     dpInfo;
            for (int d = 0; d < deviceCount; d++) {
                JSON* device = JArr_GetObject(devices, d);
                if (reqType == TYPE_ADD_GROUP_NORMAL || reqType == TYPE_UPDATE_GROUP_NORMAL) {
                    int foundDevices = Db_FindDeviceByAddr(&deviceInfo, JSON_GetText(device, "addr"));
                    if (foundDevices == 1) {
                        int status = JSON_GetNumber(device, "status");
                        if (status == 0) {
                            List_PushString(successDevices, deviceInfo.id);
                        } else {
                            char str[50];
                            sprintf(str, "%s(%d)", deviceInfo.id, status);
                            List_PushString(failedDevices, str);
                        }
                    }
                } else if (reqType == TYPE_ADD_GROUP_LINK || reqType == TYPE_UPDATE_GROUP_LINK) {
                    int foundDps = Db_FindDpByAddr(&dpInfo, JSON_GetText(device, "addr"));
                    if (foundDps == 1) {
                        int status = JSON_GetNumber(device, "status");
                        char str[60];
                        if (status == 0) {
                            sprintf(str, "%s|%d", dpInfo.deviceId, dpInfo.id);
                            List_PushString(successDevices, str);
                        } else {
                            sprintf(str, "%s|%d(%d)", dpInfo.deviceId, dpInfo.id, status);
                            List_PushString(failedDevices, str);
                        }
                    }
                } else if (reqType == TYPE_ADD_SCENE || reqType == TYPE_UPDATE_SCENE) {
                    int status = JSON_GetNumber(device, "status");
                    if (status != 0) {
                        Db_RemoveSceneAction(itemId, JSON_GetText(device, "addr"));
                    }
                }
            }
            if (reqType == TYPE_ADD_GROUP_NORMAL || reqType == TYPE_UPDATE_GROUP_NORMAL ||
                reqType == TYPE_ADD_GROUP_LINK || reqType == TYPE_UPDATE_GROUP_LINK) {
                // Save devices to AWS
                Aws_updateGroupDevices(itemId, successDevices, failedDevices);
                // Save devices to DB
                char* str = malloc((successDevices->count) * 50);
                List_ToString(successDevices, "|", str);
                Db_SaveGroupDevices(itemId, str);
                free(str);
            }
            // Remove this respItem from response list
            JArr_RemoveIndex(g_checkRespList, i);
            List_Delete(successDevices);
            List_Delete(failedDevices);
            // Send notification to user
            char noti[100];
            sprintf(noti, "Thành công: %d, Thất bại: %d", successCount, failedCount + notRespCount);
            sendNotiToUser(noti);
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
                    DpInfo dpInfo;
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
                    DpInfo dpInfo;
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
                JSON_SetText(packet, "entityId", runningAction->entityId);
                JSON_SetText(packet, "pid", runningAction->pid);
                JSON* dpArray = JSON_AddArray(packet, "dictDPs");
                JSON* dpItem = JArr_CreateObject(dpArray);
                JSON_SetNumber(dpItem, "id", runningAction->dpId);
                JSON_SetText(dpItem, "addr", runningAction->dpAddr);
                JSON_SetNumber(dpItem, "value", runningAction->dpValue);
                sendPacketTo(runningAction->serviceName, TYPE_CTR_DEVICE, packet);
                JSON_Delete(packet);
                // Move to next action
                scene->delayStart = timeInMilliseconds();
                scene->runningActionIndex++;
                if (scene->runningActionIndex >= scene->actionCount) {
                    scene->runningActionIndex = -1;     // No other actions need to execute, move scene to idle state
                }
            } else if (runningAction->actionType == EntityScene) {
                // Action type is "run or enable/disable another scene"
                if (StringCompare(runningAction->serviceName, SERVICE_BLE)) {
                    // HC or BLE scene, check if this scene is HC or BLE
                    for (int s = 0; s < g_sceneCount; s++) {
                        if (StringCompare(g_sceneList[s].id, runningAction->entityId)) {
                            // HC scene
                            if (!g_sceneList[s].isLocal) {
                                if (runningAction->dpValue == 0) {
                                    // Disable scene
                                    Db_EnableScene(runningAction->entityId, 0);
                                } else if (runningAction->dpValue == 1) {
                                    // Enable scene
                                    Db_EnableScene(runningAction->entityId, 1);
                                } else {
                                    markSceneToRun(&g_sceneList[s]);
                                }
                            } else {
                                // BLE scene: Send request to BLE service
                                JSON* packet = JSON_CreateObject();
                                JSON_SetText(packet, "entityId", runningAction->entityId);
                                JSON_SetNumber(packet, "state", runningAction->dpValue);
                                if (runningAction->dpValue == 2 && g_sceneList[s].conditionCount > 0) {
                                    // this action is run a BLE scene, we need to send the device of condition of this scene
                                    // to BLE service
                                    JSON_SetText(packet, "pid", g_sceneList[s].conditions[0].pid);
                                    JSON_SetText(packet, "dpAddr", g_sceneList[s].conditions[0].dpAddr);
                                    JSON_SetNumber(packet, "dpValue", g_sceneList[s].conditions[0].dpValue);
                                }
                                sendPacketTo(runningAction->serviceName, TYPE_CTR_SCENE, packet);
                                JSON_Delete(packet);
                            }
                            break;
                        }
                    }
                } else {
                    // Wifi scene: send request to WIFI service
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "entityId", runningAction->entityId);
                    JSON_SetNumber(packet, "state", runningAction->dpValue);
                    sendPacketTo(runningAction->serviceName, TYPE_CTR_SCENE, packet);
                    JSON_Delete(packet);
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

void getDeviceStatusLoop() {
    static long long int oldTick = 0;
    if (timeInMilliseconds() - oldTick > 60000) {
        oldTick = timeInMilliseconds();
        char sqlCmd[500];
        sprintf(sqlCmd, "SELECT address FROM devices \
                        JOIN devices_inf ON devices.deviceId = devices_inf.deviceId \
                        WHERE (instr('%s', pid) > 0 OR dpId='20') AND (state = 3) AND (%lld - updateTime > 60)", HG_BLE, oldTick);
        Sql_Query(sqlCmd, row) {
            JSON* packet = JSON_CreateObject();
            char* addr = sqlite3_column_text(row, 0);
            JSON_SetText(packet, "addr", addr);
            sendPacketTo(SERVICE_BLE, TYPE_GET_DEVICE_STATUS, packet);
            JSON_Delete(packet);
        }
    }
}

int main( int argc,char ** argv )
{
    g_checkRespList   = JSON_CreateArray();
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

    Mosq_Init();
    Db_LoadSceneToRam();
    sleep(1);
    while(xRun!=0) {
        Mosq_ProcessLoop();
        checkResponseLoop();
        executeScenes();
        // getDeviceStatusLoop();

        size_queue = get_sizeQueue(queue_received);
        if (size_queue > 0) {
            int reponse = 0;int leng =  0,number = 0, size_ = 0,i=0;
            char* recvMsg = (char *)dequeue(queue_received);

            JSON_Object *object_tmp = NULL;
            JSON_Array *actions_array_json = NULL;
            JSON_Array *condition_array_json = NULL;
            printf("\n\r");
            logInfo("Received message: %s", recvMsg);
            JSON* recvPacket = JSON_Parse(recvMsg);
            const char *object_string   = JSON_GetText(recvPacket, MOSQ_Payload);
            JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));
            char* NameService = JSON_GetText(recvPacket, "NameService");
            int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
            if (payload == NULL) {
                logError("Payload is NULL");
            }
            if (isMatchString(NameService, SERVICE_BLE) && payload) {
                switch (reqType) {
                    case GW_RESPONSE_DEVICE_CONTROL: {
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        double dpValue = JSON_GetNumber(payload, "dpValue");
                        DpInfo dpInfo;
                        int foundDps = Db_FindDpByAddr(&dpInfo, dpAddr);
                        if (foundDps == 1) {
                            JSON_SetText(payload, "deviceId", dpInfo.deviceId);
                            JSON_SetNumber(payload, "dpId", dpInfo.id);
                            JSON_SetNumber(payload, "eventType", EV_DEVICE_DP_CHANGED);
                            JSON_SetNumber(payload, "pageIndex", dpInfo.pageIndex);
                            Db_SaveDpValue(dpInfo.deviceId, dpInfo.id, dpValue);
                            Db_SaveDeviceState(dpInfo.deviceId, STATE_ONLINE);
                            Db_AddDeviceHistory(payload);
                            Aws_SaveDpValue(dpInfo.deviceId, dpInfo.id, dpValue, dpInfo.pageIndex);
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
                                Aws_SaveDeviceState(deviceInfo.id, deviceState, deviceInfo.pageIndex);
                                JSON_SetNumber(payload, "dpValue", deviceState == 2? 1 : 0);
                                JSON_SetNumber(payload, "eventType", EV_DEVICE_STATE_CHANGED);
                                Db_AddDeviceHistory(payload);
                            }
                        }
                        break;
                    }
                    case GW_RESPONSE_IR: {
                        uint16_t brandId = JSON_GetNumber(payload, "brandId");
                        uint8_t  remoteId = JSON_GetNumber(payload, "remoteId");
                        uint8_t  temp = JSON_GetNumber(payload, "temp");
                        uint8_t  mode = JSON_GetNumber(payload, "mode");
                        uint8_t  fan = JSON_GetNumber(payload, "fan");
                        uint8_t  swing = JSON_GetNumber(payload, "swing");
                        // Find device with brandId and remoteId
                        char sql[500];
                        sprintf(sql, "select * from devices where dpId=1 AND CAST(dpValue as INTEGER)=%d", brandId);
                        Sql_Query(sql, row) {
                            char* deviceId = sqlite3_column_text(row, 0);
                            sprintf(sql, "select * from devices where deviceId='%s' and dpId=2 and CAST(dpValue as INTEGER)=%d", deviceId, remoteId);
                            Sql_Query(sql, r) {
                                int pageIndex = sqlite3_column_int(r, 4);
                                Db_SaveDpValue(deviceId, 103, temp);
                                Db_SaveDpValue(deviceId, 102, mode);
                                Db_SaveDpValue(deviceId, 104, fan);
                                Db_SaveDpValue(deviceId, 105, swing);
                                Aws_SaveDpValue(deviceId, 103, temp, pageIndex);
                                Aws_SaveDpValue(deviceId, 102, mode, pageIndex);
                                Aws_SaveDpValue(deviceId, 104, fan, pageIndex);
                                Aws_SaveDpValue(deviceId, 105, swing, pageIndex);
                            }
                        }
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
                            Db_SaveDpValue(deviceInfo.id, dpId, dpValue);
                            JSON_SetNumber(payload, "causeType", 0);
                            JSON_SetText(payload, "causeId", "");
                            JSON_SetNumber(payload, "eventType", EV_DEVICE_DP_CHANGED);
                            Db_AddDeviceHistory(payload);
                            checkSceneForDevice(deviceInfo.id, dpId);     // Check and run scenes for this device if any
                        }

                        break;
                    }
                    case GW_RESPONSE_GROUP:
                    {
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        int status = JSON_GetNumber(payload, "status");
                        if (requestIsInRespList(TYPE_ADD_GROUP_NORMAL, groupAddr)) {
                            updateDeviceRespStatus(TYPE_ADD_GROUP_NORMAL, groupAddr, deviceAddr, status);
                        } else if (requestIsInRespList(TYPE_UPDATE_GROUP_NORMAL, groupAddr)) {
                            updateDeviceRespStatus(TYPE_UPDATE_GROUP_NORMAL, groupAddr, deviceAddr, status);
                        } else if (requestIsInRespList(TYPE_ADD_GROUP_LINK, groupAddr)) {
                            updateDeviceRespStatus(TYPE_ADD_GROUP_LINK, groupAddr, deviceAddr, status);
                        } else if (requestIsInRespList(TYPE_UPDATE_GROUP_LINK, groupAddr)) {
                            updateDeviceRespStatus(TYPE_UPDATE_GROUP_LINK, groupAddr, deviceAddr, status);
                        }
                        break;
                    }
                    case GW_RESPONSE_DIM_LED_SWITCH_HOMEGY: {
                        break;
                    }
                    case GW_RESPONSE_ADD_SCENE: {
                        char* sceneId = JSON_GetText(payload, "sceneId");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        int status = JSON_GetNumber(payload, "status");
                        updateDeviceRespStatus(TYPE_ADD_SCENE, sceneId, deviceAddr, status);
                        break;
                    }
                    case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE: {
                        break;
                    }
                    case GW_RESPONSE_DEVICE_KICKOUT: {
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDeviceByAddr(&deviceInfo, deviceAddr);
                        if (foundDevices == 1) {
                            Db_DeleteDevice(deviceInfo.id);
                            Aws_DeleteDevice(deviceInfo.id, deviceInfo.pageIndex);
                        }
                        break;
                    }
                    case GW_RESPONSE_SET_TTL: {
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        updateDeviceRespStatus(GW_RESPONSE_SET_TTL, "0", deviceAddr, 0);
                        break;
                    }
                    case TYPE_SYNC_DEVICE_STATE: {
                        SyncDevicesState();
                        break;
                    }
                }
            } else if (isMatchString(NameService, SERVICE_AWS) && payload) {
                switch (reqType) {
                    case TYPE_GET_DEVICE_HISTORY: {
                        long long currentTime = timeInMilliseconds();
                        long long startTime = JSON_GetNumber(payload, "start");
                        long long endTime = JSON_GetNumber(payload, "end");
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        char* dpIds = JSON_GetText(payload, "dpId");
                        int causeType = JSON_HasKey(payload, "causeType")? JSON_GetNumber(payload, "causeType") : -1;
                        int eventType = JSON_HasKey(payload, "eventType")? JSON_GetNumber(payload, "eventType") : -1;
                        int limit = JSON_GetNumber(payload, "limit");
                        // startTime = startTime == 0? currentTime - 86400000 : startTime;
                        JSON* histories = Db_FindDeviceHistories(startTime, endTime, deviceId, dpIds, causeType, eventType, limit);
                        char* msg = cJSON_PrintUnformatted(histories);
                        sendToService(SERVICE_AWS, TYPE_NOTIFI_REPONSE, msg);
                        JSON_Delete(histories);
                        free(msg);
                        break;
                    }
                    case TYPE_CTR_DEVICE:
                    case TYPE_CTR_GROUP_NORMAL: {
                        char* senderId = JSON_GetText(payload, "senderId");
                        JSON* originDPs = JSON_GetObject(payload, "dictDPs");
                        DeviceInfo deviceInfo;
                        char* deviceId;
                        int foundDevices = 0;
                        if (reqType == TYPE_CTR_DEVICE) {
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
                            JSON* dictDPs = JSON_AddArray(packet, "dictDPs");
                            JSON_ForEach(o, originDPs) {
                                int dpId = atoi(o->string);
                                DpInfo dpInfo;
                                int dpFound = Db_FindDp(&dpInfo, deviceId, dpId);
                                if (dpFound) {
                                    cJSON* dp = JSON_CreateObject();
                                    JSON_SetNumber(dp, "id", dpId);
                                    JSON_SetText(dp, "addr", dpInfo.addr);
                                    JSON_SetNumber(dp, "value", o->valueint);
                                    JSON_SetText(dp, "valueString", o->valuestring);
                                    cJSON_AddItemToArray(dictDPs, dp);
                                }
                            }
                            if (StringCompare(deviceInfo.pid, HG_BLE_IR_AC) ||
                                StringCompare(deviceInfo.pid, HG_BLE_IR_TV) ||
                                StringCompare(deviceInfo.pid, HG_BLE_IR_FAN)) {
                                for (int dpId = 1; dpId <= 3; dpId++) {
                                    DpInfo dpInfo;
                                    int dpFound = Db_FindDp(&dpInfo, deviceId, dpId);
                                    if (dpFound) {
                                        cJSON* dp = JSON_CreateObject();
                                        JSON_SetNumber(dp, "id", dpId);
                                        JSON_SetText(dp, "addr", dpInfo.addr);
                                        JSON_SetNumber(dp, "value", dpInfo.value);
                                        cJSON_AddItemToArray(dictDPs, dp);
                                    }
                                }
                            }
                            sendPacketTo(SERVICE_BLE, reqType, packet);
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
                            if (sceneType == SceneTypeManual) {
                                JSON_SetNumber(payload, "state", 2);
                            }
                            sendPacketTo(SERVICE_BLE, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_ADD_DEVICE: {
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        char* deviceId = JSON_GetText(localPacket, "deviceId");
                        int provider = JSON_GetNumber(localPacket, "provider");
                        // Delete device from database if exist
                        logInfo("Delete device %s from database if exist", deviceId);
                        Db_DeleteDevice(deviceId);
                        logInfo("Adding device: %s", deviceId);

                        if (provider == HOMEGY_BLE) {
                            JSON* protParam = JSON_GetObject(payload, "protocol_para");
                            char* gatewayAddr = JSON_GetText(protParam, "IDgateway");
                            int gatewayId = Db_FindGatewayId(gatewayAddr);
                            if (gatewayId >= 0) {
                                // Send packet to BLE to save device information in to gateway
                                JSON* protParam = JSON_GetObject(payload, "protocol_para");
                                JSON* dictDPs = JSON_GetObject(protParam, "dictDPs");
                                if (JSON_HasKey(dictDPs, "106")) {
                                    JSON_SetText(localPacket, "command", JSON_GetText(dictDPs, "106"));
                                }
                                JSON_SetNumber(localPacket, "gatewayId", gatewayId);
                                sendPacketTo(SERVICE_BLE, TYPE_ADD_DEVICE, localPacket);
                                JSON_Delete(localPacket);
                            } else {
                                logError("Gateway %s is not found", gatewayAddr);
                            }
                        }

                        // Insert device to database
                        addNewDevice(&db, payload);
                        JSON_SetNumber(payload, "eventType", EV_DEVICE_ADDED);
                        Db_AddDeviceHistory(payload);
                        break;
                    }
                    case TYPE_DEL_DEVICE: {
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        char* deviceId = JSON_GetText(localPacket, "deviceId");
                        if (JSON_HasKey(localPacket, "deviceAddr")) {
                            sendPacketTo(SERVICE_BLE, TYPE_DEL_DEVICE, localPacket);
                            Db_DeleteDevice(deviceId);
                            JSON_SetNumber(payload, "eventType", EV_DEVICE_DELETED);
                            Db_AddDeviceHistory(payload);
                            logInfo("Delete deviceId: %s", deviceId);
                        } else {
                            logError("device %s is not found", deviceId);
                        }
                        JSON_Delete(localPacket);
                        break;
                    }
                    case TYPE_SYNC_DB_DEVICES: {
                        logInfo("TYPE_SYNC_DB_DEVICES");
                        JSON* devicesNeedRemove = JSON_CreateArray();
                        JSON* devicesNeedAdd = JSON_CreateArray();
                        JSON* localDevices = Db_GetAllDevices();
                        JSON* cloudDevices = payload;
                        // Find all devices that have in the local database but not have in the cloud database
                        JSON_ForEach(localDevice, localDevices) {
                            bool found = false;
                            JSON_ForEach(cloudDevice, cloudDevices) {
                                if (compareDeviceById(localDevice, cloudDevice)) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                cJSON_AddItemReferenceToArray(devicesNeedRemove, localDevice);
                            }
                        }

                        // Find all devices that have in the cloud database but not have in the local database
                        JSON_ForEach(cloudDevice, cloudDevices) {
                            bool found = false;
                            JSON_ForEach(localDevice, localDevices) {
                                if (compareDeviceById(localDevice, cloudDevice)) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                cJSON_AddItemReferenceToArray(devicesNeedAdd, cloudDevice);
                            }
                        }

                        // Delete devices if any
                        JSON_ForEach(d, devicesNeedRemove) {
                            char* deviceId = JSON_GetText(d, "deviceId");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                            if (foundDevices == 1) {
                                JSON* p = JSON_CreateObject();
                                JSON_SetText(p, "deviceAddr", deviceInfo.addr);
                                sendPacketTo(SERVICE_BLE, TYPE_DEL_DEVICE, p);
                                JSON_Delete(p);
                            }
                            Db_DeleteDevice(deviceId);
                            logInfo("Removed device %s", deviceId);
                        }

                        // Add new devices if any
                        JSON_ForEach(d, devicesNeedAdd) {
                            // logInfo(cJSON_PrintUnformatted(d));
                            int provider = JSON_GetNumber(d, KEY_PROVIDER);
                            char* deviceId = JSON_GetText(d, "deviceId");
                            if (deviceId) {
                                if (provider == HOMEGY_BLE) {
                                    char* gatewayAddr = JSON_GetText(d, KEY_ID_GATEWAY);
                                    int gatewayId = Db_FindGatewayId(gatewayAddr);
                                    if (gatewayId >= 0) {
                                        int pageIndex = JSON_GetNumber(d, "pageIndex");
                                        JSON_SetNumber(d, "gatewayId", gatewayId);
                                        sendPacketTo(SERVICE_BLE, TYPE_ADD_DEVICE, d);
                                        JSON* dictMeta = JSON_GetObject(d, "dictMeta");
                                        JSON_ForEach(dp, dictMeta) {
                                            int dpId = atoi(dp->string);
                                            Db_AddDp(deviceId, dpId, dp->valuestring, pageIndex);
                                        }
                                    }
                                }
                                Db_AddDevice(d);
                                logInfo("Added device %s to database", deviceId);
                            }
                        }
                        break;
                    }
                    case TYPE_SYNC_DB_GROUPS: {
                        logInfo("TYPE_SYNC_DB_GROUPS");
                        Db_DeleteAllGroup();
                        JSON_ForEach(group, payload) {
                            char* groupAddr = JSON_GetText(group, "groupAddr");
                            char* groupName = JSON_GetText(group, "groupName");
                            char* devices = JSON_GetObject(group, "devices");
                            int isLight = JSON_GetNumber(group, "isLight");
                            int pageIndex = JSON_GetNumber(group, "pageIndex");
                            Db_AddGroup(groupAddr, groupName, devices, isLight, pageIndex);
                        }
                        break;
                    }
                    case TYPE_SYNC_DB_SCENES: {
                        logInfo("TYPE_SYNC_DB_SCENES");
                        Db_DeleteAllScene();
                        JSON_ForEach(scene, payload) {
                            Db_AddScene(scene);
                        }
                        break;
                    }
                    case TYPE_ADD_GW: {
                        Db_AddGateway(payload);
                        sendPacketTo(SERVICE_BLE, reqType, payload);
                        break;
                    }
                    case TYPE_ADD_SCENE: {
                        int isLocal = JSON_GetNumber(payload, "isLocal");
                        Scene_GetFullInfo(payload);
                        Db_AddScene(payload);  // Insert scene into database
                        if (isLocal) {
                            sendPacketTo(SERVICE_BLE, TYPE_ADD_SCENE, payload);
                            // Add this request to response list for checking response
                            JSON* actions = JSON_GetObject(payload, "actions");
                            JSON_ForEach(action, actions) {
                                if (JSON_HasKey(action, "pid")) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* deviceAddr = JSON_GetText(action, "entityAddr");
                                    addDeviceToRespList(reqType, sceneId, deviceAddr);
                                }
                            }
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
                            } else {
                                logInfo("Deleted HC scene %s", sceneId);
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
                                    if (JSON_HasKey(oldAction, "pid")) {
                                        bool found = false;
                                        JSON_ForEach(newAction, newActionsArray) {
                                            if (JSON_HasKey(newAction, "pid")) {
                                                if (compareSceneEntity(oldAction, newAction)) {
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        if (!found) {
                                            cJSON_AddItemReferenceToArray(actionsNeedRemove, oldAction);
                                        }
                                    }
                                }

                                // Find all actions that are need to be added
                                JSON* actionsNeedAdd = JSON_CreateArray();
                                JSON_ForEach(newAction, newActionsArray) {
                                    if (JSON_HasKey(newAction, "pid")) {
                                        bool found = false;
                                        JSON_ForEach(oldAction, oldActionsArray) {
                                            if (JSON_HasKey(oldAction, "pid")) {
                                                if (compareSceneEntity(oldAction, newAction)) {
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        if (!found) {
                                            cJSON_AddItemReferenceToArray(actionsNeedAdd, newAction);
                                        }
                                    }
                                }

                                // Check if condition is need to change
                                bool conditionNeedChange = true;
                                JSON* oldConditionsArray = JSON_GetObject(oldScene, "conditions");
                                JSON* newConditionsArray = JSON_GetObject(newScene, "conditions");
                                JSON* oldCondition = JArr_GetObject(oldConditionsArray, 0);
                                JSON* newCondition = JArr_GetObject(newConditionsArray, 0);
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
                                sendPacketTo(SERVICE_BLE, reqType, packet);

                                // Add this request to response list for checking response
                                JSON_ForEach(action, newActionsArray) {
                                    if (JSON_HasKey(action, "pid")) {
                                        char* sceneId = JSON_GetText(payload, "id");
                                        char* deviceAddr = JSON_GetText(action, "entityAddr");
                                        addDeviceToRespList(reqType, sceneId, deviceAddr);
                                        bool found = false;
                                        JSON_ForEach(newAction, actionsNeedAdd) {
                                            if (JSON_HasKey(newAction, "pid")) {
                                                if (compareSceneEntity(action, newAction)) {
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        if (!found) {
                                            // Set SUCCESS status of devices without any update to skip checking response for them
                                            updateDeviceRespStatus(reqType, sceneId, deviceAddr, 0);
                                        }
                                    }
                                }

                                JSON_Delete(packet);
                            }
                            // Save new scene to database
                            Db_DeleteScene(sceneId);
                            Db_AddScene(newScene);
                            if (!isLocal) {
                                sendNotiToUser("Cập nhật kịch bản HC thành công");
                            }
                        } else {
                            logInfo("Scene %s is not found", sceneId);
                        }

                        break;
                    }
                    case TYPE_ADD_GROUP_NORMAL: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        sendPacketTo(SERVICE_BLE, reqType, localPacket);
                        JSON* devicesArray = JSON_GetObject(localPacket, "devices");
                        JSON* srcObj = JSON_Parse(object_string);
                        // Insert group information to database
                        Db_AddGroup(groupAddr, JSON_GetText(srcObj, "name"), JSON_GetText(srcObj, "devices"), true, 1);
                        // Add this request to response list for checking response
                        JSON_ForEach(device, devicesArray) {
                            addDeviceToRespList(reqType, groupAddr, JSON_GetText(device, "deviceAddr"));
                        }
                        JSON_Delete(localPacket);
                        JSON_Delete(srcObj);
                        break;
                    }
                    case TYPE_DEL_GROUP_NORMAL: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        sendPacketTo(SERVICE_BLE, reqType, localPacket);
                        // Delete group information from database
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        Db_DeleteGroup(groupAddr);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_NORMAL: {
                        logInfo("TYPE_UPDATE_GROUP_NORMAL");
                        JSON* srcObj = payload;
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        char* devicesStr = Db_FindDevicesInGroup(groupAddr);
                        if (devicesStr) {
                            JSON* oldDevices = parseGroupNormalDevices(devicesStr);
                            JSON* newDevices = JSON_GetObject(localPacket, "devices");
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
                            sendPacketTo(SERVICE_BLE, reqType, packet);
                            // Add this request to response list for checking response
                            JSON_ForEach(device, newDevices) {
                                char* deviceAddr = JSON_GetText(device, "deviceAddr");
                                addDeviceToRespList(reqType, groupAddr, deviceAddr);
                                bool found = false;
                                JSON_ForEach(newDevice, dpsNeedAdd) {
                                    if (compareDevice(device, newDevice)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    // Set SUCCESS status of devices without any update to skip checking response for them
                                    updateDeviceRespStatus(reqType, groupAddr, deviceAddr, 0);
                                }
                            }
                            JSON_Delete(packet);
                            Db_SaveGroupDevices(groupAddr, JSON_GetText(srcObj, "devices"));
                        }
                        JSON_Delete(localPacket);
                        free(devicesStr);
                        break;
                    }
                    case TYPE_ADD_GROUP_LINK: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        sendPacketTo(SERVICE_BLE, reqType, localPacket);
                        JSON* devicesArray = JSON_GetObject(localPacket, "devices");
                        JSON* srcObj = payload;
                        // Insert group information to database
                        Db_AddGroup(groupAddr, JSON_GetText(srcObj, "name"), JSON_GetText(srcObj, "devices"), false, 1);
                        // Add this request to response list for checking response
                        JSON_ForEach(device, devicesArray) {
                            addDeviceToRespList(reqType, groupAddr, JSON_GetText(device, "dpAddr"));
                        }
                        JSON_Delete(localPacket);
                        break;
                    }
                    case TYPE_DEL_GROUP_LINK: {
                        // Send request to BLE
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
                        sendPacketTo(SERVICE_BLE, TYPE_DEL_GROUP_LINK, localPacket);
                        // Delete group information from database
                        char* groupAddr = JSON_GetText(localPacket, "groupAddr");
                        Db_DeleteGroup(groupAddr);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_LINK: {
                        logInfo("TYPE_UPDATE_GROUP_LINK");
                        JSON* srcObj = payload;
                        JSON* localPacket = ConvertToLocalPacket(reqType, object_string);
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
                            sendPacketTo(SERVICE_BLE, reqType, packet);
                            Db_SaveGroupDevices(groupAddr, JSON_GetText(srcObj, "devices"));

                            // Add this request to response list for checking response
                            JSON_ForEach(dp, newDps) {
                                char* dpAddr = JSON_GetText(dp, "dpAddr");
                                addDeviceToRespList(reqType, groupAddr, dpAddr);
                                bool found = false;
                                JSON_ForEach(newDp, dpsNeedAdd) {
                                    if (compareDp(dp, newDp)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    // Set SUCCESS status of devices without any update to skip checking response for them
                                    updateDeviceRespStatus(reqType, groupAddr, dpAddr, 0);
                                }
                            }
                            JSON_Delete(packet);
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
                            sendPacketTo(SERVICE_BLE, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_LOCK_KIDS: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            sendPacketTo(SERVICE_BLE, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_SET_GROUP_TTL: {
                        JSON_ForEach(item, payload) {
                            if (item->string && cJSON_IsObject(item)) {
                                int ttl = JSON_GetNumber(item, "ttl");
                                char* deviceStr = Db_FindDevicesInGroup(item->string);
                                JSON* devices = parseGroupNormalDevices(deviceStr);
                                JSON_ForEach(d, devices) {
                                    char* deviceAddr = JSON_GetText(d, "deviceAddr");
                                    Ble_SetTTL(deviceAddr, ttl);
                                    addDeviceToRespList(GW_RESPONSE_SET_TTL, "0", deviceAddr);
                                }
                            }
                        }
                        break;
                    }
                }
            }

            JSON_Delete(recvPacket);
            JSON_Delete(payload);
            free(recvMsg);
        }
        usleep(100);
    }
    return 0;
}

// Add device that need to check response to response list
void addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr) {
    ASSERT(itemId); ASSERT(deviceAddr);
    JSON* item = requestIsInRespList(reqType, itemId);
    if (item == NULL) {
        char reqTypeStr[10];
        sprintf(reqTypeStr, "%d.%s", reqType, itemId);
        item = JArr_CreateObject(g_checkRespList);
        JSON_SetText(item, "reqType", reqTypeStr);
        JSON_SetNumber(item, "createdTime", timeInMilliseconds());
        JSON* devices = JSON_AddArray(item, "devices");
    }
    JSON* devices = JSON_GetObject(item, "devices");
    if (JArr_FindByText(devices, "addr", deviceAddr) == NULL) {
        JSON *device = JArr_CreateObject(devices);
        JSON_SetText(device, "addr", deviceAddr);
        JSON_SetNumber(device, "status", -1);
    }
}

JSON* requestIsInRespList(int reqType, const char* itemId) {
    ASSERT(itemId);
    char reqTypeStr[10];
    sprintf(reqTypeStr, "%d.%s", reqType, itemId);
    JSON* item = JArr_FindByText(g_checkRespList, "reqType", reqTypeStr);
    return item;
}

void updateDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr, int status) {
    ASSERT(itemId); ASSERT(deviceAddr);
    JSON* item = requestIsInRespList(reqType, itemId);
    if (item) {
        JSON* devices = JSON_GetObject(item, "devices");
        JSON* device = JArr_FindByText(devices, "addr", deviceAddr);
        if (device) {
            JSON_SetNumber(device, "status", status);
        }
    }
}

int getRespNumber() {
    return json_array_get_count(json_value_get_array(g_checkRespList));
}

int getDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr) {
    JSON* item = requestIsInRespList(reqType, itemId);
    if (item) {
        JSON* devices = JSON_GetObject(item, "devices");
        JSON* device = JArr_FindByText(devices, "addr", deviceAddr);
        if (device) {
            return (int)JSON_GetNumber(device, "status");
        }
    }
    return 0;
}


bool Scene_GetFullInfo(JSON* packet) {
    JSON* actionsArray = JSON_GetObject(packet, "actions");
    int isLocal = JSON_GetNumber(packet, "isLocal");
    JSON* newActionsArray = JSON_CreateArray();
    JSON_ForEach(action, actionsArray) {
        JSON* executorProperty = JSON_GetObject(action, "executorProperty");
        char* actionExecutor = JSON_GetText(action, "actionExecutor");
        char* entityId = JSON_GetText(action, "entityId");
        int delaySeconds = 0;
        if (StringCompare(entityId, "delay")) {
            int minutes = atoi(JSON_GetText(executorProperty, "minutes"));
            delaySeconds = atoi(JSON_GetText(executorProperty, "seconds"));
            delaySeconds = minutes * 60 + delaySeconds;
            JSON_SetNumber(action, "actionType", EntityDelay);
        } else if (StringCompare(actionExecutor, "ruleTrigger")) {
            JSON_SetNumber(action, "actionType", EntityScene);
            JSON_SetNumber(action, "dpValue", 2);
        } else if (StringCompare(actionExecutor, "ruleEnable")) {
            JSON_SetNumber(action, "actionType", EntityScene);
            JSON_SetNumber(action, "dpValue", 1);
        } else if (StringCompare(actionExecutor, "ruleDisable")) {
            JSON_SetNumber(action, "actionType", EntityScene);
            JSON_SetNumber(action, "dpValue", 0);
        } else if (StringCompare(actionExecutor, "deviceGroupDpIssue")) {
            int dpId = 0, dpValue = 0;
            JSON_ForEach(o, executorProperty) {
                dpId = atoi(o->string);
                dpValue = o->valueint;
            }
            if (dpId == 20) {
                JSON_SetText(action, "entityAddr", JSON_GetText(action, "entityID"));
                JSON_SetText(action, "dpAddr", JSON_GetText(action, "entityID"));
                JSON_SetNumber(action, "actionType", EntityDevice);
                JSON_SetNumber(action, "dpId", 20);
                JSON_SetNumber(action, "dpValue", dpValue);
                // Get all devices in this group
                char* devicesStr = Db_FindDevicesInGroup(entityId);
                JSON* devices = parseGroupNormalDevices(devicesStr);
                JSON_ForEach(d, devices) {
                    JSON* newAction = JArr_CreateObject(newActionsArray);
                    JSON_SetText(newAction, "entityId", JSON_GetText(d, "deviceId"));
                    JSON_SetText(newAction, "entityAddr", JSON_GetText(d, "deviceAddr"));
                    JSON_SetText(newAction, "pid", JSON_GetText(d, "pid"));
                    JSON_SetNumber(newAction, "dpId", dpId);
                    JSON_SetNumber(newAction, "dpValue", dpValue);
                }
                JSON_Delete(devices);
            }
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
                DpInfo dpInfo;
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
                int dpId = atoi(JArr_GetText(exprArray, 0) + 3);   // Template is "$dp1"
                int dpValue = JArr_GetNumber(exprArray, 2);
                DpInfo dpInfo;
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

    // For deviceGroupDpIssue: Copy new action of devices to current action array
    if (isLocal) {
        JSON_ForEach(o, newActionsArray) {
            JSON* newAction = JArr_CreateObject(actionsArray);
            JSON_SetText(newAction, "pid", JSON_GetText(o, "pid"));
            JSON_SetText(newAction, "actionType", EntityDevice);
            JSON_SetText(newAction, "entityID", JSON_GetText(o, "entityId"));
            JSON_SetText(newAction, "entityAddr", JSON_GetText(o, "entityAddr"));
            JSON_SetText(newAction, "dpAddr", JSON_GetText(o, "entityAddr"));
            JSON_SetNumber(newAction, "dpId", JSON_GetNumber(o, "dpId"));
            JSON_SetNumber(newAction, "dpValue", JSON_GetNumber(o, "dpValue"));
        }
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
                DpInfo dpInfo;
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
                int dpId = atoi(JArr_GetText(exprArray, 0) + 3);   // Template is "$dp1"
                int dpValue = JArr_GetNumber(exprArray, 2);
                DpInfo dpInfo;
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
    JSON* root = JSON_CreateObject();
    JSON_SetText(root, "name", scene->id);
    JSON_SetNumber(root, "isLocal", scene->isLocal);
    JSON_SetNumber(root, "state", scene->isEnable);
    JSON_SetNumber(root, "sceneType", scene->type);

    // Add actions
    JSON* actionsArray = JSON_AddArray(root, "actions");
    for (int i = 0; i < scene->actionCount; i++) {
        SceneAction* sceneAction = &scene->actions[i];
        JSON* action = JArr_CreateObject(actionsArray);
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
    JSON* conditionsArray = JSON_AddArray(root, "conditions");
    for (int i = 0; i < scene->conditionCount; i++) {
        SceneCondition* sceneCondition = &scene->conditions[i];
        JSON* condition = JArr_CreateObject(conditionsArray);
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
    if (JSON_HasKey(srcObj, "provider")) {
        JSON_SetNumber(destObj, "provider", JSON_GetNumber(srcObj, "provider"));
    }
    if (JSON_HasKey(srcObj, "deviceId")) {
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
    if (JSON_HasKey(srcObj, "sceneId")) {
        char* sceneId = JSON_GetText(srcObj, "sceneId");
        JSON_SetText(destObj, "sceneId", sceneId);
    }
    if (JSON_HasKey(srcObj, "groupAdress")) {
        char* groupAddr = JSON_GetText(srcObj, "groupAdress");
        JSON_SetText(destObj, "groupAddr", groupAddr);
        // Find devices in this grou from database or from source object if has
        char* deviceIds = NULL;
        bool hasDevicesInSrc = false;   // "devices" key is exist in the root level srcObj or not
        if (!JSON_HasKey(srcObj, "devices")) {
            deviceIds = Db_FindDevicesInGroup(groupAddr);
        } else {
            hasDevicesInSrc = true;
            deviceIds = JSON_GetText(srcObj, "devices");
        }
        if (deviceIds) {
            // Split deviceIds and make devices array in dest object
            list_t* splitList = String_Split(deviceIds, "|");
            JSON* devicesArray = JSON_AddArray(destObj, "devices");
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
                            JSON_SetNumber(arrayItem, "gwIndex", deviceInfo.gwIndex);
                        }
                    } else if (arrayItem != NULL) {
                        int dpId = atoi(splitList->items[i]);
                        DpInfo dpInfo;
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
                        JSON_SetNumber(arrayItem, "gwIndex", deviceInfo.gwIndex);
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
    if (JSON_HasKey(srcObj, "protocol_para")) {
        JSON* protParam = cJSON_GetObjectItem(srcObj, "protocol_para");
        if (JSON_HasKey(protParam, "pid")) {
            char* devicePid = JSON_GetText(protParam, "pid");
            JSON_SetText(destObj, "devicePid", devicePid);
        }

        if (JSON_HasKey(protParam, "deviceKey")) {
            char* deviceKey = JSON_GetText(protParam, "deviceKey");
            JSON_SetText(destObj, "deviceKey", deviceKey);
        }

        if (JSON_HasKey(protParam, "Unicast")) {
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
                DpInfo dpInfo;
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
    ASSERT(devices);
    JSON* devicesArray = cJSON_CreateArray();
    list_t* splitList = String_Split(devices, "|");
    for (int i = 0; i < splitList->count; i++) {
        JSON* arrayItem = JSON_CreateObject();
        DeviceInfo deviceInfo;
        int foundDevices = Db_FindDevice(&deviceInfo, splitList->items[i]);
        if (foundDevices) {
            JSON_SetText(arrayItem, "deviceId", deviceInfo.id);
            JSON_SetText(arrayItem, "deviceAddr", deviceInfo.addr);
            JSON_SetText(arrayItem, "pid", deviceInfo.pid);
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
        JArr_AddText(packet, dpAddr);
    }
    sendPacketTo(SERVICE_BLE, TYPE_SYNC_DEVICE_STATE, packet);
    JSON_Delete(packet);
}
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
#include "core_api.h"
#include "core_data_process.h"
#include "helper.h"
#include "messages.h"
#include "cJSON.h"

const char* SERVICE_NAME = SERVICE_CORE;
FILE *fptr;
struct mosquitto * mosq;
sqlite3 *db;

struct Queue *queue_received;
Scene* g_sceneList;
int g_sceneCount = 0;
static bool g_mosqIsConnected = false;

JSON* ConvertToLocalPacket(int reqType, const char* cloudPacket);
bool sendInfoDeviceFromDatabase();
bool sendInfoSceneFromDatabase();
bool Scene_GetFullInfo(JSON* packet);
void SyncDevicesState();
void GetDeviceStatusForScene(Scene* scene);
void GetDeviceStatusForGroup(const char* deviceId, int dpId);

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
        } else {
            if (deviceId1 == NULL || deviceId2 == NULL) {
                char* t = cJSON_PrintUnformatted(device2);
                int a = 0;
            }
            return false;
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
    mosquitto_subscribe(mosq, NULL, "/topic/detected/#", 0); //subcribe MQTT for camera HANET
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    int reponse = 0;
    bool check_flag =false;

    int size_queue = get_sizeQueue(queue_received);
    if (size_queue < QUEUE_SIZE) {
        if(isContainString( msg->topic,MOSQ_LayerService_Core)){
            enqueue(queue_received,(char *) msg->payload);
        } else if (isContainString( msg->topic,"/topic/detected/")){
            JSON_Value *root_value = json_value_init_object();
            JSON_Object *root_object = json_value_get_object(root_value);
            json_object_set_string(root_object, MOSQ_NameService, SERVICE_HANET);
            json_object_set_number(root_object, MOSQ_ActionType, CAM_HANET_RESPONSE);
            json_object_set_number(root_object, MOSQ_TimeCreat, timeInMilliseconds());
            json_object_set_string(root_object, MOSQ_Payload, (char *) msg->payload);
            char *message = json_serialize_to_string_pretty(root_value);
            enqueue(queue_received,message);
            json_free_serialized_string(message);
            json_value_free(root_value);
        }
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

void ResponseWaiting() {
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
        StringCopy(reqId, JSON_GetText(respItem, "reqType"));   // reqId = reqType.itemId
        int reqType  = atoi(strtok(reqId, "."));
        char* itemId = strtok(NULL, ".");
        // Calculate timeout based on number of successed and failed devices
        int timeout = deviceCount * 1500;
        bool checkDone = false;

        if (currentTime - createdTime < timeout) {
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
            if (reqType != TYPE_CTR_DEVICE) {
                char noti[100];
                sprintf(noti, "Thành công: %d, Thất bại: %d", successCount, failedCount + notRespCount);
                sendNotiToUser(noti);
            }
        }
    }
}

void markSceneToRun(Scene* scene, const char* causeId) {
    // Just setting the runningActionIndex vatiable to 0 and the actions of scene will be executed
    // in ExecuteScene() function
    scene->delayStart = timeInMilliseconds();
    scene->runningActionIndex = 0;
    // Save event to history
    JSON* history = JSON_CreateObject();
    JSON_SetNumber(history, "eventType", EV_RUN_SCENE);
    if (causeId) {
        JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_APP);
        JSON_SetText(history, "causeId", causeId);
    } else {
        JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_DEVICE);
    }
    JSON_SetText(history, "deviceId", scene->id);
    Db_AddDeviceHistory(history);
    JSON_Delete(history);
}

// Check if a scene is need to be executed or not
void checkSceneCondition(Scene* scene) {
    // Don't check condition for manual scene
    if (scene->type == SceneTypeManual) {
        markSceneToRun(scene, NULL);
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
            printf("check conditions of SceneTypeOneOfConds\n");
            for (int i = 0; i < scene->conditionCount; i++) {
                if (scene->conditions[i].conditionType == EntitySchedule) {
                    if (scene->conditions[i].timeReached) {
                        scene->conditions[i].timeReached = -1;
                        markSceneToRun(scene, NULL);
                    }
                } else {
                    DpInfo dpInfo;
                    int foundDps = Db_FindDp(&dpInfo, scene->conditions[i].entityId, scene->conditions[i].dpId);
                    printf("foundDps = %d\n",foundDps);

                    //Check type of value in DB is Double or String ( with many Device is double type, with Camera Hanet is string type(personID))
                    if (foundDps == 1 && scene->conditions[i].valueType == ValueTypeDouble) {
                        printf("check conditions of ValueTypeDouble\n");
                        printf("dpInfo.value = %f\n",dpInfo.value);
                        printf("scene->conditions[i].dpValue = %f\n",scene->conditions[i].dpValue);
                        if(dpInfo.value == scene->conditions[i].dpValue){
                            printf("check conditions of scene is ok\n");
                            markSceneToRun(scene, NULL);
                            break;
                        }
                    } else if(foundDps == 1 && scene->conditions[i].valueType == ValueTypeString) {
                        printf("check conditions of ValueTypeString\n");
                        if(StringCompare(dpInfo.valueStr, scene->conditions[i].dpValueStr)){
                            printf("check conditions of scene is ok\n");
                            markSceneToRun(scene, NULL);
                            break;
                        }
                    }
                }
            }
        } else {
            int i = 0;
            printf("check conditions of SceneTypeAllConds\n");
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
                markSceneToRun(scene, NULL);
            }
        }
    }
}


// Check if there are scenes need to be executed if status of a device is changed
void checkSceneForDevice(const char* deviceId, int dpId, bool syncRelatedDevices) {
    printf("checkSceneForDevice\n");
    // Find all scenes that this device is in conditions
    for (int i = 0; i < g_sceneCount; i++) {
        Scene* scene = &g_sceneList[i];
        for (int c = 0; c < scene->conditionCount; c++) {
            printf("scene->conditions[%d].entityId = %s\n",c,scene->conditions[c].entityId);
            printf("scene->conditions[%d].dpId = %d\n",c,scene->conditions[c].dpId);
            printf("deviceId = %s\n",deviceId);
            printf("dpId = %d\n",dpId);
            if (scene->conditions[c].conditionType == EntityDevice &&
                (StringCompare(scene->conditions[c].entityId, deviceId)) &&
                (scene->conditions[c].dpId == dpId)) {
                printf("checkSceneForDevice with scene->conditions[c].dpId == dpId\n");
                if (scene->isLocal == false) {
                    checkSceneCondition(scene);
                } else if (syncRelatedDevices && scene->isEnable) {
                    DpInfo dpInfo;
                    int foundDps = Db_FindDp(&dpInfo, scene->conditions[c].entityId, scene->conditions[c].dpId);
                    if (foundDps == 1 && scene->conditions[c].valueType == ValueTypeDouble) {
                        if(dpInfo.value == scene->conditions[c].dpValue){
                            // Save history for this local scene
                            JSON* history = JSON_CreateObject();
                            JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_DEVICE);
                            JSON_SetNumber(history, "eventType", EV_RUN_SCENE);
                            JSON_SetText(history, "deviceId", scene->id);
                            Db_AddDeviceHistory(history);
                            JSON_Delete(history);
                            GetDeviceStatusForScene(scene);
                            break;
                        }
                    }
                }
            }
        }
    }
}

/*
 * Loop through all HC scene and execute them is they need to execute
 * This function has to be called constantly in main loop
 */
void ExecuteScene() {
    for (int i = 0; i < g_sceneCount; i++) {
        Scene* scene = &g_sceneList[i];
        if (!scene->isLocal && scene->runningActionIndex >= 0) {
            SceneAction* runningAction = &scene->actions[scene->runningActionIndex];
            if (runningAction->actionType == EntityDevice) {
                // Action type is "control a device"
                if (StringCompare(runningAction->serviceName, SERVICE_BLE)) {
                    Ble_ControlDeviceArray(runningAction->entityId, runningAction->dpIds, runningAction->dpValues, runningAction->dpCount, scene->id);
                } else {
                    Wifi_ControlDevice(runningAction->entityId, runningAction->wifiCode);
                }
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
                            if (!g_sceneList[s].isLocal) {
                                // HC scene
                                if (runningAction->dpValues[0] == 0) {
                                    // Disable scene
                                    Db_EnableScene(runningAction->entityId, 0);
                                } else if (runningAction->dpValues[0] == 1) {
                                    // Enable scene
                                    Db_EnableScene(runningAction->entityId, 1);
                                } else {
                                    markSceneToRun(&g_sceneList[s], g_sceneList[s].id);
                                }
                            } else {
                                // BLE scene: Send request to BLE service
                                JSON* packet = JSON_CreateObject();
                                JSON_SetText(packet, "entityId", runningAction->entityId);
                                JSON_SetNumber(packet, "state", runningAction->dpValues[0]);
                                if (runningAction->dpValues[0] == 2 && g_sceneList[s].conditionCount > 0) {
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
                    JSON_SetNumber(packet, "state", runningAction->dpValues[0]);
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


void GetDeviceStatusForScene(Scene* scene) {
    ASSERT(scene);
    JSON* devicesArray = cJSON_CreateArray();
    for (int act = 0; act < scene->actionCount; act++) {
        if (scene->actions[act].actionType == EntityDevice) {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, scene->actions[act].entityId);
            if (foundDevices == 1 && !JArr_FindByText(devicesArray, NULL, deviceInfo.addr)) {
                JArr_AddText(devicesArray, deviceInfo.addr);
            }
        }
    }
    if (JArr_Count(devicesArray) > 0) {
        sendPacketTo(SERVICE_BLE, TYPE_GET_DEVICE_STATUS, devicesArray);
    }
    JSON_Delete(devicesArray);
}

/*
 * Tìm tất cả các nhóm mà 1 hạt công tắc nằm trong đó, sau đó gửi lệnh để get trạng thái mới nhất của tất
 * cả thiết bị trong nhóm đó. Hàm này được gọi khi có 1 hạt công tắc thay đổi trạng thái được gửi về từ thiết bị
 */
void GetDeviceStatusForGroup(const char* deviceId, int dpId) {
    ASSERT(deviceId);
    JSON* devicesArray = cJSON_CreateArray();
    char sqlCommand[200];
    sprintf(sqlCommand, "SELECT * FROM group_inf WHERE isLight = 0 AND devices LIKE '%%%s|%d%%'", deviceId, dpId);
    Sql_Query(sqlCommand, row) {
        char* devices = sqlite3_column_text(row, 5);
        JSON* groupDevices = parseGroupLinkDevices(devices);
        JSON_ForEach(d, groupDevices) {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, JSON_GetText(d, "deviceId"));
            if (foundDevices == 1 && !JArr_FindByText(devicesArray, NULL, deviceInfo.addr)) {
                JArr_AddText(devicesArray, deviceInfo.addr);
            }
        }
        JSON_Delete(groupDevices);
    }
    if (JArr_Count(devicesArray) > 0) {
        sendPacketTo(SERVICE_BLE, TYPE_GET_DEVICE_STATUS, devicesArray);
    }
    JSON_Delete(devicesArray);
}

void GetDeviceStatusInterval() {
    static long long int oldTick = 0;

    if (timeInMilliseconds() - oldTick > 60000) {
        oldTick = timeInMilliseconds();
        char sqlCmd[500];
        char pid[1000];
        sprintf(pid, "%s,%s,%s", HG_BLE_SWITCH, HG_BLE_IR, HG_BLE_CURTAIN);
        sprintf(sqlCmd, "SELECT address FROM devices \
                        JOIN devices_inf ON devices.deviceId = devices_inf.deviceId \
                        WHERE (instr('%s', pid) > 0 OR dpId='20') AND (%lld - updateTime > 60)", pid, oldTick);
        JSON* devicesArray = cJSON_CreateArray();
        Sql_Query(sqlCmd, row) {
            JSON* item = JArr_CreateObject(devicesArray);
            char* addr = sqlite3_column_text(row, 0);
            JSON_SetText(item, "addr", addr);
        }
        sendPacketTo(SERVICE_BLE, TYPE_GET_DEVICE_STATUS, devicesArray);
        JSON_Delete(devicesArray);
    }
}

int main(int argc, char ** argv)
{
    CoreInit();
    int size_queue = 0;
    bool check_flag = false;
    int xRun = 1;
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
        ResponseWaiting();
        ExecuteScene();
        // GetDeviceStatusInterval();

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
            if (StringCompare(NameService, SERVICE_BLE) && payload) {
                switch (reqType) {
                    case GW_RESP_DEVICE_STATUS: {
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        double dpValue = JSON_GetNumber(payload, "dpValue");
                        uint16_t opcode = JSON_HasKey(payload, "opcode")? JSON_GetNumber(payload, "opcode") : 0;
                        DpInfo dpInfo;
                        DeviceInfo deviceInfo;
                        int foundDps = Db_FindDpByAddr(&dpInfo, dpAddr);
                        if (foundDps == 1) {
                            double oldDpValue = dpInfo.value;
                            int foundDevices = Db_FindDevice(&deviceInfo, dpInfo.deviceId);
                            if (foundDevices == 1) {
                                Db_SaveDpValue(dpInfo.deviceId, dpInfo.id, dpValue);
                                Db_SaveDeviceState(dpInfo.deviceId, STATE_ONLINE);
                                Aws_SaveDpValue(dpInfo.deviceId, dpInfo.id, dpValue, dpInfo.pageIndex);
                                JSON_SetText(payload, "deviceId", dpInfo.deviceId);
                                JSON_SetNumber(payload, "dpId", dpInfo.id);
                                JSON_SetNumber(payload, "eventType", EV_DEVICE_DP_CHANGED);
                                JSON_SetNumber(payload, "pageIndex", dpInfo.pageIndex);
                                if (opcode != 0x8201) {
                                    // Check and run scenes for this device if any
                                    checkSceneForDevice(dpInfo.deviceId, dpInfo.id, true);
                                    if (opcode == 0) {
                                        JSON_SetNumber(payload, "causeType", EV_CAUSE_TYPE_DEVICE);
                                        Db_AddDeviceHistory(payload);
                                    }
                                    GetDeviceStatusForGroup(dpInfo.deviceId, dpInfo.id);
                                } else {
                                    // The response is from getting status of devices actively
                                    if (oldDpValue != dpValue) {
                                        checkSceneForDevice(dpInfo.deviceId, dpInfo.id, false);
                                        JSON_SetNumber(payload, "causeType", EV_CAUSE_TYPE_SYNC);
                                        Db_AddDeviceHistory(payload);
                                    }
                                }
                            }
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
                                JSON_SetText(payload, "deviceId", deviceInfo.id);
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
                        uint8_t respType = JSON_GetNumber(payload, "respType");
                        if (respType == 0) {
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
                        } else {
                            uint16_t voiceId = JSON_GetNumber(payload, "voiceId");
                            char sql[500];
                            sprintf(sql, "select d.deviceId from devices d JOIN devices_inf di ON d.deviceId=di.deviceId where dpId=1 AND pid='BLEHGAA0301'");
                            Sql_Query(sql, row) {
                                char* deviceId = sqlite3_column_text(row, 0);
                                Db_SaveDpValue(deviceId, 1, voiceId);
                                checkSceneForDevice(deviceId, 1, true);
                                Db_SaveDpValue(deviceId, 1, 0);
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
                            Db_SaveDpValue(deviceInfo.id, dpId, dpValue);
                            Aws_SaveDpValue(deviceInfo.id, dpId, dpValue, deviceInfo.pageIndex);
                            JSON_SetNumber(payload, "causeType", 0);
                            JSON_SetText(payload, "causeId", "");
                            JSON_SetNumber(payload, "eventType", EV_DEVICE_DP_CHANGED);
                            Db_AddDeviceHistory(payload);
                            checkSceneForDevice(deviceInfo.id, dpId, true);     // Check and run scenes for this device if any
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
                    case TYPE_GET_ALL_DEVICES: {
                        JSON* p = JSON_CreateObject();
                        JSON_SetNumber(p, "type", TYPE_GET_ALL_DEVICES);
                        JSON_SetNumber(p, "sender", SENDER_HC_VIA_CLOUD);
                        JSON* devicesArray = Db_GetAllDevices();
                        JSON* devices = JSON_CreateObject();
                        JSON_ForEach(d, devicesArray) {
                            JSON* item = JSON_CreateObject();
                            JSON_SetObject(item, "dictDPs", JSON_GetObject(d, "dictDPs"));
                            JSON_SetObject(devices, JSON_GetText(d, "deviceId"), item);
                        }
                        JSON_SetObject(p, "devices", devices);
                        char* msg = cJSON_PrintUnformatted(p);
                        sendToService(SERVICE_AWS, TYPE_NOTIFI_REPONSE, msg);
                        JSON_Delete(p);
                        JSON_Delete(devicesArray);
                        free(msg);
                        break;
                    }
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
                    case TYPE_CTR_DEVICE: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        char* senderId = JSON_GetText(payload, "senderId");
                        JSON* dictDPs = JSON_GetObject(payload, "dictDPs");
                        Ble_ControlDeviceJSON(deviceId, dictDPs, senderId);
                        break;
                    }
                    case TYPE_CTR_GROUP_NORMAL: {
                        char* groupAddr = JSON_GetText(payload, "groupAdress");
                        JSON* dictDPs = JSON_GetObject(payload, "dictDPs");
                        Ble_ControlGroup(groupAddr, dictDPs);
                        break;
                    }
                    case TYPE_CTR_SCENE: {
                        char* senderId = JSON_GetText(payload, "senderId");
                        char* sceneId = NULL;
                        int state = 1;
                        JSON_ForEach(o, payload) {
                            if (cJSON_IsObject(o)) {
                                sceneId = o->string;
                                state = JSON_GetNumber(o, "state");
                            }
                        }
                        if (sceneId) {
                            // Check if this scene is HC or local
                            bool isLocal = true;
                            int sceneType = 0;
                            for (int i = 0; i < g_sceneCount; i++) {
                                if (StringCompare(g_sceneList[i].id, sceneId)) {
                                    isLocal = g_sceneList[i].isLocal;
                                    sceneType = g_sceneList[i].type;
                                    break;
                                }
                            }
                            if (sceneType != SceneTypeManual) {
                                if (state)  { logInfo("Enabling scene %s", sceneId); }
                                else        { logInfo("Disabling scene %s", sceneId); }
                                Db_EnableScene(sceneId, state);
                            } else {
                                if (!isLocal) {
                                    logInfo("Executing HC scene %s", sceneId);
                                    for (int i = 0; i < g_sceneCount; i++) {
                                        if (StringCompare(g_sceneList[i].id, sceneId)) {
                                            markSceneToRun(&g_sceneList[i], senderId);
                                            break;
                                        }
                                    }
                                }
                            }
                            if (isLocal) {
                                logInfo("Sending LC scene to BLE %s", sceneId);
                                JSON* p = JSON_CreateObject();
                                JSON_SetText(p, "sceneId", sceneId);
                                JSON_SetNumber(p, "state", state);
                                if (sceneType == SceneTypeManual) {
                                    JSON_SetNumber(p, "state", 2);
                                }
                                sendPacketTo(SERVICE_BLE, reqType, p);
                                // Update status of devices in this scene to AWS
                                for (int s = 0; s < g_sceneCount; s++) {
                                    if (StringCompare(g_sceneList[s].id, sceneId)) {
                                        Scene* scene = &g_sceneList[s];
                                        for (int act = 0; act < scene->actionCount; act++) {
                                            if (scene->actions[act].actionType == EntityDevice) {
                                                DeviceInfo deviceInfo;
                                                int foundDevices = Db_FindDevice(&deviceInfo, scene->actions[act].entityId);
                                                if (foundDevices == 1) {
                                                    for (int dp = 0; dp < scene->actions[act].dpCount; dp++) {
                                                        Aws_SaveDpValue(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp], deviceInfo.pageIndex);
                                                    }
                                                }
                                            }
                                        }
                                        GetDeviceStatusForScene(scene);
                                        break;
                                    }
                                }

                                JSON_Delete(p);
                            }
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
                            JSON_Delete(oldDps);

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
                                    int gwIndex = JSON_GetNumber(d, "gwIndex");
                                    Ble_SetTTL(gwIndex, deviceAddr, ttl);
                                    addDeviceToRespList(GW_RESPONSE_SET_TTL, "0", deviceAddr);
                                }
                            }
                        }
                        break;
                    }
                }
            } else if(isMatchString(NameService, SERVICE_HANET) && payload) {
                switch (reqType) {
                    case CAM_HANET_RESPONSE: {
                        logInfo("CAM_HANET_RESPONSE");
                        char* person_id = JSON_GetText(payload, "person_id");
                        char* person_name = JSON_GetText(payload, "person_name");
                        int person_type = JSON_GetNumber(payload, "person_type");
                        char* camera_id = JSON_GetText(payload, "camera_id");
                        logInfo("person_id = %s",person_id);
                        logInfo("person_name = %s",person_name);
                        logInfo("person_type = %d",person_type);
                        logInfo("camera_id = %s",camera_id);
                        if(person_type == 0 || person_type == 1){ //camera phát hiện người nhà hoặc người quen
                            person_type = 3; //synch dpID reponse of Hanet with app Homegy
                            DpInfo dpInfo;
                            int foundDps = Db_FindDp(&dpInfo, camera_id, person_type);
                            logInfo("foundDps = %d",foundDps);
                            if (foundDps == 1) {
                                Db_SaveDpValueString(dpInfo.deviceId, dpInfo.id, person_id);
                                checkSceneForDevice(camera_id, person_type, true);
                            }
                        } else if(person_type == 2){//camera phát hiện người la
                            person_type = 5; //synch dpID reponse of Hanet with app Homegy
                            DpInfo dpInfo;
                            int foundDps = Db_FindDp(&dpInfo, camera_id, person_type);
                            logInfo("foundDps = %d",foundDps);
                            if (foundDps == 1) {
                                Db_SaveDpValue(dpInfo.deviceId, dpInfo.id, 2);
                                checkSceneForDevice(camera_id, person_type, true);
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


bool Scene_GetFullInfo(JSON* packet) {
    JSON* actionsArray = JSON_GetObject(packet, "actions");
    int isLocal = JSON_GetNumber(packet, "isLocal");
    JSON* newActionsArray = JSON_CreateArray();
    int i = 1;
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
        if (StringCompare(actionExecutor, "irHGBLE")) {
            Ble_AddExtraDpsToIrDevices(entityId, executorProperty);
            JSON_SetNumber(action, "commandIndex", i++);
        }
    }
    JSON* conditionsArray = JSON_GetObject(packet, "conditions");
    JSON_ForEach(condition, conditionsArray) {
        int schMinutes = 0;
        uint8_t repeat = 0;
        JSON* exprArray = JSON_GetObject(condition, "expr");
        char* entityId = JSON_GetText(condition, "entityId");
        if (StringCompare(entityId, "timer")) {
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
                // int dpValue = JArr_GetNumber(exprArray, 2);
                JSON* objItem = JArr_GetObject(exprArray, 2); //get obj for check type value of exprArray
                DpInfo dpInfo;
                int foundDps = Db_FindDp(&dpInfo, entityId, dpId);
                if (foundDps == 1 || StringCompare(deviceInfo.pid, HG_BLE_IR)) {
                    JSON_SetText(condition, "pid", deviceInfo.pid);
                    JSON_SetText(condition, "entityAddr", deviceInfo.addr);
                    JSON_SetNumber(condition, "dpId", dpId);
                    if (foundDps == 1) {
                        JSON_SetText(condition, "dpAddr", dpInfo.addr);
                    }
                    if(cJSON_IsString(objItem)){ //check type value of conditions and set
                        JSON_SetNumber(condition, "valueType", ValueTypeString);
                        char* dpValueStr = JArr_GetText(exprArray, 2);
                        JSON_SetText(condition, "dpValueStr", dpValueStr);
                    } else if(cJSON_IsNumber(objItem) || cJSON_IsBool(objItem)){
                        JSON_SetNumber(condition, "valueType", ValueTypeDouble);
                        int dpValue = JArr_GetNumber(exprArray, 2);
                        JSON_SetNumber(condition, "dpValue", dpValue);
                    }
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>     // string function definitions
#include <fcntl.h>      // File control definitions
#include <errno.h>      // Error number definitions
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
#include "define.h"
#include "time_t.h"
#include "queue.h"
#include "parson.h"
#include "database.h"
#include "aws_mosquitto.h"
#include "core_api.h"
#include "helper.h"
#include "messages.h"
#include "cJSON.h"
#include "common.h"

int GWCFG_TIMEOUT_SCENEGROUP = 5000;
int GWCFG_GET_ONLINE_TIME = 120000;

const char* SERVICE_NAME = SERVICE_CORE;
uint8_t SERVICE_ID = SERVICE_ID_CORE;
bool g_printLog = true;

struct mosquitto * mosq;
sqlite3 *db;

struct Queue *queue_received;
Scene* g_sceneList;
int g_sceneCount = 0;
static bool g_mosqIsConnected = false;
static JSON* g_sceneTobeUpdated;        // Used to save the scene info when user want to add/edit a scene, then we will push the updated scene to aws
static JSON* g_groupTobeUpdated;        // Used to save the group info when user want to add/edit a group, then we will push the updated group to aws

bool sendInfoDeviceFromDatabase();
bool sendInfoSceneFromDatabase();
bool Scene_GetFullInfo(JSON* packet);
void SyncDevicesState();
void GetDeviceStatusForScene(Scene* scene);
void GetDeviceStatusForGroup(const char* deviceId, int dpId, double dpValue);
void DeleteDeviceFromGroups(const char* deviceId);
void DeleteDeviceFromScenes(const char* deviceId);

bool addNewDevice(JSON* packet) {
    char* deviceStr = JSON_GetText(packet, "devices");
    List* tmp = String_Split(deviceStr, "|");
    if (tmp->count >= 5) {
        JSON* dictDPs = JSON_GetObject(packet, "dictDPs");
        char* deviceId = JSON_GetText(packet, KEY_DEVICE_ID);
        char* deviceAddr = tmp->items[3];
        char* pid = tmp->items[1];
        int pageIndex = JSON_GetNumber(packet, "pageIndex");
        int onlineState = JSON_GetNumber(packet, "state");
        char* gatewayAddr = JSON_HasKey(packet, "gateWay")? JSON_GetText(packet, "gateWay"): "_";
        JSON_SetText(packet, KEY_UNICAST, tmp->items[3]);
        JSON_SetText(packet, "deviceAddr", deviceAddr);
        JSON_SetText(packet, KEY_DEVICE_KEY, tmp->items[4]);
        JSON_SetNumber(packet, KEY_PROVIDER, atoi(tmp->items[0]));
        JSON_SetText(packet, KEY_PID, pid);
        JSON_SetText(packet, "devicePid", pid);
        if (Db_AddDevice(packet) == 0) {
            return false;
        }
        Db_SaveDeviceState(deviceId, onlineState);
        bool addressIncrement = StringContains(HG_BLE_SWITCH, pid) || StringContains(HG_BLE_CURTAIN, pid)? true : false;
        // Calculate addresses of dps for Homegy switch
        char dpAddrStr[5];
        StringCopy(dpAddrStr, deviceAddr);
        uint8_t dpAddrMsb = strtol(&dpAddrStr[2], NULL, 16);
        dpAddrStr[2] = 0;
        uint16_t dpAddrLsb = strtol(dpAddrStr, NULL, 16);
        uint16_t tmp = dpAddrMsb;
        tmp = (tmp << 8) + dpAddrLsb;

        JSON_ForEach(dp, dictDPs) {
            int dpId = atoi(dp->string);
            int dpValue = dp->valueint;
            uint16_t increasedAddr = tmp + dpId - 1;
            sprintf(dpAddrStr, "%02X%02X", increasedAddr & 0x00FF, increasedAddr >> 8);
            if (addressIncrement) {
                Db_AddDp(deviceId, dpId, dpAddrStr, pageIndex);
            } else {
                Db_AddDp(deviceId, dpId, deviceAddr, pageIndex);
            }
            Db_SaveDpValue(deviceId, dpId, dpValue);
        }

        if (StringCompare(pid, HG_BLE_IR_AC) ||
            StringCompare(pid, HG_BLE_IR_TV) ||
            StringCompare(pid, HG_BLE_IR_FAN) ||
            StringCompare(pid, HG_BLE_IR_REMOTE)) {
            Db_AddDp(deviceId, 3, deviceAddr, pageIndex);
            JSON* dictDPs = JSON_GetObject(packet, "dictDPs");
            JSON_ForEach(dp, dictDPs) {
                int dpId = atoi(dp->string);
                if (cJSON_IsNumber) {
                    Db_SaveDpValue(deviceId, dpId, dp->valueint);
                } else {
                    Db_SaveDpValueString(deviceId, dpId, dp->valuestring);
                }
            }
        } else if (StringCompare(pid, CAM_HANET)) {
            JSON* dictDPs = JSON_GetObject(packet, "dictDPs");
            JSON_ForEach(dp, dictDPs) {
                int dpId = atoi(dp->string);
                Db_AddDp(deviceId, dpId, dp->valuestring, pageIndex);
                if (cJSON_IsNumber) {
                    Db_SaveDpValue(deviceId, dpId, dp->valueint);
                } else {
                    Db_SaveDpValueString(deviceId, dpId, dp->valuestring);
                }
            }
        }
        logInfo("Added device %s", deviceId);
        return true;
    } else {
        printInfo("[Error] Parsing Device Error: %s", deviceStr);
    }
    List_Delete(tmp);
    return false;
}


void SetOnlineStateForIRDevices(const char* deviceAddr, int onlineState, const char* hcAddr, bool syncToAws) {
    ASSERT(deviceAddr);
    ASSERT(hcAddr);
    DeviceInfo deviceInfos[50];
    int count = 0;
    char sqlCommand[200];
    sprintf(sqlCommand, "SELECT * FROM devices_inf d JOIN gateway g ON g.id = d.gwIndex WHERE Unicast = '%s' AND g.hcAddr = '%s';", deviceAddr, hcAddr);

    Sql_Query(sqlCommand, row) {
        deviceInfos[count].state = sqlite3_column_int(row, 1);
        deviceInfos[count].provider = sqlite3_column_int(row, 7);
        StringCopy(deviceInfos[count].id, sqlite3_column_text(row, 0));
        StringCopy(deviceInfos[count].name, sqlite3_column_text(row, 2));
        StringCopy(deviceInfos[count].addr, sqlite3_column_text(row, 4));
        deviceInfos[count].gwIndex = sqlite3_column_int(row, 5);
        StringCopy(deviceInfos[count].pid, sqlite3_column_text(row, 8));
        deviceInfos[count].pageIndex = sqlite3_column_int(row, 15);
        deviceInfos[count].offlineCount = sqlite3_column_int(row, 16);
        count++;
    }

    for (int i = 0; i < count; i++) {
        if (onlineState == STATE_OFFLINE) {
            if (deviceInfos[i].offlineCount < 2) {
                Db_SaveOfflineCountForDevice(deviceInfos[i].id, deviceInfos[i].offlineCount + 1);
                break;
            }
        }
        if (onlineState != deviceInfos[i].state) {
            Aws_SaveDeviceState(deviceInfos[i].id, onlineState, deviceInfos[i].pageIndex);
        }
        // JSON_SetText(payload, "deviceId", deviceInfos[i].id);
        Db_SaveOfflineCountForDevice(deviceInfos[i].id, 0);
        Db_SaveDeviceState(deviceInfos[i].id, onlineState);
        // JSON_SetNumber(payload, "dpValue", onlineState == 2? 1 : 0);
        // JSON_SetNumber(payload, "eventType", EV_DEVICE_STATE_CHANGED);
        // Db_AddDeviceHistory(payload);
    }
}


void DeleteDeviceFromGroups(const char* deviceId) {
    ASSERT(deviceId);
    char* sqlCmd = "SELECT groupAdress, devices, pageIndex FROM group_inf";
    Sql_Query(sqlCmd, row) {
        char* groupAddr = sqlite3_column_text(row, 0);
        char* deviceStr = sqlite3_column_text(row, 1);
        JSON* devices = JSON_Parse(deviceStr);
        JSON* newDevices = JSON_CreateArray();
        JSON_ForEach(d, devices) {
            char* id = JSON_GetText(d, "deviceId");
            if (!StringCompare(id, deviceId)) {
                JArr_AddObject(newDevices, JSON_Clone(d));
            }
        }
        // Save new devices if needed
        if (JArr_Count(newDevices) == 0) {
            // Delete group if there is no any devices in it
            Aws_DeleteGroup(groupAddr);
            Db_DeleteGroup(groupAddr);
            DeleteDeviceFromScenes(groupAddr);
        } else if (JArr_Count(devices) != JArr_Count(newDevices)) {
            logInfo("Update devices for group %s", groupAddr);
            Db_SaveGroupDevices(groupAddr, newDevices);
            Aws_SaveGroupDevices(groupAddr);
        }
        JSON_Delete(newDevices);
    }
}


void DeleteDeviceFromScenes(const char* deviceId) {
    ASSERT(deviceId);
    long start = time(NULL);
    logInfo("[DeleteDeviceFromScenes] deviceId=%s, start=%ld", deviceId, start);
    char* sqlCmd = "SELECT sceneId, actions, conditions, pageIndex FROM scene_inf";
    Sql_Query(sqlCmd, row) {
        char* sceneId = sqlite3_column_text(row, 0);
        char* actionStr = sqlite3_column_text(row, 1);
        char* conditionStr = sqlite3_column_text(row, 2);
        JSON* actions = JSON_Parse(actionStr);
        JSON* conditions = JSON_Parse(conditionStr);
        JSON* newActions = JSON_CreateArray();
        JSON* newConditions = JSON_CreateArray();
        JSON_ForEach(action, actions) {
            char* id = JSON_GetText(action, "entityId");
            if (!StringCompare(id, deviceId)) {
                JArr_AddObject(newActions, JSON_Clone(action));
            }
        }
        JSON_ForEach(condition, conditions) {
            char* id = JSON_GetText(condition, "entityId");
            if (!StringCompare(id, deviceId)) {
                JArr_AddObject(newConditions, JSON_Clone(condition));
            }
        }
        // Save new actions and conditions if needed
        if (JArr_Count(newActions) == 0) {
            // Delete scene if there is no any action in it
            logInfo("Delete scene %s because it has no any action", sceneId);
            if (JArr_Count(newConditions) > 0) {
                // Send command to delete this scene in this device
                JSON* sceneInfo = Db_FindScene(sceneId);
                if (sceneInfo) {
                    int isLocal = JSON_GetNumber(sceneInfo, "isLocal");
                    if (isLocal) {
                        Scene_GetFullInfo(sceneInfo);
                        sendPacketToBle(-1, TYPE_DEL_SCENE, sceneInfo);   // Send packet to BLE
                    }
                } else {
                    logError("Scene %s not found", sceneId);
                }
                JSON_Delete(sceneInfo);
            }
            Aws_DeleteScene(sceneId);
            Db_DeleteScene(sceneId);
            DeleteDeviceFromScenes(sceneId);
        } else if (JArr_Count(actions) != JArr_Count(newActions) || JArr_Count(conditions) != JArr_Count(newConditions)) {
            logInfo("Update actions and conditions for scene %s", sceneId);
            Db_SaveScene(sceneId, newActions, newConditions);
            Db_LoadSceneToRam();
            Aws_SaveScene(sceneId);
        }

        JSON_Delete(newActions);
    }
    Db_LoadSceneToRam();
    long end = time(NULL);
    logInfo("[DeleteDeviceFromScenes] deviceId=%s, end=%ld, spent=%d(ms)", deviceId, end, end - start);
}


void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if(rc)
    {
        logError("Error with result code: %d\n", rc);
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, "CORE_LOCAL/#", 0);
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_CORE_DATA, 0);
    mosquitto_subscribe(mosq, NULL, "/topic/detected/#", 0); //subcribe MQTT for camera HANET
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    if (StringCompare((char*)msg->payload, "CORE_PING")) {
        mosquitto_publish(mosq, NULL, "APPLICATION_SERVICES/AWS/0", strlen("CORE_PONG"), "CORE_PONG", 0, false);
        return;
    }
    int reponse = 0;
    bool check_flag = false;

    int size_queue = get_sizeQueue(queue_received);
    if (size_queue < QUEUE_SIZE) {
        if(StringContains( msg->topic,MOSQ_LayerService_Core)){
            enqueue(queue_received,(char *) msg->payload);
        } else if (StringContains( msg->topic,"/topic/detected/")){
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
    mosq = mosquitto_new("HG_CORE", true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        logInfo("Client could not connect to broker! Error Code: %d", rc);
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
        int successCount = 0, failedCount = 0, noRespCount = 0;
        for (int d = 0; d < deviceCount; d++) {
            JSON* device = JArr_GetObject(devices, d);
            int deviceStatus = JSON_GetNumber(device, "status");
            if (deviceStatus == 0) {
                successCount++;
            } else if (deviceStatus > -2) {
                failedCount++;
            } else {
                noRespCount++;
            }
        }
        char reqId[20];
        StringCopy(reqId, JSON_GetText(respItem, "reqType"));   // reqId = reqType.itemId
        int reqType  = atoi(strtok(reqId, "."));
        char* itemId = strtok(NULL, ".");
        // Calculate timeout based on number of successed and failed devices
        int timeout = deviceCount * (GWCFG_TIMEOUT_SCENEGROUP + 500);
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
            if (reqType == TYPE_ADD_GROUP_LIGHT || reqType == TYPE_UPDATE_GROUP_LIGHT ||
                reqType == TYPE_ADD_GROUP_LINK || reqType == TYPE_UPDATE_GROUP_LINK) {
                // Save state of devices to AWS
                JSON* newDevices = JSON_GetObject(g_groupTobeUpdated, "devices");
                JSON* updatedDevices = JSON_CreateArray();
                JSON_ForEach(dev, newDevices) {
                    int state = JSON_HasKey(dev, "state")? JSON_GetNumber(dev, "state") : -2;
                    if (state == 0) {
                        JSON* updatedDevice = JSON_Clone(dev);
                        JArr_AddObject(updatedDevices, updatedDevice);
                    } else if (state == -2) {
                        char* deviceId = JSON_GetText(dev, "deviceId");
                        // Find actual status of this device
                        int status = -1;
                        for (int d = 0; d < deviceCount; d++) {
                            JSON* device = JArr_GetObject(devices, d);
                            char* respDeviceId = JSON_GetText(device, "deviceId");
                            if (StringCompare(deviceId, respDeviceId)) {
                                status = JSON_GetNumber(device, "status");
                                break;
                            }
                        }
                        status = status == -2? -1 : status;
                        JSON* updatedDevice = JSON_Clone(dev);
                        JSON_SetNumber(updatedDevice, "state", status);
                        JArr_AddObject(updatedDevices, updatedDevice);
                    } else if (state == -3) {
                        char* deviceId = JSON_GetText(dev, "deviceId");
                        // Find status of this device and remove it if this device is deleted successfully
                        int status = -1;
                        for (int d = 0; d < deviceCount; d++) {
                            JSON* device = JArr_GetObject(devices, d);
                            char* respDeviceId = JSON_GetText(device, "deviceId");
                            if (StringCompare(deviceId, respDeviceId)) {
                                status = JSON_GetNumber(device, "status");
                                break;
                            }
                        }
                        if (status != 0) {
                            status = status == -2? -1 : status;
                            JSON* updatedDevice = JSON_Clone(dev);
                            JSON_SetNumber(updatedDevice, "state", status);
                            JArr_AddObject(updatedDevices, updatedDevice);
                        }
                    }
                }
                // if (JArr_Count(updatedDevices) > 0) {
                    JSON_SetObject(g_groupTobeUpdated, "devices", updatedDevices);
                    Aws_UpdateGroupDevices(g_groupTobeUpdated);
                    char* groupAddr = JSON_GetText(g_groupTobeUpdated, "groupAddr");
                    Db_SaveGroupDevices(groupAddr, updatedDevices);
                // }
                JSON_Delete(g_groupTobeUpdated);
            } else if (reqType == TYPE_ADD_SCENE || reqType == TYPE_UPDATE_SCENE) {
                // Save state of devices to AWS
                JSON* newActions = JSON_GetObject(g_sceneTobeUpdated, "actions");
                JSON* updatedActions = JSON_CreateArray();
                JSON_ForEach(action, newActions) {
                    int state = JSON_HasKey(action, "state")? JSON_GetNumber(action, "state") : -2;
                    char* entityId = JSON_GetText(action, "entityId");
                    JSON* executorProperty = JSON_GetObject(action, "executorProperty");
                    int dpId = 0;
                    if (JArr_Count(executorProperty) == 1) {
                        JSON_ForEach(o, executorProperty) {
                            dpId = atoi(o->string);
                            dpId = dpId <= 4? dpId : 0;
                        }
                    }
                    if (state == 0) {
                        JSON* updatedAction = JSON_Clone(action);
                        JArr_AddObject(updatedActions, updatedAction);
                    } else if (state == -2) {
                        // Find status of this device
                        for (int d = 0; d < deviceCount; d++) {
                            JSON* device = JArr_GetObject(devices, d);
                            char* deviceId = JSON_GetText(device, "deviceId");
                            int respDpId = JSON_GetNumber(device, "dpId");
                            if (StringCompare(entityId, deviceId)) {
                                if (dpId == 0 || dpId == respDpId) {
                                    JSON* updatedAction = JSON_Clone(action);
                                    int status = JSON_GetNumber(device, "status");
                                    status = status == -2? -1 : status;
                                    JSON_SetNumber(updatedAction, "state", status);
                                    JArr_AddObject(updatedActions, updatedAction);
                                    break;
                                }
                            }
                        }
                    } else if (state == -3) {
                        // Find status of this device and remove it if this device is deleted successfully
                        for (int d = 0; d < deviceCount; d++) {
                            JSON* device = JArr_GetObject(devices, d);
                            char* respDeviceId = JSON_GetText(device, "deviceId");
                            int respDpId = JSON_GetNumber(device, "dpId");
                            if (StringCompare(entityId, respDeviceId)) {
                                if (dpId == 0 || dpId == respDpId) {
                                    int status = JSON_GetNumber(device, "status");
                                    if (status != 0) {
                                        status = status == -2? -1 : status;
                                        JSON* updatedAction = JSON_Clone(action);
                                        JSON_SetNumber(updatedAction, "state", status);
                                        JArr_AddObject(updatedActions, updatedAction);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
                JSON_SetObject(g_sceneTobeUpdated, "actions", updatedActions);

                JSON* newConditions = JSON_GetObject(g_sceneTobeUpdated, "conditions");
                JSON* updatedConditions = JSON_CreateArray();
                JSON_ForEach(condition, newConditions) {
                    int state = JSON_HasKey(condition, "state")? JSON_GetNumber(condition, "state") : -2;
                    char* entityId = JSON_GetText(condition, "entityId");
                    int dpId = JSON_GetNumber(condition, "subDeviceId");
                    dpId = dpId <= 4? dpId : 0;
                    if (state == 0) {
                        JSON* updatedCondition = JSON_Clone(condition);
                        JArr_AddObject(updatedConditions, updatedCondition);
                    } else if (state == -2) {
                        // Find status of this device
                        for (int d = 0; d < deviceCount; d++) {
                            JSON* device = JArr_GetObject(devices, d);
                            char* deviceId = JSON_GetText(device, "deviceId");
                            int respDpId = JSON_GetNumber(device, "dpId");
                            if (StringCompare(entityId, deviceId)) {
                                if (dpId == 0 || dpId == respDpId) {
                                    JSON* updatedCondition = JSON_Clone(condition);
                                    int status = JSON_GetNumber(device, "status");
                                    status = status == -2? -1 : status;
                                    JSON_SetNumber(updatedCondition, "state", status);
                                    JArr_AddObject(updatedConditions, updatedCondition);
                                    break;
                                }
                            }
                        }
                    } else if (state == -3) {
                        // Find status of this device and remove it if this device is deleted successfully
                        for (int d = 0; d < deviceCount; d++) {
                            JSON* device = JArr_GetObject(devices, d);
                            char* respDeviceId = JSON_GetText(device, "deviceId");
                            int respDpId = JSON_GetNumber(device, "dpId");
                            if (StringCompare(entityId, respDeviceId)) {
                                if (dpId == 0 || dpId == respDpId) {
                                    int status = JSON_GetNumber(device, "status");
                                    if (status != 0) {
                                        status = status == -2? -1 : status;
                                        JSON* updatedCondition = JSON_Clone(condition);
                                        JSON_SetNumber(updatedCondition, "state", status);
                                        JArr_AddObject(updatedConditions, updatedCondition);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
                JSON_SetObject(g_sceneTobeUpdated, "conditions", updatedConditions);
                Aws_UpdateSceneInfo(g_sceneTobeUpdated);
                char* sceneId = JSON_GetText(g_sceneTobeUpdated, "id");
                Db_SaveScene(sceneId, updatedActions, updatedConditions);
                Db_LoadSceneToRam();
                JSON_Delete(g_sceneTobeUpdated);
            } else if (reqType == TYPE_CTR_DEVICE) {
                for (int d = 0; d < deviceCount; d++) {
                    JSON* device = JArr_GetObject(devices, d);
                    char* deviceId = JSON_GetText(device, "deviceId");
                    char* causeId = JSON_GetText(device, "causeId");
                    int status = JSON_GetNumber(device, "status");
                    if (status != 0) {
                        // Save event to history
                        JSON* history = JSON_CreateObject();
                        JSON_SetNumber(history, "eventType", EV_CTR_DEVICE_FAILED);
                        if (StringLength(causeId) > 20) {
                            sendNotiToUser("Thiết bị không phản hồi", false);
                            JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_APP);
                            JSON_SetText(history, "causeId", causeId);
                        } else {
                            JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_SCENE);
                            JSON_SetText(history, "causeId", causeId);
                        }
                        JSON_SetText(history, "deviceId", deviceId);
                        Db_AddDeviceHistory(history);
                        JSON_Delete(history);
                    }
                }
            }
            // Remove this respItem from response list
            JArr_RemoveIndex(g_checkRespList, i);
            // Send notification to user
            if (reqType != TYPE_CTR_DEVICE) {
                char noti[200];
                sprintf(noti, "{\"successCount\": %d, \"failedCount\": %d, \"noResponseCount\": %d, \"done\":true, \"id\": \"%s\"}", successCount, failedCount, noRespCount, itemId);
                sendNotiToUser(noti, true);
            }
        } else {
            int oldSuccessCount = JSON_HasKey(respItem, "successCount")? JSON_GetNumber(respItem, "successCount") : 0;
            int oldFailedCount = JSON_HasKey(respItem, "failedCount")? JSON_GetNumber(respItem, "failedCount") : 0;
            int oldNoResponseCount = JSON_HasKey(respItem, "noResponseCount")? JSON_GetNumber(respItem, "noResponseCount") : 0;
            if (successCount != oldSuccessCount || failedCount != oldFailedCount || noRespCount != oldNoResponseCount) {
                JSON_SetNumber(respItem, "successCount", successCount);
                JSON_SetNumber(respItem, "failedCount", failedCount);
                JSON_SetNumber(respItem, "noResponseCount", noRespCount);
                // Send real time status notification to user
                if (reqType != TYPE_CTR_DEVICE) {
                    char noti[200];
                    sprintf(noti, "{\"successCount\": %d, \"failedCount\": %d, \"noResponseCount\": %d, \"done\":false, \"id\": \"%s\"}", successCount, failedCount, noRespCount, itemId);
                    sendNotiToUser(noti, true);
                }
            }
        }
    }
}

void markSceneToRun(Scene* scene, const char* causeId) {
    ASSERT(scene);
    printInfo("  markSceneToRun: %s", scene->id);
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

void sendSceneRunEventToUser(Scene* scene, int triggerConditionIndex) {
    ASSERT(scene);
    char noti[500];
    char cond[200];
    if (scene->type == SceneTypeManual) {
        sprintf(cond, "Chạy bằng tay");
    } if (scene->type == SceneTypeAllConds) {
        sprintf(cond, "thỏa mãn tất cả điều kiện");
    } else if (triggerConditionIndex < scene->conditionCount) {
        SceneCondition* condition = &scene->conditions[triggerConditionIndex];
        if (condition->conditionType == EntitySchedule) {
            sprintf(cond, "Đặt lịch");
        } else if (condition->conditionType == EntityDevice) {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, condition->entityId);
            if (foundDevices == 1) {
                sprintf(cond, "Thiết bị '%s'", deviceInfo.name);
            }
        }
    }
    sprintf(noti, "Kịch bản '%s' được chạy do điều kiện: %s", scene->name, cond);
    sendNotiToUser(noti, false);
}

bool checkScenePrecondition(Scene* scene) {
    if (!scene->isEnable) {
        printInfo("  Scene is disabled");
        return false;
    }
    bool effectTime = false;
    if (scene->effectFrom > 0 || scene->effectTo > 0) {
        time_t rawtime; struct tm *info; time( &rawtime ); info = localtime(&rawtime);
        int todayMinutes = info->tm_hour * 60 + info->tm_min;
        if (scene->effectFrom < scene->effectTo) {
            // effectFrom and effectTo are in the same day
            uint8_t weekdayMark = (0x40 >> info->tm_wday) & scene->effectRepeat;
            if (weekdayMark && todayMinutes >= scene->effectFrom && todayMinutes <= scene->effectTo) {
                effectTime = true;
            }
        } else {
            // effectFrom and effectTo are in the different days
            uint8_t todayMark = (0x40 >> info->tm_wday) & scene->effectRepeat;
            uint8_t tomorrowMark = (scene->effectRepeat & 0x01)? scene->effectRepeat | 0x80 : scene->effectRepeat;
            tomorrowMark = (0x40 >> info->tm_wday) & (tomorrowMark >> 1);
            if ((todayMark && todayMinutes >= scene->effectFrom) ||
                (tomorrowMark && todayMinutes <= scene->effectTo)) {
                effectTime = true;
            }
        }
    } else {
        effectTime = true;
    }
    if (!effectTime) {
        printInfo("  Effect time is not satisfied");
    }
    return effectTime;
}

// Check if a scene is need to be executed or not
void checkSceneCondition(Scene* scene) {
    printInfo("  checkSceneCondition: %s", scene->id);
    // Don't check condition for manual scene
    if (scene->type == SceneTypeManual) {
        sendSceneRunEventToUser(scene, 0);
        markSceneToRun(scene, NULL);
        return;
    }

    // Check effective time
    if (checkScenePrecondition(scene) == false) {
        return false;
    }

    // Only check conditions of scene if current time is effective and scene is enabled
    if (scene->type == SceneTypeOneOfConds) {
        printInfo("  sceneType = SceneTypeOneOfConds");
        for (int i = 0; i < scene->conditionCount; i++) {
            printInfo("    conditions[%d]: type=%d", i, scene->conditions[i].conditionType);
            if (scene->conditions[i].conditionType == EntitySchedule) {
                if (scene->conditions[i].timeReached) {
                    printInfo("      Satisfied. schMinutes=%d", scene->conditions[i].schMinutes);
                    sendSceneRunEventToUser(scene, i);
                    markSceneToRun(scene, NULL);
                } else {
                    printInfo("      Not satisfied. schMinutes=%d", scene->conditions[i].schMinutes);
                }
            } else {
                DpInfo dpInfo;
                int foundDps = Db_FindDp(&dpInfo, scene->conditions[i].entityId, scene->conditions[i].dpId);
                //Check type of value in DB is Double or String ( with many Device is double type, with Camera Hanet is string type(personID))
                if (foundDps == 1) {
                    if (scene->conditions[i].valueType == ValueTypeDouble) {
                        if (dpInfo.value == scene->conditions[i].dpValue){
                            printInfo("      Satisfied. current value=%.0f, expect=%.0f", dpInfo.value, scene->conditions[i].dpValue);
                            sendSceneRunEventToUser(scene, i);
                            markSceneToRun(scene, NULL);
                            break;
                        } else {
                            printInfo("      Not satisfied. current value=%.0f, expect=%.0f", dpInfo.value, scene->conditions[i].dpValue);
                        }
                    } else {
                        if(StringCompare(dpInfo.valueStr, scene->conditions[i].dpValueStr)){
                            printInfo("      Satisfied. current value='%s', expect='%s'", dpInfo.valueStr, scene->conditions[i].dpValueStr);
                            sendSceneRunEventToUser(scene, i);
                            markSceneToRun(scene, NULL);
                            break;
                        } else {
                            printInfo("      Not satisfied. current value='%s', expect='%s'", dpInfo.valueStr, scene->conditions[i].dpValueStr);
                        }
                    }
                } else {
                    printInfo("      Not satisfied. Cannot found %s.%d", scene->conditions[i].entityId, scene->conditions[i].dpId);
                }
            }
        }
    } else {
        int i = 0;
        printInfo("  sceneType = SceneTypeAllConds");
        for (i = 0; i < scene->conditionCount; i++) {
            if (scene->conditions[i].conditionType == EntitySchedule) {
                if (scene->conditions[i].timeReached == 0) {
                    break;
                }
            } else {
                printInfo("    conditions[%d]: %s.%d=%.0f", i, scene->conditions[i].entityId, scene->conditions[i].dpId, scene->conditions[i].dpValue);
                DpInfo dpInfo;
                int foundDps = Db_FindDp(&dpInfo, scene->conditions[i].entityId, scene->conditions[i].dpId);
                if (foundDps == 0 || dpInfo.value != scene->conditions[i].dpValue) {
                    if (foundDps == 0) {
                        printInfo("      Not satisfied. Cannot found %s.%d", scene->conditions[i].entityId, scene->conditions[i].dpId);
                    } else {
                        printInfo("      Not satisfied. Current value: %.0f", scene->conditions[i].entityId, scene->conditions[i].dpId, dpInfo.value);
                    }
                    break;
                }
            }
        }
        if (i == scene->conditionCount) {
            printInfo("    Conditions of this scene are satisfied");
            sendSceneRunEventToUser(scene, 0);
            markSceneToRun(scene, NULL);
        }
    }
}


// Check if there are scenes needed to be executed if status of a device is changed
void checkSceneForDevice(const char* deviceId, int dpId, double dpValue, const char* dpValueStr, bool syncRelatedDevices) {
    printInfo("checkSceneForDevice: %s.%d", deviceId, dpId);
    // Find all scenes that this device is in conditions
    for (int i = 0; i < g_sceneCount; i++) {
        Scene* scene = &g_sceneList[i];
        for (int c = 0; c < scene->conditionCount; c++) {
            // printInfo("  scene->conditions[%d].entity = %s.%d\n", c, scene->conditions[c].entityId, scene->conditions[c].dpId);
            if (scene->conditions[c].conditionType == EntityDevice &&
                (StringCompare(scene->conditions[c].entityId, deviceId)) &&
                (scene->conditions[c].dpId == dpId)) {
                if (scene->isLocal == false) {
                    printInfo("  Found scene %s contains this element in conditions", scene->id);
                    if ((scene->conditions[c].valueType == ValueTypeDouble && scene->conditions[c].dpValue == dpValue) ||
                        (scene->conditions[c].valueType == ValueTypeString && StringCompare(scene->conditions[c].dpValueStr, dpValueStr))) {
                        if (scene->type == SceneTypeOneOfConds) {
                            if (checkScenePrecondition(scene)) {
                                printInfo("      Satisfied. current value=%.0f, expect=%.0f", dpValue, scene->conditions[c].dpValue);
                                sendSceneRunEventToUser(scene, c);
                                markSceneToRun(scene, NULL);
                                break;
                            }
                        } else {
                            checkSceneCondition(scene);
                        }
                    } else {
                        printInfo("  Not satisfied. current value=%.0f, expect=%.0f", dpValue, scene->conditions[c].dpValue);
                    }
                } else if (syncRelatedDevices && scene->isEnable) {
                    DpInfo dpInfo;
                    int foundDps = Db_FindDp(&dpInfo, scene->conditions[c].entityId, scene->conditions[c].dpId);
                    if (foundDps == 1 && scene->conditions[c].valueType == ValueTypeDouble) {
                        if(dpInfo.value == scene->conditions[c].dpValue) {
                            // Save history for this local scene
                            JSON* history = JSON_CreateObject();
                            JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_DEVICE);
                            JSON_SetNumber(history, "eventType", EV_RUN_SCENE);
                            JSON_SetText(history, "deviceId", scene->id);
                            Db_AddDeviceHistory(history);
                            JSON_Delete(history);
                            GetDeviceStatusForScene(scene);

                            // Update status of devices in this scene to AWS
                            for (int act = 0; act < scene->actionCount; act++) {
                                if (scene->actions[act].actionType == EntityDevice) {
                                    DeviceInfo deviceInfo;
                                    int foundDevices = Db_FindDevice(&deviceInfo, scene->actions[act].entityId);
                                    if (foundDevices == 1) {
                                        if (deviceInfo.state == STATE_ONLINE) {
                                            for (int dp = 0; dp < scene->actions[act].dpCount; dp++) {
                                                if (scene->actions[act].dpIds[dp] != 21 && scene->actions[act].dpIds[dp] != 106) {
                                                    logInfo("Updating value for device %s.%d by scene %s", deviceInfo.id, scene->actions[act].dpIds[dp], scene->id);
                                                    Aws_SaveDpValue(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp], deviceInfo.pageIndex);
                                                    Db_SaveDpValue(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp]);
                                                    checkSceneForDevice(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp], NULL, true);
                                                    JSON* history = JSON_CreateObject();
                                                    JSON_SetNumber(history, "eventType", EV_DEVICE_DP_CHANGED);
                                                    JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_SCENE);
                                                    JSON_SetText(history, "causeId", scene->id);
                                                    JSON_SetText(history, "deviceId", deviceInfo.id);
                                                    JSON_SetNumber(history, "dpId", scene->actions[act].dpIds[dp]);
                                                    JSON_SetNumber(history, "dpValue", scene->actions[act].dpValues[dp]);
                                                    Db_AddDeviceHistory(history);
                                                    JSON_Delete(history);
                                                }
                                            }
                                        } else {
                                            logInfo("Device %s is offline");
                                        }
                                    }
                                }
                            }
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
            if (runningAction->actionType == EntityDevice || runningAction->actionType == EntityGroup) {
                printInfo("  Executing action %d of scene %s. type: %d", scene->runningActionIndex, scene->id, runningAction->actionType);
                // Action type is "control a device"
                if (StringCompare(runningAction->serviceName, SERVICE_BLE)) {
                    if (runningAction->actionType == EntityDevice) {
                        printInfo("    Action is control a BLE device: %s. dpCount=%d", runningAction->entityId, runningAction->dpCount);
                        if (runningAction->valueType == ValueTypeDouble) {
                            Ble_ControlDeviceArray(runningAction->entityId, runningAction->dpIds, runningAction->dpValues, runningAction->dpCount, scene->id);
                        } else {
                            Ble_ControlDeviceStringDp(runningAction->entityId, runningAction->dpIds[0], runningAction->valueString, scene->id);
                        }
                    } else {
                        printInfo("    Action is control a BLE group: %s. dpCount=%d", runningAction->entityId, runningAction->dpCount);
                        if (runningAction->valueType == ValueTypeDouble) {
                            Ble_ControlGroupArray(runningAction->entityId, runningAction->dpIds, runningAction->dpValues, runningAction->dpCount, scene->id);
                        } else {
                            Ble_ControlGroupStringDp(runningAction->entityId, runningAction->dpIds[0], runningAction->valueString, scene->id);
                        }
                    }
                } else {
                    if (runningAction->actionType == EntityDevice) {
                        printInfo("    Action is control a wifi device: id: %s, code: %s", runningAction->entityId, runningAction->wifiCode);
                        Wifi_ControlDevice(runningAction->entityId, runningAction->wifiCode);
                    } else {
                        printInfo("    Action is control a wifi group: id: %s, code: %s", runningAction->entityId, runningAction->wifiCode);
                        Wifi_ControlGroup(runningAction->entityId, runningAction->wifiCode);
                    }
                }
                // Move to next action
                scene->delayStart = timeInMilliseconds();
                scene->runningActionIndex++;
                if (scene->runningActionIndex >= scene->actionCount) {
                    scene->runningActionIndex = -1;     // No other actions need to execute, move scene to idle state
                }
            } else if (runningAction->actionType == EntityScene) {
                printInfo("  Executing action %d of scene %s. type: %d", scene->runningActionIndex, scene->id, runningAction->actionType);
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
                                    Aws_EnableScene(runningAction->entityId, 0);
                                    g_sceneList[s].isEnable = 0;
                                } else if (runningAction->dpValues[0] == 1) {
                                    // Enable scene
                                    Db_EnableScene(runningAction->entityId, 1);
                                    Aws_EnableScene(runningAction->entityId, 1);
                                    g_sceneList[s].isEnable = 1;
                                } else {
                                    markSceneToRun(&g_sceneList[s], g_sceneList[s].id);
                                }
                            } else {
                                // BLE scene: Send request to BLE service
                                JSON* packet = JSON_CreateObject();
                                JSON_SetText(packet, "sceneId", runningAction->entityId);
                                JSON_SetNumber(packet, "state", runningAction->dpValues[0]);
                                if (runningAction->dpValues[0] == 2 && g_sceneList[s].conditionCount > 0) {
                                    // this action is run a BLE scene, we need to send the device of condition of this scene
                                    // to BLE service
                                    JSON_SetText(packet, "pid", g_sceneList[s].conditions[0].pid);
                                    JSON_SetText(packet, "dpAddr", g_sceneList[s].conditions[0].dpAddr);
                                    JSON_SetNumber(packet, "dpValue", g_sceneList[s].conditions[0].dpValue);
                                }
                                sendPacketToBle(-1, TYPE_CTR_SCENE, packet);
                                if (runningAction->dpValues[0] == 0) {
                                    // Disable scene
                                    Db_EnableScene(runningAction->entityId, 0);
                                    Aws_EnableScene(runningAction->entityId, 0);
                                    g_sceneList[s].isEnable = 0;
                                } else if (runningAction->dpValues[0] == 1) {
                                    // Enable scene
                                    Db_EnableScene(runningAction->entityId, 1);
                                    Aws_EnableScene(runningAction->entityId, 1);
                                    g_sceneList[s].isEnable = 1;
                                }
                                JSON_Delete(packet);
                            }
                            break;
                        }
                    }
                } else {
                    // Wifi scene: send request to WIFI service
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "sceneId", runningAction->entityId);
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
            for (int c = 0; c < scene->conditionCount; c++) {
                SceneCondition* cond = &scene->conditions[c];
                if (cond->conditionType == EntitySchedule) {
                    // Get current date time
                    time_t rawtime; struct tm *info; time( &rawtime ); info = localtime(&rawtime);
                    int todayMinutes = info->tm_hour * 60 + info->tm_min;
                    if (cond->repeat == 0) {
                        // Execute scene only 1 time
                        time_t currentEpochTime = time(NULL);
                        if (currentEpochTime - cond->schMinutes >= 0 && currentEpochTime - cond->schMinutes < 60) {
                            cond->timeReached = 1;
                            if (scene->type == SceneTypeOneOfConds) {
                                if (checkScenePrecondition(scene)) {
                                    printInfo("      Satisfied. currentEpoch=%lu, schMinutes=%d", currentEpochTime, scene->conditions[c].schMinutes);
                                    sendSceneRunEventToUser(scene, c);
                                    markSceneToRun(scene, NULL);
                                }
                            } else {
                                checkSceneCondition(scene);     // Check other conditions of this scene
                            }
                            scene->conditions[c].schMinutes = 3000000000;
                            cond->timeReached = 0;
                            Db_SaveSceneCondDate(scene->id, c, "30000101");     // Increase date to maximum date to disable any executing later
                            break;
                        }
                    } else if ((0x40 >> info->tm_wday) & cond->repeat) {
                        if (cond->timeReached == 0 && todayMinutes == cond->schMinutes) {
                            cond->timeReached = 1;
                            checkSceneCondition(scene);     // Check other conditions of this scene
                            if (todayMinutes > cond->schMinutes) {
                                cond->timeReached = 0;
                            }
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
                JSON* item = JArr_CreateObject(devicesArray);
                JSON_SetText(item, "addr", deviceInfo.addr);
                JSON_SetText(item, "hcAddr", deviceInfo.hcAddr);
                JSON_SetNumber(item, "gwIndex", deviceInfo.gwIndex);
            }
        }
    }
    if (JArr_Count(devicesArray) > 0) {
        // sendPacketToBle(-1, TYPE_GET_ONOFF_STATE, devicesArray);
    }
    JSON_Delete(devicesArray);
}

/*
 * Tìm tất cả các nhóm mà 1 hạt công tắc nằm trong đó, sau đó gửi lệnh để get trạng thái mới nhất của tất
 * cả thiết bị trong nhóm đó. Hàm này được gọi khi có 1 hạt công tắc thay đổi trạng thái được gửi về từ thiết bị
 */
void GetDeviceStatusForGroup(const char* deviceId, int dpId, double dpValue) {
    ASSERT(deviceId);
    logInfo("Process group link for device: %s.%d = %.0f", deviceId, dpId, dpValue);
    JSON* foundGroups = cJSON_CreateArray();
    JSON* devicesArray = cJSON_CreateArray();
    char sqlCommand[200];
    // Find all groups that contain this deviceId and dpId
    sprintf(sqlCommand, "SELECT groupAdress, name, devices FROM group_inf WHERE isLight = 0");
    Sql_Query(sqlCommand, row) {
        char* groupAddr = sqlite3_column_text(row, 0);
        char* groupName = sqlite3_column_text(row, 1);
        char* devicesStr = sqlite3_column_text(row, 2);
        JSON* devices = JSON_Parse(devicesStr);
        JSON_ForEach(d, devices) {
            if (StringCompare(JSON_GetText(d, "deviceId"), deviceId) && JSON_GetNumber(d, "dpId") == dpId) {
                logInfo("found Group: (%s, %s) %s", groupAddr, groupName, devicesStr);
                JArr_AddObject(foundGroups, JSON_Clone(devices));
                // Save history for group link
                JSON* history = JSON_CreateObject();
                JSON_SetNumber(history, "eventType", EV_DEVICE_DP_CHANGED);
                JSON_SetNumber(history, "causeType", 0);
                JSON_SetText(history, "causeId", deviceId);
                JSON_SetText(history, "deviceId", groupAddr);
                JSON_SetNumber(history, "dpId", 0);
                JSON_SetNumber(history, "dpValue", dpValue);
                Db_AddDeviceHistory(history);
                JSON_Delete(history);
            }
        }
        JSON_Delete(devices);
    }

    // Process for all devices that linked to this element
    JSON_ForEach(g, foundGroups) {
        JSON_ForEach(d, g) {
            char* dId = JSON_GetText(d, "deviceId");
            int dp = JSON_GetNumber(d, "dpId");
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, dId);
            if (foundDevices == 1) {
                Db_SaveDpValue(dId, dp, dpValue);
                Aws_SaveDpValue(dId, dp, dpValue, deviceInfo.pageIndex);
                checkSceneForDevice(dId, dp, dpValue, NULL, true);

                if (!JArr_FindByText(devicesArray, NULL, deviceInfo.addr)) {
                    JSON* item = JArr_CreateObject(devicesArray);
                    JSON_SetText(item, "addr", deviceInfo.addr);
                    JSON_SetText(item, "hcAddr", deviceInfo.hcAddr);
                    JSON_SetNumber(item, "gwIndex", deviceInfo.gwIndex);
                }
            }
        }
    }
    JSON_Delete(foundGroups);
    if (JArr_Count(devicesArray) > 0) {
        // sendPacketToBle(-1, TYPE_GET_ONOFF_STATE, devicesArray);
    }
    JSON_Delete(devicesArray);
}

void CheckDeviceOffline() {
    static long long int oldTick = 0;

    if (GWCFG_GET_ONLINE_TIME >= 10000 && timeInMilliseconds() - oldTick > 10000) {
        oldTick = timeInMilliseconds();
        char sqlCmd[500];
        char pid[1000];
        List* offlineDevices = List_Create();
        sprintf(pid, "%s,%s,%s,%s", HG_BLE_SWITCH, BLE_LIGHT, HG_BLE_CURTAIN, HG_BLE_IR);
        sprintf(sqlCmd, "SELECT deviceId, pageIndex FROM devices_inf d \
                        WHERE (instr('%s', pid) > 0) AND (d.last_updated IS NOT NULL AND %lld - d.last_updated > %d) AND d.deviceKey IS NOT NULL", pid, oldTick, GWCFG_GET_ONLINE_TIME * 4);
        Sql_Query(sqlCmd, row) {
            char* deviceId = sqlite3_column_text(row, 0);
            int pageIndex = sqlite3_column_int(row, 1);
            Db_SaveDeviceState(deviceId, STATE_OFFLINE);
            Aws_SaveDeviceState(deviceId, STATE_OFFLINE, pageIndex);
            List_PushString(offlineDevices, deviceId);
        }

        if (offlineDevices->count > 0) {
            char offlineDeviceStr[10000];
            List_ToString(offlineDevices, ",", offlineDeviceStr);
            logInfo("There are %d offline devices: %s", offlineDevices->count, offlineDeviceStr);
        }

        List_Delete(offlineDevices);
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
    if (!check_flag) {
        logError("Cannot open database");
    }
    // Delete hisroties older than 30 days
    Sql_Exec("DELETE FROM device_histories WHERE DATE(ROUND(time / 1000), 'unixepoch') <= DATE('now','-30 day');");

    Mosq_Init();
    Db_LoadSceneToRam();
    sleep(1);
    printInfo("Current epoch time: %d", time(NULL));

    while(xRun!=0) {
        Mosq_ProcessLoop();
        ResponseWaiting();
        ExecuteScene();
        CheckDeviceOffline();

        size_queue = get_sizeQueue(queue_received);
        if (size_queue > 0) {
            int reponse = 0;int leng =  0,number = 0, size_ = 0,i=0;
            char* recvMsg = (char *)dequeue(queue_received);

            JSON_Object *object_tmp = NULL;
            JSON_Array *actions_array_json = NULL;
            JSON_Array *condition_array_json = NULL;
            JSON* recvPacket = JSON_Parse(recvMsg);
            const char *object_string   = JSON_GetText(recvPacket, MOSQ_Payload);
            JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));
            char* NameService = JSON_GetText(recvPacket, "NameService");
            int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
            if (reqType != GW_RESP_ONOFF_STATE) {
                printInfo("\n\r");
                logInfo("Received message: %s", recvMsg);
            } else {
                uint16_t opcode = JSON_HasKey(payload, "opcode")? JSON_GetNumber(payload, "opcode") : 0;
                // if (opcode != 0x8201) {
                    printInfo("\n\r");
                    logInfo("Received message: %s", recvMsg);
                // }
            }
            if (payload == NULL) {
                logError("Payload is NULL");
            }
            if (StringCompare(NameService, SERVICE_BLE) && payload) {
                switch (reqType) {
                    case GW_RESPONSE_LIGHT_RD_CONTROL: {
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");

                        for (int dpId = 22; dpId <= 23; dpId++) {
                            uint16_t value = 0xFFFF;
                            if (dpId == 22 && JSON_HasKey(payload, "lightness")) {
                                value = JSON_GetNumber(payload, "lightness");
                            }
                            if (dpId == 23 && JSON_HasKey(payload, "color")) {
                                value = JSON_GetNumber(payload, "color");
                            }

                            if (value != 0xFFFF) {
                                DpInfo dpInfo;
                                DeviceInfo deviceInfo;
                                int foundDps = Db_FindDpByAddrAndDpId(&dpInfo, deviceAddr, dpId);
                                if (foundDps == 1) {
                                    int foundDevices = Db_FindDevice(&deviceInfo, dpInfo.deviceId);
                                    if (foundDevices == 1) {
                                        Db_SaveDpValue(dpInfo.deviceId, dpId, value);
                                        Db_SaveDeviceState(dpInfo.deviceId, STATE_ONLINE);
                                        Aws_SaveDpValue(dpInfo.deviceId, dpId, value, dpInfo.pageIndex);
                                    }
                                }
                            }
                        }
                        break;
                    }
                    case GW_RESPONSE_RGB_COLOR: {
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        char* hcAddr = JSON_GetText(payload, "hcAddr");
                        char* color = JSON_GetText(payload, "color");
                        int dpId = 24;
                        DpInfo dpInfo;
                        DeviceInfo deviceInfo;
                        int foundDps = Db_FindDpByAddrAndDpId(&dpInfo, deviceAddr, dpId);
                        if (foundDps == 1) {
                            int foundDevices = Db_FindDevice(&deviceInfo, dpInfo.deviceId);
                            if (foundDevices == 1) {
                                Db_SaveDpValueString(dpInfo.deviceId, dpId, color);
                                Db_SaveDeviceState(dpInfo.deviceId, STATE_ONLINE);
                                Aws_SaveDpValueString(dpInfo.deviceId, dpId, color, dpInfo.pageIndex);
                            }
                        }
                        break;
                    }
                    case GW_RESP_ONOFF_STATE: {
                        char* hcAddr = JSON_GetText(payload, "hcAddr");
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        double dpValue = JSON_GetNumber(payload, "dpValue");
                        uint16_t opcode = JSON_HasKey(payload, "opcode")? JSON_GetNumber(payload, "opcode") : 0;
                        DpInfo dpInfo;
                        DeviceInfo deviceInfo;
                        int foundDps = Db_FindDpByAddr(&dpInfo, dpAddr, hcAddr);
                        if (foundDps == 1) {
                            double oldDpValue = dpInfo.value;
                            int foundDevices = Db_FindDevice(&deviceInfo, dpInfo.deviceId);
                            if (foundDevices == 1) {
                                if (StringContains(HG_BLE_IR_FULL, deviceInfo.pid)) {
                                    SetOnlineStateForIRDevices(deviceInfo.addr, STATE_ONLINE, hcAddr, false);
                                }
                                if (opcode != 0x8201 || oldDpValue != dpValue || deviceInfo.state == STATE_OFFLINE) {
                                    Aws_SaveDpValue(dpInfo.deviceId, dpInfo.id, dpValue, dpInfo.pageIndex);
                                }
                                Db_SaveDpValue(dpInfo.deviceId, dpInfo.id, dpValue);
                                Db_SaveDeviceState(dpInfo.deviceId, STATE_ONLINE);
                                Db_SaveOfflineCountForDevice(dpInfo.deviceId, 0);
                                JSON_SetText(payload, "deviceId", dpInfo.deviceId);
                                JSON_SetNumber(payload, "dpId", dpInfo.id);
                                JSON_SetNumber(payload, "eventType", EV_DEVICE_DP_CHANGED);
                                JSON_SetNumber(payload, "pageIndex", dpInfo.pageIndex);
                                if (opcode == 0x8201) {
                                    // The response is from getting status of devices actively
                                    if (oldDpValue != dpValue) {
                                        JSON_SetNumber(payload, "causeType", EV_CAUSE_TYPE_SYNC);
                                        Db_AddDeviceHistory(payload);
                                    }
                                } else {
                                    // Check and run scenes for this device if any
                                    checkSceneForDevice(dpInfo.deviceId, dpInfo.id, dpValue, NULL, true);
                                    if (opcode == 0) {
                                        JSON_SetNumber(payload, "causeType", EV_CAUSE_TYPE_DEVICE);
                                        Db_AddDeviceHistory(payload);
                                    }
                                    GetDeviceStatusForGroup(dpInfo.deviceId, dpInfo.id, dpValue);
                                    if (opcode == 0x8202) {
                                        updateDeviceRespStatus(TYPE_CTR_DEVICE, dpInfo.deviceId, dpInfo.deviceId, 0);
                                    }
                                }
                            } else {
                                logError("Cannot find deviceId %s", dpInfo.deviceId);
                            }
                        } else {
                            logError("Cannot find dpAddr %s on hcAddr %s", dpAddr, hcAddr);
                        }
                        break;
                    }
                    case GW_RESP_ONLINE_STATE: {
                        char* hcAddr = JSON_GetText(payload, "hcAddr");
                        JSON* devicesArray = JSON_GetObject(payload, "devices");
                        JSON_ForEach(arrayItem, devicesArray) {
                            char* deviceAddr = JSON_GetText(arrayItem, "deviceAddr");
                            int deviceState = JSON_GetNumber(arrayItem, "deviceState");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDeviceByAddr(&deviceInfo, deviceAddr, hcAddr);
                            if (foundDevices == 1) {
                                if (StringContains(HG_BLE_IR_FULL, deviceInfo.pid)) {
                                    SetOnlineStateForIRDevices(deviceAddr, deviceState, hcAddr, true);
                                } else {
                                    if (deviceState == STATE_OFFLINE) {
                                        if (deviceInfo.offlineCount < 2) {
                                            Db_SaveOfflineCountForDevice(deviceInfo.id, deviceInfo.offlineCount + 1);
                                            break;
                                        }
                                    }
                                    JSON_SetText(payload, "deviceId", deviceInfo.id);
                                    Db_SaveOfflineCountForDevice(deviceInfo.id, 0);
                                    Db_SaveDeviceState(deviceInfo.id, deviceState);
                                    Aws_SaveDeviceState(deviceInfo.id, deviceState, deviceInfo.pageIndex);
                                    JSON_SetNumber(payload, "dpValue", deviceState == 2? 1 : 0);
                                    JSON_SetNumber(payload, "eventType", EV_DEVICE_STATE_CHANGED);
                                    Db_AddDeviceHistory(payload);
                                }
                            }
                        }
                        break;
                    }
                    case GW_RESPONSE_IR: {
                        uint8_t respType = JSON_GetNumber(payload, "respType");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        if (respType == 0) {
                            uint16_t brandId = JSON_GetNumber(payload, "brandId");
                            uint8_t  remoteId = JSON_GetNumber(payload, "remoteId");
                            uint8_t  temp = JSON_GetNumber(payload, "temp");
                            uint8_t  mode = JSON_GetNumber(payload, "mode");
                            uint8_t  fan = JSON_GetNumber(payload, "fan");
                            uint8_t  swing = JSON_GetNumber(payload, "swing");
                            // Find device with brandId and remoteId
                            char sql[500];
                            sprintf(sql, "select * from devices where address='%s' AND dpId=1 AND CAST(dpValue as INTEGER)=%d", deviceAddr, brandId);
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
                        } else if (respType == 1) {
                            uint16_t voiceId = JSON_GetNumber(payload, "voiceId");
                            char sql[500];
                            sprintf(sql, "select d.deviceId from devices d JOIN devices_inf di ON d.deviceId=di.deviceId where address='%s' AND dpId=1 AND pid='%s'", deviceAddr, HG_BLE_IR);
                            Sql_Query(sql, row) {
                                char* deviceId = sqlite3_column_text(row, 0);
                                Db_SaveDpValue(deviceId, 1, voiceId);
                                JSON* history = JSON_CreateObject();
                                JSON_SetNumber(history, "eventType", EV_DEVICE_DP_CHANGED);
                                JSON_SetNumber(history, "causeType", 0);
                                JSON_SetText(history, "deviceId", deviceId);
                                JSON_SetNumber(history, "dpId", 1);
                                JSON_SetNumber(history, "dpValue", voiceId);
                                Db_AddDeviceHistory(history);
                                JSON_Delete(history);
                                checkSceneForDevice(deviceId, 1, voiceId, NULL, true);
                                // Clear voiceId in database to prevent executing scene later
                                Db_SaveDpValue(deviceId, 1, 0);
                            }
                        } else {
                            // Command learning response
                            char* respCmd = JSON_GetText(payload, "respCmd");
                            char sql[500];
                            sprintf(sql, "SELECT deviceId, pid FROM devices_inf WHERE unicast='%s'", deviceAddr, HG_BLE_IR);
                            Sql_Query(sql, row) {
                                char* deviceId = sqlite3_column_text(row, 0);
                                char* pid = sqlite3_column_text(row, 1);
                                if (!StringCompare(pid, HG_BLE_IR_AC) && !StringCompare(pid, HG_BLE_IR_TV) && !StringCompare(pid, HG_BLE_IR_FAN) && !StringCompare(pid, HG_BLE_IR_REMOTE)) {
                                    Aws_ResponseLearningIR(deviceId, respCmd);
                                }
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
                        char* hcAddr = JSON_GetText(payload, "hcAddr");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        int dpId = JSON_GetNumber(payload, "dpId");
                        int dpValue = JSON_GetNumber(payload, "dpValue");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDeviceByAddr(&deviceInfo, deviceAddr, hcAddr);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceId", deviceInfo.id);
                            Db_SaveDpValue(deviceInfo.id, dpId, dpValue);
                            Aws_SaveDpValue(deviceInfo.id, dpId, dpValue, deviceInfo.pageIndex);
                            JSON_SetNumber(payload, "causeType", 0);
                            JSON_SetText(payload, "causeId", "");
                            JSON_SetNumber(payload, "eventType", EV_DEVICE_DP_CHANGED);
                            Db_AddDeviceHistory(payload);
                            checkSceneForDevice(deviceInfo.id, dpId, dpValue, NULL, true);     // Check and run scenes for this device if any
                        }

                        break;
                    }
                    case GW_RESPONSE_GROUP:
                    {
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        int status = JSON_GetNumber(payload, "status");
                        if (requestIsInRespList(TYPE_ADD_GROUP_LIGHT, groupAddr)) {
                            updateDeviceRespStatus(TYPE_ADD_GROUP_LIGHT, groupAddr, deviceAddr, status);
                        } else if (requestIsInRespList(TYPE_UPDATE_GROUP_LIGHT, groupAddr)) {
                            updateDeviceRespStatus(TYPE_UPDATE_GROUP_LIGHT, groupAddr, deviceAddr, status);
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
                        logInfo("GW_RESPONSE_DEVICE_KICKOUT");
                        char* hcAddr = JSON_GetText(payload, "hcAddr");
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDeviceByAddr(&deviceInfo, deviceAddr, hcAddr);
                        if (foundDevices == 1) {
                            if (StringContains(HG_BLE_IR_FULL, deviceInfo.pid)) {
                                // Remove TV, AC, FAN, Remote
                                char sqlCmd[200];
                                sprintf(sqlCmd, "SELECT * FROM devices_inf d JOIN gateway g ON g.id = d.gwIndex WHERE Unicast = '%s' AND g.hcAddr = '%s';", deviceAddr, hcAddr);
                                Sql_Query(sqlCmd, row) {
                                    char* deviceId = sqlite3_column_text(row, 0);
                                    int pageIndex = sqlite3_column_int(row, 15);
                                    Aws_DeleteDevice(deviceId, pageIndex);
                                    DeleteDeviceFromScenes(deviceId);
                                }
                                sprintf(sqlCmd, "DELETE FROM devices_inf WHERE unicast = '%s'", deviceAddr);
                                Sql_Exec(sqlCmd);
                                sprintf(sqlCmd, "DELETE FROM devices WHERE address = '%s'", deviceAddr);
                                Sql_Exec(sqlCmd);
                            } else {
                                DeleteDeviceFromGroups(deviceInfo.id);
                                DeleteDeviceFromScenes(deviceInfo.id);
                                Aws_DeleteDevice(deviceInfo.id, deviceInfo.pageIndex);
                                Db_DeleteDevice(deviceInfo.id);
                            }
                        } else {
                            logError("Device %s in hc %s is not found", deviceAddr, hcAddr);
                        }
                        break;
                    }
                    case GW_RESPONSE_SET_TTL: {
                        char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                        updateDeviceRespStatus(GW_RESPONSE_SET_TTL, "0", deviceAddr, 0);
                        break;
                    }
                    case GW_RESPONSE_LOCK_KIDS: {
                        char* hcAddr = JSON_GetText(payload, "hcAddr");
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        int lockValue = JSON_GetNumber(payload, "lockValue");
                        DpInfo dpInfo;
                        int foundDps = Db_FindDpByAddr(&dpInfo, dpAddr, hcAddr);
                        if (foundDps == 1) {
                            Aws_UpdateLockKids(dpInfo.deviceId, dpInfo.id, lockValue);
                        }
                        break;
                    }
                    case TYPE_SYNC_DEVICE_STATE: {
                        SyncDevicesState();
                        break;
                    }
                }
            } else if ((StringCompare(NameService, SERVICE_AWS) || StringCompare(NameService, SERVICE_CORE)) && payload) {
                switch (reqType) {
                    case TYPE_GET_ALL_DEVICES: {
                        JSON* p = JSON_CreateObject();
                        JSON_SetNumber(p, "type", TYPE_GET_ALL_DEVICES);
                        JSON_SetNumber(p, "sender", SENDER_HC_TO_CLOUD);
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
                        // JSON* addedDevice = addDeviceToRespList(reqType, deviceId, deviceId);
                        // if (addedDevice) {
                        //     JSON_SetText(addedDevice, "causeId", senderId);
                        // }
                        break;
                    }
                    case TYPE_CTR_GROUP_NORMAL: {
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        char* senderId = JSON_GetText(payload, "senderId");
                        JSON* dictDPs = JSON_GetObject(payload, "dictDPs");
                        Ble_ControlGroupJSON(groupAddr, dictDPs, senderId);
                        break;
                    }
                    case TYPE_CTR_SCENE: {
                        char* senderId = JSON_GetText(payload, "senderId");
                        char* sceneId = JSON_GetText(payload, "id");
                        int state = JSON_GetNumber(payload, "state");
                        if (sceneId) {
                            // Check if this scene is HC or local
                            bool isLocal = true;
                            int sceneType = 0;
                            Scene* scene = NULL;
                            for (int i = 0; i < g_sceneCount; i++) {
                                if (StringCompare(g_sceneList[i].id, sceneId)) {
                                    scene = &g_sceneList[i];
                                    isLocal = g_sceneList[i].isLocal;
                                    sceneType = g_sceneList[i].type;
                                    break;
                                }
                            }
                            if (scene != NULL) {
                                if (sceneType != SceneTypeManual) {
                                    if (state)  { logInfo("Enabling scene %s", sceneId); }
                                    else        { logInfo("Disabling scene %s", sceneId); }
                                    Db_EnableScene(sceneId, state);
                                    Aws_EnableScene(sceneId, state);
                                    scene->isEnable = state;
                                } else {
                                    if (!isLocal) {
                                        logInfo("Executing HC scene %s", sceneId);
                                        sendSceneRunEventToUser(scene, 0);
                                        markSceneToRun(scene, senderId);
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
                                    sendPacketToBle(-1, reqType, p);
                                    if (sceneType == SceneTypeManual) {
                                        // Update status of devices in this scene to AWS
                                        for (int act = 0; act < scene->actionCount; act++) {
                                            if (scene->actions[act].actionType == EntityDevice) {
                                                DeviceInfo deviceInfo;
                                                int foundDevices = Db_FindDevice(&deviceInfo, scene->actions[act].entityId);
                                                if (foundDevices == 1) {
                                                    for (int dp = 0; dp < scene->actions[act].dpCount; dp++) {
                                                        Aws_SaveDpValue(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp], deviceInfo.pageIndex);
                                                        Db_SaveDpValue(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp]);
                                                        // Save history for group link
                                                        JSON* history = JSON_CreateObject();
                                                        JSON_SetNumber(history, "eventType", EV_DEVICE_DP_CHANGED);
                                                        JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_SCENE);
                                                        JSON_SetText(history, "causeId", sceneId);
                                                        JSON_SetText(history, "deviceId", deviceInfo.id);
                                                        JSON_SetNumber(history, "dpId", scene->actions[act].dpIds[dp]);
                                                        JSON_SetNumber(history, "dpValue", scene->actions[act].dpValues[dp]);
                                                        Db_AddDeviceHistory(history);
                                                        JSON_Delete(history);

                                                        checkSceneForDevice(deviceInfo.id, scene->actions[act].dpIds[dp], scene->actions[act].dpValues[dp], NULL, true);
                                                    }
                                                }
                                            }
                                        }
                                        GetDeviceStatusForScene(scene);
                                    }
                                    JSON_Delete(p);
                                }
                            } else {
                                logError("Scene %s is not found", sceneId);
                            }
                        }
                        break;
                    }
                    case TYPE_ADD_DEVICE: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        // Delete device from database if exist
                        logInfo("Delete device %s from database if exist", deviceId);
                        Db_DeleteDevice(deviceId);
                        logInfo("Adding device: %s", deviceId);
                        // Insert device to database
                        if (addNewDevice(payload) == false) {
                            PlayAudio("add_device_error");
                            break;
                        }
                        int provider = JSON_GetNumber(payload, "provider");
                        if (provider == HOMEGY_BLE) {
                            char* gatewayAddr = JSON_GetText(payload, "gateWay");
                            int gatewayId = Db_FindGatewayId(gatewayAddr);
                            if (gatewayId >= 0) {
                                // Send packet to BLE to save device information in to gateway
                                JSON* dictDPs = JSON_GetObject(payload, "dictDPs");
                                if (JSON_HasKey(dictDPs, "106")) {
                                    JSON_SetText(payload, "command", JSON_GetText(dictDPs, "106"));
                                }
                                JSON_SetNumber(payload, "gatewayId", gatewayId);
                                sendPacketToBle(gatewayId, TYPE_ADD_DEVICE, payload);
                            } else {
                                logError("Gateway %s is not found", gatewayAddr);
                            }
                        }

                        JSON* history = JSON_CreateObject();
                        JSON_SetNumber(history, "eventType", EV_DEVICE_ADDED);
                        JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_APP);
                        JSON_SetText(history, "causeId", JSON_GetText(payload, "senderId"));
                        JSON_SetText(history, "deviceId", deviceId);
                        Db_AddDeviceHistory(history);
                        JSON_Delete(history);
                        break;
                    }
                    case TYPE_DEL_DEVICE: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetNumber(payload, "gwIndex", deviceInfo.gwIndex);
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            JSON_SetText(payload, "devicePid", deviceInfo.pid);
                            if (StringCompare(deviceInfo.pid, HG_BLE_IR_AC) ||
                                StringCompare(deviceInfo.pid, HG_BLE_IR_TV) ||
                                StringCompare(deviceInfo.pid, HG_BLE_IR_FAN) ||
                                StringCompare(deviceInfo.pid, HG_BLE_IR_REMOTE)) {

                            } else {
                                sendPacketToBle(deviceInfo.gwIndex, TYPE_DEL_DEVICE, payload);
                            }
                            Db_DeleteDevice(deviceId);
                            DeleteDeviceFromGroups(deviceId);
                            DeleteDeviceFromScenes(deviceId);
                            JSON_SetNumber(payload, "eventType", EV_DEVICE_DELETED);
                            Db_AddDeviceHistory(payload);
                            logInfo("Delete deviceId: %s", deviceId);
                        } else {
                            logError("device %s is not found", deviceId);
                        }
                        break;
                    }
                    case TYPE_RESET_DATABASE: {
                        creat_table_database(&db);
                        break;
                    }
                    case TYPE_SYNC_DB_DEVICES: {
                        logInfo("TYPE_SYNC_DB_DEVICES");
                        int failedCount = 0;
                        Db_DeleteAllDevices();
                        // Add new devices from cloud
                        JSON_ForEach(d, payload) {
                            char* tmp = cJSON_PrintUnformatted(d);
                            printf("Adding device: %s\n", tmp);
                            free(tmp);
                            if (addNewDevice(d) == false) {
                                failedCount++;
                            }
                            int provider = JSON_GetNumber(d, "provider");
                            if (provider == HOMEGY_BLE) {
                                char* gatewayAddr = JSON_GetText(d, "gateWay");
                                int gatewayId = Db_FindGatewayId(gatewayAddr);
                                if (gatewayId >= 0) {
                                    // Send packet to BLE to save device information in to gateway
                                    JSON_SetNumber(d, "gatewayId", gatewayId);
                                    // printf("ok: %s\n", cJSON_PrintUnformatted(d));
                                    sendPacketToBle(gatewayId, TYPE_ADD_DEVICE, d);
                                    usleep(SAVE_DEVICE_KEY_DELAY_MS);
                                } else {
                                    logError("Gateway %s is not found", gatewayAddr);
                                }
                            }
                        }
                        if (failedCount == 0) {
                            int currentHour = get_hour_today();
                            if (currentHour >= 6 && currentHour < 21) {
                                PlayAudio("ready");
                            }
                        } else {
                            PlayAudio("sync_device_error");
                        }
                        break;
                    }
                    case TYPE_SYNC_DB_GROUPS: {
                        logInfo("TYPE_SYNC_DB_GROUPS");
                        Db_DeleteAllGroup();
                        int groupCount = 0;
                        int failedCount = 0;
                        JSON_ForEach(group, payload) {
                            char* groupAddr = JSON_GetText(group, "groupAddr");
                            char* groupName = JSON_GetText(group, "name");
                            char* devices = cJSON_PrintUnformatted(JSON_GetObject(group, "devices"));
                            char* pid = JSON_GetText(group, "pid");
                            int isLight = StringCompare(pid, "BLEHGAA0101")? 0 : 1;
                            int pageIndex = JSON_GetNumber(group, "pageIndex");
                            if (Db_AddGroup(groupAddr, groupName, devices, isLight, pid, pageIndex) == 0) {
                                failedCount++;
                            }
                            groupCount++;
                            free(devices);
                        }
                        logInfo("Added %d groups", groupCount);
                        if (failedCount > 0) {
                            PlayAudio("sync_group_error");
                        }
                        break;
                    }
                    case TYPE_SYNC_DB_SCENES: {
                        logInfo("TYPE_SYNC_DB_SCENES");
                        Db_DeleteAllScene();
                        int sceneCount = 0;
                        int failedCount = 0;
                        JSON_ForEach(scene, payload) {
                            if (Db_AddScene(scene) == 0) {
                                failedCount++;
                            }
                            sceneCount++;
                        }
                        logInfo("Added %d scenes", sceneCount);
                        if (failedCount > 0) {
                            PlayAudio("sync_scene_error");
                        }
                        break;
                    }
                    case TYPE_ADD_GW: {
                        Sql_Exec("DELETE FROM gateway");
                        JSON_ForEach(gw, payload) {
                            Db_AddGateway(gw);
                            if (JSON_HasKey(gw, "GWCFG_TIMEOUT_SCENEGROUP")) {
                                GWCFG_TIMEOUT_SCENEGROUP = JSON_GetNumber(gw, "GWCFG_TIMEOUT_SCENEGROUP");
                            }
                            if (JSON_HasKey(gw, "GWCFG_GET_ONLINE_TIME")) {
                                GWCFG_GET_ONLINE_TIME = JSON_GetNumber(gw, "GWCFG_GET_ONLINE_TIME");
                            }
                            sendPacketToBle(0, reqType, gw);
                        }
                        break;
                    }
                    case TYPE_ADD_SCENE: {
                        int isLocal = JSON_GetNumber(payload, "isLocal");
                        g_sceneTobeUpdated = JSON_Clone(payload);  // Save scene info so that we can save it to aws later
                        if (Db_AddScene(payload) == 0) {  // Insert scene into database
                            PlayAudio("add_scene_error");
                            break;
                        }
                        Scene_GetFullInfo(payload);
                        if (isLocal) {
                            sendPacketToBle(-1, TYPE_ADD_SCENE, payload);
                            // Add this request to response list for checking response
                            JSON* actions = JSON_GetObject(payload, "actions");
                            JSON_ForEach(action, actions) {
                                if (JSON_HasKey(action, "pid")) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* dpAddr = JSON_GetText(action, "entityAddr");
                                    int dpId = 0;
                                    if (JSON_HasKey(action, "dpAddr")) {
                                        dpAddr = JSON_GetText(action, "dpAddr");
                                        dpId = JSON_GetNumber(action, "dpId");
                                    }
                                    char* deviceId = JSON_GetText(action, "entityId");
                                    JSON* addedDevice = addDeviceToRespList(reqType, sceneId, dpAddr);
                                    if (addedDevice) {
                                        JSON_SetText(addedDevice, "deviceId", deviceId);
                                        JSON_SetText(addedDevice, "entityType", "action");
                                        JSON_SetNumber(addedDevice, "dpId", dpId);
                                    }
                                }
                            }
                            JSON* conditions = JSON_GetObject(payload, "conditions");
                            JSON_ForEach(condition, conditions) {
                                if (JSON_HasKey(condition, "pid")) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* dpAddr = JSON_GetText(condition, "entityAddr");
                                    int dpId = 0;
                                    if (JSON_HasKey(condition, "dpAddr")) {
                                        dpAddr = JSON_GetText(condition, "dpAddr");
                                        dpId = JSON_GetNumber(condition, "dpId");
                                    }
                                    char* deviceId = JSON_GetText(condition, "entityId");
                                    JSON* addedDevice = addDeviceToRespList(reqType, sceneId, dpAddr);
                                    if (addedDevice) {
                                        JSON_SetText(addedDevice, "deviceId", deviceId);
                                        JSON_SetText(addedDevice, "entityType", "condition");
                                        JSON_SetNumber(addedDevice, "dpId", dpId);
                                    }
                                }
                            }
                        }
                        break;
                    }
                    case TYPE_DEL_SCENE: {
                        char* sceneId = JSON_GetText(payload, "Id");
                        logInfo("TYPE_DEL_SCENE: %s", sceneId);
                        JSON* sceneInfo = Db_FindScene(sceneId);
                        if (sceneInfo) {
                            int isLocal = JSON_GetNumber(sceneInfo, "isLocal");
                            if (isLocal) {
                                Scene_GetFullInfo(sceneInfo);
                                sendPacketToBle(-1, TYPE_DEL_SCENE, sceneInfo);   // Send packet to BLE
                            } else {
                                logInfo("Deleted HC scene %s", sceneId);
                            }
                            Aws_DeleteScene(sceneId);
                            Db_DeleteScene(sceneId);    // Delete scene from database
                            DeleteDeviceFromScenes(sceneId);
                        } else {
                            logError("Scene %s not found", sceneId);
                        }
                        JSON_Delete(sceneInfo);
                        break;
                    }
                    case TYPE_UPDATE_SCENE: {
                        JSON* newScene = JSON_Clone(payload);
                        g_sceneTobeUpdated = JSON_Clone(payload);  // Save scene info so that we can save it to aws later
                        Scene_GetFullInfo(newScene);
                        char* sceneId = JSON_GetText(newScene, "Id");
                        logInfo("[TYPE_UPDATE_SCENE]: sceneId=%s", sceneId);
                        printInfo("Parsed scene: %s", cJSON_PrintUnformatted(newScene));
                        JSON* oldScene = Db_FindScene(sceneId);
                        if (oldScene) {
                            int isLocal = JSON_GetNumber(oldScene, "isLocal");
                            if (isLocal) {
                                JSON* newActionsArray = JSON_GetObject(newScene, "actions");
                                // Find all actions that are need to be removed and added
                                JSON* actionsNeedRemove = JSON_CreateArray();
                                JSON* actionsNeedAdd = JSON_CreateArray();
                                JSON_ForEach(newAction, newActionsArray) {
                                    int state = JSON_GetNumber(newAction, "state");
                                    if (state == -3) {
                                        cJSON_AddItemReferenceToArray(actionsNeedRemove, newAction);
                                    } else if (state == -2) {
                                        cJSON_AddItemReferenceToArray(actionsNeedAdd, newAction);
                                    }
                                }

                                // Find all conditions that are need to be removed and added
                                JSON* newConditionsArray = JSON_GetObject(newScene, "conditions");
                                JSON* conditionsNeedRemove = JSON_CreateArray();
                                JSON* conditionsNeedAdd = JSON_CreateArray();
                                JSON_ForEach(newCondition, newConditionsArray) {
                                    int state = JSON_GetNumber(newCondition, "state");
                                    if (state == -3) {
                                        JArr_AddObject(conditionsNeedRemove, JSON_Clone(newCondition));
                                    } else if (state == -2) {
                                        JArr_AddObject(conditionsNeedAdd, JSON_Clone(newCondition));
                                    }
                                }

                                // Send updated packet to BLE
                                JSON* packet = JSON_CreateObject();
                                JSON_SetText(packet, "sceneId", sceneId);
                                JSON_SetObject(packet, "actionsNeedRemove", actionsNeedRemove);
                                JSON_SetObject(packet, "actionsNeedAdd", actionsNeedAdd);
                                JSON_SetObject(packet, "conditionsNeedRemove", conditionsNeedRemove);
                                JSON_SetObject(packet, "conditionsNeedAdd", conditionsNeedAdd);
                                // printf("packet: %s\n", cJSON_PrintUnformatted(packet));
                                sendPacketToBle(-1, reqType, packet);

                                // Add this request to response list for checking response
                                JSON_ForEach(action, actionsNeedAdd) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* dpAddr = JSON_GetText(action, "entityAddr");
                                    int dpId = 0;
                                    if (JSON_HasKey(action, "dpAddr")) {
                                        dpAddr = JSON_GetText(action, "dpAddr");
                                        dpId = JSON_GetNumber(action, "dpId");
                                    }
                                    char* deviceId = JSON_GetText(action, "entityId");
                                    JSON* addedDevice = addDeviceToRespList(TYPE_ADD_SCENE, sceneId, dpAddr);
                                    if (addedDevice) {
                                        JSON_SetText(addedDevice, "deviceId", deviceId);
                                        JSON_SetText(addedDevice, "entityType", "action");
                                        JSON_SetNumber(addedDevice, "dpId", dpId);
                                    }
                                }
                                JSON_ForEach(action, actionsNeedRemove) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* dpAddr = JSON_GetText(action, "entityAddr");
                                    int dpId = 0;
                                    if (JSON_HasKey(action, "dpAddr")) {
                                        dpAddr = JSON_GetText(action, "dpAddr");
                                        dpId = JSON_GetNumber(action, "dpId");
                                    }
                                    char* deviceId = JSON_GetText(action, "entityId");
                                    JSON* addedDevice = addDeviceToRespList(TYPE_ADD_SCENE, sceneId, dpAddr);
                                    if (addedDevice) {
                                        JSON_SetText(addedDevice, "deviceId", deviceId);
                                        JSON_SetText(addedDevice, "entityType", "action");
                                        JSON_SetNumber(addedDevice, "dpId", dpId);
                                    }
                                }
                                JSON_ForEach(condition, conditionsNeedAdd) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* dpAddr = JSON_GetText(condition, "entityAddr");
                                    int dpId = 0;
                                    if (JSON_HasKey(condition, "dpAddr")) {
                                        dpAddr = JSON_GetText(condition, "dpAddr");
                                        dpId = JSON_GetNumber(condition, "dpId");
                                    }
                                    char* deviceId = JSON_GetText(condition, "entityId");
                                    JSON* addedDevice = addDeviceToRespList(TYPE_ADD_SCENE, sceneId, dpAddr);
                                    if (addedDevice) {
                                        JSON_SetText(addedDevice, "deviceId", deviceId);
                                        JSON_SetText(addedDevice, "entityType", "condition");
                                        JSON_SetNumber(addedDevice, "dpId", dpId);
                                    }
                                }
                                JSON_ForEach(condition, conditionsNeedRemove) {
                                    char* sceneId = JSON_GetText(payload, "id");
                                    char* dpAddr = JSON_GetText(condition, "entityAddr");
                                    int dpId = 0;
                                    if (JSON_HasKey(condition, "dpAddr")) {
                                        dpAddr = JSON_GetText(condition, "dpAddr");
                                        dpId = JSON_GetNumber(condition, "dpId");
                                    }
                                    char* deviceId = JSON_GetText(condition, "entityId");
                                    JSON* addedDevice = addDeviceToRespList(TYPE_ADD_SCENE, sceneId, dpAddr);
                                    if (addedDevice) {
                                        JSON_SetText(addedDevice, "deviceId", deviceId);
                                        JSON_SetText(addedDevice, "entityType", "condition");
                                        JSON_SetNumber(addedDevice, "dpId", dpId);
                                    }
                                }

                                // JSON_Delete(packet);
                            }
                            // Save new scene to database
                            Db_DeleteScene(sceneId);
                            Db_AddScene(g_sceneTobeUpdated);
                            if (!isLocal) {
                                sendNotiToUser("Cập nhật kịch bản HC thành công", false);
                            }
                        } else {
                            logInfo("Scene %s is not found", sceneId);
                            int isLocal = JSON_GetNumber(newScene, "isLocal");
                            if (!isLocal) {
                                Db_AddScene(newScene);
                                logInfo("Added new HC scene");
                            }
                        }
                        // JSON_Delete(oldScene);

                        break;
                    }
                    case TYPE_ADD_GROUP_LIGHT: {
                        logInfo("TYPE_ADD_GROUP_LIGHT");
                        g_groupTobeUpdated = JSON_Clone(payload);
                        // Add detail info for each devices and send request to BLE
                        int pageIndex = JSON_GetNumber(payload, "pageIndex");
                        JSON* devices = JSON_GetObject(payload, "devices");
                        JSON_ForEach(d, devices) {
                            char* deviceId = JSON_GetText(d, "deviceId");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                            if (foundDevices == 1) {
                                JSON_SetNumber(d, "gwIndex", deviceInfo.gwIndex);
                                JSON_SetText(d, "hcAddr", deviceInfo.hcAddr);
                                JSON_SetText(d, "deviceAddr", deviceInfo.addr);
                            }
                        }
                        char* tmp = cJSON_PrintUnformatted(payload);
                        printInfo("Parsed Packet: %s", tmp);
                        free(tmp);

                        // Insert group information to database
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        char* groupName = JSON_GetText(payload, "name");
                        char* pid = JSON_GetText(payload, "pid");
                        if (StringContains(pid, "BLE")) {
                            sendPacketToBle(-1, reqType, payload); // Send to all HC
                            char* devicesStr = cJSON_PrintUnformatted(devices);
                            if (Db_AddGroup(groupAddr, groupName, devicesStr, true, pid, pageIndex) == 0) {
                                PlayAudio("add_group_error");
                                break;
                            }
                            free(devicesStr);
                            // Add this request to response list for checking response
                            JSON_ForEach(d, devices) {
                                JSON* addedDevice = addDeviceToRespList(reqType, groupAddr, JSON_GetText(d, "deviceAddr"));
                                if (addedDevice) {
                                    JSON_SetText(addedDevice, "deviceId", JSON_GetText(d, "deviceId"));
                                }
                            }
                            char* str = cJSON_PrintUnformatted(g_checkRespList);
                            printInfo("g_checkRespList=%s", str);
                            free(str);
                        }
                        break;
                    }
                    case TYPE_DEL_GROUP_LIGHT:
                    case TYPE_DEL_GROUP_LINK: {
                        // Send request to BLE
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        JSON* devices = Db_FindDevicesInGroup(groupAddr);
                        JSON_SetObject(payload, "devices", devices);
                        sendPacketToBle(-1, reqType, payload);
                        // Delete group information from database
                        Db_DeleteGroup(groupAddr);
                        DeleteDeviceFromScenes(groupAddr);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_LIGHT: {
                        logInfo("TYPE_UPDATE_GROUP_LIGHT");
                        g_groupTobeUpdated = JSON_Clone(payload);
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        JSON* newDevices = JSON_GetObject(payload, "devices");
                        // Add deviceAddr and gwIndex to each new devices before sending to BLE service
                        JSON_ForEach(d, newDevices) {
                            char* deviceId = JSON_GetText(d, "deviceId");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                            if (foundDevices == 1) {
                                JSON_SetText(d, "deviceAddr", deviceInfo.addr);
                                JSON_SetText(d, "hcAddr", deviceInfo.hcAddr);
                                JSON_SetNumber(d, "gwIndex", deviceInfo.gwIndex);
                            }
                        }

                        // Find all devices that are need to be removed and added
                        JSON* devicesNeedRemove = JSON_CreateArray();
                        JSON* devicesNeedAdd = JSON_CreateArray();
                        JSON_ForEach(newDevice, newDevices) {
                            int state = JSON_GetNumber(newDevice, "state");
                            if (state == -3) {
                                cJSON_AddItemReferenceToArray(devicesNeedRemove, newDevice);
                            } else if (state == -2) {
                                cJSON_AddItemReferenceToArray(devicesNeedAdd, newDevice);
                            }
                        }

                        // Send updated packet to BLE
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "groupAddr", groupAddr);
                        JSON_SetObject(packet, "dpsNeedRemove", devicesNeedRemove);
                        JSON_SetObject(packet, "dpsNeedAdd", devicesNeedAdd);
                        sendPacketToBle(-1, reqType, packet);
                        // Add this request to response list for checking response
                        JSON_ForEach(device, devicesNeedAdd) {
                            char* deviceId = JSON_GetText(device, "deviceId");
                            char* deviceAddr = JSON_GetText(device, "deviceAddr");
                            JSON* addedDevice = addDeviceToRespList(reqType, groupAddr, deviceAddr);
                            if (addedDevice) {
                                JSON_SetText(addedDevice, "deviceId", deviceId);
                            }
                        }
                        JSON_ForEach(device, devicesNeedRemove) {
                            char* deviceId = JSON_GetText(device, "deviceId");
                            char* deviceAddr = JSON_GetText(device, "deviceAddr");
                            JSON* addedDevice = addDeviceToRespList(reqType, groupAddr, deviceAddr);
                            if (addedDevice) {
                                JSON_SetText(addedDevice, "deviceId", deviceId);
                            }
                        }
                        JSON_Delete(packet);
                        Db_SaveGroupDevices(groupAddr, newDevices);
                        char* str = cJSON_PrintUnformatted(g_checkRespList);
                        printInfo("g_checkRespList=%s", str);
                        free(str);
                        break;
                    }
                    case TYPE_ADD_GROUP_LINK: {
                        logInfo("TYPE_ADD_GROUP_LINK");
                        g_groupTobeUpdated = JSON_Clone(payload);
                        // Add detail info for each devices and send request to BLE
                        int pageIndex = JSON_GetNumber(payload, "pageIndex");
                        JSON* devices = JSON_GetObject(payload, "devices");
                        JSON_ForEach(d, devices) {
                            char* deviceId = JSON_GetText(d, "deviceId");
                            int dpId = JSON_GetNumber(d, "dpId");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                            if (foundDevices == 1) {
                                JSON_SetNumber(d, "gwIndex", deviceInfo.gwIndex);
                                JSON_SetText(d, "hcAddr", deviceInfo.hcAddr);
                                JSON_SetText(d, "deviceAddr", deviceInfo.addr);
                                DpInfo dpInfo;
                                int foundDps = Db_FindDp(&dpInfo, deviceId, dpId);
                                if (foundDps == 1) {
                                    JSON_SetText(d, "dpAddr", dpInfo.addr);
                                }
                            }
                        }
                        sendPacketToBle(-1, reqType, payload);

                        // Insert group information to database
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        char* groupName = JSON_GetText(payload, "name");
                        char* pid = JSON_GetText(payload, "pid");
                        char* devicesStr = cJSON_PrintUnformatted(devices);
                        if (Db_AddGroup(groupAddr, groupName, devicesStr, false, pid, pageIndex) == 0) {
                            PlayAudio("add_group_error");
                            break;
                        }
                        free(devicesStr);
                        // Add this request to response list for checking response
                        JSON_ForEach(d, devices) {
                            JSON* addedDevice = addDeviceToRespList(reqType, groupAddr, JSON_GetText(d, "dpAddr"));
                            if (addedDevice) {
                                JSON_SetText(addedDevice, "deviceId", JSON_GetText(d, "deviceId"));
                            }
                        }
                        char* str = cJSON_PrintUnformatted(g_checkRespList);
                        printInfo("g_checkRespList=%s", str);
                        free(str);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_LINK: {
                        logInfo("TYPE_UPDATE_GROUP_LINK");
                        g_groupTobeUpdated = JSON_Clone(payload);
                        char* groupAddr = JSON_GetText(payload, "groupAddr");
                        JSON* newDevices = JSON_GetObject(payload, "devices");
                        // Add detail info for each devices and send request to BLE
                        int pageIndex = JSON_GetNumber(payload, "pageIndex");
                        JSON* devices = JSON_GetObject(payload, "devices");
                        JSON_ForEach(d, devices) {
                            char* deviceId = JSON_GetText(d, "deviceId");
                            int dpId = JSON_GetNumber(d, "dpId");
                            DeviceInfo deviceInfo;
                            int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                            if (foundDevices == 1) {
                                JSON_SetNumber(d, "gwIndex", deviceInfo.gwIndex);
                                JSON_SetText(d, "hcAddr", deviceInfo.hcAddr);
                                JSON_SetText(d, "deviceAddr", deviceInfo.addr);
                                DpInfo dpInfo;
                                int foundDps = Db_FindDp(&dpInfo, deviceId, dpId);
                                if (foundDps == 1) {
                                    JSON_SetText(d, "dpAddr", dpInfo.addr);
                                }
                            }
                        }

                        // Find all devices that are need to be removed and added
                        JSON* devicesNeedRemove = JSON_CreateArray();
                        JSON* devicesNeedAdd = JSON_CreateArray();
                        JSON_ForEach(newDevice, newDevices) {
                            int state = JSON_GetNumber(newDevice, "state");
                            if (state == -3) {
                                cJSON_AddItemReferenceToArray(devicesNeedRemove, newDevice);
                            } else if (state == -2) {
                                cJSON_AddItemReferenceToArray(devicesNeedAdd, newDevice);
                            }
                        }

                        // Send updated packet to BLE
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "groupAddr", groupAddr);
                        JSON_SetObject(packet, "dpsNeedRemove", devicesNeedRemove);
                        JSON_SetObject(packet, "dpsNeedAdd", devicesNeedAdd);
                        sendPacketToBle(-1, reqType, packet);
                        // Add this request to response list for checking response
                        JSON_ForEach(device, devicesNeedAdd) {
                            char* deviceId = JSON_GetText(device, "deviceId");
                            JSON* addedDevice = addDeviceToRespList(reqType, groupAddr, JSON_GetText(device, "dpAddr"));
                            if (addedDevice) {
                                JSON_SetText(addedDevice, "deviceId", deviceId);
                            }
                        }
                        JSON_ForEach(device, devicesNeedRemove) {
                            char* deviceId = JSON_GetText(device, "deviceId");
                            JSON* addedDevice = addDeviceToRespList(reqType, groupAddr, JSON_GetText(device, "dpAddr"));
                            if (addedDevice) {
                                JSON_SetText(addedDevice, "deviceId", deviceId);
                            }
                        }
                        JSON_Delete(packet);
                        Db_SaveGroupDevices(groupAddr, newDevices);
                        char* str = cJSON_PrintUnformatted(g_checkRespList);
                        printInfo("g_checkRespList=%s", str);
                        free(str);
                        break;
                    }
                    case TYPE_LOCK_AGENCY: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            JSON_SetNumber(payload, "gwIndex", deviceInfo.gwIndex);
                            JSON* lock = JSON_GetObject(payload, "lock");
                            JSON_ForEach(l, lock) {
                                JSON_SetNumber(payload, "value", l->valueint - 2);
                                break;
                            }
                            sendPacketToBle(deviceInfo.gwIndex, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_LOCK_KIDS: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            JSON_SetNumber(payload, "gwIndex", deviceInfo.gwIndex);
                            sendPacketToBle(deviceInfo.gwIndex, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_SET_GROUP_TTL: {
                        JSON_ForEach(item, payload) {
                            if (item->string && cJSON_IsObject(item)) {
                                int ttl = JSON_GetNumber(item, "ttl");
                                JSON* devices = Db_FindDevicesInGroup(item->string);
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
                    case TYPE_DIM_LED_SWITCH: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            JSON_SetNumber(payload, "gwIndex", deviceInfo.gwIndex);
                            sendPacketToBle(deviceInfo.gwIndex, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_GET_GROUPS_OF_DEVICE:
                    case TYPE_GET_SCENES_OF_DEVICE: {
                        char* deviceId = JSON_GetText(payload, "deviceId");
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
                        if (foundDevices == 1) {
                            JSON_SetText(payload, "deviceAddr", deviceInfo.addr);
                            JSON_SetNumber(payload, "gwIndex", deviceInfo.gwIndex);
                            sendPacketToBle(deviceInfo.gwIndex, reqType, payload);
                        }
                        break;
                    }
                    case TYPE_SYNC_DEVICE_STATUS: {
                        logInfo("TYPE_SYNC_DEVICE_STATUS");
                        // Sync all status of devices to aws
                        char* cmd = "SELECT d.deviceId, dpId, dpValue, d.state, d.pid, d.pageIndex FROM devices_inf d JOIN devices dp ON d.deviceId=dp.deviceId WHERE dpId != 21 AND dpId != 106";
                        Sql_Query(cmd, row) {
                            char* deviceId = sqlite3_column_text(row, 0);
                            int dpId = sqlite3_column_int(row, 1);
                            int value = sqlite3_column_int(row, 2);
                            int state = sqlite3_column_int(row, 3);
                            char* pid = sqlite3_column_text(row, 4);
                            int pageIndex = sqlite3_column_int(row, 5);

                            if (StringCompare(pid, HG_BLE_SWITCH) || StringCompare(pid, HG_BLE_LIGHT_WHITE) ||
                                StringCompare(pid, RD_BLE_SENSOR_TEMP) || StringCompare(pid, RD_BLE_SENSOR_SMOKE) ||
                                StringCompare(pid, RD_BLE_SENSOR_MOTION) || StringCompare(pid, RD_BLE_SENSOR_DOOR) ||
                                StringCompare(pid, RD_BLE_LIGHT_RGB)) {

                                char payload[200];
                                sprintf(payload,"{\"deviceId\":\"%s\", \"state\":%d, \"dpId\":%d, \"dpValue\":%d}", deviceId, state, dpId, value);
                                sendToServicePageIndex(SERVICE_AWS, GW_RESP_ONOFF_STATE, pageIndex, payload);
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
                        if (person_type == 0 || person_type == 1 || person_type == 2) { //camera phát hiện người nhà hoặc người quen hoặc người lạ
                            int dpId = 5; //synch dpID reponse of Hanet with app Homegy
                            DpInfo dpInfo;
                            int foundDps = Db_FindDp(&dpInfo, camera_id, dpId);
                            logInfo("foundDps = %d",foundDps);
                            if (foundDps == 1) {
                                Db_SaveDpValue(dpInfo.deviceId, dpInfo.id, person_type);
                                checkSceneForDevice(camera_id, dpId, person_type, NULL, true);
                            }
                        }
                        // Check camera phát hiện đúng người
                        int dpId = 3;
                        DpInfo dpInfo;
                        int foundDps = Db_FindDp(&dpInfo, camera_id, dpId);
                        logInfo("foundDps = %d", foundDps);
                        if (foundDps == 1) {
                            // Db_SaveDpValueString(dpInfo.deviceId, dpInfo.id, person_id);
                            checkSceneForDevice(camera_id, dpId, 0, person_id, true);
                        }
                        break;
                    }
                }
            }
            JSON_Delete(payload);
            JSON_Delete(recvPacket);
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

    // Parse actions
    JSON_ForEach(action, actionsArray) {
        JSON* executorProperty = JSON_GetObject(action, "executorProperty");
        char* actionExecutor = JSON_GetText(action, "actionExecutor");
        char* entityId = JSON_GetText(action, "entityId");
        if (StringCompare(actionExecutor, "ruleTrigger")) {
            JSON_SetNumber(action, "dpValue", 2);
        } else if (StringCompare(actionExecutor, "ruleEnable")) {
            JSON_SetNumber(action, "dpValue", 1);
        } else if (StringCompare(actionExecutor, "ruleDisable")) {
            JSON_SetNumber(action, "dpValue", 0);
        } else if (StringCompare(actionExecutor, "deviceGroupDpIssue")) {

        } else if (StringCompare(actionExecutor, "irHGBLE")) {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, entityId);
            if (foundDevices == 1) {
                JSON_SetText(action, "pid", deviceInfo.pid);
                JSON_SetText(action, "entityAddr", deviceInfo.addr);
                JSON_SetText(action, "hcAddr", deviceInfo.hcAddr);
                JSON_SetNumber(action, "gwIndex", deviceInfo.gwIndex);
                Ble_AddExtraDpsToIrDevices(entityId, executorProperty);
                JSON_SetNumber(action, "commandIndex", i++);
            }
        } else {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, entityId);
            if (foundDevices == 1) {
                JSON_SetText(action, "pid", deviceInfo.pid);
                JSON_SetText(action, "entityAddr", deviceInfo.addr);
                JSON_SetText(action, "hcAddr", deviceInfo.hcAddr);
                JSON_SetNumber(action, "gwIndex", deviceInfo.gwIndex);
                int dpId = 0, dpValue = 0;
                if (StringCompare(deviceInfo.pid, HG_BLE_LIGHT_WHITE)) {
                    dpId = 20;
                } else if (StringContains(RD_BLE_LIGHT_RGB, deviceInfo.pid)) {
                    if (JSON_HasKey(executorProperty, "21")) {
                        char* value = JSON_GetText(executorProperty, "21");
                        if (StringContains(value, "scene_")) {
                            dpId = 21;
                        } else {
                            dpId = 20;
                        }
                    } else {
                        dpId = 20;
                    }
                } else {
                    JSON_ForEach(o, executorProperty) {
                        dpId = atoi(o->string);
                        dpValue = o->valueint;
                    }
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
    }

    // Parse conditions
    JSON* conditionsArray = JSON_GetObject(packet, "conditions");
    JSON_ForEach(condition, conditionsArray) {
        JSON* exprArray = JSON_GetObject(condition, "expr");
        char* entityId = JSON_GetText(condition, "entityId");
        if (!StringCompare(entityId, "timer")) {
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, entityId);
            if (foundDevices == 1) {
                int dpId = atoi(JArr_GetText(exprArray, 0) + 3);   // Template is "$dp1"
                // int dpValue = JArr_GetNumber(exprArray, 2);
                JSON* objItem = JArr_GetObject(exprArray, 2); //get obj for check type value of exprArray
                DpInfo dpInfo;
                int foundDps = Db_FindDp(&dpInfo, entityId, dpId);
                if (foundDps == 1 || StringCompare(deviceInfo.pid, HG_BLE_IR)) {
                    JSON_SetText(condition, "hcAddr", deviceInfo.hcAddr);
                    JSON_SetText(condition, "pid", deviceInfo.pid);
                    JSON_SetText(condition, "entityAddr", deviceInfo.addr);
                    JSON_SetNumber(condition, "gwIndex", deviceInfo.gwIndex);
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
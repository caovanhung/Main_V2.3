#include <mosquitto.h>
#include "aws_mosquitto.h"
#include "core_api.h"
#include "time_t.h"
#include "helper.h"
#include "database.h"
#include "common.h"

JSON *g_checkRespList;


void CoreInit() {
    g_checkRespList   = JSON_CreateArray();
}

void sendPacketToBle(int gwIndex, int reqType, JSON* packet) {
    ASSERT(packet);
    char topic[200];
    if (gwIndex >= 0) {
        // find address of the HC
        char* hcAddr = Db_FindHcAddr(gwIndex);
        if (hcAddr) {
            sprintf(topic, "%s_%s", MOSQ_TOPIC_DEVICE_BLE, hcAddr);
            sendPacketTo(topic, reqType, packet);
            free(hcAddr);
        } else {
            logError("Cannot found gateway at index %d", gwIndex);
        }
    } else {
        // Send to all HC if gwIndex < 0
        List* hcList = Db_FindAllHcAddr();
        for (int i = 0; i < hcList->count; i++) {
            sprintf(topic, "%s_%s", MOSQ_TOPIC_DEVICE_BLE, hcList->items[i]);
            sendPacketTo(topic, reqType, packet);
        }
    }
}

bool CompareDeviceById(JSON* device1, JSON* device2) {
    if (device1 && device2) {
        char* deviceId1 = JSON_GetText(device1, "deviceId");
        char* deviceId2 = JSON_GetText(device2, "deviceId");
        if (StringCompare(deviceId1, deviceId2)) {
            return true;
        }
    }
    return false;
}

JSON* parseGroupLinkDevices(const char* devices) {
    JSON* devicesArray = cJSON_CreateArray();
    if (devices) {
        List* splitList = String_Split(devices, "|");
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
        }
        List_Delete(splitList);
    }
    return devicesArray;
}

// Add device that need to check response to response list
JSON* addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr) {
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
    // if (JArr_FindByText(devices, "addr", deviceAddr) == NULL) {
        JSON *device = JArr_CreateObject(devices);
        JSON_SetText(device, "addr", deviceAddr);
        JSON_SetNumber(device, "status", -2);
        return device;
    // }
    return NULL;
}

JSON* requestIsInRespList(int reqType, const char* itemId) {
    ASSERT(itemId);
    char reqTypeStr[10];
    sprintf(reqTypeStr, "%d.%s", reqType, itemId);
    JSON* item = JArr_FindByText(g_checkRespList, "reqType", reqTypeStr);
    return item;
}

void updateDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr, int status) {
    ASSERT(itemId);
    ASSERT(deviceAddr);
    JSON* item = requestIsInRespList(reqType, itemId);
    if (item) {
        JSON* devices = JSON_GetObject(item, "devices");
        JSON_ForEach(d, devices) {
            char* addr = JSON_GetText(d, "addr");
            if (StringCompare(addr, deviceAddr)) {
                JSON_SetNumber(d, "status", status);
            }
        }
    }
    char* str = cJSON_PrintUnformatted(g_checkRespList);
    logInfo("[updateDeviceRespStatus] itemId=%s, deviceAddr=%s, g_checkRespList=%s", itemId, deviceAddr, str);
    free(str);
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


void Aws_DeleteDevice(const char* deviceId, int pageIndex) {
    ASSERT(deviceId);
    char payload[200];
    sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": null}}}", TYPE_DEL_DEVICE, SENDER_HC_TO_CLOUD, deviceId);
    sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_DEVICE_KICKOUT, pageIndex, payload);
}

void Aws_SaveDeviceState(const char* deviceId, int state, int pageIndex) {
    ASSERT(deviceId);
    char payload[200];
    sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"state\":%d}}}}", TYPE_UPDATE_DEVICE, SENDER_HC_TO_CLOUD, deviceId, state);
    sendToServicePageIndex(SERVICE_AWS, GW_RESP_ONLINE_STATE, pageIndex, payload);
}

void Aws_SaveDpValue(const char* deviceId, int dpId, int value, int pageIndex) {
    ASSERT(deviceId);
    char payload[200];
    sprintf(payload,"{\"deviceId\":\"%s\", \"state\":2, \"dpId\":%d, \"dpValue\":%d}", deviceId, dpId, value);
    sendToServicePageIndex(SERVICE_AWS, GW_RESP_ONOFF_STATE, pageIndex, payload);
}

void Aws_SaveDpValueString(const char* deviceId, int dpId, const char* value, int pageIndex) {
    ASSERT(deviceId);
    ASSERT(value);
    char payload[200];
    sprintf(payload,"{\"deviceId\":\"%s\", \"state\":2, \"dpId\":%d, \"dpValue\":\"%s\"}", deviceId, dpId, value);
    sendToServicePageIndex(SERVICE_AWS, GW_RESP_ONOFF_STATE, pageIndex, payload);
}

void Aws_DeleteGroup(const char* groupAddr) {
    ASSERT(groupAddr);
    char sqlCmd[100];
    sprintf(sqlCmd, "SELECT pageIndex FROM group_inf WHERE groupAdress = '%s';", groupAddr);
    Sql_Query(sqlCmd, row) {
        int pageIndex = sqlite3_column_int(row, 0);
        char payload[200];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": null}}}", TYPE_DEL_GROUP_LIGHT, SENDER_HC_TO_CLOUD, groupAddr);
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_GROUP, pageIndex, payload);
    }
}

void Aws_UpdateGroupValue(const char* groupAddr, int dpId, int dpValue) {
    ASSERT(groupAddr);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, groupAddr);
    if (foundDevices == 1) {
        int pageIndex = deviceInfo.pageIndex;
        char payload[200];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"dictDPs\":{\"%d\":%d}}}}}", TYPE_CTR_GROUP_NORMAL, SENDER_HC_TO_CLOUD, groupAddr, dpId, dpValue);
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_GROUP, pageIndex, payload);
    }
}

void Aws_UpdateGroupValueString(const char* groupAddr, int dpId, const char* dpValue) {
    ASSERT(groupAddr);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, groupAddr);
    if (foundDevices == 1) {
        int pageIndex = deviceInfo.pageIndex;
        char payload[200];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"dictDPs\":{\"%d\":\"%s\"}}}}}", TYPE_CTR_GROUP_NORMAL, SENDER_HC_TO_CLOUD, groupAddr, dpId, dpValue);
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_GROUP, pageIndex, payload);
    }
}

void Aws_SaveGroupDevices(const char* groupAddr) {
    ASSERT(groupAddr);
    char sqlCmd[100];
    sprintf(sqlCmd, "SELECT devices, pageIndex FROM group_inf WHERE groupAdress = '%s';", groupAddr);
    Sql_Query(sqlCmd, row) {
        char* devices = sqlite3_column_text(row, 0);
        int pageIndex = sqlite3_column_int(row, 1);
        char* payload = malloc(StringLength(devices) + 200);
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"devices\":%s}}}}", TYPE_UPDATE_GROUP_LIGHT, SENDER_HC_TO_CLOUD, groupAddr, devices);
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_GROUP, pageIndex, payload);
        free(payload);
    }
}

void Aws_DeleteScene(const char* sceneId) {
    ASSERT(sceneId);
    JSON* sceneInfo = Db_FindScene(sceneId);
    if (sceneInfo) {
        int pageIndex = JSON_GetNumber(sceneInfo, "pageIndex");
        char payload[200];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": null}}}", TYPE_DEL_SCENE, SENDER_HC_TO_CLOUD, sceneId);
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_SCENE, pageIndex, payload);
    }
    JSON_Delete(sceneInfo);
}

void Aws_EnableScene(const char* sceneId, bool state) {
    ASSERT(sceneId);
    JSON* sceneInfo = Db_FindScene(sceneId);
    if (sceneInfo) {
        int pageIndex = JSON_GetNumber(sceneInfo, "pageIndex");
        char payload[200];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"state\":%s}}}}", TYPE_UPDATE_SCENE, SENDER_HC_TO_CLOUD, sceneId, state?"true":"false");
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_SCENE, pageIndex, payload);
    }
    JSON_Delete(sceneInfo);
}

void Aws_SaveScene(const char* sceneId) {
    ASSERT(sceneId);
    char sqlCmd[100];
    sprintf(sqlCmd, "SELECT actions, conditions, pageIndex FROM scene_inf WHERE sceneId='%s';", sceneId);
    Sql_Query(sqlCmd, row) {
        char* actions = sqlite3_column_text(row, 0);
        char* conditions = sqlite3_column_text(row, 1);
        int pageIndex = sqlite3_column_int(row, 2);
        char* payload = malloc(StringLength(actions) + StringLength(conditions) + 200);
        if (conditions) {
            sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"actions\":%s, \"conditions\":%s}}}}", TYPE_UPDATE_SCENE, SENDER_HC_TO_CLOUD, sceneId, actions, conditions);
        } else {
            sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"actions\":%s, \"conditions\":null}}}}", TYPE_UPDATE_SCENE, SENDER_HC_TO_CLOUD, sceneId, actions);
        }
        sendToServicePageIndex(SERVICE_AWS, GW_RESPONSE_UPDATE_SCENE, pageIndex, payload);
    }
}

void Aws_UpdateLockKids(const char* deviceId, int dpId, int lockValue) {
    ASSERT(deviceId);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
    if (foundDevices == 1) {
        int pageIndex = deviceInfo.pageIndex;
        char payload[200];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"lock\":{\"%d\":%d}}}}}", TYPE_UPDATE_DEVICE, SENDER_HC_TO_CLOUD, deviceId, dpId, lockValue);
        sendToServicePageIndex(SERVICE_AWS, GW_RESP_ONLINE_STATE, pageIndex, payload);
    }
}

void Aws_ResponseLearningIR(const char* deviceId, const char* respCmd) {
    ASSERT(deviceId);
    ASSERT(respCmd);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
    if (foundDevices == 1) {
        char payload[500];
        sprintf(payload,"{\"state\": {\"reported\": {\"type\": %d,\"sender\":%d,\"%s\": {\"dictDPs\":{\"%s\":\"%s\"}}}}}", TYPE_CTR_DEVICE, SENDER_HC_TO_CLOUD, deviceId, DPID_IR_COMMAND, respCmd);
        sendToServicePageIndex(SERVICE_AWS, TYPE_NOTIFI_REPONSE, deviceInfo.pageIndex, payload);
    }
}


void Ble_ControlDeviceArray(const char* deviceId, uint8_t* dpIds, double* dpValues, int dpCount, const char* causeId) {
    ASSERT(deviceId);
    ASSERT(dpIds);
    ASSERT(dpValues);
    ASSERT(dpCount > 0);
    printInfo("[Ble_ControlDeviceArray] deviceId=%s, dpIds[0]=%d, dpValues[0]=%f", deviceId, dpIds[0], dpValues[0]);
    JSON* dictDPs = JSON_CreateObject();
    for (int i = 0; i < dpCount; i++) {
        char str[10];
        sprintf(str, "%d", dpIds[i]);
        JSON_SetNumber(dictDPs, str, dpValues[i]);
    }
    Ble_ControlDeviceJSON(deviceId, dictDPs, causeId);
    JSON_Delete(dictDPs);
}

void Ble_ControlDeviceStringDp(const char* deviceId, uint8_t dpId, char* dpValue, const char* causeId) {
    ASSERT(deviceId);
    ASSERT(dpValue);
    printInfo("[Ble_ControlDeviceStringDp] deviceId=%s, dpId=%d, dpValue=%s", deviceId, dpId, dpValue);
    JSON* dictDPs = JSON_CreateObject();
    char str[10];
    sprintf(str, "%d", dpId);
    JSON_SetText(dictDPs, str, dpValue);
    Ble_ControlDeviceJSON(deviceId, dictDPs, causeId);
    JSON_Delete(dictDPs);
}

void Ble_ControlDeviceJSON(const char* deviceId, JSON* dictDPs, const char* causeId) {
    ASSERT(deviceId);
    ASSERT(dictDPs);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
    if (foundDevices == 1) {
        if (StringCompare(deviceInfo.pid, HG_BLE_IR_AC) || StringCompare(deviceInfo.pid, HG_BLE_IR_TV) || StringCompare(deviceInfo.pid, HG_BLE_IR_FAN) || StringCompare(deviceInfo.pid, HG_BLE_IR_REMOTE)) {
            Ble_AddExtraDpsToIrDevices(deviceId, dictDPs);
        }
        JSON* p = JSON_CreateObject();
        JSON_SetText(p, "pid", deviceInfo.pid);
        JSON_SetNumber(p, "gwIndex", deviceInfo.gwIndex);
        JSON* newDictDps = JSON_AddArray(p, "dictDPs");
        JSON_ForEach(o, dictDPs) {
            int dpId = atoi(o->string);
            if (cJSON_IsString(o) && StringContains(o->valuestring, "scene_")) {
                List* tmp = String_Split(o->valuestring, "_");
                if (tmp->count == 2) {
                    uint8_t value = atoi(tmp->items[1]);
                    o->valueint = value;
                }
                List_Delete(tmp);
            }
            if (dpId == 106) {
                JSON* dp = JSON_CreateObject();
                JSON_SetNumber(dp, "id", dpId);
                JSON_SetText(dp, "valueString", o->valuestring);
                cJSON_AddItemToArray(newDictDps, dp);
            } else {
                DpInfo dpInfo;
                int dpFound = Db_FindDp(&dpInfo, deviceId, dpId);
                if (dpFound) {
                    JSON* dp = JSON_CreateObject();
                    JSON_SetNumber(dp, "id", dpId);
                    JSON_SetText(dp, "addr", dpInfo.addr);
                    JSON_SetNumber(dp, "value", o->valueint);
                    JSON_SetText(dp, "valueString", o->valuestring);
                    cJSON_AddItemToArray(newDictDps, dp);
                    if (dpId == 21) {
                        Aws_SaveDpValueString(deviceId, dpId, o->valuestring, dpInfo.pageIndex);
                    }
                    if (causeId) {
                        JSON* history = JSON_CreateObject();
                        JSON_SetText(history, "deviceId", deviceId);
                        JSON_SetNumber(history, "dpId", dpId);
                        JSON_SetNumber(history, "dpValue", o->valueint);
                        JSON_SetNumber(history, "eventType", EV_DEVICE_DP_CHANGED);
                        if (StringLength(causeId) > 10) {
                            JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_APP);
                        } else {
                            JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_SCENE);
                        }
                        JSON_SetText(history, "causeId", causeId);
                        Db_AddDeviceHistory(history);
                        JSON_Delete(history);
                    }
                }
            }
        }
        sendPacketToBle(deviceInfo.gwIndex, TYPE_CTR_DEVICE, p);
        JSON_Delete(p);
    } else {
        printInfo("Device %s is not found in the database\n", deviceId);
    }
}


void Ble_ControlGroupArray(const char* groupAddr, uint8_t* dpIds, double* dpValues, int dpCount, const char* causeId) {
    ASSERT(groupAddr);
    ASSERT(dpIds);
    ASSERT(dpValues);
    JSON* dictDPs = JSON_CreateObject();
    for (int i = 0; i < dpCount; i++) {
        char str[10];
        sprintf(str, "%d", dpIds[i]);
        JSON_SetNumber(dictDPs, str, dpValues[i]);
    }
    Ble_ControlGroupJSON(groupAddr, dictDPs, causeId);
    JSON_Delete(dictDPs);
}

// Control a group with string dp value
void Ble_ControlGroupStringDp(const char* groupAddr, uint8_t dpId, char* dpValue, const char* causeId) {
    ASSERT(groupAddr);
    ASSERT(dpValue);
    JSON* dictDPs = JSON_CreateObject();
    char str[10];
    sprintf(str, "%d", dpId);
    JSON_SetText(dictDPs, str, dpValue);
    Ble_ControlGroupJSON(groupAddr, dictDPs, causeId);
    JSON_Delete(dictDPs);
}

void Ble_ControlGroupJSON(const char* groupAddr, JSON* dictDPs, const char* causeId) {
    ASSERT(groupAddr);
    ASSERT(dictDPs);
    char* dictDPsString = cJSON_PrintUnformatted(dictDPs);
    printInfo("[Ble_ControlGroupJSON] groupAddr = %s, dictDPs=%s", groupAddr, dictDPsString);
    free(dictDPsString);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, groupAddr);
    if (foundDevices == 1) {
        // Update status of this group to AWS
        JSON_ForEach(dp, dictDPs) {
            int dpId = atoi(dp->string);
            int dpValue = dp->valueint;
            if (dpId != 21) {
                Aws_UpdateGroupValue(groupAddr, dpId, dpValue);
            } else {
                Aws_UpdateGroupValueString(groupAddr, dpId, dp->valuestring);
            }
        }

        // Update status of devices in this group to AWS and local database
        JSON* devices = Db_FindDevicesInGroup(groupAddr);
        JSON_ForEach(d, devices) {
            JSON_ForEach(dp, dictDPs) {
                if (cJSON_IsNumber(dp) || cJSON_IsBool(dp)) {
                    int dpId = atoi(dp->string);
                    if (dpId != 21) {
                        Aws_SaveDpValue(JSON_GetText(d, "deviceId"), dpId, dp->valueint, JSON_GetNumber(d, "pageIndex"));
                        Db_SaveDpValue(JSON_GetText(d, "deviceId"), dpId, dp->valueint);
                    } else {
                        Aws_SaveDpValueString(JSON_GetText(d, "deviceId"), dpId, dp->valuestring, JSON_GetNumber(d, "pageIndex"));
                    }
                }
            }
        }
        free(devices);

        JSON* p = JSON_CreateObject();
        JSON_SetText(p, "pid", deviceInfo.pid);
        JSON_SetNumber(p, "gwIndex", deviceInfo.gwIndex);
        JSON* newDictDps = JSON_AddArray(p, "dictDPs");
        JSON_ForEach(o, dictDPs) {
            int dpId = atoi(o->string);
            if (cJSON_IsString(o) && StringContains(o->valuestring, "scene_")) {
                List* tmp = String_Split(o->valuestring, "_");
                if (tmp->count == 2) {
                    uint8_t value = atoi(tmp->items[1]);
                    o->valueint = value;
                }
                List_Delete(tmp);
            }
            DpInfo dpInfo;
            int dpFound = Db_FindDp(&dpInfo, groupAddr, dpId);
            if (dpFound) {
                JSON* dp = JSON_CreateObject();
                JSON_SetNumber(dp, "id", dpId);
                JSON_SetText(dp, "addr", dpInfo.addr);
                JSON_SetNumber(dp, "value", o->valueint);
                JSON_SetText(dp, "valueString", o->valuestring);
                cJSON_AddItemToArray(newDictDps, dp);

                if (causeId) {
                    JSON* history = JSON_CreateObject();
                    JSON_SetText(history, "deviceId", groupAddr);
                    JSON_SetNumber(history, "dpId", dpId);
                    JSON_SetNumber(history, "dpValue", o->valueint);
                    JSON_SetNumber(history, "eventType", EV_DEVICE_DP_CHANGED);
                    if (StringLength(causeId) > 10) {
                        JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_APP);
                    } else {
                        JSON_SetNumber(history, "causeType", EV_CAUSE_TYPE_SCENE);
                    }
                    JSON_SetText(history, "causeId", causeId);
                    Db_AddDeviceHistory(history);
                    JSON_Delete(history);
                }
            }
        }

        sendPacketToBle(-1, TYPE_CTR_GROUP_NORMAL, p);
        JSON_Delete(p);
    }
}


void Ble_SetTTL(int gwIndex, const char* deviceAddr, uint8_t ttl) {
    JSON* p = JSON_CreateObject();
    JSON_SetNumber(p, "gwIndex", gwIndex);
    JSON_SetText(p, "deviceAddr", deviceAddr);
    JSON_SetNumber(p, "ttl", ttl);
    sendPacketToBle(gwIndex, TYPE_SET_DEVICE_TTL, p);
    JSON_Delete(p);
}

// Add dp 1, 2, 3 to IR devices
void Ble_AddExtraDpsToIrDevices(const char* deviceId, JSON* dictDPs) {
    ASSERT(deviceId);
    DpInfo dpInfo;
    if (Db_FindDp(&dpInfo, deviceId, atoi(DPID_IR_BRAND_ID))) {
        JSON_SetNumber(dictDPs, DPID_IR_BRAND_ID, dpInfo.value);
    }
    if (Db_FindDp(&dpInfo, deviceId, atoi(DPID_IR_REMOTE_ID))) {
        JSON_SetNumber(dictDPs, DPID_IR_REMOTE_ID, dpInfo.value);
    }
    if (Db_FindDp(&dpInfo, deviceId, atoi(DPID_IR_COMMAND_TYPE))) {
        JSON_SetNumber(dictDPs, DPID_IR_COMMAND_TYPE, dpInfo.value);
    }
}


void Wifi_ControlDevice(const char* deviceId, const char* code) {
    ASSERT(deviceId);
    ASSERT(code);
    JSON* p = JSON_CreateObject();
    JSON_SetText(p, "deviceId", deviceId);
    JSON_SetText(p, "code", code);
    sendPacketTo(SERVICE_TUYA, TYPE_CTR_DEVICE, p);
    JSON_Delete(p);
}

void Wifi_ControlGroup(const char* groupId, const char* code) {
    ASSERT(groupId);
    ASSERT(code);
    JSON* p = JSON_CreateObject();
    JSON_SetText(p, "groupId", groupId);
    JSON_SetText(p, "code", code);
    sendPacketTo(SERVICE_TUYA, TYPE_CTR_GROUP_NORMAL, p);
    JSON_Delete(p);
}

#include <mosquitto.h>
#include "mosquitto.h"
#include "core_api.h"
#include "time_t.h"
#include "helper.h"
#include "database.h"


JSON *g_checkRespList;


void CoreInit() {
    g_checkRespList   = JSON_CreateArray();
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
            JSON_SetNumber(arrayItem, "gwIndex", deviceInfo.gwIndex);
            JSON_SetNumber(arrayItem, "pageIndex", deviceInfo.pageIndex);
        }
        cJSON_AddItemToArray(devicesArray, arrayItem);
    }
    return devicesArray;
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

void Ble_ControlDeviceArray(const char* deviceId, uint8_t* dpIds, double* dpValues, int dpCount, const char* causeId) {
    ASSERT(deviceId);
    ASSERT(dpIds);
    ASSERT(dpValues);
    JSON* dictDPs = JSON_CreateObject();
    for (int i = 0; i < dpCount; i++) {
        char str[10];
        sprintf(str, "%d", dpIds[i]);
        JSON_SetNumber(dictDPs, str, dpValues[i]);
    }
    Ble_ControlDeviceJSON(deviceId, dictDPs, causeId);
    JSON_Delete(dictDPs);
}

void Ble_ControlDeviceJSON(const char* deviceId, JSON* dictDPs, const char* causeId) {
    ASSERT(deviceId);
    ASSERT(dictDPs);
    DeviceInfo deviceInfo;
    int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
    if (foundDevices == 1) {
        if (StringCompare(deviceInfo.pid, HG_BLE_IR_AC) || StringCompare(deviceInfo.pid, HG_BLE_IR_TV) || StringCompare(deviceInfo.pid, HG_BLE_IR_FAN)) {
            Ble_AddExtraDpsToIrDevices(deviceId, dictDPs);
        }
        JSON* p = JSON_CreateObject();
        JSON_SetText(p, "pid", deviceInfo.pid);
        JSON* newDictDps = JSON_AddArray(p, "dictDPs");
        JSON_ForEach(o, dictDPs) {
            int dpId = atoi(o->string);
            DpInfo dpInfo;
            int dpFound = Db_FindDp(&dpInfo, deviceId, dpId);
            if (dpFound) {
                JSON* dp = JSON_CreateObject();
                JSON_SetNumber(dp, "id", dpId);
                JSON_SetText(dp, "addr", dpInfo.addr);
                JSON_SetNumber(dp, "value", o->valueint);
                JSON_SetText(dp, "valueString", o->valuestring);
                cJSON_AddItemToArray(newDictDps, dp);
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
        sendPacketTo(SERVICE_BLE, TYPE_CTR_DEVICE, p);
        JSON_Delete(p);
    }
}


void Ble_ControlGroup(const char* groupAddr, JSON* dictDPs) {
    ASSERT(groupAddr);
    ASSERT(dictDPs);
    DeviceInfo deviceInfo;
    // int foundDevices = Db_FindDevice(&deviceInfo, groupAddr); //groupAddr no have into table devices_inf in database
    int foundDevices = 1;
    if (foundDevices == 1) {
        // Update status of devices in this group to AWS
        char* devicesStr = Db_FindDevicesInGroup(groupAddr);
        JSON* groupDevices = parseGroupNormalDevices(devicesStr);
        JSON_ForEach(d, groupDevices) {
            JSON_ForEach(dp, dictDPs) {
                if (cJSON_IsNumber(dp) || cJSON_IsBool(dp)) {
                    int dpId = atoi(dp->string);
                    Aws_SaveDpValue(JSON_GetText(d, "deviceId"), dpId, dp->valueint, JSON_GetNumber(d, "pageIndex"));
                }
            }
        }
        free(devicesStr);

        JSON* p = JSON_CreateObject();
        JSON_SetText(p, "pid", deviceInfo.pid);
        JSON* newDictDps = JSON_AddArray(p, "dictDPs");
        JSON_ForEach(o, dictDPs) {
            int dpId = atoi(o->string);
            DpInfo dpInfo;
            int dpFound = Db_FindDp(&dpInfo, groupAddr, dpId);
            if (dpFound) {
                JSON* dp = JSON_CreateObject();
                JSON_SetNumber(dp, "id", dpId);
                JSON_SetText(dp, "addr", dpInfo.addr);
                JSON_SetNumber(dp, "value", o->valueint);
                JSON_SetText(dp, "valueString", o->valuestring);
                cJSON_AddItemToArray(newDictDps, dp);
            }
        }

        sendPacketTo(SERVICE_BLE, TYPE_CTR_GROUP_NORMAL, p);
        JSON_Delete(p);
    }
}


void Ble_SetTTL(int gwIndex, const char* deviceAddr, uint8_t ttl) {
    JSON* p = JSON_CreateObject();
    JSON_SetNumber(p, "gwIndex", gwIndex);
    JSON_SetText(p, "deviceAddr", deviceAddr);
    JSON_SetNumber(p, "ttl", ttl);
    sendPacketTo(SERVICE_BLE, TYPE_SET_DEVICE_TTL, p);
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
    if (JArr_FindByText(devices, "addr", deviceAddr) == NULL) {
        JSON *device = JArr_CreateObject(devices);
        JSON_SetText(device, "addr", deviceAddr);
        JSON_SetNumber(device, "status", -1);
    }
    return item;
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
#include"core_data_process.h"
#include"core_process_t.h"
#include"helper.h"

//////////////////////////////////////DATABASE PROCESS//////////////////////////////////

bool addNewDevice(sqlite3 **db, JSON* packet)
{
    JSON* protParam = JSON_GetObject(packet, "protocol_para");
    JSON* dictMeta = JSON_GetObject(protParam, "dictMeta");
    JSON* deviceInfo = JSON_CreateObject();
    char* deviceId = JSON_GetText(packet, KEY_DEVICE_ID);
    char* deviceAddr = JSON_GetText(protParam, KEY_UNICAST);
    char* pid = JSON_GetText(protParam, KEY_PID);
    int pageIndex = JSON_GetNumber(packet, "pageIndex");
    JSON_SetText(deviceInfo, KEY_DEVICE_ID, deviceId);
    JSON_SetText(deviceInfo, KEY_NAME, JSON_GetText(packet, KEY_NAME));
    JSON_SetText(deviceInfo, KEY_UNICAST, deviceAddr);
    JSON_SetText(deviceInfo, KEY_ID_GATEWAY, JSON_GetText(protParam, KEY_ID_GATEWAY));
    JSON_SetText(deviceInfo, KEY_DEVICE_KEY, JSON_GetText(protParam, KEY_DEVICE_KEY));
    JSON_SetText(deviceInfo, KEY_PID, pid);
    JSON_SetNumber(deviceInfo, KEY_PROVIDER, JSON_GetNumber(protParam, KEY_PROVIDER));
    JSON_SetNumber(deviceInfo, "pageIndex", pageIndex);
    Db_AddDevice(deviceInfo);

    JSON_ForEach(dp, dictMeta) {
        int dpId = atoi(dp->string);
        Db_AddDp(deviceId, dpId, dp->valuestring, pageIndex);
    }

    if (StringCompare(pid, HG_BLE_IR_AC) ||
        StringCompare(pid, HG_BLE_IR_TV) ||
        StringCompare(pid, HG_BLE_IR_FAN)) {
        Db_AddDp(deviceId, 3, deviceAddr, pageIndex);
        JSON* dictDPs = JSON_GetObject(protParam, "dictDPs");
        JSON_ForEach(dp, dictDPs) {
            int dpId = atoi(dp->string);
            if (cJSON_IsNumber) {
                Db_SaveDpValue(deviceId, dpId, dp->valueint);
            } else {
                Db_SaveDpValueString(deviceId, dpId, dp->valuestring);
            }
        }
    } else if (StringCompare(pid, HG_BLE_IR)) {
        Db_AddDp(deviceId, 1, deviceAddr, pageIndex);
    } else if (StringCompare(pid, CAM_HANET)) {
        JSON* dictDPs = JSON_GetObject(protParam, "dictDPs");
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
    JSON_Delete(deviceInfo);
    return true;
}


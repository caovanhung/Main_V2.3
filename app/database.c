#include "database.h"
#include "helper.h"
#include <math.h>
#include <stdlib.h>
#include <common.h>

bool open_database(const char *filename, sqlite3 **db)
{
    int rc = sqlite3_open(filename, db);
    if (rc == SQLITE_OK)
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool close_database(sqlite3 **db)
{

    int rc = sqlite3_close(*db);
    if (rc == SQLITE_OK)
    {
        return false;
    }
    else
    {
        return true;
    }
}

int Db_AddGateway(JSON* gatewayInfo) {
    ASSERT(gatewayInfo);
    char sqlCmd[500];
    char* name = JSON_GetText(gatewayInfo, "name");
    char* address1 = JSON_GetText(gatewayInfo, "gateway1");
    char* hcAddr = address1;
    char* address2 = JSON_GetText(gatewayInfo, "gateway2");
    char* appkey = JSON_GetText(gatewayInfo, KEY_APP_KEY);
    char* ivIndex = JSON_GetText(gatewayInfo, KEY_IV_INDEX);
    char* netkeyIndex = JSON_GetText(gatewayInfo, KEY_NETKEY_INDEX);
    char* netkey = JSON_GetText(gatewayInfo, KEY_NETKEY);
    char* appkeyIndex = JSON_GetText(gatewayInfo, KEY_APP_KEY_INDEX);
    char* deviceKey1 = JSON_GetText(gatewayInfo, "deviceKey1");
    char* deviceKey2 = JSON_GetText(gatewayInfo, "deviceKey2");
    int isMaster = JSON_GetNumber(gatewayInfo, "isMaster");

    // Get maximum gateway id
    int currentId = -1;
    Sql_Query("SELECT id FROM gateway ORDER BY id DESC LIMIT 1", row) {
        currentId = sqlite3_column_int(row, 0);
    }
    // printInfo("[Db_AddGateway] currentId=%d", currentId);

    sprintf(sqlCmd, "INSERT INTO GATEWAY VALUES(%d,'%s',%d,'%s','%s','%s','%s','%s','%s','%s','%s')", currentId + 1, name, isMaster, hcAddr, address1, appkey, ivIndex, netkeyIndex, netkey, appkeyIndex, deviceKey1);
    Sql_Exec(sqlCmd);
    // printf("sqlCmd: %s\n", sqlCmd);

    // sprintf(sqlCmd, "INSERT INTO GATEWAY VALUES(%d,'%s',%d,'%s','%s','%s','%s','%s','%s','%s','%s')", currentId + 2, name, isMaster, hcAddr, address2, appkey, ivIndex, netkeyIndex, netkey, appkeyIndex, deviceKey2);
    // Sql_Exec(sqlCmd);
    return 1;
}

int Db_FindGatewayId(const char* gatewayAddr) {
    ASSERT(gatewayAddr);
    int id = -1;
    char sqlCmd[300];
    sprintf(sqlCmd, "SELECT id FROM gateway WHERE address = '%s'", gatewayAddr);
    Sql_Query(sqlCmd, row) {
        id = sqlite3_column_int(row, 0);
    }
    return id;
}

char* Db_FindHcAddr(int gwIndex) {
    ASSERT(gwIndex >= 0);
    char* hcAddr = NULL;
    char sqlCmd[300];
    sprintf(sqlCmd, "SELECT hcAddr FROM gateway WHERE id = '%d'", gwIndex);
    Sql_Query(sqlCmd, row) {
        hcAddr = malloc(10);
        StringCopy(hcAddr, sqlite3_column_text(row, 0));
    }
    return hcAddr;
}

List* Db_FindAllHcAddr() {
    List* resultList = List_Create();
    char sqlCmd[200];
    sprintf(sqlCmd, "SELECT DISTINCT hcAddr FROM gateway");
    Sql_Query(sqlCmd, row) {
        List_PushString(resultList, sqlite3_column_text(row, 0));
    }
    return resultList;
}

int Db_AddDevice(JSON* deviceInfo) {
    ASSERT(deviceInfo);
    char* gwAddr = JSON_HasKey(deviceInfo, "gateWay")? JSON_GetText(deviceInfo, "gateWay") : "_";
    int gwIndex = Db_FindGatewayId(gwAddr);
    char* id = JSON_GetText(deviceInfo, KEY_DEVICE_ID);
    char* name = JSON_GetText(deviceInfo, KEY_NAME);
    char* unicast = JSON_HasKey(deviceInfo, KEY_UNICAST)? JSON_GetText(deviceInfo, KEY_UNICAST) : NULL;
    char* deviceKey = StringCompare(unicast, "0000") == false? JSON_GetText(deviceInfo, KEY_DEVICE_KEY) : NULL;
    int provider = JSON_GetNumber(deviceInfo, KEY_PROVIDER);
    char* pid = JSON_GetText(deviceInfo, KEY_PID);
    int pageIndex = JSON_GetNumber(deviceInfo, "pageIndex");
    char sqlCmd[500];
    sprintf(sqlCmd, "INSERT INTO DEVICES_INF(deviceId, name,  Unicast, gwIndex, deviceKey, provider, pid,   state, pageIndex, offlineCount) \
                                      VALUES('%s',     '%s',  '%s',    %d,      '%s',      '%d',     '%s',  '%d',    %d,      '0')",
                                             id,       name,  unicast, gwIndex, deviceKey, provider, pid,   3,     pageIndex);
    // printf("%s", sqlCmd);
    Sql_Exec(sqlCmd);
    return 1;
}

JSON* Db_GetAllDevices() {
    char sqlCmd[100];
    JSON* arr = JSON_CreateArray();
    sprintf(sqlCmd, "SELECT deviceId FROM devices_inf");
    Sql_Query(sqlCmd, row) {
        char* deviceId = sqlite3_column_text(row, 0);
        JSON* item = JArr_CreateObject(arr);
        JSON_SetText(item, "deviceId", deviceId);
    }

    JSON_ForEach(d, arr) {
        char* deviceId = JSON_GetText(d, "deviceId");
        JSON* dictDPs = JSON_CreateObject();
        sprintf(sqlCmd, "SELECT dpId, dpValue FROM devices WHERE deviceId='%s'", deviceId);
        Sql_Query(sqlCmd, row) {
            int dpId = sqlite3_column_int(row, 0);
            char* dpValue = sqlite3_column_text(row, 1);
            char tmp[20];
            sprintf(tmp, "%d", dpId);
            if (dpId == 106) {
                JSON_SetText(dictDPs, tmp, dpValue);
            } else {
                JSON_SetNumber(dictDPs, tmp, atoi(dpValue));
            }
        }
        JSON_SetObject(d, "dictDPs", dictDPs);
    }

    return arr;
}

int Db_FindDeviceBySql(DeviceInfo* deviceInfo, const char* sqlCommand) {
    ASSERT(deviceInfo); ASSERT(sqlCommand);
    int rowCount = 0;
    Sql_Query(sqlCommand, row) {
        deviceInfo->state = sqlite3_column_int(row, 1);
        deviceInfo->provider = sqlite3_column_int(row, 7);
        StringCopy(deviceInfo->id, sqlite3_column_text(row, 0));
        StringCopy(deviceInfo->name, sqlite3_column_text(row, 2));
        StringCopy(deviceInfo->addr, sqlite3_column_text(row, 4));
        deviceInfo->gwIndex = sqlite3_column_int(row, 5);
        StringCopy(deviceInfo->pid, sqlite3_column_text(row, 8));
        deviceInfo->pageIndex = sqlite3_column_int(row, 15);
        deviceInfo->offlineCount = sqlite3_column_int(row, 16);
        rowCount = 1;
    }
    if (rowCount == 1) {
        char sql[200];
        sprintf(sql, "SELECT hcAddr FROM gateway WHERE id=%d", deviceInfo->gwIndex);
        Sql_Query(sql, r) {
            StringCopy(deviceInfo->hcAddr, sqlite3_column_text(r, 0));
        }
    }
    return rowCount;
}

int Db_FindDevice(DeviceInfo* deviceInfo, const char* deviceId) {
    ASSERT(deviceInfo); ASSERT(deviceId);
    char sqlCommand[200];
    sprintf(sqlCommand, "SELECT * FROM devices_inf WHERE deviceID = '%s';", deviceId);
    return Db_FindDeviceBySql(deviceInfo, sqlCommand);
}

int Db_FindDeviceByAddr(DeviceInfo* deviceInfo, const char* deviceAddr, const* hcAddr) {
    ASSERT(deviceInfo);
    ASSERT(deviceAddr);
    ASSERT(hcAddr);
    char sqlCommand[200];
    sprintf(sqlCommand, "SELECT * FROM devices_inf d JOIN gateway g ON g.id = d.gwIndex WHERE Unicast = '%s' AND g.hcAddr = '%s';", deviceAddr, hcAddr);
    return Db_FindDeviceBySql(deviceInfo, sqlCommand);
}

int Db_SaveDeviceState(const char* deviceId, int state) {
    ASSERT(deviceId);
    long long int currentTime = timeInMilliseconds();
    char sqlCmd[200];
    sprintf(sqlCmd, "UPDATE devices_inf SET state='%d', last_updated='%lld' WHERE deviceId='%s';", state, currentTime, deviceId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_SaveOfflineCountForDevice(const char* deviceId, int offlineCount) {
    ASSERT(deviceId);
    long long int currentTime = timeInMilliseconds();
    char sqlCmd[200];
    sprintf(sqlCmd, "UPDATE devices_inf SET offlineCount='%d', last_updated='%lld' WHERE deviceId='%s';", offlineCount, currentTime, deviceId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_DeleteDevice(const char* deviceId) {
    ASSERT(deviceId);
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM devices_inf WHERE deviceId = '%s'", deviceId);
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices WHERE deviceId = '%s'", deviceId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_DeleteAllDevices() {
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM devices_inf");
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices");
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_AddGroup(const char* groupAddr, const char* groupName, const char* devices, bool isLight, const char* pid, int pageIndex) {
    ASSERT(groupAddr); ASSERT(groupName); ASSERT(devices);
    char* sqlCmd = malloc(strlen(devices) + 200);
    sprintf(sqlCmd, "INSERT INTO GROUP_INF(groupAdress, name, isLight, devices, pageIndex) VALUES('%s', '%s', '%d', '%s', %d)", groupAddr, groupName, isLight, devices, pageIndex);
    Sql_Exec(sqlCmd);
    if (isLight) {
        for (int dp = 20; dp <= 24; dp++) {
            sprintf(sqlCmd, "INSERT INTO DEVICES(deviceId, address, dpId, dpValue, pageIndex) VALUES('%s', '%s', '%d', '%d', %d)", groupAddr, groupAddr, dp, 0, pageIndex);
            Sql_Exec(sqlCmd);
        }
        sprintf(sqlCmd, "INSERT INTO DEVICES_INF(deviceId, unicast, name, pid, pageIndex) VALUES('%s', '%s', '%s', '%s', '%d')", groupAddr, groupAddr, groupName, pid, pageIndex);
        Sql_Exec(sqlCmd);
    }
    free(sqlCmd);
    return 1;
}

JSON* Db_FindDevicesInGroup(const char* groupAddr) {
    ASSERT(groupAddr);
    JSON* resultDevices = NULL;
    char sqlCommand[100];
    sprintf(sqlCommand, "SELECT * FROM group_inf WHERE groupAdress = '%s';", groupAddr);
    Sql_Query(sqlCommand, row) {
        char* devices = sqlite3_column_text(row, 5);
        resultDevices = JSON_Parse(devices);
    }
    if (resultDevices) {
        JSON_ForEach(d, resultDevices) {
            char* deviceId = JSON_GetText(d, "deviceId");
            DeviceInfo deviceInfo;
            int foundDevices = Db_FindDevice(&deviceInfo, deviceId);
            if (foundDevices == 1) {
                JSON_SetText(d, "deviceAddr", deviceInfo.addr);
                JSON_SetNumber(d, "gwIndex", deviceInfo.gwIndex);
                JSON_SetText(d, "hcAddr", deviceInfo.hcAddr);
                JSON_SetNumber(d, "pageIndex", deviceInfo.pageIndex);
            }
            if (JSON_HasKey(d, "dpId")) {
                int dpId = JSON_GetNumber(d, "dpId");
                DpInfo dpInfo;
                int foundDps = Db_FindDp(&dpInfo, deviceId, dpId);
                if (foundDps == 1) {
                    JSON_SetText(d, "dpAddr", dpInfo.addr);
                }
            }
        }
    }
    return resultDevices;
}

int Db_GetGroupType(const char* groupAddr) {
    ASSERT(groupAddr);
    char sqlCommand[100];
    int groupType = -1;
    sprintf(sqlCommand, "SELECT isLight FROM group_inf WHERE groupAdress = '%s';", groupAddr);
    Sql_Query(sqlCommand, row) {
        groupType = sqlite3_column_int(row, 0);
    }
    return groupType;
}

int Db_SaveGroupDevices(const char* groupAddr, JSON* devices) {
    ASSERT(groupAddr); ASSERT(devices);
    char* devicesStr = cJSON_PrintUnformatted(devices);
    char* sqlCmd = malloc(strlen(devicesStr) + 300);
    sprintf(sqlCmd, "UPDATE group_inf SET devices='%s' WHERE groupAdress = '%s'", devicesStr, groupAddr);
    Sql_Exec(sqlCmd);
    free(sqlCmd);
    free(devicesStr);
    return 1;
}

int Db_DeleteGroup(const char* groupAddr) {
    ASSERT(groupAddr);
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM group_inf WHERE groupAdress = '%s'", groupAddr);
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices WHERE address = '%s'", groupAddr);
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices_inf WHERE unicast = '%s'", groupAddr);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_DeleteAllGroup() {
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM group_inf");
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices WHERE deviceId = address");
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_AddDp(const char* deviceId, int dpId, const char* addr, int pageIndex) {
    ASSERT(deviceId); ASSERT(addr);
    char sqlCmd[500];
    sprintf(sqlCmd, "INSERT INTO DEVICES(deviceId, dpID,  address, dpValue, pageIndex, updateTime) \
                                  VALUES('%s',     '%d',  '%s',    '0',     %d,        %lld)",
                                         deviceId, dpId,  addr,             pageIndex, 0);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_FindDp(DpInfo* dpInfo, const char* deviceId, int dpId) {
    ASSERT(dpInfo); ASSERT(deviceId);
    int rowCount = 0;
    char sqlCommand[300];
    sprintf(sqlCommand, "SELECT deviceId, address, dpValue, pageIndex FROM devices WHERE deviceId = '%s' AND dpId='%d';", deviceId, dpId);
    Sql_Query(sqlCommand, row) {
        dpInfo->id = dpId;
        char* value = sqlite3_column_text(row, 2);
        if (StringCompare(value, "true")) {
            dpInfo->value = 1;
        } else if (StringCompare(value, "false")) {
            dpInfo->value = 0;
        } else {
            dpInfo->value = strtod(value, NULL);
        }
        StringCopy(dpInfo->valueStr,value); //get valueStr for check condition camera hanet
        StringCopy(dpInfo->deviceId, sqlite3_column_text(row, 0));
        StringCopy(dpInfo->addr, sqlite3_column_text(row, 1));
        dpInfo->pageIndex = sqlite3_column_int(row, 3);
        rowCount = 1;
    }
    return rowCount;
}

int Db_FindDpByAddr(DpInfo* dpInfo, const char* dpAddr, const* hcAddr) {
    ASSERT(dpInfo); ASSERT(dpAddr);
    int rowCount = 0;
    char sqlCommand[300];
    sprintf(sqlCommand, "SELECT dp.deviceId, dp.dpId, dp.address, dp.dpValue, dp.pageIndex FROM devices dp JOIN devices_inf d ON dp.deviceId = d.deviceId JOIN gateway g ON g.id=d.gwIndex WHERE dp.address='%s' AND g.hcAddr='%s' ORDER BY dpId LIMIT 1;", dpAddr, hcAddr);
    Sql_Query(sqlCommand, row) {
        dpInfo->id = atoi(sqlite3_column_text(row, 1));
        char* value = sqlite3_column_text(row, 3);
        if (StringCompare(value, "true")) {
            dpInfo->value = 1;
        } else if (StringCompare(value, "false")) {
            dpInfo->value = 0;
        } else {
            dpInfo->value = strtod(value, NULL);
        }
        StringCopy(dpInfo->deviceId, sqlite3_column_text(row, 0));
        StringCopy(dpInfo->addr, sqlite3_column_text(row, 2));
        dpInfo->pageIndex = sqlite3_column_int(row, 4);
        rowCount = 1;
    }
    return rowCount;
}

int Db_FindDpByAddrAndDpId(DpInfo* dpInfo, const char* dpAddr, int dpId) {
    ASSERT(dpInfo); ASSERT(dpAddr);
    int rowCount = 0;
    char sqlCommand[300];
    sprintf(sqlCommand, "SELECT deviceId, dpId, address, dpValue, pageIndex FROM devices WHERE address='%s' AND dpId=%d;", dpAddr, dpId);
    Sql_Query(sqlCommand, row) {
        dpInfo->id = atoi(sqlite3_column_text(row, 1));
        char* value = sqlite3_column_text(row, 3);
        if (StringCompare(value, "true")) {
            dpInfo->value = 1;
        } else if (StringCompare(value, "false")) {
            dpInfo->value = 0;
        } else {
            dpInfo->value = strtod(value, NULL);
        }
        StringCopy(dpInfo->deviceId, sqlite3_column_text(row, 0));
        StringCopy(dpInfo->addr, sqlite3_column_text(row, 2));
        dpInfo->pageIndex = sqlite3_column_int(row, 4);
        rowCount = 1;
    }
    return rowCount;
}

int Db_SaveDpValue(const char* deviceId, int dpId, double value) {
    ASSERT(deviceId);
    char sqlCmd[200];
    long long int currentTime = timeInMilliseconds();
    sprintf(sqlCmd, "UPDATE devices SET dpValue='%f', updateTime=%lld WHERE deviceId='%s' AND dpId=%d", value, currentTime, deviceId, dpId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_SaveDpValueString(const char* deviceId, int dpId, const char* value) {
    ASSERT(deviceId); ASSERT(value);
    char sqlCmd[500];
    long long int currentTime = timeInMilliseconds();
    sprintf(sqlCmd, "UPDATE devices SET dpValue='%s', updateTime=%lld WHERE deviceId='%s' AND dpId=%d", value, currentTime, deviceId, dpId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_LoadSceneToRam() {
    g_sceneCount = 0;
    char* sqlCmd = "SELECT * FROM scene_inf";
    Sql_Query(sqlCmd, row) {
        g_sceneList = realloc(g_sceneList, (g_sceneCount + 1) * sizeof(Scene));
        g_sceneList[g_sceneCount].isLocal = sqlite3_column_int(row, 1);
        g_sceneList[g_sceneCount].isEnable = sqlite3_column_int(row, 2);
        g_sceneList[g_sceneCount].runningActionIndex = -1;
        StringCopy(g_sceneList[g_sceneCount].id, sqlite3_column_text(row, 0));
        StringCopy(g_sceneList[g_sceneCount].name, sqlite3_column_text(row, 3));
        g_sceneList[g_sceneCount].type = atoi(sqlite3_column_text(row, 4));

        // Load preconditions
        char* preconditions = sqlite3_column_text(row, 10);
        JSON* pre = JSON_Parse(preconditions);
        if (pre) {
            char* loops = JSON_GetText(pre, "loops");
            g_sceneList[g_sceneCount].effectRepeat = strtol(loops, NULL, 2);
            char* start = JSON_GetText(pre, "start");
            char* end = JSON_GetText(pre, "end");
            List* timeItems = String_Split(start, ":");
            if (timeItems->count == 2) {
                g_sceneList[g_sceneCount].effectFrom = atoi(timeItems->items[0]) * 60 + atoi(timeItems->items[1]);
            }
            List_Delete(timeItems);
            timeItems = String_Split(end, ":");
            if (timeItems->count == 2) {
                g_sceneList[g_sceneCount].effectTo = atoi(timeItems->items[0]) * 60 + atoi(timeItems->items[1]);
            }
            List_Delete(timeItems);
        }

        // Load actions
        char* actions = sqlite3_column_text(row, 5);
        JSON* actionsArray = JSON_Parse(actions);
        int actionCount = 0;
        JSON_ForEach(act, actionsArray) {
            if (actionCount < SCENE_ACTIONS_MAX) {
                SceneAction* action = &g_sceneList[g_sceneCount].actions[actionCount];
                action->dpCount = 0;
                if (g_sceneList[g_sceneCount].isLocal == false || (JSON_HasKey(act, "actionExecutor") && JSON_HasKey(act, "state") && JSON_GetNumber(act, "state") == 0)) {
                    StringCopy(action->entityId, JSON_GetText(act, "entityId"));
                    char* actionExecutor = JSON_GetText(act, "actionExecutor");
                    JSON* executorProperty = JSON_GetObject(act, "executorProperty");
                    if (StringCompare(actionExecutor, "dpIssue")) {
                        action->actionType = EntityDevice;
                    } else if (StringCompare("ruleTrigger", actionExecutor)) {
                        action->actionType = EntityScene;
                        action->dpValues[0] = 2;
                    } else if (StringCompare("ruleEnable", actionExecutor)) {
                        action->actionType = EntityScene;
                        action->dpValues[0] = 1;
                    } else if (StringCompare("ruleDisable", actionExecutor)) {
                        action->actionType = EntityScene;
                        action->dpValues[0] = 0;
                    } else if (StringCompare(actionExecutor, "delay")) {
                        action->actionType = EntityDelay;
                        int minutes = atoi(JSON_GetText(executorProperty, "minutes"));
                        action->delaySeconds = atoi(JSON_GetText(executorProperty, "seconds"));
                        action->delaySeconds = minutes * 60 + action->delaySeconds;
                    } else if (StringCompare(actionExecutor, "deviceGroupDpIssue")) {
                        action->actionType = EntityGroup;
                    }

                    JSON_ForEach(o, executorProperty) {
                        action->dpIds[action->dpCount] = atoi(o->string);
                        if (cJSON_IsNumber(o) || cJSON_IsBool(o)) {
                            action->dpValues[action->dpCount] = (double)o->valueint;
                        } else if (cJSON_IsString(o)) {
                            if (action->dpIds[action->dpCount] == 21) {
                                if (StringContains(o->valuestring, "scene_")) {
                                    List* tmp = String_Split(o->valuestring, "_");
                                    if (tmp->count == 2) {
                                        uint8_t value = atoi(tmp->items[1]);
                                        action->dpIds[0] = 21;
                                        action->dpValues[0] = value;
                                        action->dpCount = 1;
                                        break;
                                    }
                                    List_Delete(tmp);
                                } else {
                                    action->dpValues[action->dpCount] = -1;
                                }
                            } else {
                                action->valueType = ValueTypeString;
                                action->dpIds[0] = action->dpIds[action->dpCount];
                                action->dpCount = 1;
                                StringCopy(action->valueString, o->valuestring);
                                break;
                            }
                        }
                        action->dpCount++;
                    }

                    // Load 'code' field for controlling tuya
                    if (JSON_HasKey(act, "code")) {
                        StringCopy(action->wifiCode, JSON_GetText(act, "code"));
                    }
                    int isWifi = JSON_HasKey(act, "isWifi")? JSON_GetNumber(act, "isWifi") : 0;
                    if (isWifi || JSON_HasKey(act, "code")) {
                        StringCopy(action->serviceName, SERVICE_TUYA);  // Service for controlling Tuya device
                    } else {
                        StringCopy(action->serviceName, SERVICE_BLE);   // Service for controlling BLE device
                    }
                    actionCount++;
                }
            }
        }
        g_sceneList[g_sceneCount].actionCount = actionCount;

        // Load conditions
        JSON* conditionsArray = JSON_Parse(sqlite3_column_text(row, 6));
        int conditionCount = 0;
        JSON_ForEach(condition, conditionsArray) {
            if (conditionCount < SCENE_CONDITIONS_MAX) {
                if (g_sceneList[g_sceneCount].isLocal == false || (JSON_HasKey(condition, "state") && JSON_GetNumber(condition, "state") == 0)) {
                    SceneCondition* cond = &g_sceneList[g_sceneCount].conditions[conditionCount];
                    StringCopy(cond->entityId, JSON_GetText(condition, "entityId"));
                    JSON* exprArray = JSON_GetObject(condition, "expr");
                    cond->timeReached = 0;
                    if (StringCompare(cond->entityId, "timer")) {
                        cond->conditionType = EntitySchedule;
                        cond->repeat = strtol(JSON_GetText(exprArray, "loops"), NULL, 2);
                        char* time = JSON_GetText(exprArray, "time");
                        List* timeItems = String_Split(time, ":");
                        if (timeItems->count == 2) {
                            int hour = atoi(timeItems->items[0]);
                            int minute = atoi(timeItems->items[1]);
                            cond->schMinutes = hour * 60 + minute;
                            // If repeat is 0, scheMinutes will be epoch time so that scene will be executed only 1 time
                            if (cond->repeat == 0) {
                                char* dateStr = JSON_GetText(exprArray, "date");
                                if (dateStr) {
                                    int date = atoi(&dateStr[6]);
                                    dateStr[6] = 0;
                                    int month = atoi(&dateStr[4]);
                                    dateStr[4] = 0;
                                    int year = atoi(dateStr);
                                    // Convert date time to epoch time
                                    struct tm t;
                                    t.tm_year = year - 1900;  // Year - 1900
                                    t.tm_mon = month - 1;     // Month, where 0 = jan
                                    t.tm_mday = date;         // Day of the month
                                    t.tm_hour = hour;
                                    t.tm_min = minute;
                                    t.tm_sec = 0;
                                    t.tm_isdst = -1;
                                    cond->schMinutes = mktime(&t);
                                    printInfo("schMinutes of scene %s is %u", g_sceneList[g_sceneCount].id, cond->schMinutes);
                                }
                            }
                        }
                        List_Delete(timeItems);
                    } else {
                        cond->conditionType = EntityDevice;
                        DeviceInfo deviceInfo;
                        int foundDevices = Db_FindDevice(&deviceInfo, cond->entityId);
                        if (foundDevices == 1) {
                            StringCopy(cond->expr, JArr_GetText(exprArray, 1));
                            cond->dpId = atoi(JArr_GetText(exprArray, 0) + 3);   // Template is "$dp1"
                            DpInfo dpInfo;
                            int foundDps = Db_FindDp(&dpInfo, cond->entityId, cond->dpId);
                            if (foundDps == 1 || StringCompare(deviceInfo.pid, HG_BLE_IR)) {
                                StringCopy(cond->pid, deviceInfo.pid);
                                if (foundDps == 1) {
                                    StringCopy(cond->dpAddr, dpInfo.addr);
                                }
                            }
                        }
                        JSON* objItem = JArr_GetObject(exprArray, 2);
                        if (cJSON_IsString(objItem)) {
                            cond->valueType = ValueTypeString;
                            StringCopy(cond->dpValueStr, JArr_GetText(exprArray, 2));
                        } else if(cJSON_IsNumber(objItem) || cJSON_IsBool(objItem)){
                            cond->valueType = ValueTypeDouble;
                            int dpValue = JArr_GetNumber(exprArray, 2);
                            cond->dpValue = dpValue;
                        }
                    }
                    conditionCount++;
                }
            }
        }
        if (conditionCount == 0) {
            g_sceneList[g_sceneCount].type = SceneTypeManual;
        }
        g_sceneList[g_sceneCount].conditionCount = conditionCount;
        g_sceneCount++;
        JSON_Delete(actionsArray);
        JSON_Delete(conditionsArray);
    }
    logInfo("Loaded %d scenes from database\n", g_sceneCount);
    return 1;
}

int Db_SaveSceneCondDate(const char* sceneId, int conditionIndex, const char* date) {
    ASSERT(sceneId);
    ASSERT(date);
    char sqlCmd[200];
    char* conditions = NULL;
    sprintf(sqlCmd, "SELECT conditions FROM scene_inf WHERE sceneId='%s'", sceneId);
    Sql_Query(sqlCmd, row) {
        char* cond = sqlite3_column_text(row, 0);
        conditions = malloc(strlen(cond) + 1);
        StringCopy(conditions, cond);
    }
    if (conditions) {
        JSON* conditionsArray = JSON_Parse(conditions);
        int i = 0;
        JSON_ForEach(condition, conditionsArray) {
            if (i == conditionIndex) {
                JSON* expr = JSON_GetObject(condition, "expr");
                if (expr) {
                    JSON_SetText(expr, "date", date);
                }
                break;
            }
            i++;
        }
        char* condStr = cJSON_PrintUnformatted(conditionsArray);
        char* updateSql = malloc(StringLength(condStr) + 500);
        sprintf(updateSql, "UPDATE scene_inf SET conditions='%s' WHERE sceneId='%s'", condStr, sceneId);
        Sql_Exec(updateSql);
        // Db_LoadSceneToRam();
        JSON_Delete(conditionsArray);
        free(condStr);
        free(updateSql);
        free(conditions);
    }
    return 0;
}

int Db_AddScene(JSON* sceneInfo) {
    char* sceneId = JSON_GetText(sceneInfo, "id");
    char* name = JSON_GetText(sceneInfo, "name");
    int state = JSON_GetNumber(sceneInfo, "state");
    int pageIndex = JSON_GetNumber(sceneInfo, "pageIndex");
    int isLocal = JSON_GetNumber(sceneInfo, "isLocal");
    char* sceneType = JSON_GetText(sceneInfo, "sceneType");
    JSON* actionsObj = JSON_GetObject(sceneInfo, "actions");
    JSON* conditionsObj = JSON_GetObject(sceneInfo, "conditions");
    char* actions = cJSON_PrintUnformatted(actionsObj);
    char* conditions = cJSON_PrintUnformatted(conditionsObj);
    char* preconditions;
    if (JSON_HasKey(sceneInfo, "preconditions")) {
        preconditions = cJSON_PrintUnformatted(JSON_GetObject(sceneInfo, "preconditions"));
    } else {
        preconditions = "";
    }
    char* sqlCmd = malloc(StringLength(actions) + StringLength(conditions) + 500);
    sprintf(sqlCmd, "INSERT INTO SCENE_INF(sceneId, name, state, isLocal, sceneType, actions, conditions, pageIndex, preconditions)  \
                                    VALUES('%s',    '%s', '%d',  '%d',    '%s',      '%s',    '%s',       %d,        '%s')",
                                           sceneId, name, state, isLocal, sceneType, actions, conditions, pageIndex, preconditions);
    Sql_Exec(sqlCmd);
    Db_LoadSceneToRam();
    free(sqlCmd);
    free(actions);
    free(conditions);
    if (!StringCompare(preconditions, "")) {
        free(preconditions);
    }
    return 1;
}

JSON* Db_FindScene(const char* sceneId) {
    int rowCount = 0;
    char sqlCommand[100];
    JSON* sceneInfo = JSON_CreateObject();
    sprintf(sqlCommand, "SELECT * FROM scene_inf WHERE sceneId='%s';", sceneId);
    Sql_Query(sqlCommand, row) {
        JSON_SetText(sceneInfo, "sceneId", sqlite3_column_text(row, 0));
        JSON_SetNumber(sceneInfo, "isLocal", sqlite3_column_int(row, 1));
        JSON_SetText(sceneInfo, "sceneName", sqlite3_column_text(row, 3));
        JSON_SetText(sceneInfo, "sceneType", sqlite3_column_text(row, 4));
        JSON* actionsArray = JSON_Parse(sqlite3_column_text(row, 5));
        JSON* conditionsArray = JSON_Parse(sqlite3_column_text(row, 6));
        JSON_SetObject(sceneInfo, "actions", actionsArray);
        JSON_SetObject(sceneInfo, "conditions", conditionsArray);
        JSON_SetNumber(sceneInfo, "pageIndex", sqlite3_column_int(row, 9));
        rowCount = 1;
    }
    if (rowCount == 1) {
        return sceneInfo;
    }
    JSON_Delete(sceneInfo);
    return NULL;
}

int Db_DeleteScene(const char* sceneId) {
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM scene_inf WHERE sceneId = '%s'", sceneId);
    Sql_Exec(sqlCmd);
    Db_LoadSceneToRam();
    return 1;
}

int Db_DeleteAllScene() {
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM scene_inf");
    Sql_Exec(sqlCmd);
    Db_LoadSceneToRam();
    return 1;
}

int Db_EnableScene(const char* sceneId, int enableOrDisable) {
    char sqlCmd[100];
    sprintf(sqlCmd, "UPDATE scene_inf SET state=%d WHERE sceneId = '%s'", enableOrDisable, sceneId);
    Sql_Exec(sqlCmd);
    // Db_LoadSceneToRam();
    return 1;
}

int Db_RemoveSceneAction(const char* sceneId, const char* deviceAddr) {
    ASSERT(sceneId); ASSERT(deviceAddr);
    // Get actions of scene
    char* actionStr;
    char sqlCmd[300];
    sprintf(sqlCmd, "SELECT actions FROM scene_inf WHERE sceneId='%s' AND isLocal='1';", sceneId);
    Sql_Query(sqlCmd, row) {
        char* str = sqlite3_column_text(row, 0);
        if (str) {
            actionStr = malloc(strlen(str));
            StringCopy(actionStr, str);
        }
    }
    if (actionStr) {
        JSON* actions = JSON_Parse(actionStr);
        int i = 0;
        JSON_ForEach(action, actions) {
            char* addr = JSON_GetText(action, "dpAddr");
            if (StringCompare(addr, deviceAddr)) {
                JArr_RemoveIndex(actions, i);
                break;
            }
            i++;
        }
        // Save actions
        sprintf(sqlCmd, "UPDATE scene_inf SET actions='%s' WHERE sceneId='%s'", sceneId);
        Sql_Exec(sqlCmd);
        Db_LoadSceneToRam();
        return 1;
    }
    return 0;
}


int Db_SaveScene(const char* sceneId, JSON* actions, JSON* conditions) {
    ASSERT(sceneId);
    ASSERT(actions);
    ASSERT(conditions);
    char* actionStr = cJSON_PrintUnformatted(actions);
    char* conditionStr = cJSON_PrintUnformatted(conditions);
    char* sqlCmd = malloc(StringLength(actionStr) + StringLength(conditionStr) + 500);
    if (JArr_Count(conditions) == 0) {
        sprintf(sqlCmd, "UPDATE scene_inf SET actions='%s', conditions=NULL WHERE sceneId='%s'", actionStr, sceneId);
    } else {
        sprintf(sqlCmd, "UPDATE scene_inf SET actions='%s', conditions='%s' WHERE sceneId='%s'", actionStr, conditionStr, sceneId);
    }
    Sql_Exec(sqlCmd);
}


int Db_AddDeviceHistory(JSON* packet) {
    long long time = timeInMilliseconds();
    uint8_t causeType = JSON_HasKey(packet, "causeType")? JSON_GetNumber(packet, "causeType"): 0;
    char*   causeId   = JSON_HasKey(packet, "causeId")? JSON_GetText(packet, "causeId") : "";
    uint8_t eventType = JSON_GetNumber(packet, "eventType");
    char*   deviceId  = JSON_GetText(packet, "deviceId");
    uint8_t dpId      = JSON_HasKey(packet, "dpId")? JSON_GetNumber(packet, "dpId") : 0;
    uint16_t dpValue  = JSON_HasKey(packet, "dpValue")? JSON_GetNumber(packet, "dpValue") : 0;
    char*   dpValueStr  = JSON_HasKey(packet, "dpValueStr")? JSON_GetText(packet, "dpValueStr") : NULL;
    char sqlCmd[500];
    if (dpValueStr) {
        sprintf(sqlCmd, "INSERT INTO device_histories(time  , causeType, causeId, eventType, deviceId, dpId, dpValue) \
                                               VALUES('%lld', %d       , '%s'   , %d        , '%s'    , %d  , '%s'  )",
                                                      time  , causeType, causeId, eventType, deviceId, dpId, dpValueStr);
    } else {
        sprintf(sqlCmd, "INSERT INTO device_histories(time  , causeType, causeId, eventType, deviceId, dpId, dpValue) \
                                               VALUES('%lld', %d       , '%s'   , %d        , '%s'    , %d  , '%d'  )",
                                                      time  , causeType, causeId, eventType, deviceId, dpId, dpValue);
    }
    // printf("[Db_AddDeviceHistory]: %s", sqlCmd);
    Sql_Exec(sqlCmd);
    return 1;
}

JSON* Db_FindDeviceHistories(long long startTime, long long endTime, const char* deviceId, char* dpIds, int causeType, int eventType, int limit) {
    ASSERT(deviceId);
    JSON* histories = JSON_CreateObject();
    JSON_SetNumber(histories, "type", TYPE_GET_DEVICE_HISTORY);
    JSON_SetNumber(histories, "sender", SENDER_HC_TO_CLOUD);
    JSON_SetText(histories, "deviceId", deviceId);
    char sqlCmd[500];
    char dpIdCondition[50] = "", causeTypeCondition[50] = "", eventTypeCondition[50] = "";
    if (dpIds && strlen(dpIds) > 0) {
        sprintf(dpIdCondition, "AND dpId IN (%s)", dpIds);
    }
    if (causeType >= 0) {
        sprintf(causeTypeCondition, "AND causeType=%d", causeType);
    }
    if (eventType >= 0) {
        sprintf(eventTypeCondition, "AND eventType=%d", eventType);
    }
    JSON* rows = JSON_AddArray(histories, "rows");
    // get row details
    sprintf(sqlCmd, "SELECT * FROM device_histories WHERE time >= %lld AND time <= %lld AND deviceId='%s' %s %s %s ORDER BY time DESC LIMIT %d", startTime, endTime, deviceId, dpIdCondition, causeTypeCondition, eventTypeCondition, limit);
    Sql_Query(sqlCmd, row) {
        JSON* r = JArr_CreateObject(rows);
        JSON_SetNumber(r, "time",       sqlite3_column_int64(row, 1));
        JSON_SetNumber(r, "causeType",  sqlite3_column_int(row, 2));
        JSON_SetText  (r, "causeId",    sqlite3_column_text(row, 3));
        JSON_SetNumber(r, "eventType", sqlite3_column_int(row, 4));
        JSON_SetNumber(r, KEY_DP_ID,       sqlite3_column_int(row, 6));
        JSON_SetText  (r, "dpValue",    sqlite3_column_text(row, 7));
    }
    return histories;
}

int sql_creat_table(sqlite3 **db,char *name_table)
{
    char *err_msg;
    // sqlite3_open(VAR_DATABASE,db);
    int rc = sqlite3_exec(*db, name_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("SQL ERROR: %s\n", err_msg);
        sqlite3_free(err_msg);
        // sqlite3_close(*db);
        return -1;
    }
    // sqlite3_close(*db);
    return 0;
}

bool creat_table_database(sqlite3 **db)
{
    int check = 0;
    check = sql_creat_table(db,"DROP TABLE IF EXISTS GATEWAY;CREATE TABLE GATEWAY(id INTEGER, name TEXT, isMaster INTEGER, hcAddr TEXT, address TEXT, appkey TEXT, ivIndex TEXT, netkeyIndex TEXT, netkey TEXT, appkeyIndex TEXT, deviceKey TEXT);");
    if(check != 0)
    {
        printf("DELETE GATEWAY is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS DEVICES;CREATE TABLE DEVICES(deviceID TEXT, dpID TEXT, address TEXT, dpValue TEXT, pageIndex INTEGER, updateTime INTEGER);");
    if(check != 0)
    {
        printf("DELETE DEVICES is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS DEVICES_INF;CREATE TABLE DEVICES_INF(deviceID TEXT, state INTEGER, name TEXT, MAC TEXT, Unicast TEXT, gwIndex INTEGER, deviceKey TEXT, provider INTEGER, pid TEXT, created INTEGER, modified INTEGER, last_updated INTEGER, firmware TEXT, GroupList TEXT, SceneList TEXT, pageIndex INTEGER, offlineCount INTEGER);");
    if(check != 0)
    {
        printf("DELETE DEVICES_INF is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS GROUP_INF;CREATE TABLE GROUP_INF(groupAdress TEXT, state INTEGER, name TEXT, pid TEXT, isLight INTEGER, devices TEXT, pageIndex INTEGER);");
    if(check != 0)
    {
        printf("DELETE GROUP_INF is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS SCENE_INF;CREATE TABLE SCENE_INF(sceneId TEXT,isLocal INTEGER,state INTEGER,name TEXT,sceneType TEXT,actions TEXT,conditions TEXT,created INTEGER,last_updated INTEGER, pageIndex INTEGER, preconditions TEXT);");
    if(check != 0)
    {
        printf("DELETE SCENE_INF is error!\n");
        return false;
    }
    // usleep(100);
    // check = sql_creat_table(db, "DROP TABLE IF EXISTS DEVICE_HISTORIES;CREATE TABLE DEVICE_HISTORIES(id INTEGER PRIMARY KEY AUTOINCREMENT, time INTEGER, causeType INTEGER, causeId TEXT, eventType INTEGER, deviceId TEXT, dpId INTEGER, dpValue TEXT); CREATE INDEX device_histories_time_device_id ON DEVICE_HISTORIES(time, deviceId);");
    // if(check != 0)
    // {
    //     printf("DELETE SCENE_INF is error!\n");
    //     return false;
    // }
    return true;
}

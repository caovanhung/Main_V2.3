#include "database.h"
#include "helper.h"
#include <math.h>
#include <stdlib.h>

extern sqlite3* db;

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

void printColumnValue(sqlite3_stmt* stmt, int col)
{
  int colType = sqlite3_column_type(stmt, col);
  switch(colType)
  {
    case SQLITE_INTEGER:
         printf("  %3d   ", sqlite3_column_int(stmt, col));
         break;
    case SQLITE_FLOAT:
         printf("  %5.2f", sqlite3_column_double(stmt, col));
         break;
    case SQLITE_TEXT:
         printf("  %-5s", sqlite3_column_text(stmt, col));
         break;
    case SQLITE_NULL:
         printf("  null");
         break;
    case SQLITE_BLOB:
         printf("  blob");
         break;
    }
}

void sql_print_a_table(sqlite3 **db,char *name_table)
{
    char *sql =(char*) malloc(100 * sizeof(char));
    //append_string(&sql,"SELECT * FROM ",name_table);
    sprintf(sql,"SELECT * FROM %s;",name_table);
    // sqlite3_open(VAR_DATABASE,db);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    int col  = 0,tmp_index = 0;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        tmp_index = sqlite3_data_count(stmt);
        for ( col=0; col< tmp_index; col++) {
          printColumnValue(stmt, col);
        }
        printf("\n");
    }
    sqlite3_finalize(stmt);
    sql = NULL;
    free(sql);
    // sqlite3_close(*db);

}

void print_database(sqlite3 **db)
{
    printf("\n\n\n");
    printf("GATEWAY\n");
    sql_print_a_table(db,"GATEWAY");
    printf("\n\n\n");
    printf("DEVICES\n");
    sql_print_a_table(db,"DEVICES");
    printf("\n\n\n");
    printf("DEVICES_INF\n");
    sql_print_a_table(db,"DEVICES_INF");
    printf("\n\n\n");
    printf("GROUP_INF\n");
    sql_print_a_table(db,"GROUP_INF");
    printf("\n\n\n");
    printf("SCENE_INF\n");
    sql_print_a_table(db,"SCENE_INF");
    printf("\n\n\n");
}

int Db_FindDeviceBySql(DeviceInfo* deviceInfo, const char* sqlCommand)
{
    int rc = 0, rowCount = 0;
    sqlite3_stmt *sqlResponse;
    rc = sqlite3_prepare_v2(db, sqlCommand, -1, &sqlResponse, NULL);
    if (rc == SQLITE_OK)
    {
        while (sqlite3_step(sqlResponse) == SQLITE_ROW)
        {
            deviceInfo->state = sqlite3_column_int(sqlResponse, 1);
            deviceInfo->provider = sqlite3_column_int(sqlResponse, 7);
            strcpy(deviceInfo->id, sqlite3_column_text(sqlResponse, 0));
            strcpy(deviceInfo->addr, sqlite3_column_text(sqlResponse, 4));
            strcpy(deviceInfo->gatewayId, sqlite3_column_text(sqlResponse, 5));
            strcpy(deviceInfo->pid, sqlite3_column_text(sqlResponse, 8));
            rowCount = 1;
        }
    }
    sqlite3_finalize(sqlResponse);
    return rowCount;
}

int Db_FindDevice(DeviceInfo* deviceInfo, const char* deviceId)
{
    char sqlCommand[100];
    sprintf(sqlCommand, "SELECT * FROM devices_inf WHERE deviceID = '%s';", deviceId);
    return Db_FindDeviceBySql(deviceInfo, sqlCommand);
}

int Db_FindDeviceByAddr(DeviceInfo* deviceInfo, const char* deviceAddr)
{
    char sqlCommand[100];
    sprintf(sqlCommand, "SELECT * FROM devices_inf WHERE Unicast = '%s';", deviceAddr);
    return Db_FindDeviceBySql(deviceInfo, sqlCommand);
}

int Db_SaveDeviceState(const char* deviceId, int state) {
    char sqlCmd[200];
    sprintf(sqlCmd, "UPDATE devices_inf SET state='%d' WHERE deviceId='%s';", state, deviceId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_DeleteDevice(const char* deviceId) {
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM devices_inf WHERE deviceId = '%s'", deviceId);
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices WHERE deviceId = '%s'", deviceId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_AddGroup(const char* groupAddr, const char* groupName, const char* devices, bool isLight) {
    char sqlCmd[100];
    sprintf(sqlCmd, "INSERT INTO GROUP_INF(groupAdress, name, devices) VALUES('%s', '%s', '%s')", groupAddr, groupName, devices);
    Sql_Exec(sqlCmd);
    if (isLight) {
        sprintf(sqlCmd, "INSERT INTO DEVICES(deviceId, address, dpId, dpValue) VALUES('%s', '%s', '%s', '%s')", groupAddr, groupAddr, "20", "0");
        Sql_Exec(sqlCmd);
        sprintf(sqlCmd, "INSERT INTO DEVICES(deviceId, address, dpId, dpValue) VALUES('%s', '%s', '%s', '%s')", groupAddr, groupAddr, "22", "0");
        Sql_Exec(sqlCmd);
        sprintf(sqlCmd, "INSERT INTO DEVICES(deviceId, address, dpId, dpValue) VALUES('%s', '%s', '%s', '%s')", groupAddr, groupAddr, "23", "0");
        Sql_Exec(sqlCmd);
    }
    return 1;
}

char* Db_FindDevicesInGroup(const char* groupAddr)
{
    char* resultDeviceIds = NULL;
    char sqlCommand[100];
    sprintf(sqlCommand, "SELECT * FROM group_inf WHERE groupAdress = '%s';", groupAddr);
    Sql_Query(sqlCommand, row) {
        char* devices = sqlite3_column_text(row, 4);
        resultDeviceIds = malloc(strlen(devices) + 1);
        strcpy(resultDeviceIds, devices);
    }
    return resultDeviceIds;
}

int Db_DeleteGroup(const char* groupAddr) {
    char sqlCmd[100];
    sprintf(sqlCmd, "DELETE FROM group_inf WHERE groupAdress = '%s'", groupAddr);
    Sql_Exec(sqlCmd);
    sprintf(sqlCmd, "DELETE FROM devices WHERE address = '%s'", groupAddr);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_FindDp(dp_info_t* dpInfo, const char* deviceId, int dpId) {
    int rowCount = 0;
    char sqlCommand[100];
    sprintf(sqlCommand, "SELECT * FROM devices WHERE deviceId = '%s' AND dpId='%d';", deviceId, dpId);
    Sql_Query(sqlCommand, row) {
        dpInfo->id = dpId;
        char* value = sqlite3_column_text(row, 3);
        if (strcmp(value, "true") == 0) {
            dpInfo->value = 1;
        } else if (strcmp(value, "false") == 0) {
            dpInfo->value = 0;
        } else {
            dpInfo->value = strtod(value, NULL);
        }
        strcpy(dpInfo->deviceId, sqlite3_column_text(row, 0));
        strcpy(dpInfo->addr, sqlite3_column_text(row, 2));
        rowCount = 1;
    }
    return rowCount;
}

int Db_FindDpByAddr(dp_info_t* dpInfo, const char* dpAddr) {
    int rowCount = 0;
    char sqlCommand[100];
    sprintf(sqlCommand, "SELECT * FROM devices WHERE address='%s' ORDER BY dpId LIMIT 1;", dpAddr);
    Sql_Query(sqlCommand, row) {
        dpInfo->id = atoi(sqlite3_column_text(row, 1));
        char* value = sqlite3_column_text(row, 3);
        if (strcmp(value, "true")) {
            dpInfo->value = 1;
        } else if (strcmp(value, "false")) {
            dpInfo->value = 0;
        } else {
            dpInfo->value = strtod(value, NULL);
        }
        strcpy(dpInfo->deviceId, sqlite3_column_text(row, 0));
        strcpy(dpInfo->addr, sqlite3_column_text(row, 2));
        rowCount = 1;
    }
    return rowCount;
}

int Db_SaveDpValue(const char* dpAddr, int dpId, double value) {
    char sqlCmd[200];
    sprintf(sqlCmd, "UPDATE devices SET dpValue='%f' WHERE address='%s' AND dpId=%d", value, dpAddr, dpId);
    Sql_Exec(sqlCmd);
    return 1;
}

int Db_LoadSceneToRam() {
    g_sceneCount = 0;
    char* sqlCmd = "SELECT * FROM scene_inf";
    JSON* sceneInfo = JSON_CreateObject();
    Sql_Query(sqlCmd, row) {
        g_sceneList = realloc(g_sceneList, (g_sceneCount + 1) * sizeof(Scene));
        g_sceneList[g_sceneCount].isLocal = sqlite3_column_int(row, 1);
        g_sceneList[g_sceneCount].isEnable = sqlite3_column_int(row, 2);
        g_sceneList[g_sceneCount].runningActionIndex = -1;
        StringCopy(g_sceneList[g_sceneCount].id, sqlite3_column_text(row, 0));
        g_sceneList[g_sceneCount].type = atoi(sqlite3_column_text(row, 4));

        // Load actions
        char* actions = sqlite3_column_text(row, 5);
        JSON* actionsArray = JSON_Parse(actions);
        int actionCount = 0;
        JSON_ForEach(act, actionsArray) {
            SceneAction* action = &g_sceneList[g_sceneCount].actions[actionCount];
            action->actionType = JSON_GetNumber(act, "actionType");
            action->delaySeconds = JSON_GetNumber(act, "delaySeconds");
            StringCopy(action->entityId, JSON_GetText(act, "entityId"));
            if (action->actionType == EntityDevice) {
                StringCopy(action->pid, JSON_GetText(act, "pid"));
                StringCopy(action->dpAddr, JSON_GetText(act, "dpAddr"));
                action->dpId = JSON_GetNumber(act, "dpId");
                action->dpValue = JSON_GetNumber(act, "dpValue");
            }
            actionCount++;
        }
        g_sceneList[g_sceneCount].actionCount = actionCount;

        // Load conditions
        JSON* conditionsArray = JSON_Parse(sqlite3_column_text(row, 6));
        int conditionCount = 0;
        JSON_ForEach(condition, conditionsArray) {
            SceneCondition* cond = &g_sceneList[g_sceneCount].conditions[conditionCount];
            cond->timeReached = 0;
            cond->conditionType = JSON_GetNumber(condition, "conditionType");
            cond->repeat = JSON_GetNumber(condition, "repeat");
            cond->schMinutes = JSON_GetNumber(condition, "schMinutes");
            StringCopy(cond->entityId, JSON_GetText(condition, "entityId"));
            if (cond->conditionType == EntityDevice) {
                StringCopy(cond->pid, JSON_GetText(condition, "pid"));
                StringCopy(cond->dpAddr, JSON_GetText(condition, "dpAddr"));
                StringCopy(cond->expr, "==");
                cond->dpId = JSON_GetNumber(condition, "dpId");
                cond->dpValue = JSON_GetNumber(condition, "dpValue");
            }
            conditionCount++;
            if (conditionCount > 1000) {
                conditionCount = 0;
            }
        }
        g_sceneList[g_sceneCount].conditionCount = conditionCount;
        g_sceneCount++;
        JSON_Delete(actionsArray);
        JSON_Delete(conditionsArray);
    }
    myLogInfo("Loaded %d scenes from database", g_sceneCount);
    return 1;
}

int Db_SaveSceneCondRepeat(const char* sceneId, int conditionIndex, uint8_t repeat) {
    char* sqlCmd[200];
    char* conditions = NULL;
    sprintf(sqlCmd, "SELECT conditions FROM scene_inf WHERE sceneId='%s'", sceneId);
    Sql_Query(sqlCmd, row) {
        conditions = sqlite3_column_text(row, 0);
    }
    if (conditions) {
        JSON* conditionsArray = JSON_Parse(conditions);
        int i = 0;
        JSON_ForEach(condition, conditionsArray) {
            if (i == conditionIndex) {
                JSON_SetNumber(condition, "repeat", repeat);
                break;
            }
            i++;
        }
        conditions = cJSON_PrintUnformatted(conditionsArray);
        sprintf(sqlCmd, "UPDATE scene_inf SET conditions='%s')", conditions);
        Sql_Exec(sqlCmd);
        Db_LoadSceneToRam();
        free(conditions);
        JSON_Delete(conditionsArray);
    }
}

int Db_AddScene(JSON* sceneInfo) {
    char* sceneId = JSON_GetText(sceneInfo, "id");
    char* name = JSON_GetText(sceneInfo, "name");
    int state = JSON_GetNumber(sceneInfo, "state");
    int isLocal = JSON_GetNumber(sceneInfo, "isLocal");
    char* sceneType = JSON_GetText(sceneInfo, "sceneType");
    JSON* actionsObj = JSON_GetObject(sceneInfo, "actions");
    JSON* conditionsObj = JSON_GetObject(sceneInfo, "conditions");
    char* actions = cJSON_PrintUnformatted(actionsObj);
    char* conditions = cJSON_PrintUnformatted(conditionsObj);
    char* sqlCmd = malloc(StringLength(actions) + StringLength(conditions) + 200);
    sprintf(sqlCmd, "INSERT INTO SCENE_INF(sceneId, name, state, isLocal, sceneType, actions, conditions)  \
                                    VALUES('%s',    '%s', '%d',  '%d',    '%s',      '%s',    '%s')",
                                           sceneId, name, state, isLocal, sceneType, actions, conditions);
    Sql_Exec(sqlCmd);
    Db_LoadSceneToRam();
    free(sqlCmd);
    free(actions);
    free(conditions);
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

int Db_EnableScene(const char* sceneId, int enableOrDisable) {
    char sqlCmd[100];
    sprintf(sqlCmd, "UPDATE scene_inf SET state=%d WHERE sceneId = '%s'", enableOrDisable, sceneId);
    Sql_Exec(sqlCmd);
    Db_LoadSceneToRam();
    return 1;
}

int Db_AddDeviceHistory(JSON* packet) {
    long long time = timeInMilliseconds();
    uint8_t causeType  = JSON_GetNumber(packet, "causeType");
    char*   causeId    = JSON_GetText(packet, "causeId");
    uint8_t statusType = JSON_GetNumber(packet, "statusType");
    char*   deviceId   = JSON_GetText(packet, "deviceId");
    uint8_t dpId       = JSON_GetNumber(packet, "dpId");
    uint16_t dpValue    = JSON_GetNumber(packet, "dpValue");
    char sqlCmd[500];
    sprintf(sqlCmd, "INSERT INTO device_histories(time  , causeType, causeId, statusType, deviceId, dpId, dpValue) \
                                           VALUES('%lld', %d       , '%s'   , %d        , '%s'    , %d  , %d     )",
                                                  time  , causeType, causeId, statusType, deviceId, dpId, dpValue);
    Sql_Exec(sqlCmd);
    return 1;
}

JSON* Db_FindDeviceHistories(long long startTime, long long endTime, const char* deviceId, int dpId, int pageIndex) {
    JSON* histories = JSON_CreateObject();
    char sqlCmd[500];
    int limit = 50;
    char dpIdCondition[50] = "";
    if (dpId >= 0) {
        sprintf(dpIdCondition, "AND dpId=%d", dpId);
    }
    int offset = pageIndex * limit;
    int count = 0;
    // get number of rows
    sprintf(sqlCmd, "SELECT count(*) FROM device_histories WHERE time >= %lld AND time <= %lld AND deviceId='%s' %s", startTime, endTime, deviceId, dpIdCondition);
    Sql_Query(sqlCmd, row) {
        count = sqlite3_column_int(row, 0);
    }
    JSON_SetNumber(histories, "totalRows", count);
    JSON_SetNumber(histories, "pageIndex", pageIndex);
    JSON_SetNumber(histories, "pageCount", ceil((float)count / limit));
    JSON* rows = JSON_AddArrayToObject(histories, "rows");
    // get row details
    sprintf(sqlCmd, "SELECT * FROM device_histories WHERE time >= %lld AND time <= %lld AND deviceId='%s' %s ORDER BY time DESC LIMIT %d OFFSET %d", startTime, endTime, deviceId, dpIdCondition, limit, offset);
    {Sql_Query(sqlCmd, row) {
        JSON* r = JSON_ArrayAddObject(rows);
        JSON_SetNumber(r, "id",         sqlite3_column_int(row, 0));
        JSON_SetNumber(r, "time",       sqlite3_column_int(row, 1));
        JSON_SetNumber(r, "causeType",  sqlite3_column_int(row, 2));
        JSON_SetText  (r, "causeId",    sqlite3_column_text(row, 3));
        JSON_SetNumber(r, "statusType", sqlite3_column_int(row, 4));
        JSON_SetText  (r, "deviceId",   sqlite3_column_text(row, 5));
        JSON_SetNumber(r, "dpId",       sqlite3_column_int(row, 6));
        JSON_SetNumber(r, "dpValue",    sqlite3_column_int(row, 7));
    }}
    return histories;
}

int sql_insert_data_table(sqlite3 **db,char *sql)
{
    char *err_msg = 0;
    // sqlite3_open(VAR_DATABASE, db);
    int rc = sqlite3_exec(*db,sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        sqlite3_free(err_msg);
        // sqlite3_close(*db);
        return -1;
    }
    sql = NULL;
    free(sql);
    // sqlite3_close(*db);
    return 0;
}

int sql_creat_table(sqlite3 **db,char *name_table)
{
    char *err_msg;
    // sqlite3_open(VAR_DATABASE,db);
    int rc = sqlite3_exec(*db, name_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
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
    check = sql_creat_table(db,"DROP TABLE IF EXISTS GATEWAY;CREATE TABLE GATEWAY(address TEXT,appkey TEXT,ivIndex TEXT,netkeyIndex TEXT,netkey TEXT,appkeyIndex TEXT,deviceKey TEXT);");
    if(check != 0)
    {
        printf("DELETE GATEWAY is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS DEVICES;CREATE TABLE DEVICES(deviceID TEXT,dpID TEXT,address TEXT,dpValue TEXT,state INTEGER);");
    if(check != 0)
    {
        printf("DELETE DEVICES is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS DEVICES_INF;CREATE TABLE DEVICES_INF(deviceID TEXT,state INTEGER,name TEXT,MAC TEXT,Unicast TEXT,IDgateway TEXT,deviceKey TEXT,provider INTEGER,pid TEXT,created INTEGER,modified INTEGER,last_updated INTEGER,firmware TEXT,GroupList TEXT,SceneList TEXT);");
    if(check != 0)
    {
        printf("DELETE DEVICES_INF is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS GROUP_INF;CREATE TABLE GROUP_INF(groupAdress TEXT,state INTEGER,name TEXT,pid TEXT,devices TEXT);");
    if(check != 0)
    {
        printf("DELETE GROUP_INF is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db,"DROP TABLE IF EXISTS SCENE_INF;CREATE TABLE SCENE_INF(sceneId TEXT,isLocal INTEGER,state INTEGER,name TEXT,sceneType TEXT,actions TEXT,conditions TEXT,created INTEGER,last_updated INTEGER);");
    if(check != 0)
    {
        printf("DELETE SCENE_INF is error!\n");
        return false;
    }
    usleep(100);
    check = sql_creat_table(db, "DROP TABLE IF EXISTS DEVICE_HISTORIES;CREATE TABLE DEVICE_HISTORIES(id INTEGER PRIMARY KEY AUTOINCREMENT, time INTEGER, causeType INTEGER, causeId TEXT, statusType INTEGER, deviceId TEXT, dpId INTEGER, dpValue REAL); CREATE INDEX device_histories_time_device_id ON DEVICE_HISTORIES(time, deviceId);");
    if(check != 0)
    {
        printf("DELETE SCENE_INF is error!\n");
        return false;
    }
    return true;
}

bool creat_table_database_log(sqlite3 **db)
{
    int check = 0;
    check = sql_creat_table(db,"DROP TABLE IF EXISTS DEVICE_LOG;CREATE TABLE DEVICE_LOG(deviceID TEXT,dpID TEXT,dpValue TEXT,TimeCreat INTEGER);");
    if(check != 0)
    {
        return false;
    }
    return true;
}

bool sql_insertDataTableGateway(sqlite3 **db,const char* address,const char* appkey,const char* ivIndex,const char* netkeyIndex,const char* netkey,const char* appkeyIndex,const char* deviceKey)
{
    int check = 0;
    char *sql =(char*) malloc(1024 * sizeof(char));
    sprintf(sql,"INSERT INTO GATEWAY VALUES(\'%s\',\'%s\',\'%s\',\'%s\',\'%s\',\'%s\',\'%s\');",address,appkey,ivIndex,netkeyIndex,netkey,appkeyIndex,deviceKey);
    check = sql_insert_data_table(db,sql);

    sql = NULL;
    free(sql);
    if(check == -1)
    {
        return false;
    }
    return true;
}

bool sql_insertDataTableDevices(sqlite3 **db,const char* deviceID,const char* dpID,const char* address,const char* dpvalue,int state)
{
    int check = 0;
    char *sql =(char*) malloc(1024 * sizeof(char));
    sprintf(sql,"INSERT INTO DEVICES VALUES(\'%s\',\'%s\',\'%s\',\'%s\',%d);",deviceID,dpID,address,dpvalue,state);
    check = sql_insert_data_table(db,sql);
    sql = NULL;
    free(sql);
    if(check == -1)
    {
        return false;
    }
    return true;
}

bool sql_insertDataTableDevicesInf(sqlite3 **db,const char* deviceID,int status,const char* name,
    const char* MAC,const char* Unicast,const char* IDgateway,const char* deviceKey,
    int provider,const char* pid,int created,
    int modified,int last_updated,const char* firmware,
    const char* GroupList,const char* SceneList)
{
    int check = 0;
    char *sql =(char*) malloc(1024 * sizeof(char));
    sprintf(sql,"INSERT INTO DEVICES_INF VALUES(\'%s\',%d,\'%s\',\'%s\',\'%s\',\'%s\',\'%s\',%d,\'%s\',%d,%d,%d,\'%s\',\'%s\',\'%s\');",deviceID,status,name,MAC,Unicast,IDgateway,deviceKey,provider,pid,created,modified,last_updated,firmware,GroupList,SceneList);
    check = sql_insert_data_table(db,sql);

    sql = NULL;
    free(sql);
    if(check == -1)
    {
        return false;
    }
    return true;
}

bool sql_insertDataTableGroupInf(sqlite3 **db,const char* groupAdress, int state,const char* name,const char* pid,const char* devices)
{

    int check = 0;
    char *sql =(char*) malloc(10000 * sizeof(char));
    sprintf(sql,"INSERT INTO GROUP_INF VALUES(\'%s\',%d, \'%s\',\'%s\',\'%s\');",groupAdress,state,name,pid,devices);
    check = sql_insert_data_table(db,sql);
    sql = NULL;
    free(sql);
    if(check == -1)
    {
        return false;
    }
    return true;
}

bool sql_insertDataTableSceneInf(sqlite3 **db,const char* sceneId,int isLocal, int state,const char* name,const char* sceneType,const char* actions,const char* conditions, int created, int last_updated)
{

    int check = 0;
    char *sql =(char*) malloc(10000 * sizeof(char));
    sprintf(sql,"INSERT INTO SCENE_INF VALUES(\'%s\',%d, %d, \'%s\',\'%s\',\'%s\',\'%s\',%d,%d);",sceneId,isLocal,state,name,sceneType,actions,conditions,created,last_updated);
    check = sql_insert_data_table(db,sql);
    sql = NULL;
    free(sql);
    if(check == -1)
    {
        return false;
    }
    return true;

}

bool sql_insertDataTableDeviceLog(sqlite3 **db,const char* deviceID,const char* dpID,const char* dpvalue,long long TimeCreat)
{
    if(deviceID == NULL || dpID == NULL || dpvalue == NULL)
    {
        return false;
    }
    int check = 0;
    char *sql =(char*) malloc(10000 * sizeof(char));
    sprintf(sql,"INSERT INTO DEVICE_LOG VALUES(\'%s\',\'%s\',\'%s\',%lld);",deviceID,dpID,dpvalue,TimeCreat);
    printf("sql %s\n",sql );
    check = sql_insert_data_table(db,sql);
    sql = NULL;
    free(sql);
    if(check == -1)
    {
        return false;
    }
    return true;
}

bool sql_updateStateDeviceTableDevices(sqlite3 **db,const char *deviceID,int state)
{
    if(deviceID == NULL)
    {
        return false;
    }
    char *err_msg = 0;
    char *sql = (char*) malloc(1024 * sizeof(char));
    sprintf(sql,"UPDATE DEVICES SET %s = '%d' WHERE %s = '%s';",KEY_STATE,state,KEY_DEVICE_ID,deviceID);
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("err_msg =  %s\n",err_msg );
        free(sql);
        sqlite3_free(err_msg);
        return false;
    }
    free(sql);
    return true;
}

bool sql_updateStateDeviceTableDevicesInf(sqlite3 **db,const char *deviceID,int state)
{
    if(deviceID == NULL)
    {
        return false;
    }
    char *err_msg = 0;
    char *sql = (char*) malloc(1024 * sizeof(char));
    sprintf(sql,"UPDATE DEVICES_INF SET %s = '%d' WHERE %s = '%s';",KEY_STATE,state,KEY_DEVICE_ID,deviceID);
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("err_msg =  %s\n",err_msg );
        free(sql);
        sqlite3_free(err_msg);
        return false;
    }
    free(sql);
    return true;
}

bool sql_updateStateInTableWithCondition(sqlite3 **db,const char *name_table,const char *key,int state,const char *key_condition,const char *value_condition)
{
    if(key == NULL || key_condition == NULL || value_condition == NULL)
    {
        return false;
    }
    char *err_msg = 0;
    char *sql = (char*) malloc(1024 * sizeof(char));
    sprintf(sql,"UPDATE %s SET %s = '%d' WHERE %s = '%s';",name_table,key,state,key_condition,value_condition);
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("err_msg =  %s\n",err_msg );
        free(sql);
        sqlite3_free(err_msg);
        return false;
    }
    free(sql);
    return true;
}

bool sql_updateStateInTableWithMultileCondition(sqlite3 **db,const char *name_table,const char *key,int state,const char *key_condition_1,const char *value_condition_1,const char *key_condition_2,const char *value_condition_2)
{
    if(key == NULL || key_condition_1 == NULL || value_condition_1 == NULL || key_condition_2 == NULL || value_condition_2 == NULL)
    {
        return false;
    }
    char *err_msg = 0;
    char *sql = (char*) malloc(1024 * sizeof(char));
    sprintf(sql,"UPDATE %s SET %s = '%d' WHERE %s = '%s' AND %s = '%s';",name_table,key,state,key_condition_1,value_condition_1,key_condition_2,value_condition_2);
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("err_msg =  %s\n",err_msg );
        free(sql);
        sqlite3_free(err_msg);
        return false;
    }
    free(sql);
    return true;
}

bool sql_updateValueInTableWithCondition(sqlite3 **db,const char *name_table,const char *key,const char *value,const char *key_condition,const char *value_condition)
{
    if(key == NULL || value == NULL || key_condition == NULL || value_condition == NULL)
    {
        return false;
    }

    size_t leng = strlen(value) + strlen(value_condition) + 300;


    char *err_msg = 0;
    char *sql = (char*) calloc(leng,sizeof(char));
    sprintf(sql,"UPDATE %s SET %s = '%s' WHERE %s = '%s';",name_table,key,value,key_condition,value_condition);
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("err_msg =  %s\n",err_msg );
        free(sql);
        sqlite3_free(err_msg);
        return false;
    }
    free(sql);
    return true;
}

bool sql_updateValueInTableWithMultileCondition(sqlite3 **db,const char *name_table,const char *key,const char *value,const char *key_condition_1,const char *value_condition_1,const char *key_condition_2,const char *value_condition_2)
{
    if(key == NULL || value == NULL || key_condition_1 == NULL || value_condition_1 == NULL || key_condition_2 == NULL || value_condition_2 == NULL)
    {
        return false;
    }
    char *err_msg = 0;
    char *sql = (char*) malloc(1024 * sizeof(char));
    sprintf(sql,"UPDATE %s SET %s = '%s' WHERE %s = '%s' AND %s = '%s';",name_table,key,value,key_condition_1,value_condition_1,key_condition_2,value_condition_2);
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("err_msg =  %s\n",err_msg );
        free(sql);
        sqlite3_free(err_msg);
        return false;
    }
    free(sql);
    return true;
}


bool sql_deleteDeviceInTable(sqlite3 **db ,const char *name_table,const char *key,const char *value)
{
    // int check = 0;
    char *sql =(char*) malloc(100 * sizeof(char));
    sprintf(sql,"DELETE FROM %s WHERE %s = (\"%s\");",name_table,key,value);
    char *err_msg = 0;
    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK )
    {
        printf("error : rc != SQLITE_OK\n");
        sqlite3_free(err_msg);
        return false;
    }
    sql = NULL;
    free(sql);
    return true;
}



char **sql_getValueWithMultileCondition(sqlite3 **db, int *leng_t,char *object, const char *name_table,const char *key1,const char *value1,const char *key2,const char *value2)
{
    sqlite3_stmt *res;
    int rc = 0,i = 0,size = 0;

    char *sql = (char*) malloc(1500 * sizeof(char));
    sprintf(sql,"SELECT COUNT(*) FROM %s WHERE %s = '%s' AND %s = '%s';",name_table,key1,value1,key2,value2);
    sqlite3_prepare_v2(*db,sql, -1, &res, NULL);
    sqlite3_bind_int (res, 1, 2);
    if (sqlite3_step(res) != SQLITE_DONE)
    {
        size = sqlite3_column_int(res,0);
    }
    sqlite3_finalize(res);
    *leng_t = size;

    if(size == 0)
    {
        free(sql);
        return  NULL;
    }
    char **result = generate_fields(size,10000);
    if(*result == NULL)
    {
        printf("Failed to malloc for **result\n");
    }
    sprintf(sql,"SELECT %s FROM %s WHERE %s = '%s' AND %s = '%s';",object,name_table,key1,value1,key2,value2);

    rc = sqlite3_prepare_v2(*db, sql, -1, &res, NULL);
    if (rc == SQLITE_OK)
    {
        while (sqlite3_step(res) == SQLITE_ROW)
        {
            memcpy(result[i],(char *)sqlite3_column_text(res,0),strlen((char *)sqlite3_column_text(res,0))+1);
            ++i;
        }
    }
    free(sql);
    sqlite3_finalize(res);
    return result;
}

char ** sql_getValueWithCondition(sqlite3 **db, int *leng_t,char *object,const char *name_table,const char *key,const char *value)
{
    int rc = 0,i = 0,size = 0;
    sqlite3_stmt *res;
    char *sql = (char*) calloc(500,sizeof(char));

    sprintf(sql,"SELECT COUNT(*) FROM %s WHERE %s = '%s';", name_table, key, value);
    sqlite3_prepare_v2(*db,sql, -1, &res, NULL);
    sqlite3_bind_int (res, 1, 2);
    if (sqlite3_step(res) != SQLITE_DONE)
    {
        size = sqlite3_column_int(res,0);
    }
    *leng_t = size;
    if(size == 0)
    {
        sqlite3_finalize(res);
        free(sql);
        return  NULL;
    }

    char **result = generate_fields(size,10000);
    if(*result == NULL)
    {
        printf("Failed to malloc for **result\n");
    }
    sprintf(sql,"SELECT %s FROM %s WHERE %s = '%s';",object,name_table,key,value);

    rc = sqlite3_prepare_v2(*db, sql, -1, &res, NULL);
    if (rc == SQLITE_OK)
    {
        while (sqlite3_step(res) == SQLITE_ROW)
        {
            memcpy(result[i],(char *)sqlite3_column_text(res,0),strlen((char *)sqlite3_column_text(res,0))+1);
            ++i;
        }
    }
    free(sql);
    sqlite3_finalize(res);

    return result;
}


int sql_getNumberWithCondition(sqlite3 **db, int *leng_t,char *object,const char *name_table,const char *key,const char *value)
{
    int result = -1;
    // char *tmp;
    sqlite3_stmt *res;
    int rc = 0,size = 0;

    char *sql = (char*) malloc(10000 * sizeof(char));
    sprintf(sql,"SELECT COUNT(*) FROM %s WHERE %s = '%s';",name_table,key,value);
    sqlite3_prepare_v2(*db,sql, -1, &res, NULL);
    sqlite3_bind_int (res, 1, 2);
    if (sqlite3_step(res) != SQLITE_DONE)
    {
        size = sqlite3_column_int(res,0);
    }
    sqlite3_finalize(res);
    *leng_t = size;
    if(size == 0)
    {
        free(sql);
        return  -1;
    }


    sprintf(sql,"SELECT %s FROM %s WHERE %s = '%s';",object,name_table,key,value);
    rc = sqlite3_prepare_v2(*db, sql, -1, &res, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_int (res, 1, 2);
        while (sqlite3_step(res) != SQLITE_DONE)
        {
            result = sqlite3_column_int(res,0);
        }
    }
    sqlite3_finalize(res);
    free(sql);
    return result;
}

int sql_getNumberRowWithCondition(sqlite3 **db,const char *name_table,const char *key,const char *value)
{
    int result = -1;
    char *sql = (char*) malloc(100 * sizeof(char));
    sprintf(sql,"SELECT COUNT(*) FROM %s WHERE %s = '%s';",name_table,key,value);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        result = sqlite3_column_int(stmt,0);
        printf("result %d\n",result );
    }
    sqlite3_finalize(stmt);
    sql = NULL;
    free(sql);
    return result;
}

int sql_getCountColumnWithCondition(sqlite3 **db,char *name_table,char *key_1,char *value_1,char *key_2,char *value_2)
{
    int result = 0;
    char *sql = (char*) malloc(100 * sizeof(char));
    sprintf(sql,"SELECT COUNT(*) FROM %s WHERE %s = '%s' AND %s = '%s';",name_table,key_1,value_1,key_2,value_2);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        result = sqlite3_column_int(stmt,0);
    }
    sqlite3_finalize(stmt);
    sql = NULL;
    free(sql);
    return result;
}
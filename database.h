#ifndef DATABASE_H
#define DATABASE_H

#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <unistd.h> /* read, write, close */
#include <string.h> /* memcpy, memset */
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "define.h"
#include "sqlite3.h"
#include "core_process_t.h"
#include "cJSON.h"

typedef struct {
    uint8_t id;
    char deviceId[50];
    char addr[10];
    double value;
} dp_info_t;

typedef struct {
    char id[50];
    int  state;
    char addr[10];
    char gatewayId[10];
    int provider;
    char pid[20];
} device_info_t;

typedef enum {
    SceneTypeManual = 0,
    SceneTypeOneOfConds = 1,
    SceneTypeAllConds = 2
} SceneType;

typedef enum {
    EntityDevice,
    EntityScene,
    EntityDelay,
    EntitySchedule
} EntityType;

typedef enum {
    NO_REPEAT  = 0x00,
    REPEAT_SAT = 0x01,
    REPEAT_FRI = 0x02,
    REPEAT_THU = 0x04,
    REPEAT_WED = 0x08,
    REPEAT_TUE = 0x10,
    REPEAT_MON = 0x20,
    REPEAT_SUN = 0x40
} RepeatType;

typedef struct {
    EntityType      actionType;     // Thiết bị, kịch bản hay độ trễ
    char            entityId[50];   // Id của thiết bị hoặc kịch bản cần thực hiện
    char            entityAddr[10]; // Address của thiết bị hoặc kịch bản cần thực hiện
    char            pid[50];        // PID của thiết bị nếu entityType = Thiết bị
    int             dpId;           // Id của dp nếu entityType = Thiết bị
    char            dpAddr[10];     // Address của dp nếu entityType = Thiết bị
    double          dpValue;        // dpValue của thiết bị nếu entityType = Thiết bị
    int             delaySeconds;   // Giá trị thời gian trễ tính bằng giây nếu entityType = Độ trễ
} SceneAction;

typedef struct {
    EntityType      conditionType;  // Thiết bị hoặc đặt lịch
    char            entityId[50];   // Id của thiết bị nếu entityType = Thiết bị
    char            entityAddr[10]; // Address của thiết bị nếu entityType = Thiết bị
    char            pid[50];        // PID của thiết bị nếu entityType = Thiết bị
    int             dpId;           // Id của dp nếu entityType = Thiết bị
    char            dpAddr[10];     // Address của dp nếu entityType = Thiết bị
    double          dpValue;        // Value của dp cần kiểm tra nếu entityType = Thiết bị
    char            expr[5];        // Biểu thức so sánh để so sánh giá trị thực tế của dp và dpValue để quyết định xem điều kiện có thỏa mãn hay không
    int             schMinutes;     // Thời điểm trong ngày cần thực hiện nếu entityType = đặt lịch
    uint8_t         repeat;         // Danh sách thứ trong tuần cần lặp lại nếu entityType = đặt lịch
    uint8_t         timeReached;    // Private variable for conditionType = EntitySchedule (0: this condition is not satisfied, 1: this condition is satified)
} SceneCondition;

typedef struct {
    char            id[30];
    bool            isLocal;
    int             isEnable;
    SceneType       type;
    SceneAction     actions[200];
    int             actionCount;
    SceneCondition  conditions[50];
    int             conditionCount;
    int             effectFrom;          // Effective start time (minutes)
    int             effectTo;            // Effective end time (minutes)
    uint8_t         effectRepeat;
    int             runningActionIndex;  // Private variable to store the index of action that is executing (-1 means scene is idle)
    long long       delayStart;          // Private variable to store the time when starting the DELAY action
} Scene;

extern Scene* g_sceneList;
extern int g_sceneCount;

#define Sql_Query(sqlCmd, row)    int rc = 0; sqlite3_stmt *row; rc = sqlite3_prepare_v2(db, sqlCmd, -1, &row, NULL); while (sqlite3_step(row) == SQLITE_ROW)
#define Sql_Exec(sqlCmd)    { char *err_msg; if (sqlite3_exec(db, sqlCmd, 0, 0, &err_msg) != SQLITE_OK) { myLogError("SQL Error: %s, %s", sqlCmd, err_msg); sqlite3_free(err_msg); return 0; }}

int Db_FindDeviceBySql(device_info_t* deviceInfo, const char* sqlCommand);
int Db_FindDevice(device_info_t* deviceInfo, const char* deviceId);
int Db_FindDeviceByAddr(device_info_t* deviceInfo, const char* deviceAddr);
int Db_SaveDeviceState(const char* deviceId, int state);
int Db_DeleteDevice(const char* deviceId);

int Db_AddGroup(const char* groupAddr, const char* groupName, const char* devices, bool isLight);
int Db_DeleteGroup(const char* groupAddr);
char* Db_FindDevicesInGroup(const char* groupAddr);

int Db_FindDp(dp_info_t* dpInfo, const char* deviceId, int dpId);
int Db_FindDpByAddr(dp_info_t* dpInfo, const char* dpAddr);
int Db_SaveDpValue(const char* dpAddr, double value);

int Db_LoadSceneToRam();
int Db_SaveSceneCondRepeat(const char* sceneId, int conditionIndex, uint8_t repeat);
int Db_AddScene(JSON* sceneInfo);
JSON* Db_FindScene(const char* sceneId);
int Db_DeleteScene(const char* sceneId);
int Db_EnableScene(const char* sceneId, int enableOrDisable);

int Db_AddDeviceHistory(JSON* packet);
JSON* Db_FindDeviceHistories(long long startTime, long long endTime, const char* deviceId, int dpId, int pageIndex);

bool open_database(const char *filename, sqlite3 **db);
bool close_database(sqlite3 **db);
void printColumnValue(sqlite3_stmt* stmt, int col);
void sql_print_a_table(sqlite3 **db,char *name_table);
void print_database(sqlite3 **db);
int sql_insert_data_table(sqlite3 **db,char *sql);
int sql_creat_table(sqlite3 **db,char *name_table);
bool creat_table_database(sqlite3 **db);
bool creat_table_database_log(sqlite3 **db);

bool sql_insertDataTableGateway(sqlite3 **db,const char* address,const char* appkey,const char* ivIndex,const char* netkeyIndex,const char* netkey,const char* appkeyIndex,const char* deviceKey);
bool sql_insertDataTableDevices(sqlite3 **db,const char* deviceID,const char* dpID,const char* address,const char* dpvalue,int state);
bool sql_insertDataTableDevicesInf(sqlite3 **db,const char* deviceID,int status,const char* name,const char* MAC,const char* Unicast,const char* IDgateway,const char* deviceKey,int provider,const char* pid,int created,int modified,int last_updated,const char* firmware,const char* GroupList,const char* SceneList);
bool sql_insertDataTableGroupInf(sqlite3 **db,const char* groupAdress, int state,const char* name,const char* pid,const char* devices);
bool sql_insertDataTableSceneInf(sqlite3 **db,const char* sceneId,int isLocal, int state,const char* name,const char* sceneType,const char* actions,const char* conditions, int created, int last_updated);
bool sql_insertDataTableDeviceLog(sqlite3 **db,const char* deviceID,const char* dpID,const char* dpvalue,long long TimeCreat);

bool sql_updateStateDeviceTableDevices(sqlite3 **db,const char *deviceID,int state);
bool sql_updateStateDeviceTableDevicesInf(sqlite3 **db,const char *deviceID,int state);

bool sql_updateStateInTableWithCondition(sqlite3 **db,const char *name_table,const char *key,int state,const char *key_condition,const char *value_condition);
bool sql_updateStateInTableWithMultileCondition(sqlite3 **db,const char *name_table,const char *key,int state,const char *key_condition_1,const char *value_condition_1,const char *key_condition_2,const char *value_condition_2);

bool sql_updateValueInTableWithCondition(sqlite3 **db,const char *name_table,const char *key,const char *value,const char *key_condition,const char *value_condition);
bool sql_updateValueInTableWithMultileCondition(sqlite3 **db,const char *name_table,const char *key,const char *value,const char *key_condition_1,const char *value_condition_1,const char *key_condition_2,const char *value_condition_2);

bool sql_deleteDeviceInTable(sqlite3 **db ,const char *name_table,const char *key,const char *value);

char **sql_getValueWithMultileCondition(sqlite3 **db, int *leng_t,char *object, const char *name_table,const char *key1,const char *value1,const char *key2,const char *value2);
char **sql_getValueWithCondition(sqlite3 **db, int *leng_t,char *object,const char *name_table,const char *key,const char *value);

int sql_getNumberWithCondition(sqlite3 **db, int *leng_t,char *object,const char *name_table,const char *key,const char *value);
int sql_getNumberRowWithCondition(sqlite3 **db,const char *name_table,const char *key,const char *value);
int sql_getCountColumnWithCondition(sqlite3 **db,char *name_table,char *key_1,char *value_1,char *key_2,char *value_2);



#endif  // DATABASE_H
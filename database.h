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

typedef enum {
    GroupSwitch = 0,     // Nhóm công tắc (liên động mềm)
    GroupLight  = 1      // Nhóm đèn
} GroupType;

typedef enum {
    ValueTypeDouble = 0,
    ValueTypeString = 1
} ValueType;

typedef struct {
    uint8_t id;
    char deviceId[50];
    char addr[10];
    double value;
    char valueStr[100];
    int pageIndex;
} DpInfo;

typedef struct {
    char id[50];
    int  state;
    char addr[10];
    int  gwIndex;
    int  provider;
    char pid[20];
    int  pageIndex;
} DeviceInfo;

typedef enum {
    SceneTypeManual = 0,
    SceneTypeOneOfConds = 1,
    SceneTypeAllConds = 2
} SceneType;

typedef enum {
    EntityDevice,
    EntityScene,
    EntityGroup,
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
    uint8_t         dpIds[10];      // Danh sách Id của các dps nếu entityType = Thiết bị
    double          dpValues[10];   // dpValues của từng dp nếu entityType = Thiết bị
    int             dpCount;        // Số lượng dp cần điều khiển
    int             delaySeconds;   // Giá trị thời gian trễ tính bằng giây nếu entityType = Độ trễ
    char            serviceName[10];// Service điều khiển cho action BLE, TUYA, XIAOMI, HOMEKIT, chỉ áp dụng khi actionType = EntityDevice, EntityScene
    char            wifiCode[100];
} SceneAction;

typedef struct {
    EntityType      conditionType;  // Thiết bị hoặc đặt lịch
    char            entityId[50];   // Id của thiết bị nếu entityType = Thiết bị
    char            entityAddr[10]; // Address của thiết bị nếu entityType = Thiết bị
    char            pid[50];        // PID của thiết bị nếu entityType = Thiết bị
    int             dpId;           // Id của dp nếu entityType = Thiết bị
    char            dpAddr[10];     // Address của dp nếu entityType = Thiết bị
    ValueType       valueType;        // Value type of device : ValueTypeDouble = Switch, Light, Sensor; ValueTypeString = CameraHanet
    double          dpValue;        // Value của dp cần kiểm tra nếu entityType = Thiết bị và valueType = ValueTypeDouble
    char            dpValueStr[100];   // Value của dp cần kiểm tra nếu valueType = ValueTypeString
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
    int             pageIndex;
} Scene;

extern Scene* g_sceneList;
extern int g_sceneCount;

#define Sql_Query(sqlCmd, row)    int rc = 0; sqlite3_stmt *row; rc = sqlite3_prepare_v2(db, sqlCmd, -1, &row, NULL); while (sqlite3_step(row) == SQLITE_ROW)
#define Sql_Exec(sqlCmd)    { char *err_msg; if (sqlite3_exec(db, sqlCmd, 0, 0, &err_msg) != SQLITE_OK) { myLogError("SQL Error: %s, %s", sqlCmd, err_msg); sqlite3_free(err_msg); return 0; }}

int Db_AddGateway(JSON* gatewayInfo);
int Db_FindGatewayId(const char* gatewayAddr);

int Db_AddDevice(JSON* deviceInfo);
JSON* Db_GetAllDevices();
int Db_FindDeviceBySql(DeviceInfo* deviceInfo, const char* sqlCommand);
int Db_FindDevice(DeviceInfo* deviceInfo, const char* deviceId);
int Db_FindDeviceByAddr(DeviceInfo* deviceInfo, const char* deviceAddr);
int Db_SaveDeviceState(const char* deviceId, int state);
int Db_DeleteDevice(const char* deviceId);
int Db_DeleteAllDevices();

int Db_AddGroup(const char* groupAddr, const char* groupName, const char* devices, bool isLight, const char* pid, int pageIndex);
int Db_DeleteGroup(const char* groupAddr);
int Db_DeleteAllGroup();
char* Db_FindDevicesInGroup(const char* groupAddr);
int Db_GetGroupType(const char* groupAddr);
int Db_SaveGroupDevices(const char* groupAddr, const char* devices);

int Db_AddDp(const char* deviceId, int dpId, const char* addr, int pageIndex);
int Db_FindDp(DpInfo* dpInfo, const char* deviceId, int dpId);
int Db_FindDpByAddr(DpInfo* dpInfo, const char* dpAddr);
int Db_SaveDpValue(const char* deviceId, int dpId, double value);
int Db_SaveDpValueString(const char* deviceId, int dpId, const char* value);

int Db_LoadSceneToRam();
int Db_SaveSceneCondRepeat(const char* sceneId, int conditionIndex, uint8_t repeat);
int Db_AddScene(JSON* sceneInfo);
JSON* Db_FindScene(const char* sceneId);
int Db_DeleteScene(const char* sceneId);
int Db_DeleteAllScene();
int Db_EnableScene(const char* sceneId, int enableOrDisable);
int Db_RemoveSceneAction(const char* sceneId, const char* deviceAddr);

int Db_AddDeviceHistory(JSON* packet);
JSON* Db_FindDeviceHistories(long long startTime, long long endTime, const char* deviceId, char* dpIds, int causeType, int eventType, int limit);

bool open_database(const char *filename, sqlite3 **db);
bool close_database(sqlite3 **db);
int sql_creat_table(sqlite3 **db,char *name_table);
bool creat_table_database(sqlite3 **db);

#endif  // DATABASE_H
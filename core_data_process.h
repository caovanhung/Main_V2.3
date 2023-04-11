/////////////////////////////////LOG/////////////////////////////////////////
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "CoreDataProcess"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif
#define MQTT_LIB    "core-mqtt@" MQTT_LIBRARY_VERSION
/////////////////////////////////MQTT LOCAL/////////////////////////////////////////
#define MQTT_MOSQUITTO_CIENT_ID             "CoreData_f"
/////////////////////////////////MQTT LOCAL/////////////////////////////////////////

#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "define.h"
#include "database.h"
#include "sqlite3.h"
#include "parson.h"
#include "logging_stack.h"
#include "time_t.h"


/******** Struct to check response for requests ********/
/*
 * Each request that need to check response will be added in the response list.
 * Each response will be distinguished by request type and itemId (maybe group address, scene address,...).
 * When we received a feedback from lower service, we will update the status of each device in the response struct
 */
#define RESQUEST_MAX  100           // Maximum number of concurrent request
#define DEVICE_IN_RESQ_MAX  300     // Maximum number of devices in a request

typedef struct {
    uint16_t addr;
    uint8_t status;
} device_resp_t;

typedef struct {
    uint16_t reqType;
    uint16_t itemId;
    uint32_t createdTime;
    device_resp_t devices[DEVICE_IN_RESQ_MAX];
    uint16_t deviceNum;
} response_t;
/***********************************************************/


bool addNewDevice(sqlite3 **db, JSON* packet);
bool delDevice(sqlite3 **db,const char *deviceID);

char *delDeviceIntoGroups(sqlite3 **db,const char *deviceID);
char *delDeviceIntoScenes(sqlite3 **db,const char *deviceID);

bool addSceneInfoToDatabase(sqlite3 **db,const char *object_);
bool DeleteSceneInDatabase(sqlite3 **db,const char *sceneID);
bool deleteSceneListOfDeviceFromSceneID(sqlite3 **db,const char *sceneID);


bool addGateway(sqlite3 **db,const char *object_);
bool addNewGroupNormal(sqlite3 **db,const char *object_);
bool addNewGroupLink(sqlite3 **db,const char *object_);

bool getInfoDeviceFromDatabase(sqlite3 **db,char **result);
bool getInfoTypeSceneFromDatabase(sqlite3 **db,char **result,int isLocal);
bool getInfoSceneIdFromDatabase(sqlite3 **db,char **result,char *sceneID);
bool getInfoGroupFromDatabase(sqlite3 **db,char **result,char *groupAddress);
bool checkTypeSceneWithInfoIntoDatabase(sqlite3 **db,char *sceneID);
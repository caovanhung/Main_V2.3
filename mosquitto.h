#ifndef MY_MOSQUITTO
#define MY_MOSQUITTO

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>     // string function definitions
#include <fcntl.h>  // File control definitions
#include <errno.h>  // Error number definitions
#include <termios.h>    // POSIX terminal control definitionss
#include <sys/time.h>   // time calls
#include <sys/un.h>

#include <unistd.h> /* read, write, close */
#include <netdb.h> /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <sys/un.h>
#include <assert.h>
#include "core_process_t.h"
#include "define.h"
#include "parson.h"
#include "helper.h"
#include "database.h"
#include "cJSON.h"
#include "helper.h"

typedef struct  
{
	char *layer_service;
	char *name_service;
	char *type_action;
	char *extend;
}Info_topic_mosquitto;

typedef struct {
    int             reqType;
    DeviceInfo*  deviceInfo;
    DpInfo       dps[20];
    size_t          dpCount;
} local_packet_t;

#define sendToService(serviceToSend, typeAction, payload)  g_dbgFileName = FILENAME; g_dbgLineNumber =  __LINE__; sendToServiceFunc(mosq, serviceToSend, typeAction, payload);
bool sendToServiceFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, const char* payload);

#define sendPacketTo(serviceToSend, typeAction, packet)  g_dbgFileName = FILENAME; g_dbgLineNumber =  __LINE__; sendPacketToFunc(mosq, serviceToSend, typeAction, packet);
bool sendPacketToFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, JSON* packet);


/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool get_inf_topic_mos(Info_topic_mosquitto* inf_topic_mos,char *topic);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool free_Info_topic_mosquitto(Info_topic_mosquitto* inf_topic_mos);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool get_topic(char **result_topic,const char * layer_service,const char * service,int type_action,const char * extend);


/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool creatPayloadString(char** ResultTemplate,char *Key, char *Value);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool creatPayloadNumber(char** ResultTemplate,char *Key, int Value);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool replaceInfoFormTranMOSQ(char **ResultTemplate, char * NewLayerService,char * NewService,int NewTypeCction,char * NewExtend, char* payload);


/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool replaceValuePayloadTranMOSQ(char **ResultTemplate, char *Key,char *NewValue, char *payload);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool insertObjectToPayloadTranMOSQ(char **ResultTemplate, char *Key,char *Value, char *payload);


/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool insertStringToPayloadTranMOSQ(char **ResultTemplate, char *Key,char *Value, char *payload);


/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool insertIntToPayloadTranMOSQ(char **ResultTemplate, char *Key,int Value, char *payload);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool getFormTranMOSQ(char **ResultTemplate, const char * layer_service,const char * service,int type_action,const char * extend,const char *Id,long long TimeCreat,const char* payload);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool getPayloadReponseMOSQ(char *result, char type_reponse, char *deviceID,char *dpid,char *value,long long TimeCreat);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool getPayloadDeleteDeviceToBLE(char** ResultTemplate,char *deviceID);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool getPayloadReponseAWS(char** ResultTemplate,int sender,int type, char *ID,int state);

void Aws_updateGroupState(const char* groupAddr, int state);
void Aws_updateGroupDevices(const char* groupAddr, const list_t* devices, const list_t* failedDevices);
/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */

bool getPayloadReponseDevicesGroupAWS(char** result,char *deviceID,char*  devices);

bool getPayloadReponseErrorAWS(char** ResultTemplate,int sender,int type, char *ID,char *failed);

bool getPayloadReponseDeleteDeviceAWS(char** result,char *deviceID);

bool getPayloadReponseDeleteGroupAWS(char** result,char *groupAddress);
/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */

bool getPayloadReponseDeleteSceneAWS(char** result,char *sceneID);

char* getPayloadReponseStateDeviceAWS(char *deviceID, int  state);


bool getPayloadReponseValueSceneAWS(char** result,int sender,int type,char *value);

void sendNotiToUser(const char* message);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool insertObjectReponseMOSQ(char **ResultTemplate,long long TimeCreat,char *deviceID, char *dictDPs);

/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool removeObjectReponseMOSQ(char **ResultTemplate,long long TimeCreat);


#endif   // MY_MOSQUITTO
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

extern struct mosquitto * mosq;

#define sendToServiceNoDebug(serviceToSend, typeAction, payload)  sendToServiceFunc(mosq, serviceToSend, typeAction, payload, false);
#define sendToService(serviceToSend, typeAction, payload)  g_dbgFileName = FILENAME; g_dbgLineNumber =  __LINE__; sendToServiceFunc(mosq, serviceToSend, typeAction, payload, true);
bool sendToServiceFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, const char* payload, bool printDebug);

#define sendToServicePageIndex(serviceToSend, typeAction, pageIndex, payload)  g_dbgFileName = FILENAME; g_dbgLineNumber =  __LINE__; sendToServicePageIndexFunc(mosq, serviceToSend, typeAction, pageIndex, payload);
bool sendToServicePageIndexFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, int pageIndex, const char* payload);

#define sendPacketTo(serviceToSend, typeAction, packet)  g_dbgFileName = FILENAME; g_dbgLineNumber =  __LINE__; sendPacketToFunc(mosq, serviceToSend, typeAction, packet);
bool sendPacketToFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, JSON* packet);


/**
 * @brief The timer query function.
 *
 * This function returns the elapsed time.
 *
 * @return Time in milliseconds.
 */
bool get_topic(char **result_topic,const char * layer_service,const char * service,int type_action,const char * extend);

bool getFormTranMOSQ(char **ResultTemplate, const char * layer_service,const char * service,int type_action,const char * extend,const char *Id,long long TimeCreat,const char* payload);




void Aws_updateGroupState(const char* groupAddr, int state);
void Aws_updateGroupDevices(const char* groupAddr, const list_t* devices, const list_t* failedDevices);




void sendNotiToUser(const char* message);

#endif   // MY_MOSQUITTO
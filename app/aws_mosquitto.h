#ifndef MY_MOSQUITTO
#define MY_MOSQUITTO

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/un.h>

#include <unistd.h>
#include <netdb.h>
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <sys/un.h>
#include <assert.h>
#include "define.h"
#include "parson.h"
#include "helper.h"
#include "database.h"
#include "cJSON.h"
#include "helper.h"

extern struct mosquitto * mosq;

#define sendToServiceNoDebug(serviceToSend, typeAction, payload)  sendToServiceFunc(mosq, serviceToSend, typeAction, payload, false);
#define sendToService(serviceToSend, typeAction, payload)  sendToServiceFunc(mosq, serviceToSend, typeAction, payload, true);
bool sendToServiceFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, const char* payload, bool printDebug);

#define sendToServicePageIndex(serviceToSend, typeAction, pageIndex, payload)   sendToServicePageIndexFunc(mosq, serviceToSend, typeAction, pageIndex, payload);
bool sendToServicePageIndexFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, int pageIndex, const char* payload);

#define sendPacketTo(serviceToSend, typeAction, packet)   sendPacketToFunc(mosq, serviceToSend, typeAction, packet);
bool sendPacketToFunc(struct mosquitto* mosq, const char* serviceToSend, int typeAction, JSON* packet);


void Aws_UpdateGroupState(const char* groupAddr, int state);
void Aws_UpdateGroupDevices(JSON* groupInfo);
void Aws_UpdateSceneInfo(JSON* sceneInfo);



void sendNotiToUser(const char* message, bool isRealTime);

#endif   // MY_MOSQUITTO
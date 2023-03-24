#include <termios.h>    // POSIX terminal control definitionss
#include <sys/time.h>   // time calls
#include <sys/un.h>

#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <unistd.h> /* read, write, close */
#include <string.h> /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h> /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <sys/un.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "define.h"
#include "parson.h"
#include "core_process_t.h"

struct infor_day
{
	char day[3];
	char month[3];
	char year[5];
};

struct infor_time
{
	char hour[3];
	char min[3];
};

typedef struct 
{
    char payload[1000];
    long long TimeCreat;
}InfoActionsScene;



bool getArrayActionScene(char **result,const char *stringPayload,const JSON_Value *object_devices);
bool getPayloadActionScene(char **result,char *result_actionScene,const char *sceneID);
bool getArrayConditionScene(char **result,const char *stringPayload,const JSON_Value *object_devices);
bool getCollectCondition(char **result,const char *stringPayload,const JSON_Value *object_devices);
char *get_dpid(const char *code);

bool getInforDayFromScene(struct infor_day *t,const char *time);
bool getInforTimeFromScene(struct infor_time *t,const char *time);
bool check_day_with_scene(const int dayOfWeek,const char *day_);

void insertDataActionsScene(InfoActionsScene InfoActionsScene_t[], long long TimeCreat,  int *n, char *payload);
void deleteDataActionsScene(InfoActionsScene InfoActionsScene_t[], int *n,int index);
int getCountActionWithTimeStemp(InfoActionsScene InfoActionsScene_t[], int n, long long int  TimeCreat);
void printDataActionsScene(InfoActionsScene InfoActionsScene_t[], int n);
void BubleSort(InfoActionsScene InfoActionsScene_t[], int n);
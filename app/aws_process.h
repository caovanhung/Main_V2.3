#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>     // string function definitions
#include <fcntl.h>  // File control definitions
#include <errno.h>  // Error number definitions
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

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX includes. */
#include <unistd.h>
#include "define.h"
#include "aws_common.h"
#include "parson.h"
#include "core_process_t.h"
#include "aws_mosquitto.h"
#include "cJSON.h"

#define MQTT_TOPIC_COMMON                  "$aws/things/%s/shadow/name"

typedef struct  
{
    char *deviceID;
    char *name;
    char *Service;
    char *MAC;
    char *Unicast;
    char *IDgateway;
    char *deviceKey;
    int provider;
    char *pid;
    char *lock;

    int ledDim;
    int State;
    int created;
    int modified;
    int last_updated;
    char *firmware;

    char *dictMeta;
    char *dictDPs;
    char *dictName;
    char *senderId;
    int pageIndex;
}Info_device;

typedef struct
{
    char *deviceID;
    char *name;
    char *Service;
    char *MAC;
    char *Unicast;
    char *IDgateway;
    char *deviceKey;
    int provider;
    char *pid;

    int loops;
    int delay;

    int State;
    int created;
    int modified;
    int last_updated;
    char *firmware;

    char *dictMeta;
    char *dictDPs;
    char *dictName;
}Info_device_Debug;


typedef struct
{
    char *sceneId;
    char *name;
    int isLocal;
    int  sceneState;
    int sceneControl;
    char *sceneType;
    char *actions;
    char *conditions;
}Info_scene;

typedef struct
{
    const char *groupID;
    const char *devices;
    const char *pid;
    int state;
    int provider;
    char *senderId;
    char *dictDPs;
    const char *name;
}Info_group;

typedef struct
{
    const char *appkey;
    const char *ivIndex;
    const char *netkeyIndex;
    const char *netkey;
    const char *appkeyIndex;
    const char *deviceKey;
    const char *address1;
    const char *address2;
}InfoProvisonGateway;

typedef struct
{
    int type;
    int loops;
    int delay;
    int state;

    int sender;
    char *senderId;
    char *object;
    int provider;
    int pageIndex;
    JSON_Object *JS_object;
    JSON_Value *JS_value;
}Pre_parse;

typedef struct
{
    char id[50];
    long long TimeCreat;
    long long TimeOut;
}InfoReponse;

typedef enum {
    PAGE_NONE,
    PAGE_ANY,
    PAGE_MAIN,
    PAGE_DEVICE,
    PAGE_SCENE,
    PAGE_GROUP
} AwsPageType;

typedef enum {
    TOPIC_GET_PUB,      //     /get
    TOPIC_GET_SUB,      //     /get/accepted
    TOPIC_UPD_PUB,      //     /update
    TOPIC_UPD_SUB,      //     /update/accepted
    TOPIC_NOTI_PUB,     //     /notify
    TOPIC_NOTI_SUB,     //     /notify
    TOPIC_REJECT        //     /reject
} AwsTopicType;

void getHcInformation();
char* Aws_GetTopic(AwsPageType pageType, int pageIndex, AwsTopicType topicType);
JSON* Aws_GetShadow(const char* thingName, const char* shadowName);
void Aws_SyncDatabase();
//for pre-process
bool AWS_short_message_received(char *value);
bool AWS_pre_detect_message_received(Pre_parse *var,char *mess);
bool AWS_detect_message_received_for_update(Pre_parse *var,char *mess);

//for add device
bool AWS_get_info_device(Info_device *inf_device,Pre_parse *pre_detect);
bool MOSQ_getTemplateAddDevice(char **result,Info_device *inf_device);
//dor add scene
bool AWS_getInfoScene(Info_scene *inf_scene,Pre_parse *pre_detect);
bool MOSQ_getTemplateAddScene(char **result,Info_scene *inf_scene);
bool MOSQ_getTemplateDeleteScene(char **result,const char* sceneId);

//for add gateway
bool AWS_getInfoGateway(InfoProvisonGateway *InfoProvisonGateway_t,Pre_parse *pre_detect);
bool MOSQ_getTemplateAddGateway(char **result,InfoProvisonGateway *InfoProvisonGateway_t);

//for control scene
bool AWS_getInfoControlSecene(Info_scene *inf_scene,Pre_parse *pre_detect);
bool MOSQ_getTemplateControSecene(char **result,Info_scene *inf_scene);


//for control device
bool AWS_getInfoControlDevice(Info_device *inf_device,Pre_parse *pre_detect);
bool MOSQ_getTemplateControlDevice(char **result,Info_device *inf_device);

//for deim led device
bool AWS_getInfoDimLedDevice(Info_device *inf_device,Pre_parse *pre_detect);
bool MOSQ_getTemplateDimLedDevice(char **result,Info_device *inf_device);

//for log device
bool AWS_getInfoLockDevice(Info_device *inf_device,Pre_parse *pre_detect);
bool MOSQ_getTemplateLockDevice(char **result,Info_device *inf_device);


//for update service
bool MOSQ_getTemplateUpdateService(char **result,Pre_parse *pre_detect);


//for debug control device led device
bool AWS_getInfoControlDevice_DEBUG(Info_device_Debug *inf_device_Debug,Pre_parse *pre_detect);
bool MOSQ_getTemplateControlDevice_DEBUG(char **result,Info_device_Debug *inf_device_Debug);

//for delete device
bool AWS_getInfoDeleteDevice(Info_device *inf_device,Pre_parse *pre_detect);
bool MOSQ_getTemplateDeleteDevice(char **result,Info_device *inf_device);

//for add group normal
bool AWS_getInfoAddGroupNormal(Info_group *info_group_t,Pre_parse *pre_detect);
bool MOSQ_getTemplateAddGroupNormal(char **result,Info_group *info_group_t);


//for control group normal
bool AWS_getInfoControlGroupNormal(Info_group *info_group_t,Pre_parse *pre_detect);
bool MOSQ_getTemplateControlGroupNormal(char **result,Info_group *info_group_t);

//for delete group normal
bool AWS_getInfoDeleteGroupNormal(Info_group *info_group_t,Pre_parse *pre_detect);
bool MOSQ_getTemplateDeleteGroupNormal(char **result,Info_group *info_group_t);
JSON* Aws_CreateCloudPacket(JSON* localPacket);

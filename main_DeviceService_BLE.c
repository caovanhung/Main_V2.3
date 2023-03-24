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
#include <pthread.h>

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX includes. */
#include <unistd.h>

#include <mosquitto.h>
#include <unistd.h>


#include "ble_common.h"
#include "define.h"
#include "logging_stack.h"
#include "ble_process.h"
#include "ble_security.h"
#include "queue.h"
#include "core_process_t.h"
#include "uart.h"
#include "time_t.h"
#include "mosquitto.h"
#include "cJSON.h"

const char* SERVICE_NAME = "BLE";
FILE *fptr;
int fd;
char rcv_uart_buff[MAXLINE];


JSON_Value *Json_Value_InfoDevices = NULL;
char *string_InfoAndress = "{}";

struct mosquitto * mosq;
struct Queue *queue_received;
struct Queue *queue_UART;
pthread_mutex_t mutex_lock_t                = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue             = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutex_lock_mosq_t           = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_MosqQueue         = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutex_lock_RX               = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_RX_Queue          = PTHREAD_COND_INITIALIZER;

static char* senderId = NULL;

bool addSceneLC(const char* payload);
bool delSceneLC(const char* payload);

bool addDevice(const char* payload);

bool addGroupNormal(const char* payload);
bool delGroupNormal(const char* payload);
bool updateGroupNormal(const char* payload);

bool addGroupLink(const char* payload);
bool delGroupLink(const char* payload);
bool updateGroupLink(const char* payload);

bool addSceneActions(const char* sceneId, JSON* actions);
bool deleteSceneActions(const char* sceneId, JSON* actions);
bool addSceneCondition(const char* sceneId, JSON* condition);
bool deleteSceneCondition(const char* sceneId, JSON* condition);

bool getInfoDeviceFromDatabase();
bool getInfoAddressFromDeviceDatabase(char **result);
char *getDeviceIDfromAddress(char *string_InfoAndress, char* address_);
char *getDpIDfromAddress(char *string_InfoAndress, char* address_);
char *getAndressFromDeviceID(char *string_InfoAndress, char* deviceID);
char *getAndressDeviceFromDeviceID(JSON_Value *Json_Value_InfoDevices, char* deviceID);

//Process reply UART from GW
void* UART_RX_TASK(void* p)
{
    char givenStr[MAX_PACKAGE_SIZE];
    while (1)
    {
        int len_uart = UART0_Recv( fd, rcv_uart_buff, MAX_PACKAGE_SIZE);
        if ( len_uart > 0)
        {
            // Ignore frame 0x91b5
            if (len_uart >= 4 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0xb5) {
                continue;
            }
            if (len_uart >= 10 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0x5d && rcv_uart_buff[9] == 0x00) {
                continue;
            }
            pthread_mutex_lock(&mutex_lock_RX);
            strcpy(givenStr, (char *)Hex2String(rcv_uart_buff, len_uart));
            enqueue(queue_UART, givenStr);
            pthread_cond_broadcast(&dataUpdate_RX_Queue);
            pthread_mutex_unlock(&mutex_lock_RX);
        }
        usleep(1000);  
    }
    return p;
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
    if(rc) 
    {
        LogError((get_localtime_now()),("Error with result code: %d\n", rc));
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_DEVICE_BLE, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
    // LogInfo((get_localtime_now()),("(char *) msg->payload: %s\n", (char *) msg->payload));
    pthread_mutex_lock(&mutex_lock_t);
    int size_queue = get_sizeQueue(queue_received);
    if(size_queue < MAX_SIZE_NUMBER_QUEUE)
    {
        enqueue(queue_received,(uint8_t*) msg->payload);
        pthread_cond_broadcast(&dataUpdate_Queue);
        pthread_mutex_unlock(&mutex_lock_t);
    }
    else
    {
       pthread_mutex_unlock(&mutex_lock_t); 
    }
}

void* RUN_MQTT_LOCAL(void* p)
{
    int rc = 0;
    mosquitto_lib_init();
    mosq = mosquitto_new(MQTT_MOSQUITTO_CIENT_ID, true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
    }
    LogInfo((get_localtime_now()),("We are now connected to the broker!"));
    while(1)
    {
        rc = mosquitto_loop(mosq, -1, 1);
        if(rc != 0)
        {
            LogError( (get_localtime_now()),( "rc %d.",rc ) );
            fptr = fopen("/usr/bin/log.txt","a");
            fprintf(fptr,"[%s]BLE  error %d connected mosquitto\n",get_localtime_now(),rc);
            fclose(fptr);
            // break;
        }
        usleep(100);
    }
}

void* UART_PROCESS_TASK(void* p)
{
    uint8_t msgHex[MAX_PACKAGE_SIZE];
    ble_rsp_frame_t bleFrames[MAX_FRAME_COUNT];
    int size_queue = 0;
    unsigned char *hex_uart_rev;
    unsigned char *hex_netkey;

    while (1)
    {
        pthread_mutex_lock(&mutex_lock_RX);
        size_queue = get_sizeQueue(queue_UART);
        if (size_queue > 0)
        {
            char* recvPackage = (char *)dequeue(queue_UART);
            printf("\n\r");
            logInfo("Received a BLE package: %s", recvPackage);
            String2HexArr(recvPackage, msgHex);
            int frameCount = GW_SplitFrame(bleFrames, msgHex, strlen(recvPackage) / 2);
            logInfo("Parsing package. Found %d frames:", frameCount);
            for (int i = 0; i < frameCount; i++)
            {
                // Print the frame information
                char str[3000];
                BLE_PrintFrame(str, &bleFrames[i]);
                struct state_element *tmp = malloc(sizeof(struct state_element));
                InfoDataUpdateDevice *InfoDataUpdateDevice_t = malloc(sizeof(InfoDataUpdateDevice));
                int type_devices_repons = check_form_recived_from_RX(tmp, &bleFrames[i]);
                char *deviceID  = getDeviceIDfromAddress(string_InfoAndress, tmp->address_element);
                logInfo("Frame %d: type: %d, %s", i, type_devices_repons, str);
                if (deviceID || deviceID == NULL || type_devices_repons == GW_RESPONSE_SET_TIME_SENSOR_PIR ||
                    type_devices_repons == GW_RESPONSE_ACTIVE_DEVICE_HG_RD_UNSUCCESS ||
                    type_devices_repons == GW_RESPONSE_ACTIVE_DEVICE_HG_RD_SUCCESS ||
                    type_devices_repons == GW_RESPONSE_SAVE_GATEWAY_HG ||
                    type_devices_repons == GW_RESPONSE_SAVE_GATEWAY_RD) {
                    if (type_devices_repons != GW_RESPONSE_UNKNOW)
                    {
                        long long TimeCreat = 0;
                        char response = 0;
                        char *topic;
                        char payload[1000];
                        char *message;
                        char *state;
                        char *Id;

                        switch(type_devices_repons)
                        {
                            case GW_RESPONSE_SET_TIME_SENSOR_PIR:
                            case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_UNSUCCESS:
                            case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_SUCCESS:
                            case GW_RESPONSE_SAVE_GATEWAY_HG:
                            case GW_RESPONSE_SAVE_GATEWAY_RD:
                            {
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                                InfoDataUpdateDevice_t->deviceID = tmp->address_element;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                InfoDataUpdateDevice_t->dpID = tmp->address_element;
                                response = type_devices_repons;
                                break;
                            }

                            case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE:
                            case GW_RESPONSE_SCENE_LC_WRITE_INTO_DEVICE:
                            case GW_RESPONSE_DEVICE_KICKOUT:
                            {
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                InfoDataUpdateDevice_t->dpID = tmp->address_element;
                                response = type_devices_repons;
                                break;
                            }

                            case GW_RESPONSE_DEVICE_STATE:
                            {
                                TimeCreat = timeInMilliseconds();
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;


                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = NULL;
                                if(tmp->value != NULL)
                                {
                                    deviceID  = getDeviceIDfromAddress(string_InfoAndress, tmp->value);
                                    InfoDataUpdateDevice_t->value = (char *)deviceID;
                                }
                                else
                                {
                                    InfoDataUpdateDevice_t->value = NULL;
                                }
                                response = type_devices_repons;
                                break;
                            }

                            case GW_RESPONSE_DIM_LED_SWITCH_HOMEGY:
                            case GW_RESPONSE_DEVICE_CONTROL: {
                                JSON* packet = JSON_CreateObject();
                                JSON_SetText(packet, "deviceAddr", tmp->address_element);
                                JSON_SetText(packet, "dpAddr", tmp->address_element);
                                JSON_SetNumber(packet, "dpValue", tmp->dpValue);
                                if (senderId != NULL) {
                                    JSON_SetNumber(packet, "causeType", 1);
                                    JSON_SetText(packet, "causeId", senderId);
                                } else {
                                    JSON_SetNumber(packet, "causeType", tmp->causeType);
                                    JSON_SetText(packet, "causeId", tmp->causeId);
                                }
                                sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                                JSON_Delete(packet);
                                senderId = NULL;
                                break;
                            }
                            case GW_RESPONSE_SENSOR_AIR:
                                LogInfo((get_localtime_now()),("%d",type_devices_repons));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_VALUE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_AIR_SENSOR_DETECT;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            case GW_RESPONSE_SENSOR_ENVIRONMENT:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_SENSOR_ENVIRONMENT"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_VALUE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_ENVIRONMENT_SENSOR_TEMPER;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            case GW_RESPONSE_SENSOR_BATTERY:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_SENSOR_BATTERY"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_VALUE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_BATTERY_SENSOR;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            case GW_RESPONSE_SENSOR_PIR_DETECT:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_SENSOR_PIR_DETECT"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_PIR_SENSOR_DETECT;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            case GW_RESPONSE_SENSOR_PIR_LIGHT:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_SENSOR_PIR_LIGHT"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_VALUE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_PIR_SENSOR_LUX;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            case GW_RESPONSE_SENSOR_DOOR_DETECT:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_SENSOR_DOOR_DETECT"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_DOOR_SENSOR_DETECT;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            case GW_RESPONSE_SENSOR_DOOR_ALARM:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_SENSOR_DOOR_ALARM"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                                InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_DOOR_SENSOR_ALRM;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;

                            case GW_RESPONSE_ADD_GROUP_LIGHT:
                                LogInfo((get_localtime_now()),("GW_RESPONSE_ADD_GROUP_LIGHT"));
                                InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                                InfoDataUpdateDevice_t->deviceID = tmp->address_element;
                                InfoDataUpdateDevice_t->dpID = TYPE_DPID_CTR_LIGHT;
                                InfoDataUpdateDevice_t->value = tmp->value;
                                response = type_devices_repons;
                                break;
                            default:
                                LogError((get_localtime_now()),("Error detect"));
                                break;
                        }

                        if(type_devices_repons != GW_RESPONSE_DEVICE_CONTROL && type_devices_repons != GW_RESPONSE_SENSOR_AIR && type_devices_repons != GW_RESPONSE_SENSOR_ENVIRONMENT && type_devices_repons != 0)
                        {
                            TimeCreat = timeInMilliseconds();
                            getPayloadReponseMOSQ(payload,InfoDataUpdateDevice_t->type_reponse,InfoDataUpdateDevice_t->deviceID,InfoDataUpdateDevice_t->dpID,InfoDataUpdateDevice_t->value,TimeCreat);
                            sendToService(SERVICE_CORE, response, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                        }
                        else if(type_devices_repons == GW_RESPONSE_SENSOR_ENVIRONMENT)
                        {
                            char temp[4] = {'\0'};
                            char humi[4] = {'\0'};
                            char val[7] = {'\0'};
                            strcpy(val,InfoDataUpdateDevice_t->value);
                            temp[0] = val[0];
                            temp[1] = val[1];
                            temp[2] = val[2];
                            humi[0] = val[3];
                            humi[1] = val[4];
                            humi[2] = val[5];

                            TimeCreat = timeInMilliseconds();
                            getPayloadReponseMOSQ(payload,InfoDataUpdateDevice_t->type_reponse,InfoDataUpdateDevice_t->deviceID,TYPE_DPID_ENVIRONMENT_SENSOR_TEMPER,temp,TimeCreat);
                            sendToService(SERVICE_CORE, GW_RESPONSE_SENSOR_ENVIRONMENT_TEMPER, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                            sendToService(MOSQ_NameService_Support_Rule_Schedule, GW_RESPONSE_SENSOR_ENVIRONMENT_TEMPER, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);

                            getPayloadReponseMOSQ(payload,InfoDataUpdateDevice_t->type_reponse,InfoDataUpdateDevice_t->deviceID,TYPE_DPID_ENVIRONMENT_SENSOR_HUMIDITY,humi,TimeCreat);
                            sendToService(SERVICE_CORE, GW_RESPONSE_SENSOR_ENVIRONMENT_HUMIDITY, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                            sendToService(MOSQ_NameService_Support_Rule_Schedule, GW_RESPONSE_SENSOR_ENVIRONMENT_HUMIDITY, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                        }
                        else if(type_devices_repons == GW_RESPONSE_SENSOR_AIR)
                        {
                            char val[5] = {'\0'};
                            char detect[6] = {'\0'};
                            char battery[4] = {'\0'};
                            strcpy(val,InfoDataUpdateDevice_t->value);
                            if(val[1] == '1')
                            {
                                strcpy(detect,KEY_TRUE);
                            }
                            else
                            {
                                strcpy(detect,KEY_FALSE);
                            }
                            if(val[3] == '1')
                            {
                                strcpy(battery,"0");
                            }
                            else
                            {
                                strcpy(battery,"100");
                            }
                            TimeCreat = timeInMilliseconds();
                            getPayloadReponseMOSQ(payload,InfoDataUpdateDevice_t->type_reponse,InfoDataUpdateDevice_t->deviceID,TYPE_DPID_AIR_SENSOR_DETECT,detect,TimeCreat);
                            sendToService(SERVICE_CORE, GW_RESPONSE_SENSOR_AIR, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                            sendToService(MOSQ_NameService_Support_Rule_Schedule, GW_RESPONSE_SENSOR_AIR, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);

                            getPayloadReponseMOSQ(payload,InfoDataUpdateDevice_t->type_reponse,InfoDataUpdateDevice_t->deviceID,TYPE_DPID_AIR_SENSOR_BATTERY,battery,TimeCreat);
                            sendToService(SERVICE_CORE, GW_RESPONSE_SENSOR_AIR, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                            sendToService(MOSQ_NameService_Support_Rule_Schedule, GW_RESPONSE_SENSOR_AIR, MOSQ_Reponse, InfoDataUpdateDevice_t->deviceID, payload);
                        }
                    }
                }
            }
        }
        else if(size_queue == MAX_SIZE_NUMBER_QUEUE)
        {
            pthread_mutex_unlock(&mutex_lock_RX);
        }
        else
        {
            pthread_cond_wait(&dataUpdate_RX_Queue, &mutex_lock_RX);
        }
        pthread_mutex_unlock(&mutex_lock_RX);
        usleep(1000);  
    }
    return p;
}

int main( int argc,char ** argv )
{
    int size_queue = 0;
    bool check_flag = false;
    pthread_t thr[4];
    int err,xRun = 1;
    int rc[4];
    queue_received = newQueue(MAX_SIZE_NUMBER_QUEUE);
    queue_UART = newQueue(MAX_SIZE_NUMBER_QUEUE);
    //Init for uart()
    fd = UART0_Open(fd,VAR_PORT_UART);
    if(fd == -1)
    {
        fptr = fopen("/usr/bin/log.txt","a");
        fprintf(fptr,"[%s]BLE open error ttyS2\n",get_localtime_now());
        fclose(fptr);
    }
    
    do
    {
        err = UART0_Init(fd,115200,0,8,1,'N');
        usleep(50000);
    }
    while(-1 == err || -1 == fd);
    LogInfo((get_localtime_now()),("------->>> UART START <<<-------"));
    usleep(50000);

    rc[0]=pthread_create(&thr[0],NULL,RUN_MQTT_LOCAL,NULL);
    usleep(50000);


    rc[1]=pthread_create(&thr[1],NULL,UART_RX_TASK,NULL);
    usleep(50000);

    rc[3]=pthread_create(&thr[3],NULL,UART_PROCESS_TASK,NULL);
    usleep(50000);

    if (pthread_mutex_init(&mutex_lock_t, NULL) != 0) 
    {
        LogError((get_localtime_now()),("mutex init has failed"));
        return 1;
    }
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_mosq_t, NULL) != 0) {
        LogError((get_localtime_now()),("mutex init has failed"));
        return 1;
    }
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_RX, NULL) != 0) {
        LogError((get_localtime_now()),("mutex init has failed"));
        return 1;
    }
    usleep(50000);

    while(xRun!=0)
    {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if(size_queue > 0)
        {
            int dpValue_int = 0;
            int i = 0,j=0,count=0;

            provison_inf *PRV = (provison_inf *)malloc(sizeof(provison_inf));
            InfoControlDeviceBLE    *InfoControlDeviceBLE_t     = (InfoControlDeviceBLE*)malloc(sizeof(InfoControlDeviceBLE));
            InfoControlCLT_BLE      *InfoControlCLT_BLE_t       = (InfoControlCLT_BLE*)malloc(sizeof(InfoControlCLT_BLE));
            InfoControlHSL_BLE      *InfoControlHSL_BLE_t       = (InfoControlHSL_BLE*)malloc(sizeof(InfoControlHSL_BLE));
            InfoControlBlinkRGB_BLE *InfoControlBlinkRGB_BLE_t  = (InfoControlBlinkRGB_BLE*)malloc(sizeof(InfoControlBlinkRGB_BLE));

            JSON_Value *schema = NULL;
            JSON_Value *object = NULL;
            char* recvMsg = (char *)dequeue(queue_received);
            schema = json_parse_string(recvMsg);
            const char *LayerService    = json_object_get_string(json_object(schema),MOSQ_LayerService);
            const char *NameService     = json_object_get_string(json_object(schema),MOSQ_NameService);
            int type_action_t           = json_object_get_number(json_object(schema),MOSQ_ActionType);
            const char *Extern          = json_object_get_string(json_object(schema),MOSQ_Extend);
            const char *ID              = json_object_get_string(json_object(schema),MOSQ_Id);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            object                      = json_parse_string(object_string);
            printf("\n\r");
            logInfo("Received message: %s", json_serialize_to_string(schema));

            if (type_action_t == -1) {
                cJSON* root = cJSON_Parse(recvMsg);
                int reqType = JSON_GetNumber(root, "reqType");
                char* dpAddr, *groupAddr;
                switch (reqType) {
                    case TYPE_CTR_DEVICE:
                    case TYPE_CTR_GROUP_NORMAL: {
                        senderId = JSON_GetText(root, "senderId");  // Save senderId to response to Core service after receiving response from device
                        char* pid = JSON_GetText(root, "pid");
                        cJSON* dictDPs = cJSON_GetObjectItem(root, "dictDPs");
                        int lightness = -1, colorTemperature = -1;
                        JSON_ForEach(o, dictDPs) {
                            int dpId = JSON_GetNumber(o, "id");
                            dpAddr = JSON_GetText(o, "addr");
                            int dpValue = JSON_GetNumber(o, "value");
                            char valueStr[5];
                            sprintf(valueStr, "%d", dpValue);
                            if (isMatchString(pid, HG_BLE_SWITCH_1) || isMatchString(pid, HG_BLE_SWITCH_2) || isMatchString(pid, HG_BLE_SWITCH_3) || isMatchString(pid, HG_BLE_SWITCH_4)) {
                                ble_controlOnOFF_SW(fd, dpAddr, valueStr);
                            } else if (isMatchString(pid, HG_BLE_CURTAIN_2_LAYER) || isMatchString(pid, HG_BLE_ROLLING_DOOR) ||
                                isMatchString(pid, HG_BLE_CURTAIN) || dpId == 20) {
                                ble_controlOnOFF(fd, dpAddr, valueStr);
                            } else if (dpId == 24) {
                                ble_controlHSL(fd, dpAddr, valueStr);
                            } else if (dpId == 21) {
                                ble_controlModeBlinkRGB(fd, dpAddr, valueStr);
                            } else if (dpId == 22) {
                                lightness = dpValue;
                            } else if (dpId == 23) {
                                colorTemperature = dpValue;
                            }

                            if (lightness >= 0 && colorTemperature >= 0) {
                                ble_controlCTL(fd, dpAddr, lightness, colorTemperature);
                            }
                        }
                        break;
                    }
                    case TYPE_CTR_SCENE: {
                        char* sceneId = JSON_GetText(root, "sceneId");
                        int sceneType = JSON_GetNumber(root, "sceneType");
                        int state = JSON_GetNumber(root, "state");
                        if (sceneType == SceneTypeManual) {
                            logInfo("Executing LC scene %s", sceneId);
                            ble_callSceneLocalToHC(fd, "FFFF", sceneId);
                        } else {
                            // Enable/Disable scene
                        }
                        break;
                    }
                    case TYPE_ADD_GROUP_NORMAL:
                    case TYPE_DEL_GROUP_NORMAL:
                        groupAddr = JSON_GetText(root, "groupAddr");
                        cJSON* devices = cJSON_GetObjectItem(root, "devices");
                        JSON_ForEach(o, devices) {
                            if (reqType == TYPE_ADD_GROUP_NORMAL) {
                                ble_addDeviceToGroupLightCCT_HOMEGY(fd, groupAddr, o->valuestring, o->valuestring);
                            } else {
                                ble_deleteDeviceToGroupLightCCT_HOMEGY(fd, groupAddr, o->valuestring, o->valuestring);
                            }
                        }
                        break;
                    case TYPE_DEL_GROUP_LINK:
                    case TYPE_ADD_GROUP_LINK: {
                        char* groupAddr = JSON_GetText(root, "groupAddr");
                        JSON* devicesArray = JSON_GetObject(root, "devices");
                        JSON_ForEach(device, devicesArray) {
                            char* deviceAddr = JSON_GetText(device, "deviceAddr");
                            char* dpAddr = JSON_GetText(device, "dpAddr");
                            if (reqType == TYPE_ADD_GROUP_LINK) {
                                ble_addDeviceToGroupLink(fd, groupAddr, deviceAddr, dpAddr);
                            } else {
                                ble_deleteDeviceToGroupLightCCT_HOMEGY(fd, groupAddr, deviceAddr, dpAddr);
                            }
                        }
                        break;
                    }
                    case TYPE_ADD_DEVICE: {
                        char* deviceAddr = JSON_GetText(root, "deviceAddr");
                        char* devicePid = JSON_GetText(root, "devicePid");
                        char* deviceKey = JSON_GetText(root, "deviceKey");
                        set_inf_DV_for_GW(fd, deviceAddr, devicePid, deviceKey);
                        break;
                    }
                    case TYPE_DEL_DEVICE: {
                        char* deviceId = JSON_GetText(root, "deviceId");
                        char* deviceAddr = JSON_GetText(root, "deviceAddr");
                        if (deviceAddr) {
                            setResetDeviceSofware(fd, "d401");
                            setResetDeviceSofware(fd, deviceAddr);
                            logInfo("Deleted deviceId: %s, address: %s", deviceId, deviceAddr);
                        }
                        break;
                    }
                    case TYPE_ADD_SCENE:
                    {
                        addSceneLC(recvMsg);
                        break;
                    }
                    case TYPE_DEL_SCENE: {
                        delSceneLC(recvMsg);
                        break;
                    }
                    case TYPE_UPDATE_SCENE:
                    {
                        char* sceneId = JSON_GetText(root, "sceneId");
                        JSON* actionsNeedRemove = JSON_GetObject(root, "actionsNeedRemove");
                        JSON* actionsNeedAdd = JSON_GetObject(root, "actionsNeedAdd");
                        bool ret = deleteSceneActions(sceneId, actionsNeedRemove);
                        if (ret) {
                            ret = addSceneActions(sceneId, actionsNeedAdd);
                        }
                        if (ret && JSON_HasObjectItem(root, "conditionNeedRemove")) {
                            JSON* conditionNeedRemove = JSON_GetObject(root, "conditionNeedRemove");
                            JSON* conditionNeedAdd = JSON_GetObject(root, "conditionNeedAdd");
                            ret = deleteSceneCondition(sceneId, conditionNeedRemove);
                            if (ret) {
                                ret = addSceneCondition(sceneId, conditionNeedAdd);
                            }
                        }
                        break;
                    }

                }

                cJSON_Delete(root);
            }

            switch (type_action_t)
            {
                char *topic;
                char *payload;
                char *message;
                char *deviceID;
                char *address_device;
                char *dpID;
                char *dpValue;
                char *pid;
                int delay;
                int loops;
                int value;
                int check_tmp = 0;
            //     case TYPE_DIM_LED_SWITCH:
            //     {
            //         deviceID = (char *)json_object_get_string(json_object(object),KEY_DEVICE_ID);
            //         if(deviceID == NULL)
            //         {
            //             break;
            //         }
            //         address_device = getAndressDeviceFromDeviceID(Json_Value_InfoDevices,deviceID);
            //         if(address_device == NULL )
            //         {
            //             break;
            //         }
            //         value = json_object_get_number(json_object(object),KEY_LED);
            //         ble_dimLedSwitch_HOMEGY(fd,address_device,value);
            //         break;
            //     }
            //     case TYPE_LOCK_AGENCY:
            //     {
            //         deviceID = (char *)json_object_get_string(json_object(object),KEY_DEVICE_ID);
            //         if(deviceID == NULL)
            //         {
            //             break;
            //         }
            //         address_device = getAndressDeviceFromDeviceID(Json_Value_InfoDevices,deviceID);
            //         if(address_device == NULL )
            //         {
            //             break;
            //         }
            //         count = json_object_get_count(json_object_get_object(json_object(object),KEY_LOCK));
            //         if(count == 1)
            //         {
            //             dpID = (char *)json_object_get_name(json_object_get_object(json_object(object),KEY_LOCK),i);
            //             dpValue_int = json_object_dotget_number(json_object_get_object(json_object(object),KEY_LOCK),dpID);
            //         }
            //         else
            //         {
            //             break;
            //         }
            //         ble_logDeivce(fd,address_device,dpValue_int-2);
            //         break;
            //     }
            //     case TYPE_LOCK_KIDS:
            //     {
            //         deviceID = (char *)json_object_get_string(json_object(object),KEY_DEVICE_ID);
            //         if(deviceID == NULL)
            //         {
            //             break;
            //         }
            //         address_device = getAndressDeviceFromDeviceID(Json_Value_InfoDevices,deviceID);
            //         if(address_device == NULL )
            //         {
            //             break;
            //         }
            //         count = json_object_get_count(json_object_get_object(json_object(object),KEY_LOCK));
            //         for(i=0;i<count;i++)
            //         {
            //             dpID = (char *)json_object_get_name(json_object_get_object(json_object(object),KEY_LOCK),i);
            //             dpValue_int = json_object_dotget_number(json_object_get_object(json_object(object),KEY_LOCK),dpID);
            //             if(isMatchString(dpID,"1"))
            //             {
            //                 ble_logTouch(fd,address_device,"00",dpValue_int);
            //             }
            //             else if(isMatchString(dpID,"2"))
            //             {
            //                 ble_logTouch(fd,address_device,"01",dpValue_int);
            //             }
            //             else if(isMatchString(dpID,"3"))
            //             {
            //                 ble_logTouch(fd,address_device,"02",dpValue_int);
            //             }
            //             else if(isMatchString(dpID,"4"))
            //             {
            //                 ble_logTouch(fd,address_device,"03",dpValue_int);
            //             }
            //             sleep(1);
            //         }
            //         break;
            //     }
            //     case TYPE_UPDATE_GROUP_NORMAL:
            //     {
            //         updateGroupNormal(object_string);
            //         break;
            //     }
            //     case TYPE_ADD_GROUP_LINK:
            //     {
            //         addGroupLink(object_string);
            //         // getInfoDeviceFromDatabase();
            //         break;
            //     }
            //     case TYPE_DEL_GROUP_LINK:
            //     {
            //         delGroupLink(object_string);
            //         // getInfoDeviceFromDatabase();
            //         break;
            //     }
            //     case TYPE_UPDATE_GROUP_LINK:
            //     {
            //         updateGroupLink(object_string);
            //         break;
            //     }
            //     case TYPE_ADD_DEVICE:
            //     {
            //         addDevice(object_string);
            //         // getInfoDeviceFromDatabase();
            //         break;
            //     }
            //     case TYPE_DEL_DEVICE:
            //     {
            //         char* deviceId = json_object_get_string(json_object(object),KEY_DEVICE_ID);
            //         char* deviceAddr = getAndressDeviceFromDeviceID(Json_Value_InfoDevices, deviceId);
            //         if (deviceAddr != NULL) {
            //             setResetDeviceSofware(fd,deviceAddr);
            //         }
            //         break;
            //     }
            //     case TYPE_DEL_SCENE_LC:
            //     {
            //         LogInfo((get_localtime_now()),("TYPE_DEL_SCENE_LC"));
            //         delSceneLC(object_string);
            //         // getInfoDeviceFromDatabase();
            //         break;
            //     }
            //     case TYPE_CTR_SCENE_LC:
            //     {
            //         if(ID == NULL)
            //         {
            //         LogError((get_localtime_now()),("TYPE_CTR_SCENE_LC error"));
            //            break;
            //         }
            //         LogInfo((get_localtime_now()),("TYPE_CTR_SCENE_LC"));
            //         ble_callSceneLocalToHC(fd,"FFFF",ID);

            //         // getInfoDeviceFromDatabase();
            //         break;
            //     }
            //     case TYPE_ADD_GW:
            //     {
            //         check_flag = ble_getInfoProvison(PRV,json_object(object));
            //         check_flag = ble_bindGateWay(PRV,fd);
            //         if(check_flag)
            //         {
            //             // getPayloadReponseMOSQ(payload,(char *)json_object_get_string(json_object(schema),MOSQ_Id),"1",json_object_get_number(json_object(schema),MOSQ_TimeCreat),TYPE_DEVICE_ADD_SUCCES_HC);
            //             // getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_FEEDBACK_GATEWAY,MOSQ_Reponse,(char *)json_object_get_string(json_object(schema),MOSQ_Id),json_object_get_number(json_object(schema),MOSQ_TimeCreat),payload);
            //             // pthread_mutex_lock(&mutex_lock_mosq_t);
            //             // LogInfo((get_localtime_now()),("message = %s",message));
            //             // enqueue(queue_mos_pub,message);
            //             // pthread_cond_broadcast(&dataUpdate_MosqQueue);
            //             // pthread_mutex_unlock(&mutex_lock_mosq_t);
            //             // free(payload);
            //             // free(message);
            //         }
            //         break;
            //     }
                // case TYPE_GET_INF_DEVICES:
                // {
                //         Json_Value_InfoDevices = json_value_deep_copy(object);
                //         getInfoAddressFromDeviceDatabase(&string_InfoAndress);
                //     break;
                // }
            //     case TYPE_MANAGER_PING_ON_OFF:
            //     {
            //         long long int TimeCreat = timeInMilliseconds();
            //         get_topic(&topic,MOSQ_LayerService_Manager,MOSQ_NameService_Manager_ServieceManager,TYPE_MANAGER_PING_ON_OFF,Extern);
            //         getFormTranMOSQ(&message,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_MANAGER_PING_ON_OFF,MOSQ_Reponse,ID,TimeCreat,object_string);
            //         mosquitto_publish(mosq, NULL,topic, strlen(message),message, 0, false);
            //         break;
            //     }
            //     case TYPE_DEBUG_CTL_LOOP_DEVICE:
            //     {
            //         deviceID    = (char *)json_object_get_string(json_object(object),KEY_DEVICE_ID);
            //         loops       = json_object_get_number(json_object(object),KEY_LOOP_PRECONDITION);
            //         delay       = json_object_get_number(json_object(object),KEY_DELAY)*1000;

            //         LogInfo((get_localtime_now()),("deviceID %s",deviceID));
            //         LogInfo((get_localtime_now()),("loops %d",loops));
            //         LogInfo((get_localtime_now()),("delay %d",delay));
            //         getPidDevice(&pid,Json_Value_InfoDevices,deviceID);

            //         if( isMatchString(pid,HG_BLE_SWITCH_1) ||
            //             isMatchString(pid,HG_BLE_SWITCH_2) ||
            //             isMatchString(pid,HG_BLE_SWITCH_3) ||
            //             isMatchString(pid,HG_BLE_SWITCH_4))
            //         {
            //             for(i=0;i<loops;i++)
            //             {
            //                 if(i%2==0)
            //                 {
            //                     InfoControlDeviceBLE_t->state = "0";
            //                 }
            //                 else
            //                 {
            //                     InfoControlDeviceBLE_t->state = "1";
            //                 }
            //                 getInfoControlOnOffBLE(InfoControlDeviceBLE_t,Json_Value_InfoDevices,deviceID,"1");
            //                 ble_controlOnOFF_NODELAY(fd,InfoControlDeviceBLE_t->address,InfoControlDeviceBLE_t->state );
            //                 usleep(delay);
            //             }
            //         }
            //         break;
            //     }
            //     default:
            //     {
            //         break;
            //     }
            }
        }
        else if(size_queue == MAX_SIZE_NUMBER_QUEUE)
        {
            pthread_mutex_unlock(&mutex_lock_t);
        }
        else
        {
            pthread_cond_wait(&dataUpdate_Queue, &mutex_lock_t);
        }
        pthread_mutex_unlock(&mutex_lock_t);
        usleep(1000);
    }
    return 0;
}

bool addSceneActions(const char* sceneId, JSON* actions) {
    if (sceneId == NULL || actions == NULL) {
        return false;
    }
    char commonDevices[1200]    = {'\0'};
    int  i = 0, j = 0;
    LogInfo((get_localtime_now()),("[addSceneLC] sceneId = %s", sceneId));
    int actionCount = JSON_ArrayCount(actions);
    for (i = 0; i < actionCount; i++) {
        LogInfo((get_localtime_now()),("[ACTION %d]", i));
        JSON* action = JSON_ArrayGetObject(actions, i);
        char* pid = JSON_GetText(action, "pid");
        char* deviceAddr = JSON_GetText(action, "entityAddr");
        int dpId = JSON_GetNumber(action, "dpId");

        if (pid != NULL) {
            if (isContainString(HG_BLE_SWITCH, pid)) {
                int dpValue = JSON_GetNumber(action, "dpValue");
                int dpid_t = 0;
                dpid_t = (dpId - 1)*0x10 + dpValue;
                LogInfo((get_localtime_now()),("    [addSceneLC] deviceAddr = %s", deviceAddr));
                LogInfo((get_localtime_now()),("    [addSceneLC] dpId       = %d", dpId));
                LogInfo((get_localtime_now()),("    [addSceneLC] dpValue    = %d", dpValue));
                LogInfo((get_localtime_now()),("    [addSceneLC] commonDevices = %s", commonDevices));
                if (!isContainString(commonDevices, deviceAddr)) {
                    char element_count[3]= {'\0'};
                    char param[9] = {'\0'};
                    char tmp_para[2] = {'\0'};
                    Int2Hex_1byte(dpid_t, tmp_para);
                    strcat(param, tmp_para);
                    LogInfo((get_localtime_now()),("    [addSceneLC] tmp_para    = %s", tmp_para));
                    j = i + 1;
                    for (j; j < actionCount; j++) {
                        JSON* actionCompare = JSON_ArrayGetObject(actions, j);
                        char* deviceAddrCompare = JSON_GetText(actionCompare, "entityAddr");
                        int dpIdCompare         = JSON_GetNumber(actionCompare, "dpId");
                        int dpValueCompare      = JSON_GetNumber(actionCompare, "dpValue");
                        int dpid_t_compare = 0;
                        dpid_t_compare = (dpIdCompare - 1)*0x10 + dpValueCompare;
                        LogInfo((get_localtime_now()),("    [CHECK COMMON %d]",j));
                        LogInfo((get_localtime_now()),("        [addSceneLC] deviceAddrCompare   = %s",deviceAddrCompare));
                        LogInfo((get_localtime_now()),("        [addSceneLC] dpid_t_compare     = %d", dpid_t_compare));
                        LogInfo((get_localtime_now()),("        [addSceneLC] dpValueCompare    = %d", dpValueCompare));

                        if (isMatchString(deviceAddr, deviceAddrCompare)) {
                            char tmp_para_compare[3] = {'\0'};
                            Int2Hex_1byte(dpid_t_compare, tmp_para_compare);
                            LogInfo((get_localtime_now()),("[addSceneLC] tmp_para_compare = %s",tmp_para_compare));
                            if (!isContainString(param, tmp_para_compare)) {
                                strncat(param, tmp_para_compare, 2);
                            }
                        }
                    }
                    int size_para = strlen(param)/2;
                    sprintf(element_count,"%02d",size_para);
                    LogInfo((get_localtime_now()),("    [addSceneLC] param = %s",param));
                    LogInfo((get_localtime_now()),("    [addSceneLC] element_count = %s",element_count));
                    LogInfo((get_localtime_now()),("    [addSceneLC] deviceAddr = %s", deviceAddr));
                    ble_setSceneLocalToDeviceSwitch(fd, deviceAddr, sceneId, "64", element_count, param);
                    sleep(1);
                    strcat(commonDevices, deviceAddr);
                }
            } else if (isContainString(RD_BLE_LIGHT_WHITE_TEST, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLight_RANGDONG(fd, deviceAddr, sceneId, "0x00");
                sleep(1);
            } else if (isContainString(RD_BLE_LIGHT_RGB, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLight_RANGDONG(fd, deviceAddr, sceneId, "0x01");
                sleep(1);
            } else if (isContainString(HG_BLE_LIGHT_WHITE, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLightCCT_HOMEGY(fd, deviceAddr, sceneId);
                sleep(1);
            }
        }
    }
    return true;
}

bool deleteSceneActions(const char* sceneId, JSON* actions) {
    size_t actionCount = JSON_ArrayCount(actions);
    char commonDevices[1200] = {'\0'};
    uint8_t  i = 0;
    for (i = 0; i < actionCount; i++) {
        LogInfo((get_localtime_now()),("[ACTION %d]", i));
        JSON* action     = JSON_ArrayGetObject(actions, i);
        char* deviceAddr = JSON_GetText(action, "entityAddr");
        LogInfo((get_localtime_now()),("    [deleteSceneActions] deviceAddr  = %s", deviceAddr));
        LogInfo((get_localtime_now()),("    [deleteSceneActions] commonDevices = %s", commonDevices));
        if (deviceAddr != NULL) {
            if (!isContainString(commonDevices, deviceAddr)) {
                ble_delSceneLocalToDevice(fd, deviceAddr, sceneId);
                sleep(1);
                strcat(commonDevices, deviceAddr);
            }
        }
    }
    return true;
}

bool addSceneCondition(const char* sceneId, JSON* condition) {
    char *pid = JSON_GetText(condition, "pid");
    char *dpAddr = JSON_GetText(condition, "dpAddr");
    int dpValue   = JSON_GetNumber(condition, "dpValue");
    if (isMatchString(pid, HG_BLE_SENSOR_MOTION)) {
        if (dpValue == 0) {
            ble_callSceneLocalToDevice(fd, dpAddr, sceneId, "00", 0);
        } else {
            ble_callSceneLocalToDevice(fd, dpAddr, sceneId, "01", 0);
        }
    } else {
        ble_callSceneLocalToDevice(fd, dpAddr, sceneId, "01", dpValue);
    }
    return true;
}

bool deleteSceneCondition(const char* sceneId, JSON* condition) {
    char* p = cJSON_PrintUnformatted(condition);
    char* dpAddr = JSON_GetText(condition, "dpAddr");
    int dpValue  = JSON_GetNumber(condition, "dpValue");
    ble_callSceneLocalToDevice(fd, dpAddr, "0000", "00", dpValue);
    sleep(1);
    return true;
}

bool addSceneLC(const char* payload) {
    bool ret = false;
    JSON* packet  = JSON_Parse(payload);
    char* sceneId = JSON_GetText(packet, "id");
    int sceneType = String2Int(JSON_GetText(packet, "sceneType"));
    JSON* actions = JSON_GetObject(packet, "actions");
    JSON* conditions = JSON_GetObject(packet, "conditions");
    ret = addSceneActions(sceneId, actions);
    if (ret && sceneType == SceneTypeOneOfConds) {
        JSON* condition = JSON_ArrayGetObject(conditions, 0);
        ret = addSceneCondition(sceneId, condition);
    }

    JSON_Delete(packet);
    return ret;
}

bool delSceneLC(const char* payload) {
    bool ret = false;
    JSON* packet = JSON_Parse(payload);
    char* sceneId = JSON_GetText(packet, "sceneId");
    JSON* actions = JSON_GetObject(packet, "actions");
    JSON* conditions = JSON_GetObject(packet, "conditions");
    logInfo("[delSceneLC] sceneId = %s", sceneId);
    ret = deleteSceneActions(sceneId, actions);
    if (ret) {
        JSON* condition = JSON_ArrayGetObject(conditions, 0);
        ret = deleteSceneCondition(sceneId, condition);
    }
    JSON_Delete(packet);
    return ret;
}

bool addDevice(const char* payload)
{
    bool check_flag = false;
    JSON_Value *object = NULL;
    JSON_Object *object_dictMeta = NULL;
    JSON_Object *object_protocol_para = NULL;
    object = json_parse_string(payload);
    object_protocol_para = json_object_get_object(json_object(object),KEY_PROTOCOL);
    object_dictMeta = json_object_get_object(object_protocol_para,KEY_DICT_META); 
    const char* deviceKey_ = json_object_get_string(object_protocol_para,KEY_DEVICE_KEY);
    const char* MAC_t = json_object_get_string(object_protocol_para,KEY_MAC);
    const char* address_ = json_object_get_string(object_protocol_para,KEY_UNICAST);
    const char* deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);
    const char* pid_ = json_object_get_string(object_protocol_para,KEY_PID);

    if(isMatchString(pid_,RD_BLE_SENSOR_TEMP) || isMatchString(pid_,RD_BLE_SENSOR_DOOR))
    {
        LogInfo((get_localtime_now()),("RD_BLE_SENSOR_TEMP or RD_BLE_SENSOR_DOOR"));
        set_inf_DV_for_GW(fd,address_,pid_,deviceKey_);
    }
    else if(isMatchString(pid_,RD_BLE_SENSOR_SMOKE)||
            isMatchString(pid_,RD_BLE_SENSOR_MOTION)|| 
            isContainString(RD_BLE_LIGHT_RGB,pid_)|| 
            isMatchString(pid_,RD_BLE_LIGHT_WHITE) ||
            isContainString(RD_BLE_LIGHT_WHITE_TEST,pid_))
    {
        check_flag = ble_saveInforDeviceForGatewayRangDong(fd,address_,"A000");
        sleep(1);
        check_flag = AES_get_code_encrypt_sensor(fd,(char*)MAC_t,(char*)address_);
        sleep(1);
        set_inf_DV_for_GW(fd,address_,pid_,deviceKey_);
    }
    else if(isContainString(HG_BLE,pid_))  //device  HOMEGY
    {
        LogInfo((get_localtime_now()),("ADD Devices HG"));

        set_inf_DV_for_GW(fd,address_,pid_,deviceKey_);
        sleep(1);
        check_flag = ble_saveInforDeviceForGatewayHomegy(fd,address_,"A000");
        if(check_flag)
        {
            LogInfo((get_localtime_now()),("Success to save Device %s into GW",deviceID));
        }
        else
        {
            LogWarn((get_localtime_now()),( "Failed to save Device %s into GW",deviceID));
        }
        sleep(1);
        check_flag = AES_get_code_encrypt(fd,(char*)MAC_t,(char*)address_);
        if(check_flag)
        {
            LogInfo((get_localtime_now()),("Success send code AES at %s",address_));
        }
        else
        {
            LogWarn((get_localtime_now()),( "Failed send code AES at %s",address_));
        }
    }  
}

bool addGroupNormal(const char* payload)
{
    logInfo("Start adding devices to a group");
    bool check_flag = false;
    JSON_Value *object = NULL;
    object = json_parse_string(payload);
    char *groupAddress_ = (char *)json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    char *device_inf =  (char*)json_object_get_string(json_object(object), KEY_DEVICES_GROUP);
    char *pid =  (char*)json_object_get_string(json_object(object), KEY_PID);

    int size_device_inf = 0,i=0;
    char** str = str_split(device_inf,'|',&size_device_inf);

    char index_t[2];
    for(i=0;i<(size_device_inf-1);i++)
    {
        memset(index_t,'\0',sizeof(index_t));
        Int2String(i+1,index_t);
        char *deviceID_ =   *(str+i);
        char *address_device = getAndressFromDeviceID(string_InfoAndress,deviceID_);
        if (isMatchString(pid,HG_BLE_LIGHT_WHITE) || isContainString(RD_BLE_LIGHT_WHITE_TEST,pid) || isContainString(RD_BLE_LIGHT_RGB,pid)) {
            ble_addDeviceToGroupLightCCT_HOMEGY(fd,groupAddress_,address_device,address_device);
        } else {
            logInfo("Device pid is not supported to add group. PID = %s", pid);
        }
        usleep(DELAY_BETWEEN_ACTIONS_GROUP_NORMAL_uSECONDS);
    }
    logInfo("End adding devices to a group");
    return true;
}

bool delGroupNormal(const char* payload)
{
    bool check_flag = false;
    JSON_Value *object = NULL;
    object = json_parse_string(payload);
    char *groupAddress_ = (char *)json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    char *device_inf =  (char*)json_object_get_string(json_object(object), DEVICES_INF);

    LogInfo((get_localtime_now()),("groupAddress_ %s",groupAddress_));
    LogInfo((get_localtime_now()),("device_inf %s",device_inf));

    int size_device_inf = 0,i=0;
    char** str = str_split(device_inf,'|',&size_device_inf);
    printf("size_device_inf %d\n",size_device_inf );

    char index_t[2];
    for(i=0;i<(size_device_inf-1);i++)
    {
        memset(index_t,'\0',sizeof(index_t));
        Int2String(i+1,index_t);
        LogInfo((get_localtime_now()),("index_t %s",index_t));
        char *deviceID_ =   *(str+i);
        LogInfo((get_localtime_now()),("deviceID_ %s",deviceID_));
        char *address_device = getAndressFromDeviceID(string_InfoAndress,deviceID_);
        LogInfo((get_localtime_now()),("address_device %s",address_device));
  
        check_flag = ble_deleteDeviceToGroupLightCCT_HOMEGY(fd,groupAddress_,address_device,address_device);
        if(!check_flag)
        {
            LogError((get_localtime_now()),("Failed to del group for light \n"));
        }
        usleep(DELAY_BETWEEN_ACTIONS_GROUP_NORMAL_uSECONDS);
    }
}

bool updateGroupNormal(const char* payload)
{
    bool check_flag = false;
    JSON_Value *object = NULL;
    object = json_parse_string(payload);


    char *groupAddress_ = (char *)json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    char *device_inf =  (char*)json_object_get_string(json_object(object), KEY_DEVICES_GROUP);
    char *device_inf_compare =  (char*)json_object_get_string(json_object(object), KEY_DEVICES_COMPARE_GROUP);

    LogInfo((get_localtime_now()),("groupAddress_ %s",groupAddress_));
    LogInfo((get_localtime_now()),("device_inf %s",device_inf));

    char index_t[3];
    int size_device_inf = 0,size_device_inf_compare = 0,i=0;


    char** str_device_inf = str_split(device_inf,'|',&size_device_inf);
    printf("size_device_inf %d\n",size_device_inf );

    char** str_device_inf_compare = str_split(device_inf_compare,'|',&size_device_inf_compare);
    printf("size_device_inf_compare %d\n",size_device_inf_compare );


    //del device into group
    for(i=0;i<(size_device_inf_compare-1);i++)
    {
        memset(index_t,'\0',sizeof(index_t));
        Int2String(i+1,index_t);
        LogInfo((get_localtime_now()),("index_t %s",index_t));
        char *deviceID_ =   *(str_device_inf_compare+i);
        if(!isContainString(device_inf,deviceID_))
        {
            LogInfo((get_localtime_now()),("deviceID_ %s",deviceID_));
            char *address_device = getAndressFromDeviceID(string_InfoAndress,deviceID_);
            LogInfo((get_localtime_now()),("address_device %s",address_device));
      
            check_flag = ble_deleteDeviceToGroupLightCCT_HOMEGY(fd,groupAddress_,address_device,address_device);
            if(!check_flag)
            {
                LogError((get_localtime_now()),("Failed to del group for light \n"));
            }
            usleep(DELAY_BETWEEN_ACTIONS_GROUP_NORMAL_uSECONDS);
        }
    }


    //add new device into group
    for(i=0;i<(size_device_inf-1);i++)
    {
        memset(index_t,'\0',sizeof(index_t));
        Int2String(i+1,index_t);
        LogInfo((get_localtime_now()),("index_t %s",index_t));
        char *deviceID_ =   *(str_device_inf+i);
        if(!isContainString(device_inf_compare,deviceID_))
        {
            LogInfo((get_localtime_now()),("deviceID_ %s",deviceID_));
            char *address_device = getAndressFromDeviceID(string_InfoAndress,deviceID_);
            LogInfo((get_localtime_now()),("address_device %s",address_device));
      
            check_flag = ble_addDeviceToGroupLightCCT_HOMEGY(fd,groupAddress_,address_device,address_device);
            if(!check_flag)
            {
                LogError((get_localtime_now()),("Failed to add group for light \n"));
            }
            sleep(1);
        }
    }

    free_fields(str_device_inf,size_device_inf);
    free_fields(str_device_inf_compare,size_device_inf_compare);
    return true;
}

bool addGroupLink(const char* payload)
{
    LogInfo((get_localtime_now()),("addGroupLink start..."));
    bool check_flag = false;
    JSON_Value *object = NULL;
    object = json_parse_string(payload);
    char *groupAddress_ = (char *)json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    char *device_inf =  (char*)json_object_get_string(json_object(object), DEVICES_INF);
    char *pid =  (char*)json_object_get_string(json_object(object), KEY_PID);

    LogInfo((get_localtime_now()),("groupAddress_ %s",groupAddress_));
    LogInfo((get_localtime_now()),("device_inf %s",device_inf));
    LogInfo((get_localtime_now()),("pid %s",pid));

    int size_device_inf = 0,i=0;
    char** str = str_split(device_inf,'|',&size_device_inf);

    char index_t[2];
    for(i=0;i<(size_device_inf-1)/2;i++)
    {
        memset(index_t,'\0',sizeof(index_t));
        Int2String(i+1,index_t);
        LogInfo((get_localtime_now()),("index_t %s",index_t));
        char *deviceID_ =   *(str+i*2);
        char *dpid_     =   *(str+i*2+1);
        LogInfo((get_localtime_now()),("deviceID_ %s",deviceID_));
        LogInfo((get_localtime_now()),("dpid_ %s",dpid_));
        char *address_device;
        char *address_element;
        getAndressFromDeviceAndDpid(&address_device,Json_Value_InfoDevices,deviceID_,"1");
        getAndressFromDeviceAndDpid(&address_element,Json_Value_InfoDevices,deviceID_,dpid_);
        LogInfo((get_localtime_now()),("    address_device %d = %s",i,address_device));
        LogInfo((get_localtime_now()),("    address_element %d =%s",i,address_element));
        check_flag = ble_addDeviceToGroupLink(fd,groupAddress_,address_device,address_element);
        sleep(1);
    }

    LogInfo((get_localtime_now()),("addGroupLink done!"));
    return true;
}

bool delGroupLink(const char* payload)
{
    bool check_flag = false;
    JSON_Value *object = NULL;
    object = json_parse_string(payload);
    char *groupAddress_ = (char *)json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    char *device_inf =  (char*)json_object_get_string(json_object(object), DEVICES_INF);

    LogInfo((get_localtime_now()),("groupAddress_ %s",groupAddress_));
    LogInfo((get_localtime_now()),("device_inf %s",device_inf));

    int size_device_inf = 0,i=0;
    char** str = str_split(device_inf,'|',&size_device_inf);
    printf("size_device_inf %d\n",size_device_inf );

    for(i=0;i<(size_device_inf-1)/2;i++)
    {
        char *deviceID_ =   *(str+i*2);
        char *dpid_     =   *(str+i*2+1);
        LogInfo((get_localtime_now()),("    deviceID_ %s",deviceID_));
        LogInfo((get_localtime_now()),("    dpid_ %s",dpid_));

        char *address_device;
        char *address_elemet;
        getAndressFromDeviceAndDpid(&address_device,Json_Value_InfoDevices,deviceID_,"1");
        getAndressFromDeviceAndDpid(&address_elemet,Json_Value_InfoDevices,deviceID_,dpid_);
        LogInfo((get_localtime_now()),("    address_device %s",address_device));
        LogInfo((get_localtime_now()),("    address_elemet %s",address_elemet));
        check_flag = ble_deleteDeviceToGroupLightCCT_HOMEGY(fd,groupAddress_,address_device,address_elemet);
        if(!check_flag)
        {
            LogError((get_localtime_now()),("Failed to del group for light \n"));
        }
        sleep(1);
    }
}

bool updateGroupLink(const char* payload)
{
    bool check_flag = false;
    JSON_Value *object = NULL;
    object = json_parse_string(payload);


    char *groupAddress_ = (char *)json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    char *device_inf                    =  (char*)json_object_get_string(json_object(object), KEY_DEVICES_GROUP);
    char *device_inf_compare            =  (char*)json_object_get_string(json_object(object), KEY_DEVICES_COMPARE_GROUP);

    char *device_inf_process            =  (char*)calloc(strlen(device_inf)+1,sizeof(char));
    char *device_inf_process_compare    =  (char*)calloc(strlen(device_inf_compare)+1,sizeof(char));


    strcpy(device_inf_process,device_inf);
    strcpy(device_inf_process_compare,device_inf_compare);

    LogInfo((get_localtime_now()),("groupAddress_ %s",groupAddress_));
    LogInfo((get_localtime_now()),("device_inf %s",device_inf));
    LogInfo((get_localtime_now()),("device_inf_compare %s",device_inf_compare));
    LogInfo((get_localtime_now()),("device_inf_process_compare %s",device_inf_process_compare));
    int size_device_inf = 0,size_device_inf_compare = 0,i=0;


    char** str_device_inf = str_split(device_inf_process,'|',&size_device_inf);
    char** str_device_inf_compare = str_split(device_inf_process_compare,'|',&size_device_inf_compare);
    LogInfo((get_localtime_now()),("size_device_inf %d",size_device_inf));
    LogInfo((get_localtime_now()),("size_device_inf_compare %d",size_device_inf_compare));
    for(i=0;i<(size_device_inf_compare-1)/2;i++)
    {
        LogInfo((get_localtime_now()),("deviceID_compare[%d] %s",i,str_device_inf_compare[i*2]));
        LogInfo((get_localtime_now()),("dpid_compare[%d] %s",i,str_device_inf_compare[i*2+1]));
    }
    char *temp_str;
    char *temp_compare;
    //del device into group
    for(i=0;i<(size_device_inf_compare-1)/2;i++)
    {

        char *deviceID_compare =   *(str_device_inf_compare+i*2);
        char *dpid_compare     =   *(str_device_inf_compare+i*2+1);

        temp_str = my_strcat(deviceID_compare,"|");
        temp_compare = my_strcat(temp_str,dpid_compare);
        LogInfo((get_localtime_now()),("DEL_GROUP[%d]",i));
        LogInfo((get_localtime_now()),("    deviceID_compare %s",deviceID_compare));
        LogInfo((get_localtime_now()),("    dpid_compare %s",dpid_compare));
        LogInfo((get_localtime_now()),("    temp_compare %s",temp_compare));
        LogInfo((get_localtime_now()),("    device_inf %s",device_inf));
        if(!isContainString(device_inf,temp_compare))
        {
            char *address_device;
            char *address_elemet;
            LogInfo((get_localtime_now()),("    DEL into %d",i));

            getAndressFromDeviceAndDpid(&address_device,Json_Value_InfoDevices,deviceID_compare,"1");
            getAndressFromDeviceAndDpid(&address_elemet,Json_Value_InfoDevices,deviceID_compare,dpid_compare);
            LogInfo((get_localtime_now()),("    address_device %s",address_device));
            LogInfo((get_localtime_now()),("    address_elemet %s",address_elemet));
            check_flag = ble_deleteDeviceToGroupLightCCT_HOMEGY(fd,groupAddress_,address_device,address_elemet);
            if(!check_flag)
            {
                LogError((get_localtime_now()),("Failed to add group for light \n"));
            }
            sleep(1);
        }
    }


    //add new device into group
    for(i=0;i<(size_device_inf-1)/2;i++)
    {
        
        char *deviceID_ =   *(str_device_inf+i*2);
        char *dpid_     =   *(str_device_inf+i*2+1);

        temp_str = my_strcat(deviceID_,"|");
        temp_compare = my_strcat(temp_str,dpid_);
        LogInfo((get_localtime_now()),("ADD_GROUP[%d]",i));
        LogInfo((get_localtime_now()),("    deviceID_ %s",deviceID_));
        LogInfo((get_localtime_now()),("    dpid_ %s",dpid_));
        LogInfo((get_localtime_now()),("    temp_compare %s",temp_compare));
        LogInfo((get_localtime_now()),("    device_inf_compare %s",device_inf_compare));
        if(!isContainString(device_inf_compare,temp_compare) )
        {
            char *address_device;
            char *address_element;
            getAndressFromDeviceAndDpid(&address_device,Json_Value_InfoDevices,deviceID_,"1");
            getAndressFromDeviceAndDpid(&address_element,Json_Value_InfoDevices,deviceID_,dpid_);
            LogInfo((get_localtime_now()),("    address_device %s",address_device));
            LogInfo((get_localtime_now()),("    address_element %s",address_element));
            check_flag = ble_addDeviceToGroupLink(fd,groupAddress_,address_device,address_element);
            if(!check_flag)
            {
                LogError((get_localtime_now()),("Failed to add group for light \n"));
            }
            
            sleep(1);
        }
    }

    free_fields(str_device_inf,size_device_inf);
    free_fields(str_device_inf_compare,size_device_inf_compare);
    return true;
}

bool getInfoDeviceFromDatabase()
{
    char *message;
    char *topic;
    int reponse = 0;
    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_DEVICES,MOSQ_ActResponse,"pre_detect->object",1111,"payload");
    get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_DEVICES,MOSQ_Request);
    mqttLocalPublish(topic, message);
    free(message);
    free(topic);
    return true;
}

bool getInfoAddressFromDeviceDatabase(char **result)
{
    int size_object = 0,i=0,j= 0;

    JSON_Value  *result_value       = json_parse_string(*result);
    JSON_Object *result_object      = json_value_get_object(result_value);
    JSON_Object *json_object        = json_value_get_object(Json_Value_InfoDevices);
    size_object = json_object_get_count(json_object);

    char dotString[50] = {'/0'};
    char* pid;


    for(i = 0;i < size_object; i++)
    {
        const char* deviceID = json_object_get_name(json_object,i);
        sprintf(dotString,"%s.%s",deviceID,KEY_DICT_META);
        JSON_Object *temp_object = json_object_dotget_object(json_object,dotString);

        if(isMatchString(deviceID,KEY_GROUP_NORMAL))
        {
            strcpy((char*)pid,KEY_GROUP_NORMAL);
        }
        else
        {
            sprintf(dotString,"%s.%s.%s",deviceID,KEY_DICT_INFO,KEY_PID);        
            pid = (char *)json_object_dotget_string(json_object,dotString); 
        }
        
        if(isContainString(BLE_LIGHT,pid))
        {
            char *temp_address = (char *)json_object_get_string(temp_object,TYPE_DPID_CTR_LIGHT);
            sprintf(dotString,"%s.%s",temp_address,KEY_DEVICE_ID);
            json_object_dotset_string(result_object,dotString,deviceID);

            sprintf(dotString,"%s.%s",temp_address,KEY_DP_ID);
            json_object_dotset_string(result_object,dotString,TYPE_DPID_CTR_LIGHT);
        }
        else if(isMatchString(KEY_GROUP_NORMAL,pid))
        {
            char *temp_name = (char *)json_object_get_name(temp_object,0);
            char *temp_address = (char *)json_object_get_string(temp_object,temp_name);

            sprintf(dotString,"%s.%s",temp_address,KEY_DEVICE_ID);
            json_object_dotset_string(result_object,dotString,deviceID);

            sprintf(dotString,"%s.%s",temp_address,KEY_DP_ID);
            json_object_dotset_string(result_object,dotString,temp_name);
        }
        else
        {
            int size_temp_object = json_object_get_count(temp_object);
            for (j = 0; j <size_temp_object; j++)
            {
                char *temp_name = (char *)json_object_get_name(temp_object,j);
                char *temp_address = (char *)json_object_get_string(temp_object,temp_name);

                sprintf(dotString,"%s.%s",temp_address,KEY_DEVICE_ID);
                json_object_dotset_string(result_object,dotString,deviceID);

                sprintf(dotString,"%s.%s",temp_address,KEY_DP_ID);
                json_object_dotset_string(result_object,dotString,temp_name);
            }
        }

    }

    char *serialized_string = NULL;
    serialized_string = json_serialize_to_string_pretty(result_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);

    return true;
}

char *getDeviceIDfromAddress(char *string_InfoAndress, char* address_)
{
    JSON_Value  *result_value       = json_parse_string(string_InfoAndress);
    JSON_Object *result_object      = json_value_get_object(result_value);
    if(json_object_has_value(result_object,address_))
    {
        char temp_name[50];
        sprintf(temp_name,"%s.%s",address_,KEY_DEVICE_ID);
        return (char *)json_object_dotget_string(result_object,temp_name);
    }
    else
        return NULL;
}

char *getDpIDfromAddress(char *string_InfoAndress, char* address_)
{
    // LogInfo((get_localtime_now()),("string_InfoAndress = %s \n",string_InfoAndress));
    JSON_Value  *result_value       = json_parse_string(string_InfoAndress);
    JSON_Object *result_object      = json_value_get_object(result_value);
    if(json_object_has_value(result_object,address_))
    {
        char temp_name[50];
        sprintf(temp_name,"%s.%s",address_,KEY_DP_ID);
        return (char *)json_object_dotget_string(result_object,temp_name);
    }
    else
        return NULL;    
}

char *getAndressFromDeviceID(char *string_InfoAndress, char* deviceID)
{
    JSON_Value  *result_value       = json_parse_string(string_InfoAndress);
    JSON_Object *result_object      = json_value_get_object(result_value);
    char count_object = json_object_get_count(result_object);
    // LogInfo((get_localtime_now()),("count_object = %d \n",count_object));
    char i = 0;
    for(i = 0;i<count_object;i++)
    {
        char temp_name[50] = {'\0'};
        sprintf(temp_name,"%s.%s",json_object_get_name(result_object,i),KEY_DEVICE_ID);
        // LogInfo((get_localtime_now()),("temp_name = %s \n",temp_name));
        if(isMatchString(json_object_dotget_string(result_object,temp_name),deviceID))
        {
            // LogInfo((get_localtime_now()),("check ok \n"));
            return (char *)json_object_get_name(result_object,i);
        }
    }
    return NULL;    
}

char *getAndressDeviceFromDeviceID(JSON_Value *Json_Value_InfoDevices, char* deviceID)
{
    int i = 0;
    JSON_Object *json_object_t      = json_value_get_object(Json_Value_InfoDevices);
    int size_object = json_object_get_count(json_object_t);
    char dotString[50] = {'/0'};
    for(i = 0;i < size_object; i++)
    {
        const char* deviceID_compare = json_object_get_name(json_object_t,i);
        if(isMatchString(deviceID_compare,deviceID))
        {
            sprintf(dotString,"%s.%s",deviceID,KEY_DICT_META);
            JSON_Object *temp_object = json_object_dotget_object(json_object_t,dotString);
            if(json_object_has_value(temp_object,"1"))
            {
                return (char *)json_object_get_string(temp_object,"1");
            }
            else if(json_object_has_value(temp_object,"20"))
            {
                return (char *)json_object_get_string(temp_object,"20");
            }
            else
            {
                return NULL;
            }
        }
    }
    return NULL;
}
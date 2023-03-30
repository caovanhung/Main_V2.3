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

const char* SERVICE_NAME = SERVICE_BLE;
FILE *fptr;
extern int fd;
char rcv_uart_buff[MAXLINE];

struct mosquitto * mosq;
struct Queue *queue_received;
struct Queue *queue_UART;
pthread_mutex_t mutex_lock_t                = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue             = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutex_lock_mosq_t           = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_MosqQueue         = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutex_lock_RX               = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_RX_Queue          = PTHREAD_COND_INITIALIZER;

static char g_senderId[50] = {0};

bool addSceneLC(JSON* packet);
bool delSceneLC(JSON* packet);
bool addSceneActions(const char* sceneId, JSON* actions);
bool deleteSceneActions(const char* sceneId, JSON* actions);
bool addSceneCondition(const char* sceneId, JSON* condition);
bool deleteSceneCondition(const char* sceneId, JSON* condition);

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
            for (int i = 0; i < frameCount; i++) {
                // Print the frame information
                char str[3000];
                BLE_PrintFrame(str, &bleFrames[i]);
                struct state_element *tmp = malloc(sizeof(struct state_element));
                InfoDataUpdateDevice *InfoDataUpdateDevice_t = malloc(sizeof(InfoDataUpdateDevice));
                int type_devices_repons = check_form_recived_from_RX(tmp, &bleFrames[i]);
                logInfo("Frame #%d: type: %d, %s", i, type_devices_repons, str);
                switch(type_devices_repons) {
                    case GW_RESPONSE_DEVICE_STATE: {
                        JSON* packet = JSON_CreateObject();
                        JSON* devicesArray = JSON_AddArrayToObject(packet, "devices");
                        JSON* arrayItem = JSON_ArrayAddObject(devicesArray);
                        JSON_SetText(arrayItem, "deviceAddr", tmp->address_element);
                        int onlineState = bleFrames[i].onlineState? TYPE_DEVICE_ONLINE : TYPE_DEVICE_OFFLINE;
                        JSON_SetNumber(arrayItem, "deviceState", onlineState);
                        if (bleFrames[i].sendAddr2 != 0) {
                            arrayItem = JSON_ArrayAddObject(devicesArray);
                            char str[5];
                            sprintf(str, "%04x", bleFrames[i].sendAddr2);
                            JSON_SetText(arrayItem, "deviceAddr", str);
                            onlineState = bleFrames[i].onlineState2? TYPE_DEVICE_ONLINE : TYPE_DEVICE_OFFLINE;
                            JSON_SetNumber(arrayItem, "deviceState", onlineState);
                        }
                        sendPacketTo(SERVICE_CORE, GW_RESPONSE_DEVICE_STATE, packet);
                        JSON_Delete(packet);
                        break;
                    }

                    case GW_RESPONSE_DIM_LED_SWITCH_HOMEGY:
                    case GW_RESPONSE_DEVICE_CONTROL: {
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetText(packet, "dpAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpValue", tmp->dpValue);
                        if (g_senderId[0] != 0) {
                            JSON_SetNumber(packet, "causeType", 1);
                            JSON_SetText(packet, "causeId", g_senderId);
                        } else {
                            JSON_SetNumber(packet, "causeType", tmp->causeType);
                            JSON_SetText(packet, "causeId", tmp->causeId);
                        }
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        g_senderId[0] = 0;
                        break;
                    }
                    case GW_RESPONSE_SMOKE_SENSOR: {
                        logInfo("GW_RESPONSE_SMOKE_SENSOR");
                        uint8_t hasSmoke = bleFrames[i].param[1];
                        uint8_t battery = bleFrames[i].param[2]? 100 : 0;

                        // Send smoke detection to core service
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_SMOKE_SENSOR_DETECT);
                        JSON_SetNumber(packet, "dpValue", hasSmoke);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);

                        // Send battery to core service
                        packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_SMOKE_SENSOR_BATTERY);
                        JSON_SetNumber(packet, "dpValue", battery);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    case GW_RESPONSE_SENSOR_ENVIRONMENT: {
                        logInfo("GW_RESPONSE_SENSOR_ENVIRONMENT");
                        uint16_t temperature = ((uint16_t)bleFrames[i].param[1] << 8) | bleFrames[i].param[2];
                        uint16_t humidity = ((uint16_t)bleFrames[i].param[3] << 8) | bleFrames[i].param[4];

                        // Send temperature to core service
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_ENVIRONMENT_SENSOR_TEMPER);
                        JSON_SetNumber(packet, "dpValue", (double)temperature);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);

                        // Send battery to core service
                        packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_ENVIRONMENT_SENSOR_HUMIDITY);
                        JSON_SetNumber(packet, "dpValue", (double)humidity);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    case GW_RESPONSE_SENSOR_DOOR_ALARM:
                    case GW_RESPONSE_SENSOR_DOOR_DETECT: {
                        logInfo("GW_RESPONSE_SENSOR_DOOR");
                        // Send information to core service
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        if (type_devices_repons == GW_RESPONSE_SENSOR_DOOR_ALARM) {
                            JSON_SetNumber(packet, "dpId", TYPE_DPID_DOOR_SENSOR_ALRM);
                            JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]);
                        } else {
                            JSON_SetNumber(packet, "dpId", TYPE_DPID_DOOR_SENSOR_DETECT);
                            JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]? 0 : 1);
                        }
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    case GW_RESPONSE_SENSOR_BATTERY: {
                        logInfo("GW_RESPONSE_SENSOR_BATTERY");
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_BATTERY_SENSOR);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[2]);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    case GW_RESPONSE_SENSOR_PIR_DETECT: {
                        logInfo("GW_RESPONSE_SENSOR_PIR_DETECT");
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_PIR_SENSOR_DETECT);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    case GW_RESPONSE_SENSOR_PIR_LIGHT: {
                        logInfo("GW_RESPONSE_SENSOR_PIR_LIGHT");
                        JSON* packet = JSON_CreateObject();
                        uint16_t lightIntensity = ((uint16_t)bleFrames[i].param[1] << 8) | bleFrames[i].param[2];
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_PIR_SENSOR_LUX);
                        JSON_SetNumber(packet, "dpValue", lightIntensity);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    case GW_RESPONSE_SET_TIME_SENSOR_PIR:
                    case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_UNSUCCESS:
                    case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_SUCCESS:
                    case GW_RESPONSE_SAVE_GATEWAY_HG:
                    case GW_RESPONSE_SAVE_GATEWAY_RD:
                    {
                        // InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                        // InfoDataUpdateDevice_t->deviceID = tmp->address_element;
                        // InfoDataUpdateDevice_t->value = tmp->value;
                        // InfoDataUpdateDevice_t->dpID = tmp->address_element;
                        // response = type_devices_repons;
                        break;
                    }

                    case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE:
                    case GW_RESPONSE_SCENE_LC_WRITE_INTO_DEVICE:
                    case GW_RESPONSE_DEVICE_KICKOUT:
                    {
                        // InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                        // InfoDataUpdateDevice_t->deviceID = (char *)deviceID;
                        // InfoDataUpdateDevice_t->value = tmp->value;
                        // InfoDataUpdateDevice_t->dpID = tmp->address_element;
                        // response = type_devices_repons;
                        break;
                    }
                    case GW_RESPONSE_ADD_GROUP_LIGHT: {
                        logInfo("GW_RESPONSE_ADD_GROUP_LIGHT");
                        char deviceAddr[10];
                        char groupAddr[10];
                        sprintf(deviceAddr, "%02X%02X", bleFrames[i].param[1], bleFrames[i].param[2]);
                        sprintf(groupAddr, "%02X%02X", bleFrames[i].param[3], bleFrames[i].param[4]);
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "deviceAddr", deviceAddr);
                        JSON_SetText(packet, "groupAddr", groupAddr);
                        sendPacketTo(SERVICE_CORE, type_devices_repons, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    default:
                        LogError((get_localtime_now()),("Error detect"));
                        break;
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
    sleep(1);
    // Send request to CORE service to ask it sending request to sync device state
    sendToService(SERVICE_CORE, TYPE_SYNC_DEVICE_STATE, "");
    while(xRun!=0)
    {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if(size_queue > 0)
        {
            int dpValue_int = 0;
            int i = 0,j=0,count=0;

            char* recvMsg = (char *)dequeue(queue_received);
            printf("\n\r");
            logInfo("Received message: %s", recvMsg);

            JSON* recvPacket = JSON_Parse(recvMsg);
            int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
            JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));
            char* dpAddr, *groupAddr;
            switch (reqType) {
                case TYPE_CTR_DEVICE:
                case TYPE_CTR_GROUP_NORMAL: {
                    StringCopy(g_senderId, JSON_GetText(payload, "senderId"));  // Save senderId to response to Core service after receiving response from device
                    char* pid = JSON_GetText(payload, "pid");
                    cJSON* dictDPs = cJSON_GetObjectItem(payload, "dictDPs");
                    int lightness = -1, colorTemperature = -1;
                    JSON_ForEach(o, dictDPs) {
                        int dpId = JSON_GetNumber(o, "id");
                        dpAddr = JSON_GetText(o, "addr");
                        int dpValue = JSON_GetNumber(o, "value");
                        char valueStr[5];
                        sprintf(valueStr, "%d", dpValue);
                        if (isContainString(HG_BLE_SWITCH, pid)) {
                            ble_controlOnOFF_SW(dpAddr, dpValue);
                        } else if (isContainString(HG_BLE_CURTAIN, pid) || dpId == 20) {
                            ble_controlOnOFF(dpAddr, valueStr);
                        } else if (dpId == 24) {
                            ble_controlHSL(dpAddr, valueStr);
                        } else if (dpId == 21) {
                            ble_controlModeBlinkRGB(dpAddr, valueStr);
                        } else if (dpId == 22) {
                            lightness = dpValue;
                        } else if (dpId == 23) {
                            colorTemperature = dpValue;
                        }

                        if (lightness >= 0 && colorTemperature >= 0) {
                            ble_controlCTL(dpAddr, lightness, colorTemperature);
                        }
                    }
                    break;
                }
                case TYPE_CTR_SCENE: {
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    int sceneType = JSON_GetNumber(payload, "sceneType");
                    int state = JSON_GetNumber(payload, "state");
                    if (sceneType == SceneTypeManual) {
                        logInfo("Executing LC scene %s", sceneId);
                        ble_callSceneLocalToHC("FFFF", sceneId);
                    } else {
                        // Enable/Disable scene
                    }
                    break;
                }
                case TYPE_ADD_GROUP_NORMAL:
                case TYPE_DEL_GROUP_NORMAL: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        char* deviceAddr = JSON_GetText(device, "deviceAddr");
                        if (reqType == TYPE_ADD_GROUP_NORMAL) {
                            ble_addDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                        } else {
                            ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                        }
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_NORMAL: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                        ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                        ble_addDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                    }
                    break;
                }
                case TYPE_DEL_GROUP_LINK:
                case TYPE_ADD_GROUP_LINK: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        char* deviceAddr = JSON_GetText(device, "deviceAddr");
                        char* dpAddr = JSON_GetText(device, "dpAddr");
                        if (reqType == TYPE_ADD_GROUP_LINK) {
                            ble_addDeviceToGroupLink(groupAddr, deviceAddr, dpAddr);
                        } else {
                            ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, dpAddr);
                        }
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_LINK: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                        char* dpAddr = JSON_GetText(dpNeedRemove, "dpAddr");
                        ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, dpAddr);
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                        char* dpAddr = JSON_GetText(dpNeedAdd, "dpAddr");
                        ble_addDeviceToGroupLink(groupAddr, deviceAddr, dpAddr);
                    }
                    break;
                }
                case TYPE_ADD_DEVICE: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    char* devicePid = JSON_GetText(payload, "devicePid");
                    char* deviceKey = JSON_GetText(payload, "deviceKey");
                    set_inf_DV_for_GW(deviceAddr, devicePid, deviceKey);
                    break;
                }
                case TYPE_DEL_DEVICE: {
                    char* deviceId = JSON_GetText(payload, "deviceId");
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    if (deviceAddr) {
                        setResetDeviceSofware(deviceAddr);
                        logInfo("Deleted deviceId: %s, address: %s", deviceId, deviceAddr);
                    }
                    break;
                }
                case TYPE_ADD_SCENE: {
                    addSceneLC(payload);
                    break;
                }
                case TYPE_DEL_SCENE: {
                    delSceneLC(payload);
                    break;
                }
                case TYPE_UPDATE_SCENE: {
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    JSON* actionsNeedRemove = JSON_GetObject(payload, "actionsNeedRemove");
                    JSON* actionsNeedAdd = JSON_GetObject(payload, "actionsNeedAdd");
                    bool ret = deleteSceneActions(sceneId, actionsNeedRemove);
                    if (ret) {
                        ret = addSceneActions(sceneId, actionsNeedAdd);
                    }
                    if (ret && JSON_HasObjectItem(payload, "conditionNeedRemove")) {
                        JSON* conditionNeedRemove = JSON_GetObject(payload, "conditionNeedRemove");
                        JSON* conditionNeedAdd = JSON_GetObject(payload, "conditionNeedAdd");
                        ret = deleteSceneCondition(sceneId, conditionNeedRemove);
                        if (ret) {
                            ret = addSceneCondition(sceneId, conditionNeedAdd);
                        }
                    }
                    break;
                }
                case TYPE_SYNC_DEVICE_STATE: {
                    JSON_ForEach(dpAddr, payload) {
                        BLE_GetDeviceOnOffState(dpAddr->valuestring);
                    }
                    break;
                }
                case TYPE_DIM_LED_SWITCH: {
                    char* dpAddr = JSON_GetText(payload, "dpAddr");
                    int value = JSON_GetNumber(payload, "value");
                    ble_dimLedSwitch_HOMEGY(dpAddr, value);
                }
                case TYPE_LOCK_AGENCY: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    int value = JSON_GetNumber(payload, "value");
                    ble_logDeivce(deviceAddr, value);
                    break;
                }
                case TYPE_LOCK_KIDS: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    JSON* dps = JSON_GetObject(payload, "lock");
                    JSON_ForEach(dp, dps) {
                        if (dp->string) {
                            ble_logTouch(deviceAddr, atoi(dp->string), dp->valueint);
                        }
                    }
                    break;
                }
            }
            cJSON_Delete(recvPacket);
            cJSON_Delete(payload);
        }
        else if(size_queue == MAX_SIZE_NUMBER_QUEUE) {
            pthread_mutex_unlock(&mutex_lock_t);
        } else {
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
                    ble_setSceneLocalToDeviceSwitch(deviceAddr, sceneId, "64", element_count, param);
                    sleep(1);
                    strcat(commonDevices, deviceAddr);
                }
            } else if (isContainString(RD_BLE_LIGHT_WHITE_TEST, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLight_RANGDONG(deviceAddr, sceneId, "0x00");
                sleep(1);
            } else if (isContainString(RD_BLE_LIGHT_RGB, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLight_RANGDONG(deviceAddr, sceneId, "0x01");
                sleep(1);
            } else if (isContainString(HG_BLE_LIGHT_WHITE, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLightCCT_HOMEGY(deviceAddr, sceneId);
                sleep(1);
            }
        }
    }
    return true;
}

bool deleteSceneActions(const char* sceneId, JSON* actions) {
    if (sceneId && actions) {
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
                    ble_delSceneLocalToDevice(deviceAddr, sceneId);
                    sleep(1);
                    strcat(commonDevices, deviceAddr);
                }
            }
        }
    }
    return true;
}

bool addSceneCondition(const char* sceneId, JSON* condition) {
    if (sceneId && condition) {
        char *pid = JSON_GetText(condition, "pid");
        char *dpAddr = JSON_GetText(condition, "dpAddr");
        int dpValue   = JSON_GetNumber(condition, "dpValue");
        if (isMatchString(pid, HG_BLE_SENSOR_MOTION)) {
            if (dpValue == 0) {
                ble_callSceneLocalToDevice(dpAddr, sceneId, "00", 0);
            } else {
                ble_callSceneLocalToDevice(dpAddr, sceneId, "01", 0);
            }
        } else {
            ble_callSceneLocalToDevice(dpAddr, sceneId, "01", dpValue);
        }
    }
    return true;
}

bool deleteSceneCondition(const char* sceneId, JSON* condition) {
    if (sceneId && condition) {
        char* p = cJSON_PrintUnformatted(condition);
        char* dpAddr = JSON_GetText(condition, "dpAddr");
        int dpValue  = JSON_GetNumber(condition, "dpValue");
        ble_callSceneLocalToDevice(dpAddr, "0000", "00", dpValue);
        sleep(1);
    }
    return true;
}

bool addSceneLC(JSON* packet) {
    bool ret = false;
    if (packet) {
        char* sceneId = JSON_GetText(packet, "id");
        int sceneType = String2Int(JSON_GetText(packet, "sceneType"));
        JSON* actions = JSON_GetObject(packet, "actions");
        JSON* conditions = JSON_GetObject(packet, "conditions");
        ret = addSceneActions(sceneId, actions);
        if (ret && sceneType == SceneTypeOneOfConds) {
            JSON* condition = JSON_ArrayGetObject(conditions, 0);
            ret = addSceneCondition(sceneId, condition);
        }
    }
    return ret;
}

bool delSceneLC(JSON* packet) {
    bool ret = false;
    if (packet) {
        char* sceneId = JSON_GetText(packet, "sceneId");
        JSON* actions = JSON_GetObject(packet, "actions");
        JSON* conditions = JSON_GetObject(packet, "conditions");
        logInfo("[delSceneLC] sceneId = %s", sceneId);
        ret = deleteSceneActions(sceneId, actions);
        if (ret) {
            JSON* condition = JSON_ArrayGetObject(conditions, 0);
            ret = deleteSceneCondition(sceneId, condition);
        }
    }
    return ret;
}

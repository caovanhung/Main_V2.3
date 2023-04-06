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
static bool g_mosqInitDone = false;
static bool g_mosqIsConnected = false;

struct mosquitto * mosq;
struct Queue *g_mqttMsgQueue;
struct Queue *g_lowPrioMqttMsgQueue;
static struct Queue* g_bleFrameQueue;
static pthread_mutex_t g_mqttMsgQueueMutex = PTHREAD_MUTEX_INITIALIZER;

static char g_senderId[50] = {0};

bool addSceneLC(JSON* packet);
bool delSceneLC(JSON* packet);
bool addSceneActions(const char* sceneId, JSON* actions);
bool deleteSceneActions(const char* sceneId, JSON* actions);
bool addSceneCondition(const char* sceneId, JSON* condition);
bool deleteSceneCondition(const char* sceneId, JSON* condition);
void sendGroupResp(const char* groupAddr, const char* deviceAddr, int status);
void Ble_ProcessPacket();

void On_mqttConnect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc) {
        LogError((get_localtime_now()),("Error with result code: %d\n", rc));
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_DEVICE_BLE, 0);
    g_mosqIsConnected = true;
}

void On_mqttMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    char* topic = msg->topic;
    list_t* tmp = String_Split(topic, "\\");
    pthread_mutex_lock(&g_mqttMsgQueueMutex);
    if (tmp->count == 3 && tmp->items[2] >= 100) {
        int size_queue = get_sizeQueue(g_lowPrioMqttMsgQueue);
        if (size_queue < MAX_SIZE_NUMBER_QUEUE) {
            enqueue(g_lowPrioMqttMsgQueue, (uint8_t*)msg->payload);
        }
    } else {
        int size_queue = get_sizeQueue(g_mqttMsgQueue);
        if (size_queue < MAX_SIZE_NUMBER_QUEUE) {
            enqueue(g_mqttMsgQueue, (uint8_t*)msg->payload);
        }
    }
    pthread_mutex_unlock(&g_mqttMsgQueueMutex);
}

void Mosq_Init(const char* clientId) {
    int rc = 0;
    mosquitto_lib_init();
    mosq = mosquitto_new(clientId, true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    mosquitto_connect_callback_set(mosq, On_mqttConnect);
    mosquitto_message_callback_set(mosq, On_mqttMessage);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if (rc != 0) {
        LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
        return;
    }
    logInfo("Mosq_Init() done");
    g_mosqInitDone = true;
}

void Mosq_ProcessLoop() {
    if (g_mosqInitDone) {
        int rc = mosquitto_loop(mosq, 5, 1);
        if (rc != 0) {
            logError("mosquitto_loop error: %d.", rc);
            g_mosqInitDone = false;
        }
    } else {
        mosquitto_destroy(mosq);
        Mosq_Init(SERVICE_BLE);
    }
}


/*
 * Function to send response from main thread to CORE service.
 * We need this function because Mqtt service is running in "MqttTask" thread and we shouldn't
 * publish in other threads. Any thread wanted to send packet to CORE service should call the
 * function Mosq_MarkPacketToSend()
 */
static JSON* g_respPacket = NULL;
static int g_respType = 0;
void Mosq_SendResponse() {
    if (g_respType > 0) {
        sendPacketTo(SERVICE_CORE, g_respType, g_respPacket);
        g_respType = 0;
    }
}

void Mosq_MarkPacketToSend(int reqType, JSON* packet) {
    g_respType = reqType;
    g_respPacket = packet;
    while (g_respType > 0);
}


/*
 * Function to receive BLE frames from device => Push to bleFrameQueue
 * This function must be called frequently in a loop
 */
void BLE_ReceivePacket() {
    char givenStr[MAX_PACKAGE_SIZE];
    int len_uart = UART0_Recv( fd, rcv_uart_buff, MAX_PACKAGE_SIZE);
    if ( len_uart > 0) {
        // Ignore frame 0x91b5
        if (len_uart >= 4 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0xb5) {
            return;
        }
        if (len_uart >= 10 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0x5d && rcv_uart_buff[9] == 0x00) {
            return;
        }
        strcpy(givenStr, (char *)Hex2String(rcv_uart_buff, len_uart));
        enqueue(g_bleFrameQueue, givenStr);
    }
}


/*
 * Function to get BLE frames from bleFrameQueue, process frames then publish message to CORE service
 * This function must be called frequently in a loop
 */
void Ble_ProcessPacket()
{
    uint8_t msgHex[MAX_PACKAGE_SIZE];
    ble_rsp_frame_t bleFrames[MAX_FRAME_COUNT];
    int size_queue = 0;
    unsigned char *hex_uart_rev;
    unsigned char *hex_netkey;

    size_queue = get_sizeQueue(g_bleFrameQueue);
    if (size_queue > 0) {
        char* recvPackage = (char *)dequeue(g_bleFrameQueue);
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
            int frameType = check_form_recived_from_RX(tmp, &bleFrames[i]);
            logInfo("Frame #%d: type: %d, %s", i, frameType, str);
            switch(frameType) {
                case GW_RESPONSE_DEVICE_STATE: {
                    JSON* packet = JSON_CreateObject();
                    JSON* devicesArray = JSON_AddArray(packet, "devices");
                    JSON* arrayItem = JArr_AddObject(devicesArray);
                    JSON_SetText(arrayItem, "deviceAddr", tmp->address_element);
                    int onlineState = bleFrames[i].onlineState? TYPE_DEVICE_ONLINE : TYPE_DEVICE_OFFLINE;
                    JSON_SetNumber(arrayItem, "deviceState", onlineState);
                    if (bleFrames[i].sendAddr2 != 0) {
                        arrayItem = JArr_AddObject(devicesArray);
                        char str[5];
                        sprintf(str, "%04X", bleFrames[i].sendAddr2);
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
                    sendPacketTo(SERVICE_CORE, frameType, packet);
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
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);

                    // Send battery to core service
                    packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpId", TYPE_DPID_SMOKE_SENSOR_BATTERY);
                    JSON_SetNumber(packet, "dpValue", battery);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
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
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);

                    // Send battery to core service
                    packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpId", TYPE_DPID_ENVIRONMENT_SENSOR_HUMIDITY);
                    JSON_SetNumber(packet, "dpValue", (double)humidity);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_SENSOR_DOOR_ALARM:
                case GW_RESPONSE_SENSOR_DOOR_DETECT: {
                    logInfo("GW_RESPONSE_SENSOR_DOOR");
                    // Send information to core service
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    if (frameType == GW_RESPONSE_SENSOR_DOOR_ALARM) {
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_DOOR_SENSOR_ALRM);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]);
                    } else {
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_DOOR_SENSOR_DETECT);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]? 0 : 1);
                    }
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_SENSOR_BATTERY: {
                    logInfo("GW_RESPONSE_SENSOR_BATTERY");
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpId", TYPE_DPID_BATTERY_SENSOR);
                    JSON_SetNumber(packet, "dpValue", bleFrames[i].param[2]);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_SENSOR_PIR_DETECT: {
                    logInfo("GW_RESPONSE_SENSOR_PIR_DETECT");
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpId", TYPE_DPID_PIR_SENSOR_DETECT);
                    JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
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
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_SET_TIME_SENSOR_PIR:
                case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_UNSUCCESS:
                case GW_RESPONSE_ACTIVE_DEVICE_HG_RD_SUCCESS:
                {
                    // InfoDataUpdateDevice_t->type_reponse = TYPE_DATA_REPONSE_STATE;
                    // InfoDataUpdateDevice_t->deviceID = tmp->address_element;
                    // InfoDataUpdateDevice_t->value = tmp->value;
                    // InfoDataUpdateDevice_t->dpID = tmp->address_element;
                    // response = frameType;
                    break;
                }

                // case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE:
                case GW_RESPONSE_ADD_SCENE:
                // case GW_RESPONSE_DEVICE_KICKOUT:
                {
                    logInfo("GW_RESPONSE_ADD_SCENE");
                    JSON* packet = JSON_CreateObject();
                    char sceneId[10];
                    sprintf(sceneId, "%02X%02X", bleFrames[i].param[1], bleFrames[i].param[2]);
                    JSON_SetText(packet, "sceneId", sceneId);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "status", bleFrames[i].param[0]? 0: 1);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_GROUP: {
                    logInfo("GW_RESPONSE_GROUP");
                    char deviceAddr[10];
                    char groupAddr[10];
                    sprintf(deviceAddr, "%02X%02X", bleFrames[i].param[1], bleFrames[i].param[2]);
                    sprintf(groupAddr, "%02X%02X", bleFrames[i].param[3], bleFrames[i].param[4]);
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", deviceAddr);
                    JSON_SetText(packet, "groupAddr", groupAddr);
                    JSON_SetNumber(packet, "status", bleFrames[i].param[0]);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                default:
                    logError("Packet type is not supported: %d", frameType);
                    break;
            }
        }
    }
}


void* MqttTask(void* p)
{
    Mosq_Init(SERVICE_BLE);

    while (1) {
        BLE_ReceivePacket();  // Receive BLE frames from device => Push to bleFrameQueue
        Ble_ProcessPacket();  // Get BLE frames from bleFrameQueue => publish message to CORE service
        Mosq_ProcessLoop();   // Receive mqtt message from CORE service => Push to mqttMsgQueue
        Mosq_SendResponse();
        usleep(100);
    }
    return p;
}


int main( int argc,char ** argv )
{
    int mqttSizeQueue = 0, lowPrioMqttSizeQueue = 0;
    pthread_t thr[4];
    int err,xRun = 1;
    int rc[4];
    g_mqttMsgQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    g_lowPrioMqttMsgQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    g_bleFrameQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    //Init for uart()
    fd = UART0_Open(fd,VAR_PORT_UART);
    if (fd == -1) {
        logError("Cannot open UART0");
    }
    
    do {
        err = UART0_Init(fd,115200,0,8,1,'N');
        usleep(50000);
    }
    while (-1 == err || -1 == fd);
    logInfo("UART0_Init() done");
    usleep(50000);

    rc[1] = pthread_create(&thr[1], NULL, MqttTask, NULL);
    usleep(50000);

    pthread_mutex_init(&g_mqttMsgQueueMutex, NULL);

    while (g_mosqIsConnected == false);
    sleep(3);
    // Send request to CORE service to ask it sending request to sync device state
    // sendToService(SERVICE_CORE, TYPE_SYNC_DEVICE_STATE, "{}");
    // set_inf_DV_for_GW("F302", "BLEHG010201", "C3E1817DD2C37BEF489363C335E7710C");

    while (xRun!=0) {
        pthread_mutex_lock(&g_mqttMsgQueueMutex);
        int mqttSizeQueue = get_sizeQueue(g_mqttMsgQueue);
        int lowPrioMqttSizeQueue = get_sizeQueue(g_lowPrioMqttMsgQueue);
        char* recvMsg;
        if (mqttSizeQueue > 0) {
            recvMsg = (char *)dequeue(g_mqttMsgQueue);
        } else if (lowPrioMqttSizeQueue > 0) {
            recvMsg = (char *)dequeue(g_lowPrioMqttMsgQueue);
        } else {
            pthread_mutex_unlock(&g_mqttMsgQueueMutex);     // Release g_mqttMsgQueue for other thread
            continue;
        }
        pthread_mutex_unlock(&g_mqttMsgQueueMutex);     // Release g_mqttMsgQueue for other thread

        printf("\n\r");
        logInfo("Received message: %s", recvMsg);

        int i = 0,j=0,count=0;
        JSON* recvPacket = JSON_Parse(recvMsg);
        int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
        JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));
        if (payload) {
            char* dpAddr, *groupAddr;
            switch (reqType) {
                case TYPE_ADD_GW: {
                    provison_inf PRV;
                    ble_getInfoProvison(&PRV, payload);
                    ble_bindGateWay(&PRV);
                    break;
                }
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
                        bool ret = false;
                        if (reqType == TYPE_ADD_GROUP_NORMAL) {
                            ret = ble_addDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                        } else {
                            ret = ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                        }
                        // Response TIMEOUT status to Core service
                        if (ret == false) {
                            sendGroupResp(groupAddr, deviceAddr, 1);
                        }
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_NORMAL: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    bool ret = false;
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                        ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                        ret = ble_addDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, deviceAddr);
                        // Response TIMEOUT status to Core service
                        if (ret == false) {
                            sendGroupResp(groupAddr, deviceAddr, 1);
                        }
                    }
                    break;
                }
                case TYPE_ADD_GROUP_LINK:
                case TYPE_DEL_GROUP_LINK: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        char* deviceAddr = JSON_GetText(device, "deviceAddr");
                        char* dpAddr = JSON_GetText(device, "dpAddr");
                        bool ret = false;
                        if (reqType == TYPE_ADD_GROUP_LINK) {
                            ret = ble_addDeviceToGroupLink(groupAddr, deviceAddr, dpAddr);
                        } else {
                            ret = ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, dpAddr);
                        }
                        // Response TIMEOUT status to Core service
                        if (ret == false) {
                            sendGroupResp(groupAddr, dpAddr, 1);
                        }
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_LINK: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    bool ret = false;
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                        char* dpAddr = JSON_GetText(dpNeedRemove, "dpAddr");
                        ble_deleteDeviceToGroupLightCCT_HOMEGY(groupAddr, deviceAddr, dpAddr);
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                        char* dpAddr = JSON_GetText(dpNeedAdd, "dpAddr");
                        ret = ble_addDeviceToGroupLink(groupAddr, deviceAddr, dpAddr);
                        if (ret == false) {
                            sendGroupResp(groupAddr, dpAddr, 1);
                        }
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
                    if (ret && JSON_HasKey(payload, "conditionNeedRemove")) {
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
                case TYPE_GET_DEVICE_STATUS: {
                    char* addr = JSON_GetText(payload, "addr");
                    BLE_GetDeviceOnOffState(addr);
                }
            }
        } else {
            logError("Payload is NULL");
        }
        cJSON_Delete(recvPacket);
        cJSON_Delete(payload);
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
    logInfo("[addSceneActions] sceneId = %s", sceneId);
    int actionCount = JArr_Count(actions);
    JSON* mergedActions = JSON_CreateArray();
    for (i = 0; i < actionCount; i++) {
        JSON* action = JArr_GetObject(actions, i);
        char* pid = JSON_GetText(action, "pid");
        char* deviceAddr = JSON_GetText(action, "entityAddr");
        int dpId = JSON_GetNumber(action, "dpId");
        int dpValue = JSON_GetNumber(action, "dpValue");

        if (pid != NULL) {
            if (isContainString(HG_BLE_SWITCH, pid)) {
                int dpParam = (dpId - 1)*0x10 + dpValue;
                dpParam = dpParam << 24;
                JSON* mergedActionItem = JArr_FindByText(mergedActions, "deviceAddr", deviceAddr);
                if (mergedActionItem == NULL) {
                    mergedActionItem = JArr_AddObject(mergedActions);
                    JSON_SetText(mergedActionItem, "deviceAddr", deviceAddr);
                    JSON_SetNumber(mergedActionItem, "param", dpParam);
                    JSON_SetNumber(mergedActionItem, "dpCount", 1);
                } else {
                    uint32_t newParam = (uint32_t)JSON_GetNumber(mergedActionItem, "param");
                    newParam = dpParam | (newParam >> 8);
                    JSON_SetNumber(mergedActionItem, "param", newParam);
                    JSON_SetNumber(mergedActionItem, "dpCount", JSON_GetNumber(mergedActionItem, "dpCount") + 1);
                }
            } else if (isContainString(RD_BLE_LIGHT_WHITE_TEST, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLight_RANGDONG(deviceAddr, sceneId, "0x00");
            } else if (isContainString(RD_BLE_LIGHT_RGB, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLight_RANGDONG(deviceAddr, sceneId, "0x01");
            } else if (isContainString(HG_BLE_LIGHT_WHITE, pid) && dpId == 20) {
                ble_setSceneLocalToDeviceLightCCT_HOMEGY(deviceAddr, sceneId);
            }
        }
    }

    if (JArr_Count(mergedActions) > 0) {
        JSON_ForEach(item, mergedActions) {
            char* addr = JSON_GetText(item, "deviceAddr");
            int dpCount = JSON_GetNumber(item, "dpCount");
            uint32_t param = (uint32_t)JSON_GetNumber(item, "param");
            ble_setSceneLocalToDeviceSwitch(sceneId, addr, dpCount, param);
        }
    }

    return true;
}

bool deleteSceneActions(const char* sceneId, JSON* actions) {
    if (sceneId && actions) {
        size_t actionCount = JArr_Count(actions);
        char commonDevices[1200] = {'\0'};
        uint8_t  i = 0;
        for (i = 0; i < actionCount; i++) {
            LogInfo((get_localtime_now()),("[ACTION %d]", i));
            JSON* action     = JArr_GetObject(actions, i);
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
            JSON* condition = JArr_GetObject(conditions, 0);
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
            JSON* condition = JArr_GetObject(conditions, 0);
            ret = deleteSceneCondition(sceneId, condition);
        }
    }
    return ret;
}


void sendGroupResp(const char* groupAddr, const char* deviceAddr, int status) {
    JSON* packet = JSON_CreateObject();
    JSON_SetText(packet, "deviceAddr", deviceAddr);
    JSON_SetText(packet, "groupAddr", groupAddr);
    JSON_SetNumber(packet, "status", 1);
    Mosq_MarkPacketToSend(GW_RESPONSE_GROUP, packet);
    JSON_Delete(packet);
}
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
extern int g_gatewayFds[GATEWAY_NUM];
char rcv_uart_buff[MAXLINE];
static bool g_mosqInitDone = false;
static bool g_mosqIsConnected = false;

struct mosquitto * mosq;
struct Queue *g_mqttMsgQueue;
struct Queue *g_lowPrioMqttMsgQueue;
static struct Queue* g_bleFrameQueue;

static JSON *g_checkRespList;

bool addSceneLC(JSON* packet);
bool delSceneLC(JSON* packet);
bool addSceneActions(const char* sceneId, JSON* actions);
bool deleteSceneActions(const char* sceneId, JSON* actions);
bool addSceneCondition(const char* sceneId, JSON* condition);
bool deleteSceneCondition(const char* sceneId, JSON* condition);
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
    list_t* tmp = String_Split(topic, "/");
    if (tmp->count == 3 && atoi(tmp->items[2]) >= 100) {
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
 * Function to receive BLE frames from device => Push to bleFrameQueue
 * This function must be called frequently in a loop
 */
void BLE_ReceivePacket() {
    char givenStr[MAX_PACKAGE_SIZE];
    int len_uart = UART0_Recv(g_gatewayFds[0], rcv_uart_buff, MAX_PACKAGE_SIZE);
    if ( len_uart > 0 && len_uart < MAX_PACKAGE_SIZE) {
        if (len_uart == 1 || len_uart > 998) {
            return;
        }
        // Ignore frame 0x91b5
        if (len_uart >= 4 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0xb5) {
            return;
        }
        if (len_uart >= 4 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0x9d) {
            return;
        }
        if (len_uart >= 10 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0x5d && rcv_uart_buff[9] == 0x00) {
            return;
        }
        StringCopy(givenStr, (char *)Hex2String(rcv_uart_buff, len_uart));
        enqueue(g_bleFrameQueue, givenStr);
    }

    len_uart = UART0_Recv(g_gatewayFds[1], rcv_uart_buff, MAX_PACKAGE_SIZE);
    if ( len_uart > 0 && len_uart < MAX_PACKAGE_SIZE) {
        if (len_uart == 1 || len_uart > 998) {
            return;
        }
        // Ignore frame 0x91b5
        if (len_uart >= 4 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0xb5) {
            return;
        }
        if (len_uart >= 4 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0x9d) {
            return;
        }
        if (len_uart >= 10 && rcv_uart_buff[2] == 0x91 && rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0x5d && rcv_uart_buff[9] == 0x00) {
            return;
        }
        StringCopy(givenStr, (char *)Hex2String(rcv_uart_buff, len_uart));
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
        String2HexArr(recvPackage, msgHex);
        printf("\n\r");
        logInfo("Received package from UART: %s", recvPackage);
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
            BLE_SetDeviceResp(frameType, bleFrames[i].sendAddr, 0);
            switch(frameType) {
                case GW_RESPONSE_DEVICE_STATE: {
                    JSON* packet = JSON_CreateObject();
                    JSON* devicesArray = JSON_AddArray(packet, "devices");
                    JSON* arrayItem = JArr_CreateObject(devicesArray);
                    JSON_SetText(arrayItem, "deviceAddr", tmp->address_element);
                    int onlineState = bleFrames[i].onlineState? TYPE_DEVICE_ONLINE : TYPE_DEVICE_OFFLINE;
                    JSON_SetNumber(arrayItem, "deviceState", onlineState);
                    if (bleFrames[i].sendAddr2 != 0) {
                        arrayItem = JArr_CreateObject(devicesArray);
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
                case GW_RESP_DEVICE_STATUS: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetText(packet, "dpAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpValue", tmp->dpValue);
                    if (bleFrames[i].frameSize >= 8 && bleFrames[i].param[0] == 0x82 && bleFrames[i].param[1] == 0x02) {
                        JSON_SetNumber(packet, "causeType", EV_CAUSE_TYPE_APP);
                    } else {
                        JSON_SetNumber(packet, "causeType", EV_CAUSE_TYPE_SYNC);
                    }
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
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
                case GW_RESPONSE_DEVICE_KICKOUT: {
                    logInfo("GW_RESPONSE_DEVICE_KICKOUT");
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                // case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE:
                case GW_RESPONSE_ADD_SCENE: {
                    logInfo("GW_RESPONSE_ADD_SCENE");
                    JSON* packet = JSON_CreateObject();
                    char sceneId[10];
                    sprintf(sceneId, "%02X%02X", bleFrames[i].param[1], bleFrames[i].param[2]);
                    JSON_SetText(packet, "sceneId", sceneId);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "status", bleFrames[i].param[0]);
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
                case GW_RESPONSE_SET_TTL: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "status", 0);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_IR: {
                    logInfo("GW_RESPONSE_IR");
                    uint16_t brandId = ((uint16_t)bleFrames[i].param[4] << 8) + bleFrames[i].param[3];
                    uint8_t  remoteId = bleFrames[i].param[5];
                    uint8_t  temp = bleFrames[i].param[6];
                    uint8_t  mode = bleFrames[i].param[7];
                    uint8_t  fan = bleFrames[i].param[8] >> 4;
                    uint8_t  swing = bleFrames[i].param[8] & 0x0F;
                    logInfo("brandId=%d, remoteId=%d, temp=%d, mode=%d, fan=%d, swing=%d", brandId, remoteId, temp, mode, fan, swing);

                    JSON* packet = JSON_CreateObject();
                    JSON_SetNumber(packet, "brandId", brandId);
                    JSON_SetNumber(packet, "remoteId", remoteId);
                    JSON_SetNumber(packet, "temp", temp);
                    JSON_SetNumber(packet, "mode", mode);
                    JSON_SetNumber(packet, "fan", fan);
                    JSON_SetNumber(packet, "swing", swing);
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

// Add device that need to check response to response list
void addDeviceToRespList(int reqType, const char* itemId, const char* addr) {
    ASSERT(itemId); ASSERT(addr);
    JSON* item = JArr_CreateObject(g_checkRespList);
    JSON_SetNumber(item, "reqType", reqType);
    JSON_SetNumber(item, "reqTime", timeInMilliseconds());
    JSON_SetText(item, "itemId", itemId);
    JSON_SetText(item, "addr", addr);
    JSON_SetNumber(item, "status", -1);
}



void checkResponseLoop() {
    long long int currentTime = timeInMilliseconds();
    int respCount = JArr_Count(g_checkRespList);

    // Loop through all request that need to check response
    for (int i = 0; i < respCount; i++) {
        JSON* respItem = JArr_GetObject(g_checkRespList, i);
        long long int reqTime  = JSON_GetNumber(respItem, "reqTime");
        int reqType = JSON_GetNumber(respItem, "reqType");
        char* addr = JSON_GetText(respItem, "addr");
        char* itemId = JSON_GetText(respItem, "itemId");
        int status = JSON_GetNumber(respItem, "status");
        if (status >= 0) {
            if (status > 0) {
                // Send FAILED response to CORE service
                JSON* p = JSON_CreateObject();
                if (reqType == GW_RESPONSE_GROUP) {
                    JSON_SetText(p, "groupAddr", itemId);
                    JSON_SetText(p, "deviceAddr", addr);
                    JSON_SetNumber(p, "status", status);
                    sendPacketTo(SERVICE_CORE, reqType, p);
                }
                JSON_Delete(p);
            }
            // Remove this respItem from response list
            JArr_RemoveIndex(g_checkRespList, i);
            break;
        }
    }
}

int main( int argc,char ** argv )
{
    int mqttSizeQueue = 0, lowPrioMqttSizeQueue = 0;
    pthread_t thr[4];
    int err,xRun = 1;
    int rc[4];
    g_checkRespList = JSON_CreateArray();
    g_mqttMsgQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    g_lowPrioMqttMsgQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    g_bleFrameQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    //Init for uart()
    g_gatewayFds[0] = UART0_Open(g_gatewayFds[0], UART_GATEWAY1);
    g_gatewayFds[1] = UART0_Open(g_gatewayFds[1], UART_GATEWAY2);
    if (g_gatewayFds[0] == -1) {
        logError("Cannot open %s", UART_GATEWAY1);
    }
    if (g_gatewayFds[1] == -1) {
        logError("Cannot open %s", UART_GATEWAY2);
    }
    
    do {
        err = UART0_Init(g_gatewayFds[0], 115200, 0, 8, 1, 'N');
        usleep(50000);
    }
    while (-1 == err || -1 == g_gatewayFds[0]);
    logInfo("Init %s done", UART_GATEWAY1);
    usleep(50000);

    do {
        err = UART0_Init(g_gatewayFds[1], 115200, 0, 8, 1, 'N');
        usleep(50000);
    }
    while (-1 == err || -1 == g_gatewayFds[1]);
    logInfo("Init %s done", UART_GATEWAY2);
    usleep(50000);
    Mosq_Init(SERVICE_BLE);
    sleep(3);
    // set_inf_DV_for_GW(0, "Đ00", "BLEHGAA0201", "077C533EA69371AFFA435AA0CB5B3121");
    while (xRun!=0) {
        BLE_SendUartFrameLoop();
        BLE_ReceivePacket();  // Receive BLE frames from device => Push to bleFrameQueue
        Ble_ProcessPacket();  // Get BLE frames from bleFrameQueue => publish message to CORE service
        Mosq_ProcessLoop();   // Receive mqtt message from CORE service => Push to mqttMsgQueue

        int mqttSizeQueue = get_sizeQueue(g_mqttMsgQueue);
        int lowPrioMqttSizeQueue = get_sizeQueue(g_lowPrioMqttMsgQueue);
        char* recvMsg;
        if (mqttSizeQueue > 0) {
            recvMsg = (char *)dequeue(g_mqttMsgQueue);
        } else if (lowPrioMqttSizeQueue > 0) {
            recvMsg = (char *)dequeue(g_lowPrioMqttMsgQueue);
        } else {
            continue;
        }

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
                    ble_bindGateWay(0, &PRV);
                    sleep(1);
                    ble_bindGateWay(1, &PRV);
                    break;
                }
                case TYPE_CTR_DEVICE: {
                    char* pid = JSON_GetText(payload, "pid");
                    cJSON* dictDPs = cJSON_GetObjectItem(payload, "dictDPs");
                    int lightness = -1, colorTemperature = -1;
                    int irCommandType = 0, irBrandId = 0, irRemoteId = 0, irTemp = 0, irMode = 0, irFan = 0, irSwing = 1;
                    JSON_ForEach(o, dictDPs) {
                        int dpId = JSON_GetNumber(o, "id");
                        dpAddr = JSON_GetText(o, "addr");
                        int dpValue = JSON_GetNumber(o, "value");
                        char valueStr[5];
                        sprintf(valueStr, "%d", dpValue);
                        if (isContainString(HG_BLE_SWITCH, pid)) {
                            GW_HgSwitchOnOff(dpAddr, dpValue);
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
                        } else if (dpId == 3) {
                            irCommandType = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] CommandType = %d", irCommandType);
                        } else if (dpId == 1) {
                            irBrandId = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] BrandId = %d", irBrandId);
                        } else if (dpId == 2) {
                            irRemoteId = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] RemoteId = %d", irRemoteId);
                        } else if (dpId == 103) {
                            irTemp = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] TempOrParam = %d", irTemp);
                        } else if (dpId == 102) {
                            irMode = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] Mode = %d", irMode);
                        } else if (dpId == 104) {
                            irFan = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] Fan = %d", irFan);
                        } else if (dpId == 105) {
                            irSwing = dpValue;
                            logInfo("[TYPE_CTR_DEVICE] Swing = %d", irSwing);
                        } else if (dpId == 106) {
                            char* command = JSON_GetText(o, "valueString");
                            if (StringCompare(pid, HG_BLE_IR)) {
                                GW_ControlIRCmd(command);
                            } else {
                                irTemp = atoi(command);
                            }
                        }

                        if (lightness >= 0 && colorTemperature >= 0) {
                            ble_controlCTL(dpAddr, lightness, colorTemperature);
                        }
                    }
                    if (irCommandType > 0 && irBrandId > 0 && irRemoteId > 0) {
                        GW_ControlIR(dpAddr, irCommandType, irBrandId, irRemoteId, irTemp, irMode, irFan, irSwing);
                    }
                    break;
                }
                case TYPE_CTR_GROUP_NORMAL: {
                    char* pid = JSON_GetText(payload, "pid");
                    cJSON* dictDPs = cJSON_GetObjectItem(payload, "dictDPs");
                    int lightness = -1, colorTemperature = -1;
                    int irCommandType = 0, irBrandId = 0, irRemoteId = 0, irTemp = 0, irMode = 0, irFan = 0, irSwing = 1;
                    JSON_ForEach(o, dictDPs) {
                        int dpId = JSON_GetNumber(o, "id");
                        dpAddr = JSON_GetText(o, "addr");
                        int dpValue = JSON_GetNumber(o, "value");
                        char valueStr[5];
                        sprintf(valueStr, "%d", dpValue);
                        if (isContainString(HG_BLE_SWITCH, pid)) {
                            GW_HgSwitchOnOff_NoResp(dpAddr, dpValue);
                        } else if (isContainString(HG_BLE_CURTAIN, pid) || dpId == 20) {
                            GW_CtrlGroupLightOnoff(dpAddr, dpValue);
                        } else if (dpId == 24) {
                            ble_controlHSL(dpAddr, valueStr);
                        } else if (dpId == 21) {
                            ble_controlModeBlinkRGB(dpAddr, valueStr);
                        } else if (dpId == 22) {
                            lightness = dpValue;
                        } else if (dpId == 23) {
                            colorTemperature = dpValue;
                        }
                    }
                    if (lightness >= 0 && colorTemperature >= 0) {
                        GW_CtrlGroupLightCT(dpAddr, lightness, colorTemperature);
                    }
                    break;
                }
                case TYPE_CTR_SCENE: {
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    int state = JSON_GetNumber(payload, "state");
                    if (state >= 2) {
                        logInfo("Executing LC scene %s", sceneId);
                        ble_callSceneLocalToHC("FFFF", sceneId);
                    } else {
                        // Enable/Disable scene
                        char* pid = JSON_GetText(payload, "pid");
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        double dpValue = JSON_GetNumber(payload, "dpValue");
                        ble_callSceneLocalToDevice(dpAddr, sceneId, "00", dpValue);
                    }
                    break;
                }
                case TYPE_ADD_GROUP_NORMAL:
                case TYPE_DEL_GROUP_NORMAL: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        int gwIndex = JSON_GetNumber(device, "gwIndex");
                        char* deviceAddr = JSON_GetText(device, "deviceAddr");
                        if (reqType == TYPE_ADD_GROUP_NORMAL) {
                            ble_addDeviceToGroupLightCCT_HOMEGY(gwIndex, groupAddr, deviceAddr, deviceAddr);
                        } else {
                            ble_deleteDeviceToGroupLightCCT_HOMEGY(gwIndex, groupAddr, deviceAddr, deviceAddr);
                        }
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_NORMAL: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        int gwIndex = JSON_GetNumber(dpNeedRemove, "gwIndex");
                        char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                        ble_deleteDeviceToGroupLightCCT_HOMEGY(gwIndex, groupAddr, deviceAddr, deviceAddr);
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        int gwIndex = JSON_GetNumber(dpNeedAdd, "gwIndex");
                        char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                        ble_addDeviceToGroupLightCCT_HOMEGY(gwIndex, groupAddr, deviceAddr, deviceAddr);
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                    }
                    break;
                }
                case TYPE_ADD_GROUP_LINK:
                case TYPE_DEL_GROUP_LINK: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        int gwIndex = JSON_GetNumber(device, "gwIndex");
                        char* deviceAddr = JSON_GetText(device, "deviceAddr");
                        char* dpAddr = JSON_GetText(device, "dpAddr");
                        if (reqType == TYPE_ADD_GROUP_LINK) {
                            ble_addDeviceToGroupLink(gwIndex, groupAddr, deviceAddr, dpAddr);
                        } else {
                            ble_deleteDeviceToGroupLightCCT_HOMEGY(gwIndex, groupAddr, deviceAddr, dpAddr);
                        }
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_LINK: {
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        int gwIndex = JSON_GetNumber(dpNeedRemove, "gwIndex");
                        char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                        char* dpAddr = JSON_GetText(dpNeedRemove, "dpAddr");
                        ble_deleteDeviceToGroupLightCCT_HOMEGY(gwIndex, groupAddr, deviceAddr, dpAddr);
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        int gwIndex = JSON_GetNumber(dpNeedAdd, "gwIndex");
                        char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                        char* dpAddr = JSON_GetText(dpNeedAdd, "dpAddr");
                        ble_addDeviceToGroupLink(gwIndex, groupAddr, deviceAddr, dpAddr);
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                    }
                    break;
                }
                case TYPE_ADD_DEVICE: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    char* devicePid = JSON_GetText(payload, "devicePid");
                    char* deviceKey = JSON_GetText(payload, "deviceKey");
                    int gatewayId = JSON_GetNumber(payload, "gatewayId");
                    set_inf_DV_for_GW(gatewayId, deviceAddr, devicePid, deviceKey);
                    if (JSON_HasKey(payload, "command")) {
                        GW_ControlIRCmd(JSON_GetText(payload, "command"));
                    }
                    break;
                }
                case TYPE_DEL_DEVICE: {
                    char* pid = JSON_GetText(payload, "devicePid");
                    char* deviceId = JSON_GetText(payload, "deviceId");
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    if (deviceAddr && !StringCompare(pid, HG_BLE_IR_TV) &&
                                      !StringCompare(pid, HG_BLE_IR_FAN) &&
                                      !StringCompare(pid, HG_BLE_IR_AC)) {
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
                    bool ret = false;
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    JSON* actionsNeedRemove = JSON_GetObject(payload, "actionsNeedRemove");
                    JSON* actionsNeedAdd = JSON_GetObject(payload, "actionsNeedAdd");
                    if (JArr_Count(actionsNeedRemove) > 0) {
                        deleteSceneActions(sceneId, actionsNeedRemove);
                    }
                    if (JArr_Count(actionsNeedAdd) > 0) {
                        addSceneActions(sceneId, actionsNeedAdd);
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
                case TYPE_DIM_LED_SWITCH: {
                    char* dpAddr = JSON_GetText(payload, "dpAddr");
                    int value = JSON_GetNumber(payload, "value");
                    ble_dimLedSwitch_HOMEGY(dpAddr, value);
                    break;
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
                    JSON_ForEach(d, payload) {
                        char* addr = JSON_GetText(d, "addr");
                        GW_GetDeviceOnOffState(addr);
                        addRespTypeToSendingFrame(GW_RESP_DEVICE_STATUS, addr);
                        addPriorityToSendingFrame(2);
                    }
                    break;
                }
                case TYPE_SET_DEVICE_TTL: {
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    GW_SetTTL(gwIndex, JSON_GetText(payload, "deviceAddr"), JSON_GetNumber(payload, "ttl"));
                    break;
                }
            }
        } else {
            logError("Payload is NULL");
        }
        cJSON_Delete(recvPacket);
        cJSON_Delete(payload);
        free(recvMsg);
        usleep(100);
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
        if (JSON_HasKey(action, "pid")) {
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
                        mergedActionItem = JArr_CreateObject(mergedActions);
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
                    // Add this device to response list to check TIMEOUT later
                    addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                } else if (isContainString(RD_BLE_LIGHT_RGB, pid) && dpId == 20) {
                    ble_setSceneLocalToDeviceLight_RANGDONG(deviceAddr, sceneId, "0x01");
                    // Add this device to response list to check TIMEOUT later
                    addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                } else if (isContainString(HG_BLE_LIGHT_WHITE, pid) && dpId == 20) {
                    ble_setSceneLocalToDeviceLightCCT_HOMEGY(deviceAddr, sceneId);
                    // Add this device to response list to check TIMEOUT later
                    addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                }
            }
        }
    }

    if (JArr_Count(mergedActions) > 0) {
        JSON_ForEach(item, mergedActions) {
            char* addr = JSON_GetText(item, "deviceAddr");
            int dpCount = JSON_GetNumber(item, "dpCount");
            uint32_t param = (uint32_t)JSON_GetNumber(item, "param");
            ble_setSceneLocalToDeviceSwitch(sceneId, addr, dpCount, param);
            // Add this device to response list to check TIMEOUT later
            addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
        }
    }

    return true;
}

// bool deleteSceneActions(const char* sceneId, JSON* actions) {
//     if (sceneId && actions) {
//         size_t actionCount = JArr_Count(actions);
//         char commonDevices[1200] = {'\0'};
//         uint8_t  i = 0;
//         for (i = 0; i < actionCount; i++) {
//             JSON* action     = JArr_GetObject(actions, i);
//             char* deviceAddr = JSON_GetText(action, "entityAddr");
//             if (deviceAddr != NULL) {
//                 if (!isContainString(commonDevices, deviceAddr)) {
//                     ble_delSceneLocalToDevice(deviceAddr, sceneId);
//                     strcat(commonDevices, deviceAddr);
//                 }
//             }
//         }
//     }
//     return true;
// }

bool deleteSceneActions(const char* sceneId, JSON* actions) {
    if (sceneId == NULL || actions == NULL) {
        return false;
    }
    char commonDevices[1200]    = {'\0'};
    int  i = 0, j = 0;
    logInfo("[deleteSceneActions] sceneId = %s", sceneId);
    int actionCount = JArr_Count(actions);
    JSON* mergedActions = JSON_CreateArray();
    for (i = 0; i < actionCount; i++) {
        JSON* action = JArr_GetObject(actions, i);
        if (JSON_HasKey(action, "pid")) {
            char* pid = JSON_GetText(action, "pid");
            char* deviceAddr = JSON_GetText(action, "entityAddr");
            int dpId = JSON_GetNumber(action, "dpId");

            if (pid != NULL) {
                if (isContainString(HG_BLE_SWITCH, pid)) {
                    int dpParam = (dpId - 1)*0x10 + 2;
                    dpParam = dpParam << 24;
                    JSON* mergedActionItem = JArr_FindByText(mergedActions, "deviceAddr", deviceAddr);
                    if (mergedActionItem == NULL) {
                        mergedActionItem = JArr_CreateObject(mergedActions);
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
                    ble_delSceneLocalToDevice(deviceAddr, sceneId);
                } else if (isContainString(RD_BLE_LIGHT_RGB, pid) && dpId == 20) {
                    ble_delSceneLocalToDevice(deviceAddr, sceneId);
                } else if (isContainString(HG_BLE_LIGHT_WHITE, pid) && dpId == 20) {
                    ble_delSceneLocalToDevice(deviceAddr, sceneId);
                }
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

bool addSceneCondition(const char* sceneId, JSON* condition) {
    if (sceneId && condition) {
        char *pid = JSON_GetText(condition, "pid");
        char *dpAddr = JSON_GetText(condition, "dpAddr");
        int dpValue   = JSON_GetNumber(condition, "dpValue");
        if (isMatchString(pid, HG_BLE_SENSOR_MOTION)) {
            ble_callSceneLocalToDevice(dpAddr, sceneId, "01", dpValue);
            // Add this device to response list to check TIMEOUT later
            addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
        } else {
            ble_callSceneLocalToDevice(dpAddr, sceneId, "01", dpValue);
            // Add this device to response list to check TIMEOUT later
            addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
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

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

#include <mosquitto.h>
#include <unistd.h>

#include "common.h"
#include "ble_common.h"
#include "define.h"
#include "ble_process.h"
#include "queue.h"
#include "core_process_t.h"
#include "uart.h"
#include "time_t.h"
#include "aws_mosquitto.h"
#include "cJSON.h"

const char* SERVICE_NAME = SERVICE_BLE;
uint8_t SERVICE_ID = SERVICE_ID_BLE;
bool g_printLog = true;
char g_ipAddress[100];
char g_hcAddr[10];
char g_masterIP[100];
char g_mosqIP[100];

extern int g_gatewayFds[GATEWAY_NUM];
char rcv_uart_buff[MAXLINE];
static bool g_mosqInitDone = false;
static bool g_mosqIsConnected = false;

struct mosquitto * mosq;
struct Queue *g_mqttMsgQueue;
struct Queue *g_lowPrioMqttMsgQueue;
static struct Queue* g_bleFrameQueue;

static JSON *g_checkRespList;

bool addSceneActions(const char* sceneId, JSON* actions);
bool deleteSceneActions(const char* sceneId, JSON* actions);
bool addSceneCondition(const char* sceneId, JSON* condition);
bool deleteSceneCondition(const char* sceneId, JSON* condition);
void Ble_ProcessPacket();

void On_mqttConnect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc) {
        logError("Error with result code: %d", rc);
        exit(-1);
    }

    // Read slave address if this HC is slave
    FILE* f = fopen("app.json", "r");
    char buff[1000];
    fread(buff, sizeof(char), 1000, f);
    fclose(f);
    JSON* setting = JSON_Parse(buff);
    char* hcAddr = JSON_GetText(setting, "hcAddr");
    StringCopy(g_hcAddr, hcAddr);
    logInfo("hcAddr: %s", g_hcAddr);

    char topic[200];
    sprintf(topic, "%s_%s/#", MOSQ_TOPIC_DEVICE_BLE, g_hcAddr);
    mosquitto_subscribe(mosq, NULL, "BLE_LOCAL/#", 0);
    mosquitto_subscribe(mosq, NULL, topic, 0);
    logInfo("[On_mqttConnect]: Subscribed topic: BLE_LOCAL/#, %s", topic);
    g_mosqIsConnected = true;
    JSON_Delete(setting);
}

void On_mqttMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    char* topic = msg->topic;
    List* tmp = String_Split(topic, "/");
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
    logInfo("[Mosq_Init] Connecting to %s", g_mosqIP);
    rc = mosquitto_connect(mosq, g_mosqIP, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if (rc != 0) {
        logInfo("Client could not connect to broker! Error Code: %d", rc);
        // mosquitto_destroy(mosq);
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
        char clientId[100];
        sprintf(clientId, "%lu", timeInMilliseconds());
        Mosq_Init(clientId);
    }
}


/*
 * Function to receive BLE frames from device => Push to bleFrameQueue
 * This function must be called frequently in a loop
 */
void BLE_ReceivePacket() {
    char givenStr[MAX_PACKAGE_SIZE];
    int len_uart = UART_Recv(g_gatewayFds[0], rcv_uart_buff, MAX_PACKAGE_SIZE);
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
        if (!StringContains(givenStr, "82048201")) {
            // Don't print log for getting device state actively
            printInfo("\n\r");
            logInfo("Received from UART3: %s", givenStr);
        }
    }

    len_uart = UART_Recv(g_gatewayFds[1], rcv_uart_buff, MAX_PACKAGE_SIZE);
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
        // if (!StringContains(givenStr, "82048201")) {
            // Don't print log for getting device state actively
            printInfo("\n\r");
            logInfo("Received from UART2: %s", givenStr);
        // }
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
        // logInfo("Received package from UART: %s", recvPackage);
        int frameCount = GW_SplitFrame(bleFrames, msgHex, strlen(recvPackage) / 2);
        // logInfo("Parsing package. Found %d frames:", frameCount);
        for (int i = 0; i < frameCount; i++) {
            // Print the frame information
            char str[3000];
            BLE_PrintFrame(str, &bleFrames[i]);
            struct state_element *tmp = malloc(sizeof(struct state_element));
            InfoDataUpdateDevice *InfoDataUpdateDevice_t = malloc(sizeof(InfoDataUpdateDevice));
            int frameType = check_form_recived_from_RX(tmp, &bleFrames[i]);
            if (frameType == GW_RESP_ONOFF_STATE && bleFrames[i].frameSize >= 5 && bleFrames[i].param[0] == 0x82 && bleFrames[i].param[1] == 0x01) {
                BLE_SetDeviceResp(frameType, bleFrames[i].sendAddr, 0, false);
            } else {
                logInfo("Frame #%d: type: %d, %s", i, frameType, str);
                BLE_SetDeviceResp(frameType, bleFrames[i].sendAddr, 0, true);
            }

            switch(frameType) {
                case GW_RESP_ONLINE_STATE: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
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
                    sendPacketTo(SERVICE_CORE, GW_RESP_ONLINE_STATE, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_LOCK_KIDS: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "dpAddr", tmp->address_element);
                    JSON_SetNumber(packet, "lockValue", bleFrames[i].param[3]);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_DIM_LED_SWITCH_HOMEGY:
                case GW_RESP_ONOFF_STATE: {
                    if (bleFrames[i].frameSize >= 4 && bleFrames[i].param[0] == 0x82 && bleFrames[i].param[1] == 0x01) {
                        uint8_t dpCount = bleFrames[i].param[2];
                        uint16_t deviceAddr = bleFrames[i].sendAddr;
                        char str[10];
                        for (int d = 0; d < dpCount; d++) {
                            JSON* packet = JSON_CreateObject();
                            JSON_SetText(packet, "hcAddr", g_hcAddr);
                            JSON_SetNumber(packet, "opcode", 0x8201);
                            uint16_t addr = (uint8_t)(deviceAddr >> 8);
                            addr += d;
                            addr = (addr << 8) | (uint8_t)deviceAddr;
                            sprintf(str, "%04X", addr);
                            JSON_SetText(packet, "deviceAddr", str);
                            JSON_SetText(packet, "dpAddr", str);
                            JSON_SetNumber(packet, "dpValue", bleFrames[i].param[3 + d]);
                            // g_printLog = false;
                            sendPacketTo(SERVICE_CORE, frameType, packet);
                            // g_printLog = true;
                            JSON_Delete(packet);
                        }
                    } else if (bleFrames[i].paramSize > 2) {
                        // Response onoff for HG switch, CCT light
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "hcAddr", g_hcAddr);
                        uint16_t opcode = bleFrames[i].param[0];
                        opcode = (opcode << 8) | bleFrames[i].param[1];
                        uint8_t dpValue = bleFrames[i].param[2];
                        JSON_SetNumber(packet, "opcode", opcode);
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetText(packet, "dpAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpValue", dpValue);
                        sendPacketTo(SERVICE_CORE, frameType, packet);
                        JSON_Delete(packet);
                    } else {
                        // Response onoff for RBG light
                        JSON* packet = JSON_CreateObject();
                        JSON_SetText(packet, "hcAddr", g_hcAddr);
                        JSON_SetNumber(packet, "opcode", 0);
                        JSON_SetText(packet, "deviceAddr", tmp->address_element);
                        JSON_SetText(packet, "dpAddr", tmp->address_element);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[0]? 1 : 0);
                        sendPacketTo(SERVICE_CORE, frameType, packet);
                        JSON_Delete(packet);
                    }
                    break;
                }
                case GW_RESPONSE_LIGHT_RD_CONTROL: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    if (bleFrames[i].opcode == 0x824e) {
                        // Get lightness
                        uint16_t value = ((uint16_t)bleFrames[i].param[1] << 8) | bleFrames[i].param[0];
                        if (bleFrames[i].paramSize >= 4) {
                            value = ((uint16_t)bleFrames[i].param[3] << 8) | bleFrames[i].param[2];
                        }
                        value = ((uint32_t)value * 1000) / 65535;
                        JSON_SetNumber(packet, "lightness", value);
                    } else if (bleFrames[i].opcode == 0x8266) {
                        // Get color temperature
                        uint16_t value = ((uint16_t)bleFrames[i].param[1] << 8) | bleFrames[i].param[0];
                        if (bleFrames[i].paramSize >= 6) {
                            value = ((uint16_t)bleFrames[i].param[5] << 8) | bleFrames[i].param[4];
                        }

                        value = ((value - 800) * 1000) / (20000 - 800);
                        JSON_SetNumber(packet, "color", value);
                    } else if (bleFrames[i].opcode == 0x8260) {
                        // Get lightless and color temperature
                        uint16_t lightness = ((uint16_t)bleFrames[i].param[4] << 8) | bleFrames[i].param[3];
                        uint16_t colorTemperature = ((uint16_t)bleFrames[i].param[6] << 8) | bleFrames[i].param[5];
                        lightness = ((uint32_t)lightness * 1000) / 65535;
                        colorTemperature = ((colorTemperature - 800) * 1000) / (20000 - 800);
                        JSON_SetNumber(packet, "lightness", lightness);
                        JSON_SetNumber(packet, "color", colorTemperature);
                    }
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_RGB_COLOR: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    char color[14];
                    sprintf(color, "%02x%02x%02x%02x%02x%02x", bleFrames[i].param[3], bleFrames[i].param[2], bleFrames[i].param[5], bleFrames[i].param[4], bleFrames[i].param[7], bleFrames[i].param[6]);
                    JSON_SetText(packet, "color", color);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    break;
                }
                case GW_RESPONSE_SMOKE_SENSOR: {
                    logInfo("GW_RESPONSE_SMOKE_SENSOR");
                    uint8_t hasSmoke = bleFrames[i].param[1];
                    uint8_t battery = bleFrames[i].param[2]? 100 : 0;

                    // Send smoke detection to core service
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpId", TYPE_DPID_SMOKE_SENSOR_DETECT);
                    JSON_SetNumber(packet, "dpValue", hasSmoke);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);

                    // Send battery to core service
                    packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
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
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "dpId", TYPE_DPID_ENVIRONMENT_SENSOR_TEMPER);
                    JSON_SetNumber(packet, "dpValue", (double)temperature);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);

                    // Send battery to core service
                    packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
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
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    if (frameType == GW_RESPONSE_SENSOR_DOOR_ALARM) {
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_DOOR_SENSOR_ALRM);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]);
                    } else {
                        JSON_SetNumber(packet, "dpId", TYPE_DPID_DOOR_SENSOR_DETECT);
                        JSON_SetNumber(packet, "dpValue", bleFrames[i].param[1]);
                    }
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_SENSOR_BATTERY: {
                    logInfo("GW_RESPONSE_SENSOR_BATTERY");
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
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
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
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
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
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
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                // case GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE:
                case GW_RESPONSE_ADD_SCENE: {
                    logInfo("GW_RESPONSE_ADD_SCENE");
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    char sceneId[10];
                    int status = 0;
                    if (bleFrames[i].opcode == 0x8245) {
                        // Response for scene action
                        sprintf(sceneId, "%02X%02X", bleFrames[i].param[1], bleFrames[i].param[2]);
                        status = bleFrames[i].param[0];
                    } else {
                        // Response for scene condition
                        status = bleFrames[i].param[3];
                        sprintf(sceneId, "%02X%02X", bleFrames[i].param[4], bleFrames[i].param[5]);
                    }
                    JSON_SetText(packet, "sceneId", sceneId);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "status", status);
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
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", deviceAddr);
                    JSON_SetText(packet, "groupAddr", groupAddr);
                    JSON_SetNumber(packet, "status", bleFrames[i].param[0]);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_SET_TTL: {
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    JSON_SetNumber(packet, "status", 0);
                    sendPacketTo(SERVICE_CORE, frameType, packet);
                    JSON_Delete(packet);
                    break;
                }
                case GW_RESPONSE_IR: {
                    logInfo("GW_RESPONSE_IR");
                    JSON* packet = JSON_CreateObject();
                    JSON_SetText(packet, "hcAddr", g_hcAddr);
                    JSON_SetText(packet, "deviceAddr", tmp->address_element);
                    bool sentToCoreService = false;
                    if (bleFrames[i].param[1] == 0x0A) {
                        uint16_t brandId = ((uint16_t)bleFrames[i].param[4] << 8) + bleFrames[i].param[3];
                        uint8_t  remoteId = bleFrames[i].param[5];
                        uint8_t  temp = bleFrames[i].param[6];
                        uint8_t  mode = bleFrames[i].param[7];
                        uint8_t  fan = bleFrames[i].param[8] >> 4;
                        uint8_t  swing = bleFrames[i].param[8] & 0x0F;
                        logInfo("brandId=%d, remoteId=%d, temp=%d, mode=%d, fan=%d, swing=%d", brandId, remoteId, temp, mode, fan, swing);
                        JSON_SetNumber(packet, "respType", 0);
                        JSON_SetNumber(packet, "brandId", brandId);
                        JSON_SetNumber(packet, "remoteId", remoteId);
                        JSON_SetNumber(packet, "temp", temp);
                        JSON_SetNumber(packet, "mode", mode);
                        JSON_SetNumber(packet, "fan", fan);
                        JSON_SetNumber(packet, "swing", swing);
                        sentToCoreService = true;
                    } else if (bleFrames[i].param[1] == 0x09) {
                        uint16_t voiceId = ((uint16_t)bleFrames[i].param[3] << 8) | bleFrames[i].param[2];
                        JSON_SetNumber(packet, "respType", 1);
                        JSON_SetNumber(packet, "voiceId", voiceId);
                        sentToCoreService = true;
                    } else if (bleFrames[i].param[1] == 0x0B) {
                        JSON_SetNumber(packet, "respType", 2);
                        JSON_SetText(packet, "respCmd", recvPackage);
                        sentToCoreService = true;
                    }
                    if (sentToCoreService) {
                        sendPacketTo(SERVICE_CORE, frameType, packet);
                    }
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
                    JSON_SetText(p, "hcAddr", g_hcAddr);
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

void GetIpAddressLoop() {
    static long long tick = 0;
    if (timeInMilliseconds() - tick > 30000) {
        tick = timeInMilliseconds();
        char address[50];
        FILE* fp = popen("python3 getIp.py", "r");
        while (fgets(address, StringLength(address), fp) != NULL);
        if (address) {
            if (StringCompare(address, g_ipAddress) == false) {
                StringCopy(g_ipAddress, address);
                logInfo("New IP Address: %s", g_ipAddress);
                if (StringCompare(g_ipAddress, "192.168.12.1")) {
                    StringCopy(g_mosqIP, g_ipAddress);
                    mosquitto_destroy(mosq);
                    char clientId[100];
                    sprintf(clientId, "%lu", timeInMilliseconds());
                    Mosq_Init(clientId);
                }
                // Update new IP address to AWS
                char msg[200];
                sprintf(msg, "{\"state\":{\"reported\":{\"gateWay\":{\"%s\":{\"ipLocal\":\"%s\", \"hcVersion\":\"%s\"}}, \"sender\":11}}}", g_hcAddr, g_ipAddress, HC_VERSION);
                sendToService(SERVICE_AWS, 255, msg);
            }
        }
        fclose(fp);

        // // Check connected wifi name
        // char wifiName[100];
        // fp = popen("iw wlan0 info | grep -Po '(?<=ssid ).*'", "r");
        // while (fgets(wifiName, sizeof(wifiName), fp) != NULL);
        // if (wifiName && StringLength(wifiName) > 1) {
        //     wifiName[StringLength(wifiName) - 1] = 0;
        //     if (StringCompare(wifiName, g_wifiName) == false) {
        //         StringCopy(g_wifiName, wifiName);
        //         logInfo("New connected wifi name: %s", g_wifiName);
        //         // Update new wifi name to AWS
        //         char msg[200];
        //         sprintf(msg, "{\"state\":{\"reported\":{\"gateWay\":{\"0A00\":{\"nameWifi\":\"%s\"}}, \"sender\":11}}}", g_wifiName);
        //         char* topic = Aws_GetTopic(PAGE_MAIN, 1, TOPIC_UPD_PUB);
        //         mqttCloudPublish(topic, msg);
        //         free(topic);
        //     }
        // }
        // fclose(fp);
    }
}

int main( int argc,char ** argv )
{
    int mqttSizeQueue = 0, lowPrioMqttSizeQueue = 0;
    int err,xRun = 1;
    g_checkRespList = JSON_CreateArray();
    g_mqttMsgQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    g_lowPrioMqttMsgQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    g_bleFrameQueue = newQueue(MAX_SIZE_NUMBER_QUEUE);
    //Init for uart()
    g_gatewayFds[0] = UART_Open(g_gatewayFds[0], UART_GATEWAY1);
    g_gatewayFds[1] = UART_Open(g_gatewayFds[1], UART_GATEWAY2);
    if (g_gatewayFds[0] == -1) {
        logError("Cannot open %s", UART_GATEWAY1);
    }
    if (g_gatewayFds[1] == -1) {
        logError("Cannot open %s", UART_GATEWAY2);
    }

    do {
        err = UART_Init(g_gatewayFds[0], 115200, 0, 8, 1, 'N');
        usleep(50000);
    }
    while (-1 == err || -1 == g_gatewayFds[0]);
    logInfo("Init %s done", UART_GATEWAY1);
    usleep(50000);

    do {
        err = UART_Init(g_gatewayFds[1], 115200, 0, 8, 1, 'N');
        usleep(50000);
    }
    while (-1 == err || -1 == g_gatewayFds[1]);
    logInfo("Init %s done", UART_GATEWAY2);
    usleep(50000);

    // Read ip of master HC
    FILE* f = fopen("masterIP", "r");
    if (f) {
        int len = fread(g_masterIP, sizeof(char), 100, f);
        g_masterIP[len] = 0;
        fclose(f);
    } else {
        StringCopy(g_masterIP, MQTT_MOSQUITTO_HOST);
    }
    logInfo("Master IP: %s", g_masterIP);
    StringCopy(g_mosqIP, g_masterIP);

    Mosq_Init(SERVICE_BLE);
    sleep(3);
    // GW_DelAllSceneAction("4001");
    // GW_SetLightHSL("B801", "7fffaea4ffff");
    while (xRun!=0) {
        BLE_SendUartFrameLoop();
        BLE_ReceivePacket();  // Receive BLE frames from device => Push to bleFrameQueue
        Ble_ProcessPacket();  // Get BLE frames from bleFrameQueue => publish message to CORE service
        Mosq_ProcessLoop();   // Receive mqtt message from CORE service => Push to mqttMsgQueue
        GetIpAddressLoop();

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

        JSON* recvPacket = JSON_Parse(recvMsg);
        int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
        JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));

        // if (reqType != TYPE_GET_ONOFF_STATE) {
            printInfo("\n\r");
            logInfo("Received message: %s", recvMsg);
        // }
        free(recvMsg);

        if (payload) {
            char* dpAddr, *groupAddr;
            switch (reqType) {
                case TYPE_ADD_GW: {
                    provison_inf PRV;
                    char* message = "{\"step\":3, \"message\":\"Đang cấu hình bộ trung tâm\"}";
                    mosquitto_publish(mosq, NULL, MQTT_LOCAL_RESP_TOPIC, strlen(message), message, 0, false);
                    PlayAudio("configuring_gateway");
                    sendToService(SERVICE_CFG, 0, "LED_FAST_FLASH");
                    ble_getInfoProvison(&PRV, payload);
                    GW_ConfigGateway(0, &PRV);
                    sleep(1);
                    GW_ConfigGateway(1, &PRV);
                    sendToService(SERVICE_CFG, 0, "LED_ON");
                    char* message2 = "{\"step\":4, \"message\":\"cấu hình bộ trung tâm thành công, đang khởi động lại thiết bị\"}";
                    mosquitto_publish(mosq, NULL, MQTT_LOCAL_RESP_TOPIC, strlen(message2), message, 0, false);
                    PlayAudio("gateway_end_restarting");
                    PlayAudio("device_restart_warning");
                    sleep(1);
                    system("reboot");
                    break;
                }
                case TYPE_CTR_DEVICE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    char* pid = JSON_GetText(payload, "pid");
                    cJSON* dictDPs = cJSON_GetObjectItem(payload, "dictDPs");
                    int lightness = -1, colorTemperature = -1;
                    int irCommandType = 0, irBrandId = 0, irRemoteId = 0, irTemp = 0, irMode = 0, irFan = 0, irSwing = 1;
                    bool sendCmdDirectlyFromApp = false;
                    JSON_ForEach(o, dictDPs) {
                        int dpId = JSON_GetNumber(o, "id");
                        if (dpId == 106) {   // dpId == 106 for sending BLE commands from app
                            char* command = JSON_GetText(o, "valueString");
                            GW_ControlIRCmd(gwIndex, command);
                            sendCmdDirectlyFromApp = true;
                        } else {
                            dpAddr = JSON_GetText(o, "addr");
                            int dpValue = JSON_GetNumber(o, "value");
                            if (StringContains(HG_BLE_SWITCH, pid)) {
                                GW_HgSwitchOnOff(gwIndex, dpAddr, dpValue);
                            } else if (StringContains(HG_BLE_CURTAIN, pid) || dpId == 20) {
                                GW_CtrlLightOnOff(gwIndex, dpAddr, dpValue);
                            } else if (dpId == 24) {
                                char* valueString = JSON_GetText(o, "valueString");
                                GW_SetLightHSL(gwIndex, dpAddr, valueString);
                            } else if (dpId == 21 && dpValue >= 0) {
                                GW_SetRGBLightBlinkMode(gwIndex, dpAddr, dpValue);
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
                            } else if (dpId == 101) {   // dpId == 101 for CTL IR_TV
                                char* command = JSON_GetText(o, "valueString");
                                irTemp = atoi(command);
                                logInfo("[TYPE_CTR_DEVICE] irTemp = %d", irTemp);
                            }
                        }
                    }
                    if (lightness >= 0 && colorTemperature >= 0) {
                        GW_SetLightnessTemperature(gwIndex, dpAddr, lightness, colorTemperature);
                    }
                    if (irCommandType > 0 && irBrandId > 0 && irRemoteId > 0 && sendCmdDirectlyFromApp == false) {
                        GW_ControlIR(gwIndex, dpAddr, irCommandType, irBrandId, irRemoteId, irTemp, irMode, irFan, irSwing);
                    }
                    break;
                }
                case TYPE_CTR_GROUP_NORMAL: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    char* pid = JSON_GetText(payload, "pid");
                    cJSON* dictDPs = cJSON_GetObjectItem(payload, "dictDPs");
                    int lightness = -1, colorTemperature = -1;
                    int irCommandType = 0, irBrandId = 0, irRemoteId = 0, irTemp = 0, irMode = 0, irFan = 0, irSwing = 1;
                    JSON_ForEach(o, dictDPs) {
                        int dpId = JSON_GetNumber(o, "id");
                        dpAddr = JSON_GetText(o, "addr");
                        int dpValue = JSON_GetNumber(o, "value");
                        if (StringContains(HG_BLE_SWITCH, pid)) {
                            GW_HgSwitchOnOff_NoResp(gwIndex, dpAddr, dpValue);
                        } else if (StringContains(HG_BLE_CURTAIN, pid) || dpId == 20) {
                            GW_CtrlGroupLightOnOff(gwIndex, dpAddr, dpValue);
                        } else if (dpId == 24) {
                            char* valueString = JSON_GetText(o, "valueString");
                            GW_SetLightHSL(gwIndex, dpAddr, valueString);
                        } else if (StringContains(RD_BLE_LIGHT_RGB, pid) && dpId == 21) {
                            GW_SetRGBLightBlinkMode(gwIndex, dpAddr, dpValue);
                        } else if (dpId == 22) {
                            lightness = dpValue;
                        } else if (dpId == 23) {
                            colorTemperature = dpValue;
                        }
                    }
                    if (lightness >= 0 && colorTemperature >= 0) {
                        GW_SetLightnessTemperature(gwIndex, dpAddr, lightness, colorTemperature);
                    }
                    break;
                }
                case TYPE_CTR_SCENE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    int state = JSON_GetNumber(payload, "state");
                    if (state >= 2) {
                        logInfo("Executing LC scene %s", sceneId);
                        GW_CallScene(sceneId);
                    } else {
                        // Enable/Disable scene
                        char* dpAddr = JSON_GetText(payload, "dpAddr");
                        int enableState = JSON_GetNumber(payload, "state");
                        GW_EnableDisableScene("FFFF", sceneId, enableState);
                    }
                    break;
                }
                case TYPE_ADD_GROUP_LIGHT:
                case TYPE_DEL_GROUP_LIGHT: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        char* hcAddr = JSON_GetText(device, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            int gwIndex = JSON_GetNumber(device, "gwIndex");
                            char* deviceAddr = JSON_GetText(device, "deviceAddr");
                            if (reqType == TYPE_ADD_GROUP_LIGHT) {
                                GW_AddGroupLight(gwIndex, groupAddr, deviceAddr, deviceAddr);
                            } else {
                                GW_DeleteGroup(gwIndex, groupAddr, deviceAddr, deviceAddr);
                            }
                            // Add this device to response list to check TIMEOUT later
                            addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                        }
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_LIGHT: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        char* hcAddr = JSON_GetText(dpNeedRemove, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            int gwIndex = JSON_GetNumber(dpNeedRemove, "gwIndex");
                            char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                            GW_DeleteGroup(gwIndex, groupAddr, deviceAddr, deviceAddr);
                            // Add this device to response list to check TIMEOUT later
                            addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                        }
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        char* hcAddr = JSON_GetText(dpNeedAdd, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            int gwIndex = JSON_GetNumber(dpNeedAdd, "gwIndex");
                            char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                            GW_AddGroupLight(gwIndex, groupAddr, deviceAddr, deviceAddr);
                            // Add this device to response list to check TIMEOUT later
                            addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                        }
                    }
                    break;
                }
                case TYPE_ADD_GROUP_LINK:
                case TYPE_DEL_GROUP_LINK: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* devicesArray = JSON_GetObject(payload, "devices");
                    JSON_ForEach(device, devicesArray) {
                        char* hcAddr = JSON_GetText(device, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            int gwIndex = JSON_GetNumber(device, "gwIndex");
                            char* deviceAddr = JSON_GetText(device, "deviceAddr");
                            char* dpAddr = JSON_GetText(device, "dpAddr");
                            if (reqType == TYPE_ADD_GROUP_LINK) {
                                GW_AddGroupSwitch(gwIndex, groupAddr, deviceAddr, dpAddr);
                            } else {
                                GW_DeleteGroup(gwIndex, groupAddr, deviceAddr, dpAddr);
                            }
                            // Add this device to response list to check TIMEOUT later
                            addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                        }
                    }
                    break;
                }
                case TYPE_UPDATE_GROUP_LINK: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* groupAddr = JSON_GetText(payload, "groupAddr");
                    JSON* dpsNeedRemove = JSON_GetObject(payload, "dpsNeedRemove");
                    JSON* dpsNeedAdd = JSON_GetObject(payload, "dpsNeedAdd");
                    JSON_ForEach(dpNeedRemove, dpsNeedRemove) {
                        char* hcAddr = JSON_GetText(dpNeedRemove, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            int gwIndex = JSON_GetNumber(dpNeedRemove, "gwIndex");
                            char* deviceAddr = JSON_GetText(dpNeedRemove, "deviceAddr");
                            char* dpAddr = JSON_GetText(dpNeedRemove, "dpAddr");
                            GW_DeleteGroup(gwIndex, groupAddr, deviceAddr, dpAddr);
                            // Add this device to response list to check TIMEOUT later
                            addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                        }
                    }
                    JSON_ForEach(dpNeedAdd, dpsNeedAdd) {
                        char* hcAddr = JSON_GetText(dpNeedAdd, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            int gwIndex = JSON_GetNumber(dpNeedAdd, "gwIndex");
                            char* deviceAddr = JSON_GetText(dpNeedAdd, "deviceAddr");
                            char* dpAddr = JSON_GetText(dpNeedAdd, "dpAddr");
                            GW_AddGroupSwitch(gwIndex, groupAddr, deviceAddr, dpAddr);
                            // Add this device to response list to check TIMEOUT later
                            addRespTypeToSendingFrame(GW_RESPONSE_GROUP, groupAddr);
                        }
                    }
                    break;
                }
                case TYPE_ADD_DEVICE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    char* devicePid = JSON_GetText(payload, "devicePid");
                    char* deviceKey = JSON_GetText(payload, "deviceKey");
                    int gatewayId = JSON_GetNumber(payload, "gatewayId");
                    logInfo("[TYPE_ADD_DEVICE]: gatewayId=%d,deviceAddr=%s,devicePid=%s,deviceKey=%s", gatewayId, deviceAddr, devicePid, deviceKey);
                    set_inf_DV_for_GW(gatewayId, deviceAddr, devicePid, deviceKey);
                    if (JSON_HasKey(payload, "command")) {
                        GW_ControlIRCmd(gatewayId, JSON_GetText(payload, "command"));
                    }
                    break;
                }
                case TYPE_DEL_DEVICE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* pid = JSON_GetText(payload, "devicePid");
                    char* deviceId = JSON_GetText(payload, "deviceId");
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    if (deviceAddr && !StringCompare(pid, HG_BLE_IR_TV) &&
                                      !StringCompare(pid, HG_BLE_IR_FAN) &&
                                      !StringCompare(pid, HG_BLE_IR_AC) &&
                                      !StringCompare(pid, HG_BLE_IR_REMOTE)) {
                        GW_DeleteDevice(gwIndex, deviceAddr);
                        logInfo("Deleted deviceId: %s, address: %s", deviceId, deviceAddr);
                    }
                    break;
                }
                case TYPE_ADD_SCENE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* sceneId = JSON_GetText(payload, "id");
                    JSON* actions = JSON_GetObject(payload, "actions");
                    bool ret = addSceneActions(sceneId, actions);
                    if (ret && JSON_HasKey(payload, "conditions")) {
                        JSON* conditions = JSON_GetObject(payload, "conditions");
                        JSON* condition = JArr_GetObject(conditions, 0);
                        ret = addSceneCondition(sceneId, condition);
                    }
                    break;
                }
                case TYPE_DEL_SCENE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    logInfo("[TYPE_DEL_SCENE] sceneId = %s", sceneId);
                    JSON* actions = JSON_GetObject(payload, "actions");
                    bool ret = deleteSceneActions(sceneId, actions);
                    if (ret && JSON_HasKey(payload, "conditions")) {
                        JSON* conditions = JSON_GetObject(payload, "conditions");
                        JSON* condition = JArr_GetObject(conditions, 0);
                        ret = deleteSceneCondition(sceneId, condition);
                    }
                    break;
                }
                case TYPE_UPDATE_SCENE: {
                    sendToServiceNoDebug(SERVICE_CFG, 0, "LED_FLASH_1_TIME");
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    JSON* actionsNeedRemove = JSON_GetObject(payload, "actionsNeedRemove");
                    JSON* actionsNeedAdd = JSON_GetObject(payload, "actionsNeedAdd");
                    if (JArr_Count(actionsNeedRemove) > 0) {
                        deleteSceneActions(sceneId, actionsNeedRemove);
                    }
                    if (JArr_Count(actionsNeedAdd) > 0) {
                        addSceneActions(sceneId, actionsNeedAdd);
                    }
                    if (JSON_HasKey(payload, "conditionNeedRemove")) {
                        JSON* conditionNeedRemove = JSON_GetObject(payload, "conditionNeedRemove");
                        deleteSceneCondition(sceneId, conditionNeedRemove);
                    }
                    if (JSON_HasKey(payload, "conditionNeedAdd")) {
                        JSON* conditionNeedAdd = JSON_GetObject(payload, "conditionNeedAdd");
                        addSceneCondition(sceneId, conditionNeedAdd);
                    }
                    break;
                }
                case TYPE_DIM_LED_SWITCH: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    int value = JSON_GetNumber(payload, "led");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    GW_SwitchDimLed(gwIndex, deviceAddr, value);
                    break;
                }
                case TYPE_LOCK_AGENCY: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    int value = JSON_GetNumber(payload, "value");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    GW_LockDevice(gwIndex, deviceAddr, value);
                    break;
                }
                case TYPE_LOCK_KIDS: {
                    char* deviceAddr = JSON_GetText(payload, "deviceAddr");
                    JSON* dps = JSON_GetObject(payload, "lock");
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    JSON_ForEach(dp, dps) {
                        if (dp->string) {
                            GW_LockTouch(gwIndex, deviceAddr, atoi(dp->string), dp->valueint);
                        }
                    }
                    break;
                }
                case TYPE_GET_ONOFF_STATE: {
                    JSON_ForEach(d, payload) {
                        int gwIndex = JSON_GetNumber(d, "gwIndex");
                        char* hcAddr = JSON_GetText(d, "hcAddr");
                        if (StringCompare(hcAddr, g_hcAddr)) {
                            char* addr = JSON_GetText(d, "addr");
                            GW_GetDeviceOnOffState(gwIndex, addr);
                            addRespTypeToSendingFrame(GW_RESP_ONOFF_STATE, addr);
                        }
                    }
                    break;
                }
                case TYPE_SET_DEVICE_TTL: {
                    int gwIndex = JSON_GetNumber(payload, "gwIndex");
                    GW_SetTTL(gwIndex, JSON_GetText(payload, "deviceAddr"), JSON_GetNumber(payload, "ttl"));
                    break;
                }
                case TYPE_GET_GROUPS_OF_DEVICE:
                case TYPE_GET_SCENES_OF_DEVICE: {

                }
            }
        } else {
            logError("Payload is NULL");
        }
        cJSON_Delete(recvPacket);
        cJSON_Delete(payload);
        usleep(100);
    }
    return 0;
}

bool addSceneActions(const char* sceneId, JSON* actions) {
    ASSERT(sceneId);
    ASSERT(actions);

    char commonDevices[1200]    = {'\0'};
    int  j = 0;
    logInfo("[addSceneActions] sceneId = %s", sceneId);
    JSON_ForEach(action, actions) {
        char* hcAddr = JSON_GetText(action, "hcAddr");
        if (StringCompare(hcAddr, g_hcAddr)) {
            char* actionExecutor = JSON_GetText(action, "actionExecutor");
            JSON* executorProperty = JSON_GetObject(action, "executorProperty");
            char* deviceAddr = JSON_GetText(action, "entityAddr");
            int gwIndex = JSON_GetNumber(action, "gwIndex");
            if (StringCompare(actionExecutor, "irHGBLE")) {
                uint8_t commandType = JSON_GetNumber(executorProperty, DPID_IR_COMMAND_TYPE);
                uint8_t brandId = JSON_GetNumber(executorProperty, DPID_IR_BRAND_ID);
                uint8_t remoteId = JSON_GetNumber(executorProperty, DPID_IR_REMOTE_ID);
                uint8_t temp = 0;
                if (JSON_HasKey(executorProperty, DPID_IR_TEMP)) {
                    temp = JSON_GetNumber(executorProperty, DPID_IR_TEMP);
                } else if (JSON_HasKey(executorProperty, DPID_IR_ONOFF)) {
                    temp = atoi(JSON_GetText(executorProperty, DPID_IR_ONOFF));
                }
                uint8_t mode = JSON_HasKey(executorProperty, DPID_IR_MODE)? JSON_GetNumber(executorProperty, DPID_IR_MODE) : 0;
                uint8_t fan = JSON_HasKey(executorProperty, DPID_IR_FAN)? JSON_GetNumber(executorProperty, DPID_IR_FAN) : 1;
                uint8_t swing = JSON_HasKey(executorProperty, DPID_IR_SWING)? JSON_GetNumber(executorProperty, DPID_IR_SWING) : 1;
                GW_AddSceneActionIR(gwIndex, deviceAddr, sceneId, commandType, brandId, remoteId, temp, mode, fan, swing);
            } else if (JSON_HasKey(action, "pid")) {
                char* pid = JSON_GetText(action, "pid");
                int dpId = JSON_GetNumber(action, "dpId");
                int dpValue = JSON_GetNumber(action, "dpValue");
                if (pid != NULL) {
                    if (StringContains(HG_BLE_SWITCH, pid) || StringContains(HG_BLE_CURTAIN, pid)) {
                        char* dpAddr = JSON_GetText(action, "dpAddr");
                        int dpValue = JSON_GetNumber(action, "dpValue");
                        GW_SetSceneActionForSwitch(gwIndex, dpAddr, sceneId, dpValue);
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                    } else if (StringContains(RD_BLE_LIGHT_RGB, pid) && (dpId == 20 || dpId == 21 || dpId == 24)) {
                        if (dpId == 21) {
                            char* dpValueString;
                            dpValueString = JSON_GetText(executorProperty, "21");
                            List* tmp = String_Split(dpValueString, "_");
                            if (tmp->count == 2) {
                                uint8_t blinkMode = atoi(tmp->items[1]);
                                GW_SetSceneActionForLightRGB(gwIndex, deviceAddr, sceneId, blinkMode);
                            }
                        } else {
                            GW_SetSceneActionForLightRGB(gwIndex, deviceAddr, sceneId, 0);
                        }
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                    } else if (StringContains(HG_BLE_LIGHT_WHITE, pid)) {
                        GW_SetSceneActionForLightCCT(gwIndex, deviceAddr, sceneId);
                        // Add this device to response list to check TIMEOUT later
                        addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                    }
                }
            }
        }
    }

    return true;
}

bool deleteSceneActions(const char* sceneId, JSON* actions) {
    ASSERT(sceneId);
    ASSERT(actions);
    char commonDevices[1200]    = {'\0'};
    int  i = 0, j = 0;
    logInfo("[deleteSceneActions] sceneId = %s", sceneId);
    // printf("Actions: %s\n", cJSON_PrintUnformatted(actions));
    int actionCount = JArr_Count(actions);
    JSON* mergedActions = JSON_CreateArray();
    for (i = 0; i < actionCount; i++) {
        JSON* action = JArr_GetObject(actions, i);
        char* hcAddr = JSON_GetText(action, "hcAddr");
        int gwIndex = JSON_GetNumber(action, "gwIndex");
        if (StringCompare(hcAddr, g_hcAddr)) {
            char* actionExecutor = JSON_HasKey(action, "actionExecutor")? JSON_GetText(action, "actionExecutor") : "";
            JSON* executorProperty = JSON_GetObject(action, "executorProperty");
            // printf("Action: %s\n", cJSON_PrintUnformatted(action));
            if (StringCompare(actionExecutor, "irHGBLE")) {
                char* deviceAddr = JSON_GetText(action, "entityAddr");
                uint8_t commandType = JSON_GetNumber(executorProperty, DPID_IR_COMMAND_TYPE);
                uint8_t brandId = JSON_GetNumber(executorProperty, DPID_IR_BRAND_ID);
                uint8_t remoteId = JSON_GetNumber(executorProperty, DPID_IR_REMOTE_ID);
                GW_DeleteSceneActionIR(gwIndex, deviceAddr, sceneId, commandType, brandId, remoteId);
            } else if (JSON_HasKey(action, "pid")) {
                char* pid = JSON_GetText(action, "pid");
                int dpId = JSON_GetNumber(action, "dpId");
                char* deviceAddr = JSON_GetText(action, "entityAddr");
                if (pid != NULL) {
                    if (StringContains(HG_BLE_SWITCH, pid) || StringContains(HG_BLE_CURTAIN, pid)) {
                        char* dpAddr = JSON_GetText(action, "dpAddr");
                        GW_DelSceneAction(gwIndex, dpAddr, sceneId);
                        addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                    } else if (StringContains(RD_BLE_LIGHT_RGB, pid)) {
                        GW_DelSceneAction(gwIndex, deviceAddr, sceneId);
                        addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                    } else if (StringContains(HG_BLE_LIGHT_WHITE, pid)) {
                        GW_DelSceneAction(gwIndex, deviceAddr, sceneId);
                        addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
                    }
                }
            }
        }
    }

    return true;
}

bool addSceneCondition(const char* sceneId, JSON* condition) {
    ASSERT(sceneId);
    ASSERT(condition);

    logInfo("[addSceneCondition] sceneId = %s", sceneId);
    char* hcAddr = JSON_GetText(condition, "hcAddr");
    int gwIndex = JSON_GetNumber(condition, "gwIndex");
    if (StringCompare(hcAddr, g_hcAddr)) {
        char *pid = JSON_GetText(condition, "pid");
        if (StringCompare(pid, HG_BLE_IR)) {
            char* deviceAddr = JSON_GetText(condition, "entityAddr");
            uint16_t voiceCode = JSON_GetNumber(condition, "dpValue");
            GW_AddSceneConditionIR(gwIndex, deviceAddr, sceneId, voiceCode);
        } else {
            char* dpAddr = JSON_GetText(condition, "dpAddr");
            int dpValue   = JSON_GetNumber(condition, "dpValue");
            GW_SetSceneCondition(gwIndex, dpAddr, sceneId, dpValue);
            // Add this device to response list to check TIMEOUT later
            addRespTypeToSendingFrame(GW_RESPONSE_ADD_SCENE, sceneId);
        }
    }
    return true;
}

bool deleteSceneCondition(const char* sceneId, JSON* condition) {
    ASSERT(sceneId);
    ASSERT(condition);
    char* hcAddr = JSON_GetText(condition, "hcAddr");
    int gwIndex = JSON_GetNumber(condition, "gwIndex");
    if (StringCompare(hcAddr, g_hcAddr)) {
        logInfo("[deleteSceneCondition] sceneId = %s", sceneId);
        char *pid = JSON_GetText(condition, "pid");
        if (StringCompare(pid, HG_BLE_IR)) {
            char* deviceAddr = JSON_GetText(condition, "entityAddr");
            GW_DeleteSceneConditionIR(gwIndex, deviceAddr, sceneId);
        } else {
            char* dpAddr = JSON_GetText(condition, "dpAddr");
            GW_DelSceneCondition(gwIndex, dpAddr, sceneId);
        }
    }
    return true;
}

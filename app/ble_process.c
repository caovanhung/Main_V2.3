#include <math.h>
#include "ble_process.h"
#include "time_t.h"
#include "uart.h"
#include "aws_mosquitto.h"
#include "common.h"

#define UART_SENDING_FRAME_SIZE     1000
#define UART_DEVICE_RESP_SIZE       100

extern struct mosquitto * mosq;
extern char g_hcAddr[10];

int GWCFG_TIMEOUT_SCENEGROUP = 2000;
int GWCFG_TIMEOUT_ONLINE = 2000;
int GWCFG_TIMEOUT_DEFAULT = 1000;
int GWCFG_MIN_TIME_SCENEGROUP = 1000;
int GWCFG_MIN_TIME_ONLINE = 2000;
int GWCFG_MIN_TIME_DEFAULT = 1000;

typedef struct {
    uint8_t  priority;
    int      gwIndex;
    int      respType;
    char     itemId[10];
    uint16_t addr;
    uint8_t  data[50];
    uint8_t  dataLength;
    uint8_t  retryCount;
    uint16_t timeout;
} UartSendingFrame;

typedef struct {
    int respType;
    uint16_t deviceAddr;
    int status;
} UartDeviceResp;


static UartSendingFrame g_uartSendingFrames[UART_SENDING_FRAME_SIZE];
static UartDeviceResp   g_uartDeviceResps[UART_DEVICE_RESP_SIZE];
int g_gatewayFds[GATEWAY_NUM] = { 0, 0 };
int g_uartSendingFramesIdx = 0;
int g_uartSendingIdx = 0;
extern bool g_printLog;

void BLE_SetDeviceResp(int respType, uint16_t deviceAddr, int status, bool printLog) {
    if (printLog) {
        logInfo("[BLE_SetDeviceResp] respType=%d, deviceAddr=%04X, status=%d", respType, deviceAddr, status);
    }
    for (int i = 0; i < UART_DEVICE_RESP_SIZE; i++) {
        if ((g_uartDeviceResps[i].respType == 0xff || g_uartDeviceResps[i].respType == respType)
            && g_uartDeviceResps[i].deviceAddr == deviceAddr) {
            g_uartDeviceResps[i].status = status;
            return;
        }
    }
}

void addRespTypeToSendingFrame(int respType, const char* itemId) {
    ASSERT(itemId);
    if (respType >= 0) {
        g_uartSendingFrames[g_uartSendingFramesIdx].respType = respType;
        logInfo("addRespTypeToSendingFrame[%d] = %d", g_uartSendingFramesIdx, respType);
        StringCopy(g_uartSendingFrames[g_uartSendingFramesIdx].itemId, itemId);
    }
}


void addTimeoutToSendingFrame(uint16_t timeout) {
    g_uartSendingFrames[g_uartSendingFramesIdx].timeout = timeout + (rand() % 50);
}

UartSendingFrame* sendFrameToGwIndex(int gwIndex, uint16_t addr, uint8_t* data, size_t len) {
    ASSERT(data);
    ASSERT(len > 0);
    gwIndex = 0;
    // char tmp[100];
    // memset(tmp, 100, 0);
    // for (int i = 0; i < len; i++) {
    //         sprintf(&tmp[i*3], "%02X ", data[i]);
    // }
    // logInfo("sendFrameToGwIndex[%d]: %s", len, tmp);

    gwIndex = gwIndex % 2;
    // Find lastest index
    int lastestIndex = 0;
    for (int i = UART_SENDING_FRAME_SIZE - 1; i >= 0; i--) {
        if (g_uartSendingFrames[i].respType != 0) {
            lastestIndex = i;
            break;
        }
    }
    int emptyIndex = lastestIndex == UART_SENDING_FRAME_SIZE - 1? 0 : lastestIndex + 1;
    // logInfo("emptyIndex: %d", emptyIndex);
    g_uartSendingFrames[emptyIndex].respType = 0xff;
    g_uartSendingFrames[emptyIndex].gwIndex = gwIndex;
    g_uartSendingFrames[emptyIndex].addr = addr;
    g_uartSendingFrames[emptyIndex].priority = 1;
    memcpy(g_uartSendingFrames[emptyIndex].data, data, len);
    g_uartSendingFrames[emptyIndex].dataLength = len;
    g_uartSendingFrames[emptyIndex].retryCount = 3;
    g_uartSendingFrames[emptyIndex].timeout = GWCFG_TIMEOUT_DEFAULT;
    g_uartSendingFramesIdx = emptyIndex;
    return &g_uartSendingFrames[emptyIndex];
}

UartSendingFrame* sendFrameToAnyGw(uint16_t addr, uint8_t* data, size_t len) {
    ASSERT(data);
    return sendFrameToGwIndex(-1, addr, data, len);
}
// int g_sendingIndexTmp = -1;
UartSendingFrame* findFrameToSend() {
    for (uint8_t priority = 0; priority < 5; priority++) {
        for (int i = 0; i < UART_SENDING_FRAME_SIZE; i++) {
            if (g_uartSendingFrames[i].respType > 0) {
                // if (i == 2) {
                //     printInfo("[%d,%d], priority: %d", g_uartSendingFrames[i].gwIndex, gwIndex, g_uartSendingFrames[i].priority);
                // }
                if (g_uartSendingFrames[i].priority == priority) {
                    // printInfo("sending index: %d", i);
                    // g_sendingIndexTmp = i;
                    return &g_uartSendingFrames[i];
                }
            }
        }
    }
    return NULL;
}

void BLE_SendToGateway() {
    static uint8_t  state;
    static long long int sentTime = 0;
    static UartSendingFrame sentFrame;
    static uint8_t failedCount;
    static uint8_t respIdx;
    static uint8_t sendingIsBusy = false;

    switch (state) {
        case 0: {
            // if (g_sendingIndexTmp >= 0) {
            //     logInfo("reset respType 0 [%d]: %d", g_sendingIndexTmp, g_uartSendingFrames[g_sendingIndexTmp].respType);
            // }
            // Find frame to send
            UartSendingFrame* frame = findFrameToSend();
            long long int currentTime = timeInMilliseconds();
            // uint8_t anotherGwIndex = gwIndex == 0? 1 : 0;
            if (frame != NULL && sendingIsBusy == false) {
                // printf("currentTime: %ld, sentTime: %ld\n", currentTime, sentTime[anotherGwIndex]);
                sentFrame.timeout = frame->timeout;
                sentFrame.retryCount = frame->retryCount;
                sentFrame.respType = frame->respType;
                sentFrame.addr = frame->addr;
                sentFrame.dataLength = frame->dataLength;
                sentFrame.priority = frame->priority;
                sentFrame.gwIndex = frame->gwIndex == -1? 0 : frame->gwIndex;
                StringCopy(sentFrame.itemId, frame->itemId);
                memcpy(sentFrame.data, frame->data, frame->dataLength);
                frame->respType = 0;
                memset(frame->data, 50, 0);
                // logInfo("reset respType 1 [%d]: %d", g_sendingIndexTmp, g_uartSendingFrames[g_sendingIndexTmp].respType);
                state = 1;
                failedCount = 0;
                sentTime = timeInMilliseconds();
                sendingIsBusy = true;
            }
            break;
        }
        case 1: {
            // logInfo("reset respType 2 [%d]: %d", g_sendingIndexTmp, g_uartSendingFrames[g_sendingIndexTmp].respType);
            // Send frame to gateway
            g_uartSendingIdx = sentFrame.gwIndex == 0? 3 : 2;
            UART_Send(g_gatewayFds[sentFrame.gwIndex], sentFrame.data, sentFrame.dataLength);
            
            // Go to step 2 to check timeout if sentFrame.timeout > 0, otherwise go to step 3 to wait a while
            if (sentFrame.timeout > 0) {
                state = 2;
            } else {
                state = 3;
            }

            if (failedCount == 0) {
                for (int i = 0; i < UART_DEVICE_RESP_SIZE; i++) {
                    if (g_uartDeviceResps[i].respType == 0) {
                        g_uartDeviceResps[i].respType = sentFrame.respType;
                        g_uartDeviceResps[i].deviceAddr = sentFrame.addr;
                        g_uartDeviceResps[i].status = -1;
                        respIdx = i;
                        break;
                    }
                }
            }
            break;
        }
        case 2: {
            // Wait for response
            long long int currentTime = timeInMilliseconds();
            if (g_uartDeviceResps[respIdx].respType > 0 && g_uartDeviceResps[respIdx].status == 0) {
                g_uartDeviceResps[respIdx].respType = 0;
                g_uartDeviceResps[respIdx].deviceAddr = 0;
                if (sentFrame.priority != 2) {
                    logInfo("Sending to 0x%04X took %d ms", sentFrame.addr, currentTime - sentTime);
                }
                sentFrame.respType = 0;
                state = 3;  // Response is OK. Goto next step to wait for a while before sending next frame
            }

            // Timeout handling
            if (timeInMilliseconds() - sentTime > sentFrame.timeout) {
                sentTime = timeInMilliseconds();
                failedCount++;
                if (failedCount < sentFrame.retryCount) {
                    state = 1;      // Goto step 1 to retry sending
                } else {
                    // TIMEOUT occurs
                    g_uartDeviceResps[respIdx].respType = 0;
                    g_uartDeviceResps[respIdx].deviceAddr = 0;
                    if (sentFrame.respType != 0xFF) {
                        logInfo("Sending to 0x%04X is TIMEOUT", sentFrame.addr);
                    }
                    // Send TIMEOUT response to CORE service
                    JSON* p = JSON_CreateObject();
                    if (sentFrame.respType == GW_RESPONSE_GROUP) {
                        char str[50];
                        sprintf(str, "%04X", sentFrame.addr);
                        JSON_SetText(p, "deviceAddr", str);
                        JSON_SetText(p, "groupAddr", sentFrame.itemId);
                        JSON_SetNumber(p, "status", -1);
                        sendPacketTo(SERVICE_CORE, sentFrame.respType, p);
                    } else if (sentFrame.respType == GW_RESPONSE_ADD_SCENE) {
                        char str[50];
                        sprintf(str, "%04X", sentFrame.addr);
                        JSON_SetText(p, "deviceAddr", str);
                        JSON_SetText(p, "sceneId", sentFrame.itemId);
                        JSON_SetNumber(p, "status", -1);
                        sendPacketTo(SERVICE_CORE, sentFrame.respType, p);
                    } else if (sentFrame.respType == GW_RESP_ONOFF_STATE) {
                        JSON_SetText(p, "hcAddr", g_hcAddr);
                        JSON* devicesArray = JSON_AddArray(p, "devices");
                        JSON* arrayItem = JArr_CreateObject(devicesArray);
                        char str[50];
                        sprintf(str, "%04X", sentFrame.addr);
                        JSON_SetText(arrayItem, "deviceAddr", str);
                        JSON_SetNumber(arrayItem, "deviceState", TYPE_DEVICE_OFFLINE);
                        sendPacketTo(SERVICE_CORE, GW_RESP_ONLINE_STATE, p);
                    }
                    JSON_Delete(p);
                    sendingIsBusy = false;
                    memset(sentFrame.data, 50, 0);
                    // logInfo("reset respType 3 [%d]: %d", g_sendingIndexTmp, g_uartSendingFrames[g_sendingIndexTmp].respType);
                    state = 0;      // Goto step 0 to process next frame
                }
            }
            break;
        }
        case 3: {
            // Wait 1000ms before sending next frame
            int timeout = GWCFG_MIN_TIME_DEFAULT;
            if (sentFrame.respType == GW_RESPONSE_GROUP || sentFrame.respType == GW_RESPONSE_ADD_SCENE) {
                timeout = GWCFG_MIN_TIME_SCENEGROUP;
            } else if (sentFrame.respType == GW_RESP_ONOFF_STATE) {
                timeout = GWCFG_MIN_TIME_ONLINE;
            }
            if (timeInMilliseconds() - sentTime > timeout) {
                sendingIsBusy = false;
                state = 0;
            }
            break;
        }
    }
}

void BLE_SendUartFrameLoop() {
    BLE_SendToGateway(0);
    BLE_SendToGateway(1);
}

void BLE_PrintFrame(char* str, ble_rsp_frame_t* frame) {
    ASSERT(str); ASSERT(frame);
    char paramStr[1000];
    int n = 0;
    for (int i = 0; i < frame->paramSize; i++) {
        n += sprintf(&paramStr[n], "%02x", frame->param[i]);
    }
    sprintf(str, "sendAddr: %04x, recv: %4x, opcode: %4x, param: %s", frame->sendAddr, frame->recvAddr, frame->opcode, paramStr);
}

bool ble_getInfoProvison(provison_inf *PRV, JSON* packet)
{
    ASSERT(PRV); ASSERT(packet);
    PRV->appkey = JSON_GetText(packet, KEY_APP_KEY);
    PRV->ivIndex = JSON_GetText(packet, KEY_IV_INDEX);
    PRV->netkeyIndex = JSON_GetText(packet, KEY_NETKEY_INDEX);
    PRV->netkey = JSON_GetText(packet, KEY_NETKEY);
    PRV->appkeyIndex = JSON_GetText(packet, KEY_APP_KEY_INDEX);
    PRV->deviceKey1 = JSON_GetText(packet, "deviceKey1");
    PRV->deviceKey2 = JSON_GetText(packet, "deviceKey2");
    PRV->address1 = JSON_GetText(packet, "gateway1");
    PRV->address2 = JSON_GetText(packet, "gateway2");
    return true;
}

bool GW_GetDevicesOnOffBroardcast(int gwIndex) {
    uint8_t  data[] = {0xe8,0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0xE0,0x11,0x02,0x00,0x00, 0xF1,0x00, 0x01,0x00};
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, 0xFFFF, data, 19);
    frame->timeout = 0;     // Disable handling timeout for this command
    return true;
}

bool GW_GetDeviceOnOffState(int gwIndex, const char* dpAddr) {
    ASSERT(dpAddr);
    uint8_t  data[] = {0xe8,0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0x82,0x01};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    bool found = false;
    for (int i = 0; i < UART_SENDING_FRAME_SIZE; i++) {
        if (g_uartSendingFrames[i].addr == dpAddrHex && g_uartSendingFrames[i].respType > 0 && g_uartSendingFrames[i].priority == 2) {
            found = true;
        }
    }
    if (found == false) {
        int sentLength = 0, i = 0;
        data[9] = dpAddrHex & (0xFF);
        data[8] = (dpAddrHex >> 8) & 0xFF;
        UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 12);
        frame->timeout = GWCFG_TIMEOUT_ONLINE;
        frame->priority = 2;
        frame->retryCount = 1;      // Don't retry for getting status frame
        return true;
    }
    return false;
}

bool GW_ConfigGateway(int gwIndex, provison_inf *PRV)
{
    ASSERT(PRV); ASSERT(PRV->appkey); ASSERT(PRV->netkey);
    gwIndex = gwIndex % 2;
    int i = 0;
    int tmp_len = 0;
    g_uartSendingIdx = gwIndex == 0 ? 3 : 2;

    char *appkey;
    char *ivIndex;
    char *netkeyIndex;
    char *netkey;
    char *appkeyIndex;
    char *deviceKey;
    char *address_t;

    unsigned char RESET_GW[3] = {0xe9,0xff,0x02};                   //FIX
    unsigned char SET_NODE_PARA_GW[4] = {0xe9,0xff,0x16,0x00};      //FIX
    unsigned char GET_PRO_SELF_STS_GW[3] = {0xe9,0xff,0x0c};        //FIX
    unsigned char SET_NODE_REPONS_LIVE[3] = {0xec,0xff,0x01};       //FIX

    unsigned char SET_PRO_PARA_GW[28] = {0};            //NETKEYS
    SET_PRO_PARA_GW[0] = 0xe9;
    SET_PRO_PARA_GW[1] = 0xff;
    SET_PRO_PARA_GW[2] = 0x09;

    unsigned char SET_DEV_KEY_GW[21] = {0};
    SET_DEV_KEY_GW[0] = 0xe9;
    SET_DEV_KEY_GW[1] = 0xff;
    SET_DEV_KEY_GW[2] = 0x0d;

    unsigned char START_KEYBIND_GW[22] = {0};           //APPKEYS
    START_KEYBIND_GW[0] = 0xe9;
    START_KEYBIND_GW[1] = 0xff;
    START_KEYBIND_GW[2] = 0x0b;
    START_KEYBIND_GW[3] = 0x00;

    appkey = (char *)PRV->appkey;
    ivIndex = (char *)PRV->ivIndex;
    netkeyIndex = (char *)PRV->netkeyIndex;
    netkey = (char *)PRV->netkey;
    appkeyIndex = (char *)PRV->appkeyIndex;
    if (gwIndex == 0) {
        deviceKey = (char *)PRV->deviceKey1;
        address_t = (char *)PRV->address1;
    } else {
        deviceKey = (char *)PRV->deviceKey2;
        address_t = (char *)PRV->address2;
    }

    #ifdef _DEBUG
    {
        printf("provison_GW into\n");

        printf("\n\n\n[PRV]appkey : %s\n",appkey);
        printf("[PRV]ivIndex : %s\n",ivIndex);
        printf("[PRV]netkeyIndex : %s\n",netkeyIndex);
        printf("[PRV]netkey : %s\n",netkey);
        printf("[PRV]appkeyIndex : %s\n",appkeyIndex);
        printf("[PRV]address : %s\n",address_t);
        printf("[PRV]deviceKey : %s\n\n\n",deviceKey);
    }
    #endif /* _DEBUG */

    //netkey
    tmp_len = (int)strlen(netkey);
    unsigned char *hex_netkey;
    hex_netkey  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(netkey,hex_netkey);

    if(tmp_len == 32)//
    {
        for(i = 0;i<tmp_len/2;i++) //len= 32
        {
            SET_PRO_PARA_GW[i+3] = *(hex_netkey+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth netkey\n");
        }
        #endif
        for(i = 0;i<16;i++) //len= 32
        {
            SET_PRO_PARA_GW[i+3] = 0x00;
        }
    }
    free(hex_netkey);

    //netkeyIndex
    tmp_len = (int)strlen(netkeyIndex);
    unsigned char *hex_netkeyIndex;
    hex_netkeyIndex  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(netkeyIndex,hex_netkeyIndex);
    if(tmp_len == 4)//
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_PRO_PARA_GW[i+19] = *(hex_netkeyIndex+i);
        }
    }
    else if(tmp_len == 1)
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth netkeyIndex\n");
        }
        #endif
        for(i = 0;i<2;i++)
        {
            SET_PRO_PARA_GW[i+19] = 0x00;
        }
    }
    free(hex_netkeyIndex);

    //iv update flag
    SET_PRO_PARA_GW[i+21] = 0x00;

    //ivIndex
    tmp_len = (int)strlen(ivIndex);
    unsigned char *hex_ivIndex;
    hex_ivIndex  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(ivIndex,hex_ivIndex);
    if(tmp_len == 8)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_PRO_PARA_GW[i+22] = *(hex_ivIndex+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth ivIndex\n");
        }
        #endif
        for(i = 0;i<4;i++)
        {
            SET_PRO_PARA_GW[i+22] = 0x00;
        }
    }
    free(hex_ivIndex);

    tmp_len = (int)strlen(address_t);
    unsigned char *hex_address;
    hex_address  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(address_t,hex_address);
    if(tmp_len == 4)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_PRO_PARA_GW[i+26] = *(hex_address+i);
            SET_DEV_KEY_GW[i+3] = 0;
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth address_t\n");
        }
        #endif
        for(i = 0;i<2;i++)
        {
            SET_PRO_PARA_GW[i+26] = 0x00;
            SET_DEV_KEY_GW[i+3]= 0x00;
        }
    }
    free(hex_address);


    printf("[LOG] SET_PRO_PARA_GW: ");
    for(i = 0;i<28;i++)
    {
        printf("%02x",SET_PRO_PARA_GW[i]);
    }
    printf("\n");


    //device key
    tmp_len = (int)strlen(deviceKey);
    unsigned char hex_deviceKey[100];
    String2HexArr(deviceKey,hex_deviceKey);
    if(tmp_len == 32)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_DEV_KEY_GW[i+5] = *(hex_deviceKey+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth deviceKey\n");
        }
        #endif
        for(i = 0;i<16;i++)
        {
            SET_DEV_KEY_GW[i+5] = 0x00;
        }
    }

    #ifdef _DEBUG
    {
        printf("[LOG] SET_DEV_KEY_GW: ");
        for(i = 0;i<21;i++)
        {
            printf("%02x",SET_DEV_KEY_GW[i]);
        }
        printf("\n");
    }
    #endif /* _USE_DEBUG */

    //appkeyIndex
    tmp_len = (int)strlen(appkeyIndex);
    unsigned char hex_appkeyIndex[100];
    String2HexArr(appkeyIndex,hex_appkeyIndex);
    if(tmp_len == 4)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            START_KEYBIND_GW[i+4] = *(hex_appkeyIndex+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth appkeyIndex\n");
        }
        #endif
        START_KEYBIND_GW[4] = 0x00;
        START_KEYBIND_GW[5] = 0x00;
    }

    //appkey
    tmp_len = (int)strlen(appkey);
    unsigned char hex_appkey[100];
    String2HexArr(appkey,hex_appkey);
    if(tmp_len == 32)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            START_KEYBIND_GW[i+6] = *(hex_appkey+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth appkey\n");
        }
        #endif
        for(i = 0;i<16;i++)
        {
            START_KEYBIND_GW[i+6] = 0x00;
        }
    }

    #ifdef _DEBUG
    {
        printf("[LOG] START_KEYBIND_GW: ");
        for(i = 0;i<22;i++)
        {
            printf("%02x",START_KEYBIND_GW[i]);
        }
        printf("\n");
    }
    #endif /* _USE_DEBUG */


    printf("\n\n\n[LOG] START PROVISON................. \n");
    printf("[LOG] RESET_GW: ");
    for(i = 0;i<3;i++)
    {
        printf("%02x ",RESET_GW[i]);
    }
    printf("\n");

    UART_Send(g_gatewayFds[gwIndex], RESET_GW, 3);
    sleep(5);
    #ifdef _DEBUG
    {
        printf("        RESET_GW DONE.\n");
    }
    #endif/*_DEBUG */


    printf("[LOG] SET_NODE_PARA_GW: ");
    for(i = 0;i<4;i++)
    {
        printf("%02x ",SET_NODE_PARA_GW[i]);
    }
    printf("\n");

    UART_Send(g_gatewayFds[gwIndex], SET_NODE_PARA_GW, 4);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        SET_NODE_PARA_GW DONE\n");
    }
    #endif/*_DEBUG */


    printf("[LOG] GET_PRO_SELF_STS_GW: ");
    for(i = 0;i<5;i++)
    {
        printf("%02x ",GET_PRO_SELF_STS_GW[i]);
    }
    printf("\n");
    UART_Send(g_gatewayFds[gwIndex], GET_PRO_SELF_STS_GW, 3);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        GET_PRO_SELF_STS_GW DONE\n");
    }
    #endif/*_DEBUG */

    printf("[LOG] SET_PRO_PARA_GW: ");
    for(i = 0;i<28;i++)
    {
        printf("%02x ",SET_PRO_PARA_GW[i]);
    }
    printf("\n");
    UART_Send(g_gatewayFds[gwIndex], SET_PRO_PARA_GW, 28);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        SET_PRO_PARA_GW DONE\n");
    }
    #endif/*_DEBUG */

    printf("[LOG] SET_DEV_KEY_GW: ");
    for(i = 0;i<21;i++)
    {
        printf("%02x ",SET_DEV_KEY_GW[i]);
    }
    printf("\n");
    UART_Send(g_gatewayFds[gwIndex], SET_DEV_KEY_GW, 21);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        SET_DEV_KEY_GW DONE\n");
    }
    #endif/*_DEBUG */


    printf("[LOG] START_KEYBIND_GW: ");
    for(i = 0;i<22;i++)
    {
        printf("%02x ",START_KEYBIND_GW[i]);
    }
    printf("\n");
    UART_Send(g_gatewayFds[gwIndex], START_KEYBIND_GW, 22);
    #ifdef _DEBUG
    {
        printf("        START_KEYBIND_GW DONE\n");
    }
    #endif/*_DEBUG */
    sleep(TIME_DELAY_WAIT_DONE_PROVISION);

    printf("[LOG] SET_NODE_REPONS_LIVE: ");
    for(i = 0;i<3;i++)
    {
        printf("%02x ",SET_NODE_REPONS_LIVE[i]);
    }
    printf("\n");
    UART_Send(g_gatewayFds[gwIndex], SET_NODE_REPONS_LIVE, 3);
    sleep(TIME_DELAY_PROVISION);
    printf("[LOG] DONE PROVISION>>>>>>>>>>>>>>>>>>>>>>>>\n");
    return true;
}


int get_count_element_of_DV(const char* pid_) {
    if(isMatchString(pid_,HG_BLE_SWITCH_1)) {
        return 1;
    } else if(isMatchString(pid_,HG_BLE_SWITCH_2)) {
        return 2;
    } else if(isMatchString(pid_,HG_BLE_SWITCH_3)) {
        return 3;
    } else if(isMatchString(pid_,HG_BLE_SWITCH_4)) {
        return 4;
    } else if(isMatchString(pid_,HG_BLE_CURTAIN_NORMAL)) {
        return 1;
    } else if(isMatchString(pid_,HG_BLE_ROLLING_DOOR)) {
        return 1;
    } else if(isMatchString(pid_,HG_BLE_ROLLING_DOOR_SWITCH)) {
        return 1;
    } else if(isMatchString(pid_,HG_BLE_ROLLING_DOOR_MODULE)) {
        return 1;
    } else if(isMatchString(pid_,HG_BLE_CURTAIN_2_LAYER)) {
        return 2;
    } else {
        return 0;
    }
}

void get_string_add_DV_write_GW(char **result,const char* address_device,const char* element_count,const char* deviceID)
{
    //*result  = (char*) malloc(46 * sizeof(char));
    const char *tmp_0 = "e9ff12";
    const char *tmp_1 = address_device;
    const char *tmp_2 = element_count;
    const char *tmp_3 = "00";
    const char *tmp_4 = deviceID;
    strcpy(*result, tmp_0);
    strcat(*result, tmp_1);
    strcat(*result, "0");
    strcat(*result, tmp_2);
    strcat(*result, "00");
    strcat(*result, deviceID);
}

int GW_SaveDeviceKey(int gwIndex, const char* deviceAddr, const char* pid,const char* deviceKey)
{
    ASSERT(deviceAddr); ASSERT(pid); ASSERT(deviceKey);
    gwIndex = 0;
    g_uartSendingIdx = gwIndex == 0 ? 3 : 2;
    char *str_send_uart = (char*) malloc(1000 * sizeof(char));
    unsigned char *hex_send_uart;
    int check = 0;
    char *element_count_str = (char*)malloc(2);

    int  element_count = get_count_element_of_DV(pid);
    Int2String(element_count,element_count_str);
    get_string_add_DV_write_GW(&str_send_uart,deviceAddr,element_count_str,deviceKey);
    int len_str = (int)strlen(str_send_uart);
    hex_send_uart  = (char*) malloc(len_str * sizeof(char)*10);
    String2HexArr(str_send_uart,hex_send_uart);
    UART_Send(g_gatewayFds[gwIndex],hex_send_uart,len_str/2);
    usleep(SAVE_DEVICE_KEY_DELAY_MS);
    free(element_count_str);
    free(hex_send_uart);
    free(str_send_uart);
    hex_send_uart = NULL;
    if(check != len_str/2)
    {
        return -1;
    }
    return 0;
}

bool GW_HgSwitchOnOff(int gwIndex, const char* dpAddr, uint8_t dpValue)
{
    ASSERT(dpAddr);

    uint8_t data[] = {0xe8, 0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0x82,0x02, 0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    data[9] = dpAddrHex & (0xFF);
    data[8] = (dpAddrHex >> 8) & 0xFF;
    data[12] = dpValue;

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 13);
    frame->priority = 0;
    return true;
}

bool GW_HgSwitchOnOff_NoResp(int gwIndex, const char* dpAddr, uint8_t dpValue)
{
    ASSERT(dpAddr);

    uint8_t data[] = {0xe8, 0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0x82,0x03, 0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    data[9] = dpAddrHex & (0xFF);
    data[8] = (dpAddrHex >> 8) & 0xFF;
    data[12] = dpValue;

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 13);
    frame->priority = 0;
    return true;
}


bool GW_SwitchDimLed(int gwIndex, const char *address_device, int lightness)
{
    ASSERT(address_device);
    long int addrHex = strtol(address_device, NULL, 16);
    uint8_t hex_address[5];
    uint8_t hex_lightness[5];
    char lightness_s[5];

    int lightness_ = lightness * 65535 / 100;
    Int2Hex_2byte(lightness_, lightness_s);
    uint8_t  SET_DIM[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0x4c,0x00,0x00};

    String2HexArr((char*)address_device,hex_address);
    String2HexArr((char*)lightness_s,hex_lightness);

    SET_DIM[8] = *hex_address;
    SET_DIM[9] = *(hex_address+1);

    SET_DIM[13] = *hex_lightness;
    SET_DIM[12] = *(hex_lightness+1);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, SET_DIM, 14);
    return true;
}

bool GW_AddGroupLight(int gwIndex, const char *address_group,const char *address_device,const char *address_element)
{
    ASSERT(address_group); ASSERT(address_device); ASSERT(address_element);
    gwIndex = gwIndex % 2;

    uint8_t  SET_GROUP[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00   ,0xff,0xff ,0x80,0x1b  ,0x00,0x00,0x00,0x00,  0x00,0x10};
    long int dpAddrHex = strtol(address_device, NULL, 16);
    uint8_t hex_address_group[5];
    uint8_t hex_address_device[5];
    uint8_t hex_address_element[5];

    String2HexArr((char*)address_group, hex_address_group);
    String2HexArr((char*)address_device, hex_address_device);
    String2HexArr((char*)address_element, hex_address_element);

    SET_GROUP[8] = *hex_address_device;
    SET_GROUP[9] = *(hex_address_device+1);

    SET_GROUP[12] = *hex_address_element;
    SET_GROUP[13] = *(hex_address_element+1);

    SET_GROUP[14] = *hex_address_group;
    SET_GROUP[15] = *(hex_address_group+1);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, SET_GROUP, 18);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}


bool GW_AddGroupSwitch(int gwIndex, const char *groupAddr, const char *deviceAddr, const char *dpAddr)
{
    ASSERT(groupAddr);  ASSERT(deviceAddr);  ASSERT(dpAddr);
    gwIndex = gwIndex % 2;

    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0x80,0x1b  ,0x00,0x00,0x00,0x00,  0x00,0x10,0x01};
    long int deviceAddrHex = strtol(deviceAddr, NULL, 16);
    uint8_t hex_address_group[5];
    uint8_t hex_address_device[5];
    uint8_t hex_address_element[5];

    String2HexArr((char*)groupAddr, hex_address_group);
    String2HexArr((char*)deviceAddr, hex_address_device);
    String2HexArr((char*)dpAddr, hex_address_element);

    data[8] = *hex_address_device;
    data[9] = *(hex_address_device + 1);

    data[12] = *hex_address_element;
    data[13] = *(hex_address_element + 1);

    data[14] = *hex_address_group;
    data[15] = *(hex_address_group + 1);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, deviceAddrHex, data, 18);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_DeleteGroup(int gwIndex, const char *groupAddr, const char *deviceAddr, const char *dpAddr)
{
    ASSERT(groupAddr); ASSERT(deviceAddr); ASSERT(dpAddr);
    gwIndex = gwIndex % 2;

    uint8_t  SET_GROUP[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00   ,0xff,0xff ,0x80,0x1c  ,0x00,0x00,0x00,0x00,  0x00,0x10};
    long int deviceAddrHex = strtol(deviceAddr, NULL, 16);
    uint8_t hex_address_group[5];
    uint8_t hex_address_device[5];
    uint8_t hex_address_element[5];

    String2HexArr((char*)groupAddr, hex_address_group);
    String2HexArr((char*)deviceAddr, hex_address_device);
    String2HexArr((char*)dpAddr, hex_address_element);

    SET_GROUP[8] = hex_address_device[0];
    SET_GROUP[9] = hex_address_device[1];

    SET_GROUP[12] = hex_address_element[0];
    SET_GROUP[13] = hex_address_element[1];

    SET_GROUP[14] = hex_address_group[0];
    SET_GROUP[15] = hex_address_group[1];

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, deviceAddrHex, SET_GROUP, 18);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

/************ Commands to controll light *******************/
bool GW_CtrlLightOnOff(int gwIndex, const char *deviceAddr, uint8_t onoff) {
    ASSERT(deviceAddr);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00, 0x00, 0x00,  0xff,0xff,  0x82,0x02, 0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[12] = onoff;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 13);
    frame->priority = 0;
    return true;
}

bool GW_SetLightness(int gwIndex, const char *deviceAddr, int lightness) {
    ASSERT(deviceAddr);
    logInfo("GW_SetLightness: deviceAddr = %s, color = %d", deviceAddr, lightness);
    lightness = lightness * 0xFFFF / 1000;   // value of lightness is in range 0 - 1000, so we need to convert to range 0 - 0xFFFF
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00, 0x00, 0x00,  0xff,0xff,  0x82,0x4c, 0x00,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[12] = (uint8_t)lightness;
    data[13] = (uint8_t)(lightness >> 8);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 14);
    frame->priority = 0;
    return true;
}

bool GW_SetLightColor(int gwIndex, const char *deviceAddr, int color) {
    ASSERT(deviceAddr);
    logInfo("GW_SetLightColor: deviceAddr = %s, color = %d", deviceAddr, color);
    color = 800 + (20000 - 800) * color / 1000;
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00, 0x00, 0x00,  0xff,0xff,  0x82,0x64, 0x00,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[12] = (uint8_t)color;
    data[13] = (uint8_t)(color >> 8);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 14);
    frame->priority = 0;
    return true;
}


bool GW_SetLightnessTemperature(int gwIndex, const char *dpAddr, int lightness, int colorTemperature)
{
    ASSERT(dpAddr);

    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    char *lightness_s = malloc(5);
    char *colorTemperature_s = malloc(5);
    int lightness_ = lightness*65535/1000;
    int colorTemperature_ = 800 + (20000 - 800)*colorTemperature/1000;
    Int2Hex_2byte(lightness_,lightness_s);
    Int2Hex_2byte(colorTemperature_,colorTemperature_s);

    int check = 0,i = 0;
    uint8_t  SET_CT[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0x5e,0x00,0x00,0x00,0x00};

    uint8_t hex_address[5];
    uint8_t hex_lightness[5];
    uint8_t hex_colorTemperature[5];


    String2HexArr((char*)dpAddr,hex_address);
    String2HexArr((char*)lightness_s,hex_lightness);
    String2HexArr((char*)colorTemperature_s,hex_colorTemperature);

    SET_CT[8] = hex_address[0];
    SET_CT[9] = hex_address[1];

    SET_CT[13] = hex_lightness[0];
    SET_CT[12] = hex_lightness[1];
    SET_CT[15] = hex_colorTemperature[0];
    SET_CT[14] = hex_colorTemperature[1];

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, SET_CT, 16);
    frame->priority = 0;
    return true;
}

bool GW_SetLightHSL(int gwIndex, const char *dpAddr, const char *HSL)
{
    ASSERT(dpAddr); ASSERT(HSL);

    uint8_t  SET_HSL[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0x76,0x00,0x00,0x00,0x00,0x00,0x00};

    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    uint8_t hex_address[5];
    uint8_t hex_HSL[13];
    String2HexArr((char*)dpAddr, hex_address);
    String2HexArr((char*)HSL, hex_HSL);

    SET_HSL[8] = hex_address[0];
    SET_HSL[9] = hex_address[1];

    SET_HSL[13] = hex_HSL[0];
    SET_HSL[12] = hex_HSL[1];
    SET_HSL[15] = hex_HSL[2];
    SET_HSL[14] = hex_HSL[3];
    SET_HSL[17] = hex_HSL[4];
    SET_HSL[16] = hex_HSL[5];

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, SET_HSL, 18);
    frame->priority = 0;
    return true;
}


bool GW_SetRGBLightBlinkMode(int gwIndex, const char *dpAddr, int blinkMode) {
    ASSERT(dpAddr);
    if (blinkMode >= 0) {
        uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0x82,0x50,0x19,0x09,  0x00};

        long int dpAddrHex = strtol(dpAddr, NULL, 16);
        data[8] = (uint8_t)(dpAddrHex >> 8);
        data[9] = (uint8_t)(dpAddrHex);
        data[14] = (uint8_t)blinkMode;

        UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 15);
        frame->priority = 0;
        return true;
    }
    return true;
}

int GW_SplitFrame(ble_rsp_frame_t resultFrames[MAX_FRAME_COUNT], uint8_t* originPackage, size_t size) {
    ASSERT(originPackage);
    int frameCount = 0;
    if (size < 10) {
        resultFrames[frameCount].opcode = 0;
        return 0;
    }
    for (int i = 2; i < size - 1; i++) {
        resultFrames[frameCount].opcode = 0;
        // Find the position of FLAG mark (2 bytes: 0x9181)
        if (originPackage[i] == 0x91) {
            // Calculate length of a single frame (2 bytes before FLAG mark)
            int frameSize = originPackage[i - 1];
            int frameIdx = 0;
            frameSize = (frameSize << 8) | originPackage[i - 2];
            resultFrames[frameCount].frameSize = frameSize;
            resultFrames[frameCount].flag = ((uint16_t)originPackage[i] << 8) | originPackage[i + 1];;
            if (originPackage[i + 1] == 0x9d) {
                // Online/Online frame
                resultFrames[frameCount].onlineState = originPackage[i + 8] > 0? 1 : 0;
                resultFrames[frameCount].onlineState2 = originPackage[i + 14] > 0? 1 : 0;
                resultFrames[frameCount].sendAddr = ((uint16_t)originPackage[i + 6] << 8) | originPackage[i + 7];
                resultFrames[frameCount].sendAddr2 = ((uint16_t)originPackage[i + 12] << 8) | originPackage[i + 13];
                resultFrames[frameCount].opcode = 0;
            } else {
                // Other frames
                resultFrames[frameCount].sendAddr = ((uint16_t)originPackage[i + 2] << 8) | originPackage[i + 3];
                resultFrames[frameCount].recvAddr = ((uint16_t)originPackage[i + 4] << 8) | originPackage[i + 5];
                resultFrames[frameCount].opcode = ((uint16_t)originPackage[i + 6] << 8) | originPackage[i + 7];
            }
            // Read param
            resultFrames[frameCount].paramSize = 0;
            uint8_t* param = resultFrames[frameCount].param;
            int j = 0;
            for (j = i + 8; j < i + frameSize; j++) {
                *param = originPackage[j];
                resultFrames[frameCount].paramSize++;
                param++;
            }
            frameCount++;
            i = j;
        }
    }
    return frameCount;
}

int GW_CheckReceivedFrame(struct state_element *temp, ble_rsp_frame_t* frame)
{
    ASSERT(temp); ASSERT(frame);
    uint8_t len_uart = frame->frameSize + 2;
    sprintf(temp->address_element, "%04X", frame->sendAddr);

    if (frame->flag == 0x919d) {
        // Online/Offline
        return GW_RESP_ONLINE_STATE;
    } else if (frame->opcode == 0xe111 && frame->paramSize == 4 && frame->param[0] == 0x02 && frame->param[1] == 0x16 && frame->param[2] == 0x00 && frame->param[3] == 0x01) {
        // Device is kicked out from mesh network
        return GW_RESPONSE_DEVICE_KICKOUT;
    } else if (frame->opcode == 0x804a) {
        // Device is kicked out from mesh network
        return GW_RESPONSE_DEVICE_KICKOUT;
    } else if (frame->opcode == 0xe111 && frame->paramSize == 5 && frame->param[0] == 0x02 && frame->param[1] == 0xC0 && frame->param[2] == 0x00) {
        // Lock children
        return GW_RESPONSE_LOCK_KIDS;
    } else if (frame->paramSize >= 7 && frame->opcode == 0x801f && frame->param[5] == 0x00 && frame->param[6] == 0x10) {
        // ADD_GROUP_LIGHT
        return GW_RESPONSE_GROUP;
    } else if (frame->opcode == 0x8204) {
        return GW_RESP_ONOFF_STATE;
    } else if ((frame->opcode == 0x824e || frame->opcode == 0x8266) && frame->paramSize >= 4) {
        // Response of lightness and temperature of light
        return GW_RESPONSE_LIGHT_RD_CONTROL;
    } else if (frame->opcode == 0x8260 && frame->paramSize >= 7) {
        // Response of lightness and temperature of light
        return GW_RESPONSE_LIGHT_RD_CONTROL;
    } else if ((frame->opcode == 0x8278 && frame->paramSize >= 8) || (frame->opcode == 0x8252 && frame->paramSize >= 3)) {
        // Response of lightness and temperature of light
        return GW_RESPONSE_RGB_COLOR;
    } else if(frame->opcode == 0x5208 && frame->paramSize >= 3 && frame->param[0] == 0x01) {
        // Smoke sensor
        return GW_RESPONSE_SMOKE_SENSOR;
    } else if (frame->opcode == 0x5206 && frame->paramSize >= 5 && frame->param[0] == 0x00) {
        // Temperature/Humidity sensor
        return GW_RESPONSE_SENSOR_ENVIRONMENT;
    } else if(frame->opcode == 0x5201 && frame->paramSize >= 3 && frame->param[0] == 0x00) {
        //type % pin
        return GW_RESPONSE_SENSOR_BATTERY;
    } else if(frame->opcode == 0x5209 && frame->paramSize >= 2 && frame->param[0] == 0x00) {
        // Door sensor detect
        return GW_RESPONSE_SENSOR_DOOR_DETECT;
    } else if(frame->opcode == 0x5209 && frame->paramSize >= 2 && frame->param[0] == 0x04) {
        // Door sensor hanging detect
        return GW_RESPONSE_SENSOR_DOOR_ALARM;
    } else if(frame->opcode == 0x5205 && frame->paramSize >= 2 && frame->param[0] == 0x00) {
        // PIR sensor human detect
        return GW_RESPONSE_SENSOR_PIR_DETECT;
    } else if(frame->opcode == 0x5205 && frame->paramSize >= 2 && frame->param[0] == 0x01) {
        // PIR sensor human detect
        return GW_RESPONSE_SENSOR_PIR_DETECT;
    } else if(frame->opcode == 0x5207 && frame->paramSize >= 2 && frame->param[0] == 0x01) {
        // Response current state of rolling door
        return GW_RESPONSE_SENSOR_PIR_DETECT;
    } else if(frame->opcode == 0x5204 && frame->paramSize >= 3 && frame->param[0] == 0x00) {
        // PIR sensor light intensity
        return GW_RESPONSE_SENSOR_PIR_LIGHT;
    } else if(frame->opcode == 0x520A && frame->paramSize >= 3 && frame->param[0] == 0x00) {
        // Response status for new curtain (HG_BLE_CURTAIN_IH35, HG_BLE_CURTAIN_IH68)
        return GW_RESP_NEW_CURTAIN;
    } else if (frame->opcode == 0x8245 && frame->paramSize >= 1) {
        // Add LC scene action
        return GW_RESPONSE_ADD_SCENE;
    } else if (frame->opcode == 0xe111 && frame->param[0] == 0x02 && frame->param[1] == 0x00 && frame->param[2] == 0xFF) {
        return GW_RESPONSE_FORWARD_ONLY;
    } else if (frame->opcode == 0xe111 && frame->paramSize == 7 && frame->param[0] == 0x02 && frame->param[1] == 0x02 && frame->param[2] == 0xFF) {
        return GW_RESPONSE_SENSOR_PRESENCE;
    } else if (frame->opcode == 0xe111 && frame->paramSize >= 7) {
        // Add LC scene condition
        return GW_RESPONSE_ADD_SCENE;
    } else if (frame->opcode >> 8 == 0x5E && frame->paramSize >= 6) {
        temp->dpValue =  frame->param[5];
        temp->causeType = 3;
        sprintf(temp->causeId, "%02X%02X", (frame->opcode & 0xFF), frame->param[0]);
        return GW_RESP_ONOFF_STATE;
    } else if (frame->opcode == 0x800E && frame->paramSize >= 1) {
        return GW_RESPONSE_SET_TTL;
    } else if (frame->opcode == 0xE511 && frame->paramSize >= 6 && frame->param[0] == 0x02) {
        return GW_RESPONSE_IR;
    } else if (frame->opcode == 0x5206 && frame->paramSize >= 2 && frame->param[0] == 0x01) {
        return GW_RESP_MODULE;
    }

    return GW_RESPONSE_UNKNOW;
}

bool GW_DeleteDevice(int gwIndex, const char *deviceAddr)
{
    ASSERT(deviceAddr);
    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02, 0x00,0x00,  0x40,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 17);
    return true;
}

bool GW_SetSceneActionForSwitch(int gwIndex, const char* dpAddr, const char* sceneId, uint8_t dpValue) {
    ASSERT(sceneId); ASSERT(dpAddr);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0x82,0x46,  0x00,0x00, 0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[12] = (uint8_t)(sceneIdHex >> 8);
    data[13] = (uint8_t)(sceneIdHex);
    data[14] = dpValue;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 15);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_SetSceneActionForLightCCT(int gwIndex, const char* address_device,const char* sceneID)
{
    ASSERT(address_device); ASSERT(sceneID);
    long int dpAddrHex = strtol(address_device, NULL, 16);
    uint8_t  SceneLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0x82,0x46,  0x00,0x00};
    uint8_t hex_address_device[5];
    uint8_t hex_sceneID[5];

    String2HexArr((char*)address_device, hex_address_device);
    String2HexArr((char*)sceneID, hex_sceneID);

    SceneLocalToDevice[8] = *hex_address_device;
    SceneLocalToDevice[9] = *(hex_address_device+1);

    SceneLocalToDevice[12] = *hex_sceneID;
    SceneLocalToDevice[13] = *(hex_sceneID+1);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, SceneLocalToDevice, 14);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_SetSceneActionForLightRGB(int gwIndex, const char* address_device,const char* sceneID, uint8_t blinkMode)
{
    ASSERT(address_device); ASSERT(sceneID);
    uint8_t  SceneLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0x00,0x00,   0x82,0x46,  0x00,0x00,     0x00,         0x00,0x00};
    long int dpAddrHex = strtol(address_device, NULL, 16);
    uint8_t hex_address_device[5];
    uint8_t hex_sceneID[5];

    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);

    SceneLocalToDevice[8] = hex_address_device[0];
    SceneLocalToDevice[9] = hex_address_device[1];

    SceneLocalToDevice[12] = hex_sceneID[0];
    SceneLocalToDevice[13] = hex_sceneID[1];

    SceneLocalToDevice[14] = blinkMode;

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, SceneLocalToDevice,17);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_DelSceneAction(int gwIndex, const char* dpAddr, const char* sceneId) {
    ASSERT(dpAddr); ASSERT(sceneId);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,      0x82,0x9E,  0x00,0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[12] = (uint8_t)(sceneIdHex >> 8);
    data[13] = (uint8_t)(sceneIdHex);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 14);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_DelAllSceneAction(int gwIndex, const char* dpAddr) {
    ASSERT(dpAddr);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02,0x00,0x00,  0x30,0x00, 0x01,0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);

    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 19);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_SetSceneCondition(int gwIndex, const char* dpAddr, const char* sceneId, uint8_t dpValue) {
    ASSERT(dpAddr); ASSERT(sceneId);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02,0x00,0x00,     0x50,0x00,          0x00,0x00,  0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[17] = (uint8_t)(sceneIdHex >> 8);
    data[18] = (uint8_t)(sceneIdHex);
    data[19] = dpValue;

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 20);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_DelSceneCondition(int gwIndex, const char* dpAddr, const char* sceneId) {
    ASSERT(dpAddr); ASSERT(sceneId);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02,0x00,0x00,     0x60,0x00,          0x00,0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[17] = (uint8_t)(sceneIdHex >> 8);
    data[18] = (uint8_t)(sceneIdHex);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 19);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_EnableDisableScene(const char* dpAddr, const char* sceneId, uint8_t enableOrDisable) {
    ASSERT(dpAddr); ASSERT(sceneId);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02,0x00,0x00,     0x70,0x00,          0x00,0x00, 0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[17] = (uint8_t)(sceneIdHex >> 8);
    data[18] = (uint8_t)(sceneIdHex);
    data[19] = enableOrDisable;

    UartSendingFrame* frame = sendFrameToGwIndex(0, dpAddrHex, data, 20);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_CallScene(const char* sceneId) {
    ASSERT(sceneId);
    uint8_t  data[14] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0x82,0x42,  0x00,0x00 ,     0x00,0x00,0x00};

    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[12] = (uint8_t)(sceneIdHex >> 8);
    data[13] = (uint8_t)(sceneIdHex);

    UartSendingFrame* frame = sendFrameToGwIndex(0, 0xFFFF, data, 17);
    frame->priority = 0;
    return true;
}

bool ble_setTimeForSensorPIR(int gwIndex, const char* address_device,const char* time)
{
    ASSERT(address_device); ASSERT(time);
    long int deviceAddrHex = strtol(address_device, NULL, 16);
    uint8_t setTimeForSensorPIR[19] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,      0x00,0x00,      0xE2,0x11,0x02,0xE3,0x00,  0x45,0x03,  0x00,0x00};
    uint8_t hex_address_device[5];
    uint8_t hex_time[5];

    String2HexArr((char*)address_device, hex_address_device);
    String2HexArr((char*)time, hex_time);

    setTimeForSensorPIR[8] = *hex_address_device;
    setTimeForSensorPIR[9] = *(hex_address_device + 1);

    setTimeForSensorPIR[17] = *(hex_time+1);
    setTimeForSensorPIR[18] = *(hex_time);

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, deviceAddrHex, setTimeForSensorPIR, 19);
    return true;
}

bool GW_LockDevice(int gwIndex, const char *address_element,int state)
{
    ASSERT(address_element);
    long int addrHex = strtol(address_element, NULL, 16);
    uint8_t  LOG_DEVICE[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0xE0,0x11,0x02,0x00,0x00,  0x05,0x00,   0x00};

    uint8_t  *hex_address = malloc(5);
    String2HexArr((char*)address_element,hex_address);

    LOG_DEVICE[8] = *hex_address;
    LOG_DEVICE[9] = *(hex_address+1);
    LOG_DEVICE[17] = state? 1 : 0;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, LOG_DEVICE, 19);
    return true;
}

bool GW_LockTouch(int gwIndex, const char *deviceAddr, uint8_t dpId, int state)
{
    ASSERT(deviceAddr);
    long int addrHex = strtol(deviceAddr, NULL, 16);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0xE0,0x11,0x02,0x00,0x00,  0xC0,0x00,   0x00,   0x00};
    uint8_t hex_address[5];
    String2HexArr((char*)deviceAddr, hex_address);
    data[8] = (*hex_address) + (dpId - 1);
    data[9] = *(hex_address + 1);
    data[17] = state;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, data, 19);
    return true;
}

bool GW_CtrlGroupLightOnOff(int gwIndex, const char *groupAddr, uint8_t onoff) {
    ASSERT(groupAddr);
    long int addrHex = strtol(groupAddr, NULL, 16);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff, 0x82,0x03,0x00};
    data[8] = (addrHex >> 8) & 0x00FF;
    data[9] = (addrHex & 0x00FF);
    data[12] = onoff;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, data, 13);
    frame->priority = 0;
    return true;
}

bool GW_CtrlGroupLightCT(int gwIndex, const char *dpAddr, int lightness, int colorTemperature) {
    ASSERT(dpAddr);

    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    char *lightness_s = malloc(5);
    char *colorTemperature_s = malloc(5);
    int lightness_ = lightness*65535/1000;
    int colorTemperature_ = 800 + (20000 - 800)*colorTemperature/1000;
    Int2Hex_2byte(lightness_,lightness_s);
    Int2Hex_2byte(colorTemperature_,colorTemperature_s);

    int check = 0,i = 0;
    uint8_t  SET_CT[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0x5f,0x00,0x00,0x00,0x00};

    uint8_t hex_address[5];
    uint8_t hex_lightness[5];
    uint8_t hex_colorTemperature[5];


    String2HexArr((char*)dpAddr,hex_address);
    String2HexArr((char*)lightness_s,hex_lightness);
    String2HexArr((char*)colorTemperature_s,hex_colorTemperature);

    SET_CT[8] = hex_address[0];
    SET_CT[9] = hex_address[1];

    SET_CT[13] = hex_lightness[0];
    SET_CT[12] = hex_lightness[1];
    SET_CT[15] = hex_colorTemperature[0];
    SET_CT[14] = hex_colorTemperature[1];

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, SET_CT, 16);
    frame->priority = 0;
}

bool GW_SetTTL(int gwIndex, const char *deviceAddr, uint8_t ttl) {
    ASSERT(deviceAddr); ASSERT(ttl > 0);
    gwIndex = gwIndex % 2;

    long int addrHex = strtol(deviceAddr, NULL, 16);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0x80,0x0D,0x00};
    data[8] = (addrHex >> 8) & 0x00FF;
    data[9] = (addrHex & 0x00FF);
    data[12] = ttl;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, data, 13);
    return true;
}

bool GW_ControlIRCmd(int gwIndex, const char* command) {
    ASSERT(command);
    uint8_t data[500];
    String2HexArr(command, data);
    uint16_t addr = ((uint16_t)data[9] << 8) + data[8];
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addr, data, strlen(command) / 2);
    frame->priority = 0;
    frame->retryCount = 1;
    return true;
}

bool GW_ControlIR(int gwIndex, const char* deviceAddr, int commandType, int brandId, int remoteId, int temp, int mode, int fan, int swing)
{
    ASSERT(deviceAddr);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0xff,0xff,   0xE4,0x11,0x02,0x00,0x00,    0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[15] = (uint8_t)(commandType);
    data[16] = (uint8_t)(brandId);
    data[17] = (uint8_t)(brandId >> 8);
    data[18] = (uint8_t)(remoteId);
    data[19] = (uint8_t)(temp);
    data[20] = (uint8_t)(mode);
    data[21] = (uint8_t)(fan);
    data[22] = (uint8_t)(swing);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 23);
    frame->priority = 0;
    frame->retryCount = 1;
    return true;
}

bool GW_AddSceneActionIR(int gwIndex, const char* deviceAddr, const char* sceneId, uint8_t commandType, uint8_t brandId, uint8_t remoteId, uint8_t temp, uint8_t mode, uint8_t fan, uint8_t swing) {
    ASSERT(deviceAddr);
    ASSERT(sceneId);
    ASSERT(commandType == 2 || commandType == 3);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0xff,0xff,   0xE4,0x11,0x02,0x00,0x00,    0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    temp -= 16;
    fan -= 1;
    swing -= 1;
    swing /= 2;
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[16] = (uint8_t)(sceneIdHex >> 8);
    data[17] = (uint8_t)(sceneIdHex);
    data[18] = (uint8_t)(2 << 6);   // Add: 2, Delete: 1
    data[18] |= (uint8_t)(1 << 1);  // CommandIndex is 1 as default
    data[18] |= (commandType - 2);
    data[19] = (uint8_t)(brandId);
    data[20] = (uint8_t)(brandId >> 8);
    data[21] = (uint8_t)(remoteId << 3);
    data[21] |= (uint8_t)(mode);
    data[22] = (uint8_t)(temp << 4);
    data[22] |= (uint8_t)(fan << 2);
    data[22] |= (uint8_t)(swing);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 23);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}


bool GW_DeleteSceneActionIR(int gwIndex, const char* deviceAddr, const char* sceneId, uint8_t commandType, uint8_t brandId, uint8_t remoteId) {
    ASSERT(deviceAddr);
    ASSERT(sceneId);
    ASSERT(commandType == 2 || commandType == 3);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0xff,0xff,   0xE4,0x11,0x02,0x00,0x00,    0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[16] = (uint8_t)(sceneIdHex >> 8);
    data[17] = (uint8_t)(sceneIdHex);
    data[18] = (uint8_t)(1 << 6);   // Add: 2, Delete: 1
    data[18] |= (uint8_t)(1 << 1);  // CommandIndex is 1 as default
    data[18] |= (commandType - 2);
    data[19] = (uint8_t)(brandId);
    data[20] = (uint8_t)(brandId >> 8);
    data[21] = (uint8_t)(remoteId << 3);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 23);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_AddSceneConditionIR(int gwIndex, const char* deviceAddr, const char* sceneId, uint16_t voiceCode) {
    ASSERT(deviceAddr);
    ASSERT(sceneId);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0xff,0xff,   0xE4,0x11,0x02,0x00,0x00,    0x06,0x00,0x00,0x00,0x00,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[16] = (uint8_t)(sceneIdHex >> 8);
    data[17] = (uint8_t)(sceneIdHex);
    data[18] = (uint8_t)(2 << 6);
    data[19] = (uint8_t)voiceCode;
    data[20] = (uint8_t)(voiceCode >> 8);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 21);
    return true;
}

bool GW_DeleteSceneConditionIR(int gwIndex, const char* deviceAddr, const char* sceneId) {
    ASSERT(deviceAddr);
    ASSERT(sceneId);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0xff,0xff,   0xE4,0x11,0x02,0x00,0x00,    0x06,0x00,0x00,0x00};
    long int dpAddrHex = strtol(deviceAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[16] = (uint8_t)(sceneIdHex >> 8);
    data[17] = (uint8_t)(sceneIdHex);
    data[18] = (uint8_t)(1 << 6);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 19);
    return true;
}

bool GW_AddSceneConditionPresenceSensor(const char* deviceAddr, const char* sceneId, uint8_t andOr, uint8_t lighnessCond, uint16_t lightness, uint8_t activeMask, uint8_t sensorTypeMask) {
    ASSERT(deviceAddr);
    ASSERT(sceneId);
    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02,0x00,0x00,     0x50,0x00,          0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    long int deviceAddrHex = strtol(deviceAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[8] = (uint8_t)(deviceAddrHex >> 8);
    data[9] = (uint8_t)(deviceAddrHex);
    uint8_t* payload = &data[15];
    payload[2] = (uint8_t)(sceneIdHex >> 8);
    payload[3] = (uint8_t)(sceneIdHex);
    payload[4] = andOr;
    payload[7] = (lighnessCond << 6) & 0b11000000;
    if (lighnessCond > 0) {
        payload[7] |= (uint8_t)(lightness >> 10);
        payload[6] = (uint8_t)lightness;
    }
    payload[5] = (activeMask << 4) & 0b11110000;
    payload[5] |= (sensorTypeMask >> 4);

    UartSendingFrame* frame = sendFrameToGwIndex(0, deviceAddrHex, data, 25);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}


bool GW_GetScenes(int gwIndex, const char *deviceAddr) {
    ASSERT(deviceAddr);
    long int addrHex = strtol(deviceAddr, NULL, 16);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0x82,0x44};
    data[8] = (addrHex >> 8) & 0x00FF;
    data[9] = (addrHex & 0x00FF);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, data, 12);
    return true;
}

bool GW_GetGroups(int gwIndex, const char *deviceAddr, const char *dpAddr) {
    ASSERT(deviceAddr);
    long int addrHex = strtol(deviceAddr, NULL, 16);
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    uint8_t data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff, 0x80,0x29, 0x00,0x00,0x00,0x01};
    data[8] = (addrHex >> 8) & 0x00FF;
    data[9] = (addrHex & 0x00FF);
    data[12] = (dpAddrHex >> 8) & 0x00FF;
    data[13] = (dpAddrHex & 0x00FF);
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, addrHex, data, 14);
    return true;
}

bool GW_ControlNewCurtain(int gwIndex, const char* dpAddr, uint8_t openClose, uint8_t position) {
    ASSERT(dpAddr);

    uint8_t data[] = {0xe8, 0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0x82,0x02, 0x00, 0x00, 0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    data[9] = dpAddrHex & (0xFF);
    data[8] = (dpAddrHex >> 8) & 0xFF;
    data[12] = openClose;
    data[14] = position;

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 15);
    frame->priority = 0;
    return true;
}

bool GW_SetSceneConditionNewCurtain(int gwIndex, const char* dpAddr, const char* sceneId, uint8_t operator, uint8_t position) {
    ASSERT(dpAddr); ASSERT(sceneId);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0xE0,0x11,0x02,0x00,0x00,     0x50,0x00,          0x00,0x00,  0x00,0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);

    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[17] = (uint8_t)(sceneIdHex >> 8);
    data[18] = (uint8_t)(sceneIdHex);
    data[19] = operator;
    data[20] = position;

    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 21);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}

bool GW_SetSceneActionNewCurtain(int gwIndex, const char* dpAddr, const char* sceneId, uint8_t operator, uint8_t position) {
    ASSERT(sceneId); ASSERT(dpAddr);

    uint8_t  data[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0xff,0xff,      0x82,0x46,  0x00,0x00, 0x00,0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    long int sceneIdHex = strtol(sceneId, NULL, 16);
    data[8] = (uint8_t)(dpAddrHex >> 8);
    data[9] = (uint8_t)(dpAddrHex);
    data[12] = (uint8_t)(sceneIdHex >> 8);
    data[13] = (uint8_t)(sceneIdHex);
    data[14] = operator;
    data[15] = position;
    UartSendingFrame* frame = sendFrameToGwIndex(gwIndex, dpAddrHex, data, 16);
    addTimeoutToSendingFrame(GWCFG_TIMEOUT_SCENEGROUP);
    return true;
}
#ifndef ble_process_h
#define ble_process_h


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
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

#include "core_process_t.h"
#include "parson.h"
#include "uart.h"
#include "ble_common.h"
#include "define.h"
#include "cJSON.h"

typedef struct  
{
    uint16_t frameSize;
    uint16_t flag;
    uint16_t sendAddr;
    uint16_t recvAddr;
    uint32_t opcode;
    uint8_t param[50];
    uint8_t paramSize;

    // Fields for online/offline report frame
    uint8_t onlineState;    // online/offline state of first device
    uint8_t onlineState2;   // online/offline state of second device (if exist in the frame)
    uint16_t sendAddr2;     // address of second sender device (if exist in the frame)
} ble_rsp_frame_t;

typedef struct
{
    double Sender ;
    const char *appkey;
    const char *ivIndex;
    const char *netkeyIndex;
    const char *netkey;
    const char *appkeyIndex;
    const char *deviceKey1;
    const char *deviceKey2;
    const char *address1;
    const char *address2;
}provison_inf;

struct state_element
{
    char address_element[5];
    double dpValue;
    uint8_t causeType;
    char causeId[10];
};

typedef struct
{
    char type_reponse;
    char* deviceID;
    char* dpID;
    char* value;
}InfoDataUpdateDevice;

void addRespTypeToSendingFrame(int reqType, const char* itemId);
void addPriorityToSendingFrame(int priority);
void addTimeoutToSendingFrame(uint16_t timeout);
void BLE_SetDeviceResp(int respType, uint16_t deviceAddr, int status, bool printLog);
void BLE_SendUartFrameLoop();

// Send request to get the device ON/OFF state
bool GW_GetDeviceOnOffState(const char* dpAddr);
bool GW_HgSwitchOnOff(const char* dpAddr, uint8_t dpValue);
bool GW_HgSwitchOnOff_NoResp(const char* dpAddr, uint8_t dpValue);
bool ble_dimLedSwitch_HOMEGY(const char *address_device, int lightness);

bool GW_CtrlLightOnOff(const char *deviceAddr, uint8_t onoff);
bool GW_SetLightness(const char *deviceAddr, int lightness);
bool GW_SetLightColor(const char *deviceAddr, int color);
bool GW_SetLightnessTemperature(const char *address_element, int lightness, int colorTemperature);
bool GW_SetLightHSL(const char *address_element, const char *HSL);
bool GW_SetRGBLightBlinkMode(const char *dpAddr, int blinkMode);

bool GW_AddGroupLight(int gwIndex, const char *address_group, const char *address_device, const char *address_element);
bool GW_AddGroupSwitch(int gwIndex, const char *address_group, const char *address_device, const char *address_element);
bool GW_DeleteGroup(int gwIndex, const char *address_group, const char *address_device, const char *address_element);
bool ble_logDeivce(const char *address_element, int state);
bool ble_logTouch(const char *address_element, uint8_t dpId, int state);
bool GW_SetSceneActionForSwitch(const char* sceneId, const char* deviceAddr, uint8_t dpCount, uint32_t param);
bool GW_SetSceneActionForLightCCT(const char* address_device,const char* sceneID);
bool GW_SetSceneActionForLightRGB(const char* address_device,const char* sceneID, uint8_t blinkMode);
bool GW_SetSceneCondition(const char* address_device,const char* sceneID, uint8_t enableOrDisable, uint8_t dpValue);
bool ble_delSceneLocalToDevice(const char* address_device,const char* sceneID);
bool ble_setTimeForSensorPIR(const char* address_device,const char* time);
bool ble_callSceneLocalToHC(const char* address_device, const char* sceneID);
void BLE_PrintFrame(char* str, ble_rsp_frame_t* frame);
bool ble_getInfoProvison(provison_inf *PRV, JSON* packet);
bool GW_ConfigGateway(int gwIndex, provison_inf *PRV);
int  get_count_element_of_DV(const char* pid_);
void get_string_add_DV_write_GW(char **result,const char* address_device,const char* element_count,const char* deviceID);
int  set_inf_DV_for_GW(int gwIndex, const char* address_device,const char* pid,const char* deviceKey);
int  GW_SplitFrame(ble_rsp_frame_t resultFrames[MAX_FRAME_COUNT], uint8_t* originPackage, size_t size);

int  check_form_recived_from_RX(struct state_element *temp, ble_rsp_frame_t* frame);
bool GW_DeleteDevice(const char* deviceAddr);

bool GW_CtrlGroupLightOnOff(const char *groupAddr, uint8_t onoff);
bool GW_CtrlGroupLightCT(const char *dpAddr, int lightness, int colorTemperature);
bool GW_SetTTL(int gwIndex, const char *deviceAddr, uint8_t ttl);
bool GW_GetScenes(const char *deviceAddr);
bool GW_GetGroups(const char *deviceAddr);

/*
 * Control IR
 * Models:
 */
bool GW_ControlIRCmd(const char* command);
bool GW_ControlIR(const char* deviceAddr, int commandType, int brandId, int remoteId, int temp, int mode, int fan, int swing);
bool GW_AddSceneActionIR(const char* deviceAddr, const char* sceneId, uint8_t commandType, uint8_t brandId, uint8_t remoteId, uint8_t temp, uint8_t mode, uint8_t fan, uint8_t swing);
bool GW_DeleteSceneActionIR(const char* deviceAddr, const char* sceneId, uint8_t commandType, uint8_t brandId, uint8_t remoteId);
bool GW_AddSceneConditionIR(const char* deviceAddr, const char* sceneId, uint16_t voiceCode);
bool GW_DeleteSceneConditionIR(const char* deviceAddr, const char* sceneId);

#endif
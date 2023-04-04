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
#include "error.h"
#include "define.h"
#include "cJSON.h"

typedef struct  
{
    uint16_t frameSize;
    uint16_t flag;
    uint16_t sendAddr;
    uint16_t recvAddr;
    uint16_t opcode;
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
    const char *deviceKey;
    const char *address;
}provison_inf;

typedef struct  
{
    int type;
    int loops;
    int delay;
    int sender;
    char *senderid;
    char *object;
    int provider;
    int pageIndex;
    JSON_Object *JS_object;
    JSON_Value *JS_value;
}Pre_parse;

struct state_element
{
    char* address_element;
    char* value;
    double dpValue;
    uint8_t causeType;
    char causeId[10];
};

typedef struct  
{
    char *address;
    char *state;
}InfoControlDeviceBLE;

typedef struct  
{
    char *address;
    int lightness;
    int colorTemperature;
}InfoControlCLT_BLE;

typedef struct  
{
    char *address;
    char *valueHSL;
}InfoControlHSL_BLE;

typedef struct  
{
    char *address;
    char *modeBlinkRgb;
}InfoControlBlinkRGB_BLE;


typedef struct
{
    char type_reponse;
    char* deviceID;
    char* dpID;
    char* value;
}InfoDataUpdateDevice;

// Send request to get the device ON/OFF state
bool BLE_GetDeviceOnOffState(const char* dpAddr);

/*
 * On/Off control
 * Models: HG_BLE_SWITCH_1, HG_BLE_SWITCH_2, HG_BLE_SWITCH_3, HG_BLE_SWITCH_4
 */
int ble_controlOnOFF_SW(const char* dpAddr, uint8_t dpValue);

/*
 * On/Off control
 * Models: HG_BLE_LIGHT_WHITE (20), RD_BLE_LIGHT_WHITE (20), RD_BLE_LIGHT_WHITE_TEST (20),
 *         RD_BLE_LIGHT_RGB (20), HG_BLE_CURTAIN_2_LAYER, HG_BLE_ROLLING_DOOR, HG_BLE_CURTAIN_NORMAL,
 *         LIGHT_GROUP (20)
 */
int ble_controlOnOFF(const char *address_element, const char *state);

/*
 * Control lightness and color temperature
 * Models: HG_BLE_LIGHT_WHITE (22,23), RD_BLE_LIGHT_WHITE (22,23), RD_BLE_LIGHT_WHITE_TEST (22,23),
 *         RD_BLE_LIGHT_RGB (22,23),
 *         LIGHT_GROUP (22,23)
 */
int ble_controlCTL(const char *address_element, int lightness, int colorTemperature);

/*
 * Control HSL color of RGB light
 * Models: RD_BLE_LIGHT_RGB (24), LIGHT_GROUP (24)
 */
int ble_controlHSL(const char *address_element, const char *HSL);

/*
 * Control blink mode of RGB light
 * Models: RD_BLE_LIGHT_RGB (21), LIGHT_GROUP (21)
 */
int ble_controlModeBlinkRGB(const char *address_element, const char *modeBlinkRgb);

/*
 * Dim LED of switch
 */
bool ble_dimLedSwitch_HOMEGY(const char *address_device, int lightness);

/*
 * Add/Delete a CCT light to/from a group
 */
bool ble_addDeviceToGroupLightCCT_HOMEGY(const char *address_group, const char *address_device, const char *address_element);
bool ble_deleteDeviceToGroupLightCCT_HOMEGY(const char *address_group, const char *address_device, const char *address_element);

/*
 * Add a switch to a group
 */
bool ble_addDeviceToGroupLink(const char *address_group, const char *address_device, const char *address_element);

/*
 * Lock agency
 */
int ble_logDeivce(const char *address_element, int state);

/*
 * Lock kids
 */
int ble_logTouch(const char *address_element, uint8_t dpId, int state);

/*
 * Add/delete local scene
 */
bool ble_setSceneLocalToDeviceSwitch(const char* sceneId, const char* deviceAddr, uint8_t dpCount, uint32_t param);
bool ble_setSceneLocalToDeviceLightCCT_HOMEGY(const char* address_device,const char* sceneID);
bool ble_setSceneLocalToDeviceLight_RANGDONG(const char* address_device,const char* sceneID,const char* modeBlinkRgb );
bool ble_callSceneLocalToDevice(const char* address_device,const char* sceneID, const char* enableOrDisable, uint8_t dpValue);
bool ble_delSceneLocalToDevice(const char* address_device,const char* sceneID);
bool ble_setTimeForSensorPIR(const char* address_device,const char* time);

/*
 * Run a local scene
 */
bool ble_callSceneLocalToHC(const char* address_device, const char* sceneID);


void BLE_PrintFrame(char* str, ble_rsp_frame_t* frame);

/*******************************************************************
* name:
* function:
* in param:
*               
*               
*              
*               
*               
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
bool ble_getInfoProvison(provison_inf *PRV, JSON* packet);



/*******************************************************************
* name:         provison_GW
* function:     provision for GateWay
* in param:     fd: the file descriptor of the UART
                PRV: the struct data for provision
* out param:    NO
*******************************************************************/
bool ble_bindGateWay(provison_inf *PRV);
bool ble_saveInforDeviceForGatewayRangDong(const char *address_t,const char *address_gateway);
bool ble_saveInforDeviceForGatewayHomegy(const char *address_element_0,const char *address_gateway);
bool creatFormReponseBLE(char **ResultTemplate);
bool insertObjectReponseBLE(char **ResultTemplate, char *address,char *Id,long long TimeCreat,char *State);
bool removeObjectReponseBLE(char **ResultTemplate,char *address);
long long int  getTimeCreatReponseBLE(char *ResultTemplate,char *address);
bool  getStateReponseBLE(char **state,char *ResultTemplate,char *address);
bool  getIdReponseBLE(char **Id,char *ResultTemplate,char *address);
int get_count_element_of_DV(const char* pid_);
void get_string_add_DV_write_GW(char **result,const char* address_device,const char* element_count,const char* deviceID);
int set_inf_DV_for_GW(const char* address_device,const char* pid,const char* deviceID);

void ble_getStringControlOnOff_SW(char **result,const char* strAddress,const char* strState);

void ble_getStringControlOnOff(char **result,const char* strAddress,const char* strState);


int ble_controlOnOFF_NODELAY(const char *address_element,const char *state);




long long GW_getSizeMessage(char *size_reverse_2_byte_hex);

/*
 * Split a long BLE frame that received from UART into multiple single packages
 * @param:
 *      originPackage: the package that received from BLE IC via UART
 *      size: size of originPackage
 *      resultFrames: array of ble_rsp_frame_t to store the splited single frame
 * @return: the number of single frame in the original package
 */
int GW_SplitFrame(ble_rsp_frame_t resultFrames[MAX_FRAME_COUNT], uint8_t* originPackage, size_t size);

int check_form_recived_from_RX(struct state_element *temp, ble_rsp_frame_t* frame);

void getStringResetDeviveSofware(char **result,const char* addressDevice);
bool setResetDeviceSofware(const char *addressDevice);

bool IsHasDeviceIntoDabase(const JSON_Value *object_devices,const char* deviceID);

bool getPidDevice(char **result,const JSON_Value *object_devices,const char* deviceID);
bool getAndressFromDeviceAndDpid(char **result,const JSON_Value *object_devices,const char* deviceID,const char* dpID);
int getNumberElementNeedControl(JSON_Value *object);

bool getInfoControlOnOffBLE(InfoControlDeviceBLE  *InfoControlDeviceBLE_t,const JSON_Value *object_devices,const char* deviceID,const char* dpID);
bool getInfoControlCTL_BLE(InfoControlCLT_BLE  *InfoControlCLT_BLE_t,const JSON_Value *object_devices,const char *object_string);
bool getInfoControlHSL_BLE(InfoControlHSL_BLE  *InfoControlHSL_BLE_t,const JSON_Value *object_devices,const char *object_string);
bool getInfoControlModeBlinkRGB_BLE(InfoControlBlinkRGB_BLE  *InfoControlBlinkRGB_BLE_t,const JSON_Value *object_devices,const char *object_string);

char *get_dpid(const char *code);


#endif
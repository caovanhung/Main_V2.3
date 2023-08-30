#ifndef CORE_API_H
#define CORE_API_H

#include <stdarg.h>

extern JSON *g_checkRespList;

/************************************ COMMON APIS **************************************/
void CoreInit();
void sendPacketToBle(int gwIndex, int reqType, JSON* packet);
bool CompareDeviceById(JSON* device1, JSON* device2);
JSON* parseGroupLinkDevices(const char* devices);
// Add device that need to check response to response list
JSON* addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr);
// Check and get the JSON_Object of request that is in response list
JSON* requestIsInRespList(int reqType, const char* itemId);
// Update device status in response list
void updateDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr, int status);
// Get response status of a device according to a command
int getDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr);
/***************************************************************************************/

/********************* APIS TO COMMUNICATE WITH AWS SERVICE ****************************/
void Aws_DeleteDevice(const char* deviceId, int pageIndex);
void Aws_SaveDeviceState(const char* deviceId, int state, int pageIndex);
void Aws_SaveDpValue(const char* deviceId, int dpId, int value, int pageIndex);
void Aws_SaveDpValueString(const char* deviceId, int dpId, const char* value, int pageIndex);
void Aws_DeleteGroup(const char* groupAddr);
void Aws_UpdateGroupValue(const char* groupAddr, int dpId, int dpValue);
void Aws_SaveGroupDevices(const char* groupAddr);
void Aws_DeleteScene(const char* sceneId);
void Aws_EnableScene(const char* sceneId, bool state);
void Aws_SaveScene(const char* sceneId);
void Aws_UpdateLockKids(const char* deviceId, int dpId, int lockValue);
void Aws_ResponseLearningIR(const char* deviceId, const char* respCmd);
/***************************************************************************************/

/********************* APIS TO COMMUNICATE WITH BLE SERVICE ****************************/
void Ble_ControlDeviceStringDp(const char* deviceId, uint8_t dpId, char* dpValue, const char* causeId);
void Ble_ControlDeviceArray(const char* deviceId, uint8_t* dpIds, double* dpValues, int dpCount, const char* causeId);
void Ble_ControlDeviceJSON(const char* deviceId, JSON* dictDPs, const char* causeId);
void Ble_ControlGroupJSON(const char* groupAddr, JSON* dictDPs, const char* causeId);
void Ble_ControlGroupStringDp(const char* groupAddr, uint8_t dpId, char* dpValue, const char* causeId);
void Ble_ControlGroupArray(const char* groupAddr, uint8_t* dpIds, double* dpValues, int dpCount, const char* causeId);
void Ble_SetTTL(int gwIndex, const char* deviceAddr, uint8_t ttl);
void Ble_AddExtraDpsToIrDevices(const char* deviceId, JSON* dictDPs);
/***************************************************************************************/

/********************* APIS TO COMMUNICATE WITH WIFI SERVICE ***************************/
void Wifi_ControlDevice(const char* deviceId, const char* code);
void Wifi_ControlGroup(const char* groupId, const char* code);
/***************************************************************************************/

#endif  // CORE_API_H
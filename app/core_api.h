#ifndef CORE_API_H
#define CORE_API_H

#include <stdarg.h>

extern JSON *g_checkRespList;

void CoreInit();

JSON* parseGroupLinkDevices(const char* devices);
JSON* parseGroupNormalDevices(const char* devices);
// Add device that need to check response to response list
JSON* addDeviceToRespList(int reqType, const char* itemId, const char* deviceAddr);
// Check and get the JSON_Object of request that is in response list
JSON* requestIsInRespList(int reqType, const char* itemId);
// Update device status in response list
void updateDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr, int status);
// Get response status of a device according to a command
int getDeviceRespStatus(int reqType, const char* itemId, const char* deviceAddr);

void Ble_ControlDeviceArray(const char* deviceId, uint8_t* dpIds, double* dpValues, int dpCount, const char* causeId);
void Ble_ControlDeviceJSON(const char* deviceId, JSON* dictDPs, const char* causeId);
void Ble_ControlStringDp(const char* deviceId, int dpId, const char* dpValue);
void Ble_ControlGroup(const char* groupAddr, JSON* dictDPs);
void Ble_SetTTL(int gwIndex, const char* deviceAddr, uint8_t ttl);
void Ble_AddExtraDpsToIrDevices(const char* deviceId, JSON* dictDPs);

void Wifi_ControlDevice(const char* deviceId, const char* code);

#endif  // CORE_API_H
#ifndef CORE_API_H
#define CORE_API_H

#include <stdarg.h>

// #define Ble_ControlDevice(deviceId, x, ...)   _Generic((x), uint8_t*: Ble_ControlDeviceArray, JSON*: Ble_ControlDeviceJSON)(deviceId, x, __VA_ARGS__)


JSON* parseGroupLinkDevices(const char* devices);
JSON* parseGroupNormalDevices(const char* devices);
void Ble_ControlDeviceArray(const char* deviceId, uint8_t* dpIds, double* dpValues, int dpCount);
void Ble_ControlDeviceJSON(const char* deviceId, JSON* dictDPs);
void Ble_ControlStringDp(const char* deviceId, int dpId, const char* dpValue);
void Ble_ControlGroup(const char* groupAddr, JSON* dictDPs);
void Ble_SetTTL(int gwIndex, const char* deviceAddr, uint8_t ttl);
void Ble_AddExtraDpsToIrDevices(const char* deviceId, JSON* dictDPs);

void Wifi_ControlDevice(const char* deviceId, const char* code);

#endif  // CORE_API_H
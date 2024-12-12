/////////////////////////////////LOG/////////////////////////////////////////
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "CoreData"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif
#define MQTT_LIB    "core-mqtt@" MQTT_LIBRARY_VERSION

#define MQTT_MOSQUITTO_CIENT_ID             "Command"


#define ASSERT(...)  if (!(__VA_ARGS__)) { printf("ERROR ASSERT: %s:%d\n", __FILE__, __LINE__); return; }
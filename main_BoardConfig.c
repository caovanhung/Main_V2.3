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
#include <netdb.h> /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <mosquitto.h>
#include "database_common.h"
#include "define.h"
#include "logging_stack.h"
#include "time_t.h"
#include "queue.h"
#include "parson.h"
#include "database.h"
#include "mosquitto.h"
#include "helper.h"
#include "messages.h"
#include "cJSON.h"
#include "gpio.h"

#define USER_BUTTON         (PF + 0)
#define USER_LED            (PD + 23)
#define LED_ON              g_ledDelayTime = 0; pinWrite(USER_LED, 1)
#define LED_OFF             g_ledDelayTime = 0; pinWrite(USER_LED, 0)
#define LED_FAST_FLASH      g_ledDelayTime = 300
#define LED_SLOW_FLASH      g_ledDelayTime = 700

typedef enum {
    MODE_NORMAL,
    MODE_WIFI_CONFIG
} running_mode_t;

const char* SERVICE_NAME = SERVICE_CFG;

struct mosquitto * mosq;
static bool g_mosqIsConnected = false;
char* g_dbgFileName;
int g_dbgLineNumber;
static FILE* g_createApFile;
static uint8_t g_mode = MODE_NORMAL;
static int g_ledDelayTime = 0;
static char g_wifiSSID[100] = {0};
static char g_wifiPASS[100] = {0};
static bool g_wifiConnectDone = false;
static bool g_wifiIsConnected = false;

bool CheckWifiConnection(const char* ssid);

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if(rc) {
        LogError((get_localtime_now()),("Error with result code: %d\n", rc));
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, "MANAGER_SERVICES/Setting/Wifi", 0);
    mosquitto_subscribe(mosq, NULL, "CFG/#", 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    logInfo("Received from topic: %s", msg->topic);
    logInfo("Payload: %s", msg->payload);
    JSON* recvMsg = JSON_Parse(msg->payload);
    if (StringCompare(msg->topic, "MANAGER_SERVICES/Setting/Wifi")) {
        JSON* wifiInfo = JSON_GetObject(recvMsg, "wifi_info");
        JSON* homeInfo = JSON_GetObject(recvMsg, "home_info");
        char* ssid = JSON_GetText(wifiInfo, "ssid");
        char* pass = JSON_GetText(wifiInfo, "pass");
        if (ssid) {
            StringCopy(g_wifiSSID, ssid);
        }
        if (pass) {
            StringCopy(g_wifiPASS, pass);
        }
        logInfo("SSID: %s, PASS: %s", ssid, pass);
    } else {
        JSON* payload = JSON_GetText(recvMsg, "Payload");
        if (StringCompare(payload, "AWS_CONNECTED")) {
            logInfo("AWS_CONNECTED");
            LED_ON;
        } else if (StringCompare(payload, "AWS_DISCONNECTED")) {
            logInfo("AWS_DISCONNECTED");
            LED_OFF;
        }
    }
    JSON_Delete(recvMsg);
}

void Mosq_Init() {
    int rc = 0;
    mosquitto_lib_init();
    mosq = mosquitto_new(MQTT_MOSQUITTO_CIENT_ID, true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
        return;
    }
    logInfo("Mosq_Init() done");
    g_mosqIsConnected = true;
}

void Mosq_ProcessLoop() {
    if (g_mosqIsConnected) {
        int rc = mosquitto_loop(mosq, 5, 1);
        if (rc != 0) {
            logError("mosquitto_loop error: %d.", rc);
            g_mosqIsConnected = false;
        }
    } else {
        mosquitto_destroy(mosq);
        Mosq_Init();
    }
}

void LedProcessLoop() {
    static long long oldTick = 0;

    if (g_ledDelayTime > 0 && timeInMilliseconds() - oldTick > g_ledDelayTime) {
        oldTick = timeInMilliseconds();

        pinToggle(USER_LED);
    }
}

void ConnectWifiLoop() {
    static uint8_t state = 0;
    static long long tick = 0;
    static uint8_t count = 0;
    static uint8_t failedCount = 0, reconnectCount = 0;

    switch (state) {
    case 0:
        if (g_wifiSSID[0] != 0) {
            logInfo("Rescan available networks: nmcli d wifi rescan");
            char str[400];
            sprintf(str, "nmcli c delete %s", g_wifiSSID);
            system(str);
            system("nmcli d wifi rescan");
            tick = timeInMilliseconds();
            failedCount = 0;
            state = 1;
        }
        break;
    case 1:
        // Wait 500ms for re-scanning
        if (timeInMilliseconds() - tick > 500) {
            char str[400];
            tick = timeInMilliseconds();
            logInfo("Run command: nmcli d wifi connect \"%s\" password \"%s\" ifname wlan0", g_wifiSSID, g_wifiPASS);
            sprintf(str, "nmcli d wifi connect \"%s\" password \"%s\" ifname wlan0", g_wifiSSID, g_wifiPASS);
            LED_OFF;
            system(str);
            state = 2;
        }
        break;
    case 2:
        // Check connection status every 1 second
        if (timeInMilliseconds() - tick > 1000) {
            tick = timeInMilliseconds();
            if (CheckWifiConnection(g_wifiSSID)) {
                // Connected successfully
                g_wifiSSID[0] = 0;
                reconnectCount = 0;
                state = 0;
                g_wifiConnectDone = TRUE;
            } else {
                failedCount++;
                LED_ON;
                // Reconnect after 5 seconds
                if (failedCount >= 1) {
                    reconnectCount++;
                    if (reconnectCount > 1) {
                        // Still failed after 2 reconnect times, ignore this connection
                        logInfo("Cannot connect after 10 seconds");
                        g_wifiConnectDone = TRUE;
                        reconnectCount = 0;
                        g_wifiSSID[0] = 0;
                        state = 0;
                    } else {
                        // Go to first step to reconnect the wifi
                        logInfo("Reconnect after 5 seconds");
                        state = 0;
                    }
                }
            }
        }
        break;
    }
}

void MainLoop() {
    static uint8_t state = 0;
    static long long oldTick = 0;
    static uint8_t count = 0;

    if (timeInMilliseconds() - oldTick > 100) {
        oldTick = timeInMilliseconds();

        switch (state) {
        case 0:
            // Check button long press
            if (pinRead(USER_BUTTON) == 0) {
                count++;
                if (count >= 40) {
                    count = 0;
                    state = 1;
                    // Create wifi hotspot
                    LED_SLOW_FLASH;
                    g_wifiConnectDone = false;
                    g_createApFile = popen("create_ap -n wlan0 hc2023", "r");
                }
            } else {
                count = 0;
            }
            break;
        case 1:
            // Wait for button to be released
            if (pinRead(USER_BUTTON) == 1) {
                state = 2;
            }
            break;
        case 2:
            // Wait for button press
            if (pinRead(USER_BUTTON) == 0 || g_wifiConnectDone) {
                count = 0;
                if (g_wifiIsConnected) {
                    LED_ON;
                } else {
                    LED_OFF;
                }
                logInfo("Done configuring");
                system("pkill -15 create_ap");
                pclose(g_createApFile);
                state = 0;
            }
            break;
        }
    }
}

int main(int argc, char ** argv) {
    pinMode(USER_LED, OUTPUT);
    pinMode(USER_BUTTON, INPUT);
    LED_ON;
    Mosq_Init();
    sleep(1);
    LED_OFF;

    while (1) {
        Mosq_ProcessLoop();
        MainLoop();
        ConnectWifiLoop();
        LedProcessLoop();
        usleep(1000);
    }
    return 0;
}


bool CheckWifiConnection(const char* ssid) {
    bool ret = false;
    char path[1035];
    logInfo("Checking wifi connection: %s", ssid);
    FILE* fp = popen("nmcli c show --active", "r");
    while (fgets(path, sizeof(path), fp) != NULL) {
        printf("%s", path);
        if (strstr(path, ssid)) {
            ret = true;
            break;
        }
    }
    if (ret) {
        g_wifiIsConnected = true;
        logInfo("CONNECTED");
    } else {
        logInfo("NOT CONNECTED");
    }
    pclose(fp);
    return ret;
}
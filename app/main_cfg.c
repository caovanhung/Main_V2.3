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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <assert.h>
#include <mosquitto.h>
#include "define.h"
#include "time_t.h"
#include "queue.h"
#include "parson.h"
#include "database.h"
#include "aws_mosquitto.h"
#include "helper.h"
#include "messages.h"
#include "cJSON.h"
#include "gpio.h"

#define LOG_QUEUE_SIZE          1000

#define TYPE_HC_FEEDBACK        1
#define TYPE_CONFIG_WIFI        99
#define TYPE_CONFIG_WIFI_GW     100

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

typedef enum {
    WIFI_RESCAN,
    WIFI_CHECK_EXIST,
    WIFI_CONNECT,
    WIFI_CHECK_STATUS
} WifiConnectingState;

#define SHM_SIZE            1024
#define SHM_PAYLOAD_OFFSET  10
char* g_sharedMemory;

const char* SERVICE_NAME = SERVICE_CFG;
uint8_t     SERVICE_ID   = SERVICE_ID_CFG;
bool g_printLog = true;

struct mosquitto * mosq;
static bool g_mosqIsConnected = false;
char* g_dbgFileName;
int g_dbgLineNumber;
static FILE* g_createApFile;
static uint8_t g_mode = MODE_NORMAL;
static int g_ledDelayTime = 0;
static char g_wifiSSID[100] = {0};
static char g_wifiPASS[100] = {0};
static char g_homeId[100] = {0};
static bool g_needConfigGw = false;
static char g_accountId[100] = {0};
static bool g_wifiConnectDone = false;
static bool g_wifiIsConnected = false;
static JSON* g_gatewayInfo;

static struct Queue* g_logQueues[SERVICES_NUMBER];
static char* g_serviceName[SERVICES_NUMBER] = {"aws", "core", "ble", "tuya", "cfg"};

static long long int g_connectedTime = 0;

bool CheckWifiConnection(const char* ssid);
bool CheckSSIDExist(const char* ssid);

void InitSharedMemory() {
    key_t key = ftok("/tmp", 'A');  // Same key as used in the reader
    int shmid;

    // Access the shared memory segment
    if ((shmid = shmget(key, SHM_SIZE, IPC_CREAT | 0666)) == -1) {
        perror("shmget");
        fprintf(stderr, "shmget failed with error: %d\n", errno);
    }

    // Attach to the shared memory segment
    g_sharedMemory = shmat(shmid, NULL, 0);

    if (g_sharedMemory == (void *)-1) {
        logError("shmat");
    }
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if(rc) {
        logError("Error with result code: %d", rc);
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MQTT_LOCAL_REQS_TOPIC, 0);
    // mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_CONTROL_LOCAL, 0);
    mosquitto_subscribe(mosq, NULL, "CFG/#", 0);
    mosquitto_subscribe(mosq, NULL, "LOG_REPORT/#", 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    if (StringContains(msg->topic, "LOG_REPORT/")) {
        int fromServiceId = atoi(&msg->topic[11]);
        if (fromServiceId >= 0 && fromServiceId < SERVICES_NUMBER) {
            enqueue(g_logQueues[fromServiceId], (char*)msg->payload);
        }
        return;
    }

    JSON* recvMsg = JSON_Parse(msg->payload);
    if (StringCompare(msg->topic, MQTT_LOCAL_REQS_TOPIC)) {
        logInfo("Received from topic: %s", msg->topic);
        logInfo("Payload: %s", msg->payload);

        int type = JSON_GetNumber(recvMsg, "type");
        if (type == TYPE_CONFIG_WIFI || type == TYPE_CONFIG_WIFI_GW) {
            bool needToConfigWifi = false;
            if (JSON_HasKey(recvMsg, "wifi_info")) {
                JSON* wifiInfo = JSON_GetObject(recvMsg, "wifi_info");
                char* ssid = JSON_GetText(wifiInfo, "ssid");
                char* pass = JSON_GetText(wifiInfo, "pass");
                if (ssid) {
                    StringCopy(g_wifiSSID, ssid);
                }
                if (pass) {
                    StringCopy(g_wifiPASS, pass);
                }
                logInfo("SSID: %s, PASS: %s", ssid, pass);
                needToConfigWifi = true;
            }

            g_homeId[0] = 0;
            g_accountId[0] = 0;

            if (JSON_HasKey(recvMsg, "home_info")) {
                JSON* homeInfo = JSON_GetObject(recvMsg, "home_info");
                char* homeId = JSON_GetText(homeInfo, "home_id");
                char* accountId = JSON_GetText(homeInfo, "account_id");
                if (homeId) {
                    StringCopy(g_homeId, homeId);
                }
                if (accountId) {
                    StringCopy(g_accountId, accountId);
                }
                logInfo("HomeId: %s, AccountId: %s", g_homeId, g_accountId);
            }
            if (type == TYPE_CONFIG_WIFI_GW && JSON_HasKey(recvMsg, "gateWay")) {
                g_gatewayInfo = JSON_Clone(JSON_GetObject(recvMsg, "gateWay"));
                g_needConfigGw = true;
                if (needToConfigWifi == false) {
                    FILE* f = fopen("app.json", "w");
                    int isMaster = JSON_GetNumber(g_gatewayInfo, "isMaster");
                    char* hcAddr = JSON_GetText(g_gatewayInfo, "gateway1");
                    fprintf(f, "{\"thingId\":\"%s\",\"homeId\":\"%s\",\"isMaster\":%d,\"hcAddr\":\"%s\"}", g_accountId, g_homeId, isMaster, hcAddr);
                    fclose(f);
                    // Send message to BLE service to configure gateway
                    // sendPacketTo("BLE_LOCAL", TYPE_ADD_GW, g_gatewayInfo);
                    // sendPacketTo("CORE_LOCAL", TYPE_RESET_DATABASE, g_gatewayInfo);
                    if (g_sharedMemory > 0) {
                        g_sharedMemory[0] = TYPE_ADD_GW;
                        StringCopy(g_sharedMemory + SHM_PAYLOAD_OFFSET, cJSON_PrintUnformatted(g_gatewayInfo));
                    }
                }
            } else {
                g_gatewayInfo = NULL;
                g_needConfigGw = false;
            }
        }
    } else {
        JSON* payload = JSON_GetText(recvMsg, "Payload");
        if (StringCompare(payload, "AWS_CONNECTED")) {
            // logInfo("AWS_CONNECTED");
            LED_ON;
            g_connectedTime = 0;
        } else if (StringCompare(payload, "AWS_DISCONNECTED")) {
            // logInfo("AWS_DISCONNECTED");
            LED_OFF;
            g_connectedTime = timeInMilliseconds();
        } else if (StringCompare(payload, "LED_ON")) {
            LED_ON;
        } else if (StringCompare(payload, "LED_OFF")) {
            LED_OFF;
        } else if (StringCompare(payload, "LED_FAST_FLASH")) {
            LED_FAST_FLASH;
        } else if (StringCompare(payload, "LED_SLOW_FLASH")) {
            LED_SLOW_FLASH;
        } else if (StringCompare(payload, "LED_FLASH_1_TIME")) {
            g_ledDelayTime = 1; pinWrite(USER_LED, 0);  // Turn off LED
        }
    }
    JSON_Delete(recvMsg);
}

void Mosq_Init() {
    int rc = 0;
    mosquitto_lib_init();
    mosq = mosquitto_new("HG_CFG", true, NULL);
    rc = mosquitto_username_pw_set(mosq, "homegyinternal", "sgSk@ui41DA09#Lab%1");
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        logInfo("Client could not connect to broker! Error Code: %d", rc);
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
        if (g_ledDelayTime >= 10) {
            pinToggle(USER_LED);
        }
        if (g_ledDelayTime < 10) {
            g_ledDelayTime++;
        } else if (g_ledDelayTime == 10) {
            g_ledDelayTime = 0;
        }
    }
}

void ConnectWifiLoop() {
    static uint8_t state = 0;
    static long long int tick = 0;
    static uint8_t count = 0;
    static uint8_t failedCount = 0, reconnectCount = 0;

    switch (state) {
    case WIFI_RESCAN:
        if (g_wifiSSID[0] != 0) {
            char* message = "{\"step\":1, \"message\":\"Đang cấu hình wifi, vui lòng đợi\"}";
            mosquitto_publish(mosq, NULL, MQTT_LOCAL_RESP_TOPIC, strlen(message), message, 0, false);
            PlayAudio("wifi_connecting");
            // logInfo("Rescan available networks: nmcli c delete %s", g_wifiSSID);

            // char str[400];
            // sprintf(str, "nmcli c delete %s", g_wifiSSID);
            // system(str);
            logInfo("Rescan available networks: nmcli d wifi rescan");
            system("nmcli d wifi rescan");
            tick = timeInMilliseconds();
            // failedCount = 0;
            state = WIFI_CHECK_EXIST;
        }
        break;
    case WIFI_CHECK_EXIST:
        // Wait 3000ms for re-scanning
        if (timeInMilliseconds() - tick > 3000) {
            tick = timeInMilliseconds();
            if (CheckSSIDExist(g_wifiSSID)) {
                failedCount = 0;
                state = WIFI_CONNECT;
            } else {
                state = 0;
                if (failedCount == 0) {
                    failedCount = 1;
                } else {
                    // Cannot found the ssid, exit the connecting procedure
                    g_wifiSSID[0] = 0;
                    failedCount = 0;
                    g_wifiConnectDone = TRUE;
                    char* message = "{\"step\":5, \"message\":\"Không tìm thấy mạng wifi yêu cầu\"}";
                    mosquitto_publish(mosq, NULL, MQTT_LOCAL_RESP_TOPIC, strlen(message), message, 0, false);
                }
            }
        }
        break;
    case WIFI_CONNECT: {
        char str[400];
        logInfo("Run command: nmcli d wifi connect \"%s\" password \"%s\" ifname wlan0", g_wifiSSID, g_wifiPASS);
        sprintf(str, "nmcli d wifi connect \"%s\" password \"%s\" ifname wlan0", g_wifiSSID, g_wifiPASS);
        LED_OFF;
        system(str);
        state = WIFI_CHECK_STATUS;
        break;
    }
    case WIFI_CHECK_STATUS:
        // Check connection status every 1 second
        if (timeInMilliseconds() - tick > 1000) {
            tick = timeInMilliseconds();
            if (CheckWifiConnection(g_wifiSSID)) {
                // Connected successfully
                g_wifiSSID[0] = 0;
                reconnectCount = 0;
                state = 0;
                g_wifiConnectDone = TRUE;
                char* message = "{\"step\":2, \"message\":\"Cấu hình wifi thành công\"}";
                mosquitto_publish(mosq, NULL, MQTT_LOCAL_RESP_TOPIC, strlen(message), message, 0, false);
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
                    system("touch /home/szbaijie/hc_bin/cfg");
                    system("pkill -15 create_ap");
                    LED_SLOW_FLASH;
                    g_wifiConnectDone = false;
                    system("nmcli r wifi off");
                    // system("rfkill unblock wlan");
                    system("nmcli r wifi on");
                    // Generate a random 4-digit number
                    int random_number = rand() % 10000;
                    // Create a string with the fixed prefix and the random number
                    char cmd[100]; // "Homegy" + 4 digits + '\0'
                    sprintf(cmd, "create_ap -n wlan0 Homegy%04d", random_number);
                    g_createApFile = popen(cmd, "r");
                    PlayAudio("config_mode_active");
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
            // Wait for button press or finished configuration
            if (g_wifiConnectDone) {
                count = 0;
                LED_ON;
                if (g_wifiIsConnected) {
                    PlayAudio("wifi_config_success");
                    // Save homeId and accountId to app.json file
                    if (g_needConfigGw && g_homeId[0] != 0 && g_accountId[0] != 0) {
                        FILE* f = fopen("app.json", "w");
                        int isMaster = JSON_GetNumber(g_gatewayInfo, "isMaster");
                        char* hcAddr = JSON_GetText(g_gatewayInfo, "gateway1");
                        fprintf(f, "{\"thingId\":\"%s\",\"homeId\":\"%s\",\"isMaster\":%d,\"hcAddr\":\"%s\"}", g_accountId, g_homeId, isMaster, hcAddr);
                        fclose(f);
                        // Send message to BLE service to configure gateway
                        // sendPacketTo("BLE_LOCAL", TYPE_ADD_GW, g_gatewayInfo);
                        // sendPacketTo("CORE_LOCAL", TYPE_RESET_DATABASE, g_gatewayInfo);
                        if (g_sharedMemory > 0) {
                            g_sharedMemory[0] = TYPE_ADD_GW;
                            StringCopy(g_sharedMemory + SHM_PAYLOAD_OFFSET, cJSON_PrintUnformatted(g_gatewayInfo));
                        }
                    } else {
                        PlayAudio("restarting");
                        system("reboot");
                    }
                } else {
                    LED_ON;
                    // pclose(g_createApFile);
                    system("pkill -15 create_ap");
                }
                if (!g_needConfigGw) {
                    system("pkill -15 create_ap");
                }
                logInfo("Done configuring");
                state = 0;
            } else if (pinRead(USER_BUTTON) == 0) {
                state = 0;
                LED_ON;
                logInfo("Cancelled configuring");
                PlayAudio("wifi_config_cancel");
            }
            break;
        }
    }
}

void WriteLogLoop() {
    static long long int oldTick = 0;
    if (timeInMilliseconds() - oldTick > 5000) {
        oldTick = timeInMilliseconds();

        for (int i = 0; i < SERVICES_NUMBER; i++) {
            int queueSize = get_sizeQueue(g_logQueues[i]);
            if (queueSize > 0) {
                char* msg = NULL;
                for (int m = 0; m < queueSize; m++) {
                    int msgLength = StringLength(msg);
                    char* logItem = (char*)dequeue(g_logQueues[i]);
                    msg = realloc(msg, msgLength + StringLength(logItem) + queueSize + 10);
                    StringCopy(&msg[msgLength], logItem);
                    msgLength = StringLength(msg);
                    msg[msgLength] = '\n';
                    msg[msgLength + 1] = 0;
                }

                FILE *fp;
                char* today = GetCurrentDate();
                char filename[100];
                sprintf(filename, "logs/%s/log_%s_%s.txt", g_serviceName[i], g_serviceName[i], today);
                if (access(filename, F_OK) != 0) {
                    // Create new file
                    fp  = fopen (filename, "w");
                } else {
                    fp  = fopen (filename, "a");
                }
                fprintf(fp, "%s", msg);
                fclose(fp);
                free(msg);
            }
        }
    }
}

void AutoResetNetwork() {
    static long long tick = 0;
    if (timeInMilliseconds() - tick > 60000) {
        tick = timeInMilliseconds();

        char ipAddress[100];
        ExecCommand("hostname -I", ipAddress);
        logInfo("IP Address: %s", ipAddress);
        if (!ipAddress || StringLength(ipAddress) == 0 || !StringContains(ipAddress, "192")) {
            // Disable and re-enable network card
            logInfo("Reset the wifi interface: %s", ipAddress);
            system("nmcli r wifi off");
            system("nmcli r wifi on");
        }
    }
}

int main(int argc, char ** argv) {
    srand((unsigned int)time(NULL));
    for (int i = 0; i < SERVICES_NUMBER; i++) {
        g_logQueues[i] = newQueue(LOG_QUEUE_SIZE);
    }
    pinMode(USER_LED, OUTPUT);
    pinMode(USER_BUTTON, INPUT);
    pinMode(SPEAKER_PIN, OUTPUT);
    SPEAKER_DISABLE;
    LED_ON;
    InitSharedMemory();
    Mosq_Init();
    sleep(1);

    // Check if HC is master or slave
    FILE* f = fopen("app.json", "r");
    char buff[1000];
    fread(buff, sizeof(char), 1000, f);
    fclose(f);
    JSON* setting = JSON_Parse(buff);
    int isMaster = JSON_GetNumber(setting, "isMaster");
    printf("isMaster: %d\n", isMaster);
    char str[100];
    sprintf(str, "isMaster: %d", isMaster);
    enqueue(g_logQueues[SERVICE_ID_CFG], str);
    if (isMaster) {
        LED_OFF;
    }
    g_connectedTime = timeInMilliseconds();

    while (1) {
        Mosq_ProcessLoop();
        MainLoop();
        ConnectWifiLoop();
        LedProcessLoop();
        WriteLogLoop();
        // AutoResetNetwork();
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
        char* message = "{\"step\":6, \"message\":\"Kết nối wifi thất bại\"}";
        mosquitto_publish(mosq, NULL, MQTT_LOCAL_RESP_TOPIC, strlen(message), message, 0, false);
        logInfo("NOT CONNECTED");
    }
    pclose(fp);
    return ret;
}

bool CheckSSIDExist(const char* ssid) {
    bool ret = false;
    char path[1035];
    logInfo("Checking ssid '%s' is exist or not", ssid);
    FILE* fp = popen("nmcli dev wifi list", "r");
    while (fgets(path, sizeof(path), fp) != NULL) {
        logInfo("%s", path);
        if (strstr(path, ssid)) {
            ret = true;
            break;
        }
    }
    if (ret) {
        logInfo("SSID %s is found", ssid);
        PlayAudio("wifi_found");
    } else {
        logInfo("SSID %s is not found", ssid);
        PlayAudio("wifi_not_found");
    }
    pclose(fp);
    return ret;
}
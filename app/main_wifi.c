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
#include <pthread.h>

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX includes. */
#include <unistd.h>

#include <mosquitto.h>

#include "define.h"
#include "define_wifi.h"
#include "queue.h"
#include "parson.h"
#include "aws_mosquitto.h"
#include "core_process_t.h"
#include "time_t.h"
#include "database.h"
#include "wifi_process.h"

const char* SERVICE_NAME = SERVICE_TUYA;
uint8_t     SERVICE_ID   = SERVICE_ID_TUYA;
bool g_printLog = true;

static char g_homeId[30] = {0};
struct mosquitto * mosq;
struct Queue *queue_received;
pthread_mutex_t mutex_lock_t            = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue         = PTHREAD_COND_INITIALIZER;
bool getHomeId();

void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
    if(rc)
    {
        logError("Error with result code: %d", rc);
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_DEVICE_TUYA, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    if (StringCompare((char*)msg->payload, "WIFI_PING")) {
        mosquitto_publish(mosq, NULL, "APPLICATION_SERVICES/AWS/0", strlen("WIFI_PONG"), "WIFI_PONG", 0, false);
        return;
    }
    pthread_mutex_lock(&mutex_lock_t);
    int size_queue = get_sizeQueue(queue_received);
    if(size_queue < QUEUE_SIZE)
    {
        enqueue(queue_received,(char *) msg->payload);
        pthread_cond_broadcast(&dataUpdate_Queue);
        pthread_mutex_unlock(&mutex_lock_t);
    }
    else
    {
       pthread_mutex_unlock(&mutex_lock_t);
    }
}

void* RUN_MQTT_LOCAL(void* p)
{
    int rc = 0;
    mosquitto_lib_init();
    mosq = mosquitto_new(MQTT_MOSQUITTO_CIENT_ID, true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        logInfo("Client could not connect to broker! Error Code: %d", rc);
        mosquitto_destroy(mosq);
    }
    logInfo("We are now connected to the broker!");
    while(1)
    {
        rc = mosquitto_loop(mosq, -1, 1);
        if (rc != 0)
        {
            logError("mosquitto_loop is error: %d.", rc);
            break;
        }
        usleep(1000);
    }
}

// void ReportHealthCheck() {
//     static long long time = 0;
//     if (timeInMilliseconds() - time > 10000) {
//         time = timeInMilliseconds();
//         logInfo("WIFI_PONG");
//         // mosquitto_publish(mosq, NULL, "APPLICATION_SERVICES/AWS/0", strlen("WIFI_PONG"), "WIFI_PONG", 0, false);
//     }
// }

int main( int argc,char ** argv )
{
    long long GetAccessTokenTime = 0;

    int retry = 0;
    int size_queue = 0;
    bool check_flag = false;
    pthread_t thr[2];
    int rc[2];
    int err,xRun = 1;
    queue_received = newQueue(QUEUE_SIZE);

    rc[0]=pthread_create(&thr[0],NULL,RUN_MQTT_LOCAL,NULL);
    usleep(50000);

    if (pthread_mutex_init(&mutex_lock_t, NULL) != 0) {
            logError("mutex init has failed");
            return 1;
    }

    char access_token[40] = {'\0'};
    char access_token_refresh[40] = {'\0'};

    do
    {
        check_flag = get_access_token(access_token,access_token_refresh);
        if(!check_flag){
            logError("Not get access token!!!");
        }
        sleep(1);
    } while (!check_flag);
    GetAccessTokenTime = timeInMilliseconds();
    logInfo("GetAccessTokenTime = %lld", GetAccessTokenTime);
    logInfo("access_token = %s", access_token);
    logInfo("access_token_refresh = %s", access_token_refresh);

    char val_input[MAX_SIZE_ELEMENT_QUEUE] = {'\0'};
    char body[1000] = {'\0'};
    char message[MAX_SIZE_ELEMENT_QUEUE] = {'\0'};
    char tp[MAX_SIZE_TOPIC] = {'\0'};
    char pl[MAX_SIZE_PAYLOAD] = {'\0'};

    getHomeId();
    while (xRun!=0) {
        // ReportHealthCheck();

        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if(size_queue > 0)
        {
            pthread_t t_thread;
            memset(val_input,'\0',MAX_SIZE_ELEMENT_QUEUE);
            strcpy(val_input,(char *)dequeue(queue_received));
            logInfo("val_input = %s", val_input);

            JSON_Value *schema = NULL;
            JSON_Value *object = NULL;
            schema = json_parse_string(val_input);
            const char *LayerService    = json_object_get_string(json_object(schema),MOSQ_LayerService);
            const char *NameService     = json_object_get_string(json_object(schema),MOSQ_NameService);
            int type_action_t           = json_object_get_number(json_object(schema),MOSQ_ActionType);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            object                      = json_parse_string(object_string);

            JSON* payload = JSON_Parse(object_string);
            JSON* dictDPs = cJSON_GetObjectItem(payload, "dictDPs");

            int value = 0;
            long long currentTime = timeInMilliseconds();
            int failedCount = 0;
            if ((currentTime - GetAccessTokenTime)/1000/60 >= MaxTimeGetToken_Second) {
                logInfo("Getting new access token");
                do {
                    check_flag = get_access_token(access_token, access_token_refresh);
                    if(!check_flag) {
                        failedCount++;
                        logError("Cannot get access token!!!");
                    }
                    if (failedCount > 3) {
                        break;
                    }
                    sleep(1);
                } while (!check_flag);

                if (failedCount == 0) {
                    GetAccessTokenTime = timeInMilliseconds();
                    logInfo("GetAccessTokenTime = %lld", GetAccessTokenTime);
                    logInfo("refresh_token = %s", access_token);
                    logInfo("refresh_token = %s", access_token_refresh);
                } else {
                    GetAccessTokenTime += 60000;
                }
            }

            switch(type_action_t)
            {
                case TYPE_CTR_DEVICE:
                {
                    char* deviceID = JSON_GetText(payload, "deviceId");
                    char* code = JSON_GetText(payload, "code");
                    memset(body,'\0',1000);
                    memset(message,'\0',MAX_SIZE_ELEMENT_QUEUE);
                    sprintf(body, "{\"commands\": %s}", code);
                    if(!strlen(access_token)){
                        get_access_token(access_token,access_token_refresh);
                        break;
                    }
                    sprintf(message,"/v1.0/devices/%s/commands",deviceID);
                    check_flag = send_commands(access_token,CTR_DEVICE,message, body);
                    if(!check_flag){
                        retry = 0;
                        do
                        {
                            check_flag = send_commands(access_token,CTR_DEVICE,message, body);
                            retry++;
                            if (retry == 3) {
                                get_access_token(access_token,access_token_refresh);
                            }
                        } while (!check_flag && retry < MaxNumberRetry);
                        if(retry >= MaxNumberRetry){
                            logInfo("TIME OUT");
                        }
                    }
                    break;
                }
                case TYPE_CTR_GROUP_NORMAL:
                {
                    char* deviceID = JSON_GetText(payload, "deviceId");
                    char* code = JSON_GetText(payload, "code");
                    memset(body,'\0',1000);
                    memset(message,'\0',MAX_SIZE_ELEMENT_QUEUE);
                    sprintf(body, "{\"functions\": %s}", code);
                    if(!strlen(access_token)){
                        get_access_token(access_token,access_token_refresh);
                        break;
                    }
                    char* groupId = JSON_GetText(payload, "groupId");
                    sprintf(message,"/v1.0/device-groups/%s/issued", groupId);
                    check_flag = send_commands(access_token,CTR_DEVICE,message, body);
                    if(!check_flag){
                        retry = 0;
                        do
                        {
                            check_flag = send_commands(access_token,CTR_DEVICE,message, body);
                            retry++;
                            if (retry == 3) {
                                get_access_token(access_token,access_token_refresh);
                            }
                        } while (!check_flag && retry < MaxNumberRetry);
                        if(retry >= MaxNumberRetry){
                            logInfo("TIME OUT");
                        }
                    }
                    break;
                }
                case TYPE_CTR_SCENE:
                {
                    logInfo("TYPE_CTR_SCENE_HC");
                    int state = JSON_GetNumber(payload, "state");
                    char* sceneId = JSON_GetText(payload, "sceneId");
                    if(!strlen(access_token)){
                        get_access_token(access_token,access_token_refresh);
                        break;
                    }
                    memset(body,'\0',1000);
                    memset(message,'\0',1000);
                    char* method = "PUT";
                    if (state == 1) {
                        sprintf(message, "/v1.0/homes/%s/automations/%s/actions/enable", g_homeId, sceneId);
                    } else if(state == 0) {
                        sprintf(message, "/v1.0/homes/%s/automations/%s/actions/disable", g_homeId, sceneId);
                    } else{
                        sprintf(message, "/v1.0/homes/%s/scenes/%s/trigger", g_homeId, sceneId);
                        method = "POST";
                    }
                    check_flag = send_commands(access_token, method, message, "{}");
                    if(!check_flag){
                        retry = 0;
                        do
                        {
                            check_flag = send_commands(access_token, method, message, "{}");
                            retry++;
                            if (retry == 3) {
                                 get_access_token(access_token, access_token_refresh);
                            }
                        } while (!check_flag && retry < MaxNumberRetry);
                    }
                    if(retry >= MaxNumberRetry){
                        logInfo("TIME OUT");
                    }
                    break;
                }
                default:
                {
                    logError("Error detect");
                    break;
                }
            }
        }
        else if(size_queue == QUEUE_SIZE)
        {
            pthread_mutex_unlock(&mutex_lock_t);
        }
        else
        {
            pthread_cond_wait(&dataUpdate_Queue, &mutex_lock_t);
        }
        pthread_mutex_unlock(&mutex_lock_t);
        usleep(1000);
    }
    return 0;
}


bool getHomeId()
{
    logInfo("Getting HomeId");
    FILE* f = fopen("app.json", "r");
    char buff[1000];
    fread(buff, sizeof(char), 1000, f);
    fclose(f);
    JSON* setting = JSON_Parse(buff);
    StringCopy(g_homeId, JSON_GetText(setting, "homeId"));
    logInfo("HomeId: %s", g_homeId);
    return true;
}
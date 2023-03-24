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
#include <unistd.h>

#include "common_define.h"
#include "define.h"
#include "logging_stack.h"
#include "queue.h"
#include "parson.h"
#include "mosquitto.h"
#include "sqlite3.h"
#include "core_process_t.h"
#include "time_t.h"

#undef  LIBRARY_LOG_NAME
#define LIBRARY_LOG_NAME     "CMD"

const char* SERVICE_NAME = "CMD";
struct mosquitto * mosq;
struct Queue *queue_received;
pthread_mutex_t mutex_lock_t            = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue         = PTHREAD_COND_INITIALIZER;

FILE *fptr;
void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
	if(rc) 
	{
		LogError((get_localtime_now()),("Error with result code: %d\n", rc));
		exit(-1);
	}
	mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_COMMAND, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
    // int reponse = 0;
    // bool check_flag =false;

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
        LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
    }
    LogInfo((get_localtime_now()),("We are now connected to the broker!"));
    while(1)
    {
        rc = mosquitto_loop(mosq, -1, 1);
        if(rc != 0)
        {
            LogError( (get_localtime_now()),( "rc %d.",rc ) );
            fptr = fopen("/usr/bin/log.txt","a");
            fprintf(fptr,"[%s]COMMAND  error %d connected mosquitto\n",get_localtime_now(),rc);
            fclose(fptr);
            // break;
        }
        usleep(100);
    }
}

int main( int argc,char ** argv )
{
	int size_queue = 0;
    // bool check_flag = false;
    pthread_t thr[2];
    int xRun = 1;
    // int rc[2];
	queue_received = newQueue(QUEUE_SIZE);

    pthread_create(&thr[0],NULL,RUN_MQTT_LOCAL,NULL);
    usleep(50000);

    if (pthread_mutex_init(&mutex_lock_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }

    while(xRun!=0)
    {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if(size_queue > 0)
        {
            long long TimeCreat = 0;
            int reponse = 0;
            char *val_input = (char*)malloc(10000);
            // pthread_t t_thread;


            strcpy(val_input,(char *)dequeue(queue_received));
            logInfo("Received message %s", val_input);

            JSON_Value *schema = NULL;
            JSON_Value *object = NULL;
            schema = json_parse_string(val_input);
            free(val_input);
            int type_action_t           = json_object_get_number(json_object(schema),MOSQ_ActionType);
            TimeCreat                   = json_object_get_number(json_object(schema),MOSQ_TimeCreat);
            const char *Extern          = json_object_get_string(json_object(schema),MOSQ_Extend);
            const char *Id              = json_object_get_string(json_object(schema),MOSQ_Id);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            object                      = json_parse_string(object_string);
            switch(type_action_t)
            {
                char *topic;
                // char *payload;
                char *message;
                // char *deviceID;
                // char *dpID;
                // char *dpValue;
                int provider;
                // int dpValue_int = 0;
                // int i = 0;
                
                case TYPE_LOCK_KIDS:
                case TYPE_LOCK_AGENCY:
                case TYPE_DIM_LED_SWITCH:
                case TYPE_CTR_DEVICE:
                {
                    provider = json_object_get_number(json_object(object),KEY_PROVIDER);
                    if(provider == HOMEGY_BLE)
                    {
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CMD,type_action_t,MOSQ_ActResponse,Id,TimeCreat,object_string);
                        get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,type_action_t,MOSQ_ActResponse);
                        mqttLocalPublish(topic, message);
                    }
                    break;
                }
                case TYPE_DEBUG_CTL_LOOP_DEVICE:
                {
                    provider = json_object_get_number(json_object(object),KEY_PROVIDER);
                    if(provider == HOMEGY_BLE)
                    {
                        getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CMD,TYPE_DEBUG_CTL_LOOP_DEVICE,MOSQ_ActResponse,Id,TimeCreat,object_string);
                        get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_DEBUG_CTL_LOOP_DEVICE,MOSQ_ActResponse);
                        mqttLocalPublish(topic, message);
                    }
                    break;
                }
                case TYPE_CTR_GROUP_NORMAL:
                {  
                    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CMD,TYPE_CTR_GROUP_NORMAL,MOSQ_ActResponse,Id,TimeCreat,object_string);
                    get_topic(&topic,MOSQ_LayerService_Device,SERVICE_BLE,TYPE_CTR_GROUP_NORMAL,MOSQ_ActResponse);
                    mqttLocalPublish(topic, message);
                    break;
                }
                case TYPE_MANAGER_PING_ON_OFF:
                {
                    get_topic(&topic,MOSQ_LayerService_Manager,MOSQ_NameService_Manager_ServieceManager,TYPE_MANAGER_PING_ON_OFF,Extern);
                    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CMD,TYPE_MANAGER_PING_ON_OFF,MOSQ_Reponse,Id,TimeCreat,object_string);
                    mqttLocalPublish(topic, message);
                    break;
                }
                default:
                    LogError((get_localtime_now()),("Error detect"));
                    break;
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

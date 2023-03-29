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

#include "define.h"
#include "HG_SupportingService_Notifile_define.h"
#include "logging_stack.h"
#include "queue.h"
#include "parson.h"
#include "mosquitto.h"
#include "sqlite3.h"
#include "core_process_t.h"
#include "time_t.h"
#include "database.h"

const char* SERVICE_NAME = "NOTI";
sqlite3 *db_log;
struct mosquitto * mosq;
struct Queue *queue_received;
pthread_mutex_t mutex_lock_t            = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue         = PTHREAD_COND_INITIALIZER;

bool get_HistoryControlDevice(sqlite3 **db,char** result,char* deviceID,char* dpID, long int start, long int end, int limit);

void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
	if(rc) 
	{
		LogError((get_localtime_now()),("Error with result code: %d\n", rc));
		exit(-1);
	}
	mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_NOTIFILE, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
    int reponse = 0;
    bool check_flag =false;

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
            break;
        }
        usleep(1000);
    }
}

int main( int argc,char ** argv )
{
	int size_queue = 0;
    bool check_flag = false;
    pthread_t thr[2];
    int err,xRun = 1;
    int rc[2];
	queue_received = newQueue(QUEUE_SIZE);

    rc[0]=pthread_create(&thr[0],NULL,RUN_MQTT_LOCAL,NULL);
    usleep(50000);

    if (pthread_mutex_init(&mutex_lock_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }
    


    //Open database
    LogInfo((get_localtime_now()),("log 1"));
    check_flag = open_database(VAR_DATABASE_LOG,&db_log);   
    LogInfo((get_localtime_now()),("log 2."));
    if(check_flag)
    {
        LogInfo((get_localtime_now()),("sqlite3_open is success."));
    }
    else
    {
        LogWarn((get_localtime_now()),("sqlite3_open is failed. Creat_table_database_log"));
    }
    // check_flag = creat_table_database_log(&db_log);
    while(xRun!=0)
    {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if(size_queue > 0)
        {
            long long TimeCreat = 0;
            int reponse = 0;
            char *val_input = (char*)malloc(10000);
            pthread_t t_thread;


            strcpy(val_input,(char *)dequeue(queue_received));
            LogInfo((get_localtime_now()),("val_input = %s",val_input));

            JSON_Value *schema = NULL;
            JSON_Value *object = NULL;
            schema = json_parse_string(val_input);
            const char *LayerService    = json_object_get_string(json_object(schema),MOSQ_LayerService);
            const char *NameService     = json_object_get_string(json_object(schema),MOSQ_NameService);
            int type_action_t           = json_object_get_number(json_object(schema),MOSQ_ActionType);
            TimeCreat                   = json_object_get_number(json_object(schema),MOSQ_TimeCreat);
            const char *Extern          = json_object_get_string(json_object(schema),MOSQ_Extend);
            const char *Id              = json_object_get_string(json_object(schema),MOSQ_Id);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            object                      = json_parse_string(object_string);

            switch(type_action_t)
            {
                long int start;
                long int end;
                int limit;
                char *topic;
                char *payload;
                char *message;
                char *deviceID;
                char *dpID;
                char *dpValue;
                char *pid;

                case GW_RESPONSE_SENSOR_ENVIRONMENT_HUMIDITY:
                case GW_RESPONSE_SENSOR_ENVIRONMENT_TEMPER:
                case GW_RESPONSE_SENSOR_DOOR_ALARM:
                case GW_RESPONSE_SENSOR_DOOR_DETECT:
                case GW_RESPONSE_SENSOR_PIR_LIGHT:
                case GW_RESPONSE_SENSOR_PIR_DETECT:
                case GW_RESPONSE_SENSOR_BATTERY:
                case GW_RESPONSE_SENSOR_ENVIRONMENT:
                case GW_RESPONSE_SMOKE_SENSOR:
                case GW_RESPONSE_DEVICE_CONTROL:
                {
                    deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);
                    dpID = json_object_get_string(json_object(object),KEY_DP_ID);
                    dpValue = json_object_get_string(json_object(object),KEY_VALUE);
                    LogInfo((get_localtime_now()),("%s %s %s %lld",deviceID,dpID,dpValue,TimeCreat));
                    check_flag = sql_insertDataTableDeviceLog(&db_log,deviceID,dpID,dpValue,TimeCreat);
                    if(check_flag)
                    {
                        LogInfo((get_localtime_now()),("sql_insertDataTableDeviceLog is success."));
                        sql_print_a_table(&db_log,TABLE_DEVICE_LOG);
                        printf("\n");
                    }
                    else
                    {
                        LogError((get_localtime_now()),("sql_insertDataTableDeviceLog is failed"));
                    }
                    break;
                }
                case TYPE_GET_DEVICE_HISTORY:
                {
                    deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);
                    dpID = json_object_get_string(json_object(object),KEY_DP_ID);
                    start = json_object_get_number(json_object(object),KEY_TIME_START_PRECONDITION);
                    end = json_object_get_number(json_object(object),KEY_TIME_END_PRECONDITION);
                    limit = json_object_get_number(json_object(object),KEY_LIMIT);

                    LogInfo((get_localtime_now()),("deviceID = %s",deviceID));
                    LogInfo((get_localtime_now()),("dpID = %s",dpID));
                    LogInfo((get_localtime_now()),("start = %ld",start));
                    LogInfo((get_localtime_now()),("end = %ld",end));
                    LogInfo((get_localtime_now()),("limit = %d",limit));
                    check_flag = get_HistoryControlDevice(&db_log,&payload,deviceID,dpID,start,end,limit);
                    if(check_flag)
                    {
                        TimeCreat = timeInMilliseconds();
                        getFormTranMOSQ(&message,MOSQ_LayerService_Support,MOSQ_NameService_Support_Notifile,TYPE_GET_DEVICE_HISTORY,MOSQ_ActResponse,"TYPE_GET_DEVICE_HISTORY",TimeCreat,payload);
                        get_topic(&topic,MOSQ_LayerService_App,SERVICE_AWS,TYPE_GET_DEVICE_HISTORY,MOSQ_ActResponse);
                        reponse = mosquitto_publish(mosq, NULL,topic, strlen(message), message, 0, false);
                        if(MOSQ_ERR_SUCCESS != reponse)
                        {
                            LogError((get_localtime_now()),("publish to MOSQ_TOPIC_CORE_DATA_RESPON Failed\n"));
                        }
                    }
                    else
                    {
                        LogError((get_localtime_now()),("get_HistoryControlDevice is failed"));
                    }
                    break;
                }  
                case TYPE_CREAT_DB_TABLE_DEVICE_LOG:
                {
                    check_flag = creat_table_database_log(&db_log);
                    break;
                } 
                case TYPE_PRINT_DB_TABLE_DEVICE_LOG:
                {
                    sql_print_a_table(&db_log,TABLE_DEVICE_LOG);
                    printf("\n");
                    break;
                }  
                default:
                {
                    LogError((get_localtime_now()),("Error detect"));
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


bool get_HistoryControlDevice(sqlite3 **db,char** result,char* deviceID,char* dpID, long int start, long int end, int limit)
{
    if(deviceID == NULL || dpID == NULL)
    {
        return false;
    }
    if (limit <= 0)
    {
        limit = 10;
    }
    char* condition = malloc(1000);
    int size_dpID_inf = 0;
    char** str = str_split(dpID,',',&size_dpID_inf);
    size_dpID_inf = size_dpID_inf - 1;
    limit = limit*size_dpID_inf;
    if(size_dpID_inf == 1)
    {
        sprintf(condition," ( %s LIKE \'%%%s%\' )",KEY_DP_ID,*(str+0));
    }
    else if(size_dpID_inf == 2)
    {
        sprintf(condition," ( %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' )",KEY_DP_ID,*(str+0),KEY_DP_ID,*(str+1));
    }
    else if(size_dpID_inf == 3)
    {
        sprintf(condition," ( %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' )",KEY_DP_ID,*(str+0),KEY_DP_ID,*(str+1),KEY_DP_ID,*(str+2));
    }
    else if(size_dpID_inf == 4)
    {
        sprintf(condition," ( %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' )",KEY_DP_ID,*(str+0),KEY_DP_ID,*(str+1),KEY_DP_ID,*(str+2),KEY_DP_ID,*(str+3));
    }
    else if(size_dpID_inf == 5)
    {
        sprintf(condition," ( %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\' OR %s LIKE \'%%%s%\')",KEY_DP_ID,*(str+0),KEY_DP_ID,*(str+1),KEY_DP_ID,*(str+2),KEY_DP_ID,*(str+3),KEY_DP_ID,*(str+4));
    }
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    char *message;


    char *sql = (char*) malloc(1024 * sizeof(char));

    if(start <= 0 && end <=0)
    {
        printf("1 \n");
        sprintf(sql,"SELECT * FROM DEVICE_LOG WHERE %s = '%s' AND %s LIMIT %d;",KEY_DEVICE_ID,deviceID,condition,limit);
        printf("sql = %s \n",sql);
    }
    else if(start <= 0 && end > 0)
    {
        printf("2 \n");
        sprintf(sql,"SELECT * FROM DEVICE_LOG WHERE %s = '%s' AND %s <= '%ld' AND %s LIMIT %d;",KEY_DEVICE_ID,deviceID,KEY_TIMECREAT,end,condition,limit);
        printf("sql = %s \n",sql);
    }
    else if(start > 0 && end <= 0)
    {
        printf("3 \n");
        sprintf(sql,"SELECT * FROM DEVICE_LOG WHERE %s = '%s' AND %s >= '%ld' AND %s LIMIT %d;",KEY_DEVICE_ID,deviceID,KEY_TIMECREAT,start,condition,limit);
        printf("sql = %s \n",sql);
    }
    else
    {
        printf("4 \n");
        sprintf(sql,"SELECT * FROM DEVICE_LOG WHERE %s = '%s' AND %s >= '%ld' AND %s <= '%ld' AND %s LIMIT %d;",KEY_DEVICE_ID,deviceID,KEY_TIMECREAT,start,KEY_TIMECREAT,end,condition,limit);
        printf("sql = %s \n",sql);
    }

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    while (sqlite3_step(stmt) != SQLITE_DONE) 
    {
        message = malloc(100);
        sprintf(message,"%s.%s.%lld",sqlite3_column_text(stmt,0),sqlite3_column_text(stmt,1),sqlite3_column_int64(stmt,3));
        // printf("message %s\n",message );
        if(isMatchString(sqlite3_column_text(stmt,2),KEY_TRUE))
        {
            json_object_dotset_boolean(root_object,message,1);
        }
        else if(isMatchString(sqlite3_column_text(stmt,2),KEY_FALSE))
        {
            json_object_dotset_boolean(root_object,message,0);
        }
        else
        {
            json_object_dotset_number(root_object,message,String2Int(sqlite3_column_text(stmt,2)));
        }
        free(message);
    }
    sqlite3_finalize(stmt);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    printf("serialized_string %s\n",serialized_string );
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    free(condition);
    free(sql);
    return true;
}
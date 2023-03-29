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


#include "RuleSchedule_common.h"
#include "define.h"
#include "logging_stack.h"
#include "queue.h"
#include "core_process_t.h"
#include "time_t.h"
#include "mosquitto.h"
#include "parson.h"
#include "RuleSchedule.h"

const char* SERVICE_NAME = "RULE";
struct mosquitto * mosq;
struct Queue *queue_received;
struct Queue *queue_mos_pub;
struct Queue *queue_process_scrip;
struct tm tm;

//define for MAIN_TASK
pthread_mutex_t mutex_lock_t                    = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue                 = PTHREAD_COND_INITIALIZER;

//define for MOSQ_PUB_TASK
pthread_mutex_t mutex_lock_mosq_t               = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_MosqQueue             = PTHREAD_COND_INITIALIZER;

//define for SCRIP_PRCESS_TASK
pthread_mutex_t mutex_lock_process_scrip_t      = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_ProcessScrip          = PTHREAD_COND_INITIALIZER;

//define for ACTION_SCENE_TASK
pthread_mutex_t mutex_lock_action_scene_t      = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_ActionScene          = PTHREAD_COND_INITIALIZER;

//define for CALCULATION_ACTION_SCENE_TASK
pthread_mutex_t mutex_lock_variable_t               = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  dataUpdate_CalculationActionScene   = PTHREAD_COND_INITIALIZER;

const JSON_Value *Json_Value_InfoDevices = NULL;

InfoActionsScene InfoActionsScene_t[100] = {0};
int count_ActionsScene = 0;


char *result_actionScene = "{}";
char *result_conditionScene = "{}";
char *CollectCondition = "{}";


bool callInfoDeviceFromDatabase();
void on_connect(struct mosquitto *mosq, void *obj, int rc);
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);
void* RUN_MQTT_LOCAL(void* p);
void acctionScene(const char* result_actionScene,char *sceneID);
bool callInfoSceneFromDatabase();

void* MOSQ_PUB_TASK(void* p)
{
    int size_queue = 0;
    while (1)
    {
        pthread_mutex_lock(&mutex_lock_mosq_t);
        size_queue = get_sizeQueue(queue_mos_pub);
        // printf("size_queue %d\n",size_queue );
        if(size_queue > 0)
        {   
            char *message = (char*)malloc(10000);
            char *topic;
            strcpy(message,(char *)dequeue(queue_mos_pub)); 
            JSON_Value *schema = NULL;
            schema = json_parse_string(message);
            char *LayerService  = (char *)json_object_get_string(json_object(schema),MOSQ_LayerService);
            char *NameService   = (char *)json_object_get_string(json_object(schema),MOSQ_NameService);
            int TypeAction      = json_object_get_number(json_object(schema),MOSQ_ActionType);
            char *Extern        = (char *)json_object_get_string(json_object(schema),MOSQ_Extend);
            get_topic(&topic,LayerService,NameService,TypeAction,Extern);
            mosquitto_publish(mosq, NULL,topic,strlen(message),message, 0, false);
            free(topic);
            free(message);
        }
        else if(size_queue == QUEUE_SIZE)
        {
            pthread_mutex_unlock(&mutex_lock_mosq_t);
        }
        else
        {
            pthread_cond_wait(&dataUpdate_MosqQueue, &mutex_lock_mosq_t);
        }
        pthread_mutex_unlock(&mutex_lock_mosq_t);
        usleep(1000);  
    }
    return p;
}

void* CALCULATION_REAL_TIME_TASK(void* p)
{
    while(1)
    {
        pthread_mutex_lock(&mutex_lock_process_scrip_t);
        pthread_mutex_lock(&mutex_lock_variable_t);
        JSON_Value *object = json_parse_string(CollectCondition);
        pthread_mutex_unlock(&mutex_lock_variable_t);
        if(json_object_has_value(json_object(object),KEY_TIMER))
        {
            time_t now = time(0);
            tm = *localtime(&now);        

            struct infor_day *tmp_day       = malloc(sizeof(struct infor_day));
            struct infor_time *tmp_time     = malloc(sizeof(struct infor_time));
            int day_now         = get_day_today(tm);
            int month_now       = get_mon_today(tm);
            int year_now        = get_year_today(tm);
            int hour_now        = get_hour_today(tm);
            int min_now         = get_min_today(tm);
            int dayOfWeek_now   = day_of_week(day_now,month_now,year_now);


            //get sceneID has using timer
            JSON_Array  * array_timer           = json_object_get_array(json_object(object),KEY_TIMER);
            pthread_mutex_lock(&mutex_lock_variable_t);
            JSON_Value  *value_ConditionScene   = json_parse_string(result_conditionScene);
            LogInfo((get_localtime_now()),("CALCULATION_REAL_TIME_TASK : %s\n",result_conditionScene));
            pthread_mutex_unlock(&mutex_lock_variable_t);

            int count_elementTimer = json_array_get_count(array_timer);
            int i = 0;
            char *tmpDirection;
            for(i = 0; i < count_elementTimer; i++)
            {
                const char* tmp_sceneID = json_array_get_string(array_timer,i);
                if(json_object_has_value(json_object(value_ConditionScene),tmp_sceneID))
                {
                    tmpDirection = malloc(50);
                    sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_TIME);
                    const char* time_ = json_object_dotget_string(json_object(value_ConditionScene),tmpDirection);
                    free(tmpDirection);


                    tmpDirection = malloc(50);
                    sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_DATE);                    
                    const char* date_ = json_object_dotget_string(json_object(value_ConditionScene),tmpDirection);
                    free(tmpDirection);

                    tmpDirection = malloc(50);
                    sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_LOOP_PRECONDITION);
                    const char* loop_ = json_object_dotget_string(json_object(value_ConditionScene),tmpDirection);
                    free(tmpDirection);

                    //conver data time from database
                    getInforDayFromScene(tmp_day,date_);
                    getInforTimeFromScene(tmp_time,time_);

                    int day_date = String2Int(tmp_day->day);
                    int month_date = String2Int(tmp_day->month);
                    int year_date = String2Int(tmp_day->year);
                    int hour_date = String2Int(tmp_time->hour);
                    int min_date = String2Int(tmp_time->min);

                    pthread_mutex_lock(&mutex_lock_variable_t);
                    tmpDirection = malloc(50);
                    sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_STATE);
                    int state_ = json_object_dotget_number(json_object(value_ConditionScene),tmpDirection);
                    free(tmpDirection);
                    pthread_mutex_unlock(&mutex_lock_variable_t);

                    //comper data time scene with condition
                    if(day_date == day_now && month_date == month_now && year_date == year_now && hour_date == hour_now && min_date == min_now)
                    {
                        if(state_ == 0)
                        {
                            pthread_mutex_lock(&mutex_lock_variable_t);
                            LogInfo((get_localtime_now()),("CALCULATION_REAL_TIME_TASK result_conditionScene 1 : %s\n",result_conditionScene));
                            tmpDirection = malloc(50);
                            sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_STATE);
                            json_object_dotset_number(json_object(value_ConditionScene),tmpDirection,1);

                            char *serialized_string = NULL;
                            serialized_string = json_serialize_to_string_pretty(value_ConditionScene);
                            int size_t = strlen(serialized_string);
                            result_conditionScene = malloc(size_t+1);
                            memset(result_conditionScene,'\0',size_t+1);
                            strcpy(result_conditionScene,serialized_string);
                            pthread_mutex_unlock(&mutex_lock_variable_t);

                            free(tmpDirection);
                            pthread_cond_signal(&dataUpdate_CalculationActionScene);
                        }
                    }
                    else
                    {
                        if(state_ != 1)
                        {
                            if(check_day_with_scene(dayOfWeek_now,loop_))
                            {
                                if(hour_date == hour_now && min_date == min_now)
                                {
                                    pthread_mutex_lock(&mutex_lock_variable_t);
                                    LogInfo((get_localtime_now()),("CALCULATION_REAL_TIME_TASK result_conditionScene 2 : %s\n",result_conditionScene));
                                    tmpDirection = malloc(50);
                                    sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_STATE);
                                    json_object_dotset_number(json_object(value_ConditionScene),tmpDirection,1);
                                    char *serialized_string = NULL;
                                    serialized_string = json_serialize_to_string_pretty(value_ConditionScene);
                                    int size_t = strlen(serialized_string);
                                    result_conditionScene = malloc(size_t+1);
                                    memset(result_conditionScene,'\0',size_t+1);
                                    strcpy(result_conditionScene,serialized_string);

                                    pthread_mutex_unlock(&mutex_lock_variable_t);
                                    free(tmpDirection);
                                    pthread_cond_signal(&dataUpdate_CalculationActionScene);
                                }
                            }                            
                        }
                        else
                        {

                            pthread_mutex_lock(&mutex_lock_variable_t);
                            tmpDirection = malloc(50);
                            sprintf(tmpDirection,"%s.%s.%s.%s.%s",tmp_sceneID,KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_STATE);
                            json_object_dotset_number(json_object(value_ConditionScene),tmpDirection,0);
                            char *serialized_string = NULL;
                            serialized_string = json_serialize_to_string_pretty(value_ConditionScene);
                            int size_t = strlen(serialized_string);
                            result_conditionScene = malloc(size_t+1);
                            memset(result_conditionScene,'\0',size_t+1);
                            strcpy(result_conditionScene,serialized_string);
                            pthread_mutex_unlock(&mutex_lock_variable_t);
                            free(tmpDirection);                        }
                    }
                }
            }
        }
        pthread_mutex_unlock(&mutex_lock_process_scrip_t);
        sleep(3);
    }
}

void* CALCULATION_ACTION_SCENE_TASK(void* p)
{
    while(1)
    {
        pthread_mutex_lock(&mutex_lock_process_scrip_t);
        pthread_cond_wait(&dataUpdate_CalculationActionScene,&mutex_lock_process_scrip_t);
        pthread_mutex_lock(&mutex_lock_action_scene_t);
        LogInfo((get_localtime_now()),("CALCULATION_ACTION_SCENE_TASK : %lld\n",timeInMilliseconds()));
        JSON_Value   *value_ConditionScene  =   json_parse_string(result_conditionScene);
        int count_elementSceneID = json_object_get_count(json_object(value_ConditionScene));
        int i = 0;
        for(i=0;i<count_elementSceneID;i++)
        {
            const char *tmp_sceneID             =   json_object_get_name(json_object(value_ConditionScene),i);
            JSON_Object  *object_SceneID        =   json_object_get_object(json_object(value_ConditionScene),tmp_sceneID);
            JSON_Object  *object_DictMeta       =   json_object_get_object(object_SceneID,KEY_DICT_META);
            JSON_Object  *object_DictInfo       =   json_object_get_object(object_SceneID,KEY_DICT_INFO);

            char *sceneType_t = json_object_get_string(object_DictInfo,KEY_TYPE_SCENE);
            if(isMatchString(sceneType_t,TYPE_OneOfAll_SCENE))
            {
                int check = 0;            
                int count_elementDictMeta = json_object_get_count(object_DictMeta);
                int j=0;
                for(j = 0;j <count_elementDictMeta;j++)
                {
                    int k = 0;

                    char *deviceID_temp = json_object_get_name(object_DictMeta,j);
                    LogInfo((get_localtime_now()),("deviceID_temp : %s\n",deviceID_temp));
                
                    JSON_Object  *object_DeviceID =      json_object_get_object(object_DictMeta,deviceID_temp);
                    int count_elementDeviceID = json_object_get_count(object_DeviceID);
                    // LogInfo((get_localtime_now()),("count_elementDeviceID : %d\n",count_elementDeviceID));
                    for(k = 0; k < count_elementDeviceID; k++)
                    {
                        char* tmp_dpid =    json_object_get_name(object_DeviceID,k);
                        // LogInfo((get_localtime_now()),("tmp_dpid : %s\n",tmp_dpid));

                        JSON_Object  *object_DeviceWithdpID =      json_object_get_object(object_DeviceID,tmp_dpid);

                        int state_t = json_object_get_number(object_DeviceWithdpID,KEY_STATE);
                        // LogInfo((get_localtime_now()),("state_t : %d\n",state_t));
                        if(state_t == 1)
                        {
                            LogInfo((get_localtime_now()),("ACTION_SCENE_TASK\n"));
                            check = 1;
                        }                        
                    }
                    if(check)
                    {
                        acctionScene(result_actionScene,tmp_sceneID);
                        pthread_cond_signal(&dataUpdate_ActionScene);  
                    }                                           
                }
            }
            else if(isMatchString(sceneType_t,TYPE_All_SCENE))
            {
                int check = 1;
                int count_elementDictMeta = json_object_get_count(object_DictMeta);
                int j=0;
                for(j = 0;j <count_elementDictMeta;j++)
                {
                    int k = 0;
                    char *deviceID_temp = json_object_get_name(object_DictMeta,j);
                    JSON_Object  *object_DeviceID =      json_object_get_object(object_DictMeta,deviceID_temp);
                    int count_elementDeviceID = json_object_get_count(object_DeviceID);
                    for(k = 0; k < count_elementDeviceID; i++)
                    {
                        char* tmp_dpid =    json_object_get_name(object_DeviceID,k);
                        JSON_Object  *object_DeviceWithdpID =      json_object_get_object(object_DeviceID,tmp_dpid);
                        check = check* json_object_get_number(object_DeviceWithdpID,KEY_STATE);
                    }
                }
                if(check)
                {
                    acctionScene(result_actionScene,tmp_sceneID);
                    pthread_cond_signal(&dataUpdate_ActionScene);  
                }

            }
        }
        pthread_mutex_unlock(&mutex_lock_action_scene_t);
        pthread_mutex_unlock(&mutex_lock_process_scrip_t);
        usleep(5000);
    }    
}

void* SCRIP_PROCESS_TASK(void* p)
{
    int size_queue = 0;
    while(1)
    {
        pthread_mutex_lock(&mutex_lock_process_scrip_t);
        size_queue = get_sizeQueue(queue_process_scrip);
        // LogInfo((get_localtime_now()),("size_queue = %d",size_queue));
        if(size_queue > 0)
        {
            LogInfo((get_localtime_now()),("SCRIP_PROCESS_TASK : %lld\n",timeInMilliseconds()));
            char *val_input = (char*)malloc(10000);
            strcpy(val_input,(char *)dequeue(queue_process_scrip));
            // LogInfo((get_localtime_now()),("SCRIP_PROCESS_TASK : %s\n",val_input));

            JSON_Value *object = json_parse_string(val_input);

            //get status device
            char *deviceID_t    =   json_object_get_string(json_object(object),KEY_DEVICE_ID);
            char *dpID_t        =   json_object_get_string(json_object(object),KEY_DP_ID);
            char *value_input   =   json_object_get_string(json_object(object),KEY_VALUE);
            LogInfo((get_localtime_now()),("value_input : %s\n",value_input));
            char *dotString = malloc(50);
            sprintf(dotString,"%s.%s",deviceID_t,dpID_t);
            //get list sceneID have address
            pthread_mutex_lock(&mutex_lock_variable_t);
            JSON_Value *value_CollectCondition = json_parse_string(CollectCondition);
            pthread_mutex_unlock(&mutex_lock_variable_t);
            JSON_Array *array_CollectCondition = json_object_dotget_array(json_object(value_CollectCondition),dotString);
            if(array_CollectCondition != NULL) //address have into CollectCondition{}
            {
                int count_elementScene = json_array_get_count(array_CollectCondition);

                pthread_mutex_lock(&mutex_lock_variable_t);
                int i = 0;
                int size_t_1 = strlen(result_conditionScene);
                char *input = malloc(size_t_1+1);
                memset(input,'\0',size_t_1+1);
                strcpy(input,result_conditionScene);

                pthread_mutex_unlock(&mutex_lock_variable_t);

                JSON_Value   *value_ConditionScene  =   json_parse_string(input);
                //LogInfo((get_localtime_now()),("result_conditionScene after: %s\n",result_conditionScene));
                //update state into ConditionScene
                for(i = 0; i < count_elementScene; i++)
                {
                    char *tmp_sceneID = json_array_get_string(array_CollectCondition,i);
                    //check state of sceneID into ArrayConditonScene
                    JSON_Object  *object_SceneID        =   json_object_get_object(json_object(value_ConditionScene),tmp_sceneID);
                    JSON_Object  *object_DictMeta       =   json_object_get_object(object_SceneID,KEY_DICT_META);
                    JSON_Object  *object_DeviceID       =   json_object_dotget_object(object_DictMeta,dotString);


                    char *dpvalue_t = json_object_get_string(object_DeviceID,KEY_DP_VAL);
                    // LogInfo((get_localtime_now()),("tmp_sceneID : %s\n",tmp_sceneID));
                    // LogInfo((get_localtime_now()),("dpvalue_t : %s\n",dpvalue_t));

                    if(isMatchString(dpvalue_t,value_input))
                    {
                        json_object_set_number(object_DeviceID,KEY_STATE,1);
                    }
                    else
                    {
                        json_object_set_number(object_DeviceID,KEY_STATE,0);
                    }
                }

                pthread_mutex_lock(&mutex_lock_variable_t);
                char *serialized_string = NULL;
                serialized_string = json_serialize_to_string_pretty(value_ConditionScene);
                int size_t = strlen(serialized_string);
                result_conditionScene = malloc(size_t+1);
                memset(result_conditionScene,'\0',size_t+1);
                strcpy(result_conditionScene,serialized_string);
                // LogInfo((get_localtime_now()),("result_conditionScene befor : %s\n",result_conditionScene));
                pthread_mutex_unlock(&mutex_lock_variable_t);
                pthread_cond_signal(&dataUpdate_CalculationActionScene);

            }
            free(dotString);
        }
        else if(size_queue == QUEUE_SIZE)
        {
            pthread_mutex_unlock(&mutex_lock_process_scrip_t);
        }
        else
        {
            pthread_cond_wait(&dataUpdate_ProcessScrip, &mutex_lock_process_scrip_t);
        }
        pthread_mutex_unlock(&mutex_lock_process_scrip_t);
        usleep(1000);
    }
    return p;
}

void* ACTION_SCENE_TASK(void* p)
{
    char *value;
    char *topic;
    char *message;
    int reponse = 0;
    int size_actionScene = 0;
    while(1)
    {
        pthread_mutex_lock(&mutex_lock_action_scene_t);
        size_actionScene = count_ActionsScene;
        if(count_ActionsScene > 0)
        {
            int i  = 0,count = 0;
            long long int TimeStamp = timeInMilliseconds() + 1;
            BubleSort(InfoActionsScene_t,count_ActionsScene);
            count = getCountActionWithTimeStemp(InfoActionsScene_t,count_ActionsScene,TimeStamp);
            if(count > 0)
            {
                for( i = 0; i < count; i++)
                {
                    value = InfoActionsScene_t[i].payload;
                    JSON_Value *schema = json_parse_string(value);
                    if(json_object_get_number(json_object(schema),KEY_PROVIDER) != HOMEGY_DELAY)
                    {
                        getFormTranMOSQ(&message,MOSQ_LayerService_Support,MOSQ_NameService_Support_Rule_Schedule,TYPE_CTR_DEVICE,MOSQ_ActResponse,json_object_get_string(json_object(schema),KEY_DEVICE_ID),InfoActionsScene_t[i].TimeCreat,value);
                        get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CMD,TYPE_CTR_DEVICE,MOSQ_ActResponse);
                    }
                    reponse = mosquitto_publish(mosq, NULL,topic, strlen(message),message, 0, false);
                    if(MOSQ_ERR_SUCCESS != reponse)
                    {
                    LogError((get_localtime_now()),("publish to MOSQ_TOPIC_CORE_DATA_RESPON Failed\n"));
                    }
                }
                for( i = 0; i < count; i++)
                {
                    deleteDataActionsScene(InfoActionsScene_t,&count_ActionsScene,0);
                }
            }
        }
        else
        {
            pthread_cond_wait(&dataUpdate_ActionScene, &mutex_lock_action_scene_t);
        }
        pthread_mutex_unlock(&mutex_lock_action_scene_t);
        LogInfo((get_localtime_now()),("DONE = %lld\n",timeInMilliseconds()));
        usleep(5000);
    }
    return p;
}

int main( int argc,char ** argv )
{
    int size_queue = 0;
    bool check_flag = false;
    pthread_t thr[6];
    int err,xRun = 1;
    int rc[6];
    queue_received      = newQueue(QUEUE_SIZE);
    queue_mos_pub       = newQueue(QUEUE_SIZE);
    queue_process_scrip = newQueue(QUEUE_SIZE);

    //Init for thread()
    rc[0]=pthread_create(&thr[0],NULL,RUN_MQTT_LOCAL,NULL);
    usleep(50000);
    rc[1]=pthread_create(&thr[1],NULL,MOSQ_PUB_TASK,NULL);
    usleep(50000);
    rc[2]=pthread_create(&thr[2],NULL,ACTION_SCENE_TASK,NULL);
    usleep(50000);
    rc[3]=pthread_create(&thr[3],NULL,SCRIP_PROCESS_TASK,NULL);
    usleep(50000);
    rc[4]=pthread_create(&thr[4],NULL,CALCULATION_ACTION_SCENE_TASK,NULL);
    usleep(50000);
    rc[5]=pthread_create(&thr[5],NULL,CALCULATION_REAL_TIME_TASK,NULL);
    usleep(50000);

    if (pthread_mutex_init(&mutex_lock_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_mosq_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_process_scrip_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_action_scene_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }
    usleep(50000);
    if (pthread_mutex_init(&mutex_lock_variable_t, NULL) != 0) {
            LogError((get_localtime_now()),("mutex init has failed"));
            return 1;
    }

    usleep(50000);
    callInfoDeviceFromDatabase();
    usleep(50000);
    callInfoSceneFromDatabase();
    usleep(50000);
    while(xRun!=0)
    {
        pthread_mutex_lock(&mutex_lock_t);
        size_queue = get_sizeQueue(queue_received);
        if(size_queue > 0)
        {
            long long int TimeCreat = 0;
            int reponse = 0;
            int state = 0;
            int dpValue_int = 0;
            int i = 0, size_ObjectPayload = 0;
            char *val_input = (char*)malloc(10000);
            strcpy(val_input,(char *)dequeue(queue_received));
            LogInfo((get_localtime_now()),("val_input : %s\n",val_input));

            JSON_Value *schema = NULL;
            JSON_Value *object = NULL;
            schema = json_parse_string(val_input);
            const char *LayerService    = json_object_get_string(json_object(schema),MOSQ_LayerService);
            const char *NameService     = json_object_get_string(json_object(schema),MOSQ_NameService);
            int type_action_t           = json_object_get_number(json_object(schema),MOSQ_ActionType);
            TimeCreat                   = json_object_get_number(json_object(schema),MOSQ_TimeCreat);
            const char *Id              = json_object_get_string(json_object(schema),MOSQ_Id);
            const char *Extern          = json_object_get_string(json_object(schema),MOSQ_Extend);
            const char *object_string   = json_object_get_string(json_object(schema),MOSQ_Payload);
            object = json_parse_string(object_string);

            switch(type_action_t)
            {
                char *object_string_tmp;
                char *topic;
                char *payload;
                char *message;
                char *deviceID;
                char *sceneID;
                char *dpID;
                char *dpValue;
                char *address;
                case TYPE_CTR_SCENE://send to list actions of scene to COMMAND service
                    sceneID = json_object_get_string(json_object(object),KEY_ID_SCENE);
                    state =  json_object_get_number(json_object(object),KEY_STATE);
                    if(state)
                    {
                        pthread_mutex_lock(&mutex_lock_action_scene_t);
                        acctionScene(result_actionScene,sceneID);
                        pthread_cond_signal(&dataUpdate_ActionScene);
                        pthread_mutex_unlock(&mutex_lock_action_scene_t);
                    }
                    break;
                case TYPE_DEL_DEVICE:
                case TYPE_ADD_DEVICE://update data device from database
                    callInfoDeviceFromDatabase();
                    break;
                case TYPE_ADD_SCENE://get infor condition, action of scene
                    pthread_mutex_lock(&mutex_lock_process_scrip_t);
                    pthread_mutex_lock(&mutex_lock_variable_t);
                    getArrayActionScene(&result_actionScene,object_string,Json_Value_InfoDevices);
                    getArrayConditionScene(&result_conditionScene,object_string,Json_Value_InfoDevices);
                    getCollectCondition(&CollectCondition,object_string,Json_Value_InfoDevices);
                    LogInfo((get_localtime_now()),("result_conditionScene = %s",result_conditionScene));
                    LogInfo((get_localtime_now()),("result_actionScene = %s",result_actionScene));
                    LogInfo((get_localtime_now()),("CollectCondition = %s",CollectCondition));
                    pthread_mutex_unlock(&mutex_lock_variable_t);
                    pthread_mutex_unlock(&mutex_lock_process_scrip_t);
                    break;
 
                case TYPE_UPDATE_SCENE_HC:
                case TYPE_DEL_SCENE:
                {
                    callInfoSceneFromDatabase();
                    break;
                }
               
                case TYPE_GET_INF_DEVICES:
                {
                    Json_Value_InfoDevices = json_value_deep_copy(object);
                    break;
                }
               
                case TYPE_GET_INF_SCENES:
                {
                    pthread_mutex_lock(&mutex_lock_process_scrip_t);
                    size_ObjectPayload                  = json_object_get_count(json_object(object));
                    for(i=0;i<size_ObjectPayload;i++)
                    {
                        pthread_mutex_lock(&mutex_lock_variable_t);
                        object_string_tmp = json_serialize_to_string_pretty(json_object_get_value(json_object(object),json_object_get_name(json_object(object),i)));
                        LogInfo((get_localtime_now()),("object_string_tmp = %s",object_string_tmp));
                        getArrayActionScene(&result_actionScene,object_string_tmp,Json_Value_InfoDevices);
                        LogInfo((get_localtime_now()),("result_actionScene = %s",result_actionScene));
                        getArrayConditionScene(&result_conditionScene,object_string_tmp,Json_Value_InfoDevices);
                        LogInfo((get_localtime_now()),("result_conditionScene = %s",result_conditionScene));
                        getCollectCondition(&CollectCondition,object_string_tmp,Json_Value_InfoDevices);
                        LogInfo((get_localtime_now()),("CollectCondition = %s",CollectCondition));
                        free(object_string_tmp);
                        pthread_mutex_unlock(&mutex_lock_variable_t);
                    }
                    // LogInfo((get_localtime_now()),("result_actionScene = %s",result_actionScene));
                    // LogInfo((get_localtime_now()),("result_conditionScene = %s",result_conditionScene));
                    // LogInfo((get_localtime_now()),("CollectCondition = %s",CollectCondition));
                    pthread_mutex_unlock(&mutex_lock_process_scrip_t);
                    break;
                }

                case GW_RESPONSE_SMOKE_SENSOR:
                case GW_RESPONSE_SENSOR_ENVIRONMENT_TEMPER:
                case GW_RESPONSE_SENSOR_ENVIRONMENT_HUMIDITY:
                case GW_RESPONSE_SENSOR_PIR_DETECT:   
                case GW_RESPONSE_SENSOR_PIR_LIGHT:
                case GW_RESPONSE_SENSOR_DOOR_DETECT:
                case GW_RESPONSE_DEVICE_CONTROL:
                {
                    pthread_mutex_lock(&mutex_lock_variable_t);
                    //check deviceID into getCollectCondition{}?
                    deviceID = (char*)json_object_get_string(json_object(object),KEY_DEVICE_ID);
                    if(json_object_has_value(json_object(json_parse_string(CollectCondition)),deviceID))
                    {
                        //deviceID has into CollectCondition
                        pthread_mutex_lock(&mutex_lock_process_scrip_t);
                        enqueue(queue_process_scrip,(char*)object_string);
                        pthread_cond_broadcast(&dataUpdate_ProcessScrip);
                        pthread_mutex_unlock(&mutex_lock_process_scrip_t);
                    }
                    pthread_mutex_unlock(&mutex_lock_variable_t);
                    break;
                }
                case TYPE_MANAGER_PING_ON_OFF:
                {
                    get_topic(&topic,MOSQ_LayerService_Manager,MOSQ_NameService_Manager_ServieceManager,TYPE_MANAGER_PING_ON_OFF,Extern);
                    getFormTranMOSQ(&message,MOSQ_LayerService_Support,MOSQ_NameService_Support_Rule_Schedule,TYPE_MANAGER_PING_ON_OFF,MOSQ_Reponse,Id,TimeCreat,object_string);
                    reponse = mosquitto_publish(mosq, NULL,topic, strlen(message),message, 0, false);
                    if(MOSQ_ERR_SUCCESS != reponse)
                    {
                        LogError((get_localtime_now()),("publish to MOSQ_TOPIC_CORE_DATA_RESPON Failed\n"));
                    }
                    break;
                }
                default:
                    LogError((get_localtime_now()),("Error detect"));
                    break;
            }
            free(val_input);
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


void on_connect(struct mosquitto *mosq, void *obj, int rc) 
{
    if(rc) 
    {
        LogError((get_localtime_now()),("Error with result code: %d\n", rc));
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_RULE, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
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
        // LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
    }
    // LogInfo((get_localtime_now()),("We are now connected to the broker!"));
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


void acctionScene(const char* result_actionScene,char *sceneID)
{
    long long int TimeCreat = timeInMilliseconds();
    int i = 0, size_array_actions = 0;
    JSON_Value * Payload_value      = json_parse_string(result_actionScene);
    JSON_Array * actions_array      = json_object_get_array(json_object(Payload_value),sceneID);
    size_array_actions              = json_array_get_count(actions_array);
    for(i = 0; i < size_array_actions; i++)
    {
        JSON_Value  *payload = (char*)json_array_get_value(actions_array,i);
        if(json_object_get_number(json_object(payload),KEY_PROVIDER) != HOMEGY_DELAY)
        {
            TimeCreat = TimeCreat + i;
            // LogInfo((get_localtime_now()),("TimeCreat[%d] = %lld",i,TimeCreat));
            // LogInfo((get_localtime_now()),("json_serialize_to_string_pretty(payload)[%d] = %s",i,json_serialize_to_string_pretty(payload)));
            insertDataActionsScene(InfoActionsScene_t,TimeCreat,&count_ActionsScene,json_serialize_to_string_pretty(payload));
        }
        else
        {
            char *dotString = "dictDPs.delay";
            int sec_delay_ = json_object_dotget_number(json_object(payload),dotString);
            // printf("sec_delay_ %d\n",sec_delay_ );
            TimeCreat = timeInMilliseconds() + sec_delay_*1000;
        }
    }
    // LogInfo((get_localtime_now()),("count_ActionsScene = %d",count_ActionsScene));
    // printDataActionsScene(InfoActionsScene_t,count_ActionsScene);
    // LogInfo((get_localtime_now()),("end acctionScene"));
}

bool callInfoDeviceFromDatabase()
{
    // char *message;
    // char *topic;
    // int reponse = 0;
    // getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_DEVICES,MOSQ_ActResponse,"pre_detect->object",1111,"payload");
    // get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_DEVICES,MOSQ_Request);
    // reponse = mosquitto_publish(mosq, NULL,topic, strlen(message),message, 0, false);
    // if(MOSQ_ERR_SUCCESS != reponse)
    // {
    //     LogError((get_localtime_now()),("publish to MOSQ_TOPIC_CORE_DATA_RESPON Failed\n"));
    // }
    // free(message);
    // free(topic);
    return true;
}

bool callInfoSceneFromDatabase()
{
    char *message;
    char *topic;
    int reponse = 0;  
    getFormTranMOSQ(&message,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_SCENES,MOSQ_ActResponse,"pre_detect->object",1111,"payload");
    get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_GET_INF_SCENES,MOSQ_Request);
    reponse = mosquitto_publish(mosq, NULL,topic, strlen(message),message, 0, false);
    if(MOSQ_ERR_SUCCESS != reponse)
    {
        LogError((get_localtime_now()),("publish to MOSQ_TOPIC_CORE_DATA_RESPON Failed\n"));
    }
    free(message);
    free(topic);
    return true;
}

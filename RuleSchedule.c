#include "RuleSchedule.h"


bool getArrayActionScene(char **result,const char *stringPayload,const JSON_Value *object_devices)
{
    int size_array_actions = 0,i=0;

    int size_t_1 = strlen(*result);
    char *input = malloc(size_t_1+1);
    memset(input,'\0',size_t_1+1);
    strcpy(input,*result);

    JSON_Value  *result_value       = json_parse_string(input);
    JSON_Object *result_object      = json_value_get_object(result_value);

    JSON_Value *root_array_value    = json_value_init_array();
    JSON_Array *root_array          =  json_value_get_array(root_array_value);

    JSON_Value *temp_value          = json_value_init_object();
     
    JSON_Value * Payload_value      = json_parse_string(stringPayload);
    JSON_Array * actions_array      = json_object_get_array(json_object(Payload_value),KEY_ACTIONS);
    int state_scene                 = json_object_get_number(json_object(Payload_value),KEY_STATE);
    if(state_scene == KEY_SCENE_STATE_OFF)
    {
        return false;
    }
    size_array_actions              = json_array_get_count(actions_array);
    for(i = 0;i < size_array_actions; i++) //get all actions
    {
        JSON_Value *tmp =  json_array_get_value(actions_array,i);
        const char* entityID = json_object_get_string(json_object(tmp), KEY_ENTITY_ID);
        JSON_Object* executorProperty = json_object_get_object(json_object(tmp), KEY_PROPERTY_EXECUTOR);
        if(isMatchString(entityID,KEY_DELAY))// type delay
        {
            const char *minutes = json_object_get_string(executorProperty,KEY_MINUTES);
            const char *seconds = json_object_get_string(executorProperty,KEY_SECONDS);
            int sec_delay_ = Min2Sec(minutes,seconds);

            char *dotString = malloc(50);
            sprintf(dotString,"%s.%s",KEY_DICT_DPS,KEY_DELAY);
            json_object_dotset_number(json_object(temp_value),dotString,sec_delay_);
            free(dotString);

            json_object_set_number(json_object(temp_value),KEY_PROVIDER,HOMEGY_DELAY);
            json_object_set_string(json_object(temp_value),KEY_DEVICE_ID,json_object_get_string(json_object(tmp), KEY_ENTITY_ID));

            json_array_append_value(root_array,json_parse_string(json_serialize_to_string_pretty(temp_value)));
        }
        else // type device
        {
            if(isMatchString(json_object_get_string(json_object(tmp),KEY_ACT_EXECUTOR),KEY_DP_ISSUE))
            {
                JSON_Status status = json_object_set_value (json_object(temp_value),KEY_DICT_DPS,json_parse_string(json_serialize_to_string_pretty(json_object_get_value(json_object(tmp), KEY_PROPERTY_EXECUTOR))));
                json_object_set_number(json_object(temp_value),KEY_PROVIDER,HOMEGY_BLE);
                json_object_set_string(json_object(temp_value),KEY_DEVICE_ID,json_object_get_string(json_object(tmp), KEY_ENTITY_ID));
            }
            json_array_append_value(root_array,json_parse_string(json_serialize_to_string_pretty(temp_value)));
        }
    }

    JSON_Status status_t = json_object_set_value(result_object,json_object_get_string(json_object(Payload_value),MOSQ_Id),root_array_value);
    char *serialized_string = NULL;
    serialized_string = json_serialize_to_string_pretty(result_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);

    // json_free_serialized_string(serialized_string);
    // json_value_free(root_array_value);
    // free(input);
    // json_value_free(temp_value);
    return true;
}

bool getPayloadActionScene(char **result,char *result_actionScene,const char *sceneID)
{
    JSON_Value  *result_value       = json_parse_string(result_actionScene);

    JSON_Array  *array_tmp =  json_object_get_array(json_object(result_value),sceneID);
    if(array_tmp !=NULL)
    {
        JSON_Value *array_value_tmp = json_array_get_value(array_tmp,0);
        char *serialized_string = NULL;
        serialized_string = json_serialize_to_string_pretty(array_value_tmp);
        int size_t = strlen(serialized_string);
        *result = malloc(size_t+1);
        memset(*result,'\0',size_t+1);
        strcpy(*result,serialized_string);
        json_free_serialized_string(serialized_string);
        json_value_free(array_value_tmp);
        return true;
    }
    else
    {
        return false;
    }
}

bool getArrayConditionScene(char **result,const char *stringPayload,const JSON_Value *object_devices)
{
    int size_array_condition = 0,i=0;
    int size_t_1 = strlen(*result);
    char *input = malloc(size_t_1+1);
    memset(input,'\0',size_t_1+1);
    strcpy(input,*result);

    JSON_Value  *result_value           = json_parse_string(input);
    JSON_Object *result_object          = json_value_get_object(result_value);

    JSON_Value *root_value              = json_value_init_object(); 

    JSON_Value * Payload_value          = json_parse_string(stringPayload);
    int state_scene                     = json_object_get_number(json_object(Payload_value),KEY_STATE);
    if(state_scene == KEY_SCENE_STATE_OFF)
    {
        return false;
    }

    JSON_Array * condition_array        = json_object_get_array(json_object(Payload_value),KEY_CONDITIONS);
    size_array_condition                = json_array_get_count(condition_array);
    for(i = 0;i < size_array_condition; i++)
    {
        char *dotString;
        JSON_Value *tmp =  json_array_get_value(condition_array,i);

        const char* entityID = json_object_get_string(json_object(tmp), KEY_ENTITY_ID);
        if(isMatchString(entityID,KEY_TIMER))// type timer
        {
            JSON_Object *expr   = json_object_get_object(json_object(tmp), KEY_EXPR);
            const char  *date   = json_object_get_string(expr,KEY_DATE);
            const char  *loops  = json_object_get_string(expr,KEY_LOOP_PRECONDITION);
            const char  *time   = json_object_get_string(expr,KEY_TIME);

            dotString     = malloc(50);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_TIME);
            json_object_dotset_string(json_object(root_value),dotString,time);
            free(dotString); 

            dotString = malloc(50);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_DATE);
            json_object_dotset_string(json_object(root_value),dotString,date);
            free(dotString); 

            dotString = malloc(50);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_LOOP_PRECONDITION);
            json_object_dotset_string(json_object(root_value),dotString,loops);
            free(dotString);

            dotString = malloc(50);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,KEY_TIMER,KEY_TIMER,KEY_STATE);
            json_object_dotset_number(json_object(root_value),dotString,0);
            free(dotString);
        }
        else
        {
            JSON_Array* expr    = json_object_get_array(json_object(tmp), KEY_EXPR);
            char *dpid_         = get_dpid(json_array_get_string(expr,0));
            char *type_         = (char *)json_array_get_string(expr,1);
            char *state_        = json_serialize_to_string_pretty(json_array_get_value(expr,2));

            char *deviceID_     = entityID;
            // dotString    = malloc(50);
            // sprintf(dotString,"%s.%s.%s",deviceID_,KEY_DICT_META,dpid_);
            // char *address       = (char *)json_object_dotget_string(json_object(object_devices),dotString);
            // free(dotString);

            dotString = malloc(100);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,deviceID_,dpid_,KEY_DP_ID);
            json_object_dotset_string(json_object(root_value),dotString,dpid_);

            free(dotString);
            dotString = malloc(100);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,deviceID_,dpid_,KEY_TYPE);
            json_object_dotset_string(json_object(root_value),dotString,type_);

            free(dotString);
            dotString = malloc(100);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,deviceID_,dpid_,KEY_DP_VAL);
            json_object_dotset_string(json_object(root_value),dotString,state_);

            free(dotString);
            dotString = malloc(100);
            sprintf(dotString,"%s.%s.%s.%s",KEY_DICT_META,deviceID_,dpid_,KEY_STATE);
            json_object_dotset_number(json_object(root_value),dotString,0);
            free(dotString);

        }

        dotString = malloc(50);
        sprintf(dotString,"%s.%s",KEY_DICT_INFO,KEY_STATE);
        json_object_dotset_number(json_object(root_value),dotString,0);
        free(dotString);

        dotString = malloc(50);
        sprintf(dotString,"%s.%s",KEY_DICT_INFO,KEY_TYPE_SCENE);
        json_object_dotset_string(json_object(root_value),dotString,json_object_get_string(json_object(Payload_value),KEY_TYPE_SCENE));
        free(dotString);
    }

    JSON_Status status_t = json_object_set_value(result_object,json_object_get_string(json_object(Payload_value),MOSQ_Id), root_value);

    char *serialized_string = NULL;
    serialized_string = json_serialize_to_string_pretty(result_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);

    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    free(input);
    // json_value_free(result_value); //need free
    return true;
}

bool getCollectCondition(char **result,const char *stringPayload,const JSON_Value *object_devices)
{
    int size_array_condition = 0,i=0;

    int size_t_1 = strlen(*result);
    char *input = malloc(size_t_1+1);
    memset(input,'\0',size_t_1+1);
    strcpy(input,*result);
    JSON_Value  *result_value       = json_parse_string(input);
    JSON_Object *result_object      = json_value_get_object(result_value);


    JSON_Value * Payload_value      = json_parse_string(stringPayload);
    int state_scene                 = json_object_get_number(json_object(Payload_value),KEY_STATE);
    if(state_scene == KEY_SCENE_STATE_OFF)
    {
        return false;
    }
    JSON_Array * condition_array        = json_object_get_array(json_object(Payload_value),KEY_CONDITIONS);
    size_array_condition                = json_array_get_count(condition_array);

    for(i = 0;i < size_array_condition; i++)
    {
        JSON_Value *tmp =  json_array_get_value(condition_array,i);
        const char* entityID = json_object_get_string(json_object(tmp), KEY_ENTITY_ID);

        if(isMatchString(entityID,KEY_TIMER))// type timer
        {
            JSON_Object *expr = json_object_get_object(json_object(tmp), KEY_EXPR);
            const char *date = json_object_get_string(expr,KEY_DATE);
            const char *loops = json_object_get_string(expr,KEY_LOOP_PRECONDITION);
            const char *time = json_object_get_string(expr,KEY_TIME);


            //add to key timer into CollectCondition
            JSON_Array * root_array        = json_object_get_array(result_object,KEY_TIMER);
            if(root_array == NULL)
            {
                JSON_Value *root_value    = json_value_init_array();
                json_array_append_string(json_array(root_value),json_object_get_string(json_object(Payload_value),MOSQ_Id));
                json_object_set_value(result_object,KEY_TIMER,root_value);
            }
            else
            {
                json_array_append_string(root_array,json_object_get_string(json_object(Payload_value),MOSQ_Id));
            }            
        }
        else
        {
            JSON_Array* expr = json_object_get_array(json_object(tmp), KEY_EXPR);
            char *dpid_ = get_dpid(json_array_get_string(expr,0));
            char *type_ = (char *)json_array_get_string(expr,1);
            char *state_ = json_serialize_to_string_pretty(json_array_get_value(expr,2));
            char *deviceID_ = entityID;

            char *dotString = malloc(50);
            sprintf(dotString,"%s.%s",deviceID_,dpid_);
            JSON_Array * root_array        = json_object_dotget_array(result_object,dotString);
            if(root_array == NULL)
            {
                JSON_Value *root_value    = json_value_init_array();
                json_array_append_string(json_array(root_value),json_object_get_string(json_object(Payload_value),MOSQ_Id));
                json_object_dotset_value(result_object,dotString,root_value);
            }
            else
            {
                json_array_append_string(root_array,json_object_get_string(json_object(Payload_value),MOSQ_Id));
            }
            free(dotString);
        } 
    }

    char *serialized_string = NULL;
    serialized_string = json_serialize_to_string_pretty(result_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    free(input);
    return true;
}

char *get_dpid(const char *code)
{
    return (char *)code+3;
}

bool getInforDayFromScene(struct infor_day *t,const char *time)
{
    char day[3] = {'\0'};
    char month[3] = {'\0'};
    char year[5] = {'\0'};

    year[0] = time[0];
    year[1] = time[1];
    year[2] = time[2];
    year[3] = time[3];
    month[0] = time[4];
    month[1] = time[5];
    day[0] = time[6];
    day[1] = time[7];

    strcpy(t->day,day);
    strcpy(t->month,month);
    strcpy(t->year,year);


}

bool getInforTimeFromScene(struct infor_time *t,const char *time)
{
    char hour_[3] = {'\0'};
    char min_[3] = {'\0'};


    int index = strlen(time);
    int leng_hour = 0, leng_min = 0;
    int i = 0;
    for(i=0;i<index;++i)
    {
        if(time[i] == ':')
        {
            leng_hour = i;
        }
    }
    leng_min = index - leng_hour - 1;

    if(leng_hour == 1)
    {
        hour_[0] = time[0];
        if(leng_min == 1)
        {
            min_[0] = time[2];
        }
        else
        {
            min_[0] = time[2];
            min_[1] = time[3];
        }
    }
    else
    {
        hour_[0] = time[0];
        hour_[1] = time[1];
        if(leng_min == 1)
        {
            min_[0] = time[3];
        }
        else
        {
            min_[0] = time[3];
            min_[1] = time[4];
        }        
    }

    strcpy(t->hour,hour_);
    strcpy(t->min,min_);
}

bool check_day_with_scene(const int dayOfWeek,const char *day_)
{
    if(day_[dayOfWeek-1] == '1')
    {
        return true;
    }
    return false;
}



void insertDataActionsScene(InfoActionsScene InfoActionsScene_t[], long long TimeCreat,  int *n, char *payload)
{
    strcpy(InfoActionsScene_t[*n].payload,payload);
    InfoActionsScene_t[*n].TimeCreat = TimeCreat;
    (*n)++;
}

void deleteDataActionsScene(InfoActionsScene InfoActionsScene_t[], int *n,int index)
{
    int i = 0;
    for(i = 0; i < *n - index; i++)
    {
        InfoActionsScene_t[index + i]   =   InfoActionsScene_t[index + i + 1];       
    }

    (*n)--;
}

int getCountActionWithTimeStemp(InfoActionsScene InfoActionsScene_t[], int n, long long int TimeCreat)
{
    int i = 0, count = 0;
    for(i = 0; i < n; i++)
    {
        if(InfoActionsScene_t[i].TimeCreat <= TimeCreat)
        {
            count ++;
        }
    }
    return count;    
}


void printDataActionsScene(InfoActionsScene InfoActionsScene_t[], int n)
{
    for(int i=0; i<n; i++)
    {
        printf("%lld \t %s\n",InfoActionsScene_t[i].TimeCreat,InfoActionsScene_t[i].payload);
    }
}


void BubleSort(InfoActionsScene InfoActionsScene_t[], int n)
{//thuat toan sap xep noi bot
    int i = 0;
    for(i=0;i<n-1;i++)
    {
        int j = 0;
        for(j=n-1;j>i;j--)
        {
            if(InfoActionsScene_t[j].TimeCreat < InfoActionsScene_t[j-1].TimeCreat)
            {
                InfoActionsScene tg     =   InfoActionsScene_t[j];
                InfoActionsScene_t[j]   =   InfoActionsScene_t[j-1];
                InfoActionsScene_t[j-1] =   tg;
            }        
        }    
    }   
    // printDataActionsScene(InfoActionsScene_t,n);
}

// int BinarySearch(InfoReponse InfoReponse_t[], int n, long long TimeCreat)
// {//thuat toan tim kiem nhi phan
//     int left=0, right=n-1;// left = 0 la vi tri dau mang, right = n-1 la vi tri cuoi mang
//     int mid;//phan tu dung de gan vi tri giua mang
//     do
//     {
//         mid=(left+right)/2;
//         if (InfoReponse_t[mid].TimeCreat==TimeCreat)//neu vi tri giua mang co DTK = x thi tra ve mid
//             return mid;
//         else if (InfoReponse_t[mid].TimeCreat < TimeCreat)//neu neu vi tri giua mang co DTK < x thì left = mid + 1
//             left = mid + 1;
//         else
//             right = mid-1;//neu neu vi tri giua mang co DTK > x thì right = mid-1
//     }while(left<=right);//neu con phan tu trong day hien hanh
//     return -1;//khong co phan tu can tim
// }

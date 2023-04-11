#include"core_data_process.h"
#include"core_process_t.h"

//////////////////////////////////////DATABASE PROCESS//////////////////////////////////

bool addNewDevice(sqlite3 **db, JSON* packet)
{
    JSON* protParam = JSON_GetObject(packet, "protocol_para");
    JSON* dictMeta = JSON_GetObject(protParam, "dictMeta");
    JSON* deviceInfo = JSON_CreateObject();
    char* deviceId = JSON_GetText(packet, KEY_DEVICE_ID);
    int pageIndex = JSON_GetNumber(packet, "pageIndex");
    JSON_SetText(deviceInfo, KEY_DEVICE_ID, deviceId);
    JSON_SetText(deviceInfo, KEY_NAME, JSON_GetText(packet, KEY_NAME));
    JSON_SetText(deviceInfo, KEY_UNICAST, JSON_GetText(protParam, KEY_UNICAST));
    JSON_SetText(deviceInfo, KEY_ID_GATEWAY, JSON_GetText(protParam, KEY_ID_GATEWAY));
    JSON_SetText(deviceInfo, KEY_DEVICE_KEY, JSON_GetText(protParam, KEY_DEVICE_KEY));
    JSON_SetText(deviceInfo, KEY_PID, JSON_GetText(protParam, KEY_PID));
    JSON_SetNumber(deviceInfo, KEY_PROVIDER, JSON_GetNumber(protParam, KEY_PROVIDER));
    JSON_SetNumber(deviceInfo, "pageIndex", pageIndex);
    Db_AddDevice(deviceInfo);

    JSON_ForEach(dp, dictMeta) {
        int dpId = atoi(dp->string);
        Db_AddDp(deviceId, dpId, dp->valuestring, pageIndex);
    }

    JSON_Delete(deviceInfo);
    return true;
}

bool delDevice(sqlite3 **db,const char *deviceID)
{
    if(deviceID == NULL)
    {
        return false;
    }

    sql_deleteDeviceInTable(db,TABLE_DEVICES,KEY_DEVICE_ID,deviceID);
    sql_deleteDeviceInTable(db,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);

    return true;
}


/*
Lay SceneList cua thiet bi trong database
Duyet qua cac sceneID trong SceneList
    Lay action cua sceneID, xoa DeviceID ra khoi action va cap nhat len database
    Lay condition cua sceneID, xoa DeviceID ra khoi condition va cap nhat len database
Ham tra ve Danh sach cac sceneID da update

*/
char *delDeviceIntoGroups(sqlite3 **db,const char *deviceID)
{
    if(deviceID == NULL)
    {
        return NULL;
    }
    bool check_flag = false;
    int size_groupList_DB = 0,count_elementGroupAddress=0,count_Devices_DB = 0;
    char **GroupList_DB;
    char **devices_DB;

    //get SceneList from DataBase with condition : scenenID
    GroupList_DB = sql_getValueWithCondition(db,&size_groupList_DB,KEY_GROUP_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
    if(size_groupList_DB < 1)
    {
        return NULL;
    }
    char *result = (char*)calloc(10,sizeof(char));
    char *groupAddress_tmp = (char*)calloc(LENGTH_GROUP_ADDRESS+1,sizeof(char));
    //char *deviceID_DB = (char*)calloc(LENGTH_DEVICE_ID+1,sizeof(char));

    LogInfo((get_localtime_now()),("[delDeviceIntoGroups] GroupList_DB = %s",GroupList_DB[0]));
    JSON_Array *ArrayGroup =  json_value_get_array(json_parse_string_with_comments(GroupList_DB[0]));
    count_elementGroupAddress  = json_array_get_count(ArrayGroup);
    LogInfo((get_localtime_now()),("[delDeviceIntoGroups] GroupList_DB = %s",GroupList_DB[0]));

    //browse the elements in the list
    for (size_t i = 0; i < count_elementGroupAddress; i++)
    {
        groupAddress_tmp = NULL;
        groupAddress_tmp = json_array_get_string(ArrayGroup,i);
        LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    count_DevgroupAddress_tmpices_DB = %s\n",groupAddress_tmp));

        //get devices with groupAddress into GROUP_INF
        devices_DB = sql_getValueWithCondition(db,&count_Devices_DB,KEY_DEVICES_GROUP,TABLE_GROUP_INF,KEY_ADDRESS_GROUP,groupAddress_tmp);
        LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    count_Devices_DB = %d\n",count_Devices_DB));
        if(count_Devices_DB < 1)
        {
            LogError((get_localtime_now()),("[delDeviceIntoGroups] Failed to get devices from TABLE_GROUP_INF at groupAddress = %s",groupAddress_tmp));     
        }
        else
        {
            LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    devices_DB = %s\n",devices_DB[0]));

            //check deviceID have into devices 
            if(isContainString(devices_DB[0],deviceID))
            {
                char *remove_DeviceID = (char *)calloc(strlen(deviceID)+7,sizeof(char));
                strcpy(remove_DeviceID, deviceID);
                strcat(remove_DeviceID, "|");
                int index = findSubstring(devices_DB[0],remove_DeviceID);
                LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    index = %d\n",index));
                index = index + strlen(remove_DeviceID);
                LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    index = %d\n",index));
                LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    devices_DB[0][index] = %c\n",devices_DB[0][index]));

                int index_remove = strlen(remove_DeviceID);
                remove_DeviceID[index_remove] = devices_DB[0][index];

                strcat(remove_DeviceID, "|");
                LogInfo((get_localtime_now()),("[delDeviceIntoGroups]    remove_DeviceID = %s\n",remove_DeviceID));


                check_flag = sql_updateValueInTableWithCondition(db,TABLE_GROUP_INF,
                                                    KEY_DEVICES_GROUP, strremove(devices_DB[0],remove_DeviceID),
                                                    KEY_ADDRESS_GROUP,groupAddress_tmp);
                free(remove_DeviceID);
                if(!check_flag)
                {
                    LogError((get_localtime_now()),("[delDeviceIntoGroups] Failed to update TABLE_GROUP_INF at groupAddress = %s",groupAddress_tmp));
                }
                else
                {
                    LogInfo((get_localtime_now()),("[delDeviceIntoGroups]UPDATE TABLE_GROUP_INF at groupAddress = %s",groupAddress_tmp));
                    append_String2String(&result,groupAddress_tmp);
                    append_String2String(&result,"|");
                }
            }
            free_fields(devices_DB,count_Devices_DB);
        }
    }
    
    free(groupAddress_tmp);
    return result;
}

/*
Lay SceneList cua thiet bi trong database
Duyet qua cac sceneID trong SceneList
    Lay action cua sceneID, xoa DeviceID ra khoi action va cap nhat len database
    Lay condition cua sceneID, xoa DeviceID ra khoi condition va cap nhat len database
Ham tra ve Danh sach cac sceneID da update

*/
char *delDeviceIntoScenes(sqlite3 **db,const char *deviceID)
{
    char *result = (char*)calloc(10,sizeof(char));


    JSON_Value *object = NULL;
    JSON_Object *object_tmp = NULL;
    JSON_Array *ArrayActions = NULL;
    JSON_Array *ArrayConditions = NULL;
    bool check_flag_action = false,check_flag_condition = false;

    int leng = 0,count = 0,index = 0,i=0,j=0,size_action =0,size_condition=0;
    char *sceneId_tmp = (char*)calloc(LENGTH_SCENE_ID+1,sizeof(char));
    char *deviceID_DB = (char*)calloc(LENGTH_DEVICE_ID+1,sizeof(char));


    char **SceneList_DB;
    char **action_DB;
    char **condition_DB;

    //get SceneList from DataBase with condition : scenenID
    SceneList_DB = sql_getValueWithCondition(db,&leng,KEY_SCENE_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
    if(leng < 1)
    {
        LogError((get_localtime_now()),("Failed to get SceneList of deviceID : %s\n",deviceID));
        free(sceneId_tmp);
        free(deviceID_DB);
        return result;
    }

    //array access for each sceneID
    JSON_Array *ArrayScene =  json_value_get_array(json_parse_string_with_comments(SceneList_DB[0]));
    index = json_array_get_count(ArrayScene);
    LogInfo((get_localtime_now()),("    count  ArrayScene = : %d\n",index));
    for( i = 0;i<index;i++)
    {
        sceneId_tmp = NULL;
        sceneId_tmp = json_array_get_string(ArrayScene,i);


        //get condition with sceneID
        check_flag_condition = false;
        condition_DB = sql_getValueWithCondition(db,&size_condition,KEY_CONDITIONS,TABLE_SCENE_INF,KEY_ID_SCENE,sceneId_tmp);
        LogInfo((get_localtime_now()),("    size_condition = %d\n",size_condition));
        if(size_condition < 1)
        {
            return result;
        }
        LogInfo((get_localtime_now()),("        condition = %s\n",condition_DB[0]));
        object = json_parse_string_with_comments(condition_DB[0]);
        ArrayConditions = json_value_get_array(object);
        count = json_array_get_count(ArrayConditions);
        LogInfo((get_localtime_now()),("        count = %d\n",count));
        for(j=0;j<count;j++)
        {
            deviceID_DB = NULL;
            object_tmp = json_array_get_object(ArrayConditions,j);
            if(object_tmp == NULL)
            {
                printf("    object_tmp = NULL\n");
            }
            deviceID_DB = json_object_get_string(object_tmp,KEY_ENTITY_ID);
            if(deviceID_DB == NULL)
            {
                LogError((get_localtime_now()),("            deviceID_DB %d is NULL\n",j));
            }
            LogInfo((get_localtime_now()),("            deviceID_DB %d = %s\n",j, deviceID_DB));
            if(isMatchString(deviceID_DB,deviceID))
            {
                json_array_remove(ArrayConditions,j);
                check_flag_condition = true;
                --j;
                --count;
            }
        }
        //check for update ArrayConditions for scene
        if(check_flag_condition)
        {
            sql_updateValueInTableWithCondition(db,TABLE_SCENE_INF,
                                                KEY_CONDITIONS,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayConditions)),
                                                KEY_ID_SCENE,sceneId_tmp);
        }


        //get action with sceneID
        check_flag_action = false;
        action_DB = sql_getValueWithCondition(db,&size_action,KEY_ACTIONS,TABLE_SCENE_INF,KEY_ID_SCENE,sceneId_tmp);
        LogInfo((get_localtime_now()),("    size_action = %d\n",size_action));
        if(size_action < 1)
        {
            return result;
        }
        LogInfo((get_localtime_now()),("        action = %s\n",action_DB[0]));
        object = json_parse_string_with_comments(action_DB[0]);
        ArrayActions = json_value_get_array(object);
        count = json_array_get_count(ArrayActions);
        LogInfo((get_localtime_now()),("        count = %d\n",count));
        for(j=0;j<count;j++)
        {
            deviceID_DB = NULL;
            object_tmp = json_array_get_object(ArrayActions,j);
            if(object_tmp == NULL)
            {
                printf("    object_tmp = NULL\n");
            }
            deviceID_DB = json_object_get_string(object_tmp,KEY_ENTITY_ID);
            if(deviceID_DB == NULL)
            {
                LogError((get_localtime_now()),("            deviceID_DB %d is NULL\n",j));
            }
            LogInfo((get_localtime_now()),("            deviceID_DB %d = %s\n",j, deviceID_DB));
            if(isMatchString(deviceID_DB,deviceID))
            {
                json_array_remove(ArrayActions,j);
                check_flag_action = true;
                --j;
                --count;
            }
        }

        //check for update ArrayActions for scene
        if(check_flag_action)
        {
            if(json_array_get_count(ArrayActions) )
            {

            }
            else
            {
                sql_updateValueInTableWithCondition(db,TABLE_SCENE_INF,
                                                    KEY_ACTIONS,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayActions)),
                                                    KEY_ID_SCENE,sceneId_tmp);
            }
        }

        LogInfo((get_localtime_now()),("        size_action = %d;\n",size_action));
        if(action_DB != NULL)
        {
            free_fields(action_DB,size_action);
        }
        LogInfo((get_localtime_now()),("        size_condition = %d;\n",size_condition));
        if(condition_DB != NULL)
        {
            free_fields(condition_DB,size_condition);
        }

        if(check_flag_action || check_flag_condition)
        {
            append_String2String(&result,sceneId_tmp);
            append_String2String(&result,"|");
        }
    }

    // LogInfo((get_localtime_now()),("    free(sceneId_tmp)\n"));
    // free(sceneId_tmp);
    // LogInfo((get_localtime_now()),("    free(deviceID_DB)\n"));
    // free(deviceID_DB);
    LogInfo((get_localtime_now()),("    free_fields(SceneList_DB,leng);\n"));
    free_fields(SceneList_DB,leng);

    return result;
}

bool addSceneInfoToDatabase(sqlite3 **db,const char *object_)
{
    size_t count = 0;
    int leng=0,index=0;
    bool check_flag = false;
    JSON_Value *object = NULL;
    JSON_Object *object_tmp = NULL;
    JSON_Array *ArrayActions = NULL;
    JSON_Array *ArrayConditions = NULL;
    JSON_Array *ArrayTmp = NULL;
    char *deviceID;
    char **ListDevice;

    object = json_parse_string(object_);
   
    const char* sceneId     = json_object_get_string(json_object(object), MOSQ_Id);
    int isLocal             = (int)json_object_get_number(json_object(object), KEY_IS_LOCAL);
    int state               = (int)json_object_get_number(json_object(object), KEY_STATE);
    const char* name        = json_object_get_string(json_object(object), KEY_NAME);
    const char* sceneType   = json_object_get_string(json_object(object), KEY_TYPE_SCENE);
    const char* actions     = (char*)json_serialize_to_string_pretty(json_object_get_value(json_object(object), KEY_ACTIONS));
    const char* conditions  = (char*)json_serialize_to_string_pretty(json_object_get_value(json_object(object), KEY_CONDITIONS));
    int created             = 1111;
    int last_updated        = 2222;

    check_flag = sql_insertDataTableSceneInf(db,sceneId,isLocal,state,name,sceneType,actions,conditions,created,last_updated);
    if(!check_flag)
    {
        printf("sql_insertDataTableSceneInf failed \n");
        return false;
    }



    printf("debug\n");
    ArrayActions    = json_object_get_array(json_object(object), KEY_ACTIONS);
    ArrayConditions = json_object_get_array(json_object(object), KEY_CONDITIONS);
    if(ArrayConditions == NULL)
    {
        printf("ArrayConditions = NULL\n");
    }


    count = json_array_get_count(ArrayConditions);
    printf("\n\n\ncount ArrayConditions = %d\n",count);
    object_tmp = NULL; 
    for(index=0;index<count;index++)
    {
        deviceID = calloc(100,sizeof(char));
        printf("\n\n\n    index = %d , count =  %d\n",index,count);

        leng = 0;
        object_tmp = json_array_get_object(ArrayConditions,index);
        if(object_tmp == NULL)
        {
            printf("    object_tmp = NULL\n");
        }
        deviceID = json_object_get_string(object_tmp,KEY_ENTITY_ID);
        if(deviceID == NULL)
        {
            printf("    deviceID = NULL\n");
        }
        printf("    deviceID %d = %s\n",index, deviceID);
        ListDevice = sql_getValueWithCondition(db,&leng,KEY_SCENE_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
        printf("    ListDevice = %s\n",ListDevice[0]);
        printf("    leng = %d\n",leng);
        if(leng < 1)
        {
            break;
        }
        ArrayTmp = json_value_get_array(json_parse_string_with_comments(ListDevice[0]));
        if(ArrayTmp == NULL)
        {           
            break;
        }
        else
        {
            printf("    ArrayTmp != NULL\n");
            int size_array = json_array_get_count(ArrayTmp);
            printf("    size_array = %d\n",size_array);
            int j  = 0;
            if(size_array == 0)
            {
                json_array_append_string(ArrayTmp,sceneId);
            }
            else
            {
                check_flag = true;
                for(j=0;j<size_array;j++)
                {
                    char *checkAddress = json_array_get_string(ArrayTmp,j);
                    printf("    checkAddress = %s\n",checkAddress);
                    if(isMatchString(checkAddress,sceneId))
                    {
                        printf("    checkAddress == sceneId\n");
                        check_flag = false;
                        break;
                    }
                }
                if(check_flag)
                {
                   json_array_append_string(ArrayTmp,sceneId); 
                }
            }
        }
        sql_updateValueInTableWithCondition(db,TABLE_DEVICES_INF,
                                            KEY_SCENE_LIST,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayTmp)),
                                            KEY_DEVICE_ID,deviceID);
        free_fields(ListDevice,leng);
        free(deviceID);
    }

    count = json_array_get_count(ArrayActions);
    printf("\n\n\ncount ArrayActions = %d\n",count);
    object_tmp = NULL;
    for(index=0;index<count;index++)
    {
        deviceID = calloc(100,sizeof(char));
        printf("\n\n\n    index = %d , count =  %d\n",index,count);

        leng = 0;
        object_tmp = json_array_get_object(ArrayActions,index);
        if(object_tmp == NULL)
        {
            printf("    object_tmp = NULL\n");
        }
        deviceID = json_object_get_string(object_tmp,KEY_ENTITY_ID);
        if(deviceID == NULL)
        {
            printf("    deviceID = NULL\n");
        }
        printf("    deviceID %d = %s\n",index, deviceID);



        ListDevice = sql_getValueWithCondition(db,&leng,KEY_SCENE_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
        printf("    ListDevice = %s\n",ListDevice[0]);
        printf("    leng = %d\n",leng);
        if(leng < 1)
        {
            break;
        }
        ArrayTmp = json_value_get_array(json_parse_string_with_comments(ListDevice[0]));
        if(ArrayTmp == NULL)
        {           
            break;
        }
        else
        {
            printf("    ArrayTmp != NULL\n");
            int size_array = json_array_get_count(ArrayTmp);
            printf("    size_array = %d\n",size_array);
            int j  = 0;
            if(size_array == 0)
            {
                json_array_append_string(ArrayTmp,sceneId);
            }
            else
            {
                check_flag = true;
                for(j=0;j<size_array;j++)
                {
                    char *checkAddress = json_array_get_string(ArrayTmp,j);
                    printf("    checkAddress = %s\n",checkAddress);
                    if(isMatchString(checkAddress,sceneId))
                    {
                        printf("    checkAddress == sceneId\n");
                        check_flag = false;
                        break;
                    }
                }
                if(check_flag)
                {
                   json_array_append_string(ArrayTmp,sceneId); 
                }
            }
        }
        sql_updateValueInTableWithCondition(db,TABLE_DEVICES_INF,
                                            KEY_SCENE_LIST,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayTmp)),
                                            KEY_DEVICE_ID,deviceID);
        free_fields(ListDevice,leng);
        free(deviceID);
    }

    print_database(db);
    return true;
}



bool DeleteSceneInDatabase(sqlite3 **db,const char *sceneID)
{
    if(sceneID == NULL )
    {
        printf("sceneID is NULL \n");
        return false;        
    }  

    bool check_flag = false;

    //remove sceneID into table SCENE_INF of Database
    check_flag = sql_deleteDeviceInTable(db,TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
    if(!check_flag)
    {
        printf("sql_deleteDeviceInTable failed \n");
        return false;
    }
    return true;
}

bool deleteSceneListOfDeviceFromSceneID(sqlite3 **db,const char *sceneID)
{
   if(sceneID == NULL )
    {
        printf("sceneID is NULL \n");
        return false;        
    }  

    size_t count = 0;

    JSON_Object *object_tmp = NULL;
    JSON_Array *ArrayTmp = NULL;

    char *deviceID;
    JSON_Value *object = NULL;
    JSON_Array *ArrayActions = NULL;
    JSON_Array *ArrayConditions = NULL;


    int size_action = 0,size_condition = 0,size_ListDevice = 0,i=0,j=0;
    bool check_flag = false;
    char **ListDevice;
    char **action;
    char **condition;

    action = sql_getValueWithCondition(db,&size_action,KEY_ACTIONS,TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
    if(size_action < 1)
    {
        return false;
    }
    printf("    size_action = %d\n",size_action);
    printf("    action = %s\n",action[0]);


    condition = sql_getValueWithCondition(db,&size_condition,KEY_CONDITIONS,TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
    if(size_condition < 1)
    {
        return false;
    }
    printf("    size_condition = %d\n",size_condition);
    printf("    condition = %s\n",condition[0]);

    //remove info sceneID contai SceneList of Device
    object = json_parse_string_with_comments(action[0]);
    ArrayActions = json_value_get_array(object);
    size_action = json_array_get_count(ArrayActions);
    for(i=0;i<size_action;i++)
    {
        deviceID = calloc(LENGTH_DEVICE_ID+1,sizeof(char));
        object_tmp = json_array_get_object(ArrayActions,i);
        if(object_tmp == NULL)
        {
            printf("    object_tmp = NULL\n");
        }
        deviceID = json_object_get_string(object_tmp,KEY_ENTITY_ID);
        if(deviceID == NULL)
        {
            printf("    deviceID = NULL\n");
        }
        printf("    deviceID %d = %s\n",i, deviceID);

        ListDevice = sql_getValueWithCondition(db,&size_ListDevice,KEY_SCENE_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
        printf("    ListDevice = %s\n",ListDevice[0]);
        printf("    size_ListDevice = %d\n",size_ListDevice);
        if(size_ListDevice < 1)
        {
            break;
        }
        ArrayTmp = json_value_get_array(json_parse_string_with_comments(ListDevice[0]));
        if(ArrayTmp == NULL)
        {           
            break;
        }
        else
        {
            printf("    ArrayTmp != NULL\n");
            int size_array = json_array_get_count(ArrayTmp);
            printf("    size_array = %d\n",size_array);
            check_flag = false;
            for(j=0;j<size_array;j++)
            {
                char *checkAddress = json_array_get_string(ArrayTmp,j);
                printf("    checkAddress = %s\n",checkAddress);
                if(isMatchString(checkAddress,sceneID))
                {
                    json_array_remove(ArrayTmp,j);
                    check_flag = true;
                    break;
                }
            }
        }

        //check for update ListDevice for Device
        if(check_flag)
        {
            sql_updateValueInTableWithCondition(db,TABLE_DEVICES_INF,
                                                KEY_SCENE_LIST,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayTmp)),
                                                KEY_DEVICE_ID,deviceID);
        }

        free_fields(ListDevice,size_ListDevice);
        free(deviceID);
    }

    object = json_parse_string_with_comments(condition[0]);
    ArrayConditions = json_value_get_array(object);
    size_condition = json_array_get_count(ArrayConditions);
    for(i=0;i<size_condition;i++)
    {
        deviceID = calloc(LENGTH_DEVICE_ID+1,sizeof(char));
        object_tmp = json_array_get_object(ArrayConditions,i);
        if(object_tmp == NULL)
        {
            printf("    object_tmp = NULL\n");
        }
        deviceID = json_object_get_string(object_tmp,KEY_ENTITY_ID);
        if(deviceID == NULL)
        {
            printf("    deviceID = NULL\n");
        }
        printf("    deviceID %d = %s\n",i, deviceID);

        ListDevice = sql_getValueWithCondition(db,&size_ListDevice,KEY_SCENE_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,deviceID);
        printf("    ListDevice = %s\n",ListDevice[0]);
        printf("    size_ListDevice = %d\n",size_ListDevice);
        if(size_ListDevice < 1)
        {
            break;
        }
        ArrayTmp = json_value_get_array(json_parse_string_with_comments(ListDevice[0]));
        if(ArrayTmp == NULL)
        {           
            break;
        }
        else
        {
            printf("    ArrayTmp != NULL\n");
            int size_array = json_array_get_count(ArrayTmp);
            printf("    size_array = %d\n",size_array);
            check_flag = false;
            for(j=0;j<size_array;j++)
            {
                char *checkAddress = json_array_get_string(ArrayTmp,j);
                printf("    checkAddress = %s\n",checkAddress);
                if(isMatchString(checkAddress,sceneID))
                {
                    json_array_remove(ArrayTmp,j);
                    check_flag = true;
                    break;
                }
            }
        }
}

    return true;
}

bool addNewGroupNormal(sqlite3 **db,const char *object_)
{
    bool check_flag = false,check_flag_update = false;
    JSON_Value *object = NULL;
    JSON_Array *ArrayTmp = NULL;
    object = json_parse_string(object_);


    const char* groupAdress     = json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    const char* name            = json_object_get_string(json_object(object), KEY_NAME);
    const char* pid             = json_object_get_string(json_object(object), KEY_PID);
    const char* devices         = json_object_get_string(json_object(object), KEY_DEVICES_GROUP);
    //check data is NULL?
    if(groupAdress == NULL || name == NULL || pid ==NULL || devices == NULL)
    {
        return false;
    }

    //add info into Table GroupInf
    check_flag = sql_insertDataTableGroupInf(db,groupAdress,TYPE_DEVICE_ONLINE,name,pid,devices);
    if(!check_flag)
    {
        printf("sql_insertDataTableSceneInf failed \n");
        return false;
    }

    //add info into Table Devices
    check_flag = sql_insertDataTableDevices(db,groupAdress, "20", groupAdress, 0, TYPE_DEVICE_ONLINE);
    check_flag = sql_insertDataTableDevices(db,groupAdress, "22", groupAdress, 0, TYPE_DEVICE_ONLINE);
    check_flag = sql_insertDataTableDevices(db,groupAdress, "23", groupAdress, 0, TYPE_DEVICE_ONLINE);
    if(!check_flag)
    {
        printf("sql_insertDataTableDevices failed \n");
    }   
    

    //add groupID to GroupList if devices
    int count_device = 0,i=0,count_GroupList = 0;
    char **deviceId;
    char **GroupList;
    deviceId = str_split(devices,'|',&count_device); // get device from devices
    if(count_device < 1)
    {
        LogError((get_localtime_now()),("Failed to get list device from devices into group!"));
        return false;
    }

    for(i=0;i<count_device-1;i++)
    {
        GroupList = sql_getValueWithCondition(db,&count_GroupList,KEY_GROUP_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(deviceId+i));
        if(count_GroupList != 1)
        {
            LogError((get_localtime_now()),("Failed to get GroupList from deviceID\n"));
            break;
        }
        ArrayTmp = json_value_get_array(json_parse_string_with_comments(GroupList[0]));
        if(ArrayTmp == NULL)
        {           
            break;
        }
        else
        {
            int size_array = json_array_get_count(ArrayTmp);
            int j  = 0;
            if(size_array == 0)
            {
                json_array_append_string(ArrayTmp,groupAdress);
                check_flag_update = true; 
            }
            else
            {
                check_flag = true;
                for(j=0;j<size_array;j++)
                {
                    char *checkAddress = json_array_get_string(ArrayTmp,j);
                    if(isMatchString(checkAddress,groupAdress))
                    {
                        check_flag = false;
                        break;
                    }
                }
                if(check_flag)
                {
                   json_array_append_string(ArrayTmp,groupAdress);
                   check_flag_update = true; 
                }
            }
        }
        if(check_flag_update)
        {
            check_flag_update = false; 
            sql_updateValueInTableWithCondition(db,TABLE_DEVICES_INF,
                                                KEY_GROUP_LIST,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayTmp)),
                                                KEY_DEVICE_ID,*(deviceId+i));            
        }

        free_fields(GroupList,count_GroupList);
    }
    free_fields(deviceId,count_device);
    return true;   
}

bool addNewGroupLink(sqlite3 **db,const char *object_)
{
    bool check_flag = false,check_flag_update = false;
    JSON_Value *object = NULL;
    JSON_Array *ArrayTmp = NULL;

    object = json_parse_string(object_);

    const char* groupAdress     = json_object_get_string(json_object(object), KEY_ADDRESS_GROUP);
    const char* name            = json_object_get_string(json_object(object), KEY_NAME);
    const char* pid             = json_object_get_string(json_object(object), KEY_PID);
    const char* devices         = json_object_get_string(json_object(object), KEY_DEVICES_GROUP);

    if(groupAdress == NULL || name == NULL || pid ==NULL || devices == NULL)
    {
        return false;
    }

    //add info into Table GroupInf
    check_flag = sql_insertDataTableGroupInf(db,groupAdress,TYPE_DEVICE_ONLINE,name,pid,devices);
    if(!check_flag)
    {
        printf("sql_insertDataTableSceneInf failed \n");
        return false;
    }

    //add groupID to GroupList if devices
    int count_device = 0,i=0,count_GroupList = 0;
    char **deviceId;
    char **GroupList;
    deviceId = str_split(devices,'|',&count_device); // get device from devices
    if(count_device < 1)
    {
        LogError((get_localtime_now()),("Failed to get list device from devices into group!"));
        return false;
    }

    for(i=0;i<count_device-1;i=i+2)
    {
        GroupList = sql_getValueWithCondition(db,&count_GroupList,KEY_GROUP_LIST,TABLE_DEVICES_INF,KEY_DEVICE_ID,*(deviceId+i));
        if(count_GroupList != 1)
        {
            LogError((get_localtime_now()),("Failed to get GroupList from deviceID\n"));
            break;
        }
        LogInfo((get_localtime_now()),("    GroupList = %s\n",GroupList[0]));
        LogInfo((get_localtime_now()),("    count_GroupList = %d\n",count_GroupList));
        ArrayTmp = json_value_get_array(json_parse_string_with_comments(GroupList[0]));
        if(ArrayTmp == NULL)
        {           
            break;
        }
        else
        {
            int size_array = json_array_get_count(ArrayTmp);
            LogInfo((get_localtime_now()),("        size_array = %d\n",size_array));
            int j  = 0;
            if(size_array == 0)
            {
                json_array_append_string(ArrayTmp,groupAdress);
                check_flag_update = true; 
            }
            else
            {
                check_flag = true;
                for(j=0;j<size_array;j++)
                {
                    char *checkAddress = json_array_get_string(ArrayTmp,j);
                    LogInfo((get_localtime_now()),("        checkAddress = %s\n",checkAddress));
                    if(isMatchString(checkAddress,groupAdress))
                    {
                        LogInfo((get_localtime_now()),("        checkAddress == groupAdress\n"));
                        check_flag = false;
                        break;
                    }
                }
                if(check_flag)
                {
                   json_array_append_string(ArrayTmp,groupAdress);
                   check_flag_update = true; 
                }
            }
        }
        if(check_flag_update)
        {
            check_flag_update = false; 
            sql_updateValueInTableWithCondition(db,TABLE_DEVICES_INF,
                                                KEY_GROUP_LIST,json_serialize_to_string_pretty(json_array_get_wrapping_value(ArrayTmp)),
                                                KEY_DEVICE_ID,*(deviceId+i));            
        }

        free_fields(GroupList,count_GroupList);
    }
    free_fields(deviceId,count_device);
    return true;   
}

bool getInfoDeviceFromDatabase(sqlite3 **db,char **result)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    char *message;

    char *sql = "SELECT * FROM DEVICES;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    while (sqlite3_step(stmt) != SQLITE_DONE) 
    {
        message = malloc(100);
        sprintf(message,"%s.%s.%s",sqlite3_column_text(stmt,0),KEY_DICT_META,sqlite3_column_text(stmt,1));
        json_object_dotset_string(root_object,(const char *)message,sqlite3_column_text(stmt,2));
        free(message);
    }
    sqlite3_finalize(stmt);

    char *sql_2 = "SELECT * FROM DEVICES_INF;";
    sqlite3_stmt *stmt_2;
    sqlite3_prepare_v2(*db,sql_2, -1, &stmt_2, NULL);
    sqlite3_bind_int (stmt_2, 1, 2);
    while (sqlite3_step(stmt_2) != SQLITE_DONE) 
    {
        message = malloc(100);
        sprintf(message,"%s.%s.%s",sqlite3_column_text(stmt_2,0),KEY_DICT_INFO,KEY_PROVIDER);
        json_object_dotset_number(root_object,message,sqlite3_column_int(stmt_2,7));
        free(message);

        message = malloc(100);
        sprintf(message,"%s.%s.%s",sqlite3_column_text(stmt_2,0),KEY_DICT_INFO,KEY_PID);
        json_object_dotset_string(root_object,message,sqlite3_column_text(stmt_2,8));
        free(message);


        message = malloc(100);
        sprintf(message,"%s.%s.%s",sqlite3_column_text(stmt_2,0),KEY_DICT_INFO,KEY_NAME);
        json_object_dotset_string(root_object,message,sqlite3_column_text(stmt_2,2));
        free(message);

    }
    sqlite3_finalize(stmt_2);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    return true;
}

bool getInfoTypeSceneFromDatabase(sqlite3 **db,char **result,int isLocal)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    char *message = malloc(50);

    char *sql = calloc(100,sizeof(char));
    sprintf(sql,"SELECT * FROM %s WHERE %s = '%d';",TABLE_SCENE_INF,KEY_IS_LOCAL,isLocal);
 
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    while (sqlite3_step(stmt) != SQLITE_DONE) 
    {
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),MOSQ_Id);
        json_object_dotset_string(root_object,(const char *)message,sqlite3_column_text(stmt,0));
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_STATE);
        json_object_dotset_number(root_object,(const char *)message,sqlite3_column_int(stmt,2));
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_TYPE_SCENE);
        json_object_dotset_string(root_object,(const char *)message,sqlite3_column_text(stmt,4));
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_ACTIONS);
        json_object_dotset_value(root_object,(const char *)message,json_parse_string(sqlite3_column_text(stmt,5)));
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_CONDITIONS);
        json_object_dotset_value(root_object,(const char *)message,json_parse_string(sqlite3_column_text(stmt,6)));
    }
    sqlite3_finalize(stmt);
    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    free(message);
    free(sql);
    return true;
}

bool getInfoSceneIdFromDatabase(sqlite3 **db,char **result,char *sceneID)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    char *message = malloc(50);

    char *sql = calloc(100,sizeof(char));
    sprintf(sql,"SELECT * FROM %s WHERE %s = '%s';",TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
 
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    while (sqlite3_step(stmt) != SQLITE_DONE) 
    {
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_TYPE_SCENE);
        json_object_dotset_string(root_object,(const char *)message,sqlite3_column_text(stmt,4));

        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_ACTIONS);
        json_object_dotset_value(root_object,(const char *)message,json_parse_string(sqlite3_column_text(stmt,5)));

        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_CONDITIONS);
        json_object_dotset_value(root_object,(const char *)message,json_parse_string(sqlite3_column_text(stmt,6)));
    }
    sqlite3_finalize(stmt);
    serialized_string = json_serialize_to_string_pretty(root_value);
    LogInfo((get_localtime_now()),("[delDeviceIntoGroups]   serialized_string = %s",serialized_string));

    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    free(message);
    free(sql);
    return true;    
}

bool getInfoGroupFromDatabase(sqlite3 **db,char **result,char *groupAddress)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;
    char *message = malloc(50);

    char *sql = calloc(100,sizeof(char));
    sprintf(sql,"SELECT * FROM %s WHERE %s = '%s';",TABLE_GROUP_INF,KEY_ADDRESS_GROUP,groupAddress);
 
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(*db,sql, -1, &stmt, NULL);
    sqlite3_bind_int (stmt, 1, 2);
    while (sqlite3_step(stmt) != SQLITE_DONE) 
    {
        memset(message,'\0',50);
        sprintf(message,"%s.%s",sqlite3_column_text(stmt,0),KEY_DEVICES_GROUP);
        LogInfo((get_localtime_now()),("[delDeviceIntoGroups]   message = %s",message));

        json_object_dotset_string(root_object,(const char *)message,sqlite3_column_text(stmt,4));
    }
    sqlite3_finalize(stmt);
    serialized_string = json_serialize_to_string_pretty(root_value);
    LogInfo((get_localtime_now()),("[delDeviceIntoGroups]   serialized_string = %s",serialized_string));

    int size_t = strlen(serialized_string);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
    free(message);
    free(sql);
    return true;    
}

bool checkTypeSceneWithInfoIntoDatabase(sqlite3 **db,char *sceneID)
{
    bool check_flag = false;
    int isLocal = 0, size_sceneType =0, size_actions = 0, size_conditions = 0;
    LogInfo((get_localtime_now()),("[checkTypeSceneWithInfoIntoDatabase]"));
    if(sceneID == NULL)
    {
        LogInfo((get_localtime_now()),("[checkTypeSceneWithInfoIntoDatabase] sceneID == NULL"));
        return false;
    }
    char **conditions;
    char **sceneType;
    char **actions      = sql_getValueWithCondition(db,&size_actions,KEY_ACTIONS,TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
    if(size_actions < 1)
    {
        return false;
    }
    LogInfo((get_localtime_now()),("[checkTypeSceneWithInfoIntoDatabase] strlen(actions[0]) = %ld",strlen(actions[0])));
    if(strlen(actions[0]) <= 2)
    {
        //delete sceneID
        check_flag = DeleteSceneInDatabase(db,sceneID);
        if(!check_flag)
        {
            LogError((get_localtime_now()),("[checkTypeSceneWithInfoIntoDatabase] Failed to delete sceneID %s from database",sceneID));
        }
        free_fields(actions,size_actions);
        return true;
    }
    sceneType    = sql_getValueWithCondition(db,&size_sceneType,KEY_TYPE_SCENE,TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
    if(size_sceneType < 1)
    {
        return false;
    }    
    if(isMatchString(sceneType[0],TYPE_OneOfAll_SCENE) || isMatchString(sceneType[0],TYPE_All_SCENE))
    {
        conditions   = sql_getValueWithCondition(db,&size_conditions,KEY_CONDITIONS,TABLE_SCENE_INF,KEY_ID_SCENE,sceneID);
        if(size_conditions < 1)
        {
            free_fields(sceneType,size_sceneType);
            return false;
        }   
        LogInfo((get_localtime_now()),("[checkTypeSceneWithInfoIntoDatabase] strlen(conditions[0]) = %ld",strlen(conditions[0])));
        if(strlen(conditions[0]) <= 2)
        {
            //convert to TYPE_TakeAction_SCENE
            check_flag = sql_updateValueInTableWithCondition(db,TABLE_SCENE_INF,KEY_TYPE_SCENE,TYPE_TakeAction_SCENE,KEY_ID_SCENE,sceneID); 
            if(!check_flag)
            {
                LogError((get_localtime_now()),("[checkTypeSceneWithInfoIntoDatabase] Failed to update TYPE_TakeAction_SCENE of sceneID %s from database",sceneID));
            }
            free_fields(conditions,size_conditions);
            return true;
        }
    }

    return true;
}
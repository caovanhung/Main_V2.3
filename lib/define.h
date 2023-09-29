#ifndef __DEFINE_H
#define __DEFINE_H


#include "time_t.h"

#define HC_VERSION      2

#define TRUE    1
#define FALSE   0

#define FILENAME               ( strrchr( __FILE__, '/' ) ? strrchr( __FILE__, '/' ) + 1 : __FILE__ )

#define GATEWAY_NUM             2
//UART
#define UART_GATEWAY1           "/dev/ttyS3"
#define UART_GATEWAY2           "/dev/ttyS2"
#define DELAY_SEND_UART_MS         250000
#define _USE_DEBUG_UART //define for debug UART
#define _USE_DEBUG //define for debug UART
#define MAXLINE 100000U     //max length for message receive of UART

//DATABASE
#define VAR_DATABASE            "/home/szbaijie/main.db"
#define VAR_DATABASE_LOG        "/home/szbaijie/log.db"
///////////////////////////////////////////////////////////
#define max_size_message_received           2000
///////////////////////////////////////////////////////////
//define for key of JSON data
#define KEY_TYPE                "type"
#define KEY_REPORT              "reported"
#define KEY_SENDER              "sender"
#define KEY_SENDER_ID           "senderid"
#define KEY_SETTING             "setting"


#define SENDER_APP_VIA_LOCAL       2
#define SENDER_APP_TO_CLOUD        1
#define SENDER_HC_VIA_LOCAL        10
#define SENDER_HC_TO_CLOUD         11

#define TYPE_HOME_ID                        (48U)
#define TYPE_APP_SEND_CHANGE_INFO_WIFI                          99
#define TYPE_APP_SEND_INFO_INIT                                 100
#define TYPE_HC_REPONSE_RECEIVE_INFO_INIT_SUCCESS               101
#define TYPE_HC_REPONSE_PROCESS_INFO_INIT_SUCCESS               102
#define TYPE_HC_REPONSE_PROCESS_INFO_INIT_ERROR_WIFI            103
#define TYPE_HC_REPONSE_PROCESS_INFO_INIT_ERROR_INTERNET        104
#define TYPE_HC_REPONSE_PROCESS_INFO_INIT_ERROR_AWS_SUBCRI      105


#define TYPE_APP_SEND_INFO_MESH_NETWORK                         106
#define TYPE_HC_REPONSE_RECEIVE_INFO_MESH_NETWORK_SUCCESS       107
#define TYPE_HC_REPONSE_PROCESS_INFO_MESH_NETWORK_UNSUCCESS     108



//define for value of JSON data
#define TYPE_ADD_GW                     (26U)
#define TYPE_FEEDBACK_GATEWAY           (1U)
#define TYPE_ADD_DEVICE                 (2U)
#define TYPE_DEL_DEVICE                 (3U)
#define TYPE_CTR_DEVICE                 (4U)
#define TYPE_ADD_SCENE                  (5U)
#define TYPE_DEL_SCENE                  (7U)
#define TYPE_CTR_SCENE                  (8U)
#define TYPE_UPDATE_SCENE               (6U)
#define TYPE_DEL_SCENE_LC               (7U)
#define TYPE_CTR_SCENE_LC               (8U)
#define TYPE_UPDATE_DEVICE              (9U)
#define TYPE_ADD_GROUP_LIGHT            (12U)
#define TYPE_DEL_GROUP_LIGHT            (13U)
#define TYPE_CTR_GROUP_NORMAL           (14U)
#define TYPE_UPDATE_GROUP_LIGHT         (15U)
//SWITCH
#define TYPE_ADD_GROUP_LINK             (16U)
#define TYPE_UPDATE_GROUP_LINK          (17U)
#define TYPE_DEL_GROUP_LINK             (18U)
#define TYPE_GET_DEVICE_HISTORY         (19U)
#define TYPE_DIM_LED_SWITCH             (20U)
#define TYPE_LOCK_KIDS                  (21U)
#define TYPE_LOCK_AGENCY                (22U)
#define TYPE_NOTIFI_REPONSE             (23U)
#define TYPE_UPDATE_SERVICE             (25U)
#define TYPE_SET_GROUP_TTL              (27U)
#define TYPE_SET_DEVICE_TTL             (28U)
#define TYPE_GET_ALL_DEVICES            (29U)
#define TYPE_REALTIME_STATUS_FB         (31U)
#define TYPE_GET_SCENES_OF_DEVICE       (33U)
#define TYPE_GET_GROUPS_OF_DEVICE       (34U)
#define TYPE_SYNC_DEVICE_STATUS         (35U)
#define TYPE_OTA_GATEWAY                (36U)
#define TYPE_OTA_HC                     (37U)
#define TYPE_GET_LOG                    (39U)
#define TYPE_DEL_HOMEKIT                (40U)

#define TYPE_RESET_DATABASE             (44U)
#define TYPE_GET_NUM_OF_PAGE            (45U)
#define TYPE_SYNC_DB_DEVICES            (46U)
#define TYPE_SYNC_DB_SCENES             (47U)
#define TYPE_SYNC_DB_GROUPS             (48U)

#define TYPE_SYNC_DEVICE_STATE          (99U)
#define TYPE_GET_ONOFF_STATE            (100U)

///////////////////////Define type repons from Devices///////////////////////////////////////
#define GW_RESPONSE_UNKNOW               (-1)
#define GW_RESP_ONOFF_STATE              (50U)
#define GW_RESPONSE_DEVICE_KICKOUT       (51U)
#define GW_RESPONSE_SMOKE_SENSOR         (52U)
#define GW_RESPONSE_SENSOR_ENVIRONMENT   (53U)
#define GW_RESP_ONLINE_STATE             (54U)
#define GW_RESPONSE_SENSOR_BATTERY       (55U)
#define GW_RESPONSE_SENSOR_PIR_DETECT    (56U)
#define GW_RESPONSE_SENSOR_PIR_LIGHT     (57U)
#define GW_RESPONSE_SENSOR_DOOR_DETECT   (58U)
#define GW_RESPONSE_SENSOR_DOOR_ALARM    (59U)
#define GW_RESPONSE_GROUP                (60U)
#define GW_RESPONSE_LIGHT_RD_CONTROL     (61U)
#define GW_RESPONSE_RGB_COLOR            (86U)

#define GW_RESPONSE_SENSOR_ENVIRONMENT_TEMPER   ( 62U )
#define GW_RESPONSE_SENSOR_ENVIRONMENT_HUMIDITY ( 63U )

#define GW_RESPONSE_ACTIVE_DEVICE_HG_RD_SUCCESS         (64U)
#define GW_RESPONSE_SAVE_GATEWAY_HG                     (65U)
#define GW_RESPONSE_ACTIVE_DEVICE_HG_RD_UNSUCCESS       (66U)
#define GW_RESPONSE_SAVE_GATEWAY_RD                     (67U)

#define GW_RESPONSE_ADD_DEVICE                          (68U)
#define GW_RESPONSE_ADD_SCENE_HC                        (69U)
#define GW_RESPONSE_DEL_SCENE_HC                        (70U)
#define GW_RESPONSE_ADD_SCENE                           (71U)
#define GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE           (72U)
#define GW_RESPONSE_ADD_SCENE_LC                        (73U)
#define GW_RESPONSE_DEL_SCENE_LC                        (74U)
#define GW_RESPONSE_DIM_LED_SWITCH_HOMEGY               (75U)
#define GW_RESPONSE_ADD_GROUP_NORMAL                    (76U)
#define GW_RESPONSE_DEL_GROUP_NORMAL                    (77U)
#define GW_RESPONSE_ADD_GROUP_LINK                      (78U)
#define GW_RESPONSE_DEL_GROUP_LINK                      (79U)
#define GW_RESPONSE_SET_TIME_SENSOR_PIR                 (80U)
#define GW_RESPONSE_UPDATE_SCENE                        (81U)
#define GW_RESPONSE_UPDATE_GROUP                        (82U)
#define GW_RESPONSE_SET_TTL                             (83U)
#define GW_RESPONSE_IR                                  (84U)
#define CAM_HANET_RESPONSE                              (85U)
#define GW_RESPONSE_LOCK_KIDS                           (87U)

#define GW_RESPONSE_NOTIFI                              ( 111U)
#define KEY_MESSAGE                                     "message"
#define MESSAGE_ERROR_DEL_DEVICE                        "XÓA THIẾT BỊ THẤT BẠI!"
#define MESSAGE_SUCCES_DEL_DEVICE                       "XÓA THIẾT BỊ THÀNH CÔNG!"
#define MESSAGE_SUCCES_ADD                              "TẠO NHÓM ĐÈN THÀNH CÔNG!"
#define MESSAGE_SUCCES_ADD_GROUP_LINK                   "TẠO NHÓM CÔNG TẮC THÀNH CÔNG!"
#define MESSAGE_DEVICE_ERROR                            "THIẾT BỊ LỖI"

///////////////////////////////////////////////////////////////////////////////////////////////
#define TYPE_ADD_DEVICE_STRING                      "2"
#define TYPE_DEL_DEVICE_STRING                      "3"
#define TYPE_CTR_DEVICE_STRING                      "4"
#define TYPE_ADD_SCENE_STRING                       "5"
#define TYPE_DEL_SCENE_STRING                       "7"

#define TYPE_ADD_GROUP_NORMAL_STRING                "12"
#define TYPE_DEL_GROUP_NORMAL_STRING                "13"
#define TYPE_ADD_GROUP_LINK_STRING                  "16"
#define TYPE_DEL_GROUP_LINK_STRING                  "18"


#define TYPE_ADD_SCENE_LC_ACTIONS_STRING            "500"
#define TYPE_ADD_SCENE_LC_CONDITIONS_STRING         "501"

#define TYPE_DEL_SCENE_LC_ACTIONS_STRING            "510"
#define TYPE_DEL_SCENE_LC_CONDITIONS_STRING         "511"



#define TYPE_ADD_SCENE_LC_ACTIONS               (500U)
#define TYPE_ADD_SCENE_LC_CONDITIONS            (501U)

#define TYPE_DEL_SCENE_LC_ACTIONS               (510U)
#define TYPE_DEL_SCENE_LC_CONDITIONS            (511U)


#define TYPE_ADD_GROUP_NORMAL_ACTIONS               (520U)
#define TYPE_ADD_GROUP_NORMAL_CONDITIONS            (521U)




#define TIMEOUT_ADD_DEVICE_MS           (10000U)
#define TIMEOUT_ADD_SCENE_MS            (10000U)
#define TIMEOUT_ADD_GROUP_MS            (10000U)
#define TIMEOUT_CTL_MS                  (5000U)
#define TIMEOUT_DEL_MS                  (5000U)


#define STATUS_WAITING                  (0U)
#define STATUS_SUCCESS                  (2U)
#define STATUS_UNSUCCESS                (1U)

#define KEY_STATUS                      "Status"
// #define KEY_LIST                     "List"
#define KEY_FAILED                      "failed"


#define KEY_SAVE_GATEWAY                "SaveGW"
#define KEY_ACTIVE                      "Active"

#define KEY_CTL                         "Control"

#define KEY_DEL                         "Del"

#define KEY_DEL_SCENE_LC                "DelScene"
#define KEY_CREATE_SCENE_LC             "CreatScene"
#define KEY_SET_SCENE_LC                "SetScene"


#define KEY_ADD_GROUP_NORMAL            "AddGroupNormal"
#define KEY_DEL_GROUP_NORMAL            "DelGroupNormal"
#define KEY_ADD_GROUP_LINK              "AddGroupLink"
#define KEY_DEL_GROUP_LINK              "DelGroupLink"

///////////////////////Define for DEBUG///////////////////////////////////////
#define TYPE_MANAGER_PING_ON_OFF        (30U)
#define TYPE_GET_INF_DEVICES            (41U)
#define TYPE_GET_INF_SCENES             (42U)
#define TYPE_CREAT_DB_TABLE_DEVICE_LOG  (43U)
#define TYPE_PRINT_DB_TABLE_DEVICE_LOG  (44U)
#define TYPE_DEBUG_CTL_LOOP_DEVICE      (80U)
///////////////////////////////////////////////////////////////////////////////////////////////


#define TYPE_DEVICE_CTL_ADD_FROM_APP       -1
#define TYPE_DEVICE_REPONSE_ADD_FROM_APP    0
#define TYPE_DEVICE_ADD_UNSUCCES_HC         1
#define TYPE_DEVICE_ADD_SUCCES_HC           2
#define TYPE_DEVICE_ONLINE                  2
#define TYPE_DEVICE_OFFLINE                 3
#define TYPE_DEVICE_RESETED                 4

#define AWS_STATUS_FAILED                   1
#define AWS_STATUS_SUCCESS                  2
#define STATE_ONLINE                        2
#define STATE_OFFLINE                       3


//define type for scene
#define TYPE_TakeAction_SCENE   "0"
#define TYPE_OneOfAll_SCENE     "1"
#define TYPE_All_SCENE          "2"

//define type for response
#define TYPE_REPONSE                            "TypeResponse"
#define TYPE_DATA_REPONSE_STATE                 1
#define TYPE_DATA_REPONSE_VALUE                 2

#define TYPE_DPID_SMOKE_SENSOR_DETECT           1
#define TYPE_DPID_SMOKE_SENSOR_BATTERY          3

#define TYPE_DPID_DOOR_SENSOR_DETECT            1
#define TYPE_DPID_DOOR_SENSOR_ALRM              2
#define TYPE_DPID_DOOR_SENSOR_BATTERY           "3"


#define TYPE_DPID_PIR_SENSOR_DETECT             101
#define TYPE_DPID_PIR_SENSOR_LUX                102
#define TYPE_DPID_PIR_SENSOR_BATTERY            "103"
#define TYPE_DPID_PIR_SENSOR_SET_TIME           "104"


#define TYPE_DPID_ENVIRONMENT_SENSOR_TEMPER     1
#define TYPE_DPID_ENVIRONMENT_SENSOR_HUMIDITY   2
#define TYPE_DPID_ENVIRONMENT_SENSOR_BATTERY    "3"

#define TYPE_DPID_BATTERY_SENSOR                3

#define TYPE_DPID_CTR_LIGHT                     "20"

//define type for control
#define TYPE_CTR_ONOFF              1
#define TYPE_CTR_CTL                2
#define TYPE_CTR_HSL                3
//define name for Tables in Databse
#define TABLE_DEVICES                   "DEVICES"
#define TABLE_DEVICES_INF               "DEVICES_INF"
#define TABLE_GATEWAY_INF               "GATEWAY"
#define TABLE_SCENE_INF                 "SCENE_INF"
#define TABLE_GROUP_INF                 "GROUP_INF"
#define TABLE_DEVICE_LOG                "DEVICE_LOG"


#define DEVICES_INF                     "devices"
#define SCENE_INF                       "scenes"

//define for PROVIDER
#define HOMEGY_DELAY        -1


#define HOMEGY_WIFI         0
#define HOMEGY_ZIGBEE       1
#define HOMEGY_BLE          2
#define HANET_CAMERA        12
//define for type devices for BLEHG
#define HG_BLE                  "BLEHGAA0101,BLEHGAA0102,BLEHGAA0103,BLEHGAA0104,BLEHGAA0105,BLEHGAA0106,BLEHGAA0107,BLEHGAA0201,BLEHGAA0401"
#define HG_BLE_SWITCH           "BLEHGAA0101,BLEHGAA0102,BLEHGAA0103,BLEHGAA0104"
#define HG_BLE_SWITCH_1         "BLEHGAA0101"
#define HG_BLE_SWITCH_2         "BLEHGAA0102"
#define HG_BLE_SWITCH_3         "BLEHGAA0103"
#define HG_BLE_SWITCH_4         "BLEHGAA0104"


#define HG_BLE_IR               "BLEHGAA0301"
#define HG_BLE_IR_TV            "BLEHGAA0302"
#define HG_BLE_IR_FAN           "BLEHGAA0303"
#define HG_BLE_IR_AC            "BLEHGAA0304"
#define HG_BLE_IR_REMOTE        "BLEHGAA0305"
#define HG_BLE_IR_FULL          "BLEHGAA0301,BLEHGAA0302,BLEHGAA0303,BLEHGAA0304,BLEHGAA0305"

#define HG_BLE_LIGHT_WHITE      "BLEHGAA0201"
#define HG_BLE_SENSOR_MOTION    "BLEHGAA0401"

#define RD_BLE_SENSOR_TEMP      "BLEHG030801"
#define RD_BLE_SENSOR_SMOKE     "BLEHG030301,BLEHGAA0406"
#define RD_BLE_SENSOR_MOTION    "BLEHG030201,BLEHGAA0401"
#define RD_BLE_SENSOR_DOOR      "BLEHG030601,BLEHGAA0404"
#define RD_BLE_LIGHT_RGB        "lqezswimvgrha0fm,WVy6k8Lc6g0sHYC5,bcuobg1jjmtxmesx,uuA2N4dyCeYlcvqA,bIovIDMEC0SdkqZD,BLEHGAA0202,BLEHG010401,BLEHG010402"
#define RD_BLE_LIGHT_WHITE      "BLEHG010201"

#define RD_BLE_LIGHT_WHITE_TEST "uyaenrl6qdapsclz,qil8rhfmzguzxchz,zfbj19qm,szfpeatw,ijsj2evj,BLEHG01020D,BLEHG01020B,BLEHG01020A,BLEHG010201,BLEHG010202,BLEHG010204,BLEHG010205,BLEHG010206,BLEHG010207"

#define BLE_LIGHT               "uyaenrl6qdapsclz,qil8rhfmzguzxchz,zfbj19qm,szfpeatw,ijsj2evj,BLEHGAA0201,BLEHG01020D,BLEHG01020B,BLEHG01020A,BLEHG010201,BLEHG010202,BLEHG010204,BLEHG010205,BLEHG010206,BLEHG010207,lqezswimvgrha0fm,WVy6k8Lc6g0sHYC5,bcuobg1jjmtxmesx,uuA2N4dyCeYlcvqA,bIovIDMEC0SdkqZD,BLEHGAA0202,BLEHG010401,BLEHG010402"


#define CAM_HANET               "BLEHGCH0101"
#define TUYA_DEVICE             "WIFI"
#define HG_BLE_CURTAIN_NORMAL   "BLEHGAA0105"
#define HG_BLE_ROLLING_DOOR     "BLEHGAA0106"
#define HG_BLE_CURTAIN_2_LAYER  "BLEHGAA0107"
#define HG_BLE_CURTAIN          "BLEHGAA0105,BLEHGAA0106,BLEHGAA0107"

///////////////////////////////////////////////////////////////////////
//define
#define KEY_LIGHTNESS               "lightness"
#define KEY_COLOR_TEMPERATURE       "colorTemperature"
#define KEY_HSL                     "HSL"
#define KEY_GROUP_NORMAL            "GROUP_NORMAL"

///////////////////////////////////////////////////////////////////////
//define name for fields of TABLE_DEVICES_INF in Databse
#define KEY_PAGE_INDEX                  "pageIndex"
#define KEY_NAME                        "name"
#define KEY_DEVICE_ID                   "deviceID"
#define KEY_DP_ID                       "dpID"
#define KEY_ADDRESS                     "address"
#define KEY_DP_VAL                      "dpValue"
#define KEY_PID                         "pid"
#define KEY_PROVIDER                    "provider"
#define KEY_STATE                       "state"

#define KEY_GROUPS                      "groups"

#define KEY_DICT_INFO                   "dictInfo"
#define KEY_DICT_DPS                    "dictDPs"
#define KEY_DICT_NAME                   "dictName"
#define KEY_MAC                         "MAC"
#define KEY_UNICAST                     "Unicast"
#define KEY_ID_GATEWAY                  "IDgateway"

#define KEY_LED                         "led"
#define KEY_LOCK                        "lock"


#define KEY_CREATED                     "created"
#define KEY_MODIFILED                   "modified"
#define KEY_LAST_UPDATE                 "last_updated"
#define KEY_FIRMWARE_VERSION            "firmware"
#define KEY_PROTOCOL                    "protocol_para"

#define KEY_TIMECREAT                   "TimeCreated"
#define KEY_SCENE_LIST                  "SceneList"
#define KEY_GROUP_LIST                  "GroupList"

///////////////////////////////////////////////////////////////////////
//define name for fields of TABLE_GATEWAY_INF in Databse
#define KEY_APP_KEY             "appkey"
#define KEY_IV_INDEX            "ivIndex"
#define KEY_NETKEY_INDEX        "netkeyIndex"
#define KEY_NETKEY              "netkey"
#define KEY_APP_KEY_INDEX       "appkeyIndex"
#define KEY_DEVICE_KEY          "deviceKey"
#define KEY_ADDRESS_GW          "address"
///////////////////////////////////////////////////////////////////////
//define name for fields of TABLE_SCENE in Database
#define KEY_SCENE               "scenes"
#define KEY_ID_SCENE            "sceneId"
#define KEY_TYPE_SCENE          "sceneType"
#define KEY_TYPE_SCENE_COMPARE          "sceneType_compare"

#define KEY_STATE_SCENE         "sceneState"
#define KEY_ACTIONS                     "actions"
#define KEY_ACTIONS_COMPARE             "actions_compare"
#define KEY_CONDITIONS                  "conditions"
#define KEY_CONDITIONS_COMPARE          "conditions_compare"

#define KEY_PRECONDITIONS       "preconditions"
#define KEY_COMMON_DEVICES      "commonDevices"
#define KEY_VAL                 "val"
#define KEY_IS_LOCAL            "isLocal"


#define POSITION_IS_LOCAL_IN_DB         (1U)
#define POSITION_STATE_IN_DB            (2U)
#define POSITION_CREATED_IN_DB          (7U)
#define POSITION_LAST_UPDATE_IN_DB      (8U)



#define KEY_SCENE_TYPE_LC           (1U)
#define KEY_SCENE_TYPE_HC           (0U)
#define KEY_SCENE_STATE_ON          (1U)
#define KEY_SCENE_STATE_OFF         (0U)

#define KEY_ENTITY_ID           "entityID"
#define KEY_ACT_EXECUTOR        "actionExecutor"
#define KEY_PROPERTY_EXECUTOR   "executorProperty"
#define KEY_EXPR                "expr"
#define KEY_TIMER               "timer"
#define KEY_RULE_ENABLE         "ruleEnable"
#define KEY_RULE_DISABLE        "ruleDisable"

#define KEY_DELAY               "delay"
#define KEY_MINUTES             "minutes"
#define KEY_SECONDS             "seconds"

#define KEY_SEC                 "sec"
#define KEY_MIN                 "min"
#define KEY_HOUR                "hour"
#define KEY_DAY                 "day"
#define KEY_MONTH               "month"
#define KEY_YEAR                "year"


#define KEY_TRUE                "true"
#define KEY_FALSE               "false"

#define KEY_TRUE_INT                1
#define KEY_FALSE_INT               0


#define KEY_VALUE               "value"
#define KEY_DP_TUYA             "dpTuya"
#define KEY_DP_ISSUE            "dpIssue"
#define KEY_CODE                "code"
///////////////////////////////////////////////////////////////////////
//define name for fields of Group
#define KEY_ID_GROUP                    "groupID"
#define KEY_ADDRESS_GROUP               "groupAdress"
#define KEY_DEVICES_GROUP               "devices"
#define KEY_DEVICES_COMPARE_GROUP       "devices_compare"


///////////////////////////////////////////////////////////////////////
//define name for fields of TABLE_CONDITION in Databse
#define KEY_LOOP_PRECONDITION                   "loops"
#define KEY_DATE                                "date"
#define KEY_TIME                                "time"
#define KEY_TIME_INTERVAL_PRECONDITION          "timeInterval"
#define KEY_TIME_START_PRECONDITION             "start"
#define KEY_TIME_END_PRECONDITION               "end"
#define KEY_TIME_ZONE_PRECONDITION              "timeZoneId"
#define KEY_LIMIT                               "limit"


#define LENGTH_DEVICE_ID                    ( 12U )
#define LENGTH_SCENE_ID                     ( 12U )
#define LENGTH_GROUP_ADDRESS                ( 12U )
/////////////////////////////////DEFINE FOR MQTT LOCAL/////////////////////////////////////////
#define MQTT_MOSQUITTO_PORT                 (1883u)
#define MQTT_MOSQUITTO_KEEP_ALIVE           (60u)
#define MQTT_MOSQUITTO_HOST                 "localhost"

#define MOSQ_TOPIC_CONTROL_LOCAL            "APPLICATION_SERVICES/Mosq/Control"
#define MOSQ_TOPIC_MANAGER_SETTING          "MANAGER_SERVICES/Setting/Wifi"
#define MOSQ_TOPIC_MANAGER                  "MANAGER_SERVICES/ServieceManager/#"
#define MQTT_LOCAL_REQS_TOPIC               "MANAGER_SERVICES/Setting/Wifi"
#define MQTT_LOCAL_RESP_TOPIC               "MANAGER_SERVICES/SETTING/RESP"


#define MOSQ_TOPIC_AWS                      "APPLICATION_SERVICES/AWS/#"

#define MOSQ_TOPIC_CORE_DATA                "CORE_SERVICES/CORE/#"
#define MOSQ_TOPIC_COMMAND                  "CORE_SERVICES/Command/#"

#define MOSQ_TOPIC_RULE                     "SUPPORTING_SERVICES/Rule_Schedule/#"
#define MOSQ_TOPIC_NOTIFILE                 "SUPPORTING_SERVICES/Notifile/#"

#define MOSQ_TOPIC_DEVICE_BLE               "DEVICE_SERVICES/BLE"
#define MOSQ_TOPIC_DEVICE_TUYA              "DEVICE_SERVICES/TUYA/#"


#define MOSQ_TOPIC_TOOL                     "SUPPORTING_SERVICES/Tool/#"
/////////////////////////////////DEFINE name of layer service/////////////////////////////////////////
#define MOSQ_LayerService_App               "APPLICATION_SERVICES"

#define MOSQ_LayerService_Support           "SUPPORTING_SERVICES"

#define MOSQ_LayerService_Core              "CORE_SERVICES"

#define MOSQ_LayerService_Device            "DEVICE_SERVICES"

#define MOSQ_LayerService_Manager           "MANAGER_SERVICES"
/////////////////////////////////DEFINE name of service/////////////////////////////////////////


#define MOSQ_NameService_Support_Rule_Schedule          "Rule_Schedule"
#define MOSQ_NameService_Support_Notifile               "Notifile"
#define MOSQ_NameService_Support_Tool                   "Tool"

#define SERVICES_NUMBER                   5
#define SERVICE_AWS                  "AWS"
#define SERVICE_CORE                 "CORE"
#define SERVICE_BLE                  "BLE"
#define SERVICE_TUYA                 "TUYA"
#define SERVICE_HANET                "HANET"
#define SERVICE_CFG                  "CFG"
#define SERVICE_ID_AWS                0
#define SERVICE_ID_CORE               1
#define SERVICE_ID_BLE                2
#define SERVICE_ID_TUYA               3
#define SERVICE_ID_CFG                4

#define MOSQ_NameService_Manager_ServieceManager        "ServieceManager"
/////////////////////////////////DEFINE name of extend/////////////////////////////////////////
#define MOSQ_Request            "Request"
#define MOSQ_Reponse            "Response"
#define MOSQ_ActResponse        "ActResponse"
#define MOSQ_ActNoResponse      "ActNoResponse"
#define MOSQ_Telemetry          "Telemetry"
/////////////////////////////////DEFINE name of extend/////////////////////////////////////////
#define MOSQ_LayerService       "LayerService"
#define MOSQ_NameService        "NameService"
#define MOSQ_ActionType         "ActionType"
#define MOSQ_Extend             "Extend"
#define MOSQ_Payload            "Payload"
#define MOSQ_Id                 "Id"
#define MOSQ_TimeCreat          "TimeCreat"
#define MOSQ_ResponseState      "state"
#define MOSQ_ResponseValue      "value"

#define KEY_ID                  "ID"



//////////////////////////////////////////////////
#define DELAY_BETWEEN_ACTIONS_GROUP_NORMAL_uSECONDS (2000000u)


//using for BLE service
#define MAX_SIZE_UART_STRING_RECEIVE            ( 100000U )
#define MAX_SIZE_ELEMENT_QUEUE                  ( 100000U )
#define MAX_SIZE_NUMBER_QUEUE                   ( 1000U )
#define MAX_SIZE_INFO_ADDRESS                   ( 100000U )


//using common
#define MAX_SIZE_TMP                ( 100000U)
#define MAX_SIZE_PAYLOAD            ( 100000U)
#define MAX_SIZE_TOPIC              ( 10000U)
#define MAX_SIZE_MESSAGE            ( 100000U)
#define MAX_SIZE_STATE              ( 100U )
#define MAX_SIZE_ID                 ( 200U )


//using for Rule service
#define MAX_SIZE_RESULT_ACTIONSCENE                 ( 1000000U )
#define MAX_SIZE_RESULT_CONDITIONSCENE              ( 1000000U )
#define MAX_SIZE_COLLECT_CONDITION                  ( 1000000U )


#define KEY_SHADOWNAME_AWS              "ShadowName"


// DPs of IR devices
#define DPID_IR_BRAND_ID        "1"
#define DPID_IR_REMOTE_ID       "2"
#define DPID_IR_COMMAND_TYPE    "3"
#define DPID_IR_VOICE           "4"
#define DPID_IR_ONOFF           "101"
#define DPID_IR_MODE            "102"
#define DPID_IR_TEMP            "103"
#define DPID_IR_FAN             "104"
#define DPID_IR_SWING           "105"
#define DPID_IR_COMMAND         "106"


// Event history type
typedef enum {
    EV_DEVICE_DP_CHANGED = 1,
    EV_DEVICE_STATE_CHANGED = 2,
    EV_DEVICE_ADDED = 3,
    EV_DEVICE_DELETED = 4,
    EV_LOCK_TOUCH = 5,
    EV_LOCK_DEVICE = 6,
    EV_GROUP_CHANGED = 7,
    EV_RUN_SCENE = 8,
    EV_ONOFF_SCENE = 9,
    EV_CTR_DEVICE_FAILED = 10,
} HistoryEventType;

// Event history cause type
typedef enum {
    EV_CAUSE_TYPE_DEVICE = 0,
    EV_CAUSE_TYPE_APP = 1,
    EV_CAUSE_TYPE_GROUP = 2,
    EV_CAUSE_TYPE_SCENE = 3,
    EV_CAUSE_TYPE_SYNC = 4,
} HistoryCauseType;

#endif // __DEFINE_H
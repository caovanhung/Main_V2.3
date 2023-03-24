/////////////////////////////////LOG/////////////////////////////////////////
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "BLE"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif
#define MQTT_LIB    "core-mqtt@" MQTT_LIBRARY_VERSION
/////////////////////////////////MQTT LOCAL/////////////////////////////////////////
#define MQTT_MOSQUITTO_CIENT_ID             	"BleDeviceService"


#define TSCRIPT_GATEWAY_DIR_RSP 				"0x91"
#define HCI_GATEWAY_RSP_OP_CODE 				"0x81"
#define HCI_GATEWAY_CMD_SEND_BACK_VC 			"0xb5"

///////////////////////Define for process_infor///////////////////////////////////////

#define _DEBUG //debug tranfer infor and warring
#define TIME_DELAY_PROVISION                    ( 1U ) //delay code write UART into provision
#define TIME_DELAY_WAIT_DONE_PROVISION          ( 50U ) //waiting still provision done!

///////////////////////Define for process_infor///////////////////////////////////////
#define MAX_FRAME_SIZE              ( 50U )     // Maximum number of bytes in a single frame
#define MAX_PACKAGE_SIZE            ( 1000U )   // Maximum number of bytes in an UART package (contains multiple single frame)
#define MAX_FRAME_COUNT             ( 20U )     // Maximum number of single frame in a received UART package
#define MAX_SIZE_ELEMENT_QUEUE      ( 10000U )
#define MAX_SIZE_NUMBER_QUEUE       ( 10U )
#include <math.h>
#include "ble_process.h"
#include "logging_stack.h"
#include "time_t.h"
#include "uart.h"

// Variables to wait response after send a packet
static uint8_t g_bleFrameRespType = 0;
static uint16_t g_bleFrameSender = 0;
int fd = 0;

void waitResponse(uint16_t addr) {
    int i = 0;
    long long start = timeInMilliseconds();
    while (g_bleFrameSender != addr) {
        usleep(10);
        i++;
        if (i > 2500) {
            break;
        }
    }
    long long end = timeInMilliseconds();
    if (addr != g_bleFrameSender) {
        myLogInfo("Time spent: TIMEOUT after %lld ms", end - start);
    } else {
        myLogInfo("Time spent: %lld ms", end - start);
    }
}

void BLE_PrintFrame(char* str, ble_rsp_frame_t* frame) {
    char paramStr[1000];
    int n = 0;
    for (int i = 0; i < frame->paramSize; i++) {
        n += sprintf(&paramStr[n], "%02x", frame->param[i]);
    }
    sprintf(str, "sendAddr: %04x, recv: %4x, opcode: %4x, param: %s", frame->sendAddr, frame->recvAddr, frame->opcode, paramStr);
}

bool ble_getInfoProvison(provison_inf *PRV, JSON_Object *object)
{
    PRV->appkey = json_object_get_string(object, KEY_APP_KEY);
    PRV->ivIndex = json_object_get_string(object, KEY_IV_INDEX);
    PRV->netkeyIndex = json_object_get_string(object, KEY_NETKEY_INDEX);
    PRV->netkey = json_object_get_string(object, KEY_NETKEY);
    PRV->appkeyIndex = json_object_get_string(object, KEY_APP_KEY_INDEX);
    PRV->deviceKey = json_object_get_string(object, KEY_DEVICE_KEY);
    PRV->address = json_object_get_string(object, KEY_ADDRESS);
    return true;
}

bool BLE_GetDeviceOnOffState(const char* dpAddr) {
    int sentLength = 0, i = 0;
    uint8_t  data[] = {0xe8,0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0x00,0x00,  0x82,0x01};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    data[9] = dpAddrHex & (0xFF);
    data[8] = (dpAddrHex >> 8) & 0xFF;
    sentLength = UART0_Send(fd, data, 12);
    waitResponse(dpAddrHex);
    if (sentLength != 12) {
        return false;
    }
    return true;
}

bool ble_bindGateWay(provison_inf *PRV,int fd)
{

    int i = 0;
    int tmp_len = 0;

    char *appkey;
    char *ivIndex;
    char *netkeyIndex;
    char *netkey;
    char *appkeyIndex;
    char *deviceKey;
    char *address_t;

    unsigned char RESET_GW[3] = {0xe9,0xff,0x02};                   //FIX
    unsigned char SET_NODE_PARA_GW[4] = {0xe9,0xff,0x16,0x00};  //FIX
    unsigned char GET_PRO_SELF_STS_GW[3] = {0xe9,0xff,0x0c};        //FIX
    unsigned char SET_NODE_REPONS_LIVE[3] = {0xec,0xff,0x01};       //FIX

    unsigned char SET_PRO_PARA_GW[28] = {0};            //NETKEYS
    SET_PRO_PARA_GW[0] = 0xe9;
    SET_PRO_PARA_GW[1] = 0xff;
    SET_PRO_PARA_GW[2] = 0x09;

    unsigned char SET_DEV_KEY_GW[21] = {0};
    SET_DEV_KEY_GW[0] = 0xe9;
    SET_DEV_KEY_GW[1] = 0xff;
    SET_DEV_KEY_GW[2] = 0x0d;

    unsigned char START_KEYBIND_GW[22] = {0};           //APPKEYS
    START_KEYBIND_GW[0] = 0xe9;
    START_KEYBIND_GW[1] = 0xff;
    START_KEYBIND_GW[2] = 0x0b;
    START_KEYBIND_GW[3] = 0x01;

    appkey = (char *)PRV->appkey;
    ivIndex = (char *)PRV->ivIndex;
    netkeyIndex = (char *)PRV->netkeyIndex;
    netkey = (char *)PRV->netkey;
    appkeyIndex = (char *)PRV->appkeyIndex;
    deviceKey = (char *)PRV->deviceKey;
    address_t = (char *)PRV->address;

    #ifdef _DEBUG
    {
        printf("provison_GW into\n");

        printf("\n\n\n[PRV]appkey : %s\n",appkey);
        printf("[PRV]ivIndex : %s\n",ivIndex);
        printf("[PRV]netkeyIndex : %s\n",netkeyIndex);
        printf("[PRV]netkey : %s\n",netkey);
        printf("[PRV]appkeyIndex : %s\n",appkeyIndex);
        printf("[PRV]address : %s\n",address_t);
        printf("[PRV]deviceKey : %s\n\n\n",deviceKey);
    }
    #endif /* _DEBUG */

    //netkey
    tmp_len = (int)strlen(netkey);
    unsigned char *hex_netkey;
    hex_netkey  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(netkey,hex_netkey);

    if(tmp_len == 32)//
    {
        for(i = 0;i<tmp_len/2;i++) //len= 32
        {
            SET_PRO_PARA_GW[i+3] = *(hex_netkey+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth netkey\n");
        }
        #endif
        for(i = 0;i<16;i++) //len= 32
        {
            SET_PRO_PARA_GW[i+3] = 0x00;
        }
    }
    free(hex_netkey);

    //netkeyIndex
    tmp_len = (int)strlen(netkeyIndex);
    unsigned char *hex_netkeyIndex;
    hex_netkeyIndex  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(netkeyIndex,hex_netkeyIndex);
    if(tmp_len == 4)//
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_PRO_PARA_GW[i+19] = *(hex_netkeyIndex+i);
        }
    }
    else if(tmp_len == 1)
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth netkeyIndex\n");
        }
        #endif
        for(i = 0;i<2;i++)
        {
            SET_PRO_PARA_GW[i+19] = 0x00;
        }
    }
    free(hex_netkeyIndex);

    //iv update flag
    SET_PRO_PARA_GW[i+21] = 0x00;

    //ivIndex
    tmp_len = (int)strlen(ivIndex);
    unsigned char *hex_ivIndex;
    hex_ivIndex  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(ivIndex,hex_ivIndex);
    if(tmp_len == 8)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_PRO_PARA_GW[i+22] = *(hex_ivIndex+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth ivIndex\n");
        }
        #endif
        for(i = 0;i<4;i++)
        {
            SET_PRO_PARA_GW[i+22] = 0x00;
        }
    }
    free(hex_ivIndex);

    tmp_len = (int)strlen(address_t);
    unsigned char *hex_address;
    hex_address  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(address_t,hex_address);
    if(tmp_len == 4)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_PRO_PARA_GW[i+26] = *(hex_address+i);
            SET_DEV_KEY_GW[i+3] = *(hex_address+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth address_t\n");
        }
        #endif
        for(i = 0;i<2;i++)
        {
            SET_PRO_PARA_GW[i+26] = 0x00;
            SET_DEV_KEY_GW[i+3]= 0x00;
        }
    }
    free(hex_address);


    printf("[LOG] SET_PRO_PARA_GW: ");
    for(i = 0;i<28;i++)
    {
        printf("%02x",SET_PRO_PARA_GW[i]);
    }
    printf("\n");


    //device key
    tmp_len = (int)strlen(deviceKey);
    unsigned char *hex_deviceKey;
    hex_deviceKey  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(deviceKey,hex_deviceKey);
    if(tmp_len == 32)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            SET_DEV_KEY_GW[i+5] = *(hex_deviceKey+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth deviceKey\n");
        }
        #endif
        for(i = 0;i<16;i++)
        {
            SET_DEV_KEY_GW[i+5] = 0x00;
        }
    }
    free(hex_deviceKey);

    #ifdef _DEBUG
    {
        printf("[LOG] SET_DEV_KEY_GW: ");
        for(i = 0;i<21;i++)
        {
            printf("%02x",SET_DEV_KEY_GW[i]);
        }
        printf("\n");
    }
    #endif /* _USE_DEBUG */

    //appkeyIndex
    tmp_len = (int)strlen(appkeyIndex);
    unsigned char *hex_appkeyIndex;
    hex_appkeyIndex  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(appkeyIndex,hex_appkeyIndex);
    if(tmp_len == 4)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            START_KEYBIND_GW[i+4] = *(hex_appkeyIndex+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth appkeyIndex\n");
        }
        #endif
        START_KEYBIND_GW[4] = 0x00;
        START_KEYBIND_GW[5] = 0x00;
    }
    free(hex_appkeyIndex);

    //appkey
    tmp_len = (int)strlen(appkey);
    unsigned char *hex_appkey;
    hex_appkey  = (char*) malloc(tmp_len * sizeof(char));
    String2HexArr(appkey,hex_appkey);
    if(tmp_len == 32)
    {
        for(i = 0;i<tmp_len/2;i++)
        {
            START_KEYBIND_GW[i+6] = *(hex_appkey+i);
        }
    }
    else
    {
        #ifdef _DEBUG
        {
            printf("[WARNING] lenngth appkey\n");
        }
        #endif
        for(i = 0;i<16;i++)
        {
            START_KEYBIND_GW[i+6] = 0x00;
        }
    }
    free(hex_appkey);

    #ifdef _DEBUG
    {
        printf("[LOG] START_KEYBIND_GW: ");
        for(i = 0;i<22;i++)
        {
            printf("%02x",START_KEYBIND_GW[i]);
        }
        printf("\n");
    }
    #endif /* _USE_DEBUG */


    printf("\n\n\n[LOG] START PROVISON................. \n");
    printf("[LOG] RESET_GW: ");
    for(i = 0;i<3;i++)
    {
        printf("%02x ",RESET_GW[i]);
    }
    printf("\n");

    UART0_Send(fd,RESET_GW,3);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        RESET_GW DONE.\n");
    }
    #endif/*_DEBUG */


    printf("[LOG] SET_NODE_PARA_GW: ");
    for(i = 0;i<4;i++)
    {
        printf("%02x ",SET_NODE_PARA_GW[i]);
    }
    printf("\n");

    UART0_Send(fd,SET_NODE_PARA_GW,4);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        SET_NODE_PARA_GW DONE\n");
    }
    #endif/*_DEBUG */


    printf("[LOG] GET_PRO_SELF_STS_GW: ");
    for(i = 0;i<5;i++)
    {
        printf("%02x ",GET_PRO_SELF_STS_GW[i]);
    }
    printf("\n");
    UART0_Send(fd,GET_PRO_SELF_STS_GW,3);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        GET_PRO_SELF_STS_GW DONE\n");
    }
    #endif/*_DEBUG */

    printf("[LOG] SET_PRO_PARA_GW: ");
    for(i = 0;i<28;i++)
    {
        printf("%02x ",SET_PRO_PARA_GW[i]);
    }
    printf("\n");
    UART0_Send(fd,SET_PRO_PARA_GW,28);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        SET_PRO_PARA_GW DONE\n");
    }
    #endif/*_DEBUG */

    printf("[LOG] SET_DEV_KEY_GW: ");
    for(i = 0;i<21;i++)
    {
        printf("%02x ",SET_DEV_KEY_GW[i]);
    }
    printf("\n");
    UART0_Send(fd,SET_DEV_KEY_GW,21);
    sleep(TIME_DELAY_PROVISION);
    #ifdef _DEBUG
    {
        printf("        SET_DEV_KEY_GW DONE\n");
    }
    #endif/*_DEBUG */


    printf("[LOG] START_KEYBIND_GW: ");
    for(i = 0;i<22;i++)
    {
        printf("%02x ",START_KEYBIND_GW[i]);
    }
    printf("\n");
    UART0_Send(fd,START_KEYBIND_GW,22);
    #ifdef _DEBUG
    {
        printf("        START_KEYBIND_GW DONE\n");
    }
    #endif/*_DEBUG */
    sleep(TIME_DELAY_WAIT_DONE_PROVISION);

    printf("[LOG] SET_NODE_REPONS_LIVE: ");
    for(i = 0;i<3;i++)
    {
        printf("%02x ",SET_NODE_REPONS_LIVE[i]);
    }
    printf("\n");
    UART0_Send(fd,SET_NODE_REPONS_LIVE,3);
    sleep(TIME_DELAY_PROVISION);
    printf("[LOG] DONE PROVISION>>>>>>>>>>>>>>>>>>>>>>>>\n");
    return true;
}

bool ble_saveInforDeviceForGatewayRangDong(const char *address_t,const char *address_gateway)
{
    int check = 0,i = 0;
    uint8_t  SET_SAVE_GW[] = {0xe8,0xff,  0x00,0x00,0x00,0x00,  0x02,0x00,  0x00,0x00,  0xe0,0x11,0x02,0xe1,0x00,  0x02,0x00,   0x00,0x00,0x00,0x00,0x00,0x00};

    uint8_t  *hex_tmp = malloc(5);
    String2HexArr((char*)address_t,hex_tmp);

    uint8_t  *hex_address_gateway = malloc(5);
    String2HexArr((char*)address_gateway,hex_address_gateway);


    SET_SAVE_GW[8] = *hex_tmp;
    SET_SAVE_GW[9] = *(hex_tmp+1);
    SET_SAVE_GW[17] = *hex_address_gateway;
    SET_SAVE_GW[18] = *(hex_address_gateway+1);


    printf("SET_SAVE_GW = ");
    for (i = 0; i < 23; ++i)
    {
        printf("0x%02X ",SET_SAVE_GW[i] );
    }
    printf("\n\n");

    check = UART0_Send(fd,SET_SAVE_GW,23);
    free(hex_tmp);
    hex_tmp = NULL;
    free(hex_address_gateway);
    hex_address_gateway = NULL;
    if(check != 23)
    {
        return false;
    }
    return true;
}

bool ble_saveInforDeviceForGatewayHomegy(const char *address_element_0,const char *address_gateway)
{
    int check = 0,i = 0;
    uint8_t  SET_SAVE_GW[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0x11,0x02,0x00,0x00,0x02,0x00,0x00,0x00};

    uint8_t  *hex_address_element = malloc(5);
    String2HexArr((char*)address_element_0,hex_address_element);

    uint8_t  *hex_address_gateway = malloc(5);
    String2HexArr((char*)address_gateway,hex_address_gateway);


    SET_SAVE_GW[8] = *hex_address_element;
    SET_SAVE_GW[9] = *(hex_address_element+1);
    SET_SAVE_GW[17] = *hex_address_gateway;
    SET_SAVE_GW[18] = *(hex_address_gateway+1);

    printf("SET_SAVE_GW = ");
    for (i = 0; i < 19; ++i)
    {
        printf("0x%02X ",SET_SAVE_GW[i] );
    }
    printf("\n\n");

    check = UART0_Send(fd,SET_SAVE_GW,19);
    free(hex_address_element);
    hex_address_element = NULL;
    free(hex_address_gateway);
    hex_address_gateway = NULL;

    if(check != 19)
    {
        return false;
    }
    return true;
}


bool creatFormReponseBLE(char **ResultTemplate)
{
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    char *serialized_string = NULL;

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
}

bool insertObjectReponseBLE(char **ResultTemplate, char *address,char *Id,long long TimeCreat,char *State)
{
    JSON_Value *root_value = json_parse_string(*ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    char *temp = malloc(50);
    memset(temp,'\0',sizeof(temp));
    sprintf(temp,"%s.%s",address,MOSQ_Id);
    json_object_dotset_string(root_object, temp, Id);

    memset(temp,'\0',sizeof(temp));
    sprintf(temp,"%s.%s",address,MOSQ_TimeCreat);
    json_object_dotset_number(root_object, temp, TimeCreat);

    memset(temp,'\0',sizeof(temp));
    sprintf(temp,"%s.%s",address,MOSQ_ResponseState);
    json_object_dotset_string(root_object, temp, State);


    free(temp);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    printf("insertObjectReponseBLE = %s\n", *ResultTemplate);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
}

bool removeObjectReponseBLE(char **ResultTemplate,char *address)
{
    JSON_Value *root_value = json_parse_string(*ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);

    char *serialized_string = NULL;

    json_object_remove(root_object, address);

    serialized_string = json_serialize_to_string_pretty(root_value);
    int size_t = strlen(serialized_string);
    *ResultTemplate = malloc(size_t+1);
    memset(*ResultTemplate,'\0',size_t+1);
    strcpy(*ResultTemplate,serialized_string);
    printf("removeObjectReponseBLE = %s\n", *ResultTemplate);
    json_free_serialized_string(serialized_string);
    json_value_free(root_value);
}

long long int  getTimeCreatReponseBLE(char *ResultTemplate,char *address)
{
    long long int result;
    JSON_Value *root_value = json_parse_string(ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);


    char *temp = malloc(50);
    memset(temp,'\0',sizeof(temp));
    sprintf(temp,"%s.%s",address,MOSQ_TimeCreat);
    result = json_object_dotget_number(root_object, temp);

    free(temp);
    json_value_free(root_value);
    return result;
}

bool  getStateReponseBLE(char **state,char *ResultTemplate,char *address)
{
    char *result = NULL;
    JSON_Value *root_value = json_parse_string(ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);


    char *temp = malloc(50);
    memset(temp,'\0',sizeof(temp));
    sprintf(temp,"%s.%s",address,MOSQ_ResponseState);
    result = (char *)json_object_dotget_string(root_object, temp);

    int size_t = strlen(result);
    *state = malloc(size_t+1);
    memset(*state,'\0',size_t+1);
    strcpy(*state,result);

    free(temp);
    json_value_free(root_value);
    return true;
}

bool  getIdReponseBLE(char **Id,char *ResultTemplate,char *address)
{
    char *result = NULL;
    JSON_Value *root_value = json_parse_string(ResultTemplate);
    JSON_Object *root_object = json_value_get_object(root_value);


    char *temp = malloc(50);
    memset(temp,'\0',sizeof(temp));
    sprintf(temp,"%s.%s",address,MOSQ_Id);
    result = (char *)json_object_dotget_string(root_object, temp);

    int size_t = strlen(result);
    *Id = malloc(size_t+1);
    memset(*Id,'\0',size_t+1);
    strcpy(*Id,result);

    free(temp);
    json_value_free(root_value);
    return true;
}



int get_count_element_of_DV(const char* pid_)
{
    if(isMatchString(pid_,HG_BLE_SWITCH_1))
    {
        return 1;
    }
    else if(isMatchString(pid_,HG_BLE_SWITCH_2))
    {
        return 2;
    }
    else if(isMatchString(pid_,HG_BLE_SWITCH_3))
    {
        return 3;
    }
    else if(isMatchString(pid_,HG_BLE_SWITCH_4))
    {
        return 4;
    }
        else if(isMatchString(pid_,HG_BLE_CURTAIN_NORMAL))
    {
        return 1;
    }
        else if(isMatchString(pid_,HG_BLE_ROLLING_DOOR))
    {
        return 1;
    }
    else if(isMatchString(pid_,HG_BLE_CURTAIN_2_LAYER))
    {
        return 2;
    }
    else
        return 0;
}

void get_string_add_DV_write_GW(char **result,const char* address_device,const char* element_count,const char* deviceID)
{
    //*result  = (char*) malloc(46 * sizeof(char));
    const char *tmp_0 = "e9ff12";
    const char *tmp_1 = address_device;
    const char *tmp_2 = element_count;
    const char *tmp_3 = "00";
    const char *tmp_4 = deviceID;
    strcpy(*result, tmp_0);
    strcat(*result, tmp_1);
    strcat(*result, "0");
    strcat(*result, tmp_2);
    strcat(*result, "00");
    strcat(*result, deviceID);
}

int set_inf_DV_for_GW(const char* address_device,const char* pid,const char* deviceID)
{
    if (address_device && pid && deviceID) {
        char *str_send_uart = (char*) malloc(1000 * sizeof(char));
        unsigned char *hex_send_uart;
        int check = 0;
        char *element_count_str = (char*)malloc(2);

        int  element_count = get_count_element_of_DV(pid);
        Int2String(element_count,element_count_str);
        get_string_add_DV_write_GW(&str_send_uart,address_device,element_count_str,deviceID);
        int len_str = (int)strlen(str_send_uart);
        hex_send_uart  = (char*) malloc(len_str * sizeof(char)*10);
        String2HexArr(str_send_uart,hex_send_uart);
        check = UART0_Send(fd,hex_send_uart,len_str/2);
        usleep(DELAY_SEND_UART_MS);
        free(element_count_str);
        free(hex_send_uart);
        free(str_send_uart);
        hex_send_uart = NULL;
        if(check != len_str/2)
        {
            return -1;
        }
    }
    return 0;
}

void ble_getStringControlOnOff_SW(char **result,const char* strAddress,const char* strState)
{
    *result  = (char*) malloc(27 * sizeof(char));
    const char *tmp_0 = "e8ff000000000000";
    char *tmp_1 = (char *)strAddress;
    const char *tmp_2 = "8203";
    char* tmp_3;

    if(isMatchString((char *)strState,"0"))
    {
        tmp_3 = "00";
    }
    else if(isMatchString((char *)strState,"1"))
    {
        tmp_3 = "01";
    }
    else if(isMatchString((char *)strState,"2"))
    {
        tmp_3 = "02";
    }
    strcpy(*result, tmp_0);
    strcat(*result, tmp_1);
    strcat(*result, tmp_2);
    strcat(*result, tmp_3);
}

int ble_controlOnOFF_SW(const char* dpAddr, uint8_t dpValue)
{
    uint8_t data[] = {0xe8, 0xff,  0x00,0x00,0x00,0x00,0x00,0x00,  0xff,0xff,  0x82,0x03, 0x00};
    long int dpAddrHex = strtol(dpAddr, NULL, 16);
    data[9] = dpAddrHex & (0xFF);
    data[8] = (dpAddrHex >> 8) & 0xFF;
    data[12] = dpValue;

    g_bleFrameSender = 0;
    int sentLength = UART0_Send(fd, data, 13);
    waitResponse(dpAddrHex);

    if (sentLength != 13) {
        return -1;
    }
    return 0;
}


void ble_getStringControlOnOff(char **result,const char* strAddress,const char* strState)
{
    *result  = (char*) malloc(27 * sizeof(char));
    const char *tmp_0 = "e8ff000000000000";
    char *tmp_1 = (char *)strAddress;
    const char *tmp_2 = "8202";
    char* tmp_3;

    if(isMatchString((char *)strState,"0"))
    {
        tmp_3 = "00";
    }
    else if(isMatchString((char *)strState,"1"))
    {
        tmp_3 = "01";
    }
    else if(isMatchString((char *)strState,"2"))
    {
        tmp_3 = "02";
    }
    strcpy(*result, tmp_0);
    strcat(*result, tmp_1);
    strcat(*result, tmp_2);
    strcat(*result, tmp_3);
}

int ble_controlOnOFF(const char *address_element,const char *state)
{
    char *str_send_uart;
    unsigned char *hex_send_uart;
    int check = 0;
    long int dpAddrHex = strtol(address_element, NULL, 16);
    g_bleFrameSender = 0;
    ble_getStringControlOnOff(&str_send_uart,address_element,state);

    int len_str = (int)strlen(str_send_uart);
    hex_send_uart  = (char*) malloc(len_str * sizeof(char));
    String2HexArr(str_send_uart,hex_send_uart);
    check = UART0_Send(fd,hex_send_uart,len_str/2);
    waitResponse(dpAddrHex);
    free(hex_send_uart);
    free(str_send_uart);
    hex_send_uart = NULL;
    if(check != len_str/2)
    {
        return -1;
    }
    return 0;
}

int ble_controlOnOFF_NODELAY(const char *address_element,const char *state)
{
    char *str_send_uart;
    unsigned char *hex_send_uart;
    int check = 0;

    ble_getStringControlOnOff_SW(&str_send_uart,address_element,state);
    int len_str = (int)strlen(str_send_uart);
    hex_send_uart  = (char*) malloc(len_str * sizeof(char));
    String2HexArr(str_send_uart,hex_send_uart);
    printf("str_send_uart %s\n",str_send_uart );
    check = UART0_Send(fd,hex_send_uart,len_str/2);
    free(hex_send_uart);
    free(str_send_uart);
    hex_send_uart = NULL;
    if(check != len_str/2)
    {
        return -1;
    }
    return 0;
}

bool ble_dimLedSwitch_HOMEGY(const char *address_device,int lightness)
{
    ASSERT(address_device);

    uint8_t  *hex_address = malloc(5);
    uint8_t  *hex_lightness = malloc(5);
    char *lightness_s = malloc(5);


    int lightness_ = lightness*65535/100;
    Int2Hex_2byte(lightness_,lightness_s);

    int check = 0,i = 0;
    uint8_t  SET_DIM[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0x4c,0x00,0x00};


    String2HexArr((char*)address_device,hex_address);
    String2HexArr((char*)lightness_s,hex_lightness);

    SET_DIM[8] = *hex_address;
    SET_DIM[9] = *(hex_address+1);

    SET_DIM[13] = *hex_lightness;
    SET_DIM[12] = *(hex_lightness+1);

    check = UART0_Send(fd,SET_DIM,14);
    free(hex_address);
    free(hex_lightness);
    free(lightness_s);

    hex_address = NULL;
    hex_lightness = NULL;
    lightness_s = NULL;
    if(check != 14)
    {
        return false;
    }
    return true;
}

bool ble_addDeviceToGroupLightCCT_HOMEGY(const char *address_group,const char *address_device,const char *address_element)
{
    ASSERT(address_group && address_device && address_element);
    int check = 0,i = 0;
    uint8_t  SET_GROUP[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00   ,0x00,0x00 ,0x80,0x1b  ,0x00,0x00,0x00,0x00,  0x00,0x10};

    uint8_t hex_address_group[5];
    uint8_t hex_address_device[5];
    uint8_t hex_address_element[5];

    String2HexArr((char*)address_group, hex_address_group);
    String2HexArr((char*)address_device, hex_address_device);
    String2HexArr((char*)address_element, hex_address_element);

    SET_GROUP[8] = *hex_address_device;
    SET_GROUP[9] = *(hex_address_device+1);

    SET_GROUP[12] = *hex_address_element;
    SET_GROUP[13] = *(hex_address_element+1);

    SET_GROUP[14] = *hex_address_group;
    SET_GROUP[15] = *(hex_address_group+1);

    check = UART0_Send(fd, SET_GROUP, 18);
    usleep(1000000);
    if(check != 18)
    {
        return false;
    }
    return true;
}


bool ble_addDeviceToGroupLink(const char *address_group,const char *address_device,const char *address_element)
{
    if (address_group == NULL || address_device == NULL || address_element == NULL) {
        return false;
    }
    int check = 0,i = 0;
    uint8_t  SET_GROUP[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00   ,0x00,0x00 ,0x80,0x1b  ,0x00,0x00,0x00,0x00,  0x00,0x10,0x01};

    uint8_t  *hex_address_group = malloc(5);
    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_address_element = malloc(5);

    String2HexArr((char*)address_group,hex_address_group);
    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)address_element,hex_address_element);

    SET_GROUP[8] = *hex_address_device;
    SET_GROUP[9] = *(hex_address_device+1);

    SET_GROUP[12] = *hex_address_element;
    SET_GROUP[13] = *(hex_address_element+1);

    SET_GROUP[14] = *hex_address_group;
    SET_GROUP[15] = *(hex_address_group+1);

    check = UART0_Send(fd,SET_GROUP,19);
    usleep(DELAY_SEND_UART_MS);
    free(hex_address_group);
    free(hex_address_device);
    free(hex_address_element);

    hex_address_group = NULL;
    hex_address_device = NULL;
    hex_address_element = NULL;

    if(check != 19)
    {
        return false;
    }
    return true;
}

bool ble_deleteDeviceToGroupLightCCT_HOMEGY(const char *address_group,const char *address_device,const char *address_element)
{
    ASSERT(address_group);
    ASSERT(address_device);
    ASSERT(address_element);

    int check = 0,i = 0;
    uint8_t  SET_GROUP[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00   ,0x00,0x00 ,0x80,0x1c  ,0x00,0x00,0x00,0x00,  0x00,0x10};

    uint8_t  *hex_address_group = malloc(5);
    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_address_element = malloc(5);

    String2HexArr((char*)address_group,hex_address_group);
    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)address_element,hex_address_element);

    SET_GROUP[8] = *hex_address_device;
    SET_GROUP[9] = *(hex_address_device+1);

    SET_GROUP[12] = *hex_address_element;
    SET_GROUP[13] = *(hex_address_element+1);

    SET_GROUP[14] = *hex_address_group;
    SET_GROUP[15] = *(hex_address_group+1);
    check = UART0_Send(fd,SET_GROUP,18);
    usleep(DELAY_SEND_UART_MS);
    free(hex_address_group);
    free(hex_address_device);
    free(hex_address_element);

    hex_address_group = NULL;
    hex_address_device = NULL;
    hex_address_element = NULL;

    if(check != 18)
    {
        return false;
    }
    return true;
}

int ble_controlCTL(const char *address_element,int lightness,int colorTemperature)
{
    ASSERT(address_element);

    char *lightness_s = malloc(5);
    char *colorTemperature_s = malloc(5);
    int lightness_ = lightness*65535/1000;
    int colorTemperature_ = 800 + (20000 - 800)*colorTemperature/1000;
    Int2Hex_2byte(lightness_,lightness_s);
    Int2Hex_2byte(colorTemperature_,colorTemperature_s);

    int check = 0,i = 0;
    uint8_t  SET_CT[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x82,0x5e,0x00,0x00,0x00,0x00};

    uint8_t  *hex_address = malloc(5);
    uint8_t  *hex_lightness = malloc(5);
    uint8_t  *hex_colorTemperature = malloc(5);


    String2HexArr((char*)address_element,hex_address);
    String2HexArr((char*)lightness_s,hex_lightness);
    String2HexArr((char*)colorTemperature_s,hex_colorTemperature);

    SET_CT[8] = *hex_address;
    SET_CT[9] = *(hex_address+1);

    SET_CT[13] = *hex_lightness;
    SET_CT[12] = *(hex_lightness+1);
    SET_CT[15] = *hex_colorTemperature;
    SET_CT[14] = *(hex_colorTemperature+1);

    check = UART0_Send(fd,SET_CT,16);
    free(hex_address);
    free(hex_lightness);
    free(hex_colorTemperature);
    free(lightness_s);
    free(colorTemperature_s);

    hex_address = NULL;
    hex_lightness = NULL;
    hex_colorTemperature = NULL;
    lightness_s = NULL;
    colorTemperature_s = NULL;
    if(check != 16)
    {
        return false;
    }
    return true;
}

int ble_controlHSL(const char *address_element,const char *HSL)
{
    int check = 0,i = 0;
    uint8_t  SET_HSL[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x82,0x76,0x00,0x00,0x00,0x00,0x00,0x00};

    uint8_t  *hex_address = malloc(5);
    uint8_t  *hex_HSL = malloc(13);
    String2HexArr((char*)address_element,hex_address);
    String2HexArr((char*)HSL,hex_HSL);

    SET_HSL[8] = *hex_address;
    SET_HSL[9] = *(hex_address+1);

    SET_HSL[13] = *hex_HSL;
    SET_HSL[12] = *(hex_HSL+1);
    SET_HSL[15] = *(hex_HSL+2);
    SET_HSL[14] = *(hex_HSL+3);
    SET_HSL[17] = *(hex_HSL+4);
    SET_HSL[16] = *(hex_HSL+5);

    check = UART0_Send(fd,SET_HSL,18);
    free(hex_address);
    free(hex_HSL);
    hex_address = NULL;
    hex_HSL = NULL;
    if(check != 18)
    {
        return false;
    }
    return true;
}


int ble_controlModeBlinkRGB(const char *address_element,const char *modeBlinkRgb)
{
    int check = 0,i = 0;
    uint8_t  SET_BLINK[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0x82,0x50,0x19,0x09,  0x00};

    uint8_t  *hex_address = malloc(5);
    uint8_t  *hex_modeBlinkRgb = malloc(2);

    String2HexArr((char*)address_element,hex_address);
    String2HexArr((char*)modeBlinkRgb,hex_modeBlinkRgb);

    SET_BLINK[8] = *hex_address;
    SET_BLINK[9] = *(hex_address+1);

    SET_BLINK[14] = *hex_modeBlinkRgb;

    check = UART0_Send(fd,SET_BLINK,15);


    free(hex_address);
    free(hex_modeBlinkRgb);
    hex_address = NULL;
    hex_modeBlinkRgb = NULL;
    if(check != 15)
    {
        return false;
    }
    return true;
}



long long GW_getSizeMessage(char *size_reverse_2_byte_hex)
{
    long long decimal = 0;
    char *size_2_byte_hex = malloc(sizeof(char)*5);
    size_2_byte_hex[0] = size_reverse_2_byte_hex[2];
    size_2_byte_hex[1] = size_reverse_2_byte_hex[3];
    size_2_byte_hex[2] = size_reverse_2_byte_hex[0];
    size_2_byte_hex[3] = size_reverse_2_byte_hex[1];
    size_2_byte_hex[4] = '\0';
    int size = strlen(size_2_byte_hex);
    int i = 0;
    decimal = Hex2Dec(size_2_byte_hex);
    return decimal;
}


int GW_SplitFrame(ble_rsp_frame_t resultFrames[MAX_FRAME_COUNT], uint8_t* originPackage, size_t size) {
    int frameCount = 0;
    for (int i = 2; i < size - 1; i++) {
        // Find the position of FLAG mark (2 bytes: 0x9181)
        if (originPackage[i] == 0x91) {
            // Calculate length of a single frame (2 bytes before FLAG mark)
            int frameSize = originPackage[i - 1];
            int frameIdx = 0;
            frameSize = (frameSize << 8) | originPackage[i - 2];
            resultFrames[frameCount].frameSize = frameSize;
            resultFrames[frameCount].flag = ((uint16_t)originPackage[i] << 8) | originPackage[i + 1];;
            if (originPackage[i + 1] == 0x9d) {
                // Online/Online frame
                resultFrames[frameCount].onlineState = originPackage[i + 8] > 0? 1 : 0;
                resultFrames[frameCount].onlineState2 = originPackage[i + 14] > 0? 1 : 0;
                resultFrames[frameCount].sendAddr = ((uint16_t)originPackage[i + 6] << 8) | originPackage[i + 7];
                resultFrames[frameCount].sendAddr2 = ((uint16_t)originPackage[i + 12] << 8) | originPackage[i + 13];
            } else {
                // Other frames
                resultFrames[frameCount].sendAddr = ((uint16_t)originPackage[i + 2] << 8) | originPackage[i + 3];
                resultFrames[frameCount].recvAddr = ((uint16_t)originPackage[i + 4] << 8) | originPackage[i + 5];
                resultFrames[frameCount].opcode = ((uint16_t)originPackage[i + 6] << 8) | originPackage[i + 7];
            }
            // Read param
            resultFrames[frameCount].paramSize = 0;
            uint8_t* param = resultFrames[frameCount].param;
            int j = 0;
            for (j = i + 8; j < i + frameSize; j++) {
                *param = originPackage[j];
                resultFrames[frameCount].paramSize++;
                param++;
            }
            frameCount++;
            i = j;
        }
    }
    return frameCount;
}

char str[10];
int check_form_recived_from_RX(struct state_element *temp, ble_rsp_frame_t* frame)
{
    g_bleFrameSender = frame->sendAddr;
    char *address_t = (char *)malloc(5);
    char *value_t = (char *)malloc(10);
    memset(address_t,'\0',5);
    memset(value_t,'\0',10);
    uint8_t len_uart = frame->frameSize + 2;

    sprintf(address_t, "%04X", frame->sendAddr);
    temp->address_element=address_t;

    // Online/Offline
    if (frame->flag == 0x919d) {
        return GW_RESPONSE_DEVICE_STATE;
    }
    // Device is kicked out from mesh network
    else if (frame->opcode == 0x804a)
    {
        temp->value = "0";
        return GW_RESPONSE_DEVICE_KICKOUT;
    }
    // ADD_GROUP_LIGHT
    else if (frame->paramSize >= 7 && frame->opcode == 0x801f && frame->param[5] == 0x00 && frame->param[6] == 0x10)
    {
        return GW_RESPONSE_ADD_GROUP_LIGHT;
    }
    // Homegy smart switch, Rang Dong light
    if (frame->opcode == 0x8204)
    {
        if (frame->paramSize == 4) {
            sprintf(str, "%d", frame->param[0]);
            temp->dpValue = frame->param[0];
            temp->causeType = frame->param[3];
            sprintf(temp->causeId, "%02X%02X", frame->param[1], frame->param[2]);
        } else if (frame->paramSize > 1) {
            sprintf(str, "%d", frame->param[1]);
            temp->dpValue = frame->param[1];
        } else {
            sprintf(str, "%d", frame->param[0]);
            temp->dpValue = frame->param[0];
        }
        temp->value = str;
        g_bleFrameRespType = GW_RESPONSE_DEVICE_CONTROL;
        return GW_RESPONSE_DEVICE_CONTROL;
    }

    // Smoke sensor
    else if(frame->opcode == 0x5208 && frame->paramSize >= 3 && frame->param[0] == 0x01) {
        if (frame->param[1] == 0x00)
        {
            if (frame->param[2] == 0x00)
            {
                sprintf(value_t, "%s%s", "00","00");
            }
            else
            {
                sprintf(value_t, "%s%s", "00","01");
            }
        }
        else if(frame->param[1] == 0x01)
        {
            if (frame->param[2] == 0x00)
            {
                sprintf(value_t, "%s%s", "01","00");
            }
            else
            {
                sprintf(value_t, "%s%s", "01","01");
            }
        }
        temp->value = value_t;
        return GW_RESPONSE_SMOKE_SENSOR;
    }

    // Temperature/Humidity sensor
    else if (frame->opcode == 0x5206 && frame->paramSize >= 5 && frame->param[0] == 0x00)
    {
        char *nhietdo_ = malloc(10);
        sprintf(nhietdo_, "%02X%02X", frame->param[1], frame->param[2]);
        char *doam_ = malloc(10);
        sprintf(doam_, "%02X%02X", frame->param[3], frame->param[4]);
        sprintf(value_t, "%lld%lld", Hex2Dec(nhietdo_),Hex2Dec(doam_));
        temp->value = value_t;
        free(nhietdo_);
        free(doam_);
        return GW_RESPONSE_SENSOR_ENVIRONMENT;
    }

    //type % pin
    else if(frame->opcode == 0x5201 && frame->paramSize >= 3 && frame->param[0] == 0x00)
    {
        sprintf(value_t, "%d", frame->param[2]);
        temp->value = value_t;
        return GW_RESPONSE_SENSOR_BATTERY;
    }

    // Door sensor detect
    else if(frame->opcode == 0x5209 && frame->paramSize >= 2 && frame->param[0] == 0x00)
    {
        return GW_RESPONSE_SENSOR_DOOR_DETECT;
    }

    // Door sensor hanging detect
    else if(frame->opcode == 0x5209 && frame->paramSize >= 2 && frame->param[0] == 0x04)
    {
        return GW_RESPONSE_SENSOR_DOOR_ALARM;
    }

    // PIR sensor human detect
    else if(frame->opcode == 0x5205 && frame->paramSize >= 2 && frame->param[0] == 0x00)
    {
        return GW_RESPONSE_SENSOR_PIR_DETECT;
    }

    // PIR sensor light intensity
    else if(frame->opcode == 0x5204 && frame->paramSize >= 3 && frame->param[0] == 0x00)
    {
        return GW_RESPONSE_SENSOR_PIR_LIGHT;
    }

    // else if(rcv_uart_buff[2] == 0x91&& rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0x82 &&rcv_uart_buff[9] == 0x45)
    // {
    //     printf("GW_RESPONSE_SCENE_LC_WRITE_INTO_DEVICE\n");
    //     sprintf(address_t, "%02X%02X", rcv_uart_buff[4],rcv_uart_buff[5]);
    //     temp->address_element=address_t;
    //     if(rcv_uart_buff[10] == 0x00)
    //     {
    //         temp->value = KEY_TRUE;
    //     }
    //     else
    //     {
    //         temp->value = KEY_FALSE;
    //     }
    //     return GW_RESPONSE_SCENE_LC_WRITE_INTO_DEVICE;
    // }
    // else if(rcv_uart_buff[2] == 0x91&& rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0xE1 &&rcv_uart_buff[9] == 0x11 && rcv_uart_buff[10] == 0x02 && rcv_uart_buff[11] == 0x04 && rcv_uart_buff[12] == 0x00)
    // {
    //     printf("GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE\n");
    //     sprintf(address_t, "%02X%02X", rcv_uart_buff[4],rcv_uart_buff[5]);
    //     temp->address_element=address_t;
    //     temp->value = KEY_TRUE;
    //     return GW_RESPONSE_SCENE_LC_CALL_FROM_DEVICE;
    // }

    // //GW_RESPONSE_DIM_LED_SWITCH_HOMEGY
    // else if(rcv_uart_buff[2] == 0x91&& rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0x82 &&rcv_uart_buff[9] == 0x4E)
    // {
    //     printf("GW_RESPONSE_DIM_LED_SWITCH_HOMEGY\n");
    //     sprintf(address_t, "%02X%02X", rcv_uart_buff[4],rcv_uart_buff[5]);
    //     sprintf(value_t, "%02X%02X%02X%02X%02X", rcv_uart_buff[10],rcv_uart_buff[11],rcv_uart_buff[12],rcv_uart_buff[13],rcv_uart_buff[14]);
    //     temp->address_element=address_t;
    //     temp->value = value_t;
    //     return GW_RESPONSE_DIM_LED_SWITCH_HOMEGY;
    // }


    // //SAVE GW RANGDONG
    // else if(rcv_uart_buff[2] == 0x91&& rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0xE1 &&rcv_uart_buff[9] == 0x11 && rcv_uart_buff[10] == 0x02 && rcv_uart_buff[11] == 0x02 && rcv_uart_buff[10] == 0x02 && rcv_uart_buff[11] == 0x02 && rcv_uart_buff[15] == 0x01 && rcv_uart_buff[16] == 0x02)
    // {
    //     printf("SAVE_GW SW RD\n");
    //     sprintf(address_t, "%02X%02X", rcv_uart_buff[4],rcv_uart_buff[5]);
    //     sprintf(value_t, "%02X%02X",rcv_uart_buff[13],rcv_uart_buff[14]);
    //     temp->address_element=address_t;
    //     temp->value = value_t;
    //     return GW_RESPONSE_SAVE_GATEWAY_RD;
    //     // return GW_RESPONSE_SAVE_GATEWAY_HG;
    // }

    // //GW_RESPONSE_SET_TIME_SENSOR_PIR
    // else if(rcv_uart_buff[2] == 0x91&& rcv_uart_buff[3] == 0x81 && rcv_uart_buff[8] == 0xE1 &&rcv_uart_buff[9] == 0x11 && rcv_uart_buff[10] == 0x02 && rcv_uart_buff[11] == 0x45 && rcv_uart_buff[12] == 0x03)
    // {
    //     printf("GW_RESPONSE_SET_TIME_SENSOR_PIR\n");
    //     sprintf(address_t, "%02X%02X", rcv_uart_buff[4],rcv_uart_buff[5]);
    //     sprintf(value_t, "%02X%02X",rcv_uart_buff[14],rcv_uart_buff[13]);
    //     temp->address_element=address_t;
    //     temp->value = value_t;
    //     return GW_RESPONSE_SET_TIME_SENSOR_PIR;
    // }
    free(address_t);
    free(value_t);
    return GW_RESPONSE_UNKNOW;
}


void getStringResetDeviveSofware(char **result,const char* addressDevice)
{
    *result  = (char*) malloc(25 * sizeof(char));
    // memset(*result,'\0',25*sizeof(*result));
    const char *tmp_0 = "e8ff000000000000";
    char *tmp_1 = (char *)addressDevice;
    const char *tmp_2 = "8049";
    strcpy(*result, tmp_0);
    strcat(*result, tmp_1);
    strcat(*result, tmp_2);
}

bool setResetDeviceSofware(const char *addressDevice)
{
    char *str_send_uart;
    unsigned char *hex_send_uart;
    int check = 0;
    getStringResetDeviveSofware(&str_send_uart,addressDevice);
    int len_str = (int)strlen(str_send_uart);
    hex_send_uart  = (char*) malloc(len_str * sizeof(char));
    // memset(hex_send_uart,'\0',len_str*sizeof(hex_send_uart));
    String2HexArr(str_send_uart,hex_send_uart);
    check = UART0_Send(fd,hex_send_uart,len_str/2);
    usleep(DELAY_SEND_UART_MS);
    free(hex_send_uart);
    free(str_send_uart);
    hex_send_uart = NULL;
    if(check != len_str/2)
    {
        return false;
    }
    return true;
}


bool IsHasDeviceIntoDabase(const JSON_Value *object_devices,const char* deviceID)
{
    return json_object_has_value(json_object(object_devices),deviceID)?true:false;
}

bool getPidDevice(char **result,const JSON_Value *object_devices,const char* deviceID)
{
    char *pid = NULL;
    char *temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s.%s",deviceID,KEY_DICT_INFO,KEY_PID);
    pid = (char *)json_object_dotget_string(json_object(object_devices),temp);
    int size_t = strlen(pid);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,pid);    
    free(temp);
}



bool getAndressFromDeviceAndDpid(char **result,const JSON_Value *object_devices,const char* deviceID,const char* dpID)
{
    char *andress = NULL;
    char *temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s.%s",deviceID,KEY_DICT_META,dpID);
    andress = (char *)json_object_dotget_string(json_object(object_devices),temp);
    int size_t = strlen(andress);
    *result = malloc(size_t+1);
    memset(*result,'\0',size_t+1);
    strcpy(*result,andress);    
    free(temp);
}

int getNumberElementNeedControl(JSON_Value *object)
{
    return json_object_get_count(json_object_get_object(json_object(object),KEY_DICT_DPS));
}

bool getInfoControlOnOffBLE(InfoControlDeviceBLE  *InfoControlDeviceBLE_t,const JSON_Value *object_devices,const char* deviceID,const char* dpID)
{
    char *temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s.%s",deviceID,KEY_DICT_META,dpID);
    InfoControlDeviceBLE_t->address = (char *)json_object_dotget_string(json_object(object_devices),temp);
    free(temp);
    return true;
}

bool getInfoControlCTL_BLE(InfoControlCLT_BLE  *InfoControlCLT_BLE_t,const JSON_Value *object_devices,const char *object_string)
{
    JSON_Value *object = json_parse_string(object_string);
    const char* deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);

    char *temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s.%s",deviceID,KEY_DICT_META,"22");
    InfoControlCLT_BLE_t->address = (char *)json_object_dotget_string(json_object(object_devices),temp);
    free(temp);

    temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s",KEY_DICT_DPS,"22");
    InfoControlCLT_BLE_t->lightness = json_object_dotget_number(json_object(object),temp);
    free(temp);

    temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s",KEY_DICT_DPS,"23");
    InfoControlCLT_BLE_t->colorTemperature = json_object_dotget_number(json_object(object),temp);
    free(temp);

    return true;
}

bool getInfoControlHSL_BLE(InfoControlHSL_BLE  *InfoControlHSL_BLE_t,const JSON_Value *object_devices,const char *object_string)
{
    JSON_Value *object = json_parse_string(object_string);
    const char* deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);

    char *temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s.%s",deviceID,KEY_DICT_META,"24");
    InfoControlHSL_BLE_t->address = (char *)json_object_dotget_string(json_object(object_devices),temp);
    free(temp);

    temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s",KEY_DICT_DPS,"24");
    InfoControlHSL_BLE_t->valueHSL = (char *)json_object_dotget_string(json_object(object),temp);
    free(temp);

    return true;
}

bool getInfoControlModeBlinkRGB_BLE(InfoControlBlinkRGB_BLE  *InfoControlBlinkRGB_BLE_t,const JSON_Value *object_devices,const char *object_string)
{
    JSON_Value *object = json_parse_string(object_string);
    const char* deviceID = json_object_get_string(json_object(object),KEY_DEVICE_ID);

    char *temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s.%s",deviceID,KEY_DICT_META,"21");
    InfoControlBlinkRGB_BLE_t->address = (char *)json_object_dotget_string(json_object(object_devices),temp);
    free(temp);

    temp = ( char *)calloc(50,sizeof(char));
    sprintf(temp,"%s.%s",KEY_DICT_DPS,"21");
    InfoControlBlinkRGB_BLE_t->modeBlinkRgb = (char *)json_object_dotget_string(json_object(object),temp);
    free(temp);

    return true;
}


bool ble_setSceneLocalToDeviceSwitch(const char* address_device,const char* sceneID, const char* dimLED,const char* element_count,const char* param)
{
    int check = 0,i = 0;
    //################################FLAG###################################address_device####ofcode#####sceneID#####DIM##ELE_COUNT##########para############
    uint8_t  SceneLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,      0x82,0x46,  0x00,0x00, 0x00,  0x00,       0x00,0x00,0x00,0x00,};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_sceneID = malloc(5);
    uint8_t  *hex_DIM = malloc(3);
    uint8_t  *hex_element_count = malloc(3);
    uint8_t  *hex_pare = malloc(9);

    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);
    String2HexArr((char*)dimLED,hex_DIM);
    String2HexArr((char*)element_count,hex_element_count);
    String2HexArr((char*)param,hex_pare);

    SceneLocalToDevice[8] = *hex_address_device;
    SceneLocalToDevice[9] = *(hex_address_device+1);

    SceneLocalToDevice[12] = *hex_sceneID;
    SceneLocalToDevice[13] = *(hex_sceneID+1);


    SceneLocalToDevice[14] = *hex_DIM;

    SceneLocalToDevice[15] = *hex_element_count;


    SceneLocalToDevice[16] = *hex_pare;
    SceneLocalToDevice[17] = *(hex_pare+1);
    SceneLocalToDevice[18] = *(hex_pare+2);
    SceneLocalToDevice[19] = *(hex_pare+3);


    check = UART0_Send(fd,SceneLocalToDevice,20);
    free(hex_address_device);
    free(hex_sceneID);
    free(hex_DIM);
    free(hex_element_count);
    free(hex_pare);
    hex_address_device = NULL;
    hex_sceneID = NULL;
    hex_DIM = NULL;
    hex_element_count = NULL;
    hex_pare = NULL;
    if(check != 20)
    {
        return false;
    }
    return true;
}

bool ble_setSceneLocalToDeviceLightCCT_HOMEGY(const char* address_device,const char* sceneID)
{
    int check = 0,i = 0;
    //################################FLAG###################################address_device####ofcode#####sceneID#####DIM##ELE_COUNT##########para############
    uint8_t  SceneLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,      0x82,0x46,  0x00,0x00};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_sceneID = malloc(5);

    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);

    SceneLocalToDevice[8] = *hex_address_device;
    SceneLocalToDevice[9] = *(hex_address_device+1);

    SceneLocalToDevice[12] = *hex_sceneID;
    SceneLocalToDevice[13] = *(hex_sceneID+1);


    check = UART0_Send(fd,SceneLocalToDevice,14);
    free(hex_address_device);
    free(hex_sceneID);

    hex_address_device = NULL;
    hex_sceneID = NULL;

    if(check != 14)
    {
        return false;
    }
    return true;
}

bool ble_setSceneLocalToDeviceLight_RANGDONG(const char* address_device,const char* sceneID,const char* modeBlinkRgb )
{
    int check = 0,i = 0;
    //################################FLAG###################################address_device####ofcode#####sceneID####modeBlinkRgb##############
    uint8_t  SceneLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,   0x00,0x00,   0x82,0x46,  0x00,0x00,     0x00,         0x00,0x00};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_sceneID = malloc(5);
    uint8_t  *hex_modeBlinkRgb = malloc(3);

    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);
    String2HexArr((char*)modeBlinkRgb,hex_modeBlinkRgb);

    SceneLocalToDevice[8] = *hex_address_device;
    SceneLocalToDevice[9] = *(hex_address_device+1);

    SceneLocalToDevice[12] = *hex_sceneID;
    SceneLocalToDevice[13] = *(hex_sceneID+1);

    SceneLocalToDevice[14] = *hex_modeBlinkRgb;


    check = UART0_Send(fd,SceneLocalToDevice,17);
    free(hex_address_device);
    free(hex_sceneID);
    free(hex_modeBlinkRgb);

    hex_address_device = NULL;
    hex_sceneID = NULL;
    hex_modeBlinkRgb = NULL;

    if(check != 17)
    {
        return false;
    }
    return true;
}

bool ble_callSceneLocalToDevice(const char* address_device,const char* sceneID, const char* enableOrDisable, uint8_t dpValue)
{
    if (address_device == NULL || sceneID == NULL || enableOrDisable == NULL) {
        return false;
    }
    int check = 0,i = 0;
    //################################FLAG###################################address_device####ofcode###############################################sceneID############
    uint8_t  CallSceneLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,      0xE0,0x11,0x02,0x00,0x00,     0x04,0x00,          0x00,0x00,  0x00, 0x00};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_sceneID = malloc(5);
    uint8_t  *hex_state = malloc(3);

    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);
    String2HexArr((char*)enableOrDisable,hex_state);


    CallSceneLocalToDevice[8] = *hex_address_device;
    CallSceneLocalToDevice[9] = *(hex_address_device+1);

    CallSceneLocalToDevice[17] = *hex_sceneID;
    CallSceneLocalToDevice[18] = *(hex_sceneID+1);
    CallSceneLocalToDevice[19] = *hex_state;
    CallSceneLocalToDevice[20] = dpValue;

    check = UART0_Send(fd, CallSceneLocalToDevice, 21);
    free(hex_address_device);
    free(hex_sceneID);
    free(hex_state);
    hex_address_device = NULL;
    hex_sceneID = NULL;
    hex_state = NULL;
    if(check != 20)
    {
        return false;
    }
    return true;
}

bool ble_delSceneLocalToDevice(const char* address_device,const char* sceneID)
{
    int check = 0,i = 0;
    //################################FLAG###################################address_device####ofcode#####sceneID#####
    uint8_t  DelLocalToDevice[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,      0x82,0x9E,  0x00,0x00};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_sceneID = malloc(5);


    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);


    DelLocalToDevice[8] = *hex_address_device;
    DelLocalToDevice[9] = *(hex_address_device+1);

    DelLocalToDevice[12] = *hex_sceneID;
    DelLocalToDevice[13] = *(hex_sceneID+1);


    check = UART0_Send(fd,DelLocalToDevice,14);
    free(hex_address_device);
    free(hex_sceneID);
    hex_address_device = NULL;
    hex_sceneID = NULL;
    if(check != 14)
    {
        return false;
    }
    return true;
}

bool ble_callSceneLocalToHC(const char* address_device,const char* sceneID)
{
    int check = 0,i = 0;
    //################################FLAG###################################address_device####ofcode#######sceneID############
    uint8_t  CallSceneLocalToHC[14] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,      0x82,0x42,  0x00,0x00 ,     0x00,0x00,0x00};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_sceneID = malloc(5);

    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)sceneID,hex_sceneID);


    CallSceneLocalToHC[8] = *hex_address_device;
    CallSceneLocalToHC[9] = *(hex_address_device+1);

    CallSceneLocalToHC[12] = *hex_sceneID;
    CallSceneLocalToHC[13] = *(hex_sceneID+1);

    printf("CallSceneLocalToHC : ");
    for(i=0;i<17;i++)
    {
        printf("  %02X",CallSceneLocalToHC[i]);
    }
    printf("\n\n");
    check = UART0_Send(fd,CallSceneLocalToHC,17);
    free(hex_address_device);
    free(hex_sceneID);
    hex_address_device = NULL;
    hex_sceneID = NULL;
    if(check != 17)
    {
        return false;
    }
    return true;
}

bool ble_setTimeForSensorPIR(const char* address_device,const char* time)
{
    int check = 0,i = 0;
    //################################FLAG###################################address_device################ofcode###########header########time#####
    uint8_t  setTimeForSensorPIR[19] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00,      0x00,0x00,      0xE2,0x11,0x02,0xE3,0x00,  0x45,0x03,  0x00,0x00};

    uint8_t  *hex_address_device = malloc(5);
    uint8_t  *hex_time = malloc(5);


    String2HexArr((char*)address_device,hex_address_device);
    String2HexArr((char*)time,hex_time);


    setTimeForSensorPIR[8] = *hex_address_device;
    setTimeForSensorPIR[9] = *(hex_address_device+1);

    setTimeForSensorPIR[17] = *(hex_time+1);
    setTimeForSensorPIR[18] = *(hex_time);


    check = UART0_Send(fd,setTimeForSensorPIR,19);
    free(hex_address_device);
    free(hex_time);
    hex_address_device = NULL;
    hex_time = NULL;
    if(check != 19)
    {
        return false;
    }
    return true;
}

char *get_dpid(const char *code)
{
    return (char *)code+3;
}


int ble_logDeivce(const char *address_element,int state)
{
    ASSERT(address_element);

    int check = 0,i = 0;
    uint8_t  LOG_DEVICE[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0xE0,0x11,0x02,0x00,0x00,  0x05,0x00,   0x00};

    uint8_t  *hex_address = malloc(5);
    String2HexArr((char*)address_element,hex_address);

    LOG_DEVICE[8] = *hex_address;
    LOG_DEVICE[9] = *(hex_address+1);

    if(state == 1)
    {
        LOG_DEVICE[17] = 0x01;
    }
    else
    {
        LOG_DEVICE[17] = 0x00;
    }

    check = UART0_Send(fd,LOG_DEVICE,18);
    free(hex_address);
    hex_address = NULL;
    if(check != 18)
    {
        return false;
    }
    return true;
}

int ble_logTouch(const char *address_element, uint8_t dpId, int state)
{
    int check = 0,i = 0;
    uint8_t  LOG_TOUCH[] = {0xe8,0xff,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00, 0xE0,0x11,0x02,0x00,0x00,  0x0D,0x00,   0x00,   0x00};

    uint8_t  *hex_address = malloc(5);

    String2HexArr((char*)address_element,hex_address);

    LOG_TOUCH[8] = (*hex_address) + (dpId - 1);
    LOG_TOUCH[9] = *(hex_address+1);
    LOG_TOUCH[17] = state;
    check = UART0_Send(fd, LOG_TOUCH, 18);
    usleep(DELAY_SEND_UART_MS);
    free(hex_address);
    hex_address = NULL;
    if(check != 19)
    {
        return false;
    }
    return true;
}
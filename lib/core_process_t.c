#include "core_process_t.h"

char* Hex2String( char *hex,unsigned int len)
{
    int i;
    char *app = malloc(sizeof(char)*3);//char *app = malloc(sizeof(char)) error heap-buffer-overflow
    char *buff = malloc(len*2+1);

    strcpy(buff,"");
    for (i=0;i<len;i++)
    {
        sprintf(app,"%02x",hex[i]);
        strcat(buff,app);
    }
    buff[2*len] = '\0';
    return buff;
}


void String2HexArr(char *givenStr, unsigned char *hexStr)
{
    int length = (int)strlen(givenStr);
    int i  = 0;
    char temp1[3] = {'\0'};
    for(i = 0;i< length/2;i ++) 
    {
        temp1[0] = givenStr[i*2];
        temp1[1] = givenStr[i*2+1];
        hexStr[i] = strtol(temp1,NULL,16);
    }
}

int String2Int(const char *str)
{
    if (str) {
       return atoi(str);
    }
    return 0;
}

int Int2String(int i,char *str)
{
    int len = 0;
    len = sprintf(str, "%d", i);
    return len;
}

bool isMatchString(const char *string_1,const char *string_2 )
{
    if(string_1 != NULL && string_2 != NULL)
    {
        if(strcmp((char *)string_1,(char *)string_2) == 0)
        {
            return true;
        }
        else
            return false;       
    }
    else
        return false;
}


int Int2Hex_2byte(int value,char *str)
{
    char array[2];

    int len = 0;
    array[0] = value >> 8;
    array[1] = value & 0xff;

    len = sprintf(str,"%02x%02x",array[0], array[1]);
    return len;
}


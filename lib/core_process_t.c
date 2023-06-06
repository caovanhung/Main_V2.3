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

// uint16_t String2Hex(char *str, unsigned char *hexStr)
// {
//     int length = (int)strlen(str);
//     int i  = 0;
//     char temp1[3] = {'\0'};
//     for(i = 0;i< length/2;i ++)
//     {
//         temp1[0] = str[i*2];
//         temp1[1] = str[i*2+1];
//         hexStr[i] = strtol(temp1,NULL,16);
//     }
// }

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

char *strremove(char *str, const char *sub) 
{
    char *p, *q, *r;
    if (*sub && (q = r = strstr(str, sub)) != NULL) 
    {
        size_t len = strlen(sub);
        while ((r = strstr(p = r + len, sub)) != NULL) 
        {
            while (p < r)
                *q++ = *p++;
        }
        while ((*q++ = *p++) != '\0')
            continue;
    }
    return str;
}


int Min2Sec(const char *minutes,const char *seconds)
{
    int min = String2Int(minutes);
    int sec = String2Int(seconds);
    return min*60+sec;
}

long long Hex2Dec(char *hex)
{
    long long decimal = 0, base = 1;
    int i = 0, length;

    length = strlen(hex);
    for(i = length--; i >= 0; i--)
    {
        if(hex[i] >= '0' && hex[i] <= '9')
        {
            decimal += (hex[i] - 48) * base;
            base *= 16;
        }
        else if(hex[i] >= 'A' && hex[i] <= 'F')
        {
            decimal += (hex[i] - 55) * base;
            base *= 16;
        }
        else if(hex[i] >= 'a' && hex[i] <= 'f')
        {
            decimal += (hex[i] - 87) * base;
            base *= 16;
        }
    }
    return decimal;
}


char ** generate_fields(int rows, int length) 
{
   int i = 0;
   char ** options = (char **)malloc(rows*sizeof(char*));
   for(i = 0; i < rows; i++)
       options[i] = (char *)malloc(length*sizeof(char));
   return options;
}


void free_fields(char ** options,int rows)
{
    int i = 0;
    for(i = 0; i < rows; i++)
    {
        free(options[i]);
    }
    free(options);
}

void splitString(char str[], char delim, char **result, int *numParts) {
    *numParts = 0;
    char *token = strtok(str, &delim);
    while (token != NULL) {
        result[*numParts] = (char*) malloc(strlen(token) + 1);
        strcpy(result[*numParts], token);
        (*numParts)++;
        token = strtok(NULL, &delim);
    }
}


char** str_split(char* a_str, const char a_delim,int *size)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;
    *size = count;
    result = generate_fields(count,100);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        // assert(idx == count - 1);
        *(result + idx) = 0;
    }

    return result;
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

int Int2Hex_1byte(int value,char *str)
{
    char array[1];

    int len = 0;
    array[0] = value & 0xff;

    len = sprintf(str,"%02x",array[0]);
    return len; 
}


char * my_strcat(const char * str1, const char * str2)
{
   char * ret = calloc(strlen(str1)+strlen(str2)+1,sizeof(char));

   if(ret!=NULL)
   {
     sprintf(ret, "%s%s", str1, str2);
     return ret;
   }
   return NULL;    
}


bool append_String2String(char** original, char* to_append) 
{
    if(to_append == NULL)
    {
        return false;
    }
    int len_original = strlen(*original);
    int len_to_append = strlen(to_append);
    *original = (char*)realloc(*original, len_original + len_to_append+1);
    strcat(*original, to_append);
    return true;
}

int findSubstring(char* str, char* substr) {
    char* ptr = strstr(str, substr);
    if (ptr != NULL) {
        return ptr - str;
    }
    return -1;
}
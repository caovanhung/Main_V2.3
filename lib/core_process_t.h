#ifndef core_process_t_h
#define core_process_t_h


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
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

char* Hex2String( char *hex,unsigned int len);

/*******************************************************************
* name:         String2HexArr
* function:     Convert a string to array hex 2 byte
* in param:     givenStr: input string need convert
*               hexStr  : input hex after convert                     
* out param:    return result into pointer hexStr 
*******************************************************************/
void String2HexArr(char *givenStr, unsigned char *hexStr);


int String2Int(const char *str);


int Int2String(int i,char *str);

/*******************************************************************
* name:         isMatchString
* function:     check 2 strings are the same
* in param:     string_1
*               string_2                      
* out param:        return true if same, or false if not same
*******************************************************************/
bool isMatchString(const char *string_1,const char *string_2 );

/*******************************************************************
* name:             Int2Hex_2byte
* function:         convert integers to 2-byte hexadecimal strings, respectively
* in param:         
                    
* out param:        
*******************************************************************/
int Int2Hex_2byte(int value,char *str);


#endif

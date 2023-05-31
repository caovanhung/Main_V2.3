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
* out param:	return result into pointer hexStr 
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
* name:             isContainString
* function:         which checks if a given string contains another given string
* in param:         string_contain: 
                    string_match  : string need check
* out param:        True if success or False if Failed
*******************************************************************/
bool isContainString(const char *string_contain,const char *string_match);

/*******************************************************************
* name:             strremove
* function:         removes all occurrences of a substring from a given string
* in param:         str:  
                    sub:
* out param:        pointer after removes
*******************************************************************/
char *strremove(char *str, const char *sub);

/*******************************************************************
* name:             Min2Sec
* function:         converts minutes and seconds to seconds
* in param:         minutes: 
                    seconds:
* out param:        
*******************************************************************/
int Min2Sec(const char *minutes,const char *seconds);

/*******************************************************************
* name:             Hex2Dec
* function:         converts a hexadecimal value to its decimal equivalent
* in param:         
                    
* out param:        
*******************************************************************/
long long Hex2Dec(char *hex);

/*******************************************************************
* name:             generate_fields
* function:         allocate memory for an array of strings, respectively
* in param:         
                    
* out param:        
*******************************************************************/
char ** generate_fields(int rows, int length);

/*******************************************************************
* name:             free_fields
* function:         free memory for an array of strings, respectively
* in param:         
                    
* out param:        
*******************************************************************/
void free_fields(char ** options,int rows);


void splitString(char str[], char delim, char **result, int *numParts);


/*******************************************************************
* name:             str_split
* function:         splits a given string into multiple substrings based on the given delimiter
* in param:         
                    
* out param:        
*******************************************************************/
char** str_split(char* a_str, const char a_delim,int *size);

/*******************************************************************
* name:             Int2Hex_2byte
* function:         convert integers to 2-byte hexadecimal strings, respectively
* in param:         
                    
* out param:        
*******************************************************************/
int Int2Hex_2byte(int value,char *str);

/*******************************************************************
* name:             Int2Hex_1byte
* function:         convert integers to 1-byte hexadecimal strings, respectively
* in param:         
                    
* out param:        
*******************************************************************/
int Int2Hex_1byte(int value,char *str);

/*******************************************************************
* name:             free_value
* function:         
* in param:         
                    
* out param:        
*******************************************************************/
bool free_value(char *value);

/*******************************************************************
* name:             my_strcat
* function:         concatenate two strings
* in param:         
                    
* out param:        
*******************************************************************/
char * my_strcat(const char * str1, const char * str2);

/*******************************************************************
* name:             append_String2String
* function:         appen a string to a string, using realloc
* in param:         original: pointer of pointer to original string 
                    to_append  :pointer want add to tring
* out param:        1 if success or 0 if fail
*******************************************************************/
bool append_String2String(char** original, char* to_append);

int findSubstring(char* str, char* substr);

#endif

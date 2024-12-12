#include"stdio.h"      /* Standard input and output */
#include"stdlib.h"     /* Standard library */
#include"unistd.h"     /* Standard function definition for UNIX */
#include"sys/types.h" 
#include"sys/stat.h"   
#include"fcntl.h"      /* Definition for file control*/
#include"termios.h"    /* Definition for terminal control*/
#include"errno.h"      /* Definition for error signal*/
#include"string.h"
#include <stdbool.h>

/*******************************************************************
* name:             UART_Open
* function:         open the UART and get the file descriptor
* in param:         fd: file descriptor    port:UART devices(ttyS0, ttyS1, ttyS2)
* out param:        1 if success or 0 if fail
*******************************************************************/
int UART_Open(int fd,char* port);


/*******************************************************************
* name:         UART_Close
* function:     Close the UART
* in param:     fd: file descriptor port:UART(ttyS0,ttyS1,ttyS2)
* out param:    void
*******************************************************************/
void UART_Close(int fd);

/*******************************************************************
* name:         UART_Set
* function:     set the data bits, stop bit, paraity bit of UART
* in param:     fd: the file descriptor of the UART
*               speed: UART speed
*               flow_ctrl: data flow control
*               databits: data bits, 7 or 8
*               stopbits: stop bit, 1 or 2
*               parity: parity mode, N,E,O,S
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
int UART_Set(int fd,int speed,int flow_ctrl,int databits,int stopbits,int parity);

/*******************************************************************
* name:         UART_Init()
* function:     UART initialization
* in param:     fd: the file descriptor of the UART
*               speed: UART speed
*               flow_ctrl: data flow control
*               databits: data bits, 7 or 8
*               stopbits: stop bit, 1 or 2
*               parity: parity mode, N,E,O,S
*                      
* out param:        return 0 if success, or 1 if fail
*******************************************************************/
int UART_Init(int fd, int speed,int flow_ctrl,int databits,int stopbits,int parity);



/*******************************************************************
* name:         UART_Recv
* function:     receive the UART data
* in param:     fd: file descriptor
*               rcv_buf: buffer for received data
*               data_len: length of the data frame
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
int UART_Recv(int fd, char *rcv_buf,int data_len);


/********************************************************************
* name:         UART_Send
* function:     sending data
* in param:     fd: file descriptor    
*               send_buf: buffer storaging the data will be sent
*               data_len: the length of the data frame
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
bool UART_Send(int fd, char *send_buf, int data_len);
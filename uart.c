#include "uart.h"
#include "define.h"
#include "execinfo.h"

FILE *fptr;
int UART0_Open(int fd,char* port)
{
    fd = open( port, O_RDWR|O_NOCTTY|O_NDELAY);
    if(FALSE == fd)
    {
        fptr = fopen("/usr/bin/log.txt","a");
        fprintf(fptr,"BLE Can't Open Serial Port \n");
        fclose(fptr);
        return(FALSE);
    }
    //set the UART into blocked mode
    if(fcntl(fd, F_SETFL, 0) < 0)
    {
        fptr = fopen("/usr/bin/log.txt","a");
        fprintf(fptr,"BLE fcntl failed \n");
        fclose(fptr);
        return(FALSE);
    }
    else
    {
        fptr = fopen("/usr/bin/log.txt","a");
        fprintf(fptr,"BLE [LOG] fcntl=%d\n",fcntl(fd, F_SETFL,0));
        fclose(fptr);
    }
    // check whether this serial port is terminal device or not
    if(0 == isatty(STDIN_FILENO))
    {
        fptr = fopen("/usr/bin/log.txt","a");
        fprintf(fptr,"BLE standard input is not a terminal device\n");
        fclose(fptr);
        return(FALSE);
    }
    else
    {
        perror("[LOG] isatty success!\n");
    }
    return fd;
}
/*******************************************************************
* name:         UART0_Close
* function:     Close the UART
* in param:     fd: file descriptor port:UART(ttyS0,ttyS1,ttyS2)
* out param:    void
*******************************************************************/
void UART0_Close(int fd)
{
    close(fd);
}
/*******************************************************************
* name:         UART0_Set
* function:     set the data bits, stop bit, paraity bit of UART
* in param:     fd: the file descriptor of the UART
*               speed: UART speed
*               flow_ctrl: data flow control
*               databits: data bits, 7 or 8
*               stopbits: stop bit, 1 or 2
*               parity: parity mode, N,E,O,S
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
int UART0_Set(int fd,int speed,int flow_ctrl,int databits,int stopbits,int parity)
{
    int i;
    int status;
    int speed_arr[] = { B115200, B19200, B9600, B4800, B2400, B1200, B300};
    int name_arr[] = {115200,  19200,  9600,  4800,  2400,  1200,  300};
    
    struct termios options;
    
    tcgetattr(fd,&options);
    if(tcgetattr(fd, &options) != 0)
    {
        return(FALSE);
    }
    //set the baudrate for the input and output of the UART
    for(i=0; i < sizeof(speed_arr)/sizeof(int);  i++)
    {
        if(speed == name_arr[i])
        {
            cfmakeraw(&options) ;
            cfsetispeed(&options, speed_arr[i]);
            cfsetospeed(&options, speed_arr[i]);
        }
    }
    
    //modify the control mode of the UART
    options.c_cflag |= CLOCAL;
    options.c_cflag |= CREAD;
  
    // set the data flow control
    switch(flow_ctrl)
    {
        case 0:
            options.c_cflag &= ~CRTSCTS;
            break;   
        case 1:
            options.c_cflag |= CRTSCTS;
            break;
        case 2:
            options.c_cflag |= IXON | IXOFF | IXANY;
            break;
    }
    //set the data bits
    // mask other flags
    options.c_cflag &= ~CSIZE;
    switch (databits)
    {  
       case 5    :
                     options.c_cflag |= CS5;
                     break;
       case 6    :
                     options.c_cflag |= CS6;
                     break;
       case 7    :    
                 options.c_cflag |= CS7;
                 break;
       case 8:    
                 options.c_cflag |= CS8;
                 break;  
       default:   
                 fprintf(stderr,"Unsupported data size\n");
                 return (FALSE); 
    }
    // set parity bits
    switch (parity)
    {  
       case 'n':
       case 'N':
                 options.c_cflag &= ~PARENB; 
                 options.c_iflag &= ~INPCK;    
                 break; 
       case 'o':  
       case 'O':   
                 options.c_cflag |= (PARODD | PARENB); 
                 options.c_iflag |= INPCK;             
                 break; 
       case 'e': 
       case 'E': 
                 options.c_cflag |= PARENB;       
                 options.c_cflag &= ~PARODD;       
                 options.c_iflag |= INPCK;      
                 break;
       case 's':
       case 'S':
                 options.c_cflag &= ~PARENB;
                 options.c_cflag &= ~CSTOPB;
                 break; 
        default:  
                 fprintf(stderr,"Unsupported parity\n");    
                 return (FALSE); 
    } 
    // set stop bit
    switch (stopbits)
    {  
       case 1:
            options.c_cflag &= ~CSTOPB; break; 
       case 2:
            options.c_cflag |= CSTOPB; break;
       default:
            fprintf(stderr,"Unsupported stop bits\n"); 
            return (FALSE);
    }
    
    //modify the output mode, using the RAM mode
    options.c_oflag &= ~OPOST;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 0;

    tcflush(fd,TCIFLUSH);

    // active the configuration
    if(tcsetattr(fd,TCSANOW,&options) != 0)
    {
        return (FALSE);
    }
    usleep (10000);
    return (TRUE); 
}
/*******************************************************************
* name:         UART0_Init()
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
int UART0_Init(int fd, int speed,int flow_ctrl,int databits,int stopbits,int parity)
{
    int err;
    if (UART0_Set(fd,115200,0,8,1,'N') == FALSE)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}
/*******************************************************************
* name:         UART0_Recv
* function:     receive the UART data
* in param:     fd: file descriptor
*               rcv_buf: buffer for received data
*               data_len: length of the data frame
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
int UART0_Recv(int fd, char *rcv_buf,int data_len)
{
    int len,fs_sel;
    fd_set fs_read;
   
    struct timeval time;
   
    FD_ZERO(&fs_read);
    FD_SET(fd,&fs_read);
   
    time.tv_sec = 1;
    time.tv_usec = 0;
  
    // time.tv_sec = 0;
    // time.tv_usec = 10000;

    fs_sel = select(fd+1,&fs_read,NULL,NULL,&time);
    if(fs_sel)
    {
        len = read(fd,rcv_buf,data_len);
        return len;
    }
    else
    {
        return FALSE;
    }     
}
/********************************************************************
* name:         UART0_Send
* function:     sending data
* in param:     fd: file descriptor    
*               send_buf: buffer storaging the data will be sent
*               data_len: the length of the data frame
* out param:    return 0 if success, or 1 if fail
*******************************************************************/
int UART0_Send(int fd, char *send_buf,int data_len)
{
    char tmp[100];
    int len = 0;
   
    len = write(fd, send_buf, data_len);
    if (len == data_len )
    {
        for (int i = 0; i < data_len; i++) {
            sprintf(&tmp[i*3], "%02X ", send_buf[i]);
        }

        myLogInfo("Sent UART frame to device: %s", tmp);
        return len;
    }
    else
    {
        tcflush(fd,TCOFLUSH);
        myLogError("Failed to sent data to UART. Sent length = %d", len);
        return 0;
    }
}

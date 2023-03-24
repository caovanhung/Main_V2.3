#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "udp.h"


struct sockaddr_in UDP_Init(struct sockaddr_in  serverAddress, char *IP,int PORT)
{
	struct sockaddr_in  serverAddress_t = serverAddress;
	char *IP_t = IP;
	int PORT_t =  PORT;
    //memset(serverAddress_t, 0, sizeof(serverAddress_t));
    serverAddress_t.sin_family = AF_INET;
    serverAddress_t.sin_addr.s_addr= inet_addr(IP_t);
    serverAddress_t.sin_port=htons(PORT_t);
    
    return serverAddress_t;
}

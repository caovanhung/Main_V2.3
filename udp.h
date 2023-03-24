#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>


struct sockaddr_in UDP_Init(struct sockaddr_in  serverAddress, char *IP ,int PORT);

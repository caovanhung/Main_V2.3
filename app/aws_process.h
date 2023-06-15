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
#include <sys/un.h>

#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <unistd.h> /* read, write, close */
#include <string.h> /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h> /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <sys/un.h>

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX includes. */
#include <unistd.h>
#include "define.h"
#include "parson.h"
#include "core_process_t.h"
#include "aws_mosquitto.h"
#include "cJSON.h"

#define MQTT_TOPIC_COMMON                  "$aws/things/%s/shadow/name"

typedef enum {
    PAGE_NONE,
    PAGE_ANY,
    PAGE_MAIN,
    PAGE_DEVICE,
    PAGE_SCENE,
    PAGE_GROUP
} AwsPageType;

typedef enum {
    TOPIC_GET_PUB,      //     /get
    TOPIC_GET_SUB,      //     /get/accepted
    TOPIC_UPD_PUB,      //     /update
    TOPIC_UPD_SUB,      //     /update/accepted
    TOPIC_NOTI_PUB,     //     /notify
    TOPIC_NOTI_SUB,     //     /notify
    TOPIC_REJECT        //     /reject
} AwsTopicType;

void getHcInformation();
char* Aws_GetTopic(AwsPageType pageType, int pageIndex, AwsTopicType topicType);
JSON* Aws_GetShadow(const char* thingName, const char* shadowName);
void Aws_SyncDatabase();
//for pre-process
bool AWS_short_message_received(char *value);
JSON* Aws_CreateCloudPacket(JSON* localPacket);

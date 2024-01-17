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


#include <stdio.h> /* printInfo, sprintf */
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

#include <mosquitto.h>
#include <unistd.h>

/* Include Demo Config as the first non-system header. */
#include "config_MQTT.h"

/* MQTT API headers. */
#include "core_mqtt.h"
#include "core_mqtt_state.h"

/* OpenSSL sockets transport implementation. */
#include "openssl_posix.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"

/* Clock for timer. */
#include "clock.h"
#include "queue.h"
#include "parson.h"
#include "aws_process.h"
#include "time_t.h"
#include "define.h"
#include "helper.h"
#include "cJSON.h"
#include "aws_mosquitto.h"
/*-----------------------------------------------------------*/

const char* SERVICE_NAME = SERVICE_AWS;
uint8_t SERVICE_ID = SERVICE_ID_AWS;
bool g_printLog = true;
extern char* g_homeId;

FILE *fptr;
int rc;
struct mosquitto * mosq;
struct Queue *queue_received_aws,*queue_mos_sub;
MQTTFixedBuffer_t networkBuffer;
TransportInterface_t transport;
NetworkContext_t networkContext;
OpensslParams_t opensslParams;
const char g_ipAddress[50];

static bool g_awsIsConnected = false;
static bool g_mosqIsConnected = false;
char aws_buff[MAX_SIZE_ELEMENT_QUEUE] = {'\0'};

typedef struct PublishPackets
{
    uint16_t packetId;
    MQTTPublishInfo_t pubInfo;
}PublishPackets_t;

struct NetworkContext
{
    OpensslParams_t *pParams;
};

MQTTContext_t mqttContext = { 0 };
uint16_t globalSubscribePacketIdentifier = 0U;
uint16_t globalUnsubscribePacketIdentifier = 0U;
PublishPackets_t outgoingPublishPackets[ MAX_OUTGOING_PUBLISHES ] = { 0 };
uint8_t buffer[NETWORK_BUFFER_SIZE];
MQTTSubAckStatus_t globalSubAckStatus = MQTTSubAckFailure;

static MQTTSubscribeInfo_t* g_awsSubscriptionList;
static int g_awsSubscriptionCount = 0;
static JSON* g_mergePayloads;
static int g_wifiServiceWatchdog = 0, g_coreServiceWatchdog = 0, g_bleServiceWatchdog = 0;

void* Thread_ConnectToAws(void* p);
uint32_t generateRandomNumber();
int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext,MQTTContext_t * pMqttContext,bool * pClientSessionPresent,bool * pBrokerSessionPresent );
void Aws_ReceivedHandler( MQTTPublishInfo_t * pPublishInfo,uint16_t packetIdentifier );
void eventCallback( MQTTContext_t * pMqttContext,MQTTPacketInfo_t * pPacketInfo,MQTTDeserializedInfo_t * pDeserializedInfo );
int initializeMqtt( MQTTContext_t * pMqttContext,NetworkContext_t * pNetworkContext );
int establishMqttSession( MQTTContext_t * pMqttContext,bool createCleanSession,bool * pSessionPresent );
int disconnectMqttSession( MQTTContext_t * pMqttContext );
int subscribeToTopic( MQTTContext_t * pMqttContext );
int getNextFreeIndexForOutgoingPublishes( uint8_t * pIndex );
void cleanupOutgoingPublishAt( uint8_t index );
void cleanupOutgoingPublishes( void );
void cleanupOutgoingPublishWithPacketID( uint16_t packetId );
int handlePublishResend( MQTTContext_t * pMqttContext );
void updateSubAckStatus( MQTTPacketInfo_t * pPacketInfo );
int handleResubscribe( MQTTContext_t * pMqttContext );
int publishToTopicAndProcessIncomingMessage( MQTTContext_t * pMqttContext,const char * pTopic,uint16_t topicLength,const char * pMessage );



uint32_t generateRandomNumber()
{
    return(rand());
}

#define mqttCloudPublish(topic, message)  {publishToTopicAndProcessIncomingMessage(&mqttContext, topic, 0, message); logInfo("Published to cloud topic: %s, payload: %s", topic, message);}

void sendPacketToCloud(const char* topic, JSON* packet) {
    char* message = cJSON_PrintUnformatted(packet);
    mqttCloudPublish(topic, message);
    mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
    free(message);
}

/*-----------------------------------------------------------*/
int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext,MQTTContext_t * pMqttContext,bool * pClientSessionPresent,bool * pBrokerSessionPresent )
{
    int returnStatus = EXIT_FAILURE;
    BackoffAlgorithmStatus_t backoffAlgStatus = BackoffAlgorithmSuccess;
    OpensslStatus_t opensslStatus = OPENSSL_SUCCESS;
    BackoffAlgorithmContext_t reconnectParams;
    ServerInfo_t serverInfo;
    OpensslCredentials_t opensslCredentials;
    uint16_t nextRetryBackOff;
    bool createCleanSession;

    /* Initialize information to connect to the MQTT broker. */
    serverInfo.pHostName = AWS_IOT_ENDPOINT;
    serverInfo.hostNameLength = AWS_IOT_ENDPOINT_LENGTH;
    serverInfo.port = AWS_MQTT_PORT;

    /* Initialize credentials for establishing TLS session. */
    memset( &opensslCredentials, 0, sizeof( OpensslCredentials_t ) );
    opensslCredentials.pRootCaPath = ROOT_CA_CERT_PATH;

    /* If #CLIENT_USERNAME is defined, username/password is used for authenticating
     * the client. */
    #ifndef CLIENT_USERNAME
        opensslCredentials.pClientCertPath = CLIENT_CERT_PATH;
        opensslCredentials.pPrivateKeyPath = CLIENT_PRIVATE_KEY_PATH;
    #endif

    opensslCredentials.sniHostName = AWS_IOT_ENDPOINT;

    if( AWS_MQTT_PORT == 443 )
    {
        #ifdef CLIENT_USERNAME
            opensslCredentials.pAlpnProtos = AWS_IOT_PASSWORD_ALPN;
            opensslCredentials.alpnProtosLen = AWS_IOT_PASSWORD_ALPN_LENGTH;
        #else
            opensslCredentials.pAlpnProtos = AWS_IOT_MQTT_ALPN;
            opensslCredentials.alpnProtosLen = AWS_IOT_MQTT_ALPN_LENGTH;
        #endif
    }

    /* Initialize reconnect attempts and interval */
    BackoffAlgorithm_InitializeParams( &reconnectParams,
                                       CONNECTION_RETRY_BACKOFF_BASE_MS,
                                       CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                       CONNECTION_RETRY_MAX_ATTEMPTS );

    /* Attempt to connect to MQTT broker. If connection fails, retry after
     * a timeout. Timeout value will exponentially increase until maximum
     * attempts are reached.
     */
    do
    {
        /* Establish a TLS session with the MQTT broker. This example connects
         * to the MQTT broker as specified in AWS_IOT_ENDPOINT and AWS_MQTT_PORT
         * at the demo config header. */
        LogInfo((get_localtime_now()), ( "Establishing a TLS session to %.*s:%d.",
                   AWS_IOT_ENDPOINT_LENGTH,
                   AWS_IOT_ENDPOINT,
                   AWS_MQTT_PORT ) );
        opensslStatus = Openssl_Connect( pNetworkContext,
                                         &serverInfo,
                                         &opensslCredentials,
                                         TRANSPORT_SEND_RECV_TIMEOUT_MS,
                                         TRANSPORT_SEND_RECV_TIMEOUT_MS );

        if( opensslStatus == OPENSSL_SUCCESS )
        {
            LogInfo( (get_localtime_now()),( "OPENSSL_SUCCESS." ) );
            /* A clean MQTT session needs to be created, if there is no session saved
             * in this MQTT client. */
            createCleanSession = ( *pClientSessionPresent == true ) ? false : true;

            /* Sends an MQTT Connect packet using the established TLS session,
             * then waits for connection acknowledgment (CONNACK) packet. */
            returnStatus = establishMqttSession( pMqttContext, createCleanSession, pBrokerSessionPresent );

            if( returnStatus == EXIT_FAILURE )
            {
                ( void ) Openssl_Disconnect( pNetworkContext );
                LogError( (get_localtime_now()),( "establishMqttSession to failed." ) );

            }
        }

        if( returnStatus == EXIT_FAILURE )
        {
            backoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &reconnectParams, generateRandomNumber(), &nextRetryBackOff );

            if( backoffAlgStatus == BackoffAlgorithmRetriesExhausted )
            {
                LogError( (get_localtime_now()),( "Connection to the broker failed, all attempts exhausted." ) );
                returnStatus = EXIT_FAILURE;
            }
            else if( backoffAlgStatus == BackoffAlgorithmSuccess )
            {
                fptr = fopen("/usr/bin/log.txt","a");
                fprintf(fptr,"[%s]AWS Connection to the broker failed\n",get_localtime_now());
                fclose(fptr);

                LogWarn((get_localtime_now()), ( "Connection to the broker failed. Retrying connection "
                           "after %hu ms backoff.",
                           ( unsigned short ) nextRetryBackOff ) );
                Clock_SleepMs( nextRetryBackOff );
            }
        }
        usleep(100);
    } while( ( returnStatus == EXIT_FAILURE ) && ( backoffAlgStatus == BackoffAlgorithmSuccess ) );

    return returnStatus;
}

/*-----------------------------------------------------------*/
int getNextFreeIndexForOutgoingPublishes( uint8_t * pIndex )
{
    int returnStatus = EXIT_FAILURE;
    uint8_t index = 0;

    assert( outgoingPublishPackets != NULL );
    assert( pIndex != NULL );

    for( index = 0; index < MAX_OUTGOING_PUBLISHES; index++ )
    {
        /* A free index is marked by invalid packet id.
         * Check if the the index has a free slot. */
        if( outgoingPublishPackets[ index ].packetId == MQTT_PACKET_ID_INVALID )
        {
            returnStatus = EXIT_SUCCESS;
            break;
        }
        usleep(100);
    }
    *pIndex = index;
    return returnStatus;
}

/*-----------------------------------------------------------*/
void cleanupOutgoingPublishAt( uint8_t index )
{
    assert( outgoingPublishPackets != NULL );
    assert( index < MAX_OUTGOING_PUBLISHES );

    /* Clear the outgoing publish packet. */
    ( void ) memset( &( outgoingPublishPackets[ index ] ),
                     0x00,
                     sizeof( outgoingPublishPackets[ index ] ) );
}

/*-----------------------------------------------------------*/
void cleanupOutgoingPublishes( void )
{
    assert( outgoingPublishPackets != NULL );

    /* Clean up all the outgoing publish packets. */
    ( void ) memset( outgoingPublishPackets, 0x00, sizeof( outgoingPublishPackets ) );
}

/*-----------------------------------------------------------*/
void cleanupOutgoingPublishWithPacketID( uint16_t packetId )
{
    uint8_t index = 0;

    assert( outgoingPublishPackets != NULL );
    assert( packetId != MQTT_PACKET_ID_INVALID );

    /* Clean up all the saved outgoing publishes. */
    for( ; index < MAX_OUTGOING_PUBLISHES; index++ )
    {
        if( outgoingPublishPackets[ index ].packetId == packetId )
        {
            cleanupOutgoingPublishAt( index );
            LogInfo( (get_localtime_now()),( "Cleaned up outgoing publish packet with packet id %u.\n\n",
                    packetId ) );
            break;
        }
        usleep(100);
    }
}

/*-----------------------------------------------------------*/
int handlePublishResend( MQTTContext_t * pMqttContext )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus = MQTTSuccess;
    uint8_t index = 0U;
    MQTTStateCursor_t cursor = MQTT_STATE_CURSOR_INITIALIZER;
    uint16_t packetIdToResend = MQTT_PACKET_ID_INVALID;
    bool foundPacketId = false;

    assert( pMqttContext != NULL );
    assert( outgoingPublishPackets != NULL );

    packetIdToResend = MQTT_PublishToResend( pMqttContext, &cursor );

    while( packetIdToResend != MQTT_PACKET_ID_INVALID )
    {
        usleep(100);
        foundPacketId = false;

        for( index = 0U; index < MAX_OUTGOING_PUBLISHES; index++ )
        {
            if( outgoingPublishPackets[ index ].packetId == packetIdToResend )
            {
                foundPacketId = true;
                outgoingPublishPackets[ index ].pubInfo.dup = true;

                LogInfo( (get_localtime_now()), ( "Sending duplicate PUBLISH with packet id %u.",
                           outgoingPublishPackets[ index ].packetId ) );
                mqttStatus = MQTT_Publish( pMqttContext,
                                           &outgoingPublishPackets[ index ].pubInfo,
                                           outgoingPublishPackets[ index ].packetId );

                if( mqttStatus != MQTTSuccess )
                {
                    LogError((get_localtime_now()), ( "Sending duplicate PUBLISH for packet id %u "
                                " failed with status %s.",
                                outgoingPublishPackets[ index ].packetId,
                                MQTT_Status_strerror( mqttStatus ) ) );
                    returnStatus = EXIT_FAILURE;
                    break;
                }
                else
                {
                    LogInfo( (get_localtime_now()),( "Sent duplicate PUBLISH successfully for packet id %u.\n\n",
                               outgoingPublishPackets[ index ].packetId ) );
                }
            }
        }

        if( foundPacketId == false )
        {
            LogError( (get_localtime_now()),( "Packet id %u requires resend, but was not found in "
                        "outgoingPublishPackets.",
                        packetIdToResend ) );
            returnStatus = EXIT_FAILURE;
            break;
        }
        else
        {
            /* Get the next packetID to be resent. */
            packetIdToResend = MQTT_PublishToResend( pMqttContext, &cursor );
        }

    }

    return returnStatus;
}

/*-----------------------------------------------------------*/
void Aws_ReceivedHandler( MQTTPublishInfo_t * pPublishInfo,uint16_t packetIdentifier )
{
    assert( pPublishInfo != NULL );
    memset(aws_buff,'\0',MAX_SIZE_ELEMENT_QUEUE);
    bool flag = false;
    if (pPublishInfo->payloadLength == 0) {
        return;
    }
    strncpy(aws_buff, pPublishInfo->pPayload, pPublishInfo->payloadLength);
    aws_buff[(int)pPublishInfo->payloadLength] = '\0';
    AWS_short_message_received(aws_buff);
    char* topic = malloc(pPublishInfo->topicNameLength + 1);
    memcpy(topic, pPublishInfo->pTopicName, pPublishInfo->topicNameLength);
    topic[pPublishInfo->topicNameLength] = 0;
    // if (StringContains(topic, "accountInfo")) {
    //     free(topic);
    //     return;
    // }

    JSON* recvPacket = JSON_Parse(aws_buff);
    if (recvPacket) {
        JSON* state = JSON_GetObject(recvPacket, "state");
        if (state) {
            JSON* reported = JSON_GetObject(state, "reported");
            if (reported) {
                int sender = JSON_GetNumber(reported, "sender");
                if (JSON_HasKey(reported, "type") && (sender == SENDER_APP_VIA_LOCAL || sender == SENDER_APP_TO_CLOUD)) {
                    printInfo("\n");
                    logInfo("Received msg from cloud. topic: %s, payload: %s", topic, aws_buff);
                    int size_queue = get_sizeQueue(queue_received_aws);
                    if (size_queue < QUEUE_SIZE) {
                        enqueue(queue_received_aws, aws_buff);
                    }
                }
            }
        } else if (JSON_HasKey(recvPacket, "type")) {
            int sender = JSON_GetNumber(recvPacket, "sender");
            int type = JSON_GetNumber(recvPacket, "type");
            if (sender == SENDER_APP_VIA_LOCAL || sender == SENDER_APP_TO_CLOUD) {
                if (type != 24) {
                    printInfo("\n");
                    logInfo("Received msg from cloud. topic: %s, payload: %s", topic, aws_buff);

                    int size_queue = get_sizeQueue(queue_received_aws);
                    if (size_queue < QUEUE_SIZE) {
                        enqueue(queue_received_aws, aws_buff);
                    }
                } else {
                    char* p = "{\"type\": 24,\"sender\":11,\"msg\": \"PONG\"}";
                    publishToTopicAndProcessIncomingMessage(&mqttContext, topic, 0, p);
                }
            }
        }
    }
    JSON_Delete(recvPacket);
    free(topic);
}

void Mosq_ReceivedHandler(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    if (StringCompare((char*)msg->payload, "WIFI_PONG")) {
        g_wifiServiceWatchdog = 0;
        return;
    }
    if (StringCompare((char*)msg->payload, "CORE_PONG")) {
        g_coreServiceWatchdog = 0;
        return;
    }
    if (StringCompare((char*)msg->payload, "BLE_PONG")) {
        g_bleServiceWatchdog = 0;
        return;
    }

    if (StringCompare(msg->topic, MOSQ_TOPIC_CONTROL_LOCAL)) {
        char* payload = calloc((int)msg->payloadlen + 1, sizeof(char));
        strncpy(payload, msg->payload, msg->payloadlen);
        payload[(int)msg->payloadlen] = '\0';
        printInfo("\n");
        logInfo("Received msg from local control. topic: %s, payload: %s", msg->topic, payload);
        int size_queue = get_sizeQueue(queue_received_aws);
        if(size_queue < QUEUE_SIZE) {
            enqueue(queue_received_aws, payload);
        }
        free(payload);
    } else {
        int size_queue = get_sizeQueue(queue_mos_sub);
        if (size_queue < QUEUE_SIZE) {
            enqueue(queue_mos_sub, (char *) msg->payload);
        }
    }
}

/*-----------------------------------------------------------*/
void updateSubAckStatus( MQTTPacketInfo_t * pPacketInfo )
{
    uint8_t * pPayload = NULL;
    size_t pSize = 0;

    MQTTStatus_t mqttStatus = MQTT_GetSubAckStatusCodes( pPacketInfo, &pPayload, &pSize );

    /* MQTT_GetSubAckStatusCodes always returns success if called with packet info
     * from the event callback and non-NULL parameters. */
    assert( mqttStatus == MQTTSuccess );

    /* Suppress unused variable warning when asserts are disabled in build. */
    ( void ) mqttStatus;

    /* Demo only subscribes to one topic, so only one status code is returned. */
    globalSubAckStatus = ( MQTTSubAckStatus_t ) pPayload[ 0 ];
}

/*-----------------------------------------------------------*/
int handleResubscribe( MQTTContext_t * pMqttContext )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus = MQTTSuccess;
    BackoffAlgorithmStatus_t backoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t retryParams;
    uint16_t nextRetryBackOff = 0U;

    assert( pMqttContext != NULL );

    /* Initialize retry attempts and interval. */
    BackoffAlgorithm_InitializeParams( &retryParams,
                                       CONNECTION_RETRY_BACKOFF_BASE_MS,
                                       CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                       CONNECTION_RETRY_MAX_ATTEMPTS );

    do
    {
        mqttStatus = MQTT_Subscribe( pMqttContext,
                                     g_awsSubscriptionList,
                                     g_awsSubscriptionCount,
                                     globalSubscribePacketIdentifier );

        if( mqttStatus != MQTTSuccess )
        {
            LogError((get_localtime_now()), ( "Failed to send SUBSCRIBE packet to broker with error = %s.",
                        MQTT_Status_strerror( mqttStatus ) ) );
            returnStatus = EXIT_FAILURE;
            break;
        }


        /* Process incoming packet. */
        mqttStatus = MQTT_ProcessLoop( pMqttContext, MQTT_PROCESS_LOOP_TIMEOUT_MS );

        if( mqttStatus != MQTTSuccess )
        {
            LogError( (get_localtime_now()),( "MQTT_ProcessLoop returned with status = %s.",
                        MQTT_Status_strerror( mqttStatus ) ) );
            returnStatus = EXIT_FAILURE;
            break;
        }

        if( globalSubAckStatus == MQTTSubAckFailure )
        {
            /* Generate a random number and get back-off value (in milliseconds) for the next re-subscribe attempt. */
            backoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &retryParams, generateRandomNumber(), &nextRetryBackOff );

            if( backoffAlgStatus == BackoffAlgorithmRetriesExhausted )
            {
                LogError( (get_localtime_now()),( "Subscription to topic failed, all attempts exhausted." ) );
                returnStatus = EXIT_FAILURE;
            }
            else if( backoffAlgStatus == BackoffAlgorithmSuccess )
            {
                LogWarn( (get_localtime_now()),( "Server rejected subscription request. Retrying "
                           "connection after %hu ms backoff.",
                           ( unsigned short ) nextRetryBackOff ) );
                Clock_SleepMs( nextRetryBackOff );
            }
        }

        usleep(5000);
    } while( ( globalSubAckStatus == MQTTSubAckFailure ) && ( backoffAlgStatus == BackoffAlgorithmSuccess ) );

    return returnStatus;
}

/*-----------------------------------------------------------*/
void eventCallback( MQTTContext_t * pMqttContext,MQTTPacketInfo_t * pPacketInfo,MQTTDeserializedInfo_t * pDeserializedInfo )
{
    uint16_t packetIdentifier;

    assert( pMqttContext != NULL );
    assert( pPacketInfo != NULL );
    assert( pDeserializedInfo != NULL );

    /* Suppress unused parameter warning when asserts are disabled in build. */
    ( void ) pMqttContext;

    packetIdentifier = pDeserializedInfo->packetIdentifier;
    if( ( pPacketInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        assert( pDeserializedInfo->pPublishInfo != NULL );
        Aws_ReceivedHandler( pDeserializedInfo->pPublishInfo, packetIdentifier );
    }
    else
    {
        /* Handle other packets. */
        switch( pPacketInfo->type )
        {
            case MQTT_PACKET_TYPE_SUBACK:
                updateSubAckStatus( pPacketInfo );
                if( globalSubAckStatus != MQTTSubAckFailure )
                {
                    // LogInfo( (get_localtime_now()),( "Subscribed to the topic %.*s. with maximum QoS %u.\n\n",
                    //         MQTT_EXAMPLE_TOPIC_LENGTH,
                    //         MQTT_EXAMPLE_TOPIC,
                    //         globalSubAckStatus ) );
                }
                break;

            case MQTT_PACKET_TYPE_UNSUBACK:
                // LogInfo((get_localtime_now()), ( "Unsubscribed from the topic %.*s.\n\n",
                //         MQTT_EXAMPLE_TOPIC_LENGTH,
                //         MQTT_EXAMPLE_TOPIC ) );
                /* Make sure ACK packet identifier matches with Request packet identifier. */
                assert( globalUnsubscribePacketIdentifier == packetIdentifier );
                break;

            case MQTT_PACKET_TYPE_PINGRESP:

                /* Nothing to be done from application as library handles
                 * PINGRESP. */
                LogWarn( (get_localtime_now()),( "PINGRESP should not be handled by the application "
                           "callback when using MQTT_ProcessLoop.\n\n" ) );
                break;

            case MQTT_PACKET_TYPE_PUBACK:
                /* Cleanup publish packet when a PUBACK is received. */
                cleanupOutgoingPublishWithPacketID( packetIdentifier );
                break;

            /* Any other packet type is invalid. */
            default:
                printInfo("Unknown packet type received:(%02x).\n\n",pPacketInfo->type);
                LogError((get_localtime_now()), ( "Unknown packet type received:(%02x).\n\n",
                            pPacketInfo->type ) );
        }
    }
}

/*-----------------------------------------------------------*/
int establishMqttSession( MQTTContext_t * pMqttContext,bool createCleanSession,bool * pSessionPresent )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus;
    MQTTConnectInfo_t connectInfo = { 0 };

    assert( pMqttContext != NULL );
    assert( pSessionPresent != NULL );
    char *temp = get_localtime_now();
    connectInfo.cleanSession = createCleanSession;
    connectInfo.pClientIdentifier = temp;
    connectInfo.clientIdentifierLength = ( ( uint16_t ) ( sizeof( temp ) - 1 ) );
    // connectInfo.pClientIdentifier = CLIENT_IDENTIFIER;
    // connectInfo.clientIdentifierLength = CLIENT_IDENTIFIER_LENGTH;
    connectInfo.keepAliveSeconds = MQTT_KEEP_ALIVE_INTERVAL_SECONDS;

    #ifdef CLIENT_USERNAME
        connectInfo.pUserName = CLIENT_USERNAME_WITH_METRICS;
        connectInfo.userNameLength = strlen( CLIENT_USERNAME_WITH_METRICS );
        connectInfo.pPassword = CLIENT_PASSWORD;
        connectInfo.passwordLength = strlen( CLIENT_PASSWORD );
    #else
        connectInfo.pUserName = METRICS_STRING;
        connectInfo.userNameLength = METRICS_STRING_LENGTH;
        /* Password for authentication is not used. */
        connectInfo.pPassword = NULL;
        connectInfo.passwordLength = 0U;
    #endif /* ifdef CLIENT_USERNAME */

    /* Send MQTT CONNECT packet to broker. */
    mqttStatus = MQTT_Connect( pMqttContext, &connectInfo, NULL, CONNACK_RECV_TIMEOUT_MS, pSessionPresent );

    if( mqttStatus != MQTTSuccess )
    {
        returnStatus = EXIT_FAILURE;
        LogError( (get_localtime_now()),( "Connection with MQTT broker failed with status %s.",
                    MQTT_Status_strerror( mqttStatus ) ) );
    }
    else
    {
        LogInfo( (get_localtime_now()),( "MQTT connection successfully established with broker.\n\n" ) );
    }
    return returnStatus;
}

/*-----------------------------------------------------------*/
int disconnectMqttSession( MQTTContext_t * pMqttContext )
{
    MQTTStatus_t mqttStatus = MQTTSuccess;
    int returnStatus = EXIT_SUCCESS;

    assert( pMqttContext != NULL );

    /* Send DISCONNECT. */
    mqttStatus = MQTT_Disconnect( pMqttContext );

    if( mqttStatus != MQTTSuccess )
    {
        LogError((get_localtime_now()), ( "Sending MQTT DISCONNECT failed with status=%s.",
                    MQTT_Status_strerror( mqttStatus ) ) );
        returnStatus = EXIT_FAILURE;
    }

    return returnStatus;
}



/*-----------------------------------------------------------*/
int subscribeToTopic( MQTTContext_t * pMqttContext ) {
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus;

    assert( pMqttContext != NULL );
    /* Send SUBSCRIBE packet. */
    for (int i = 0; i < g_awsSubscriptionCount; i++) {
        globalSubscribePacketIdentifier = MQTT_GetPacketId( pMqttContext );
        mqttStatus = MQTT_Subscribe(pMqttContext,
                                    &g_awsSubscriptionList[i],
                                    1,
                                    globalSubscribePacketIdentifier );

        if (mqttStatus != MQTTSuccess ) {
            logError("Failed to send SUBSCRIBE packet to broker with error = %s", MQTT_Status_strerror(mqttStatus));
            returnStatus = EXIT_FAILURE;
        }
    }

    return returnStatus;
}


/*-----------------------------------------------------------*/
int initializeMqtt( MQTTContext_t * pMqttContext,NetworkContext_t * pNetworkContext )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus;

    assert( pMqttContext != NULL );
    assert( pNetworkContext != NULL );

    /* Fill in TransportInterface send and receive function pointers.
     * For this demo, TCP sockets are used to send and receive data
     * from network. Network context is SSL context for OpenSSL.*/
    transport.pNetworkContext = pNetworkContext;
    transport.send = Openssl_Send;
    transport.recv = Openssl_Recv;

    /* Fill the values for network buffer. */
    networkBuffer.pBuffer = buffer;
    networkBuffer.size = NETWORK_BUFFER_SIZE;

    /* Initialize MQTT library. */
    mqttStatus = MQTT_Init( pMqttContext,
                            &transport,
                            Clock_GetTimeMs,
                            eventCallback,
                            &networkBuffer );

    if( mqttStatus != MQTTSuccess )
    {
        returnStatus = EXIT_FAILURE;
        LogError( (get_localtime_now()),( "MQTT init failed: Status = %s.", MQTT_Status_strerror( mqttStatus ) ) );
    }
    return returnStatus;
}

/*-----------------------------------------------------------*/
 int publishToTopicAndProcessIncomingMessage( MQTTContext_t * pMqttContext,const char * pTopic,uint16_t topicLength,const char * pMessage )
{
    int returnStatus = EXIT_SUCCESS;

    MQTTStatus_t mqttStatus = MQTTSuccess;
    MQTTPublishInfo_t pubInfo;
    uint32_t publishCount = 0;
    const uint32_t maxPublishCount = MQTT_PUBLISH_COUNT_PER_LOOP;
    uint8_t publishIndex = MAX_OUTGOING_PUBLISHES;

    assert( pMqttContext != NULL );

    ( void ) memset( &pubInfo, 0x00, sizeof( MQTTPublishInfo_t ) );

     /* Get the next free index for the outgoing publish. All QoS1 outgoing
         * publishes are stored until a PUBACK is received. These messages are
         * stored for supporting a resend if a network connection is broken before
         * receiving a PUBACK. */
    returnStatus = getNextFreeIndexForOutgoingPublishes( &publishIndex );
    if( returnStatus == EXIT_FAILURE )
    {
        LogError( (get_localtime_now()),( "Unable to find a free spot for outgoing PUBLISH message.\n\n" ) );
    }
    else
    {
        outgoingPublishPackets[ publishIndex ].pubInfo.qos = MQTTQoS0;
        outgoingPublishPackets[ publishIndex ].pubInfo.pTopicName = pTopic;
        outgoingPublishPackets[ publishIndex ].pubInfo.topicNameLength = strlen(pTopic);
        outgoingPublishPackets[ publishIndex ].pubInfo.pPayload = pMessage;
        outgoingPublishPackets[ publishIndex ].pubInfo.payloadLength = strlen( pMessage );
        outgoingPublishPackets[ publishIndex ].packetId = MQTT_GetPacketId( pMqttContext );


        for( publishCount = 0; publishCount < maxPublishCount; publishCount++ )
        {
            // returnStatus = publishToTopic( pMqttContext );
            mqttStatus = MQTT_Publish(pMqttContext,&outgoingPublishPackets[ publishIndex ].pubInfo,outgoingPublishPackets[ publishIndex ].packetId);

            if( mqttStatus != MQTTSuccess )
            {
                LogError( (get_localtime_now()),( "Failed to send PUBLISH packet to broker with error = %s.",MQTT_Status_strerror( mqttStatus ) ) );
                cleanupOutgoingPublishAt( publishIndex );
                returnStatus = EXIT_FAILURE;
            }
            else
            {
                // cleanupOutgoingPublishWithPacketID(outgoingPublishPackets[ publishIndex ].packetId);
                cleanupOutgoingPublishes();

            }
            // usleep(DELAY_BETWEEN_PUBLISHES_MSECONDS);
        }
    }
    return returnStatus;
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if(rc) {
        printInfo("Error with result code: %d\n", rc);
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_AWS, 0);
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_CONTROL_LOCAL, 0);
}


void Aws_Init() {
    int returnStatus = EXIT_SUCCESS;
    networkContext.pParams = &opensslParams;
    returnStatus = initializeMqtt( &mqttContext, &networkContext );

    // Initialize the subscribe list
    g_awsSubscriptionCount = 3;
    g_awsSubscriptionList = malloc(sizeof(MQTTSubscribeInfo_t) * g_awsSubscriptionCount);

    g_awsSubscriptionList[0].qos = MQTTQoS0;
    g_awsSubscriptionList[0].pTopicFilter = Aws_GetTopic(PAGE_ANY, 0, TOPIC_UPD_SUB);
    g_awsSubscriptionList[0].topicFilterLength = strlen(g_awsSubscriptionList[0].pTopicFilter);

    g_awsSubscriptionList[1].qos = MQTTQoS0;
    g_awsSubscriptionList[1].pTopicFilter = Aws_GetTopic(PAGE_NONE, 0, TOPIC_NOTI_SUB);
    g_awsSubscriptionList[1].topicFilterLength = strlen(g_awsSubscriptionList[1].pTopicFilter);

    g_awsSubscriptionList[2].qos = MQTTQoS0;
    g_awsSubscriptionList[2].pTopicFilter = Aws_GetTopic(PAGE_MAIN, 0, TOPIC_NOTI_SUB);
    g_awsSubscriptionList[2].topicFilterLength = strlen(g_awsSubscriptionList[2].pTopicFilter);

    logInfo("Aws Topics to subscribe: %s, %s, %s", g_awsSubscriptionList[0].pTopicFilter, g_awsSubscriptionList[1].pTopicFilter, g_awsSubscriptionList[2].pTopicFilter);
}

void Aws_ProcessLoop() {
    MQTTStatus_t mqttStatus;
    if (g_awsIsConnected) {
        mqttStatus = MQTT_ProcessLoop( &mqttContext, 5 );
        if (mqttStatus != MQTTSuccess ) {
            g_awsIsConnected = false;
            sendToService(SERVICE_CFG, 0, "AWS_DISCONNECTED");
            (void) Openssl_Disconnect( &networkContext );
            logError("MQTT_ProcessLoop returned with status = %s.", MQTT_Status_strerror( mqttStatus));
        }
    }
}

void Mosq_Init() {
    mosquitto_lib_init();
    mosq = mosquitto_new("HG_AWS", true, NULL);
    rc = mosquitto_username_pw_set(mosq, "homegyinternal", "sgSk@ui41DA09#Lab%1");
    if(rc != 0)
    {
        LogInfo((get_localtime_now()),("mosquitto_username_pw_set! Error Code: %d\n", rc));
        return;
    }
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, Mosq_ReceivedHandler);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if(rc != 0)
    {
        LogInfo((get_localtime_now()),("Client could not connect to broker! Error Code: %d\n", rc));
        mosquitto_destroy(mosq);
        return;
    }
    logInfo("Mosq_Init done");
    g_mosqIsConnected = true;
}

void Mosq_ProcessLoop() {
    if (g_mosqIsConnected) {
        int rc = mosquitto_loop(mosq, 5, 1);
        if (rc != 0) {
            logError("mosquitto_loop error: %d.", rc);
            g_mosqIsConnected = false;
        }
    } else {
        mosquitto_destroy(mosq);
        Mosq_Init();
    }
}


void Mosq_ProcessMessage() {
    int size_queue = get_sizeQueue(queue_mos_sub);
    if (size_queue > 0) {
        char* val_input = (char*)dequeue(queue_mos_sub);
        logInfo("Received msg from MQTT local: %s", val_input);

        JSON* recvPacket = JSON_Parse(val_input);
        int pageIndex = -1;
        if (JSON_HasKey(recvPacket, "pageIndex")) {
            pageIndex = JSON_GetNumber(recvPacket, "pageIndex");
        }
        int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
        char* payloadString = JSON_GetText(recvPacket, MOSQ_Payload);
        JSON* payload = JSON_Parse(payloadString);
        switch (reqType) {
            case GW_RESP_ONOFF_STATE:
            case GW_RESP_ONLINE_STATE: {
                if (pageIndex >= 0) {
                    char* deviceId = JSON_GetText(payload, "deviceId");
                    char str[50];
                    sprintf(str, "%d", pageIndex);
                    JSON* page = JSON_GetObject(g_mergePayloads, str);
                    if (page == NULL) {
                        page = JSON_CreateObject();
                        JSON_SetObject(g_mergePayloads, str, page);
                    }
                    JSON* deviceInfo = JSON_GetObject(page, deviceId);
                    if (deviceInfo == NULL) {
                        deviceInfo = JSON_CreateObject();
                        JSON_SetObject(page, deviceId, deviceInfo);
                    }
                    // printf("deviceInfo: %s\n", cJSON_PrintUnformatted(deviceInfo));
                    if (JSON_HasKey(payload, "state")) {
                        JSON_SetNumber(deviceInfo, "state", JSON_GetNumber(payload, "state"));
                    }
                    if (JSON_HasKey(payload, "dpId")) {
                        JSON* dictDPs = JSON_GetObject(deviceInfo, "dictDPs");
                        if (dictDPs == NULL) {
                            dictDPs = JSON_CreateObject();
                            JSON_SetObject(deviceInfo, "dictDPs", dictDPs);
                        }
                        int dpId = JSON_GetNumber(payload, "dpId");
                        sprintf(str, "%d", dpId);
                        if (dpId == 24 || dpId == 21 || dpId == 106) {
                            JSON_SetText(dictDPs, str, JSON_GetText(payload, "dpValue"));
                        } else {
                            JSON_SetNumber(dictDPs, str, JSON_GetNumber(payload, "dpValue"));
                        }
                    }
                }
                break;
            }
            case GW_RESPONSE_DEVICE_KICKOUT: {
                if (pageIndex >= 0) {
                    char* topic = Aws_GetTopic(PAGE_DEVICE, pageIndex, TOPIC_UPD_PUB);
                    sendPacketToCloud(topic, payload);
                    free(topic);
                }
                break;
            }
            case GW_RESPONSE_UPDATE_GROUP:
            case GW_RESPONSE_ADD_GROUP_LINK:
            case GW_RESPONSE_DEL_GROUP_LINK:
            case GW_RESPONSE_DEL_GROUP_NORMAL:
            case GW_RESPONSE_ADD_GROUP_NORMAL: {
                if (pageIndex >= 0) {
                    char* topic = Aws_GetTopic(PAGE_GROUP, pageIndex, TOPIC_UPD_PUB);
                    sendPacketToCloud(topic, payload);
                    free(topic);
                }
                break;
            }
            case GW_RESPONSE_SENSOR_BATTERY:
            case GW_RESPONSE_SMOKE_SENSOR:
            case GW_RESPONSE_SENSOR_PIR_DETECT:
            case GW_RESPONSE_SENSOR_PIR_LIGHT:
            case GW_RESPONSE_SENSOR_ENVIRONMENT:
            case GW_RESPONSE_SENSOR_DOOR_DETECT:
            case GW_RESPONSE_SENSOR_DOOR_ALARM: {
                if (pageIndex >= 0) {
                    JSON* cloudPacket = Aws_CreateCloudPacket(payload);
                    char* topic = Aws_GetTopic(PAGE_DEVICE, pageIndex, TOPIC_UPD_PUB);
                    sendPacketToCloud(topic, cloudPacket);
                    free(topic);
                    JSON_Delete(cloudPacket);
                }
                break;
            }
            case GW_RESPONSE_ADD_SCENE_HC:
            case GW_RESPONSE_UPDATE_SCENE:
            case GW_RESPONSE_ADD_SCENE_LC:
            case GW_RESPONSE_DEL_SCENE_HC: {
                if (pageIndex >= 0) {
                    char* topic = Aws_GetTopic(PAGE_SCENE, pageIndex, TOPIC_UPD_PUB);
                    sendPacketToCloud(topic, payload);
                    free(topic);
                }
                break;
            }
            case TYPE_NOTIFI_REPONSE: {
                char* topic = Aws_GetTopic(PAGE_NONE, pageIndex, TOPIC_NOTI_PUB);
                sendPacketToCloud(topic, payload);
                free(topic);
                break;
            }
            case 255: {
                char* topic = Aws_GetTopic(PAGE_MAIN, 1, TOPIC_UPD_PUB);
                sendPacketToCloud(topic, payload);
                free(topic);
                break;
            }
        }
        JSON_Delete(recvPacket);
        JSON_Delete(payload);
        free(val_input);
    }
}


void Aws_SendMergePayload() {
    static int pingServiceCount = 0;
    static long long time = 0;
    if (timeInMilliseconds() - time > 500) {
        time = timeInMilliseconds();

        JSON_ForEach(page, g_mergePayloads) {
            int pageIndex = atoi(page->string);
            JSON* p = JSON_CreateObject();
            JSON* state = JSON_CreateObject();
            JSON* reported = JSON_CreateObject();
            JSON_SetObject(state, "reported", reported);
            JSON_SetObject(p, "state", state);
            JSON_SetNumber(reported, "type", TYPE_UPDATE_DEVICE);
            JSON_SetNumber(reported, "sender", SENDER_HC_TO_CLOUD);
            // printf("g_mergePayloads: %s\n", cJSON_PrintUnformatted(page));
            JSON_ForEach(d, page) {
                JSON_SetObject(reported, d->string, JSON_Clone(d));
            }
            char* topic = Aws_GetTopic(PAGE_DEVICE, pageIndex, TOPIC_UPD_PUB);
            sendPacketToCloud(topic, p);
            char str[10];
            sprintf(str, "%d", pageIndex);
            JSON_RemoveKey(g_mergePayloads, str);
            free(topic);
            return;
        }

        // Ping services every 10 seconds
        pingServiceCount++;
        if (pingServiceCount > 20) {
            pingServiceCount = 0;
            mosquitto_publish(mosq, NULL, "DEVICE_SERVICES/TUYA/0", strlen("WIFI_PING"), "WIFI_PING", 0, false);
            mosquitto_publish(mosq, NULL, "CORE_SERVICES/CORE/0", strlen("CORE_PING"), "CORE_PING", 0, false);
            mosquitto_publish(mosq, NULL, "DEVICE_SERVICES/BLE_0A00/0", strlen("BLE_PING"), "BLE_PING", 0, false);
        }

        // Process watchdog for wifi service
        g_wifiServiceWatchdog++;
        if (g_wifiServiceWatchdog > 60) {
            g_wifiServiceWatchdog = 0;
            logInfo("Restarting wifi service");
            system("systemctl restart hg_wifi");
        }

        g_coreServiceWatchdog++;
        if (g_coreServiceWatchdog > 120) {
            g_coreServiceWatchdog = 0;
            logInfo("Restarting CORE service");
            system("systemctl restart hg_core");
        }

        g_bleServiceWatchdog++;
        if (g_bleServiceWatchdog > 240) {
            g_bleServiceWatchdog = 0;
            logInfo("Restarting BLE service");
            system("systemctl restart hg_ble");
        }
    }
}

int main( int argc,char ** argv ) {
    int xRun = 1;
    pthread_t thrConnectToAws;

    queue_received_aws = newQueue(QUEUE_SIZE);
    queue_mos_sub = newQueue(QUEUE_SIZE);
    g_mergePayloads = JSON_CreateObject();

    getHcInformation();
    Aws_Init();
    Mosq_Init();
    logInfo("Current Time: %s", get_localtime_now());
    int currentHour = get_hour_today();
    if (currentHour >= 6 && currentHour < 21) {
        PlayAudio("server_connecting");
    }
    Aws_SyncDatabase();
    int size_queue = 0;
    bool check_flag = false;
    pthread_create(&thrConnectToAws, NULL, Thread_ConnectToAws, NULL);
    while (xRun!=0) {
        Aws_ProcessLoop();
        Aws_SendMergePayload();
        Mosq_ProcessLoop();
        Mosq_ProcessMessage();

        size_queue = get_sizeQueue(queue_received_aws);
        if (size_queue > 0) {
            char* recvMsg = (char *)dequeue(queue_received_aws);
            JSON* recvPacket = JSON_Parse(recvMsg);
            free(recvMsg);
            if (recvPacket) {
                JSON* state = JSON_GetObject(recvPacket, "state");
                JSON* reported = JSON_GetObject(state, "reported");
                int reqType = JSON_GetNumber(reported, "type");
                int sender = JSON_GetNumber(reported, "sender");
                if (sender == 0) {
                    sender = JSON_GetNumber(recvPacket, "sender");
                }
                if (reqType == 0) {
                    reqType = JSON_GetNumber(recvPacket, "type");
                }
                if (sender != SENDER_APP_VIA_LOCAL && sender != SENDER_APP_TO_CLOUD) {
                    continue;
                }
                char* senderId;
                int provider = -1;
                if (JSON_HasKey(reported, "senderId")) {
                    senderId = JSON_GetText(reported, "senderId");
                }
                if (JSON_HasKey(reported, "provider")) {
                    provider = JSON_GetNumber(reported, "provider");
                }
                switch(reqType) {
                    case TYPE_ADD_DEVICE:
                    case TYPE_CTR_DEVICE:
                    case TYPE_DEL_DEVICE:
                    case TYPE_LOCK_KIDS:
                    case TYPE_LOCK_AGENCY:
                    case TYPE_DIM_LED_SWITCH: {
                        int pageIndex = JSON_HasKey(reported, "pageIndex")? JSON_GetNumber(reported, "pageIndex") : 1;
                        JSON_ForEach(o, reported) {
                            if (cJSON_IsObject(o)) {
                                JSON_SetText(o, "deviceId", o->string);
                                JSON_SetText(o, "senderId", senderId);
                                JSON_SetNumber(o, "provider", provider);
                                JSON_SetNumber(o, "pageIndex", pageIndex);
                                sendPacketTo(SERVICE_CORE, reqType, o);
                            } else if (cJSON_IsNull(o)) {
                                JSON* p = JSON_CreateObject();
                                JSON_SetText(p, "deviceId", o->string);
                                JSON_SetText(o, "senderId", senderId);
                                JSON_SetNumber(o, "provider", provider);
                                sendPacketTo(SERVICE_CORE, reqType, p);
                                JSON_Delete(p);
                            }
                        }
                        break;
                    }
                    case TYPE_GET_ALL_DEVICES:
                    case TYPE_SET_GROUP_TTL: {
                        sendPacketTo(SERVICE_CORE, reqType, reported);
                        break;
                    }
                    case TYPE_CTR_SCENE:
                    case TYPE_ADD_SCENE:
                    case TYPE_UPDATE_SCENE:
                    case TYPE_DEL_SCENE: {
                        int pageIndex = JSON_HasKey(reported, "pageIndex")? JSON_GetNumber(reported, "pageIndex") : 1;
                        JSON_ForEach(o, reported) {
                            if (cJSON_IsObject(o)) {
                                JSON_SetText(o, "Id", o->string);
                                if (JSON_HasKey(o, "scenes")) {
                                    char* scenes = JSON_GetText(o, "scenes");
                                    scenes[1] = 0;
                                    JSON_SetText(o, "sceneType", scenes);
                                }
                                JSON_SetNumber(o, "pageIndex", pageIndex);
                                JSON_SetText(o, "senderId", senderId);
                                sendPacketTo(SERVICE_CORE, reqType, o);
                            } else if (cJSON_IsNull(o)) {
                                JSON* p = JSON_CreateObject();
                                JSON_SetText(p, "Id", o->string);
                                sendPacketTo(SERVICE_CORE, reqType, p);
                                JSON_Delete(p);
                            }
                        }
                        break;
                    }
                    case TYPE_ADD_GROUP_LIGHT:
                    case TYPE_DEL_GROUP_LIGHT:
                    case TYPE_UPDATE_GROUP_LIGHT:
                    case TYPE_CTR_GROUP_NORMAL:
                    case TYPE_ADD_GROUP_LINK:
                    case TYPE_DEL_GROUP_LINK:
                    case TYPE_UPDATE_GROUP_LINK: {
                        int pageIndex = JSON_HasKey(reported, "pageIndex")? JSON_GetNumber(reported, "pageIndex") : 1;
                        JSON_ForEach(o, reported) {
                            if (cJSON_IsObject(o)) {
                                JSON_SetText(o, "groupAddr", o->string);
                                JSON_SetText(o, "senderId", senderId);
                                JSON_SetNumber(o, "provider", provider);
                                JSON_SetNumber(o, "pageIndex", pageIndex);
                                sendPacketTo(SERVICE_CORE, reqType, o);
                            } else if (cJSON_IsNull(o)) {
                                JSON* p = JSON_CreateObject();
                                JSON_SetText(p, "groupAddr", o->string);
                                JSON_SetText(o, "senderId", senderId);
                                JSON_SetNumber(o, "provider", provider);
                                sendPacketTo(SERVICE_CORE, reqType, p);
                                JSON_Delete(p);
                            }
                        }
                        break;
                    }
                    case TYPE_GET_DEVICE_HISTORY:
                    case TYPE_GET_GROUPS_OF_DEVICE:
                    case TYPE_GET_SCENES_OF_DEVICE:
                    case TYPE_SYNC_DEVICE_STATUS:
                        sendPacketTo(SERVICE_CORE, reqType, recvPacket);
                        break;
                    case TYPE_DEL_HOMEKIT:
                        system("rm -r db");
                        system("systemctl restart hg_homekit");
                        sendNotiToUser("Xóa liên kết homekit thành công", false);
                        break;
                    case TYPE_GET_LOG:
                        logInfo("TYPE_GET_LOG");
                        char* topic = Aws_GetTopic(PAGE_NONE, 0, TOPIC_NOTI_PUB);
                        if (JSON_HasKey(reported, "fileName")) {
                            char cmd[200];
                            char* fileName = JSON_GetText(reported, "fileName");
                            sprintf(cmd, "python3 /usr/bin/uploadFile.pyc %s %s", fileName, g_homeId);
                            logInfo("Uploading file %s to S3", fileName);
                            system(cmd);
                            char str[200];
                            sprintf(str, "{\"sender\":11, \"type\": %d, \"message\": \"DONE\"}", TYPE_GET_LOG);
                            mqttCloudPublish(topic, str);
                        } else if (JSON_HasKey(reported, "reboot")) {
                            char str[200];
                            sprintf(str, "{\"sender\":11, \"type\": %d, \"message\": \"RESTARTING\"}", TYPE_GET_LOG);
                            mqttCloudPublish(topic, str);
                            PlayAudio("restarting");
                            system("reboot");
                        } else if (JSON_HasKey(reported, "remotessh")) {
                            // FILE* f = fopen("/root/.ssh/known_hosts", "w");
                            // fprintf(f, "|1|QBx6g0gYfbxK++MJwhczSYQrM6M=|/U9Fyytg3KGEmV1RRRvLf1pzo4I= ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDxYGqSKVwJpQD1F0YIhz+bd5lpl$\n|1|NME2zKJnGNW96RCTyz2gx9rGa9M=|GP7CvNbUuBjjVUuTAIBsrrmK8KU= ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDxYGqSKVwJpQD1F0YIhz+bd5lpl$");
                            // fclose(f);

                            char str[200];
                            sprintf(str, "{\"sender\":11, \"type\": %d, \"message\": \"ENABLED_SSH\"}", TYPE_GET_LOG);
                            mqttCloudPublish(topic, str);
                            char cmd[200];
                            sprintf(cmd, "ssh -o \"UserKnownHostsFile=/dev/null\" -o \"StrictHostKeyChecking=no\" -R hg_%s:22:localhost:22 serveo.net", g_homeId);
                            popen(cmd, "r");
                        }
                        free(topic);
                        break;
                }
            }
        }
        usleep(100);
    }
    return 0;
}


void* Thread_ConnectToAws(void* p) {
    bool clientSessionPresent = false;
    bool brokerSessionPresent = false;
    int returnStatus;

    while (1) {
        if (g_awsIsConnected == false) {
            returnStatus = connectToServerWithBackoffRetries( &networkContext, &mqttContext, &clientSessionPresent, &brokerSessionPresent );
            if (returnStatus == EXIT_SUCCESS) {
                returnStatus = subscribeToTopic( &mqttContext );
                if (returnStatus == EXIT_SUCCESS) {
                    g_awsIsConnected = true;
                    sendToService(SERVICE_CFG, 0, "AWS_CONNECTED");
                }
            }
        }
        sleep(1);
    }
}
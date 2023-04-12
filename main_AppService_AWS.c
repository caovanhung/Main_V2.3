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
//#include <pthread>
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
#include "error.h"
#include "define.h"
#include "helper.h"
#include "cJSON.h"
/*-----------------------------------------------------------*/

const char* SERVICE_NAME = SERVICE_AWS;
FILE *fptr;
int rc;
struct mosquitto * mosq;
struct Queue *queue_received_aws,*queue_mos_sub;
MQTTFixedBuffer_t networkBuffer;
TransportInterface_t transport;
NetworkContext_t networkContext;
OpensslParams_t opensslParams;

pthread_mutex_t mutex_lock_t            = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_Queue         = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutex_lock_mosq_t       = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dataUpdate_MosqQueue     = PTHREAD_COND_INITIALIZER;

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
static int g_awsDevicePageNum = 0, g_awsGroupPageNum = 0, g_awsScenePageNum = 0;
static int g_awsSyncDatabaseStep = 0; // 1: getting number of pages;  2: syncing database

uint32_t generateRandomNumber();
int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext,MQTTContext_t * pMqttContext,bool * pClientSessionPresent,bool * pBrokerSessionPresent );
void handleIncomingPublish( MQTTPublishInfo_t * pPublishInfo,uint16_t packetIdentifier );
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
void handleIncomingPublish( MQTTPublishInfo_t * pPublishInfo,uint16_t packetIdentifier )
{
    assert( pPublishInfo != NULL );
    memset(aws_buff,'\0',MAX_SIZE_ELEMENT_QUEUE);
    bool flag = false;
    strncpy(aws_buff, pPublishInfo->pPayload, pPublishInfo->payloadLength);
    aws_buff[(int)pPublishInfo->payloadLength] = '\0';
    AWS_short_message_received(aws_buff);
    if (isContainString(pPublishInfo->pTopicName, "get")) {
        flag = true;
        JSON* recvPacket = JSON_Parse(aws_buff);
        JSON* state = JSON_GetObject(recvPacket, "state");
        JSON* reported = JSON_GetObject(state, "reported");

        if (isContainString(pPublishInfo->pTopicName, "d_")) {
            JSON_SetNumber(reported, "type", TYPE_GET_DEVICES);
            JSON_SetNumber(reported, "sender", SENDER_APP_VIA_CLOUD);
        } else if(isContainString(pPublishInfo->pTopicName, "s_")) {
            JSON_SetNumber(reported, "type", TYPE_GET_SCENES);
            JSON_SetNumber(reported, "sender", SENDER_APP_VIA_CLOUD);
        } else if(isContainString(pPublishInfo->pTopicName, "g_")) {
            JSON_SetNumber(reported, "type", TYPE_GET_GROUPS);
            JSON_SetNumber(reported, "sender", SENDER_APP_VIA_CLOUD);
        } else {
            JSON_SetNumber(reported, "type", TYPE_GET_NUM_OF_PAGE);
            JSON_SetNumber(reported, "sender", SENDER_APP_VIA_CLOUD);
        }
        memset(aws_buff,'\0',MAX_SIZE_ELEMENT_QUEUE);
        char* updatedPacket = cJSON_PrintUnformatted(recvPacket);
        strcpy(aws_buff, updatedPacket);
        free(updatedPacket);
        JSON_Delete(recvPacket);
    }

    if (flag == false || g_awsSyncDatabaseStep) {
        int size_queue = get_sizeQueue(queue_received_aws);
        if (size_queue < QUEUE_SIZE) {
            enqueue(queue_received_aws, aws_buff);
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
        handleIncomingPublish( pDeserializedInfo->pPublishInfo, packetIdentifier );
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

                /* Make sure ACK packet identifier matches with Request packet identifier. */
                if ( globalSubscribePacketIdentifier != packetIdentifier ) {
                    logError("globalSubscribePacketIdentifier = %d, packetIdentifier = %d", globalSubscribePacketIdentifier, packetIdentifier);
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
                printf("Unknown packet type received:(%02x).\n\n",pPacketInfo->type);
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
    globalSubscribePacketIdentifier = MQTT_GetPacketId( pMqttContext );
    /* Send SUBSCRIBE packet. */
    mqttStatus = MQTT_Subscribe( pMqttContext,
                                 g_awsSubscriptionList,
                                 g_awsSubscriptionCount,
                                 globalSubscribePacketIdentifier );

    if (mqttStatus != MQTTSuccess ) {
        LogError( (get_localtime_now()),( "Failed to send SUBSCRIBE packet to broker with error = %s.",
                    MQTT_Status_strerror( mqttStatus ) ) );
        returnStatus = EXIT_FAILURE;
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
        printf("Error with result code: %d\n", rc);
        exit(-1);
    }
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_AWS, 0);
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_MANAGER_SETTING, 0);
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_CONTROL_LOCAL, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) 
{
    long long int TimeCreated = timeInMilliseconds();
    if(!memcmp(msg->topic,MOSQ_LayerService_App,strlen(MOSQ_LayerService_App)-1))
    {
        pthread_mutex_lock(&mutex_lock_mosq_t);
        int size_queue = get_sizeQueue(queue_mos_sub);
        if(size_queue < QUEUE_SIZE)
        {
            enqueue(queue_mos_sub,(char *) msg->payload);
            pthread_cond_broadcast(&dataUpdate_MosqQueue);
            pthread_mutex_unlock(&mutex_lock_mosq_t);            
        }
        else
        {
           pthread_mutex_unlock(&mutex_lock_mosq_t); 
        }
    }
    else if(isMatchString(msg->topic,MOSQ_TOPIC_MANAGER_SETTING)) //process init MQTT local
    {
        pthread_mutex_lock(&mutex_lock_mosq_t);
        int size_queue = get_sizeQueue(queue_mos_sub);
        if(size_queue < QUEUE_SIZE)
        {
            char *message;
            JSON_Value *schema = NULL;
            schema = json_parse_string((char *) msg->payload);
            char type = json_object_get_number(json_object(schema),KEY_TYPE);
            getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,type,MOSQ_ActResponse,"INIT",0,(char *) msg->payload);
            enqueue(queue_mos_sub,message);
            pthread_cond_broadcast(&dataUpdate_MosqQueue);
            pthread_mutex_unlock(&mutex_lock_mosq_t);            
        }
        else
        {
           pthread_mutex_unlock(&mutex_lock_mosq_t); 
        }
    } 
    else //process AWS message
    {
        char *result;
        char* control_local_buff = calloc((int)msg->payloadlen+1,sizeof(char));
        strncpy(control_local_buff,msg->payload,msg->payloadlen);
        control_local_buff[(int)msg->payloadlen] = '\0';
        AWS_short_message_received(control_local_buff);
        pthread_mutex_lock(&mutex_lock_t);
        int size_queue = get_sizeQueue(queue_received_aws);
        if(size_queue < QUEUE_SIZE)
        {
            enqueue(queue_received_aws,result);
            pthread_cond_broadcast(&dataUpdate_Queue);
            pthread_mutex_unlock(&mutex_lock_t);
        }
        else
        {
           pthread_mutex_unlock(&mutex_lock_t); 
        }
        if(control_local_buff != NULL) free(control_local_buff);
        if(result != NULL) free(result);
    }
}

void Aws_BuildSubscriptionTopics() {
    logInfo("g_awsDevicePageNum = %d", g_awsDevicePageNum);
    logInfo("g_awsGroupPageNum = %d", g_awsGroupPageNum);
    logInfo("g_awsScenePageNum = %d", g_awsScenePageNum);
    if (g_awsSubscriptionList) {
        for (int i = 0; i < g_awsSubscriptionCount; i++) {
            free(g_awsSubscriptionList[i].pTopicFilter);
        }
        free(g_awsSubscriptionList);
        g_awsSubscriptionCount = 0;
    }
    // Each page type need 2 topics for getting all page and get an update)
    int topicNum = (g_awsDevicePageNum + g_awsGroupPageNum + g_awsScenePageNum) * 2 + 1;
    int topicIdx = 0;
    g_awsSubscriptionList = malloc(sizeof(MQTTSubscribeInfo_t) * topicNum);
    for (int i = 0; i < g_awsDevicePageNum; i++) {
        g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
        g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_DEVICE, i + 1, TOPIC_GET_SUB);
        g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
        topicIdx++;
        g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
        g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_DEVICE, i + 1, TOPIC_UPD_SUB);
        g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
        topicIdx++;
    }

    for (int i = 0; i < g_awsGroupPageNum; i++) {
        g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
        g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_GROUP, i + 1, TOPIC_GET_SUB);
        g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
        topicIdx++;
        g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
        g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_GROUP, i + 1, TOPIC_UPD_SUB);
        g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
        topicIdx++;
    }

    for (int i = 0; i < g_awsScenePageNum; i++) {
        g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
        g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_SCENE, i + 1, TOPIC_GET_SUB);
        g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
        topicIdx++;
        g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
        g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_SCENE, i + 1, TOPIC_UPD_SUB);
        g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
        topicIdx++;
    }
    g_awsSubscriptionList[topicIdx].qos = MQTTQoS0;
    g_awsSubscriptionList[topicIdx].pTopicFilter = Aws_GetTopic(PAGE_MAIN, 0, TOPIC_NOTI_PUB);
    g_awsSubscriptionList[topicIdx].topicFilterLength = strlen(g_awsSubscriptionList[topicIdx].pTopicFilter);
    topicIdx++;
    g_awsSubscriptionCount = topicIdx;
}


void Aws_Init() {
    int returnStatus = EXIT_SUCCESS;
    networkContext.pParams = &opensslParams;
    returnStatus = initializeMqtt( &mqttContext, &networkContext );
    // Initialize the subscribe list
    g_awsSubscriptionList = (MQTTSubscribeInfo_t*)malloc(sizeof(MQTTSubscribeInfo_t));
    g_awsSubscriptionList[0].qos = MQTTQoS0;
    g_awsSubscriptionList[0].pTopicFilter = Aws_GetTopic(PAGE_MAIN, 0, TOPIC_GET_SUB);
    g_awsSubscriptionList[0].topicFilterLength = strlen(g_awsSubscriptionList[0].pTopicFilter);
    g_awsSubscriptionCount++;
}

void Aws_ProcessLoop() {
    MQTTStatus_t mqttStatus;
    bool clientSessionPresent = false;
    bool brokerSessionPresent = false;
    int returnStatus;
    if (g_awsIsConnected == false) {
        returnStatus = connectToServerWithBackoffRetries( &networkContext, &mqttContext, &clientSessionPresent, &brokerSessionPresent );
        if (returnStatus == EXIT_SUCCESS) {
            returnStatus = subscribeToTopic( &mqttContext );
            if (returnStatus == EXIT_SUCCESS) {
                g_awsIsConnected = true;
            }
        }
    } else {
        mqttStatus = MQTT_ProcessLoop( &mqttContext, 5 );
        if (mqttStatus != MQTTSuccess ) {
            returnStatus = EXIT_FAILURE;
            g_awsIsConnected = false;
            (void) Openssl_Disconnect( &networkContext );
            LogError( (get_localtime_now()),( "MQTT_ProcessLoop returned with status = %s.",MQTT_Status_strerror( mqttStatus ) ) );
        }
    }

    if (g_awsIsConnected) {
        if (g_awsDevicePageNum == 0) {
            // Get number of pageIndexes of devices, groups, scenes
            char* topic = Aws_GetTopic(PAGE_MAIN, 0, TOPIC_GET_PUB);
            mqttCloudPublish(topic, "");
            free(topic);
            g_awsSyncDatabaseStep = 1;
            g_awsDevicePageNum = 1;
        } else if (g_awsSyncDatabaseStep == 1 && g_awsGroupPageNum > 0) {
            // Send request to get page 1 of devices
            char* topic = Aws_GetTopic(PAGE_DEVICE, 1, TOPIC_GET_PUB);
            mqttCloudPublish(topic, "");
            free(topic);
            g_awsSyncDatabaseStep = 2;
        }
    }
}

void Mosq_Init() {
    mosquitto_lib_init();
    mosq = mosquitto_new(MQTT_MOSQUITTO_CIENT_ID, true, NULL);
    rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    if(rc != 0)
    {
        LogInfo((get_localtime_now()),("mosquitto_username_pw_set! Error Code: %d\n", rc));
        return;
    }
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
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
        char *val_input = (char*)malloc(10000);
        strcpy(val_input,(char *)dequeue(queue_mos_sub));
        logInfo("Received msg from MQTT local: %s", val_input);

        JSON* recvPacket = JSON_Parse(val_input);
        int pageIndex = -1;
        if (JSON_HasKey(recvPacket, "pageIndex")) {
            pageIndex = JSON_GetNumber(recvPacket, "pageIndex");
        }
        int reqType = JSON_GetNumber(recvPacket, MOSQ_ActionType);
        JSON* payload = JSON_Parse(JSON_GetText(recvPacket, MOSQ_Payload));
        switch (reqType) {
            case GW_RESPONSE_DEVICE_CONTROL:
            case GW_RESPONSE_DEVICE_KICKOUT:
            case GW_RESPONSE_DEVICE_STATE: {
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
                char* topic = Aws_GetTopic(PAGE_SCENE, 1, TOPIC_UPD_PUB);
                sendPacketToCloud(topic, payload);
                free(topic);
                break;
            }
            case TYPE_GET_DEVICE_HISTORY:
            case TYPE_NOTIFI_REPONSE: {
                char* topic = Aws_GetTopic(PAGE_MAIN, 0, TOPIC_NOTI_PUB);
                sendPacketToCloud(topic, payload);
                break;
            }
        }
        JSON_Delete(recvPacket);
        JSON_Delete(payload);
        free(val_input);
    }
}

int main( int argc,char ** argv ) {
    pthread_t thr[3];
    int xRun = 1;

    queue_received_aws = newQueue(QUEUE_SIZE);
    queue_mos_sub = newQueue(QUEUE_SIZE);

    getHcInformation();
    Aws_Init();
    Mosq_Init();

    int size_queue = 0;
    bool check_flag = false;
    while (xRun!=0) {
        Aws_ProcessLoop();
        Mosq_ProcessLoop();
        Mosq_ProcessMessage();

        size_queue = get_sizeQueue(queue_received_aws);
        if (size_queue > 0) {
            int reponse = 0;
            long long TimeCreat = 0;

            Pre_parse *pre_detect = (Pre_parse *)malloc(sizeof(Pre_parse));
            Info_device *inf_device = (Info_device *)malloc(sizeof(Info_device));
            // Info_device_Debug *inf_device_Debug = (Info_device_Debug *)malloc(sizeof(Info_device_Debug));

            Info_scene *inf_scene = (Info_scene *)malloc(sizeof(Info_scene));
            InfoProvisonGateway InfoProvisonGateway_t;
            Info_group *info_group_t = (Info_group *)malloc(sizeof(Info_group));

            char* recvMsg = (char *)dequeue(queue_received_aws);
            char *val_input = (char*)malloc(strlen(recvMsg) + 1);
            strcpy(val_input, recvMsg);
            check_flag = AWS_pre_detect_message_received(pre_detect,val_input);
            if (check_flag && (pre_detect->sender == SENDER_APP_VIA_LOCAL || pre_detect->sender == SENDER_APP_VIA_CLOUD ))
            {
                printf("\n\r");
                logInfo("Received msg from MQTT cloud: %s", val_input);
                JSON* recvPacket = JSON_Parse(recvMsg);
                if (recvPacket == NULL) {
                    continue;
                }
                JSON* state = JSON_GetObject(recvPacket, "state");
                JSON* reported = JSON_GetObject(state, "reported");
                char *topic;
                char *payload;
                char *message;
                switch(pre_detect->type)
                {
                    case TYPE_CTR_DEVICE:
                    {
                        AWS_getInfoControlDevice(inf_device,pre_detect);
                        MOSQ_getTemplateControlDevice(&payload,inf_device);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_CTR_GROUP_NORMAL:
                    {
                        AWS_getInfoControlGroupNormal(info_group_t,pre_detect);
                        MOSQ_getTemplateControlGroupNormal(&payload, info_group_t);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_DIM_LED_SWITCH:
                    {
                        AWS_getInfoDimLedDevice(inf_device, pre_detect);
                        MOSQ_getTemplateDimLedDevice(&payload, inf_device);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_LOCK_KIDS:
                    {
                        AWS_getInfoLockDevice(inf_device, pre_detect);
                        MOSQ_getTemplateLockDevice(&payload, inf_device);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_LOCK_AGENCY:
                    {
                        AWS_getInfoLockDevice(inf_device, pre_detect);
                        MOSQ_getTemplateLockDevice(&payload, inf_device);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_CTR_SCENE:
                    {
                        TimeCreat = timeInMilliseconds();
                        AWS_getInfoControlSecene(inf_scene,pre_detect);
                        MOSQ_getTemplateControSecene(&payload,inf_scene);
                        getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_CTR_SCENE,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                        get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_CTR_SCENE,MOSQ_ActResponse);
                        mqttLocalPublish(topic, message);
                        break;
                    }
                    case TYPE_ADD_DEVICE:
                    {
                        TimeCreat = timeInMilliseconds();
                        check_flag =  AWS_get_info_device(inf_device, pre_detect);
                        if (check_flag) {
                            // Send device info to Core service
                            MOSQ_getTemplateAddDevice(&payload, inf_device);
                            sendToService(SERVICE_CORE, pre_detect->type, payload);
                        } else {
                            logError("Invalid message");
                        }
                        break;
                    }
                    case TYPE_DEL_DEVICE:
                    {
                        AWS_getInfoDeleteDevice(inf_device, pre_detect);
                        MOSQ_getTemplateDeleteDevice(&payload, inf_device);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_ADD_SCENE:
                    {
                        TimeCreat = timeInMilliseconds();
                        if(AWS_getInfoScene(inf_scene,pre_detect))
                        {
                            MOSQ_getTemplateAddScene(&payload,inf_scene);
                            getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_ADD_SCENE,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                            get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_ADD_SCENE,MOSQ_ActResponse);
                            mqttLocalPublish(topic, message);
                        }
                        else
                        {
                            LogError((get_localtime_now()),("ADD_SCENE_HC to Failed, because lost Condition\n"));
                        }
                        break;
                    }
                    case TYPE_DEL_SCENE:
                    {
                        TimeCreat = timeInMilliseconds();

                        MOSQ_getTemplateDeleteScene(&payload,pre_detect->object);
                        getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_DEL_SCENE,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                        get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_DEL_SCENE,MOSQ_ActResponse);
                        mqttLocalPublish(topic, message);
                        break;
                    }
                    case TYPE_UPDATE_SCENE:
                    {
                        TimeCreat = timeInMilliseconds();
                        if(AWS_getInfoScene(inf_scene,pre_detect))
                        {
                            MOSQ_getTemplateAddScene(&payload,inf_scene);
                            getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_UPDATE_SCENE,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                            get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_UPDATE_SCENE,MOSQ_ActResponse);
                            mqttLocalPublish(topic, message);
                        }
                        else
                        {
                            LogError((get_localtime_now()),("ADD_SCENE_HC to Failed, because lost Condition\n"));
                        }
                        break;
                    }
                    case TYPE_ADD_GROUP_NORMAL:
                    {
                        TimeCreat = timeInMilliseconds();
                        AWS_getInfoAddGroupNormal(info_group_t, pre_detect);
                        MOSQ_getTemplateAddGroupNormal(&payload, info_group_t);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_DEL_GROUP_NORMAL:
                    {
                        AWS_getInfoDeleteGroupNormal(info_group_t,pre_detect);
                        MOSQ_getTemplateDeleteGroupNormal(&payload, info_group_t);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_NORMAL:
                    {
                        AWS_getInfoAddGroupNormal(info_group_t, pre_detect);
                        MOSQ_getTemplateAddGroupNormal(&payload, info_group_t);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                     }
                    case TYPE_ADD_GROUP_LINK:
                    {
                        AWS_getInfoAddGroupNormal(info_group_t,pre_detect);
                        MOSQ_getTemplateAddGroupNormal(&payload,info_group_t);
                        get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_ADD_GROUP_LINK,MOSQ_ActResponse);
                        getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_ADD_GROUP_LINK,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                        mqttLocalPublish(topic, message);
                        break;
                    }
                    case TYPE_DEL_GROUP_LINK:
                    {
                        TimeCreat = timeInMilliseconds();
                        AWS_getInfoDeleteGroupNormal(info_group_t,pre_detect);
                        MOSQ_getTemplateDeleteGroupNormal(&payload,info_group_t);
                        getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_DEL_GROUP_LINK,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                        get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_DEL_GROUP_LINK,MOSQ_ActResponse);
                        mqttLocalPublish(topic, message);
                        break;
                    }
                    case TYPE_UPDATE_GROUP_LINK:
                    {
                        TimeCreat = timeInMilliseconds();
                        AWS_getInfoAddGroupNormal(info_group_t,pre_detect);
                        MOSQ_getTemplateAddGroupNormal(&payload,info_group_t);
                        get_topic(&topic,MOSQ_LayerService_Core,SERVICE_CORE,TYPE_UPDATE_GROUP_LINK,MOSQ_ActResponse);
                        getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_UPDATE_GROUP_LINK,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                        mqttLocalPublish(topic, message);
                        break;
                    }
                    case TYPE_ADD_GW:
                    {
                        AWS_getInfoGateway(&InfoProvisonGateway_t, pre_detect);
                        MOSQ_getTemplateAddGateway(&payload, &InfoProvisonGateway_t);
                        sendToService(SERVICE_CORE, pre_detect->type, payload);
                        break;
                    }
                    case TYPE_UPDATE_SERVICE:
                    {
                        LogInfo((get_localtime_now()),("TYPE_UPDATE_SERVICE "));
                        MOSQ_getTemplateUpdateService(&payload,pre_detect);
                        TimeCreat = timeInMilliseconds();
                        getFormTranMOSQ(&message,MOSQ_LayerService_App,SERVICE_AWS,TYPE_UPDATE_SERVICE,MOSQ_ActResponse,pre_detect->object,TimeCreat,payload);
                        get_topic(&topic,MOSQ_LayerService_Manager,MOSQ_NameService_Manager_ServieceManager,TYPE_UPDATE_SERVICE,MOSQ_ActResponse);
                        mqttLocalPublish(topic, message);
                        break;
                    }
                    case TYPE_GET_NUM_OF_PAGE: {
                        g_awsDevicePageNum = 1;
                        g_awsScenePageNum = 1;
                        g_awsGroupPageNum = 1;
                        if (JSON_HasKey(reported, "pageIndex0")) {
                            g_awsDevicePageNum = JSON_GetNumber(reported, "pageIndex0");
                        }
                        if (JSON_HasKey(reported, "pageIndex2")) {
                            g_awsScenePageNum = JSON_GetNumber(reported, "pageIndex2");
                        }
                        if (JSON_HasKey(reported, "pageIndex3")) {
                            g_awsGroupPageNum = JSON_GetNumber(reported, "pageIndex3");
                        }
                        Aws_BuildSubscriptionTopics();
                        g_awsIsConnected = false;
                        Openssl_Disconnect( &networkContext );
                        break;
                    }
                    case TYPE_GET_DEVICES: {
                        JSON_ForEach(item, reported) {
                            if (cJSON_IsObject(item)) {
                                // logInfo(item->string);
                            }
                        }
                        g_awsSyncDatabaseStep = 0;
                    }
                }
                free(val_input);
                free(inf_device);
                free(pre_detect);
                free(inf_scene);
                free(info_group_t);
                JSON_Delete(recvPacket);
            }
            else
            {
                AWS_detect_message_received_for_update(pre_detect,val_input);
                char *topic;
                char *message;
                switch (pre_detect->type)
                {
                    case TYPE_GET_DEVICE_HISTORY:
                    {
                        JSON* packet = JSON_Parse(val_input);
                        sendPacketTo(SERVICE_CORE, pre_detect->type, packet);
                        JSON_Delete(packet);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
            free(recvMsg);
        }
        usleep(100);
    }
    return 0;
}


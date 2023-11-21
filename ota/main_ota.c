#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>     // string function definitions
#include <fcntl.h>      // File control definitions
#include <errno.h>      // Error number definitions
#include <termios.h>    // POSIX terminal control definitionss
#include <sys/time.h>   // time calls
#include <sys/un.h>
#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <stdint.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <mosquitto.h>

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


#define AWS_TOPIC_SETTING       "$aws/things/agency2022/shadow/name/Setting/update/accepted"
#define AWS_TOPIC_ACCOUNTINFO   "$aws/things/%s/shadow/name/accountInfo/update"
#define AWS_TOPIC_NOTIFY        "$aws/things/%s/shadow/name/notify"


const char* SERVICE_NAME = "OTA";
uint8_t SERVICE_ID = 9;
bool g_printLog = true;

struct mosquitto * mosq;
struct Queue *queue_received_aws,*queue_mos_sub;
MQTTFixedBuffer_t networkBuffer;
TransportInterface_t transport;
NetworkContext_t networkContext;
OpensslParams_t opensslParams;
const char g_thingId[100];
const char g_hcAddr[10] = {0};
int g_currentVersion = 0;

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


uint32_t generateRandomNumber();
int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext,MQTTContext_t * pMqttContext,bool * pClientSessionPresent,bool * pBrokerSessionPresent );
void Aws_OnReveicedMessage( MQTTPublishInfo_t * pPublishInfo,uint16_t packetIdentifier );
void eventCallback( MQTTContext_t * pMqttContext,MQTTPacketInfo_t * pPacketInfo,MQTTDeserializedInfo_t * pDeserializedInfo );
int initializeMqtt( MQTTContext_t * pMqttContext,NetworkContext_t * pNetworkContext );
int establishMqttSession( MQTTContext_t * pMqttContext,bool createCleanSession,bool * pSessionPresent );
int disconnectMqttSession( MQTTContext_t * pMqttContext );
int subscribeToTopic( MQTTContext_t * pMqttContext );
int getNextFreeIndexForOutgoingPublishes( uint8_t * pIndex );
void cleanupOutgoingPublishAt( uint8_t index );
void cleanupOutgoingPublishes( void );
void cleanupOutgoingPublishWithPacketID( uint16_t packetId );
void updateSubAckStatus( MQTTPacketInfo_t * pPacketInfo );
int Aws_PublishMessage(const char* pTopic, const char* pMessage);


void GetThingId() {
    // Get thingId
    FILE* f = fopen("app.json", "r");
    char buff[1000];
    if (f) {
        fread(buff, sizeof(char), 1000, f);
        fclose(f);
        JSON* setting = JSON_Parse(buff);
        char* thingId = JSON_GetText(setting, "thingId");
        char* hcAddr = JSON_GetText(setting, "hcAddr");
        if (thingId) {
            StringCopy(g_thingId, thingId);
        }
        // if (hcAddr) {
        //     StringCopy(g_hcAddr, hcAddr);
        // }
        JSON_Delete(setting);
    }
}

int GetCurrentVersion() {
    FILE* f = fopen("version", "r");
    char buff[20];
    if (f) {
        fread(buff, sizeof(char), 10, f);
        fclose(f);
        g_currentVersion = atoi(buff);
        return g_currentVersion;
    }
    return 0;
}

void SaveCurrentVersion(int version) {
    FILE* f = fopen("version", "w");
    fprintf(f, "%d", version);
    fclose(f);
}

uint32_t generateRandomNumber()
{
    return(rand());
}

void sendPacketToCloud(const char* topic, JSON* packet) {
    char* message = cJSON_PrintUnformatted(packet);
    Aws_PublishMessage(topic, message);
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
void Aws_OnReveicedMessage( MQTTPublishInfo_t * pPublishInfo,uint16_t packetIdentifier )
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

    if (StringContains(topic, "accountInfo")) {
        free(topic);
        return;
    }

    JSON* recvPacket = JSON_Parse(aws_buff);
    if (recvPacket) {
        JSON* state = JSON_GetObject(recvPacket, "state");
        if (state) {
            JSON* reported = JSON_GetObject(state, "reported");
            if (reported) {
                int sender = JSON_GetNumber(reported, "sender");
                if (sender == SENDER_APP_VIA_LOCAL || sender == SENDER_APP_TO_CLOUD) {
                    // logInfo("Received msg from cloud. topic: %s, payload: %s", topic, aws_buff);
                    int size_queue = get_sizeQueue(queue_received_aws);
                    if (size_queue < QUEUE_SIZE) {
                        enqueue(queue_received_aws, aws_buff);
                    }
                }
            }
        }
    }
    free(topic);
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
        Aws_OnReveicedMessage( pDeserializedInfo->pPublishInfo, packetIdentifier );
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
int Aws_PublishMessage(const char* pTopic, const char* pMessage) {
    int returnStatus = EXIT_SUCCESS;
    MQTTContext_t* pMqttContext = &mqttContext;

    MQTTStatus_t mqttStatus = MQTTSuccess;
    MQTTPublishInfo_t pubInfo;
    uint32_t publishCount = 0;
    const uint32_t maxPublishCount = MQTT_PUBLISH_COUNT_PER_LOOP;
    uint8_t publishIndex = MAX_OUTGOING_PUBLISHES;

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
    mosquitto_subscribe(mosq, NULL, MOSQ_TOPIC_CONTROL_LOCAL, 0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    if (StringCompare(msg->topic, MOSQ_TOPIC_CONTROL_LOCAL)) {
        char* payload = calloc((int)msg->payloadlen + 1, sizeof(char));
        strncpy(payload, msg->payload, msg->payloadlen);
        payload[(int)msg->payloadlen] = '\0';
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


void Aws_Init() {
    int returnStatus = EXIT_SUCCESS;
    networkContext.pParams = &opensslParams;
    returnStatus = initializeMqtt( &mqttContext, &networkContext );

    // Initialize the subscribe list
    g_awsSubscriptionCount = 1;
    g_awsSubscriptionList = malloc(sizeof(MQTTSubscribeInfo_t) * g_awsSubscriptionCount);

    // g_awsSubscriptionList[0].qos = MQTTQoS0;
    // g_awsSubscriptionList[0].pTopicFilter = AWS_TOPIC_SETTING;
    // g_awsSubscriptionList[0].topicFilterLength = strlen(g_awsSubscriptionList[0].pTopicFilter);

    char* notifyTopic = malloc(200);
    sprintf(notifyTopic, AWS_TOPIC_NOTIFY, g_thingId);
    g_awsSubscriptionList[0].qos = MQTTQoS0;
    g_awsSubscriptionList[0].pTopicFilter = notifyTopic;
    g_awsSubscriptionList[0].topicFilterLength = strlen(g_awsSubscriptionList[0].pTopicFilter);
    logInfo("Aws Topics to subscribe: %s", g_awsSubscriptionList[0].pTopicFilter);
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
            logError("MQTT_ProcessLoop returned with status = %s.", MQTT_Status_strerror(mqttStatus));
        }
    }
}

void Mosq_Init() {
    mosquitto_lib_init();
    mosq = mosquitto_new("HG_OTA", true, NULL);
    int rc = mosquitto_username_pw_set(mosq, "MqttLocalHomegy", "Homegysmart");
    if (rc != 0) {
        LogInfo((get_localtime_now()),("mosquitto_username_pw_set! Error Code: %d\n", rc));
        return;
    }
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_connect(mosq, MQTT_MOSQUITTO_HOST, MQTT_MOSQUITTO_PORT, MQTT_MOSQUITTO_KEEP_ALIVE);
    if (rc != 0) {
        logInfo("Client could not connect to broker! Error Code: %d\n", rc);
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

void CheckNewForceVersion() {
    // PlayAudio("version_checking");
    JSON* setting = Aws_GetShadow("agency2022", "Setting");
    if (setting) {
        JSON* hcforce = JSON_GetObject(setting, "hcVersion");
        if (hcforce) {
            int newVersion = JSON_GetNumber(hcforce, "forceVersion");
            int currentVersion = g_currentVersion;
            logInfo("currentVersion: %d, newVersion: %d\n", currentVersion, newVersion);
            if (currentVersion < newVersion) {
                // Update to new version
                PlayAudio("version_updating");
                char cmd[100];
                char result[1000];
                sprintf(cmd, "python3 /usr/bin/ota.pyc %d\n", newVersion);
                logInfo("Executing command: %s", cmd);
                FILE* fp = popen(cmd, "r");
                while (fgets(result, sizeof(result), fp) != NULL);
                fclose(fp);
                if (StringContains(result, "SUCCESS")) {
                    SaveCurrentVersion(newVersion);
                    logInfo("Upgraded to version %d\n", newVersion);
                    PlayAudio("update_success");
                    system("reboot");
                } else {
                    logInfo("[ERROR] Error to update to new version\n");
                    PlayAudio("update_error");
                }
            } else {
                logInfo("There is no new version to upgrade");
                // PlayAudio("no_new_version");
            }
        }
    }
}

void CheckNewVersion() {
    JSON* setting = Aws_GetShadow("agency2022", "Setting");
    if (setting) {
        JSON* hcforce = JSON_GetObject(setting, "hcVersion");
        if (hcforce) {
            int newVersion = JSON_GetNumber(hcforce, "lastestVersion");
            int currentVersion = g_currentVersion;
            logInfo("currentVersion: %d, newVersion: %d\n", currentVersion, newVersion);
            if (currentVersion < newVersion) {
                // Update to new version
                PlayAudio("version_updating");
                char cmd[100];
                char result[1000];
                sprintf(cmd, "python3 /usr/bin/ota.pyc %d\n", newVersion);
                logInfo("Executing command: %s", cmd);
                FILE* fp = popen(cmd, "r");
                while (fgets(result, sizeof(result), fp) != NULL);
                fclose(fp);
                if (StringContains(result, "SUCCESS")) {
                    SaveCurrentVersion(newVersion);
                    logInfo("Upgraded to version %d\n", newVersion);
                    PlayAudio("update_success");
                    system("reboot");
                } else {
                    logInfo("[ERROR] Error to update to new version\n");
                    PlayAudio("update_error");
                }
            } else {
                logInfo("There is no new version to upgrade");
                PlayAudio("no_new_version");
            }
        }
    }
}

void ForceUpdate() {
    // Update to new version
    PlayAudio("version_updating");
    char cmd[100];
    char result[1000];
    sprintf(cmd, "python3 /usr/bin/ota.pyc %d\n", 0);
    logInfo("Executing command: %s", cmd);
    FILE* fp = popen(cmd, "r");
    while (fgets(result, sizeof(result), fp) != NULL);
    fclose(fp);
    if (StringContains(result, "SUCCESS")) {
        logInfo("Upgraded to version 0\n");
        PlayAudio("update_success");
        system("reboot");
    } else {
        logInfo("[ERROR] Error to update to default version\n");
        PlayAudio("update_error");
    }
}


int main( int argc,char ** argv ) {
    queue_received_aws = newQueue(QUEUE_SIZE);
    GetCurrentVersion();
    GetThingId();
    if (argc > 1) {
        CheckNewForceVersion();
        return 0;
    }
    Aws_Init();

    while (1) {
        Aws_ProcessLoop();

        int size_queue = get_sizeQueue(queue_received_aws);
        if (size_queue > 0) {
            char* recvMsg = (char *)dequeue(queue_received_aws);
            JSON* recvPacket = JSON_Parse(recvMsg);
            free(recvMsg);
            if (recvPacket) {

                JSON* state = JSON_GetObject(recvPacket, "state");
                JSON* reported = JSON_GetObject(state, "reported");
                int reqType = JSON_GetNumber(reported, "type");
                int sender = JSON_GetNumber(reported, "sender");
                if (sender != SENDER_APP_VIA_LOCAL && sender != SENDER_APP_TO_CLOUD) {
                    continue;
                }
                if (reqType != TYPE_OTA_HC) {
                    continue;
                }

                switch(reqType) {
                    case TYPE_OTA_HC: {
                        int checkVersion = 1;
                        if (JSON_HasKey(reported, "checkVersion")) {
                            checkVersion = JSON_GetNumber(reported, "checkVersion");
                        }
                        if (checkVersion) {
                            logInfo("TYPE_OTA_HC");
                            PlayAudio("version_checking");
                            CheckNewVersion();
                        } else {
                            logInfo("TYPE_OTA_HC with default version");
                            ForceUpdate();
                            char* topic = Aws_GetTopic(PAGE_NONE, 0, TOPIC_NOTI_PUB);
                            Aws_PublishMessage(topic, "Updating...");
                            free(topic);
                        }
                        break;
                    }
                }
            }
        }

        usleep(100);
    }
    return 0;
}


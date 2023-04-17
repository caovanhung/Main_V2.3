#ifndef DEMO_CONFIG_H_
#define DEMO_CONFIG_H_


/* Include header that defines log levels. */
#include "logging_levels.h"

/* Logging configuration for the Demo. */
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "AWS"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif

#include "logging_stack.h"
#include "core_mqtt.h"

#define AWS_IOT_ENDPOINT            "a2376tec8bakos-ats.iot.ap-southeast-1.amazonaws.com"
#define ROOT_CA_CERT_PATH           "/usr/bin/AmazonRootCA1.pem"
#define CLIENT_CERT_PATH            "/usr/bin/c8f9a13dc7c253251b9e250439897bc010f501edd780348ecc1c2e91add22237-certificate.pem.crt"
#define CLIENT_PRIVATE_KEY_PATH     "/usr/bin/c8f9a13dc7c253251b9e250439897bc010f501edd780348ecc1c2e91add22237-private.pem.key"

#define MQTT_LIB    "core-mqtt@" MQTT_LIBRARY_VERSION


///////////////////////////////////////////////////////////
#ifndef AWS_MQTT_PORT
    #define AWS_MQTT_PORT    ( 8883 )
#endif

#ifndef NETWORK_BUFFER_SIZE
    #define NETWORK_BUFFER_SIZE    ( 1000000U )
#endif

#ifndef OS_NAME
    #define OS_NAME    "Ubuntu"
#endif

#ifndef OS_VERSION
    #define OS_VERSION    "18.04 LTS"
#endif

#ifndef HARDWARE_PLATFORM_NAME
    #define HARDWARE_PLATFORM_NAME    "Posix"
#endif

#ifndef CLIENT_IDENTIFIER
    #define CLIENT_IDENTIFIER    "HOMEGY_1"
#endif
#define AWS_IOT_ENDPOINT_LENGTH         ( ( uint16_t ) ( sizeof( AWS_IOT_ENDPOINT ) - 1 ) )
#define CLIENT_IDENTIFIER_LENGTH        ( ( uint16_t ) ( sizeof( CLIENT_IDENTIFIER ) - 1 ) )
#define AWS_IOT_MQTT_ALPN               "\x0ex-amzn-mqtt-ca"
#define AWS_IOT_MQTT_ALPN_LENGTH        ( ( uint16_t ) ( sizeof( AWS_IOT_MQTT_ALPN ) - 1 ) )
#define AWS_IOT_PASSWORD_ALPN           "\x04mqtt"
#define AWS_IOT_PASSWORD_ALPN_LENGTH    ( ( uint16_t ) ( sizeof( AWS_IOT_PASSWORD_ALPN ) - 1 ) )


/**
 * @brief The maximum number of retries for connecting to server.
 */
#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying connection to server.
 */
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U  )


/**
 * @brief The base back-off delay (in milliseconds) to use for connection retry attempts.
 */
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U  )


/**
 * @brief Timeout for receiving CONNACK packet in milli seconds.
 */
#define CONNACK_RECV_TIMEOUT_MS                  ( 1000U  )


/**
 * @brief Maximum number of outgoing publishes maintained in the application
 * until an ack is received from the broker.
 */
#define MAX_OUTGOING_PUBLISHES              (5U)

/**
 * @brief Invalid packet identifier for the MQTT packets. Zero is always an
 * invalid packet identifier as per MQTT 3.1.1 spec.
 */
#define MQTT_PACKET_ID_INVALID              ( ( uint16_t ) 0U )


/**
 * @brief Timeout for MQTT_ProcessLoop function in milliseconds.
 */
#define MQTT_PROCESS_LOOP_TIMEOUT_MS        ( 500U )


/**
 * @brief The maximum time interval in seconds which is allowed to elapse
 *  between two Control Packets.
 *
 *  It is the responsibility of the Client to ensure that the interval between
 *  Control Packets being sent does not exceed the this Keep Alive value. In the
 *  absence of sending any other Control Packets, the Client MUST send a
 *  PINGREQ Packet.
 */
#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS    ( 60U  )


/**
 * @brief Delay between MQTT publishes in miliseconds.
 */
#define DELAY_BETWEEN_PUBLISHES_MSECONDS    ( 10000U )


/**
 * @brief Number of PUBLISH messages sent per iteration.
 */
#define MQTT_PUBLISH_COUNT_PER_LOOP         ( 1U )


/**
 * @brief Delay in seconds between two iterations of subscribePublishLoop().
 */
#define MQTT_SUBPUB_LOOP_DELAY_MSECONDS     ( 10000U )


/**
 * @brief Transport timeout in milliseconds for transport send and receive.
 */
#define TRANSPORT_SEND_RECV_TIMEOUT_MS      ( 500 )


#define METRICS_STRING                      "HOMEGY"


#define METRICS_STRING_LENGTH               ( ( uint16_t ) ( sizeof( METRICS_STRING ) - 1 ) )


#ifdef CLIENT_USERNAME
    #define CLIENT_USERNAME_WITH_METRICS    CLIENT_USERNAME METRICS_STRING
#endif


#endif /* ifndef DEMO_CONFIG_H_ */






typedef enum ServiceStatus
{
    SERVICE_Success = 0,     /**< Function completed successfully. */
    // MQTTBadParameter,    /**< At least one parameter was invalid. */
    // MQTTNoMemory,        /**< A provided buffer was too small. */
    // MQTTSendFailed,      /**< The transport send function failed. */
    // MQTTRecvFailed,      /**< The transport receive function failed. */
    // MQTTBadResponse,     /**< An invalid packet was received from the server. */
    // MQTTServerRefused,   /**< The server refused a CONNECT or SUBSCRIBE. */
    // MQTTNoDataAvailable, /**< No data available from the transport interface. */
    // MQTTIllegalState,    /**< An illegal state in the state record. */
    // MQTTStateCollision,  /**< A collision with an existing state record entry. */
    // MQTTKeepAliveTimeout /**< Timeout while waiting for PINGRESP. */
} ServiceStatus_t;

// const char * MQTT_Status_strerror( MQTTStatus_t status )
// {
//     const char * str = NULL;

//     switch( status )
//     {
//         case MQTTSuccess:
//             str = "MQTTSuccess";
//             break;

//         case MQTTBadParameter:
//             str = "MQTTBadParameter";
//             break;

//         case MQTTNoMemory:
//             str = "MQTTNoMemory";
//             break;

//         case MQTTSendFailed:
//             str = "MQTTSendFailed";
//             break;

//         case MQTTRecvFailed:
//             str = "MQTTRecvFailed";
//             break;

//         case MQTTBadResponse:
//             str = "MQTTBadResponse";
//             break;

//         case MQTTServerRefused:
//             str = "MQTTServerRefused";
//             break;

//         case MQTTNoDataAvailable:
//             str = "MQTTNoDataAvailable";
//             break;

//         case MQTTIllegalState:
//             str = "MQTTIllegalState";
//             break;

//         case MQTTStateCollision:
//             str = "MQTTStateCollision";
//             break;

//         case MQTTKeepAliveTimeout:
//             str = "MQTTKeepAliveTimeout";
//             break;

//         default:
//             str = "Invalid MQTT Status code";
//             break;
//     }

//     return str;
// }
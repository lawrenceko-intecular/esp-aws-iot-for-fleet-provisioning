/*
 * AWS IoT Device SDK for Embedded C 202103.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file shadow_demo_main.c
 *
 * @brief Demo for showing how to use the Device Shadow library's API. This version
 * of Device Shadow API provide macros and helper functions for assembling MQTT topics
 * strings, and for determining whether an incoming MQTT message is related to a
 * device shadow. The shadow can be either the classic shadow or a named shadow. Change
 * #SHADOW_NAME to select the shadow. The Device Shadow library does not depend on a MQTT library,
 * therefore the code for MQTT connections are placed in another file (shadow_demo_helpers.c)
 * to make it easy to read the code using Device Shadow library.
 *
 * This example assumes there is a powerOn state in the device shadow. It does the
 * following operations:
 * 1. Establish a MQTT connection by using the helper functions in shadow_demo_helpers.c.
 * 2. Assemble strings for the MQTT topics of device shadow, by using macros defined by the Device Shadow library.
 * 3. Subscribe to those MQTT topics by using helper functions in shadow_demo_helpers.c.
 * 4. Publish a desired state of powerOn by using helper functions in shadow_demo_helpers.c.  That will cause
 * a delta message to be sent to device.
 * 5. Handle incoming MQTT messages in eventCallback, determine whether the message is related to the device
 * shadow by using a function defined by the Device Shadow library (Shadow_MatchTopicString). If the message is a
 * device shadow delta message, set a flag for the main function to know, then the main function will publish
 * a second message to update the reported state of powerOn.
 * 6. Handle incoming message again in eventCallback. If the message is from update/accepted, verify that it
 * has the same clientToken as previously published in the update message. That will mark the end of the demo.
 */

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* POSIX includes. */
#include <unistd.h>

/* Shadow config include. */
#include "shadow_config.h"

/* SHADOW API header. */
#include "shadow.h"

/* JSON API header. */
#include "core_json.h"

/* Clock for timer. */
#include "clock.h"

/* shadow demo helpers header. */
#include "shadow_demo_helpers.h"

/* AWS IoT Fleet Provisioning Library. */
#include "fleet_provisioning.h"

/**
 * @brief The length of #PROVISIONING_TEMPLATE_NAME.
 */
#define PROVISIONING_TEMPLATE_NAME_LENGTH    ( ( uint16_t ) ( sizeof( PROVISIONING_TEMPLATE_NAME ) - 1 ) )

/**
 * @brief The length of #DEVICE_SERIAL_NUMBER.
 */
#define DEVICE_SERIAL_NUMBER_LENGTH          ( ( uint16_t ) ( sizeof( DEVICE_SERIAL_NUMBER ) - 1 ) )

/**
 * @brief Size of AWS IoT Thing name buffer.
 *
 * See https://docs.aws.amazon.com/iot/latest/apireference/API_CreateThing.html#iot-CreateThing-request-thingName
 */
#define MAX_THING_NAME_LENGTH                128

/**
 * @brief The maximum number of times to run the loop in this demo.
 *
 * @note The demo loop is attempted to re-run only if it fails in an iteration.
 * Once the demo loop succeeds in an iteration, the demo exits successfully.
 */
#ifndef FLEET_PROV_MAX_DEMO_LOOP_COUNT
    #define FLEET_PROV_MAX_DEMO_LOOP_COUNT    ( 3 )
#endif

/**
 * @brief Time in seconds to wait between retries of the demo loop if
 * demo loop fails.
 */
#define DELAY_BETWEEN_DEMO_RETRY_ITERATIONS_SECONDS    ( 5 )

/**
 * @brief Size of buffer in which to hold the certificate signing request (CSR).
 */
#define CSR_BUFFER_LENGTH                              2048

/**
 * @brief Size of buffer in which to hold the certificate.
 */
#define CERT_BUFFER_LENGTH                             2048

/**
 * @brief Size of buffer in which to hold the certificate id.
 *
 * See https://docs.aws.amazon.com/iot/latest/apireference/API_Certificate.html#iot-Type-Certificate-certificateId
 */
#define CERT_ID_BUFFER_LENGTH                          64

/**
 * @brief Size of buffer in which to hold the certificate ownership token.
 */
#define OWNERSHIP_TOKEN_BUFFER_LENGTH                  512

/**
 * @brief Status values of the Fleet Provisioning response.
 */
typedef enum
{
    ResponseNotReceived,
    ResponseAccepted,
    ResponseRejected
} ResponseStatus_t;

/*-----------------------------------------------------------*/

/**
 * @brief Status reported from the MQTT publish callback.
 */
static ResponseStatus_t responseStatus;

/**
 * @brief Buffer to hold the provisioned AWS IoT Thing name.
 */
static char thingName[ MAX_THING_NAME_LENGTH ];

/**
 * @brief Length of the AWS IoT Thing name.
 */
static size_t thingNameLength;

/**
 * @brief Buffer to hold responses received from the AWS IoT Fleet Provisioning
 * APIs. When the MQTT publish callback receives an expected Fleet Provisioning
 * accepted payload, it copies it into this buffer.
 */
static uint8_t payloadBuffer[ NETWORK_BUFFER_SIZE ];

/**
 * @brief Length of the payload stored in #payloadBuffer. This is set by the
 * MQTT publish callback when it copies a received payload into #payloadBuffer.
 */
static size_t payloadLength;

/*-----------------------------------------------------------*/

/**
 * @brief Format string representing a Shadow document with a "desired" state.
 *
 * The real json document will look like this:
 * {
 *   "state": {
 *     "desired": {
 *       "powerOn": 1
 *     }
 *   },
 *   "clientToken": "021909"
 * }
 *
 * Note the client token, which is optional for all Shadow updates. The client
 * token must be unique at any given time, but may be reused once the update is
 * completed. For this demo, a timestamp is used for a client token.
 */
#define SHADOW_DESIRED_JSON     \
    "{"                         \
    "\"state\":{"               \
    "\"desired\":{"             \
    "\"powerOn\":%01d"          \
    "}"                         \
    "},"                        \
    "\"clientToken\":\"%06lu\"" \
    "}"

/**
 * @brief The expected size of #SHADOW_DESIRED_JSON.
 *
 * Because all the format specifiers in #SHADOW_DESIRED_JSON include a length,
 * its full actual size is known by pre-calculation, here's the formula why
 * the length need to minus 3:
 * 1. The length of "%01d" is 4.
 * 2. The length of %06lu is 5.
 * 3. The actual length we will use in case 1. is 1 ( for the state of powerOn ).
 * 4. The actual length we will use in case 2. is 6 ( for the clientToken length ).
 * 5. Thus the additional size 3 = 4 + 5 - 1 - 6 + 1 (termination character).
 *
 * In your own application, you could calculate the size of the json doc in this way.
 */
#define SHADOW_DESIRED_JSON_LENGTH    ( sizeof( SHADOW_DESIRED_JSON ) - 3 )

/**
 * @brief Format string representing a Shadow document with a "reported" state.
 *
 * The real json document will look like this:
 * {
 *   "state": {
 *     "reported": {
 *       "powerOn": 1
 *     }
 *   },
 *   "clientToken": "021909"
 * }
 *
 * Note the client token, which is required for all Shadow updates. The client
 * token must be unique at any given time, but may be reused once the update is
 * completed. For this demo, a timestamp is used for a client token.
 */
#define SHADOW_REPORTED_JSON    \
    "{"                         \
    "\"state\":{"               \
    "\"reported\":{"            \
    "\"powerOn\":%01d"          \
    "}"                         \
    "},"                        \
    "\"clientToken\":\"%06lu\"" \
    "}"

/**
 * @brief The expected size of #SHADOW_REPORTED_JSON.
 *
 * Because all the format specifiers in #SHADOW_REPORTED_JSON include a length,
 * its full size is known at compile-time by pre-calculation. Users could refer to
 * the way how to calculate the actual length in #SHADOW_DESIRED_JSON_LENGTH.
 */
#define SHADOW_REPORTED_JSON_LENGTH    ( sizeof( SHADOW_REPORTED_JSON ) - 3 )

/**
 * @brief The maximum number of times to run the loop in this demo.
 *
 * @note The demo loop is attempted to re-run only if it fails in an iteration.
 * Once the demo loop succeeds in an iteration, the demo exits successfully.
 */
#ifndef SHADOW_MAX_DEMO_LOOP_COUNT
    #define SHADOW_MAX_DEMO_LOOP_COUNT    ( 3 )
#endif

/**
 * @brief Time in seconds to wait between retries of the demo loop if
 * demo loop fails.
 */
#define DELAY_BETWEEN_DEMO_RETRY_ITERATIONS_S           ( 5 )

/**
 * @brief JSON key for response code that indicates the type of error in
 * the error document received on topic `/delete/rejected`.
 */
#define SHADOW_DELETE_REJECTED_ERROR_CODE_KEY           "code"

/**
 * @brief Length of #SHADOW_DELETE_REJECTED_ERROR_CODE_KEY
 */
#define SHADOW_DELETE_REJECTED_ERROR_CODE_KEY_LENGTH    ( ( uint16_t ) ( sizeof( SHADOW_DELETE_REJECTED_ERROR_CODE_KEY ) - 1 ) )

/*-----------------------------------------------------------*/

/**
 * @brief The simulated device current power on state.
 */
static uint32_t currentPowerOnState = 0;

/**
 * @brief The flag to indicate the device current power on state changed.
 */
static bool stateChanged = false;

/**
 * @brief When we send an update to the device shadow, and if we care about
 * the response from cloud (accepted/rejected), remember the clientToken and
 * use it to match with the response.
 */
static uint32_t clientToken = 0U;

/**
 * @brief Indicator that an error occurred during the MQTT event callback. If an
 * error occurred during the MQTT event callback, then the demo has failed.
 */
static bool eventCallbackError = false;

/**
 * @brief Status of the response of Shadow delete operation from AWS IoT
 * message broker.
 */
static bool deleteResponseReceived = false;

/**
 * @brief Status of the Shadow delete operation.
 *
 * The Shadow delete status will be updated by the incoming publishes on the
 * MQTT topics for delete acknowledgement from AWS IoT message broker
 * (accepted/rejected). Shadow document is considered to be deleted if an
 * incoming publish is received on `/delete/accepted` topic or an incoming
 * publish is received on `/delete/rejected` topic with error code 404. Code 404
 * indicates that the Shadow document does not exist for the Thing yet.
 */
static bool shadowDeleted = false;

/*-----------------------------------------------------------*/

/**
 * @brief Run the MQTT process loop to get a response.
 */
static int32_t waitForResponse( void );

/**
 * @brief Subscribe to the CreateKeysAndCertificate accepted and rejected topics.
 */
static int32_t subscribeToKeyCertificateResponseTopics( void );

/**
 * @brief Subscribe to the RegisterThing accepted and rejected topics.
 */
static int32_t subscribeToRegisterThingResponseTopics( void );

/**
 * @brief This example uses the MQTT library of the AWS IoT Device SDK for
 * Embedded C. This is the prototype of the callback function defined by
 * that library. It will be invoked whenever the MQTT library receives an
 * incoming message.
 *
 * @param[in] pMqttContext MQTT context pointer.
 * @param[in] pPacketInfo Packet Info pointer for the incoming packet.
 * @param[in] pDeserializedInfo Deserialized information from the incoming packet.
 */
static void eventCallback( MQTTContext_t * pMqttContext,
                           MQTTPacketInfo_t * pPacketInfo,
                           MQTTDeserializedInfo_t * pDeserializedInfo );

/**
 * @brief This example uses the MQTT library of the AWS IoT Device SDK for
 * Embedded C. This is the prototype of the callback function defined by
 * that library. It will be invoked whenever the MQTT library receives an
 * incoming message.
 *
 * @param[in] pMqttContext MQTT context pointer.
 * @param[in] pPacketInfo Packet Info pointer for the incoming packet.
 * @param[in] pDeserializedInfo Deserialized information from the incoming packet.
 */
static void provisioningPublishCallback( MQTTContext_t * pMqttContext,
                                         MQTTPublishInfo_t * pPublishInfo,
                                         MQTTDeserializedInfo_t * pDeserializedInfo );

/**
 * @brief Process payload from /update/delta topic.
 *
 * This handler examines the version number and the powerOn state. If powerOn
 * state has changed, it sets a flag for the main function to take further actions.
 *
 * @param[in] pPublishInfo Deserialized publish info pointer for the incoming
 * packet.
 */
static void updateDeltaHandler( MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Process payload from /update/accepted topic.
 *
 * This handler examines the accepted message that carries the same clientToken
 * as sent before.
 *
 * @param[in] pPublishInfo Deserialized publish info pointer for the incoming
 * packet.
 */
static void updateAcceptedHandler( MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Process payload from `/delete/rejected` topic.
 *
 * This handler examines the rejected message to look for the reject reason code.
 * If the reject reason code is `404`, an attempt was made to delete a shadow
 * document which was not present yet. This is considered to be success for this
 * demo application.
 *
 * @param[in] pPublishInfo Deserialized publish info pointer for the incoming
 * packet.
 */
static void deleteRejectedHandler( MQTTPublishInfo_t * pPublishInfo );

/*-----------------------------------------------------------*/

static void deleteRejectedHandler( MQTTPublishInfo_t * pPublishInfo )
{
    JSONStatus_t result = JSONSuccess;
    char * pOutValue = NULL;
    uint32_t outValueLength = 0U;
    long errorCode = 0L;

    assert( pPublishInfo != NULL );
    assert( pPublishInfo->pPayload != NULL );

    LogInfo( ( "/delete/rejected json payload:%s.", ( const char * ) pPublishInfo->pPayload ) );

    /* The payload will look similar to this:
     * {
     *    "code": error-code,
     *    "message": "error-message",
     *    "timestamp": timestamp,
     *    "clientToken": "token"
     * }
     */

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate( ( const char * ) pPublishInfo->pPayload,
                            pPublishInfo->payloadLength );

    if( result == JSONSuccess )
    {
        /* Then we start to get the version value by JSON keyword "version". */
        result = JSON_Search( ( char * ) pPublishInfo->pPayload,
                              pPublishInfo->payloadLength,
                              SHADOW_DELETE_REJECTED_ERROR_CODE_KEY,
                              SHADOW_DELETE_REJECTED_ERROR_CODE_KEY_LENGTH,
                              &pOutValue,
                              ( size_t * ) &outValueLength );
    }
    else
    {
        LogError( ( "The json document is invalid!!" ) );
    }

    if( result == JSONSuccess )
    {
        LogInfo( ( "Error code is: %.*s.",
                   outValueLength,
                   pOutValue ) );

        /* Convert the extracted value to an unsigned integer value. */
        errorCode = strtoul( pOutValue, NULL, 10 );
    }
    else
    {
        LogError( ( "No error code in json document!!" ) );
    }

    LogInfo( ( "Error code:%ld.", errorCode ) );

    /* Mark Shadow delete operation as a success if error code is 404. */
    if( errorCode == 404UL )
    {
        shadowDeleted = true;
    }
}

/*-----------------------------------------------------------*/

static void updateDeltaHandler( MQTTPublishInfo_t * pPublishInfo )
{
    static uint32_t currentVersion = 0; /* Remember the latestVersion # we've ever received */
    uint32_t version = 0U;
    uint32_t newState = 0U;
    char * outValue = NULL;
    uint32_t outValueLength = 0U;
    JSONStatus_t result = JSONSuccess;

    assert( pPublishInfo != NULL );
    assert( pPublishInfo->pPayload != NULL );

    LogInfo( ( "/update/delta json payload:%s.", ( const char * ) pPublishInfo->pPayload ) );

    /* The payload will look similar to this:
     * {
     *      "version": 12,
     *      "timestamp": 1595437367,
     *      "state": {
     *          "powerOn": 1
     *      },
     *      "metadata": {
     *          "powerOn": {
     *          "timestamp": 1595437367
     *          }
     *      },
     *      "clientToken": "388062"
     *  }
     */

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate( ( const char * ) pPublishInfo->pPayload,
                            pPublishInfo->payloadLength );

    if( result == JSONSuccess )
    {
        /* Then we start to get the version value by JSON keyword "version". */
        result = JSON_Search( ( char * ) pPublishInfo->pPayload,
                              pPublishInfo->payloadLength,
                              "version",
                              sizeof( "version" ) - 1,
                              &outValue,
                              ( size_t * ) &outValueLength );
    }
    else
    {
        LogError( ( "The json document is invalid!!" ) );
        eventCallbackError = true;
    }

    if( result == JSONSuccess )
    {
        LogInfo( ( "version: %.*s",
                   outValueLength,
                   outValue ) );

        /* Convert the extracted value to an unsigned integer value. */
        version = ( uint32_t ) strtoul( outValue, NULL, 10 );
    }
    else
    {
        LogError( ( "No version in json document!!" ) );
        eventCallbackError = true;
    }

    LogInfo( ( "version:%d, currentVersion:%d \r\n", version, currentVersion ) );

    /* When the version is much newer than the on we retained, that means the powerOn
     * state is valid for us. */
    if( version > currentVersion )
    {
        /* Set to received version as the current version. */
        currentVersion = version;

        /* Get powerOn state from json documents. */
        result = JSON_Search( ( char * ) pPublishInfo->pPayload,
                              pPublishInfo->payloadLength,
                              "state.powerOn",
                              sizeof( "state.powerOn" ) - 1,
                              &outValue,
                              ( size_t * ) &outValueLength );
    }
    else
    {
        /* In this demo, we discard the incoming message
         * if the version number is not newer than the latest
         * that we've received before. Your application may use a
         * different approach.
         */
        LogWarn( ( "The received version is smaller than current one!!" ) );
    }

    if( result == JSONSuccess )
    {
        /* Convert the powerOn state value to an unsigned integer value. */
        newState = ( uint32_t ) strtoul( outValue, NULL, 10 );

        LogInfo( ( "The new power on state newState:%d, currentPowerOnState:%d \r\n",
                   newState, currentPowerOnState ) );

        if( newState != currentPowerOnState )
        {
            /* The received powerOn state is different from the one we retained before, so we switch them
             * and set the flag. */
            currentPowerOnState = newState;

            /* State change will be handled in main(), where we will publish a "reported"
             * state to the device shadow. We do not do it here because we are inside of
             * a callback from the MQTT library, so that we don't re-enter
             * the MQTT library. */
            stateChanged = true;
        }
    }
    else
    {
        LogError( ( "No powerOn in json document!!" ) );
        eventCallbackError = true;
    }
}

/*-----------------------------------------------------------*/

static void updateAcceptedHandler( MQTTPublishInfo_t * pPublishInfo )
{
    char * outValue = NULL;
    uint32_t outValueLength = 0U;
    uint32_t receivedToken = 0U;
    JSONStatus_t result = JSONSuccess;

    assert( pPublishInfo != NULL );
    assert( pPublishInfo->pPayload != NULL );

    LogInfo( ( "/update/accepted json payload:%s.", ( const char * ) pPublishInfo->pPayload ) );

    /* Handle the reported state with state change in /update/accepted topic.
     * Thus we will retrieve the client token from the json document to see if
     * it's the same one we sent with reported state on the /update topic.
     * The payload will look similar to this:
     *  {
     *      "state": {
     *          "reported": {
     *          "powerOn": 1
     *          }
     *      },
     *      "metadata": {
     *          "reported": {
     *          "powerOn": {
     *              "timestamp": 1596573647
     *          }
     *          }
     *      },
     *      "version": 14698,
     *      "timestamp": 1596573647,
     *      "clientToken": "022485"
     *  }
     */

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate( ( const char * ) pPublishInfo->pPayload,
                            pPublishInfo->payloadLength );

    if( result == JSONSuccess )
    {
        /* Get clientToken from json documents. */
        result = JSON_Search( ( char * ) pPublishInfo->pPayload,
                              pPublishInfo->payloadLength,
                              "clientToken",
                              sizeof( "clientToken" ) - 1,
                              &outValue,
                              ( size_t * ) &outValueLength );
    }
    else
    {
        LogError( ( "Invalid json documents !!" ) );
        eventCallbackError = true;
    }

    if( result == JSONSuccess )
    {
        LogInfo( ( "clientToken: %.*s", outValueLength,
                   outValue ) );

        /* Convert the code to an unsigned integer value. */
        receivedToken = ( uint32_t ) strtoul( outValue, NULL, 10 );

        LogInfo( ( "receivedToken:%d, clientToken:%u \r\n", receivedToken, clientToken ) );

        /* If the clientToken in this update/accepted message matches the one we
         * published before, it means the device shadow has accepted our latest
         * reported state. We are done. */
        if( receivedToken == clientToken )
        {
            LogInfo( ( "Received response from the device shadow. Previously published "
                       "update with clientToken=%u has been accepted. ", clientToken ) );
        }
        else
        {
            LogWarn( ( "The received clientToken=%u is not identical with the one=%u we sent "
                       , receivedToken, clientToken ) );
        }
    }
    else
    {
        LogError( ( "No clientToken in json document!!" ) );
        eventCallbackError = true;
    }
}

/*-----------------------------------------------------------*/

/* This is the callback function invoked by the MQTT stack when it receives
 * incoming messages. This function demonstrates how to use the Shadow_MatchTopicString
 * function to determine whether the incoming message is a device shadow message
 * or not. If it is, it handles the message depending on the message type.
 */
static void eventCallback( MQTTContext_t * pMqttContext,
                           MQTTPacketInfo_t * pPacketInfo,
                           MQTTDeserializedInfo_t * pDeserializedInfo )
{
    ShadowMessageType_t messageType = ShadowMessageTypeMaxNum;
    const char * pThingName = NULL;
    uint8_t thingNameLength = 0U;
    const char * pShadowName = NULL;
    uint8_t shadowNameLength = 0U;
    uint16_t packetIdentifier;

    ( void ) pMqttContext;

    assert( pDeserializedInfo != NULL );
    assert( pMqttContext != NULL );
    assert( pPacketInfo != NULL );

    packetIdentifier = pDeserializedInfo->packetIdentifier;

    /* Handle incoming publish. The lower 4 bits of the publish packet
     * type is used for the dup, QoS, and retain flags. Hence masking
     * out the lower bits to check if the packet is publish. */
    if( ( pPacketInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        assert( pDeserializedInfo->pPublishInfo != NULL );
        LogInfo( ( "pPublishInfo->pTopicName:%s.", pDeserializedInfo->pPublishInfo->pTopicName ) );

        /* Let the Device Shadow library tell us whether this is a device shadow message. */
        if( SHADOW_SUCCESS == Shadow_MatchTopicString( pDeserializedInfo->pPublishInfo->pTopicName,
                                                       pDeserializedInfo->pPublishInfo->topicNameLength,
                                                       &messageType,
                                                       &pThingName,
                                                       &thingNameLength,
                                                       &pShadowName,
                                                       &shadowNameLength ) )
        {
            /* Upon successful return, the messageType has been filled in. */
            if( messageType == ShadowMessageTypeUpdateDelta )
            {
                /* Handler function to process payload. */
                updateDeltaHandler( pDeserializedInfo->pPublishInfo );
            }
            else if( messageType == ShadowMessageTypeUpdateAccepted )
            {
                /* Handler function to process payload. */
                updateAcceptedHandler( pDeserializedInfo->pPublishInfo );
            }
            else if( messageType == ShadowMessageTypeUpdateDocuments )
            {
                LogInfo( ( "/update/documents json payload:%s.", ( const char * ) pDeserializedInfo->pPublishInfo->pPayload ) );
            }
            else if( messageType == ShadowMessageTypeUpdateRejected )
            {
                LogInfo( ( "/update/rejected json payload:%s.", ( const char * ) pDeserializedInfo->pPublishInfo->pPayload ) );
            }
            else if( messageType == ShadowMessageTypeDeleteAccepted )
            {
                LogInfo( ( "Received an MQTT incoming publish on /delete/accepted topic." ) );
                shadowDeleted = true;
                deleteResponseReceived = true;
            }
            else if( messageType == ShadowMessageTypeDeleteRejected )
            {
                /* Handler function to process payload. */
                deleteRejectedHandler( pDeserializedInfo->pPublishInfo );
                deleteResponseReceived = true;
            }
            else
            {
                LogInfo( ( "Other message type:%d !!", messageType ) );
            }
        }
        else
        {
            LogError( ( "Shadow_MatchTopicString parse failed:%s !!", ( const char * ) pDeserializedInfo->pPublishInfo->pTopicName ) );
            eventCallbackError = true;
        }
    }
    else
    {
        HandleOtherIncomingPacket( pPacketInfo, packetIdentifier );
    }
}

/*-----------------------------------------------------------*/

static int32_t waitForResponse( void )
{
    int returnStatus = EXIT_SUCCESS;

    responseStatus = ResponseNotReceived;

    /* responseStatus is updated from the MQTT publish callback. */
    ( void ) ProcessLoop();

    if( responseStatus == ResponseNotReceived )
    {
        LogError( ( "Timed out waiting for response." ) );
    }

    if( responseStatus == ResponseAccepted )
    {
        returnStatus = EXIT_SUCCESS;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t subscribeToKeyCertificateResponseTopics( void )
{
    int returnStatus = EXIT_SUCCESS;

    returnStatus = SubscribeToTopic( FP_JSON_CREATE_KEYS_ACCEPTED_TOPIC,
                                     FP_JSON_CREATE_KEYS_ACCEPTED_LENGTH );

    if( returnStatus != EXIT_SUCCESS )
    {
        LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                    FP_JSON_CREATE_KEYS_ACCEPTED_LENGTH,
                    FP_JSON_CREATE_KEYS_ACCEPTED_TOPIC ) );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus = SubscribeToTopic( FP_JSON_CREATE_KEYS_REJECTED_TOPIC,
                                   FP_JSON_CREATE_KEYS_REJECTED_LENGTH );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                        FP_JSON_CREATE_KEYS_REJECTED_LENGTH,
                        FP_JSON_CREATE_KEYS_REJECTED_TOPIC ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t subscribeToRegisterThingResponseTopics( void )
{
    int returnStatus = EXIT_SUCCESS;

    returnStatus = SubscribeToTopic( FP_JSON_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                     FP_JSON_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

    if( returnStatus != EXIT_SUCCESS )
    {
        LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                    FP_JSON_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                    FP_JSON_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus = SubscribeToTopic( FP_JSON_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                   FP_JSON_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                        FP_JSON_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                        FP_JSON_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

/* This is the callback function invoked by the MQTT stack when it receives
 * incoming messages. This function demonstrates how to use the Shadow_MatchTopicString
 * function to determine whether the incoming message is a device shadow message
 * or not. If it is, it handles the message depending on the message type.
 */
static void provisioningPublishCallback( MQTTContext_t * pMqttContext,
                           MQTTPublishInfo_t * pPublishInfo,
                           MQTTDeserializedInfo_t * pDeserializedInfo )
{
    FleetProvisioningStatus_t status;
    FleetProvisioningTopic_t api;
    const char * jsonDump;

    status = FleetProvisioning_MatchTopic( pPublishInfo->pTopicName,
                                           pPublishInfo->topicNameLength, &api );

    if( status != FleetProvisioningSuccess )
    {
        LogWarn( ( "Unexpected publish message received. Topic: %.*s.",
                   ( int ) pPublishInfo->topicNameLength,
                   ( const char * ) pPublishInfo->pTopicName ) );
    }
    else
    {
        if( api == FleetProvJsonCreateKeysAndCertAccepted )
        {
            LogInfo( ( "Received accepted response from Fleet Provisioning CreateKeysAndCertificate API." ) );
            
            jsonDump = pPublishInfo->pPayload;
            LogInfo( ( "Payload: %s", jsonDump ) );
            free( ( void * ) jsonDump );

            responseStatus = ResponseAccepted;

            /* Copy the payload from the MQTT library's buffer to #payloadBuffer. */
            ( void ) memcpy( ( void * ) payloadBuffer,
                             ( const void * ) pPublishInfo->pPayload,
                             ( size_t ) pPublishInfo->payloadLength );

            payloadLength = pPublishInfo->payloadLength;
        }
        else if( api == FleetProvJsonCreateKeysAndCertRejected )
        {
            LogError( ( "Received rejected response from Fleet Provisioning CreateKeysAndCertificate API." ) );
            
            jsonDump = pPublishInfo->pPayload;
            LogError( ( "Payload: %s", jsonDump ) );
            free( ( void * ) jsonDump );

            responseStatus = ResponseRejected;
        }
        else if( api == FleetProvJsonRegisterThingAccepted )
        {
            LogInfo( ( "Received accepted response from Fleet Provisioning RegisterThing API." ) );

            jsonDump = pPublishInfo->pPayload;
            LogInfo( ( "Payload: %s", jsonDump ) );
            free( ( void * ) jsonDump );

            responseStatus = ResponseAccepted;

            /* Copy the payload from the MQTT library's buffer to #payloadBuffer. */
            ( void ) memcpy( ( void * ) payloadBuffer,
                             ( const void * ) pPublishInfo->pPayload,
                             ( size_t ) pPublishInfo->payloadLength );

            payloadLength = pPublishInfo->payloadLength;
        }
        else if( api == FleetProvJsonRegisterThingRejected )
        {
            LogError( ( "Received rejected response from Fleet Provisioning RegisterThing API." ) );

            jsonDump = pPublishInfo->pPayload;
            LogError( ( "Payload: %s", jsonDump ) );
            free( ( void * ) jsonDump );

            responseStatus = ResponseRejected;
        }
        else
        {
            LogError( ( "Received message on unexpected Fleet Provisioning topic. Topic: %.*s.",
                        ( int ) pPublishInfo->topicNameLength,
                        ( const char * ) pPublishInfo->pTopicName ) );
        }
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Entry point of shadow demo.
 */
int aws_iot_demo_main( int argc,
          char ** argv )
{
    int returnStatus = EXIT_SUCCESS;
    /* Buffer for holding received certificate until it is saved. */
    char certificate[ CERT_BUFFER_LENGTH ];
    size_t certificateLength;
    /* Buffer for holding the certificate ID. */
    char certificateId[ CERT_ID_BUFFER_LENGTH ];
    size_t certificateIdLength;
    /* Buffer for holding the certificate ownership token. */
    char ownershipToken[ OWNERSHIP_TOKEN_BUFFER_LENGTH ];
    size_t ownershipTokenLength;
    bool connectionEstablished = false;

    /* Silence compiler warnings about unused variables. */
    ( void ) argc;
    ( void ) argv;

    do
    {
        /* Initialize the buffer lengths to their max lengths. */
        certificateLength = CERT_BUFFER_LENGTH;
        certificateIdLength = CERT_ID_BUFFER_LENGTH;
        ownershipTokenLength = OWNERSHIP_TOKEN_BUFFER_LENGTH;

        // TODO: Initialize the PKCS #11 module

        /**** Connect to AWS IoT Core with provisioning claim credentials *****/

        /* Attempts to connect to the AWS IoT MQTT broker. If the
         * connection fails, retries after a timeout. Timeout value will
         * exponentially increase until maximum attempts are reached. */
        LogInfo( ( "Establishing MQTT session with claim certificate..." ) );
        returnStatus = EstablishMqttSession( provisioningPublishCallback );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to establish MQTT session." ) );
        }
        else
        {
            LogInfo( ( "Established connection with claim credentials." ) );
            connectionEstablished = true;
        }

        /**** Call the CreateKeysAndCertificate API ***************************/

        /* We use the CreateKeysAndCertificate API to obtain a client certificate. */
        if( returnStatus == EXIT_SUCCESS )
        {
            /* Subscribe to the CreateKeysAndCertificate accepted and rejected
             * topics. In this demo we use JSON encoding for the payloads,
             * so we use the JSON variants of the topics. */
            returnStatus = subscribeToKeyCertificateResponseTopics();
        }

        if ( returnStatus == EXIT_SUCCESS )
        {
            /* Subscribe to the RegisterThing response topics. */
            returnStatus = subscribeToRegisterThingResponseTopics();
        }
        

        // Note: Skipped create a new key and CSR.

        // Note: Skipped generateCsrRequest()

        if ( returnStatus == EXIT_SUCCESS )
        {
            /* Publish to the CreateKeysAndCertificate API. */
            returnStatus = PublishToTopic( FP_JSON_CREATE_KEYS_PUBLISH_TOPIC,
                            FP_JSON_CREATE_KEYS_PUBLISH_LENGTH,
                            ( char * ) payloadBuffer,
                            payloadLength );

            if( returnStatus == EXIT_FAILURE )
            {
                LogError( ( "Failed to publish to fleet provisioning topic: %.*s.",
                            FP_JSON_CREATE_KEYS_PUBLISH_LENGTH,
                            FP_JSON_CREATE_KEYS_PUBLISH_TOPIC ) );
            }
        }

        if ( returnStatus == EXIT_SUCCESS )
        {
            /* Get the response to the CreateKeysAndCertificate request. */
            returnStatus = waitForResponse();
        }
        
        // if( status == true )
        // {
        //     /* From the response, extract the certificate, certificate ID, and
        //      * certificate ownership token. */
        //     status = parseKeyCertResponse( payloadBuffer,
        //                                 payloadLength,
        //                                 certificate,
        //                                 &certificateLength,
        //                                 certificateId,
        //                                 &certificateIdLength,
        //                                 ownershipToken,
        //                                 &ownershipTokenLength );

        //     if( status == true )
        //     {
        //         LogInfo( ( "Received certificate with Id: %.*s", ( int ) certificateIdLength, certificateId ) );
        //     }
        // }
        
        

        if( returnStatus == EXIT_FAILURE )
        {
            /* Log error to indicate connection failure. */
            LogError( ( "Failed to connect to MQTT broker." ) );
        }
        else
        {
            

            /* The MQTT session is always disconnected, even there were prior failures. */
            returnStatus = DisconnectMqttSession();
        }

        /* This demo performs only Device Shadow operations. If matching the Shadow
         * topic fails or there are failures in parsing the received JSON document,
         * then this demo was not successful. */
        if( eventCallbackError == true )
        {
            returnStatus = EXIT_FAILURE;
        }

        
    } while( returnStatus != EXIT_SUCCESS );

    if( returnStatus == EXIT_SUCCESS )
    {
        /* Log message indicating the demo completed successfully. */
        LogInfo( ( "Demo completed successfully." ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/
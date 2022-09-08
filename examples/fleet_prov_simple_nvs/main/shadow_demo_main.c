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

/* Clock for timer. */
#include "clock.h"

/* Demo includes. */
#include "shadow_demo_helpers.h"
#include "fleet_provisioning_serializer.h"

/* AWS IoT Fleet Provisioning Library. */
#include "fleet_provisioning.h"

/* Shadow config include. */
#include "shadow_config.h"

/* SHADOW API header. */
#include "shadow.h"

/* JSON API header. */
#include "core_json.h"

/* NVS header. */
#include "nvs.h"
#include "nvs_flash.h"

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
 * @brief Size of buffer in which to hold the private key.
 */
#define PRIV_KEY_BUFFER_LENGTH                             2048

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
static void provisioningPublishCallback(    MQTTContext_t * pMqttContext,
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
static void eventCallback( MQTTContext_t * pMqttContext,
                           MQTTPacketInfo_t * pPacketInfo,
                           MQTTDeserializedInfo_t * pDeserializedInfo );

/**
 * @brief Subscribe to the CreateKeysAndCertificate accepted and rejected topics.
 */
static int32_t subscribeToKeyCertificateResponseTopics( void );

/**
 * @brief Unsubscribe from the CreateKeysAndCertificate accepted and rejected topics.
 */
static int32_t unsubscribeFromKeyCertificateResponseTopics( void );

/**
 * @brief Unsubscribe from the RegisterThing accepted and rejected topics.
 */
static int32_t unsubscribeFromRegisterThingResponseTopics( void );

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

    returnStatus = SubscribeToTopic( FP_CBOR_CREATE_KEYS_ACCEPTED_TOPIC,
                                     FP_CBOR_CREATE_KEYS_ACCEPTED_LENGTH );

    if( returnStatus != EXIT_SUCCESS )
    {
        LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                    FP_CBOR_CREATE_KEYS_ACCEPTED_LENGTH,
                    FP_CBOR_CREATE_KEYS_ACCEPTED_TOPIC ) );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus = SubscribeToTopic( FP_CBOR_CREATE_KEYS_REJECTED_TOPIC,
                                   FP_CBOR_CREATE_KEYS_REJECTED_LENGTH );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                        FP_CBOR_CREATE_KEYS_REJECTED_LENGTH,
                        FP_CBOR_CREATE_KEYS_REJECTED_TOPIC ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t unsubscribeFromKeyCertificateResponseTopics( void )
{
    int returnStatus = EXIT_SUCCESS;

    returnStatus = UnsubscribeFromTopic(  FP_CBOR_CREATE_KEYS_ACCEPTED_TOPIC,
                                        FP_CBOR_CREATE_KEYS_ACCEPTED_LENGTH );

    if( returnStatus != EXIT_SUCCESS )
    {
        LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                    FP_CBOR_CREATE_KEYS_ACCEPTED_LENGTH,
                    FP_CBOR_CREATE_KEYS_ACCEPTED_TOPIC ) );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus = UnsubscribeFromTopic(  FP_CBOR_CREATE_KEYS_REJECTED_TOPIC,
                                            FP_CBOR_CREATE_KEYS_REJECTED_LENGTH );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                        FP_CBOR_CREATE_KEYS_REJECTED_LENGTH,
                        FP_CBOR_CREATE_KEYS_REJECTED_TOPIC ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t subscribeToRegisterThingResponseTopics( void )
{
    int returnStatus = EXIT_SUCCESS;

    returnStatus = SubscribeToTopic( FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                     FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

    if( returnStatus != EXIT_SUCCESS )
    {
        LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                    FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                    FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus = SubscribeToTopic( FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                   FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to subscribe to fleet provisioning topic: %.*s.",
                        FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                        FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t unsubscribeFromRegisterThingResponseTopics( void )
{
    int returnStatus = EXIT_SUCCESS;

    returnStatus = UnsubscribeFromTopic(FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                        FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

    if( returnStatus != EXIT_SUCCESS )
    {
        LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                    FP_CBOR_REGISTER_ACCEPTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                    FP_CBOR_REGISTER_ACCEPTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        returnStatus =UnsubscribeFromTopic( FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                   FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ) );

        if( returnStatus != EXIT_SUCCESS )
        {
            LogError( ( "Failed to unsubscribe from fleet provisioning topic: %.*s.",
                        FP_CBOR_REGISTER_REJECTED_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                        FP_CBOR_REGISTER_REJECTED_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

/* This is the callback function invoked by the MQTT stack when it receives
 * incoming messages. This function demonstrates how to use the FleetProvisioning_MatchTopic
 * function to determine whether the incoming message is a fleet provisioning message
 * or not. If it is, it handles the message depending on the message type.
 */
static void provisioningPublishCallback(    MQTTContext_t * pMqttContext,
                                            MQTTPacketInfo_t * pPacketInfo,
                                            MQTTDeserializedInfo_t * pDeserializedInfo )
{
    uint16_t packetIdentifier;

    FleetProvisioningStatus_t status;
    FleetProvisioningTopic_t api;
    const char * cborDump;

    ( void ) pMqttContext;

    assert( pDeserializedInfo != NULL );
    assert( pMqttContext != NULL );
    assert( pPacketInfo != NULL );

    packetIdentifier = pDeserializedInfo->packetIdentifier;

    /* Handle incoming publish. The lower 4 bits of the publish packet
     * type is used for the dup, QoS, and retain flags. Hence masking
     * out the lower bits to check if the packet is publish. */
    if ( ( pPacketInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        assert( pDeserializedInfo->pPublishInfo != NULL );
        // LogInfo( ( "pPublishInfo->pTopicName:%s.", pDeserializedInfo->pPublishInfo->pTopicName ) );
        // LogInfo( ( "cbor payload:%s.", ( const char * ) pDeserializedInfo->pPublishInfo->pPayload ) );

        /* Let the Device Shadow library tell us whether this is a device shadow message. */
        status = FleetProvisioning_MatchTopic(  pDeserializedInfo->pPublishInfo->pTopicName,
                                                pDeserializedInfo->pPublishInfo->topicNameLength, &api );

        if ( status == FleetProvisioningError )
        {
            LogError( ( "FleetProvisioningError" ) );
        }
        else if ( status == FleetProvisioningNoMatch )
        {
            LogError( ( "FleetProvisioningNoMatch" ) );
        }
        else if ( status == FleetProvisioningBadParameter )
        {
            LogError( ( "FleetProvisioningBadParameter" ) );
        }
        else if ( status == FleetProvisioningBufferTooSmall )
        {
            LogError( ( "FleetProvisioningBufferTooSmall" ) );
        }
        else if ( status == FleetProvisioningSuccess )
        {
            LogInfo( ( "FleetProvisioningSuccess" ) );
            if( api == FleetProvCborCreateKeysAndCertAccepted )
            {
                LogInfo( ( "Received accepted response from Fleet Provisioning CreateKeysAndCertificate API." ) );
                
                cborDump = pDeserializedInfo->pPublishInfo->pPayload;
                // LogInfo( ( "Payload: %s", cborDump ) );

                responseStatus = ResponseAccepted;

                /* Copy the payload from the MQTT library's buffer to #payloadBuffer. */
                ( void ) memcpy( ( void * ) payloadBuffer,
                                ( const void * ) pDeserializedInfo->pPublishInfo->pPayload,
                                ( size_t ) pDeserializedInfo->pPublishInfo->payloadLength );

                payloadLength = pDeserializedInfo->pPublishInfo->payloadLength;
            }
            else if( api == FleetProvCborCreateKeysAndCertRejected )
            {
                LogError( ( "Received rejected response from Fleet Provisioning CreateKeysAndCertificate API." ) );
                
                cborDump = pDeserializedInfo->pPublishInfo->pPayload;
                // LogError( ( "Payload: %s", cborDump ) );

                responseStatus = ResponseRejected;
            }
            else if( api == FleetProvCborRegisterThingAccepted )
            {
                LogInfo( ( "Received accepted response from Fleet Provisioning RegisterThing API." ) );

                cborDump = pDeserializedInfo->pPublishInfo->pPayload;
                // LogInfo( ( "Payload: %s", cborDump ) );

                responseStatus = ResponseAccepted;

                /* Copy the payload from the MQTT library's buffer to #payloadBuffer. */
                ( void ) memcpy( ( void * ) payloadBuffer,
                                ( const void * ) pDeserializedInfo->pPublishInfo->pPayload,
                                ( size_t ) pDeserializedInfo->pPublishInfo->payloadLength );

                payloadLength = pDeserializedInfo->pPublishInfo->payloadLength;
            }
            else if( api == FleetProvCborRegisterThingRejected )
            {
                LogError( ( "Received rejected response from Fleet Provisioning RegisterThing API." ) );

                cborDump = pDeserializedInfo->pPublishInfo->pPayload;
                // LogError( ( "Payload: %s", cborDump ) );

                responseStatus = ResponseRejected;
            }
            else
            {
                LogError( ( "Received message on unexpected Fleet Provisioning topic. Topic: %.*s.",
                            ( int ) pDeserializedInfo->pPublishInfo->topicNameLength,
                            ( const char * ) pDeserializedInfo->pPublishInfo->pTopicName ) );
            }
        }
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
        // LogInfo( ( "pPublishInfo->pTopicName:%s.", pDeserializedInfo->pPublishInfo->pTopicName ) );
        // LogInfo( ( "pPublish->pPayload:%s.", ( const char * ) pDeserializedInfo->pPublishInfo->pPayload ) );

        /* Let the Device Shadow library tell us whether this is a device shadow message. */
        if( SHADOW_SUCCESS == Shadow_MatchTopicString( pDeserializedInfo->pPublishInfo->pTopicName,
                                                       pDeserializedInfo->pPublishInfo->topicNameLength,
                                                       &messageType,
                                                       &pThingName,
                                                       &thingNameLength,
                                                       &pShadowName,
                                                       &shadowNameLength ) )
        {
            // /* Upon successful return, the messageType has been filled in. */
            // if( messageType == ShadowMessageTypeUpdateDelta )
            // {
            //     /* Handler function to process payload. */
            //     updateDeltaHandler( pDeserializedInfo->pPublishInfo );
            // }
            // else if( messageType == ShadowMessageTypeUpdateAccepted )
            // {
            //     /* Handler function to process payload. */
            //     updateAcceptedHandler( pDeserializedInfo->pPublishInfo );
            // }
            // else if( messageType == ShadowMessageTypeUpdateDocuments )
            // {
            //     LogInfo( ( "/update/documents json payload:%s.", ( const char * ) pDeserializedInfo->pPublishInfo->pPayload ) );
            // }
            // else if( messageType == ShadowMessageTypeUpdateRejected )
            // {
            //     LogInfo( ( "/update/rejected json payload:%s.", ( const char * ) pDeserializedInfo->pPublishInfo->pPayload ) );
            // }
            // else if( messageType == ShadowMessageTypeDeleteAccepted )
            // {
            //     LogInfo( ( "Received an MQTT incoming publish on /delete/accepted topic." ) );
            //     shadowDeleted = true;
            //     deleteResponseReceived = true;
            // }
            // else if( messageType == ShadowMessageTypeDeleteRejected )
            // {
            //     /* Handler function to process payload. */
            //     deleteRejectedHandler( pDeserializedInfo->pPublishInfo );
            //     deleteResponseReceived = true;
            // }
            // else
            // {
            //     LogInfo( ( "Other message type:%d !!", messageType ) );
            // }
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
    /* Buffer for holding the private key. */
    char privateKey[ PRIV_KEY_BUFFER_LENGTH ];
    size_t privateKeyLength;
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
        privateKeyLength = PRIV_KEY_BUFFER_LENGTH;

        // TODO: Initialize the PKCS #11 module

        /**** Connect to AWS IoT Core with provisioning claim credentials *****/

        if (!provisioned)
        {
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
                * topics. In this demo we use CBOR encoding for the payloads,
                * so we use the CBOR variants of the topics. */
                returnStatus = subscribeToKeyCertificateResponseTopics();
            }

            // Note: Skipped create a new key and CSR.

            // Note: Skipped generateCsrRequest()

            if ( returnStatus == EXIT_SUCCESS )
            {
                /* Publish to the CreateKeysAndCertificate API. */
                returnStatus = PublishToTopic( FP_CBOR_CREATE_KEYS_PUBLISH_TOPIC,
                                FP_CBOR_CREATE_KEYS_PUBLISH_LENGTH,
                                ( char * ) payloadBuffer,
                                payloadLength );

                if( returnStatus == EXIT_FAILURE )
                {
                    LogError( ( "Failed to publish to fleet provisioning topic: %.*s.",
                                FP_CBOR_CREATE_KEYS_PUBLISH_LENGTH,
                                FP_CBOR_CREATE_KEYS_PUBLISH_TOPIC ) );
                }
            }

            // if ( returnStatus == EXIT_SUCCESS )
            // {
            //     /* Get the response to the CreateKeysAndCertificate request. */
            //     returnStatus = waitForResponse();
            // }
            
            if( returnStatus == EXIT_SUCCESS )
            {
                /* From the response, extract the certificate, certificate ID, and
                * certificate ownership token. */
                bool parseStatus = parseKeyCertResponse(    payloadBuffer,
                                                            payloadLength,
                                                            certificate,
                                                            &certificateLength,
                                                            certificateId,
                                                            &certificateIdLength,
                                                            ownershipToken,
                                                            &ownershipTokenLength,
                                                            privateKey,
                                                            &privateKeyLength );

                if( parseStatus == true )
                {
                    LogInfo( ( "Received certificate: %.*s", ( int ) certificateLength, certificate ) );
                    LogInfo( ( "Received certificate with Id: %.*s", ( int ) certificateIdLength, certificateId ) );
                    LogInfo( ( "Received ownershipToken: %.*s", ( int ) ownershipTokenLength, ownershipToken ) );
                    LogInfo( ( "Received privateKey: %.*s", ( int ) privateKeyLength, privateKey ) );
                    provisioned_cert = certificate;
                    provisioned_certID = certificateId;
                    provisioned_ownership_token = ownershipToken;
                    provisioned_privatekey = privateKey;
                    /* --- Storing CERT and KEY into NVS --- */
                    nvs_handle_t my_handle;
                    esp_err_t nvs_err = nvs_open("storage", NVS_READWRITE, &my_handle);
                    if (nvs_err != ESP_OK) {
                        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(nvs_err));
                    } else {
                        printf("Done\n");

                        // Write
                        printf("Updating CERT and KEY in NVS ... ");
                        nvs_err = nvs_set_str(my_handle, "aws_cert", certificate);
                        printf((nvs_err != ESP_OK) ? "Failed!\n" : "Done in storing CERT in NVS\n");

                        nvs_err = nvs_set_str(my_handle, "aws_certID", certificateId);
                        printf((nvs_err != ESP_OK) ? "Failed!\n" : "Done in storing CERT_ID in NVS\n");

                        nvs_err = nvs_set_str(my_handle, "aws_token", ownershipToken);
                        printf((nvs_err != ESP_OK) ? "Failed!\n" : "Done in storing TOKEN in NVS\n");

                        nvs_err = nvs_set_str(my_handle, "aws_key", privateKey);
                        printf((nvs_err != ESP_OK) ? "Failed!\n" : "Done in storing KEY in NVS\n");

                        // Commit written value.
                        // After setting any values, nvs_commit() must be called to ensure changes are written
                        // to flash storage. Implementations may write to storage at other times,
                        // but this is not guaranteed.
                        printf("Committing updates in NVS ... ");
                        nvs_err = nvs_commit(my_handle);
                        printf((nvs_err != ESP_OK) ? "Failed!\n" : "Done\n");

                        // Close
                        nvs_close(my_handle);
                    }
                    /* --- END of storing CERT and KEY in NVS --- */

                    returnStatus = EXIT_SUCCESS;
                }
            }

            // Note: Skipped saving certificate into PKCS #11
            // if ( returnStatus == EXIT_SUCCESS )
            // {
            //     /* Save the certificate into PKCS #11. */
            //     status = loadCertificate( p11Session,
            //                               certificate,
            //                               pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
            //                               certificateLength );
            // }
            

            if ( returnStatus == EXIT_SUCCESS )
            {
                /* Unsubscribe from the CreateKeysAndCertificate topics. */
                returnStatus = unsubscribeFromKeyCertificateResponseTopics();
            }

            /**** Call the RegisterThing API **************************************/

            /* We then use the RegisterThing API to activate the received certificate,
            * provision AWS IoT resources according to the provisioning template, and
            * receive device configuration. */            

            if( returnStatus == EXIT_SUCCESS )
            {
                /* Create the request payload to publish to the RegisterThing API. */
                bool generateStatus = generateRegisterThingRequest( payloadBuffer,
                                                                    NETWORK_BUFFER_SIZE,
                                                                    ownershipToken,
                                                                    ownershipTokenLength,
                                                                    DEVICE_SERIAL_NUMBER,
                                                                    DEVICE_SERIAL_NUMBER_LENGTH,
                                                                    &payloadLength );

                if( generateStatus == true )
                {
                    LogInfo( ( "generateRegisterThingRequest success" ) );
                    returnStatus = EXIT_SUCCESS;
                }
            }

            if ( returnStatus == EXIT_SUCCESS )
            {
                /* Subscribe to the RegisterThing response topics. */
                returnStatus = subscribeToRegisterThingResponseTopics();
            }

            if( returnStatus == EXIT_SUCCESS )
            {
                /* Publish the RegisterThing request. */
                returnStatus = PublishToTopic(  FP_CBOR_REGISTER_PUBLISH_TOPIC( PROVISIONING_TEMPLATE_NAME ),
                                                FP_CBOR_REGISTER_PUBLISH_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                                                ( char * ) payloadBuffer,
                                                payloadLength );

                if( returnStatus != EXIT_SUCCESS )
                {
                    LogError( ( "Failed to publish to fleet provisioning topic: %.*s.",
                                FP_CBOR_REGISTER_PUBLISH_LENGTH( PROVISIONING_TEMPLATE_NAME_LENGTH ),
                                FP_CBOR_REGISTER_PUBLISH_TOPIC( PROVISIONING_TEMPLATE_NAME ) ) );
                }
            }

            if( returnStatus == EXIT_SUCCESS )
            {
                /* Get the response to the RegisterThing request. */
                returnStatus = waitForResponse();
            }

            if( returnStatus == EXIT_SUCCESS )
            {
                /* Extract the Thing name from the response. */
                thingNameLength = MAX_THING_NAME_LENGTH;
                bool parseStatus = parseRegisterThingResponse(  payloadBuffer,
                                                                payloadLength,
                                                                thingName,
                                                                &thingNameLength );

                if( parseStatus == true )
                {
                    LogInfo( ( "Received AWS IoT Thing name: %.*s", ( int ) thingNameLength, thingName ) );
                    returnStatus = EXIT_SUCCESS;
                }
            }
            
            if( returnStatus == EXIT_SUCCESS )
            {
                /* Unsubscribe from the RegisterThing topics. */
                returnStatus = unsubscribeFromRegisterThingResponseTopics();
            }
            
            /**** Disconnect from AWS IoT Core ************************************/

            /* As we have completed the provisioning workflow, we disconnect from
            * the connection using the provisioning claim credentials. We will
            * establish a new MQTT connection with the newly provisioned
            * credentials. */
            if( connectionEstablished == true )
            {
                returnStatus = DisconnectMqttSession();
                if ( returnStatus == EXIT_SUCCESS )
                {
                    connectionEstablished = false;
                    provisioned = true;
                }
            }
        }

        /**** Connect to AWS IoT Core with provisioned certificate ************/

        if ( returnStatus == EXIT_SUCCESS && provisioned == true )
        {
            LogInfo( ( "Establishing MQTT session with provisioned certificate..." ) );
            returnStatus = EstablishProvisionedMqttSession( eventCallback );

            if( returnStatus != EXIT_SUCCESS )
            {
                LogError( ( "Failed to establish MQTT session with provisioned "
                            "credentials. Verify on your AWS account that the "
                            "new certificate is active and has an attached IoT "
                            "Policy that allows the \"iot:Connect\" action." ) );
            }
            else
            {
                LogInfo( ( "Sucessfully established connection with provisioned credentials." ) );
                connectionEstablished = true;
            }
        }

        /**** Update Thing Status documents ***********************************/

        if ( returnStatus == EXIT_SUCCESS )
        {
            /* Reset the shadow delete status flags. */
            deleteResponseReceived = false;
            shadowDeleted = false;

            /* First of all, try to delete any Shadow document in the cloud.
             * Try to subscribe to `/delete/accepted` and `/delete/rejected` topics. */
            returnStatus = SubscribeToTopic( SHADOW_TOPIC_STR_DELETE_ACC( THING_NAME, SHADOW_NAME ),
                                             SHADOW_TOPIC_LEN_DELETE_ACC( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }
        
        if( returnStatus == EXIT_SUCCESS )
        {
            /* Try to subscribe to `/delete/rejected` topic. */
            returnStatus = SubscribeToTopic( SHADOW_TOPIC_STR_DELETE_REJ( THING_NAME, SHADOW_NAME ),
                                             SHADOW_TOPIC_LEN_DELETE_REJ( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        /* A buffer containing the update document. It has static duration to prevent
         * it from being placed on the call stack. */
        static char updateDocument[ SHADOW_REPORTED_JSON_LENGTH + 1 ] = { 0 };
        if( returnStatus == EXIT_SUCCESS )
        {
            /* Publish to Shadow `delete` topic to attempt to delete the
             * Shadow document if exists. */
            returnStatus = PublishToTopic( SHADOW_TOPIC_STR_DELETE( THING_NAME, SHADOW_NAME ),
                                            SHADOW_TOPIC_LEN_DELETE( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ),
                                            updateDocument,
                                            0U );
        }

        /* Unsubscribe from the `/delete/accepted` and 'delete/rejected` topics.*/
        if( returnStatus == EXIT_SUCCESS )
        {
            returnStatus = UnsubscribeFromTopic( SHADOW_TOPIC_STR_DELETE_ACC( THING_NAME, SHADOW_NAME ),
                                                    SHADOW_TOPIC_LEN_DELETE_ACC( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            returnStatus = UnsubscribeFromTopic( SHADOW_TOPIC_STR_DELETE_REJ( THING_NAME, SHADOW_NAME ),
                                                    SHADOW_TOPIC_LEN_DELETE_REJ( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        /* Check if an incoming publish on `/delete/accepted` or `/delete/rejected`
         * topics. If a response is not received, mark the demo execution as a failure.*/
        if( ( returnStatus == EXIT_SUCCESS ) && ( deleteResponseReceived != true ) )
        {
            LogError( ( "Failed to receive a response for Shadow delete." ) );
            returnStatus = EXIT_FAILURE;
        }

        /* Check if Shadow document delete was successful. A delete can be
        * successful in cases listed below.
        *  1. If an incoming publish is received on `/delete/accepted` topic.
        *  2. If an incoming publish is received on `/delete/rejected` topic
        *     with an error code 404. This indicates that a delete was
        *     attempted when a Shadow document is not available for the
        *     Thing. */
        if( returnStatus == EXIT_SUCCESS )
        {
            if( shadowDeleted == false )
            {
                LogError( ( "Shadow delete operation failed." ) );
                returnStatus = EXIT_FAILURE;
            }
            else
            {
                LogInfo( ( "Shadow delete success.") );
            }
        }

        /* Successfully connect to MQTT broker, the next step is
         * to subscribe shadow topics. */
        returnStatus = SubscribeToTopic( SHADOW_TOPIC_STR_UPDATE_DELTA( THING_NAME, SHADOW_NAME ),
                                            SHADOW_TOPIC_LEN_UPDATE_DELTA( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );


        if( returnStatus == EXIT_SUCCESS )
        {
            returnStatus = SubscribeToTopic( SHADOW_TOPIC_STR_UPDATE_ACC( THING_NAME, SHADOW_NAME ),
                                                SHADOW_TOPIC_LEN_UPDATE_ACC( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            returnStatus = SubscribeToTopic( SHADOW_TOPIC_STR_UPDATE_REJ( THING_NAME, SHADOW_NAME ),
                                                SHADOW_TOPIC_LEN_UPDATE_REJ( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        /* This demo uses a constant #THING_NAME and #SHADOW_NAME known at compile time therefore
            * we can use macros to assemble shadow topic strings.
            * If the thing name or shadow name is only known at run time, then we could use the API
            * #Shadow_AssembleTopicString to assemble shadow topic strings, here is the example for /update/delta:
            *
            * For /update/delta:
            *
            * #define SHADOW_TOPIC_MAX_LENGTH  (256U)
            *
            * ShadowStatus_t shadowStatus = SHADOW_SUCCESS;
            * char topicBuffer[ SHADOW_TOPIC_MAX_LENGTH ] = { 0 };
            * uint16_t bufferSize = SHADOW_TOPIC_MAX_LENGTH;
            * uint16_t outLength = 0;
            * const char thingName[] = { "TestThingName" };
            * uint16_t thingNameLength  = ( sizeof( thingName ) - 1U );
            * const char shadowName[] = { "TestShadowName" };
            * uint16_t shadowNameLength  = ( sizeof( shadowName ) - 1U );
            *
            * shadowStatus = Shadow_AssembleTopicString( ShadowTopicStringTypeUpdateDelta,
            *                                            thingName,
            *                                            thingNameLength,
            *                                            shadowName,
            *                                            shadowNameLength,
            *                                            & ( topicBuffer[ 0 ] ),
            *                                            bufferSize,
            *                                            & outLength );
            */

        /* Then we publish a desired state to the /update topic. Since we've deleted
            * the device shadow at the beginning of the demo, this will cause a delta message
            * to be published, which we have subscribed to.
            * In many real applications, the desired state is not published by
            * the device itself. But for the purpose of making this demo self-contained,
            * we publish one here so that we can receive a delta message later.
            */
        if( returnStatus == EXIT_SUCCESS )
        {
            /* desired power on state . */
            LogInfo( ( "Send desired power state with 1." ) );

            ( void ) memset( updateDocument,
                                0x00,
                                sizeof( updateDocument ) );

            /* Keep the client token in global variable used to compare if
                * the same token in /update/accepted. */
            clientToken = ( Clock_GetTimeMs() % 1000000 );

            snprintf( updateDocument,
                        SHADOW_DESIRED_JSON_LENGTH + 1,
                        SHADOW_DESIRED_JSON,
                        ( int ) 1,
                        ( long unsigned ) clientToken );

            returnStatus = PublishToTopic( SHADOW_TOPIC_STR_UPDATE( THING_NAME, SHADOW_NAME ),
                                            SHADOW_TOPIC_LEN_UPDATE( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ),
                                            updateDocument,
                                            ( SHADOW_DESIRED_JSON_LENGTH + 1 ) );
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            /* Note that PublishToTopic already called MQTT_ProcessLoop,
                * therefore responses may have been received and the eventCallback
                * may have been called, which may have changed the stateChanged flag.
                * Check if the state change flag has been modified or not. If it's modified,
                * then we publish reported state to update topic.
                */
            if( stateChanged == true )
            {
                /* Report the latest power state back to device shadow. */
                LogInfo( ( "Report to the state change: %d", currentPowerOnState ) );
                ( void ) memset( updateDocument,
                                    0x00,
                                    sizeof( updateDocument ) );

                /* Keep the client token in global variable used to compare if
                    * the same token in /update/accepted. */
                clientToken = ( Clock_GetTimeMs() % 1000000 );

                snprintf( updateDocument,
                            SHADOW_REPORTED_JSON_LENGTH + 1,
                            SHADOW_REPORTED_JSON,
                            ( int ) currentPowerOnState,
                            ( long unsigned ) clientToken );

                returnStatus = PublishToTopic( SHADOW_TOPIC_STR_UPDATE( THING_NAME, SHADOW_NAME ),
                                                SHADOW_TOPIC_LEN_UPDATE( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ),
                                                updateDocument,
                                                ( SHADOW_REPORTED_JSON_LENGTH + 1 ) );
            }
            else
            {
                LogInfo( ( "No change from /update/delta, unsubscribe all shadow topics and disconnect from MQTT.\r\n" ) );
            }
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            LogInfo( ( "Start to unsubscribe shadow topics and disconnect from MQTT. \r\n" ) );
            returnStatus = UnsubscribeFromTopic( SHADOW_TOPIC_STR_UPDATE_DELTA( THING_NAME, SHADOW_NAME ),
                                                    SHADOW_TOPIC_LEN_UPDATE_DELTA( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            returnStatus = UnsubscribeFromTopic( SHADOW_TOPIC_STR_UPDATE_ACC( THING_NAME, SHADOW_NAME ),
                                                    SHADOW_TOPIC_LEN_UPDATE_ACC( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            returnStatus = UnsubscribeFromTopic( SHADOW_TOPIC_STR_UPDATE_REJ( THING_NAME, SHADOW_NAME ),
                                                    SHADOW_TOPIC_LEN_UPDATE_REJ( THING_NAME_LENGTH, SHADOW_NAME_LENGTH ) );
        }
        
        /**** Finish **********************************************************/

        if( connectionEstablished == true )
        {
            /* Close the connection. */
            DisconnectMqttSession();
            connectionEstablished = false;
        }

        // TODO: Close PKCS11 session
    } while ( 0 );
    // } while( returnStatus != EXIT_SUCCESS );

    if( returnStatus == EXIT_SUCCESS )
    {
        /* Log message indicating the demo completed successfully. */
        LogInfo( ( "Demo completed successfully." ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/
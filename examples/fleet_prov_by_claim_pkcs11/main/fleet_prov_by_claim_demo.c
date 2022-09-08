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
 * @file fleet_prov_by_claim_demo.c
 * @brief Fleet Provisioning by Claim example using coreMQTT.
 */

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"

/* POSIX includes. */
#include <unistd.h>

/* Clock for timer. */
#include "clock.h"

/* Demo includes. */
#include "fleet_prov_demo_helpers.h"
#include "pkcs11_operations.h"
#include "fleet_provisioning_serializer.h"

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
 * @brief Run the MQTT process loop to get a response.
 */
static int32_t waitForResponse( void );

/**
 * @brief Subscribe to the CreateKeysAndCertificate accepted and rejected topics.
 */
static int32_t subscribeToKeyCertificateResponseTopics( void );

/**
 * @brief Unsubscribe from the CreateKeysAndCertificate accepted and rejected topics.
 */
static int32_t unsubscribeFromKeyCertificateResponseTopics( void );

/**
 * @brief Subscribe to the RegisterThing accepted and rejected topics.
 */
static int32_t subscribeToRegisterThingResponseTopics( void );

/**
 * @brief Unsubscribe from the RegisterThing accepted and rejected topics.
 */
static int32_t unsubscribeFromRegisterThingResponseTopics( void );

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

int aws_iot_demo_main( int argc, char ** argv );

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
 * incoming messages. This function demonstrates how to use the Shadow_MatchTopicString
 * function to determine whether the incoming message is a device shadow message
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

/**
 * @brief Entry point of demo.
 *
 * This example initializes the OTA library to enable OTA updates via the
 * MQTT broker. It simply connects to the MQTT broker with the users
 * credentials and spins in an indefinite loop to allow MQTT messages to be
 * forwarded to the OTA agent for possible processing. The OTA agent does all
 * of the real work; checking to see if the message topic is one destined for
 * the OTA agent. If not, it is simply ignored.
 */
int aws_iot_demo_main( int argc,
          char ** argv )
{
    /* Return error status. */
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
    CK_SESSION_HANDLE p11Session;
    CK_RV pkcs11ret = CKR_OK;

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
        
        /* Initialize NVS */
        nvs_flash_init();
        LogInfo( ( "NVS Flash Initialized" ) );

        /* Initialize the PKCS #11 module */
        pkcs11ret = xInitializePkcs11Session( &p11Session );

        bool pkcs11Status = false;
        if( pkcs11ret != CKR_OK )
        {
            LogError( ( "Failed to initialize PKCS #11." ) );
            pkcs11Status = false;
        }
        else
        {
            char* cert =    "-----BEGIN CERTIFICATE-----\nMIIDWjCCAkKgAwIBAgIVAPoXJ3kVqO0ZCqAltu0qKPdBZAnRMA0GCSqGSIb3DQEB\nCwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29t\nIEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0yMjA2MDExNjEz\nNTlaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNh\ndGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCiqWUZ7rtmbGNztwC4\nMibPuLOBD5lQUvBfWL8bXlzghc9vDJpRy1O2aY/lteHwSuWcoM0k43wYyfYsPToJ\nVEGnWJQvg9+fD92b0DvuzbUwZhqZ5BzUhK18WfXzcxZ1kKyqneYYjIn30pHqih+f\ndUvTgc8xXiWo4Zv19Ec6aFrp1SpRoWDIxfpOGi2/sbRVZ1bsOcIkHFa/gBcz7rr+\nx+mE37dYGDwMm68ngnPFmhW33fep6a+sgpvOo+Nw7kqkK4r6W7N+RRWUmZQpRaSp\nw+RTjeD8eAD9MGUBSJr+i0uW0IU2jiFkxqxHvE6n99D/RoZDP0oiZ5afYzhd/BNG\nF5ejAgMBAAGjYDBeMB8GA1UdIwQYMBaAFIXZdeK0wCC8Lizek60wKdVPUAYMMB0G\nA1UdDgQWBBR2qht5MaawLGUeMFX8v7gLYgqWejAMBgNVHRMBAf8EAjAAMA4GA1Ud\nDwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEAEfghQWTuv6HlC182PPSkoDqq\nfUgR2mJ516Ms3uA0wUc8Hf82k0BZYfUWpc0npLZ88BFxQ2kSO3X4bktpljnLUyOT\nIAGb5D59/EIxM5lOVe1Ci1fV0FIGlISU0FWfNjpnUXf8ZWpKDF5zJ4x4li/woT9j\nyi+rqPpd2cAKhzSvm163i2SV3x18L9qYwbiaZrc08F1v4EkRBr4hNXuo1lCsDYzI\n3Nu+/8g3gSftvTbJokOgN3apIZzWm8zMOc1FX5GWGd6Ky6ntgT4IVSyBkI41qZxU\nLqir2gPonKFCoR4TP2sBu+GOFitkKfq1MIyKlJHJFxKMgUVCtPLuLqXSJ/DBtA==\n-----END CERTIFICATE-----";
            char* privkey = "-----BEGIN RSA PRIVATE KEY-----\nMIIEogIBAAKCAQEAoqllGe67Zmxjc7cAuDImz7izgQ+ZUFLwX1i/G15c4IXPbwya\nUctTtmmP5bXh8ErlnKDNJON8GMn2LD06CVRBp1iUL4Pfnw/dm9A77s21MGYameQc\n1IStfFn183MWdZCsqp3mGIyJ99KR6oofn3VL04HPMV4lqOGb9fRHOmha6dUqUaFg\nyMX6Thotv7G0VWdW7DnCJBxWv4AXM+66/sfphN+3WBg8DJuvJ4JzxZoVt933qemv\nrIKbzqPjcO5KpCuK+luzfkUVlJmUKUWkqcPkU43g/HgA/TBlAUia/otLltCFNo4h\nZMasR7xOp/fQ/0aGQz9KImeWn2M4XfwTRheXowIDAQABAoIBAEmxSs9728TkWA4l\nm5rXhcPX3uMaqQ+9846Oy03f614A4WBjKkriPhPHMV0VkL3ngKz8INSUhzVH0lJq\njq+JT5E8TS5VpWsPqgucRHrFEVBTAbw0n2cckOhkbUwVGNi8aa2GiacXjK4M5PSI\nDRmV73tsNO0dxRwE0j6Uo+xvOLj0k/k2dYgLfFsp9GQBbKs5fEIdUfDO6Fn9hdGO\nzdGnY8CnMZRpp5X1tcPYM0RTJQj7M+UeImNHMYR640P5luB4FPTt5uGFIZZecmIR\no3VaIplpmqMM3cjGsdf5zhb8pXfyLiRF6WVIZxHg4zjRxesxTuMohghbWr6ClviG\ninoAutkCgYEA1VnhtE2UiJIV8ySlsjKOMyByL9oeRDqfVixci8G1ZwEC7voJ9Hig\nK0hhzzAwxpKJFUPMstHRm1xGlB56Rg9f9eDQQq4UJPN+q+XJiBNWQnb9Z2Csi25u\ntlVp0UrO4buo/5UeNrARYYRrSs2Gx2/3IGCV2mlJNLGa2DlFOEMM2b0CgYEAwy2D\nPk2blFIkfy++9rTJBViXZijpHoE0vD5b/+79B1Ac4K9rwJNtmNgv3l+SN9bOxJ2A\nafSNhf1BTHgmg8ZJZJ4NDOM6j7QZcZNMvuZF1TgNWV1f7xTIm/JpOXSZK77k30D9\nTAOB9h3xtH0vPERUfjRV+ycd2w/+FCfOIqGeXN8CgYBIcxhJMRsicXFQuv4lkDNn\nuznrdAdZJgsbqT7YGrSuQNKtMm2U/i1t5UuJnxTBKduxQ+/MPaIPPvucqujcx7XP\nekNekVy325QFbafNNLvTIDMXGuYdByhDdKfVcbDlSOOvvwSej5WnZt9EbJy7NxNV\nhFb+70fzw+gQSwpte59uhQKBgEmw3WSgmKUffngm5sru5xcFo+QGfj3uOqL4SHQR\nH6erL7wFf4FuKGsU9L3ZB7PdfqPtc5aNpwF35TeiBairLPq4UeUTxgCL1y9ylf9d\nofAnAaNEBfyWtEds9x2iUFKb+H3yY7BXgrISDDhBK5xtkBk2WWBCHJuhJiUmAkZB\nDzGxAoGAKNE+KR8KIXccyPQ9sQsKxO5cxzKB7JQkl0GLh2xhyckmjF8eMd4yxLNI\n9dTQFqtXrj757ANHGP8qipOuDB2K/+93WfjS2YbBUa/r88nY/RZkxYTtkjaZZ3Tk\ne/C3gNpmMCiBfkiyLjF2n0JmTvErEGIkoWwXWAbEPQrI+jvpg7A=\n-----END RSA PRIVATE KEY-----";
            /* Insert the claim credentials into the PKCS #11 module */
            pkcs11Status = loadClaimCredentials( p11Session,
                                                cert,
                                                "Claim Cert",
                                                privkey,
                                                "Claim Key" );

            if( pkcs11Status == false )
            {
                LogError( ( "Failed to provision PKCS #11 with claim credentials." ) );
            }
        }

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
                returnStatus = EXIT_SUCCESS;
            }
        }

        if ( returnStatus == EXIT_SUCCESS )
        {
            /* Save the certificate into PKCS #11. */
            bool certStatus = loadCertificate(  p11Session,
                                                certificate,
                                                "Device Cert",
                                                certificateLength );

            if ( certStatus == true )
            {
                returnStatus = EXIT_SUCCESS;
            }
            
        }

    } while ( returnStatus != EXIT_SUCCESS );
    

    return returnStatus;
}

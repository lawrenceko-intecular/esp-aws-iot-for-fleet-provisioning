/* Standard includes */
#include <stdarg.h>

/* TinyCBOR library for CBOR encoding and decoding operations. */
#include "cbor.h"

/* Demo config. */
#include "demo_config.h"

/* AWS IoT Fleet Provisioning Library. */
#include "fleet_provisioning.h"

/* Header include. */
#include "fleet_provisioning_serializer.h"

/*-----------------------------------------------------------*/

/**
 * @brief Context passed to tinyCBOR for #cborPrinter. Initial
 * state should be zeroed.
 */
typedef struct
{
    const char * str;
    size_t length;
} CborPrintContext_t;

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

bool parseRegisterThingResponse( const uint8_t * pResponse,
                                 size_t length,
                                 char * pThingNameBuffer,
                                 size_t * pThingNameBufferLength )
{
    CborError cborRet;
    CborParser parser;
    CborValue map;
    CborValue value;

    assert( pResponse != NULL );
    assert( pThingNameBuffer != NULL );
    assert( pThingNameBufferLength != NULL );

    /* For details on the RegisterThing response payload format, see:
     * https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html#register-thing-response-payload
     */
    cborRet = cbor_parser_init( pResponse, length, 0, &parser, &map );

    if( cborRet != CborNoError )
    {
        LogError( ( "Error initializing parser for RegisterThing response: %s.", cbor_error_string( cborRet ) ) );
    }
    else if( !cbor_value_is_map( &map ) )
    {
        LogError( ( "RegisterThing response not a map type." ) );
    }
    else
    {
        cborRet = cbor_value_map_find_value( &map, "thingName", &value );

        if( cborRet != CborNoError )
        {
            LogError( ( "Error searching RegisterThing response: %s.", cbor_error_string( cborRet ) ) );
        }
        else if( value.type == CborInvalidType )
        {
            LogError( ( "\"thingName\" not found in RegisterThing response." ) );
        }
        else if( value.type != CborTextStringType )
        {
            LogError( ( "\"thingName\" is an unexpected type in RegisterThing response." ) );
        }
        else
        {
            cborRet = cbor_value_copy_text_string( &value, pThingNameBuffer, pThingNameBufferLength, NULL );

            if( cborRet == CborErrorOutOfMemory )
            {
                size_t requiredLen = 0;
                ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
                LogError( ( "Thing name buffer insufficiently large. Thing name length: %lu", ( unsigned long ) requiredLen ) );
            }
            else if( cborRet != CborNoError )
            {
                LogError( ( "Failed to parse \"thingName\" value from RegisterThing response: %s.", cbor_error_string( cborRet ) ) );
            }
        }
    }

    return( cborRet == CborNoError );
}

/**
 * @brief Printing function to pass to tinyCBOR.
 *
 * cbor_value_to_pretty_stream calls it multiple times to print a textual CBOR
 * representation.
 *
 * @param token Context for the function.
 * @param fmt Printf style format string.
 * @param ... Printf style args after format string.
 */
static CborError cborPrinter( void * token,
                              const char * fmt,
                              ... );

/*-----------------------------------------------------------*/

bool generateRegisterThingRequest( uint8_t * pBuffer,
                                   size_t bufferLength,
                                   const char * pCertificateOwnershipToken,
                                   size_t certificateOwnershipTokenLength,
                                   const char * pSerial,
                                   size_t serialLength,
                                   size_t * pOutLengthWritten )
{
    CborEncoder encoder, mapEncoder, parametersEncoder;
    CborError cborRet;

    assert( pBuffer != NULL );
    assert( pCertificateOwnershipToken != NULL );
    assert( pSerial != NULL );
    assert( pOutLengthWritten != NULL );

    /* For details on the RegisterThing request payload format, see:
     * https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html#register-thing-request-payload
     */
    cbor_encoder_init( &encoder, pBuffer, bufferLength, 0 );
    /* The RegisterThing request payload is a map with two keys. */
    cborRet = cbor_encoder_create_map( &encoder, &mapEncoder, 2 );

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encode_text_stringz( &mapEncoder, "certificateOwnershipToken" );
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encode_text_string( &mapEncoder, pCertificateOwnershipToken, certificateOwnershipTokenLength );
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encode_text_stringz( &mapEncoder, "parameters" );
    }

    if( cborRet == CborNoError )
    {
        /* Parameters in this example is length 1. */
        cborRet = cbor_encoder_create_map( &mapEncoder, &parametersEncoder, 1 );
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encode_text_stringz( &parametersEncoder, "SerialNumber" );
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encode_text_string( &parametersEncoder, pSerial, serialLength );
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encoder_close_container( &mapEncoder, &parametersEncoder );
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_encoder_close_container( &encoder, &mapEncoder );
    }

    if( cborRet == CborNoError )
    {
        *pOutLengthWritten = cbor_encoder_get_buffer_size( &encoder, ( uint8_t * ) pBuffer );
    }
    else
    {
        LogError( ( "Error during CBOR encoding: %s", cbor_error_string( cborRet ) ) );

        if( ( cborRet & CborErrorOutOfMemory ) != 0 )
        {
            LogError( ( "Cannot fit RegisterThing request payload into buffer." ) );
        }
    }

    return( cborRet == CborNoError );
}

/*-----------------------------------------------------------*/

bool parseKeyCertResponse(  const uint8_t * pResponse,
                            size_t length,
                            char * pCertificateBuffer,
                            size_t * pCertificateBufferLength,
                            char * pCertificateIdBuffer,
                            size_t * pCertificateIdBufferLength,
                            char * pOwnershipTokenBuffer,
                            size_t * pOwnershipTokenBufferLength,
                            char * pPrivateKeyBuffer,
                            size_t * pPrivateKeyBufferLength )
{
    CborError cborRet;
    CborParser parser;
    CborValue map;
    CborValue value;

    assert( pResponse != NULL );
    assert( pCertificateBuffer != NULL );
    assert( pCertificateBufferLength != NULL );
    assert( pCertificateIdBuffer != NULL );
    assert( pCertificateIdBufferLength != NULL );
    assert( *pCertificateIdBufferLength >= 64 );
    assert( pOwnershipTokenBuffer != NULL );
    assert( pOwnershipTokenBufferLength != NULL );
    assert( pPrivateKeyBuffer != NULL );
    assert( pPrivateKeyBufferLength != NULL );

    /* For details on the CreateCertificatefromCsr response payload format, see:
     * https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html#register-thing-response-payload
     */
    cborRet = cbor_parser_init( pResponse, length, 0, &parser, &map );

    if( cborRet != CborNoError )
    {
        LogError( ( "Error initializing parser for CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    }
    else if( !cbor_value_is_map( &map ) )
    {
        LogError( ( "CreateCertificateFromCsr response is not a valid map container type." ) );
    }
    else
    {
        cborRet = cbor_value_map_find_value( &map, "certificatePem", &value );

        if( cborRet != CborNoError )
        {
            LogError( ( "Error searching CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
        }
        else if( value.type == CborInvalidType )
        {
            LogError( ( "\"certificatePem\" not found in CreateKeysAndCertificate response." ) );
        }
        else if( value.type != CborTextStringType )
        {
            LogError( ( "Value for \"certificatePem\" key in CreateKeysAndCertificate response is not a text string type." ) );
        }
        else
        {
            cborRet = cbor_value_copy_text_string( &value, pCertificateBuffer, pCertificateBufferLength, NULL );

            if( cborRet == CborErrorOutOfMemory )
            {
                size_t requiredLen = 0;
                ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
                LogError( ( "Certificate buffer insufficiently large. Certificate length: %lu", ( unsigned long ) requiredLen ) );
            }
            else if( cborRet != CborNoError )
            {
                LogError( ( "Failed to parse \"certificatePem\" value from CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
            }
        }
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_value_map_find_value( &map, "certificateId", &value );

        if( cborRet != CborNoError )
        {
            LogError( ( "Error searching CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
        }
        else if( value.type == CborInvalidType )
        {
            LogError( ( "\"certificateId\" not found in CreateKeysAndCertificate response." ) );
        }
        else if( value.type != CborTextStringType )
        {
            LogError( ( "\"certificateId\" is an unexpected type in CreateKeysAndCertificate response." ) );
        }
        else
        {
            cborRet = cbor_value_copy_text_string( &value, pCertificateIdBuffer, pCertificateIdBufferLength, NULL );

            if( cborRet == CborErrorOutOfMemory )
            {
                size_t requiredLen = 0;
                ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
                LogError( ( "Certificate ID buffer insufficiently large. Certificate ID length: %lu", ( unsigned long ) requiredLen ) );
            }
            else if( cborRet != CborNoError )
            {
                LogError( ( "Failed to parse \"certificateId\" value from CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
            }
            // LogInfo( ( "Certificate ID given length: %lu", ( unsigned long ) pCertificateIdBufferLength ) );
        }
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_value_map_find_value( &map, "certificateOwnershipToken", &value );

        if( cborRet != CborNoError )
        {
            LogError( ( "Error searching CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
        }
        else if( value.type == CborInvalidType )
        {
            LogError( ( "\"certificateOwnershipToken\" not found in CreateKeysAndCertificate response." ) );
        }
        else if( value.type != CborTextStringType )
        {
            LogError( ( "\"certificateOwnershipToken\" is an unexpected type in CreateKeysAndCertificate response." ) );
        }
        else
        {
            cborRet = cbor_value_copy_text_string( &value, pOwnershipTokenBuffer, pOwnershipTokenBufferLength, NULL );

            if( cborRet == CborErrorOutOfMemory )
            {
                size_t requiredLen = 0;
                ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
                LogError( ( "Certificate ownership token buffer insufficiently large. Certificate ownership token buffer length: %lu", ( unsigned long ) requiredLen ) );
            }
            else if( cborRet != CborNoError )
            {
                LogError( ( "Failed to parse \"certificateOwnershipToken\" value from CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
            }
        }
    }

    if( cborRet == CborNoError )
    {
        cborRet = cbor_value_map_find_value( &map, "privateKey", &value );

        if( cborRet != CborNoError )
        {
            LogError( ( "Error searching CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
        }
        else if( value.type == CborInvalidType )
        {
            LogError( ( "\"privateKey\" not found in CreateKeysAndCertificate response." ) );
        }
        else if( value.type != CborTextStringType )
        {
            LogError( ( "\"privateKey\" is an unexpected type in CreateKeysAndCertificate response." ) );
        }
        else
        {
            cborRet = cbor_value_copy_text_string( &value, pPrivateKeyBuffer, pPrivateKeyBufferLength, NULL );

            if( cborRet == CborErrorOutOfMemory )
            {
                size_t requiredLen = 0;
                ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
                LogError( ( "Private key buffer insufficiently large. Private key buffer length: %lu; given length: %lu", ( unsigned long ) requiredLen, (unsigned long) pPrivateKeyBufferLength ) );
            }
            else if( cborRet != CborNoError )
            {
                LogError( ( "Failed to parse \"privateKey\" value from CreateKeysAndCertificate response: %s.", cbor_error_string( cborRet ) ) );
            }
        }
    }

    return( cborRet == CborNoError );
}
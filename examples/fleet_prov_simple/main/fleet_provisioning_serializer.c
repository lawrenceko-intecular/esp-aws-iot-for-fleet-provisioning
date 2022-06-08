/* Standard includes */
#include <stdarg.h>

/* Demo config. */
#include "demo_config.h"

/* AWS IoT Fleet Provisioning Library. */
#include "fleet_provisioning.h"

/* Header include. */
#include "fleet_provisioning_serializer.h"


/*-----------------------------------------------------------*/

bool parseKeyCertResponse(  const uint8_t * pResponse,
                            size_t length,
                            char * pCertificateBuffer,
                            size_t * pCertificateBufferLength,
                            char * pCertificateIdBuffer,
                            size_t * pCertificateIdBufferLength,
                            char * pOwnershipTokenBuffer,
                            size_t * pOwnershipTokenBufferLength )
{
    assert( pResponse != NULL );
    assert( pCertificateBuffer != NULL );
    assert( pCertificateBufferLength != NULL );
    assert( pCertificateIdBuffer != NULL );
    assert( pCertificateIdBufferLength != NULL );
    assert( *pCertificateIdBufferLength >= 64 );
    assert( pOwnershipTokenBuffer != NULL );
    assert( pOwnershipTokenBufferLength != NULL );

    /* For details on the CreateKeysAndCertificate response payload format, see:
     * https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html#register-thing-response-payload
     */
    LogInfo( ( "Recieved JSON response: %s", pResponse ) );
    // cborRet = cbor_parser_init( pResponse, length, 0, &parser, &map );

    // if( cborRet != CborNoError )
    // {
    //     LogError( ( "Error initializing parser for CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    // }
    // else if( !cbor_value_is_map( &map ) )
    // {
    //     LogError( ( "CreateCertificateFromCsr response is not a valid map container type." ) );
    // }
    // else
    // {
    //     cborRet = cbor_value_map_find_value( &map, "certificatePem", &value );

    //     if( cborRet != CborNoError )
    //     {
    //         LogError( ( "Error searching CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    //     }
    //     else if( value.type == CborInvalidType )
    //     {
    //         LogError( ( "\"certificatePem\" not found in CreateCertificateFromCsr response." ) );
    //     }
    //     else if( value.type != CborTextStringType )
    //     {
    //         LogError( ( "Value for \"certificatePem\" key in CreateCertificateFromCsr response is not a text string type." ) );
    //     }
    //     else
    //     {
    //         cborRet = cbor_value_copy_text_string( &value, pCertificateBuffer, pCertificateBufferLength, NULL );

    //         if( cborRet == CborErrorOutOfMemory )
    //         {
    //             size_t requiredLen = 0;
    //             ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
    //             LogError( ( "Certificate buffer insufficiently large. Certificate length: %lu", ( unsigned long ) requiredLen ) );
    //         }
    //         else if( cborRet != CborNoError )
    //         {
    //             LogError( ( "Failed to parse \"certificatePem\" value from CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    //         }
    //     }
    // }

    // if( cborRet == CborNoError )
    // {
    //     cborRet = cbor_value_map_find_value( &map, "certificateId", &value );

    //     if( cborRet != CborNoError )
    //     {
    //         LogError( ( "Error searching CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    //     }
    //     else if( value.type == CborInvalidType )
    //     {
    //         LogError( ( "\"certificateId\" not found in CreateCertificateFromCsr response." ) );
    //     }
    //     else if( value.type != CborTextStringType )
    //     {
    //         LogError( ( "\"certificateId\" is an unexpected type in CreateCertificateFromCsr response." ) );
    //     }
    //     else
    //     {
    //         cborRet = cbor_value_copy_text_string( &value, pCertificateIdBuffer, pCertificateIdBufferLength, NULL );

    //         if( cborRet == CborErrorOutOfMemory )
    //         {
    //             size_t requiredLen = 0;
    //             ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
    //             LogError( ( "Certificate ID buffer insufficiently large. Certificate ID length: %lu", ( unsigned long ) requiredLen ) );
    //         }
    //         else if( cborRet != CborNoError )
    //         {
    //             LogError( ( "Failed to parse \"certificateId\" value from CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    //         }
    //     }
    // }

    // if( cborRet == CborNoError )
    // {
    //     cborRet = cbor_value_map_find_value( &map, "certificateOwnershipToken", &value );

    //     if( cborRet != CborNoError )
    //     {
    //         LogError( ( "Error searching CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    //     }
    //     else if( value.type == CborInvalidType )
    //     {
    //         LogError( ( "\"certificateOwnershipToken\" not found in CreateCertificateFromCsr response." ) );
    //     }
    //     else if( value.type != CborTextStringType )
    //     {
    //         LogError( ( "\"certificateOwnershipToken\" is an unexpected type in CreateCertificateFromCsr response." ) );
    //     }
    //     else
    //     {
    //         cborRet = cbor_value_copy_text_string( &value, pOwnershipTokenBuffer, pOwnershipTokenBufferLength, NULL );

    //         if( cborRet == CborErrorOutOfMemory )
    //         {
    //             size_t requiredLen = 0;
    //             ( void ) cbor_value_calculate_string_length( &value, &requiredLen );
    //             LogError( ( "Certificate ownership token buffer insufficiently large. Certificate ownership token buffer length: %lu", ( unsigned long ) requiredLen ) );
    //         }
    //         else if( cborRet != CborNoError )
    //         {
    //             LogError( ( "Failed to parse \"certificateOwnershipToken\" value from CreateCertificateFromCsr response: %s.", cbor_error_string( cborRet ) ) );
    //         }
    //     }
    // }

    return( 1 );
}
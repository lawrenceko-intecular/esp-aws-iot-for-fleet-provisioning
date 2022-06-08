/**
 * This file declares functions for serializing and parsing JSON encoded Fleet
 * Provisioning API payloads.
 */

/* Standard includes. */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Creates the request payload to be published to the RegisterThing API
 * in order to activate the provisioned certificate and receive a Thing name.
 *
 * @param[in] pBuffer Buffer into which to write the publish request payload.
 * @param[in] bufferLength Length of #buffer.
 * @param[in] pCertificateOwnershipToken The certificate's certificate
 * ownership token.
 * @param[in] certificateOwnershipTokenLength Length of
 * #certificateOwnershipToken.
 * @param[out] pOutLengthWritten The length of the publish request payload.
 */
bool generateRegisterThingRequest( uint8_t * pBuffer,
                                   size_t bufferLength,
                                   const char * pCertificateOwnershipToken,
                                   size_t certificateOwnershipTokenLength,
                                   const char * pSerial,
                                   size_t serialLength,
                                   size_t * pOutLengthWritten );

/**
 * @brief Extracts the certificate, certificate ID, and certificate ownership
 * token from a CreateKeysAndCertificate accepted response. These are copied
 * to the provided buffers so that they can outlive the data in the response
 * buffer and as CBOR strings may be chunked.
 *
 * @param[in] pResponse The response payload.
 * @param[in] length Length of #pResponse.
 * @param[in] pCertificateBuffer The buffer to which to write the certificate.
 * @param[in,out] pCertificateBufferLength The length of #pCertificateBuffer.
 * The length written is output here.
 * @param[in] pCertificateIdBuffer The buffer to which to write the certificate
 * ID.
 * @param[in,out] pCertificateIdBufferLength The length of
 * #pCertificateIdBuffer. The length written is output here.
 * @param[in] pOwnershipTokenBuffer The buffer to which to write the
 * certificate ownership token.
 * @param[in,out] pOwnershipTokenBufferLength The length of
 * #pOwnershipTokenBuffer. The length written is output here.
 * @param[in] pPrivateKeyBuffer The buffer to which to write the
 * certificate private key.
 * @param[in,out] pPrivateKeyBufferLength The length of
 * #pPrivateKeyBuffer. The length written is output here.
 */
bool parseKeyCertResponse(  const uint8_t * pResponse,
                            size_t length,
                            char * pCertificateBuffer,
                            size_t * pCertificateBufferLength,
                            char * pCertificateIdBuffer,
                            size_t * pCertificateIdBufferLength,
                            char * pOwnershipTokenBuffer,
                            size_t * pOwnershipTokenBufferLength,
                            char * pPrivateKeyBuffer,
                            size_t * pPrivateKeyBufferLength );

/**
 * @brief Extracts the Thing name from a RegisterThing accepted response.
 *
 * @param[in] pResponse The response document.
 * @param[in] length Length of #pResponse.
 * @param[in] pThingNameBuffer The buffer to which to write the Thing name.
 * @param[in,out] pThingNameBufferLength The length of #pThingNameBuffer. The
 * written length is output here.
 */
bool parseRegisterThingResponse( const uint8_t * pResponse,
                                 size_t length,
                                 char * pThingNameBuffer,
                                 size_t * pThingNameBufferLength );
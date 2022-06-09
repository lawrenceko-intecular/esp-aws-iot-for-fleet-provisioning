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
 * @file ota_demo_core_mqtt.c
 * @brief OTA update example using coreMQTT.
 */

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

/* Include Demo Config as the first non-system header. */
#include "demo_config.h"

/* OpenSSL sockets transport implementation. */
#include "network_transport.h"

/* Clock for timer. */
#include "clock.h"

/* pthread include. */
#include <pthread.h>
#include "semaphore.h"
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_pthread.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"


#ifndef ROOT_CA_CERT_PATH
    extern const char root_cert_auth_pem_start[]   asm("_binary_root_cert_auth_pem_start");
    extern const char root_cert_auth_pem_end[]   asm("_binary_root_cert_auth_pem_end");
#endif
#ifndef CLIENT_CERT_PATH
    extern const char client_cert_pem_start[] asm("_binary_client_crt_start");
    extern const char client_cert_pem_end[] asm("_binary_client_crt_end");
#endif
#ifndef CLIENT_PRIVATE_KEY_PATH
    extern const char client_key_pem_start[] asm("_binary_client_key_start");
    extern const char client_key_pem_end[] asm("_binary_client_key_end");
#endif

extern const char pcAwsCodeSigningCertPem[] asm("_binary_aws_codesign_crt_start");

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

int aws_iot_demo_main( int argc, char ** argv );

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
    ( void ) argc;
    ( void ) argv;

    /* Return error status. */
    int returnStatus = EXIT_SUCCESS;

    return returnStatus;
}

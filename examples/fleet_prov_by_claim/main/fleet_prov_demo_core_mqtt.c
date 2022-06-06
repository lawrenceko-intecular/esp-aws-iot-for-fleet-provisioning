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
 * @file fleet_prov_demo_core_mqtt.c
 * @brief Fleet Provisioning by Claim example using coreMQTT.
 */

/* Standard includes. */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* POSIX includes. */
#include <unistd.h>
#include <errno.h>

/* Include Demo Config as the first non-system header. */
#include "demo_config.h"

/* corePKCS11 includes. */
#include "core_pkcs11.h"
#include "core_pkcs11_config.h"

/* AWS IoT Fleet Provisioning Library. */
#include "fleet_provisioning.h"

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

    
    return 1;
}

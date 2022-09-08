/* thing-shadow example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "shadow_demo_helpers.h"

int aws_iot_demo_main( int argc, char ** argv );

#include "esp_log.h"

static const char *TAG = "SHADOW_EXAMPLE";

/*
 * Prototypes for the demos that can be started from this project.  Note the
 * Shadow demo is not actually started until the network is already.
 */

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* --- Reading CERT and KEY from NVS --- */
    nvs_handle_t my_handle;
    esp_err_t nvs_err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (nvs_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(nvs_err));
    } 

    // Read
    ESP_LOGI(TAG, "Reading content from NVS...");

    size_t required_cert_size;
    size_t required_certID_size;
    size_t required_token_size;
    size_t required_key_size;
    nvs_err = nvs_get_str(my_handle, "aws_cert", NULL, &required_cert_size);

    if (nvs_err != ESP_OK)
    {
        if (nvs_err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGI(TAG, "CERT not found in NVS, proceeding with Fleet Provisioning...");
        } else {
            ESP_LOGE(TAG, "Error (%s) reading!\n", esp_err_to_name(nvs_err));
        }
        provisioned = false;
    } else if (nvs_err == ESP_OK)
    {
        nvs_err = nvs_get_str(my_handle, "aws_key", NULL, &required_key_size);
    }
    
    switch (nvs_err)
    {
    case ESP_OK:
        ESP_LOGI(TAG, "CERT and KEY found in NVS.");
        provisioned = true;
        break;
    case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGI(TAG, "CERT and KEY not found in NVS, proceeding with Fleet Provisioning...");
        provisioned = false;
        break;

    default:
        ESP_LOGE(TAG, "Error (%s) reading!\n", esp_err_to_name(nvs_err));
        provisioned = false;
        break;
    }
    
    if (provisioned)
    {
        char* aws_cert = malloc(required_cert_size);
        char* aws_key = malloc(required_key_size);
        nvs_get_str(my_handle, "aws_cert", aws_cert, &required_cert_size);
        nvs_get_str(my_handle, "aws_key", aws_key, &required_key_size);

        // Storing into global variable for AWS IoT use (Note: in production, either store in PKCS11 or save as local variable)
        provisioned_cert = aws_cert;
        provisioned_privatekey = aws_key;

        ESP_LOGI(TAG, "CERT read from NVS: %s", aws_cert);
        ESP_LOGI(TAG, "KEY read from NVS: %s", aws_key);
    }
    nvs_close(my_handle);
    /* --- End of reading CERT and KEY from NVS --- */
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    aws_iot_demo_main(0,NULL);
}

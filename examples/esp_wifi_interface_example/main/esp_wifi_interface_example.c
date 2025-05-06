/*
Copyright (c) 2025 Tulio Carvalho
Licensed under the MIT License. See LICENSE file for details.
*/

#include <stdio.h>
#include "esp_wifi_interface.h"

#define LED_STATUS 2

void app_main(void)
{
    esp_wifi_interface_config_t wifi_inteface_config = {
        .channel = 1, // Access point channel
        .esp_max_retry = 10, // Maximum number of retries to connect to the AP         
        .wifi_sae_mode = WPA3_SAE_PWE_BOTH, // SAE mode for WPA3
        .esp_wifi_scan_auth_mode_treshold = WIFI_AUTH_WPA_WPA2_PSK, // Authentication mode threshold for Wi-Fi scan
        .status_io = LED_STATUS, // Connection status. 
    };
    
    WiFiInit (&wifi_inteface_config);

    WiFiSimpleConnection();
    

}

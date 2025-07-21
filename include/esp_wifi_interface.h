#ifndef _esp_wifi_interface_H_
#define _esp_wifi_interface_H_

#include <stdio.h>
#include "esp_check.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "esp_nvs.h"

typedef struct esp_wifi_interface_t *esp_wifi_interface_handle_t;

typedef struct {
    uint8_t channel; // Access point channel
    uint8_t esp_max_retry; // Maximum number of retries to connect to the AP
    uint8_t wifi_sae_mode; // SAE mode for WPA3
    uint8_t esp_wifi_scan_auth_mode_treshold; // Authentication mode threshold for Wi-Fi scan
    gpio_num_t status_io;
    gpio_num_t reset_io;
  
} esp_wifi_interface_config_t;

esp_err_t WiFiInit (esp_wifi_interface_config_t *config);

void WiFiDeinit ();

void WiFiSimpleConnection();

void esp_wifi_check_reset_button();

#endif



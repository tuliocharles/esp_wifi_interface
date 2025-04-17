#ifndef _esp_wifi_interface_H_
#define _esp_wifi_interface_H_

#include <stdio.h>
#include "esp_check.h"

typedef struct esp_wifi_interface_t *esp_wifi_interface_handle_t;

typedef struct {
    char ssid[32]; // SSID do ponto de acesso
    char password[64]; // Senha do ponto de acesso
    uint8_t channel; // Canal do ponto de acesso
} esp_wifi_interface_config_t;

esp_err_t WiFiInit (esp_wifi_interface_config_t *config, esp_wifi_interface_handle_t *handle);
void WiFiDeinit (esp_wifi_interface_handle_t *handle);

void WiFiSimpleConnection(esp_wifi_interface_handle_t handle);


#endif



#include <stdio.h>
#include "esp_wifi_interface.h"

#define ESP_WIFI_SSID      "gtpcarvalho3"
#define ESP_WIFI_PASS      "Gi70!Tu79?Pe06"

void app_main(void)
{
    esp_wifi_interface_config_t wifi_inteface_config = {
        .ssid = ESP_WIFI_SSID, // SSID do ponto de acesso
        .password = ESP_WIFI_PASS, // Senha do ponto de acesso
        .channel = 1 // Canal do ponto de acesso            
    };
    esp_wifi_interface_handle_t wifi_inteface_handle = NULL;

    WiFiInit (&wifi_inteface_config, &wifi_inteface_handle);
    
    WiFiSimpleConnection(wifi_inteface_handle);

    

}

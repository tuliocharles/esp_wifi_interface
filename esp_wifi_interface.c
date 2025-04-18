/* 
Descrição: Componente para conexão Wifi de forma simples e direta

Conecção depende do SSID e Senha armazenado em sdconfig.
inputs: Não possui.
outputs: Não possui. 
O programa não continua caso a conexão não estabeleça. 

autor: Túlio Carvalho

     * This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
*/

#include "esp_wifi_interface.h"

#include "esp_log.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_nvs.h" //component from namespace: tuliocharles/
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_err.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#include "esp_wifi.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static const char *tag_wifi = "WiFi";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define EXAMPLE_ESP_MAXIMUM_RETRY  5
//#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
//#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
//#define EXAMPLE_H2E_IDENTIFIER ""
//#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
//#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
//#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
//#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH 
#define EXAMPLE_H2E_IDENTIFIER ""//CONFIG_ESP_WIFI_PW_ID
//#endif
//#if CONFIG_ESP_WIFI_AUTH_OPEN
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
//#elif CONFIG_ESP_WIFI_AUTH_WEP
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
//#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
//#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK 
//#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
//#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
//#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
//#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
//#endif

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

typedef struct esp_wifi_interface_t esp_wifi_interface_t; 

struct esp_wifi_interface_t{
    uint8_t ssid[32]; // SSID do ponto de acesso
    uint8_t password[64]; // Senha do ponto de acesso
    uint8_t channel; // Canal do ponto de acesso
    esp_nvs_handle_t nvs_handle; // Handle para o NVS
};

static const char *TAG = "wifi station";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


esp_err_t WiFiInit (esp_wifi_interface_config_t *config, esp_wifi_interface_handle_t *handle){

    esp_err_t ret = ESP_OK;
    esp_wifi_interface_t *wifi_interface = NULL;
    ESP_LOGI(tag_wifi, "[APP] WiFiInit..");

    ESP_GOTO_ON_FALSE(config && handle, ESP_ERR_INVALID_ARG, err, tag_wifi, "[APP] Invalid argument");
    ESP_LOGI(tag_wifi, "[APP] Configuração válida.");

    wifi_interface = calloc(1, sizeof(esp_wifi_interface_t));

    // Copiar o SSID
    memcpy(wifi_interface->ssid, config->ssid, sizeof(wifi_interface->ssid));

    // Copiar a senha
    memcpy(wifi_interface->password, config->password, sizeof(wifi_interface->password));

    wifi_interface->channel = config->channel;

    *handle = wifi_interface;
    ESP_LOGI(tag_wifi, "[APP] Configuração WiFi criada com sucesso.");
    
    esp_nvs_config_t esp_nvs_config = {
        .name_space = "wifi_nvs",
        .key = "SSD1",
        .value_size = 64,
    };
    
    init_esp_nvs(&esp_nvs_config, &wifi_interface->nvs_handle);
    ESP_LOGI(tag_wifi, "[APP] Configuração NVS criada com sucesso.");
   
    return ESP_OK;
err:
    ESP_LOGE(tag_wifi, "[APP] Falha ao criar configuração WiFi.");
    if (wifi_interface) {
        free(wifi_interface);
        wifi_interface = NULL;
    }
    return ret;
}


void WiFiSimpleConnection(esp_wifi_interface_handle_t handle)
{
    /*****************************************************
     * Conexão WiFi da forma mais simples boierplate 
     *  
    ****************************************************/ 

    ESP_LOGI(tag_wifi, "[APP] Startup..");
    ESP_LOGI(tag_wifi, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(tag_wifi, "[APP] IDF version: %s", esp_get_idf_version());

    
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
        
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            //.ssid = handle->ssid,
            //.password = handle->password,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    memcpy(wifi_config.sta.ssid, handle->ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, handle->password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 handle->ssid, handle->password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
            handle->ssid, handle->password);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

}

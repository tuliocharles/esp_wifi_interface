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





#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>


#include "protocol_examples_utils.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>

#include "esp_tls.h"
#include "esp_check.h"

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN (64)

static const char *tag_wifi = "WiFi";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define EXAMPLE_ESP_MAXIMUM_RETRY 5
// #if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
// #define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
// #define EXAMPLE_H2E_IDENTIFIER ""
// #elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
// #define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
// #define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
// #elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER "" // CONFIG_ESP_WIFI_PW_ID
// #endif
// #if CONFIG_ESP_WIFI_AUTH_OPEN
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
// #elif CONFIG_ESP_WIFI_AUTH_WEP
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
// #elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
// #elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
// #define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
// #endif

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef struct esp_wifi_interface_t esp_wifi_interface_t;

struct esp_wifi_interface_t
{
    uint8_t ssid[32];            // SSID do ponto de acesso
    uint8_t password[64];        // Senha do ponto de acesso
    uint8_t channel;             // Canal do ponto de acesso
    esp_nvs_handle_t nvs_handle; // Handle para o NVS
};

static int s_retry_num = 0;

/* An HTTP GET handler */
static esp_err_t getssid_get_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, tag_wifi, "buffer alloc failed");
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(tag_wifi, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, tag_wifi, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(tag_wifi, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, tag_wifi, "buffer alloc failed");
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(tag_wifi, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, tag_wifi, "buffer alloc failed");
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(tag_wifi, "Found URL query => %s", buf);
            char param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN], dec_param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN] = {0};
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK)
            {
                ESP_LOGI(tag_wifi, "Found URL query parameter => query1=%s", param);
                example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(tag_wifi, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK)
            {
                ESP_LOGI(tag_wifi, "Found URL query parameter => query3=%s", param);
                example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(tag_wifi, "Decoded query parameter => %s", dec_param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK)
            {
                ESP_LOGI(tag_wifi, "Found URL query parameter => query2=%s", param);
                example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(tag_wifi, "Decoded query parameter => %s", dec_param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    static char wifi_form_html[] =
        "<!DOCTYPE html>"
        "<html><body>"
        "<h2>Configurar Wi Fi</h2>"
        "<form action=\"/savessid\" method=\"post\">"
        "SSID: <input name=\"ssid\" type=\"text\"><br>"
        "Senha: <input name=\"password\" type=\"password\"><br>"
        "<button type=\"submit\">Enviar</button>"
        "</form>"
        "</body></html>";

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char *resp_str = (const char *)wifi_form_html;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0)
    {
        ESP_LOGI(tag_wifi, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t getssid = {
    .uri = "/getssid",
    .method = HTTP_GET,
    .handler = getssid_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = NULL};

/* An HTTP POST handler */
static esp_err_t savessid_post_handler(httpd_req_t *req)
{
    char buf[1000];
    int ret, remaining = req->content_len;

    while (remaining > 0)
    {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                                  MIN(remaining, sizeof(buf)))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        /* Send back the same data */
        httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;

        /* Log data received */
        ESP_LOGI(tag_wifi, "=========== RECEIVED DATA ==========");
        ESP_LOGI(tag_wifi, "%.*s", ret, buf);
        ESP_LOGI(tag_wifi, "====================================");
        // salva na memória
    }

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t savessid = {
    .uri = "/savessid",
    .method = HTTP_POST,
    .handler = savessid_post_handler,
    .user_ctx = NULL};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/getssid", req->uri) == 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/getssid URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    }
    else if (strcmp("/savessid", req->uri) == 0)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/savessid URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // config.max_uri_len     = 512;    // padrão 256 :contentReference[oaicite:9]{index=9}
    //  config.max_req_hdr_len = 1024;   // padrão 512 :contentReference[oaicite:10]{index=10}

    // Start the httpd server
    ESP_LOGI(tag_wifi, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(tag_wifi, "Registering URI handlers");
        httpd_register_uri_handler(server, &getssid);
        httpd_register_uri_handler(server, &savessid);
        return server;
    }

    ESP_LOGI(tag_wifi, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

// inicio dos Handlers relacionados a conexão com o WiFi

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(tag_wifi, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(tag_wifi, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(tag_wifi, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server)
    {
        ESP_LOGI(tag_wifi, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK)
        {
            *server = NULL;
        }
        else
        {
            ESP_LOGE(tag_wifi, "Failed to stop http server");
        }
    }
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL)
    {
        ESP_LOGI(tag_wifi, "Starting webserver");
        *server = start_webserver();
    }
}

// Fim dos Handlers relacionados a conexão com o WiFi

// inicio funções internas da biblioteca

// fim funções internas da biblioteca


// inicio funções públicas da biblioteca

esp_err_t WiFiInit(esp_wifi_interface_config_t *config, esp_wifi_interface_handle_t *handle)
{

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
    if (wifi_interface)
    {
        free(wifi_interface);
        wifi_interface = NULL;
    }
    return ret;
}

void WiFiSimpleConnection(esp_wifi_interface_handle_t handle)
{

    // Logs e event group
    ESP_LOGI(tag_wifi, "[APP] Startup..");
    ESP_LOGI(tag_wifi, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(tag_wifi, "[APP] IDF version: %s", esp_get_idf_version());

    s_wifi_event_group = xEventGroupCreate();

    // TCP/IP + event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // escolher entre criar netif para sta ou ap
    // esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // comentado para fazer scan

    esp_err_t ret = esp_wifi_start();
    printf("%s\n", esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

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

    esp_wifi_connect();
    ESP_LOGI(tag_wifi, "wifi_connect finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns after bits are set - so we can test if the bits we need are set */
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(tag_wifi, "connected to ap SSID:%s password:%s",
                 handle->ssid, handle->password);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(tag_wifi, "Failed to connect to SSID:%s, password:%s",
                 handle->ssid, handle->password);
    }
    else
    {
        ESP_LOGE(tag_wifi, "UNEXPECTED EVENT");
    }

    // start server quando for AP
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
    
    server = start_webserver();
    if (server == NULL)
    {
        ESP_LOGE(tag_wifi, "Failed to start webserver");
    }
    else
    {
        ESP_LOGI(tag_wifi, "Webserver started successfully");
    }
    while (server)
    {
        sleep(5);
    }
}

// fim funções públicas da biblioteca













/* Exemplo ainda perdido sobre scanear ssids*/

/*esp_wifi_scan_start(NULL, true);

ESP_LOGI(TAG, "Scan done");

uint16_t number = 5;
wifi_ap_record_t ap_info[5];
uint16_t ap_count = 0;
memset(ap_info, 0, sizeof(ap_info));

ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", number);
ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);
for (int i = 0; i < number; i++) {
    ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
    ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
    //print_auth_mode(ap_info[i].authmode);
    //if (ap_info[i].authmode != WIFI_AUTH_WEP) {
   //     print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
    //}
    //ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
}*/

// faz o levantamento de quantos ssid existem armazenado na nvs
/*const uint8_t ssid_mem_len = 6;
const uint8_t ssid_len = 32;
const uint8_t ssid_max = 5;
char newkey[ssid_mem_len];
char nvs_ssid[ssid_max][ssid_len];
for(int i = 0; i < 5; i++){
    char *ssid_ptr = NULL;
    sprintf(newkey,"SSID%d", i);
    esp_nvs_change_key(newkey, handle->nvs_handle);
    ESP_LOGI(TAG, "Key changed to %s", newkey);
    esp_err_t err = esp_nvs_read_string(handle->nvs_handle, &ssid_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read string: %s", esp_err_to_name(err));
        break;
    }
    memcpy(nvs_ssid[i], ssid_ptr, strlen(ssid_ptr)+1); //
    ESP_LOGI(TAG, "Read SSID: %s", nvs_ssid[i]);
}
if (strlen(nvs_ssid[0]) == 0) {
    ESP_LOGI(TAG, "SSID not found in NVS.");
} else {
    ESP_LOGI(TAG, "At least one SSID found in NVS.");
}
*/

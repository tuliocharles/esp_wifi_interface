/*
Copyright (c) 2025 Tulio Carvalho
Licensed under the MIT License. See LICENSE file for details.
*/

#include "esp_wifi_interface.h"

#include "esp_log.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_system.h"
#include "nvs_flash.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

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

#include <ctype.h>

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN (64)
#define SSID_PA "COIIOTE"
#define SSID_PASS_PA "coiiote123"
#define EXAMPLE_H2E_IDENTIFIER "" // CONFIG_ESP_WIFI_PW_ID
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef enum
{
    sta,
    ap
} wifi_typemode_t;

typedef struct esp_wifi_interface_t esp_wifi_interface_t;

struct esp_wifi_interface_t
{
    uint8_t ssid[32];                         // name of the access point
    uint8_t password[64];                     // password of the access point
    uint8_t channel;                          // channel of the access point
    wifi_typemode_t wifi_mode;                // mode of the access point
    esp_nvs_handle_t nvs_handle;              // NVS handle
    char local_ip[16];                        // local IP address
    httpd_handle_t server;                    // Handle off the web server
    uint8_t esp_max_retry;                    // maximum number of retries to connect to the AP
    uint8_t s_retry_num;                      // Number of attempts to connect to the AP
    uint8_t wifi_sae_mode;                    // SAE mode for WPA3
    uint8_t esp_wifi_scan_auth_mode_treshold; // Authentication mode threshold for Wi-Fi scan
    gpio_num_t status_io;
    gpio_num_t reset_io;
};

static esp_wifi_interface_handle_t wifi_interface_handle = NULL;
static const char *tag_wifi = "WiFi";
static EventGroupHandle_t s_wifi_event_group; // FreeRTOS event group to signal when we are connected

// convert a hex digit to its integer value
static char from_hex(char ch)
{
    if (isdigit((unsigned char)ch))
        return ch - '0';
    if (isupper((unsigned char)ch))
        return ch - 'A' + 10;
    return ch - 'a' + 10;
}

// decod percent‑encoding (URL) acording to RFC 3986
// https://datatracker.ietf.org/doc/html/rfc3986#section-2.1
static void url_decode(char *dst, const char *src)
{
    while (*src)
    {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2]))
        {
            *dst++ = from_hex(src[1]) << 4 | from_hex(src[2]);
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

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


    static char wifi_form_html[2048];
    
    sprintf(wifi_form_html,
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<style>"
        "body {  margin: 0;  padding: 0;  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;  background: linear-gradient(135deg, #e0eafc, #cfdef3);  display: flex;  flex-direction: column;  align-items: center;}"
        ".container {  text-align: center;  background: white;  padding: 40px 60px;  border-radius: 16px;  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1);}"
        "form-group {  margin: 10px 0;  width: 100%%;  max-width: 400px;  text-align: left; }"
        "input {  width: 100%%;  padding: 10px;  font-size: 1rem;  margin-top: 5px;  border: 1px solid #ccc;  border-radius: 4px;}"
        "button {  width: 100%%;  padding: 12px;  background-color: #007bff;  color: white;  border: none;  border-radius: 4px;  font-size: 1rem;  cursor: pointer;  margin-top: 10px; }"        
        "</style>"
        "</head>"
        "<body>"
        "<div class=\"container\">"
        "<h2>Wi-Fi</h2>"
        "<form action=\"/savessid\" method=\"post\">"
        "SSID: <input name=\"ssid\" type=\"text\"> <br>"
        "Password: <input name=\"password\" type=\"password\"><br>"
        "<button type=\"submit\">Enviar</button>"
        "</form>"
        "</div>"
        "</body></html>");

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

    void *ctx = httpd_get_global_user_ctx(req->handle);
    esp_wifi_interface_handle_t handle = (esp_wifi_interface_handle_t)ctx;

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

        char *saveptr1, *saveptr2;
        char *str = strndup(buf, ret);
        char *pair = strtok_r(str, "&", &saveptr1);
        while (pair)
        {
            char *key = strtok_r(pair, "=", &saveptr2);
            char *value = strtok_r(NULL, "=", &saveptr2);
            printf("Chave: %s, Valor: %s\n", key, value);
            pair = strtok_r(NULL, "&", &saveptr1);
            // if key == "ssid" or "password"
            if (strcmp(key, "ssid") == 0)
            {
                // salva o valor no ssid do handle

                esp_nvs_change_key("SSID", handle->nvs_handle);
                char ssid[100];
                url_decode(ssid, value);
                esp_nvs_write_string(ssid, handle->nvs_handle);
            }
            else if (strcmp(key, "password") == 0)
            {
                // salva o valor na senha do handle
                esp_nvs_change_key("PASS", handle->nvs_handle);
                char pass[100];
                url_decode(pass, value);
                esp_nvs_write_string(pass, handle->nvs_handle);
            }
            else
            {
                ESP_LOGI(tag_wifi, "Chave não reconhecida: %s", key);

            }
            
        }
        free(str);

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




static httpd_handle_t start_webserver(esp_wifi_interface_handle_t handle1)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    esp_wifi_interface_handle_t handle = (esp_wifi_interface_handle_t)handle1;
    config.global_user_ctx = handle;

    printf("handle: %p\n", handle);
    printf("handle->nvs_handle webserver: %p\n", handle->nvs_handle);
    printf("handle->ssid webserver: %s\n", handle->ssid);
    printf("handle->password webserver: %s\n", handle->password);

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

static void esp_wifi_restart()
{
    esp_wifi_stop();
    esp_wifi_deinit();
    stop_webserver(wifi_interface_handle->server);
    wifi_interface_handle->server = NULL;
    fflush(stdout);
    esp_restart();
}

static void esp_wifi_forget()
{
    esp_nvs_change_key("SSID", wifi_interface_handle->nvs_handle);
    esp_nvs_write_string("empty", wifi_interface_handle->nvs_handle);
    esp_nvs_change_key("PASS", wifi_interface_handle->nvs_handle);
    esp_nvs_write_string("empty", wifi_interface_handle->nvs_handle);
}

void esp_wifi_check_reset_button()
{
    int ret = 0;
    ret = gpio_get_level(wifi_interface_handle->reset_io);
    if (ret == 0)
    {
        esp_wifi_forget();
        esp_wifi_restart();
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {

        if (wifi_interface_handle->s_retry_num < wifi_interface_handle->esp_max_retry)
        {
            esp_wifi_connect();
            wifi_interface_handle->s_retry_num++;

            ESP_LOGI(tag_wifi, "retry to connect to the AP");
            gpio_set_level(wifi_interface_handle->status_io, wifi_interface_handle->s_retry_num % 2);
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            gpio_set_level(wifi_interface_handle->status_io, 0);
        }
        ESP_LOGI(tag_wifi, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(tag_wifi, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_interface_handle->s_retry_num = 0;
        sprintf(wifi_interface_handle->local_ip, IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        gpio_set_level(wifi_interface_handle->status_io, 1);
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
    esp_wifi_interface_handle_t handle = (esp_wifi_interface_handle_t)arg;
    // httpd_handle_t *server = (httpd_handle_t *)arg;
    printf("handle no connect: %p\n", handle);
    if (handle->server == NULL)
    {
        ESP_LOGI(tag_wifi, "Starting webserver");
        handle->server = start_webserver(handle);
    }
}

esp_err_t WiFiInit(esp_wifi_interface_config_t *config)
{

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(tag_wifi, "Reset reason: %d", reason);

    esp_err_t ret = ESP_OK;
    esp_wifi_interface_t *wifi_interface = NULL;
    ESP_LOGI(tag_wifi, "WiFiInit..");

    ESP_GOTO_ON_FALSE(config, ESP_ERR_INVALID_ARG, err, tag_wifi, "Invalid argument");
    ESP_LOGI(tag_wifi, "Configuration done");

    wifi_interface = calloc(1, sizeof(esp_wifi_interface_t));

    wifi_interface->channel = config->channel;
    wifi_interface->esp_max_retry = config->esp_max_retry;
    wifi_interface->s_retry_num = 0;
    wifi_interface->wifi_sae_mode = config->wifi_sae_mode;
    wifi_interface->status_io = config->status_io;
    wifi_interface->reset_io = config->reset_io;

    // Gpio configuration
    uint64_t gpio_pin_sel = (1ULL << wifi_interface->status_io);

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = gpio_pin_sel;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_pin_sel = (1ULL << wifi_interface->reset_io);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = gpio_pin_sel;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    esp_nvs_config_t esp_nvs_config = {
        .name_space = "wifi_nvs",
        .key = "SSID",
        .value_size = 64,
    };

    init_esp_nvs(&esp_nvs_config, &wifi_interface->nvs_handle);
    ESP_LOGI(tag_wifi, "NVS Created Successfully");

    char *p_ssid = NULL;
    esp_err_t ret_nvs = esp_nvs_read_string(wifi_interface->nvs_handle, &p_ssid);

    if (ret_nvs == ESP_ERR_NVS_NOT_FOUND)
    {
        esp_nvs_change_key("SSID", wifi_interface->nvs_handle);
        ESP_LOGI(tag_wifi, "Key changed to SSID");
        esp_nvs_write_string("empty", wifi_interface->nvs_handle);

        esp_nvs_change_key("PASS", wifi_interface->nvs_handle);
        ESP_LOGI(tag_wifi, "key changed to PASS");
        esp_nvs_write_string("empty", wifi_interface->nvs_handle);
    }

    esp_nvs_change_key("SSID", wifi_interface->nvs_handle);
    ESP_LOGI(tag_wifi, "Key changed to SSID");

    if (esp_nvs_read_string(wifi_interface->nvs_handle, &p_ssid) != ESP_OK)
    {
        ESP_LOGI(tag_wifi, "Error to read SSID");
    }

    if (strcmp(p_ssid, "empty") == 0)
    {
        wifi_interface->wifi_mode = ap; // modo AP
        ESP_LOGI(tag_wifi, "AP mode Activeted");
    }
    else
    {
        wifi_interface->wifi_mode = sta; // modo STA
        ESP_LOGI(tag_wifi, "STA Mode activated");
        // Copiar o SSID
        memcpy(wifi_interface->ssid, p_ssid, strlen(p_ssid) + 1);

        char *p_password = NULL;
        esp_nvs_change_key("PASS", wifi_interface->nvs_handle);
        ESP_LOGI(tag_wifi, "Key changed to PASS");
        if (esp_nvs_read_string(wifi_interface->nvs_handle, &p_password) != ESP_OK)
        {
            ESP_LOGE(tag_wifi, "Error to read Password");
        }
        // Copiar a senha
        memcpy(wifi_interface->password, p_password, strlen(p_password) + 1);
    }

    esp_nvs_list_namespaces();

    wifi_interface_handle = wifi_interface;
    ESP_LOGI(tag_wifi, "Configuration done");

    return ESP_OK;
err:
    ESP_LOGE(tag_wifi, "Error to Conifgure");
    if (wifi_interface)
    {
        free(wifi_interface);
        wifi_interface = NULL;
    }
    return ret;
}

void WiFiSimpleConnection()
{
    // wifi_typemode_t WifiMode = ap; // escolher entre AP ou STA
    // Logs e event group bu
    ESP_LOGI(tag_wifi, "[APP] Startup..");
    ESP_LOGI(tag_wifi, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(tag_wifi, "[APP] IDF version: %s", esp_get_idf_version());

    s_wifi_event_group = xEventGroupCreate();

    // TCP/IP + event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // escolher entre criar netif para sta ou ap
    // esp_netif_create_default_wifi_ap();
    if (wifi_interface_handle->wifi_mode == sta)
    {
        esp_netif_create_default_wifi_sta();
    }
    else if (wifi_interface_handle->wifi_mode == ap)
    {
        esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config_sta = {
        .sta = {
            .threshold.authmode = wifi_interface_handle->esp_wifi_scan_auth_mode_treshold,
            .sae_pwe_h2e = wifi_interface_handle->wifi_sae_mode,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },

    };

    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = SSID_PA,
            .ssid_len = strlen(SSID_PA),
            .channel = 1,
            .password = SSID_PASS_PA,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    if (wifi_interface_handle->wifi_mode == ap)
    {
        // COLOCAR AS DIFERENÇAS DE CONFIGARAÇÕES

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap)); // comentado para fazer scan
    }
    else if (wifi_interface_handle->wifi_mode == sta)
    {
        // COLOCAR AS DIFERENÇAS DE CONFIGURAÇÃO
        memcpy(wifi_config_sta.sta.ssid, wifi_interface_handle->ssid, sizeof(wifi_config_sta.sta.ssid));
        memcpy(wifi_config_sta.sta.password, wifi_interface_handle->password, sizeof(wifi_config_sta.sta.password));

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta)); // comentado para fazer scan
    }

    esp_err_t ret = esp_wifi_start();

    printf("%s\n", esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    if (wifi_interface_handle->wifi_mode == sta)
    {
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
                     wifi_interface_handle->ssid, wifi_interface_handle->password);
        }
        else if (bits & WIFI_FAIL_BIT)
        {
            ESP_LOGI(tag_wifi, "Failed to connect to SSID:%s, password:%s",
                     wifi_interface_handle->ssid, wifi_interface_handle->password);

            esp_wifi_forget();
            esp_wifi_restart();
        }
        else
        {
            ESP_LOGE(tag_wifi, "UNEXPECTED EVENT");
        }
    }

    // ESSA PARTE VAI SER SÓ PARA AP... MAS POR ENQUANTO VOU DEIXAR ABERTO
    // start server quando for AP
    if (wifi_interface_handle->wifi_mode == ap)
    {
        wifi_interface_handle->server = NULL;

        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, wifi_interface_handle));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &wifi_interface_handle->server));

        wifi_interface_handle->server = start_webserver(wifi_interface_handle);
        if (wifi_interface_handle->server == NULL)
        {
            ESP_LOGE(tag_wifi, "Failed to start webserver");
        }
        else
        {
            ESP_LOGI(tag_wifi, "Webserver started successfully");
        }

        char *p_ssid = NULL;
        bool status_io_aux = false;
        while (wifi_interface_handle->server)
        {
            vTaskDelay(200 / portTICK_PERIOD_MS);
            status_io_aux = !status_io_aux;
            gpio_set_level(wifi_interface_handle->status_io, status_io_aux);
            esp_nvs_change_key("SSID", wifi_interface_handle->nvs_handle);
            if (esp_nvs_read_string(wifi_interface_handle->nvs_handle, &p_ssid) != ESP_OK)
            {
                ESP_LOGE(tag_wifi, "Falha ao ler o SSID %d", 0);
            }

            if (strcmp(p_ssid, "empty") != 0)
            {
                ESP_LOGI(tag_wifi, "New SSID entered, restarting... ");
                ESP_LOGI(tag_wifi, "SSID: %s", p_ssid);
                esp_wifi_restart();
            }
        }
    }

    
}

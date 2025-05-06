idf_component_register(SRCS "esp_wifi_interface.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES
                        nvs_flash
                        esp_event
                        esp_netif
                        esp_wifi
                        protocol_examples_common
                        esp-tls
                        esp_http_server
                        esp_driver_gpio)

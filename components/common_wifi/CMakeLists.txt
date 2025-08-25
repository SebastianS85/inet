idf_component_register(SRCS "common_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server  json esp_wifi log display nvs_flash esp_timer)

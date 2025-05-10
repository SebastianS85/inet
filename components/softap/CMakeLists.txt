
idf_component_register(SRCS "softap.c"
INCLUDE_DIRS "include"
                    REQUIRES esp_http_server  json esp_wifi log common_wifi)

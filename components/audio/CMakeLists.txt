idf_component_register(SRCS "audio.c"
                    INCLUDE_DIRS "include"
                    REQUIRES audio_hal audio_pipeline my_board log esp_http_client esp_peripherals audio_stream esp_http_server display common_wifi)

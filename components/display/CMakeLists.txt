idf_component_register(SRCS "display.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver i2c k0i05__esp_ssd1306 log)

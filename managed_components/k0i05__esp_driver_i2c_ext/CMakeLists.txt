idf_component_register(
    SRCS i2c_master_ext.c
    INCLUDE_DIRS include
    REQUIRES esp_driver_i2c esp_type_utils
)

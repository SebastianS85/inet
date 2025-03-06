# ESP-IDF I2C Component Extension
This ESP32 espressif IoT development framework (esp-idf) i2c component extension was developed to extend `i2c_master.h` i2c peripheral driver functionality.  This component is a dependency for all i2c peripheral components.  Information on features and functionality are documented and can be found in the `i2c_master_ext.h` header file.

## Repository
The component is hosted on github and is located here: https://github.com/K0I05/ESP32-S3_ESP-IDF_COMPONENTS/tree/main/components/peripherals/i2c/esp_driver_i2c_ext

## General Usage
To get started, simply copy the component to your project's `components` folder and reference the `i2c_master_ext.h` header file as an include.  The component includes documentation for the peripheral such as the datasheet, application notes, and/or user manual where applicable.

```
components
└── esp_driver_i2c_ext
    ├── CMakeLists.txt
    ├── README.md
    ├── LICENSE
    ├── idf_component.yml
    ├── library.json
    ├── documentation
    │   └── datasheets, etc.
    ├── include
    │   └── i2c_master_ext_version.h
    │   └── i2c_master_ext.h
    └── i2c_master_ext.c
```

## Basic Example
Once the component is referenced as an include, the extended i2c functions should be visible and available for usage.  The below example demonstrates an i2c `get` and `set` transaction which reads the register's contents and/or writes contents to the register.

```
#include <i2c_master_ext.h>

/* i2c read transaction: i2c_master_bus_read_uint8 function performs an i2c write and then read transaction  */
esp_err_t i2c_bmp280_get_control_measurement_register(i2c_bmp280_handle_t handle, i2c_bmp280_control_measurement_register_t *const reg) {
    /* validate arguments */
    ESP_ARG_CHECK( handle && reg );

    /* attempt i2c read transaction */
    ESP_RETURN_ON_ERROR( i2c_master_bus_read_uint8(handle->i2c_handle, I2C_BMP280_REG_CTRL, &reg->reg), TAG, "read control measurement register failed" );

    /* delay before next i2c transaction */
    vTaskDelay(pdMS_TO_TICKS(I2C_BMP280_CMD_DELAY_MS));

    return ESP_OK;
}

/* i2c read transaction: i2c_master_bus_write_uint8 function performs an i2c write transaction  */
esp_err_t i2c_bmp280_set_control_measurement_register(i2c_bmp280_handle_t handle, const i2c_bmp280_control_measurement_register_t reg) {
    /* validate arguments */
    ESP_ARG_CHECK( handle );

    /* attempt i2c write transaction */
    ESP_RETURN_ON_ERROR( i2c_master_bus_write_uint8(handle->i2c_handle, I2C_BMP280_REG_CTRL, reg.reg), TAG, "write control measurement register failed" );

    /* delay before next i2c transaction */
    vTaskDelay(pdMS_TO_TICKS(I2C_BMP280_CMD_DELAY_MS));

    return ESP_OK;
}
```



Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
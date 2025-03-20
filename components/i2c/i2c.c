#include <stdio.h>
#include "i2c.h"

#include "driver/i2c_master.h"



//.scl_io_num = GPIO_NUM_21,
//.sda_io_num = GPIO_NUM_20,
i2c_master_bus_handle_t i2c0_bus_hdl;
void i2c_init(){
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT, 
        .i2c_port = 0,  // I2C0
        .scl_io_num = GPIO_NUM_18,
        .sda_io_num = GPIO_NUM_17,
        .glitch_ignore_cnt = 7, 
        .flags.enable_internal_pullup = true
    };

    esp_err_t err = i2c_new_master_bus(&i2c_bus_config, &i2c0_bus_hdl);
    if (err != ESP_OK) {
        printf("Failed to initialize I2C: %s\n", esp_err_to_name(err));
    }
}
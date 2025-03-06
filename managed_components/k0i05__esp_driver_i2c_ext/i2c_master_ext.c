/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file i2c_master_ext.c
 *
 * ESP-IDF driver extension for i2c peripheral drivers
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
 *
 * MIT Licensed as described in the file LICENSE
 */

#include "include/i2c_master_ext.h"
#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_check.h>
//#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*
 * macro definitions
*/

#define ESP_ARG_CHECK(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)


/*
* static constant declarations
*/

static const char *TAG = "i2c_master_ext";


/*
* functions and subroutines
*/


esp_err_t i2c_master_bus_detect_devices(i2c_master_bus_handle_t handle) {
    const uint16_t probe_timeout_ms = 50; // timeout in milliseconds
    uint8_t address;

    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");

    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);

        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            
            address = i + j;

            esp_err_t ret = i2c_master_probe(handle, address, probe_timeout_ms);

            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }

    return ESP_OK; 
}

esp_err_t i2c_master_bus_read_uint8(i2c_master_dev_handle_t handle, const uint8_t reg_addr, uint8_t *const data) {
    const bit8_uint8_buffer_t tx = { reg_addr };
    bit8_uint8_buffer_t rx = { 0 };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, rx, BIT8_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_uint8 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint8 - rx[0] %02x", rx[0]);

    *data = rx[0];

    return ESP_OK;
}

esp_err_t i2c_master_bus_read_uint16(i2c_master_dev_handle_t handle, const uint8_t reg_addr, uint16_t *const data) {
    const bit8_uint8_buffer_t tx  = { reg_addr };
    bit16_uint8_buffer_t rx = { 0 };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, rx, BIT16_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_uint16 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint16 - rx[0] %02x | rx[1] %02x", rx[0], rx[1]);

    *data = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read_byte16(i2c_master_dev_handle_t handle, const uint8_t reg_addr, bit16_uint8_buffer_t *const data) {
    const bit8_uint8_buffer_t tx = { reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, *data, BIT16_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_byte16 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint16 - data[0] %02x | data[1] %02x", *data[0], *data[1]);

    return ESP_OK;   
}

esp_err_t i2c_master_bus_read_byte24(i2c_master_dev_handle_t handle, const uint8_t reg_addr, bit24_uint8_buffer_t *const data) {
    const bit8_uint8_buffer_t tx = { reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, *data, BIT24_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_byte24 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint24 - data[0] %02x | data[1] %02x | data[2] %02x", *data[0], *data[1], *data[2]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read16_byte24(i2c_master_dev_handle_t handle, const uint16_t reg_addr, bit24_uint8_buffer_t *const data) {
    const bytes_to_uint16_t tx = { .value = reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx.bytes, BIT16_UINT8_BUFFER_SIZE, *data, BIT24_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read16_byte24 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read16_uint24 - data[0] %02x | data[1] %02x | data[2] %02x", *data[0], *data[1], *data[2]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read_uint32(i2c_master_dev_handle_t handle, const uint8_t reg_addr, uint32_t *const data) {
    const bit8_uint8_buffer_t tx  = { reg_addr };
    bit32_uint8_buffer_t rx = { 0 };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, rx, BIT32_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_uint32 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint32 - rx[0] %02x | rx[1] %02x | rx[2] %02x | rx[3] %02x", rx[0], rx[1], rx[2], rx[3]);

    *data = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8) | ((uint32_t)rx[2] << 16) | ((uint32_t)rx[3] << 24);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read_byte32(i2c_master_dev_handle_t handle, const uint8_t reg_addr, bit32_uint8_buffer_t *const data) {
    const bit8_uint8_buffer_t tx = { reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, *data, BIT32_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_byte32 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint32 - rx[0] %02x | rx[1] %02x | rx[2] %02x | rx[3] %02x", *data[0], *data[1], *data[2], *data[3]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read_byte48(i2c_master_dev_handle_t handle, const uint8_t reg_addr, bit48_uint8_buffer_t *const data) {
    const bit8_uint8_buffer_t tx = { reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, *data, BIT48_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_byte48 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint48 - rx[0] %02x | rx[1] %02x | rx[2] %02x | rx[3] %02x | rx[4] %02x | rx[5] %02x", *data[0], *data[1], *data[2], *data[3], *data[4], *data[5]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read16_byte48(i2c_master_dev_handle_t handle, const uint16_t reg_addr, bit48_uint8_buffer_t *const data) {
    const bytes_to_uint16_t tx = { .value = reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx.bytes, BIT16_UINT8_BUFFER_SIZE, *data, BIT48_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read16_byte48 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read16_uint48 - rx[0] %02x | rx[1] %02x | rx[2] %02x | rx[3] %02x | rx[4] %02x | rx[5] %02x", *data[0], *data[1], *data[2], *data[3], *data[4], *data[5]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read_byte64(i2c_master_dev_handle_t handle, const uint8_t reg_addr, bit64_uint8_buffer_t *const data) {
    const bit8_uint8_buffer_t tx = { reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx, BIT8_UINT8_BUFFER_SIZE, *data, BIT64_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read_byte48 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read_uint64 - rx[0] %02x | rx[1] %02x | rx[2] %02x | rx[3] %02x | rx[4] %02x | rx[5] %02x | rx[6] %02x | rx[7] %02x", *data[0], *data[1], *data[2], *data[3], *data[4], *data[5], *data[6], *data[7]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_read16_byte64(i2c_master_dev_handle_t handle, const uint16_t reg_addr, bit64_uint8_buffer_t *const data) {
    const bytes_to_uint16_t tx = { .value = reg_addr };

    ESP_ARG_CHECK( handle && data ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit_receive(handle, tx.bytes, BIT16_UINT8_BUFFER_SIZE, *data, BIT64_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_read16_byte48 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_read16_uint64 - rx[0] %02x | rx[1] %02x | rx[2] %02x | rx[3] %02x | rx[4] %02x | rx[5] %02x | rx[6] %02x | rx[7] %02x", *data[0], *data[1], *data[2], *data[3], *data[4], *data[5], *data[6], *data[7]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_write_cmd(i2c_master_dev_handle_t handle, const uint8_t command) {
    const bit8_uint8_buffer_t tx = { command };

    ESP_ARG_CHECK( handle ); // ignore `command` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit(handle, tx, BIT8_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_write_cmd failed" );

    ESP_LOGD(TAG, "i2c_master_bus_write_cmd - tx[0] %02x", tx[0]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_write16_cmd(i2c_master_dev_handle_t handle, const uint16_t command) {
    const bytes_to_uint16_t tx = { .value = command };

    ESP_ARG_CHECK( handle ); // ignore `command` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit(handle, tx.bytes, BIT16_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_write16_cmd failed" );

    ESP_LOGD(TAG, "i2c_master_bus_write16_cmd - tx[0] %02x | tx[1] %02x ", tx.bytes[0], tx.bytes[1]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_write_uint8(i2c_master_dev_handle_t handle, const uint8_t reg_addr, const uint8_t data) {
    const bit16_uint8_buffer_t tx = { reg_addr, data };

    ESP_ARG_CHECK( handle ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit(handle, tx, BIT16_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_write_uint8 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_write_uint8 - tx[0] %02x | tx[1] %02x", tx[0], tx[1]);

    return ESP_OK;
}

esp_err_t i2c_master_bus_write_uint16(i2c_master_dev_handle_t handle, const uint8_t reg_addr, const uint16_t data) {
    const bit24_uint8_buffer_t tx = { reg_addr, (uint8_t)(data & 0xff), (uint8_t)((data >> 8) & 0xff) }; // register, lsb, msb

    ESP_ARG_CHECK( handle ); // ignore `reg_addr` given a range of 0x00 to 0xff is acceptable

    ESP_RETURN_ON_ERROR( i2c_master_transmit(handle, tx, BIT24_UINT8_BUFFER_SIZE, I2C_XFR_TIMEOUT_MS), TAG, "i2c_master_bus_write_uint16 failed" );

    ESP_LOGD(TAG, "i2c_master_bus_write_uint8 - tx[0] %02x | tx[1] %02x | tx[2] %02x", tx[0], tx[1], tx[2]);

    return ESP_OK;
}

const char* i2c_master_ext_get_fw_version(void) {
    return I2C_MASTER_EXT_FW_VERSION_STR;
}

int32_t i2c_master_ext_get_fw_version_number(void) {
    return I2C_MASTER_EXT_FW_VERSION_INT32;
}

# ESP-IDF Type Utilities
This ESP32 espressif IoT development framework (esp-idf) type utilities component was developed as a helper for byte manipulation and string functionality.  Information on features and functionality are documented and can be found in the `type_utils.h` header file.

## Repository
The component is hosted on github and is located here: https://github.com/K0I05/ESP32-S3_ESP-IDF_COMPONENTS/tree/main/components/utilities/esp_type_utils

## General Usage
To get started, simply copy the component to your project's `components` folder and reference the `type_utils.h` header file as an include.  The component includes documentation for the peripheral such as the datasheet, application notes, and/or user manual where applicable.

```
components
└── esp_type_utils
    ├── CMakeLists.txt
    ├── README.md
    ├── LICENSE
    ├── idf_component.yml
    ├── library.json
    ├── documentation
    │   └── datasheets, etc.
    ├── include
    │   └── type_utils_version.h
    │   └── type_utils.h
    └── type_utils.c
```

## Basic Example
Once the component is referenced as an include, the functions should be visible and available for usage.  The below example demonstrates an i2c transaction which reads the register's contents and writes the contents of the register as a binary string.

```
#include <type_utils.h>

/* declare device registers */
ltr390uv_control_register_t c_reg;
ltr390uv_interrupt_config_register_t ic_reg;
ltr390uv_measure_register_t m_reg;
ltr390uv_gain_register_t    g_reg;

/* attempt i2c read transaction from device registers */
ltr390uv_get_measure_register(dev_hdl, &m_reg);
ltr390uv_get_gain_register(dev_hdl, &g_reg);
ltr390uv_get_interrupt_config_register(dev_hdl, &ic_reg);
ltr390uv_get_control_register(dev_hdl, &c_reg);

/* print device register as a binary string */
ESP_LOGI(APP_TAG, "Control Register (0x%02x): %s", c_reg.reg, uint8_to_binary(c_reg.reg));
ESP_LOGI(APP_TAG, "Measure Register (0x%02x): %s", m_reg.reg, uint8_to_binary(m_reg.reg));
ESP_LOGI(APP_TAG, "Gain Register    (0x%02x): %s", g_reg.reg, uint8_to_binary(g_reg.reg));
ESP_LOGI(APP_TAG, "IRQ Cfg Register (0x%02x): %s", ic_reg.reg, uint8_to_binary(ic_reg.reg));


```



Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
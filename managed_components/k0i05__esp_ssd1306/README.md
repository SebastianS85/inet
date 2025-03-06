# Generic SSD1306 (128x32 and 128x64) OLED Display
This esp-idf driver was developed for generic SSD1306 OLED displays.  Information on features and functionality are documented and can be found in the `ssd1306.h` header file.  The SSD1306 component is a compact and simplified driver compatible with 128x64 and 128x32 OLED displays.  There are three font sizes supported, hardware and software scrolling capabilities, bitmap visualization, and more.  This component has one font implemented now (i.e. 8x8 basic Latin + control + extended Latin) but is ideal for most use cases.

## Repository
The component is hosted on github and is located here: https://github.com/K0I05/ESP32-S3_ESP-IDF_COMPONENTS/tree/main/components/peripherals/i2c/esp_ssd1306

## General Usage
To get started, simply copy the component to your project's `components` folder and reference the `ssd1306.h` header file as an include.  The component includes documentation for the peripheral such as the datasheet, application notes, and/or user manual where applicable.

```
components
└── esp_ssd1306
    ├── CMakeLists.txt
    ├── README.md
    ├── LICENSE
    ├── idf_component.yml
    ├── library.json
    ├── documentation
    │   └── datasheets, etc.
    ├── include
    │   └── ssd1306_version.h
    │   └── ssd1306.h
    └── ssd1306.c
```

## Basic Example
Once a driver instance is instantiated the display panel is ready for usage as shown in the below example.   This basic implementation of the driver utilizes default configuration settings and displays a sequence of text messages and bitmaps at user defined interval and prints the results.

The example initializes a 128x64 SSD1306 OLED display and demonstrates the following features:
- Display large text (x3)
- Display file receive and transmit bitmap icons
- Display medium text (x2)
- Display text
- Display countdown timer
- Display text scrolling up
- Display text scrolling down
- Display text paging down and up
- Display text scrolling horizontally from right and left
- Display text scrolling vertically downwards and upwards
- Display bitmap images
- Display inverted text and fadeout

```
#include <ssd1306.h>


void i2c0_ssd1306_task( void *pvParameters ) {
    // initialize the xLastWakeTime variable with the current time.
    TickType_t          last_wake_time   = xTaskGetTickCount ();
    //
    // initialize i2c device configuration
    ssd1306_config_t dev_cfg         = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    ssd1306_handle_t dev_hdl;
    //
    // init device
    ssd1306_init(i2c0_bus_hdl, &dev_cfg, &dev_hdl);
    if (dev_hdl == NULL) {
        ESP_LOGE(APP_TAG, "ssd1306 handle init failed");
        assert(dev_hdl);
    }
    //
    // task loop entry point
    for ( ;; ) {
        ESP_LOGI(APP_TAG, "######################## SSD1306 - START #########################");
        //
        int center = 1, top = 1, bottom = 4;
        char lineChar[16];
        uint8_t image[24];

        ESP_LOGI(APP_TAG, "Panel is 128x64");

        // Display x3 text
        ESP_LOGI(APP_TAG, "Display x3 Text");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text_x3(dev_hdl, 0, "Hello", 5, false);
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Display bitmap icons
        ESP_LOGI(APP_TAG, "Display bitmap icons");
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_display_bitmap(dev_hdl, 31, 0, data_rx_img_32x32, 32, 32, false);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        ssd1306_display_bitmap(dev_hdl, 31, 0, data_tx_img_32x32, 32, 32, false);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // Display x2 text
        ESP_LOGI(APP_TAG, "Display x2 Text");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text_x2(dev_hdl, 0, "{xTEXTx}", 8, false);
        ssd1306_display_text_x2(dev_hdl, 2, " X2-X2", 6, false);
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Display text
        ESP_LOGI(APP_TAG, "Display Text");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, 0, "SSD1306 128x64", 14, false);
        ssd1306_display_text(dev_hdl, 1, "Hello World!!", 13, false);
        ssd1306_display_text(dev_hdl, 2, "SSD1306 128x64", 14, true);
        ssd1306_display_text(dev_hdl, 3, "Hello World!!", 13, true);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        
        // Display Count Down
        ESP_LOGI(APP_TAG, "Display Count Down");
        memset(image, 0, sizeof(image));
        ssd1306_display_image(dev_hdl, top, (6*8-1), image, sizeof(image));
        ssd1306_display_image(dev_hdl, top+1, (6*8-1), image, sizeof(image));
        ssd1306_display_image(dev_hdl, top+2, (6*8-1), image, sizeof(image));
        for(int font = 0x39; font > 0x30; font--) {
            memset(image, 0, sizeof(image));
            ssd1306_display_image(dev_hdl, top+1, (7*8-1), image, 8);
            memcpy(image, font8x8_latin_tr[font], 8);
            if (dev_hdl->dev_config.flip_enabled) ssd1306_flip_buffer(image, 8);
            ssd1306_display_image(dev_hdl, top+1, (7*8-1), image, 8);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        
        // Scroll Up
        ESP_LOGI(APP_TAG, "Scroll Up");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, 0, "---Scroll  UP---", 16, true);
        ssd1306_set_software_scroll(dev_hdl, (dev_hdl->pages - 1), 1);
        for (int line = 0; line < bottom+10; line++) {
            lineChar[0] = 0x01;
            sprintf(&lineChar[1], " Line %02d", line);
            ssd1306_display_scroll_text(dev_hdl, lineChar, strlen(lineChar), false);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        
        // Scroll Down
        ESP_LOGI(APP_TAG, "Scroll Down");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, 0, "--Scroll  DOWN--", 16, true);
        ssd1306_set_software_scroll(dev_hdl, 1, (dev_hdl->pages - 1) );
        for (int page = 0; page < bottom+10; page++) {
            lineChar[0] = 0x02;
            sprintf(&lineChar[1], " Line %02d", page);
            ssd1306_display_scroll_text(dev_hdl, lineChar, strlen(lineChar), false);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Page Down
        ESP_LOGI(APP_TAG, "Page Down");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, 0, "---Page	DOWN---", 16, true);
        ssd1306_set_software_scroll(dev_hdl, 1, (dev_hdl->pages-1) );
        for (int page = 0; page < bottom+10; page++) {
            if ( (page % (dev_hdl->pages-1)) == 0) ssd1306_clear_scroll_display(dev_hdl);
            lineChar[0] = 0x02;
            sprintf(&lineChar[1], " Line %02d", page);
            ssd1306_display_scroll_text(dev_hdl, lineChar, strlen(lineChar), false);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Horizontal Scroll
        ESP_LOGI(APP_TAG, "Horizontal Scroll");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, center, "Horizontal", 10, false);
        ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_RIGHT, SSD1306_SCROLL_2_FRAMES);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_LEFT, SSD1306_SCROLL_2_FRAMES);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_STOP, SSD1306_SCROLL_2_FRAMES);
        
        // Vertical Scroll
        ESP_LOGI(APP_TAG, "Vertical Scroll");
        ssd1306_clear_display(dev_hdl, false);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, center, "Vertical", 8, false);
        ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_DOWN, SSD1306_SCROLL_2_FRAMES);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_UP, SSD1306_SCROLL_2_FRAMES);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_STOP, SSD1306_SCROLL_2_FRAMES);

        // Bitmaps
        ESP_LOGI(APP_TAG, "Bitmaps");
        ssd1306_display_text(dev_hdl, 1, "BATMAN", 6, false);
		int bitmapWidth = 4*8;
		int width = dev_hdl->width;
		int xpos = width / 2; // center of width
		xpos = xpos - bitmapWidth/2; 
		int ypos = 16;
		ESP_LOGD(APP_TAG, "width=%d xpos=%d", width, xpos);
		ssd1306_display_bitmap(dev_hdl, xpos, ypos, batman, 32, 13, false);
		vTaskDelay(3000 / portTICK_PERIOD_MS);
		for(int i=0;i<128;i++) {
			ssd1306_set_display_wrap_around(dev_hdl, SSD1306_SCROLL_RIGHT, 2, 3, 0);
		}
		vTaskDelay(2000 / portTICK_PERIOD_MS);

        ssd1306_clear_display(dev_hdl, false);
		ssd1306_display_bitmap(dev_hdl, 0, 0, logo_mischianti, 128, 64, false);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		for(int i=0;i<64;i++) {
			ssd1306_set_display_wrap_around(dev_hdl, SSD1306_SCROLL_UP, 0, 127, 0);
		}
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		ssd1306_clear_display(dev_hdl, false);
		ssd1306_display_bitmap(dev_hdl, 0, 0, fleischer, 128, 64, false);
		vTaskDelay(2000 / portTICK_PERIOD_MS);

        // Invert
        ESP_LOGI(APP_TAG, "Invert");
        ssd1306_clear_display(dev_hdl, true);
        ssd1306_set_display_contrast(dev_hdl, 0xff);
        ssd1306_display_text(dev_hdl, center, "  Good Bye!!", 12, true);
        vTaskDelay(5000 / portTICK_PERIOD_MS);

        // Fade Out
        ESP_LOGI(APP_TAG, "Fade Out");
        ssd1306_fadeout_display(dev_hdl);
        //
        ESP_LOGI(APP_TAG, "######################## SSD1306 - END ###########################");
        //
        //
        // pause the task per defined wait period
        vTaskDelaySecUntil( &last_wake_time, I2C0_TASK_SAMPLING_RATE + 10 );
    }
    //
    // free resources
    ssd1306_delete( dev_hdl );
    vTaskDelete( NULL );
}
```

Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)
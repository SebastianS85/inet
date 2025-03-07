#include <stdio.h>
#include "display.h"
#include "ssd1306.h"
#include "esp_log.h"
#include "i2c.h"


static const char *TAG = "DISPLAY";


ssd1306_config_t dev_cfg = I2C_SSD1306_128x32_CONFIG_DEFAULT;
ssd1306_handle_t dev_hdl;


  
void init_display(void){

    ssd1306_init(i2c0_bus_hdl, &dev_cfg, &dev_hdl);
    if (dev_hdl == NULL)
    {
        ESP_LOGE(TAG, "ssd1306 handle init failed");
        assert(dev_hdl);
    }
}

void display_set_text(char *text,uint8_t line,bool invert){
   
    ssd1306_display_text(dev_hdl, line,text, invert);
   
}


void display_set_text_x2(char *text,uint8_t line){

    ssd1306_display_text_x2(dev_hdl, line, text, false);

}


void display_clear(void){
    ssd1306_clear_display(dev_hdl, false);
}

void display_set_contrast(uint8_t contrast){
    ssd1306_set_contrast(dev_hdl, contrast);
}


// Display x3 text


//ssd1306_clear_display(dev_hdl, false);
//ssd1306_set_contrast(dev_hdl, 0xff);
//ssd1306_display_text_x2(dev_hdl, 0, "RMF FM", false);
// ssd1306_set_hardware_scroll(dev_hdl, SSD1306_SCROLL_LEFT, SSD1306_SCROLL_64_FRAMES);




#include <stdio.h>
#include <stdint.h>
#include "common_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"

#define TAG "COMMON_WIFI"
static void delete_wifi_credentials_timer_cb(void* arg);
#define GPIO38_DEBOUNCE_US 20000 // 20ms debounce
volatile int a = 0;
volatile int gpio38_debounced = 0;
volatile int64_t gpio38_last_time = 0;
static esp_timer_handle_t del_wifi_timer = NULL;


static void IRAM_ATTR gpio38_isr_handler(void* arg) {
    int64_t now = esp_timer_get_time();
    if (now - gpio38_last_time > GPIO38_DEBOUNCE_US) {
        gpio38_last_time = now;
        
        gpio38_debounced++;
        // Start one-shot timer to delete WiFi credentials (e.g., after 100ms)
        if (del_wifi_timer) {
            esp_timer_stop(del_wifi_timer); // Cancel any previous
        }
        esp_timer_start_once(del_wifi_timer, 100000); // 100ms (adjust as needed)
    }
}
// In your initialization code (e.g., app_main or a setup function):
void setup_gpio38_interrupt() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Only falling edge
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << 38),
        .pull_up_en = GPIO_PULLUP_ENABLE, // Enable if your button/switch is open-drain
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0); // Pass 0 for default ISR service
    gpio_isr_handler_add(38, gpio38_isr_handler, NULL);

    // Create the one-shot timer for WiFi credential deletion
    if (!del_wifi_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = &delete_wifi_credentials_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "del_wifi_nvs"
        };
        esp_timer_create(&timer_args, &del_wifi_timer);
    }
}
void print_ip_address(const char *mode)
{
    esp_netif_ip_info_t ip_info;

    // Get the default netif (interface)
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(mode);

    if (netif == NULL)
    {
        ESP_LOGE(TAG, "No network interface found!");
        return;
    }

    // Get the IP info
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
    {
        char ip_str[32];
        snprintf(ip_str, sizeof(ip_str), " " IPSTR, IP2STR(&ip_info.ip));

        display_set_text(ip_str, 0, false);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get IP information");
        display_set_text("No IP check wifi", 0, false);
    }

}



// One-shot timer callback to delete WiFi credentials from NVS
static void delete_wifi_credentials_timer_cb(void* arg) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, SSID_KEY);
        nvs_erase_key(nvs_handle, PASSWORD_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi credentials deleted from NVS");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for deleting WiFi credentials: %s", esp_err_to_name(err));
    }
}





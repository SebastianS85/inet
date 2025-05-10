#include <stdio.h>
#include "common_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "display.h"

#define TAG "COMMON_WIFI"

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





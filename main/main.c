/*
 * ESP32 Internet Radio - Main Application
 * Manages system initialization, Wi-Fi connectivity, and task creation
 */

/* ============================================================================ */
/* Standard Library Includes */
/* ============================================================================ */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================ */
/* FreeRTOS Includes */
/* ============================================================================ */
#include "freertos/freertos.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

/* ============================================================================ */
/* ESP-IDF and Networking Includes */
/* ============================================================================ */
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_phy_init.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "mdns.h"

/* ============================================================================ */
/* ESP-ADF and Peripherals Includes */
/* ============================================================================ */
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "audio_idf_version.h"

/* ============================================================================ */
/* Hardware Driver Includes */
/* ============================================================================ */
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include "cJSON.h"

/* ============================================================================ */
/* Project-Specific Includes */
/* ============================================================================ */
#include "audio.h"
#include "server.h"
#include "display.h"
#include "common_wifi.h"
#include "i2c.h"

static const char *TAG = "HTTP_LIVINGSTREAM_EXAMPLE";
static const char *BASE_PATH = "/store";

/* ============================================================================ */
/* Wi-Fi Signal Strength Utility Functions */
/* ============================================================================ */

/**
 * Convert RSSI (dBm) to signal strength level (0-4 bars)
 * @param rssi_dbm Signal strength in decibel-milliwatts
 * @return Signal level 0-4 (0=no signal, 4=excellent)
 */
static int rssi_to_level(int rssi_dbm)
{
    if (rssi_dbm >= -55)
        return 4;
    if (rssi_dbm >= -65)
        return 3;
    if (rssi_dbm >= -75)
        return 2;
    if (rssi_dbm >= -85)
        return 1;
    return 0;
}

/**
 * Build visual Wi-Fi signal strength indicator
 * Creates string like "[####]" for strong signal or "[#---]" for weak
 * @param level Signal level 0-4
 * @param out Output buffer for icon string
 * @param out_len Buffer size (minimum 7 bytes)
 */
static void build_wifi_icon(int level, char *out, size_t out_len)
{
    if (!out || out_len < 7)
        return;

    if (level < 0)
        level = 0;
    if (level > 4)
        level = 4;

    out[0] = '[';
    for (int i = 0; i < 4; i++)
        out[i + 1] = (i < level) ? '#' : '-';
    out[5] = ']';
    out[6] = '\0';
}

/* ============================================================================ */
/* Wi-Fi Signal Display Task */
/* ============================================================================ */

/**
 * FreeRTOS task: Display current Wi-Fi signal strength on OLED
 * Updates every 2 seconds with 5-sample rolling average
 * Shows format: "WiFi:[####] -67" on OLED line 3
 * @param pvParameter Task parameter (unused)
 */
static void rssi_display_task(void *pvParameter)
{
    (void)pvParameter;
    char oled_line[17];
    char wifi_icon[7];
    int samples[5] = {0};
    int sample_count = 0;
    int sample_index = 0;

    while (1)
    {
        wifi_ap_record_t ap_info = {0};
        esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

        if (err == ESP_OK)
        {
            int sum = 0;
            int avg_rssi;
            int level;

            /* Add new sample and update rolling average */
            samples[sample_index] = ap_info.rssi;
            sample_index = (sample_index + 1) % 5;
            if (sample_count < 5)
                sample_count++;

            /* Calculate average of available samples */
            for (int i = 0; i < sample_count; i++)
                sum += samples[i];
            avg_rssi = sum / sample_count;

            /* Convert to visual representation */
            level = rssi_to_level(avg_rssi);
            build_wifi_icon(level, wifi_icon, sizeof(wifi_icon));

            snprintf(oled_line, sizeof(oled_line), "WiFi:%s %3d", wifi_icon, avg_rssi);
        }
        else
        {
            /* Connection lost - show no signal */
            sample_count = 0;
            sample_index = 0;
            build_wifi_icon(0, wifi_icon, sizeof(wifi_icon));
            snprintf(oled_line, sizeof(oled_line), "WiFi:%s ---", wifi_icon);
        }

        display_set_text(oled_line, 3, false);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ============================================================================ */
/* NVS and System Utilities */
/* ============================================================================ */

/**
 * Debug function: Print NVS contents to logs
 * Checks for stored credentials: SSID and password
 */
static void debug_nvs_contents(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    /* Check for SSID */
    size_t ssid_len = 0;
    err = nvs_get_str(nvs_handle, SSID_KEY, NULL, &ssid_len);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "SSID found in NVS (length: %d)", (int)ssid_len - 1);
    }
    else
    {
        ESP_LOGE(TAG, "SSID not found in NVS: %s", esp_err_to_name(err));
    }

    /* Check for password */
    size_t pass_len = 0;
    err = nvs_get_str(nvs_handle, PASSWORD_KEY, NULL, &pass_len);
    if (err == ESP_OK)
    {
        char *pass = malloc(pass_len);
        err = nvs_get_str(nvs_handle, PASSWORD_KEY, pass, &pass_len);
        ESP_LOGI(TAG, "Password found in NVS (length: %d)", pass_len);
        free(pass);
    }
    else
    {
        ESP_LOGE(TAG, "Password not found in NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
}

/**
 * Initialize mDNS service for local network discovery
 * Advertises as "radio.local"
 */
static void start_mdns_service(void)
{
    mdns_init();
    mdns_hostname_set("radio");
    mdns_instance_name_set("LEARN esp32 thing");
}

/**
 * Mount SPIFFS file system to /store mount point
 * Used for storing stations list and configuration
 */
static void mount_fs(void)
{
    wl_handle_t wl_handle;

    esp_vfs_fat_mount_config_t esp_vfs_fat_mount_config = {
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    esp_vfs_fat_spiflash_mount_rw_wl(BASE_PATH, "storage", 
                                      &esp_vfs_fat_mount_config, &wl_handle);
}

/* ============================================================================ */
/* Debug Tasks */
/* ============================================================================ */

/**
 * Debug task: Monitor and log free heap memory
 * Runs every second, logs current and minimum free heap
 * @param pvParameter Task parameter (unused)
 */
static void check_memory_task(void *pvParameter)
{
    (void)pvParameter;

    while (1)
    {
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        ESP_LOGI(TAG, "Free heap: %d bytes", free_heap);
        ESP_LOGI(TAG, "Minimum free heap: %d bytes", min_free_heap);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ============================================================================ */
/* File System Utilities */
/* ============================================================================ */

/* ============================================================================ */
/* Wi-Fi Event Handling */
/* ============================================================================ */

/**
 * Wi-Fi event handler for Access Point mode
 * Logs station connections and disconnections
 * @param arg Event handler argument
 * @param event_base Event base (WIFI_EVENT)
 * @param event_id Event ID (AP_STACONNECTED or AP_STADISCONNECTED)
 * @param event_data Event data with MAC address and AID
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "SoftAP: station " MACSTR " joined, AID=%d",
               MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "SoftAP: station " MACSTR " left, AID=%d",
               MAC2STR(event->mac), event->aid);
    }
}

/* ============================================================================ */
/* Application Entry Point */
/* ============================================================================ */

/**
 * Main application entry point
 * Initializes hardware, checks Wi-Fi credentials, and starts appropriate mode:
 * - Station mode (STA) if credentials found
 * - Access Point mode (SoftAP) if no credentials
 */
void app_main(void)
{
    /* Initialize hardware: I2C and OLED display */
    i2c_init();
    init_display();
    display_clear();
    display_set_contrast(1);
    display_set_text("Connecting to wifi..", 0, false);
    setup_gpio38_interrupt();

    /* Initialize NVS flash storage */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    /* Initialize networking layer */
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    debug_nvs_contents();

    /* Check if Wi-Fi credentials are stored in NVS */
    size_t ssid_len = 0, password_len = 0;
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle));

    esp_err_t ssid_err = nvs_get_str(nvs_handle, SSID_KEY, NULL, &ssid_len);
    esp_err_t password_err = nvs_get_str(nvs_handle, PASSWORD_KEY, NULL, &password_len);
    nvs_close(nvs_handle);

    /* Mount file system for stations storage */
    mount_fs();

    /* Start in Station mode if credentials are available */
    if (ssid_err == ESP_OK && password_err == ESP_OK && 
        ssid_len > 0 && password_len > 0)
    {
        ESP_LOGI(TAG, "Wi-Fi credentials found in NVS");

        /* Retrieve stored credentials from NVS */
        char *ssid = malloc(ssid_len);
        char *password = malloc(password_len);

        ESP_ERROR_CHECK(nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle));
        ESP_ERROR_CHECK(nvs_get_str(nvs_handle, SSID_KEY, ssid, &ssid_len));
        ESP_ERROR_CHECK(nvs_get_str(nvs_handle, PASSWORD_KEY, password, &password_len));
        nvs_close(nvs_handle);

        /* Initialize Wi-Fi in Station mode */
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
        esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

        periph_wifi_cfg_t wifi_cfg = {0};
        memcpy(wifi_cfg.wifi_config.sta.ssid, ssid, ssid_len);
        memcpy(wifi_cfg.wifi_config.sta.password, password, password_len);

        esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
        esp_periph_start(set, wifi_handle);
        periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

        /* Free allocated memory */
        free(ssid);
        free(password);

        /* Update display with connection status */
        display_clear();
        display_set_text("wifi connected", 0, false);
        print_ip_address(STATION_MODE_ENABLED);
        display_set_text("  radio.local    ", 2, true);

        /* Disable Wi-Fi power saving for stable connection */
        esp_err_t ret1 = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ret1 == ESP_OK)
        {
            ESP_LOGI("WiFi", "Wi-Fi power saving disabled.");
        }
        else
        {
            ESP_LOGE("WiFi", "Failed to disable Wi-Fi power saving: %d", ret1);
        }

        /* Initialize audio system and web server */
        init_server();
        load_stations();
        audio_init();
        audio_start(set);

        /* Create background tasks */
        xTaskCreatePinnedToCore(stream_task, "stream_task", (10 * 1024), NULL, 7, NULL, 1);
        xTaskCreatePinnedToCore(rssi_display_task, "rssi_task", (4 * 1024), NULL, 5, NULL, 1);
    }
    else
    {
        /* Start in Access Point (SoftAP) mode if no credentials found */
        wifi_init_softap();
        print_ip_address(AP_MODE_ENABLED);
        display_set_text("config wif-fi", 2, false);
    }
}

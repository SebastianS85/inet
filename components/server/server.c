#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "audio.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "common_wifi.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_mac.h"

#define TAG "SERVER"

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    wifi_ap_record_t ap_records[MAX_AP_COUNT];
    uint16_t ap_count = MAX_AP_COUNT;

    // Ensure Wi-Fi mode is correct
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    // Configure scan parameters
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300};

    // Stop any ongoing scan
    esp_wifi_scan_stop();

    // Start the scan
    esp_err_t scan_result = esp_wifi_scan_start(&scan_config, true);

    if (scan_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Scan start failed with error: %s", esp_err_to_name(scan_result));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan start failed");
        return ESP_FAIL;
    }

    // Delay to allow scan to complete

    // Get scan results
    scan_result = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (scan_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Scan get records failed with error: %s", esp_err_to_name(scan_result));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get scan results");
        return ESP_FAIL;
    }

    if (ap_count == 0)
    {
        ESP_LOGW(TAG, "No networks found");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No networks found");
        return ESP_FAIL;
    }

    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();

    for (int i = 0; i < ap_count; i++)
    {
        cJSON *network = cJSON_CreateObject();
        if (strlen((char *)ap_records[i].ssid) > 0)
        {
            cJSON_AddStringToObject(network, "ssid", (char *)ap_records[i].ssid);

            char bssid_str[18];
            snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                     ap_records[i].bssid[0], ap_records[i].bssid[1],
                     ap_records[i].bssid[2], ap_records[i].bssid[3],
                     ap_records[i].bssid[4], ap_records[i].bssid[5]);
            cJSON_AddStringToObject(network, "bssid", bssid_str);

            cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
            cJSON_AddNumberToObject(network, "channel", ap_records[i].primary);
            cJSON_AddNumberToObject(network, "authmode", ap_records[i].authmode);

            cJSON_AddItemToArray(networks, network);
        }
    }

    cJSON_AddItemToObject(root, "networks", networks);
    cJSON_AddNumberToObject(root, "total_networks", ap_count);

    char *response = cJSON_Print(root);
    if (response == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON response");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);

    free(response);
    cJSON_Delete(root);

    return ESP_OK;
}
esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS in read-write mode
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    // Check if the SSID and password are already stored
    size_t ssid_len = 0, password_len = 0;
    char stored_ssid[32] = {0};
    char stored_password[64] = {0};

    nvs_get_str(nvs_handle, SSID_KEY, NULL, &ssid_len);
    nvs_get_str(nvs_handle, PASSWORD_KEY, NULL, &password_len);

    if (ssid_len > 0 && password_len > 0)
    {
        nvs_get_str(nvs_handle, SSID_KEY, stored_ssid, &ssid_len);
        nvs_get_str(nvs_handle, PASSWORD_KEY, stored_password, &password_len);

        // Compare with the new credentials
        if (strcmp(stored_ssid, ssid) == 0 && strcmp(stored_password, password) == 0)
        {
            // Credentials are the same, no need to write again
            nvs_close(nvs_handle);
            return ESP_OK;
        }
    }

    // Write SSID
    err = nvs_set_str(nvs_handle, SSID_KEY, ssid);
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }

    // Write Password
    err = nvs_set_str(nvs_handle, PASSWORD_KEY, password);
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes to avoid frequent writes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    esp_restart();
    return err;
}

// Handler for saving Wi-Fi credentials
static esp_err_t save_wifi_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "save_wifi_handler called");

    char buf[256];
    int ret, remaining = req->content_len;
    ESP_LOGI(TAG, "Content length: %d", remaining);

    // Read the request body
    ret = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
    if (ret <= 0)
    {
        ESP_LOGE(TAG, "Failed to receive request body");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Request body: %.*s", ret, buf);

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Extract SSID and Password
    cJSON *ssid_json = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItemCaseSensitive(json, "password");

    if (!cJSON_IsString(ssid_json) || !cJSON_IsString(password_json))
    {
        ESP_LOGE(TAG, "Invalid JSON format");
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SSID: %s, Password: %s", ssid_json->valuestring, password_json->valuestring);

    // Save credentials
    esp_err_t save_result = save_wifi_credentials(ssid_json->valuestring, password_json->valuestring);

    // Prepare response
    httpd_resp_set_type(req, "application/json");
    if (save_result == ESP_OK)
    {
        ESP_LOGI(TAG, "Wi-Fi credentials saved successfully");
        httpd_resp_sendstr(req, "{\"success\": true, \"message\": \"Wi-Fi credentials saved successfully!\"}");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save Wi-Fi credentials");
        httpd_resp_sendstr(req, "{\"success\": false, \"message\": \"Failed to save Wi-Fi credentials.\"}");
    }

    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t get_station_list(httpd_req_t *req)
{
    load_stations();
    ESP_LOGI("STATIONS", "Serving station list");

    // Start building the JSON response
    cJSON *response = cJSON_CreateArray();

    for (int i = 0; i < station_count; i++)
    {
        // Create a new object for each station
        cJSON *station = cJSON_CreateObject();
        cJSON_AddItemToObject(station, "index", cJSON_CreateNumber(stations[i].index));
        cJSON_AddItemToObject(station, "name", cJSON_CreateString(stations[i].name));

        // Add the station object to the response array
        cJSON_AddItemToArray(response, station);
    }

    // Convert JSON to string and send the response
    char *json_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    printf("%s", json_str);
    // Clean up the JSON object
    cJSON_Delete(response);
    free(json_str);

    return ESP_OK;
}
esp_err_t on_rssi(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling /get_rssi request");

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get Wi-Fi AP info: %s", esp_err_to_name(err));
        httpd_resp_send_500(req); // Send a 500 Internal Server Error response
        return ESP_FAIL;
    }

    char rssi[16]; // Buffer to store RSSI as a string
    snprintf(rssi, sizeof(rssi), "%d", ap_info.rssi);

    httpd_resp_set_type(req, "text/plain"); // Set response type
    httpd_resp_send(req, rssi, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

esp_err_t global_uri_filter(httpd_req_t *req)
{
    if (esp_get_free_heap_size() < 20000)
    { // Jeśli RAM < 20kB, odrzucamy
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server overloaded");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t on_default_url(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);
    char path[600];
    sprintf(path, "/store%s", req->uri);
    char *ext = strrchr(path, '.');
    if (ext)
    {
        if (strcmp(ext, ".css") == 0)
            httpd_resp_set_type(req, "text/css");
        if (strcmp(ext, ".js") == 0)
            httpd_resp_set_type(req, "text/javascript");
        if (strcmp(ext, ".png") == 0)
            httpd_resp_set_type(req, "image/png");
        if (strcmp(ext, ".jpg") == 0)
            httpd_resp_set_type(req, "image/jpeg");
        if (strcmp(ext, ".ico") == 0)
            httpd_resp_set_type(req, "image/x-icon");
        if (strcmp(ext, ".svg") == 0)
            httpd_resp_set_type(req, "image/svg+xml");
    }

    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        file = fopen("/store/index.html", "r");
        if (file == NULL)
        {
            httpd_resp_send_404(req);
        }
    }
    char buffer[1024];
    int bytes_read = 0;
    while ((bytes_read = fread(buffer, sizeof(char), sizeof(buffer), file)) > 0)
    {
        httpd_resp_send_chunk(req, buffer, bytes_read);
    }
    fclose(file);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t on_pause(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);
    audio_pause();
    httpd_resp_send(req, "Audio Paused!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t start(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);

    audio_resume();
    httpd_resp_send(req, "Audio Started!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t current_station(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);

    // Get the current station information
    char *response = current_station_info(); // No need to dereference

    // Check if response is valid before sending
    if (response == NULL)
    {
        const char *error_msg = "Error: Invalid station index or no station info available.";
        httpd_resp_send(req, error_msg, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL; // Return an error if the station info is not available
    }

    // Send the station info as a response

    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    char content[100];                  // Buffer to store POST data
    int content_len = req->content_len; // Get the actual length of the data being sent

    // Make sure content fits in the buffer
    if (content_len > sizeof(content) - 1)
    {
        ESP_LOGW(TAG, "Content too large for buffer, truncating.");
        content_len = sizeof(content) - 1;
    }

    // Receive the POST data
    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            ESP_LOGE(TAG, "Timeout while receiving data");
        }
        return ESP_FAIL;
    }

    // Null-terminate the received content
    content[ret] = '\0';

    // Log the received content for debugging
    ESP_LOGI(TAG, "Received POST data: %s", content);

    // Parse JSON if needed, e.g., {"index": 1}
    cJSON *json = cJSON_Parse(content);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *index_item = cJSON_GetObjectItem(json, "index");
    if (cJSON_IsNumber(index_item))
    {
        int index = index_item->valueint;
        change_radio_station(index);
        ESP_LOGI(TAG, "Received index: %d", index);
        // Handle the index and change the station accordingly
    }

    cJSON_Delete(json);

    // Send a response back to the client
    httpd_resp_send(req, "Station Set Successfully!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t add_station_handler(httpd_req_t *req)
{
    char content[256];
    int content_len = req->content_len;
    if (content_len > sizeof(content) - 1)
        content_len = sizeof(content) - 1;
    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            ESP_LOGE(TAG, "Timeout while receiving data");
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    ESP_LOGI(TAG, "Received add-station POST data: %s", content);

    cJSON *json = cJSON_Parse(content);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    const cJSON *name = cJSON_GetObjectItem(json, "name");
    const cJSON *url = cJSON_GetObjectItem(json, "url");
    const cJSON *genre = cJSON_GetObjectItem(json, "genre");
    if (!cJSON_IsString(name) || !cJSON_IsString(url) || !cJSON_IsString(genre))
    {
        ESP_LOGE(TAG, "Missing or invalid fields in add-station");
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_err_t res = add_station_to_file(name->valuestring, url->valuestring, genre->valuestring);
    cJSON_Delete(json);
    if (res == ESP_OK)
    {
        httpd_resp_send(req, "Station added successfully!", HTTPD_RESP_USE_STRLEN);

        return ESP_OK;
    }
    else
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
}

static esp_err_t delete_station_handler(httpd_req_t *req)
{
    char content[64];
    int content_len = req->content_len;
    if (content_len > sizeof(content) - 1)
        content_len = sizeof(content) - 1;
    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            ESP_LOGE(TAG, "Timeout while receiving data");
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    ESP_LOGI(TAG, "Received delete-station POST data: %s", content);

    cJSON *json = cJSON_Parse(content);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON *index_item = cJSON_GetObjectItem(json, "index");
    if (!cJSON_IsNumber(index_item))
    {
        ESP_LOGE(TAG, "Missing or invalid index in delete-station");
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int index = index_item->valueint;
    esp_err_t res = delete_station_from_file(index);
    cJSON_Delete(json);
    if (res == ESP_OK)
    {
        httpd_resp_send(req, "Station deleted successfully!", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
}

// Handler for /wifi-mode endpoint
static esp_err_t wifi_mode_handler(httpd_req_t *req)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    const char *mode_str = "unknown";
    if (mode == WIFI_MODE_AP)
        mode_str = "ap";
    else if (mode == WIFI_MODE_STA)
        mode_str = "sta";
    else if (mode == WIFI_MODE_APSTA)
        mode_str = "apsta";
    httpd_resp_set_type(req, "application/json");
    char resp[32];
    snprintf(resp, sizeof(resp), "{\"mode\":\"%s\"}", mode_str);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void init_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 9216;
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = (void *)global_uri_filter;

    ESP_ERROR_CHECK(httpd_start(&server, &config));
    config.task_priority = 6;

    httpd_uri_t wifi_scan_uri = {
        .uri = "/wifi_scan",
        .method = HTTP_GET,
        .handler = wifi_scan_handler,
        .user_ctx = NULL};

    static const httpd_uri_t save_wifi = {
        .uri = "/save_wifi",
        .method = HTTP_POST,
        .handler = save_wifi_handler};

    httpd_uri_t pause_url = {
        .uri = "/pause",
        .method = HTTP_GET,
        .handler = on_pause};

    httpd_uri_t set_station_url = {
        .uri = "/set-station",
        .method = HTTP_POST,
        .handler = post_handler};

    httpd_uri_t start_url = {
        .uri = "/start",
        .method = HTTP_GET,
        .handler = start};

    httpd_uri_t station_list_url = {
        .uri = "/stations",
        .method = HTTP_GET,
        .handler = get_station_list};

    httpd_uri_t rssi_url = {
        .uri = "/rssi",
        .method = HTTP_GET,
        .handler = on_rssi};

    httpd_uri_t current_station_url = {
        .uri = "/current-station",
        .method = HTTP_GET,
        .handler = current_station};
    httpd_uri_t add_station_url = {
        .uri = "/add-station",
        .method = HTTP_POST,
        .handler = add_station_handler};

    httpd_uri_t remove_station_url = {
        .uri = "/delete-station",
        .method = HTTP_POST,
        .handler = delete_station_handler};

    httpd_uri_t wifi_mode_url = {
        .uri = "/wifi-mode",
        .method = HTTP_GET,
        .handler = wifi_mode_handler};

    httpd_uri_t default_url = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = on_default_url};

    httpd_register_uri_handler(server, &save_wifi);
    httpd_register_uri_handler(server, &wifi_scan_uri);
    httpd_register_uri_handler(server, &current_station_url);
    httpd_register_uri_handler(server, &start_url);
    httpd_register_uri_handler(server, &remove_station_url);
    httpd_register_uri_handler(server, &add_station_url);
    httpd_register_uri_handler(server, &set_station_url);
    httpd_register_uri_handler(server, &pause_url);
    httpd_register_uri_handler(server, &rssi_url);
    httpd_register_uri_handler(server, &station_list_url);
    httpd_register_uri_handler(server, &wifi_mode_url);
    httpd_register_uri_handler(server, &default_url);
}

void wifi_init_softap(void)
{
    ESP_LOGI(TAG, "Initializing SoftAP...");

    // Initialize networking
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // WiFi init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure AP settings
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_AP_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Set WiFi mode and start

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP initialized. SSID: %s, Password: %s",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASS);

    init_server();
}
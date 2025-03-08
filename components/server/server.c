#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "audio.h"
#include "cJSON.h"
#include "esp_wifi.h"

#define TAG "SERVER"
static esp_err_t get_station_list(httpd_req_t *req)
{
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

    // Clean up the JSON object
    cJSON_Delete(response);
    free(json_str);

    return ESP_OK;
}
static esp_err_t on_rssi(httpd_req_t *req)
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

void init_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = (void *)global_uri_filter;

    ESP_ERROR_CHECK(httpd_start(&server, &config));
    config.task_priority = 6;

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

    httpd_uri_t current_station_url = {
        .uri = "/current-station",
        .method = HTTP_GET,
        .handler = current_station};

    httpd_uri_t station_list_url = {
        .uri = "/stations",
        .method = HTTP_GET,
        .handler = get_station_list};

    httpd_uri_t rssi_url = {
        .uri = "/rssi",
        .method = HTTP_GET,
        .handler = on_rssi};

    httpd_uri_t default_url = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = on_default_url};

    httpd_register_uri_handler(server, &current_station_url);
    httpd_register_uri_handler(server, &start_url);
    httpd_register_uri_handler(server, &set_station_url);
    httpd_register_uri_handler(server, &pause_url);
    httpd_register_uri_handler(server, &rssi_url);
    httpd_register_uri_handler(server, &station_list_url);
    httpd_register_uri_handler(server, &default_url);
    
}
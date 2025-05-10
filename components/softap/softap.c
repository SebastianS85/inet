#include "softap.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include "esp_netif.h"
#include "common_wifi.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_mac.h"
#define TAG "SOFTAP"



esp_err_t save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS in read-write mode
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    // Check if the SSID and password are already stored
    size_t ssid_len = 0, password_len = 0;
    char stored_ssid[32] = {0};
    char stored_password[64] = {0};

    nvs_get_str(nvs_handle, SSID_KEY, NULL, &ssid_len);
    nvs_get_str(nvs_handle, PASSWORD_KEY, NULL, &password_len);

    if (ssid_len > 0 && password_len > 0) {
        nvs_get_str(nvs_handle, SSID_KEY, stored_ssid, &ssid_len);
        nvs_get_str(nvs_handle, PASSWORD_KEY, stored_password, &password_len);

        // Compare with the new credentials
        if (strcmp(stored_ssid, ssid) == 0 && strcmp(stored_password, password) == 0) {
            // Credentials are the same, no need to write again
            nvs_close(nvs_handle);
            return ESP_OK;
        }
    }

    // Write SSID
    err = nvs_set_str(nvs_handle, SSID_KEY, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    // Write Password
    err = nvs_set_str(nvs_handle, PASSWORD_KEY, password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes to avoid frequent writes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

// Handler for the root page (index.html)
const char index_html[] = "<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>ESP32 WiFi Configuration</title>\n"
"    <style>\n"
"        body {\n"
"            font-family: Arial, sans-serif;\n"
"            max-width: 600px;\n"
"            margin: 0 auto;\n"
"            padding: 20px;\n"
"            background-color: #f4f4f4;\n"
"        }\n"
"        .container {\n"
"            background-color: white;\n"
"            padding: 20px;\n"
"            border-radius: 8px;\n"
"            box-shadow: 0 2px 4px rgba(0,0,0,0.1);\n"
"            margin-bottom: 20px;\n"
"        }\n"
"        input {\n"
"            width: 100%;\n"
"            padding: 10px;\n"
"            margin: 10px 0;\n"
"            border: 1px solid #ddd;\n"
"            border-radius: 4px;\n"
"        }\n"
"        button {\n"
"            width: 100%;\n"
"            padding: 10px;\n"
"            background-color: #4CAF50;\n"
"            color: white;\n"
"            border: none;\n"
"            border-radius: 4px;\n"
"            cursor: pointer;\n"
"            margin: 5px 0;\n"
"        }\n"
"        button:hover {\n"
"            background-color: #45a049;\n"
"        }\n"
"        button:disabled {\n"
"            background-color: #cccccc;\n"
"            cursor: not-allowed;\n"
"        }\n"
"        #message {\n"
"            margin-top: 15px;\n"
"            text-align: center;\n"
"        }\n"
"        .network-list {\n"
"            margin-top: 20px;\n"
"        }\n"
"        .network-item {\n"
"            padding: 10px;\n"
"            border: 1px solid #ddd;\n"
"            margin: 5px 0;\n"
"            border-radius: 4px;\n"
"            cursor: pointer;\n"
"            display: flex;\n"
"            justify-content: space-between;\n"
"            align-items: center;\n"
"        }\n"
"        .network-item:hover {\n"
"            background-color: #f5f5f5;\n"
"        }\n"
"        .network-info {\n"
"            flex-grow: 1;\n"
"        }\n"
"        .network-ssid {\n"
"            font-weight: bold;\n"
"        }\n"
"        .network-details {\n"
"            font-size: 0.8em;\n"
"            color: #666;\n"
"        }\n"
"        .loading {\n"
"            text-align: center;\n"
"            padding: 20px;\n"
"            color: #666;\n"
"        }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <h2>WiFi Configuration</h2>\n"
"        <button id=\"scanBtn\" onclick=\"scanNetworks()\">Scan for Networks</button>\n"
"        <div id=\"networkList\" class=\"network-list\"></div>\n"
"        <form id=\"wifiForm\" style=\"display: none;\">\n"
"            <input type=\"text\" id=\"ssid\" placeholder=\"WiFi SSID\" required>\n"
"            <input type=\"password\" id=\"password\" placeholder=\"WiFi Password\" required>\n"
"            <button type=\"submit\">Save Credentials</button>\n"
"        </form>\n"
"        <div id=\"message\"></div>\n"
"    </div>\n"
"    <script>\n"
"        let scanning = false;\n"
"        const messageDiv = document.getElementById('message');\n"
"        const networkList = document.getElementById('networkList');\n"
"        const scanBtn = document.getElementById('scanBtn');\n"
"        const wifiForm = document.getElementById('wifiForm');\n"
"\n"
"        function scanNetworks() {\n"
"            if (scanning) return;\n"
"            scanning = true;\n"
"            scanBtn.disabled = true;\n"
"            networkList.innerHTML = '<div class=\"loading\">Scanning for networks...</div>';\n"
"            messageDiv.textContent = '';\n"
"\n"
"            fetch('/wifi_scan')\n"
"                .then(response => response.json())\n"
"                .then(data => {\n"
"                    scanning = false;\n"
"                    scanBtn.disabled = false;\n"
"                    displayNetworks(data.networks);\n"
"                })\n"
"                .catch(error => {\n"
"                    scanning = false;\n"
"                    scanBtn.disabled = false;\n"
"                    networkList.innerHTML = '';\n"
"                    messageDiv.style.color = 'red';\n"
"                    messageDiv.textContent = 'Failed to scan networks. Please try again.';\n"
"                    console.error('Error:', error);\n"
"                });\n"
"        }\n"
"\n"
"        function displayNetworks(networks) {\n"
"            if (!networks || networks.length === 0) {\n"
"                networkList.innerHTML = '<div class=\"loading\">No networks found</div>';\n"
"                return;\n"
"            }\n"
"\n"
"            networkList.innerHTML = '';\n"
"            networks.sort((a, b) => b.rssi - a.rssi).forEach(network => {\n"
"                const div = document.createElement('div');\n"
"                div.className = 'network-item';\n"
"                div.onclick = () => selectNetwork(network.ssid);\n"
"\n"
"                const signalStrength = Math.min(100, Math.max(0, (network.rssi + 100) * 2));\n"
"                const authMode = getAuthMode(network.authmode);\n"
"\n"
"                div.innerHTML = `\n"
"                    <div class=\"network-info\">\n"
"                        <div class=\"network-ssid\">${network.ssid}</div>\n"
"                        <div class=\"network-details\">\n"
"                            Signal: ${signalStrength}%, Channel: ${network.channel}, Security: ${authMode}\n"
"                        </div>\n"
"                    </div>\n"
"                `;\n"
"\n"
"                networkList.appendChild(div);\n"
"            });\n"
"        }\n"
"\n"
"        function getAuthMode(mode) {\n"
"            const modes = {\n"
"                0: 'Open',\n"
"                1: 'WEP',\n"
"                2: 'WPA-PSK',\n"
"                3: 'WPA2-PSK',\n"
"                4: 'WPA/WPA2-PSK',\n"
"                5: 'WPA2-ENTERPRISE',\n"
"                6: 'WPA3-PSK',\n"
"                7: 'WPA2/WPA3-PSK'\n"
"            };\n"
"            return modes[mode] || 'Unknown';\n"
"        }\n"
"\n"
"        function selectNetwork(ssid) {\n"
"            document.getElementById('ssid').value = ssid;\n"
"            wifiForm.style.display = 'block';\n"
"            document.getElementById('password').focus();\n"
"        }\n"
"\n"
"        document.getElementById('wifiForm').addEventListener('submit', function(e) {\n"
"            e.preventDefault();\n"
"            const ssid = document.getElementById('ssid').value;\n"
"            const password = document.getElementById('password').value;\n"
"\n"
"            fetch('/save_wifi', {\n"
"                method: 'POST',\n"
"                headers: {\n"
"                    'Content-Type': 'application/json',\n"
"                },\n"
"                body: JSON.stringify({ ssid, password })\n"
"            })\n"
"            .then(response => response.json())\n"
"            .then(data => {\n"
"                if (data.success) {\n"
"                    messageDiv.style.color = 'green';\n"
"                    messageDiv.textContent = data.message;\n"
"                    document.getElementById('ssid').value = '';\n"
"                    document.getElementById('password').value = '';\n"
"                    wifiForm.style.display = 'none';\n"
"                } else {\n"
"                    messageDiv.style.color = 'red';\n"
"                    messageDiv.textContent = data.message;\n"
"                }\n"
"            })\n"
"            .catch(error => {\n"
"                messageDiv.style.color = 'red';\n"
"                messageDiv.textContent = 'Error saving WiFi credentials';\n"
"                console.error('Error:', error);\n"
"            });\n"
"        });\n"
"\n"
"        // Initial scan when page loads\n"
"        scanNetworks();\n"
"    </script>\n"
"</body>\n"
"</html>";


    // Modify the root_get_handler to use this constant
    static esp_err_t root_get_handler(httpd_req_t *req) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, index_html, strlen(index_html));
        return ESP_OK;
    }

// Handler for saving Wi-Fi credentials
static esp_err_t save_wifi_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "save_wifi_handler called");

    char buf[256];
    int ret, remaining = req->content_len;
    ESP_LOGI(TAG, "Content length: %d", remaining);

    // Read the request body
    ret = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive request body");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Request body: %.*s", ret, buf);

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Extract SSID and Password
    cJSON *ssid_json = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItemCaseSensitive(json, "password");

    if (!cJSON_IsString(ssid_json) || !cJSON_IsString(password_json)) {
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
    if (save_result == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi credentials saved successfully");
        httpd_resp_sendstr(req, "{\"success\": true, \"message\": \"Wi-Fi credentials saved successfully!\"}");
    } else {
        ESP_LOGE(TAG, "Failed to save Wi-Fi credentials");
        httpd_resp_sendstr(req, "{\"success\": false, \"message\": \"Failed to save Wi-Fi credentials.\"}");
    }

    cJSON_Delete(json);
    return ESP_OK;
}

// Handler for deleting Wi-Fi credentials
static esp_err_t delete_wifi_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "delete_wifi_handler called");

    // Open NVS in read-write mode
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Erase SSID and Password
    err = nvs_erase_key(nvs_handle, SSID_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase SSID");
        nvs_close(nvs_handle);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    err = nvs_erase_key(nvs_handle, PASSWORD_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase Password");
        nvs_close(nvs_handle);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit changes to NVS");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Send success response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\": true, \"message\": \"Wi-Fi credentials deleted successfully!\"}");
    ESP_LOGI(TAG, "Wi-Fi credentials deleted successfully");
    return ESP_OK;
}

static esp_err_t wifi_scan_handler(httpd_req_t *req) {
    wifi_ap_record_t ap_records[MAX_AP_COUNT];
    uint16_t ap_count = MAX_AP_COUNT;

    // Ensure Wi-Fi mode is correct
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "Invalid WiFi Mode: %d. Scan requires AP or APSTA mode.", mode);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid WiFi Mode");
        return ESP_FAIL;
    }

    // Configure scan parameters
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };

    // Stop any ongoing scan
    esp_wifi_scan_stop();

    // Start the scan
    esp_err_t scan_result = esp_wifi_scan_start(&scan_config, true);
    
    if (scan_result != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed with error: %s", esp_err_to_name(scan_result));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan start failed");
        return ESP_FAIL;
    }

    // Delay to allow scan to complete
    
    // Get scan results
    scan_result = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (scan_result != ESP_OK) {
        ESP_LOGE(TAG, "Scan get records failed with error: %s", esp_err_to_name(scan_result));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get scan results");
        return ESP_FAIL;
    }

    if (ap_count == 0) {
        ESP_LOGW(TAG, "No networks found");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No networks found");
        return ESP_FAIL;
    }

    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();

    for (int i = 0; i < ap_count; i++) {
        cJSON *network = cJSON_CreateObject();
        if (strlen((char *)ap_records[i].ssid) > 0) {
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
    if (response == NULL) {
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


// HTTP Server URI handlers
static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};

static const httpd_uri_t save_wifi = {
    .uri       = "/save_wifi",
    .method    = HTTP_POST,
    .handler   = save_wifi_handler
};

// Add the delete_wifi URI handler
static const httpd_uri_t delete_wifi = {
    .uri       = "/delete_wifi",
    .method    = HTTP_POST,
    .handler   = delete_wifi_handler
};

 httpd_uri_t wifi_scan_uri = {
            .uri       = "/wifi_scan",
            .method    = HTTP_GET,
            .handler   = wifi_scan_handler,
            .user_ctx  = NULL
        };
// Register the new handler in the webserver setup
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL; // Declare and initialize server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); // Declare and initialize config
    config.lru_purge_enable = true;

    // Start the httpd server
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_register_uri_handler(server, &root);
         httpd_register_uri_handler(server, &wifi_scan_uri); 
        httpd_register_uri_handler(server, &save_wifi);
        httpd_register_uri_handler(server, &delete_wifi); // Register delete_wifi handler
    } else {
        ESP_LOGE(TAG, "Failed to start webserver");
    }

    return server;
}



void wifi_init_softap(void) {
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
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Set WiFi mode and start
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP initialized. SSID: %s, Password: %s",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASS);
    
    start_webserver();
}
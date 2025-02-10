/* Play M3U HTTP Living stream

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include "freertos/freertos.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
//#include "mdns.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"
#include "esp_http_server.h"
#include "esp_phy_init.h"

#include "audio_idf_version.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "HTTP_LIVINGSTREAM_EXAMPLE";







#define NUM_STATIONS 2
const char *station_list[NUM_STATIONS] = {
     // RADIO_PARK
    "https://icecast1.play.cz/kisshady64.mp3",    
    "https://sluchaj2.radiopark.biz.pl:8443/stream" ,   // RADIO_KISS
   
                   
};
int current_station_index = 0;



audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder;
audio_event_iface_handle_t evt;

void change_radio_station()
{
    // Increment and loop back to 0 when reaching the end of the station list
    current_station_index = (current_station_index + 1) % NUM_STATIONS;
    if (current_station_index == 0) {
        ESP_LOGI(TAG, "Reached the end of the station list. Resetting to the first station.");
        current_station_index = 0;
    }
    
    const char *uri = station_list[current_station_index];  // Get next station URI
    ESP_LOGI(TAG, "Changing to station: %s", uri);
    
    audio_pipeline_pause(pipeline);                      // Stop current pipeline
    audio_element_set_uri(http_stream_reader, uri);     // Set new station URI
    audio_pipeline_wait_for_stop(pipeline);             // Wait for stop to complete
    audio_element_reset_state(mp3_decoder);             // Reset decoder state
    audio_element_reset_state(i2s_stream_writer);       // Reset I2S state
    audio_pipeline_reset_ringbuffer(pipeline);          // Reset pipeline buffers
    audio_pipeline_reset_items_state(pipeline);         // Reset pipeline states
    audio_pipeline_resume(pipeline);                       // Start pipeline with new URI
}


static esp_err_t change_rs_url(httpd_req_t *req)
{
    change_radio_station();  // Switch to the next station
    const char *current_station = station_list[current_station_index];  // Get current station URI
    ESP_LOGI(TAG, "Station changed. Current index: %d, URL: %s", current_station_index, current_station);

    // Send the current station URI as the HTTP response
    httpd_resp_send(req, current_station, strlen(current_station));
    return ESP_OK;
}

static esp_err_t pause(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);
     audio_pipeline_pause(pipeline);
    httpd_resp_send(req, "Audio Paused!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t start(httpd_req_t *req)
{
    ESP_LOGI(TAG, "URL: %s", req->uri);
    
    audio_pipeline_resume(pipeline);
    httpd_resp_send(req, "Audio Started!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t control_page(httpd_req_t *req)
{
    const char *current_station = station_list[current_station_index];  // Assuming you have current_station_index

    char html_page[2048];  // Make sure this is large enough for the full HTML
    snprintf(html_page, sizeof(html_page),
        "<html>"
        "<head>"
        "<style>"
        "body {"
        "    background-color: #1e1e1e;"
        "    color: white;"
        "    font-family: Arial, sans-serif;"
        "    text-align: center;"
        "    padding: 20px;"
        "}"
        "h2 {"
        "    color: #f0f0f0;"
        "    margin-bottom: 20px;"
        "}"
        "button {"
        "    background-color: #4CAF50;"
        "    color: white;"
        "    border: none;"
        "    padding: 10px 20px;"
        "    font-size: 16px;"
        "    margin: 10px;"
        "    cursor: pointer;"
        "    border-radius: 5px;"
        "    transition: background-color 0.3s;"
        "}"
        "button:hover {"
        "    background-color: #45a049;"
        "}"
        "button:active {"
        "    background-color: #3e8e41;"
        "}"
        "#status {"
        "    margin-top: 20px;"
        "    font-size: 18px;"
        "    color: #ff9800;"
        "}"
        "#currentStation {"
        "    margin-top: 30px;"
        "    font-size: 18px;"
        "}"
        "#stationName {"
        "    font-weight: bold;"
        "    color: #4CAF50;"
        "}"
        "</style>"
        "</head>"
        "<body>"
        "<h2>Audio Control</h2>"
        "<button onclick='startAudio()'>Start Audio</button>"
        "<button onclick='pauseAudio()'>Pause Audio</button>"
        "<button onclick='changeStation()'>Change Station</button>"
        "<div id='status'></div>"
        "<div id='currentStation'>Current Station: <span id='stationName'>%s</span></div>"
        "<script>"
        "function startAudio() {"
        "  var xhr = new XMLHttpRequest();"
        "  xhr.open('GET', '/start', true);"
        "  xhr.onreadystatechange = function() {"
        "    if (xhr.readyState == 4 && xhr.status == 200) {"
        "      document.getElementById('status').innerHTML = 'Audio Started!';"
        "    }"
        "  };"
        "  xhr.send();"
        "}"
        "function pauseAudio() {"
        "  var xhr = new XMLHttpRequest();"
        "  xhr.open('GET', '/pause', true);"
        "  xhr.onreadystatechange = function() {"
        "    if (xhr.readyState == 4 && xhr.status == 200) {"
        "      document.getElementById('status').innerHTML = 'Audio Paused!';"
        "    }"
        "  };"
        "  xhr.send();"
        "}"
        "function changeStation() {"
        "  var xhr = new XMLHttpRequest();"
        "  xhr.open('GET', '/change_rs', true);"
        "  xhr.onreadystatechange = function() {"
        "    if (xhr.readyState == 4 && xhr.status == 200) {"
        "      document.getElementById('stationName').innerHTML = xhr.responseText;"
        "      document.getElementById('status').innerHTML = 'Station Changed!';"
        "    }"
        "  };"
        "  xhr.send();"
        "}"
        "</script>"
        "</body>"
        "</html>",
        current_station);  // Insert the current station URL

    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}


static void init_server()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    

    ESP_ERROR_CHECK(httpd_start(&server, &config));
    config.task_priority = 10;


    httpd_uri_t pause_url = {
        .uri = "/pause",
        .method = HTTP_GET,
        .handler = pause};

    httpd_uri_t start_url = {
        .uri = "/start",
        .method = HTTP_GET,
        .handler = start};

    httpd_uri_t control_url = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = control_page};

       httpd_uri_t ch_rs_url = {
        .uri = "/change_rs",
        .method = HTTP_GET,
        .handler = change_rs_url};

    httpd_register_uri_handler(server, &start_url);
    httpd_register_uri_handler(server, &pause_url);
    httpd_register_uri_handler(server, &control_url);
    httpd_register_uri_handler(server, &ch_rs_url);
}

int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS)
    {
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK)
    {
        return http_stream_next_track(msg->el);
    }
    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST)
    {
        return http_stream_fetch_again(msg->el);
    }
    return ESP_OK;
}


void stream_task(void *arg){
     while (1)
    {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)mp3_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
        {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from aac decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* restart stream when the first pipeline element (http_stream_reader in this case) receives stop event (caused by reading errors) */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)http_stream_reader && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int)msg.data == AEL_STATUS_ERROR_OPEN)
        {
            ESP_LOGW(TAG, "[ * ] Restart stream");
            audio_pipeline_stop(pipeline);
            audio_pipeline_wait_for_stop(pipeline);
            audio_element_reset_state(mp3_decoder);
            audio_element_reset_state(i2s_stream_writer);
            audio_pipeline_reset_ringbuffer(pipeline);
            audio_pipeline_reset_items_state(pipeline);
            audio_pipeline_run(pipeline);
            continue;
        }
       // vTaskDelay(pdMS_TO_TICKS(10));
    }



}

void app_main(void)



{


    
    
    esp_err_t err = nvs_flash_init();
   
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

 esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    ESP_LOGI(TAG, "[2.1] Create http stream to read data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    http_stream_reader = http_stream_init(&http_cfg);

    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.3] Create aac decoder to decode aac file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
  
    
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[2.5] Link it together http_stream-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[2.6] Set up  uri (http as http_stream, aac as aac decoder, and default output is i2s)");
    audio_element_set_uri(http_stream_reader, station_list[0]);

    ESP_LOGI(TAG, "[ 3 ] Start and wait for Wi-Fi network");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = "FRITZ!Box 7590 CH",
        .wifi_config.sta.password = "46050909421239206217",
    };

    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
     evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");

    audio_pipeline_run(pipeline);
    audio_pipeline_pause(pipeline);
    
    init_server();
    xTaskCreatePinnedToCore(stream_task, "stream_task", 4096, NULL, 5, NULL, 1);
    
}


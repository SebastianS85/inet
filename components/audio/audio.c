#include <stdio.h>
#include "audio.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "esp_peripherals.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "board.h"
#include "display.h"

static const char *TAG = "AUDIO STREAM";

Station stations[MAX_STATIONS];
int station_count = 0;

void load_stations() {
    FILE *file = fopen("/store/stations.txt", "r");
    if (!file) {
        ESP_LOGE("STATIONS", "Failed to open stations.txt");
        return;
    }

    char line[512];

    while (fgets(line, sizeof(line), file) && station_count < MAX_STATIONS) {
        // Trim spaces (optional)
        char *newline_pos = strchr(line, '\n');
        if (newline_pos) *newline_pos = '\0';  // Remove newline

        // Parse name, URL, and genre separated by '|'
        if (sscanf(line, "%127[^|]|%255[^|]|%63[^\n]", stations[station_count].name, stations[station_count].url, stations[station_count].genre) == 3) {
            if (strlen(stations[station_count].name) > 0 && strlen(stations[station_count].url) > 0 && strlen(stations[station_count].genre) > 0) {
                stations[station_count].index = station_count;  // Assign index dynamically
                station_count++;
            } else {
                ESP_LOGW("STATIONS", "Skipping invalid entry: %s", line);
            }
        } else {
            ESP_LOGW("STATIONS", "Failed to parse line: %s", line);
        }
    }

    fclose(file);

    if (station_count == 0) {
        ESP_LOGW("STATIONS", "No stations loaded from the file.");
    } else {
        ESP_LOGI("STATIONS", "Successfully loaded %d stations.", station_count);
    }
}

SemaphoreHandle_t station_Mutex;



int current_station_index = 0;
audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder;
audio_event_iface_handle_t evt;

void audio_init(void)
{

    station_Mutex = xSemaphoreCreateMutex();
    if (station_Mutex == NULL)
    {
        printf("Failed to create mutex!\n");
    }

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
    i2s_cfg.task_prio = 8;

    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.3] Create aac decoder to decode aac file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();

    mp3_decoder = mp3_decoder_init(&mp3_cfg);
}

void audio_start(esp_periph_set_handle_t set)
{

    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[2.5] Link it together http_stream-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[2.6] Set up  uri (http as http_stream, aac as aac decoder, and default output is i2s)");
    ESP_LOGE(TAG, "uri: %s", stations[current_station_index].url);
    audio_element_set_uri(http_stream_reader, stations[current_station_index].url);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);
    display_set_text(stations[current_station_index].name, 1,false);
}

void change_radio_station(uint8_t station_index)
{
   

    // Assuming station_list is populated via the web request
    if (current_station_index != station_index)
    {
        if (xSemaphoreTake(station_Mutex, pdMS_TO_TICKS(1000)))
        { 
            // Check if the index is valid before proceeding
            if (station_index < MAX_STATIONS)
            {
                // Print the current station details
                printf("Changing station to: %s\n", stations[station_index].name);  // Assuming stations[] contains station objects with 'name' field

                // Display the station name on OLED
                display_set_text("                ", 1, false);
                display_set_text(stations[station_index].name, 1, false);

                const char *uri = stations[station_index].url;  // Get the station's URL

                // Update the current station index
                current_station_index = station_index;

                ESP_LOGI(TAG, "Changing to station: %s", uri);

                // Stop current audio pipeline
                audio_pipeline_pause(pipeline);                 
                
                // Set new station URI
                audio_element_set_uri(http_stream_reader, uri);
                
                // Wait for the stop operation to complete
                audio_pipeline_wait_for_stop(pipeline);

                // Reset the decoder and I2S states
                audio_element_reset_state(mp3_decoder);
                audio_element_reset_state(i2s_stream_writer);

                // Reset pipeline buffers and item states
                audio_pipeline_reset_ringbuffer(pipeline);
                audio_pipeline_reset_items_state(pipeline);

                // Resume the pipeline
                audio_pipeline_resume(pipeline);

                // Update OLED again to show it's playing the new station
               
                

                xSemaphoreGive(station_Mutex);  // Release mutex
            }
            else
            {
                ESP_LOGE(TAG, "Invalid station index: %d", station_index);
            }
        }
        else
        {
            ESP_LOGI(TAG, "Failed to acquire mutex. Try again later.\n");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Station is already playing. Skipping.\n");
        
    }
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

void stream_task(void *arg)
{
    while (1)
    {
      
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "[ * ] Event interface error: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(100)); // Prevent busy looping
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT)
        {
            if (msg.source == (void *)mp3_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
            {
                audio_element_info_t music_info = {0};

                if (audio_element_getinfo(mp3_decoder, &music_info) == ESP_OK)
                {
                    ESP_LOGI(TAG, "[ * ] Music info: Sample Rate=%d, Bits=%d, Channels=%d",
                             music_info.sample_rates, music_info.bits, music_info.channels);

                    if (i2s_stream_writer) // Ensure i2s_stream_writer is valid
                    {
                        i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "[ * ] i2s_stream_writer is NULL!");
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "[ * ] Failed to get music info from mp3_decoder!");
                }
                continue;
            }

            if (msg.source == (void *)http_stream_reader && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int)msg.data == AEL_STATUS_ERROR_OPEN)
            {
                ESP_LOGW(TAG, "[ * ] Stream error detected, restarting pipeline...");

                // Ensure pipeline is valid before performing operations
                if (pipeline)
                {
                    audio_pipeline_stop(pipeline);

                    if (mp3_decoder)
                        audio_element_reset_state(mp3_decoder);
                    if (i2s_stream_writer)
                        audio_element_reset_state(i2s_stream_writer);

                    audio_pipeline_reset_ringbuffer(pipeline);
                    audio_pipeline_reset_items_state(pipeline);

                    if (audio_pipeline_run(pipeline) == ESP_OK)
                    {
                        ESP_LOGI(TAG, "[ * ] Stream restarted successfully.");
                    }
                    else
                    {
                        ESP_LOGE(TAG, "[ * ] Failed to restart pipeline.");
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "[ * ] Pipeline is NULL, cannot restart!");
                }
                continue;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Reduce CPU usage
    }
}

char *current_station_info(void)
{
    static char index_str[12]; // Increase buffer size to safely fit large integers and null terminator

    if (current_station_index < 0 || current_station_index >= station_count)
    {
        ESP_LOGE(TAG, "Invalid station index: %d", current_station_index);
        return NULL;
    }

    snprintf(index_str, sizeof(index_str), "%d", current_station_index);
    ESP_LOGI(TAG, "Current station index: %s", index_str);
    return index_str;
}
void audio_pause()
{
    audio_pipeline_pause(pipeline);
    display_set_text("                ", 1,false);
    display_set_text(" pause stream", 1,false);
}

void audio_resume()
{
    audio_pipeline_resume(pipeline);
    display_set_text("                ", 1,false);
    display_set_text(" playing stream", 1,false);
}
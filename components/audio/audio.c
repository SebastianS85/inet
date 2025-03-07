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

SemaphoreHandle_t station_Mutex;

#define NUM_STATIONS 27
const char *station_list[NUM_STATIONS] = {
    "https://icecast1.play.cz/kisshady64.mp3",                                                                         // kiss fm
    "https://sluchaj2.radiopark.biz.pl:8443/stream",                                                                   // park fm
    "http://c6.radioboss.fm:8207/stream",                                                                              //	ETHNIK
    "http://c26.radioboss.fm:8441/stream",                                                                             // spanisch cocktail
    "http://c6.radioboss.fm:8207/stream",                                                                              // Mykonos Scorpions
    "http://c15.radioboss.fm:8520/stream",                                                                             // Greek Taverna
    "http://c2.radioboss.fm:8438/stream",                                                                              // bar house
    "http://c2.radioboss.fm:8241/stream",                                                                              // chillout
    "http://c2.radioboss.fm:8671/stream",                                                                              // greek emporika
    "http://c26.radioboss.fm:8420/stream",                                                                             // greek vs ethnik
    "https://stream.sunshine-live.de/2000er/mp3-192/stream.sunshine-live.de/",                                         // sunshine
    "https://80er-90er.stream.laut.fm/80er-90er?ref=vtuner",                                                           // 90"
    "https://c15.radioboss.fm:8566/stream",                                                                            // fiesta mexico
    "http://31.192.216.8/rmf_fm",                                                                                      // rmff fm
    "https://ic2.smcdn.pl/1180-1.mp3",                                                                                 // eska
    "http://n32a-eu.rcs.revma.com/an1ugyygzk8uv?rj-ttl=5&rj-tok=AAABlT6ZdMEADfAg3Z-lirUfoA",                           // radio357
    "https://stream3.technologicznie.net/muzyczne_radio_192.mp3",                                                      // muzyczne
    "https://n09a-eu.rcs.revma.com/an1ugyygzk8uv?rj-ttl=5&rj-tok=AAABlDvCVcQAzCZhxrbQNf2GfQ",                          // ZET
    "https://27793.live.streamtheworld.com/ANTYRADIO.mp3?dist=myradioonline",                                          // Antyradio
    "https://kathy.torontocast.com:1190/stream",                                                                       // CINEMIX
    "https://c18.radioboss.fm:8061/stream",                                                                            // timeDanceFM
    "https://c34.radioboss.fm:8106/stream",                                                                            // Radio 857
    "https://c2.radioboss.fm:8224/320k.mp3",                                                                           // live house
    "https://c15.radioboss.fm:8512/stream",                                                                            // Playa Radio
    "https://radiosidewinder.out.airtime.pro:8000/radiosidewinder_b?_ga=1.133037898.513622194.1447957646/;stream.mp3", // Sidewinder
    "https://public.isekoi-radio.com/listen/isekoi/radio.mp3",                                                         // Isekoi radio
    "https://c34.radioboss.fm:8106/stream",                                                                             // Radio 897

};

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
    ;
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
    audio_element_set_uri(http_stream_reader, station_list[0]);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);
    display_set_text(" playing stream", 1,false);
}

void change_radio_station(uint8_t station_index)
{
    display_set_text("                ", 1,false);
    display_set_text("changing station", 1,false);
    if (current_station_index != station_index)
    {
        if (xSemaphoreTake(station_Mutex, pdMS_TO_TICKS(1000) && current_station_index != station_index))
        { // Wait max 5s
            printf("Changing station to: %s\n", station_list[station_index]);

            const char *uri = station_list[station_index];
            current_station_index = station_index; // Get next station URI
            ESP_LOGI(TAG, "Changing to station: %s", uri);

            audio_pipeline_pause(pipeline);                 // Stop current pipeline
            audio_element_set_uri(http_stream_reader, uri); // Set new station URI
            audio_pipeline_wait_for_stop(pipeline);         // Wait for stop to complete
            audio_element_reset_state(mp3_decoder);         // Reset decoder state
            audio_element_reset_state(i2s_stream_writer);   // Reset I2S state
            audio_pipeline_reset_ringbuffer(pipeline);      // Reset pipeline buffers
            audio_pipeline_reset_items_state(pipeline);     // Reset pipeline states
            audio_pipeline_resume(pipeline);
            display_set_text("                ", 1,false);
            display_set_text(" playing stream", 1,false);
            xSemaphoreGive(station_Mutex); // Release mutex
        }
        else
        {
            ESP_LOGI(TAG, "Failed to acquire mutex. Try again later.\n");
        }
    }

    else
    {
        ESP_LOGI(TAG, "Station is already playing. Skipping.\n");
        display_set_text("                ", 1,false);
        display_set_text(" playing stream", 1,false);
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
char index_str[10];

char *current_station_info(void)
{
    if (current_station_index < 0 || current_station_index >= sizeof(station_list) / sizeof(station_list[0]))
    {
        ESP_LOGE(TAG, "Invalid station index: %d", current_station_index);
        return NULL; // or a default value
    }


    snprintf(index_str, sizeof(index_str), "%d", current_station_index);
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
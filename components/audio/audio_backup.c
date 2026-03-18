/*
 * Audio Stream Management - ESP32 Internet Radio
 * Handles MP3 streaming via HTTP, pipeline control, and automatic recovery
 */

#include <stdio.h>
#include <stdbool.h>
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
#include "nvs_flash.h"
#include "nvs.h"
#include "common_wifi.h"
#include "ringbuf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <unistd.h>

static const char *TAG = "AUDIO STREAM";

/* ============================================================================ */
/* Configuration Constants */
/* ============================================================================ */
#define STATION_INDEX_KEY "station_idx"
#define STREAM_EVENT_WAIT_MS 1000              /* Event listener timeout */
#define STREAM_RESTART_COOLDOWN_MS 3000        /* Min time between restarts */
#define STREAM_EMPTY_BUFFER_TIMEOUT_MS 5000    /* I2S buffer empty threshold */
#define STREAM_HTTP_IDLE_TIMEOUT_MS 15000      /* HTTP data timeout */
#define STREAM_STARTUP_GRACE_MS 8000           /* Startup grace period */

/* Audio buffer size profiles */
#define AUDIO_BUFFER_PROFILE_NORMAL 1
#define AUDIO_BUFFER_PROFILE_STABLE_STREAM 2

#ifndef AUDIO_BUFFER_PROFILE
#define AUDIO_BUFFER_PROFILE AUDIO_BUFFER_PROFILE_STABLE_STREAM
#endif

/* Profile-based ringbuffer sizes */
#if AUDIO_BUFFER_PROFILE == AUDIO_BUFFER_PROFILE_NORMAL
#define HTTP_STREAM_RB_SIZE_BYTES (24 * 1024)
#define MP3_DECODER_RB_SIZE_BYTES (8 * 1024)
#define I2S_STREAM_RB_SIZE_BYTES (4 * 1024)
#elif AUDIO_BUFFER_PROFILE == AUDIO_BUFFER_PROFILE_STABLE_STREAM
#define HTTP_STREAM_RB_SIZE_BYTES (64 * 1024)  /* Larger for network stability */
#define MP3_DECODER_RB_SIZE_BYTES (16 * 1024)
#define I2S_STREAM_RB_SIZE_BYTES (8 * 1024)
#else
#error "Unsupported AUDIO_BUFFER_PROFILE value"
#endif

/* ============================================================================ */
/* Station Data */
/* ============================================================================ */
Station stations[MAX_STATIONS];
int station_count = 0;

/* ============================================================================ */
/* Station Persistence (NVS Flash) */
/* ============================================================================ */

/**
 * Save the current station index to NVS flash
 */
esp_err_t save_station_index_to_nvs(int index)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_i32(nvs_handle, STATION_INDEX_KEY, index);
    if (err == ESP_OK)
        err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

/**
 * Load the saved station index from NVS flash
 */
int load_station_index_from_nvs(int default_index)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
        return default_index;
    int32_t index = 0;
    err = nvs_get_i32(nvs_handle, STATION_INDEX_KEY, &index);
    nvs_close(nvs_handle);
    return (err == ESP_OK) ? index : default_index;
}

/**
 * Load all radio stations from /store/stations.txt
 * Format: name|url|genre (one per line)
 */
void load_stations()
{
    station_count = 0; // Reset station count to avoid appending to old data
    FILE *file = fopen("/store/stations.txt", "r");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt");
        return;
    }

    char line[512];

    while (fgets(line, sizeof(line), file) && station_count < MAX_STATIONS)
    {
        // Trim spaces (optional)
        char *newline_pos = strchr(line, '\n');
        if (newline_pos)
            *newline_pos = '\0'; // Remove newline

        // Parse name, URL, and genre separated by '|'
        if (sscanf(line, "%127[^|]|%255[^|]|%63[^\n]", stations[station_count].name, stations[station_count].url, stations[station_count].genre) == 3)
        {
            if (strlen(stations[station_count].name) > 0 && strlen(stations[station_count].url) > 0 && strlen(stations[station_count].genre) > 0)
            {
                stations[station_count].index = station_count; // Assign index dynamically
                station_count++;
            }
            else
            {
                ESP_LOGW("STATIONS", "Skipping invalid entry: %s", line);
            }
        }
        else
        {
            ESP_LOGW("STATIONS", "Failed to parse line: %s", line);
        }
    }

    fclose(file);

    if (station_count == 0)
    {
        ESP_LOGW("STATIONS", "No stations loaded from the file.");
    }
    else
    {
        ESP_LOGI("STATIONS", "Successfully loaded %d stations.", station_count);
    }
}

/* ============================================================================ */
/* Global State and Synchronization */
/* ============================================================================ */

SemaphoreHandle_t station_Mutex;  /* Protects station index changes */

/* Audio pipeline components */
int current_station_index = 0;
audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder;
audio_event_iface_handle_t evt;

/* Stream state tracking */
static bool audio_user_paused = false;
static TickType_t last_http_activity_tick = 0;      /* Last HTTP data received */
static TickType_t last_i2s_data_tick = 0;           /* Last PCM data to I2S */
static TickType_t last_stream_restart_tick = 0;     /* Last restart timestamp */
static TickType_t last_pipeline_run_tick = 0;       /* Last pipeline start */

/* ============================================================================ */
/* Utility Functions */
/* ============================================================================ */

/**
 * Calculate milliseconds elapsed since a timestamp
 * Returns -1 if timestamp is zero (not set)
 */
static int elapsed_ms_since(TickType_t from_tick)
{
    if (from_tick == 0)
    {
        return -1;
    }
    return (int)pdTICKS_TO_MS(xTaskGetTickCount() - from_tick);
}

/**
 * Mark that stream activity just occurred (HTTP or PCM data)
 */
static void mark_stream_activity(void)
{
    TickType_t now = xTaskGetTickCount();
    last_http_activity_tick = now;
    last_i2s_data_tick = now;
}

/**
 * Check if restart cooldown period is still active
 * Prevents rapid restart loops
 */
static bool restart_cooldown_active(void)
{
    TickType_t now = xTaskGetTickCount();
    return last_stream_restart_tick != 0 &&
           (now - last_stream_restart_tick) < pdMS_TO_TICKS(STREAM_RESTART_COOLDOWN_MS);
}

/**
 * Check if startup grace period is still active
 * Ignores element state errors during initial connection
 */
static bool startup_grace_active(void)
{
    TickType_t now = xTaskGetTickCount();
    return last_pipeline_run_tick != 0 &&
           (now - last_pipeline_run_tick) < pdMS_TO_TICKS(STREAM_STARTUP_GRACE_MS);
}

/* ============================================================================ */
/* Stream Restart and Recovery */
/* ============================================================================ */

/**
 * Internal function to restart the audio stream for a specific station
 * Assumes station_Mutex is already acquired
 * Handles safe pipeline stop with timeout fallbacks
 */
static esp_err_t restart_stream_for_station_locked(int station_index, const char *reason)
{
    if (!pipeline || !http_stream_reader || !mp3_decoder || !i2s_stream_writer)
    {
        ESP_LOGE(TAG, "Cannot restart stream, audio pipeline is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (station_index < 0 || station_index >= station_count)
    {
        ESP_LOGE(TAG, "Cannot restart stream, invalid station index: %d", station_index);
        return ESP_ERR_INVALID_ARG;
    }

    const char *uri = stations[station_index].url;
    ESP_LOGW(TAG, "Restarting stream for station %d (%s), reason: %s",
             station_index, stations[station_index].name, reason);

    last_stream_restart_tick = xTaskGetTickCount();
    last_http_activity_tick = last_stream_restart_tick;
    last_i2s_data_tick = last_stream_restart_tick;
    audio_user_paused = false;

    audio_pipeline_stop(pipeline);
    esp_err_t stop_ret = audio_pipeline_wait_for_stop_with_ticks(pipeline, pdMS_TO_TICKS(3000));
    if (stop_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Pipeline stop wait timeout, forcing terminate: %d", stop_ret);
        stop_ret = audio_pipeline_terminate_with_ticks(pipeline, pdMS_TO_TICKS(2000));
        if (stop_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Pipeline terminate with ticks failed, forcing terminate: %d", stop_ret);
            audio_pipeline_terminate(pipeline);
        }
    }

    audio_element_reset_state(http_stream_reader);
    audio_element_reset_state(mp3_decoder);
    audio_element_reset_state(i2s_stream_writer);

    audio_pipeline_reset_elements(pipeline);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_items_state(pipeline);

    esp_err_t err = audio_element_set_uri(http_stream_reader, uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set station URI during restart: %s", uri);
        return err;
    }

    err = audio_pipeline_run(pipeline);
    if (err == ESP_OK)
    {
        if (evt)
        {
            audio_event_iface_discard(evt);
        }
        last_pipeline_run_tick = xTaskGetTickCount();
        display_set_text("                ", 1, false);
        display_set_text(stations[station_index].name, 1, false);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to restart audio pipeline: %d", err);
    }
    return err;
}

/**
 * Log detailed diagnostics before restart
 * Shows element states, idle times, buffer levels, and suspect component
 */
static void log_stream_diagnostics(const char *reason)
{
    audio_element_state_t http_state = http_stream_reader ? audio_element_get_state(http_stream_reader) : AEL_STATE_NONE;
    audio_element_state_t mp3_state = mp3_decoder ? audio_element_get_state(mp3_decoder) : AEL_STATE_NONE;
    audio_element_state_t i2s_state = i2s_stream_writer ? audio_element_get_state(i2s_stream_writer) : AEL_STATE_NONE;
    const char *suspect = "unknown";
    int http_idle_ms = elapsed_ms_since(last_http_activity_tick);
    int pcm_idle_ms = elapsed_ms_since(last_i2s_data_tick);
    int i2s_rb_filled = -1;
    int i2s_rb_size = -1;

    if (i2s_stream_writer)
    {
        ringbuf_handle_t i2s_input_rb = audio_element_get_input_ringbuf(i2s_stream_writer);
        if (i2s_input_rb)
        {
            i2s_rb_filled = rb_bytes_filled(i2s_input_rb);
            i2s_rb_size = rb_get_size(i2s_input_rb);
        }
    }

    if (http_state == AEL_STATE_ERROR || mp3_state == AEL_STATE_ERROR || i2s_state == AEL_STATE_ERROR)
    {
        suspect = "element_state_error";
    }
    else if (http_state == AEL_STATE_STOPPED || http_state == AEL_STATE_FINISHED ||
             mp3_state == AEL_STATE_STOPPED || mp3_state == AEL_STATE_FINISHED ||
             i2s_state == AEL_STATE_STOPPED || i2s_state == AEL_STATE_FINISHED)
    {
        suspect = "element_state_stopped_or_finished";
    }
    else if (http_idle_ms >= STREAM_HTTP_IDLE_TIMEOUT_MS)
    {
        suspect = "http_idle_timeout";
    }
    else if (i2s_rb_filled == 0 && pcm_idle_ms >= STREAM_EMPTY_BUFFER_TIMEOUT_MS)
    {
        suspect = "i2s_ringbuffer_empty_timeout";
    }

    ESP_LOGW(TAG,
             "Restart diagnostics: reason=%s suspect=%s http_state=%d mp3_state=%d i2s_state=%d http_idle_ms=%d pcm_idle_ms=%d i2s_rb_filled=%d i2s_rb_size=%d station=%d",
             reason,
             suspect,
             http_state,
             mp3_state,
             i2s_state,
             http_idle_ms,
             pcm_idle_ms,
             i2s_rb_filled,
             i2s_rb_size,
             current_station_index);
}

/**
 * Request stream restart with proper locking and diagnostics
 * Public interface for restart requests
 */
static esp_err_t restart_current_stream(const char *reason)
{
    if (audio_user_paused)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (restart_cooldown_active())
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(station_Mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire station mutex for stream restart");
        return ESP_ERR_TIMEOUT;
    }

    log_stream_diagnostics(reason);
    esp_err_t err = restart_stream_for_station_locked(current_station_index, reason);
    xSemaphoreGive(station_Mutex);
    return err;
}

/* ============================================================================ */
/* Automatic Stream Recovery Watchdog */
/* ============================================================================ */

/**
 * Check if stream needs recovery and return restart reason
 * Returns NULL if stream is healthy, or reason string otherwise
 * Checks: element errors, HTTP timeout, I2S buffer empty
 */
static const char *stream_watchdog_restart_reason(void)
{
    if (audio_user_paused || restart_cooldown_active())
    {
        return NULL;
    }

    if (!http_stream_reader || !mp3_decoder || !i2s_stream_writer)
    {
        return NULL;
    }

    if (startup_grace_active())
    {
        return NULL;
    }

    audio_element_state_t http_state = audio_element_get_state(http_stream_reader);
    audio_element_state_t mp3_state = audio_element_get_state(mp3_decoder);
    audio_element_state_t i2s_state = audio_element_get_state(i2s_stream_writer);
    TickType_t now = xTaskGetTickCount();

    if (http_state == AEL_STATE_ERROR || mp3_state == AEL_STATE_ERROR || i2s_state == AEL_STATE_ERROR)
    {
        return "watchdog: element entered error state";
    }

    if (http_state == AEL_STATE_STOPPED || http_state == AEL_STATE_FINISHED ||
        mp3_state == AEL_STATE_STOPPED || mp3_state == AEL_STATE_FINISHED ||
        i2s_state == AEL_STATE_STOPPED || i2s_state == AEL_STATE_FINISHED)
    {
        return "watchdog: element stopped or finished unexpectedly";
    }

    if (last_http_activity_tick != 0 &&
        (now - last_http_activity_tick) >= pdMS_TO_TICKS(STREAM_HTTP_IDLE_TIMEOUT_MS))
    {
        return "watchdog: no HTTP activity timeout";
    }

    ringbuf_handle_t i2s_input_rb = audio_element_get_input_ringbuf(i2s_stream_writer);
    if (!i2s_input_rb)
    {
        return NULL;
    }

    if (rb_bytes_filled(i2s_input_rb) > 0)
    {
        last_i2s_data_tick = now;
        return NULL;
    }

    if (last_i2s_data_tick == 0)
    {
        last_i2s_data_tick = now;
        return NULL;
    }

    if ((now - last_i2s_data_tick) >= pdMS_TO_TICKS(STREAM_EMPTY_BUFFER_TIMEOUT_MS))
    {
        return "watchdog: i2s input ringbuffer empty timeout";
    }

    return NULL;
}

/* ============================================================================ */
/* Audio Pipeline Initialization */
/* ============================================================================ */

/**
 * Initialize audio codec and create the ADF pipeline
 * Creates elements: http_stream_reader -> mp3_decoder -> i2s_stream_writer
 * Configures ringbuffers based on selected profile
 */
void audio_init(void)
{

    station_Mutex = xSemaphoreCreateMutex();
    if (station_Mutex == NULL)
    {
        printf("Failed to create mutex!\n");
    }

    ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");
    ESP_LOGI(TAG,
             "Audio buffers profile=%d, http_rb=%d, mp3_rb=%d, i2s_rb=%d",
             AUDIO_BUFFER_PROFILE,
             HTTP_STREAM_RB_SIZE_BYTES,
             MP3_DECODER_RB_SIZE_BYTES,
             I2S_STREAM_RB_SIZE_BYTES);
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
    http_cfg.out_rb_size = HTTP_STREAM_RB_SIZE_BYTES;

    http_stream_reader = http_stream_init(&http_cfg);
    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.task_prio = 8;
    i2s_cfg.out_rb_size = I2S_STREAM_RB_SIZE_BYTES;

    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.3] Create aac decoder to decode aac file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.out_rb_size = MP3_DECODER_RB_SIZE_BYTES;

    mp3_decoder = mp3_decoder_init(&mp3_cfg);
}

/**
 * Connect pipeline elements and start streaming from saved or default station
 * Sets up event listeners and begins audio playback
 */
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
    current_station_index = load_station_index_from_nvs(0);
    ESP_LOGE(TAG, "uri: %s", stations[current_station_index].url);
    audio_element_set_uri(http_stream_reader, stations[current_station_index].url);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt_cfg.external_queue_size = 64;
    evt_cfg.internal_queue_size = 64;
    evt_cfg.queue_set_size = 64;
    evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    mark_stream_activity();
    audio_pipeline_run(pipeline);
    last_pipeline_run_tick = xTaskGetTickCount();
    display_set_text(stations[current_station_index].name, 1, false);
}

/* ============================================================================ */
/* Station File Operations */
/* ============================================================================ */

/**
 * Add a new radio station to /store/stations.txt
 * Reloads station list after adding
 */
esp_err_t add_station_to_file(const char *name, const char *url, const char *genre)
{
    FILE *file = fopen("/store/stations.txt", "a+");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt for appending, errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    // Ensure file ends with a newline before appending
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    if (filesize > 0)
    {
        fseek(file, -1, SEEK_END);
        int last = fgetc(file);
        if (last != '\n')
        {
            fputc('\n', file);
        }
    }
    // Write in the expected format
    int written = fprintf(file, "%s|%s|%s\n", name, url, genre);
    fflush(file);
    int fd = fileno(file);
    if (fd >= 0)
    {
        fsync(fd);
    }
    fclose(file);
    if (written < 0)
    {
        ESP_LOGE("STATIONS", "Failed to write to stations.txt, errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI("STATIONS", "Added new station: %s", name);
    load_stations();
    return ESP_OK;
}

/**
 * Delete a station from /store/stations.txt by index
 * Reloads station list after deletion
 */
esp_err_t delete_station_from_file(int index_to_delete)
{
    FILE *file = fopen("/store/stations.txt", "r");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt for reading");
        return ESP_FAIL;
    }

    // Read all lines into a dynamic array
    char **lines = NULL;
    size_t count = 0;
    size_t capacity = 32;
    lines = malloc(capacity * sizeof(char *));
    if (!lines)
    {
        fclose(file);
        ESP_LOGE("STATIONS", "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), file))
    {
        size_t len = strlen(buf);
        if (len == 0 || (len == 1 && buf[0] == '\n'))
            continue; // skip blank lines
        if (count >= capacity)
        {
            capacity *= 2;
            char **new_lines = realloc(lines, capacity * sizeof(char *));
            if (!new_lines)
            {
                for (size_t i = 0; i < count; ++i)
                    free(lines[i]);
                free(lines);
                fclose(file);
                ESP_LOGE("STATIONS", "Memory allocation failed");
                return ESP_ERR_NO_MEM;
            }
            lines = new_lines;
        }
        lines[count] = strdup(buf);
        if (!lines[count])
        {
            for (size_t i = 0; i < count; ++i)
                free(lines[i]);
            free(lines);
            fclose(file);
            ESP_LOGE("STATIONS", "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
        count++;
    }
    fclose(file);

    if (index_to_delete < 0 || (size_t)index_to_delete >= count)
    {
        ESP_LOGE("STATIONS", "Invalid index to delete: %d", index_to_delete);
        for (size_t i = 0; i < count; ++i)
            free(lines[i]);
        free(lines);
        return ESP_ERR_INVALID_ARG;
    }

    // Write back all except the one to delete
    file = fopen("/store/stations.txt", "w");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt for writing");
        for (size_t i = 0; i < count; ++i)
            free(lines[i]);
        free(lines);
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if ((int)i != index_to_delete)
        {
            fputs(lines[i], file);
            size_t l = strlen(lines[i]);
            if (l == 0 || lines[i][l - 1] != '\n')
                fputc('\n', file);
        }
    }
    fflush(file);
    int fd = fileno(file);
    if (fd >= 0)
        fsync(fd);
    fclose(file);
    // Free all lines after writing
    for (size_t i = 0; i < count; ++i)
    {
        free(lines[i]);
    }
    free(lines);
    ESP_LOGI("STATIONS", "Deleted station at index: %d", index_to_delete);
    load_stations();
    return ESP_OK;
}

/* ============================================================================ */
/* Station Control */
/* ============================================================================ */

/**
 * Change to a different radio station
 * Saves selection to NVS and restarts stream
 */
void change_radio_station(uint8_t station_index)
{

    // Assuming station_list is populated via the web request
    if (current_station_index != station_index)
    {
        if (xSemaphoreTake(station_Mutex, pdMS_TO_TICKS(1000)))
        {
            // Check if the index is valid before proceeding
            if (station_index < station_count)
            {
                // Print the current station details
                printf("Changing station to: %s\n", stations[station_index].name); // Assuming stations[] contains station objects with 'name' field

                // Display the station name on OLED
                display_set_text("                ", 1, false);
                display_set_text(stations[station_index].name, 1, false);

                // Update the current station index
                current_station_index = station_index;

                esp_err_t err = restart_stream_for_station_locked(current_station_index, "station change");
                if (err == ESP_OK)
                {
                    save_station_index_to_nvs(current_station_index);
                }

                xSemaphoreGive(station_Mutex); // Release mutex
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

/* ============================================================================ */
/* HTTP Stream Event Handler */
/* ============================================================================ */

/**
 * Handle HTTP stream events (request/response/track transitions)
 * Marks HTTP activity and handles playlist/stream transitions
 */
int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_PRE_REQUEST ||
        msg->event_id == HTTP_STREAM_ON_RESPONSE ||
        msg->event_id == HTTP_STREAM_FINISH_REQUEST)
    {
        last_http_activity_tick = xTaskGetTickCount();
    }

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

/* ============================================================================ */
/* Main Audio Event Loop Task */
/* ============================================================================ */

/**
 * FreeRTOS task that manages the audio pipeline
 * Listens for AEL events and triggers watchdog recovery when needed
 * Runs on a 1-second event timeout for periodic health checks
 */
void stream_task(void *arg)
{
    while (1)
    {

        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, pdMS_TO_TICKS(STREAM_EVENT_WAIT_MS));
        if (ret == ESP_ERR_TIMEOUT || ret == ESP_FAIL)
        {
            const char *watchdog_reason = stream_watchdog_restart_reason();
            if (watchdog_reason)
            {
                restart_current_stream(watchdog_reason);
            }
            continue;
        }

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
                    last_i2s_data_tick = xTaskGetTickCount();
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

            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS)
            {
                int status = (int)msg.data;

                if (msg.source == (void *)http_stream_reader)
                {
                    if (status == AEL_STATUS_INPUT_BUFFERING || status == AEL_STATUS_STATE_RUNNING)
                    {
                        last_http_activity_tick = xTaskGetTickCount();
                    }

                    if (status == AEL_STATUS_ERROR_OPEN ||
                        status == AEL_STATUS_ERROR_INPUT ||
                        status == AEL_STATUS_ERROR_TIMEOUT ||
                        status == AEL_STATUS_ERROR_CLOSE ||
                        status == AEL_STATUS_ERROR_UNKNOWN ||
                        status == AEL_STATUS_STATE_STOPPED ||
                        status == AEL_STATUS_STATE_FINISHED)
                    {
                        restart_current_stream("http stream closed or returned an error");
                        continue;
                    }
                }

                if ((msg.source == (void *)mp3_decoder || msg.source == (void *)i2s_stream_writer) &&
                    (status == AEL_STATUS_ERROR_PROCESS ||
                     status == AEL_STATUS_ERROR_OUTPUT ||
                     status == AEL_STATUS_STATE_STOPPED ||
                     status == AEL_STATUS_STATE_FINISHED))
                {
                    if (startup_grace_active())
                    {
                        continue;
                    }
                    restart_current_stream("decoder or i2s writer stopped producing samples");
                    continue;
                }
            }
        }

        const char *watchdog_reason = stream_watchdog_restart_reason();
        if (watchdog_reason)
        {
            restart_current_stream(watchdog_reason);
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Reduce CPU usage
    }
}

/**
 * Get the current station index as a string
 * Used for REST API responses
 */
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

/**
 * Pause audio playback
 */
void audio_pause()
{
    audio_user_paused = true;
    audio_pipeline_pause(pipeline);
}

/**
 * Resume audio playback
 */
void audio_resume()
{
    audio_user_paused = false;
    mark_stream_activity();
    audio_pipeline_resume(pipeline);
}
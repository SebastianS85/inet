/*
 * Audio Stream Management - ESP32 Internet Radio
 * Handles MP3 streaming via HTTP, pipeline control, and automatic recovery
 */

/* ============================================================================ */
/* Standard Library Includes */
/* ============================================================================ */
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

/* ============================================================================ */
/* FreeRTOS Includes */
/* ============================================================================ */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* ============================================================================ */
/* ESP-IDF and ESP-ADF Includes */
/* ============================================================================ */
#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"

/* ============================================================================ */
/* Audio Development Framework (ADF) Includes */
/* ============================================================================ */
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "aac_decoder.h"
#include "ringbuf.h"

/* ============================================================================ */
/* Project-Specific Includes */
/* ============================================================================ */
#include "audio.h"
#include "board.h"
#include "display.h"
#include "common_wifi.h"

static const char *TAG = "AUDIO STREAM";

/* ============================================================================ */
/* Configuration: Timing Constants */
/* ============================================================================ */
#define STATION_INDEX_KEY "station_idx"
#define STREAM_EVENT_WAIT_MS 1000              /* Event listener timeout */
#define STREAM_RESTART_COOLDOWN_MS 3000        /* Min time between restarts */
#define STREAM_EMPTY_BUFFER_TIMEOUT_MS 5000    /* I2S buffer empty threshold */
#define STREAM_HTTP_IDLE_TIMEOUT_MS 15000      /* HTTP data timeout */
#define STREAM_STARTUP_GRACE_MS 8000           /* Startup grace period */

/* ============================================================================ */
/* Configuration: Audio Buffer Profiles */
/* ============================================================================ */
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
/* Global Variables: Station Data */
/* ============================================================================ */
Station stations[MAX_STATIONS];
int station_count = 0;

/* ============================================================================ */
/* Global Variables: Synchronization */
/* ============================================================================ */
SemaphoreHandle_t station_Mutex;  /* Protects station index and restart operations */

/* ============================================================================ */
/* Global Variables: Audio Pipeline Components */
/* ============================================================================ */
int current_station_index = 0;              /* Currently playing station */
audio_pipeline_handle_t pipeline;           /* Main audio pipeline */
audio_element_handle_t http_stream_reader;  /* HTTP stream input */
audio_element_handle_t mp3_decoder;         /* MP3 decoding element */
audio_element_handle_t i2s_stream_writer;   /* I2S/DAC output */
audio_event_iface_handle_t evt;             /* Event interface for pipeline */

/* ============================================================================ */
/* Global Variables: Stream State Tracking */
/* ============================================================================ */
static bool audio_user_paused = false;                 /* User-initiated pause */
static TickType_t last_http_activity_tick = 0;        /* Last HTTP data received tick */
static TickType_t last_i2s_data_tick = 0;             /* Last PCM data written tick */
static TickType_t last_stream_restart_tick = 0;       /* Last stream restart tick */
static TickType_t last_pipeline_run_tick = 0;         /* Last pipeline.run() tick */

/* Dedicated restart task — keeps stream_task free to drain the event queue */
static TaskHandle_t s_restart_task_handle = NULL;
static volatile bool s_restart_in_progress = false;
static QueueHandle_t s_restart_queue = NULL;
static int s_consecutive_restart_failures = 0;

typedef enum {
    RESTART_REQ_WATCHDOG = 0,
    RESTART_REQ_STATION_CHANGE,
} restart_req_type_t;

typedef struct {
    restart_req_type_t type;
    int station_index;
    char reason[64];
} restart_req_t;

/* ============================================================================ */
/* Utility: Time Calculations */
/* ============================================================================ */

/**
 * Calculate milliseconds elapsed since a timestamp
 * @param from_tick Timestamp to calculate from
 * @return Elapsed milliseconds, or -1 if timestamp is zero
 */
static int elapsed_ms_since(TickType_t from_tick)
{
    if (from_tick == 0)
        return -1;
    return (int)pdTICKS_TO_MS(xTaskGetTickCount() - from_tick);
}

/**
 * Mark that stream activity just occurred (HTTP or PCM data)
 * Updates both HTTP and I2S activity timestamps
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
/* Station NVS Persistence */
/* ============================================================================ */

/**
 * Save the current station index to NVS flash
 * @param index Station index to save
 * @return ESP_OK on success
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
 * @param default_index Index to return if NVS read fails
 * @return Saved or default station index
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
 * Logs errors and warnings for invalid entries
 */
void load_stations(void)
{
    station_count = 0;
    FILE *file = fopen("/store/stations.txt", "r");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), file) && station_count < MAX_STATIONS)
    {
        char *newline_pos = strchr(line, '\n');
        if (newline_pos)
            *newline_pos = '\0';

        if (sscanf(line, "%127[^|]|%255[^|]|%63[^\n]",
                   stations[station_count].name,
                   stations[station_count].url,
                   stations[station_count].genre) == 3)
        {
            if (strlen(stations[station_count].name) > 0 &&
                strlen(stations[station_count].url) > 0 &&
                strlen(stations[station_count].genre) > 0)
            {
                stations[station_count].index = station_count;
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
/* Station File Operations */
/* ============================================================================ */

/**
 * Add a new radio station to /store/stations.txt
 * Ensures proper newline handling and reloads station list
 * @param name Station display name
 * @param url Stream URL
 * @param genre Station genre/category
 * @return ESP_OK on success, ESP_FAIL on file error
 */
esp_err_t add_station_to_file(const char *name, const char *url, const char *genre)
{
    FILE *file = fopen("/store/stations.txt", "a+");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt for appending, errno=%d (%s)",
                 errno, strerror(errno));
        return ESP_FAIL;
    }

    /* Ensure file ends with newline before appending */
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    if (filesize > 0)
    {
        fseek(file, -1, SEEK_END);
        int last = fgetc(file);
        if (last != '\n')
            fputc('\n', file);
    }

    /* Write new station in expected format */
    int written = fprintf(file, "%s|%s|%s\n", name, url, genre);
    fflush(file);
    int fd = fileno(file);
    if (fd >= 0)
        fsync(fd);
    fclose(file);

    if (written < 0)
    {
        ESP_LOGE("STATIONS", "Failed to write to stations.txt, errno=%d (%s)",
                 errno, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI("STATIONS", "Added new station: %s", name);
    load_stations();
    return ESP_OK;
}

/**
 * Delete a station from /store/stations.txt by index
 * Rewrites file without the deleted entry
 * @param index_to_delete Station index to remove
 * @return ESP_OK on success, ESP_FAIL on file error
 */
esp_err_t delete_station_from_file(int index_to_delete)
{
    FILE *file = fopen("/store/stations.txt", "r");
    if (!file)
    {
        ESP_LOGE("STATIONS", "Failed to open stations.txt for reading");
        return ESP_FAIL;
    }

    /* Read all lines into dynamic array */
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
            continue;

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

    /* Validate index */
    if (index_to_delete < 0 || (size_t)index_to_delete >= count)
    {
        ESP_LOGE("STATIONS", "Invalid index to delete: %d", index_to_delete);
        for (size_t i = 0; i < count; ++i)
            free(lines[i]);
        free(lines);
        return ESP_ERR_INVALID_ARG;
    }

    /* Rewrite file without deleted entry */
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

    /* Free memory */
    for (size_t i = 0; i < count; ++i)
        free(lines[i]);
    free(lines);

    ESP_LOGI("STATIONS", "Deleted station at index: %d", index_to_delete);
    load_stations();
    return ESP_OK;
}

/* ============================================================================ */
/* Stream Recovery: Diagnostic Logging */
/* ============================================================================ */

/**
 * Log detailed diagnostics before restart
 * Shows element states, idle times, buffer levels, and suspect component
 * Used to identify root cause of stream failures
 */
static void log_stream_diagnostics(const char *reason)
{
    audio_element_state_t http_state = http_stream_reader ? 
        audio_element_get_state(http_stream_reader) : AEL_STATE_NONE;
    audio_element_state_t mp3_state = mp3_decoder ? 
        audio_element_get_state(mp3_decoder) : AEL_STATE_NONE;
    audio_element_state_t i2s_state = i2s_stream_writer ? 
        audio_element_get_state(i2s_stream_writer) : AEL_STATE_NONE;

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

    /* Determine suspect component */
    if (http_state == AEL_STATE_ERROR || mp3_state == AEL_STATE_ERROR || 
        i2s_state == AEL_STATE_ERROR)
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
             "Restart diagnostics: reason=%s suspect=%s http_state=%d mp3_state=%d "
             "i2s_state=%d http_idle_ms=%d pcm_idle_ms=%d i2s_rb_filled=%d "
             "i2s_rb_size=%d station=%d",
             reason, suspect, http_state, mp3_state, i2s_state,
             http_idle_ms, pcm_idle_ms, i2s_rb_filled, i2s_rb_size,
             current_station_index);
}

/**
 * Stop/terminate pipeline with fallback and post-check.
 * Returns ESP_OK only when pipeline threads are confirmed stopped.
 */
static esp_err_t stop_pipeline_safely(const char *context)
{
    if (!pipeline)
        return ESP_ERR_INVALID_STATE;

    audio_pipeline_stop(pipeline);

    esp_err_t stop_ret = audio_pipeline_wait_for_stop_with_ticks(pipeline, pdMS_TO_TICKS(3000));
    if (stop_ret == ESP_OK)
        return ESP_OK;

    ESP_LOGW(TAG, "%s: pipeline stop wait timeout, forcing terminate: %d", context, stop_ret);

    stop_ret = audio_pipeline_terminate_with_ticks(pipeline, pdMS_TO_TICKS(2000));
    if (stop_ret == ESP_OK)
        return ESP_OK;

    ESP_LOGW(TAG, "%s: terminate with ticks failed, forcing terminate: %d", context, stop_ret);
    audio_pipeline_terminate(pipeline);

    /* Verify that force-terminate actually stopped all element tasks. */
    stop_ret = audio_pipeline_wait_for_stop_with_ticks(pipeline, pdMS_TO_TICKS(800));
    if (stop_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: pipeline still not stopped after force terminate: %d", context, stop_ret);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* ============================================================================ */
/* Stream Recovery: Restart Logic */
/* ============================================================================ */

/**
 * Internal function to restart the audio stream for a specific station
 * Assumes station_Mutex is already acquired
 * Handles safe pipeline stop with timeout fallbacks
 * @param station_index Station to restart
 * @param reason String describing why restart was triggered
 * @return ESP_OK on success
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

    /* Update activity timestamps */
    last_stream_restart_tick = xTaskGetTickCount();
    last_http_activity_tick = last_stream_restart_tick;
    last_i2s_data_tick = last_stream_restart_tick;
    audio_user_paused = false;

    /* Drop stale events before restart to reduce queue pressure during teardown */
    if (evt)
        audio_event_iface_discard(evt);

    /* Stop pipeline before resetting/running it again. */
    esp_err_t stop_ret = stop_pipeline_safely("Restart");
    if (stop_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Restart aborted: pipeline did not stop cleanly");
        return stop_ret;
    }

    /* Reset all elements */
    audio_element_reset_state(http_stream_reader);
    audio_element_reset_state(mp3_decoder);
    audio_element_reset_state(i2s_stream_writer);

    audio_pipeline_reset_elements(pipeline);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_items_state(pipeline);

    /* Set new URI and restart */
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
            audio_event_iface_discard(evt);
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
 * Full pipeline teardown and recreation.
 * Called when the normal restart sequence fails repeatedly because the pipeline
 * is stuck in an unrecoverable internal state (e.g. state:7 / RUNNING with no
 * element tasks alive) that audio_pipeline_stop/terminate cannot escape.
 * Destroys all ADF objects and builds a fresh pipeline from scratch.
 */
static esp_err_t full_pipeline_recover(int station_index)
{
    ESP_LOGW(TAG, "Full pipeline recovery: teardown + recreate for station %d (%s)",
             station_index,
             (station_index >= 0 && station_index < station_count)
                 ? stations[station_index].name : "?");

    /* Best-effort stop before teardown to avoid destroy-command failures. */
    if (evt)
        audio_event_iface_discard(evt);
    if (pipeline)
        stop_pipeline_safely("Full recovery");

    /* Detach event listener before destroying so evt queue stays valid */
    if (pipeline && evt)
        audio_pipeline_remove_listener(pipeline);

    /* Force-destroy pipeline; warns internally but frees the struct */
    if (pipeline)
    {
        audio_pipeline_deinit(pipeline);
        pipeline = NULL;
    }

    /* Free elements — audio_pipeline_deinit only frees list nodes, not elements */
    if (http_stream_reader) { audio_element_deinit(http_stream_reader); http_stream_reader = NULL; }
    if (mp3_decoder)        { audio_element_deinit(mp3_decoder);        mp3_decoder        = NULL; }
    if (i2s_stream_writer)  { audio_element_deinit(i2s_stream_writer);  i2s_stream_writer  = NULL; }

    /* Let FreeRTOS reap any lingering zombie tasks */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* --- Recreate pipeline --- */
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline)
    {
        ESP_LOGE(TAG, "Full recovery: failed to create pipeline");
        return ESP_FAIL;
    }

    /* Recreate HTTP stream element */
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    http_cfg.out_rb_size = HTTP_STREAM_RB_SIZE_BYTES;
    http_stream_reader = http_stream_init(&http_cfg);

    /* Recreate I2S stream element */
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.task_prio = 8;
    i2s_cfg.out_rb_size = I2S_STREAM_RB_SIZE_BYTES;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    /* Recreate MP3 decoder element */
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.out_rb_size = MP3_DECODER_RB_SIZE_BYTES;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    if (!http_stream_reader || !mp3_decoder || !i2s_stream_writer)
    {
        ESP_LOGE(TAG, "Full recovery: failed to create audio elements");
        return ESP_FAIL;
    }

    /* Register and link elements: http -> mp3 -> i2s */
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    /* Reattach existing event listener to the new pipeline */
    if (evt)
    {
        audio_pipeline_set_listener(pipeline, evt);
        audio_event_iface_discard(evt);
    }

    /* Set URI and run fresh pipeline */
    int safe_idx = (station_index >= 0 && station_index < station_count) ? station_index : 0;
    const char *uri = stations[safe_idx].url;
    esp_err_t err = audio_element_set_uri(http_stream_reader, uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Full recovery: failed to set URI: %d", err);
        return err;
    }

    err = audio_pipeline_run(pipeline);
    if (err == ESP_OK)
    {
        last_pipeline_run_tick = xTaskGetTickCount();
        last_stream_restart_tick = last_pipeline_run_tick;
        mark_stream_activity();
        display_set_text("                ", 1, false);
        display_set_text(stations[safe_idx].name, 1, false);
        ESP_LOGI(TAG, "Full recovery successful, now playing station %d (%s)",
                 safe_idx, stations[safe_idx].name);
    }
    else
    {
        ESP_LOGE(TAG, "Full recovery: audio_pipeline_run also failed: %d", err);
    }
    return err;
}

/**
 * Worker task that performs the blocking pipeline stop/restart sequence.
 * Runs separately from stream_task so the event queue stays drained during restart.
 */
static void audio_restart_task(void *arg)
{
    (void)arg;
    while (1)
    {
        restart_req_t req = {0};
        if (!s_restart_queue)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (xQueueReceive(s_restart_queue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        s_restart_in_progress = true;

        /* Coalesce bursts: keep only the latest queued station change request. */
        if (req.type == RESTART_REQ_STATION_CHANGE)
        {
            restart_req_t queued = {0};
            while (xQueueReceive(s_restart_queue, &queued, 0) == pdTRUE)
            {
                if (queued.type == RESTART_REQ_STATION_CHANGE)
                    req = queued;
            }
        }

        if (xSemaphoreTake(station_Mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
        {
            int target_station = current_station_index;
            const char *reason = req.reason;

            if (req.type == RESTART_REQ_STATION_CHANGE)
            {
                if (req.station_index < 0 || req.station_index >= station_count)
                {
                    ESP_LOGW(TAG, "Restart task: invalid queued station index %d", req.station_index);
                    xSemaphoreGive(station_Mutex);
                    s_restart_in_progress = false;
                    continue;
                }

                if (req.station_index == current_station_index)
                {
                    xSemaphoreGive(station_Mutex);
                    s_restart_in_progress = false;
                    continue;
                }

                target_station = req.station_index;
                ESP_LOGI(TAG, "Changing station to: %s", stations[target_station].name);
                current_station_index = target_station;
            }

            log_stream_diagnostics(reason);
            esp_err_t err = restart_stream_for_station_locked(target_station, reason);
            if (err == ESP_OK)
            {
                s_consecutive_restart_failures = 0;
                if (req.type == RESTART_REQ_STATION_CHANGE)
                    save_station_index_to_nvs(current_station_index);
            }
            else
            {
                s_consecutive_restart_failures++;
                ESP_LOGW(TAG, "Restart task: restart failed (attempt %d/2), err=%d, station=%d",
                         s_consecutive_restart_failures, err, target_station);

                if (s_consecutive_restart_failures >= 2)
                {
                    /* Pipeline is stuck in an unrecoverable state — tear down and
                     * recreate all ADF objects so we can play again. */
                    s_consecutive_restart_failures = 0;
                    full_pipeline_recover(target_station);
                }
            }
            xSemaphoreGive(station_Mutex);
        }
        else
        {
            ESP_LOGW(TAG, "Restart task: failed to acquire station mutex");
        }
        s_restart_in_progress = false;
    }
}

/**
 * Request stream restart — non-blocking.
 * Signals audio_restart_task to do the heavy lifting so stream_task
 * is never blocked and continues draining the ADF event queue.
 * @param reason String describing restart reason
 * @return ESP_OK if restart was scheduled, ESP_ERR_INVALID_STATE otherwise
 */
static esp_err_t restart_current_stream(const char *reason)
{
    if (audio_user_paused || restart_cooldown_active() || s_restart_in_progress)
        return ESP_ERR_INVALID_STATE;

    if (!s_restart_task_handle || !s_restart_queue)
        return ESP_ERR_INVALID_STATE;

    restart_req_t req = {
        .type = RESTART_REQ_WATCHDOG,
        .station_index = current_station_index,
    };
    last_stream_restart_tick = xTaskGetTickCount(); /* Start cooldown immediately */
    strncpy(req.reason, reason, sizeof(req.reason) - 1);
    req.reason[sizeof(req.reason) - 1] = '\0';

    return (xQueueOverwrite(s_restart_queue, &req) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

/* ============================================================================ */
/* Stream Recovery: Automatic Watchdog */
/* ============================================================================ */

/**
 * Check if stream needs recovery and return restart reason
 * Returns NULL if stream is healthy, or reason string otherwise
 * Checks: element errors, HTTP timeout, I2S buffer empty
 * @return NULL if healthy, reason string if restart needed
 */
static const char *stream_watchdog_restart_reason(void)
{
    if (audio_user_paused || restart_cooldown_active() || s_restart_in_progress)
        return NULL;

    if (!http_stream_reader || !mp3_decoder || !i2s_stream_writer)
        return NULL;

    if (startup_grace_active())
        return NULL;

    audio_element_state_t http_state = audio_element_get_state(http_stream_reader);
    audio_element_state_t mp3_state = audio_element_get_state(mp3_decoder);
    audio_element_state_t i2s_state = audio_element_get_state(i2s_stream_writer);
    TickType_t now = xTaskGetTickCount();

    /* Check for element errors */
    if (http_state == AEL_STATE_ERROR || mp3_state == AEL_STATE_ERROR || 
        i2s_state == AEL_STATE_ERROR)
    {
        return "watchdog: element entered error state";
    }

    /* Check for unexpected stops */
    if (http_state == AEL_STATE_STOPPED || http_state == AEL_STATE_FINISHED ||
        mp3_state == AEL_STATE_STOPPED || mp3_state == AEL_STATE_FINISHED ||
        i2s_state == AEL_STATE_STOPPED || i2s_state == AEL_STATE_FINISHED)
    {
        return "watchdog: element stopped or finished unexpectedly";
    }

    /* Check for HTTP data timeout */
    if (last_http_activity_tick != 0 &&
        (now - last_http_activity_tick) >= pdMS_TO_TICKS(STREAM_HTTP_IDLE_TIMEOUT_MS))
    {
        return "watchdog: no HTTP activity timeout";
    }

    /* Check I2S buffer status */
    ringbuf_handle_t i2s_input_rb = audio_element_get_input_ringbuf(i2s_stream_writer);
    if (!i2s_input_rb)
        return NULL;

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

    /* Check for I2S buffer empty timeout */
    if ((now - last_i2s_data_tick) >= pdMS_TO_TICKS(STREAM_EMPTY_BUFFER_TIMEOUT_MS))
    {
        return "watchdog: i2s input ringbuffer empty timeout";
    }

    return NULL;
}

/* ============================================================================ */
/* Audio Pipeline: Initialization */
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
        ESP_LOGE(TAG, "Failed to create station mutex");

    ESP_LOGI(TAG, "Initializing audio codec and pipeline");
    ESP_LOGI(TAG,
             "Audio buffers profile=%d, http_rb=%d, mp3_rb=%d, i2s_rb=%d",
             AUDIO_BUFFER_PROFILE,
             HTTP_STREAM_RB_SIZE_BYTES,
             MP3_DECODER_RB_SIZE_BYTES,
             I2S_STREAM_RB_SIZE_BYTES);

    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, 
                        AUDIO_HAL_CTRL_START);

    /* Create pipeline */
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    /* Create HTTP stream element */
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    http_cfg.out_rb_size = HTTP_STREAM_RB_SIZE_BYTES;
    http_stream_reader = http_stream_init(&http_cfg);

    /* Create I2S stream element */
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.task_prio = 8;
    i2s_cfg.out_rb_size = I2S_STREAM_RB_SIZE_BYTES;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    /* Create MP3 decoder element */
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.out_rb_size = MP3_DECODER_RB_SIZE_BYTES;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
}

/**
 * Connect pipeline elements and start streaming from saved or default station
 * Sets up event listeners and begins audio playback
 * @param set ESP peripherals set for event listener
 */
void audio_start(esp_periph_set_handle_t set)
{
    /* Register and link elements: http -> mp3 -> i2s */
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    /* Load station and set initial URI */
    current_station_index = load_station_index_from_nvs(0);
    ESP_LOGI(TAG, "Starting station %d: %s", current_station_index,
             stations[current_station_index].url);
    audio_element_set_uri(http_stream_reader, stations[current_station_index].url);

    /* Setup event listener with enlarged queues */
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt_cfg.external_queue_size = 64;
    evt_cfg.internal_queue_size = 64;
    evt_cfg.queue_set_size = 64;
    evt = audio_event_iface_init(&evt_cfg);

    audio_pipeline_set_listener(pipeline, evt);
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    /* Start playback */
    ESP_LOGI(TAG, "Starting audio pipeline");
    mark_stream_activity();
    audio_pipeline_run(pipeline);
    last_pipeline_run_tick = xTaskGetTickCount();
    display_set_text(stations[current_station_index].name, 1, false);

    /* Create restart request queue (single-slot, always keep latest request). */
    s_restart_queue = xQueueCreate(1, sizeof(restart_req_t));
    if (!s_restart_queue)
    {
        ESP_LOGE(TAG, "Failed to create restart queue");
        return;
    }

    /* Create dedicated restart task (priority 5, below stream_task priority 7) */
    xTaskCreatePinnedToCore(audio_restart_task, "audio_restart", 4096, NULL, 5,
                            &s_restart_task_handle, 1);
}

/* ============================================================================ */
/* Station Control */
/* ============================================================================ */

/**
 * Change to a different radio station
 * Saves selection to NVS and restarts stream
 * @param station_index Index of station to switch to
 */
void change_radio_station(uint8_t station_index)
{
    if (current_station_index == station_index)
        return;

    if (station_index >= station_count)
    {
        ESP_LOGE(TAG, "Invalid station index: %d", station_index);
        return;
    }

    if (!s_restart_queue)
    {
        ESP_LOGW(TAG, "Restart queue not initialized, station change ignored");
        return;
    }

    restart_req_t req = {
        .type = RESTART_REQ_STATION_CHANGE,
        .station_index = station_index,
    };
    strncpy(req.reason, "station change", sizeof(req.reason) - 1);
    req.reason[sizeof(req.reason) - 1] = '\0';

    if (xQueueOverwrite(s_restart_queue, &req) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to enqueue station change to index %d", station_index);
        return;
    }

    display_set_text("                ", 1, false);
    display_set_text(stations[station_index].name, 1, false);
    ESP_LOGI(TAG, "Queued station change request to index %d (%s)",
             station_index, stations[station_index].name);
}

/* ============================================================================ */
/* HTTP Stream Event Handler */
/* ============================================================================ */

/**
 * Handle HTTP stream events (request/response/track transitions)
 * Marks HTTP activity and handles playlist/stream transitions
 * @param msg HTTP stream event message
 * @return ESP_OK
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
        return ESP_OK;

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK)
        return http_stream_next_track(msg->el);

    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST)
        return http_stream_fetch_again(msg->el);

    return ESP_OK;
}

/* ============================================================================ */
/* Audio Event Loop Task */
/* ============================================================================ */

/**
 * FreeRTOS task that manages the audio pipeline
 * Listens for AEL events and triggers watchdog recovery when needed
 * Runs on a 1-second event timeout for periodic health checks
 * @param arg Task argument (unused)
 */
void stream_task(void *arg)
{
    (void)arg;

    while (1)
    {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, 
                                                 pdMS_TO_TICKS(STREAM_EVENT_WAIT_MS));

        /* Handle timeout - periodic watchdog check */
        if (ret == ESP_ERR_TIMEOUT || ret == ESP_FAIL)
        {
            const char *watchdog_reason = stream_watchdog_restart_reason();
            if (watchdog_reason)
                restart_current_stream(watchdog_reason);
            continue;
        }

        /* Handle other errors */
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Event interface error: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Handle pipeline element events */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT)
        {
            /* Music info update from MP3 decoder */
            if (msg.source == (void *)mp3_decoder && 
                msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
            {
                audio_element_info_t music_info = {0};

                if (audio_element_getinfo(mp3_decoder, &music_info) == ESP_OK)
                {
                    last_i2s_data_tick = xTaskGetTickCount();
                    ESP_LOGI(TAG, "Music info: Sample Rate=%d, Bits=%d, Channels=%d",
                             music_info.sample_rates, music_info.bits, 
                             music_info.channels);

                    if (i2s_stream_writer)
                    {
                        i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
                                          music_info.bits, music_info.channels);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "i2s_stream_writer is NULL!");
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to get music info from mp3_decoder!");
                }
                continue;
            }

            /* Status report from pipeline elements */
            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS)
            {
                int status = (int)msg.data;

                /* HTTP stream status handling */
                if (msg.source == (void *)http_stream_reader)
                {
                    if (status == AEL_STATUS_INPUT_BUFFERING || 
                        status == AEL_STATUS_STATE_RUNNING)
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

                /* Decoder/I2S status handling */
                if ((msg.source == (void *)mp3_decoder || 
                     msg.source == (void *)i2s_stream_writer) &&
                    (status == AEL_STATUS_ERROR_PROCESS ||
                     status == AEL_STATUS_ERROR_OUTPUT ||
                     status == AEL_STATUS_STATE_STOPPED ||
                     status == AEL_STATUS_STATE_FINISHED))
                {
                    if (!startup_grace_active())
                    {
                        restart_current_stream(
                            "decoder or i2s writer stopped producing samples");
                    }
                    continue;
                }
            }
        }

        /* Final periodic watchdog check */
        const char *watchdog_reason = stream_watchdog_restart_reason();
        if (watchdog_reason)
            restart_current_stream(watchdog_reason);
    }
}

/* ============================================================================ */
/* Playback Control */
/* ============================================================================ */

/**
 * Pause audio playback
 */
void audio_pause(void)
{
    audio_user_paused = true;
    audio_pipeline_pause(pipeline);
}

/**
 * Resume audio playback
 */
void audio_resume(void)
{
    audio_user_paused = false;
    mark_stream_activity();
    audio_pipeline_resume(pipeline);
}

/* ============================================================================ */
/* Status Inquiry */
/* ============================================================================ */

/**
 * Get the current station index as a string
 * Used for REST API responses
 * @return Pointer to static buffer containing index string
 */
bool audio_is_paused(void)
{
    return audio_user_paused;
}

char *current_station_info(void)
{
    static char index_str[12];

    if (current_station_index < 0 || current_station_index >= station_count)
    {
        ESP_LOGE(TAG, "Invalid station index: %d", current_station_index);
        return NULL;
    }

    snprintf(index_str, sizeof(index_str), "%d", current_station_index);
    return index_str;
}

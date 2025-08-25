#ifndef AUDIO_H
#define AUDIO_H
#include "http_stream.h"
#include "esp_peripherals.h"

#define MAX_STATIONS 50

typedef struct {
    char name[128];  // Station name
    char url[256];   // Streaming URL
    char genre[64];  // Genre or other information
    int index;       // Station index
} Station;

extern Station stations[MAX_STATIONS];
extern SemaphoreHandle_t station_Mutex; 
extern int station_count;
void audio_init(void);
void audio_start(esp_periph_set_handle_t periph_set_handle);
void audio_pause();
void audio_resume();
void stream_task(void *arg);
int _http_stream_event_handle(http_stream_event_msg_t *msg);
void change_radio_station(uint8_t station_index);
char * current_station_info(void);
void load_stations(void);
esp_err_t add_station_to_file(const char *name, const char *url, const char *genre);
esp_err_t delete_station_from_file(int index_to_delete);



#endif

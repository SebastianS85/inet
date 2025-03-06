#ifndef AUDIO_H
#define AUDIO_H
#include "http_stream.h"
#include "esp_peripherals.h"

extern SemaphoreHandle_t station_Mutex; 
void audio_init(void);
void audio_start(esp_periph_set_handle_t periph_set_handle);
void audio_pause();
void audio_resume();
void stream_task(void *arg);
int _http_stream_event_handle(http_stream_event_msg_t *msg);
void change_radio_station(uint8_t station_index);
char * current_station_info(void);



#endif

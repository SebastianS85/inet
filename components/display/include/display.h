#ifndef DISPLAY_H   
#define DISPLAY_H
#include "stdbool.h"

void init_display(void);
void display_set_contrast(uint8_t contrast);
void display_set_text(char *text,uint8_t line,bool invert);
void display_set_text_x2(char *text,uint8_t line);
void display_clear(void);




#endif

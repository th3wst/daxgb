#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define INT_JOYPAD (1 << 4)

void joypad_init(void);
void joypad_handle_event(SDL_Event *e);

uint8_t joypad_read(void);
void joypad_write(uint8_t data);

#endif // JOYPAD_H
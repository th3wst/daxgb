#include "../include/joypad.h"
#include "../include/mmu.h"

static uint8_t action_buttons = 0x0F;
static uint8_t dir_buttons = 0x0F;
static uint8_t joypad_select = 0xCF; 

void joypad_init(void) {
    action_buttons = 0x0F;
    dir_buttons = 0x0F;
    joypad_select = 0xCF;
}

void joypad_handle_event(SDL_Event *e) {
    bool is_pressed = (e->type == SDL_KEYDOWN);
    SDL_Scancode key = e->key.keysym.scancode;
    
    //Remember the state BEFORE the press to detect 1->0 transitions
    uint8_t old_state = action_buttons & dir_buttons;

    switch (key) {
        case SDL_SCANCODE_Z:         if (is_pressed) action_buttons &= ~(1 << 0); else action_buttons |= (1 << 0); break;
        case SDL_SCANCODE_X:         if (is_pressed) action_buttons &= ~(1 << 1); else action_buttons |= (1 << 1); break;
        case SDL_SCANCODE_BACKSPACE: if (is_pressed) action_buttons &= ~(1 << 2); else action_buttons |= (1 << 2); break;
        case SDL_SCANCODE_RETURN:    if (is_pressed) action_buttons &= ~(1 << 3); else action_buttons |= (1 << 3); break;
        case SDL_SCANCODE_RIGHT:     if (is_pressed) dir_buttons &= ~(1 << 0);    else dir_buttons |= (1 << 0);    break;
        case SDL_SCANCODE_LEFT:      if (is_pressed) dir_buttons &= ~(1 << 1);    else dir_buttons |= (1 << 1);    break;
        case SDL_SCANCODE_UP:        if (is_pressed) dir_buttons &= ~(1 << 2);    else dir_buttons |= (1 << 2);    break;
        case SDL_SCANCODE_DOWN:      if (is_pressed) dir_buttons &= ~(1 << 3);    else dir_buttons |= (1 << 3);    break;
        default: return;
    }

    uint8_t new_state = action_buttons & dir_buttons;

    //Trigger interrupt if ANY button is pressed (transition from unpressed to pressed)
    if (is_pressed && (old_state != new_state)) {
        mmu_write8(0xFF0F, mmu_read8(0xFF0F) | INT_JOYPAD);
    }
}

uint8_t joypad_read(void) {
    uint8_t res = joypad_select & 0xF0;
    
    //If Directional buttons are requested (Bit 4 is 0)
    if (!(joypad_select & 0x10)) {
        res |= (dir_buttons & 0x0F);
    }
    
    //If Action buttons are requested (Bit 5 is 0)
    if (!(joypad_select & 0x20)) {
        res |= (action_buttons & 0x0F);
    }
    
    //If neither group is selected, the hardware returns 0x0F in the lower nibble
    if ((joypad_select & 0x30) == 0x30) {
        res |= 0x0F;
    }

    return res;
}

void joypad_write(uint8_t data) {
    //Only bits 4 and 5 are writable. Top two bits are always 1.
    joypad_select = (data & 0x30) | 0xC0;
}
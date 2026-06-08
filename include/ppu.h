#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define INT_VBLANK   (1 << 0)
#define INT_LCD_STAT (1 << 1)

#define PPU_RES_X 160
#define PPU_RES_Y 144

bool ppu_init(void);
void ppu_cleanup(void);

void ppu_step(int cycles);

//VRAM (0x8000-0x9FFF)
uint8_t ppu_read_vram(uint16_t address);
void ppu_write_vram(uint16_t address, uint8_t data);

//OAM (0xFE00-0xFE9F)
uint8_t ppu_read_oam(uint16_t address);
void ppu_write_oam(uint16_t address, uint8_t data);

//LCD Registers (0xFF40-0xFF4B)
uint8_t ppu_read_lcd(uint16_t address);
void ppu_write_lcd(uint16_t address, uint8_t data);

void ppu_set_window_title(const char* title);

uint8_t ppu_read_ly(void);

void ppu_save_state(FILE *f);
void ppu_load_state(FILE *f);

#endif // PPU_H
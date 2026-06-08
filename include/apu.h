#ifndef APU_H
#define APU_H

#include <stdint.h>
#include <stdbool.h>

void apu_init(void);
void apu_cleanup(void);
void apu_step(int cycles);
uint8_t apu_read(uint16_t address);
void apu_write(uint16_t address, uint8_t data);

#endif
#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <stdbool.h>

void mmu_init(void);
void mmu_cleanup(void);

uint8_t mmu_read8(uint16_t address);
void mmu_write8(uint16_t address, uint8_t data);

uint16_t mmu_read16(uint16_t address);

//Writes a 16-bit value in little-endian order
void mmu_write16(uint16_t address, uint16_t data);

#include <stdio.h>
void mmu_save_state(FILE *f);
void mmu_load_state(FILE *f);

#endif // MMU_H
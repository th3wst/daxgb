#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum { //Cartridge types based on MBC hardware
    MBC_NONE,
    MBC_1,
    MBC_2,
    MBC_3,
    MBC_5
} MBCType;

typedef struct {
    char filename[1024];
    char title[17];

    uint8_t *rom_data;
    uint32_t rom_size;

    uint8_t *ram_data;
    uint32_t ram_size;

    MBCType mbc_type;

    uint32_t rom_bank;   //Active switchable ROM bank
    uint32_t ram_bank;   //Active external RAM bank

    bool ram_enabled;
    uint8_t banking_mode; //MBC1: 0 = ROM banking, 1 = RAM banking
} CartridgeContext;

bool cart_load(const char *filename);
void cart_cleanup(void);

uint8_t cart_read(uint16_t address);
void cart_write(uint16_t address, uint8_t data);

//Access current cartridge state for debugging/emulation.
CartridgeContext* cart_get_context(void);

void cart_save_state(FILE *f);
void cart_load_state(FILE *f);

#endif //CARTRIDGE_H
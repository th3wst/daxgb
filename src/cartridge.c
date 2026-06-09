#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/cartridge.h"

static CartridgeContext ctx = {0};
static char save_filename[256];

//Independent MBC1 Hardware Registers
static uint8_t mbc1_reg1 = 1;
static uint8_t mbc1_reg2 = 0;
static uint8_t mbc1_mode = 0;

static void update_mbc1_banks(void) {
    if (mbc1_mode == 0) {
        ctx.rom_bank = (mbc1_reg2 << 5) | mbc1_reg1;
    } else {
        ctx.rom_bank = mbc1_reg1; 
    }
    
    if (ctx.rom_size > 0) ctx.rom_bank %= (ctx.rom_size / 0x4000);

    if (mbc1_mode == 1) {
        ctx.ram_bank = mbc1_reg2;
    } else {
        ctx.ram_bank = 0;
    }
    
    if (ctx.ram_size > 0) ctx.ram_bank %= (ctx.ram_size / 0x2000);
}

static void build_save_filename(const char *rom_filename) {
    strncpy(save_filename, rom_filename, sizeof(save_filename) - 1);
    save_filename[sizeof(save_filename) - 1] = '\0';
    
    char *dot = strrchr(save_filename, '.');
    if (dot) strcpy(dot, ".sav");
    else strcat(save_filename, ".sav");
}

static void save_battery_ram(void) {
    if (ctx.ram_size == 0 || !ctx.ram_data) return;
    
    FILE *fp = fopen(save_filename, "wb");
    if (fp) {
        fwrite(ctx.ram_data, 1, ctx.ram_size, fp);
        fflush(fp); //force flush to disk
        fclose(fp);
        printf("[SRAM] Flushed save data to disk: %s\n", save_filename);
    } else {
        fprintf(stderr, "[SRAM] Error: Failed to write save file: %s\n", save_filename);
    }
}

bool cart_load(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size <= 0) { fclose(fp); return false; }

    ctx.rom_size = (uint32_t)size;
    ctx.rom_data = (uint8_t *)malloc(ctx.rom_size);
    if (!ctx.rom_data) { fclose(fp); return false; }

    size_t bytes_read = fread(ctx.rom_data, 1, ctx.rom_size, fp);
    fclose(fp);
    if (bytes_read != ctx.rom_size) { free(ctx.rom_data); return false; }

    if (ctx.rom_size > 0x0143) {
        memcpy(ctx.title, &ctx.rom_data[0x0134], 16);
        ctx.title[16] = '\0';
    } else {
        strcpy(ctx.title, "UNKNOWN");
    }

    uint8_t mbc_flag = ctx.rom_data[0x0147];
    if (mbc_flag >= 0x01 && mbc_flag <= 0x03) ctx.mbc_type = MBC_1;
    else if (mbc_flag >= 0x0F && mbc_flag <= 0x13) ctx.mbc_type = MBC_3;
    else if (mbc_flag >= 0x19 && mbc_flag <= 0x1E) ctx.mbc_type = MBC_5;
    else ctx.mbc_type = MBC_NONE; 

    uint8_t ram_flag = ctx.rom_data[0x0149];
    switch (ram_flag) {
        case 0x02: ctx.ram_size = 0x2000; break;  
        case 0x03: ctx.ram_size = 0x8000; break;  
        case 0x04: ctx.ram_size = 0x20000; break; 
        case 0x05: ctx.ram_size = 0x10000; break; 
        default: ctx.ram_size = 0; break;
    }

    if (ctx.ram_size > 0) {
        ctx.ram_data = (uint8_t *)malloc(ctx.ram_size);
        memset(ctx.ram_data, 0xFF, ctx.ram_size); // 0xFF default is hardware accurate
        
        build_save_filename(filename);
        FILE *save_fp = fopen(save_filename, "rb");
        if (save_fp) {
            fread(ctx.ram_data, 1, ctx.ram_size, save_fp);
            fclose(save_fp);
            printf("[SRAM] Successfully loaded save file: %s\n", save_filename);
        } else {
            printf("[SRAM] No existing save file found. Starting fresh.\n");
        }
    } else {
        ctx.ram_data = NULL;
    }

    //Initialize the active memory banking variables
    ctx.rom_bank = 1; 
    ctx.ram_bank = 0;
    ctx.ram_enabled = false; 
    ctx.banking_mode = 0;

    strncpy(ctx.filename, filename, sizeof(ctx.filename) - 1);
    ctx.filename[sizeof(ctx.filename) - 1] = '\0';
    return true;
}

void cart_cleanup(void) {
    if (ctx.ram_size > 0 && ctx.ram_data) save_battery_ram();
    if (ctx.rom_data) free(ctx.rom_data);
    if (ctx.ram_data) free(ctx.ram_data);
    memset(&ctx, 0, sizeof(CartridgeContext));
}

uint8_t cart_read(uint16_t address) {
    CartridgeContext *ctx = cart_get_context();

    if (address < 0x4000) {
        // ROM Bank 0 (Always fixed at the start of the ROM)
        return ctx->rom_data[address];
        
    } else if (address < 0x8000) {
        // Switchable ROM Bank (The dynamic window)
        // Offset = (Address within window) + (Bank Number * 16KB)
        uint32_t offset = (address - 0x4000) + (ctx->rom_bank * 0x4000);
        
        // Modulo against rom_size prevents segfaults if the bank number goes out of bounds
        return ctx->rom_data[offset % ctx->rom_size];
        
    } else if (address >= 0xA000 && address < 0xC000) {
        // Cartridge External RAM
        if (ctx->ram_enabled && ctx->ram_data != NULL) {
            uint32_t offset = (address - 0xA000) + (ctx->ram_bank * 0x2000);
            if (offset < ctx->ram_size) {
                return ctx->ram_data[offset];
            }
        }
        return 0xFF; // Open bus behavior if RAM is disabled
    }
    
    return 0xFF;
}

void cart_write(uint16_t address, uint8_t data) {
    CartridgeContext *ctx = cart_get_context();

    if (address < 0x2000) {
        //RAM Enable/Disable
        ctx->ram_enabled = ((data & 0x0F) == 0x0A);
        
    } else if (address < 0x4000) {
        // ROM Bank Number (Lower 5 bits)
        ctx->rom_bank = (ctx->rom_bank & 0xE0) | (data & 0x1F);
        
        // MBC1 Hardware Quirk: Bank 0 is physically wired to Bank 1
        if (ctx->rom_bank == 0) {
            ctx->rom_bank = 1; 
        }
        
    } else if (address < 0x6000) {
        // RAM Bank Number OR Upper 2 bits of ROM Bank
        if (ctx->banking_mode == 1) {
            ctx->ram_bank = data & 0x03;
        } else {
            // Apply data to the upper bits of the ROM bank
            ctx->rom_bank = (ctx->rom_bank & 0x1F) | ((data & 0x03) << 5);
            if (ctx->rom_bank == 0) ctx->rom_bank = 1;
        }
        
    } else if (address < 0x8000) {
        // Banking Mode Select (0 = ROM Banking Mode, 1 = RAM Banking Mode)
        ctx->banking_mode = data & 0x01;
        if (ctx->banking_mode == 0) {
            ctx->ram_bank = 0; // Lock RAM to bank 0 in simple mode
        }
        
    } else if (address >= 0xA000 && address < 0xC000) {
        // Write actual data to the Cartridge SRAM
        if (ctx->ram_enabled && ctx->ram_data != NULL) {
            uint32_t offset = (address - 0xA000) + (ctx->ram_bank * 0x2000);
            if (offset < ctx->ram_size) {
                ctx->ram_data[offset] = data;
            }
        }
    }
}

void cart_save_state(FILE *f) {
    CartridgeContext *ctx = cart_get_context();
    
    //Save active banking state
    fwrite(&ctx->ram_enabled, sizeof(bool), 1, f);
    fwrite(&ctx->rom_bank, sizeof(uint32_t), 1, f);
    fwrite(&ctx->ram_bank, sizeof(uint32_t), 1, f);
    fwrite(&ctx->banking_mode, sizeof(uint8_t), 1, f);
    
    // If the cartridge has SRAM, save it
    if (ctx->ram_size > 0 && ctx->ram_data != NULL) {
        fwrite(ctx->ram_data, 1, ctx->ram_size, f);
    }
}

void cart_load_state(FILE *f) {
    CartridgeContext *ctx = cart_get_context();
    
    fread(&ctx->ram_enabled, sizeof(bool), 1, f);
    fread(&ctx->rom_bank, sizeof(uint32_t), 1, f);
    fread(&ctx->ram_bank, sizeof(uint32_t), 1, f);
    fread(&ctx->banking_mode, sizeof(uint8_t), 1, f);
    
    if (ctx->ram_size > 0 && ctx->ram_data != NULL) {
        fread(ctx->ram_data, 1, ctx->ram_size, f);
    }
}

CartridgeContext* cart_get_context(void) {
    return &ctx;
}
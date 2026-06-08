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
        memset(ctx.ram_data, 0xFF, ctx.ram_size); //0xFF default is hardware accurate
        
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

    mbc1_reg1 = 1; mbc1_reg2 = 0; mbc1_mode = 0;
    ctx.rom_bank = 1; ctx.ram_bank = 0;
    ctx.ram_enabled = false; ctx.banking_mode = 0;

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
    if (address < 0x4000) {
        return ctx.rom_data[address];
    } else if (address < 0x8000) {
        uint8_t bank = ctx.rom_bank;
        if (ctx.mbc_type == MBC_1 && (bank == 0 || bank == 0x20 || bank == 0x40 || bank == 0x60)) bank++;
        uint32_t mapped_address = (bank * 0x4000) + (address - 0x4000);
        if (mapped_address < ctx.rom_size) return ctx.rom_data[mapped_address];
    } else if (address >= 0xA000 && address < 0xC000) {
        if (ctx.ram_enabled && ctx.ram_data) {
            //Safely block RTC register reads to prevent checksum corruption
            if (ctx.mbc_type == MBC_3 && ctx.ram_bank >= 0x08 && ctx.ram_bank <= 0x0C) {
                return 0x00; 
            }
            
            uint32_t mapped_address = (ctx.ram_bank * 0x2000) + (address - 0xA000);
            if (mapped_address < ctx.ram_size) {
                return ctx.ram_data[mapped_address];
            }
        }
    }
    return 0xFF;
}

void cart_write(uint16_t address, uint8_t data) {
    if (ctx.mbc_type == MBC_NONE) return;

    if (address < 0x2000) {
        bool enable = ((data & 0x0F) == 0x0A);
        if (ctx.ram_enabled && !enable) save_battery_ram();
        ctx.ram_enabled = enable;
    } else if (address < 0x4000) {
        if (ctx.mbc_type == MBC_1) {
            mbc1_reg1 = data & 0x1F;
            if (mbc1_reg1 == 0) mbc1_reg1 = 1;
            update_mbc1_banks();
        } else if (ctx.mbc_type == MBC_3) {
            uint8_t bank = data & 0x7F;
            if (bank == 0) bank = 1;
            ctx.rom_bank = bank;
            ctx.rom_bank %= (ctx.rom_size / 0x4000);
        } else if (ctx.mbc_type == MBC_5) {
            ctx.rom_bank = data; 
            ctx.rom_bank %= (ctx.rom_size / 0x4000);
        }
    } else if (address < 0x6000) {
        if (ctx.mbc_type == MBC_1) {
            mbc1_reg2 = data & 0x03;
            update_mbc1_banks();
        } else {
            ctx.ram_bank = data & 0x0F;
        }
    } else if (address < 0x8000) {
        if (ctx.mbc_type == MBC_1) {
            mbc1_mode = data & 0x01;
            update_mbc1_banks();
        }
    } else if (address >= 0xA000 && address < 0xC000) {
        if (ctx.ram_enabled && ctx.ram_data) {
            //Safely block RTC register writes
            if (ctx.mbc_type == MBC_3 && ctx.ram_bank >= 0x08 && ctx.ram_bank <= 0x0C) return;
            
            uint32_t mapped_address = (ctx.ram_bank * 0x2000) + (address - 0xA000);
            if (mapped_address < ctx.ram_size) {
                ctx.ram_data[mapped_address] = data;
            }
        }
    }
}

void cart_save_state(FILE *f) {
    CartridgeContext *ctx = cart_get_context();
    
    /* We don't save the ROM array (too big, never changes), just the active banking state */
    fwrite(&ctx->ram_enabled, sizeof(bool), 1, f);
    fwrite(&ctx->rom_bank, sizeof(uint32_t), 1, f);
    fwrite(&ctx->ram_bank, sizeof(uint32_t), 1, f);
    fwrite(&ctx->banking_mode, sizeof(uint8_t), 1, f);
    
    /* If the cartridge has RAM (SRAM), we absolutely must save it! */
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
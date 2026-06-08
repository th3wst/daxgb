#include <string.h>
#include "../include/mmu.h"
#include "../include/cartridge.h"
#include "../include/timer.h"
#include "../include/ppu.h"
#include "../include/joypad.h"
#include "../include/apu.h"

//VRAM and OAM are owned by the PPU
static uint8_t wram[0x2000];
static uint8_t io[0x0080];
static uint8_t hram[0x007F];

//Interrupt Registers
static uint8_t ie_register; // 0xFFFF
static uint8_t if_register; // 0xFF0F

void mmu_init(void) {
    memset(wram, 0, sizeof(wram));
    memset(io, 0, sizeof(io));
    memset(hram, 0, sizeof(hram));
    
    ie_register = 0x00;
    if_register = 0xE1; //DMG post-boot default, top 3 bits are 1
}

void mmu_cleanup(void) {
    mmu_init();
}

uint8_t mmu_read8(uint16_t address) {
    if (address < 0x8000) {
        return cart_read(address); // ROM
    } else if (address < 0xA000) {
        return ppu_read_vram(address); // Route to PPU
    } else if (address < 0xC000) {
        return cart_read(address); // Route External RAM to Cartridge
    } else if (address < 0xE000) {
        return wram[address - 0xC000]; // WRAM
    } else if (address < 0xFE00) {
        return wram[address - 0xE000]; // ECHO RAM
    } else if (address < 0xFEA0) {
        return ppu_read_oam(address); // Route to PPU
    } else if (address < 0xFF00) {
        return 0xFF; // Unusable space
    } else if (address < 0xFF80) {
        // Route Joypad
        if (address == 0xFF00) {
            return joypad_read();
        }
        // Route Timer Registers
        if (address >= 0xFF04 && address <= 0xFF07) {
            return timer_read(address);
        }
        // Route APU / Audio Registers
        if (address >= 0xFF10 && address <= 0xFF3F) {
            return apu_read(address);
        }
        // Route LCD/PPU Control Registers
        if (address >= 0xFF40 && address <= 0xFF4B) {
            return ppu_read_lcd(address);
        }
        // Route Interrupt Flag (IF)
        if (address == 0xFF0F) {
            return if_register | 0xE0; // Unused bits 5-7 always read as 1
        }
        return io[address - 0xFF00]; // Other I/O
    } else if (address < 0xFFFF) {
        return hram[address - 0xFF80]; // HRAM
    } else if (address == 0xFFFF) {
        return ie_register; // IE
    }
    
    return 0xFF;
}

void mmu_write8(uint16_t address, uint8_t data) {
    if (address < 0x8000) {
        cart_write(address, data); // ROM Bank Switching (MBC)
    } else if (address < 0xA000) {
        ppu_write_vram(address, data); // Route to PPU
    } else if (address < 0xC000) {
        cart_write(address, data); // External Cartridge RAM
    } else if (address < 0xE000) {
        wram[address - 0xC000] = data;
    } else if (address < 0xFE00) {
        wram[address - 0xE000] = data;
    } else if (address < 0xFEA0) {
        ppu_write_oam(address, data); // Route to PPU
    } else if (address < 0xFF00) {
        // Unusable space
    } else if (address < 0xFF80) {
        // Route Joypad
        if (address == 0xFF00) {
            joypad_write(data);
            return;
        }
        // Route Serial Transfer (Stubbed) - This will probably stay stubbed for a while lol
        if (address == 0xFF02) {
            // Clear bit 7 immediately to fake a finished serial transfer
            io[0x02] = data & 0x7F; 
            return;
        }
        // Route Timer Registers
        if (address >= 0xFF04 && address <= 0xFF07) {
            timer_write(address, data);
            return;
        }
        
        //Routes APU/Audio Registers
        if (address >= 0xFF10 && address <= 0xFF3F) {
            apu_write(address, data);
            return;
        }

        // Route LCD/PPU Control Registers
        if (address >= 0xFF40 && address <= 0xFF4B) {
            ppu_write_lcd(address, data);
            return;
        }
        // Route Interrupt Flag (IF)
        if (address == 0xFF0F) {
            if_register = data;
            return;
        }
        io[address - 0xFF00] = data; //Other I/O
    } else if (address < 0xFFFF) {
        hram[address - 0xFF80] = data;
    } else if (address == 0xFFFF) {
        ie_register = data;
    }
}

uint16_t mmu_read16(uint16_t address) {
    uint16_t lo = mmu_read8(address);
    uint16_t hi = mmu_read8(address + 1);
    return lo | (hi << 8);
}

void mmu_write16(uint16_t address, uint16_t data) {
    mmu_write8(address, data & 0xFF);
    mmu_write8(address + 1, data >> 8);
}

void mmu_save_state(FILE *f) {
    fwrite(wram, 1, sizeof(wram), f);
    fwrite(hram, 1, sizeof(hram), f);
    fwrite(io, 1, sizeof(io), f);
    fwrite(&ie_register, sizeof(uint8_t), 1, f);
    fwrite(&if_register, sizeof(uint8_t), 1, f);
}

void mmu_load_state(FILE *f) {
    fread(wram, 1, sizeof(wram), f);
    fread(hram, 1, sizeof(hram), f);
    fread(io, 1, sizeof(io), f);
    fread(&ie_register, sizeof(uint8_t), 1, f);
    fread(&if_register, sizeof(uint8_t), 1, f);
}
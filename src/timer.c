#include "../include/timer.h"
#include "../include/mmu.h"

static uint16_t div_register;
static uint8_t tima;
static uint8_t tma;
static uint8_t tac;
static int timer_counter;

//TAC frequency selections in CPU cycles.
static const int FREQ_CYCLES[] = {1024, 16, 64, 256};

void timer_init(void) {
    // DMG post-boot state
    div_register = 0xABCC;
    tima = 0x00;
    tma = 0x00;
    tac = 0xF8;
    timer_counter = 1024;
}

void timer_step(int cycles) {
    div_register += cycles;

    if (!(tac & 0x04)) {
        return;
    }

    timer_counter -= cycles;

    if (timer_counter <= 0) {
        int freq_index = tac & 0x03;
        timer_counter += FREQ_CYCLES[freq_index];

        if (tima == 0xFF) {
            tima = tma;

            // Request timer interrupt.
            uint8_t interrupt_flags = mmu_read8(0xFF0F);
            mmu_write8(0xFF0F, interrupt_flags | INT_TIMER);
        } else {
            tima++;
        }
    }
}

uint8_t timer_read(uint16_t address) {
    switch (address) {
        case 0xFF04:
            return (div_register >> 8) & 0xFF;
        case 0xFF05:
            return tima;
        case 0xFF06:
            return tma;
        case 0xFF07:
            return tac | 0xF8; // Unused bits read as 1
        default:
            return 0xFF;
    }
}

void timer_write(uint16_t address, uint8_t data) {
    switch (address) {
        case 0xFF04:
            div_register = 0; // Writing to DIV resets it
            break;
        case 0xFF05:
            tima = data;
            break;
        case 0xFF06:
            tma = data;
            break;
        case 0xFF07:
            tac = data;
            break;
    }
}
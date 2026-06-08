#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define INT_TIMER (1 << 2)

void timer_init(void);
void timer_step(int cycles);

//DIV=FF04, TIMA=FF05, TMA=FF06, TAC=FF07
uint8_t timer_read(uint16_t address);

void timer_write(uint16_t address, uint8_t data);

#endif // TIMER_H
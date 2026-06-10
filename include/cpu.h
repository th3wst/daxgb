#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#define FLAG_Z (1 << 7) // Zero
#define FLAG_N (1 << 6) // Subtract
#define FLAG_H (1 << 5) // Half Carry
#define FLAG_C (1 << 4) // Carry

typedef struct {
    uint8_t a, f;
    uint8_t b, c;
    uint8_t d, e;
    uint8_t h, l;

    uint16_t sp; // Stack Pointer
    uint16_t pc; // Program Counter

    bool ime;    // Interrupt Master Enable
    bool halted; // HALT state
} CPUContext;

void cpu_init(void);

//Executes one instruction and returns cycles consumed.
int cpu_step(void);

//Access current CPU state for debugging/logging
CPUContext* cpu_get_context(void);

void cpu_print_log(void);

void cpu_save_state(FILE *f);
void cpu_load_state(FILE *f);

#endif //CPU_H
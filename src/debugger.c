#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/debugger.h"
#include "../include/cpu.h"
#include "../include/mmu.h"

static bool is_paused = false;
static int breakpoint = -1;
static bool step_mode = false; //Tracks single-step execution

static void print_help(void) {
    printf("\n--- Debugger Commands ---\n");
    printf("  s         : Step to next instruction\n");
    printf("  c         : Continue execution\n");
    printf("  r         : Print CPU registers\n");
    printf("  b [hex]   : Set breakpoint at address (e.g., b 0100)\n");
    printf("  bc        : Clear breakpoint\n");
    printf("  m [hex]   : Dump 16 bytes of memory at address\n");
    printf("  help      : Show this menu\n");
    printf("-------------------------\n");
}

static void disassemble_instruction(uint16_t pc, char* output) {
    uint8_t opcode = mmu_read8(pc);
    
    switch(opcode) {
        case 0x00: sprintf(output, "NOP"); break;
        case 0x20: sprintf(output, "JR NZ, %d", (int8_t)mmu_read8(pc+1)); break;
        case 0x21: sprintf(output, "LD HL, $%02X%02X", mmu_read8(pc+2), mmu_read8(pc+1)); break;
        case 0x31: sprintf(output, "LD SP, $%02X%02X", mmu_read8(pc+2), mmu_read8(pc+1)); break;
        case 0x3E: sprintf(output, "LD A, $%02X", mmu_read8(pc+1)); break;
        case 0xAF: sprintf(output, "XOR A"); break;
        case 0xC3: sprintf(output, "JP $%02X%02X", mmu_read8(pc+2), mmu_read8(pc+1)); break;
        case 0xCD: sprintf(output, "CALL $%02X%02X", mmu_read8(pc+2), mmu_read8(pc+1)); break;
        case 0xEA: sprintf(output, "LD ($%02X%02X), A", mmu_read8(pc+2), mmu_read8(pc+1)); break;
        case 0xFA: sprintf(output, "LD A, ($%02X%02X)", mmu_read8(pc+2), mmu_read8(pc+1)); break;
        
        // Prefix CB instructions
        case 0xCB: 
            sprintf(output, "CB-PREFIX %02X", mmu_read8(pc+1)); 
            break;
            
        default: 
            sprintf(output, "??? (0x%02X)", opcode); 
            break;
    }
}

static void print_registers(void) {
    CPUContext *ctx = cpu_get_context();
    printf("\n[CPU STATE]\n");
    printf("AF: %02X%02X  BC: %02X%02X  DE: %02X%02X  HL: %02X%02X\n", 
            ctx->a, ctx->f, ctx->b, ctx->c, ctx->d, ctx->e, ctx->h, ctx->l);
    printf("SP: %04X  PC: %04X  IME: %d\n", ctx->sp, ctx->pc, ctx->ime);
    
    char asm_string[32];
    disassemble_instruction(ctx->pc, asm_string);
    
    printf("INSTR: %02X %02X %02X %02X | ASM: %s\n", 
        mmu_read8(ctx->pc), mmu_read8(ctx->pc+1), 
        mmu_read8(ctx->pc+2), mmu_read8(ctx->pc+3),
        asm_string);
}

static void handle_command(char *cmd) {
    cmd[strcspn(cmd, "\n")] = 0;
    if (strlen(cmd) == 0) return;

    if (strcmp(cmd, "s") == 0) {
        //Unpause for one cycle
        is_paused = false; 
        step_mode = true;
    } else if (strcmp(cmd, "c") == 0) {
        printf("Continuing execution...\n");
        is_paused = false;
    } else if (strcmp(cmd, "r") == 0) {
        print_registers();
        is_paused = true; 
    } else if (strncmp(cmd, "b ", 2) == 0) {
        sscanf(cmd + 2, "%x", &breakpoint);
        printf("Breakpoint set at: 0x%04X\n", breakpoint);
        is_paused = true;
    } else if (strcmp(cmd, "bc") == 0) {
        breakpoint = -1;
        printf("Breakpoint cleared.\n");
        is_paused = true;
    } else if (strncmp(cmd, "m ", 2) == 0) {
        unsigned int addr;
        sscanf(cmd + 2, "%x", &addr);
        printf("\n[MEM 0x%04X]: ", addr);
        for(int i = 0; i < 16; i++) {
            printf("%02X ", mmu_read8(addr + i));
        }
        printf("\n");
        is_paused = true;
    } else {
        print_help();
        is_paused = true;
    }
}

void debugger_init(bool start_paused) {
    is_paused = start_paused;
    breakpoint = -1;
    step_mode = false;
    
    if (is_paused) {
        printf("\n=== DAXGB DEBUGGER INITIALIZED ===\n");
        printf("Emulator paused at entry point. Type 'c' to continue or 's' to step.\n");
        print_registers();
    }
}

void debugger_pause(void) {
    is_paused = true;
    printf("\n--- EXECUTION PAUSED ---\n");
    print_registers();
}

void debugger_update(void) {
    CPUContext *ctx = cpu_get_context();

    //If one step executes, pause again immediately
    if (step_mode) {
        is_paused = true;
        step_mode = false;
        print_registers();
    }

    if (breakpoint != -1 && ctx->pc == breakpoint) {
        if (!is_paused) {
            printf("\n*** BREAKPOINT HIT AT 0x%04X ***\n", ctx->pc);
            print_registers();
        }
        is_paused = true;
    }

    if (is_paused) {
        char input[64];
        printf("daxgb-dbg> ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            handle_command(input);
        }
    }
}

bool debugger_is_active(void) {
    return is_paused;
}
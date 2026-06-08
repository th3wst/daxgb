#include <stdio.h>
#include <stdlib.h>
#include "../include/cpu.h"
#include "../include/mmu.h"

static CPUContext ctx = {0};
static int cpu_cycles = 0;
static int ei_delay = 0;

//Timing and memory helpers
static void tick(void) { cpu_cycles += 4; }
static uint8_t read8(uint16_t addr) { tick(); return mmu_read8(addr); }
static void write8(uint16_t addr, uint8_t val) { tick(); mmu_write8(addr, val); }
static uint8_t fetch8(void) { return read8(ctx.pc++); }
static uint16_t fetch16(void) { uint8_t lo = fetch8(); return lo | (fetch8() << 8); }

// 16-bit Register Pair Helpers
static uint16_t read_bc(void) { return (ctx.b << 8) | ctx.c; }
static void write_bc(uint16_t v) { ctx.b = v >> 8; ctx.c = v & 0xFF; }
static uint16_t read_de(void) { return (ctx.d << 8) | ctx.e; }
static void write_de(uint16_t v) { ctx.d = v >> 8; ctx.e = v & 0xFF; }
static uint16_t read_hl(void) { return (ctx.h << 8) | ctx.l; }
static void write_hl(uint16_t v) { ctx.h = v >> 8; ctx.l = v & 0xFF; }
static uint16_t read_af(void) { return (ctx.a << 8) | ctx.f; }
static void write_af(uint16_t v) { ctx.a = v >> 8; ctx.f = v & 0xF0; }

static void set_flags(int z, int n, int h, int c) {
    if (z != -1) { if (z) ctx.f |= FLAG_Z; else ctx.f &= ~FLAG_Z; }
    if (n != -1) { if (n) ctx.f |= FLAG_N; else ctx.f &= ~FLAG_N; }
    if (h != -1) { if (h) ctx.f |= FLAG_H; else ctx.f &= ~FLAG_H; }
    if (c != -1) { if (c) ctx.f |= FLAG_C; else ctx.f &= ~FLAG_C; }
}

//Algorithmic Decoding Helpers
static uint8_t get_r8(int idx) {
    switch(idx) {
        case 0: return ctx.b; case 1: return ctx.c; case 2: return ctx.d; case 3: return ctx.e;
        case 4: return ctx.h; case 5: return ctx.l; case 6: return read8(read_hl()); case 7: return ctx.a;
    }
    return 0;
}

static void set_r8(int idx, uint8_t val) {
    switch(idx) {
        case 0: ctx.b = val; break; case 1: ctx.c = val; break; case 2: ctx.d = val; break; case 3: ctx.e = val; break;
        case 4: ctx.h = val; break; case 5: ctx.l = val; break; case 6: write8(read_hl(), val); break; case 7: ctx.a = val; break;
    }
}

static uint16_t get_r16_sp(int p) {
    switch(p) { case 0: return read_bc(); case 1: return read_de(); case 2: return read_hl(); case 3: return ctx.sp; } return 0;
}
static void set_r16_sp(int p, uint16_t val) {
    switch(p) { case 0: write_bc(val); break; case 1: write_de(val); break; case 2: write_hl(val); break; case 3: ctx.sp = val; break; }
}
static uint16_t get_r16_af(int p) {
    switch(p) { case 0: return read_bc(); case 1: return read_de(); case 2: return read_hl(); case 3: return read_af(); } return 0;
}
static void set_r16_af(int p, uint16_t val) {
    switch(p) { case 0: write_bc(val); break; case 1: write_de(val); break; case 2: write_hl(val); break; case 3: write_af(val); break; }
}

static bool check_cond(int y) {
    switch(y) {
        case 0: return !(ctx.f & FLAG_Z); case 1: return (ctx.f & FLAG_Z);
        case 2: return !(ctx.f & FLAG_C); case 3: return (ctx.f & FLAG_C);
    }
    return false;
}

//ALU Core
static void execute_alu(int op, uint8_t val) {
    uint8_t a = ctx.a;
    int carry = (ctx.f & FLAG_C) ? 1 : 0;
    int res;
    switch(op) {
        case 0: res = a + val; set_flags((res & 0xFF) == 0, 0, (a & 0xF) + (val & 0xF) > 0xF, res > 0xFF); ctx.a = res; break;
        case 1: res = a + val + carry; set_flags((res & 0xFF) == 0, 0, (a & 0xF) + (val & 0xF) + carry > 0xF, res > 0xFF); ctx.a = res; break;
        case 2: res = a - val; set_flags((res & 0xFF) == 0, 1, (a & 0x0F) < (val & 0x0F), a < val); ctx.a = res; break;
        case 3: res = a - val - carry; set_flags((res & 0xFF) == 0, 1, (a & 0x0F) - (val & 0x0F) - carry < 0, a - val - carry < 0); ctx.a = res; break;
        case 4: res = a & val; set_flags(res == 0, 0, 1, 0); ctx.a = res; break;
        case 5: res = a ^ val; set_flags(res == 0, 0, 0, 0); ctx.a = res; break;
        case 6: res = a | val; set_flags(res == 0, 0, 0, 0); ctx.a = res; break;
        case 7: res = a - val; set_flags((res & 0xFF) == 0, 1, (a & 0x0F) < (val & 0x0F), a < val); break;
    }
}

//CB Prefix Decoder
static void execute_cb(void) {
    uint8_t op = fetch8();
    int y = (op >> 3) & 7;
    int z = op & 7;
    uint8_t val = get_r8(z);
    int carry = (ctx.f & FLAG_C) ? 1 : 0;

    if (op < 0x40) {
        switch(y) {
            case 0: carry = val >> 7; val = (val << 1) | carry; set_flags(val == 0, 0, 0, carry); break;
            case 1: carry = val & 1; val = (val >> 1) | (carry << 7); set_flags(val == 0, 0, 0, carry); break;
            case 2: { int nc = val >> 7; val = (val << 1) | carry; set_flags(val == 0, 0, 0, nc); break; }
            case 3: { int nc = val & 1; val = (val >> 1) | (carry << 7); set_flags(val == 0, 0, 0, nc); break; }
            case 4: carry = val >> 7; val <<= 1; set_flags(val == 0, 0, 0, carry); break;
            case 5: carry = val & 1; val = (val >> 1) | (val & 0x80); set_flags(val == 0, 0, 0, carry); break;
            case 6: val = (val << 4) | (val >> 4); set_flags(val == 0, 0, 0, 0); break;
            case 7: carry = val & 1; val >>= 1; set_flags(val == 0, 0, 0, carry); break;
        }
        set_r8(z, val);
    } else if (op < 0x80) {
        set_flags((val & (1 << (y & 7))) == 0, 0, 1, -1);
    } else if (op < 0xC0) {
        set_r8(z, val & ~(1 << (y & 7)));
    } else {
        set_r8(z, val | (1 << (y & 7)));
    }
}

//Main CPU Interface
void cpu_init(void) {
    ctx.a = 0x01; ctx.f = 0xB0; ctx.b = 0x00; ctx.c = 0x13; ctx.d = 0x00; ctx.e = 0xD8; ctx.h = 0x01; ctx.l = 0x4D;
    ctx.pc = 0x0100; ctx.sp = 0xFFFE; ctx.ime = false; ctx.halted = false; ei_delay = 0;
}

CPUContext* cpu_get_context(void) { return &ctx; }

void cpu_print_log(void) {
    printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
           ctx.a, ctx.f, ctx.b, ctx.c, ctx.d, ctx.e, ctx.h, ctx.l, ctx.sp, ctx.pc,
           mmu_read8(ctx.pc), mmu_read8(ctx.pc+1), mmu_read8(ctx.pc+2), mmu_read8(ctx.pc+3));
}

int cpu_step(void) {
    cpu_cycles = 0;

    //Interrupt Dispatch
    uint8_t ie = mmu_read8(0xFFFF);
    uint8_t iflag = mmu_read8(0xFF0F);
    uint8_t pending = ie & iflag & 0x1F;

    if (pending) {
        ctx.halted = false; //Wake up regardless of IME
        if (ctx.ime) {
            ctx.ime = false;
            for (int i = 0; i < 5; i++) {
                if (pending & (1 << i)) {
                    mmu_write8(0xFF0F, iflag & ~(1 << i)); //Clear flag
                    tick(); tick(); //2 dummy M-cycles
                    write8(--ctx.sp, ctx.pc >> 8);
                    write8(--ctx.sp, ctx.pc & 0xFF);
                    ctx.pc = 0x0040 + (i * 0x08); //Jump to vector
                    tick(); //1 M-cycle for jump
                    return cpu_cycles; //5 M-cycles (20 T-cycles) total
                }
            }
        }
    }

    //Process EI instruction delay
    if (ei_delay == 1) {
        ctx.ime = true;
        ei_delay = 0;
    }

    if (ctx.halted) { tick(); return 4; }
    
    //Instruction Fetch & Decode
    uint8_t opcode = fetch8();
    int x = opcode >> 6;
    int y = (opcode >> 3) & 7;
    int z = opcode & 7;
    int p = y >> 1;
    int q = y & 1;

    switch (x) {
        case 0:
            switch(z) {
                case 0:
                    if (y == 0) {} //NOP
                    else if (y == 1) { 
                        uint16_t a = fetch16(); 
                        write8(a, ctx.sp & 0xFF); 
                        write8(a + 1, ctx.sp >> 8); 
                    } // LD (a16), SP
                    else if (y == 2) { fetch8(); //STOP consumes 0x00 
                    } 
                    else if (y == 3) { int8_t e = fetch8(); tick(); ctx.pc += e; } // JR e
                    else { int8_t e = fetch8(); if (check_cond(y-4)) { tick(); ctx.pc += e; } } // JR cond, e
                    break;
                case 1:
                    if (q == 0) { set_r16_sp(p, fetch16()); } // LD r16, d16
                    else { 
                        uint16_t hl = read_hl(); uint16_t rr = get_r16_sp(p); uint32_t res = hl + rr;
                        set_flags(-1, 0, (((hl & 0x0FFF) + (rr & 0x0FFF)) > 0x0FFF), res > 0xFFFF);
                        write_hl(res); tick();
                    } // ADD HL, r16
                    break;
                case 2: {
                    uint16_t addr = (p == 0) ? read_bc() : (p == 1) ? read_de() : read_hl();
                    if (q == 0) { write8(addr, ctx.a); } else { ctx.a = read8(addr); }
                    if (p == 2) write_hl(addr + 1); else if (p == 3) write_hl(addr - 1);
                    break;
                }
                case 3:
                    if (q == 0) { set_r16_sp(p, get_r16_sp(p) + 1); tick(); } // INC r16
                    else { set_r16_sp(p, get_r16_sp(p) - 1); tick(); } // DEC r16
                    break;
                case 4: { uint8_t v = get_r8(y) + 1; set_flags(v == 0, 0, (v & 0x0F) == 0, -1); set_r8(y, v); break; } // INC r8
                case 5: { uint8_t v = get_r8(y) - 1; set_flags(v == 0, 1, (v & 0x0F) == 0x0F, -1); set_r8(y, v); break; } // DEC r8
                case 6: set_r8(y, fetch8()); break; // LD r8, d8
                case 7:
                    if (y == 0) { int c = ctx.a >> 7; ctx.a = (ctx.a << 1) | c; set_flags(0, 0, 0, c); } // RLCA
                    else if (y == 1) { int c = ctx.a & 1; ctx.a = (ctx.a >> 1) | (c << 7); set_flags(0, 0, 0, c); } // RRCA
                    else if (y == 2) { int c = ctx.a >> 7; ctx.a = (ctx.a << 1) | ((ctx.f & FLAG_C) ? 1 : 0); set_flags(0, 0, 0, c); } // RLA
                    else if (y == 3) { int c = ctx.a & 1; ctx.a = (ctx.a >> 1) | (((ctx.f & FLAG_C) ? 1 : 0) << 7); set_flags(0, 0, 0, c); } // RRA
                    else if (y == 4) { 
                        int adj = 0; if ((ctx.f & FLAG_H) || (!(ctx.f & FLAG_N) && (ctx.a & 0xF) > 9)) adj |= 0x06;
                        if ((ctx.f & FLAG_C) || (!(ctx.f & FLAG_N) && ctx.a > 0x99)) adj |= 0x60;
                        ctx.a += (ctx.f & FLAG_N) ? -adj : adj; set_flags(ctx.a == 0, -1, 0, (adj & 0x60) ? 1 : 0);
                    } // DAA
                    else if (y == 5) { ctx.a = ~ctx.a; set_flags(-1, 1, 1, -1); } // CPL
                    else if (y == 6) { set_flags(-1, 0, 0, 1); } // SCF
                    else if (y == 7) { set_flags(-1, 0, 0, (ctx.f & FLAG_C) ? 0 : 1); } // CCF
                    break;
            }
            break;
        case 1:
            if (y == 6 && z == 6) ctx.halted = true; // HALT
            else set_r8(y, get_r8(z)); // LD r, r'
            break;
        case 2: execute_alu(y, get_r8(z)); break; // ALU operations
        case 3:
            switch(z) {
                case 0:
                    if (y < 4) { if (check_cond(y)) { tick(); uint16_t lo = read8(ctx.sp++); uint16_t hi = read8(ctx.sp++); ctx.pc = lo | (hi << 8); tick(); } } // RET cond
                    else if (y == 4) { write8(0xFF00 + fetch8(), ctx.a); } // LDH (a8), A
                    else if (y == 5) { 
                        int8_t e = fetch8(); uint32_t res = ctx.sp + e;
                        set_flags(0, 0, ((ctx.sp & 0xF) + (e & 0xF) > 0xF), ((ctx.sp & 0xFF) + (uint8_t)e > 0xFF));
                        ctx.sp = res; tick(); tick();
                    } // ADD SP, e
                    else if (y == 6) { ctx.a = read8(0xFF00 + fetch8()); } // LDH A, (a8)
                    else if (y == 7) {
                        int8_t e = fetch8(); uint32_t res = ctx.sp + e;
                        set_flags(0, 0, ((ctx.sp & 0xF) + (e & 0xF) > 0xF), ((ctx.sp & 0xFF) + (uint8_t)e > 0xFF));
                        write_hl(res); tick();
                    } // LD HL, SP+e
                    break;
                case 1:
                    if (q == 0) { uint16_t lo = read8(ctx.sp++); uint16_t hi = read8(ctx.sp++); set_r16_af(p, lo | (hi << 8)); } // POP
                    else {
                        if (p == 0) { uint16_t lo = read8(ctx.sp++); uint16_t hi = read8(ctx.sp++); ctx.pc = lo | (hi << 8); tick(); } // RET
                        else if (p == 1) { uint16_t lo = read8(ctx.sp++); uint16_t hi = read8(ctx.sp++); ctx.pc = lo | (hi << 8); ctx.ime = true; tick(); } // RETI
                        else if (p == 2) { ctx.pc = read_hl(); } // JP HL
                        else if (p == 3) { ctx.sp = read_hl(); tick(); } // LD SP, HL
                    }
                    break;
                case 2:
                    if (y < 4) { uint16_t a = fetch16(); if (check_cond(y)) { tick(); ctx.pc = a; } } // JP cond, a16
                    else if (y == 4) { write8(0xFF00 + ctx.c, ctx.a); } // LD (C), A
                    else if (y == 5) { write8(fetch16(), ctx.a); } // LD (a16), A
                    else if (y == 6) { ctx.a = read8(0xFF00 + ctx.c); } // LD A, (C)
                    else if (y == 7) { ctx.a = read8(fetch16()); } // LD A, (a16)
                    break;
                case 3:
                    if (y == 0) { uint16_t a = fetch16(); tick(); ctx.pc = a; } // JP a16
                    else if (y == 1) { execute_cb(); } // CB Prefix
                    else if (y == 6) { ctx.ime = false; } // DI
                    else if (y == 7) { ei_delay = 1; } // EI
                    break;
                case 4:
                    if (y < 4) { 
                        uint16_t a = fetch16(); 
                        if (check_cond(y)) { tick(); write8(--ctx.sp, ctx.pc >> 8); write8(--ctx.sp, ctx.pc & 0xFF); ctx.pc = a; } 
                    } //CALL cond, a16
                    break;
                case 5:
                    if (q == 0) { tick(); write8(--ctx.sp, get_r16_af(p) >> 8); write8(--ctx.sp, get_r16_af(p) & 0xFF); } // PUSH
                    else if (p == 0) { uint16_t a = fetch16(); tick(); write8(--ctx.sp, ctx.pc >> 8); write8(--ctx.sp, ctx.pc & 0xFF); ctx.pc = a; } // CALL a16
                    break;
                case 6: execute_alu(y, fetch8()); break; // ALU d8
                case 7: tick(); write8(--ctx.sp, ctx.pc >> 8); write8(--ctx.sp, ctx.pc & 0xFF); ctx.pc = y * 8; break; // RST
            }
            break;
    }
    return cpu_cycles;
}

void cpu_save_state(FILE *f) {
    //Grabs pointer to the cpu context
    CPUContext *ctx = cpu_get_context();
    fwrite(ctx, sizeof(CPUContext), 1, f);
}

void cpu_load_state(FILE *f) {
    //Grabs the pointer and overwrites the memory with the file data
    CPUContext *ctx = cpu_get_context();
    fread(ctx, sizeof(CPUContext), 1, f);
}
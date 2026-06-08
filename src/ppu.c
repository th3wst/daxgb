#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "../include/ppu.h"
#include "../include/mmu.h"
#include "../include/joypad.h"

extern bool running;

static const uint32_t DMG_COLORS[4] = { 0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820 };

static uint8_t vram[0x2000];
static uint8_t oam[0x00A0];
static uint8_t lcdc, stat, scy, scx, ly, lyc, bgp, obp0, obp1, wy, wx;
static int ppu_cycles = 0;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint32_t frame_buffer[PPU_RES_X * PPU_RES_Y];

bool ppu_init(void) {
    memset(vram, 0, sizeof(vram)); 
    memset(oam, 0, sizeof(oam));
    memset(frame_buffer, 0, sizeof(frame_buffer));
    
    lcdc = 0x91; stat = 0x85; ly = 0; ppu_cycles = 0;
    bgp = 0xFC; obp0 = 0xFF; obp1 = 0xFF;
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return false;
    window = SDL_CreateWindow("DAXGB Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PPU_RES_X * 3, PPU_RES_Y * 3, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PPU_RES_X, PPU_RES_Y);
    
    if (renderer) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Set color to Black
        SDL_RenderClear(renderer);                      // Clear the uninitialized buffer
        SDL_RenderPresent(renderer);                    // Push the black frame to the window  //This is here after messing around and implementing the DAXGB debugger
    }

    return (window && renderer && texture);
}

void ppu_cleanup(void) {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

//Helper to check LY=LYC
static void check_lyc(void) {
    if (ly == lyc) {
        stat |= 0x04;
        if (stat & 0x40) {
            mmu_write8(0xFF0F, mmu_read8(0xFF0F) | 0x02);
        }
    } else {
        stat &= ~0x04;
    }
}

void ppu_step(int cycles) {
    if (!(lcdc & 0x80)) return; 
    
    ppu_cycles += cycles;

    switch (stat & 0x03) {
        case 2:
            if (ppu_cycles >= 80) { 
                ppu_cycles -= 80; 
                stat = (stat & ~0x03) | 3; 
            }
            break;
            
        case 3: // Pixel Transfer
            if (ppu_cycles >= 172) { 
                ppu_cycles -= 172; 
                stat = (stat & ~0x03) | 0; // Transition to HBlank
                
                // Trigger HBlank Interrupt if enabled
                if (stat & 0x08) mmu_write8(0xFF0F, mmu_read8(0xFF0F) | 0x02);
                
                int off = ly * PPU_RES_X;
                
                // Background & Window
                for (int px = 0; px < PPU_RES_X; px++) {
                    bool is_win = (lcdc & 0x20) && (ly >= wy) && (px >= (wx - 7));
                    if ((lcdc & 0x01) || is_win) {
                        uint8_t y_pos = is_win ? (ly - wy) : (ly + scy);
                        uint8_t x_pos = is_win ? (px - (wx - 7)) : (px + scx);
                        uint16_t map = is_win ? ((lcdc & 0x40) ? 0x1C00 : 0x1800) : ((lcdc & 0x08) ? 0x1C00 : 0x1800);
                        uint8_t tile = vram[map + (y_pos / 8) * 32 + (x_pos / 8)];
                        uint16_t addr = (lcdc & 0x10) ? (tile * 16) : (0x1000 + ((int8_t)tile * 16));
                        uint8_t d1 = vram[addr + ((y_pos % 8) * 2)];
                        uint8_t d2 = vram[addr + ((y_pos % 8) * 2) + 1];
                        int b = 7 - (x_pos % 8);
                        uint8_t col = ((d2 >> b) & 1) << 1 | ((d1 >> b) & 1);
                        frame_buffer[off + px] = DMG_COLORS[(bgp >> (col * 2)) & 0x03];
                    } else {
                        frame_buffer[off + px] = DMG_COLORS[0];
                    }
                }

                // Sprites
                if (lcdc & 0x02) {
                    for (int i = 0; i < 40; i++) {
                        uint8_t sy = oam[i * 4] - 16;
                        uint8_t sx = oam[i * 4 + 1] - 8;
                        uint8_t tile = oam[i * 4 + 2];
                        uint8_t attrs = oam[i * 4 + 3];
                        
                        if (ly >= sy && ly < sy + 8) {
                            uint8_t line = ly - sy;
                            if (attrs & 0x40) line = 7 - line;
                            uint16_t addr = (tile * 16) + (line * 2);
                            uint8_t d1 = vram[addr];
                            uint8_t d2 = vram[addr + 1];
                            
                            for (int px = 0; px < 8; px++) {
                                if (sx + px < 0 || sx + px >= PPU_RES_X) continue;
                                int b = (attrs & 0x20) ? px : (7 - px);
                                uint8_t col = ((d2 >> b) & 1) << 1 | ((d1 >> b) & 1);
                                if (col != 0) {
                                    uint8_t pal = (attrs & 0x10) ? obp1 : obp0;
                                    frame_buffer[off + sx + px] = DMG_COLORS[(pal >> (col * 2)) & 0x03];
                                }
                            }
                        }
                    }
                }
            }
            break;
            
        case 0: // HBlank
            if (ppu_cycles >= 204) { 
                ppu_cycles -= 204; 
                ly++;
                check_lyc(); // Update coincidence for the new line
                
                if (ly == 144) { 
                    stat = (stat & ~0x03) | 1; // Enter VBlank
                    mmu_write8(0xFF0F, mmu_read8(0xFF0F) | 0x01); // VBLANK Interrupt
                    if (stat & 0x10) mmu_write8(0xFF0F, mmu_read8(0xFF0F) | 0x02);
                    
                    SDL_UpdateTexture(texture, NULL, frame_buffer, PPU_RES_X * 4);
                    SDL_RenderClear(renderer); 
                    SDL_RenderCopy(renderer, texture, NULL, NULL); 
                    SDL_RenderPresent(renderer);
                    
                    SDL_Event e;
                    while (SDL_PollEvent(&e)) { 
                        if (e.type == SDL_QUIT) running = false; 
                        else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) joypad_handle_event(&e); 
                    }
                } else {
                    stat = (stat & ~0x03) | 2;
                    if (stat & 0x20) mmu_write8(0xFF0F, mmu_read8(0xFF0F) | 0x02);
                }
            }
            break;
            
        case 1: // VBlank
            if (ppu_cycles >= 456) { 
                ppu_cycles -= 456; 
                ly++; 
                check_lyc();
                
                if (ly > 153) { 
                    ly = 0; 
                    stat = (stat & ~0x03) | 2;
                    check_lyc();
                    if (stat & 0x20) mmu_write8(0xFF0F, mmu_read8(0xFF0F) | 0x02);
                } 
            }
            break;
    }
}

uint8_t ppu_read_lcd(uint16_t a) { 
    if (a == 0xFF40) return lcdc;
    if (a == 0xFF41) return stat;
    if (a == 0xFF42) return scy;
    if (a == 0xFF43) return scx;
    if (a == 0xFF44) return ly;
    if (a == 0xFF45) return lyc;
    if (a == 0xFF47) return bgp;
    if (a == 0xFF48) return obp0;
    if (a == 0xFF49) return obp1;
    if (a == 0xFF4A) return wy;
    if (a == 0xFF4B) return wx;
    return 0xFF; 
}

void ppu_write_lcd(uint16_t a, uint8_t d) {
    if (a == 0xFF40) { 
        bool was_on = (lcdc & 0x80);
        lcdc = d; 
        if (was_on && !(lcdc & 0x80)) { 
            ly = 0; 
            ppu_cycles = 0; 
            stat &= ~0x03; 
        } 
    }
    else if (a == 0xFF41) stat = (stat & 0x07) | (d & 0x78);
    else if (a == 0xFF42) scy = d;
    else if (a == 0xFF43) scx = d;
    else if (a == 0xFF45) lyc = d;
    else if (a == 0xFF46) { uint16_t s = d << 8; for(int i=0; i<0xA0; i++) oam[i] = mmu_read8(s+i); }
    else if (a == 0xFF47) bgp = d;
    else if (a == 0xFF48) obp0 = d;
    else if (a == 0xFF49) obp1 = d;
    else if (a == 0xFF4A) wy = d;
    else if (a == 0xFF4B) wx = d;
}

void ppu_set_window_title(const char* title) {
    if (window) {
        SDL_SetWindowTitle(window, title);
    }
}

uint8_t ppu_read_ly(void) {
    return ly; 
}

uint8_t ppu_read_vram(uint16_t a) { return vram[a - 0x8000]; }
void ppu_write_vram(uint16_t a, uint8_t d) { vram[a - 0x8000] = d; }
uint8_t ppu_read_oam(uint16_t a) { return oam[a - 0xFE00]; }
void ppu_write_oam(uint16_t a, uint8_t d) { oam[a - 0xFE00] = d; }

void ppu_save_state(FILE *f) { //saves the entire PPU state, including VRAM, OAM, registers, and timing info
    fwrite(vram, 1, sizeof(vram), f);
    fwrite(oam, 1, sizeof(oam), f);
    
    // Save Control and Status
    fwrite(&lcdc, sizeof(uint8_t), 1, f);
    fwrite(&stat, sizeof(uint8_t), 1, f);
    fwrite(&ly, sizeof(uint8_t), 1, f);
    
    fwrite(&scy, sizeof(uint8_t), 1, f);
    fwrite(&scx, sizeof(uint8_t), 1, f);
    fwrite(&wy, sizeof(uint8_t), 1, f);
    fwrite(&wx, sizeof(uint8_t), 1, f);
    
    fwrite(&bgp, sizeof(uint8_t), 1, f);
    fwrite(&obp0, sizeof(uint8_t), 1, f);
    fwrite(&obp1, sizeof(uint8_t), 1, f);
    
    //PPU Timing Sync
    fwrite(&ppu_cycles, sizeof(int), 1, f);
}

void ppu_load_state(FILE *f) {
    fread(vram, 1, sizeof(vram), f);
    fread(oam, 1, sizeof(oam), f);
    
    fread(&lcdc, sizeof(uint8_t), 1, f);
    fread(&stat, sizeof(uint8_t), 1, f);
    fread(&ly, sizeof(uint8_t), 1, f);
    
    fread(&scy, sizeof(uint8_t), 1, f);
    fread(&scx, sizeof(uint8_t), 1, f);
    fread(&wy, sizeof(uint8_t), 1, f);
    fread(&wx, sizeof(uint8_t), 1, f);
    
    fread(&bgp, sizeof(uint8_t), 1, f);
    fread(&obp0, sizeof(uint8_t), 1, f);
    fread(&obp1, sizeof(uint8_t), 1, f);
    
    fread(&ppu_cycles, sizeof(int), 1, f);
}
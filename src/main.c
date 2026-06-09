#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include "../include/cartridge.h"
#include "../include/mmu.h"
#include "../include/cpu.h"
#include "../include/timer.h"
#include "../include/ppu.h"
#include "../include/joypad.h"
#include "../include/debugger.h"
#include "../include/apu.h"
#include "../include/savestate.h"

// Global state
bool running = true;
bool debug_mode_enabled = false;
bool test_mode_enabled = false;

// Handles Ctrl+C safely based on launch arguments
void handle_sigint(int sig) {
    (void)sig;
    if (debug_mode_enabled) {
        if (!debugger_is_active()) {
            debugger_pause();
        } else {
            printf("\n[SIGINT] Force quitting...\n");
            running = false;
        }
    } else {
        printf("\n[SIGINT] Stopping emulation...\n");
        running = false;
    }
}

int main(int argc, char **argv) {
    const char *rom_path = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode_enabled = true;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) { //New test mode flag
            test_mode_enabled = true;
        } else {
            rom_path = argv[i];
        }
    }

    if (!rom_path) {
        fprintf(stderr, "Usage: %s [-d | -t] <path_to_rom.gb>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Register the signal handler
    signal(SIGINT, handle_sigint);

    // 1. Load the Cartridge
    if (!cart_load(rom_path)) {
        fprintf(stderr, "Failed to load ROM. Exiting.\n");
        return EXIT_FAILURE;
    }

    CartridgeContext *cart_ctx = cart_get_context();
    
    // Suppress standard output in test mode for a cleaner terminal report
    if (!test_mode_enabled) {
        printf("Successfully loaded ROM: %s\n", cart_ctx->title);
        printf("ROM Size: %u bytes\n", cart_ctx->rom_size);
    }

    // 2. Initialize Subsystems
    mmu_init();
    cpu_init();
    timer_init();
    joypad_init();
    
    // Only initialize SDL-dependent systems (PPU/APU) if NOT in test mode
    if (!test_mode_enabled) {
        //PPU MUST be initialized first because it wakes up SDL
        if (!ppu_init()) {
            fprintf(stderr, "Failed to initialize PPU and SDL2. Exiting.\n");
            mmu_cleanup();
            cart_cleanup();
            return EXIT_FAILURE;
        }

        //Safely initializes APU
        apu_init();

        printf("\nStarting execution pipeline...\n");
        printf("Controls:\n");
        printf("  D-Pad  : Arrow Keys\n");
        printf("  A      : Z\n");
        printf("  B      : X\n");
        printf("  Start  : Enter\n");
        printf("  Select : Backspace\n");
        printf("  Save   : O\n");
        printf("  Load   : L\n\n");
        
        if (debug_mode_enabled) {
            printf("Debug Mode Enabled. Press Ctrl+C to drop into the debugger.\n");
        } else {
            printf("Press Ctrl+C in the terminal or close the window to stop.\n");
        }
    }

    // Initialize the debugger (will only pause if debug_mode_enabled is true)
    debugger_init(debug_mode_enabled); 

    // Set up FPS tracking variables
    uint32_t fps_last_time = 0;
    if (!test_mode_enabled) {
        fps_last_time = SDL_GetTicks();
    }
    int fps_frames = 0;
    bool show_fps = false;
    char title_buffer[64];
    bool f_key_was_pressed = false;

    // 3. Main Emulation Loop
    while (running) {
        // 1. Check the debugger
        debugger_update();

        // 2. Step the hardware if not paused
        if (!debugger_is_active()) {
            int cycles = cpu_step();
            timer_step(cycles);
            
            // Bypass PPU, APU, and input polling in test mode to run headlessly at max speed
            if (!test_mode_enabled) {
                ppu_step(cycles); // PPU handles the main SDL_PollEvent loop internally
                apu_step(cycles);

                // Frame counter (1 Game Boy frame is exactly 70,224 CPU cycles)
                static int frame_cycles = 0;
                frame_cycles += cycles;
                if (frame_cycles >= 70224) {
                    frame_cycles -= 70224;
                    fps_frames++;
                    
                    // Read keyboard state WITHOUT stealing events from your joypad
                    const Uint8 *state = SDL_GetKeyboardState(NULL);
                    
                    // Toggle FPS counter on full key press
                    bool f_key_is_pressed = state[SDL_SCANCODE_F];
                    if (f_key_is_pressed && !f_key_was_pressed) {
                        show_fps = !show_fps;
                        if (!show_fps) {
                            ppu_set_window_title("DAXGB Emulator"); // Reset title
                        }
                    }
                    f_key_was_pressed = f_key_is_pressed;

                    //Time Travel: Save State (O Key)
                    static bool o_key_was_pressed = false;
                    bool o_key_is_pressed = state[SDL_SCANCODE_O];
                    if (o_key_is_pressed && !o_key_was_pressed) {
                        savestate_save("slot1.state");
                    }
                    o_key_was_pressed = o_key_is_pressed;

                    //Time Travel: Load State (L Key)
                    static bool l_key_was_pressed = false;
                    bool l_key_is_pressed = state[SDL_SCANCODE_L];
                    if (l_key_is_pressed && !l_key_was_pressed) {
                        savestate_load("slot1.state");
                    }
                    l_key_was_pressed = l_key_is_pressed;
                }

                //Calculate FPS every 1000 milliseconds
                uint32_t current_time = SDL_GetTicks();
                if (current_time - fps_last_time >= 1000) {
                    if (show_fps) {
                        snprintf(title_buffer, sizeof(title_buffer), "GB Emulator - FPS: %d", fps_frames);
                        ppu_set_window_title(title_buffer);
                    }
                    fps_frames = 0;
                    fps_last_time = current_time;
                }
            }
            
        } else {
            // 3. When the debugger is PAUSED, the PPU stops running. 
            if (!test_mode_enabled) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) { //poll events so the window doesn't freeze
                    if (e.type == SDL_QUIT) {
                        running = false;
                    }
                }
                
                //Sleep briefly to prevent 100% CPU usage while paused
                SDL_Delay(16); 
            }
        }
    }

    //Cleanup
    if (!test_mode_enabled) {
        printf("Cleaning up resources...\n");
        apu_cleanup();
        ppu_cleanup();
    }
    
    mmu_cleanup();
    cart_cleanup();
    
    if (!test_mode_enabled) {
        printf("Exited cleanly.\n");
    }

    return EXIT_SUCCESS;
}
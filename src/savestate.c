#include <stdio.h>
#include <string.h>
#include "../include/savestate.h"
#include "../include/cpu.h"
#include "../include/mmu.h"
#include "../include/ppu.h"
#include "../include/cartridge.h"

// Magic bytes to identify custom saving format
static const char MAGIC_HEADER[4] = "DAX1"; 

bool savestate_save(const char *filepath) {
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        printf("[SAVESTATE] Error: Could not open %s for writing.\n", filepath);
        return false;
    }

    //Write the magic header
    fwrite(MAGIC_HEADER, 1, 4, f);

    // Command the hardware to dump its state 
    cpu_save_state(f);
    mmu_save_state(f);
    ppu_save_state(f);
    cart_save_state(f);

    fclose(f);
    printf("[SAVESTATE] State successfully saved to: %s\n", filepath);
    return true;
}

bool savestate_load(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf("[SAVESTATE] Error: State file %s not found.\n", filepath);
        return false;
    }

    //Verify the magic header
    char header[4];
    fread(header, 1, 4, f);
    if (strncmp(header, MAGIC_HEADER, 4) != 0) {
        printf("[SAVESTATE] Error: Invalid or corrupt save state file.\n");
        fclose(f);
        return false;
    }

    //Overwrite current hardware memory with the file data
    cpu_load_state(f);
    mmu_load_state(f);
    ppu_load_state(f);
    cart_load_state(f);

    fclose(f);
    printf("[SAVESTATE] Time travel complete. State loaded from: %s\n", filepath);
    return true;
}
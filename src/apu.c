#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>
#include "../include/apu.h"

static uint8_t apu_ram[0x30]; 
static SDL_AudioDeviceID audio_device;

static uint64_t apu_timer = 0; 
static float audio_buffer[1024];
static int audio_buffer_index = 0;

static int frame_sequencer_cycles = 0;
static int frame_sequencer_step = 0;

// High-Pass Filter State variables to remove DC Offset
static float hpf_left_prev_in = 0.0f;
static float hpf_left_prev_out = 0.0f;
static float hpf_right_prev_in = 0.0f;
static float hpf_right_prev_out = 0.0f;

//Channels
static bool ch1_enabled = false; static int ch1_length_timer = 0; static uint8_t ch1_volume = 0; static int ch1_volume_timer = 0; static float ch1_phase = 0.0f; static int ch1_sweep_timer = 0;
static bool ch2_enabled = false; static int ch2_length_timer = 0; static uint8_t ch2_volume = 0; static int ch2_volume_timer = 0; static float ch2_phase = 0.0f;
static bool ch3_enabled = false; static int ch3_length_timer = 0; static float ch3_phase = 0.0f;
static bool ch4_enabled = false; static int ch4_length_timer = 0; static uint8_t ch4_volume = 0; static int ch4_volume_timer = 0; static uint16_t lfsr = 0x7FFF; static int ch4_timer = 0;

static const float DUTY_CYCLES[4] = { 0.125f, 0.250f, 0.500f, 0.750f };
static const int NOISE_DIVISORS[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

// A simple First-Order High-Pass Filter to center the waveform
static float apply_hpf(float in, float *prev_in, float *prev_out) {
    float out = in - *prev_in + 0.995f * (*prev_out);
    *prev_in = in;
    *prev_out = out;
    return out;
}

void apu_init(void) {
    memset(apu_ram, 0, sizeof(apu_ram));
    apu_timer = 0;
    frame_sequencer_cycles = 0;
    frame_sequencer_step = 0;
    audio_buffer_index = 0;
    
    hpf_left_prev_in = 0.0f; hpf_left_prev_out = 0.0f;
    hpf_right_prev_in = 0.0f; hpf_right_prev_out = 0.0f;
    
    ch1_enabled = false; ch1_length_timer = 0; ch1_volume = 0; ch1_volume_timer = 0; ch1_phase = 0.0f; ch1_sweep_timer = 0;
    ch2_enabled = false; ch2_length_timer = 0; ch2_volume = 0; ch2_volume_timer = 0; ch2_phase = 0.0f;
    ch3_enabled = false; ch3_length_timer = 0; ch3_phase = 0.0f;
    ch4_enabled = false; ch4_length_timer = 0; ch4_volume = 0; ch4_volume_timer = 0; lfsr = 0x7FFF; ch4_timer = 0;

    SDL_AudioSpec desired_spec = {0};
    SDL_AudioSpec obtained_spec = {0};

    desired_spec.freq = 44100;
    desired_spec.format = AUDIO_F32SYS;
    desired_spec.channels = 2; 
    desired_spec.samples = 512;
    desired_spec.callback = NULL; 
    desired_spec.userdata = NULL;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &obtained_spec, 0);
    
    if (audio_device == 0) {
        fprintf(stderr, "Failed to open SDL Audio: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_device, 0);
    }
}

void apu_cleanup(void) {
    if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
}

void apu_step(int cycles) {
    //Frame sequencer
    frame_sequencer_cycles += cycles;
    while (frame_sequencer_cycles >= 8192) {
        frame_sequencer_cycles -= 8192;
        frame_sequencer_step = (frame_sequencer_step + 1) % 8;

        if (frame_sequencer_step % 2 == 0) {
            if (apu_ram[0x04] & 0x40) { if (ch1_length_timer > 0 && --ch1_length_timer == 0) ch1_enabled = false; }
            if (apu_ram[0x09] & 0x40) { if (ch2_length_timer > 0 && --ch2_length_timer == 0) ch2_enabled = false; }
            if (apu_ram[0x1E] & 0x40) { if (ch3_length_timer > 0 && --ch3_length_timer == 0) ch3_enabled = false; }
            if (apu_ram[0x23] & 0x40) { if (ch4_length_timer > 0 && --ch4_length_timer == 0) ch4_enabled = false; }
        }

        if (frame_sequencer_step == 2 || frame_sequencer_step == 6) {
            uint8_t sweep_pace = (apu_ram[0x00] >> 4) & 0x07;
            if (sweep_pace > 0 && ch1_enabled) {
                if (ch1_sweep_timer > 0) ch1_sweep_timer--;
                if (ch1_sweep_timer <= 0) {
                    ch1_sweep_timer = sweep_pace;
                    uint8_t shift = apu_ram[0x00] & 0x07;
                    if (shift > 0) {
                        uint16_t freq = apu_ram[0x03] | ((apu_ram[0x04] & 0x07) << 8);
                        uint16_t delta = freq >> shift;
                        if (apu_ram[0x00] & 0x08) { 
                            if (freq >= delta) freq -= delta; else freq = 0;
                        } else { 
                            freq += delta;
                            if (freq > 2047) ch1_enabled = false; 
                        }
                        if (ch1_enabled) {
                            apu_ram[0x03] = freq & 0xFF;
                            apu_ram[0x04] = (apu_ram[0x04] & 0xF8) | ((freq >> 8) & 0x07);
                        }
                    }
                }
            }
        }

        if (frame_sequencer_step == 7) {
            uint8_t ch1_pace = apu_ram[0x02] & 0x07;
            if (ch1_pace != 0) {
                if (ch1_volume_timer > 0) ch1_volume_timer--;
                if (ch1_volume_timer == 0) {
                    ch1_volume_timer = ch1_pace;
                    uint8_t dir = (apu_ram[0x02] & 0x08) >> 3;
                    if (dir == 1 && ch1_volume < 15) ch1_volume++;
                    else if (dir == 0 && ch1_volume > 0) ch1_volume--;
                }
            }
            uint8_t ch2_pace = apu_ram[0x07] & 0x07;
            if (ch2_pace != 0) {
                if (ch2_volume_timer > 0) ch2_volume_timer--;
                if (ch2_volume_timer == 0) {
                    ch2_volume_timer = ch2_pace;
                    uint8_t dir = (apu_ram[0x07] & 0x08) >> 3;
                    if (dir == 1 && ch2_volume < 15) ch2_volume++;
                    else if (dir == 0 && ch2_volume > 0) ch2_volume--;
                }
            }
            uint8_t ch4_pace = apu_ram[0x21] & 0x07;
            if (ch4_pace != 0) {
                if (ch4_volume_timer > 0) ch4_volume_timer--;
                if (ch4_volume_timer == 0) {
                    ch4_volume_timer = ch4_pace;
                    uint8_t dir = (apu_ram[0x21] & 0x08) >> 3;
                    if (dir == 1 && ch4_volume < 15) ch4_volume++;
                    else if (dir == 0 && ch4_volume > 0) ch4_volume--;
                }
            }
        }
    }

    if (ch4_enabled) {
        ch4_timer -= cycles;
        while (ch4_timer <= 0) {
            uint8_t shift_clock = apu_ram[0x22] >> 4;
            uint8_t div_code = apu_ram[0x22] & 0x07;
            bool width_mode = (apu_ram[0x22] & 0x08) != 0;
            ch4_timer += NOISE_DIVISORS[div_code] << shift_clock;
            uint8_t xor_res = (lfsr & 0x01) ^ ((lfsr >> 1) & 0x01);
            lfsr >>= 1;
            lfsr |= (xor_res << 14);
            if (width_mode) {
                lfsr &= ~(1 << 6);
                lfsr |= (xor_res << 6);
            }
        }
    }

    // Exact fractional timing (sample rate * cycles)
    apu_timer += (uint64_t)cycles * 44100ULL;
    
    while (apu_timer >= 4194304ULL) {
        apu_timer -= 4194304ULL;
        
        float ch1_out = 0.0f;
        float ch2_out = 0.0f;
        float ch3_out = 0.0f;
        float ch4_out = 0.0f;

        if (apu_ram[0x16] & 0x80) { 
            
            // CH1: Nyquist Limit Check
            if (ch1_enabled && ch1_volume > 0) {
                uint16_t freq_raw = apu_ram[0x03] | ((apu_ram[0x04] & 0x07) << 8);
                float freq_hz = 131072.0f / (2048.0f - freq_raw);
                if (freq_hz > 22000.0f) {
                    ch1_out = 0.0f;
                } else {
                    ch1_phase += freq_hz / 44100.0f;
                    if (ch1_phase >= 1.0f) ch1_phase -= 1.0f;
                    ch1_out = (ch1_phase < DUTY_CYCLES[apu_ram[0x01] >> 6]) ? 1.0f : -1.0f;
                    ch1_out *= ((ch1_volume / 15.0f) * 0.025f); 
                }
            }

            // CH2: Nyquist Limit Check
            if (ch2_enabled && ch2_volume > 0) {
                uint16_t freq_raw = apu_ram[0x08] | ((apu_ram[0x09] & 0x07) << 8);
                float freq_hz = 131072.0f / (2048.0f - freq_raw);
                if (freq_hz > 22000.0f) {
                    ch2_out = 0.0f;
                } else {
                    ch2_phase += freq_hz / 44100.0f;
                    if (ch2_phase >= 1.0f) ch2_phase -= 1.0f;
                    ch2_out = (ch2_phase < DUTY_CYCLES[apu_ram[0x06] >> 6]) ? 1.0f : -1.0f;
                    ch2_out *= ((ch2_volume / 15.0f) * 0.025f);
                }
            }

            // CH3: Nyquist Limit Check
            if (ch3_enabled && (apu_ram[0x1A] & 0x80)) { 
                uint16_t freq_raw = apu_ram[0x1D] | ((apu_ram[0x1E] & 0x07) << 8);
                float freq_hz = 65536.0f / (2048.0f - freq_raw);
                if (freq_hz > 22000.0f) {
                    ch3_out = 0.0f;
                } else {
                    ch3_phase += freq_hz / 44100.0f;
                    if (ch3_phase >= 1.0f) ch3_phase -= 1.0f;
                    int sample_index = (int)(ch3_phase * 32.0f) % 32;
                    uint8_t wave_byte = apu_ram[0x20 + (sample_index / 2)];
                    uint8_t wave_sample = (sample_index % 2 == 0) ? (wave_byte >> 4) : (wave_byte & 0x0F);
                    uint8_t vol_code = (apu_ram[0x1C] >> 5) & 0x03;
                    float vol_mult = (vol_code == 1) ? 1.0f : (vol_code == 2) ? 0.5f : (vol_code == 3) ? 0.25f : 0.0f;
                    ch3_out = ((wave_sample / 7.5f) - 1.0f) * (vol_mult * 0.025f);
                }
            }

            if (ch4_enabled && ch4_volume > 0) {
                ch4_out = (((lfsr & 0x01) == 0) ? 1.0f : -1.0f) * ((ch4_volume / 15.0f) * 0.025f);
            }
        }

        uint8_t nr51 = apu_ram[0x15]; 
        float raw_left = 0.0f;
        float raw_right = 0.0f;

        if (nr51 & 0x01) raw_left += ch1_out;
        if (nr51 & 0x02) raw_left += ch2_out;
        if (nr51 & 0x04) raw_left += ch3_out;
        if (nr51 & 0x08) raw_left += ch4_out;

        if (nr51 & 0x10) raw_right += ch1_out;
        if (nr51 & 0x20) raw_right += ch2_out;
        if (nr51 & 0x40) raw_right += ch3_out;
        if (nr51 & 0x80) raw_right += ch4_out;

        //Master volume control
        uint8_t nr50 = apu_ram[0x14];
        float master_left_vol = (((nr50 >> 4) & 0x07) + 1) / 8.0f;
        float master_right_vol = ((nr50 & 0x07) + 1) / 8.0f;

        raw_left *= master_left_vol;
        raw_right *= master_right_vol;

        // Apply High-Pass Filter to remove DC offset before queuing
        float filtered_left = apply_hpf(raw_left, &hpf_left_prev_in, &hpf_left_prev_out);
        float filtered_right = apply_hpf(raw_right, &hpf_right_prev_in, &hpf_right_prev_out);

        audio_buffer[audio_buffer_index++] = filtered_left; 
        audio_buffer[audio_buffer_index++] = filtered_right; 

        if (audio_buffer_index >= 1024) {
            
            // AUDIO BACKPRESSURE: Double runway to 8192 (~90ms)
            while (SDL_GetQueuedAudioSize(audio_device) > 8192 * sizeof(float)) {
                SDL_Delay(1);
            }
            
            SDL_QueueAudio(audio_device, audio_buffer, sizeof(audio_buffer));
            audio_buffer_index = 0;
        }
    }
}

uint8_t apu_read(uint16_t address) {
    if (address >= 0xFF10 && address <= 0xFF3F) return apu_ram[address - 0xFF10];
    return 0xFF;
}

void apu_write(uint16_t address, uint8_t data) {
    if (address >= 0xFF10 && address <= 0xFF3F) {
        uint16_t offset = address - 0xFF10;
        apu_ram[offset] = data;

        if (address == 0xFF12) { if ((data & 0xF8) == 0) ch1_enabled = false; }
        else if (address == 0xFF11) ch1_length_timer = 64 - (data & 0x3F);
        else if (address == 0xFF14) { 
            if (data & 0x80) { 
                if ((apu_ram[0x02] & 0xF8) != 0) ch1_enabled = true;
                if (ch1_length_timer == 0) ch1_length_timer = 64;
                ch1_volume = apu_ram[0x02] >> 4; 
                ch1_volume_timer = apu_ram[0x02] & 0x07;
                ch1_sweep_timer = (apu_ram[0x00] >> 4) & 0x07;
                ch1_phase = 0.0f; 
            }
        }

        if (address == 0xFF17) { if ((data & 0xF8) == 0) ch2_enabled = false; }
        else if (address == 0xFF16) ch2_length_timer = 64 - (data & 0x3F);
        else if (address == 0xFF19) { 
            if (data & 0x80) { 
                if ((apu_ram[0x07] & 0xF8) != 0) ch2_enabled = true; 
                if (ch2_length_timer == 0) ch2_length_timer = 64;
                ch2_volume = apu_ram[0x07] >> 4; 
                ch2_volume_timer = apu_ram[0x07] & 0x07;
                ch2_phase = 0.0f; 
            }
        }

        if (address == 0xFF1A) { if ((data & 0x80) == 0) ch3_enabled = false; }
        else if (address == 0xFF1B) ch3_length_timer = 256 - data; 
        else if (address == 0xFF1E) { 
            if (data & 0x80) {
                if (apu_ram[0x1A] & 0x80) ch3_enabled = true;
                if (ch3_length_timer == 0) ch3_length_timer = 256;
                ch3_phase = 0.0f; 
            }
        }

        if (address == 0xFF21) { if ((data & 0xF8) == 0) ch4_enabled = false; }
        else if (address == 0xFF20) ch4_length_timer = 64 - (data & 0x3F);
        else if (address == 0xFF23) { 
            if (data & 0x80) { 
                if ((apu_ram[0x21] & 0xF8) != 0) ch4_enabled = true; 
                if (ch4_length_timer == 0) ch4_length_timer = 64;
                ch4_volume = apu_ram[0x21] >> 4; 
                ch4_volume_timer = apu_ram[0x21] & 0x07;
                lfsr = 0x7FFF; 
            }
        }
    }
}
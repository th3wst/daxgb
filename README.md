# daxgb

A minimalist, high-performance Game Boy emulator built in C. Designed for systems-level educational purposes and retro-hardware experimentation.

## Screenshots

![Pokémon Red](screenshots/pokemonred.png)

![Wario Land](screenshots/warioland.png)

![Debugger](screenshots/debugger.png)

## Features & Functionality

- Full support for the DMG-01 (original Game Boy) architecture.
- Accurate CPU timing and memory management.
- Pixel-perfect rendering with support for background, window, and sprite layers.
- Multi-channel audio generation supporting Square Wave, Noise, and Wave channels.
- Save state serialization and deserialization.
- Built-in terminal debugger with instruction disassembly and FPS monitoring.

## Known Limitations

- Audio currently experiences timing-related artifacts and synchronization issues.
- MBC3 Real-Time Clock (RTC) support is currently a stub.
- Serial port / Link Cable support is stubbed to bypass hardware-check hangs.
- Does not yet pass the complete Blargg test suite; minor edge-case timing inaccuracies may exist.

## Quick Start

### Requirements

- GCC
- Make
- SDL2 Development Libraries (`libsdl2`)

### Build

```bash
make
```

### Usage

```bash
./daxgb_emulator [-d] <path_to_rom.gb>
```

### Options

- `-d` or `--debug` : Launch emulator in debugger mode.

## Controls

- D-Pad: Arrow Keys
- A: Z
- B: X
- Start: Enter
- Select: Backspace
- Toggle FPS Display: F
- Save State: O
- Load State: L

## Debugger Controls

- Ctrl+C: Pause execution and enter debugger.
- s: Step one instruction.
- r: Print CPU registers and current instruction disassembly.
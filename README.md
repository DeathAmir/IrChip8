# IrChip8

IrChip8 is a portable C++20 CHIP-8 emulator with optional Super-CHIP instructions, configurable compatibility quirks, keyboard input, gamepad input, timers, sound, and automated Windows/Linux builds.

## Features

- Complete classic CHIP-8 instruction set
- Super-CHIP 1.1 conveniences: high/low resolution, scrolling, 16x16 sprites, large font, RPL flags and exit opcode
- 4 KiB memory, 16 V registers, index register, 16-level stack, delay/sound timers and 16-key keypad
- XOR sprite drawing and collision flag handling
- Compatibility profiles for common historical behavior differences
- SDL3 desktop frontend with resizable pixel rendering
- Keyboard and SDL gamepad/controller support
- 440 Hz CHIP-8 buzzer
- Configurable CPU clock
- Unit tests for arithmetic, memory, drawing, keypad wait behavior and compatibility quirks
- GitHub Actions artifacts for Windows x64 and Linux x64

## Build

Requirements for a normal desktop build are CMake 3.24+ and a C++20 compiler. SDL 3.4.12 is fetched automatically by CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

To build only the dependency-free emulator core and tests:

```bash
cmake -S . -B build -DIRCHIP8_BUILD_APP=OFF -DIRCHIP8_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run

```bash
IrChip8 path/to/game.ch8
```

Useful options:

```text
--profile modern|vip|chip48
--hz 700
--no-schip
```

Examples:

```bash
IrChip8 PONG.ch8
IrChip8 TETRIS.ch8 --profile vip --hz 600
```

On Windows you can also drag a `.ch8` file onto `IrChip8.exe`; Windows passes the ROM path to the program.

## Keyboard

The left side is the original CHIP-8 hexadecimal keypad and the right side is the PC keyboard mapping.

```text
1 2 3 C        1 2 3 4
4 5 6 D        Q W E R
7 8 9 E        A S D F
A 0 B F        Z X C V
```

Extra keys:

- `Esc`: quit
- `F1`: modern profile
- `F2`: COSMAC VIP-style profile
- `F3`: CHIP-48-style profile
- `F5`: reload current ROM

## Controller mapping

SDL-compatible gamepads are detected at startup and can be hot-plugged.

```text
D-pad Up/Down/Left/Right -> 2/8/4/6
South/East/West/North   -> 5/0/7/9
L1/R1                   -> 1/3
Left/Right stick click  -> A/B
Back/Start              -> C/D
Guide/Misc1             -> E/F
```

## Compatibility profiles

CHIP-8 interpreters historically disagree on a few instructions. IrChip8 keeps these behaviors explicit instead of hard-coding one interpretation.

- `modern`: shifts use Vx, Fx55/Fx65 keep I unchanged, Bnnn uses V0, drawing clips at the edge
- `vip`: shifts use Vy, Fx55/Fx65 increment I, logic operations clear VF, drawing wraps
- `chip48`: shifts use Vx, Fx55/Fx65 keep I unchanged, Bxnn-style jump uses Vx, drawing clips

If a ROM behaves incorrectly, switching profiles is the first thing to try.

## ROMs

ROM files are not included in this repository. Use ROMs that you have the right to run or public-domain/homebrew CHIP-8 programs.

Good compatibility tests include the Timendus CHIP-8 test suite: splash screen, IBM logo, Corax+ opcode test, flags test, quirks test, keypad test and beep test.

## Project layout

```text
include/irchip8/chip8.hpp  CPU/core public API
src/chip8.cpp               CHIP-8 and Super-CHIP implementation
src/main.cpp                SDL3 window, renderer, audio and input frontend
tests/chip8_tests.cpp       dependency-free core unit tests
.github/workflows/build.yml Windows/Linux CI and artifacts
```

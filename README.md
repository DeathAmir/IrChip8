# IrChip8

IrChip8 is a portable C++20 CHIP-8 emulator with classic CHIP-8, Super-CHIP support, compatibility quirks, native SDL3 frontends, and a compact WebAssembly browser frontend.

## Features

- Complete classic CHIP-8 instruction set
- Super-CHIP 1.1 conveniences: high/low resolution, scrolling, 16x16 sprites, large font, RPL flags and exit opcode
- 4 KiB memory, 16 V registers, index register, 16-level stack, delay/sound timers and 16-key keypad
- XOR sprite drawing and collision flag handling
- Compatibility profiles for common historical behavior differences
- SDL3 desktop frontend with resizable pixel rendering
- WebAssembly frontend with no SDL dependency in the browser build
- Browser `.ch8` file picker and drag/drop ROM loading
- Heuristic ROM dialect/profile detection with a manual compatibility override
- Keyboard, virtual keypad and gamepad/controller support
- 440 Hz CHIP-8 buzzer on desktop and Web Audio buzzer in browsers
- Configurable CPU clock
- Unit tests for arithmetic, memory, drawing, keypad wait behavior and compatibility quirks
- GitHub Actions artifacts for Windows x64, Linux x64 and WebAssembly
- UPX-packed native release executables
- `-Oz`, LTO and Binaryen `wasm-opt -Oz` size optimization for WebAssembly

## Desktop build

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

## WebAssembly build

The web target uses Emscripten and intentionally does not compile SDL into the browser binary. The C++ emulator core becomes `irchip8.wasm`; a small JavaScript runtime handles Canvas rendering, Web Audio and browser input.

With an activated Emscripten SDK:

```bash
emcmake cmake -S . -B build-web \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DIRCHIP8_BUILD_APP=OFF \
  -DIRCHIP8_BUILD_TESTS=OFF \
  -DIRCHIP8_BUILD_WEB=ON

cmake --build build-web --parallel
wasm-opt build-web/irchip8.wasm -Oz --strip-debug --strip-producers -o build-web/irchip8.optimized.wasm
```

The browser distribution consists of:

```text
index.html
app.js
style.css
irchip8.js
irchip8.wasm
```

Serve those files over HTTP. For example:

```bash
python3 -m http.server 8080 -d dist-web
```

Then open `http://localhost:8080` in a browser, click **Open a .ch8 ROM**, choose a ROM, and IrChip8 loads it directly into WebAssembly memory.

Browsers do not allow a page to silently scan arbitrary files on a user's computer. The user explicitly selects the `.ch8` file with the browser picker or drags it onto the page.

### Automatic ROM detection

The web frontend scans aligned CHIP-8 opcodes before execution:

- Super-CHIP-only instructions such as scrolling, high/low resolution switching, 16x16 drawing, high font and RPL opcodes strongly select Super-CHIP/CHIP-48 behavior.
- Original `0NNN` calls and two-register shift patterns increase the COSMAC VIP score.
- `BxNN`-style jumps increase the CHIP-48 score.
- Ambiguous ROMs default to the modern profile.

CHIP-8 compatibility differences are behavioral quirks, so no static detector can identify every historical ROM perfectly. The UI therefore shows a confidence value and keeps a manual Modern / VIP / CHIP-48 override.

## Desktop run

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

The left side is the original CHIP-8 hexadecimal keypad and the right side is the PC/browser keyboard mapping.

```text
1 2 3 C        1 2 3 4
4 5 6 D        Q W E R
7 8 9 E        A S D F
A 0 B F        Z X C V
```

Desktop extra keys:

- `Esc`: quit
- `F1`: modern profile
- `F2`: COSMAC VIP-style profile
- `F3`: CHIP-48-style profile
- `F5`: reload current ROM

## Controller mapping

The SDL desktop frontend supports SDL-compatible gamepads and hot-plugging. The WebAssembly frontend uses the browser Gamepad API and maps the D-pad to CHIP-8 directional-style keys plus the four face buttons to common action keys.

Desktop mapping:

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

## Size optimization

Native Windows/Linux artifacts are packed with UPX during GitHub Actions and validated with `upx -t` before packaging.

WebAssembly is not an UPX executable format, so the web pipeline uses the WebAssembly-native optimization path instead:

- Emscripten `-Oz`
- link-time optimization (`-flto`)
- small `emmalloc` allocator
- no SDL in the browser target
- disabled Emscripten filesystem and assertions
- Binaryen `wasm-opt -Oz`
- stripped debug/producer metadata
- pre-generated gzip copies of the JS and WASM payloads for servers configured to serve compressed assets

## ROMs

ROM files are not included in this repository. Use ROMs that you have the right to run or public-domain/homebrew CHIP-8 programs.

Good compatibility tests include the Timendus CHIP-8 test suite: splash screen, IBM logo, Corax+ opcode test, flags test, quirks test, keypad test and beep test.

## Project layout

```text
include/irchip8/chip8.hpp  CPU/core public API
src/chip8.cpp               CHIP-8 and Super-CHIP implementation
src/main.cpp                SDL3 desktop window, renderer, audio and input
web/wasm_bridge.cpp         narrow C ABI exported from C++ into WebAssembly
web/index.html              browser ROM picker and emulator UI
web/app.js                  browser runtime, Canvas, Web Audio and Gamepad input
web/style.css                responsive web frontend styling
tests/chip8_tests.cpp       dependency-free core unit tests
.github/workflows/build.yml Windows/Linux/Wasmtime-style WebAssembly CI artifacts
```

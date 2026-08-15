# IrChip8

IrChip8 is a compact C++20 CHIP-8 emulator with classic CHIP-8, CHIP-48 compatibility quirks and Super-CHIP support. The same emulation core is used by native SDL3 builds and the browser/WebAssembly build.

## Release targets

| Target | Runtime | Package |
|---|---|---|
| Windows x64 | C++20 + SDL3 | `IrChip8-Windows-x64.zip` |
| Linux x64 | C++20 + SDL3 | `IrChip8-Linux-x64.tar.gz` |
| Browser | C++20 + SDL3 + Emscripten/WebAssembly | `IrChip8-WebAssembly.zip` |

Native release executables are packed with UPX 5.2.0 using `--best --lzma` and checked with `upx -t` before packaging.

## WebAssembly build

The web version is no longer a JavaScript-rendered emulator. Rendering, input handling, timing and audio are driven by SDL3 from C++ and compiled by Emscripten to WebAssembly.

The web release contains two ways to run it:

- `index.html`, `index.js`, `index.wasm`: normal files for a web server or static hosting.
- `IrChip8-Standalone.html`: one self-contained HTML file with WebAssembly embedded by Emscripten `SINGLE_FILE`; this is the easiest local launcher and avoids the usual `file://` fetch problem.

Open the standalone HTML, choose or drag a `.ch8` ROM, inspect the detected architecture/profile and press **Run**. The normal hosted build exposes the same controls.

Browser controls:

- `Choose .ch8`: select a ROM from disk.
- Drag/drop: drop a `.ch8` file on the ROM area.
- `Run`: start/pause emulation.
- `Reset`: reload the current ROM from the beginning.
- Profile: Auto, Modern, COSMAC VIP or CHIP-48.
- CPU: 60–2000 Hz from the UI.
- `Fullscreen`: fullscreen the SDL canvas.

Keyboard layout:

```text
CHIP-8          PC
1 2 3 C         1 2 3 4
4 5 6 D         Q W E R
7 8 9 E         A S D F
A 0 B F         Z X C V
```

Space also toggles Run/Pause in the SDL3 web runtime. Browser gamepads are delivered through SDL3's gamepad layer.

### ROM architecture detection

Before execution, the browser runtime scans ROM opcodes and estimates one of:

- CHIP-8
- CHIP-48
- Super-CHIP

Super-CHIP can be identified strongly from dedicated instructions such as scroll, high/low-resolution and RPL opcodes. Historical CHIP-8 quirks are not always statically distinguishable, so detection reports a confidence value and the UI keeps a manual profile override.

## Desktop usage

```bash
IrChip8 path/to/game.ch8
```

Options:

```text
--profile modern|vip|chip48
--hz 700
--no-schip
```

On Windows you can drag a `.ch8` file onto `IrChip8.exe`.

Desktop hotkeys:

- `Esc`: exit
- `F1`: Modern profile
- `F2`: COSMAC VIP profile
- `F3`: CHIP-48 profile
- `F5`: reload ROM

SDL gamepad mapping:

```text
D-pad Up/Down/Left/Right -> 2/8/4/6
South/East/West/North   -> 5/0/7/9
L1/R1                   -> 1/3
Left/Right stick click  -> A/B
Back/Start              -> C/D
Guide/Misc1             -> E/F
```

## Compatibility profiles

- `modern`: shifts use Vx; Fx55/Fx65 keep I unchanged; Bnnn uses V0; drawing clips.
- `vip`: shifts use Vy; Fx55/Fx65 increment I; logic operations clear VF; drawing wraps.
- `chip48`: shifts use Vx; Fx55/Fx65 keep I unchanged; Bxnn-style jumps use Vx; drawing clips.

## Implemented instruction families

The core implements the complete classic CHIP-8 instruction set plus useful Super-CHIP 1.1 instructions including resolution switching, scrolling, 16x16 sprites, large font, RPL flags and exit.

The classic machine model uses 4 KiB memory and starts programs at `0x200`, so the largest directly supported classic/Super-CHIP ROM payload is 3584 bytes. XO-CHIP is not a target of this version.

## Source layout

```text
include/irchip8/chip8.hpp   public core API
src/chip8.cpp               CPU, memory, display, timers and quirks
src/main.cpp                SDL3 Windows/Linux frontend
web/sdl_web.cpp             SDL3/Emscripten browser runtime
web/shell.html               browser UI / Emscripten shell
tests/chip8_tests.cpp       core unit tests
.github/workflows/build.yml CI, UPX packaging, WASM packaging and release
```

`web/sdl_web.cpp` intentionally uses an old-school single-translation-unit style with global state, compact switches, macros and label-based ROM probing. This keeps the requested retro/spaghetti frontend style isolated from the tested emulator core, so instruction correctness remains easier to verify.

## Build desktop

Requirements: CMake 3.24+ and a C++20 compiler. SDL 3.4.12 is fetched by CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Build WebAssembly

Use a current Emscripten SDK:

```bash
emcmake cmake -S . -B build-web \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DIRCHIP8_BUILD_APP=OFF \
  -DIRCHIP8_BUILD_TESTS=OFF \
  -DIRCHIP8_BUILD_WEB=ON
cmake --build build-web --parallel
```

This produces a hosted `index.html/index.js/index.wasm` build and the single-file `IrChip8-Standalone.html` launcher.

## Tests

Core tests cover arithmetic/carry/borrow, BCD and register memory operations, sprite collision, shift quirks, keypad wait behavior and Super-CHIP resolution/exit behavior.

For broader compatibility testing, the Timendus CHIP-8 test suite is useful. ROMs are not bundled with IrChip8; use ROMs you have the right to run.

## License

MIT. See `LICENSE`.

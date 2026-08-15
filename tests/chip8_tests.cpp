#include "irchip8/chip8.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

using irchip8::Chip8;
using irchip8::Config;
using irchip8::Profile;

namespace {
int failures = 0;

void check(bool condition, std::string_view name) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void run_cycles(Chip8& c, int count) {
    for (int i = 0; i < count; ++i) check(c.cycle(), "cycle executes");
}

void arithmetic_test() {
    const std::array<std::uint8_t, 10> rom = {
        0x60,0xFA, 0x61,0x0A, 0x80,0x14, 0x80,0x15, 0x80,0x17
    };
    Chip8 c;
    check(c.load_rom(rom), "load arithmetic ROM");
    run_cycles(c, 3);
    check(c.registers()[0] == 4, "8xy4 result");
    check(c.registers()[0xF] == 1, "8xy4 carry");
    check(c.cycle(), "subtract cycle");
    check(c.registers()[0] == 250, "8xy5 wraps");
    check(c.registers()[0xF] == 0, "8xy5 borrow");
    check(c.cycle(), "reverse subtract cycle");
    check(c.registers()[0] == 16, "8xy7 result");
    check(c.registers()[0xF] == 0, "8xy7 borrow");
}

void memory_test() {
    const std::array<std::uint8_t, 14> rom = {
        0x60,0x7B, 0xA3,0x00, 0xF0,0x33,
        0x61,0x42, 0xF1,0x55, 0x60,0x00, 0xF1,0x65
    };
    Chip8 c;
    check(c.load_rom(rom), "load memory ROM");
    run_cycles(c, 3);
    check(c.memory()[0x300] == 1 && c.memory()[0x301] == 2 && c.memory()[0x302] == 3, "BCD store");
    run_cycles(c, 4);
    check(c.registers()[0] == 123 && c.registers()[1] == 66, "Fx55/Fx65 roundtrip");
}

void draw_test() {
    const std::array<std::uint8_t, 14> rom = {
        0x60,0x00, 0x61,0x00, 0xA2,0x0C, 0xD0,0x11, 0xD0,0x11, 0x12,0x00, 0x80,0x00
    };
    Chip8 c;
    check(c.load_rom(rom), "load draw ROM");
    run_cycles(c, 4);
    check(c.framebuffer()[0] == 1, "draw turns pixel on");
    check(c.registers()[0xF] == 0, "first draw no collision");
    check(c.cycle(), "second draw cycle");
    check(c.framebuffer()[0] == 0, "xor draw turns pixel off");
    check(c.registers()[0xF] == 1, "collision flag");
}

void quirk_test() {
    const std::array<std::uint8_t, 8> rom = {0x60,0x03, 0x61,0x08, 0x80,0x16, 0xA3,0x00};
    Chip8 modern(Config{.profile=Profile::Modern});
    check(modern.load_rom(rom), "load modern quirk ROM");
    run_cycles(modern, 3);
    check(modern.registers()[0] == 1, "modern shift uses Vx");

    Chip8 vip(Config{.profile=Profile::Vip});
    check(vip.load_rom(rom), "load vip quirk ROM");
    run_cycles(vip, 3);
    check(vip.registers()[0] == 4, "VIP shift uses Vy");
}

void keypad_wait_test() {
    const std::array<std::uint8_t, 4> rom = {0xF2,0x0A, 0x63,0x99};
    Chip8 c;
    check(c.load_rom(rom), "load keypad ROM");
    check(c.cycle(), "Fx0A cycle");
    const auto held_pc = c.pc();
    check(c.cycle(), "waiting cycle");
    check(c.pc() == held_pc, "Fx0A stalls CPU");
    c.set_key(0xA, true);
    check(c.registers()[2] == 0xA, "Fx0A captures key");
    check(c.cycle(), "resume after key");
    check(c.registers()[3] == 0x99, "CPU resumes after key");
}

void superchip_test() {
    const std::array<std::uint8_t, 6> rom = {0x00,0xFF, 0x00,0xFE, 0x00,0xFD};
    Chip8 c;
    check(c.load_rom(rom), "load SCHIP ROM");
    check(c.cycle() && c.high_resolution(), "00FF high resolution");
    check(c.cycle() && !c.high_resolution(), "00FE low resolution");
    check(c.cycle() && c.halted(), "00FD exit");
}
}

int main() {
    arithmetic_test();
    memory_test();
    draw_test();
    quirk_test();
    keypad_wait_test();
    superchip_test();
    if (failures == 0) {
        std::cout << "All IrChip8 core tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
}

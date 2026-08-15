#include "irchip8/chip8.hpp"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct Detection {
    int dialect = 0;      // 0 CHIP-8, 1 CHIP-48, 2 Super-CHIP
    int profile = 0;      // 0 modern, 1 VIP, 2 CHIP-48
    int confidence = 55;  // heuristic confidence, 0..100
    int superchip_hits = 0;
    int vip_hits = 0;
    int chip48_hits = 0;
};

irchip8::Chip8 machine{};
std::vector<std::uint8_t> loaded_rom{};
Detection last_detection{};

bool is_superchip_opcode(std::uint16_t opcode) {
    if (opcode == 0x00FB || opcode == 0x00FC || opcode == 0x00FD ||
        opcode == 0x00FE || opcode == 0x00FF) {
        return true;
    }
    if ((opcode & 0xFFF0u) == 0x00C0u) {
        return true;
    }
    if ((opcode & 0xF00Fu) == 0xD000u) {
        return true;
    }
    const auto low = static_cast<std::uint8_t>(opcode & 0x00FFu);
    return (opcode & 0xF000u) == 0xF000u &&
           (low == 0x30u || low == 0x75u || low == 0x85u);
}

Detection detect_rom(std::span<const std::uint8_t> rom) {
    Detection result{};

    for (std::size_t offset = 0; offset + 1 < rom.size(); offset += 2) {
        const auto opcode = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(rom[offset]) << 8u) | rom[offset + 1]);

        if (is_superchip_opcode(opcode)) {
            result.superchip_hits += 1;
            continue;
        }

        const auto top = static_cast<std::uint16_t>(opcode & 0xF000u);
        const auto x = static_cast<std::uint8_t>((opcode >> 8u) & 0x0Fu);
        const auto y = static_cast<std::uint8_t>((opcode >> 4u) & 0x0Fu);
        const auto low_nibble = static_cast<std::uint8_t>(opcode & 0x000Fu);

        // Original COSMAC VIP programs can rely on 0NNN machine-code calls.
        if (top == 0x0000u && opcode != 0x0000u && opcode != 0x00E0u && opcode != 0x00EEu) {
            result.vip_hits += 3;
        }

        // 8XY6 / 8XYE with X != Y strongly suggests the original VIP shift semantics.
        if ((top == 0x8000u) && (low_nibble == 0x6u || low_nibble == 0xEu) && x != y) {
            result.vip_hits += 2;
        }

        // BxNN-style jumps with X != 0 are a strong CHIP-48/SCHIP family signal.
        if (top == 0xB000u && x != 0) {
            result.chip48_hits += 3;
        }
    }

    if (result.superchip_hits > 0) {
        result.dialect = 2;
        result.profile = 2;
        result.confidence = std::min(99, 88 + result.superchip_hits * 2);
        return result;
    }

    if (result.chip48_hits >= result.vip_hits + 2) {
        result.dialect = 1;
        result.profile = 2;
        result.confidence = std::min(90, 62 + result.chip48_hits * 4);
        return result;
    }

    if (result.vip_hits >= result.chip48_hits + 2) {
        result.dialect = 0;
        result.profile = 1;
        result.confidence = std::min(88, 60 + result.vip_hits * 4);
        return result;
    }

    result.dialect = 0;
    result.profile = 0;
    result.confidence = 55;
    return result;
}

irchip8::Profile profile_from_code(int code) {
    switch (code) {
        case 1: return irchip8::Profile::Vip;
        case 2: return irchip8::Profile::Chip48;
        default: return irchip8::Profile::Modern;
    }
}

int profile_to_code(irchip8::Profile profile) {
    switch (profile) {
        case irchip8::Profile::Vip: return 1;
        case irchip8::Profile::Chip48: return 2;
        case irchip8::Profile::Modern: return 0;
    }
    return 0;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int irchip8_probe_rom(const std::uint8_t* data, int size) {
    if (data == nullptr || size <= 0) {
        last_detection = {};
        return -1;
    }
    last_detection = detect_rom(std::span<const std::uint8_t>(data, static_cast<std::size_t>(size)));
    return last_detection.dialect;
}

EMSCRIPTEN_KEEPALIVE int irchip8_probe_profile() {
    return last_detection.profile;
}

EMSCRIPTEN_KEEPALIVE int irchip8_probe_confidence() {
    return last_detection.confidence;
}

EMSCRIPTEN_KEEPALIVE int irchip8_probe_superchip_hits() {
    return last_detection.superchip_hits;
}

EMSCRIPTEN_KEEPALIVE int irchip8_probe_vip_hits() {
    return last_detection.vip_hits;
}

EMSCRIPTEN_KEEPALIVE int irchip8_probe_chip48_hits() {
    return last_detection.chip48_hits;
}

EMSCRIPTEN_KEEPALIVE int irchip8_load_rom(const std::uint8_t* data, int size, int profile_override) {
    if (data == nullptr || size <= 0) {
        return 0;
    }

    loaded_rom.assign(data, data + size);
    last_detection = detect_rom(loaded_rom);

    const int selected_profile = profile_override >= 0 ? profile_override : last_detection.profile;
    machine.set_profile(profile_from_code(selected_profile));
    return machine.load_rom(loaded_rom) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int irchip8_reset() {
    if (loaded_rom.empty()) {
        return 0;
    }
    return machine.load_rom(loaded_rom) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int irchip8_cycle_n(int cycles) {
    if (cycles <= 0) {
        return 0;
    }

    int completed = 0;
    for (; completed < cycles; ++completed) {
        if (!machine.cycle()) {
            break;
        }
    }
    return completed;
}

EMSCRIPTEN_KEEPALIVE void irchip8_tick_timers() {
    machine.tick_timers();
}

EMSCRIPTEN_KEEPALIVE void irchip8_set_key(int key, int pressed) {
    if (key < 0 || key > 0xF) {
        return;
    }
    machine.set_key(static_cast<std::uint8_t>(key), pressed != 0);
}

EMSCRIPTEN_KEEPALIVE const std::uint8_t* irchip8_framebuffer() {
    return machine.framebuffer().data();
}

EMSCRIPTEN_KEEPALIVE int irchip8_display_width() {
    return machine.display_width();
}

EMSCRIPTEN_KEEPALIVE int irchip8_display_height() {
    return machine.display_height();
}

EMSCRIPTEN_KEEPALIVE int irchip8_draw_pending() {
    return machine.draw_pending() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void irchip8_clear_draw_pending() {
    machine.clear_draw_pending();
}

EMSCRIPTEN_KEEPALIVE int irchip8_sound_active() {
    return machine.sound_active() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int irchip8_halted() {
    return machine.halted() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* irchip8_error() {
    return machine.error().data();
}

EMSCRIPTEN_KEEPALIVE int irchip8_active_profile() {
    return profile_to_code(machine.profile());
}

} // extern "C"

int main() {
    return 0;
}

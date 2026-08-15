#include "irchip8/chip8.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace irchip8 {
namespace {
constexpr std::uint16_t FontStart = 0x050;
constexpr std::uint16_t HighFontStart = 0x0A0;
constexpr std::array<std::uint8_t, 80> LowFont = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
    0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0,
    0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0,
    0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80
};

std::uint8_t expand_nibble(std::uint8_t row) {
    std::uint8_t out = 0;
    for (int bit = 0; bit < 4; ++bit) {
        if (row & (0x80u >> bit)) {
            const int dst = bit * 2;
            out |= static_cast<std::uint8_t>((0xC0u) >> dst);
        }
    }
    return out;
}
}

Chip8::Chip8(Config config) : config_(config), quirks_(quirks_for(config.profile)) {
    reset();
}

Quirks Chip8::quirks_for(Profile profile) {
    switch (profile) {
        case Profile::Vip:
            return {.shift_uses_vy=true, .load_store_increment_i=true, .jump_uses_vx=false,
                    .logic_resets_vf=true, .draw_wrap=true, .fx1e_sets_vf=false};
        case Profile::Chip48:
            return {.shift_uses_vy=false, .load_store_increment_i=false, .jump_uses_vx=true,
                    .logic_resets_vf=false, .draw_wrap=false, .fx1e_sets_vf=false};
        case Profile::Modern:
        default:
            return {.shift_uses_vy=false, .load_store_increment_i=false, .jump_uses_vx=false,
                    .logic_resets_vf=false, .draw_wrap=false, .fx1e_sets_vf=false};
    }
}

void Chip8::set_profile(Profile profile) {
    config_.profile = profile;
    quirks_ = quirks_for(profile);
}

void Chip8::reset() {
    memory_.fill(0);
    v_.fill(0);
    stack_.fill(0);
    keypad_.fill(false);
    framebuffer_.fill(0);
    rpl_flags_.fill(0);
    i_ = 0;
    pc_ = ProgramStart;
    sp_ = 0;
    delay_timer_ = 0;
    sound_timer_ = 0;
    waiting_register_ = -1;
    high_resolution_ = false;
    draw_pending_ = true;
    halted_ = false;
    error_.clear();
    rng_state_ = config_.random_seed ? config_.random_seed : 0xC0FFEEu;
    install_fonts();
}

void Chip8::install_fonts() {
    std::copy(LowFont.begin(), LowFont.end(), memory_.begin() + FontStart);
    for (int glyph = 0; glyph < 16; ++glyph) {
        for (int row = 0; row < 5; ++row) {
            const std::uint8_t expanded = expand_nibble(LowFont[glyph * 5 + row]);
            const auto dst = HighFontStart + glyph * 10 + row * 2;
            memory_[dst] = expanded;
            memory_[dst + 1] = expanded;
        }
    }
}

bool Chip8::load_rom(std::span<const std::uint8_t> rom) {
    if (rom.empty()) {
        set_error("ROM is empty");
        return false;
    }
    if (rom.size() > MemorySize - ProgramStart) {
        set_error("ROM is too large for CHIP-8 memory");
        return false;
    }
    reset();
    std::copy(rom.begin(), rom.end(), memory_.begin() + ProgramStart);
    return true;
}

bool Chip8::load_rom_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error("Could not open ROM: " + path);
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return load_rom(bytes);
}

void Chip8::set_error(std::string message) {
    error_ = std::move(message);
    halted_ = true;
}

std::uint8_t Chip8::random_byte() {
    std::uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return static_cast<std::uint8_t>(x & 0xFFu);
}

void Chip8::clear_display() {
    framebuffer_.fill(0);
    draw_pending_ = true;
}

void Chip8::scroll_down(std::uint8_t rows) {
    const int w = display_width();
    const int h = display_height();
    const int shift = high_resolution_ ? rows : rows / 2;
    if (shift <= 0) return;
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            framebuffer_[y * HighWidth + x] = (y >= shift) ? framebuffer_[(y - shift) * HighWidth + x] : 0;
        }
    }
    draw_pending_ = true;
}

void Chip8::scroll_left() {
    const int w = display_width();
    const int h = display_height();
    const int amount = high_resolution_ ? 4 : 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            framebuffer_[y * HighWidth + x] = (x + amount < w) ? framebuffer_[y * HighWidth + x + amount] : 0;
        }
    }
    draw_pending_ = true;
}

void Chip8::scroll_right() {
    const int w = display_width();
    const int h = display_height();
    const int amount = high_resolution_ ? 4 : 2;
    for (int y = 0; y < h; ++y) {
        for (int x = w - 1; x >= 0; --x) {
            framebuffer_[y * HighWidth + x] = (x >= amount) ? framebuffer_[y * HighWidth + x - amount] : 0;
        }
    }
    draw_pending_ = true;
}

bool Chip8::cycle() {
    if (halted_) return false;
    if (waiting_register_ >= 0) return true;
    if (pc_ > MemorySize - 2) {
        set_error("Program counter moved outside CHIP-8 memory");
        return false;
    }
    const std::uint16_t opcode = static_cast<std::uint16_t>((memory_[pc_] << 8u) | memory_[pc_ + 1]);
    pc_ += 2;
    return execute(opcode);
}

bool Chip8::execute(std::uint16_t opcode) {
    const std::uint8_t x = static_cast<std::uint8_t>((opcode >> 8u) & 0x0Fu);
    const std::uint8_t y = static_cast<std::uint8_t>((opcode >> 4u) & 0x0Fu);
    const std::uint8_t n = static_cast<std::uint8_t>(opcode & 0x0Fu);
    const std::uint8_t kk = static_cast<std::uint8_t>(opcode & 0xFFu);
    const std::uint16_t nnn = static_cast<std::uint16_t>(opcode & 0x0FFFu);

    switch (opcode & 0xF000u) {
        case 0x0000u:
            if (opcode == 0x00E0u) { clear_display(); return true; }
            if (opcode == 0x00EEu) {
                if (sp_ == 0) { set_error("Stack underflow on RET"); return false; }
                pc_ = stack_[--sp_];
                return true;
            }
            if (config_.enable_superchip) {
                if ((opcode & 0xFFF0u) == 0x00C0u) { scroll_down(n); return true; }
                if (opcode == 0x00FBu) { scroll_right(); return true; }
                if (opcode == 0x00FCu) { scroll_left(); return true; }
                if (opcode == 0x00FDu) { halted_ = true; return true; }
                if (opcode == 0x00FEu) { high_resolution_ = false; clear_display(); return true; }
                if (opcode == 0x00FFu) { high_resolution_ = true; clear_display(); return true; }
            }
            return true;

        case 0x1000u: pc_ = nnn; return true;
        case 0x2000u:
            if (sp_ >= stack_.size()) { set_error("Stack overflow on CALL"); return false; }
            stack_[sp_++] = pc_;
            pc_ = nnn;
            return true;
        case 0x3000u: if (v_[x] == kk) pc_ += 2; return true;
        case 0x4000u: if (v_[x] != kk) pc_ += 2; return true;
        case 0x5000u:
            if (n != 0) break;
            if (v_[x] == v_[y]) pc_ += 2;
            return true;
        case 0x6000u: v_[x] = kk; return true;
        case 0x7000u: v_[x] = static_cast<std::uint8_t>(v_[x] + kk); return true;
        case 0x8000u: {
            const std::uint8_t vx = v_[x];
            const std::uint8_t vy = v_[y];
            switch (n) {
                case 0x0: v_[x] = vy; return true;
                case 0x1: v_[x] = static_cast<std::uint8_t>(vx | vy); if (quirks_.logic_resets_vf) v_[0xF] = 0; return true;
                case 0x2: v_[x] = static_cast<std::uint8_t>(vx & vy); if (quirks_.logic_resets_vf) v_[0xF] = 0; return true;
                case 0x3: v_[x] = static_cast<std::uint8_t>(vx ^ vy); if (quirks_.logic_resets_vf) v_[0xF] = 0; return true;
                case 0x4: {
                    const std::uint16_t sum = static_cast<std::uint16_t>(vx) + vy;
                    v_[x] = static_cast<std::uint8_t>(sum & 0xFFu);
                    v_[0xF] = sum > 0xFFu ? 1 : 0;
                    return true;
                }
                case 0x5: v_[x] = static_cast<std::uint8_t>(vx - vy); v_[0xF] = vx >= vy ? 1 : 0; return true;
                case 0x6: {
                    const std::uint8_t src = quirks_.shift_uses_vy ? vy : vx;
                    v_[x] = static_cast<std::uint8_t>(src >> 1u);
                    v_[0xF] = static_cast<std::uint8_t>(src & 1u);
                    return true;
                }
                case 0x7: v_[x] = static_cast<std::uint8_t>(vy - vx); v_[0xF] = vy >= vx ? 1 : 0; return true;
                case 0xE: {
                    const std::uint8_t src = quirks_.shift_uses_vy ? vy : vx;
                    v_[x] = static_cast<std::uint8_t>(src << 1u);
                    v_[0xF] = static_cast<std::uint8_t>((src >> 7u) & 1u);
                    return true;
                }
                default: break;
            }
            break;
        }
        case 0x9000u:
            if (n != 0) break;
            if (v_[x] != v_[y]) pc_ += 2;
            return true;
        case 0xA000u: i_ = nnn; return true;
        case 0xB000u: {
            const std::uint8_t base_reg = quirks_.jump_uses_vx ? x : 0;
            pc_ = static_cast<std::uint16_t>(nnn + v_[base_reg]);
            return true;
        }
        case 0xC000u: v_[x] = static_cast<std::uint8_t>(random_byte() & kk); return true;
        case 0xD000u: return draw_sprite(v_[x], v_[y], n);
        case 0xE000u:
            if (kk == 0x9E) { if (v_[x] < 16 && keypad_[v_[x]]) pc_ += 2; return true; }
            if (kk == 0xA1) { if (v_[x] >= 16 || !keypad_[v_[x]]) pc_ += 2; return true; }
            break;
        case 0xF000u:
            switch (kk) {
                case 0x07: v_[x] = delay_timer_; return true;
                case 0x0A: waiting_register_ = x; return true;
                case 0x15: delay_timer_ = v_[x]; return true;
                case 0x18: sound_timer_ = v_[x]; return true;
                case 0x1E: {
                    const std::uint32_t sum = static_cast<std::uint32_t>(i_) + v_[x];
                    if (quirks_.fx1e_sets_vf) v_[0xF] = sum > 0x0FFFu ? 1 : 0;
                    i_ = static_cast<std::uint16_t>(sum & 0xFFFFu);
                    return true;
                }
                case 0x29: i_ = static_cast<std::uint16_t>(FontStart + (v_[x] & 0x0Fu) * 5u); return true;
                case 0x30:
                    if (!config_.enable_superchip) break;
                    i_ = static_cast<std::uint16_t>(HighFontStart + (v_[x] & 0x0Fu) * 10u);
                    return true;
                case 0x33:
                    if (static_cast<std::size_t>(i_) + 2 >= MemorySize) { set_error("BCD write outside memory"); return false; }
                    memory_[i_] = static_cast<std::uint8_t>(v_[x] / 100u);
                    memory_[i_ + 1] = static_cast<std::uint8_t>((v_[x] / 10u) % 10u);
                    memory_[i_ + 2] = static_cast<std::uint8_t>(v_[x] % 10u);
                    return true;
                case 0x55:
                    if (i_ + x >= MemorySize) { set_error("Register store outside memory"); return false; }
                    for (std::uint8_t r = 0; r <= x; ++r) memory_[i_ + r] = v_[r];
                    if (quirks_.load_store_increment_i) i_ = static_cast<std::uint16_t>(i_ + x + 1);
                    return true;
                case 0x65:
                    if (i_ + x >= MemorySize) { set_error("Register load outside memory"); return false; }
                    for (std::uint8_t r = 0; r <= x; ++r) v_[r] = memory_[i_ + r];
                    if (quirks_.load_store_increment_i) i_ = static_cast<std::uint16_t>(i_ + x + 1);
                    return true;
                case 0x75:
                    if (!config_.enable_superchip || x > 7) break;
                    for (std::uint8_t r = 0; r <= x; ++r) rpl_flags_[r] = v_[r];
                    return true;
                case 0x85:
                    if (!config_.enable_superchip || x > 7) break;
                    for (std::uint8_t r = 0; r <= x; ++r) v_[r] = rpl_flags_[r];
                    return true;
                default: break;
            }
            break;
        default: break;
    }

    std::ostringstream oss;
    oss << "Unsupported opcode 0x" << std::hex << opcode << " at 0x" << (pc_ - 2);
    set_error(oss.str());
    return false;
}

bool Chip8::draw_sprite(std::uint8_t vx, std::uint8_t vy, std::uint8_t n) {
    const int w = display_width();
    const int h = display_height();
    const bool wide = config_.enable_superchip && n == 0;
    const int rows = wide ? 16 : n;
    const int bytes_per_row = wide ? 2 : 1;
    if (rows == 0) return true;
    if (static_cast<std::size_t>(i_) + static_cast<std::size_t>(rows * bytes_per_row) > MemorySize) {
        set_error("Sprite read outside memory");
        return false;
    }

    std::uint8_t collision = 0;
    for (int row = 0; row < rows; ++row) {
        std::uint16_t bits = memory_[i_ + row * bytes_per_row];
        int columns = 8;
        if (wide) {
            bits = static_cast<std::uint16_t>((bits << 8u) | memory_[i_ + row * 2 + 1]);
            columns = 16;
        }
        for (int col = 0; col < columns; ++col) {
            const std::uint16_t mask = static_cast<std::uint16_t>(1u << (columns - 1 - col));
            if ((bits & mask) == 0) continue;
            int px = static_cast<int>(vx) + col;
            int py = static_cast<int>(vy) + row;
            if (quirks_.draw_wrap) {
                px %= w;
                py %= h;
            } else if (px >= w || py >= h) {
                continue;
            }
            auto& pixel = framebuffer_[py * HighWidth + px];
            collision |= pixel;
            pixel ^= 1u;
        }
    }
    v_[0xF] = collision ? 1 : 0;
    draw_pending_ = true;
    return true;
}

void Chip8::tick_timers() {
    if (delay_timer_ > 0) --delay_timer_;
    if (sound_timer_ > 0) --sound_timer_;
}

void Chip8::set_key(std::uint8_t key, bool pressed) {
    if (key >= keypad_.size()) return;
    const bool was_pressed = keypad_[key];
    keypad_[key] = pressed;
    if (pressed && !was_pressed && waiting_register_ >= 0) {
        v_[static_cast<std::size_t>(waiting_register_)] = key;
        waiting_register_ = -1;
    }
}

bool Chip8::key(std::uint8_t key) const {
    return key < keypad_.size() && keypad_[key];
}

Profile parse_profile(std::string_view name) {
    if (name == "vip" || name == "VIP") return Profile::Vip;
    if (name == "chip48" || name == "CHIP48" || name == "chip-48") return Profile::Chip48;
    return Profile::Modern;
}

std::string_view profile_name(Profile profile) {
    switch (profile) {
        case Profile::Vip: return "vip";
        case Profile::Chip48: return "chip48";
        case Profile::Modern: default: return "modern";
    }
}

} // namespace irchip8

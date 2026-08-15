#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace irchip8 {

enum class Profile {
    Modern,
    Vip,
    Chip48,
};

struct Quirks {
    bool shift_uses_vy = false;
    bool load_store_increment_i = false;
    bool jump_uses_vx = false;
    bool logic_resets_vf = false;
    bool draw_wrap = false;
    bool fx1e_sets_vf = false;
};

struct Config {
    Profile profile = Profile::Modern;
    bool enable_superchip = true;
    std::uint32_t random_seed = 0xC0FFEEu;
};

class Chip8 {
public:
    static constexpr std::size_t MemorySize = 4096;
    static constexpr std::uint16_t ProgramStart = 0x200;
    static constexpr int LowWidth = 64;
    static constexpr int LowHeight = 32;
    static constexpr int HighWidth = 128;
    static constexpr int HighHeight = 64;

    explicit Chip8(Config config = {});

    void reset();
    bool load_rom(std::span<const std::uint8_t> rom);
    bool load_rom_file(const std::string& path);

    bool cycle();
    void tick_timers();

    void set_key(std::uint8_t key, bool pressed);
    bool key(std::uint8_t key) const;

    [[nodiscard]] std::span<const std::uint8_t> framebuffer() const noexcept { return framebuffer_; }
    [[nodiscard]] int display_width() const noexcept { return high_resolution_ ? HighWidth : LowWidth; }
    [[nodiscard]] int display_height() const noexcept { return high_resolution_ ? HighHeight : LowHeight; }
    [[nodiscard]] bool high_resolution() const noexcept { return high_resolution_; }
    [[nodiscard]] bool draw_pending() const noexcept { return draw_pending_; }
    void clear_draw_pending() noexcept { draw_pending_ = false; }

    [[nodiscard]] bool sound_active() const noexcept { return sound_timer_ > 0; }
    [[nodiscard]] std::uint8_t delay_timer() const noexcept { return delay_timer_; }
    [[nodiscard]] std::uint8_t sound_timer() const noexcept { return sound_timer_; }
    [[nodiscard]] bool halted() const noexcept { return halted_; }
    [[nodiscard]] std::string_view error() const noexcept { return error_; }

    [[nodiscard]] std::uint16_t pc() const noexcept { return pc_; }
    [[nodiscard]] std::uint16_t index() const noexcept { return i_; }
    [[nodiscard]] std::span<const std::uint8_t, 16> registers() const noexcept { return v_; }
    [[nodiscard]] std::span<const std::uint8_t, MemorySize> memory() const noexcept { return memory_; }

    void set_profile(Profile profile);
    [[nodiscard]] Profile profile() const noexcept { return config_.profile; }
    [[nodiscard]] const Quirks& quirks() const noexcept { return quirks_; }

private:
    static Quirks quirks_for(Profile profile);
    void install_fonts();
    void clear_display();
    void scroll_down(std::uint8_t rows);
    void scroll_left();
    void scroll_right();
    void set_error(std::string message);
    bool execute(std::uint16_t opcode);
    bool draw_sprite(std::uint8_t vx, std::uint8_t vy, std::uint8_t n);
    std::uint8_t random_byte();

    Config config_{};
    Quirks quirks_{};

    std::array<std::uint8_t, MemorySize> memory_{};
    std::array<std::uint8_t, 16> v_{};
    std::array<std::uint16_t, 16> stack_{};
    std::array<bool, 16> keypad_{};
    std::array<std::uint8_t, HighWidth * HighHeight> framebuffer_{};
    std::array<std::uint8_t, 8> rpl_flags_{};

    std::uint16_t i_ = 0;
    std::uint16_t pc_ = ProgramStart;
    std::uint8_t sp_ = 0;
    std::uint8_t delay_timer_ = 0;
    std::uint8_t sound_timer_ = 0;
    int waiting_register_ = -1;
    bool high_resolution_ = false;
    bool draw_pending_ = true;
    bool halted_ = false;
    std::string error_{};
    std::uint32_t rng_state_ = 0;
};

Profile parse_profile(std::string_view name);
std::string_view profile_name(Profile profile);

} // namespace irchip8

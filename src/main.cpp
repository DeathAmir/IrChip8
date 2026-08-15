#include "irchip8/chip8.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string rom_path;
    irchip8::Profile profile = irchip8::Profile::Modern;
    int hz = 700;
    bool superchip = true;
};

void print_usage() {
    std::cout
        << "IrChip8 - CHIP-8 / Super-CHIP emulator\n\n"
        << "Usage:\n"
        << "  IrChip8 <rom.ch8> [--profile modern|vip|chip48] [--hz 700] [--no-schip]\n\n"
        << "Keyboard mapping:\n"
        << "  CHIP-8: 1 2 3 C    PC: 1 2 3 4\n"
        << "          4 5 6 D        Q W E R\n"
        << "          7 8 9 E        A S D F\n"
        << "          A 0 B F        Z X C V\n\n"
        << "F1/F2/F3 switch profiles, F5 reloads the ROM, Esc exits.\n";
}

std::optional<Options> parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return std::nullopt;
        }
        if (arg == "--profile" && i + 1 < argc) {
            options.profile = irchip8::parse_profile(argv[++i]);
            continue;
        }
        if (arg == "--hz" && i + 1 < argc) {
            options.hz = std::clamp(std::atoi(argv[++i]), 60, 5000);
            continue;
        }
        if (arg == "--no-schip") {
            options.superchip = false;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << '\n';
            return std::nullopt;
        }
        options.rom_path = arg;
    }
    if (options.rom_path.empty()) {
        print_usage();
        return std::nullopt;
    }
    return options;
}

std::optional<std::uint8_t> keyboard_to_chip8(SDL_Keycode key) {
    switch (key) {
        case SDLK_1: return 0x1; case SDLK_2: return 0x2; case SDLK_3: return 0x3; case SDLK_4: return 0xC;
        case SDLK_Q: return 0x4; case SDLK_W: return 0x5; case SDLK_E: return 0x6; case SDLK_R: return 0xD;
        case SDLK_A: return 0x7; case SDLK_S: return 0x8; case SDLK_D: return 0x9; case SDLK_F: return 0xE;
        case SDLK_Z: return 0xA; case SDLK_X: return 0x0; case SDLK_C: return 0xB; case SDLK_V: return 0xF;
        default: return std::nullopt;
    }
}

std::optional<std::uint8_t> gamepad_to_chip8(SDL_GamepadButton button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0x2;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 0x8;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 0x4;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 0x6;
        case SDL_GAMEPAD_BUTTON_SOUTH: return 0x5;
        case SDL_GAMEPAD_BUTTON_EAST: return 0x0;
        case SDL_GAMEPAD_BUTTON_WEST: return 0x7;
        case SDL_GAMEPAD_BUTTON_NORTH: return 0x9;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 0x1;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 0x3;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 0xA;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 0xB;
        case SDL_GAMEPAD_BUTTON_BACK: return 0xC;
        case SDL_GAMEPAD_BUTTON_START: return 0xD;
        case SDL_GAMEPAD_BUTTON_GUIDE: return 0xE;
        case SDL_GAMEPAD_BUTTON_MISC1: return 0xF;
        default: return std::nullopt;
    }
}

void open_gamepad(SDL_JoystickID id, std::vector<SDL_Gamepad*>& gamepads) {
    if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
        gamepads.push_back(pad);
        const char* name = SDL_GetGamepadName(pad);
        std::cout << "Gamepad connected: " << (name ? name : "Unknown") << '\n';
    }
}

void remove_gamepad(SDL_JoystickID id, std::vector<SDL_Gamepad*>& gamepads) {
    auto it = std::remove_if(gamepads.begin(), gamepads.end(), [id](SDL_Gamepad* pad) {
        if (SDL_GetGamepadID(pad) != id) return false;
        SDL_CloseGamepad(pad);
        return true;
    });
    gamepads.erase(it, gamepads.end());
}

void render(SDL_Renderer* renderer, const irchip8::Chip8& chip8) {
    int output_w = 0;
    int output_h = 0;
    SDL_GetRenderOutputSize(renderer, &output_w, &output_h);

    SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
    SDL_RenderClear(renderer);

    const float sx = static_cast<float>(output_w) / static_cast<float>(chip8.display_width());
    const float sy = static_cast<float>(output_h) / static_cast<float>(chip8.display_height());
    const float scale = std::max(1.0f, std::min(sx, sy));
    const float draw_w = scale * static_cast<float>(chip8.display_width());
    const float draw_h = scale * static_cast<float>(chip8.display_height());
    const float ox = (static_cast<float>(output_w) - draw_w) * 0.5f;
    const float oy = (static_cast<float>(output_h) - draw_h) * 0.5f;

    std::vector<SDL_FRect> lit;
    lit.reserve(static_cast<std::size_t>(chip8.display_width() * chip8.display_height() / 2));
    const auto fb = chip8.framebuffer();
    for (int y = 0; y < chip8.display_height(); ++y) {
        for (int x = 0; x < chip8.display_width(); ++x) {
            if (fb[static_cast<std::size_t>(y * irchip8::Chip8::HighWidth + x)] == 0) continue;
            lit.push_back(SDL_FRect{
                ox + static_cast<float>(x) * scale,
                oy + static_cast<float>(y) * scale,
                scale,
                scale
            });
        }
    }

    SDL_SetRenderDrawColor(renderer, 238, 244, 255, 255);
    if (!lit.empty()) SDL_RenderFillRects(renderer, lit.data(), static_cast<int>(lit.size()));
    SDL_RenderPresent(renderer);
}

class Beeper {
public:
    bool init() {
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 48000};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) {
            std::cerr << "Audio disabled: " << SDL_GetError() << '\n';
            return false;
        }
        if (!SDL_ResumeAudioStreamDevice(stream_)) {
            std::cerr << "Audio resume failed: " << SDL_GetError() << '\n';
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
            return false;
        }
        return true;
    }

    void update(bool active) {
        if (!stream_) return;
        if (!active) {
            if (was_active_) SDL_ClearAudioStream(stream_);
            was_active_ = false;
            return;
        }
        was_active_ = true;
        constexpr int sample_rate = 48000;
        constexpr int chunk_samples = 480;
        constexpr float frequency = 440.0f;
        constexpr float tau = 6.2831853071795864769f;
        if (SDL_GetAudioStreamQueued(stream_) > static_cast<int>(sizeof(float) * sample_rate / 10)) return;
        std::array<float, chunk_samples> samples{};
        const float phase_step = tau * frequency / static_cast<float>(sample_rate);
        for (float& sample : samples) {
            sample = std::sin(phase_) * 0.12f;
            phase_ += phase_step;
            if (phase_ >= tau) phase_ -= tau;
        }
        SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
    }

    ~Beeper() {
        if (stream_) SDL_DestroyAudioStream(stream_);
    }

private:
    SDL_AudioStream* stream_ = nullptr;
    float phase_ = 0.0f;
    bool was_active_ = false;
};

std::string window_title(const Options& options) {
    std::filesystem::path p(options.rom_path);
    return "IrChip8 - " + p.filename().string() + " [" + std::string(irchip8::profile_name(options.profile)) + "]";
}

} // namespace

int main(int argc, char** argv) {
    auto parsed = parse_args(argc, argv);
    if (!parsed) return argc > 1 ? EXIT_SUCCESS : EXIT_FAILURE;
    Options options = *parsed;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "IrChip8",
        960,
        480,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    irchip8::Chip8 chip8(irchip8::Config{
        .profile = options.profile,
        .enable_superchip = options.superchip,
        .random_seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())
    });
    if (!chip8.load_rom_file(options.rom_path)) {
        std::cerr << chip8.error() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_SetWindowTitle(window, window_title(options).c_str());

    std::vector<SDL_Gamepad*> gamepads;
    int pad_count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&pad_count)) {
        for (int i = 0; i < pad_count; ++i) open_gamepad(ids[i], gamepads);
        SDL_free(ids);
    }

    Beeper beeper;
    beeper.init();

    using clock = std::chrono::steady_clock;
    auto previous = clock::now();
    double cpu_accumulator = 0.0;
    double timer_accumulator = 0.0;
    bool running = true;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
                if (pressed && !event.key.repeat) {
                    if (event.key.key == SDLK_ESCAPE) running = false;
                    if (event.key.key == SDLK_F1) { options.profile = irchip8::Profile::Modern; chip8.set_profile(options.profile); }
                    if (event.key.key == SDLK_F2) { options.profile = irchip8::Profile::Vip; chip8.set_profile(options.profile); }
                    if (event.key.key == SDLK_F3) { options.profile = irchip8::Profile::Chip48; chip8.set_profile(options.profile); }
                    if (event.key.key == SDLK_F5 && !chip8.load_rom_file(options.rom_path)) {
                        std::cerr << chip8.error() << '\n';
                    }
                    SDL_SetWindowTitle(window, window_title(options).c_str());
                }
                if (const auto key = keyboard_to_chip8(event.key.key)) chip8.set_key(*key, pressed);
                continue;
            }
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                open_gamepad(event.gdevice.which, gamepads);
                continue;
            }
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                remove_gamepad(event.gdevice.which, gamepads);
                continue;
            }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
                if (const auto key = gamepad_to_chip8(static_cast<SDL_GamepadButton>(event.gbutton.button))) {
                    chip8.set_key(*key, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                }
            }
        }

        const auto now = clock::now();
        double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        elapsed = std::min(elapsed, 0.1);
        cpu_accumulator += elapsed * static_cast<double>(options.hz);
        timer_accumulator += elapsed * 60.0;

        int cycle_guard = 0;
        while (cpu_accumulator >= 1.0 && cycle_guard++ < 5000 && !chip8.halted()) {
            if (!chip8.cycle()) break;
            cpu_accumulator -= 1.0;
        }
        while (timer_accumulator >= 1.0) {
            chip8.tick_timers();
            timer_accumulator -= 1.0;
        }

        if (chip8.halted() && !chip8.error().empty()) {
            std::cerr << "Emulation stopped: " << chip8.error() << '\n';
            running = false;
        }

        beeper.update(chip8.sound_active());
        render(renderer, chip8);
        SDL_Delay(1);
    }

    for (SDL_Gamepad* pad : gamepads) SDL_CloseGamepad(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

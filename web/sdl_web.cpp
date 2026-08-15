#include "irchip8/chip8.hpp"

#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#define IR8_CONT SDL_APP_CONTINUE
#define IR8_OK SDL_APP_SUCCESS
#define IR8_BAD SDL_APP_FAILURE
#define IR8_CAP(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
#define IR8_ON(t) ((t)==SDL_EVENT_KEY_DOWN || (t)==SDL_EVENT_GAMEPAD_BUTTON_DOWN)

static irchip8::Chip8 C{};
static std::vector<std::uint8_t> R;
static SDL_Window *W = nullptr;
static SDL_Renderer *D = nullptr;
static SDL_AudioStream *A = nullptr;
static SDL_Gamepad *P[8]{};
static SDL_FRect X[irchip8::Chip8::HighWidth * irchip8::Chip8::HighHeight]{};
static float S[512]{};
static int PN = 0, GO = 0, HZ = 700, FORCE = -1, DIA = 0, PROF = 0, CONF = 55, BEEP = 0, DEAD = 0;
static double CA = 0.0, TA = 0.0;
static Uint64 T0 = 0;
static float PH = 0.0f;

EM_JS(void, ui_boot, (), {
    const q = (x) => document.getElementById(x);
    const f = q('romFile'), pick = q('pick'), run = q('run'), reset = q('reset');
    const hz = q('hz'), profile = q('profile'), drop = q('drop'), full = q('full');
    if (!f || !pick || !run || !reset) return;
    const load = (file) => {
        if (!file) return;
        if (!file.name.toLowerCase().endsWith('.ch8')) { q('msg').textContent = 'Only .ch8 ROM files are accepted.'; return; }
        const rd = new FileReader();
        rd.onload = () => {
            const u = new Uint8Array(rd.result);
            const p = Module['_ir8_alloc'](u.length);
            HEAPU8.set(u, p);
            const ok = Module['_ir8_rom'](p, u.length);
            Module['_ir8_free'](p);
            q('rom').textContent = file.name + '  (' + u.length + ' bytes)';
            run.disabled = !ok; reset.disabled = !ok;
            run.textContent = 'Run';
            q('msg').textContent = ok ? 'ROM loaded. Press Run.' : 'ROM rejected by CHIP-8 memory/core checks.';
        };
        rd.readAsArrayBuffer(file);
    };
    pick.onclick = () => f.click();
    f.onchange = () => load(f.files && f.files[0]);
    run.onclick = () => { const n = Module['_ir8_run'](); run.textContent = n ? 'Pause' : 'Run'; q('msg').textContent = n ? 'Running in SDL3 / WebAssembly.' : 'Paused.'; };
    reset.onclick = () => { Module['_ir8_reset'](); run.textContent = 'Run'; q('msg').textContent = 'Reset. Press Run.'; };
    hz.oninput = () => { q('hzv').textContent = hz.value + ' Hz'; Module['_ir8_hz'](parseInt(hz.value, 10)); };
    profile.onchange = () => { Module['_ir8_profile'](parseInt(profile.value, 10)); run.textContent = 'Run'; q('msg').textContent = 'Profile changed; ROM reset.'; };
    full.onclick = () => q('canvas').requestFullscreen && q('canvas').requestFullscreen();
    ['dragenter','dragover'].forEach((e) => drop.addEventListener(e, (v) => { v.preventDefault(); drop.classList.add('hot'); }));
    ['dragleave','drop'].forEach((e) => drop.addEventListener(e, (v) => { v.preventDefault(); drop.classList.remove('hot'); }));
    drop.addEventListener('drop', (e) => load(e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]));
    q('msg').textContent = 'SDL3 runtime ready. Choose a .ch8 ROM.';
});

EM_JS(void, ui_probe, (int d, int p, int c), {
    const dn = ['CHIP-8','CHIP-48','Super-CHIP'];
    const pn = ['Modern','COSMAC VIP','CHIP-48'];
    document.getElementById('dialect').textContent = dn[d] || 'Unknown';
    document.getElementById('detected').textContent = pn[p] || 'Unknown';
    document.getElementById('confidence').textContent = c + '%';
});

EM_JS(void, ui_dead, (), {
    const m = document.getElementById('msg'), r = document.getElementById('run');
    if (m) m.textContent = 'Program halted. Reset or load another ROM.';
    if (r) r.textContent = 'Run';
});

extern "C" {
EMSCRIPTEN_KEEPALIVE void *ir8_alloc(int n) { return n > 0 ? std::malloc(static_cast<std::size_t>(n)) : nullptr; }
EMSCRIPTEN_KEEPALIVE void ir8_free(void *p) { std::free(p); }
}

static int superop(std::uint16_t o) {
    if (o == 0x00FB || o == 0x00FC || o == 0x00FD || o == 0x00FE || o == 0x00FF) return 1;
    if ((o & 0xFFF0u) == 0x00C0u) return 1;
    if ((o & 0xF00Fu) == 0xD000u) return 1;
    o &= 0xF0FFu;
    return o == 0xF030u || o == 0xF075u || o == 0xF085u;
}

static void probe(const std::uint8_t *p, int n) {
    int i = 0, sc = 0, vip = 0, c48 = 0;
L0:
    if (i + 1 >= n) goto L9;
    {
        const std::uint16_t o = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[i]) << 8u) | p[i + 1]);
        const int x = (o >> 8) & 15, y = (o >> 4) & 15, z = o & 15;
        if (superop(o)) ++sc;
        else if ((o & 0xF000u) == 0 && o != 0 && o != 0x00E0 && o != 0x00EE) vip += 3;
        if ((o & 0xF000u) == 0x8000u && (z == 6 || z == 14) && x != y) vip += 2;
        if ((o & 0xF000u) == 0xB000u && x != 0) c48 += 3;
    }
    i += 2; goto L0;
L9:
    if (sc) goto SCH;
    if (c48 >= vip + 2) goto C48;
    if (vip >= c48 + 2) goto VIP;
    DIA = 0; PROF = 0; CONF = 55; goto OUT;
SCH:
    DIA = 2; PROF = 2; CONF = IR8_CAP(88 + sc * 2, 0, 99); goto OUT;
C48:
    DIA = 1; PROF = 2; CONF = IR8_CAP(62 + c48 * 4, 0, 90); goto OUT;
VIP:
    DIA = 0; PROF = 1; CONF = IR8_CAP(60 + vip * 4, 0, 88);
OUT:
    ui_probe(DIA, PROF, CONF);
}

static irchip8::Profile pp(int x) {
    if (x == 1) return irchip8::Profile::Vip;
    if (x == 2) return irchip8::Profile::Chip48;
    return irchip8::Profile::Modern;
}

static int reload() {
    if (R.empty()) return 0;
    C.set_profile(pp(FORCE >= 0 ? FORCE : PROF));
    GO = 0; DEAD = 0; CA = TA = 0.0;
    return C.load_rom(std::span<const std::uint8_t>(R.data(), R.size())) ? 1 : 0;
}

static int audio_open() {
    if (A) return 1;
    const SDL_AudioSpec sp{SDL_AUDIO_F32, 1, 48000};
    A = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sp, nullptr, nullptr);
    if (!A) return 0;
    if (!SDL_ResumeAudioStreamDevice(A)) { SDL_DestroyAudioStream(A); A = nullptr; return 0; }
    return 1;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int ir8_rom(const std::uint8_t *p, int n) {
    if (!p || n <= 0 || n > static_cast<int>(irchip8::Chip8::MemorySize - irchip8::Chip8::ProgramStart)) return 0;
    R.assign(p, p + n); probe(p, n); return reload();
}
EMSCRIPTEN_KEEPALIVE int ir8_run() { if (R.empty()) return 0; GO = !GO; if (GO) audio_open(); return GO; }
EMSCRIPTEN_KEEPALIVE int ir8_reset() { return reload(); }
EMSCRIPTEN_KEEPALIVE int ir8_hz(int n) { HZ = IR8_CAP(n, 60, 5000); return HZ; }
EMSCRIPTEN_KEEPALIVE int ir8_profile(int n) { FORCE = (n < -1 || n > 2) ? -1 : n; return reload(); }
EMSCRIPTEN_KEEPALIVE int ir8_is_running() { return GO; }
}

static int key(SDL_Keycode k) {
    switch (k) {
        case SDLK_1:return 1; case SDLK_2:return 2; case SDLK_3:return 3; case SDLK_4:return 12;
        case SDLK_Q:return 4; case SDLK_W:return 5; case SDLK_E:return 6; case SDLK_R:return 13;
        case SDLK_A:return 7; case SDLK_S:return 8; case SDLK_D:return 9; case SDLK_F:return 14;
        case SDLK_Z:return 10; case SDLK_X:return 0; case SDLK_C:return 11; case SDLK_V:return 15;
        default:return -1;
    }
}

static int gkey(int b) {
    switch (b) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:return 2; case SDL_GAMEPAD_BUTTON_DPAD_DOWN:return 8;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:return 4; case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:return 6;
        case SDL_GAMEPAD_BUTTON_SOUTH:return 5; case SDL_GAMEPAD_BUTTON_EAST:return 0;
        case SDL_GAMEPAD_BUTTON_WEST:return 7; case SDL_GAMEPAD_BUTTON_NORTH:return 9;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return 1; case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return 3;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:return 10; case SDL_GAMEPAD_BUTTON_RIGHT_STICK:return 11;
        case SDL_GAMEPAD_BUTTON_BACK:return 12; case SDL_GAMEPAD_BUTTON_START:return 13;
        case SDL_GAMEPAD_BUTTON_GUIDE:return 14; case SDL_GAMEPAD_BUTTON_MISC1:return 15;
        default:return -1;
    }
}

static void padd(SDL_JoystickID id) {
    if (PN >= 8) return;
    SDL_Gamepad *q = SDL_OpenGamepad(id);
    if (q) P[PN++] = q;
}

static void pdel(SDL_JoystickID id) {
    for (int i = 0; i < PN; ++i) if (P[i] && SDL_GetGamepadID(P[i]) == id) {
        SDL_CloseGamepad(P[i]);
        for (int j = i; j + 1 < PN; ++j) P[j] = P[j + 1];
        P[--PN] = nullptr; return;
    }
}

static void draw() {
    if (!D) return;
    int ow = 0, oh = 0, n = 0;
    SDL_GetRenderOutputSize(D, &ow, &oh);
    SDL_SetRenderDrawColor(D, 5, 7, 10, 255); SDL_RenderClear(D);
    const int w = C.display_width(), h = C.display_height();
    float sx = static_cast<float>(ow) / static_cast<float>(w), sy = static_cast<float>(oh) / static_cast<float>(h);
    float sc = std::max(1.0f, std::min(sx, sy));
    float ox = (ow - w * sc) * 0.5f, oy = (oh - h * sc) * 0.5f;
    auto fb = C.framebuffer();
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        if (fb[static_cast<std::size_t>(y * irchip8::Chip8::HighWidth + x)]) X[n++] = SDL_FRect{ox + x * sc, oy + y * sc, sc, sc};
    SDL_SetRenderDrawColor(D, 238, 244, 255, 255);
    if (n) SDL_RenderFillRects(D, X, n);
    SDL_RenderPresent(D);
}

static void buzz() {
    const int on = GO && C.sound_active();
    if (on && !A && !audio_open()) return;
    if (!A) return;
    if (!on) { if (BEEP) SDL_ClearAudioStream(A); BEEP = 0; return; }
    BEEP = 1;
    if (SDL_GetAudioStreamQueued(A) > 9600) return;
    const float step = 440.0f / 48000.0f;
    for (float &v : S) { v = PH < 0.5f ? 0.09f : -0.09f; PH += step; if (PH >= 1.0f) PH -= 1.0f; }
    SDL_PutAudioStreamData(A, S, static_cast<int>(sizeof(S)));
}

SDL_AppResult SDL_AppInit(void **appstate, int, char **) {
    *appstate = nullptr;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return IR8_BAD;
    if (!SDL_CreateWindowAndRenderer("IrChip8 SDL3 / WebAssembly", 1024, 512, SDL_WINDOW_RESIZABLE, &W, &D)) return IR8_BAD;
    SDL_SetRenderVSync(D, 1);
    int n = 0; SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids) { for (int i = 0; i < n && i < 8; ++i) padd(ids[i]); SDL_free(ids); }
    T0 = SDL_GetTicksNS(); ui_boot(); draw();
    return IR8_CONT;
}

SDL_AppResult SDL_AppEvent(void *, SDL_Event *e) {
    const Uint32 t = e->type;
    if (t == SDL_EVENT_QUIT || t == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return IR8_OK;
    if (t == SDL_EVENT_KEY_DOWN || t == SDL_EVENT_KEY_UP) {
        const int k = key(e->key.key); if (k >= 0) C.set_key(static_cast<std::uint8_t>(k), t == SDL_EVENT_KEY_DOWN);
        if (t == SDL_EVENT_KEY_DOWN && !e->key.repeat && e->key.key == SDLK_SPACE && !R.empty()) { GO = !GO; if (GO) audio_open(); }
        return IR8_CONT;
    }
    if (t == SDL_EVENT_GAMEPAD_ADDED) { padd(e->gdevice.which); return IR8_CONT; }
    if (t == SDL_EVENT_GAMEPAD_REMOVED) { pdel(e->gdevice.which); return IR8_CONT; }
    if (t == SDL_EVENT_GAMEPAD_BUTTON_DOWN || t == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        const int k = gkey(e->gbutton.button); if (k >= 0) C.set_key(static_cast<std::uint8_t>(k), IR8_ON(t));
    }
    return IR8_CONT;
}

SDL_AppResult SDL_AppIterate(void *) {
    const Uint64 t = SDL_GetTicksNS();
    double dt = static_cast<double>(t - T0) / 1000000000.0; T0 = t; if (dt > 0.1) dt = 0.1;
    if (GO && !C.halted()) {
        CA += dt * HZ; TA += dt * 60.0;
        int guard = 0;
        while (CA >= 1.0 && guard++ < 5000 && !C.halted()) { if (!C.cycle()) break; CA -= 1.0; }
        while (TA >= 1.0) { C.tick_timers(); TA -= 1.0; }
    }
    if (C.halted() && !DEAD) { DEAD = 1; GO = 0; ui_dead(); }
    buzz(); draw(); return IR8_CONT;
}

void SDL_AppQuit(void *, SDL_AppResult) {
    for (int i = 0; i < PN; ++i) if (P[i]) SDL_CloseGamepad(P[i]);
    if (A) SDL_DestroyAudioStream(A);
    if (D) SDL_DestroyRenderer(D);
    if (W) SDL_DestroyWindow(W);
}

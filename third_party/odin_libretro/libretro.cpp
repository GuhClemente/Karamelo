/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗    ██╗     ██╗██████╗ ██████╗ ███████╗████████╗██████╗  ██████╗ 
 * ██╔═══██╗██╔══██╗██║████╗  ██║    ██║     ██║██╔══██╗██╔══██╗██╔════╝╚══██╔══╝██╔══██╗██╔═══██╗
 * ██║   ██║██║  ██║██║██╔██╗ ██║    ██║     ██║██████╔╝██████╔╝█████╗     ██║   ██████╔╝██║   ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║    ██║     ██║██╔══██╗██╔══██╗██╔══╝     ██║   ██╔══██╗██║   ██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║    ███████╗██║██████╔╝██║  ██║███████╗   ██║   ██║  ██║╚██████╔╝
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝    ╚══════╝╚═╝╚═════╝ ╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝ 
 *
 * Libretro Core Wrapper for Sega Hikaru & Gaelco 3D (cores/hikaru.dll)
 * Based on Odin by Réda Chérif-Touil (Tovarichtch) & Stefano Teso (Valkyrie)
 *
 * Upstream: https://github.com/Tovarichtch/Odin
 * License: GNU General Public License v3.0 (GPL-3.0)
 * Integrated for: Karamelo Emulador (https://karamelo-emu.com)
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libretro.h"

#ifdef _WIN32
#define RETRO_API extern "C" __declspec(dllexport)
#else
#define RETRO_API extern "C" __attribute__((visibility("default")))
#endif

#include "odin/host/gfx/gl_common.hpp"
#include "odin/host/renderer/hikaru/drawing.hpp"
#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/host/app/games.hpp"
#include "odin/host/app/rom_files.hpp"
#include "odin/host/app/user_files.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Libretro Callbacks
// ---------------------------------------------------------------------------
static retro_environment_t s_environ_cb = nullptr;
static retro_video_refresh_t s_video_cb = nullptr;
static retro_audio_sample_t s_audio_cb = nullptr;
static retro_audio_sample_batch_t s_audio_batch_cb = nullptr;
static retro_input_poll_t s_input_poll_cb = nullptr;
static retro_input_state_t s_input_state_cb = nullptr;
static struct retro_hw_render_callback s_hw_render{};

// ---------------------------------------------------------------------------
// Core State
// ---------------------------------------------------------------------------
static std::unique_ptr<odin::Renderer> s_renderer = nullptr;
static std::unique_ptr<odin::Hikaru> s_board = nullptr;
static std::optional<odin::Game> s_game = std::nullopt;
static odin::GameList s_games;
static bool s_game_loaded = false;
static bool s_gl_initialized = false;
static unsigned s_base_width = 496;   // Sega Hikaru native raster width
static unsigned s_base_height = 384;  // Sega Hikaru native raster height
static double s_fps = 60.0;
static double s_sample_rate = 44100.0;

// Forward declaration
static void init_renderer();
RETRO_API void retro_unload_game(void);

// ---------------------------------------------------------------------------
// LibretroPanel: Bridges Libretro controller input to Sega JVS / MIE inputs
// ---------------------------------------------------------------------------
class LibretroPanel final : public odin::HikaruPanel {
public:
    void read_inputs(odin::CabinetInputs &inputs) override {
        std::memset(&inputs, 0, sizeof(inputs));

        if (!s_input_state_cb)
            return;

        // Player 1 Digital Controls
        uint16_t p1 = 0;
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
            p1 |= (1 << 15); // START
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))
            inputs.jvs_coin[0] = 1; // COIN
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
            p1 |= (1 << 13); // UP
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
            p1 |= (1 << 12); // DOWN
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
            p1 |= (1 << 11); // LEFT
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
            p1 |= (1 << 10); // RIGHT
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
            p1 |= (1 << 9);  // PUSH 1
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
            p1 |= (1 << 8);  // PUSH 2
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
            p1 |= (1 << 7);  // PUSH 3
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
            p1 |= (1 << 6);  // PUSH 4
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))
            p1 |= (1 << 5);  // PUSH 5
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
            p1 |= (1 << 4);  // PUSH 6
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3)) {
            inputs.mainboard_test = 1;
            inputs.jvs_system |= (1 << 7); // TEST
        }
        if (s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3)) {
            inputs.mainboard_service = 1;
            p1 |= (1 << 14); // SERVICE
        }

        inputs.jvs_player[0] = p1;

        // Player 2 Digital Controls
        uint16_t p2 = 0;
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
            p2 |= (1 << 15);
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))
            inputs.jvs_coin[1] = 1;
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
            p2 |= (1 << 13);
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
            p2 |= (1 << 12);
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
            p2 |= (1 << 11);
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
            p2 |= (1 << 10);
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
            p2 |= (1 << 9);
        if (s_input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
            p2 |= (1 << 8);

        inputs.jvs_player[1] = p2;

        // Analog Controls (Steering Wheel / Pitch / Throttle / Brake / Twin Sticks)
        int16_t analog_x = s_input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        int16_t analog_y = s_input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        int16_t analog_rx = s_input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        int16_t analog_ry = s_input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
        int16_t analog_r2 = s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
        int16_t analog_l2 = s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);

        inputs.jvs_analog[0] = static_cast<uint16_t>(analog_x + 32768);
        inputs.jvs_analog[1] = static_cast<uint16_t>(analog_y + 32768);
        inputs.jvs_analog[2] = analog_r2 ? 0xFFFF : 0x0000;
        inputs.jvs_analog[3] = analog_l2 ? 0xFFFF : 0x0000;
        inputs.jvs_analog[4] = static_cast<uint16_t>(analog_rx + 32768);
        inputs.jvs_analog[5] = static_cast<uint16_t>(analog_ry + 32768);
    }
};

static LibretroPanel s_panel;

// ---------------------------------------------------------------------------
// LibretroScreen: Receives draw lists and texture notifications from Hikaru GPU
// ---------------------------------------------------------------------------
class LibretroScreen final : public odin::HikaruScreen {
public:
    void list_launched() override {
        if (s_renderer) {
            s_renderer->launch();
        }
    }

    void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) override {
        if (s_renderer) {
            s_renderer->invalidate_textures(bank, x, y, w, h);
        }
    }

    void before_memory_changes() override {}

    void memory_changed() override {
        if (s_renderer) {
            s_renderer->invalidate_textures(-1);
        }
    }

    bool save_pictures(odin::StateFile &state) override {
        (void)state;
        return true;
    }

    bool load_pictures(odin::StateFile &state) override {
        (void)state;
        return true;
    }
};

static LibretroScreen s_screen;

// ---------------------------------------------------------------------------
// Hardware Render Context Callbacks
// ---------------------------------------------------------------------------
static void context_reset() {
    std::printf("[HIKARU] OpenGL Context Reset (initializing GLEW and FBO pipeline)\n");
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
        std::fprintf(stderr, "[HIKARU] glewInit error: %s\n", glewGetErrorString(err));
    }
    s_gl_initialized = true;

    if (s_board && !s_renderer) {
        init_renderer();
    }
}

static void context_destroy() {
    std::printf("[HIKARU] OpenGL Context Destroyed\n");
    s_renderer.reset();
    s_gl_initialized = false;
}

static void init_renderer() {
    if (!s_board || !s_gl_initialized)
        return;

    odin::Renderer::Memory memory;
    memory.fb = s_board->framebuffer_ram();
    memory.fb_writes = &s_board->framebuffer_writes();
    memory.texram[0] = s_board->texture_ram(0);
    memory.texram[1] = s_board->texture_ram(1);

    odin::Renderer::Options options{};
    options.programs_made = "cache/hikaru";

    auto drawing = odin::Drawing::make_gl();
    s_renderer = std::make_unique<odin::Renderer>(std::move(drawing), memory, options);
    s_renderer->set_gpu(&s_board->gpu());
    std::printf("[HIKARU] Stage 2: Hardware OpenGL Renderer connected to Hikaru GPU!\n");
}

// ---------------------------------------------------------------------------
// Libretro Core API Implementation
// ---------------------------------------------------------------------------

RETRO_API void retro_set_environment(retro_environment_t cb) {
    s_environ_cb = cb;

    struct retro_variable variables[] = {
        { "hikaru_resolution", "Internal Resolution; 496x384|640x480|992x768|1280x960" },
        { "hikaru_analog_deadzone", "Analog Deadzone; 5%|10%|15%|20%" },
        { nullptr, nullptr }
    };
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { s_video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { s_audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { s_input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { s_input_state_cb = cb; }

RETRO_API void retro_init(void) {
    s_hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
    s_hw_render.context_reset = context_reset;
    s_hw_render.context_destroy = context_destroy;
    s_hw_render.version_major = 4;
    s_hw_render.version_minor = 3;
    s_hw_render.depth = true;
    s_hw_render.stencil = true;
    s_hw_render.bottom_left_origin = true;

    if (!s_environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &s_hw_render)) {
        std::fprintf(stderr, "[HIKARU] Frontend does not support OpenGL 4.3 Core HW Render!\n");
    }
}

RETRO_API void retro_deinit(void) {
    retro_unload_game();
    s_gl_initialized = false;
}

RETRO_API unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->library_name     = "Odin Hikaru";
    info->library_version  = "2.0 (Odin/Karamelo)";
    info->valid_extensions = "zip|7z|bin";
    info->need_fullpath    = true;
    info->block_extract    = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->geometry.base_width   = s_base_width;
    info->geometry.base_height  = s_base_height;
    info->geometry.max_width    = 1920;
    info->geometry.max_height   = 1440;
    info->geometry.aspect_ratio = 4.0f / 3.0f;

    info->timing.fps         = s_fps;
    info->timing.sample_rate = s_sample_rate;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port; (void)device;
}

RETRO_API void retro_reset(void) {
    if (s_board)
        s_board->reset();
}

RETRO_API void retro_run(void) {
    if (!s_board)
        return;

    // 1. Poll Controller Inputs
    if (s_input_poll_cb)
        s_input_poll_cb();

    // 2. Query target FBO from Libretro callback
    GLuint target_fbo = 0;
    if (s_hw_render.get_current_framebuffer) {
        target_fbo = static_cast<GLuint>(s_hw_render.get_current_framebuffer());
    }

    // 3. Step frame and render into target FBO
    if (s_renderer) {
        s_renderer->begin_frame();
        s_renderer->card().set_target_fbo(target_fbo);
    }

    s_board->run_frame();

    if (s_renderer) {
        s_renderer->take_frame(s_board->frame(), s_base_width, s_base_height, {});
        s_renderer->set_scale(1);
        s_renderer->draw_frame();
        s_renderer->frame_shown();
    }

    // 4. Signal frame was rendered directly into bound FBO
    if (s_video_cb) {
        s_video_cb(RETRO_HW_FRAME_BUFFER_VALID, s_base_width, s_base_height, 0);
    }

    // 5. Send Yamaha AICA 44.1 kHz 16-bit Stereo PCM Audio
    if (s_audio_batch_cb) {
        const auto &sound_samples = s_board->sound(false);
        if (!sound_samples.empty()) {
            s_audio_batch_cb(sound_samples.data(), sound_samples.size() / 2);
        }
    }
}

RETRO_API size_t retro_serialize_size(void) {
    return 32 * 1024 * 1024;
}

RETRO_API bool retro_serialize(void *data, size_t size) {
    (void)data; (void)size;
    return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size) {
    (void)data; (void)size;
    return false;
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index; (void)enabled; (void)code;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game_info) {
    if (!game_info || !game_info->path)
        return false;

    std::string game_path = game_info->path;
    std::string game_stem = fs::path(game_path).stem().string();
    std::transform(game_stem.begin(), game_stem.end(), game_stem.begin(), ::tolower);
    std::string rom_dir = fs::path(game_path).parent_path().string();

    std::printf("[HIKARU] Loading Arcade ROM: %s (stem: %s)\n", game_path.c_str(), game_stem.c_str());

    const char *sys_dir = nullptr;
    if (!s_environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_dir) || !sys_dir) {
        sys_dir = "app/bios";
    }

    std::vector<std::string> json_candidates = {
        "third_party/odin/build/share/odin/odin-games.json",
        "third_party/odin/odin-games.json",
        "app/odin-games.json",
        "odin-games.json",
        "cores/odin-games.json"
    };

    bool json_loaded = false;
    for (const auto &p : json_candidates) {
        if (fs::exists(p) && s_games.load(p)) {
            json_loaded = true;
            std::printf("[HIKARU] Loaded game catalogue from: %s\n", p.c_str());
            break;
        }
    }

    if (!json_loaded) {
        std::fprintf(stderr, "[HIKARU] Failed to locate and load odin-games.json!\n");
        return false;
    }

    odin::RomFiles files(rom_dir, sys_dir);
    s_game = s_games.load_game(files, game_stem);
    if (!s_game) {
        std::fprintf(stderr, "[HIKARU] Failed to resolve and load ROM files for game '%s'!\n", game_stem.c_str());
        return false;
    }

    odin::NetLinkSettings link_settings{};
    s_board = odin::Hikaru::make(*s_game, false, link_settings);
    if (!s_board) {
        std::fprintf(stderr, "[HIKARU] Failed to instantiate Hikaru board for '%s'!\n", game_stem.c_str());
        return false;
    }

    s_board->set_panel(&s_panel);
    s_board->set_screen(&s_screen);

    // Initialise saves folder and cabinet settings (resolves ERROR 2 region/market check)
    const char *save_dir = nullptr;
    if (!s_environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) || !save_dir) {
        save_dir = "saves/Hikaru";
    }
    std::string game_save_folder = std::string(save_dir) + "/" + game_stem;
    std::error_code ec;
    fs::create_directories(game_save_folder, ec);
    odin::SaveFolder saves(game_save_folder);
    s_board->keep_in(saves);

    s_board->reset();

    if (s_gl_initialized && !s_renderer) {
        init_renderer();
    }

    s_game_loaded = true;
    std::printf("[HIKARU] Game '%s' successfully initialized into Hikaru hardware core!\n", game_stem.c_str());
    return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) {
    (void)game_type; (void)info; (void)num_info;
    return false;
}

RETRO_API void retro_unload_game(void) {
    if (s_board) {
        s_board->power_off();
    }
    s_renderer.reset();
    s_board.reset();
    s_game.reset();
    s_game_loaded = false;
    std::printf("[HIKARU] Game unloaded\n");
}

RETRO_API unsigned retro_get_region(void) {
    return 0; // 0 = NTSC
}

RETRO_API void *retro_get_memory_data(unsigned id) {
    (void)id;
    return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}

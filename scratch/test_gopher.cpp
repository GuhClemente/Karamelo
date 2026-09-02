#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef void (*retro_init_t)();
typedef void (*retro_deinit_t)();
typedef bool (*retro_load_game_t)(const void* game);
typedef void (*retro_run_t)();
typedef void (*retro_set_video_refresh_t)(void (*cb)(const void* data, unsigned width, unsigned height, size_t pitch));
typedef void (*retro_set_audio_sample_batch_t)(size_t (*cb)(const int16_t* data, size_t frames));
typedef void (*retro_set_input_poll_t)(void (*cb)());
typedef void (*retro_set_input_state_t)(int16_t (*cb)(unsigned port, unsigned device, unsigned index, unsigned id));
typedef bool (*retro_set_environment_t)(bool (*cb)(unsigned cmd, void* data));

struct retro_game_info {
    const char* path;
    const void* data;
    size_t size;
    const char* meta;
};

static int g_video_frames = 0;
static int g_audio_frames = 0;
static uint32_t g_last_frame[640 * 480];
static unsigned g_last_w = 0;
static unsigned g_last_h = 0;

static bool env_cb(unsigned cmd, void* data) {
    if (cmd == 10) return true;
    return false;
}

static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
    g_video_frames++;
    g_last_w = width;
    g_last_h = height;
    
    // Check nonblack pixels
    const uint32_t* src = (const uint32_t*)data;
    uint32_t nonblack = 0;
    for (unsigned y = 0; y < height && y < 480; y++) {
        for (unsigned x = 0; x < width && x < 640; x++) {
            uint32_t p = src[y * (pitch / 4) + x];
            g_last_frame[y * 640 + x] = p;
            if ((p & 0x00FFFFFF) != 0) nonblack++;
        }
    }

    if (g_video_frames % 60 == 0 || nonblack > 0) {
        printf("[Harness] Frame %d: %ux%u nonblack pixels = %u / %u\n", g_video_frames, width, height, nonblack, width * height);
    }
}

static size_t audio_cb(const int16_t* data, size_t frames) {
    g_audio_frames += (int)frames;
    return frames;
}

static void input_poll_cb() {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) { return 0; }

int main() {
    HMODULE mod = LoadLibraryA("cores/n64_gopher.dll");
    if (!mod) {
        printf("[Harness] Failed to load DLL\n");
        return 1;
    }

    retro_init_t r_init = (retro_init_t)GetProcAddress(mod, "retro_init");
    retro_set_environment_t r_set_env = (retro_set_environment_t)GetProcAddress(mod, "retro_set_environment");
    retro_set_video_refresh_t r_set_video = (retro_set_video_refresh_t)GetProcAddress(mod, "retro_set_video_refresh");
    retro_set_audio_sample_batch_t r_set_audio = (retro_set_audio_sample_batch_t)GetProcAddress(mod, "retro_set_audio_sample_batch");
    retro_set_input_poll_t r_set_poll = (retro_set_input_poll_t)GetProcAddress(mod, "retro_set_input_poll");
    retro_set_input_state_t r_set_state = (retro_set_input_state_t)GetProcAddress(mod, "retro_set_input_state");
    retro_load_game_t r_load_game = (retro_load_game_t)GetProcAddress(mod, "retro_load_game");
    retro_run_t r_run = (retro_run_t)GetProcAddress(mod, "retro_run");

    r_set_env(env_cb);
    r_set_video(video_cb);
    r_set_audio(audio_cb);
    r_set_poll(input_poll_cb);
    r_set_state(input_state_cb);

    r_init();

    const char* rom_path = "app/roms/Nintendo64/Dr. Mario 64 (USA).n64";
    FILE* f = fopen(rom_path, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* rom_data = malloc(sz);
    fread(rom_data, 1, sz, f);
    fclose(f);

    struct retro_game_info gi = { rom_path, rom_data, (size_t)sz, NULL };
    r_load_game(&gi);

    printf("[Harness] Running 240 frames of Dr. Mario 64...\n");
    for (int frame = 0; frame < 240; frame++) {
        r_run();
    }
    printf("[Harness] Finished 240 frames. Total video frames: %d, audio samples: %d\n", g_video_frames, g_audio_frames);

    // Save final frame as PPM
    if (g_last_w > 0 && g_last_h > 0) {
        FILE* out = fopen("scratch/frame_output.ppm", "wb");
        if (out) {
            fprintf(out, "P6\n%u %u\n255\n", g_last_w, g_last_h);
            for (unsigned y = 0; y < g_last_h; y++) {
                for (unsigned x = 0; x < g_last_w; x++) {
                    uint32_t p = g_last_frame[y * 640 + x];
                    uint8_t r = (p >> 16) & 0xFF;
                    uint8_t g = (p >> 8) & 0xFF;
                    uint8_t b = p & 0xFF;
                    fputc(r, out);
                    fputc(g, out);
                    fputc(b, out);
                }
            }
            fclose(out);
            printf("[Harness] Saved snapshot to scratch/frame_output.ppm\n");
        }
    }

    return 0;
}

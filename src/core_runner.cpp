#define WIN32_LEAN_AND_MEAN
#include <thread>
#include <vector>
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <xinput.h>
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <fstream>

// LoadLibraryA/GetProcAddress/FreeLibrary have no direct Windows-only
// dependency other than the calling convention and the handle type - dlopen/
// dlsym/dlclose take the same (path, RTLD_NOW)/(handle, name)/(handle)
// shapes, so every existing call site below can stay exactly as written.
// windows.h above still makes the rest of this file Windows-only for now
// (WASAPI, XInput, __try/__except); this shim just means the core-loading
// code specifically won't need touching again once those other blockers
// are addressed.
#ifndef _WIN32
#include <dlfcn.h>
typedef void* HMODULE;
#define LoadLibraryA(path) dlopen(path, RTLD_NOW)
#define GetProcAddress(h, name) dlsym(h, name)
#define FreeLibrary(h) dlclose(h)
#endif

#include "libretro.h"
#include "core_runner.h"
#include "menu.h"
#include "input_map.h"
#include "gamepad_sdl.h"
#include "archive_helper.h"
#include "retroachievements.h"
#include "karamelo_math.h"
#include "osd.h"
#include "netplay.h"
#include "hw_render.h"
#include "hw_render_vulkan.h"
#include "hw_render_d3d11.h"

namespace fs = std::filesystem;

#pragma comment(lib, "winmm.lib")

// Libretro Core Function Pointers
typedef void (*retro_init_t)(void);
typedef void (*retro_deinit_t)(void);
typedef unsigned (*retro_api_version_t)(void);
typedef void (*retro_get_system_info_t)(struct retro_system_info* info);
typedef void (*retro_get_system_av_info_t)(struct retro_system_av_info* info);
typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_set_controller_port_device_t)(unsigned port, unsigned device);
typedef void (*retro_reset_t)(void);
typedef void (*retro_run_t)(void);
typedef size_t (*retro_serialize_size_t)(void);
typedef bool (*retro_serialize_t)(void* data, size_t size);
typedef bool (*retro_unserialize_t)(const void* data, size_t size);
typedef bool (*retro_load_game_t)(const struct retro_game_info* game);
typedef void (*retro_unload_game_t)(void);
typedef unsigned (*retro_get_region_t)(void);
typedef void* (*retro_get_memory_data_t)(unsigned id);
typedef size_t (*retro_get_memory_size_t)(unsigned id);

enum retro_log_level
{
	RETRO_LOG_DEBUG = 0,
	RETRO_LOG_INFO,
	RETRO_LOG_WARN,
	RETRO_LOG_ERROR
};

typedef void (*retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);

struct retro_log_callback
{
	retro_log_printf_t log;
};

static HMODULE h_core_dll = NULL;
static retro_init_t p_retro_init = NULL;
static retro_deinit_t p_retro_deinit = NULL;
static retro_get_system_info_t p_retro_get_system_info = NULL;
static retro_get_system_av_info_t p_retro_get_system_av_info = NULL;
static retro_set_environment_t p_retro_set_environment = NULL;
static retro_set_video_refresh_t p_retro_set_video_refresh = NULL;
static retro_set_audio_sample_t p_retro_set_audio_sample = NULL;
static retro_set_audio_sample_batch_t p_retro_set_audio_sample_batch = NULL;
static retro_set_input_poll_t p_retro_set_input_poll = NULL;
static retro_set_input_state_t p_retro_set_input_state = NULL;
static retro_set_controller_port_device_t p_retro_set_controller_port_device = NULL;
static retro_reset_t p_retro_reset = NULL;
static retro_run_t p_retro_run = NULL;
static retro_serialize_size_t p_retro_serialize_size = NULL;
static retro_serialize_t p_retro_serialize = NULL;
static retro_unserialize_t p_retro_unserialize = NULL;
static retro_load_game_t p_retro_load_game = NULL;
static retro_unload_game_t p_retro_unload_game = NULL;
static retro_get_memory_data_t p_retro_get_memory_data = NULL;
static retro_get_memory_size_t p_retro_get_memory_size = NULL;

static bool is_core_loaded = false;
static bool is_game_loaded = false;
static enum retro_pixel_format core_pixel_format = RETRO_PIXEL_FORMAT_RGB565;

// Triple-Buffered Lock-Free Framebuffer System
#define MAX_FB_WIDTH  1024
#define MAX_FB_HEIGHT 1024
// Lock-free triple buffer.
//
// The previous version rotated indices in a way that only ever used two of the
// three buffers, and nothing stopped the core thread from writing into the very
// buffer the UI thread was scaling. A slow render or a fast core produced a
// torn frame: the top of the picture from one frame, the bottom from the next.
//
// Here the three indices are always a permutation of {0,1,2}. The producer owns
// `back`, the consumer owns `front`, and `ready` is the hand-off - each side
// only ever swaps its own index with the atomic slot, so the buffer being read
// can never be the buffer being written.
static uint32_t g_triple_fb[3][MAX_FB_WIDTH * MAX_FB_HEIGHT];
static unsigned g_fb_dim_w[3] = { 320, 320, 320 };
static unsigned g_fb_dim_h[3] = { 240, 240, 240 };

static volatile LONG g_fb_ready = 1;   // hand-off slot
static volatile LONG g_fb_dirty = 0;   // a new frame is waiting
static int g_fb_back = 0;              // producer thread only
static int g_fb_front = 2;             // consumer thread only

// 480i / 448i Motion-Adaptive Deinterlace for PS2 (Play! core)
// PS2 GS alternates odd/even fields. Blending alternating scanlines with previous
// field data forms a solid 448p progressive frame with zero screen shake and solid crisp text.
static uint32_t g_deinterlace_prev[MAX_FB_WIDTH * MAX_FB_HEIGHT];
static bool     g_deinterlace_have_prev = false;
static unsigned g_deinterlace_field = 0; // 0=even, 1=odd



static unsigned core_fb_width = 320;
static unsigned core_fb_height = 240;
static double   core_target_fps = 60.0;

// The rate the core actually wants, kept apart from the rate we run it at.
// Slaving the core to the display removes the judder that comes from a core
// at 59.19fps being presented on a 59.94Hz panel; the cost is a pitch shift
// of the same ratio, which the resampler absorbs by rescaling the input rate.
static double   core_native_fps = 60.0;
static double   core_native_sample_rate = 48000.0;
static volatile LONG g_sync_to_display = 0;
static double   g_display_fps = 60.0;

// Game & System Metadata
static std::string loaded_game_stem = "";
// These two are written on the core thread during a load and read on the UI
// thread every frame. Handing out .c_str() of a std::string being reassigned
// elsewhere is a dangling pointer the moment the string reallocates, so both
// sides go through this lock.
static CRITICAL_SECTION name_lock;

// loaded_game_stem shares that exact hazard (CoreTakeScreenshot reads it from
// the UI thread while CoreLoadGame writes it on the core thread) but has no
// public getter of its own - it is only ever used inside this file. Take a
// safe copy under the lock instead of reading the string directly.
static std::string GetLoadedGameStemSafe()
{
	EnterCriticalSection(&name_lock);
	std::string copy = loaded_game_stem;
	LeaveCriticalSection(&name_lock);
	return copy;
}
struct NameLockInit { NameLockInit() { InitializeCriticalSection(&name_lock); } };
static NameLockInit g_name_lock_init;

static std::string loaded_game_name = "";
static std::string loaded_system_dir = "";

// Whether the gamepad-to-keyboard bridge down in CB_InputState applies to the
// content currently loaded. Read on the core thread every input poll, so it is
// a plain flag rather than a string comparison, and it is decided once when the
// game loads.
static volatile bool keyboard_bridge_active = false;

// Which arcade core actually ran a given romset, keyed by the original ROM
// path. Only ever touched from the core thread, during load. See the comment
// where it is consulted for why forgetting this was expensive.
static std::map<std::string, std::string> s_arcade_core_for_rom;

// Cores que exigem negociação de contexto Vulkan, que este frontend não
// implementa. Casados por trecho do nome do arquivo da DLL.
//
// Existe porque a flag por carga chega tarde demais: o PPSSPP pede
// SET_HW_RENDER (e escolhe Vulkan) **antes** de pedir a interface de
// negociação. Quando descobrimos, ele já se comprometeu com Vulkan e já
// começou a subir as threads dele. Recusar naquele ponto não desfaz nada -
// medido: o core segue inicializando (`Entering __IoManagerThread`,
// `PPGe drawing library initialized`) enquanto o frontend desmonta tudo por
// baixo, e morre com 0xC0000005.
//
// Antes existia aqui uma lista de cores conhecidos por precisarem de
// negociacao de contexto Vulkan, usada para recusar Vulkan a eles antes mesmo
// de tentarem. Com a negociacao implementada em hw_render_vulkan.cpp, nao ha
// mais o que recusar: o core pede, negocia e recebe o dispositivo que
// descreveu.


// The bridge exists for machines whose games are driven by the keyboard and
// have no joystick mapping worth speaking of - without it, a player holding
// only a gamepad cannot type LOAD"" and start a C64 tape. It must NOT be on for
// anything else: a core that polls both RETRO_DEVICE_JOYPAD and
// RETRO_DEVICE_KEYBOARD (MAME does) then sees one physical button arrive twice,
// once as the button and once as a keystroke, and performs both actions. That
// is the punch-and-jump-at-once bug reported on Altered Beast.
static bool SystemNeedsKeyboardBridge(const std::string& system_dir)
{
	std::string s = system_dir;
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s == "msx" || s == "c64" || s == "zxspectrum" || s == "dos" || s == "amiga";
}

// Same family of bug as the keyboard bridge above, different pair of
// controls: on a real N64/PSX/PS2/Saturn/Dreamcast/GameCube/PSP/3DS pad the
// D-Pad and the analog stick are independent controls that many games bind
// to different actions (PSP: D-Pad often cycles weapons/items while the
// stick walks; N64: D-Pad is a separate item/camera control from the stick).
// Below, both the physical-gamepad stick and the default keyboard layout
// deliberately also drive the D-Pad so a stick-only 3D game stays playable
// from a keyboard or a pad with a mushy D-Pad. Left unscoped, that mirroring
// fires the D-Pad's OWN action every single step - confirmed against PSP:
// walking with the analog stick also punched out whatever the game's D-Pad
// is bound to, i.e. one push producing two unrelated actions. Systems whose
// only real control IS the D-Pad (NES, SNES, Genesis, arcade, ...) still get
// the full mirroring - there is nothing for it to collide with there.
static bool SystemHasIndependentAnalogStick(const std::string& core_dll_path)
{
	std::string s = core_dll_path;
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s.find("n64") != std::string::npos ||
	       s.find("psx") != std::string::npos ||
	       s.find("ps2") != std::string::npos ||
	       s.find("saturn") != std::string::npos ||
	       s.find("dreamcast") != std::string::npos ||
	       s.find("gamecube") != std::string::npos ||
	       s.find("psp") != std::string::npos ||
	       s.find("3ds") != std::string::npos;
}
// The folder the loaded ROM came from, so the menu can reopen that same list.
static std::string loaded_rom_dir = "";
static std::string loaded_core_name = "";

// Whether the loaded game came from a disc image. The Mega CD options only
// mean anything for a CD game, and Genesis carts share the same core.
static bool is_disc = false;
static int selected_state_slot = 0;

// Audio and Controls State
static int  master_volume = 100;
static bool audio_muted = false;
static int16_t joypad_buttons[4][16] = { 0 };
static int16_t analog_sticks[4][2][2] = { 0 };

// Labels the core supplies through SET_INPUT_DESCRIPTORS, so the Controller
// page can name a control the way that system does - "Cross" on PSP instead of
// a generic "Botao B". Port 0 only: that is what the menu edits. Written on
// the core thread during load and read by the menu afterwards, never at once.
static char g_desc_button[16][24] = { { 0 } };
static char g_desc_axis[2][2][24] = { { { 0 } } };
static bool g_desc_present = false;
static void ClearInputDescriptors();

// Toast Notification State. Written by the core thread, read by the UI thread
// every frame, so it is a fixed buffer under a lock rather than a std::string:
// reading a string while another thread reassigns it can fault.
static CRITICAL_SECTION toast_lock;
static char  toast_message[128] = "";
static char  toast_readback[128] = "";
// Prazo em relogio de parede (GetTickCount64), nao em quadros. A contagem era
// feita por CoreUpdateToast(), que so roda no laco de quadros do core - entao
// sem jogo carregado nada descontava, e qualquer mensagem disparada pelo menu
// ficava na tela para sempre. O caso que expos isso foi o "INICIANDO PORT
// NATIVO...": o port fecha o core, o contador congela e a mensagem nao sai
// mais. Com prazo absoluto a mensagem expira sozinha, rodando core ou nao.
static volatile LONGLONG toast_expires_ms = 0;

// Os chamadores continuam falando em quadros (sao 52 chamadas, com valores
// escolhidos pensando em 60 fps); a conversao acontece so aqui.
static LONGLONG ToastFramesToMs(int frames) { return (LONGLONG)frames * 1000 / 60; }
static bool ToastExpired()
{
	return (LONGLONG)GetTickCount64() >= InterlockedCompareExchange64(&toast_expires_ms, 0, 0);
}

// Initialised before main() runs, so no thread can race the initialisation.
struct ToastLockInit { ToastLockInit() { InitializeCriticalSection(&toast_lock); } };
static ToastLockInit g_toast_lock_init;

// Multi-Threading Handles
static HANDLE h_core_thread = NULL;
static volatile LONG core_thread_running = 0;

// Set around every LoadLibraryA/FreeLibrary call the core thread makes.
// TerminateThread()-ing the core thread while it owns the process-wide
// loader lock (i.e. mid-LoadLibrary/FreeLibrary) wedges that lock forever -
// every later LoadLibrary/FreeLibrary/CreateThread anywhere in the process,
// including the next core load, then hangs with no explanation. CoreShutdown()
// checks this before giving up and terminating, so it waits out a module
// operation instead of interrupting one.
static volatile LONG g_core_in_module_op = 0;

// Async load state, owned by the core thread once it starts.
#define CORE_STATE_IDLE    0
#define CORE_STATE_LOADING 1
#define CORE_STATE_RUNNING 2
static volatile LONG g_core_state = CORE_STATE_IDLE;
static volatile LONG g_av_info_dirty = 0;
static struct retro_memory_map g_memory_map = { NULL, 0 };
static struct retro_memory_descriptor* g_memory_descriptors = NULL;
static bool g_has_memory_map = false;

static void CoreReleaseMemoryMap()
{
	if (g_memory_descriptors) { free(g_memory_descriptors); g_memory_descriptors = NULL; }
	g_memory_map.descriptors = NULL;
	g_memory_map.num_descriptors = 0;
	g_has_memory_map = false;
}

const void* CoreGetMemoryMap()
{
	return g_has_memory_map ? &g_memory_map : NULL;
}

static long g_video_diag_frames = 0;
static bool g_render_diag_done = false;
// Set by RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, read
// by RETRO_ENVIRONMENT_SET_HW_RENDER (see both cases in CB_Environment) -
// reset per load so a stale true from a previous core cannot wrongly refuse
// Vulkan to the next one that never asked for negotiation at all.
// Diagnostico: registra que o core entregou a interface de negociacao nesta
// carga. Nao decide mais nada - antes era o gatilho da recusa.
static bool g_core_wants_vk_negotiation = false;
static std::string   g_pending_rom;
static std::string   g_pending_core_hint;

// Forward declarations: these run on the core thread only.
static bool CoreLoad(const char* core_dll_path);
static bool CoreLoadGame(const char* rom_path, bool suppress_toast = false);
static void CoreUnload();
static void ProcessPendingCommand();
static void ApplyDisplaySync();

// Consumer side of the triple buffer. Takes the newest completed frame if one
// is waiting, and returns the buffer plus the geometry it was rendered at.
static const uint32_t* AcquireFrame(int* out_w, int* out_h)
{
	if (InterlockedExchange(&g_fb_dirty, 0))
		g_fb_front = (int)InterlockedExchange(&g_fb_ready, (LONG)g_fb_front);

	if (out_w) *out_w = (int)g_fb_dim_w[g_fb_front];
	if (out_h) *out_h = (int)g_fb_dim_h[g_fb_front];
	return g_triple_fb[g_fb_front];
}

void CoreSetToast(const char* message, int frames_duration)
{
	if (!message) return;

	// toast_message and toast_expires_ms must change together. Setting the timer
	// after releasing toast_lock left a window where two threads calling
	// CoreSetToast() close together (e.g. a port's background monitor thread
	// and the core thread loading a new game) could interleave: whichever
	// thread's InterlockedExchange(timer) lands last wins, independent of
	// whose message text last landed - the wrong text could sit on screen
	// with a timer duration set by an unrelated call, or a just-cleared
	// toast could pop back up if a stale duration overwrote timer=0 after it.
	EnterCriticalSection(&toast_lock);
	strncpy_s(toast_message, sizeof(toast_message), message, _TRUNCATE);
	LeaveCriticalSection(&toast_lock);

	// InterlockedExchange itself still can't be inside the critical section
	// (CoreUpdateToast's decrement path also needs toast_lock only for the
	// clear, not the countdown), but publishing it immediately after, with
	// nothing else able to run between the two, keeps the pairing correct
	// for the actual race that mattered: two CoreSetToast() calls stepping
	// on each other.
	InterlockedExchange64(&toast_expires_ms,
		frames_duration > 0 ? (LONGLONG)GetTickCount64() + ToastFramesToMs(frames_duration) : 0);
}

const char* CoreGetToast()
{
	// Copies into a UI-thread buffer so the caller never holds a pointer into
	// storage the core thread may overwrite mid-draw.
	EnterCriticalSection(&toast_lock);
	if (ToastExpired())
		toast_message[0] = '\0';
	strncpy_s(toast_readback, sizeof(toast_readback), toast_message, _TRUNCATE);
	LeaveCriticalSection(&toast_lock);
	return toast_readback;
}

bool CoreIsToastActive()
{
	return !ToastExpired() && toast_message[0] != '\0';
}

void CoreUpdateToast()
{
	// Ja nao conta nada: o prazo e absoluto. So limpa o texto vencido, para que
	// quem le toast_message direto nao encontre uma mensagem expirada.
	//
	// O teste do texto vem ANTES do lock, e isso importa. Esta funcao roda na
	// thread do core a cada quadro, e ToastExpired() e verdadeiro quase o tempo
	// todo - sem o teste, a thread do core pegaria toast_lock 60 vezes por
	// segundo. E ela e justamente a thread que CoreShutdown mata a forca quando
	// o core nao sai em 3 s: morta segurando o lock, deixaria toast_lock preso
	// e travaria todo CoreSetToast/CoreGetToast dali em diante. Com o teste, o
	// lock volta a ser tomado uma vez por mensagem, como na versao por quadros.
	// A leitura de um char fora do lock e a mesma que CoreIsToastActive ja faz.
	if (ToastExpired() && toast_message[0] != '\0')
	{
		EnterCriticalSection(&toast_lock);
		toast_message[0] = '\0';
		LeaveCriticalSection(&toast_lock);
	}
}

// -------------------------------------------------------------
// High-Performance Audio Resampling & Output Engine
// -------------------------------------------------------------
#define RING_BUFFER_SIZE   65536 // 65536 stereo frames (~1.36s capacity)
#define NUM_WAVE_BUFFERS   48    // upper bound: 48 * 512 frames = 512ms of queue
#define MIN_WAVE_BUFFERS   6     // lower bound: 6 * 512 frames = 64ms
#define SAMPLES_PER_BUFFER 512   // ~10.6ms per buffer at 48kHz

// A card whose shared-mode mix format is natively 44.1kHz still accepts a
// 48kHz SDL_OpenAudioDeviceStream() - the OS mixer resamples - but that is a second
// conversion stage stacked on top of the one this file already does from the
// core's rate. DetectPreferredOutputSampleRate() asks the device what it
// actually wants, once, so most machines end up doing only one conversion
// instead of two. 48000 is the fallback for anything that cannot be asked
// (COM unavailable, no default device, a locked-down session).
static int g_output_sample_rate = 48000;

// Recurring underruns escalate the queue depth (see EscalateWaveQueue); this
// bounds how far above NUM_WAVE_BUFFERS*SAMPLES_PER_BUFFER/rate it can go in
// milliseconds, purely for the log line - the loop itself clamps to the array.
#define UNDERRUN_ESCALATE_WINDOW_MS    2000 // how often the underrun count is judged
#define UNDERRUN_ESCALATE_THRESHOLD    3    // this many starved passes in the window...
#define UNDERRUN_ESCALATE_STEP_BUFFERS 4    // ...grows the queue by this many buffers
#define DEESCALATE_CLEAN_WINDOWS_NEEDED 8   // this many perfectly clean windows in a row (16s)...
                                             // ...shrinks it back by UNDERRUN_ESCALATE_STEP_BUFFERS

// dev-sdl3: g_audio_stream replaces HWAVEOUT/WAVEHDR. SDL_OpenAudioDeviceStream
// hands back a stream already bound to the default output device; feeding it
// is just SDL_PutAudioStreamData with no header prepare/unprepare bookkeeping.
static SDL_AudioStream* g_audio_stream = nullptr;
static HANDLE   h_audio_event = NULL;
static HANDLE   h_audio_thread = NULL;
static bool     audio_thread_running = false;

// How many SAMPLES_PER_BUFFER-sized chunks of latency to keep queued in the
// stream. Set from the Audio Latency setting when a game loads. A deeper
// queue rides out longer hitches, a shallower one responds faster - same
// role NUM_WAVE_BUFFERS/g_active_wave_buffers played for the WAVEHDR array,
// just measured against SDL_GetAudioStreamQueued() now instead of counting
// how many discrete headers are still WHDR_INQUEUE.
static volatile LONG g_active_wave_buffers = 12;

// Passes (not samples) that starved during the current judging window, and
// when that window started. Touched only by the audio thread.
static DWORD s_escalate_window_start = 0;
static LONG  s_escalate_window_underruns = 0;

static int16_t  g_ring_buffer[RING_BUFFER_SIZE * 2];
static volatile LONG g_ring_write_pos = 0;
static volatile LONG g_ring_read_pos = 0;

static double   g_core_sample_rate = 48000.0;
static double   g_resample_phase = 0.0;
static int16_t  g_hist_l[4] = { 0, 0, 0, 0 };
static int16_t  g_hist_r[4] = { 0, 0, 0, 0 };
// Marcada quando a thread de audio nao respondeu e foi abandonada: o stream
// dela nao pode ser destruido, porque ela pode estar dentro dele.
static bool     audio_abandoned = false;
static bool     audio_initialized = false;
static volatile LONG g_startup_mute_samples = 0;
static volatile LONG g_audio_underrun_count = 0; // incremented by audio thread, read by PERF log

// Set once SendAudioSamples has ever pushed real frames from the core into
// the ring buffer for the current game; reset on every load. A handful of
// cores go a real stretch after retro_load_game before their first sample -
// the audio thread has nothing to play yet and correctly pads with silence,
// which should never count as a real underrun. Measured to matter little for
// Gopher64 specifically (it starts submitting audio almost immediately), but
// it costs nothing and is correct for cores that do have a genuine silent
// gap, so it stays as a first filter ahead of the de-escalation below, which
// is what actually addresses a core that runs below realtime for a few
// seconds of boot (JIT/cache warmup, disc parsing) and then recovers.
static volatile LONG g_core_ever_produced_audio = 0;

// The queue depth InitAudio computed from the Audio Latency setting, before
// any EscalateWaveQueue growth - the floor DeescalateWaveQueue will not step
// below. Without a floor, a long enough clean stretch would erode the user's
// own chosen latency, not just an escalation this session added on top of it.
static volatile LONG g_baseline_wave_buffers = 12;

// Consecutive fully-elapsed judging windows with zero starved passes, at the
// current (possibly escalated) queue depth. Touched only by the audio thread.
static LONG s_clean_windows = 0;
static bool          s_loaded_core_is_pcsx2 = false;
static std::string   s_loaded_core_path = "";



// Resampler rate-control state. This used to live as function-local statics
// inside SendAudioSamples, which meant a lock from one game's audio survived
// into the next: load a PS2 title, its measured ~29.6kHz got locked in, then
// load anything else (another PS2 game with a different real rate, or a
// different system entirely) and it kept playing at the first game's ratio
// with no recalibration, because the "already locked" check never saw a
// reason to re-measure. Living here instead lets InitAudio - which already
// runs on every game load - reset it via ResetAudioRateController().
static LARGE_INTEGER s_rate_last = { 0 };
static size_t   s_samples_accum = 0;
static double   s_prev_measure = 0.0;
static int      s_agree_count = 0;
static double   s_locked_step = 0.0;   // 0 while still measuring
static double   s_step = 0.0;

// Post-lock trim state. See the hysteresis block in SendAudioSamples for why
// this exists separately from the lock above.
static double   s_tempo_ema = 1.0;
static bool     s_trim_active = true;
static double   s_stable_time = 0.0;
static LARGE_INTEGER s_tempo_last_time = { 0 };

static void ResetAudioRateController()
{
	s_rate_last.QuadPart = 0;
	s_samples_accum = 0;
	s_prev_measure = 0.0;
	s_agree_count = 0;
	s_locked_step = 0.0;
	s_step = 0.0;
	s_tempo_ema = 1.0;
	s_trim_active = true;
	s_stable_time = 0.0;
	s_tempo_last_time.QuadPart = 0;

	// PCSX2 has an internal SPU2 engine that produces clean 48000 Hz.
	// When running output at 48000 Hz, pre-lock step at 1.0 to eliminate initial drift and stutter.
	if (s_loaded_core_is_pcsx2 && g_output_sample_rate == 48000)
	{
		s_locked_step = 1.0;
		s_step = 1.0;
		s_trim_active = false;
	}
}

// Queries the default render device's shared-mode mix format once, so
// InitAudio can open the audio stream at the rate the card actually runs
// instead of an assumed 48000. Every failure path falls back to 48000, which every
// device accepts (the OS mixer resamples for it, same as it always has).
static int DetectPreferredOutputSampleRate()
{
	int result = 48000;
	HRESULT hr_init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	bool need_uninit = SUCCEEDED(hr_init);

	IMMDeviceEnumerator* enumerator = NULL;
	IMMDevice* device = NULL;
	IAudioClient* client = NULL;
	WAVEFORMATEX* mix_format = NULL;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (SUCCEEDED(hr))
		hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (SUCCEEDED(hr))
		hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client);
	if (SUCCEEDED(hr))
		hr = client->GetMixFormat(&mix_format);
	if (SUCCEEDED(hr) && mix_format && mix_format->nSamplesPerSec >= 8000 && mix_format->nSamplesPerSec <= 192000)
		result = (int)mix_format->nSamplesPerSec;

	if (mix_format)  CoTaskMemFree(mix_format);
	if (client)      client->Release();
	if (device)      device->Release();
	if (enumerator)  enumerator->Release();
	if (need_uninit) CoUninitialize();

	FILE* lf = fopen("karamelo.log", "a");
	if (lf)
	{
		fprintf(lf, "[INFO] [AUDIO] dispositivo de saida: %d Hz%s\n",
			result, (result == 48000 && FAILED(hr)) ? " (fallback, deteccao falhou)" : "");
		fclose(lf);
	}
	return result;
}

// 4-point / 3rd-order Catmull-Rom Cubic Hermite Spline Interpolator:
// Eliminates high-frequency imaging noise and triangular aliasing artifacts (the metallic buzz/hiss).
static inline int16_t HermiteInterpolate(int16_t y0, int16_t y1, int16_t y2, int16_t y3, double x)
{
	double c0 = (double)y1;
	double c1 = 0.5 * ((double)y2 - (double)y0);
	double c2 = (double)y0 - 2.5 * (double)y1 + 2.0 * (double)y2 - 0.5 * (double)y3;
	double c3 = 0.5 * ((double)y3 - (double)y0) + 1.5 * ((double)y1 - (double)y2);
	double out = ((c3 * x + c2) * x + c1) * x + c0;
	if (out < -32768.0) return -32768;
	if (out > 32767.0) return 32767;
	return (int16_t)out;
}


// Grows the queue depth when the ring keeps running dry - a slow machine, a
// background scan stealing CPU, a driver with a wide scheduling jitter. The
// Latency menu setting only ever picks the *starting* depth; this is what
// lets the same build hold up on hardware weaker than whatever it was tuned
// on, without the user ever finding the setting. Called only from the audio
// thread, which is the sole owner of g_audio_stream.
static void EscalateWaveQueue()
{
	LONG active = InterlockedCompareExchange(&g_active_wave_buffers, 0, 0);
	LONG want = active + UNDERRUN_ESCALATE_STEP_BUFFERS;
	if (want > NUM_WAVE_BUFFERS) want = NUM_WAVE_BUFFERS;
	if (want <= active) return;

	// Nothing to prepare/queue up front the way WAVEHDR needed - the target
	// byte count AudioThreadProc tops the stream up to is derived from this
	// value on every pass, so raising it takes effect on the very next one.
	InterlockedExchange(&g_active_wave_buffers, want);

	FILE* lf = fopen("karamelo.log", "a");
	if (lf)
	{
		fprintf(lf, "[INFO] [AUDIO] underruns recorrentes; fila ampliada de %ldms para %ldms\n",
			(long)((long long)active * SAMPLES_PER_BUFFER * 1000 / g_output_sample_rate),
			(long)((long long)want * SAMPLES_PER_BUFFER * 1000 / g_output_sample_rate));
		fclose(lf);
	}
}

// The other half of EscalateWaveQueue: a boot-time hiccup (JIT/cache warmup,
// disc parsing running a few seconds below realtime - N64 cores are the
// common case) escalates the queue exactly like a genuinely weak machine
// would, and had no way back down once the core reached full speed. That
// left every affected session carrying extra fixed latency for the rest of
// its run for no ongoing reason. Symmetric with the escalation side: only
// after a long enough *clean* stretch at the current depth, step back down
// one notch at a time, never below the depth the user's own Latency setting
// asked for. Called only from the audio thread.
static void DeescalateWaveQueue()
{
	LONG active = InterlockedCompareExchange(&g_active_wave_buffers, 0, 0);
	LONG baseline = InterlockedCompareExchange(&g_baseline_wave_buffers, 0, 0);
	LONG want = active - UNDERRUN_ESCALATE_STEP_BUFFERS;
	if (want < baseline) want = baseline;
	if (want >= active) return;

	InterlockedExchange(&g_active_wave_buffers, want);

	FILE* lf = fopen("karamelo.log", "a");
	if (lf)
	{
		fprintf(lf, "[INFO] [AUDIO] audio estavel; fila reduzida de %ldms para %ldms\n",
			(long)((long long)active * SAMPLES_PER_BUFFER * 1000 / g_output_sample_rate),
			(long)((long long)want * SAMPLES_PER_BUFFER * 1000 / g_output_sample_rate));
		fclose(lf);
	}
}

static DWORD WINAPI AudioThreadProc(LPVOID lpParam)
{
	(void)lpParam;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	while (audio_thread_running)
	{
		WaitForSingleObject(h_audio_event, 5);

		if (!audio_thread_running || !g_audio_stream) break;

		// Top the stream up to the target queue depth rather than always
		// pushing one fixed chunk - SDL_GetAudioStreamQueued() is the
		// byte-level equivalent of "how many WAVEHDRs are still WHDR_INQUEUE"
		// the old loop tracked per-buffer. Looping in SAMPLES_PER_BUFFER-sized
		// chunks keeps the underrun/hold-last-sample behavior identical to
		// before instead of computing one enormous catch-up chunk.
		const LONG active = InterlockedCompareExchange(&g_active_wave_buffers, 0, 0);
		const int target_bytes = (int)active * SAMPLES_PER_BUFFER * 2 * (int)sizeof(int16_t);
		bool pass_underran = false;

		static int16_t s_hold_l = 0;
		static int16_t s_hold_r = 0;
		int16_t chunk[SAMPLES_PER_BUFFER * 2];

		while (SDL_GetAudioStreamQueued(g_audio_stream) < target_bytes)
		{
			LONG wp = InterlockedCompareExchange(&g_ring_write_pos, 0, 0);
			LONG rp = InterlockedCompareExchange(&g_ring_read_pos, 0, 0);
			LONG available = (wp >= rp) ? (wp - rp) : (RING_BUFFER_SIZE - rp + wp);

			// Always keep the device fed. Letting the queue run dry stops
			// playback outright, which is what produced the periodic gaps;
			// short frames are padded by holding the last sample instead.
			for (int s = 0; s < SAMPLES_PER_BUFFER; s++)
			{
				if (available > 0)
				{
					s_hold_l = g_ring_buffer[rp * 2 + 0];
					s_hold_r = g_ring_buffer[rp * 2 + 1];
					rp = (rp + 1) % RING_BUFFER_SIZE;
					available--;
				}
				else
				{
					// Underrun: decay smoothly to silence from last sample.
					// Only counted once the core has produced its first real
					// sample - padding before that is the loading screen
					// playing silence as intended, not a real underrun (see
					// g_core_ever_produced_audio), and was otherwise drowning
					// out the [PERF] log's underrun count with tens of
					// thousands of false hits during every boot.
					if (InterlockedCompareExchange(&g_core_ever_produced_audio, 0, 0))
						InterlockedIncrement(&g_audio_underrun_count);
					pass_underran = true;
					s_hold_l = (int16_t)(s_hold_l * 63 / 64);
					s_hold_r = (int16_t)(s_hold_r * 63 / 64);
				}
				chunk[s * 2 + 0] = s_hold_l;
				chunk[s * 2 + 1] = s_hold_r;
			}

			InterlockedExchange(&g_ring_read_pos, rp);
			// A false return means nothing was actually queued - breaking
			// here avoids ever spinning forever on a while condition that
			// SDL_GetAudioStreamQueued() would otherwise never satisfy.
			if (!SDL_PutAudioStreamData(g_audio_stream, chunk, (int)sizeof(chunk)))
				break;
		}

		DWORD now_tick = GetTickCount();
		if (s_escalate_window_start == 0) s_escalate_window_start = now_tick;
		// Ignore pre-first-sample padding (see g_core_ever_produced_audio) -
		// that silence is expected and not evidence the machine is struggling.
		if (pass_underran && InterlockedCompareExchange(&g_core_ever_produced_audio, 0, 0))
			s_escalate_window_underruns++;
		if (now_tick - s_escalate_window_start >= UNDERRUN_ESCALATE_WINDOW_MS)
		{
			if (s_escalate_window_underruns >= UNDERRUN_ESCALATE_THRESHOLD)
			{
				EscalateWaveQueue();
				s_clean_windows = 0;
			}
			else if (s_escalate_window_underruns == 0)
			{
				// A window with SOME underruns but below the escalate
				// threshold is not "clean" either - only a perfectly quiet
				// window counts, so a machine hovering right at the edge
				// doesn't get de-escalated back into audible trouble.
				if (++s_clean_windows >= DEESCALATE_CLEAN_WINDOWS_NEEDED)
				{
					DeescalateWaveQueue();
					s_clean_windows = 0;
				}
			}
			else
			{
				s_clean_windows = 0;
			}
			s_escalate_window_start = now_tick;
			s_escalate_window_underruns = 0;
		}
	}
	return 0;
}

// Translates the user's Latency setting (milliseconds) into a wave-buffer
// queue depth at the given output sample rate. Shared by InitAudio's primary
// path and its 48kHz fallback so the PCSX2 floor and the MIN/MAX clamp can't
// drift out of sync between them the way they already once did (the fallback
// path silently dropped the PCSX2 floor and never updated
// g_baseline_wave_buffers until that was caught and fixed in place).
static int ComputeWantedQueueDepth(int output_sample_rate)
{
	int want = (MenuGetAudioLatencyMs() * output_sample_rate / 1000) / SAMPLES_PER_BUFFER;
	if (s_loaded_core_is_pcsx2 && want < 20) want = 20; // 20 buffers = ~213ms headroom for PCSX2 multi-threading
	if (want < MIN_WAVE_BUFFERS) want = MIN_WAVE_BUFFERS;
	if (want > NUM_WAVE_BUFFERS) want = NUM_WAVE_BUFFERS;
	return want;
}

static void InitAudio(int sample_rate)
{
	// Resolved once, before anything below uses it to convert milliseconds to
	// buffer counts - a stale 48000 assumption here would mis-size the very
	// first game's queue depth and startup mute window on a 44.1kHz device.
	{
		static bool s_output_rate_detected = false;
		if (!s_output_rate_detected)
		{
			g_output_sample_rate = DetectPreferredOutputSampleRate();
			s_output_rate_detected = true;
		}
	}

	// Translate the Latency setting into a queue depth. Done before the
	// already-initialised early return so changing it and loading another
	// game takes effect without restarting the app.
	{
		int want = ComputeWantedQueueDepth(g_output_sample_rate);
		InterlockedExchange(&g_active_wave_buffers, (LONG)want);
		// Floor for DeescalateWaveQueue - a long clean stretch should only ever
		// unwind an escalation this session added, never erode below what the
		// user's own Latency setting asked for.
		InterlockedExchange(&g_baseline_wave_buffers, (LONG)want);
	}

	g_core_sample_rate = sample_rate > 0 ? (double)sample_rate : 48000.0;
	g_resample_phase = 0.0;
	memset(g_hist_l, 0, sizeof(g_hist_l));
	memset(g_hist_r, 0, sizeof(g_hist_r));

	// Every game load gets its own calibration: the core just changed (or
	// reloaded), and any previous lock/tempo state belonged to whatever ran
	// before it.
	ResetAudioRateController();
	s_escalate_window_start = 0;
	s_escalate_window_underruns = 0;
	s_clean_windows = 0;
	InterlockedExchange(&g_core_ever_produced_audio, 0);

	// Anti-pop: Zero ring buffer and activate soft startup ramp
	InterlockedExchange(&g_startup_mute_samples, (LONG)(g_output_sample_rate * 0.15));
	InterlockedExchange(&g_ring_read_pos, 0);
	memset(g_ring_buffer, 0, sizeof(g_ring_buffer));

	// Start the ring already at its target occupancy rather than empty. The
	// buffer is zeroed, so this is silence, and the startup ramp covers it.
	// Filling up to the target instead would mean running detuned for the
	// whole climb - which is what the deeper settings made audible.
	InterlockedExchange(&g_ring_write_pos,
		InterlockedCompareExchange(&g_active_wave_buffers, 0, 0) * SAMPLES_PER_BUFFER);

	if (audio_initialized)
	{
		return;
	}

	SDL_AudioSpec spec = { 0 };
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	spec.freq = g_output_sample_rate;

	h_audio_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	g_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (!g_audio_stream)
	{
		// The exact device rate can still be rejected outright by some
		// drivers. Fall back to the one rate every Windows audio stack has
		// accepted since XP, rather than leaving audio silently dead for the
		// rest of the session.
		g_output_sample_rate = 48000;
		spec.freq = g_output_sample_rate;
		// Recomputed at the new rate via the same ComputeWantedQueueDepth() the
		// primary path above uses, so the PCSX2 floor and g_baseline_wave_buffers
		// can no longer silently drift out of sync between the two paths the way
		// they already once did here.
		int want = ComputeWantedQueueDepth(g_output_sample_rate);
		InterlockedExchange(&g_active_wave_buffers, (LONG)want);
		InterlockedExchange(&g_baseline_wave_buffers, (LONG)want);
		InterlockedExchange(&g_ring_write_pos, want * SAMPLES_PER_BUFFER);
		g_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	}

	if (g_audio_stream)
	{
		SDL_ResumeAudioStreamDevice(g_audio_stream);

		// Prime the stream with silence up to the target queue depth - the
		// same preload the old code did by writing N zeroed WAVEHDR buffers,
		// so playback starts already at target latency instead of climbing
		// to it (the startup mute ramp above covers the pop, not the climb).
		const LONG prime = InterlockedCompareExchange(&g_active_wave_buffers, 0, 0);
		static const int16_t s_silence[SAMPLES_PER_BUFFER * 2] = { 0 };
		for (int i = 0; i < prime; i++)
			SDL_PutAudioStreamData(g_audio_stream, s_silence, (int)sizeof(s_silence));

		// Ring positions were already set above, including the target-occupancy
		// preload; resetting them here would empty it on the first game.
		audio_thread_running = true;
		h_audio_thread = CreateThread(NULL, 0, AudioThreadProc, NULL, 0, NULL);
		audio_initialized = true;
	}
	else
	{
		// Both attempts failed - every game from here on plays completely
		// silent with nothing else in the log to explain why (audio_initialized
		// stays false, so SendAudioSamples() no-ops every call with no error of
		// its own). This is exactly the failure mode that happens if SDL_INIT_AUDIO
		// was never passed to SDL_Init() - the subsystem call fails quietly
		// rather than crashing.
		FILE* lf = fopen("karamelo.log", "a");
		if (lf)
		{
			fprintf(lf, "[ERROR] [AUDIO] SDL_OpenAudioDeviceStream falhou: %s\n", SDL_GetError());
			fclose(lf);
		}
	}
}

static void SendAudioSamples(const int16_t* data, size_t frames)
{
	if (!audio_initialized || !data || frames == 0) return;

	InterlockedExchange(&g_core_ever_produced_audio, 1);

	float vol_mult = audio_muted ? 0.0f : (master_volume / 100.0f);

	// Startup anti-pop / DC offset suppression:
	// Mutes initial 50ms of hardware reset spikes and softly ramps volume over 100ms
	LONG mute_countdown = InterlockedCompareExchange(&g_startup_mute_samples, 0, 0);
	if (mute_countdown > 0)
	{
		LONG total_ramp = (LONG)(g_output_sample_rate * 0.15);
		LONG mute_period = (LONG)(g_output_sample_rate * 0.05);
		float startup_gain = 0.0f;
		if (mute_countdown <= (total_ramp - mute_period))
		{
			startup_gain = 1.0f - ((float)mute_countdown / (float)(total_ramp - mute_period));
			if (startup_gain < 0.0f) startup_gain = 0.0f;
			if (startup_gain > 1.0f) startup_gain = 1.0f;
		}
		vol_mult *= startup_gain;
		InterlockedAdd(&g_startup_mute_samples, -(LONG)frames);
		if (InterlockedCompareExchange(&g_startup_mute_samples, 0, 0) < 0)
			InterlockedExchange(&g_startup_mute_samples, 0);
	}

	LONG wp = InterlockedCompareExchange(&g_ring_write_pos, 0, 0);
	LONG rp = InterlockedCompareExchange(&g_ring_read_pos, 0, 0);
	LONG occupancy = (wp >= rp) ? (wp - rp) : (RING_BUFFER_SIZE - rp + wp);

	// Never let the writer lap the reader; dropping the tail of an oversized
	// batch is far less audible than overwriting unplayed audio.
	LONG capacity = RING_BUFFER_SIZE - occupancy - 1;

	// Resampler rate control, in two stages.
	//
	// Stage one finds the rate the core really delivers, because a declaration
	// cannot be trusted: Play! (PS2) announces 44.1kHz and delivers around
	// 29.6kHz, clocked to game logic. Once consecutive measurements agree, the
	// rate is LOCKED - this is the coarse pass that gets close fast, so the
	// listener isn't hearing a multi-second warble while windows disagree.
	//
	// Stage two used to be a permanent 0.5% trim once locked, driven straight
	// off the instantaneous occupancy error. That is fine for a rate that
	// never moves again, but this file loads a new game (a new lock) into
	// static state that outlives the game, and even within one session the
	// real rate is not a constant: it is however fast that specific machine's
	// CPU actually carries the core's game logic, which shifts with thermal
	// throttling, background load, or the game itself hitting a heavier scene.
	// So stage two now runs the way PCSX2's own SPU2 stretcher does it (see
	// pcsx2/Host/AudioStream.cpp, UpdateStretchTempo): smooth the occupancy
	// ratio into a slow-moving average, and only ever act on it once the
	// average has drifted far enough, for long enough, that doing nothing
	// would mean an audible gap or a hard reseed. Below that, playback runs
	// bit-exact at the locked step - no trim at all, which is the difference
	// between "silent unless something is actually wrong" and a permanent,
	// low-level pitch wobble nobody asked for.
	if (s_step <= 0.0) s_step = g_core_sample_rate / (double)g_output_sample_rate;

	// The counter frequency is fixed for the life of the process (Microsoft's
	// own guidance is to query it once), so caching it here avoids a wasted
	// call on every single one of these - this function runs once per
	// emulated frame, i.e. 50-60+ times a second during gameplay.
	static LARGE_INTEGER freq = { 0 };
	if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (s_rate_last.QuadPart == 0) s_rate_last = now;

	if (s_locked_step <= 0.0)
	{
		s_samples_accum += frames;
		double elapsed = (double)(now.QuadPart - s_rate_last.QuadPart) / (double)freq.QuadPart;
		if (elapsed >= 0.5)
		{
			double measured = (double)s_samples_accum / elapsed;
			if (measured >= 8000.0 && measured <= 192000.0)
			{
				// Within 1% of the previous window counts as agreement.
				if (s_prev_measure > 0.0 &&
				    fabs(measured - s_prev_measure) < s_prev_measure * 0.01)
				{
					if (++s_agree_count >= 3)
					{
						s_locked_step = measured / (double)g_output_sample_rate;
						FILE* lf = fopen("karamelo.log", "a");
						if (lf)
						{
							fprintf(lf, "[INFO] [AUDIO] taxa travada em %.0f Hz (declarada %.0f Hz), passo %.5f\n",
								measured, g_core_sample_rate, s_locked_step);
							fclose(lf);
						}
					}
				}
				else
				{
					s_agree_count = 0;
				}
				s_prev_measure = measured;

				// Glide toward the reading only while still searching.
				s_step = s_step * 0.7 + (measured / (double)g_output_sample_rate) * 0.3;
			}
			s_samples_accum = 0;
			s_rate_last = now;
		}
	}

	double step = s_step;
	if (s_locked_step > 0.0)
	{
		// One audio-stream queue in reserve, not half the ring. The ring is 65536
		// frames of headroom for bursts; aiming at the middle of it would mean
		// holding 683ms of audio, and the slow climb toward that target is
		// itself an audible drift. 12 buffers of 512 frames is 128ms, which is
		// enough to ride out a hitch without adding noticeable delay.
		const double target_fill = (double)(InterlockedCompareExchange(&g_active_wave_buffers, 0, 0)
		                                    * SAMPLES_PER_BUFFER);

		// Resync outright when the gap is gross. The trim below is deliberately
		// too gentle to close a large gap quickly - at 0.5% it moves 240
		// frames a second, so recovering from a stalled boot would mean half a
		// minute of detune. A hard reseed is a discontinuity, but it only ever
		// fires when playback is already broken (core booting below speed, a
		// long stall), where silence is playing anyway. Padding with silence
		// Too empty pads with silence; too full discards the excess, which is
		// stale latency nobody wants to hear anyway.
		const LONG seed = (LONG)target_fill;
		const LONG min_thresh = s_loaded_core_is_pcsx2 ? (SAMPLES_PER_BUFFER * 2) : (seed / 3);
		if (occupancy < min_thresh || occupancy > seed * 3)
		{
			// Re-read: the audio thread has been draining since the snapshot
			// above, so the stale value would seed us behind the reader.
			rp = InterlockedCompareExchange(&g_ring_read_pos, 0, 0);
			for (LONG i = occupancy; i < seed; i++)
			{
				LONG idx = (rp + i) % RING_BUFFER_SIZE;
				g_ring_buffer[idx * 2 + 0] = 0;
				g_ring_buffer[idx * 2 + 1] = 0;
			}
			// wp and capacity have to move with it: the write loop below
			// continues from wp and stores it back at the end, which would
			// otherwise undo this.
			wp = (rp + seed) % RING_BUFFER_SIZE;
			occupancy = seed;
			capacity = RING_BUFFER_SIZE - occupancy - 1;

			// The gap that just got closed was, by definition, not something
			// the slow average had any hope of tracking - restart it at the
			// fresh occupancy instead of decaying toward it over the next
			// couple of seconds while still applying a stale trim.
			s_tempo_ema = 1.0;
			s_stable_time = 0.0;
		}

		double raw_ratio = (target_fill > 0.0) ? ((double)occupancy / target_fill) : 1.0;

		double dt = (s_tempo_last_time.QuadPart != 0)
			? (double)(now.QuadPart - s_tempo_last_time.QuadPart) / (double)freq.QuadPart
			: 0.0;
		s_tempo_last_time = now;
		if (dt > 0.0 && dt < 2.0)
		{
			// Continuous-time EMA rather than a fixed-size window of calls:
			// SendAudioSamples fires once per emulated frame, and how much
			// real time that spans depends on the core (50 vs 60Hz, PS2's own
			// uneven pacing) - a window sized in calls would average a
			// different span of wall-clock time per core. This does not.
			double alpha = 1.0 - exp(-dt / 1.5); // ~1.5s time constant
			s_tempo_ema += (raw_ratio - s_tempo_ema) * alpha;
		}

		// Hysteresis on top of the average: once occupancy has sat close to
		// target for a couple of seconds, stop trimming altogether and play
		// the locked step bit-exact. Only resume once the drift is large
		// enough that the alternative is a gap or another hard reseed. This
		// mirrors PCSX2's stretcher, which spends most of a session at
		// tempo==1.0 (stretch "inactive") for the same reason: a correction
		// applied at every callback, even a small one, is itself a signal a
		// listener can pick up on over a long enough session.
		if (s_trim_active)
		{
			if (fabs(s_tempo_ema - 1.0) < 0.003)
			{
				s_stable_time += (dt > 0.0 && dt < 2.0) ? dt : 0.0;
				if (s_stable_time > 2.0) s_trim_active = false;
			}
			else
			{
				s_stable_time = 0.0;
			}
		}
		else if (fabs(s_tempo_ema - 1.0) > 0.02)
		{
			s_trim_active = true;
			s_stable_time = 0.0;
		}

		if (s_trim_active)
		{
			double err = raw_ratio - 1.0;
			if (err < -1.0) err = -1.0;
			else if (err > 1.0) err = 1.0;

			// Reading ahead of playback means the ring is filling, so the
			// input needs to be consumed slightly faster - and vice versa.
			// 0.5% is under the threshold where pitch change becomes audible.
			step = s_locked_step * (1.0 + err * 0.005);
		}
		else
		{
			step = s_locked_step;
		}
	}


	// Stops consuming input the moment the ring is full. The old loop went on
	// shifting the interpolator history for samples it then had no room to
	// emit, so the phase and the history disagreed and the seam clicked.
	for (size_t i = 0; i < frames && capacity > 0; i++)
	{
		int16_t cur_l = (int16_t)(data[i * 2 + 0] * vol_mult);
		int16_t cur_r = (int16_t)(data[i * 2 + 1] * vol_mult);

		// Shift 4-point Hermite history
		g_hist_l[0] = g_hist_l[1]; g_hist_l[1] = g_hist_l[2]; g_hist_l[2] = g_hist_l[3]; g_hist_l[3] = cur_l;
		g_hist_r[0] = g_hist_r[1]; g_hist_r[1] = g_hist_r[2]; g_hist_r[2] = g_hist_r[3]; g_hist_r[3] = cur_r;

		while (g_resample_phase < 1.0 && capacity > 0)
		{
			int16_t out_l = HermiteInterpolate(g_hist_l[0], g_hist_l[1], g_hist_l[2], g_hist_l[3], g_resample_phase);
			int16_t out_r = HermiteInterpolate(g_hist_r[0], g_hist_r[1], g_hist_r[2], g_hist_r[3], g_resample_phase);

			g_ring_buffer[wp * 2 + 0] = out_l;
			g_ring_buffer[wp * 2 + 1] = out_r;

			wp = (wp + 1) % RING_BUFFER_SIZE;
			capacity--;
			g_resample_phase += step;
		}

		if (g_resample_phase >= 1.0) g_resample_phase -= 1.0;
	}

	InterlockedExchange(&g_ring_write_pos, wp);
	SetEvent(h_audio_event);
}


// -------------------------------------------------------------
// Libretro Virtual File System (VFS) Interface Implementation
// -------------------------------------------------------------
struct retro_vfs_file_handle
{
	FILE* fp;
};

static const char* vfs_get_path(struct retro_vfs_file_handle* stream) { (void)stream; return ""; }
static struct retro_vfs_file_handle* vfs_open(const char* path, unsigned mode, unsigned hints)
{
	(void)hints;
	const char* m = "rb";
	if (mode & 2) m = "wb+";
	FILE* f = fopen(path, m);
	if (!f) return NULL;
	retro_vfs_file_handle* handle = (retro_vfs_file_handle*)malloc(sizeof(retro_vfs_file_handle));
	handle->fp = f;
	return handle;
}
static int vfs_close(struct retro_vfs_file_handle* stream)
{
	if (!stream || !stream->fp) return -1;
	fclose(stream->fp);
	free(stream);
	return 0;
}
static int64_t vfs_size(struct retro_vfs_file_handle* stream)
{
	if (!stream || !stream->fp) return -1;
	int64_t cur = _ftelli64(stream->fp);
	_fseeki64(stream->fp, 0, SEEK_END);
	int64_t sz = _ftelli64(stream->fp);
	_fseeki64(stream->fp, cur, SEEK_SET);
	return sz;
}
static int64_t vfs_tell(struct retro_vfs_file_handle* stream)
{
	if (!stream || !stream->fp) return -1;
	return _ftelli64(stream->fp);
}
static int64_t vfs_seek(struct retro_vfs_file_handle* stream, int64_t offset, int seek_position)
{
	if (!stream || !stream->fp) return -1;
	int origin = SEEK_SET;
	if (seek_position == 1) origin = SEEK_CUR;
	else if (seek_position == 2) origin = SEEK_END;
	_fseeki64(stream->fp, offset, origin);
	return _ftelli64(stream->fp);
}
static int64_t vfs_read(struct retro_vfs_file_handle* stream, void* s, uint64_t len)
{
	if (!stream || !stream->fp) return -1;
	return (int64_t)fread(s, 1, (size_t)len, stream->fp);
}
static int64_t vfs_write(struct retro_vfs_file_handle* stream, const void* s, uint64_t len)
{
	if (!stream || !stream->fp) return -1;
	return (int64_t)fwrite(s, 1, (size_t)len, stream->fp);
}
static int vfs_flush(struct retro_vfs_file_handle* stream)
{
	if (!stream || !stream->fp) return -1;
	return fflush(stream->fp);
}
static int vfs_remove(const char* path) { return remove(path); }
static int vfs_rename(const char* old_path, const char* new_path) { return rename(old_path, new_path); }

// Not advertised through GET_VFS_INTERFACE: only the 11 version-1 entry points
// are implemented, and the stdio path cores use by default already works. It is
// kept here as the base for a proper v3 implementation.
static struct retro_vfs_interface g_vfs_iface = {
	vfs_get_path,
	vfs_open,
	vfs_close,
	vfs_size,
	vfs_tell,
	vfs_seek,
	vfs_read,
	vfs_write,
	vfs_flush,
	vfs_remove,
	vfs_rename,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void CoreLogPrintf(enum retro_log_level level, const char* fmt, ...)
{
	static const char* lvl_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
	const char* lvl = (level >= 0 && level <= 3) ? lvl_names[level] : "LOG";

	char buf[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	// Kept open: reopening per line meant 35 open/close pairs just to
	// list the tracks of one CD image.
	static FILE* f = fopen("karamelo.log", "a");
	if (f)
	{
		fprintf(f, "[%s] %s\n", lvl, buf);
		fflush(f);
	}
	printf("[%s] %s\n", lvl, buf);
}

#include <map>
#include <set>
static std::map<std::string, std::string> g_core_options;

// Defaults the core declared through SET_VARIABLES. A user choice in the OSD
// overrides one of these; without a choice the core must still get its own
// default back, not nothing.
static std::map<std::string, std::string> g_core_defaults;

// g_core_defaults is a std::map, so iterating it directly for the Core
// Options menu page would list keys alphabetically instead of in the order
// the core actually declared them (RETRO_ENVIRONMENT_SET_VARIABLES hands them
// over as a plain array) - a cosmetic difference from RetroArch's own Quick
// Menu, but confusing when a core groups related options together on
// purpose. This mirrors that declaration order; kept in lockstep with
// g_core_defaults at both of its own clear/populate sites.
static std::vector<std::string> g_core_defaults_order;

// g_core_defaults itself only ever holds the resolved default VALUE (see
// ParsedCoreVar::value's own comment - just the first choice, nothing else),
// because that is all GET_VARIABLE's fallback path has ever needed. The Core
// Options menu page needs the label and the full choice list too, so this
// keeps each key's untouched declaration ("Label; choice1|choice2|...")
// separately instead of changing what g_core_defaults itself stores. Kept in
// lockstep with g_core_defaults at both of its own clear/populate sites.
static std::map<std::string, std::string> g_core_option_raw;

// The map is written by the UI thread and read by the core thread from inside
// retro_run. Guarded, and values are handed out through a rotating buffer so a
// core never holds a pointer into map storage that may be reallocated.
static CRITICAL_SECTION options_lock;
static volatile LONG g_options_dirty = 0;

struct OptionsLockInit { OptionsLockInit() { InitializeCriticalSection(&options_lock); } };
static OptionsLockInit g_options_lock_init;

static char g_option_slots[8][128];
static int  g_option_slot_next = 0;

static const char* StableOptionValue(const std::string& value)
{
	char* slot = g_option_slots[g_option_slot_next];
	g_option_slot_next = (g_option_slot_next + 1) % 8;
	strncpy_s(slot, sizeof(g_option_slots[0]), value.c_str(), _TRUNCATE);
	return slot;
}

// The copy lands in a thread_local buffer: a single shared static would just
// move the race from the string to the buffer.
const char* CoreGetGameName()
{
	static thread_local char buf[260];
	EnterCriticalSection(&name_lock);
	strncpy_s(buf, sizeof(buf), loaded_game_name.c_str(), _TRUNCATE);
	LeaveCriticalSection(&name_lock);
	return buf;
}

const char* CoreGetRomDir()
{
	static thread_local char buf[512];
	EnterCriticalSection(&name_lock);
	strncpy_s(buf, sizeof(buf), loaded_rom_dir.c_str(), _TRUNCATE);
	LeaveCriticalSection(&name_lock);
	return buf;
}

const char* CoreGetCoreName()
{
	static thread_local char buf[128];
	EnterCriticalSection(&name_lock);
	strncpy_s(buf, sizeof(buf), loaded_core_name.c_str(), _TRUNCATE);
	LeaveCriticalSection(&name_lock);
	return buf;
}
void CoreSetOption(const char* key, const char* value)
{
	if (!key || !value) return;

	EnterCriticalSection(&options_lock);
	g_core_options[key] = value;
	LeaveCriticalSection(&options_lock);

	// Lets the core re-read its options on the next frame instead of only on
	// the next game load.
	InterlockedExchange(&g_options_dirty, 1);
}
const char* CoreGetOption(const char* key)
{
	if (!key) return "";

	EnterCriticalSection(&options_lock);
	auto it = g_core_options.find(key);
	const char* result = (it != g_core_options.end()) ? StableOptionValue(it->second) : "";
	LeaveCriticalSection(&options_lock);
	return result;
}

// Splits a core-declared SET_VARIABLES value ("Label; choice1|choice2|...")
// into its display label and pipe-separated choice list - every core follows
// this exact shape (see the SET_VARIABLES case's own comment above).
static void SplitCoreOptionDecl(const std::string& raw, std::string* label, std::vector<std::string>* choices)
{
	size_t semi = raw.find(';');
	*label = (semi == std::string::npos) ? raw : raw.substr(0, semi);
	std::string rest = (semi == std::string::npos) ? std::string() : raw.substr(semi + 1);
	size_t start = rest.find_first_not_of(' '); // conventional space after ";", not guaranteed
	if (start != std::string::npos) rest = rest.substr(start);

	choices->clear();
	size_t pos = 0;
	while (pos <= rest.size())
	{
		size_t bar = rest.find('|', pos);
		if (bar == std::string::npos) { choices->push_back(rest.substr(pos)); break; }
		choices->push_back(rest.substr(pos, bar - pos));
		pos = bar + 1;
	}
}

// Shared by every CoreOption* accessor below - caller must hold options_lock.
static bool GetCoreOptionDeclLocked(int index, std::string* key_out, std::string* label_out, std::vector<std::string>* choices_out)
{
	if (index < 0 || index >= (int)g_core_defaults_order.size()) return false;
	*key_out = g_core_defaults_order[index];
	auto it = g_core_option_raw.find(*key_out);
	if (it == g_core_option_raw.end()) return false;
	SplitCoreOptionDecl(it->second, label_out, choices_out);
	return true;
}

// Generic Core Options menu support (menu.cpp's "Core Options" page): the
// currently loaded core's own SET_VARIABLES declarations, exposed as
// key/label/choice-list/current-selection so any core's options - internal
// resolution, region, DSP, whatever it declared - get a working menu row
// with zero core-specific code, the same way RetroArch's Quick Menu >
// Options does. Choices set here go through the existing CoreSetOption(),
// so they only last for the current run, exactly like every other option
// set this way - not persisted to karamelo.cfg across restarts.
int CoreOptionCount()
{
	EnterCriticalSection(&options_lock);
	int n = (int)g_core_defaults_order.size();
	LeaveCriticalSection(&options_lock);
	return n;
}

const char* CoreOptionKey(int index)
{
	EnterCriticalSection(&options_lock);
	const char* result = (index >= 0 && index < (int)g_core_defaults_order.size())
		? StableOptionValue(g_core_defaults_order[index]) : "";
	LeaveCriticalSection(&options_lock);
	return result;
}

const char* CoreOptionLabel(int index)
{
	std::string key, label; std::vector<std::string> choices;
	EnterCriticalSection(&options_lock);
	bool ok = GetCoreOptionDeclLocked(index, &key, &label, &choices);
	const char* result = ok ? StableOptionValue(label) : "";
	LeaveCriticalSection(&options_lock);
	return result;
}

int CoreOptionChoiceCount(int index)
{
	std::string key, label; std::vector<std::string> choices;
	EnterCriticalSection(&options_lock);
	bool ok = GetCoreOptionDeclLocked(index, &key, &label, &choices);
	int n = ok ? (int)choices.size() : 0;
	LeaveCriticalSection(&options_lock);
	return n;
}

const char* CoreOptionChoiceAt(int index, int choice_index)
{
	std::string key, label; std::vector<std::string> choices;
	EnterCriticalSection(&options_lock);
	bool ok = GetCoreOptionDeclLocked(index, &key, &label, &choices);
	const char* result = (ok && choice_index >= 0 && choice_index < (int)choices.size())
		? StableOptionValue(choices[choice_index]) : "";
	LeaveCriticalSection(&options_lock);
	return result;
}

// The user's override if one has been set this run, else choice 0 - the
// first choice in the list is always the core's own declared default, per
// the SET_VARIABLES contract.
int CoreOptionCurrentChoiceIndex(int index)
{
	std::string key, label; std::vector<std::string> choices;
	EnterCriticalSection(&options_lock);
	bool ok = GetCoreOptionDeclLocked(index, &key, &label, &choices);
	int result = 0;
	if (ok)
	{
		auto ov = g_core_options.find(key);
		const std::string* current = (ov != g_core_options.end()) ? &ov->second
			: (!choices.empty() ? &choices[0] : nullptr);
		if (current)
			for (size_t c = 0; c < choices.size(); c++)
				if (choices[c] == *current) { result = (int)c; break; }
	}
	LeaveCriticalSection(&options_lock);
	return result;
}

void CoreOptionSetChoiceIndex(int index, int choice_index)
{
	std::string key, label, value; std::vector<std::string> choices;
	EnterCriticalSection(&options_lock);
	bool ok = GetCoreOptionDeclLocked(index, &key, &label, &choices);
	if (ok && choice_index >= 0 && choice_index < (int)choices.size())
		value = choices[choice_index];
	LeaveCriticalSection(&options_lock);

	if (!key.empty() && !value.empty())
		CoreSetOption(key.c_str(), value.c_str());
}

// PS2 BIOS discovery and auto-preparation
static std::string ResolveAndPreparePs2Bios()
{
	namespace fs = std::filesystem;
	std::error_code ec;

	auto is_ps2_bios = [](const fs::directory_entry& entry) -> bool {
		std::error_code ec2;
		if (!entry.is_regular_file(ec2)) return false;
		std::string ext = entry.path().extension().string();
		for (char& c : ext) c = (char)tolower((unsigned char)c);
		if (ext != ".bin") return false;

		uintmax_t sz = entry.file_size(ec2);
		std::string fn = entry.path().filename().string();
		std::string fn_lower = fn;
		for (char& c : fn_lower) c = (char)tolower((unsigned char)c);

		// PS1 BIOSes like scph5501 are 512KB (524288) - ignore them
		if (sz == 524288) return false;
		// Non-PS2 files
		if (fn_lower.find("saturn") != std::string::npos ||
		    fn_lower.find("sega") != std::string::npos ||
		    fn_lower.find("dc_boot") != std::string::npos ||
		    fn_lower.find("bios7") != std::string::npos ||
		    fn_lower.find("bios9") != std::string::npos ||
		    fn_lower.find("firmware") != std::string::npos) return false;

		// Real PS2 BIOS: standard is 4,194,304 bytes (4MB), or 2MB/8MB, or filename matches SCPH-7xxxx / SCPH-xxxxx / ps2
		if (sz == 4194304 || sz == 2097152 || sz == 8388608) return true;
		if (fn_lower.find("scph-7") != std::string::npos ||
		    fn_lower.find("scph-3") != std::string::npos ||
		    fn_lower.find("scph-5") != std::string::npos ||
		    fn_lower.find("scph-9") != std::string::npos ||
		    fn_lower.find("scph-1") != std::string::npos ||
		    fn_lower.find("ps2") != std::string::npos) return true;

		return false;
	};

	// 1. Search directories for an existing PS2 BIOS
	const char* search_dirs[] = {
		"app/bios/pcsx2/bios",
		"bios/pcsx2/bios",
		"app/bios",
		"bios"
	};

	fs::path found_path;
	std::string found_filename;

	for (const char* dir : search_dirs)
	{
		if (fs::exists(dir, ec))
		{
			for (const auto& entry : fs::directory_iterator(dir, ec))
			{
				if (is_ps2_bios(entry))
				{
					found_path = entry.path();
					found_filename = entry.path().filename().string();
					break;
				}
			}
		}
		if (!found_filename.empty()) break;
	}

	if (found_filename.empty())
	{
		found_filename = "SCPH-70012_BIOS_V12_USA_200.bin";
	}
	else
	{
		// 2. Ensure the BIOS is present in all target pcsx2/bios directories so LRPS2 always finds it:
		const char* target_dirs[] = {
			"app/bios/pcsx2/bios",
			"bios/pcsx2/bios"
		};
		for (const char* tdir : target_dirs)
		{
			fs::create_directories(tdir, ec);
			fs::path dest = fs::path(tdir) / found_filename;
			if (!fs::exists(dest, ec))
			{
				fs::copy_file(found_path, dest, fs::copy_options::overwrite_existing, ec);
			}

			// Copy companion .mec and .nvm if they exist next to the source
			fs::path src_stem = found_path.parent_path() / found_path.stem();
			fs::path dest_stem = fs::path(tdir) / found_path.stem();

			fs::path mec_src = src_stem; mec_src += ".mec";
			fs::path mec_dest = dest_stem; mec_dest += ".mec";
			if (fs::exists(mec_src, ec) && !fs::exists(mec_dest, ec))
				fs::copy_file(mec_src, mec_dest, fs::copy_options::overwrite_existing, ec);

			fs::path nvm_src = src_stem; nvm_src += ".nvm";
			fs::path nvm_dest = dest_stem; nvm_dest += ".nvm";
			if (fs::exists(nvm_src, ec) && !fs::exists(nvm_dest, ec))
				fs::copy_file(nvm_src, nvm_dest, fs::copy_options::overwrite_existing, ec);
		}

		// 3. Ensure GameIndex.yaml is available in pcsx2/resources
		const char* res_dirs[] = { "app/bios/pcsx2/resources", "bios/pcsx2/resources" };
		fs::path src_gameindex;
		for (const char* rdir : res_dirs)
		{
			fs::path gi = fs::path(rdir) / "GameIndex.yaml";
			if (fs::exists(gi, ec)) { src_gameindex = gi; break; }
		}
		if (!src_gameindex.empty())
		{
			for (const char* rdir : res_dirs)
			{
				fs::create_directories(rdir, ec);
				fs::path dest_gi = fs::path(rdir) / "GameIndex.yaml";
				if (!fs::exists(dest_gi, ec))
					fs::copy_file(src_gameindex, dest_gi, fs::copy_options::overwrite_existing, ec);
			}
		}
	}

	return found_filename;
}

// -------------------------------------------------------------
// Libretro Environment & Callbacks
// -------------------------------------------------------------

// Plain-old-data only (no std::string/std::vector members): a __try/__except
// block cannot share a function with a C++ object whose destructor would need
// to run on unwind (MSVC C2712). SET_VARIABLES hands us a core-owned array we
// have to walk and dereference; if that array is malformed - or the core's own
// heap is already corrupted, the same class of bug that crashed on MSX - the
// access violation used to fire while options_lock was held (EnterCriticalSection
// with no matching LeaveCriticalSection on this path), leaving it permanently
// locked for the rest of the process once the core thread unwound past it: any
// later CoreSetOption()/CoreGetOption() call (e.g. from the Settings menu) would
// hang forever with no crash and nothing in the log to explain why. Parsing into
// a POD buffer first, entirely before options_lock is ever taken, means the only
// memory touched while the lock is held is our own - a bad core can now fail
// this call, but it can never wedge the lock.
// `value` keeps only this call's original purpose (the GET_VARIABLE
// fallback default - just the first choice, nothing else). `raw` is the
// core's entire declaration untouched ("Label; choice1|choice2|...") for the
// Core Options menu page, which needs the label and the full choice list -
// data `value` alone deliberately throws away. Both are copied out under the
// same SEH guard below, for the same reason `value` already was: a core is
// free to unload the memory `vars` points into as soon as this call returns,
// so nothing outside this function may ever read through `vars` again.
struct ParsedCoreVar { char key[80]; char value[400]; char raw[600]; };

static bool ParseCoreVariablesGuarded(const struct retro_variable* vars,
	ParsedCoreVar* out, int max_out, int* out_count)
{
	__try
	{
		int n = 0;
		for (; vars->key && n < max_out; vars++)
		{
			if (!vars->value) continue;

			const char* semi = strchr(vars->value, ';');
			const char* first = semi ? semi + 1 : vars->value;
			while (*first == ' ') first++;

			const char* bar = strchr(first, '|');
			size_t len = bar ? (size_t)(bar - first) : strlen(first);
			if (len >= sizeof(out[n].value)) len = sizeof(out[n].value) - 1;
			while (len > 0 && (first[len - 1] == ' ' || first[len - 1] == '\r' || first[len - 1] == '\n'))
				len--;
			if (len == 0) continue;

			strncpy(out[n].key, vars->key, sizeof(out[n].key) - 1);
			out[n].key[sizeof(out[n].key) - 1] = 0;
			memcpy(out[n].value, first, len);
			out[n].value[len] = 0;
			strncpy(out[n].raw, vars->value, sizeof(out[n].raw) - 1);
			out[n].raw[sizeof(out[n].raw) - 1] = 0;
			n++;
		}
		*out_count = n;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*out_count = 0;
		return false;
	}
}

static bool CB_Environment(unsigned cmd, void* data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
	{
		struct retro_log_callback* cb = (struct retro_log_callback*)data;
		if (cb)
		{
			cb->log = CoreLogPrintf;
			return true;
		}
		return false;
	}

	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	{
		const enum retro_pixel_format* fmt = (const enum retro_pixel_format*)data;
		if (fmt)
		{
			core_pixel_format = *fmt;
			return true;
		}
		return false;
	}

	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
	{
		const char** dir = (const char**)data;
		if (dir)
		{
			static std::string sys_dir;
			if (sys_dir.empty())
			{
				std::error_code ec;
				if (fs::exists("bios/PPSSPP", ec) || (fs::exists("bios", ec) && !fs::exists("app/bios/PPSSPP", ec)))
					sys_dir = fs::absolute("bios").string();
				else if (fs::exists("app/bios", ec))
					sys_dir = fs::absolute("app/bios").string();
				else
					sys_dir = fs::absolute("bios").string();
			}

			// fMSX (msx.dll) retro_load_game runs a strrchr for '/' and '\\' on the
			// system directory buffer and zeroes the last slash, assuming the frontend
			// provided a trailing slash (e.g. "bios/"). Without a trailing slash, fMSX
			// cuts off the final directory component (".../app/bios" -> ".../app"),
			// causing StartMSX() to search for MSX.ROM, MSX2.ROM, MSX2EXT.ROM in "app"
			// instead of "app/bios", failing to load any BIOS and causing a black screen.
			if (s_loaded_core_path.find("msx.dll") != std::string::npos)
			{
				static std::string msx_sys_dir;
				if (msx_sys_dir.empty())
				{
					msx_sys_dir = sys_dir;
					if (!msx_sys_dir.empty() && msx_sys_dir.back() != '\\' && msx_sys_dir.back() != '/')
						msx_sys_dir += "\\";
				}
				*dir = msx_sys_dir.c_str();
				return true;
			}

			*dir = sys_dir.c_str();
			return true;
		}
		return false;
	}

	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
	{
		const char** dir = (const char**)data;
		if (dir)
		{
			// Not static: cores query this during retro_load_game, before
			// loaded_system_dir is known, and a static would freeze the empty
			// value for the rest of the session.
			static std::string save_dir;
			save_dir = fs::absolute("saves/" + loaded_system_dir).string();
			fs::create_directories(save_dir);
			*dir = save_dir.c_str();
			return true;
		}
		return false;
	}

	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
	{
		// Saturn and PSX change timing mid-game. Refusing this left the core
		// thread pacing at the old rate and the audio resampler at the old
		// sample rate.
		const struct retro_system_av_info* av = (const struct retro_system_av_info*)data;
		if (!av) return false;

		if (av->timing.fps > 1.0 && av->timing.fps < 1000.0)
			core_native_fps = av->timing.fps;
		if (av->timing.sample_rate > 1000.0)
			core_native_sample_rate = av->timing.sample_rate;

		ApplyDisplaySync();
		return true;
	}

	case RETRO_ENVIRONMENT_SET_GEOMETRY:
		// Accepted: the real frame size arrives with every video_refresh, and
		// the aspect is chosen by the user in the OSD.
		return (data != NULL);

	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
	{
		// Went unanswered before, so an option changed in the OSD only took
		// effect on the next load. Cores poll this and re-read when it is true.
		bool* updated = (bool*)data;
		if (updated) *updated = (InterlockedExchange(&g_options_dirty, 0) != 0);
		return true;
	}

	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
	{
		bool* can_dupe = (bool*)data;
		if (can_dupe) *can_dupe = true;
		return true;
	}

	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		struct retro_variable* var = (struct retro_variable*)data;
		if (var && var->key)
		{
			EnterCriticalSection(&options_lock);
			auto it = g_core_options.find(var->key);
			const char* user_value = (it != g_core_options.end()) ? StableOptionValue(it->second) : NULL;
			LeaveCriticalSection(&options_lock);

			// A Vulkan choice for pcsx2_renderer saved before this was known
			// (Settings > Core Options lists every value the core declares,
			// Vulkan included) would otherwise be honored here, ahead of the
			// safe-default logic below that keeps Vulkan out of the running for
			// this specific option - see the long comment on that branch for
			// why. Treated as if nothing were saved so it falls through to it.
			if (user_value && strcmp(var->key, "pcsx2_renderer") == 0 && strcmp(user_value, "Vulkan") == 0)
				user_value = NULL;

			if (user_value)
			{
				var->value = user_value;
				return true;
			}

			// Defaults we explicitly override. Values below are the exact enum
			// strings accepted by each core - a value the core does not
			// recognise leaves it in an undefined state (a wrong
			// geolith_system_type is what blanked the Neo Geo CD screen).
			//
			// Geolith 0.4.1 accepts:
			//   geolith_system_type    : aes | mvs | uni
			//   geolith_cd_system_type : cd_front | cd_top | cdz | cdz_unibios
			if (strcmp(var->key, "geolith_system_type") == 0) var->value = "aes";
			else if (strcmp(var->key, "geolith_cd_system_type") == 0) var->value = "cdz";
			// These two are NOT equivalent. cd_speed_hack only patches BIOS
			// busy-wait loops and is Geolith's own default - without it a Neo
			// Geo CD loads at true 1x, which is a minute of black screen with
			// the CD audio playing. cd_skip_loading fast-forwards without
			// emitting video at all, so that one stays off.
			else if (strcmp(var->key, "geolith_cd_speed_hack") == 0) var->value = "enabled";
			else if (strcmp(var->key, "geolith_cd_skip_loading") == 0) var->value = "disabled";
			else if (strcmp(var->key, "n64_ram_size") == 0) var->value = "8MByte";
			else if (strcmp(var->key, "parallel-n64-cpucore") == 0) var->value = "pure_interpreter";

			// The graphics plugin has to match the render mode. A GL plugin on
			// a machine with no context hangs the core, and a software
			// rasteriser when GL is available throws the GPU away and runs at
			// a crawl - that is what made angrylion look like a freeze.
			// HwGlProbed() is a cached flag decided once at startup. Calling
			// HwIsAvailable() here would create a window from inside a core
			// callback, on the core thread, in a path cores hit every frame -
			// a thread that owns a window and never pumps messages is asking
			// for a stall.
			else if (strcmp(var->key, "parallel-n64-gfxplugin") == 0)
				var->value = (MenuGetHwRender() && HwGlProbed()) ? "glide64" : "angrylion";
			else if (strcmp(var->key, "parallel-n64-rspplugin") == 0)
				var->value = (MenuGetHwRender() && HwGlProbed()) ? "hle" : "cxd4";
			else if (strcmp(var->key, "mupen64plus-rdp-plugin") == 0)
				var->value = (MenuGetHwRender() && HwGlProbed()) ? "gliden64" : "angrylion";
			else if (strcmp(var->key, "mupen64plus-rsp-plugin") == 0)
				var->value = (MenuGetHwRender() && HwGlProbed()) ? "hle" : "cxd4";

			// Play! PS2 core options (v0.77+).
			else if (strcmp(var->key, "play_bilinear_filtering") == 0) var->value = "true";
			else if (strcmp(var->key, "play_presentation_mode") == 0) var->value = "Fit Screen";
			else if (strcmp(var->key, "play_res_multi") == 0) var->value = "1x";
			else if (strcmp(var->key, "play_limit_framerate") == 0) var->value = "enabled";
			else if (strcmp(var->key, "play_fastboot") == 0) var->value = "enabled";
			else if (strcmp(var->key, "play_widescreen_hack") == 0) var->value = "disabled";

			// PCSX2 / LRPS2 core options
			else if (strcmp(var->key, "pcsx2_bios") == 0)
			{
				static std::string s_pcsx2_bios;
				if (s_pcsx2_bios.empty())
				{
					s_pcsx2_bios = ResolveAndPreparePs2Bios();
				}
				var->value = s_pcsx2_bios.c_str();
			}
			else if (strcmp(var->key, "pcsx2_fastboot") == 0) var->value = "enabled";
			else if (strcmp(var->key, "pcsx2_renderer") == 0)
			{
				int drv = MenuGetVideoDriver();
				// Vulkan fica de fora da lista deste core, de proposito - nao e
				// falta de suporte a negociacao (essa existe, ver
				// hw_render_vulkan.cpp), e um travamento medido dentro do
				// proprio ps2.dll. Fechar um jogo de PS2 com Vulkan forcado
				// prende o processo inteiro: confirmado com cdb (thread
				// principal presa girando dentro do ps2.dll, endereco avancando
				// devagar - um busy-wait, nao um lock nosso) e reproduzido
				// tanto na thread original (watchdog de 3s mata via
				// TerminateThread) quanto na segunda tentativa que
				// RecoverAfterKilledCore faz a partir da thread principal, que
				// trava exatamente no mesmo lugar - sem watchdog para essa
				// segunda vez. Nao ha nada do lado do frontend para
				// timeoutar sem arriscar corromper o processo (a mesma
				// classe de risco documentada em CoreRecoverLocksHeldByThread).
				// PPSSPP usa a mesma negociacao (VkHwContextDestroy em
				// hw_render_vulkan.cpp) sem esse problema - e especifico do
				// proprio PCSX2/LRPS2, nao da negociacao em si.
				if (drv == 3 && D3D11HwIsAvailable()) var->value = "Direct3D11";
				else if (drv == 1 && HwGlProbed()) var->value = "OpenGL";
				else if (drv == 4) var->value = "Software";
				else var->value = (MenuGetHwRender() && HwGlProbed()) ? "OpenGL" : "Auto";
			}
			else if (strcmp(var->key, "pcsx2_fastmem") == 0) var->value = "enabled";
			else if (strcmp(var->key, "pcsx2_mtvu") == 0) var->value = "enabled";
			else if (strcmp(var->key, "pcsx2_instant_vu1") == 0) var->value = "enabled";

			// PPSSPP core options
			else if (strcmp(var->key, "ppsspp_fast_memory") == 0) var->value = "disabled";
			else if (strcmp(var->key, "ppsspp_ignore_bad_memory_access") == 0) var->value = "enabled";
			else if (strcmp(var->key, "ppsspp_io_timing_method") == 0) var->value = "Simulate UMD delays";
			else if (strcmp(var->key, "ppsspp_skip_buffer_effects") == 0) var->value = "disabled";
			else if (strcmp(var->key, "ppsspp_skip_gpu_readbacks") == 0) var->value = "disabled";
			else if (strcmp(var->key, "ppsspp_backend") == 0)
			{
				int drv = MenuGetVideoDriver();
				if (drv == 2 && VkHwIsAvailable()) var->value = "vulkan";
				else if (drv == 3 && D3D11HwIsAvailable()) var->value = "direct3d11";
				else if (drv == 1 && HwGlProbed()) var->value = "opengl";
				else var->value = (MenuGetHwRender() && HwGlProbed()) ? "opengl" : "auto";
			}
			else if (strcmp(var->key, "ppsspp_auto_frameskip") == 0) var->value = "disabled";
			else if (strcmp(var->key, "ppsspp_frameskip") == 0) var->value = "disabled";

			// If no explicit override matched: hand back what the core itself declared as default.
			else
			{
				EnterCriticalSection(&options_lock);
				auto d = g_core_defaults.find(var->key);
				const char* default_value = (d != g_core_defaults.end()) ? StableOptionValue(d->second) : NULL;
				LeaveCriticalSection(&options_lock);

				if (default_value)
				{
					var->value = default_value;
					return true;
				}
				return false;
			}
			return true;
		}
		return false;
	}

	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
	{
		// Went unhandled, so RetroAchievements had to guess the layout from
		// RETRO_MEMORY_SYSTEM_RAM alone. On a console whose achievement address
		// space spans more than one region - Neo Geo CD has System RAM and a
		// Reserved RAM block above it - everything past the first region came
		// out "Invalid address" and those achievements were disabled.
		const struct retro_memory_map* mm = (const struct retro_memory_map*)data;
		if (!mm || !mm->descriptors || mm->num_descriptors == 0) return false;

		CoreReleaseMemoryMap();

		g_memory_descriptors = (struct retro_memory_descriptor*)calloc(
			mm->num_descriptors, sizeof(struct retro_memory_descriptor));
		if (!g_memory_descriptors) return false;

		// Deep copy: the core may hand us a stack temporary.
		memcpy(g_memory_descriptors, mm->descriptors,
			mm->num_descriptors * sizeof(struct retro_memory_descriptor));

		g_memory_map.descriptors = g_memory_descriptors;
		g_memory_map.num_descriptors = mm->num_descriptors;
		g_has_memory_map = true;

		CoreLogPrintf(RETRO_LOG_INFO, "[MEM] mapa recebido: %u descritores",
			mm->num_descriptors);
		return true;
	}

	case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
	{
		// Por esta interface o core escolhe o proprio dispositivo Vulkan: GPU,
		// extensoes, features e filas. Ela era recusada, e o efeito era grande:
		// sem poder negociar, PPSSPP, Flycast e PCSX2 recebiam um dispositivo
		// generico e falhavam dentro do proprio renderizador - o Flycast com GPF
		// de ponteiro nulo -, entao o frontend passou a recusar Vulkan para eles
		// e os tres cairam para OpenGL. Implementada a negociacao, a recusa
		// deixou de fazer sentido.
		// O libretro.h vendorizado aqui nao traz a struct base, so o enum de
		// tipo; a versao Vulkan dela mora em libretro_vulkan.h, que arrasta
		// vulkan.h junto e nao tem por que entrar neste arquivo. Os dois
		// primeiros campos sao o prefixo comum a toda interface de negociacao,
		// entao le-los por este espelho e valido e mantem a dependencia de
		// Vulkan confinada em hw_render_vulkan.cpp.
		struct negotiation_header
		{
			enum retro_hw_render_context_negotiation_interface_type interface_type;
			unsigned interface_version;
		};
		const struct negotiation_header* iface = (const struct negotiation_header*)data;
		if (!iface)
			return false;
		if (iface->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
		{
			CoreLogPrintf(RETRO_LOG_INFO,
				"[HW] negociacao de contexto tipo %d nao suportada (so Vulkan)",
				(int)iface->interface_type);
			return false;
		}

		VkHwSetNegotiationInterface(iface);
		g_core_wants_vk_negotiation = true;
		CoreLogPrintf(RETRO_LOG_INFO,
			"[HW] negociacao de contexto Vulkan aceita (interface v%u)",
			iface->interface_version);
		return true;
	}
	case RETRO_ENVIRONMENT_SET_HW_RENDER:
		// Accepting this is what lets mupen64plus-next, flycast and dolphin
		// load at all - they refuse outright without OpenGL. But saying yes
		// also takes away the software fallback every core used before, so a
		// problem on our side becomes their crash. It stays opt-in.
		if (!MenuGetHwRender())
		{
			CoreLogPrintf(RETRO_LOG_INFO,
				"[HW] core pediu render por hardware; recusado "
				"(3D Acceleration esta Off em Settings > Video)");
			return false;
		}
		{
			struct retro_hw_render_callback* cb = (struct retro_hw_render_callback*)data;
			if (cb && cb->context_type == RETRO_HW_CONTEXT_D3D11)
				return D3D11HwSetRenderCallback(cb);
			if (cb && cb->context_type == RETRO_HW_CONTEXT_VULKAN)
			{
				// A recusa que existia aqui era o contorno para a negociacao
				// nao existir. Agora ela existe, e aceitar e o comportamento
				// correto: o dispositivo so e criado quando o jogo carrega, ja
				// com o que o core pediu. Se a negociacao falhar naquele ponto,
				// VkHwEnsureDevice cai sozinho no dispositivo padrao, e se nem
				// esse servir o carregamento falha de forma limpa em vez de
				// crashar dentro do core.
				return VkHwSetRenderCallback(cb);
			}
			return HwSetRenderCallback(cb);
		}

	case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
	{
		// Handshake companion to SET_HW_RENDER above: called (usually right
		// after context_reset) once the core wants the actual D3D11/Vulkan
		// handles - device/context/set_image/etc - rather than just having
		// been told "yes, hardware rendering is available". Only meaningful
		// once that context actually exists; the OpenGL path has no
		// equivalent (get_proc_address and get_current_framebuffer are
		// handed to the core directly on its own retro_hw_render_callback
		// instead). D3D11HwGetRenderInterface/VkHwGetRenderInterface each
		// return NULL when their own backend isn't the active one.
		const void* iface = D3D11HwGetRenderInterface();
		if (!iface) iface = VkHwGetRenderInterface();
		if (!iface) return false;
		*(const void**)data = iface;
		return true;
	}

	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
	{
		// We implement the original interface only: SET_VARIABLES plus
		// GET_VARIABLE. Leaving this unanswered meant the core read version 0
		// by omission while we were separately claiming its v2 options had
		// been accepted - a contradiction that told the core its options were
		// registered when we had parsed none of them.
		unsigned* version = (unsigned*)data;
		if (version) *version = 0;
		return true;
	}

	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	{
		// The core is telling us what each control is called on this system.
		// Ignoring it is why every core showed the same generic RetroPad
		// names. Port 0 only, which is what the Controller page edits.
		const struct retro_input_descriptor* desc =
			(const struct retro_input_descriptor*)data;
		if (!desc) return true;
		ClearInputDescriptors();
		for (; desc->description; desc++)
		{
			if (desc->port != 0) continue;
			if (desc->device == RETRO_DEVICE_JOYPAD && desc->id < 16)
			{
				strncpy(g_desc_button[desc->id], desc->description,
					sizeof(g_desc_button[0]) - 1);
				g_desc_present = true;
			}
			else if (desc->device == RETRO_DEVICE_ANALOG &&
			         desc->index < 2 && desc->id < 2)
			{
				strncpy(g_desc_axis[desc->index][desc->id], desc->description,
					sizeof(g_desc_axis[0][0]) - 1);
				g_desc_present = true;
			}
		}
		return true;
	}

	case RETRO_ENVIRONMENT_SET_VARIABLES:
	{
		// Cores declare their options here, each value shaped as
		//     "Description; default|alt1|alt2"
		// and the frontend is expected to remember those defaults and serve
		// them back from GET_VARIABLE. We used to answer true and read none of
		// it, so every GET_VARIABLE came back empty and cores ran with no
		// configuration at all - Beetle PSX produced no video whatsoever.
		const struct retro_variable* vars = (const struct retro_variable*)data;
		if (!vars) return true;

		static ParsedCoreVar s_parsed[512];
		int count = 0;
		if (!ParseCoreVariablesGuarded(vars, s_parsed, 512, &count))
		{
			CoreLogPrintf(RETRO_LOG_ERROR,
				"[OPT] core forneceu SET_VARIABLES malformado - ignorado");
			return true;
		}

		EnterCriticalSection(&options_lock);
		g_core_defaults.clear();
		g_core_defaults_order.clear();
		g_core_option_raw.clear();
		for (int i = 0; i < count; i++)
		{
			// A core is free to declare the same key twice (rare, but seen) -
			// only record it in the order list once, at its first appearance,
			// so the Core Options menu does not list it twice.
			if (g_core_defaults.find(s_parsed[i].key) == g_core_defaults.end())
				g_core_defaults_order.push_back(s_parsed[i].key);
			g_core_defaults[s_parsed[i].key] = s_parsed[i].value;
			g_core_option_raw[s_parsed[i].key] = s_parsed[i].raw;
		}
		size_t n = g_core_defaults.size();
		LeaveCriticalSection(&options_lock);

		// The generic Core Options menu page persists a player's choice to
		// karamelo.cfg (see menu.cpp's g_persisted_core_options); re-apply
		// any match now, the same way ApplyPersistedCoreOptions() already does
		// for the older, hand-picked per-core settings - without this a chosen
		// Internal Resolution (or any other generic option) silently reverted
		// to the core's own default on every fresh launch, which is exactly
		// the bug that function's own comment was written to avoid for the
		// settings it already covers. Skips a key the user already set this
		// session (g_core_options), so a core that re-declares SET_VARIABLES
		// mid-session cannot clobber a choice just made with a stale disk value.
		for (int i = 0; i < count; i++)
		{
			EnterCriticalSection(&options_lock);
			bool already_set = g_core_options.find(s_parsed[i].key) != g_core_options.end();
			LeaveCriticalSection(&options_lock);
			if (already_set) continue;

			const char* persisted = MenuGetPersistedCoreOption(s_parsed[i].key);
			if (persisted && persisted[0])
				CoreSetOption(s_parsed[i].key, persisted);
		}

		for (int i = 0; i < count; i++)
			CoreLogPrintf(RETRO_LOG_DEBUG, "[OPT] opcao: %s = %s", s_parsed[i].key, s_parsed[i].value);

		CoreLogPrintf(RETRO_LOG_INFO, "[OPT] %u opcoes declaradas pelo core", (unsigned)n);
		return true;
	}

	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
		// Answering true here was a lie. A core told its v2 options were
		// registered skips the legacy SET_VARIABLES path, and then every
		// GET_VARIABLE comes back empty - it ends up with no option values at
		// all. Refusing makes it fall back to the interface we do implement.
		return false;

	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;

	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
	{
		int* mask = (int*)data;
		if (mask) *mask = 1 | 2; // Bit 0: Video, Bit 1: Audio
		return true;
	}

	case RETRO_ENVIRONMENT_SHUTDOWN:
		// Raised from inside retro_run. Unloading here would FreeLibrary the
		// DLL we are currently executing, so only ask the loop to stop.
		InterlockedExchange(&core_thread_running, 0);
		return true;

	default:
		return false;
	}
}

static double g_render_us_total = 0.0;
static long   g_render_count = 0;

static void CB_VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
	// Once a second: how many frames the core actually produced, and how much
	// of each was spent in our own filter.
	{
		static LARGE_INTEGER last = { 0 };
		static long frames = 0;
		LARGE_INTEGER now, freq;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&freq);
		frames++;
		if (last.QuadPart == 0) last = now;
		double secs = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
		if (secs >= 1.0)
		{
			double avg_us = g_render_count ? (g_render_us_total / g_render_count) : 0.0;
			LONG wp = InterlockedCompareExchange(&g_ring_write_pos, 0, 0);
			LONG rp = InterlockedCompareExchange(&g_ring_read_pos, 0, 0);
			LONG occ = (wp >= rp) ? (wp - rp) : (RING_BUFFER_SIZE - rp + wp);
			LONG underruns = InterlockedExchange(&g_audio_underrun_count, 0);
			CoreLogPrintf(RETRO_LOG_INFO,
				"[PERF] core=%.1f fps  filtro=%.2f ms/quadro  alvo=%.1f  buf=%ld  underrun=%ld",
				frames / secs, avg_us / 1000.0, core_target_fps, (long)occ, (long)underruns);
			frames = 0; last = now;
			g_render_us_total = 0.0; g_render_count = 0;
		}
	}

	if (width == 0 || height == 0) return;

	// RETRO_HW_FRAME_BUFFER_VALID means "the frame is on the GPU, in the FBO I
	// handed you". This used to be discarded along with NULL, which is why a
	// hardware-rendered core could never show anything.
	const bool from_gpu = (data == RETRO_HW_FRAME_BUFFER_VALID);
	if (!from_gpu && (!data || data == (void*)1)) return;

	if (width > MAX_FB_WIDTH) width = MAX_FB_WIDTH;
	if (height > MAX_FB_HEIGHT) height = MAX_FB_HEIGHT;

	core_fb_width = width;
	core_fb_height = height;

	uint32_t* dst_fb = g_triple_fb[g_fb_back];

	if (from_gpu)
	{
		// One copy GPU -> CPU per frame. That is the deliberate trade: the CRT
		// filters, the OSD and the blit all stay on the software path instead
		// of being rewritten as shaders. Only one of these three is ever active
		// for a given loaded core - dispatch on whichever it is.
		bool hw_ok = D3D11HwIsActive() ? D3D11HwReadPixels(dst_fb, width, height)
		           : VkHwIsActive()    ? VkHwReadPixels(dst_fb, width, height)
		                               : HwReadPixels(dst_fb, width, height);
		if (!hw_ok) return;

		// 480i / 448i Motion-Adaptive Deinterlacing for PS2 (Play! core only).
		// PCSX2 handles hardware deinterlacing natively in its OpenGL GS pipeline.
		if (!s_loaded_core_is_pcsx2 && height == 448 && width >= 512)
		{
			if (g_deinterlace_have_prev)
			{
				for (unsigned row = 0; row < height; row++)
				{
					bool is_alt_row = ((row & 1) != (g_deinterlace_field & 1));
					if (is_alt_row)
					{
						uint32_t* cur_row = dst_fb + row * width;
						const uint32_t* prev_row = g_deinterlace_prev + row * width;
						for (unsigned x = 0; x < width; x++)
						{
							uint32_t c_cur = cur_row[x];
							uint32_t c_prev = prev_row[x];
							uint32_t rb = (((c_cur & 0x00FF00FF) + (c_prev & 0x00FF00FF)) >> 1) & 0x00FF00FF;
							uint32_t g  = (((c_cur & 0x0000FF00) + (c_prev & 0x0000FF00)) >> 1) & 0x0000FF00;
							cur_row[x] = rb | g;
						}
					}
				}
			}
			memcpy(g_deinterlace_prev, dst_fb, width * height * sizeof(uint32_t));
			g_deinterlace_have_prev = true;
			g_deinterlace_field ^= 1;
		}
	}
	else if (core_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
	{
		const uint8_t* src_row = (const uint8_t*)data;
		for (unsigned y = 0; y < height; y++)
		{
			const uint32_t* src_pixels = (const uint32_t*)src_row;
			for (unsigned x = 0; x < width; x++)
			{
				dst_fb[y * width + x] = src_pixels[x];
			}
			src_row += pitch;
		}
	}
	else if (core_pixel_format == RETRO_PIXEL_FORMAT_RGB565)
	{
		const uint8_t* src_row = (const uint8_t*)data;
		for (unsigned y = 0; y < height; y++)
		{
			const uint16_t* src_pixels = (const uint16_t*)src_row;
			for (unsigned x = 0; x < width; x++)
			{
				uint16_t p = src_pixels[x];
				uint32_t r = ((p >> 11) & 0x1F) * 255 / 31;
				uint32_t g = ((p >> 5) & 0x3F) * 255 / 63;
				uint32_t b = (p & 0x1F) * 255 / 31;
				dst_fb[y * width + x] = (r << 16) | (g << 8) | b;
			}
			src_row += pitch;
		}
	}
	else if (core_pixel_format == RETRO_PIXEL_FORMAT_0RGB1555)
	{
		const uint8_t* src_row = (const uint8_t*)data;
		for (unsigned y = 0; y < height; y++)
		{
			const uint16_t* src_pixels = (const uint16_t*)src_row;
			for (unsigned x = 0; x < width; x++)
			{
				uint16_t p = src_pixels[x];
				uint32_t r = ((p >> 10) & 0x1F) * 255 / 31;
				uint32_t g = ((p >> 5) & 0x1F) * 255 / 31;
				uint32_t b = (p & 0x1F) * 255 / 31;
				dst_fb[y * width + x] = (r << 16) | (g << 8) | b;
			}
			src_row += pitch;
		}
	}

	// Video diagnostic: tells us whether the core is emitting real pixels at
	// all, which separates "the core is drawing a black loading screen" from
	// "our presentation path is broken".
	// Kept to the first frames of a load only: it costs nothing there and tells
	// us instantly whether a core is emitting real pixels. Logging it forever
	// would just bloat the log.
	static int nonblack_logged = 0;
	uint32_t acc = 0;
	if (g_video_diag_frames < 300 && nonblack_logged < 5)
	{
		for (unsigned yy = 0; yy < height; yy += 4)
			for (unsigned xx = 0; xx < width; xx += 4)
				acc |= dst_fb[yy * width + xx] & 0x00FFFFFFu;

		if (g_video_diag_frames < 5 || acc)
		{
			if (acc) nonblack_logged++;
			CoreLogPrintf(RETRO_LOG_INFO, "[VIDEO] f=%ld %ux%u pitch=%u fmt=%d nonblack=%s",
				g_video_diag_frames, width, height, (unsigned)pitch,
				(int)core_pixel_format, acc ? "SIM" : "nao");
		}
	}
	g_video_diag_frames++;

	// Publish: hand this buffer over and take whatever was waiting. The
	// geometry travels with the buffer, so a mid-game resolution change cannot
	// leave the consumer reading one frame with another frame's stride.
	g_fb_dim_w[g_fb_back] = width;
	g_fb_dim_h[g_fb_back] = height;

	g_fb_back = (int)InterlockedExchange(&g_fb_ready, (LONG)g_fb_back);
	InterlockedExchange(&g_fb_dirty, 1);
}

static void CB_AudioSample(int16_t left, int16_t right)
{
	int16_t frame[2] = { left, right };
	SendAudioSamples(frame, 1);
}

static size_t CB_AudioSampleBatch(const int16_t* data, size_t frames)
{
	SendAudioSamples(data, frames);
	return frames;
}

static void CB_InputPoll(void)
{
	// Must clear buttons every frame, otherwise any pressed button remains stuck
	memset(joypad_buttons, 0, sizeof(joypad_buttons));
	memset(analog_sticks, 0, sizeof(analog_sticks));

	// With the OSD open the player is driving the menu, not the game. Without
	// this gate every arrow key and Enter used to navigate also reached the
	// core, so browsing the menu made the game move underneath it.
	if (OsdIsEnabled()) return;

	// GetAsyncKeyState and XInput read global state and do not care which window
	// has focus, so alt-tabbing to a browser and typing would press buttons in
	// the game.
	//
	// This deliberately fails OPEN. GetForegroundWindow returns NULL during
	// focus transitions, while a UAC prompt is up, or when the foreground
	// window belongs to another desktop - and the earlier version treated all
	// of those as "not us" and silently swallowed every button. Losing input
	// with no way to tell why is far worse than a stray keypress, so we only
	// block when another process is positively known to hold focus.
	HWND fg = GetForegroundWindow();
	if (fg)
	{
		DWORD fg_pid = 0;
		GetWindowThreadProcessId(fg, &fg_pid);
		if (fg_pid != 0 && fg_pid != GetCurrentProcessId()) return;
	}

	// Analog deadzone, read once rather than per pad.
	const int deadzone_thresh = (MenuGetDeadzone() * 32768) / 100;

	// See SystemHasIndependentAnalogStick: on these systems the D-Pad and the
	// stick are separate controls, so the stick must NOT also punch the D-Pad.
	const bool indep_analog = SystemHasIndependentAnalogStick(s_loaded_core_path);

	// 1. Direct JIT Hardware Polling via SDL_Gamepad (dev-sdl3: was XInput)
	for (DWORD pad = 0; pad < 4; pad++)
	{
		XINPUT_STATE state;
		if (!GamepadGetState((int)pad, &state)) continue;

		WORD w = state.Gamepad.wButtons;
		if (state.Gamepad.bLeftTrigger > 50) w |= PAD_BTN_LT;
		if (state.Gamepad.bRightTrigger > 50) w |= PAD_BTN_RT;

		int16_t* jb = joypad_buttons[pad];

		// Configurable bindings for the 12 core RetroPad buttons
		for (int bind = 0; bind < BIND_COUNT; bind++)
		{
			int code = InputBindGetPad(bind);
			if (code <= 0 || !(w & code)) continue;

			if (InputBindIsAnalog(bind))
			{
				// Only reached if a player put a stick direction on a button;
				// the physical sticks are read further down.
				int stick = InputBindAnalogStick(bind);
				int axis  = InputBindAnalogAxis(bind);
				int val   = analog_sticks[pad][stick][axis]
				          + InputBindAnalogSign(bind) * 32767;
				if (val < -32767) val = -32767; else if (val > 32767) val = 32767;
				analog_sticks[pad][stick][axis] = (int16_t)val;
			}
			else
			{
				int rid = InputBindRetroId(bind);
				if (rid >= 0 && rid < 16) jb[rid] = 1;
			}
		}

		if (abs(state.Gamepad.sThumbLX) > deadzone_thresh)
		{
			analog_sticks[pad][0][0] = state.Gamepad.sThumbLX;
			if (!indep_analog)
			{
				if (state.Gamepad.sThumbLX > deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_RIGHT] = 1;
				else if (state.Gamepad.sThumbLX < -deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_LEFT] = 1;
			}
		}
		if (abs(state.Gamepad.sThumbLY) > deadzone_thresh)
		{
			// Negating -32768 overflows int16 straight back to -32768, so a
			// stick held fully down would read as fully up for that sample.
			analog_sticks[pad][0][1] = (state.Gamepad.sThumbLY == -32768)
				? 32767 : (int16_t)(-state.Gamepad.sThumbLY);
			if (!indep_analog)
			{
				if (state.Gamepad.sThumbLY > deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_UP] = 1;
				else if (state.Gamepad.sThumbLY < -deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_DOWN] = 1;
			}
		}
		if (abs(state.Gamepad.sThumbRX) > deadzone_thresh) analog_sticks[pad][1][0] = state.Gamepad.sThumbRX;
		if (abs(state.Gamepad.sThumbRY) > deadzone_thresh)
			analog_sticks[pad][1][1] = (state.Gamepad.sThumbRY == -32768)
				? 32767 : (int16_t)(-state.Gamepad.sThumbRY);
	}

	// 2. Keyboard (P1), read from the bindings table so the keys can be
	// changed in Settings > Controller instead of being compiled in.
	for (int bind = 0; bind < BIND_COUNT; bind++)
	{
		int vk = InputBindGetKey(bind);
		if (vk <= 0) continue;
		if (!(GetAsyncKeyState(vk) & 0x8000)) continue;

		if (InputBindIsAnalog(bind))
		{
			// A key is all-or-nothing, so it deflects the axis fully. Held
			// together, an opposing pair cancels rather than fighting, which
			// is what a real stick does when centred.
			int stick = InputBindAnalogStick(bind);
			int axis  = InputBindAnalogAxis(bind);
			int sign  = InputBindAnalogSign(bind);
			int cur   = analog_sticks[0][stick][axis];
			int val   = cur + sign * 32767;
			if (val < -32767) val = -32767; else if (val > 32767) val = 32767;
			analog_sticks[0][stick][axis] = (int16_t)val;
		}
		else
		{
			// Same collision as the gamepad stick above (see
			// SystemHasIndependentAnalogStick), reached from the keyboard side:
			// the default layout puts BIND_UP/DOWN/LEFT/RIGHT on the same arrow
			// keys as BIND_LSTICK_UP/DOWN/LEFT/RIGHT (input_map.cpp), on purpose,
			// so a keyboard can still walk in a stick-only game. On a system
			// where the D-Pad is its own control, firing it from that same key
			// also does whatever the game binds the D-Pad to on every step.
			// Only suppressed while the keys still actually coincide - rebinding
			// either one apart (Settings > Controller) restores independent
			// D-Pad control from the keyboard.
			if (indep_analog && bind >= BIND_UP && bind <= BIND_RIGHT &&
			    vk == InputBindGetKey(BIND_LSTICK_UP + (bind - BIND_UP)))
				continue;

			int rid = InputBindRetroId(bind);
			if (rid >= 0 && rid < 16) joypad_buttons[0][rid] = 1;
		}
	}

	// For MSX computer cores: also route Space to Joypad A/B so games reading
	// either the MSX joystick or MSX keyboard respond naturally to Space and Enter.
	if (s_loaded_core_path.find("msx.dll") != std::string::npos)
	{
		if (GetAsyncKeyState(VK_SPACE) & 0x8000)
		{
			joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A] = 1;
			joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] = 1;
		}
		if (GetAsyncKeyState(VK_RETURN) & 0x8000)
		{
			joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_START] = 1;
		}
	}

	// 3. Netplay: ship this frame's local input and take the peer's as P2.
	//
	// This call did not exist anywhere in the project. Sessions connected, said
	// "Player 2 connected", and then exchanged nothing - the remote player's
	// input never reached the core. Here is the only place with both the local
	// input already assembled and the frame not yet run.
	if (NetplayGetState() == NETPLAY_CONNECTED)
	{
		NetplaySyncInputs(joypad_buttons[0], analog_sticks[0],
		                  joypad_buttons[1], analog_sticks[1]);
	}
}

// Where the game image actually sits inside the presented buffer. The mouse
// arrives in window coordinates and has to be mapped through this to reach a
// framebuffer position, so the touch lands where the cursor is regardless of
// aspect ratio, letterboxing or window size.
static volatile LONG g_vp_x = 0, g_vp_y = 0, g_vp_w = 0, g_vp_h = 0;
static volatile LONG g_vp_dest_w = 0, g_vp_dest_h = 0;

// Mouse-as-stylus. Written by the presentation thread, read by the core
// thread, so x and y travel packed in one word - reading them separately
// could pair the x of one frame with the y of the next.
static volatile LONG g_pointer_xy = 0;
static volatile LONG g_pointer_down = 0;

void CoreSetPointer(double client_fx, double client_fy, bool pressed)
{
	// The client area is a straight stretch of the whole presented buffer, so
	// a fraction of one is the same fraction of the other.
	const double vpx = (double)g_vp_x, vpy = (double)g_vp_y;
	const double vpw = (double)g_vp_w, vph = (double)g_vp_h;
	const double dw = (double)g_vp_dest_w, dh = (double)g_vp_dest_h;

	if (vpw <= 0.0 || vph <= 0.0 || dw <= 0.0 || dh <= 0.0)
	{
		InterlockedExchange(&g_pointer_down, 0);
		return;
	}

	double u = (client_fx * dw - vpx) / vpw;
	double v = (client_fy * dh - vpy) / vph;

	// Outside the picture is not a touch. Without this the pillarbox counted
	// as the edge of the screen and dragged the stylus there.
	if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
	{
		InterlockedExchange(&g_pointer_down, 0);
		return;
	}

	// libretro pointer space runs -0x7fff (left/top) to 0x7fff (right/bottom).
	int px = (int)(u * 65534.0) - 32767;
	int py = (int)(v * 65534.0) - 32767;
	if (px < -32767) px = -32767; else if (px > 32767) px = 32767;
	if (py < -32767) py = -32767; else if (py > 32767) py = 32767;

	InterlockedExchange(&g_pointer_xy, (LONG)(((uint32_t)(px & 0xFFFF) << 16) | (uint32_t)(py & 0xFFFF)));
	InterlockedExchange(&g_pointer_down, pressed ? 1 : 0);
}

static int RetroKeyToVK(unsigned id)
{
	if (id >= 'a' && id <= 'z') return 'A' + (id - 'a');
	if (id >= '0' && id <= '9') return '0' + (id - '0');
	switch (id)
	{
	case 8:   return VK_BACK;      // RETROK_BACKSPACE
	case 9:   return VK_TAB;       // RETROK_TAB
	case 12:  return VK_CLEAR;     // RETROK_CLEAR
	case 13:  return VK_RETURN;    // RETROK_RETURN
	case 19:  return VK_PAUSE;     // RETROK_PAUSE
	case 27:  return VK_ESCAPE;    // RETROK_ESCAPE
	case 32:  return VK_SPACE;     // RETROK_SPACE
	case 39:  return VK_OEM_7;     // RETROK_QUOTE (')
	case 44:  return VK_OEM_COMMA; // RETROK_COMMA (,)
	case 45:  return VK_OEM_MINUS; // RETROK_MINUS (-)
	case 46:  return VK_OEM_PERIOD;// RETROK_PERIOD (.)
	case 47:  return VK_OEM_2;     // RETROK_SLASH (/)
	case 59:  return VK_OEM_1;     // RETROK_SEMICOLON (;)
	case 61:  return VK_OEM_PLUS;  // RETROK_EQUALS (=)
	case 91:  return VK_OEM_4;     // RETROK_LEFTBRACKET ([)
	case 92:  return VK_OEM_5;     // RETROK_BACKSLASH (\)
	case 93:  return VK_OEM_6;     // RETROK_RIGHTBRACKET (])
	case 96:  return VK_OEM_3;     // RETROK_BACKQUOTE (`)
	case 127: return VK_DELETE;    // RETROK_DELETE

	// Keypad
	case 256: return VK_NUMPAD0;
	case 257: return VK_NUMPAD1;
	case 258: return VK_NUMPAD2;
	case 259: return VK_NUMPAD3;
	case 260: return VK_NUMPAD4;
	case 261: return VK_NUMPAD5;
	case 262: return VK_NUMPAD6;
	case 263: return VK_NUMPAD7;
	case 264: return VK_NUMPAD8;
	case 265: return VK_NUMPAD9;
	case 266: return VK_DECIMAL;
	case 267: return VK_DIVIDE;
	case 268: return VK_MULTIPLY;
	case 269: return VK_SUBTRACT;
	case 270: return VK_ADD;
	case 271: return VK_RETURN;

	// Arrows & Navigation
	case 273: return VK_UP;        // RETROK_UP
	case 274: return VK_DOWN;      // RETROK_DOWN
	case 275: return VK_RIGHT;     // RETROK_RIGHT
	case 276: return VK_LEFT;      // RETROK_LEFT
	case 277: return VK_INSERT;    // RETROK_INSERT
	case 278: return VK_HOME;      // RETROK_HOME
	case 279: return VK_END;       // RETROK_END
	case 280: return VK_PRIOR;     // RETROK_PAGEUP
	case 281: return VK_NEXT;      // RETROK_PAGEDOWN

	// Function keys
	case 282: return VK_F1;
	case 283: return VK_F2;
	case 284: return VK_F3;
	case 285: return VK_F4;
	case 286: return VK_F5;
	case 287: return VK_F6;
	case 288: return VK_F7;
	case 289: return VK_F8;
	case 290: return VK_F9;
	case 291: return VK_F10;
	case 292: return VK_F11;
	case 293: return VK_F12;

	// Modifiers
	case 300: return VK_NUMLOCK;
	case 301: return VK_CAPITAL;   // RETROK_CAPSLOCK
	case 302: return VK_SCROLL;
	case 303: return VK_RSHIFT;
	case 304: return VK_LSHIFT;
	case 305: return VK_RCONTROL;
	case 306: return VK_LCONTROL;
	case 307: return VK_RMENU;     // RETROK_RALT
	case 308: return VK_LMENU;     // RETROK_LALT
	case 311: return VK_LWIN;
	case 312: return VK_RWIN;
	default:  return 0;
	}
}

static int16_t CB_InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port >= 4) return 0;

	if (device == RETRO_DEVICE_JOYPAD)
	{
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		{
			int16_t mask = 0;
			for (int b = 0; b < 16; b++)
			{
				if (joypad_buttons[port][b]) mask |= (int16_t)(1 << b);
			}
			return mask;
		}
		if (id < 16) return joypad_buttons[port][id];
	}
	else if (device == RETRO_DEVICE_KEYBOARD && port == 0)
	{
		if (OsdIsEnabled()) return 0;
		HWND fg = GetForegroundWindow();
		if (fg)
		{
			DWORD fg_pid = 0;
			GetWindowThreadProcessId(fg, &fg_pid);
			if (fg_pid != 0 && fg_pid != GetCurrentProcessId()) return 0;
		}
		if (id == 27 || id == 293) return 0; // Esc and F12 reserved for frontend OSD

		int vk = RetroKeyToVK(id);
		if (vk > 0)
		{
			if (vk == VK_LSHIFT || vk == VK_RSHIFT)
			{
				if ((GetAsyncKeyState(vk) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) return 1;
			}
			else if (vk == VK_LCONTROL || vk == VK_RCONTROL)
			{
				if ((GetAsyncKeyState(vk) & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000)) return 1;
			}
			else if (vk == VK_LMENU || vk == VK_RMENU)
			{
				if ((GetAsyncKeyState(vk) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000)) return 1;
			}
			else
			{
				if (GetAsyncKeyState(vk) & 0x8000) return 1;
			}
		}

		// Bridge gamepad buttons onto keyboard keys, but only for the machines
		// that need it - see SystemNeedsKeyboardBridge. Leaving this on for every
		// core made one button do two things at once on any core that reads both
		// the joypad and the keyboard.
		if (!keyboard_bridge_active) return 0;

		if (id == 273 && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_UP]) return 1;
		if (id == 274 && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_DOWN]) return 1;
		if (id == 276 && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_LEFT]) return 1;
		if (id == 275 && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_RIGHT]) return 1;
		if (id == 32  && (joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_B] || joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_A])) return 1; // Space
		if (id == 13  && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_START]) return 1; // Enter
		if (id == 282 && joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_SELECT]) return 1; // F1
		if (id == 'm' && (joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_Y] || joypad_buttons[0][RETRO_DEVICE_ID_JOYPAD_X])) return 1; // M / Powerup

		return 0;
	}
	else if (device == RETRO_DEVICE_ANALOG)
	{
		if (index < 2 && id < 2) return analog_sticks[port][index][id];
	}
	else if (device == RETRO_DEVICE_POINTER && port == 0 && index == 0)
	{
		LONG xy = g_pointer_xy;
		switch (id)
		{
		case RETRO_DEVICE_ID_POINTER_X:       return (int16_t)((xy >> 16) & 0xFFFF);
		case RETRO_DEVICE_ID_POINTER_Y:       return (int16_t)(xy & 0xFFFF);
		case RETRO_DEVICE_ID_POINTER_PRESSED: return (int16_t)g_pointer_down;
		case RETRO_DEVICE_ID_POINTER_COUNT:   return g_pointer_down ? 1 : 0;
		default: return 0;
		}
	}
	return 0;
}

// -------------------------------------------------------------
// Dedicated Core Worker Thread Proc
// -------------------------------------------------------------
// SEH cannot live in a function that has C++ objects needing unwinding, so the
// guarded call gets its own tiny function.
// Plain POD, not std::string/etc: __try cannot share a function with a C++
// object that needs unwinding, so whatever this reports has to travel out as
// data the caller (which has no __try of its own) can log with full context.
struct FrameRunResult { bool ok; DWORD exc_code; void* exc_addr; };

static FrameRunResult RunOneFrameGuarded()
{
	FrameRunResult r = { true, 0, NULL };
	__try
	{
		p_retro_run();
	}
	__except (
		r.exc_code = GetExceptionCode(),
		r.exc_addr = GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
		EXCEPTION_EXECUTE_HANDLER)
	{
		r.ok = false;
	}
	return r;
}

// Known Sega Naomi/Naomi 2/Atomiswave arcade romset short names - see
// app/gamedb/naomi_atomiswave.json for provenance (pulled from MAME's own
// driver source, not a guess). None of MAME2003/FBNeo/MAME2010 emulate this
// hardware at all, so for a title in this list the usual try-every-core
// fallback dance is pure wasted time (and, worse, a wasted probation crash
// each try) before it ever reaches the one core that actually can: Flycast.
// Anything not in this small, curated list still goes through the normal
// loop unaffected - a newly dumped or unlisted title is never left unrouted,
// just not fast-tracked.
static const std::unordered_set<std::string>& NaomiAtomiswaveRomsets()
{
	static std::unordered_set<std::string> s_names;
	static bool s_loaded = false;
	if (!s_loaded)
	{
		s_loaded = true;
		std::ifstream f("gamedb/naomi_atomiswave.json");
		if (f)
		{
			std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			size_t key = json.find("\"shortnames\"");
			size_t open = (key != std::string::npos) ? json.find('[', key) : std::string::npos;
			size_t close = (open != std::string::npos) ? json.find(']', open) : std::string::npos;
			if (open != std::string::npos && close != std::string::npos)
			{
				size_t pos = open;
				while (true)
				{
					size_t q1 = json.find('"', pos);
					if (q1 == std::string::npos || q1 > close) break;
					size_t q2 = json.find('"', q1 + 1);
					if (q2 == std::string::npos || q2 > close) break;
					s_names.insert(json.substr(q1 + 1, q2 - q1 - 1));
					pos = q2 + 1;
				}
			}
		}
	}
	return s_names;
}

static DWORD WINAPI CoreExecutionThreadProc(LPVOID lpParam)
{
	(void)lpParam;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	// --- Load phase (this is the part that used to freeze the window) ---
	EnterCriticalSection(&name_lock);
	std::string rom_path = g_pending_rom;
	std::string core_dll = g_pending_core_hint;
	LeaveCriticalSection(&name_lock);

	// Flycast supports "zip" directly (ext=...|zip|7z|...) and needs to for a
	// Naomi title: extracting the zip down to just its .chd, as happens below
	// for any archive whose first-guess core doesn't read archives itself,
	// throws away whatever else the romset carried alongside the GD-ROM image.
	// A bare .chd loads fine as generic Dreamcast content and boots straight
	// to Flycast's own BIOS dashboard instead of the actual game. Keep the
	// original zip path so the arcade candidate loop can hand Flycast the
	// whole thing instead of the reduced extraction.
	const std::string original_rom_path = rom_path;

	// CoreLoadGame() below derives loaded_rom_dir (what the OSD's "Load" row
	// reopens) from whatever path it is handed. For an archive that gets
	// extracted, that path is the per-game folder under app/cache/ - browsing
	// it shows only this one game's own extracted files, which reads as "Load
	// does nothing" since there is nothing else there to pick. Keep the
	// original library folder (app/roms/<system>) so it can be restored once
	// loading succeeds, regardless of which of the two paths below got there.
	const std::string original_rom_dir = fs::path(rom_path).parent_path().string();

	// Same idea as original_rom_dir, for is_arcade_rom below: extraction can
	// re-resolve core_dll to "dreamcast.dll" for a Naomi title whose GD-ROM
	// image is just a .chd with nothing arcade-specific in its own path
	// (app/cache/<name>/track.chd, not app/roms/Arcade/<name>.zip). By the
	// time is_arcade_rom is computed neither rom_path nor core_dll says
	// "Arcade" anymore, so a Naomi zip that resolved to Flycast skipped the
	// candidate-core loop entirely and loaded straight into Flycast's own
	// Dreamcast BIOS dashboard instead of ever being tried as arcade content.
	const bool original_was_arcade = (rom_path.find("Arcade") != std::string::npos);

	// Arcade cores read the archive themselves: the .zip IS the ROM set, a
	// bundle of chip dumps only the core knows how to assemble. Extracting it
	// and handing over one inner file is meaningless - and worse, the core was
	// then re-resolved from the extracted path, which no longer says "Arcade".
	// Every arcade ROM ended up on Genesis Plus GX (the .bin fallback) or on
	// Beetle PSX (when the zip held a .chd). Verified from what the cores
	// declare: FinalBurn Neo says ext=zip|7z|cue|ccd, MAME 2010 says
	// ext=zip|chd|7z, Flycast says ext=chd|cdi|elf|cue|gdi|lst|bin|dat|zip|7z|m3u.
	//
	// This list is a maintenance hazard and has already been wrong twice. The
	// real fix is to load the core first, read valid_extensions, and only then
	// decide whether to extract - which needs the load order rearranged.
	// ext=zip|chd|7z. The [CORE] line in the log shows this for any core.
	std::string rom_stem = fs::path(rom_path).stem().string();
	std::transform(rom_stem.begin(), rom_stem.end(), rom_stem.begin(), ::tolower);

	const bool is_wrapped_chd = (rom_stem.find("-chd") != std::string::npos || rom_stem.find("_chd") != std::string::npos);

	const bool core_reads_archive =
		!is_wrapped_chd &&
		!core_dll.empty() &&
		(core_dll.find("arcade_fbneo") != std::string::npos ||
		 core_dll.find("mame2003") != std::string::npos ||
		 core_dll.find("mame2010") != std::string::npos ||
		 core_dll.find("dreamcast") != std::string::npos ||
		 core_dll.find("dosbox") != std::string::npos);

	if (ArchiveIsCompressed(rom_path) && !core_reads_archive)
	{
		std::string extracted_rom, extracted_core;
		if (ArchiveExtractRom(rom_path, extracted_rom, extracted_core))
		{
			rom_path = extracted_rom;
			if (!extracted_core.empty()) core_dll = extracted_core;

			// fMSX's fmsx_mode core option defaults to "MSX2+" (the first of
			// MSX2+|MSX1|MSX2), and that specific default can fail to render in some builds.
			// Explicitly setting "MSX2" provides full MSX2 hardware (V9938 VDP, 128KB VRAM)
			// which is required for MSX2 titles (such as Aleste, Metal Gear 2, Rastan Saga,
			// Outrun) while remaining backwards compatible with MSX1 games.
			// Only explicit .mx1 files are constrained to "MSX1".
			if (core_dll.find("msx.dll") != std::string::npos)
			{
				std::string extracted_ext = fs::path(rom_path).extension().string();
				std::transform(extracted_ext.begin(), extracted_ext.end(), extracted_ext.begin(), ::tolower);
				CoreSetOption("fmsx_mode", extracted_ext == ".mx1" ? "MSX1" : "MSX2");
			}
		}
		else
		{
			// Extraction usually works; what fails is recognising the contents.
			// Saying "failed to extract" for an archive holding a format we do
			// not support sent the user looking in the wrong place.
			CoreSetToast("ARQUIVO SEM JOGO RECONHECIDO", 240);
			InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
			return 0;
		}
	}

	{
		fs::path init_p(rom_path);
		EnterCriticalSection(&name_lock);
		loaded_game_stem = init_p.stem().string();
		loaded_game_name = init_p.filename().string();
		loaded_rom_dir = init_p.parent_path().string();
		loaded_system_dir = init_p.parent_path().filename().string();
		if (loaded_system_dir.empty() || loaded_system_dir == "." ||
			loaded_system_dir == "roms" || loaded_system_dir == "cache" || loaded_system_dir == "app")
		{
			loaded_system_dir = init_p.stem().string();
		}
		keyboard_bridge_active = SystemNeedsKeyboardBridge(loaded_system_dir);
		LeaveCriticalSection(&name_lock);
	}

	const bool is_arcade_rom =
		(original_was_arcade ||
		 rom_path.find("Arcade") != std::string::npos ||
		 core_dll.find("arcade_fbneo") != std::string::npos ||
		 core_dll.find("mame2003") != std::string::npos ||
		 core_dll.find("mame2010") != std::string::npos);

	bool loaded = false;

	if (is_arcade_rom)
	{
		// Build candidate core priority list starting with the preferred core
		std::vector<std::string> candidate_cores;

		// An arcade romset is built for one specific core revision and nothing
		// in the file says which, so the chain below finds out by trying. What
		// it must not do is forget the answer. Reloading a game that had been
		// running a minute earlier used to walk the whole chain again from the
		// top - and walking it is not free, because several cores cannot be
		// initialised twice in one process: after four such rounds Flycast
		// trapped on its own assertion (state == Init, emulator.cpp:499) and
		// took the application down with an illegal instruction. Remembering
		// which core actually ran a given romset turns the reload into one
		// correct attempt instead of fifteen wrong ones.
		{
			auto it = s_arcade_core_for_rom.find(original_rom_path);
			if (it != s_arcade_core_for_rom.end() && fs::exists(it->second))
			{
				candidate_cores.push_back(it->second);
			}
		}

		if (!core_dll.empty() && fs::exists(core_dll) &&
			std::find(candidate_cores.begin(), candidate_cores.end(), core_dll) == candidate_cores.end())
		{
			candidate_cores.push_back(core_dll);
		}

		// Flycast is deliberately not in this list. It emulates Dreamcast and
		// the Naomi/Atomiswave boards built on it, and nothing else - handing
		// it a Sega System 16 romset like altbeast was never going to work.
		// It was in the list as a last resort, which meant every arcade game
		// that matched no MAME revision ended its search inside a core that
		// cannot survive a second retro_init in one process: Flycast traps on
		// its own assertion and kills the application. The block below adds it,
		// at the front, for the titles it actually emulates.
		const std::string default_arcade_cores[] = {
			"cores/arcade_fbneo.dll",
			"cores/mame2003.dll",
			"cores/mame2010.dll"
		};

		for (const auto& c : default_arcade_cores)
		{
			if (std::find(candidate_cores.begin(), candidate_cores.end(), c) == candidate_cores.end() && fs::exists(c))
			{
				candidate_cores.push_back(c);
			}
		}

		// A known Naomi/Atomiswave title: none of the MAME candidates above
		// emulate this hardware at all, so Flycast goes to the front - and for
		// anything else it does not go in at all.
		{
			std::string stem = fs::path(original_rom_path).stem().string();
			std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
			if (stem == "cvs2gd" || stem == "cvs2gd-chd" || stem == "cvs2mf" || NaomiAtomiswaveRomsets().count(stem))
			{
				const std::string fly = "cores/dreamcast.dll";
				auto it = std::find(candidate_cores.begin(), candidate_cores.end(), fly);
				if (it != candidate_cores.end())
				{
					if (it != candidate_cores.begin()) std::rotate(candidate_cores.begin(), it, it + 1);
				}
				else if (fs::exists(fly))
				{
					candidate_cores.insert(candidate_cores.begin(), fly);
				}
			}
		}

		for (size_t i = 0; i < candidate_cores.size(); i++)
		{
			const std::string& cand_dll = candidate_cores[i];
			bool is_last_candidate = (i == candidate_cores.size() - 1);

			// Flycast reads zip/7z natively and needs to for a Naomi title:
			// the .chd extraction above throws away whatever else the romset
			// carried alongside the GD-ROM image, and a bare .chd loads fine
			// as generic Dreamcast content but boots straight to Flycast's
			// own BIOS dashboard instead of recognising the actual game.
			// Give it the intact original archive instead.
			std::string effective_load_path =
				(cand_dll.find("dreamcast") != std::string::npos && ArchiveIsCompressed(original_rom_path))
					? original_rom_path : rom_path;

			// Handle Naomi GD-ROM aliases for Flycast (e.g. cvs2gd / cvs2gd-chd -> cvs2mf / cvs2)
			if (cand_dll.find("dreamcast") != std::string::npos)
			{
				std::string stem = fs::path(effective_load_path).stem().string();
				std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
				if (stem == "cvs2gd" || stem == "cvs2gd-chd")
				{
					fs::path p_dir = fs::path(effective_load_path).parent_path();
					if (fs::exists(p_dir / "cvs2mf.zip"))
						effective_load_path = (p_dir / "cvs2mf.zip").string();
					else if (fs::exists(p_dir / "cvs2.zip"))
						effective_load_path = (p_dir / "cvs2.zip").string();
				}
			}

			const std::string& load_path = effective_load_path;

			CoreLogPrintf(RETRO_LOG_INFO, "[CoreRunner] Tentando core de arcade: %s para '%s'", cand_dll.c_str(), load_path.c_str());

			if (CoreLoad(cand_dll.c_str()))
			{
				// Suppress toast unless all candidates have failed
				if (CoreLoadGame(load_path.c_str(), !is_last_candidate))
				{
					// retro_load_game() returning true only means the core
					// accepted the file - not that the content actually runs.
					// A romset built for a different driver revision can boot
					// far enough to say yes and then fault a moment later once
					// real emulation starts (confirmed in the field: MAME2003
					// loaded a Mortal Kombat dump clean, then hit "op-code
					// execute on mapped I/O" and froze). Run it unthrottled
					// for a couple of probation seconds before committing -
					// a crash in that window is treated the same as a load
					// failure and the loop moves on to the next candidate,
					// instead of leaving the player looking at a dead game
					// with no indication anything else was ever available.
					bool probation_ok = true;
					const int kProbationFrames = 120;
					for (int pf = 0; pf < kProbationFrames; pf++)
					{
						FrameRunResult pr = RunOneFrameGuarded();
						if (!pr.ok)
						{
							CoreLogPrintf(RETRO_LOG_ERROR,
								"[CoreRunner] %s travou no periodo de teste (frame %d/%d) apos carregar '%s'; tentando proximo core",
								cand_dll.c_str(), pf, kProbationFrames, load_path.c_str());
							probation_ok = false;
							break;
						}
					}

					if (probation_ok)
					{
						core_dll = cand_dll;
						rom_path = load_path;
						loaded = true;
						s_arcade_core_for_rom[original_rom_path] = cand_dll;
						CoreLogPrintf(RETRO_LOG_INFO, "[CoreRunner] Sucesso no carregamento de arcade com: %s", cand_dll.c_str());
						break;
					}
				}
			}
			CoreUnload();
		}
	}
	else
	{
		if (core_dll.empty() || !fs::exists(core_dll))
		{
			char msg[160];
			snprintf(msg, sizeof(msg), "CORE AUSENTE: %s",
				core_dll.empty() ? "nenhum core para este formato" : core_dll.c_str());
			CoreSetToast(msg, 300);
			InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
			return 0;
		}

		if (CoreLoad(core_dll.c_str()))
		{
			// citra_touch_touchscreen defaults to "disabled" (first of
			// disabled|enabled) - the core's own conservative default for a
			// platform that might have no pointer device at all. We always
			// feed real touch/mouse position through CoreSetPointer()
			// (PollMouseStylus in main_win32.cpp), so leaving this at the
			// core's default meant every click on the touch screen was
			// silently ignored. citra_render_touchscreen defaults off the
			// same way, hiding the touch-screen cursor entirely.
			if (core_dll.find("3ds.dll") != std::string::npos)
			{
				CoreSetOption("citra_touch_touchscreen", "enabled");
				CoreSetOption("citra_render_touchscreen", "enabled");
			}

			if (core_dll.find("msx.dll") != std::string::npos)
			{
				std::string ext = fs::path(rom_path).extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				CoreSetOption("fmsx_mode", ext == ".mx1" ? "MSX1" : "MSX2");
			}

			if (CoreLoadGame(rom_path.c_str(), false))
			{
				loaded = true;
			}
		}
	}

	if (!loaded)
	{
		CoreUnload();
		InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
		return 0;
	}

	// Restore the real library folder now that CoreLoadGame() has (possibly)
	// overwritten it with the extraction cache path - see original_rom_dir above.
	if (!original_rom_dir.empty())
	{
		EnterCriticalSection(&name_lock);
		loaded_rom_dir = original_rom_dir;
		LeaveCriticalSection(&name_lock);
	}

	RaOnGameLoad(rom_path.c_str(), loaded_core_name.c_str());

	InterlockedExchange(&g_core_state, CORE_STATE_RUNNING);

	// --- Run phase, paced at the core's OWN refresh rate ---
	// A fixed 60Hz here is what detuned every core that is not exactly 60fps
	// (Neo Geo 59.19, Genesis 59.92, Saturn 59.83, anything PAL at 50).
	LARGE_INTEGER freq, next, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&next);

	double fps = (core_target_fps > 1.0 && core_target_fps < 1000.0) ? core_target_fps : 60.0;
	LONGLONG tick = (LONGLONG)((double)freq.QuadPart / fps);

	// A core that faults inside retro_run() usually does so because something
	// in its own state got corrupted - retrying just re-enters retro_run()
	// against that same broken state, which typically faults again on the very
	// next frame. Field reports against a specific MSX title confirmed this
	// twice over: the SAME faulting address hit on every one of 5 retries, and
	// the app still went down hard both times even with the retry cap in
	// place - the access violation itself is only the byte that finally landed
	// on an unmapped page; a bad write earlier in that same faulting frame can
	// have already scribbled over unrelated process memory before the OS ever
	// raises anything. Every additional retry against already-faulted state is
	// another chance to extend that corruption into something no __try can
	// catch (heap-corruption fast-fail, stack exhaustion) - including in code
	// that has nothing to do with the core, like our own cleanup path. One
	// fault is enough to call the session unrecoverable; retrying has never
	// once self-healed it and was only raising the stakes.
	int consecutive_crashes = 0;
	const int kMaxConsecutiveCrashes = 1;

	while (InterlockedCompareExchange(&core_thread_running, 0, 0) && is_game_loaded)
	{
		FrameRunResult run = RunOneFrameGuarded();
		if (!run.ok)
		{
			consecutive_crashes++;
			// This is the line that turns the next "it crashed" report into
			// something actionable instead of guesswork re-derived from PERF/
			// AUDIO log fallout after the fact.
			CoreLogPrintf(RETRO_LOG_ERROR,
				"[CoreRunner] retro_run() excecao 0x%08lX no endereco %p (core=%s rom=%s) [%d/%d]",
				run.exc_code, run.exc_addr, core_dll.c_str(), rom_path.c_str(),
				consecutive_crashes, kMaxConsecutiveCrashes);
			if (consecutive_crashes >= kMaxConsecutiveCrashes)
			{
				CoreSetToast("ERROR: CORE TRAVOU REPETIDAMENTE - JOGO ENCERRADO", 300);
				break;
			}
			CoreSetToast("WARNING: RECOVERED FROM INTERNAL EXCEPTION", 180);
			Sleep(16);
		}
		else
		{
			consecutive_crashes = 0;
		}

		RaDoFrame();
		CoreUpdateToast();
		ProcessPendingCommand();

		if (InterlockedExchange(&g_av_info_dirty, 0))
		{
			fps = (core_target_fps > 1.0 && core_target_fps < 1000.0) ? core_target_fps : 60.0;
			tick = (LONGLONG)((double)freq.QuadPart / fps);
		}

		next.QuadPart += tick;
		QueryPerformanceCounter(&now);

		LONGLONG remaining = next.QuadPart - now.QuadPart;
		if (remaining > 0)
		{
			// Sleep for the bulk, then spin the last millisecond. WinMain calls
			// timeBeginPeriod(1), so Sleep is accurate to about 1ms here.
			DWORD ms = (DWORD)((remaining * 1000) / freq.QuadPart);
			if (ms > 1) Sleep(ms - 1);
			do { QueryPerformanceCounter(&now); } while (now.QuadPart < next.QuadPart);
		}
		else if (-remaining > tick * 4)
		{
			next = now; // fell far behind (a load hitch); resync instead of sprinting
		}
	}

	CoreUnload();
	InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
	return 0;
}

// -------------------------------------------------------------
// Async lifecycle, called from the UI thread
// -------------------------------------------------------------
// Picks up what the core thread never got to do, because it was killed part way
// through CoreUnload().
//
// Closing GameCube exposed this: Dolphin does not return from retro_unload_game,
// the wait timed out, TerminateThread stopped the thread mid-cleanup, and
// options_lock was left held by a thread that no longer exists. The next
// EnterCriticalSection then waited forever - which is why loading Master System
// afterwards did nothing until the whole app was restarted.
static void ResetCoreFunctionPointers()
{
	p_retro_init = NULL;
	p_retro_deinit = NULL;
	p_retro_get_system_info = NULL;
	p_retro_get_system_av_info = NULL;
	p_retro_set_environment = NULL;
	p_retro_set_video_refresh = NULL;
	p_retro_set_audio_sample = NULL;
	p_retro_set_audio_sample_batch = NULL;
	p_retro_set_input_poll = NULL;
	p_retro_set_input_state = NULL;
	p_retro_set_controller_port_device = NULL;
	p_retro_reset = NULL;
	p_retro_run = NULL;
	p_retro_serialize_size = NULL;
	p_retro_serialize = NULL;
	p_retro_unserialize = NULL;
	p_retro_get_memory_data = NULL;
	p_retro_get_memory_size = NULL;
	p_retro_load_game = NULL;
	p_retro_unload_game = NULL;
}

// A core-spawned background thread that crashes while holding one of these
// locks (SEH-caught in main_win32.cpp's CrashHandler, which has to
// TerminateThread it rather than let the whole process die - see that file)
// would otherwise leave the lock "owned" by a thread ID that no longer
// exists, wedging every future EnterCriticalSection on it forever with no
// crash to explain why. OwningThread is part of the public CRITICAL_SECTION
// layout and stable across Windows versions. Only a lock THIS thread
// actually holds is recreated - blindly recreating all three the way
// RecoverAfterKilledCore() does would corrupt one a different, still-running
// thread legitimately holds at the same moment.
void CoreRecoverLocksHeldByThread(unsigned long thread_id)
{
	HANDLE owner = (HANDLE)(ULONG_PTR)thread_id;
	if (toast_lock.OwningThread == owner)
	{
		DeleteCriticalSection(&toast_lock);
		InitializeCriticalSection(&toast_lock);
	}
	if (name_lock.OwningThread == owner)
	{
		DeleteCriticalSection(&name_lock);
		InitializeCriticalSection(&name_lock);
	}
	if (options_lock.OwningThread == owner)
	{
		DeleteCriticalSection(&options_lock);
		InitializeCriticalSection(&options_lock);
	}
}

// Defined further down, next to the buffer it owns.
static void FreeLoadedRomData();

static void RecoverAfterKilledCore()
{
	// The core thread was killed, so RetroUnloadGameGuarded never ran and never
	// got to release the game buffer. Nothing can still be reading it - the only
	// thing that ever held the pointer was the core we just terminated.
	FreeLoadedRomData();

	// A critical section owned by a dead thread is never released. Recreating it
	// is the only way back; nothing else can be running against it here, because
	// the only other user was the thread we just killed.
	DeleteCriticalSection(&options_lock);
	InitializeCriticalSection(&options_lock);
	DeleteCriticalSection(&name_lock);
	InitializeCriticalSection(&name_lock);
	DeleteCriticalSection(&toast_lock);
	InitializeCriticalSection(&toast_lock);

	// All three are safe to call unconditionally - each only acts if its own
	// g_hw_active is set, and a given core only ever activates one of them.
	HwContextDestroy();
	VkHwContextDestroy();
	D3D11HwContextDestroy();

	is_game_loaded = false;
	is_core_loaded = false;
	ResetCoreFunctionPointers();

	// The DLL is deliberately left mapped. FreeLibrary on a module whose thread
	// was killed inside it can fault during its own cleanup, and a leaked
	// mapping costs memory, not correctness - a different core still loads.
	h_core_dll = NULL;

	// Stop-then-close, same order as CoreUnload: AudioThreadProc is a separate
	// live thread the core-thread kill above never touched. Destroying
	// g_audio_stream out from under it while it can still be mid-
	// SDL_PutAudioStreamData() races the handle close against that thread's
	// own use of it.
	if (audio_thread_running)
	{
		audio_thread_running = false;
		if (h_audio_event) SetEvent(h_audio_event);
		if (h_audio_thread)
		{
			if (WaitForSingleObject(h_audio_thread, 1000) == WAIT_TIMEOUT)
			{
				// Presa dentro de uma chamada do SDL (driver travado). Nao se
				// mata: TerminateThread solta a thread sem soltar os locks que
				// ela segura, e se um deles for interno do SDL a proxima
				// chamada de audio trava para sempre. Foi essa a mecanica que
				// deixou a interface presa em "LOADING..." no PS2, por outro
				// caminho.
				//
				// Abandona-se a thread E o stream juntos. Destruir o stream
				// aqui correria contra a chamada em que ela esta parada, que e
				// o motivo original de existir um kill. Deixando os dois de pe
				// nada corre risco: ela sai sozinha se a chamada retornar, e um
				// jogo novo abre um stream novo.
				audio_abandoned = true;
				CoreLogPrintf(RETRO_LOG_WARN,
					"[CoreShutdown] audio thread nao respondeu em 1s (driver travado?) - abandonando thread e stream (vazam ate fechar o app)");
			}
			CloseHandle(h_audio_thread);
			h_audio_thread = NULL;
		}
	}

	if (g_audio_stream)
	{
		// Destroying the stream also closes the device it was opened
		// against - SDL_OpenAudioDeviceStream's own contract.
		if (!audio_abandoned)
			SDL_DestroyAudioStream(g_audio_stream);
		g_audio_stream = nullptr;
		audio_initialized = false;
		audio_abandoned = false;
	}
	if (h_audio_event) { CloseHandle(h_audio_event); h_audio_event = NULL; }

	g_resample_phase = 0.0;
	memset(g_hist_l, 0, sizeof(g_hist_l));
	memset(g_hist_r, 0, sizeof(g_hist_r));
	InterlockedExchange(&g_ring_write_pos, 0);
	InterlockedExchange(&g_ring_read_pos, 0);

	memset(g_triple_fb, 0, sizeof(g_triple_fb));
	InterlockedExchange(&g_fb_dirty, 0);

	CoreLogPrintf(RETRO_LOG_ERROR,
		"[CoreRunner] o core nao encerrou sozinho; estado recuperado a forca");
}

void CoreShutdown()
{
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreShutdown] Inicio do shutdown...");
	InterlockedExchange(&core_thread_running, 0);

	if (h_core_thread)
	{
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreShutdown] Aguardando core thread encerrar (3000ms)...");
		if (WaitForSingleObject(h_core_thread, 3000) == WAIT_TIMEOUT)
		{
			CoreLogPrintf(RETRO_LOG_WARN, "[CoreShutdown] Timeout aguardando core thread. Verificando module_op=%ld",
				InterlockedCompareExchange(&g_core_in_module_op, 0, 0));
			int extra_waited_ms = 0;
			while (InterlockedCompareExchange(&g_core_in_module_op, 0, 0) &&
				extra_waited_ms < 5000)
			{
				if (WaitForSingleObject(h_core_thread, 200) != WAIT_TIMEOUT)
					break;
				extra_waited_ms += 200;
			}

			if (WaitForSingleObject(h_core_thread, 0) == WAIT_TIMEOUT)
			{
				CoreLogPrintf(RETRO_LOG_ERROR, "[CoreShutdown] Matando core thread via TerminateThread...");
				TerminateThread(h_core_thread, 0);
			}
			CloseHandle(h_core_thread);
			h_core_thread = NULL;
			RecoverAfterKilledCore();
			InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
			CoreLogPrintf(RETRO_LOG_INFO, "[CoreShutdown] Finalizado apos recuperacao forcada.");
			return;
		}
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreShutdown] Core thread encerrou normalmente.");
		CloseHandle(h_core_thread);
		h_core_thread = NULL;
	}

	InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreShutdown] Shutdown concluido com sucesso.");
}

bool CoreRequestLoad(const char* rom_path, const char* core_dll_hint)
{
	if (!rom_path) return false;

	CoreShutdown();

	EnterCriticalSection(&name_lock);
	g_pending_rom = rom_path;
	g_pending_core_hint = core_dll_hint ? core_dll_hint : "";
	LeaveCriticalSection(&name_lock);

	// Wipe the framebuffers before loading. They still held the last frame of
	// the previous game, so starting a new one showed the old one frozen on
	// screen for the whole load.
	memset(g_triple_fb, 0, sizeof(g_triple_fb));
	for (int i = 0; i < 3; i++) { g_fb_dim_w[i] = 320; g_fb_dim_h[i] = 240; }
	core_fb_width = 320;
	core_fb_height = 240;

	InterlockedExchange(&g_core_state, CORE_STATE_LOADING);
	InterlockedExchange(&core_thread_running, 1);

	h_core_thread = CreateThread(NULL, 0, CoreExecutionThreadProc, NULL, 0, NULL);
	if (!h_core_thread)
	{
		InterlockedExchange(&core_thread_running, 0);
		InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
		return false;
	}
	return true;
}

bool CoreIsLoading()
{
	return InterlockedCompareExchange(&g_core_state, 0, 0) == CORE_STATE_LOADING;
}

double CoreGetTargetFps() { return core_target_fps; }

// -------------------------------------------------------------
// Core Engine Public API
// -------------------------------------------------------------
static bool RetroSetEnvironmentGuarded()
{
	__try
	{
		if (p_retro_set_environment) p_retro_set_environment(CB_Environment);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static bool RetroInitGuarded()
{
	__try { if (p_retro_init) p_retro_init(); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void ClearInputDescriptors()
{
	memset(g_desc_button, 0, sizeof(g_desc_button));
	memset(g_desc_axis, 0, sizeof(g_desc_axis));
	g_desc_present = false;
}

const char* CoreGetButtonLabel(int retro_id)
{
	if (!g_desc_present || retro_id < 0 || retro_id >= 16) return NULL;
	return g_desc_button[retro_id][0] ? g_desc_button[retro_id] : NULL;
}

const char* CoreGetAxisLabel(int stick, int axis)
{
	if (!g_desc_present) return NULL;
	if (stick < 0 || stick > 1 || axis < 0 || axis > 1) return NULL;
	return g_desc_axis[stick][axis][0] ? g_desc_axis[stick][axis] : NULL;
}

static void SetPortDevicesGuarded()
{
	if (!p_retro_set_controller_port_device) return;
	__try
	{
		unsigned port0_device = RETRO_DEVICE_JOYPAD;
		if (s_loaded_core_path.find("msx.dll") != std::string::npos)
		{
			// fMSX requires port 0 device to be RETRO_DEVICE_KEYBOARD (3) so that
			// it polls the emulated MSX keyboard matrix via RETRO_DEVICE_KEYBOARD while
			// simultaneously polling RETRO_DEVICE_JOYPAD on both ports for joystick/gamepad input.
			port0_device = RETRO_DEVICE_KEYBOARD;
		}
		p_retro_set_controller_port_device(0, port0_device);
		p_retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		CoreLogPrintf(RETRO_LOG_ERROR,
			"[CoreRunner] set_controller_port_device falhou; core usa o padrao dele");
	}
}

static bool RetroLoadGameGuarded(const struct retro_game_info* info)
{
	__try { return p_retro_load_game ? p_retro_load_game(info) : false; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The buffer handed to the core as retro_game_info::data, kept alive for
// exactly as long as the libretro spec requires.
//
// When need_fullpath is false the frontend loads the content itself, and the
// pointer it passes has to stay valid until retro_unload_game() returns - the
// whole point of that path is that a core may keep the pointer instead of
// copying the ROM. This used to be free()d on the line right after
// retro_load_game(), which is fine for the cores that do copy (mupen, mGBA -
// most of them) and a use-after-free for the ones that do not.
//
// This was found while chasing gopher64's heap corruption on unload, and it
// is worth being explicit that it did NOT fix it: gopher64 still dies inside
// its own retro_unload_game() with the buffer kept alive, with it allocated
// from the process heap instead of the CRT's, and with the free removed
// entirely. So this is a real latent use-after-free that was waiting for the
// next core that keeps the pointer - not an explanation of that crash.
static uint8_t* g_loaded_rom_data = nullptr;

static void FreeLoadedRomData()
{
	if (g_loaded_rom_data) { free(g_loaded_rom_data); g_loaded_rom_data = nullptr; }
}

static void RetroUnloadGameGuarded()
{
	__try { if (p_retro_unload_game) p_retro_unload_game(); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// Engolir e proposital - um core que estoura ao descarregar nao deve
		// levar o app junto. Em silencio nao era: toda a investigacao de hoje
		// dependeu do log, e uma falha aqui nao deixava rastro nenhum.
		CoreLogPrintf(RETRO_LOG_ERROR, "[CoreUnload] excecao dentro de retro_unload_game do core - ignorada");
	}

	// Only now is the core guaranteed to be done with it. Every call site of
	// this function is a point where the game is going away, so freeing here
	// covers all of them at once instead of relying on each to remember.
	FreeLoadedRomData();
}

static bool RetroGetSystemAvInfoGuarded(struct retro_system_av_info* av_info)
{
	__try { if (p_retro_get_system_av_info) p_retro_get_system_av_info(av_info); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void RetroDeinitGuarded()
{
	__try { if (p_retro_deinit) p_retro_deinit(); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		CoreLogPrintf(RETRO_LOG_ERROR, "[CoreUnload] excecao dentro de retro_deinit do core - ignorada");
	}
}

static bool RetroResetGuarded()
{
	__try { if (p_retro_reset) p_retro_reset(); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Returns 0 on exception, same sentinel callers already treat as "invalid size".
static size_t RetroSerializeSizeGuarded()
{
	__try { return p_retro_serialize_size ? p_retro_serialize_size() : 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool RetroSerializeGuarded(void* data, size_t size)
{
	__try { return p_retro_serialize ? p_retro_serialize(data, size) : false; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool RetroUnserializeGuarded(const void* data, size_t size)
{
	__try { return p_retro_unserialize ? p_retro_unserialize(data, size) : false; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool CoreLoad(const char* core_dll_path)
{
	CoreUnload();

	// A core that sends no descriptors must not inherit the previous one's
	// names, so this is cleared on the way in rather than only on the way out.
	ClearInputDescriptors();
	g_core_wants_vk_negotiation = false;
	// A interface de negociacao e um ponteiro para dentro da DLL do core. Ao
	// trocar de core ele tem que ser esquecido aqui, antes do LoadLibrary do
	// proximo: mante-lo apontaria para memoria de uma DLL que ja saiu, e a
	// proxima criacao de dispositivo chamaria create_device do core anterior.
	VkHwClearNegotiationInterface();

	InterlockedExchange(&g_core_in_module_op, 1);
	h_core_dll = LoadLibraryA(core_dll_path);
	InterlockedExchange(&g_core_in_module_op, 0);
	if (!h_core_dll)
	{
		printf("[CoreRunner] Failed to load DLL: %s\n", core_dll_path);
		return false;
	}

	s_loaded_core_path = core_dll_path ? core_dll_path : "";
	s_loaded_core_is_pcsx2 = (strstr(core_dll_path, "pcsx2") != NULL) ||
	                         (strstr(core_dll_path, "ps2") != NULL && strstr(core_dll_path, "play") == NULL);

	p_retro_init = (retro_init_t)GetProcAddress(h_core_dll, "retro_init");
	p_retro_deinit = (retro_deinit_t)GetProcAddress(h_core_dll, "retro_deinit");
	p_retro_get_system_info = (retro_get_system_info_t)GetProcAddress(h_core_dll, "retro_get_system_info");
	p_retro_get_system_av_info = (retro_get_system_av_info_t)GetProcAddress(h_core_dll, "retro_get_system_av_info");
	p_retro_set_environment = (retro_set_environment_t)GetProcAddress(h_core_dll, "retro_set_environment");
	p_retro_set_video_refresh = (retro_set_video_refresh_t)GetProcAddress(h_core_dll, "retro_set_video_refresh");
	p_retro_set_audio_sample = (retro_set_audio_sample_t)GetProcAddress(h_core_dll, "retro_set_audio_sample");
	p_retro_set_audio_sample_batch = (retro_set_audio_sample_batch_t)GetProcAddress(h_core_dll, "retro_set_audio_sample_batch");
	p_retro_set_input_poll = (retro_set_input_poll_t)GetProcAddress(h_core_dll, "retro_set_input_poll");
	p_retro_set_input_state = (retro_set_input_state_t)GetProcAddress(h_core_dll, "retro_set_input_state");
	p_retro_set_controller_port_device = (retro_set_controller_port_device_t)GetProcAddress(h_core_dll, "retro_set_controller_port_device");
	p_retro_reset = (retro_reset_t)GetProcAddress(h_core_dll, "retro_reset");
	p_retro_run = (retro_run_t)GetProcAddress(h_core_dll, "retro_run");
	p_retro_serialize_size = (retro_serialize_size_t)GetProcAddress(h_core_dll, "retro_serialize_size");
	p_retro_serialize = (retro_serialize_t)GetProcAddress(h_core_dll, "retro_serialize");
	p_retro_unserialize = (retro_unserialize_t)GetProcAddress(h_core_dll, "retro_unserialize");
	p_retro_load_game = (retro_load_game_t)GetProcAddress(h_core_dll, "retro_load_game");
	p_retro_unload_game = (retro_unload_game_t)GetProcAddress(h_core_dll, "retro_unload_game");
	p_retro_get_memory_data = (retro_get_memory_data_t)GetProcAddress(h_core_dll, "retro_get_memory_data");
	p_retro_get_memory_size = (retro_get_memory_size_t)GetProcAddress(h_core_dll, "retro_get_memory_size");

	// p_retro_get_system_av_info is required by the libretro API just like the
	// three below it - CoreLoadGame() calls it unconditionally further down
	// with no null check of its own (unlike every other core entry point in
	// this file, which is either SEH-guarded or null-checked at its call
	// site). A core DLL missing it is malformed; reject it here rather than
	// null-call it later.
	if (!p_retro_init || !p_retro_run || !p_retro_load_game || !p_retro_get_system_av_info)
	{
		InterlockedExchange(&g_core_in_module_op, 1);
		FreeLibrary(h_core_dll);
		InterlockedExchange(&g_core_in_module_op, 0);
		h_core_dll = NULL;
		return false;
	}

	if (!RetroSetEnvironmentGuarded())
	{
		CoreLogPrintf(RETRO_LOG_ERROR, "[CoreRunner] retro_set_environment falhou: %s", core_dll_path);
		InterlockedExchange(&g_core_in_module_op, 1);
		FreeLibrary(h_core_dll);
		InterlockedExchange(&g_core_in_module_op, 0);
		h_core_dll = NULL;
		return false;
	}
	if (!RetroInitGuarded())
	{
		InterlockedExchange(&g_core_in_module_op, 1);
		FreeLibrary(h_core_dll);
		InterlockedExchange(&g_core_in_module_op, 0);
		h_core_dll = NULL;
		return false;
	}

	// Guarded: a stripped or unusual build can omit any of these, and calling
	// through a null pointer takes the whole app down.
	if (p_retro_set_video_refresh)     p_retro_set_video_refresh(CB_VideoRefresh);
	if (p_retro_set_audio_sample)      p_retro_set_audio_sample(CB_AudioSample);
	if (p_retro_set_audio_sample_batch) p_retro_set_audio_sample_batch(CB_AudioSampleBatch);
	if (p_retro_set_input_poll)        p_retro_set_input_poll(CB_InputPoll);
	if (p_retro_set_input_state)       p_retro_set_input_state(CB_InputState);

	is_core_loaded = true;

	// Substring tests, so the order is load-bearing: "nes" is inside "snes",
	// and checking it first labelled every SNES game as NES. Cores missing
	// from this list got an empty name, which is why the in-game menu fell back
	// to the application title for them and no per-core options could appear.
	EnterCriticalSection(&name_lock);
	if (strstr(core_dll_path, "n64")) loaded_core_name = "Nintendo 64";
	else if (strstr(core_dll_path, "snes")) loaded_core_name = "SNES";
	else if (strstr(core_dll_path, "genesis")) loaded_core_name = "Genesis";
	else if (strstr(core_dll_path, "nes")) loaded_core_name = "NES";
	else if (strstr(core_dll_path, "sms")) loaded_core_name = "Master System";
	else if (strstr(core_dll_path, "pce")) loaded_core_name = "TurboGrafx 16";
	else if (strstr(core_dll_path, "neocd")) loaded_core_name = "NeoGeo CD";
	else if (strstr(core_dll_path, "neogeo")) loaded_core_name = "NeoGeo";
	else if (strstr(core_dll_path, "psx")) loaded_core_name = "PlayStation";
	else if (strstr(core_dll_path, "ps2")) loaded_core_name = "PlayStation 2";
	else if (strstr(core_dll_path, "saturn")) loaded_core_name = "Saturn";
	else if (strstr(core_dll_path, "atari7800")) loaded_core_name = "Atari 7800";
	else if (strstr(core_dll_path, "atari5200") || strstr(core_dll_path, "a5200")) loaded_core_name = "Atari 5200";
	else if (strstr(core_dll_path, "atari2600") || strstr(core_dll_path, "atari")) loaded_core_name = "Atari 2600";
	else if (strstr(core_dll_path, "dreamcast")) loaded_core_name = "Dreamcast";
	else if (strstr(core_dll_path, "gamecube")) loaded_core_name = "GameCube";
	else if (strstr(core_dll_path, "3ds")) loaded_core_name = "Nintendo 3DS";
	else if (strstr(core_dll_path, "nds")) loaded_core_name = "Nintendo DS";
	else if (strstr(core_dll_path, "gba")) loaded_core_name = "Game Boy Advance";
	else if (strstr(core_dll_path, "gb")) loaded_core_name = "Game Boy";
	else if (strstr(core_dll_path, "psp") || strstr(core_dll_path, "ppsspp")) loaded_core_name = "PSP";
	else if (strstr(core_dll_path, "dosbox")) loaded_core_name = "MS-DOS";
	else if (strstr(core_dll_path, "msx")) loaded_core_name = "MSX";
	else if (strstr(core_dll_path, "amiga") || strstr(core_dll_path, "puae")) loaded_core_name = "Amiga";
	else if (strstr(core_dll_path, "c64") || strstr(core_dll_path, "vice")) loaded_core_name = "Commodore 64";
	else if (strstr(core_dll_path, "spectrum") || strstr(core_dll_path, "fuse")) loaded_core_name = "ZX Spectrum";
	else if (strstr(core_dll_path, "32x") || strstr(core_dll_path, "picodrive")) loaded_core_name = "Sega 32X";
	else if (strstr(core_dll_path, "3do") || strstr(core_dll_path, "opera")) loaded_core_name = "3DO";
	else if (strstr(core_dll_path, "jaguar")) loaded_core_name = "Atari Jaguar";
	else if (strstr(core_dll_path, "coleco")) loaded_core_name = "ColecoVision";
	else if (strstr(core_dll_path, "ngp")) loaded_core_name = "Neo Geo Pocket";
	else if (strstr(core_dll_path, "wswan")) loaded_core_name = "WonderSwan";
	else if (strstr(core_dll_path, "lynx")) loaded_core_name = "Atari Lynx";
	else if (strstr(core_dll_path, "pcfx")) loaded_core_name = "PC-FX";
	else if (strstr(core_dll_path, "arcade") || strstr(core_dll_path, "fbneo") || strstr(core_dll_path, "mame")) loaded_core_name = "Arcade";
	LeaveCriticalSection(&name_lock);

	return true;
}

static bool CoreLoadGame(const char* rom_path, bool suppress_toast)
{
	if (!is_core_loaded) return false;

	fs::path p(rom_path);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	struct retro_system_info sys_info = { 0 };
	if (p_retro_get_system_info) p_retro_get_system_info(&sys_info);

	CoreLogPrintf(RETRO_LOG_INFO, "[CORE] '%s' v%s ext=%s",
		sys_info.library_name ? sys_info.library_name : "?",
		sys_info.library_version ? sys_info.library_version : "?",
		sys_info.valid_extensions ? sys_info.valid_extensions : "?");

	is_disc = (sys_info.need_fullpath || ext == ".chd" || ext == ".cue" || ext == ".iso" || ext == ".m3u" || ext == ".pbp" || ext == ".toc" || ext == ".gcm" || ext == ".rvz");

	// rom_path is CoreExecutionThreadProc's own local copy of g_pending_rom,
	// captured once under name_lock at thread start (see there). Re-reading
	// the global here instead used to let a second CoreRequestLoad() - fired
	// while this thread was still finishing up after CoreShutdown()'s bounded
	// wait gave up on a wedged previous core - overwrite g_pending_rom out
	// from under this session, so a new game's save file could inherit the
	// *next* game's platform folder instead of its own.
	fs::path orig_p(rom_path);
	EnterCriticalSection(&name_lock);
	loaded_game_stem = orig_p.stem().string();
	loaded_game_name = orig_p.filename().string();
	LeaveCriticalSection(&name_lock);
	EnterCriticalSection(&name_lock);
	loaded_rom_dir = orig_p.parent_path().string();
	LeaveCriticalSection(&name_lock);
	loaded_system_dir = orig_p.parent_path().filename().string();
	{
		// Fall back to the core name if directory says nothing
		if (loaded_system_dir.empty() || loaded_system_dir == "." ||
			loaded_system_dir == "roms" || loaded_system_dir == "cache" || loaded_system_dir == "app")
		{
			loaded_system_dir = loaded_core_name;
		}
	}

	struct retro_game_info game_info = { 0 };
	std::string abs_rom_path = fs::absolute(p).string();
	game_info.path = abs_rom_path.c_str();

	// A previous game's buffer must never outlive its own unload; if anything
	// slipped through, drop it before taking ownership of a new one.
	FreeLoadedRomData();
	uint8_t* rom_data = nullptr;

	const bool is_archive = (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".tar" || ext == ".gz");

	const bool is_arcade_or_disc = (sys_info.need_fullpath || is_disc || is_archive ||
		loaded_core_name == "Arcade" || loaded_core_name == "MS-DOS" ||
		loaded_core_name == "Dreamcast" || loaded_core_name == "GameCube" ||
		loaded_core_name == "PlayStation 2" || loaded_core_name == "PSP");

	if (!is_arcade_or_disc)
	{
		FILE* f = fopen(rom_path, "rb");
		if (f)
		{
			// ftell returns a signed long and -1 on error; assigning that to a
			// size_t asks malloc for SIZE_MAX bytes.
			_fseeki64(f, 0, SEEK_END);
			int64_t rom_bytes = _ftelli64(f);
			_fseeki64(f, 0, SEEK_SET);
			size_t rom_size = (rom_bytes > 0) ? (size_t)rom_bytes : 0;

			rom_data = rom_size ? (uint8_t*)malloc(rom_size) : NULL;
			if (rom_data)
			{
				fread(rom_data, 1, rom_size, f);
				game_info.data = rom_data;
				game_info.size = rom_size;
			}
			fclose(f);
		}
	}

	// Handed over, not released: the core may have kept this pointer (see
	// g_loaded_rom_data). RetroUnloadGameGuarded frees it once the core is
	// actually done with it.
	g_loaded_rom_data = rom_data;

	bool ok = RetroLoadGameGuarded(&game_info);

	if (!ok)
	{
		// The load failed, so retro_unload_game() will not be called for it and
		// nothing can still be holding the buffer - release it here instead.
		FreeLoadedRomData();
		CoreLogPrintf(RETRO_LOG_ERROR, "[CoreRunner] Core failed to load game: %s", rom_path);

		if (!suppress_toast)
		{
			// Name the file the user has to go find. "BIOS required" alone sent
			// them hunting with no idea what for.
			const char* needed = NULL;
			const char* sys = loaded_core_name.c_str();

			if (strstr(sys, "Mega CD") || (strstr(sys, "Genesis") && ext != ".md" && ext != ".gen" && ext != ".bin"))
				needed = "bios_CD_U.bin (ou _E/_J)";
			else if (strstr(sys, "TurboGrafx") || strstr(sys, "PC Engine"))
				needed = "syscard3.pce";
			else if (strstr(sys, "PlayStation"))
				needed = "scph5501.bin (ou 5500/5502)";
			else if (strstr(sys, "Saturn"))
				needed = "sega_101.bin / mpr-17933.bin";
			else if (strstr(sys, "NeoGeo CD"))
				needed = "bios/neocd/*.rom";
			else if (strstr(sys, "NeoGeo"))
				needed = "neogeo.zip";
			else if (strstr(sys, "Arcade") || strstr(sys, "Dreamcast"))
				needed = "naomi.zip / awbios.zip (Naomi/Atomiswave)";

			if (strstr(sys, "Dreamcast") || strstr(sys, "GameCube"))
			{
				CoreSetToast("MUDE VIDEO DRIVER PARA OPENGL EM SETTINGS > VIDEO", 300);
			}
			else if (strstr(sys, "Nintendo 3DS"))
			{
				// An encrypted eShop title Citra cannot decrypt without the
				// user's own console keys is the most common reason this core
				// refuses, and "failed to load" alone left nobody any idea a
				// key file was even involved. But it is not the only reason,
				// and the message used to be shown for every failure: someone
				// whose keys were already in place spent time hunting a file
				// they had, while the real cause sat in the log one line away
				// ("Failed to set HW renderer"). Only blame the keys when they
				// are actually absent.
				std::error_code kec;
				if (!fs::exists("saves/3DS/Citra/sysdata/aes_keys.txt", kec))
				{
					CoreSetToast("3DS CIFRADO: falta saves/3DS/Citra/sysdata/aes_keys.txt", 360);
				}
				else
				{
					CoreSetToast("3DS: FALHA AO CARREGAR (chaves presentes) - VEJA karamelo.log", 300);
				}
			}
			else if (needed)
			{
				char msg[192];
				snprintf(msg, sizeof(msg), "BIOS AUSENTE EM bios/: %s", needed);
				CoreSetToast(msg, 360);
			}
			else
			{
				CoreSetToast("FALHA AO CARREGAR - VEJA karamelo.log", 240);
			}
		}
		// The core can have already called SET_MEMORY_MAPS from inside this
		// same, ultimately-failed retro_load_game() before faulting/refusing -
		// is_game_loaded is still false here so CoreUnload() won't release it.
		CoreReleaseMemoryMap();
		return false;
	}

	// Port devices are declared only now, after retro_load_game. Doing it
	// earlier crashed Beetle PSX: the core builds its emulated input ports
	// while loading content, so a set_controller_port_device before that
	// walks a port object that does not exist yet - psx.dll faulted reading
	// offset 0x48 of a null pointer, right after logging the controller it
	// thought it was configuring. RetroArch also sets these post-load.
	SetPortDevicesGuarded();

	struct retro_system_av_info av_info = { 0 };
	if (!RetroGetSystemAvInfoGuarded(&av_info))
	{
		CoreLogPrintf(RETRO_LOG_ERROR,
			"[CoreRunner] Core faulted inside retro_get_system_av_info: %s", rom_path);
		if (!suppress_toast)
			CoreSetToast("FALHA AO CARREGAR - VEJA karamelo.log", 240);
		RetroUnloadGameGuarded();
		// is_game_loaded is still false here, so CoreUnload()'s own
		// CoreReleaseMemoryMap() call (gated on is_game_loaded) never runs -
		// a core that registered SET_MEMORY_MAPS before faulting here would
		// otherwise leave its descriptor table behind for the next, unrelated
		// game to inherit.
		CoreReleaseMemoryMap();
		return false;
	}

	core_fb_width = av_info.geometry.base_width > 0 ? av_info.geometry.base_width : 320;
	core_fb_height = av_info.geometry.base_height > 0 ? av_info.geometry.base_height : 240;
	core_native_fps = av_info.timing.fps > 0.0 ? av_info.timing.fps : 60.0;
	core_target_fps = core_native_fps;

	CoreLogPrintf(RETRO_LOG_INFO,
		"[AV] geom=%ux%u fps=%.4f sample_rate=%.0f",
		av_info.geometry.base_width, av_info.geometry.base_height,
		av_info.timing.fps, av_info.timing.sample_rate);

	InitAudio((int)av_info.timing.sample_rate);
	core_native_sample_rate = g_core_sample_rate;
	ApplyDisplaySync();

	g_video_diag_frames = 0;
	g_render_diag_done = false;

	// Reset 480i deinterlace state
	g_deinterlace_have_prev = false;
	g_deinterlace_field = 0;



	if (HwIsActive() || VkHwIsActive() || D3D11HwIsActive())
	{
		unsigned hw_w = av_info.geometry.max_width  ? av_info.geometry.max_width  : core_fb_width;
		unsigned hw_h = av_info.geometry.max_height ? av_info.geometry.max_height : core_fb_height;
		if (hw_w > MAX_FB_WIDTH)  hw_w = MAX_FB_WIDTH;
		if (hw_h > MAX_FB_HEIGHT) hw_h = MAX_FB_HEIGHT;

		CoreLogPrintf(RETRO_LOG_INFO,
			"[HW] geometria do core: base=%ux%u max=%ux%u -> superficie %ux%u",
			av_info.geometry.base_width, av_info.geometry.base_height,
			av_info.geometry.max_width, av_info.geometry.max_height, hw_w, hw_h);

		bool surface_ok = D3D11HwIsActive() ? D3D11HwEnsureSurface(hw_w, hw_h)
		                : VkHwIsActive()    ? VkHwEnsureSurface(hw_w, hw_h)
		                                    : HwEnsureSurface(hw_w, hw_h);
		if (surface_ok)
		{
			// The core allocates its GL/Vulkan/D3D11 resources here, so it has
			// to happen after load and before the first retro_run. Only
			// Vulkan's reset can fail from a known cause (context negotiation,
			// checked above before we get here) - D3D11HwContextReset/
			// HwContextReset returning false means an actually unexpected
			// native fault, not something we already know how to avoid.
			bool using_hw_reset = VkHwIsActive() || D3D11HwIsActive();
			bool context_ok = D3D11HwIsActive() ? D3D11HwContextReset()
			                : VkHwIsActive()    ? VkHwContextReset()
			                                    : (HwContextReset(), true);
			if (using_hw_reset && !context_ok)
			{
				// Rede de seguranca para o core que crasha dentro do proprio
				// context_reset (capturado pelo SEH de VkHwContextReset e
				// D3D11HwContextReset). Antes havia, acima, uma recusa que
				// barrava o carregamento de qualquer core que pedisse
				// negociacao Vulkan; ela existia so porque a negociacao nao
				// era implementada, e saiu junto com essa limitacao.
				CoreLogPrintf(RETRO_LOG_ERROR,
					"[HW] context_reset falhou/crashou para '%s'",
					loaded_core_name.c_str());
				if (!suppress_toast)
					CoreSetToast("MUDE VIDEO DRIVER PARA OPENGL EM SETTINGS > VIDEO", 300);
				RetroUnloadGameGuarded();
				CoreReleaseMemoryMap();
				return false;
			}
		}
	}

	is_game_loaded = true;
	return true;
}

static void CoreUnload()
{
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Inicio: is_game_loaded=%d is_core_loaded=%d", is_game_loaded, is_core_loaded);

	if (audio_thread_running)
	{
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Parando audio thread...");
		audio_thread_running = false;
		if (h_audio_event) SetEvent(h_audio_event);
		if (h_audio_thread)
		{
			if (WaitForSingleObject(h_audio_thread, 1000) == WAIT_TIMEOUT)
			{
				// Mesmo raciocinio do CoreShutdown: abandonar em vez de matar.
				// Ver o comentario la para o porque.
				audio_abandoned = true;
				CoreLogPrintf(RETRO_LOG_WARN,
					"[CoreUnload] audio thread nao respondeu a tempo (driver travado?) - abandonando thread e stream (vazam ate fechar o app)");
			}
			CloseHandle(h_audio_thread);
			h_audio_thread = NULL;
		}
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Audio thread finalizada.");
	}

	if (g_audio_stream)
	{
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Fechando audio stream...");
		// Destroying the stream also closes the device it was opened
		// against - SDL_OpenAudioDeviceStream's own contract.
		if (!audio_abandoned)
			SDL_DestroyAudioStream(g_audio_stream);
		g_audio_stream = nullptr;
		audio_initialized = false;
		audio_abandoned = false;
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] audio stream finalizado.");
	}

	if (h_audio_event)
	{
		CloseHandle(h_audio_event);
		h_audio_event = NULL;
	}

	g_resample_phase = 0.0;
	memset(g_hist_l, 0, sizeof(g_hist_l));
	memset(g_hist_r, 0, sizeof(g_hist_r));
	InterlockedExchange(&g_ring_write_pos, 0);
	InterlockedExchange(&g_ring_read_pos, 0);

	if (is_game_loaded)
	{
		RaOnGameUnload();
		CoreReleaseMemoryMap();

		EnterCriticalSection(&options_lock);
		g_core_defaults.clear();
		g_core_defaults_order.clear();
		g_core_option_raw.clear();
		LeaveCriticalSection(&options_lock);

		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Chamando RetroUnloadGameGuarded...");
		RetroUnloadGameGuarded();
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] RetroUnloadGameGuarded concluido.");
		is_game_loaded = false;
	}

	// Uma linha por backend, e nao uma para os quatro juntos. Cada um destes
	// chama de volta o context_destroy do core, que e codigo de terceiro e pode
	// nao voltar: com um unico log antes do bloco, o que se via era "Chamando
	// HwContextDestroy..." e silencio, sem dizer qual dos quatro prendeu.
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] HwContextDestroy (GL)...");
	HwContextDestroy();
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] VkHwContextDestroy...");
	VkHwContextDestroy();
	// Belt and suspenders alongside the CoreLoad() clear before the next
	// LoadLibrary: this DLL's negotiation interface has no business surviving
	// its own unload, whether or not another game gets loaded afterward.
	VkHwClearNegotiationInterface();
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] D3D11HwContextDestroy...");
	D3D11HwContextDestroy();
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] HwReleaseCurrent...");
	HwReleaseCurrent();
	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] teardown de video concluido.");

	if (is_core_loaded)
	{
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Chamando RetroDeinitGuarded...");
		RetroDeinitGuarded();
		CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] RetroDeinitGuarded concluido.");
		is_core_loaded = false;
	}

	EnterCriticalSection(&name_lock);
	loaded_system_dir.clear();
	LeaveCriticalSection(&name_lock);

	// Which cores stay mapped after unloading.
	//
	// Leaving a DLL mapped is not free: the core's globals survive with it, and
	// a core that checks "am I already initialised?" refuses the next
	// retro_load_game. That is exactly what made loading the same arcade game a
	// second time fail and a third time crash - MAME ran the game once, was
	// unloaded but never freed, and every later attempt was handed a core that
	// still thought it was running. The chain then walked every candidate,
	// found none, and kept going.
	//
	// So the list is now only the cores that genuinely cannot be freed, which
	// are the ones that leave threads of their own behind: PCSX2, PPSSPP and
	// the Dreamcast/GameCube pair. The MAME builds and the generic "Arcade"
	// name came off it - nothing recorded why they had been added, the commit
	// that introduced this bundled it into an unrelated feature, and the
	// justification written a few lines below in RecoverAfterKilledCore is
	// about a thread that was killed mid-module, which is a different path
	// that still keeps its mapping.
	//
	// If an arcade core starts faulting inside FreeLibrary, put "mame" back and
	// the reload bug comes back with it - that is the trade being made here.
	bool skip_free = false;
	{
		std::string lower_path = s_loaded_core_path;
		for (char& c : lower_path) c = (char)tolower((unsigned char)c);

		// Só o GameCube continua aqui. Cada core que saiu desta lista parou de
		// quebrar ao ser recarregado: manter a DLL mapeada preserva os globais
		// do core, e a inicialização seguinte reencontra estado de uma sessão
		// que já terminou. Foi assim com o MAME (recusava o segundo load), com
		// o Flycast (DEBUGBREAK no segundo retro_init) e com o PCSX2, que na
		// terceira carga escreveu num ponteiro de fastmem morto e levou o
		// processo junto - 0xC0000005 em 0x7FFEA0000000, logo após
		// "Resetting host memory for virtual systems".
		// MAME, FBNeo e Flycast saíram desta lista com prova: os três voltaram
		// a suportar recarga depois disso, verificado pela bateria.
		//
		// PCSX2 e PPSSPP continuam aqui, e a tentativa de tirá-los foi
		// desfeita. Não porque tenha piorado, mas porque não melhorou nada
		// mensurável e o risco não é simétrico: o que esta lista evita é
		// deadlock de loader lock no descarregamento, e o travamento que este
		// projeto tem hoje - a thread do core morta por TerminateThread
		// segurando um lock - é exatamente dessa família. Trocar um crash
		// conhecido por um deadlock possível, sem evidência, não é troca boa.
		//
		// O que falta para decidir: carregar um jogo de PS2 três vezes seguidas
		// no app com janela, com e sem a DLL liberada. A bateria headless não
		// serve - o PCSX2 trava nela por falta de contexto de GPU, antes de
		// chegar no ponto que interessa.
		if (s_loaded_core_is_pcsx2 ||
		    loaded_core_name == "PlayStation 2" ||
		    loaded_core_name == "PSP" ||
		    loaded_core_name == "GameCube" ||
		    lower_path.find("pcsx2") != std::string::npos ||
		    lower_path.find("ppsspp") != std::string::npos ||
		    lower_path.find("psp") != std::string::npos)
		{
			skip_free = true;
		}
	}

	if (h_core_dll)
	{
		if (skip_free)
		{
			CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Mantendo DLL mapeada para evitar loader lock deadlock (%s)", s_loaded_core_path.c_str());
		}
		else
		{
			CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Chamando FreeLibrary(h_core_dll)...");
			InterlockedExchange(&g_core_in_module_op, 1);
			FreeLibrary(h_core_dll);
			InterlockedExchange(&g_core_in_module_op, 0);
			CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] FreeLibrary concluido.");
		}
		h_core_dll = NULL;
	}

	ResetCoreFunctionPointers();
	s_loaded_core_is_pcsx2 = false;
	s_loaded_core_path.clear();

	CoreLogPrintf(RETRO_LOG_INFO, "[CoreUnload] Concluido com sucesso.");
}


static void DoReset()
{
	if (CoreIsRunning() && p_retro_reset)
	{
		if (RetroResetGuarded())
			CoreSetToast("CORE RESTARTED", 90);
		else
			CoreSetToast("ERROR: CORE CRASHED ON RESET", 120);
	}
}

static bool DoSaveState(int slot)
{
	if (!CoreIsRunning() || !p_retro_serialize || !p_retro_serialize_size)
	{
		CoreSetToast("SAVESTATE UNAVAILABLE ON THIS CORE", 120);
		return false;
	}

	size_t sz = RetroSerializeSizeGuarded();
	if (sz == 0)
	{
		CoreSetToast("ERROR: INVALID STATE SIZE", 120);
		return false;
	}

	void* buf = malloc(sz);
	if (!buf) return false;

	if (!RetroSerializeGuarded(buf, sz))
	{
		free(buf);
		CoreSetToast("ERROR SAVING STATE", 120);
		return false;
	}

	fs::path save_dir = fs::absolute("saves/" + loaded_system_dir);
	fs::create_directories(save_dir);

	char slot_filename[512];
	snprintf(slot_filename, sizeof(slot_filename), "%s.state%d", GetLoadedGameStemSafe().c_str(), slot);
	fs::path save_file = save_dir / slot_filename;

	FILE* f = fopen(save_file.string().c_str(), "wb");
	if (!f)
	{
		free(buf);
		CoreSetToast("DISK ERROR WHILE SAVING STATE", 120);
		return false;
	}

	fwrite(buf, 1, sz, f);
	fclose(f);
	free(buf);

	char toast_str[128];
	snprintf(toast_str, sizeof(toast_str), "STATE SAVED [SLOT %d]", slot);
	CoreSetToast(toast_str, 120);
	return true;
}

static bool DoLoadState(int slot)
{
	if (!CoreIsRunning() || !p_retro_unserialize || !p_retro_serialize_size)
	{
		CoreSetToast("LOADSTATE UNAVAILABLE ON THIS CORE", 120);
		return false;
	}

	fs::path save_dir = fs::absolute("saves/" + loaded_system_dir);
	char slot_filename[512];
	snprintf(slot_filename, sizeof(slot_filename), "%s.state%d", GetLoadedGameStemSafe().c_str(), slot);
	fs::path save_file = save_dir / slot_filename;

	FILE* f = fopen(save_file.string().c_str(), "rb");
	if (!f)
	{
		char toast_str[128];
		snprintf(toast_str, sizeof(toast_str), "SLOT %d EMPTY (NO SAVE FOUND)", slot);
		CoreSetToast(toast_str, 120);
		return false;
	}

	_fseeki64(f, 0, SEEK_END);
	int64_t state_bytes = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	if (state_bytes <= 0) { fclose(f); return false; }
	size_t sz = (size_t)state_bytes;

	void* buf = malloc(sz);
	if (!buf)
	{
		fclose(f);
		return false;
	}

	fread(buf, 1, sz, f);
	fclose(f);

	bool ok = RetroUnserializeGuarded(buf, sz);
	free(buf);

	if (ok)
	{
		char toast_str[128];
		snprintf(toast_str, sizeof(toast_str), "STATE LOADED [SLOT %d]", slot);
		CoreSetToast(toast_str, 120);
		return true;
	}
	else
	{
		CoreSetToast("ERROR LOADING STATE", 120);
		return false;
	}
}


// -------------------------------------------------------------
// State commands are queued, not executed inline: they must not run while the
// core thread is inside retro_run.
// -------------------------------------------------------------
#define CORE_CMD_NONE  0
#define CORE_CMD_RESET 1
#define CORE_CMD_SAVE  2
#define CORE_CMD_LOAD  3

// cmd and slot used to be two separate InterlockedExchange calls, which is not
// one atomic update: RaEventHandler can post CORE_CMD_RESET from the core
// thread itself (RaDoFrame -> rc_client_do_frame -> RC_CLIENT_EVENT_RESET) at
// the same time the UI thread posts CORE_CMD_SAVE/LOAD from a hotkey. The two
// threads' writes to g_pending_cmd and g_pending_cmd_slot could interleave, so
// the command that ran could end up paired with the other call's slot -
// silently saving/loading the wrong slot, or dropping one command entirely.
// Packing both into a single LONG makes the update one indivisible exchange.
static volatile LONG g_pending_cmd_packed = 0; // low byte = cmd, next byte = slot

static void ProcessPendingCommand()
{
	LONG packed = InterlockedExchange(&g_pending_cmd_packed, 0);
	int cmd = packed & 0xFF;
	if (cmd == CORE_CMD_NONE) return;
	int slot = (packed >> 8) & 0xFF;

	if (cmd == CORE_CMD_RESET) DoReset();
	else if (cmd == CORE_CMD_SAVE) DoSaveState(slot);
	else if (cmd == CORE_CMD_LOAD) DoLoadState(slot);
}

static bool PostCoreCommand(LONG cmd, int slot)
{
	if (!CoreIsRunning()) return false;

	// RetroAchievements hardcore forbids savestates. Silently allowing one can
	// get the player's account flagged, so refuse and say why.
	if ((cmd == CORE_CMD_SAVE || cmd == CORE_CMD_LOAD) && RaIsHardcoreActive())
	{
		CoreSetToast("SAVESTATE LOCKED (RA HARDCORE)", 200);
		return false;
	}
	InterlockedExchange(&g_pending_cmd_packed, (cmd & 0xFF) | ((slot & 0xFF) << 8));
	return true;
}

void CoreReset()               { PostCoreCommand(CORE_CMD_RESET, 0); }
bool CoreSaveState(int slot)   { return PostCoreCommand(CORE_CMD_SAVE, slot); }
bool CoreLoadState(int slot)   { return PostCoreCommand(CORE_CMD_LOAD, slot); }

int CoreGetSelectedSlot() { return selected_state_slot; }
void CoreSetSelectedSlot(int slot)
{
	if (slot >= 0 && slot <= 9)
	{
		selected_state_slot = slot;
		char toast_str[64];
		snprintf(toast_str, sizeof(toast_str), "ACTIVE SLOT: %d", slot);
		CoreSetToast(toast_str, 90);
	}
}

bool CoreTakeScreenshot()
{
	if (!CoreIsRunning() || core_fb_width == 0 || core_fb_height == 0) return false;

	fs::create_directories("screenshots");

	time_t now = time(NULL);
	struct tm* tm_now = localtime(&now);

	char date_str[64];
	strftime(date_str, sizeof(date_str), "%Y%m%d_%H%M%S", tm_now);

	char filename[512];
	snprintf(filename, sizeof(filename), "screenshots/%s_%s.bmp", GetLoadedGameStemSafe().c_str(), date_str);

	FILE* f = fopen(filename, "wb");
	if (!f) return false;

	// Geometry must come from the frame we are about to read, not from the
	// globals: a core that changes resolution between frames (PS1 and N64 do it
	// routinely) leaves core_fb_* describing a different buffer than the one
	// AcquireFrame just handed over, and the indexing below then walks off the
	// end of it.
	int shot_w = 0, shot_h = 0;
	const uint32_t* src_fb = AcquireFrame(&shot_w, &shot_h);
	if (!src_fb || shot_w <= 0 || shot_h <= 0) { fclose(f); return false; }

	uint32_t width = (uint32_t)shot_w;
	uint32_t height = (uint32_t)shot_h;
	uint32_t row_bytes = width * 3;
	uint32_t pad = (4 - (row_bytes % 4)) % 4;
	uint32_t image_size = (row_bytes + pad) * height;

	unsigned char file_hdr[14] = { 'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0 };
	uint32_t file_size = 54 + image_size;
	file_hdr[2] = (unsigned char)(file_size);
	file_hdr[3] = (unsigned char)(file_size >> 8);
	file_hdr[4] = (unsigned char)(file_size >> 16);
	file_hdr[5] = (unsigned char)(file_size >> 24);

	unsigned char info_hdr[40] = { 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0 };
	info_hdr[4] = (unsigned char)(width);
	info_hdr[5] = (unsigned char)(width >> 8);
	info_hdr[6] = (unsigned char)(width >> 16);
	info_hdr[7] = (unsigned char)(width >> 24);
	info_hdr[8] = (unsigned char)(height);
	info_hdr[9] = (unsigned char)(height >> 8);
	info_hdr[10] = (unsigned char)(height >> 16);
	info_hdr[11] = (unsigned char)(height >> 24);

	fwrite(file_hdr, 1, 14, f);
	fwrite(info_hdr, 1, 40, f);

	unsigned char pad_bytes[3] = { 0, 0, 0 };
	for (int y = (int)height - 1; y >= 0; y--)
	{
		for (unsigned x = 0; x < width; x++)
		{
			uint32_t rgb = src_fb[y * width + x];
			unsigned char bgr[3] = { (unsigned char)(rgb & 0xFF), (unsigned char)((rgb >> 8) & 0xFF), (unsigned char)((rgb >> 16) & 0xFF) };
			fwrite(bgr, 1, 3, f);
		}
		if (pad > 0) fwrite(pad_bytes, 1, pad, f);
	}

	fclose(f);
	CoreSetToast("SCREENSHOT SAVED", 120);
	return true;
}

// -------------------------------------------------------------
// State-of-the-Art CRT Shaders & Presentation Pipeline
// -------------------------------------------------------------
#define BEAM_DIST_STEPS 64
#define BEAM_LUM_STEPS  32

// One destination column: the two source columns it straddles, plus the
// sharp-bilinear weight between them.
struct XSample { int x0, x1; uint8_t w; uint8_t wsoft; };

void CoreRender(uint32_t* dest_buffer, int dest_w, int dest_h, int aspect_mode, int filter_mode)
{
	// Cost of the filter itself, so a slow game can be blamed on the right
	// thing: the core emulating, or us painting.
	LARGE_INTEGER t_begin;
	QueryPerformanceCounter(&t_begin);
	struct RenderTimer {
		LARGE_INTEGER b;
		~RenderTimer() {
			LARGE_INTEGER e, f;
			QueryPerformanceCounter(&e);
			QueryPerformanceFrequency(&f);
			g_render_us_total += (double)(e.QuadPart - b.QuadPart) * 1e6 / (double)f.QuadPart;
			g_render_count++;
		}
	} rt{ t_begin };
	if (!CoreIsRunning() || !dest_buffer || core_fb_width == 0 || core_fb_height == 0) return;
	if (dest_w <= 0 || dest_h <= 0) return;

	int target_w = dest_w;
	int target_h = dest_h;
	int offset_x = 0;
	int offset_y = 0;

	ComputeViewport(aspect_mode, dest_w, dest_h, &offset_x, &offset_y, &target_w, &target_h);

	InterlockedExchange(&g_vp_x, offset_x);
	InterlockedExchange(&g_vp_y, offset_y);
	InterlockedExchange(&g_vp_w, target_w);
	InterlockedExchange(&g_vp_h, target_h);
	InterlockedExchange(&g_vp_dest_w, dest_w);
	InterlockedExchange(&g_vp_dest_h, dest_h);

	if (target_w <= 0 || target_h <= 0) return;

	// Clear the pillar/letterbox bars. CoreRender only writes inside the target
	// rect, so without this the bars keep whatever was in the buffer before.
	if (offset_x > 0 || offset_y > 0)
	{
		for (int y = 0; y < dest_h; y++)
		{
			uint32_t* row = dest_buffer + (size_t)y * dest_w;
			if (y < offset_y || y >= offset_y + target_h)
			{
				memset(row, 0, (size_t)dest_w * sizeof(uint32_t));
			}
			else if (offset_x > 0)
			{
				memset(row, 0, (size_t)offset_x * sizeof(uint32_t));
				int right = offset_x + target_w;
				if (right < dest_w)
					memset(row + right, 0, (size_t)(dest_w - right) * sizeof(uint32_t));
			}
		}
	}

	// Geometry comes from the frame itself, not from a global the core thread
	// may have already moved on from.
	int fbw = 0, fbh = 0;
	const uint32_t* src_fb = AcquireFrame(&fbw, &fbh);
	if (fbw <= 0 || fbh <= 0) return;

	if (!g_render_diag_done && g_video_diag_frames > 2)
	{
		g_render_diag_done = true;
		uint32_t centre = src_fb[(size_t)(fbh / 2) * fbw + (fbw / 2)];
		CoreLogPrintf(RETRO_LOG_INFO,
			"[RENDER] buf=%d src=%dx%d dest=%dx%d target=%dx%d off=%d,%d filter=%d centre=%06X",
			g_fb_front, fbw, fbh, dest_w, dest_h, target_w, target_h,
			offset_x, offset_y, filter_mode, centre & 0x00FFFFFFu);
	}

	// ---------------------------------------------------------
	// Curved-tube filter: needs true per-pixel warping, so it keeps the
	// floating point path. Every other mode maps linearly and goes through the
	// integer path below.
	// ---------------------------------------------------------
	if (filter_mode == 3)
	{
		// The warp geometry (which source pixel each output pixel samples, its
		// vignette/scanline multiplier, or whether it's outside the tube and
		// gets the bezel color) depends only on target_w/target_h/offset_x/
		// offset_y/dest_w/dest_h/fbw/fbh - not on the frame's actual pixel
		// content. Those dimensions only change on a window resize or a core
		// resolution change, essentially never frame-to-frame, so recomputing
		// this per pixel every single frame (the float warp math below) was
		// pure waste - 19 ms a frame at a large canvas size, most of it spent
		// re-deriving the same lookup table this cache now builds once and
		// reuses until a dimension actually changes.
		struct CrtLutEntry { int32_t src_idx; float mul; }; // src_idx < 0 = bezel
		struct CrtLutCache {
			std::vector<CrtLutEntry> entries;
			int target_w = -1, target_h = -1, offset_x = -1, offset_y = -1;
			int dest_w = -1, dest_h = -1, fbw = -1, fbh = -1;
		};
		static CrtLutCache lut;

		bool dims_changed = (lut.target_w != target_w || lut.target_h != target_h ||
			lut.offset_x != offset_x || lut.offset_y != offset_y ||
			lut.dest_w != dest_w || lut.dest_h != dest_h ||
			lut.fbw != fbw || lut.fbh != fbh);

		if (dims_changed)
		{
			lut.entries.assign((size_t)target_w * target_h, CrtLutEntry{ -1, 1.0f });

			auto build_band = [&](int y_from, int y_to)
			{
				for (int y = y_from; y < y_to; y++)
				{
					int dst_y = offset_y + y;
					float ny = ((float)y / (float)target_h) * 2.0f - 1.0f;
					CrtLutEntry* row = &lut.entries[(size_t)y * target_w];

					for (int x = 0; x < target_w; x++)
					{
						int dst_x = offset_x + x;
						if (dst_y < 0 || dst_y >= dest_h || dst_x < 0 || dst_x >= dest_w)
							continue; // stays the -1/bezel-skip default, never sampled below

						float nx = ((float)x / (float)target_w) * 2.0f - 1.0f;
						float dist = nx * nx + ny * ny;
						float cnx = nx * (1.0f + dist * 0.08f);
						float cny = ny * (1.0f + dist * 0.08f);

						if (fabsf(cnx) > 1.02f || fabsf(cny) > 1.02f)
						{
							row[x].src_idx = -2; // -2 = draw the bezel color; -1 = skip entirely
							continue;
						}

						float vignette = (1.0f - cnx * cnx * 0.15f) * (1.0f - cny * cny * 0.15f);
						if (vignette < 0.2f) vignette = 0.2f;
						if (y & 1) vignette *= 0.75f; // odd-row scanline darkening, folded in

						int src_x = (int)(((cnx + 1.0f) * 0.5f) * (float)fbw);
						int src_y = (int)(((cny + 1.0f) * 0.5f) * (float)fbh);
						if (src_x < 0) src_x = 0; else if (src_x >= fbw) src_x = fbw - 1;
						if (src_y < 0) src_y = 0; else if (src_y >= fbh) src_y = fbh - 1;

						row[x].src_idx = src_y * fbw + src_x;
						row[x].mul = vignette;
					}
				}
			};

			unsigned warp_hw = std::thread::hardware_concurrency();
			int warp_bands = (warp_hw > 1) ? (int)((warp_hw > 8) ? 8 : warp_hw) : 1;
			if (target_h < 64) warp_bands = 1;

			if (warp_bands <= 1)
			{
				build_band(0, target_h);
			}
			else
			{
				const int wstep = (target_h + warp_bands - 1) / warp_bands;
				std::vector<std::thread> wpool;
				wpool.reserve((size_t)warp_bands - 1);
				for (int b = 1; b < warp_bands; b++)
				{
					const int a = b * wstep;
					const int z = (a + wstep < target_h) ? a + wstep : target_h;
					if (a < z) wpool.emplace_back(build_band, a, z);
				}
				build_band(0, (wstep < target_h) ? wstep : target_h);
				for (auto& t : wpool) t.join();
			}

			lut.target_w = target_w; lut.target_h = target_h;
			lut.offset_x = offset_x; lut.offset_y = offset_y;
			lut.dest_w = dest_w; lut.dest_h = dest_h;
			lut.fbw = fbw; lut.fbh = fbh;
		}

		// Hot path: no trig-like math left, just a lookup and a multiply.
		auto sample_band = [&](int y_from, int y_to)
		{
			for (int y = y_from; y < y_to; y++)
			{
				int dst_y = offset_y + y;
				if (dst_y < 0 || dst_y >= dest_h) continue;
				const CrtLutEntry* row = &lut.entries[(size_t)y * target_w];

				for (int x = 0; x < target_w; x++)
				{
					int dst_x = offset_x + x;
					if (dst_x < 0 || dst_x >= dest_w) continue;

					int32_t idx = row[x].src_idx;
					if (idx == -1) continue;
					if (idx == -2)
					{
						dest_buffer[dst_y * dest_w + dst_x] = 0x00040404; // CRT bezel edge
						continue;
					}

					uint32_t col = src_fb[(size_t)idx];
					uint32_t r = (col >> 16) & 0xFF;
					uint32_t g = (col >> 8) & 0xFF;
					uint32_t b = col & 0xFF;

					float mul = row[x].mul;
					r = (uint32_t)(r * mul);
					g = (uint32_t)(g * mul);
					b = (uint32_t)(b * mul);

					dest_buffer[dst_y * dest_w + dst_x] = (r << 16) | (g << 8) | b;
				}
			}
		};

		unsigned hw = std::thread::hardware_concurrency();
		int bands = (hw > 1) ? (int)((hw > 8) ? 8 : hw) : 1;
		if (target_h < 64) bands = 1;

		if (bands <= 1)
		{
			sample_band(0, target_h);
		}
		else
		{
			const int step = (target_h + bands - 1) / bands;
			std::vector<std::thread> pool;
			pool.reserve((size_t)bands - 1);
			for (int b = 1; b < bands; b++)
			{
				const int a = b * step;
				const int z = (a + step < target_h) ? a + step : target_h;
				if (a < z) pool.emplace_back(sample_band, a, z);
			}
			sample_band(0, (step < target_h) ? step : target_h);
			for (auto& t : pool) t.join();
		}
		return;
	}

	// ---------------------------------------------------------
	// Scaling and CRT emulation.
	//
	// Three things separate a real scanline from a dark stripe, and all three
	// were missing:
	//
	//  1. The beam must follow the SOURCE line. Darkening every other output
	//     row gives a pattern with no relation to the game - 224 source lines
	//     stretched over 720 is a 3.21x scale, so odd/even output rows are
	//     noise. Here each output row knows where it falls inside its source
	//     line and is attenuated by its distance from that line's centre.
	//
	//  2. Attenuation has to happen in LINEAR light. Multiplying sRGB values
	//     by 0.5 only makes the picture muddy; a CRT loses light, and light
	//     is linear.
	//
	//  3. The beam widens with brightness. That is where a CRT's glow comes
	//     from: bright lines bloom into the gap, dark ones stay thin. A
	//     constant multiplier can never look like one.
	//
	// Horizontal sampling is sharp-bilinear: flat across each source pixel
	// with a one-pixel ramp at the boundary, so a non-integer scale stops
	// producing pixels of uneven width.
	// ---------------------------------------------------------
	static uint16_t s_srgb_to_lin[256];
	static uint8_t  s_lin_to_srgb[4096];
	static uint8_t  s_beam[BEAM_DIST_STEPS][BEAM_LUM_STEPS];
	static bool     s_crt_tables_ready = false;

	if (!s_crt_tables_ready)
	{
		for (int i = 0; i < 256; i++)
		{
			double c = i / 255.0;
			double l = (c <= 0.04045) ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
			s_srgb_to_lin[i] = (uint16_t)(l * 4095.0 + 0.5);
		}
		for (int i = 0; i < 4096; i++)
		{
			double l = i / 4095.0;
			double c = (l <= 0.0031308) ? (l * 12.92) : (1.055 * pow(l, 1.0 / 2.4) - 0.055);
			s_lin_to_srgb[i] = (uint8_t)(c * 255.0 + 0.5);
		}
		for (int d = 0; d < BEAM_DIST_STEPS; d++)
		{
			double dist = d / (double)(BEAM_DIST_STEPS - 1);   // 0 = centre of the line
			for (int L = 0; L < BEAM_LUM_STEPS; L++)
			{
				double lum = L / (double)(BEAM_LUM_STEPS - 1);
				// A wider floor and a gentler falloff. The old 0.38 minimum put
				// a hard-edged dark band under every dim line.
				double width = 0.62 + 0.55 * lum;               // bright beams bloom
				double t = dist / width;
				double a = exp(-t * t * 1.35);
				s_beam[d][L] = (uint8_t)(a * 255.0 + 0.5);
			}
		}
		s_crt_tables_ready = true;
	}

	// Horizontal map: the two source columns plus a sharp-bilinear weight.
	//
	// Two slots, not one. While the OSD is open this function runs twice per
	// frame at two different widths (the native-res surface and the 640x360
	// canvas), and a single-slot cache thrashed - rebuilding a 960-entry table
	// twice every frame, with a divide and a floor per entry.
	struct XMapSlot { std::vector<XSample> map; int target_w = -1; int fbw = -1; };
	static XMapSlot slots[2];
	static int slot_next = 0;

	XMapSlot* slot = NULL;
	for (int i = 0; i < 2; i++)
		if (slots[i].target_w == target_w && slots[i].fbw == fbw) { slot = &slots[i]; break; }

	if (!slot)
	{
		slot = &slots[slot_next];
		slot_next ^= 1;
	}

	std::vector<XSample>& xmap = slot->map;

	if (slot->target_w != target_w || slot->fbw != fbw)
	{
		xmap.resize((size_t)target_w);
		double scale = (double)target_w / (double)fbw;

		for (int x = 0; x < target_w; x++)
		{
			double sxf = ((double)x + 0.5) * (double)fbw / (double)target_w - 0.5;
			int x0 = (int)floor(sxf);
			double f = sxf - x0;

			double sharp = (f - 0.5) * scale + 0.5;
			if (sharp < 0.0) sharp = 0.0;
			else if (sharp > 1.0) sharp = 1.0;

			int x1 = x0 + 1;
			if (x0 < 0) x0 = 0; else if (x0 >= fbw) x0 = fbw - 1;
			if (x1 < 0) x1 = 0; else if (x1 >= fbw) x1 = fbw - 1;

			xmap[x].x0 = x0;
			xmap[x].x1 = x1;
			xmap[x].w = (uint8_t)(sharp * 255.0 + 0.5);
			// A softer edge, not a blurred picture. Plain bilinear at these
			// scale factors smears every pixel across several screen pixels,
			// which made the soft mode look like the game was out of focus
			// rather than like a consumer TV. Flattening the sharp-bilinear
			// ramp instead rounds the edges while the pixel grid survives.
			double soft_scale = scale * 0.45;
			if (soft_scale < 1.0) soft_scale = 1.0;
			double soft = (f - 0.5) * soft_scale + 0.5;
			if (soft < 0.0) soft = 0.0;
			else if (soft > 1.0) soft = 1.0;
			xmap[x].wsoft = (uint8_t)(soft * 255.0 + 0.5);
		}
		slot->target_w = target_w;
		slot->fbw = fbw;
	}

	const XSample* xm = xmap.data();

	int x_begin = 0;
	int x_end = target_w;
	if (offset_x + x_begin < 0) x_begin = -offset_x;
	if (offset_x + x_end > dest_w) x_end = dest_w - offset_x;

	// Scanline strength per mode. 0 means the mode leaves the beam alone.
	double beam_strength = 0.0;
	switch (filter_mode)
	{
	case 1: beam_strength = 0.80; break;  // Sony Trinitron
	case 2: beam_strength = 0.90; break;  // Arcade shadow mask
	case 4: beam_strength = 0.45; break;  // PVM Pro, gentle
	case 5: beam_strength = 1.00; break;  // Scanlines 50%, full
	case 8: beam_strength = 0.30; break;  // Scanlines Leve
	case 9: beam_strength = 0.45; break;  // CRT Suave, with soft sampling
	default: break;
	}

	// Mode 9 skips the sharpening on both axes: a rounded, consumer-TV image
	// rather than crisp pixel edges.
	const bool soft_sampling = (filter_mode == 9);

	// A convincing beam needs output rows to draw itself across. Below about
	// 3x vertical scale there simply are not enough, and forcing it produces
	// hard 1-on-1-off striping instead of scanlines - which is exactly what a
	// 480-line source like the N64 hits. Taper it off rather than lie.
	const double scale_y = (double)target_h / (double)fbh;
	double scale_fade = (scale_y - 1.6) / 1.8;
	if (scale_fade < 0.0) scale_fade = 0.0;
	else if (scale_fade > 1.0) scale_fade = 1.0;
	beam_strength *= scale_fade;

	// Scanlines cost light, so the peak gets lifted a little - but only a
	// little. At 0.45 this blew out every bright scene: the boost is applied in
	// linear light before the beam, and on already-bright content it clipped to
	// white and washed the picture out completely.
	const double gain = 1.0 + beam_strength * 0.12;

	// Measured at 22 ms per frame over a 1920x1200 buffer on a single thread,
	// against a 16.7 ms budget at 60 Hz. The filter, not the core, was what made
	// heavy games feel slow: FinalBurn Neo and MAME 2010 both emulated Tekken 3
	// at a full 60 fps while this held presentation back.
	//
	// Rows are independent - each writes only its own rows of dest_buffer and
	// reads shared tables - so the work splits cleanly across hardware threads.
	auto render_band = [&](int y_from, int y_to)
	{
		for (int y = y_from; y < y_to; y++)
		{
			int dst_y = offset_y + y;
			if (dst_y < 0 || dst_y >= dest_h) continue;

			// Where this output row falls inside its source line.
			double syf = ((double)y + 0.5) * (double)fbh / (double)target_h - 0.5;
			int src_y = (int)floor(syf);
			double phase = syf - src_y;

			// Vertical sharp-bilinear, matching the horizontal axis. Without it the
			// rows are nearest-sampled and the picture aliases, which is most of
			// what reads as "not smooth".
			double vscale = scale_y;
			if (soft_sampling)
			{
				vscale = scale_y * 0.45;
				if (vscale < 1.0) vscale = 1.0;
			}
			double vsharp = (phase - 0.5) * vscale + 0.5;
			if (vsharp < 0.0) vsharp = 0.0;
			else if (vsharp > 1.0) vsharp = 1.0;
			uint32_t vw = (uint32_t)(vsharp * 255.0 + 0.5);
			uint32_t ivw = 255u - vw;

			int src_y1 = src_y + 1;
			if (src_y < 0) src_y = 0; else if (src_y >= fbh) src_y = fbh - 1;
			if (src_y1 < 0) src_y1 = 0; else if (src_y1 >= fbh) src_y1 = fbh - 1;

			double dist = fabs(phase - 0.5) * 2.0;   // 0 at the centre of the line
			if (dist > 1.0) dist = 1.0;
			int d_idx = (int)(dist * (BEAM_DIST_STEPS - 1) + 0.5);

			const uint32_t* src_row = src_fb + (size_t)src_y * fbw;
			const uint32_t* src_row1 = src_fb + (size_t)src_y1 * fbw;
			uint32_t* dst_row = dest_buffer + (size_t)dst_y * dest_w + offset_x;

			// Mode 0 (Raw / Off / Clean): Blazing fast nearest-neighbor row mapping
			if (filter_mode == 0)
			{
				for (int x = x_begin; x < x_end; x++)
				{
					dst_row[x] = src_row[xm[x].x0];
				}
				continue;
			}

			// Modes with no beam model keep a fast integer path.
			if (beam_strength == 0.0 && filter_mode != 6 && filter_mode != 7)
			{
				for (int x = x_begin; x < x_end; x++)
				{
					const XSample& xs = xm[x];
					uint32_t w = soft_sampling ? xs.wsoft : xs.w, iw = 255u - w;

					uint32_t t0 = src_row[xs.x0],  t1 = src_row[xs.x1];
					uint32_t b0 = src_row1[xs.x0], b1 = src_row1[xs.x1];

					uint32_t rt = (((t0 >> 16) & 0xFF) * iw + ((t1 >> 16) & 0xFF) * w + 128) >> 8;
					uint32_t gt = (((t0 >> 8) & 0xFF) * iw + ((t1 >> 8) & 0xFF) * w + 128) >> 8;
					uint32_t bt = ((t0 & 0xFF) * iw + (t1 & 0xFF) * w + 128) >> 8;
					uint32_t rb = (((b0 >> 16) & 0xFF) * iw + ((b1 >> 16) & 0xFF) * w + 128) >> 8;
					uint32_t gb = (((b0 >> 8) & 0xFF) * iw + ((b1 >> 8) & 0xFF) * w + 128) >> 8;
					uint32_t bb = ((b0 & 0xFF) * iw + (b1 & 0xFF) * w + 128) >> 8;

					uint32_t r = (rt * ivw + rb * vw + 128) >> 8;
					uint32_t g = (gt * ivw + gb * vw + 128) >> 8;
					uint32_t b = (bt * ivw + bb * vw + 128) >> 8;

					dst_row[x] = (r << 16) | (g << 8) | b;
				}
				continue;
			}

			uint32_t gain_fp = (uint32_t)(gain * 256.0 + 0.5);

			for (int x = x_begin; x < x_end; x++)
			{
				const XSample& xs = xm[x];
				uint32_t w = soft_sampling ? xs.wsoft : xs.w, iw = 255u - w;

				uint32_t t0 = src_row[xs.x0],  t1 = src_row[xs.x1];
				uint32_t b0 = src_row1[xs.x0], b1 = src_row1[xs.x1];

				uint32_t rt = (((t0 >> 16) & 0xFF) * iw + ((t1 >> 16) & 0xFF) * w + 128) >> 8;
				uint32_t gt = (((t0 >> 8) & 0xFF) * iw + ((t1 >> 8) & 0xFF) * w + 128) >> 8;
				uint32_t bt = ((t0 & 0xFF) * iw + (t1 & 0xFF) * w + 128) >> 8;
				uint32_t rb = (((b0 >> 16) & 0xFF) * iw + ((b1 >> 16) & 0xFF) * w + 128) >> 8;
				uint32_t gb = (((b0 >> 8) & 0xFF) * iw + ((b1 >> 8) & 0xFF) * w + 128) >> 8;
				uint32_t bb = ((b0 & 0xFF) * iw + (b1 & 0xFF) * w + 128) >> 8;

				uint32_t r = (rt * ivw + rb * vw + 128) >> 8;
				uint32_t g = (gt * ivw + gb * vw + 128) >> 8;
				uint32_t b = (bt * ivw + bb * vw + 128) >> 8;

				if (filter_mode == 6 && xs.x0 > 0) // NTSC composite bleed
				{
					uint32_t prev = src_row[xs.x0 - 1];
					r = (r * 3 + ((prev >> 16) & 0xFF)) >> 2;
					g = (g * 3 + ((prev >> 8) & 0xFF)) >> 2;
					b = (b * 3 + (prev & 0xFF)) >> 2;
				}
				else if (filter_mode == 7) // LCD matrix grid
				{
					if ((y % 3) == 0 || (x % 3) == 0)
					{
						r = (r * 2) / 3; g = (g * 2) / 3; b = (b * 2) / 3;
					}
				}

				if (beam_strength > 0.0)
				{
					// Luminance drives the beam width, so bright lines bloom.
					uint32_t lum = (r * 77 + g * 151 + b * 28) >> 8;
					if (lum > 255) lum = 255;
					int l_idx = (int)((lum * (BEAM_LUM_STEPS - 1)) / 255);

					uint32_t atten = s_beam[d_idx][l_idx];
					atten = (uint32_t)(255.0 - (255.0 - (double)atten) * beam_strength);

					uint32_t lr = (s_srgb_to_lin[r] * gain_fp * atten) >> 16;
					uint32_t lg = (s_srgb_to_lin[g] * gain_fp * atten) >> 16;
					uint32_t lb = (s_srgb_to_lin[b] * gain_fp * atten) >> 16;

					if (lr > 4095) lr = 4095;
					if (lg > 4095) lg = 4095;
					if (lb > 4095) lb = 4095;

					r = s_lin_to_srgb[lr];
					g = s_lin_to_srgb[lg];
					b = s_lin_to_srgb[lb];
				}

				// Phosphor masks go on after the beam.
				if (filter_mode == 1) // aperture grille
				{
					int ph = x % 3;
					if (ph == 0) { g = (g * 7) >> 3; b = (b * 7) >> 3; }
					else if (ph == 1) { r = (r * 7) >> 3; b = (b * 7) >> 3; }
					else { r = (r * 7) >> 3; g = (g * 7) >> 3; }
				}
				else if (filter_mode == 2) // slot mask
				{
					int ph = (x + ((y % 4) >= 2 ? 1 : 0)) % 3;
					if (ph == 0) { g = (g * 5) / 6; b = (b * 5) / 6; }
					else if (ph == 1) { r = (r * 5) / 6; b = (b * 5) / 6; }
					else { r = (r * 5) / 6; g = (g * 5) / 6; }
				}

				dst_row[x] = (r << 16) | (g << 8) | b;
			}
		}
	};

	unsigned hw_threads = std::thread::hardware_concurrency();
	int bands = (hw_threads > 1) ? (int)((hw_threads > 8) ? 8 : hw_threads) : 1;
	if (target_h < 64) bands = 1;   // not worth the hand-off

	if (bands <= 1)
	{
		render_band(0, target_h);
	}
	else
	{
		const int step = (target_h + bands - 1) / bands;
		std::vector<std::thread> pool;
		pool.reserve((size_t)bands - 1);
		for (int b = 1; b < bands; b++)
		{
			const int a = b * step;
			const int z = (a + step < target_h) ? a + step : target_h;
			if (a < z) pool.emplace_back(render_band, a, z);
		}
		// This thread takes the first band instead of idling.
		render_band(0, (step < target_h) ? step : target_h);
		for (auto& t : pool) t.join();
	}
}

void CoreSetButtonState(int player, int button_id, bool pressed)
{
	if (player >= 0 && player < 4 && button_id >= 0 && button_id < 16)
	{
		joypad_buttons[player][button_id] = pressed ? 1 : 0;
	}
}

void CoreSetAnalogState(int player, int stick_id, int axis_id, int16_t value)
{
	if (player >= 0 && player < 4 && stick_id >= 0 && stick_id < 2 && axis_id >= 0 && axis_id < 2)
	{
		analog_sticks[player][stick_id][axis_id] = value;
	}
}

void CoreSetVolume(int volume_percent) { master_volume = std::clamp(volume_percent, 0, 100); }
int  CoreGetVolume() { return master_volume; }
void CoreSetMute(bool mute) { audio_muted = mute; }
bool CoreGetMute() { return audio_muted; }
bool CoreIsRunning() { return (is_core_loaded && is_game_loaded); }

static void ApplyDisplaySync()
{
	double target = core_native_fps;

	if (InterlockedCompareExchange(&g_sync_to_display, 0, 0) &&
		g_display_fps > 20.0 && core_native_fps > 1.0)
	{
		// Pick the nearest whole number of display refreshes per emulated
		// frame, then run the core at exactly that fraction of the display
		// rate. Every emulated frame then lands on a refresh boundary and
		// nothing repeats.
		//
		//    59.94Hz / 59.19fps -> 1 refresh -> 59.94  (0.6% off, fine)
		//   120.00Hz / 59.19fps -> 2         -> 60.00  (1.4% off, fine)
		//   144.00Hz / 59.19fps -> 2         -> 72.00  (21% off - refused)
		//
		// A 144Hz panel has no usable divisor for 60fps content, so rather
		// than run the game 21% fast we leave it at its native rate and accept
		// the judder. Switching the desktop to 120Hz or 60Hz is the real fix.
		int refreshes = (int)(g_display_fps / core_native_fps + 0.5);
		if (refreshes < 1) refreshes = 1;

		double candidate = g_display_fps / (double)refreshes;
		double deviation = fabs(candidate - core_native_fps) / core_native_fps;

		if (deviation <= 0.06) target = candidate;
	}

	core_target_fps = target;

	// Running the core off its native rate means it emits samples at a
	// different real-world rate; tell the resampler, or the ring starves.
	g_core_sample_rate = core_native_sample_rate * (target / core_native_fps);

	InterlockedExchange(&g_av_info_dirty, 1);
}

void CoreSetDisplaySync(bool enable, double display_fps)
{
	if (display_fps > 20.0 && display_fps < 400.0) g_display_fps = display_fps;
	InterlockedExchange(&g_sync_to_display, enable ? 1 : 0);
	ApplyDisplaySync();

	CoreLogPrintf(RETRO_LOG_INFO,
		"[SYNC] modo=%s monitor=%.3fHz jogo=%.3ffps rodando=%.3ffps audio=%.0fHz",
		enable ? "monitor" : "nativa", g_display_fps, core_native_fps,
		core_target_fps, g_core_sample_rate);
}

// True when the sync mode is on but no usable divisor was found, so the OSD can
// say so instead of claiming a sync that is not happening.
bool CoreDisplaySyncActive()
{
	return InterlockedCompareExchange(&g_sync_to_display, 0, 0) != 0 &&
		   fabs(core_target_fps - core_native_fps) > 0.0001;
}

double CoreGetDisplayFps() { return g_display_fps; }

void* CoreGetMemoryData(unsigned id)
{
	if (!is_core_loaded || !p_retro_get_memory_data) return NULL;
	__try { return p_retro_get_memory_data(id); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

size_t CoreGetMemorySize(unsigned id)
{
	if (!is_core_loaded || !p_retro_get_memory_size) return 0;
	__try { return p_retro_get_memory_size(id); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool CoreIsDiscGame()
{
	return is_disc;
}

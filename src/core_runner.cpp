#define WIN32_LEAN_AND_MEAN
#include <thread>
#include <vector>
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
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

#include "libretro.h"
#include "core_runner.h"
#include "menu.h"
#include "input_map.h"
#include "archive_helper.h"
#include "retroachievements.h"
#include "mister_math.h"
#include "osd.h"
#include "netplay.h"
#include "hw_render.h"

namespace fs = std::filesystem;

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "xinput.lib")

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

// 480i / 448i Field Stabilization (De-jitter) for PS2 (Play! core)
// PS2 GS alternates raster scanline offset by 1px between odd and even fields.
// By shifting odd fields up by 1 scanline without interweaving old frames,
// 100% of the pixels come from the current frame, completely eliminating font combing
// and ghosting while keeping the image vertically stable.
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
struct NameLockInit { NameLockInit() { InitializeCriticalSection(&name_lock); } };
static NameLockInit g_name_lock_init;

static std::string loaded_game_name = "";
static std::string loaded_system_dir = "";
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

// Toast Notification State. Written by the core thread, read by the UI thread
// every frame, so it is a fixed buffer under a lock rather than a std::string:
// reading a string while another thread reassigns it can fault.
static CRITICAL_SECTION toast_lock;
static char  toast_message[128] = "";
static char  toast_readback[128] = "";
static volatile LONG toast_timer = 0;

// Initialised before main() runs, so no thread can race the initialisation.
struct ToastLockInit { ToastLockInit() { InitializeCriticalSection(&toast_lock); } };
static ToastLockInit g_toast_lock_init;

// Multi-Threading Handles
static HANDLE h_core_thread = NULL;
static volatile LONG core_thread_running = 0;

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

	EnterCriticalSection(&toast_lock);
	strncpy_s(toast_message, sizeof(toast_message), message, _TRUNCATE);
	LeaveCriticalSection(&toast_lock);

	InterlockedExchange(&toast_timer, frames_duration);
}

const char* CoreGetToast()
{
	// Copies into a UI-thread buffer so the caller never holds a pointer into
	// storage the core thread may overwrite mid-draw.
	EnterCriticalSection(&toast_lock);
	strncpy_s(toast_readback, sizeof(toast_readback), toast_message, _TRUNCATE);
	LeaveCriticalSection(&toast_lock);
	return toast_readback;
}

bool CoreIsToastActive()
{
	return (InterlockedCompareExchange(&toast_timer, 0, 0) > 0 && toast_message[0] != '\0');
}

void CoreUpdateToast()
{
	if (InterlockedCompareExchange(&toast_timer, 0, 0) > 0)
	{
		if (InterlockedDecrement(&toast_timer) == 0)
		{
			EnterCriticalSection(&toast_lock);
			toast_message[0] = '\0';
			LeaveCriticalSection(&toast_lock);
		}
	}
}

// -------------------------------------------------------------
// High-Performance Audio Resampling & Output Engine
// -------------------------------------------------------------
#define OUTPUT_SAMPLE_RATE 48000
#define RING_BUFFER_SIZE   65536 // 65536 stereo frames (~1.36s capacity)
#define NUM_WAVE_BUFFERS   12    // 12 wave buffers (~128ms queue capacity)
#define SAMPLES_PER_BUFFER 512   // ~10.6ms per buffer at 48kHz
#define TARGET_OCCUPANCY   4800  // 100ms at 48kHz - ideal safety cushion

static HWAVEOUT h_wave_out = NULL;
static HANDLE   h_audio_event = NULL;
static HANDLE   h_audio_thread = NULL;
static bool     audio_thread_running = false;

static WAVEHDR  wave_headers[NUM_WAVE_BUFFERS];
static int16_t  wave_buffer_data[NUM_WAVE_BUFFERS][SAMPLES_PER_BUFFER * 2];

static int16_t  g_ring_buffer[RING_BUFFER_SIZE * 2];
static volatile LONG g_ring_write_pos = 0;
static volatile LONG g_ring_read_pos = 0;

static double   g_core_sample_rate = 48000.0;
static double   g_resample_phase = 0.0;
static int16_t  g_last_left = 0;
static int16_t  g_last_right = 0;
static bool     audio_initialized = false;
static volatile LONG g_startup_mute_samples = 0;
static volatile LONG g_audio_underrun_count = 0; // incremented by audio thread, read by PERF log

static DWORD WINAPI AudioThreadProc(LPVOID lpParam)
{
	(void)lpParam;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	while (audio_thread_running)
	{
		WaitForSingleObject(h_audio_event, 5);

		if (!audio_thread_running || !h_wave_out) break;

		for (int i = 0; i < NUM_WAVE_BUFFERS; i++)
		{
			WAVEHDR* hdr = &wave_headers[i];
			// Only refill a buffer the driver has finished with. Touching one
			// that is still WHDR_INQUEUE corrupts the playing audio.
			if (!(hdr->dwFlags & WHDR_DONE) && (hdr->dwFlags & WHDR_INQUEUE))
				continue;

			LONG wp = InterlockedCompareExchange(&g_ring_write_pos, 0, 0);
			LONG rp = InterlockedCompareExchange(&g_ring_read_pos, 0, 0);

			LONG available = (wp >= rp) ? (wp - rp) : (RING_BUFFER_SIZE - rp + wp);

			// Always keep the device fed. Letting the queue run dry stops
			// playback outright, which is what produced the periodic gaps;
			// short frames are padded by holding the last sample instead.
			int16_t* dst = wave_buffer_data[i];
			int16_t hold_l = 0, hold_r = 0;

			for (int s = 0; s < SAMPLES_PER_BUFFER; s++)
			{
				if (available > 0)
				{
					hold_l = g_ring_buffer[rp * 2 + 0];
					hold_r = g_ring_buffer[rp * 2 + 1];
					rp = (rp + 1) % RING_BUFFER_SIZE;
					available--;
				}
				else
				{
					// Underrun: decay to silence rather than holding a DC
					// level, which would thump when audio resumes.
					InterlockedIncrement(&g_audio_underrun_count);
					hold_l = (int16_t)(hold_l * 63 / 64);
					hold_r = (int16_t)(hold_r * 63 / 64);
				}
				dst[s * 2 + 0] = hold_l;
				dst[s * 2 + 1] = hold_r;
			}

			InterlockedExchange(&g_ring_read_pos, rp);

			hdr->dwFlags &= ~WHDR_DONE;
			waveOutWrite(h_wave_out, hdr, sizeof(WAVEHDR));
		}
	}
	return 0;
}

static void InitAudio(int sample_rate)
{
	g_core_sample_rate = sample_rate > 0 ? (double)sample_rate : 48000.0;
	g_resample_phase = 0.0;
	g_last_left = 0;
	g_last_right = 0;

	// Anti-pop: Zero ring buffer and activate soft startup ramp
	InterlockedExchange(&g_startup_mute_samples, (LONG)(OUTPUT_SAMPLE_RATE * 0.15));
	InterlockedExchange(&g_ring_write_pos, 0);
	InterlockedExchange(&g_ring_read_pos, 0);
	memset(g_ring_buffer, 0, sizeof(g_ring_buffer));

	if (audio_initialized)
	{
		return;
	}

	WAVEFORMATEX wfx = { 0 };
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = OUTPUT_SAMPLE_RATE;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	h_audio_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (waveOutOpen(&h_wave_out, WAVE_MAPPER, &wfx, (DWORD_PTR)h_audio_event, 0, CALLBACK_EVENT) == MMSYSERR_NOERROR)
	{
		for (int i = 0; i < NUM_WAVE_BUFFERS; i++)
		{
			memset(&wave_headers[i], 0, sizeof(WAVEHDR));
			memset(wave_buffer_data[i], 0, sizeof(wave_buffer_data[i]));
			wave_headers[i].lpData = (LPSTR)wave_buffer_data[i];
			wave_headers[i].dwBufferLength = sizeof(wave_buffer_data[i]);
			waveOutPrepareHeader(h_wave_out, &wave_headers[i], sizeof(WAVEHDR));
			waveOutWrite(h_wave_out, &wave_headers[i], sizeof(WAVEHDR));
		}

		g_ring_write_pos = 0;
		g_ring_read_pos = 0;
		audio_thread_running = true;
		h_audio_thread = CreateThread(NULL, 0, AudioThreadProc, NULL, 0, NULL);
		audio_initialized = true;
	}
}

static void SendAudioSamples(const int16_t* data, size_t frames)
{
	if (!audio_initialized || !data || frames == 0) return;

	float vol_mult = audio_muted ? 0.0f : (master_volume / 100.0f);

	// Startup anti-pop / DC offset suppression:
	// Mutes initial 50ms of hardware reset spikes and softly ramps volume over 100ms
	LONG mute_countdown = InterlockedCompareExchange(&g_startup_mute_samples, 0, 0);
	if (mute_countdown > 0)
	{
		LONG total_ramp = (LONG)(OUTPUT_SAMPLE_RATE * 0.15);
		LONG mute_period = (LONG)(OUTPUT_SAMPLE_RATE * 0.05);
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

	// Dynamic Rate Control (DRC) with low-pass smoothing and deadband:
	// Maintains ring buffer near TARGET_OCCUPANCY (100ms) without audible pitch wobble.
	// - Within ±20% of target (3840..5760 samples): inaudible micro-adjustment (< 0.15%).
	// - Beyond ±20%: smooth progressive correction (up to ±15%) filtered by exponential moving average.
	// - Low-pass smoothing eliminates sample-to-sample pitch flutter/vibrato.
	LONG wp = InterlockedCompareExchange(&g_ring_write_pos, 0, 0);
	LONG rp = InterlockedCompareExchange(&g_ring_read_pos, 0, 0);
	LONG occupancy = (wp >= rp) ? (wp - rp) : (RING_BUFFER_SIZE - rp + wp);

	static double s_smooth_drc = 1.0;
	double norm_dev = (double)(occupancy - TARGET_OCCUPANCY) / (double)TARGET_OCCUPANCY; // -1.0 to +1.0
	double raw_target_drc = 1.0;

	if (fabs(norm_dev) < 0.20)
	{
		// Inaudible deadband zone (within 20ms of target): max ±0.15% pitch correction
		raw_target_drc = 1.0 + norm_dev * 0.0075;
	}
	else if (norm_dev < 0.0)
	{
		// Buffer deficit: scale down step to produce more output samples smoothly
		raw_target_drc = 1.0 + norm_dev * 0.25;
		if (raw_target_drc < 0.70) raw_target_drc = 0.70;
	}
	else
	{
		// Buffer excess: scale up step to drain excess
		raw_target_drc = 1.0 + norm_dev * 0.20;
		if (raw_target_drc > 1.25) raw_target_drc = 1.25;
	}

	// Exponential smoothing: prevents pitch flutter and audible vibrato
	s_smooth_drc = s_smooth_drc * 0.92 + raw_target_drc * 0.08;

	double step = (g_core_sample_rate / (double)OUTPUT_SAMPLE_RATE) * s_smooth_drc;


	// Never let the writer lap the reader; dropping the tail of an oversized
	// batch is far less audible than overwriting unplayed audio.
	LONG capacity = RING_BUFFER_SIZE - occupancy - 1;

	for (size_t i = 0; i < frames; i++)
	{
		int16_t cur_l = (int16_t)(data[i * 2 + 0] * vol_mult);
		int16_t cur_r = (int16_t)(data[i * 2 + 1] * vol_mult);

		while (g_resample_phase < 1.0)
		{
			if (capacity <= 0) break;

			int16_t out_l = (int16_t)(g_last_left + (cur_l - g_last_left) * g_resample_phase);
			int16_t out_r = (int16_t)(g_last_right + (cur_r - g_last_right) * g_resample_phase);

			g_ring_buffer[wp * 2 + 0] = out_l;
			g_ring_buffer[wp * 2 + 1] = out_r;

			wp = (wp + 1) % RING_BUFFER_SIZE;
			capacity--;
			g_resample_phase += step;
		}

		// Guarded because the capacity break above can exit the loop with the
		// phase still below 1.0; unguarded that would go negative.
		if (g_resample_phase >= 1.0) g_resample_phase -= 1.0;
		g_last_left = cur_l;
		g_last_right = cur_r;
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
	static FILE* f = fopen("mister_flavor.log", "a");
	if (f)
	{
		fprintf(f, "[%s] %s\n", lvl, buf);
		fflush(f);
	}
	printf("[%s] %s\n", lvl, buf);
}

#include <map>
static std::map<std::string, std::string> g_core_options;

// Defaults the core declared through SET_VARIABLES. A user choice in the OSD
// overrides one of these; without a choice the core must still get its own
// default back, not nothing.
static std::map<std::string, std::string> g_core_defaults;

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

// -------------------------------------------------------------
// Libretro Environment & Callbacks
// -------------------------------------------------------------
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
			static std::string sys_dir = fs::absolute("bios").string();
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

			if (user_value)
			{
				var->value = user_value;
				return true;
			}

			// No user override: hand back what the core itself declared.
			EnterCriticalSection(&options_lock);
			auto d = g_core_defaults.find(var->key);
			const char* default_value = (d != g_core_defaults.end()) ? StableOptionValue(d->second) : NULL;
			LeaveCriticalSection(&options_lock);

			if (default_value)
			{
				var->value = default_value;
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

			// Anything we have no verified value for: report "unset" so the
			// core keeps its own default. Returning true with a NULL value
			// makes cores that do strcmp(var.value, ...) fault.
			else return false;
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
		return HwSetRenderCallback((struct retro_hw_render_callback*)data);

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

		EnterCriticalSection(&options_lock);
		g_core_defaults.clear();

		for (; vars->key; vars++)
		{
			if (!vars->value) continue;

			const char* semi = strchr(vars->value, ';');
			const char* first = semi ? semi + 1 : vars->value;
			while (*first == ' ') first++;

			// Log each option for diagnostics
			CoreLogPrintf(RETRO_LOG_DEBUG, "[OPT] opcao: %s = %s", vars->key, first);

			while (*first == ' ') first++;

			// The default is the first choice; the rest are separated by '|'.
			const char* bar = strchr(first, '|');
			std::string def = bar ? std::string(first, bar - first) : std::string(first);

			while (!def.empty() && (def.back() == ' ' || def.back() == '\r' || def.back() == '\n'))
				def.pop_back();

			if (!def.empty()) g_core_defaults[vars->key] = def;
		}

		size_t n = g_core_defaults.size();
		LeaveCriticalSection(&options_lock);

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
		// of being rewritten as shaders.
		if (!HwReadPixels(dst_fb, width, height)) return;

		// 480i / 448i Field Stabilization (De-jitter) for PS2 (Play! core).
		// In PS2 interlaced rendering, Field 1 is shifted down by 1 scanline relative to Field 0.
		// Displaying them unshifted causes a 1px vertical oscillation at 30Hz ("tela tremendo").
		// Naive weaving causes horizontal combing/striping on text and moving objects.
		//
		// Solution: On odd fields (field 1), shift the scanlines up by 1 row so all fields
		// share the exact same scanline alignment. 100% of the image comes from the CURRENT
		// frame, guaranteeing zero combing artifacts, solid crisp fonts, and zero vertical jitter!
		if (height == 448 && width >= 512)
		{
			if (g_deinterlace_field == 1)
			{
				memmove(dst_fb, dst_fb + width, (height - 1) * width * sizeof(uint32_t));
				memcpy(dst_fb + (height - 1) * width, dst_fb + (height - 2) * width, width * sizeof(uint32_t));
			}
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
	if (g_video_diag_frames < 5)
	{
		uint32_t acc = 0;
		for (unsigned yy = 0; yy < height; yy += 4)
			for (unsigned xx = 0; xx < width; xx += 4)
				acc |= dst_fb[yy * width + xx] & 0x00FFFFFFu;

		CoreLogPrintf(RETRO_LOG_INFO, "[VIDEO] f=%ld %ux%u pitch=%u fmt=%d nonblack=%s",
			g_video_diag_frames, width, height, (unsigned)pitch,
			(int)core_pixel_format, acc ? "SIM" : "nao");
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

	// 1. Direct JIT Hardware Polling via XInput (Zero Windows Queue Latency)
	for (DWORD pad = 0; pad < 4; pad++)
	{
		XINPUT_STATE state;
		if (XInputGetState(pad, &state) != ERROR_SUCCESS) continue;

		WORD w = state.Gamepad.wButtons;
		if (state.Gamepad.bLeftTrigger > 50) w |= PAD_BTN_LT;
		if (state.Gamepad.bRightTrigger > 50) w |= PAD_BTN_RT;

		int16_t* jb = joypad_buttons[pad];

		// Configurable bindings for the 12 core RetroPad buttons
		for (int bind = 0; bind < BIND_COUNT; bind++)
		{
			int code = InputBindGetPad(bind);
			if (code > 0 && (w & code))
			{
				jb[InputBindRetroId(bind)] = 1;
			}
		}

		// Additional extended controls (triggers and thumb buttons)
		jb[RETRO_DEVICE_ID_JOYPAD_L2] = (w & PAD_BTN_LT) ? 1 : 0;
		jb[RETRO_DEVICE_ID_JOYPAD_R2] = (w & PAD_BTN_RT) ? 1 : 0;
		jb[RETRO_DEVICE_ID_JOYPAD_L3] = (w & PAD_BTN_L3) ? 1 : 0;
		jb[RETRO_DEVICE_ID_JOYPAD_R3] = (w & PAD_BTN_R3) ? 1 : 0;

		if (abs(state.Gamepad.sThumbLX) > deadzone_thresh)
		{
			analog_sticks[pad][0][0] = state.Gamepad.sThumbLX;
			if (state.Gamepad.sThumbLX > deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_RIGHT] = 1;
			else if (state.Gamepad.sThumbLX < -deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_LEFT] = 1;
		}
		if (abs(state.Gamepad.sThumbLY) > deadzone_thresh)
		{
			// Negating -32768 overflows int16 straight back to -32768, so a
			// stick held fully down would read as fully up for that sample.
			analog_sticks[pad][0][1] = (state.Gamepad.sThumbLY == -32768)
				? 32767 : (int16_t)(-state.Gamepad.sThumbLY);
			if (state.Gamepad.sThumbLY > deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_UP] = 1;
			else if (state.Gamepad.sThumbLY < -deadzone_thresh) jb[RETRO_DEVICE_ID_JOYPAD_DOWN] = 1;
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
		if (GetAsyncKeyState(vk) & 0x8000)
			joypad_buttons[0][InputBindRetroId(bind)] = 1;
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
static bool RunOneFrameGuarded()
{
	__try
	{
		p_retro_run();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static DWORD WINAPI CoreExecutionThreadProc(LPVOID lpParam)
{
	(void)lpParam;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	// --- Load phase (this is the part that used to freeze the window) ---
	std::string rom_path = g_pending_rom;
	std::string core_dll = g_pending_core_hint;

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

	const bool is_arcade_rom =
		(rom_path.find("Arcade") != std::string::npos ||
		 core_dll.find("arcade_fbneo") != std::string::npos ||
		 core_dll.find("mame2003") != std::string::npos ||
		 core_dll.find("mame2010") != std::string::npos);

	bool loaded = false;

	if (is_arcade_rom)
	{
		// Build candidate core priority list starting with the preferred core
		std::vector<std::string> candidate_cores;
		if (!core_dll.empty() && fs::exists(core_dll))
		{
			candidate_cores.push_back(core_dll);
		}

		const std::string default_arcade_cores[] = {
			"cores/arcade_fbneo.dll",
			"cores/mame2003.dll",
			"cores/mame2010.dll",
			"cores/dreamcast.dll"
		};

		for (const auto& c : default_arcade_cores)
		{
			if (std::find(candidate_cores.begin(), candidate_cores.end(), c) == candidate_cores.end() && fs::exists(c))
			{
				candidate_cores.push_back(c);
			}
		}

		for (size_t i = 0; i < candidate_cores.size(); i++)
		{
			const std::string& cand_dll = candidate_cores[i];
			bool is_last_candidate = (i == candidate_cores.size() - 1);
			CoreLogPrintf(RETRO_LOG_INFO, "[CoreRunner] Tentando core de arcade: %s para '%s'", cand_dll.c_str(), rom_path.c_str());

			if (CoreLoad(cand_dll.c_str()))
			{
				// Suppress toast unless all candidates have failed
				if (CoreLoadGame(rom_path.c_str(), !is_last_candidate))
				{
					core_dll = cand_dll;
					loaded = true;
					CoreLogPrintf(RETRO_LOG_INFO, "[CoreRunner] Sucesso no carregamento de arcade com: %s", cand_dll.c_str());
					break;
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

		if (CoreLoad(core_dll.c_str()) && CoreLoadGame(rom_path.c_str(), false))
		{
			loaded = true;
		}
	}

	if (!loaded)
	{
		CoreUnload();
		InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
		return 0;
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

	while (InterlockedCompareExchange(&core_thread_running, 0, 0) && is_game_loaded)
	{
		if (!RunOneFrameGuarded())
		{
			CoreSetToast("WARNING: RECOVERED FROM INTERNAL EXCEPTION", 180);
			Sleep(16);
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

static void RecoverAfterKilledCore()
{
	// A critical section owned by a dead thread is never released. Recreating it
	// is the only way back; nothing else can be running against it here, because
	// the only other user was the thread we just killed.
	DeleteCriticalSection(&options_lock);
	InitializeCriticalSection(&options_lock);
	DeleteCriticalSection(&name_lock);
	InitializeCriticalSection(&name_lock);

	HwContextDestroy();

	is_game_loaded = false;
	is_core_loaded = false;
	ResetCoreFunctionPointers();

	// The DLL is deliberately left mapped. FreeLibrary on a module whose thread
	// was killed inside it can fault during its own cleanup, and a leaked
	// mapping costs memory, not correctness - a different core still loads.
	h_core_dll = NULL;

	if (h_wave_out)
	{
		waveOutReset(h_wave_out);
		waveOutClose(h_wave_out);
		h_wave_out = NULL;
		audio_initialized = false;
	}
	if (h_audio_event) { CloseHandle(h_audio_event); h_audio_event = NULL; }
	if (h_audio_thread) { CloseHandle(h_audio_thread); h_audio_thread = NULL; }
	audio_thread_running = false;

	g_resample_phase = 0.0;
	g_last_left = 0;
	g_last_right = 0;
	InterlockedExchange(&g_ring_write_pos, 0);
	InterlockedExchange(&g_ring_read_pos, 0);

	memset(g_triple_fb, 0, sizeof(g_triple_fb));
	InterlockedExchange(&g_fb_dirty, 0);

	CoreLogPrintf(RETRO_LOG_ERROR,
		"[CoreRunner] o core nao encerrou sozinho; estado recuperado a forca");
}

void CoreShutdown()
{
	InterlockedExchange(&core_thread_running, 0);

	if (h_core_thread)
	{
		// Was 8000. A core that has not finished in three seconds is wedged, and
		// the whole wait blocks the UI thread - that pause is what read as the
		// app freezing when a game was closed.
		if (WaitForSingleObject(h_core_thread, 3000) == WAIT_TIMEOUT)
		{
			TerminateThread(h_core_thread, 0);
			CloseHandle(h_core_thread);
			h_core_thread = NULL;
			RecoverAfterKilledCore();
			InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
			return;
		}
		CloseHandle(h_core_thread);
		h_core_thread = NULL;
	}

	InterlockedExchange(&g_core_state, CORE_STATE_IDLE);
}

bool CoreRequestLoad(const char* rom_path, const char* core_dll_hint)
{
	if (!rom_path) return false;

	CoreShutdown();

	g_pending_rom = rom_path;
	g_pending_core_hint = core_dll_hint ? core_dll_hint : "";

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

static void SetPortDevicesGuarded()
{
	if (!p_retro_set_controller_port_device) return;
	__try
	{
		p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
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

static void RetroUnloadGameGuarded()
{
	__try { if (p_retro_unload_game) p_retro_unload_game(); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void RetroDeinitGuarded()
{
	__try { if (p_retro_deinit) p_retro_deinit(); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool CoreLoad(const char* core_dll_path)
{
	CoreUnload();

	h_core_dll = LoadLibraryA(core_dll_path);
	if (!h_core_dll)
	{
		printf("[CoreRunner] Failed to load DLL: %s\n", core_dll_path);
		return false;
	}

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

	if (!p_retro_init || !p_retro_run || !p_retro_load_game)
	{
		FreeLibrary(h_core_dll);
		h_core_dll = NULL;
		return false;
	}

	if (!RetroSetEnvironmentGuarded())
	{
		CoreLogPrintf(RETRO_LOG_ERROR, "[CoreRunner] retro_set_environment falhou: %s", core_dll_path);
		FreeLibrary(h_core_dll);
		h_core_dll = NULL;
		return false;
	}
	if (!RetroInitGuarded())
	{
		FreeLibrary(h_core_dll);
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
	// from this list got an empty name, which is why the in-game menu showed
	// "MiSTer Flavor" for them and no per-core options could ever appear.
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

	fs::path orig_p(g_pending_rom.empty() ? rom_path : g_pending_rom);
	loaded_game_stem = orig_p.stem().string();
	EnterCriticalSection(&name_lock);
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

	bool ok = RetroLoadGameGuarded(&game_info);
	if (rom_data) free(rom_data);

	if (!ok)
	{
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
				CoreSetToast("ATIVE 3D ACCELERATION EM SETTINGS > VIDEO", 300);
			}
			else if (needed)
			{
				char msg[192];
				snprintf(msg, sizeof(msg), "BIOS AUSENTE EM bios/: %s", needed);
				CoreSetToast(msg, 360);
			}
			else
			{
				CoreSetToast("FALHA AO CARREGAR - VEJA mister_flavor.log", 240);
			}
		}
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
	p_retro_get_system_av_info(&av_info);

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

	// Reset 480i de-jitter field state
	g_deinterlace_field = 0;


	if (HwIsActive())
	{
		unsigned hw_w = av_info.geometry.max_width  ? av_info.geometry.max_width  : core_fb_width;
		unsigned hw_h = av_info.geometry.max_height ? av_info.geometry.max_height : core_fb_height;
		if (hw_w > MAX_FB_WIDTH)  hw_w = MAX_FB_WIDTH;
		if (hw_h > MAX_FB_HEIGHT) hw_h = MAX_FB_HEIGHT;

		CoreLogPrintf(RETRO_LOG_INFO,
			"[HW] geometria do core: base=%ux%u max=%ux%u -> superficie %ux%u",
			av_info.geometry.base_width, av_info.geometry.base_height,
			av_info.geometry.max_width, av_info.geometry.max_height, hw_w, hw_h);

		if (HwEnsureSurface(hw_w, hw_h))
		{
			// The core allocates its GL resources here, so it has to happen
			// after load and before the first retro_run.
			HwContextReset();
		}
	}

	is_game_loaded = true;
	return true;
}

static void CoreUnload()
{
	if (is_game_loaded)
	{
		RaOnGameUnload();
		CoreReleaseMemoryMap();

		EnterCriticalSection(&options_lock);
		g_core_defaults.clear();
		LeaveCriticalSection(&options_lock);

		RetroUnloadGameGuarded();
		is_game_loaded = false;
	}

	if (is_core_loaded)
	{
		RetroDeinitGuarded();
		is_core_loaded = false;
	}

	HwContextDestroy();
	HwReleaseCurrent();

	if (h_core_dll)
	{
		FreeLibrary(h_core_dll);
		h_core_dll = NULL;
	}

	ResetCoreFunctionPointers();

	if (audio_thread_running)
	{
		audio_thread_running = false;
		if (h_audio_event) SetEvent(h_audio_event);
		if (h_audio_thread)
		{
			WaitForSingleObject(h_audio_thread, 1000);
			CloseHandle(h_audio_thread);
			h_audio_thread = NULL;
		}
	}

	if (h_wave_out)
	{
		waveOutReset(h_wave_out);
		for (int i = 0; i < NUM_WAVE_BUFFERS; i++)
		{
			if (wave_headers[i].dwFlags & WHDR_PREPARED)
			{
				waveOutUnprepareHeader(h_wave_out, &wave_headers[i], sizeof(WAVEHDR));
			}
		}
		waveOutClose(h_wave_out);
		h_wave_out = NULL;
		audio_initialized = false;
	}

	if (h_audio_event)
	{
		CloseHandle(h_audio_event);
		h_audio_event = NULL;
	}

	g_resample_phase = 0.0;
	g_last_left = 0;
	g_last_right = 0;
	InterlockedExchange(&g_ring_write_pos, 0);
	InterlockedExchange(&g_ring_read_pos, 0);
}

static void DoReset()
{
	if (CoreIsRunning() && p_retro_reset)
	{
		p_retro_reset();
		CoreSetToast("CORE RESTARTED", 90);
	}
}

static bool DoSaveState(int slot)
{
	if (!CoreIsRunning() || !p_retro_serialize || !p_retro_serialize_size)
	{
		CoreSetToast("SAVESTATE UNAVAILABLE ON THIS CORE", 120);
		return false;
	}

	size_t sz = p_retro_serialize_size();
	if (sz == 0)
	{
		CoreSetToast("ERROR: INVALID STATE SIZE", 120);
		return false;
	}

	void* buf = malloc(sz);
	if (!buf) return false;

	if (!p_retro_serialize(buf, sz))
	{
		free(buf);
		CoreSetToast("ERROR SAVING STATE", 120);
		return false;
	}

	fs::path save_dir = fs::absolute("saves/" + loaded_system_dir);
	fs::create_directories(save_dir);

	char slot_filename[512];
	snprintf(slot_filename, sizeof(slot_filename), "%s.state%d", loaded_game_stem.c_str(), slot);
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
	snprintf(slot_filename, sizeof(slot_filename), "%s.state%d", loaded_game_stem.c_str(), slot);
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

	bool ok = p_retro_unserialize(buf, sz);
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
static volatile LONG g_pending_cmd = CORE_CMD_NONE;
static volatile LONG g_pending_cmd_slot = 0;

static void ProcessPendingCommand()
{
	LONG cmd = InterlockedExchange(&g_pending_cmd, CORE_CMD_NONE);
	if (cmd == CORE_CMD_NONE) return;

	int slot = (int)InterlockedCompareExchange(&g_pending_cmd_slot, 0, 0);

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
	InterlockedExchange(&g_pending_cmd_slot, slot);
	InterlockedExchange(&g_pending_cmd, cmd);
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
	snprintf(filename, sizeof(filename), "screenshots/%s_%s.bmp", loaded_game_stem.c_str(), date_str);

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
		// Same treatment as the linear path below: this one warps every pixel in
		// floating point and was the most expensive of the lot at 19 ms a frame.
		auto warp_band = [&](int y_from, int y_to)
		{
			for (int y = y_from; y < y_to; y++)
			{
				int dst_y = offset_y + y;
				if (dst_y < 0 || dst_y >= dest_h) continue;

				float ny = ((float)y / (float)target_h) * 2.0f - 1.0f;

				for (int x = 0; x < target_w; x++)
				{
					int dst_x = offset_x + x;
					if (dst_x < 0 || dst_x >= dest_w) continue;

					float nx = ((float)x / (float)target_w) * 2.0f - 1.0f;
					float dist = nx * nx + ny * ny;
					float cnx = nx * (1.0f + dist * 0.08f);
					float cny = ny * (1.0f + dist * 0.08f);

					if (fabsf(cnx) > 1.02f || fabsf(cny) > 1.02f)
					{
						dest_buffer[dst_y * dest_w + dst_x] = 0x00040404; // CRT bezel edge
						continue;
					}

					float vignette = (1.0f - cnx * cnx * 0.15f) * (1.0f - cny * cny * 0.15f);
					if (vignette < 0.2f) vignette = 0.2f;

					int src_x = (int)(((cnx + 1.0f) * 0.5f) * (float)fbw);
					int src_y = (int)(((cny + 1.0f) * 0.5f) * (float)fbh);
					if (src_x < 0) src_x = 0; else if (src_x >= fbw) src_x = fbw - 1;
					if (src_y < 0) src_y = 0; else if (src_y >= fbh) src_y = fbh - 1;

					uint32_t col = src_fb[(size_t)src_y * fbw + src_x];
					uint32_t r = (col >> 16) & 0xFF;
					uint32_t g = (col >> 8) & 0xFF;
					uint32_t b = col & 0xFF;

					if (y & 1) { r = (r * 3) / 4; g = (g * 3) / 4; b = (b * 3) / 4; }
					r = (uint32_t)(r * vignette);
					g = (uint32_t)(g * vignette);
					b = (uint32_t)(b * vignette);

					dest_buffer[dst_y * dest_w + dst_x] = (r << 16) | (g << 8) | b;
				}
			}
		};

		unsigned warp_hw = std::thread::hardware_concurrency();
		int warp_bands = (warp_hw > 1) ? (int)((warp_hw > 8) ? 8 : warp_hw) : 1;
		if (target_h < 64) warp_bands = 1;

		if (warp_bands <= 1)
		{
			warp_band(0, target_h);
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
				if (a < z) wpool.emplace_back(warp_band, a, z);
			}
			warp_band(0, (wstep < target_h) ? wstep : target_h);
			for (auto& t : wpool) t.join();
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
	return p_retro_get_memory_data(id);
}

size_t CoreGetMemorySize(unsigned id)
{
	if (!is_core_loaded || !p_retro_get_memory_size) return 0;
	return p_retro_get_memory_size(id);
}

bool CoreIsDiscGame()
{
	return is_disc;
}

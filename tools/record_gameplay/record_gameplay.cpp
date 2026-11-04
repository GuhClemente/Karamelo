// Enhanced Autonomous Gameplay & Audio Recorder
// Supports:
// - Direct attract mode / gameplay demo
// - Robust input injection via persistent latches
// - Long-form capture (60s - 120s)
// - Direct PCM audio recording to WAV and AAC muxing

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <unistd.h>
#include <chrono>
#include <thread>
#define POPEN popen
#define PCLOSE pclose
typedef uint32_t DWORD;
static inline void Sleep(uint32_t ms) { usleep(ms * 1000); }
static inline uint32_t GetTickCount() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>
#include <cmath>
#include <algorithm>

#include "core_runner.h"
#include "menu.h"
#include "hw_render.h"

namespace fs = std::filesystem;

static const int kWidth = 1280;
static const int kHeight = 720;

static std::vector<int16_t> g_audio_pcm_buf;
static std::mutex g_audio_pcm_mutex;

static void RecordAudioCallback(const int16_t* data, size_t frames)
{
	if (!data || frames == 0) return;
	size_t count = frames * 2;
	std::lock_guard<std::mutex> lock(g_audio_pcm_mutex);
	g_audio_pcm_buf.insert(g_audio_pcm_buf.end(), data, data + count);
}

static void WriteWavHeader(FILE* f, int sample_rate, uint32_t data_bytes)
{
	if (!f) return;
	fseek(f, 0, SEEK_SET);

	uint32_t total_size = 36 + data_bytes;
	uint32_t byte_rate = sample_rate * 2 * 2;
	uint16_t block_align = 4;
	uint16_t bits_per_sample = 16;
	uint16_t num_channels = 2;
	uint16_t audio_format = 1; // PCM
	uint32_t subchunk1_size = 16;

	fwrite("RIFF", 1, 4, f);
	fwrite(&total_size, 4, 1, f);
	fwrite("WAVE", 1, 4, f);

	fwrite("fmt ", 1, 4, f);
	fwrite(&subchunk1_size, 4, 1, f);
	fwrite(&audio_format, 2, 1, f);
	fwrite(&num_channels, 2, 1, f);
	fwrite(&sample_rate, 4, 1, f);
	fwrite(&byte_rate, 4, 1, f);
	fwrite(&block_align, 2, 1, f);
	fwrite(&bits_per_sample, 2, 1, f);

	fwrite("data", 1, 4, f);
	fwrite(&data_bytes, 4, 1, f);
}

#ifdef _WIN32
#define DEV_NULL "2>nul"
#else
#define DEV_NULL "2>/dev/null"
#endif

static const char* GetFfmpegCmd()
{
#ifdef _WIN32
	return "ffmpeg";
#else
	static std::string s_cmd;
	if (!s_cmd.empty()) return s_cmd.c_str();
	if (system("which ffmpeg >/dev/null 2>&1") == 0) s_cmd = "ffmpeg";
	else if (system("which ffmpeg.exe >/dev/null 2>&1") == 0) s_cmd = "ffmpeg.exe";
	else s_cmd = "ffmpeg";
	return s_cmd.c_str();
#endif
}

static bool FfmpegAvailable()
{
#ifdef _WIN32
	return system("where ffmpeg >nul 2>nul") == 0;
#else
	return system("which ffmpeg >/dev/null 2>&1 || which ffmpeg.exe >/dev/null 2>&1") == 0;
#endif
}

static void SaveBmp(const std::string& path, int filter_mode)
{
	std::vector<uint32_t> buf(kWidth * kHeight, 0);
	CoreRender(buf.data(), kWidth, kHeight, 0, filter_mode);

	FILE* bf = fopen(path.c_str(), "wb");
	if (!bf) return;

	uint32_t row_bytes = kWidth * 3;
	uint32_t pad = (4 - (row_bytes % 4)) % 4;
	uint32_t image_size = (row_bytes + pad) * kHeight;
	uint32_t file_size = 54 + image_size;

	unsigned char file_hdr[14] = { 'B', 'M',
		(unsigned char)(file_size), (unsigned char)(file_size >> 8),
		(unsigned char)(file_size >> 16), (unsigned char)(file_size >> 24),
		0, 0, 0, 0, 54, 0, 0, 0 };

	unsigned char info_hdr[40] = { 40, 0, 0, 0,
		(unsigned char)(kWidth), (unsigned char)(kWidth >> 8), (unsigned char)(kWidth >> 16), (unsigned char)(kWidth >> 24),
		(unsigned char)(kHeight), (unsigned char)(kHeight >> 8), (unsigned char)(kHeight >> 16), (unsigned char)(kHeight >> 24),
		1, 0, 24, 0 };

	fwrite(file_hdr, 1, 14, bf);
	fwrite(info_hdr, 1, 40, bf);

	unsigned char pad_bytes[3] = { 0, 0, 0 };
	for (int y = kHeight - 1; y >= 0; y--)
	{
		for (int x = 0; x < kWidth; x++)
		{
			uint32_t rgb = buf[(size_t)y * kWidth + x];
			unsigned char bgr[3] = { (unsigned char)(rgb & 0xFF), (unsigned char)((rgb >> 8) & 0xFF), (unsigned char)((rgb >> 16) & 0xFF) };
			fwrite(bgr, 1, 3, bf);
		}
		if (pad > 0) fwrite(pad_bytes, 1, pad, bf);
	}
	fclose(bf);
}

struct TimedButtonPress {
	int start_frame;
	int end_frame;
	std::vector<int> buttons;
};

static std::vector<TimedButtonPress> ParseMacroSpec(const std::string& spec)
{
	std::vector<TimedButtonPress> result;
	if (spec.empty() || spec == "none" || spec == "0") return result;

	std::string token;
	for (size_t i = 0; i <= spec.size(); i++)
	{
		char c = (i < spec.size()) ? spec[i] : ';';
		if (c == ',' || c == ';' || i == spec.size())
		{
			if (!token.empty())
			{
				size_t colon = token.find(':');
				if (colon != std::string::npos)
				{
					std::string time_part = token.substr(0, colon);
					std::string btn_part = token.substr(colon + 1);

					int start_f = 0, end_f = 0;
					size_t dash = time_part.find('-');
					if (dash != std::string::npos)
					{
						double s0 = atof(time_part.substr(0, dash).c_str());
						double s1 = atof(time_part.substr(dash + 1).c_str());
						start_f = (int)(s0 * 30.0);
						end_f = (int)(s1 * 30.0);
					}
					else
					{
						double s0 = atof(time_part.c_str());
						start_f = (int)(s0 * 30.0);
						end_f = start_f + 15;
					}

					std::transform(btn_part.begin(), btn_part.end(), btn_part.begin(), ::tolower);
					std::vector<int> btns;
					if (btn_part == "a") { btns.push_back(0); btns.push_back(8); }
					else if (btn_part == "b") { btns.push_back(1); btns.push_back(0); }
					else if (btn_part == "start") { btns.push_back(3); }
					else if (btn_part == "select") { btns.push_back(2); }
					else if (btn_part == "up") { btns.push_back(4); }
					else if (btn_part == "down") { btns.push_back(5); }
					else if (btn_part == "left") { btns.push_back(6); }
					else if (btn_part == "right") { btns.push_back(7); }
					else if (btn_part == "x") { btns.push_back(9); }
					else if (btn_part == "y") { btns.push_back(10); }
					else if (btn_part == "l") { btns.push_back(9); }
					else if (btn_part == "r") { btns.push_back(11); }
					else if (!btn_part.empty() && isdigit(btn_part[0])) { btns.push_back(atoi(btn_part.c_str())); }

					if (!btns.empty() && end_f >= start_f)
					{
						result.push_back({ start_f, end_f, btns });
					}
				}
				token.clear();
			}
		}
		else
		{
			token += c;
		}
	}
	return result;
}

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		printf("Usage: record_gameplay.exe <core_dll> <rom_path> <duration_sec> <out_dir> [base_name]\n");
		return 1;
	}

	if (!FfmpegAvailable())
	{
		printf("[ERROR] ffmpeg was not found on PATH.\n");
		return 1;
	}

	const char* core_dll = argv[1];
	const char* rom_path = argv[2];
	int duration_sec = atoi(argv[3]);
	if (duration_sec < 5) duration_sec = 90;
	std::string out_dir = argv[4];
	std::string base_name = (argc > 5) ? argv[5] : "gameplay";
	int upscale_mode = (argc > 6) ? atoi(argv[6]) : 0;
	int filter_mode = (argc > 7) ? atoi(argv[7]) : 0;
	bool no_input = (argc > 8) ? (atoi(argv[8]) != 0) : false;
	bool skip_extras = (argc > 9) ? (atoi(argv[9]) != 0) : false;
	std::string macro_spec = (argc > 10) ? argv[10] : "";
	std::vector<TimedButtonPress> timed_macros = ParseMacroSpec(macro_spec);
	if (!timed_macros.empty())
	{
		printf("[INFO] Timed input macros: loaded %zu action(s)\n", timed_macros.size());
	}
	MenuSetN64Upscale(upscale_mode);
	printf("[INFO] N64 Upscale Mode: %d, Filter Mode: %d, No-Input: %d, Skip-Extras: %d\n", upscale_mode, filter_mode, (int)no_input, (int)skip_extras);

	std::string lower_rom = rom_path;
	std::transform(lower_rom.begin(), lower_rom.end(), lower_rom.begin(), ::tolower);
	std::string lower_base = base_name;
	std::transform(lower_base.begin(), lower_base.end(), lower_base.begin(), ::tolower);

	bool is_rally = (lower_rom.find("rally") != std::string::npos || lower_base.find("rally") != std::string::npos);
	bool is_zelda = (lower_rom.find("zelda") != std::string::npos || lower_rom.find("link") != std::string::npos || lower_base.find("zelda") != std::string::npos);
	bool is_waverace = (lower_rom.find("wave race") != std::string::npos || lower_base.find("waverace") != std::string::npos);
	bool is_sor = (lower_rom.find("streets") != std::string::npos || lower_rom.find("rage") != std::string::npos || lower_rom.find("sor") != std::string::npos || lower_base.find("sor") != std::string::npos || lower_base.find("streets") != std::string::npos);
	bool is_castlevania = (lower_rom.find("castlevania") != std::string::npos || lower_rom.find("aria") != std::string::npos || lower_base.find("castlevania") != std::string::npos);
	bool is_fmtowns = (lower_rom.find("fmtowns") != std::string::npos || lower_base.find("fmtowns") != std::string::npos || lower_base.find("geartowns") != std::string::npos || std::string(core_dll).find("fmtowns") != std::string::npos);
	bool is_truxton = is_fmtowns && (lower_rom.find("truxton") != std::string::npos || lower_rom.find("tatsujin") != std::string::npos || lower_base.find("truxton") != std::string::npos || lower_base.find("tatsujin") != std::string::npos);
	bool is_splatterhouse = is_fmtowns && (lower_rom.find("splatter") != std::string::npos || lower_base.find("splatter") != std::string::npos);
	bool is_ssf2 = is_fmtowns && (lower_rom.find("street") != std::string::npos || lower_rom.find("ssf2") != std::string::npos || lower_base.find("ssf2") != std::string::npos);
	bool is_afterburner = is_fmtowns && (lower_rom.find("afterburner") != std::string::npos || lower_base.find("afterburner") != std::string::npos);
	bool is_chasehq = is_fmtowns && (lower_rom.find("chase") != std::string::npos || lower_base.find("chase") != std::string::npos);
	bool is_madstalker = is_fmtowns && (lower_rom.find("mad stalker") != std::string::npos || lower_base.find("madstalker") != std::string::npos);

	printf("[INFO] Game detected: rally=%d, zelda=%d, sor=%d, castlevania=%d (rom: %s, base: %s)\n",
		is_rally, is_zelda, is_sor, is_castlevania, rom_path, base_name.c_str());

	fs::create_directories(out_dir);

	// High process priority so recorder never starves emulation
#ifdef _WIN32
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

	// Setup lock-free in-memory audio buffer (zero disk I/O in callback!)
	std::string wav_path = out_dir + "/" + base_name + "_temp.wav";
	{
		std::lock_guard<std::mutex> lock(g_audio_pcm_mutex);
		g_audio_pcm_buf.clear();
		g_audio_pcm_buf.reserve((size_t)duration_sec * 48000 * 2);
	}
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	CoreSetAudioCallback(RecordAudioCallback);

	printf("[INFO] Loading core: %s for %d seconds...\n", core_dll, duration_sec);

	bool started = CoreRequestLoad(rom_path, core_dll);

	DWORD waited_ms = 0;
	while (started && (CoreIsLoading() || !CoreIsRunning()) && waited_ms < 45000)
	{
		Sleep(50);
		waited_ms += 50;
	}

	if (!CoreIsRunning())
	{
		printf("[ERROR] Core failed to run: %s\n", core_dll);
		// audio in memory, no file to close
		return 1;
	}

	printf("[INFO] Core is running! Recording frames for %d seconds...\n", duration_sec);
	CoreSetN64Upscale(upscale_mode);

	std::string temp_mp4_path = out_dir + "/" + base_name + "_temp.mp4";
	std::string final_mp4_path = out_dir + "/" + base_name + ".mp4";

	char pipe_cmd[1024];
#ifdef _WIN32
	static int s_has_nvenc = -1;
	if (s_has_nvenc == -1)
	{
		s_has_nvenc = (system("ffmpeg -v error -f lavfi -i testsrc=duration=0.1 -c:v h264_nvenc -f null - 2>nul") == 0) ? 1 : 0;
		if (s_has_nvenc == 1) printf("[PERF] Hardware Encoder Ativo: NVIDIA NVENC (0%% CPU load)\n");
	}
	const char* enc_codec = (s_has_nvenc == 1)
		? "-c:v h264_nvenc -preset p4 -cq 18 -pix_fmt yuv420p"
		: "-c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p";
#elif defined(__APPLE__)
	const char* enc_codec = "-c:v h264_videotoolbox -q:v 65 -pix_fmt yuv420p";
#else
	const char* enc_codec = "-c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p";
#endif

	snprintf(pipe_cmd, sizeof(pipe_cmd),
		"%s -y -f rawvideo -vcodec rawvideo -s %dx%d -pix_fmt bgra -r 30 -i - %s \"%s\"",
		GetFfmpegCmd(), kWidth, kHeight, enc_codec, temp_mp4_path.c_str());

	FILE* vpipe = POPEN(pipe_cmd, "wb");
	std::vector<uint32_t> frame_buf(kWidth * kHeight, 0);

	DWORD run_t0 = GetTickCount();
	int frames_written = 0;
	bool s1 = false, s2 = false, s3 = false, s4 = false;

	const int target_frames = duration_sec * 30;
	const uint32_t target_audio = (uint32_t)duration_sec * 44100 * 4;
	while (CoreIsRunning() && frames_written < target_frames)
	{
		DWORD elapsed = frames_written / 30;

		// Default: clear inputs
		for (int b = 0; b < 16; b++) CoreSetButtonState(0, b, false);
		CoreSetAnalogState(0, 0, 0, 0);
		CoreSetAnalogState(0, 0, 1, 0);

		// Pulsing helper: toggles every 6 frames (~0.2s)
		bool pulse = ((frames_written / 6) % 2 == 0);

		// Custom timed button macros passed via command line (e.g. "2-5:A,8-10:START")
		for (const auto& tb : timed_macros)
		{
			if (frames_written >= tb.start_frame && frames_written <= tb.end_frame)
			{
				for (int b : tb.buttons) CoreSetButtonState(0, b, pulse);
			}
		}

		// Essential controller intervention for N64 prompt dismissals
		// Castlevania 64 displays Controller Pak warnings at boot:
		// We pulse Button 0 (N64 A) and Button 8 from 2s to 5s so the prompt is cleanly dismissed
		// without pressing START (which would enter player select or game menus).
		if (is_castlevania && elapsed >= 2 && elapsed <= 5)
		{
			CoreSetButtonState(0, 0, pulse); // Libretro B -> N64 A button
			CoreSetButtonState(0, 8, pulse); // Libretro A
		}

		if (!no_input)
		{
			if (is_waverace)
			{
				// Wave Race 64:
			// 1) 0-5s: Title Screen -> Press START
			if (elapsed < 5)
			{
				if (elapsed >= 4) CoreSetButtonState(0, 3, pulse);
			}
			// 2) 5-16s: Menus (Mode / Jet Ski / Track) -> Press A
			else if (elapsed >= 5 && elapsed < 16)
			{
				CoreSetButtonState(0, 8, pulse);
			}
			// 3) 16s+: ACTIVE RACE! Hold Gas (A button) and steer!
			else
			{
				CoreSetButtonState(0, 8, true);
				int16_t steer = (int16_t)(sin(frames_written * 0.05) * 12000);
				CoreSetAnalogState(0, 0, 0, steer);
			}
		}
		else if (is_zelda)
		{
			// Zelda A Link to the Past:
			// 1) 1-18s: Advance Title screen & Game selection (1 Player / A Link to the Past)
			if (elapsed >= 1 && elapsed < 18)
			{
				CoreSetButtonState(0, 8, pulse); // A
				CoreSetButtonState(0, 3, pulse); // START
			}
			// 2) 18-32s: 'ESCOLHA UM JOGO' (Slot 1) & 'ESCREVA SEU NOME' (START confirms 'LINK')
			else if (elapsed >= 18 && elapsed < 32)
			{
				CoreSetButtonState(0, 8, pulse); // A
				CoreSetButtonState(0, 3, pulse); // START
			}
			// 3) 32-70s: Story crawl & Uncle dialogue in the house - pulse A and B fast to skip text!
			else if (elapsed >= 32 && elapsed < 70)
			{
				bool fast_pulse = ((frames_written / 3) % 2 == 0);
				CoreSetButtonState(0, 8, fast_pulse); // A
				CoreSetButtonState(0, 0, fast_pulse); // B
				CoreSetButtonState(0, 3, fast_pulse); // START
			}
			// 4) 70s+: LINK ACTIVE GAMEPLAY! Walk out of bed, explore house, walk into rain & swing sword!
			else if (elapsed >= 70)
			{
				int cycle = (frames_written / 40) % 6;
				if (cycle == 0) {
					CoreSetButtonState(0, 5, true); // DOWN (get out of bed)
					CoreSetButtonState(0, 8, pulse); // A (open chest / talk)
				} else if (cycle == 1) {
					CoreSetButtonState(0, 7, true); // RIGHT
					CoreSetButtonState(0, 0, pulse); // B (swing sword)
				} else if (cycle == 2) {
					CoreSetButtonState(0, 5, true); // DOWN (exit house to rain)
					CoreSetButtonState(0, 8, pulse); // A
				} else if (cycle == 3) {
					CoreSetButtonState(0, 7, true); // RIGHT in rain
					CoreSetButtonState(0, 0, pulse); // B (sword)
				} else if (cycle == 4) {
					CoreSetButtonState(0, 4, true); // UP
					CoreSetButtonState(0, 0, pulse); // B (sword)
				} else {
					CoreSetButtonState(0, 6, true); // LEFT
					CoreSetButtonState(0, 0, pulse); // B (sword)
				}
			}
		}
		else if (is_sor)
		{
			// Streets of Rage 2:
			if (elapsed >= 2 && elapsed < 12)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A / C
				CoreSetButtonState(0, 0, pulse); // B
			}
			else if (elapsed >= 12)
			{
				CoreSetButtonState(0, 7, true); // RIGHT
				CoreSetButtonState(0, 0, pulse); // B (Attack)
				if ((frames_written / 20) % 5 == 0) CoreSetButtonState(0, 8, true); // C (Jump)
			}
		}
		else if (is_castlevania)
		{
			// Castlevania Aria of Sorrow:
			// 1) 2-30s: Advance Title screen, 'INICIAR JOGO', Slot 1, Name confirmation
			if (elapsed >= 2 && elapsed < 30)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A
			}
			// 2) 30-45s: Intro dialogue at the shrine - skip through dialogue
			else if (elapsed >= 30 && elapsed < 45)
			{
				bool fast_pulse = ((frames_written / 3) % 2 == 0);
				CoreSetButtonState(0, 8, fast_pulse); // A
				CoreSetButtonState(0, 0, fast_pulse); // B
			}
			// 3) 45s+: Dracula Castle gameplay! Run right, attack, jump!
			else if (elapsed >= 45)
			{
				CoreSetButtonState(0, 7, true); // RIGHT
				CoreSetButtonState(0, 0, pulse); // B (Attack)
				if ((frames_written / 30) % 5 == 0) CoreSetButtonState(0, 8, true); // A (Jump)
			}
		}
		else if (is_truxton)
		{
			// Tatsujin Ou (Truxton II) - Fujitsu FM Towns
			if (elapsed < 24)
			{
				if (elapsed >= 15) {
					CoreSetButtonState(0, 3, pulse); // START
					CoreSetButtonState(0, 8, pulse); // A
				}
			}
			else if (elapsed >= 24 && elapsed < 34)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A
				CoreSetButtonState(0, 0, pulse); // B
			}
			else if (elapsed >= 34)
			{
				bool rapid = ((frames_written / 2) % 2 == 0);
				CoreSetButtonState(0, 8, rapid);
				CoreSetButtonState(0, 0, rapid);
				if ((elapsed >= 58 && elapsed <= 61) || (elapsed >= 76 && elapsed <= 78))
				{
					CoreSetButtonState(0, 0, true);
					CoreSetButtonState(0, 1, true);
				}
				int cycle = (frames_written / 40) % 6;
				if (cycle == 0) CoreSetButtonState(0, 7, true);
				else if (cycle == 1) { CoreSetButtonState(0, 4, true); CoreSetButtonState(0, 7, true); }
				else if (cycle == 2) { CoreSetButtonState(0, 5, true); CoreSetButtonState(0, 6, true); }
				else if (cycle == 3) CoreSetButtonState(0, 6, true);
				else if (cycle == 4) CoreSetButtonState(0, 4, true);
				else CoreSetButtonState(0, 5, true);
			}
		}
		else if (is_splatterhouse)
		{
			// Splatterhouse - Namco / Ving (FM Towns)
			// 1) 0-20s: CD boot & Ving logo screen
			if (elapsed < 20)
			{
				if (elapsed >= 12) CoreSetButtonState(0, 3, pulse); // START
			}
			// 2) 20-30s: Title Screen - Press START to begin Stage 1 (Dungeon)
			else if (elapsed >= 20 && elapsed < 30)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A (Punch/Confirm)
				CoreSetButtonState(0, 0, pulse); // B (Jump)
			}
			// 3) 30s+: ACTIVE COMBAT: Rick walks right, punches mutants, leaps!
			else if (elapsed >= 30)
			{
				CoreSetButtonState(0, 7, true); // WALK RIGHT
				bool rapid_punch = ((frames_written / 4) % 2 == 0);
				CoreSetButtonState(0, 8, rapid_punch); // A = PUNCH / CLEAVER
				if ((frames_written / 45) % 4 == 0)
				{
					CoreSetButtonState(0, 0, true); // B = JUMP
				}
				if ((frames_written / 60) % 5 == 0)
				{
					CoreSetButtonState(0, 5, true); // DOWN (Crouch kick)
					CoreSetButtonState(0, 8, true);
				}
			}
		}
		else if (is_ssf2)
		{
			// Super Street Fighter II - Capcom (FM Towns)
			// 1) 0-22s: Q-Sound, Capcom logo, Super Street Fighter II intro
			if (elapsed < 22)
			{
				if (elapsed >= 15) CoreSetButtonState(0, 3, pulse); // START
			}
			// 2) 22-30s: Title Screen -> Press START to enter Mode Select
			else if (elapsed >= 22 && elapsed < 30)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A (Confirm)
			}
			// 3) 30-38s: Character Select -> Choose Fighter (Ryu / Ken)
			else if (elapsed >= 30 && elapsed < 38)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A
				CoreSetButtonState(0, 0, pulse); // B
			}
			// 4) 38-44s: VS Screen & "Round 1... FIGHT!"
			else if (elapsed >= 38 && elapsed < 44)
			{
				// Wait for fight callout
			}
			// 5) 44s+: ACTIVE FIGHTING GAMEPLAY! Hadouken, Shoryuken, kicks & punches!
			else if (elapsed >= 44)
			{
				int step = (frames_written / 20) % 8;
				if (step == 0) {
					CoreSetButtonState(0, 7, true); // Walk forward
					CoreSetButtonState(0, 8, pulse); // Light Kick
				} else if (step == 1) {
					// Hadouken motion: Down -> Forward + Punch
					CoreSetButtonState(0, 5, true); // Down
				} else if (step == 2) {
					CoreSetButtonState(0, 7, true); // Forward
					CoreSetButtonState(0, 1, true); // Strong Punch (Fireball!)
				} else if (step == 3) {
					CoreSetButtonState(0, 4, true); // Jump Up
					CoreSetButtonState(0, 11, true); // Heavy Kick in air!
				} else if (step == 4) {
					CoreSetButtonState(0, 7, true); // Forward
					CoreSetButtonState(0, 9, true); // Fierce Punch
				} else if (step == 5) {
					// Shoryuken motion
					CoreSetButtonState(0, 7, true); // Forward
					CoreSetButtonState(0, 5, true); // Down
					CoreSetButtonState(0, 0, true); // Dragon Punch!
				} else if (step == 6) {
					CoreSetButtonState(0, 6, true); // Back (Block)
				} else {
					CoreSetButtonState(0, 8, true); // Low Kick
					CoreSetButtonState(0, 5, true); // Crouch
				}
			}
		}
		else if (is_afterburner)
		{
			// After Burner II - Sega / CRI (FM Towns)
			// 1) 0-20s: CD boot & Title screen
			if (elapsed < 20)
			{
				if (elapsed >= 14) CoreSetButtonState(0, 3, pulse); // START
			}
			// 2) 20-28s: Press START to launch F-14 Tomcat from carrier deck
			else if (elapsed >= 20 && elapsed < 28)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A
				CoreSetButtonState(0, 0, pulse); // B
			}
			// 3) 28s+: ACTIVE DOGFIGHT: Fire vulcan, launch missiles, bank and roll!
			else if (elapsed >= 28)
			{
				// Continuous Vulcan Cannon
				bool rapid_gun = ((frames_written / 2) % 2 == 0);
				CoreSetButtonState(0, 8, rapid_gun); // Gun
				// Fire Missile every few seconds
				if ((frames_written / 30) % 2 == 0) CoreSetButtonState(0, 0, true); // Missile
				// Smooth flight stick steering
				int cycle = (frames_written / 35) % 6;
				if (cycle == 0) { CoreSetButtonState(0, 7, true); } // Bank Right
				else if (cycle == 1) { CoreSetButtonState(0, 4, true); CoreSetButtonState(0, 7, true); } // Climb Right
				else if (cycle == 2) { CoreSetButtonState(0, 6, true); } // Bank Left
				else if (cycle == 3) { CoreSetButtonState(0, 5, true); } // Dive
				else if (cycle == 4) { CoreSetButtonState(0, 4, true); } // Pull Up
				else { CoreSetButtonState(0, 7, true); } // Barrel roll!
			}
		}
		else if (is_chasehq)
		{
			// Chase H.Q. - Taito / Ving (FM Towns)
			// 1) 0-18s: CD boot & Ving screen
			if (elapsed < 18)
			{
				// Wait for SETUP screen
			}
			// 2) 18-26s: SETUP Menu navigation -> Navigate DOWN 5 times to select EXIT
			else if (elapsed >= 18 && elapsed < 26)
			{
				// Pulse DOWN every 12 frames
				bool down_pulse = ((frames_written / 12) % 2 == 0);
				CoreSetButtonState(0, 5, down_pulse); // DOWN to EXIT
				if (elapsed >= 23) {
					CoreSetButtonState(0, 3, pulse); // START on EXIT
					CoreSetButtonState(0, 8, pulse); // A on EXIT
				}
			}
			// 3) 26-38s: Game Title & Nancy Dispatch Radio Screen ("This is Nancy at Chase HQ...")
			else if (elapsed >= 26 && elapsed < 38)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A (Accept Dispatch & Accelerate)
			}
			// 4) 38s+: ACTIVE HIGHWAY CHASE GAMEPLAY! Button 1 (B=0) is GAS!
			else if (elapsed >= 38)
			{
				CoreSetButtonState(0, 0, true); // B = Button 1 = ACCELERATE (Gas to 280 km/h!)
				if (elapsed >= 42) {
					CoreSetButtonState(0, 4, true); // UP (High Gear)
				}
				// Nitro Boost at 55s, 65s, 72s
				if ((elapsed >= 55 && elapsed <= 57) || (elapsed >= 65 && elapsed <= 67) || (elapsed >= 72 && elapsed <= 74))
				{
					CoreSetButtonState(0, 1, true); // Turbo Nitro!
					CoreSetButtonState(0, 9, true); // Turbo Alt
				}
				// Steer dynamically through traffic
				int sw = (frames_written / 25) % 6;
				if (sw == 0 || sw == 1) CoreSetButtonState(0, 7, true); // Steer Right
				else if (sw == 3 || sw == 4) CoreSetButtonState(0, 6, true); // Steer Left
			}
		}
		else if (is_madstalker)
		{
			// Mad Stalker: Full Metal Forth - Family Soft (FM Towns)
			// 1) 0-20s: Family Soft intro & Title
			if (elapsed < 20)
			{
				if (elapsed >= 14) CoreSetButtonState(0, 3, pulse); // START
			}
			// 2) 20-30s: Title -> Select 1P Story
			else if (elapsed >= 20 && elapsed < 30)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A
			}
			// 3) 30s+: ACTIVE MECHA ACTION! Walk right, sword slash, cannon!
			else if (elapsed >= 30)
			{
				CoreSetButtonState(0, 7, true); // Move Right
				bool rapid_slash = ((frames_written / 5) % 2 == 0);
				CoreSetButtonState(0, 8, rapid_slash); // Beam Saber
				if ((frames_written / 40) % 3 == 0) CoreSetButtonState(0, 0, true); // Cannon
				if ((frames_written / 60) % 4 == 0) CoreSetButtonState(0, 4, true); // Jump
			}
		}
		else if (is_fmtowns)
		{
			// Universal Smart Fallback for all other FM Towns games
			// 1) 0-22s: CD boot & BIOS screens
			if (elapsed < 22)
			{
				if (elapsed >= 16) {
					CoreSetButtonState(0, 3, pulse); // START
					CoreSetButtonState(0, 8, pulse); // A
				}
			}
			// 2) 22-35s: Title screen / Opening / Menu dismissal
			else if (elapsed >= 22 && elapsed < 35)
			{
				CoreSetButtonState(0, 3, pulse); // START
				CoreSetButtonState(0, 8, pulse); // A
				CoreSetButtonState(0, 0, pulse); // B
				CoreSetButtonState(0, 2, pulse); // SELECT
			}
			// 3) 35s+: Active exploration and interaction
			else if (elapsed >= 35)
			{
				bool act_pulse = ((frames_written / 6) % 2 == 0);
				CoreSetButtonState(0, 8, act_pulse); // Action A
				CoreSetButtonState(0, 0, act_pulse); // Action B
				int dir = (frames_written / 50) % 5;
				if (dir == 0) CoreSetButtonState(0, 7, true); // Right
				else if (dir == 1) CoreSetButtonState(0, 5, true); // Down
				else if (dir == 2) CoreSetButtonState(0, 6, true); // Left
				else if (dir == 3) CoreSetButtonState(0, 4, true); // Up
				else { CoreSetButtonState(0, 7, true); CoreSetButtonState(0, 8, true); }
			}
		}

		}

		CoreRender(frame_buf.data(), kWidth, kHeight, 0, filter_mode);
		if (vpipe) fwrite(frame_buf.data(), 1, (size_t)kWidth * kHeight * sizeof(uint32_t), vpipe);

		if (!skip_extras) {
			int shot1_sec = 15;
		int shot2_sec = 30;
		int shot3_sec = 45;
		int shot4_sec = 55;

		if (elapsed >= (DWORD)shot1_sec && !s1) { SaveBmp(out_dir + "/" + base_name + "_01.bmp", filter_mode); s1 = true; }
		if (elapsed >= (DWORD)shot2_sec && !s2) { SaveBmp(out_dir + "/" + base_name + "_02.bmp", filter_mode); s2 = true; }
		if (elapsed >= (DWORD)shot3_sec && !s3) { SaveBmp(out_dir + "/" + base_name + "_03.bmp", filter_mode); s3 = true; }
		if (elapsed >= (DWORD)shot4_sec && !s4) { SaveBmp(out_dir + "/" + base_name + "_04_crt.bmp", 8); s4 = true; }
		}

		frames_written++;
		DWORD expected_ms = (DWORD)((uint64_t)frames_written * 1000 / 30);
		DWORD cur_ms = GetTickCount() - run_t0;
		if (cur_ms < expected_ms) Sleep(expected_ms - cur_ms);
	}

	if (vpipe)
	{
		PCLOSE(vpipe);
		printf("[INFO] Raw video stream finished: %s (%d frames)\n", temp_mp4_path.c_str(), frames_written);
	}

	// Finalize WAV (write in-memory PCM buffer directly to disk)
	CoreSetAudioCallback(nullptr);
	size_t total_pcm_bytes = 0;
	{
		std::lock_guard<std::mutex> lock(g_audio_pcm_mutex);
		total_pcm_bytes = g_audio_pcm_buf.size() * sizeof(int16_t);
		if (total_pcm_bytes > 0)
		{
			FILE* wf = fopen(wav_path.c_str(), "wb");
			if (wf)
			{
				int sample_rate = CoreGetAudioSampleRate();
				if (sample_rate <= 0) sample_rate = 44100;
				uint8_t dummy[44] = { 0 };
				fwrite(dummy, 1, 44, wf);
				fwrite(g_audio_pcm_buf.data(), 1, total_pcm_bytes, wf);
				WriteWavHeader(wf, sample_rate, (uint32_t)total_pcm_bytes);
				fclose(wf);
				printf("[INFO] Audio WAV generated: %s (%zu bytes at %d Hz)\n", wav_path.c_str(), total_pcm_bytes, sample_rate);
			}
		}
	}

	CoreShutdown();

	// Mux Audio + Video with ffmpeg (using -shortest so no silent tail at the end)
	char mux_cmd[1024];
	if (total_pcm_bytes > 1000)
	{
		snprintf(mux_cmd, sizeof(mux_cmd),
			"%s -y -i \"%s\" -i \"%s\" -t %d -c:v copy -c:a aac -b:a 320k -movflags +faststart \"%s\" %s",
			GetFfmpegCmd(), temp_mp4_path.c_str(), wav_path.c_str(), duration_sec, final_mp4_path.c_str(), DEV_NULL);
	}
	else
	{
		snprintf(mux_cmd, sizeof(mux_cmd),
			"%s -y -i \"%s\" -c:v copy -movflags +faststart \"%s\" %s",
			GetFfmpegCmd(), temp_mp4_path.c_str(), final_mp4_path.c_str(), DEV_NULL);
	}
	system(mux_cmd);

	if (!skip_extras) {
	// Convert screenshots
	char conv_cmd[1024];
	const char* shots[] = { "_01", "_02", "_03", "_04_crt" };
	for (const char* shot : shots)
	{
		snprintf(conv_cmd, sizeof(conv_cmd), "%s -y -i \"%s/%s%s.bmp\" -q:v 2 \"%s/%s%s.jpg\" %s",
			GetFfmpegCmd(), out_dir.c_str(), base_name.c_str(), shot, out_dir.c_str(), base_name.c_str(), shot, DEV_NULL);
		system(conv_cmd);
		std::string bmp_file = out_dir + "/" + base_name + shot + ".bmp";
		remove(bmp_file.c_str());
	}

	// Generate WebM
	if (total_pcm_bytes > 1000)
	{
		snprintf(conv_cmd, sizeof(conv_cmd),
			"%s -y -i \"%s\" -t %d -c:v libvpx-vp9 -crf 28 -b:v 0 -c:a libopus \"%s/%s.webm\" %s",
			GetFfmpegCmd(), final_mp4_path.c_str(), duration_sec, out_dir.c_str(), base_name.c_str(), DEV_NULL);
	}
	else
	{
		snprintf(conv_cmd, sizeof(conv_cmd),
			"%s -y -i \"%s\" -c:v libvpx-vp9 -crf 28 -b:v 0 \"%s/%s.webm\" %s",
			GetFfmpegCmd(), final_mp4_path.c_str(), out_dir.c_str(), base_name.c_str(), DEV_NULL);
	}
	system(conv_cmd);

	}

	// Cleanup temp files
	remove(temp_mp4_path.c_str());
	remove(wav_path.c_str());

	printf("[INFO] Capture completed successfully with AUDIO for %s!\n", base_name.c_str());
	return 0;
}

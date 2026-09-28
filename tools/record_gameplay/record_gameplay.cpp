// Enhanced Autonomous Gameplay & Audio Recorder
// Supports:
// - Direct attract mode / gameplay demo
// - Robust input injection via persistent latches
// - Long-form capture (60s - 120s)
// - Direct PCM audio recording to WAV and AAC muxing

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>

#include "core_runner.h"
#include "hw_render.h"

namespace fs = std::filesystem;

static const int kWidth = 1280;
static const int kHeight = 720;

static FILE* g_audio_file = nullptr;
static uint32_t g_audio_bytes = 0;

static void RecordAudioCallback(const int16_t* data, size_t frames)
{
	if (!g_audio_file || !data || frames == 0) return;
	size_t byte_count = frames * 2 * sizeof(int16_t); // 2 channels stereo, 16-bit
	fwrite(data, 1, byte_count, g_audio_file);
	g_audio_bytes += (uint32_t)byte_count;
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

static bool FfmpegAvailable()
{
	return system("where ffmpeg >nul 2>nul") == 0;
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

	std::string lower_rom = rom_path;
	std::transform(lower_rom.begin(), lower_rom.end(), lower_rom.begin(), ::tolower);
	std::string lower_base = base_name;
	std::transform(lower_base.begin(), lower_base.end(), lower_base.begin(), ::tolower);

	bool is_rally = (lower_rom.find("rally") != std::string::npos || lower_base.find("rally") != std::string::npos);
	bool is_zelda = (lower_rom.find("zelda") != std::string::npos || lower_rom.find("link") != std::string::npos || lower_base.find("zelda") != std::string::npos);
	bool is_sor = (lower_rom.find("streets") != std::string::npos || lower_rom.find("rage") != std::string::npos || lower_rom.find("sor") != std::string::npos || lower_base.find("sor") != std::string::npos || lower_base.find("streets") != std::string::npos);
	bool is_castlevania = (lower_rom.find("castlevania") != std::string::npos || lower_rom.find("aria") != std::string::npos || lower_base.find("castlevania") != std::string::npos);

	printf("[INFO] Game detected: rally=%d, zelda=%d, sor=%d, castlevania=%d (rom: %s, base: %s)\n",
		is_rally, is_zelda, is_sor, is_castlevania, rom_path, base_name.c_str());

	fs::create_directories(out_dir);

	// Setup audio WAV file
	std::string wav_path = out_dir + "/" + base_name + "_temp.wav";
	g_audio_file = fopen(wav_path.c_str(), "wb+");
	if (g_audio_file)
	{
		uint8_t dummy[44] = { 0 };
		fwrite(dummy, 1, 44, g_audio_file);
		g_audio_bytes = 0;
		CoreSetAudioCallback(RecordAudioCallback);
	}

	printf("[INFO] Loading core: %s for %d seconds...\n", core_dll, duration_sec);

	bool started = CoreRequestLoad(rom_path, core_dll);

	DWORD waited_ms = 0;
	while (started && (CoreIsLoading() || !CoreIsRunning()) && waited_ms < 15000)
	{
		Sleep(50);
		waited_ms += 50;
	}

	if (!CoreIsRunning())
	{
		printf("[ERROR] Core failed to run: %s\n", core_dll);
		if (g_audio_file) fclose(g_audio_file);
		return 1;
	}

	printf("[INFO] Core is running! Recording frames for %d seconds...\n", duration_sec);

	std::string temp_mp4_path = out_dir + "/" + base_name + "_temp.mp4";
	std::string final_mp4_path = out_dir + "/" + base_name + ".mp4";

	char pipe_cmd[1024];
	snprintf(pipe_cmd, sizeof(pipe_cmd),
		"ffmpeg -y -f rawvideo -vcodec rawvideo -s %dx%d -pix_fmt bgra -r 30 -i - -c:v libx264 -preset fast -crf 20 -pix_fmt yuv420p \"%s\"",
		kWidth, kHeight, temp_mp4_path.c_str());

	FILE* vpipe = _popen(pipe_cmd, "wb");
	std::vector<uint32_t> frame_buf(kWidth * kHeight, 0);

	DWORD run_t0 = GetTickCount();
	int frames_written = 0;
	bool s1 = false, s2 = false, s3 = false, s4 = false;

	while (CoreIsRunning() && (GetTickCount() - run_t0) < (DWORD)(duration_sec * 1000))
	{
		DWORD elapsed = (GetTickCount() - run_t0) / 1000;

		// Default: clear inputs
		for (int b = 0; b < 16; b++) CoreSetButtonState(0, b, false);
		CoreSetAnalogState(0, 0, 0, 0);
		CoreSetAnalogState(0, 0, 1, 0);

		// Pulsing helper: toggles every 6 frames (~0.2s)
		bool pulse = ((frames_written / 6) % 2 == 0);

		if (is_rally)
		{
			// Top Gear Rally 2:
			// 1) 1-20s: Dismiss 'Controller Pak not found' and advance Title Screen
			if (elapsed >= 1 && elapsed < 20)
			{
				CoreSetButtonState(0, 8, pulse); // A button
				CoreSetButtonState(0, 0, pulse); // B button
				CoreSetButtonState(0, 3, pulse); // START
			}
			// 2) 20-28s: Confirm Menu / Practice / Car / Track selection (ONLY A button, NEVER START!)
			else if (elapsed >= 20 && elapsed < 28)
			{
				CoreSetButtonState(0, 8, pulse); // A button
			}
			// 3) 28s+: ACTIVE RACE! Hold Gas (A + R) and steer smoothly! NEVER press START!
			else if (elapsed >= 28)
			{
				CoreSetButtonState(0, 8, true);  // A button = ACCELERATE / GAS
				CoreSetButtonState(0, 11, true); // R trigger = ACCELERATE
				// Smooth steering sine wave so car follows dirt track
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

		CoreRender(frame_buf.data(), kWidth, kHeight, 0, 0);
		if (vpipe) fwrite(frame_buf.data(), 1, (size_t)kWidth * kHeight * sizeof(uint32_t), vpipe);

		if (elapsed >= 15 && !s1) { SaveBmp(out_dir + "/" + base_name + "_01.bmp", 0); s1 = true; }
		if (elapsed >= 35 && !s2) { SaveBmp(out_dir + "/" + base_name + "_02.bmp", 0); s2 = true; }
		if (elapsed >= 60 && !s3) { SaveBmp(out_dir + "/" + base_name + "_03.bmp", 0); s3 = true; }
		if (elapsed >= (DWORD)(duration_sec - 5) && !s4) { SaveBmp(out_dir + "/" + base_name + "_04_crt.bmp", 8); s4 = true; }

		frames_written++;
		Sleep(33); // ~30 fps
	}

	if (vpipe)
	{
		_pclose(vpipe);
		printf("[INFO] Raw video stream finished: %s (%d frames)\n", temp_mp4_path.c_str(), frames_written);
	}

	// Finalize WAV
	CoreSetAudioCallback(nullptr);
	if (g_audio_file)
	{
		int sample_rate = CoreGetAudioSampleRate();
		if (sample_rate <= 0) sample_rate = 44100;
		WriteWavHeader(g_audio_file, sample_rate, g_audio_bytes);
		fclose(g_audio_file);
		g_audio_file = nullptr;
		printf("[INFO] Audio WAV generated: %s (%u bytes at %d Hz)\n", wav_path.c_str(), g_audio_bytes, sample_rate);
	}

	CoreShutdown();

	// Mux Audio + Video with ffmpeg (using -shortest so no silent tail at the end)
	char mux_cmd[1024];
	if (g_audio_bytes > 1000)
	{
		snprintf(mux_cmd, sizeof(mux_cmd),
			"ffmpeg -y -i \"%s\" -i \"%s\" -c:v copy -c:a aac -b:a 192k -shortest -movflags +faststart \"%s\" 2>nul",
			temp_mp4_path.c_str(), wav_path.c_str(), final_mp4_path.c_str());
	}
	else
	{
		snprintf(mux_cmd, sizeof(mux_cmd),
			"ffmpeg -y -i \"%s\" -c:v copy -movflags +faststart \"%s\" 2>nul",
			temp_mp4_path.c_str(), final_mp4_path.c_str());
	}
	system(mux_cmd);

	// Convert screenshots
	char conv_cmd[1024];
	const char* shots[] = { "_01", "_02", "_03", "_04_crt" };
	for (const char* shot : shots)
	{
		snprintf(conv_cmd, sizeof(conv_cmd), "ffmpeg -y -i \"%s/%s%s.bmp\" -q:v 2 \"%s/%s%s.jpg\" 2>nul",
			out_dir.c_str(), base_name.c_str(), shot, out_dir.c_str(), base_name.c_str(), shot);
		system(conv_cmd);
		std::string bmp_file = out_dir + "/" + base_name + shot + ".bmp";
		remove(bmp_file.c_str());
	}

	// Generate WebM
	if (g_audio_bytes > 1000)
	{
		snprintf(conv_cmd, sizeof(conv_cmd),
			"ffmpeg -y -i \"%s\" -c:v libvpx-vp9 -crf 28 -b:v 0 -c:a libopus \"%s/%s.webm\" 2>nul",
			final_mp4_path.c_str(), out_dir.c_str(), base_name.c_str());
	}
	else
	{
		snprintf(conv_cmd, sizeof(conv_cmd),
			"ffmpeg -y -i \"%s\" -c:v libvpx-vp9 -crf 28 -b:v 0 \"%s/%s.webm\" 2>nul",
			final_mp4_path.c_str(), out_dir.c_str(), base_name.c_str());
	}
	system(conv_cmd);

	// Cleanup temp files
	remove(temp_mp4_path.c_str());
	remove(wav_path.c_str());

	printf("[INFO] Capture completed successfully with AUDIO for %s!\n", base_name.c_str());
	return 0;
}

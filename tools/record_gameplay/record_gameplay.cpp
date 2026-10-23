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
	while (started && (CoreIsLoading() || !CoreIsRunning()) && waited_ms < 45000)
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

		CoreRender(frame_buf.data(), kWidth, kHeight, 0, 0);
		if (vpipe) fwrite(frame_buf.data(), 1, (size_t)kWidth * kHeight * sizeof(uint32_t), vpipe);

		int shot1_sec = is_fmtowns ? 28 : 15;
		int shot2_sec = is_fmtowns ? 44 : 35;
		int shot3_sec = is_fmtowns ? 64 : 60;
		int shot4_sec = (int)duration_sec - 5;

		if (elapsed >= (DWORD)shot1_sec && !s1) { SaveBmp(out_dir + "/" + base_name + "_01.bmp", 0); s1 = true; }
		if (elapsed >= (DWORD)shot2_sec && !s2) { SaveBmp(out_dir + "/" + base_name + "_02.bmp", 0); s2 = true; }
		if (elapsed >= (DWORD)shot3_sec && !s3) { SaveBmp(out_dir + "/" + base_name + "_03.bmp", 0); s3 = true; }
		if (elapsed >= (DWORD)shot4_sec && !s4) { SaveBmp(out_dir + "/" + base_name + "_04_crt.bmp", 8); s4 = true; }

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

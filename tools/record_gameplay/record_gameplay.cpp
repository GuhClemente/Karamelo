// Dev-only content generator: loads a libretro core + ROM headlessly, plays
// it for a fixed duration with a couple of scripted button presses to get
// past a boot/title screen, and records the output to an MP4/WEBM plus a
// handful of JPG screenshots (including one with the CRT filter applied).
// Built for producing preview media for the karamelo-emu.com site - it is not
// part of the shipped app and is never linked into Karamelo.exe.
//
// Usage: record_gameplay.exe <core_dll> <rom_path> <duration_sec> <out_dir> [base_name]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <filesystem>

#include "core_runner.h"
#include "hw_render.h"

namespace fs = std::filesystem;

static const int kWidth = 1280;
static const int kHeight = 720;

static bool FfmpegAvailable()
{
	// "where" exits 0 only if ffmpeg.exe is found on PATH.
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
		printf("[ERROR] ffmpeg was not found on PATH - install it (or add it to PATH) before running this tool.\n");
		return 1;
	}

	const char* core_dll = argv[1];
	const char* rom_path = argv[2];
	int duration_sec = atoi(argv[3]);
	if (duration_sec < 5) duration_sec = 30;
	std::string out_dir = argv[4];
	std::string base_name = (argc > 5) ? argv[5] : "gameplay";
	fs::create_directories(out_dir);

	printf("[INFO] Loading core: %s, rom: %s for %d seconds...\n", core_dll, rom_path, duration_sec);
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
		return 1;
	}

	printf("[INFO] Core is running! Recording 30fps frames for %d seconds...\n", duration_sec);

	std::string mp4_path = out_dir + "/" + base_name + ".mp4";
	char pipe_cmd[1024];
	snprintf(pipe_cmd, sizeof(pipe_cmd),
		"ffmpeg -y -f rawvideo -vcodec rawvideo -s %dx%d -pix_fmt bgra -r 30 -i - -c:v libx264 -preset fast -crf 20 -movflags +faststart -pix_fmt yuv420p \"%s\"",
		kWidth, kHeight, mp4_path.c_str());

	FILE* vpipe = _popen(pipe_cmd, "wb");
	std::vector<uint32_t> frame_buf(kWidth * kHeight, 0);

	DWORD run_t0 = GetTickCount();
	int frames_written = 0;
	bool s1 = false, s2 = false, s3 = false, s4 = false;

	while (CoreIsRunning() && (GetTickCount() - run_t0) < (DWORD)(duration_sec * 1000))
	{
		DWORD elapsed = (GetTickCount() - run_t0) / 1000;

		// Auto-press start/action buttons at a couple of points to get past a
		// title screen into real gameplay, then release for the rest.
		if (elapsed >= 4 && elapsed < 6)
		{
			CoreSetButtonState(0, 3, true); // START
			CoreSetButtonState(0, 0, true); // B/A
		}
		else if (elapsed >= 8 && elapsed < 10)
		{
			CoreSetButtonState(0, 3, true); // START
			CoreSetButtonState(0, 1, true); // A
		}
		else
		{
			CoreSetButtonState(0, 3, false);
			CoreSetButtonState(0, 0, false);
			CoreSetButtonState(0, 1, false);
		}

		CoreRender(frame_buf.data(), kWidth, kHeight, 0, 0);
		if (vpipe) fwrite(frame_buf.data(), 1, (size_t)kWidth * kHeight * sizeof(uint32_t), vpipe);

		if (elapsed >= 8 && !s1) { SaveBmp(out_dir + "/" + base_name + "_01.bmp", 0); s1 = true; printf("[SHOT 1] 8s captured\n"); }
		if (elapsed >= 18 && !s2) { SaveBmp(out_dir + "/" + base_name + "_02.bmp", 0); s2 = true; printf("[SHOT 2] 18s captured\n"); }
		if (elapsed >= 26 && !s3) { SaveBmp(out_dir + "/" + base_name + "_03.bmp", 0); s3 = true; printf("[SHOT 3] 26s captured\n"); }
		if (elapsed >= 29 && !s4) { SaveBmp(out_dir + "/" + base_name + "_04_crt.bmp", 8); s4 = true; printf("[SHOT 4 CRT] 29s captured\n"); }

		frames_written++;
		Sleep(33); // ~30 fps
	}

	if (vpipe)
	{
		_pclose(vpipe);
		printf("[INFO] Video MP4 generated: %s (%d frames)\n", mp4_path.c_str(), frames_written);
	}

	CoreShutdown();

	char conv_cmd[1024];
	const char* shots[] = { "_01", "_02", "_03", "_04_crt" };
	for (const char* shot : shots)
	{
		snprintf(conv_cmd, sizeof(conv_cmd), "ffmpeg -y -i \"%s/%s%s.bmp\" -q:v 2 \"%s/%s%s.jpg\" 2>nul",
			out_dir.c_str(), base_name.c_str(), shot, out_dir.c_str(), base_name.c_str(), shot);
		system(conv_cmd);
	}

	snprintf(conv_cmd, sizeof(conv_cmd), "ffmpeg -y -i \"%s\" -c:v libvpx-vp9 -crf 28 -b:v 0 \"%s/%s.webm\" 2>nul", mp4_path.c_str(), out_dir.c_str(), base_name.c_str());
	system(conv_cmd);

	printf("[INFO] Capture completed for %s!\n", base_name.c_str());
	return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <dwmapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "osd.h"
#include "menu.h"
#include "app_info.h"
#include <filesystem>
namespace fs = std::filesystem;
#include "charrom.h"
#include "libretro.h"
#include "core_runner.h"
#include "netplay.h"
#include "retroachievements.h"
#include "updater.h"
#include "port_runner.h"
#include "hw_render.h"
#include "resource.h"
#include "mister_math.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "dwmapi.lib")

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

const int CANVAS_WIDTH = 640;
const int CANVAS_HEIGHT = 360;

// Color Theme Structure
struct ThemeColor
{
	uint32_t header_bg;
	uint32_t header_txt;
	uint32_t border_out;
	uint32_t border_in;
	uint32_t side_bg;
	uint32_t side_txt;
	uint32_t side_line;
	uint32_t menu_bg;
	uint32_t cursor_bg;
	uint32_t cursor_txt;
	uint32_t text_white;
	uint32_t text_dim;
};

// 0=Red/Burgundy (Screenshot Authentic), 1=Blue (Classic MiSTer), 2=Green, 3=Amber, 4=Gray, 5=Dark
static const ThemeColor THEMES[] = {
	// 0: Red / Warm Burgundy (Authentic MiSTer Screenshot Palette)
	{ 0x00DFC4C1, 0x001E0808, 0x00000000, 0x00000000, 0x00DFC4C1, 0x001E0808, 0x001E0808, 0x001E0808, 0x00DFC4C1, 0x001E0808, 0x00DFC4C1, 0x00A09090 },
	// 1: Blue (Classic MiSTer)
	{ 0x00D0DEF4, 0x000E1428, 0x00000000, 0x00000000, 0x00D0DEF4, 0x000E1428, 0x000E1428, 0x000E1428, 0x00D0DEF4, 0x000E1428, 0x00D0DEF4, 0x0090A0B0 },
	// 2: Green (Phosphor)
	{ 0x00D0F4D8, 0x00082010, 0x00000000, 0x00000000, 0x00D0F4D8, 0x00082010, 0x00082010, 0x00082010, 0x00D0F4D8, 0x00082010, 0x00D0F4D8, 0x0090B098 },
	// 3: Amber (Retro CRT Amber)
	{ 0x00F8E8C8, 0x00281808, 0x00000000, 0x00000000, 0x00F8E8C8, 0x00281808, 0x00281808, 0x00281808, 0x00F8E8C8, 0x00281808, 0x00F8E8C8, 0x00B8A890 },
	// 4: Gray (Arcade Monochrome)
	{ 0x00E0E0E0, 0x00181818, 0x00000000, 0x00000000, 0x00E0E0E0, 0x00181818, 0x00181818, 0x00181818, 0x00E0E0E0, 0x00181818, 0x00E0E0E0, 0x00A0A0A0 },
	// 5: Dark Obsidian
	{ 0x00C8C8D4, 0x000E0E14, 0x00000000, 0x00000000, 0x00C8C8D4, 0x000E0E14, 0x000E0E14, 0x000E0E14, 0x00C8C8D4, 0x000E0E14, 0x00C8C8D4, 0x009090A0 }
};

static uint32_t* pixel_buffer = nullptr;
static HBITMAP h_bitmap = nullptr;
static HDC h_mem_dc = nullptr;
static HWND g_hwnd = nullptr;
HWND MainGetHwnd() { return g_hwnd; }
static bool g_running = true;
static bool g_is_fullscreen = false;

static DWORD last_gamepad_packet = 0;
static uint32_t noise_seed = 0x12345678;

static inline uint32_t FastRand()
{
	noise_seed ^= noise_seed << 13;
	noise_seed ^= noise_seed >> 17;
	noise_seed ^= noise_seed << 5;
	return noise_seed;
}

static void InitBackbuffer(HDC hdc)
{
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = CANVAS_WIDTH;
	bmi.bmiHeader.biHeight = -CANVAS_HEIGHT;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	h_mem_dc = CreateCompatibleDC(hdc);
	h_bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&pixel_buffer, NULL, 0);
	SelectObject(h_mem_dc, h_bitmap);
}

// -------------------------------------------------------------
// Full-resolution presentation buffer.
//
// The 640x360 canvas above is the OSD's native size and stays as it is. But
// routing gameplay through it meant a 640x480 N64 frame was squeezed to 480x360
// and then blown back up to the window - two resamples and a hard quality loss.
// While a game is running with no OSD on screen we scale the core's framebuffer
// straight to the window size instead, so there is only one resample.
// -------------------------------------------------------------
#define PRESENT_MAX_W 1920
#define PRESENT_MAX_H 1200

static uint32_t* present_buffer = nullptr;
static HBITMAP   h_present_bitmap = nullptr;
static HDC       h_present_dc = nullptr;
static int       present_w = 0;
static int       present_h = 0;
static bool      g_use_present = false;

// When the OSD is open we still show the game at native resolution and blit only
// the OSD card over it, instead of dropping the whole picture back to 640x360.
static RECT      g_osd_rect = { 0, 0, 0, 0 };
static bool      g_osd_rect_valid = false;

static bool EnsurePresentBuffer(int w, int h)
{
	if (w < 16 || h < 16) return false;
	if (w > PRESENT_MAX_W) w = PRESENT_MAX_W;
	if (h > PRESENT_MAX_H) h = PRESENT_MAX_H;

	if (present_buffer && w == present_w && h == present_h) return true;

	if (h_present_dc) { DeleteDC(h_present_dc); h_present_dc = nullptr; }
	if (h_present_bitmap) { DeleteObject(h_present_bitmap); h_present_bitmap = nullptr; }
	present_buffer = nullptr;

	HDC hdc = GetDC(g_hwnd);
	if (!hdc) return false;

	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	h_present_dc = CreateCompatibleDC(hdc);
	h_present_bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&present_buffer, NULL, 0);
	ReleaseDC(g_hwnd, hdc);

	if (!h_present_dc || !h_present_bitmap || !present_buffer)
	{
		if (h_present_dc) { DeleteDC(h_present_dc); h_present_dc = nullptr; }
		if (h_present_bitmap) { DeleteObject(h_present_bitmap); h_present_bitmap = nullptr; }
		present_buffer = nullptr;
		return false;
	}

	SelectObject(h_present_dc, h_present_bitmap);
	present_w = w;
	present_h = h;
	memset(present_buffer, 0, (size_t)w * h * sizeof(uint32_t));
	return true;
}

static void DrawCharTo(uint32_t* buf, int bw, int bh, int x, int y, char c, uint32_t color)
{
	if (!buf) return;
	const unsigned char* p = &charfont[(unsigned char)c][0];
	for (int col = 0; col < 8; col++)
	{
		unsigned char b = p[col];
		for (int row = 0; row < 8; row++)
		{
			if (b & (1 << row))
			{
				int px = x + col;
				int py = y + row;
				if (px >= 0 && px < bw && py >= 0 && py < bh)
				{
					buf[py * bw + px] = color;
				}
			}
		}
	}
}

static void DrawRotatedCharTo(uint32_t* buf, int bw, int bh, int x, int y, char c, uint32_t color)
{
	if (!buf) return;
	const unsigned char* p = &charfont[(unsigned char)c][0];
	for (int col = 0; col < 8; col++)
	{
		unsigned char b = p[col];
		for (int row = 0; row < 8; row++)
		{
			if (b & (1 << row))
			{
				int px = x + row;
				int py = y + (7 - col);
				if (px >= 0 && px < bw && py >= 0 && py < bh)
				{
					buf[py * bw + px] = color;
				}
			}
		}
	}
}

static void DrawStringTo(uint32_t* buf, int bw, int bh, int x, int y, const char* str, uint32_t color)
{
	while (*str)
	{
		DrawCharTo(buf, bw, bh, x, y, *str++, color);
		x += 8;
	}
}

// Integer-scaled variants. The 8x8 font is sized for the 640x360 canvas; on a
// buffer at the display's native resolution it needs multiplying up or it
// comes out a few millimetres tall.
static void DrawCharToScaled(uint32_t* buf, int bw, int bh, int x, int y, char c, uint32_t color, int scale)
{
	if (!buf || scale < 1) return;
	const unsigned char* p = &charfont[(unsigned char)c][0];
	for (int col = 0; col < 8; col++)
	{
		unsigned char b = p[col];
		for (int row = 0; row < 8; row++)
		{
			if (!(b & (1 << row))) continue;
			for (int sy = 0; sy < scale; sy++)
			{
				int py = y + row * scale + sy;
				if (py < 0 || py >= bh) continue;
				uint32_t* dst = buf + (size_t)py * bw;
				for (int sx = 0; sx < scale; sx++)
				{
					int px = x + col * scale + sx;
					if (px >= 0 && px < bw) dst[px] = color;
				}
			}
		}
	}
}

static void DrawStringToScaled(uint32_t* buf, int bw, int bh, int x, int y, const char* str, uint32_t color, int scale)
{
	while (*str)
	{
		DrawCharToScaled(buf, bw, bh, x, y, *str++, color, scale);
		x += 8 * scale;
	}
}

static void DrawChar(int x, int y, char c, uint32_t color)
{
	DrawCharTo(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, x, y, c, color);
}

static void DrawString(int x, int y, const char* str, uint32_t color)
{
	DrawStringTo(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, x, y, str, color);
}

// Drawn into whichever buffer is being presented, so a save-state toast does
// not knock the picture back down to the 640x360 canvas for two seconds.
static void DrawToast(uint32_t* buf, int bw, int bh, const ThemeColor& theme)
{
	const char* msg = CoreGetToast();
	int msg_len = (int)strlen(msg);
	if (msg_len <= 0) return;

	// Sized against the buffer, not in fixed pixels: this is drawn into the
	// presented frame at the display's native resolution, so a fixed 18px bar
	// shrinks to a sliver on a 1440p screen. 360 is the design canvas height.
	int scale = bh / 360;
	if (scale < 1) scale = 1;
	else if (scale > 6) scale = 6;

	// A long message - an achievement line carries the whole game title - must
	// still fit across the width, so give scale back until it does.
	while (scale > 1 && msg_len * 8 * scale + 24 * scale > bw) scale--;

	int tw = msg_len * 8 * scale + 24 * scale;
	int th = 18 * scale;
	int tx = (bw - tw) / 2;
	int ty = bh / 24;
	int sh = 2 * scale;

	for (int y = ty + sh; y < ty + th + sh; y++)
		for (int x = tx + sh; x < tx + tw + sh; x++)
			if (x >= 0 && x < bw && y >= 0 && y < bh) buf[y * bw + x] = 0x00020308;

	for (int y = ty - scale; y < ty + th + scale; y++)
		for (int x = tx - scale; x < tx + tw + scale; x++)
			if (x >= 0 && x < bw && y >= 0 && y < bh) buf[y * bw + x] = theme.border_out;

	for (int y = ty; y < ty + th; y++)
		for (int x = tx; x < tx + tw; x++)
			if (x >= 0 && x < bw && y >= 0 && y < bh) buf[y * bw + x] = theme.menu_bg;

	DrawStringToScaled(buf, bw, bh, tx + 12 * scale, ty + 5 * scale, msg, theme.text_white, scale);
}

static void ToggleFullscreen(HWND hwnd)
{
	static WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };
	DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);

	if (dwStyle & WS_OVERLAPPEDWINDOW)
	{
		MONITORINFO mi = { sizeof(mi) };
		if (GetWindowPlacement(hwnd, &g_wpPrev) &&
			GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi))
		{
			SetWindowLong(hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
			SetWindowPos(hwnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left,
				mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			g_is_fullscreen = true;
		}
	}
	else
	{
		SetWindowLong(hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(hwnd, &g_wpPrev);
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		g_is_fullscreen = false;
	}
	MenuSetFullscreen(g_is_fullscreen);
}

// One presented frame: menu logic, drawing, and the blit.
// -------------------------------------------------------------
// -------------------------------------------------------------
// Accurate Hardware Display Refresh Rate Detection
// -------------------------------------------------------------
// Accurate Hardware Display Refresh Rate Detection
// SnapToStandardRate now lives in mister_math.cpp so the tests can reach it.
// -------------------------------------------------------------
static double DetectDisplayRefreshRate(HWND hwnd, bool* out_dwm_ok = nullptr)
{
	double display_fps = 60.0;
	bool dwm_timing_ok = false;

	// 1. Try DwmGetCompositionTimingInfo (precise desktop compositor rate)
	BOOL dwm_enabled = FALSE;
	DwmIsCompositionEnabled(&dwm_enabled);
	if (dwm_enabled)
	{
		DWM_TIMING_INFO ti;
		memset(&ti, 0, sizeof(ti));
		ti.cbSize = sizeof(ti);
		if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &ti)) && ti.rateRefresh.uiDenominator > 0)
		{
			double raw = (double)ti.rateRefresh.uiNumerator / (double)ti.rateRefresh.uiDenominator;
			if (raw >= 24.0 && raw <= 400.0)
			{
				display_fps = SnapToStandardRate(raw);
				dwm_timing_ok = true;

				// DWM can report the cadence of a virtual display adapter
				// rather than the panel. If the display mode disagrees by more
				// than 2%, trust the mode: it comes from the driver.
				DEVMODEA dm;
				memset(&dm, 0, sizeof(dm));
				dm.dmSize = sizeof(dm);
				if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) &&
					dm.dmDisplayFrequency >= 24 && dm.dmDisplayFrequency <= 400)
				{
					double mode_fps = SnapToStandardRate((double)dm.dmDisplayFrequency);
					if (fabs(mode_fps - display_fps) / mode_fps > 0.02)
						display_fps = mode_fps;
				}
			}
		}
	}

	// 2. If DWM timing not available or invalid, check EnumDisplaySettings
	if (!dwm_timing_ok)
	{
		DEVMODEA dm;
		memset(&dm, 0, sizeof(dm));
		dm.dmSize = sizeof(dm);
		if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency >= 24 && dm.dmDisplayFrequency <= 400)
		{
			display_fps = SnapToStandardRate((double)dm.dmDisplayFrequency);
		}
		else
		{
			HDC dc = hwnd ? GetDC(hwnd) : NULL;
			int v = dc ? GetDeviceCaps(dc, VREFRESH) : 0;
			if (dc) ReleaseDC(hwnd, dc);
			if (v >= 24 && v <= 400) display_fps = SnapToStandardRate((double)v);
		}
	}

	if (out_dwm_ok) *out_dwm_ok = dwm_timing_ok;
	if (display_fps < 24.0 || display_fps > 400.0) display_fps = 60.0;
	return display_fps;
}

static void PresentFrame(HWND hwnd);

// Last-resort crash reporter: writes the faulting address as an offset into
// the executable, which maps straight to a function in the .map file.
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
	HMODULE base = GetModuleHandleA(NULL);
	void* addr = ep->ExceptionRecord->ExceptionAddress;

	// Name the module that actually faulted. An RVA outside our executable only
	// says "not us"; the module name says which core, and the thread id says
	// whether the fault is even on a thread we control - our SEH guards only
	// cover the thread running them, and cores spawn their own.
	char module[MAX_PATH] = "?";
	HMODULE fault_mod = NULL;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)addr, &fault_mod) && fault_mod)
	{
		char full[MAX_PATH];
		if (GetModuleFileNameA(fault_mod, full, MAX_PATH))
		{
			const char* slash = strrchr(full, 92);
			strncpy_s(module, sizeof(module), slash ? slash + 1 : full, _TRUNCATE);
		}
	}

	FILE* f = fopen("mister_flavor.log", "a");
	if (f)
	{
		fprintf(f, "[ERROR] [CRASH] codigo=0x%08lX modulo=%s offset=0x%llX thread=%lu\n",
			ep->ExceptionRecord->ExceptionCode, module,
			fault_mod ? (unsigned long long)((uintptr_t)addr - (uintptr_t)fault_mod)
			          : (unsigned long long)((uintptr_t)addr - (uintptr_t)base),
			GetCurrentThreadId());

		if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
			ep->ExceptionRecord->NumberParameters >= 2)
		{
			fprintf(f, "[ERROR] [CRASH] %s no endereco 0x%llX\n",
				ep->ExceptionRecord->ExceptionInformation[0] ? "escrita" : "leitura",
				(unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
		}
		// Crude stack walk: scan upward from RSP for values that land inside our
		// own code section. Enough to identify the caller without dbghelp.
		// .text runs to roughly +0x80000 in this build; anything in that range
		// on the stack is a return address into our own code.
		uintptr_t lo = (uintptr_t)base + 0x1000;
		uintptr_t hi = (uintptr_t)base + 0x80000;

		uintptr_t* sp = (uintptr_t*)ep->ContextRecord->Rsp;
		fprintf(f, "[ERROR] [CRASH] pilha (RVAs no executavel):");

		int found = 0;
		for (int i = 0; i < 4096 && found < 12; i++)
		{
			uintptr_t v = 0;
			// Was IsBadReadPtr, which is documented as broken for exactly this:
			// it probes the address and in doing so consumes the stack guard
			// page, corrupting the very stack we are trying to walk. VirtualQuery
			// asks the same question without touching the memory.
			MEMORY_BASIC_INFORMATION mbi;
			if (!VirtualQuery(&sp[i], &mbi, sizeof(mbi))) break;
			if (mbi.State != MEM_COMMIT) break;
			if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) break;
			v = sp[i];
			if (v > lo && v < hi) { fprintf(f, " 0x%llX", (unsigned long long)(v - (uintptr_t)base)); found++; }
		}
		fprintf(f, "\n");

		fclose(f);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

static void RenderFrame()
{
	if (!pixel_buffer) return;

	int theme_idx = MenuGetOsdTheme();
	if (theme_idx < 0 || theme_idx >= 6) theme_idx = 0;
	const ThemeColor& theme = THEMES[theme_idx];

	// Gameplay with nothing overlaid: scale the core framebuffer straight to
	// the window. Anything that needs the OSD layer falls through to the
	// 640x360 canvas path below, unchanged.
	g_use_present = false;
	g_osd_rect_valid = false;

	if (CoreIsRunning() && !CoreIsLoading())
	{
		RECT client;
		GetClientRect(g_hwnd, &client);
		int cw = client.right - client.left;
		int ch = client.bottom - client.top;

		if (EnsurePresentBuffer(cw, ch))
		{
			CoreRender(present_buffer, present_w, present_h, MenuGetAspectMode(), MenuGetFilterMode());
			g_use_present = true;

			if (!OsdIsEnabled())
			{
				if (CoreIsToastActive()) DrawToast(present_buffer, present_w, present_h, theme);
				return;
			}

			// The OSD card has soft shadow corners that let the picture behind
			// show through, so the small canvas still needs the game drawn on
			// it as a backdrop. Only the card's rectangle is blitted on top.
			CoreRender(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, MenuGetAspectMode(), MenuGetFilterMode());
		}
	}

	if (CoreIsLoading())
	{
		// The core thread is busy extracting / opening the disc. Keep painting
		// so the window stays responsive instead of going grey.
		for (int y = 0; y < CANVAS_HEIGHT; y++)
			for (int x = 0; x < CANVAS_WIDTH; x++)
				pixel_buffer[y * CANVAS_WIDTH + x] = 0x00020308;

		static int spin = 0;
		spin++;
		const char* dots = (spin / 20) % 3 == 0 ? "." : ((spin / 20) % 3 == 1 ? ".." : "...");

		char line[64];
		snprintf(line, sizeof(line), "LOADING%s", dots);
		DrawString((CANVAS_WIDTH - (int)strlen(line) * 8) / 2, CANVAS_HEIGHT / 2 - 4, line, theme.text_white);

		return;
	}

	if (CoreIsRunning())
	{
		// The core runs on its own thread; here we only present its framebuffer.
		if (!g_use_present)
			CoreRender(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, MenuGetAspectMode(), MenuGetFilterMode());

		// Drawn even with the OSD open. A failed load re-enables the OSD, so
		// hiding the toast behind it meant every failure - missing BIOS,
		// unsupported format, absent core - happened in complete silence and
		// looked like the menu had simply ignored the click.
		if (CoreIsToastActive())
		{
			DrawToast(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, theme);
		}
	}
	else
	{
		int wall_mode = MenuGetWallpaperMode();

		static uint32_t wall_chef[CANVAS_WIDTH * CANVAS_HEIGHT] = { 0 };
		static uint32_t wall_arcade[CANVAS_WIDTH * CANVAS_HEIGHT] = { 0 };
		static bool custom_walls_loaded = false;

		if (!custom_walls_loaded)
		{
			FILE* fc = fopen("wallpapers/sabor_mister_chef.raw", "rb");
			if (fc) { fread(wall_chef, sizeof(uint32_t), CANVAS_WIDTH * CANVAS_HEIGHT, fc); fclose(fc); }

			FILE* fa = fopen("wallpapers/sabor_mister_arcade.raw", "rb");
			if (fa) { fread(wall_arcade, sizeof(uint32_t), CANVAS_WIDTH * CANVAS_HEIGHT, fa); fclose(fa); }

			custom_walls_loaded = true;
		}

		if (wall_mode == 1) // TV Static Noise (From National MiSTer Screenshots!)
		{
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				uint32_t r = FastRand();
				uint8_t grain = (r & 0xFF) > 130 ? ((r >> 8) & 0xC0) : ((r >> 16) & 0x30);
				pixel_buffer[i] = (grain << 16) | (grain << 8) | grain;
			}
		}
		else if (wall_mode == 2) // 128 Parallax Stars
		{
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				pixel_buffer[i] = 0x00040710;
			}

			int star_count = 0;
			const StarPoint* stars = OsdGetStars(&star_count);
			for (int i = 0; i < star_count; i++)
			{
				int sx = stars[i].x;
				int sy = stars[i].y;
				if (sx >= 0 && sx < CANVAS_WIDTH && sy >= 0 && sy < CANVAS_HEIGHT)
				{
					uint8_t br = stars[i].brightness;
					uint32_t col = (br << 16) | (br << 8) | (br < 220 ? (br + 25) : 255);
					pixel_buffer[sy * CANVAS_WIDTH + sx] = col;
					if (stars[i].speed >= 3 && sx + 1 < CANVAS_WIDTH)
					{
						pixel_buffer[sy * CANVAS_WIDTH + sx + 1] = col;
					}
				}
			}
		}
		else if (wall_mode == 3) // Cyber Grid
		{
			for (int y = 0; y < CANVAS_HEIGHT; y++)
			{
				for (int x = 0; x < CANVAS_WIDTH; x++)
				{
					if (y > 180 && (y % 16 == 0 || (x + y * 2) % 32 == 0))
					{
						pixel_buffer[y * CANVAS_WIDTH + x] = 0x00006080;
					}
					else
					{
						pixel_buffer[y * CANVAS_WIDTH + x] = 0x00080814;
					}
				}
			}
		}
		else if (wall_mode == 4) // Flavor Chef (Gourmet Retro)
		{
			memcpy(pixel_buffer, wall_chef, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint32_t));
		}
		else if (wall_mode == 5) // Flavor Arcade (Spicy 60FPS)
		{
			memcpy(pixel_buffer, wall_arcade, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint32_t));
		}
		else // None (Pure Deep Black)
		{
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				pixel_buffer[i] = 0x00000000;
			}
		}
	}

	// Render Centered OSD Card + Floating Header
	if (OsdIsEnabled())
	{
		const uint8_t* osd = OsdGetBuffer();
		const uint8_t* invert_map = OsdGetInvertMap();
		int osd_lines = OsdGetSize();
		if (osd_lines <= 0) osd_lines = 15;

		const int OSD_ROW_H = 12;
		const int side_w = 18;
		int osd_w = 232;
		int osd_h = osd_lines * OSD_ROW_H;

		int ox = (CANVAS_WIDTH - osd_w) / 2;
		int hdr_h = 14;
		int gap = 12;
		int total_h = hdr_h + gap + osd_h;
		int hdr_y = (CANVAS_HEIGHT - total_h) / 2;
		int oy = hdr_y + hdr_h + gap;

		// Bounds of everything this block paints: floating header + card + 1px border.
		// WM_PAINT blits exactly this region over the native-res game.
		g_osd_rect.left   = ox - 2;
		g_osd_rect.top    = hdr_y - 2;
		g_osd_rect.right  = ox + osd_w + 2;
		g_osd_rect.bottom = oy + osd_h + 2;
		if (g_osd_rect.left < 0) g_osd_rect.left = 0;
		if (g_osd_rect.top < 0) g_osd_rect.top = 0;
		if (g_osd_rect.right > CANVAS_WIDTH) g_osd_rect.right = CANVAS_WIDTH;
		if (g_osd_rect.bottom > CANVAS_HEIGHT) g_osd_rect.bottom = CANVAS_HEIGHT;
		g_osd_rect_valid = true;

		// 1. Floating Header Outer Border (1px)
		for (int y = hdr_y - 1; y < hdr_y + hdr_h + 1; y++)
		{
			for (int x = ox - 1; x < ox + osd_w + 1; x++)
			{
				if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT)
					pixel_buffer[y * CANVAS_WIDTH + x] = theme.border_out;
			}
		}

		// 2. Floating Header Background
		for (int y = hdr_y; y < hdr_y + hdr_h; y++)
		{
			for (int x = ox; x < ox + osd_w; x++)
			{
				if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT)
					pixel_buffer[y * CANVAS_WIDTH + x] = theme.header_bg;
			}
		}

		// 3. Floating Header Text (Left: MiSTer, Right: Date/Time like "Aug 30 Sun17:30:01")
		int txt_y = hdr_y + (hdr_h - 8) / 2;
		DrawString(ox + 6, txt_y, "MiSTer", theme.header_txt);

		time_t now = time(NULL);
		struct tm* tm_now = localtime(&now);
		if (tm_now)
		{
			static const char* MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
			static const char* DAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

			char date_str[64];
			snprintf(date_str, sizeof(date_str), "%s %02d %s%02d:%02d:%02d",
				MONTHS[tm_now->tm_mon], tm_now->tm_mday, DAYS[tm_now->tm_wday],
				tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
			DrawString(ox + osd_w - (int)strlen(date_str) * 8 - 6, txt_y, date_str, theme.header_txt);
		}

		// 4. Main OSD Card Outer Border (1px)
		for (int y = oy - 1; y < oy + osd_h + 1; y++)
		{
			for (int x = ox - 1; x < ox + osd_w + 1; x++)
			{
				if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT)
					pixel_buffer[y * CANVAS_WIDTH + x] = theme.border_out;
			}
		}

		// 5. Main OSD Card Backgrounds (Left Sidebar + Rows)
		for (int y = oy; y < oy + osd_h; y++)
		{
			int rel_y = y - oy;
			int row = rel_y / OSD_ROW_H;
			bool is_row_inverted = (row < 32 && invert_map[row] != 0);

			for (int x = ox; x < ox + osd_w; x++)
			{
				int rel_x = x - ox;
				if (rel_x < side_w)
				{
					pixel_buffer[y * CANVAS_WIDTH + x] = theme.header_bg;
				}
				else if (rel_x == side_w)
				{
					pixel_buffer[y * CANVAS_WIDTH + x] = theme.border_out;
				}
				else
				{
					pixel_buffer[y * CANVAS_WIDTH + x] = is_row_inverted ? theme.header_bg : theme.menu_bg;
				}
			}
		}

		// 6. Vertical Title in Left Sidebar (Authentic MiSTer FPGA: 'M' at bottom, 'r' at top)
		const char* title = MenuGetTitle();
		if (!title || !*title) title = "MiSTer";
		int tlen = (int)strlen(title);
		int th = tlen * 8;
		int title_start_y = oy + (osd_h - th) / 2;
		int title_x = ox + (side_w - 8) / 2;
		for (int i = 0; i < tlen; i++)
		{
			// i = 0 ('M') at the bottom, i = tlen - 1 ('r') at the top
			int char_y = title_start_y + (tlen - 1 - i) * 8;
			DrawRotatedCharTo(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, title_x, char_y, title[i], theme.header_txt);
		}

		// 7. Render Text Glyphs for Each Row
		for (int row = 0; row < osd_lines; row++)
		{
			int row_y = oy + row * OSD_ROW_H;
			int font_y = row_y + (OSD_ROW_H - 8) / 2;
			bool is_row_inverted = (row < 32 && invert_map[row] != 0);
			uint32_t text_col = is_row_inverted ? theme.menu_bg : theme.header_bg;
			int base_idx = row * 256;

			for (int c = 22; c < 256; c++)
			{
				int px = ox + side_w + 1 + (c - 22);
				if (px >= ox + side_w + 1 && px < ox + osd_w - 1)
				{
					uint8_t byte_val = osd[base_idx + c];
					for (int bit = 0; bit < 8; bit++)
					{
						bool is_glyph = is_row_inverted ? ((byte_val & (1 << bit)) == 0) : ((byte_val & (1 << bit)) != 0);
						if (is_glyph)
						{
							int py = font_y + bit;
							if (py >= 0 && py < CANVAS_HEIGHT)
							{
								pixel_buffer[py * CANVAS_WIDTH + px] = text_col;
							}
						}
					}
				}
			}
		}

		// Composite OSD directly into the offscreen present buffer if game is running.
		// This guarantees that WM_PAINT performs ONE single atomic BitBlt to the screen,
		// completely eliminating OSD menu flickering.
		if (g_use_present && h_present_dc && h_mem_dc && g_osd_rect_valid)
		{
			int sx = g_osd_rect.left;
			int sy = g_osd_rect.top;
			int sw = g_osd_rect.right - g_osd_rect.left;
			int sh = g_osd_rect.bottom - g_osd_rect.top;

			if (sw > 0 && sh > 0)
			{
				int dx = MulDiv(sx, present_w, CANVAS_WIDTH);
				int dy = MulDiv(sy, present_h, CANVAS_HEIGHT);
				int dw = MulDiv(sw, present_w, CANVAS_WIDTH);
				int dh = MulDiv(sh, present_h, CANVAS_HEIGHT);

				SetStretchBltMode(h_present_dc, COLORONCOLOR);
				StretchBlt(h_present_dc, dx, dy, dw, dh, h_mem_dc, sx, sy, sw, sh, SRCCOPY);
			}
		}
	}
}

// Mouse as the DS stylus. Only while the left button is held and the cursor
// is over the picture, so it cannot interfere with anything else - and never
// while the OSD is up, where the mouse belongs to the menu.
static void PollMouseStylus()
{
	if (!CoreIsRunning() || OsdIsEnabled()) { CoreSetPointer(0.0, 0.0, false); return; }
	if (GetForegroundWindow() != g_hwnd) { CoreSetPointer(0.0, 0.0, false); return; }

	POINT pt;
	if (!GetCursorPos(&pt) || !ScreenToClient(g_hwnd, &pt)) return;

	RECT client;
	if (!GetClientRect(g_hwnd, &client)) return;
	if (client.right <= 0 || client.bottom <= 0) return;

	const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	CoreSetPointer((double)pt.x / (double)client.right,
	               (double)pt.y / (double)client.bottom, down);
}

static WORD s_prev_pad_buttons = 0;
static int s_prev_dpad_dir = 0; // 0=none, 1=up, 2=down, 3=left, 4=right
static DWORD s_dpad_first_press_tick = 0;
static DWORD s_dpad_last_repeat_tick = 0;
static bool s_prev_osd_enabled = false;

static void PollGamepad()
{
	XINPUT_STATE state;
	ZeroMemory(&state, sizeof(XINPUT_STATE));

	if (XInputGetState(0, &state) == ERROR_SUCCESS)
	{
		WORD wButtons = state.Gamepad.wButtons;
		SHORT sThumbY = state.Gamepad.sThumbLY;
		SHORT sThumbX = state.Gamepad.sThumbLX;

		int deadzone_val = (MenuGetDeadzone() * 32767) / 100;
		DWORD now = GetTickCount();

		bool osd_now = OsdIsEnabled();
		if (osd_now != s_prev_osd_enabled)
		{
			s_prev_osd_enabled = osd_now;
			s_prev_pad_buttons = wButtons; // Acknowledge button state upon open/close transition
			s_prev_dpad_dir = 0;
		}

		WORD wPressed = wButtons & ~s_prev_pad_buttons;
		s_prev_pad_buttons = wButtons;

		if (CoreIsRunning() && !osd_now)
		{
			// Quick Save / Load via Gamepad: Select + R1 (Save), Select + L1 (Load)
			if ((wButtons & XINPUT_GAMEPAD_BACK) && (wPressed & XINPUT_GAMEPAD_RIGHT_SHOULDER))
			{
				CoreSaveState(CoreGetSelectedSlot());
			}
			else if ((wButtons & XINPUT_GAMEPAD_BACK) && (wPressed & XINPUT_GAMEPAD_LEFT_SHOULDER))
			{
				CoreLoadState(CoreGetSelectedSlot());
			}
			// Toggle OSD with Start+Select
			else if ((wButtons & XINPUT_GAMEPAD_START) && (wButtons & XINPUT_GAMEPAD_BACK))
			{
				if ((wPressed & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)) != 0)
				{
					MenuProcessKey(KEY_MENU_TOGGLE);
				}
			}
		}
		else
		{
			// Menu Navigation:
			// Action buttons (Edge-triggered on press down only, never repeating while held)
			if (wPressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X))
			{
				MenuProcessKey(KEY_SELECT);
			}
			else if (wPressed & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_Y))
			{
				MenuProcessKey(KEY_CANCEL);
			}
			else if (wPressed & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK))
			{
				MenuProcessKey(KEY_MENU_TOGGLE);
			}

			// Directional navigation (D-Pad and Left Analog Stick)
			int cur_dir = 0;
			if ((wButtons & XINPUT_GAMEPAD_DPAD_UP) || (sThumbY > deadzone_val)) cur_dir = 1;
			else if ((wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || (sThumbY < -deadzone_val)) cur_dir = 2;
			else if ((wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || (sThumbX < -deadzone_val)) cur_dir = 3;
			else if ((wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || (sThumbX > deadzone_val)) cur_dir = 4;

			if (cur_dir != 0)
			{
				bool fire = false;
				if (cur_dir != s_prev_dpad_dir)
				{
					fire = true;
					s_dpad_first_press_tick = now;
					s_dpad_last_repeat_tick = now;
				}
				else if (now - s_dpad_first_press_tick >= 300 && now - s_dpad_last_repeat_tick >= 110)
				{
					fire = true;
					s_dpad_last_repeat_tick = now;
				}

				if (fire)
				{
					if (cur_dir == 1) MenuProcessKey(KEY_UP);
					else if (cur_dir == 2) MenuProcessKey(KEY_DOWN);
					else if (cur_dir == 3) MenuProcessKey(KEY_LEFT);
					else if (cur_dir == 4) MenuProcessKey(KEY_RIGHT);
				}
			}
			s_prev_dpad_dir = cur_dir;
		}
	}
	else
	{
		s_prev_pad_buttons = 0;
		s_prev_dpad_dir = 0;
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
		{
			ToggleFullscreen(hwnd);
			return 0;
		}

		if (CoreIsRunning() && !OsdIsEnabled())
		{
			// F12 only. Esc and Tab used to open the OSD here too, which meant a
			// game never saw them: pressing Esc in Doom under DOSBox opened our
			// menu instead of the game's. They are ordinary keys during play.
			// Esc still closes the OSD once it is open - there the game is not
			// reading input anyway.
			if (wParam == VK_F12)
			{
				MenuProcessKey(KEY_MENU_TOGGLE);
			}
			else if (wParam == VK_F5 || wParam == VK_F2) // Quick Save State
			{
				CoreSaveState(CoreGetSelectedSlot());
			}
			else if (wParam == VK_F8 || wParam == VK_F4) // Quick Load State
			{
				CoreLoadState(CoreGetSelectedSlot());
			}
			else if (wParam == VK_F6) // Previous State Slot
			{
				int s = (CoreGetSelectedSlot() + 9) % 10;
				CoreSetSelectedSlot(s);
			}
			else if (wParam == VK_F7) // Next State Slot
			{
				int s = (CoreGetSelectedSlot() + 1) % 10;
				CoreSetSelectedSlot(s);
			}
			else if (wParam == VK_F9) // Quick Screenshot
			{
				CoreTakeScreenshot();
			}
		}
		else
		{
			switch (wParam)
			{
			case VK_UP: MenuProcessKey(KEY_UP); break;
			case VK_DOWN: MenuProcessKey(KEY_DOWN); break;
			case VK_LEFT: MenuProcessKey(KEY_LEFT); break;
			case VK_RIGHT: MenuProcessKey(KEY_RIGHT); break;
			case VK_PRIOR: MenuProcessKey(KEY_PAGEUP); break;
			case VK_NEXT: MenuProcessKey(KEY_PAGEDOWN); break;
			case VK_RETURN:
			case VK_SPACE:
			case 'Z':
			case 'X': MenuProcessKey(KEY_SELECT); break;
			case VK_ESCAPE:
			case VK_BACK: MenuProcessKey(KEY_CANCEL); break;
			case VK_F12:
			case VK_TAB: MenuProcessKey(KEY_MENU_TOGGLE); break;
			default: break;
			}
		}
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;

	case WM_KEYUP:
		// Nothing to do: the core thread samples the keyboard directly with
		// GetAsyncKeyState, so key-up needs no bookkeeping here.
		return 0;

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT client;
		GetClientRect(hwnd, &client);

		SetStretchBltMode(hdc, COLORONCOLOR);

		if (g_use_present && h_present_dc)
		{
			if (present_w == client.right && present_h == client.bottom)
				BitBlt(hdc, 0, 0, client.right, client.bottom, h_present_dc, 0, 0, SRCCOPY);
			else
				StretchBlt(hdc, 0, 0, client.right, client.bottom,
					h_present_dc, 0, 0, present_w, present_h, SRCCOPY);
		}
		else
		{
			StretchBlt(
				hdc, 0, 0, client.right, client.bottom,
				h_mem_dc, 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT,
				SRCCOPY
			);
		}

		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_ERASEBKGND:
		return 1;

	case WM_DESTROY:
		g_running = false;
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void PresentFrame(HWND hwnd)
{
	// A load that ended without a running core failed; bring the menu back so
	// the user is not left staring at the wallpaper.
	static bool was_busy = false;
	bool busy = CoreIsLoading() || CoreIsRunning();
	if (was_busy && !busy && !OsdIsEnabled()) OsdEnable();
	was_busy = busy;

	MenuRun();
	RenderFrame();

	InvalidateRect(hwnd, NULL, FALSE);
	UpdateWindow(hwnd);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hPrevInstance;


	SetUnhandledExceptionFilter(CrashHandler);

	// bios/, cores/, saves/, wallpapers/ and the log are all opened by relative
	// path, so the working directory has to be the executable's own folder.
	// Launched from a shortcut or another drive, none of them resolved.
	{
		char exe_path[MAX_PATH];
		DWORD n = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
		if (n > 0 && n < MAX_PATH)
		{
			char* slash = strrchr(exe_path, '\\');
			if (slash)
			{
				*slash = '\0';
				SetCurrentDirectoryA(exe_path);
			}
		}
	}

	// Headless port install: "MiSTer_4_ALL.exe --install-port <id>" downloads
	// and extracts a known port and exits, before any window is created, so
	// ports/ can be pre-populated (packaging, CI, or just getting ahead of a
	// slow download) without touching the menu UI at all.
	if (__argc > 2 && _stricmp(__argv[1], "--install-port") == 0)
	{
		PortInit();
		std::string install_error;
		bool ok = PortInstallOnly(__argv[2], install_error);
		FILE* lf = fopen("mister_flavor.log", "a");
		if (lf)
		{
			fprintf(lf, "[INFO] [PORT-INSTALL] %s -> %s%s%s\n", __argv[2],
				ok ? "OK" : "FALHOU", install_error.empty() ? "" : ": ", install_error.c_str());
			fclose(lf);
		}
		return ok ? 0 : 1;
	}

	// Headless end-to-end test: "MiSTer_4_ALL.exe --launch-port <id>" calls
	// PortLaunch() exactly the way the "Ports & Recomp" menu entry does -
	// download-if-needed, then launch - and pumps PortPumpPendingLaunch()
	// itself since there is no window/message loop running yet to do it.
	// Exists to exercise that exact call path (including the background
	// install thread and the pending-launch handoff) without needing the
	// menu UI at all, since driving the actual menu risks stealing input
	// focus from whatever else is running on the machine.
	if (__argc > 2 && _stricmp(__argv[1], "--launch-port") == 0)
	{
		PortInit();
		bool started = PortLaunch(__argv[2]);
		FILE* lf = fopen("mister_flavor.log", "a");

		DWORD waited_ms = 0;
		while (started && !PortIsRunning() && waited_ms < 180000)
		{
			PortPumpPendingLaunch();
			Sleep(200);
			waited_ms += 200;
		}

		if (lf)
		{
			fprintf(lf, "[INFO] [PORT-LAUNCH-TEST] %s -> PortLaunch()=%s running_after_wait=%s (%lums)\n",
				__argv[2], started ? "true" : "false", PortIsRunning() ? "true" : "false", waited_ms);
			fclose(lf);
		}
		return (started && PortIsRunning()) ? 0 : 1;
	}

	// Diagnostic-only: logs what PortGetAvailableList() actually resolved for
	// every known port (id, exe path, installed state) and exits. Exists so
	// the executable-picking logic can be checked against real downloaded
	// files without spawning a game or going through the menu.
	if (__argc > 1 && _stricmp(__argv[1], "--list-ports") == 0)
	{
		PortInit();
		FILE* lf = fopen("mister_flavor.log", "a");
		if (lf)
		{
			for (const auto& p : PortGetAvailableList())
			{
				fprintf(lf, "[INFO] [PORT-LIST] %s installed=%d exe=%s\n",
					p.id.c_str(), p.is_installed ? 1 : 0, p.exe_path.c_str());
			}
			fclose(lf);
		}
		return 0;
	}

	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
	wc.hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 16, 16, 0);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = "MiSTerFlavorWindowClass";

	if (!RegisterClassEx(&wc)) return 1;

	RECT wr = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindowEx(
		0,
		wc.lpszClassName,
		APP_NAME " v" APP_VERSION " " APP_ARCH " [mister4all.com | @GuhClemente]",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		wr.right - wr.left, wr.bottom - wr.top,
		NULL, NULL, hInstance, NULL
	);

	if (!hwnd) return 1;
	g_hwnd = hwnd;

	HDC hdc = GetDC(hwnd);
	InitBackbuffer(hdc);
	ReleaseDC(hwnd, hdc);

	MenuInit();

	// Probe OpenGL once, here on the main thread. Doing it lazily from a core
	// callback created a window on the core thread, which is a bad place for one.
	//
	// Then hand the context back: it can only be current on one thread, and the
	// core thread is the one that needs it. Holding it here made every GL call
	// from the core thread do nothing at all.
	HwIsAvailable();
	HwReleaseCurrent();

	RaInit();
	UpdaterInit();
	PortInit();

	// A path on the command line loads that game straight away. Useful for file
	// associations and drag-and-drop, and it removes menu navigation as a
	// variable when reproducing a core-specific crash.
	//
	// This used to run twice - once here and once right after MenuInit() - which
	// queued two loads and let the second tear down the first mid-flight. The
	// surviving copy is this one on purpose: it sits after the OpenGL probe has
	// released the context and after RaInit(), so a game opened from the command
	// line gets the same GL handover and achievement session as one opened from
	// the menu. __argv is used over lpCmdLine because the CRT has already parsed
	// the quoting.
	if (__argc > 1 && __argv[1] && __argv[1][0] != '\0')
	{
		std::string arg = __argv[1];
		if (fs::exists(arg))
		{
			if (MenuLaunchGamePath(arg)) OsdDisable();
		}
	}

	if (MenuGetFullscreen())
	{
		ToggleFullscreen(hwnd);
	}

	timeBeginPeriod(1);

	BOOL dwm_enabled = FALSE;
	DwmIsCompositionEnabled(&dwm_enabled);

	bool dwm_timing_ok = false;
	double display_fps = DetectDisplayRefreshRate(hwnd, &dwm_timing_ok);

	{
		// Written to the log so display cadence diagnosis is clear.
		FILE* lf = fopen("mister4all.log", "a");
		if (lf)
		{
			fprintf(lf, "[INFO] [DISPLAY] composicao DWM=%s taxa=%.3f Hz (fonte=%s)\n",
				dwm_enabled ? "ativa" : "INATIVA", display_fps,
				dwm_timing_ok ? "DwmGetCompositionTimingInfo" : "EnumDisplaySettings/GDI");
			fclose(lf);
		}
	}

	int last_sync_mode = MenuGetSyncMode();
	CoreSetDisplaySync(last_sync_mode == 1, display_fps);

	LARGE_INTEGER freq, curr_time;
	QueryPerformanceFrequency(&freq);

	double target_frame_time = 1.0 / display_fps;
	LONGLONG target_ticks = (LONGLONG)(target_frame_time * (double)freq.QuadPart);

	LARGE_INTEGER next_frame;
	QueryPerformanceCounter(&next_frame);
	next_frame.QuadPart += target_ticks;

	MSG msg;
	while (g_running)
	{
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				g_running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		PollGamepad();
		PollMouseStylus();
		PortPumpPendingLaunch();

		if (MenuGetFullscreen() != g_is_fullscreen)
		{
			ToggleFullscreen(hwnd);
		}

		if (MenuGetSyncMode() != last_sync_mode)
		{
			last_sync_mode = MenuGetSyncMode();
			CoreSetDisplaySync(last_sync_mode == 1, display_fps);
		}

		// Intelligent V-Sync and Frame Pacer
		bool vsync_active = (MenuGetVsync() != 0) && !IsIconic(hwnd);

		if (vsync_active && dwm_enabled)
		{
			PresentFrame(hwnd);
			DwmFlush();

			// Resynchronize baseline clock so transient hiccups never cause frame pacing stalls
			QueryPerformanceCounter(&curr_time);
			next_frame = curr_time;
			next_frame.QuadPart += target_ticks;
		}
		else
		{
			QueryPerformanceCounter(&curr_time);
			LONGLONG remaining = next_frame.QuadPart - curr_time.QuadPart;

			if (remaining > 0)
			{
				double ms = ((double)remaining * 1000.0) / (double)freq.QuadPart;
				if (ms > 2.0)
				{
					Sleep((DWORD)(ms - 1.5));
				}
				do
				{
					YieldProcessor();
					QueryPerformanceCounter(&curr_time);
				} while (curr_time.QuadPart < next_frame.QuadPart);
			}

			PresentFrame(hwnd);

			next_frame.QuadPart += target_ticks;
			QueryPerformanceCounter(&curr_time);

			// Prevent accumulator lag if a long hitch occurred (drag window, disc read, etc)
			if (curr_time.QuadPart > next_frame.QuadPart + target_ticks * 2)
			{
				next_frame = curr_time;
				next_frame.QuadPart += target_ticks;
			}
		}
	}

	timeEndPeriod(1);

	CoreShutdown();
	RaShutdown();
	UpdaterShutdown();

	if (h_mem_dc) DeleteDC(h_mem_dc);
	if (h_bitmap) DeleteObject(h_bitmap);
	if (h_present_dc) DeleteDC(h_present_dc);
	if (h_present_bitmap) DeleteObject(h_present_bitmap);

	return 0;
}

#include "compat_win32.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

#include "app_info.h"
#include "charrom.h"
#include "libretro.h"
#include "core_runner.h"
#include "menu.h"
#include "osd.h"
#include "gamepad_sdl.h"
#include "input_map.h"
#include "netplay.h"
#include "retroachievements.h"
#include "updater.h"
#include "port_runner.h"
#include "hw_render.h"
#include "hw_render_vulkan.h"
#include "hw_render_d3d11.h"
#include "karamelo_math.h"

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;
const int CANVAS_WIDTH = 640;
const int CANVAS_HEIGHT = 360;

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

static const ThemeColor THEMES[] = {
	{ 0xFFDFC4C1, 0xFF1E0808, 0xFF000000, 0xFF000000, 0xFFDFC4C1, 0xFF1E0808, 0xFF1E0808, 0xFF1E0808, 0xFFDFC4C1, 0xFF1E0808, 0xFFDFC4C1, 0xFFA09090 },
	{ 0xFFD0DEF4, 0xFF0E1428, 0xFF000000, 0xFF000000, 0xFFD0DEF4, 0xFF0E1428, 0xFF0E1428, 0xFF0E1428, 0xFFD0DEF4, 0xFF0E1428, 0xFFD0DEF4, 0xFF90A0B0 },
	{ 0xFFD0F4D8, 0xFF082010, 0xFF000000, 0xFF000000, 0xFFD0F4D8, 0xFF082010, 0xFF082010, 0xFF082010, 0xFFD0F4D8, 0xFF082010, 0xFFD0F4D8, 0xFF90B098 },
	{ 0xFFF8E8C8, 0xFF281808, 0xFF000000, 0xFF000000, 0xFFF8E8C8, 0xFF281808, 0xFF281808, 0xFF281808, 0xFFF8E8C8, 0xFF281808, 0xFFF8E8C8, 0xFFB8A890 },
	{ 0xFFE0E0E0, 0xFF181818, 0xFF000000, 0xFF000000, 0xFFE0E0E0, 0xFF181818, 0xFF181818, 0xFF181818, 0xFFE0E0E0, 0xFF181818, 0xFFE0E0E0, 0xFFA0A0A0 },
	{ 0xFFC8C8D4, 0xFF0E0E14, 0xFF000000, 0xFF000000, 0xFFC8C8D4, 0xFF0E0E14, 0xFF0E0E14, 0xFF0E0E14, 0xFFC8C8D4, 0xFF0E0E14, 0xFFC8C8D4, 0xFF9090A0 }
};

static uint32_t* pixel_buffer = nullptr;
static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static SDL_Texture* g_texture = nullptr;

SDL_Window* MainGetSdlWindow()
{
	return g_window;
}

#define PRESENT_MAX_W 1920
#define PRESENT_MAX_H 1200

static uint32_t* present_buffer = nullptr;
static SDL_Texture* g_present_texture = nullptr;
static int present_w = 0;
static int present_h = 0;
static bool g_use_present = false;

static bool EnsurePresentBuffer(int w, int h)
{
	if (w < 16 || h < 16) return false;
	if (w > PRESENT_MAX_W) w = PRESENT_MAX_W;
	if (h > PRESENT_MAX_H) h = PRESENT_MAX_H;

	if (present_buffer && g_present_texture && w == present_w && h == present_h) return true;

	if (g_present_texture) { SDL_DestroyTexture(g_present_texture); g_present_texture = nullptr; }
	delete[] present_buffer;
	present_buffer = nullptr;

	present_buffer = new (std::nothrow) uint32_t[w * h];
	if (!present_buffer) return false;

	g_present_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!g_present_texture)
	{
		delete[] present_buffer;
		present_buffer = nullptr;
		return false;
	}
	SDL_SetTextureBlendMode(g_present_texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(g_present_texture, SDL_SCALEMODE_LINEAR);
	present_w = w;
	present_h = h;
	return true;
}

static void DrawCharTo(uint32_t* buf, int bw, int bh, int x, int y, char c, uint32_t color)
{
	if (!buf) return;
	color |= 0xFF000000;
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
	color |= 0xFF000000;
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

static void DrawCharToScaled(uint32_t* buf, int bw, int bh, int x, int y, char c, uint32_t color, int scale)
{
	if (!buf || scale < 1) return;
	color |= 0xFF000000;
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

static void DrawStringTo(uint32_t* buf, int bw, int bh, int x, int y, const char* str, uint32_t color)
{
	while (*str)
	{
		DrawCharTo(buf, bw, bh, x, y, *str++, color);
		x += 8;
	}
}

static void DrawString(int x, int y, const char* str, uint32_t color)
{
	DrawStringTo(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, x, y, str, color);
}

static void DrawToast(uint32_t* buf, int bw, int bh, const ThemeColor& theme)
{
	const char* msg = CoreGetToast();
	int msg_len = (int)strlen(msg);
	if (msg_len <= 0) return;

	int scale = bh / 360;
	if (scale < 1) scale = 1;
	else if (scale > 6) scale = 6;

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

static void RenderFrame()
{
	if (!pixel_buffer) return;

	int theme_idx = MenuGetOsdTheme();
	if (theme_idx < 0 || theme_idx > 5) theme_idx = 0;
	const ThemeColor& theme = THEMES[theme_idx];

	static uint32_t noise_seed = 0x12345678;
	auto FastRand = [&]() -> uint32_t {
		noise_seed ^= noise_seed << 13;
		noise_seed ^= noise_seed >> 17;
		noise_seed ^= noise_seed << 5;
		return noise_seed;
	};

	g_use_present = false;
	if (CoreIsRunning() && !CoreIsLoading() && !OsdIsEnabled())
	{
		int win_w = 0, win_h = 0;
		SDL_GetWindowSizeInPixels(g_window, &win_w, &win_h);
		if (EnsurePresentBuffer(win_w, win_h))
		{
			CoreRender(present_buffer, present_w, present_h, MenuGetAspectMode(), MenuGetFilterMode());
			for (int i = 0; i < present_w * present_h; i++)
			{
				present_buffer[i] |= 0xFF000000;
			}
			g_use_present = true;
			if (CoreIsToastActive())
			{
				DrawToast(present_buffer, present_w, present_h, theme);
			}
			return;
		}
	}

	if (CoreIsLoading())
	{
		for (int y = 0; y < CANVAS_HEIGHT; y++)
			for (int x = 0; x < CANVAS_WIDTH; x++)
				pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF020308;

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
		CoreRender(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, MenuGetAspectMode(), MenuGetFilterMode());
		for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
		{
			pixel_buffer[i] |= 0xFF000000;
		}
	}
	else
	{
		int wall_mode = MenuGetWallpaperMode();

		static uint32_t custom_wall[CANVAS_WIDTH * CANVAS_HEIGHT] = { 0 };
		static int loaded_custom_mode = -1;

		if (wall_mode == 1) // TV Static Noise
		{
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				uint32_t r = FastRand();
				uint8_t grain = (r & 0xFF) > 130 ? ((r >> 8) & 0xC0) : ((r >> 16) & 0x30);
				pixel_buffer[i] = 0xFF000000 | (grain << 16) | (grain << 8) | grain;
			}
		}
		else if (wall_mode == 2) // 128 Parallax Stars
		{
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				pixel_buffer[i] = 0xFF040710;
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
					pixel_buffer[sy * CANVAS_WIDTH + sx] = 0xFF000000 | col;
					if (stars[i].speed >= 3 && sx + 1 < CANVAS_WIDTH)
					{
						pixel_buffer[sy * CANVAS_WIDTH + sx + 1] = 0xFF000000 | col;
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
						pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF006080;
					}
					else
					{
						pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF080814;
					}
				}
			}
		}
		else if (wall_mode >= 4) // Custom .raw wallpapers
		{
			if (wall_mode != loaded_custom_mode)
			{
				memset(custom_wall, 0, sizeof(custom_wall));
				const char* wall_path = MenuGetWallpaperCustomPath(wall_mode - 4);
				if (wall_path && wall_path[0])
				{
					FILE* fw = fopen(wall_path, "rb");
					if (fw)
					{
						fread(custom_wall, sizeof(uint32_t), CANVAS_WIDTH * CANVAS_HEIGHT, fw);
						fclose(fw);
					}
				}
				loaded_custom_mode = wall_mode;
			}
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				pixel_buffer[i] = 0xFF000000 | custom_wall[i];
			}
		}
		else // None (Pure Deep Black)
		{
			for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++)
			{
				pixel_buffer[i] = 0xFF000000;
			}
		}
	}

	if (CoreIsToastActive())
	{
		DrawToast(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, theme);
	}

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

		// Floating Header Outer Border (1px)
		for (int y = hdr_y - 1; y < hdr_y + hdr_h + 1; y++)
		{
			for (int x = ox - 1; x < ox + osd_w + 1; x++)
			{
				if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT)
					pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF000000 | theme.border_out;
			}
		}

		// Floating Header Background
		for (int y = hdr_y; y < hdr_y + hdr_h; y++)
		{
			for (int x = ox; x < ox + osd_w; x++)
			{
				if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT)
					pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF000000 | theme.header_bg;
			}
		}

		// Floating Header Text (Left: Karamelo, Right: Date/Time)
		int txt_y = hdr_y + (hdr_h - 8) / 2;
		DrawString(ox + 6, txt_y, APP_NAME, theme.header_txt);

		time_t now = time(NULL);
		struct tm* tm_now = localtime(&now);
		if (tm_now)
		{
			static const char* MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

			char date_str[64];
			snprintf(date_str, sizeof(date_str), "%s %02d %02d:%02d",
				MONTHS[tm_now->tm_mon], tm_now->tm_mday,
				tm_now->tm_hour, tm_now->tm_min);
			int date_w = (int)strlen(date_str) * 8;
			int date_x = ox + osd_w - date_w - 6;
			int title_w = (int)strlen(APP_NAME) * 8;
			if (date_x > ox + 6 + title_w + 4)
			{
				DrawString(date_x, txt_y, date_str, theme.header_txt);
			}
		}

		// Main OSD Card Outer Border (1px)
		for (int y = oy - 1; y < oy + osd_h + 1; y++)
		{
			for (int x = ox - 1; x < ox + osd_w + 1; x++)
			{
				if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT)
					pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF000000 | theme.border_out;
			}
		}

		// Main OSD Card Backgrounds (Left Sidebar + Rows)
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
					pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF000000 | theme.header_bg;
				}
				else if (rel_x == side_w)
				{
					pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF000000 | theme.border_out;
				}
				else
				{
					pixel_buffer[y * CANVAS_WIDTH + x] = 0xFF000000 | (is_row_inverted ? theme.header_bg : theme.menu_bg);
				}
			}
		}

		// Vertical Title in Left Sidebar
		const char* title = MenuGetTitle();
		if (!title || !*title) title = APP_NAME;
		int tlen = (int)strlen(title);
		int th = tlen * 8;
		int title_start_y = oy + (osd_h - th) / 2;
		int title_x = ox + (side_w - 8) / 2;
		for (int i = 0; i < tlen; i++)
		{
			int char_y = title_start_y + (tlen - 1 - i) * 8;
			DrawRotatedCharTo(pixel_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, title_x, char_y, title[i], theme.header_txt);
		}

		// Render Text Glyphs for Each Row
		for (int row = 0; row < osd_lines; row++)
		{
			int row_y = oy + row * OSD_ROW_H;
			int font_y = row_y + (OSD_ROW_H - 8) / 2;
			bool is_row_inverted = (row < 32 && invert_map[row] != 0);
			uint32_t text_col = 0xFF000000 | (is_row_inverted ? theme.menu_bg : theme.header_bg);
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
	}
}

static void PollMouseStylus()
{
	if (!CoreIsRunning() || OsdIsEnabled())
	{
		CoreSetPointer(0.0, 0.0, false);
		SDL_ShowCursor();
		return;
	}

	SDL_HideCursor();

	float mx = 0.0f, my = 0.0f;
	SDL_MouseButtonFlags btn = SDL_GetMouseState(&mx, &my);

	int win_w = 0, win_h = 0;
	SDL_GetWindowSizeInPixels(g_window, &win_w, &win_h);
	if (win_w <= 0 || win_h <= 0) return;

	bool down = (btn & SDL_BUTTON_LMASK) != 0;
	CoreSetPointer((double)mx / (double)win_w, (double)my / (double)win_h, down);
}

static WORD s_prev_pad_buttons = 0;
static int s_prev_dpad_dir = 0;
static DWORD s_dpad_first_press_tick = 0;
static DWORD s_dpad_last_repeat_tick = 0;
static bool s_prev_osd_enabled = false;

static void PollGamepad()
{
	XINPUT_STATE state;
	memset(&state, 0, sizeof(XINPUT_STATE));

	if (GamepadGetState(0, &state))
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
			s_prev_pad_buttons = wButtons;
			s_prev_dpad_dir = 0;
		}

		WORD wPressed = wButtons & ~s_prev_pad_buttons;
		s_prev_pad_buttons = wButtons;

		if (CoreIsRunning() && !osd_now)
		{
			if ((wButtons & XINPUT_GAMEPAD_BACK) && (wPressed & XINPUT_GAMEPAD_RIGHT_SHOULDER))
			{
				CoreSaveState(CoreGetSelectedSlot());
			}
			else if ((wButtons & XINPUT_GAMEPAD_BACK) && (wPressed & XINPUT_GAMEPAD_LEFT_SHOULDER))
			{
				CoreLoadState(CoreGetSelectedSlot());
			}
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

int main(int argc, char* argv[])
{
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	// bios/, cores/, saves/, Wallpapers/, Config/, roms/ and the log are all opened
	// by relative path, so the working directory has to be the executable's own folder
	// (matching Win32 main_win32.cpp lines 1362-1377).
	{
		char exe_path[PATH_MAX] = { 0 };
		DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path) - 1);
		if (len > 0)
		{
			exe_path[len] = '\0';
			char* slash = strrchr(exe_path, '/');
			if (slash)
			{
				*slash = '\0';
				chdir(exe_path);
			}
		}
	}

	// Fallback: If launched from the root repo and 'roms' is in 'app/roms'
	if (!fs::exists("roms") && fs::exists("app/roms"))
	{
		chdir("app");
	}

	// -------------------------------------------------------------
	// 0. CLI Help & Version
	// -------------------------------------------------------------
	if (argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0))
	{
		printf("%s v%s (%s)\n", APP_NAME_FULL, APP_VERSION, APP_SITE);
		return 0;
	}

	if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
	{
		printf("Uso: %s [OPCOES]\n", argv[0]);
		printf("  --version, -v                 Exibe a versao\n");
		printf("  --help, -h                    Exibe esta mensagem de ajuda\n");
		printf("  --core-selftest <core> <rom>  Executa teste de carregamento e encerramento de core\n");
		printf("  --core-stress <c> <r> [n]     Executa stress test de N ciclos (abrir/carregar/fechar)\n");
		printf("  --install-port <id>           Instala um port sem abrir a interface grafica\n");
		return 0;
	}

	// -------------------------------------------------------------
	// 1. Headless Core Self-Test Mode (--core-selftest)
	// -------------------------------------------------------------
	if (argc > 3 && strcmp(argv[1], "--core-selftest") == 0)
	{
		FILE* lf = fopen("karamelo.log", "a");
		auto log = [&](const char* fmt, ...) {
			va_list ap;
			va_start(ap, fmt);
			printf("[INFO] [CORE-SELFTEST] ");
			vprintf(fmt, ap);
			printf("\n");
			va_end(ap);

			if (lf)
			{
				va_start(ap, fmt);
				fprintf(lf, "[INFO] [CORE-SELFTEST] ");
				vfprintf(lf, fmt, ap);
				fprintf(lf, "\n");
				va_end(ap);
				fflush(lf);
			}
		};

		for (int i = 1; i + 1 < argc; i++)
		{
			if (strcasecmp(argv[i], "--driver") == 0)
			{
				int drv = atoi(argv[i + 1]);
				MenuSetVideoDriverForSelftest(drv);
				log("driver forcado para %d", drv);
				break;
			}
		}

		DWORD t0 = GetTickCount();
		bool started = CoreRequestLoad(argv[3], argv[2]);

		DWORD waited_ms = 0;
		while (started && (CoreIsLoading() || !CoreIsRunning()) && waited_ms < 10000)
		{
			Sleep(50);
			waited_ms += 50;
		}
		bool running = CoreIsRunning();
		log("load: dll=%s rom=%s started=%d running=%d load_ms=%u",
			argv[2], argv[3], started ? 1 : 0, running ? 1 : 0, GetTickCount() - t0);

		if (running)
		{
			Sleep(300);
		}

		DWORD shutdown_t0 = GetTickCount();
		CoreShutdown();
		DWORD shutdown_ms = GetTickCount() - shutdown_t0;
		log("shutdown: ms=%u running_after=%d loading_after=%d",
			shutdown_ms, CoreIsRunning() ? 1 : 0, CoreIsLoading() ? 1 : 0);

		auto toast_done = std::make_shared<std::atomic<bool>>(false);
		std::thread([toast_done]() {
			CoreSetToast("core-selftest", 1);
			CoreGetToast();
			CoreIsToastActive();
			toast_done->store(true);
		}).detach();
		DWORD toast_wait = 0;
		while (!toast_done->load() && toast_wait < 2000) { Sleep(20); toast_wait += 20; }
		log("post-recovery lock check: toast_ok=%d (%ums)", toast_done->load() ? 1 : 0, toast_wait);

		bool retry_started = false, retry_running = false;
		if (argc > 5)
		{
			DWORD t1 = GetTickCount();
			retry_started = CoreRequestLoad(argv[5], argv[4]);
			DWORD waited2 = 0;
			while (retry_started && (CoreIsLoading() || !CoreIsRunning()) && waited2 < 10000)
			{
				Sleep(50);
				waited2 += 50;
			}
			retry_running = CoreIsRunning();
			log("recovery-check load: dll=%s rom=%s started=%d running=%d load_ms=%u",
				argv[4], argv[5], retry_started ? 1 : 0, retry_running ? 1 : 0, GetTickCount() - t1);
			if (retry_running)
			{
				DWORD t2 = GetTickCount();
				CoreShutdown();
				log("recovery-check shutdown: ms=%u", GetTickCount() - t2);
			}
		}

		bool pass = started && running && (argc <= 5 || (retry_started && retry_running));
		log("RESULT=%s", pass ? "PASS" : "FAIL");
		if (lf) fclose(lf);
		return pass ? 0 : 1;
	}

	// -------------------------------------------------------------
	// 2. Headless Core Stress Mode (--core-stress)
	// -------------------------------------------------------------
	if (argc > 3 && strcmp(argv[1], "--core-stress") == 0)
	{
		const char* core_path = argv[2];
		const char* rom_path = argv[3];
		int iterations = (argc > 4) ? atoi(argv[4]) : 20;
		if (iterations <= 0) iterations = 20;

		printf("[STRESS] Iniciando stress test de %d ciclos para o core: %s (ROM: %s)\n",
			iterations, core_path, rom_path);
		int passed = 0;
		DWORD t_total = GetTickCount();

		for (int i = 1; i <= iterations; i++)
		{
			DWORD t0 = GetTickCount();
			bool started = CoreRequestLoad(rom_path, core_path);
			DWORD waited_ms = 0;
			while (started && (CoreIsLoading() || !CoreIsRunning()) && waited_ms < 5000)
			{
				Sleep(10);
				waited_ms += 10;
			}
			bool running = CoreIsRunning();
			if (running)
			{
				Sleep(40);
			}
			DWORD shutdown_t0 = GetTickCount();
			CoreShutdown();
			DWORD shutdown_ms = GetTickCount() - shutdown_t0;
			DWORD cycle_ms = GetTickCount() - t0;

			if (started && running)
			{
				passed++;
				printf("  [Ciclo %2d/%d] OK: load=%ums shutdown=%ums total=%ums\n",
					i, iterations, waited_ms, shutdown_ms, cycle_ms);
			}
			else
			{
				printf("  [Ciclo %2d/%d] FALHA: started=%d running=%d waited=%ums\n",
					i, iterations, started ? 1 : 0, running ? 1 : 0, waited_ms);
				break;
			}
		}
		DWORD elapsed = GetTickCount() - t_total;
		printf("[STRESS] Concluido: %d/%d ciclos com sucesso em %.2fs (media: %.1fms/ciclo)\n",
			passed, iterations, elapsed / 1000.0, elapsed / (double)iterations);
		return (passed == iterations) ? 0 : 1;
	}

	if (argc > 1 && strcasecmp(argv[1], "--list-ports") == 0)
	{
		PortInit();
		auto list = PortGetAvailableList();
		printf("[PORTS] Total: %zu\n", list.size());
		for (const auto& p : list) {
			printf("  - [%s] %s (installed=%d, exe=%s)\n",
				p.id.c_str(), p.name.c_str(), p.is_installed ? 1 : 0, p.exe_path.c_str());
		}
		PortShutdown();
		return 0;
	}

	// -------------------------------------------------------------
	// 2. Headless Port Installer Mode
	// -------------------------------------------------------------
	if (argc > 2 && strcasecmp(argv[1], "--install-port") == 0)
	{
		PortInit();
		const char* port_id = argv[2];
		printf("[INFO] [PORT-INSTALL] Baixando e instalando port: %s ...\n", port_id);
		std::string err;
		bool ok = PortInstallOnly(port_id, err);
		printf("[INFO] [PORT-INSTALL] Resultado: %s %s\n", ok ? "SUCESSO" : "FALHA", err.c_str());
		PortShutdown();
		return ok ? 0 : 1;
	}

	// -------------------------------------------------------------
	// 3. SDL3 Initialization & Window Creation
	// -------------------------------------------------------------
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
	{
		fprintf(stderr, "[ERROR] [SDL3] SDL_Init falhou: %s\n", SDL_GetError());
		return 1;
	}

	g_window = SDL_CreateWindow(
		APP_NAME_FULL " v" APP_VERSION " " APP_ARCH " [" APP_SITE " | @GuhClemente]",
		WINDOW_WIDTH, WINDOW_HEIGHT,
		SDL_WINDOW_RESIZABLE);
	if (!g_window)
	{
		fprintf(stderr, "[ERROR] [SDL3] SDL_CreateWindow falhou: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	g_renderer = SDL_CreateRenderer(g_window, NULL);
	if (!g_renderer)
	{
		fprintf(stderr, "[ERROR] [SDL3] SDL_CreateRenderer falhou: %s\n", SDL_GetError());
		SDL_DestroyWindow(g_window);
		SDL_Quit();
		return 1;
	}

	SDL_SetRenderLogicalPresentation(g_renderer, CANVAS_WIDTH, CANVAS_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, CANVAS_WIDTH, CANVAS_HEIGHT);
	if (!g_texture)
	{
		fprintf(stderr, "[ERROR] [SDL3] SDL_CreateTexture falhou: %s\n", SDL_GetError());
		SDL_DestroyRenderer(g_renderer);
		SDL_DestroyWindow(g_window);
		SDL_Quit();
		return 1;
	}
	SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);

	pixel_buffer = new uint32_t[CANVAS_WIDTH * CANVAS_HEIGHT];
	memset(pixel_buffer, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint32_t));

	MenuInit();

	double display_fps = 60.0;
	SDL_DisplayID display_id = SDL_GetDisplayForWindow(g_window);
	const SDL_DisplayMode* disp_mode = SDL_GetCurrentDisplayMode(display_id);
	if (disp_mode && disp_mode->refresh_rate > 20.0f)
	{
		display_fps = disp_mode->refresh_rate;
	}

	int last_sync_mode = MenuGetSyncMode();
	CoreSetDisplaySync(last_sync_mode == 1, display_fps);

	int last_vsync = MenuGetVsync();
	SDL_SetRenderVSync(g_renderer, last_vsync ? 1 : 0);

	HwIsAvailable();
	HwReleaseCurrent();
	VkHwIsAvailable();

	RaInit();
	UpdaterInit();
	PortInit();

	if (argc > 1 && argv[1] && argv[1][0] != '\0' && argv[1][0] != '-')
	{
		std::string arg = argv[1];
		if (arg.rfind("app/", 0) == 0 && fs::exists(arg.substr(4)))
		{
			arg = arg.substr(4);
		}
		if (fs::exists(arg))
		{
			if (MenuLaunchGamePath(arg)) OsdDisable();
		}
	}

	// -------------------------------------------------------------
	// 4. Main Event & Rendering Loop
	// -------------------------------------------------------------
	bool running = true;
	static bool was_busy = false;

	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			GamepadHandleDeviceEvent(&event);

			if (event.type == SDL_EVENT_QUIT)
			{
				running = false;
				break;
			}
			else if (event.type == SDL_EVENT_KEY_DOWN)
			{
				SDL_Keycode key = event.key.key;
				SDL_Scancode sc = event.key.scancode;

				if (sc == SDL_SCANCODE_F12)
				{
					MenuProcessKey(KEY_MENU_TOGGLE);
				}
				else if (sc == SDL_SCANCODE_F11)
				{
					bool is_full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
					SDL_SetWindowFullscreen(g_window, !is_full);
				}
				else if (CoreIsRunning() && !OsdIsEnabled())
				{
					if (sc == SDL_SCANCODE_F5 || sc == SDL_SCANCODE_F2)
					{
						CoreSaveState(CoreGetSelectedSlot());
					}
					else if (sc == SDL_SCANCODE_F8 || sc == SDL_SCANCODE_F4)
					{
						CoreLoadState(CoreGetSelectedSlot());
					}
					else if (sc == SDL_SCANCODE_F6)
					{
						int s = (CoreGetSelectedSlot() + 9) % 10;
						CoreSetSelectedSlot(s);
					}
					else if (sc == SDL_SCANCODE_F7)
					{
						int s = (CoreGetSelectedSlot() + 1) % 10;
						CoreSetSelectedSlot(s);
					}
					else if (sc == SDL_SCANCODE_F9)
					{
						CoreTakeScreenshot();
					}
				}
				else if (OsdIsEnabled())
				{
					switch (sc)
					{
					case SDL_SCANCODE_UP:    MenuProcessKey(KEY_UP); break;
					case SDL_SCANCODE_DOWN:  MenuProcessKey(KEY_DOWN); break;
					case SDL_SCANCODE_LEFT:  MenuProcessKey(KEY_LEFT); break;
					case SDL_SCANCODE_RIGHT: MenuProcessKey(KEY_RIGHT); break;
					case SDL_SCANCODE_PAGEUP: MenuProcessKey(KEY_PAGEUP); break;
					case SDL_SCANCODE_PAGEDOWN: MenuProcessKey(KEY_PAGEDOWN); break;
					case SDL_SCANCODE_HOME:  MenuProcessKey(KEY_HOME); break;
					case SDL_SCANCODE_END:   MenuProcessKey(KEY_END); break;
					case SDL_SCANCODE_RETURN:
					case SDL_SCANCODE_SPACE:
					case SDL_SCANCODE_Z:
					case SDL_SCANCODE_X:     MenuProcessKey(KEY_SELECT); break;
					case SDL_SCANCODE_ESCAPE:
					case SDL_SCANCODE_BACKSPACE: MenuProcessKey(KEY_CANCEL); break;
					case SDL_SCANCODE_TAB:   MenuProcessKey(KEY_MENU_TOGGLE); break;
					default: break;
					}
				}
			}
		}

		PollGamepad();
		PollMouseStylus();

		if (MenuGetSyncMode() != last_sync_mode)
		{
			last_sync_mode = MenuGetSyncMode();
			CoreSetDisplaySync(last_sync_mode == 1, display_fps);
		}

		if (MenuGetVsync() != last_vsync)
		{
			last_vsync = MenuGetVsync();
			SDL_SetRenderVSync(g_renderer, last_vsync ? 1 : 0);
		}

		bool busy = CoreIsLoading() || CoreIsRunning();
		if (was_busy && !busy && !OsdIsEnabled()) OsdEnable();
		was_busy = busy;

		MenuRun();
		if (MenuIsQuitRequested())
		{
			running = false;
			break;
		}
		RenderFrame();

		if (g_use_present && g_present_texture && present_buffer)
		{
			// present_w/present_h are the *buffer's* dimensions, capped at
			// PRESENT_MAX_W/H - on a window bigger than the cap (a maximized
			// window on a 1440p/4K display) they no longer match the real
			// window size. Stretching dst to the actual window here, instead
			// of reusing present_w/present_h, is what makes that still fill
			// the window instead of only covering a PRESENT_MAX_W x
			// PRESENT_MAX_H rectangle in the corner with black past it.
			int draw_w = 0, draw_h = 0;
			SDL_GetWindowSizeInPixels(g_window, &draw_w, &draw_h);
			if (draw_w <= 0 || draw_h <= 0) { draw_w = present_w; draw_h = present_h; }

			SDL_SetRenderLogicalPresentation(g_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
			SDL_UpdateTexture(g_present_texture, NULL, present_buffer, present_w * sizeof(uint32_t));
			SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
			SDL_RenderClear(g_renderer);
			SDL_FRect dst = { 0.0f, 0.0f, (float)draw_w, (float)draw_h };
			SDL_RenderTexture(g_renderer, g_present_texture, NULL, &dst);
			SDL_RenderPresent(g_renderer);
		}
		else
		{
			SDL_SetRenderLogicalPresentation(g_renderer, CANVAS_WIDTH, CANVAS_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
			SDL_UpdateTexture(g_texture, NULL, pixel_buffer, CANVAS_WIDTH * sizeof(uint32_t));
			SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
			SDL_RenderClear(g_renderer);
			SDL_FRect dst = { 0.0f, 0.0f, (float)CANVAS_WIDTH, (float)CANVAS_HEIGHT };
			SDL_RenderTexture(g_renderer, g_texture, NULL, &dst);
			SDL_RenderPresent(g_renderer);
		}

		if (!last_vsync)
		{
			SDL_Delay(1);
		}
	}

	// -------------------------------------------------------------
	// 5. Clean Teardown
	// -------------------------------------------------------------
	GamepadShutdown();
	PortShutdown();
	CoreShutdown();
	RaShutdown();
	UpdaterShutdown();
	HwShutdown();
	VkHwShutdown();

	if (present_buffer) { delete[] present_buffer; present_buffer = nullptr; }
	if (g_present_texture) { SDL_DestroyTexture(g_present_texture); g_present_texture = nullptr; }
	if (pixel_buffer) { delete[] pixel_buffer; pixel_buffer = nullptr; }
	if (g_texture) { SDL_DestroyTexture(g_texture); g_texture = nullptr; }
	if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
	if (g_window) { SDL_DestroyWindow(g_window); g_window = nullptr; }

	SDL_Quit();
	return 0;
}

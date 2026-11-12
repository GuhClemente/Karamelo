/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/host/ui/menu.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <optional>
#include <utility>

#include <cstdio>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
/* The item under a click, Dear ImGui's own code */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "imgui/imgui_internal.h"
#pragma GCC diagnostic pop
#include "odin/host/input/control_panel.hpp"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/video/picture.hpp"
#include "odin/host/ui/system_fonts.hpp"
#include "odin/host/app/version.hpp"

/* The pictures of the page Display (menu_pictures.S), each from its first
 * byte to the one after its last */
#define PICTURE(name) extern "C" const unsigned char odin_picture_##name[], odin_picture_##name##_end[];
PICTURE(upscale_1) PICTURE(upscale_2) PICTURE(upscale_3) PICTURE(upscale_4) PICTURE(upscale_5) PICTURE(upscale_6)
PICTURE(filter_1) PICTURE(filter_2) PICTURE(filter_4) PICTURE(filter_8) PICTURE(filter_16) PICTURE(screen)
PICTURE(whole) PICTURE(torn) PICTURE(mode_cabinet) PICTURE(mode_raw) PICTURE(mode_snap) PICTURE(mode_native)
PICTURE(crt_none) PICTURE(crt_sega_monitor_29) PICTURE(crt_sega_projector_50) PICTURE(crt_gaelco_monitor_28)
PICTURE(crt_gaelco_monitor_34)
#undef PICTURE
/* The font of the menu (menu_fonts.S) */
extern "C" const unsigned char odin_font_cozette[], odin_font_cozette_end[];

namespace odin {

namespace {

/* What a page says when its settings are not written to odin.ini */
constexpr const char *NOT_SAVED = "Not saved (ODIN_CONFIG=none).";

/* Their files, in the order of Menu::Picture */
const std::pair<const unsigned char *, const unsigned char *> PICTURE_FILES[] = {
	{ odin_picture_upscale_1, odin_picture_upscale_1_end }, { odin_picture_upscale_2, odin_picture_upscale_2_end },
	{ odin_picture_upscale_3, odin_picture_upscale_3_end }, { odin_picture_upscale_4, odin_picture_upscale_4_end },
	{ odin_picture_upscale_5, odin_picture_upscale_5_end }, { odin_picture_upscale_6, odin_picture_upscale_6_end },
	{ odin_picture_filter_1, odin_picture_filter_1_end }, { odin_picture_filter_2, odin_picture_filter_2_end },
	{ odin_picture_filter_4, odin_picture_filter_4_end }, { odin_picture_filter_8, odin_picture_filter_8_end },
	{ odin_picture_filter_16, odin_picture_filter_16_end }, { odin_picture_screen, odin_picture_screen_end },
	{ odin_picture_whole, odin_picture_whole_end }, { odin_picture_torn, odin_picture_torn_end },
	{ odin_picture_mode_cabinet, odin_picture_mode_cabinet_end }, { odin_picture_mode_raw, odin_picture_mode_raw_end },
	{ odin_picture_mode_snap, odin_picture_mode_snap_end }, { odin_picture_mode_native, odin_picture_mode_native_end },
	{ odin_picture_crt_none, odin_picture_crt_none_end },
	{ odin_picture_crt_sega_monitor_29, odin_picture_crt_sega_monitor_29_end },
	{ odin_picture_crt_sega_projector_50, odin_picture_crt_sega_projector_50_end },
	{ odin_picture_crt_gaelco_monitor_28, odin_picture_crt_gaelco_monitor_28_end },
	{ odin_picture_crt_gaelco_monitor_34, odin_picture_crt_gaelco_monitor_34_end },
};

/* The hints of the mouse Dear ImGui sets for itself (imgui_impl_sdl3.cpp):
 * a click that brings the window to the front is a click, and no capture
 * while a button is held. Odin keeps its own outside the menu. */
constexpr std::array<std::pair<const char *, const char *>, 2> MENU_HINTS = { {
	{ SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" },
	{ SDL_HINT_MOUSE_AUTO_CAPTURE, "0" },
} };

/* The menu is made for 640 by 480 points (a window nearly square) or 960 by
 * 540 (16:9 and wider), then larger by one factor as the window grows. It
 * takes that share of the window, the game around it, at the least the size it
 * is made for where the window has room for it. The cut of its corners and its
 * padding, in points. */
constexpr float COMPACT_W = 640.0f;
constexpr float COMPACT_H = 480.0f;
constexpr float LARGE_W = 960.0f;
constexpr float LARGE_H = 540.0f;
constexpr float SHARE = 0.88f;
constexpr float CUT = 8.0f;
constexpr float PADDING = 10.0f;
/* The settings of the page Display, that wide at the least in points, show
 * the sizes of the window whole, else their widths */
constexpr float SIZES_WHOLE = 560.0f;
/* The line under the tabs, of the accent of the palette: what it leaves
 * at each end of the width (it spans the row of the games under it), how
 * much of it fades in and out at each end, its opacity; how far under the
 * tabs it is, and the room under it, in points */
constexpr float LINE_ENDS = 0.04f;
constexpr float LINE_FADE = 0.08f;
constexpr float LINE_ALPHA = 0.85f;
constexpr float LINE_UNDER = 2.0f;
constexpr float LINE_ROOM = 9.0f;

/* The OpenGL Odin needs: 4.3, 4.1 on macOS (the translucent fragments
 * peeled there) */
#if defined(__APPLE__)
constexpr const char *NEEDS_OPENGL = "Odin needs OpenGL 4.1";
#else
constexpr const char *NEEDS_OPENGL = "Odin needs OpenGL 4.3";
#endif

/* The tabs of the pages, in the order of Menu::Page */
constexpr const char *TABS[] = { "Input", "Display", "Audio", "Hotkeys", "Outputs", "Link", "Paths", "About" };
constexpr MenuKit::Icon TAB_ICONS[] = { MenuKit::Icon::INPUT, MenuKit::Icon::DISPLAY, MenuKit::Icon::AUDIO,
	                                MenuKit::Icon::HOTKEYS, MenuKit::Icon::OUTPUTS, MenuKit::Icon::LINK,
	                                MenuKit::Icon::FOLDER, MenuKit::Icon::ABOUT };
/* A window this many times wider than high takes the large menu; a
 * narrower one has the names of the tabs not chosen left out */
constexpr float WIDE = 1.5f;
/* The letters take the next whole factor that much below it: larger
 * sooner, as the window is larger (the drawings of the page Input already
 * are, at 1600 by 900) */
constexpr float LETTERS_UP = 0.45f;

/* The size of the menu in a window of that size, and the size it is made
 * for */
std::pair<ImVec2, ImVec2>
menu_size(float w, float h)
{
	const ImVec2 made = w >= WIDE * h ? ImVec2{ LARGE_W, LARGE_H } : ImVec2{ COMPACT_W, COMPACT_H };

	return { { std::min(w, std::max(w * SHARE, made.x)), std::min(h, std::max(h * SHARE, made.y)) }, made };
}

ImVec4
colour(ImU32 c)
{
	return ImGui::ColorConvertU32ToFloat4(c);
}

/* Dear ImGui's colours from the palette: its texts and scroll bars. The
 * frame of the menu is the kit's (MenuFocus); Dear ImGui's only in a list
 * dropped open (MenuKit::list_popup). */
void
colour_style(ImGuiStyle &style, const MenuPalette &palette)
{
	style.Colors[ImGuiCol_Text] = colour(palette.ink);
	style.Colors[ImGuiCol_TextDisabled] = colour(palette.faint);
	style.Colors[ImGuiCol_ChildBg] = colour(0);
	style.Colors[ImGuiCol_ScrollbarBg] = colour(0);
	style.Colors[ImGuiCol_ScrollbarGrab] = colour(palette.outline);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = colour(palette.dim);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = colour(palette.ink);
	style.Colors[ImGuiCol_NavCursor] = colour(0);
}

/* How much light the background keeps */
constexpr float LIGHT = 0.55f;

void
set_hint(const char *name, const std::optional<std::string> &value)
{
	if (value)
		SDL_SetHint(name, value->c_str());
	else
		SDL_ResetHint(name);
}

} /* namespace */

std::string
Menu::first_key(const MenuState &state, Hotkeys::Action action)
{
	return Hotkeys::first_text(state.hotkeys[static_cast<size_t>(action)]);
}

int
Menu::anisotropy_index(unsigned anisotropy)
{
	const auto at = std::find(std::begin(ANISOTROPY), std::end(ANISOTROPY), anisotropy);

	return at == std::end(ANISOTROPY) ? -1 : static_cast<int>(at - std::begin(ANISOTROPY));
}

Menu::Menu(SDL_Window *window, HostGraphics &graphics)
	: m_window(window), m_graphics(graphics), m_imgui(nullptr), m_scale(0.0f), m_page(INPUT), m_cursor_shown(true)
{
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		if (const char *const value = SDL_GetHint(MENU_HINTS[i].first))
			m_hints[i] = value;

	m_imgui = ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	/* Nothing written beside the game */
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	/* The kit draws the widgets; the windows only hold them */
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowBorderSize = 0.0f;
	style.ChildBorderSize = 0.0f;
	style.WindowPadding = { PADDING, PADDING };
	style.ItemSpacing = { 0.0f, 0.0f };
	style.ScrollbarSize = 8.0f;
	style.ScrollbarRounding = 0.0f;
	colour_style(style, *m_palette);
	m_style = style;

	ImGui_ImplSDL3_InitForOpenGL(window, nullptr);
	/* The gamepads are Odin's (controls.hpp) */
	ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		set_hint(MENU_HINTS[i].first, m_hints[i]);
}

Menu::~Menu()
{
	for (const auto &[game, rgba] : m_library_handed)
		SDL_DestroySurface(rgba);
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext(m_imgui);
}

void
Menu::open(int page)
{
	for (const auto &[name, value] : MENU_HINTS)
		SDL_SetHint(name, value);
	m_cursor_shown = SDL_CursorVisible();
	if (page >= 0 && page < PAGES && page != m_page) {
		m_page = page;
		m_focus = {};
	}
}

void
Menu::close()
{
	for (size_t i = 0; i < MENU_HINTS.size(); i++)
		set_hint(MENU_HINTS[i].first, m_hints[i]);
	SDL_CaptureMouse(false);
	if (SDL_TextInputActive(m_window))
		SDL_StopTextInput(m_window);
	if (m_cursor_shown)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

void
Menu::event(const SDL_Event &event)
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui_ImplSDL3_ProcessEvent(&event);
}

bool
Menu::item_at(MenuState &state, float x, float y)
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui::GetIO().AddMousePosEvent(x, y);
	build(state);
	/* This frame's alone: IsAnyItemHovered also counts the one before */
	return GImGui->HoveredId != 0;
}

bool
Menu::popup_open() const
{
	ImGui::SetCurrentContext(m_imgui);
	return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

bool
Menu::typing() const
{
	ImGui::SetCurrentContext(m_imgui);
	return ImGui::GetIO().WantTextInput;
}

/* What a gamepad asks: the page or the game before or after (the games of
 * the page Outputs; on the page Input these buttons are tested as the
 * controls they work, and its first rows change the page and the game); a
 * list dropped open takes the moves, A and B as the keys of its list */
void
Menu::navigate(MenuState &state)
{
	MenuState::Nav &nav = state.nav;

	/* LB or RB: the frame on the first row under the tabs */
	if (nav.page && m_page != INPUT) {
		m_page = (m_page + PAGES + nav.page % PAGES) % PAGES;
		m_focus = {};
		m_focus.at = 1;
		nav = {};
	}
	if (nav.game && (m_page == OUTPUTS || m_page == LINK)) {
		const std::vector<size_t> games = row_games(state, m_page == LINK);
		const int n = static_cast<int>(games.size());
		const auto at = std::find(games.begin(), games.end(), state.shown);
		const int shown = at == games.end() ? 0 : static_cast<int>(at - games.begin());

		if (n)
			state.show = games[static_cast<size_t>((shown + nav.game % n + n) % n)];
	}
	m_focus.side = 0;
	m_focus.press = false;
	m_focus.moved = false;
	if (!popup_open()) {
		/* The frame: on the first item at the first button, else moved */
		if (m_focus.at >= m_focus.last && m_focus.last > 0)
			m_focus.at = m_focus.last - 1;
		if (m_focus.at < 0 && (nav.move || nav.side || nav.press)) {
			m_focus.at = 0;
			m_focus.moved = true;
		} else if (m_focus.at >= 0) {
			if (nav.move && m_focus.last > 0) {
				m_focus.at = ((m_focus.at + nav.move) % m_focus.last + m_focus.last) % m_focus.last;
				m_focus.moved = true;
				m_focus.part = 0;
			}
			m_focus.side = nav.side;
			m_focus.press = nav.press;
		}
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	const auto tap = [&io](ImGuiKey key) {
		io.AddKeyEvent(key, true);
		io.AddKeyEvent(key, false);
	};

	for (int i = 0; i < std::abs(nav.move); i++)
		tap(nav.move < 0 ? ImGuiKey_UpArrow : ImGuiKey_DownArrow);
	if (nav.press)
		tap(ImGuiKey_Enter);
	if (nav.back)
		tap(ImGuiKey_Escape);
	nav = {};
}

void
Menu::use_palette(unsigned theme)
{
	const MenuPalette *const palette = THEMES[theme < std::size(THEMES) ? theme : 0].palette;

	if (palette == m_palette)
		return;
	m_palette = palette;
	colour_style(m_style, *palette);
	colour_style(ImGui::GetStyle(), *palette);
}

/* A frame of Dear ImGui begun in the palette of that theme, at the scale
 * of the window; the gamepad's buttons for the menu taken before it */
void
Menu::begin_frame(unsigned theme, MenuState *state)
{
	ImGui::SetCurrentContext(m_imgui);
	use_palette(theme);
	follow_scale();
	if (state)
		navigate(*state);
	ImGui_ImplSDL3_NewFrame();
	add_fonts();
	ImGui::NewFrame();
	close_bar_menus();
}

/* The menu grows with the window, by one factor for all: as
 * much as the menu made for 640 by 480, or 960 by 540 in a wide window,
 * takes to fill its share one way, the other way given the room left;
 * never smaller. In points: the pixels of a dense display are SDL's. Its
 * letters, drawn by pixels, grow by a whole factor in the pixels of the
 * display (LETTERS_UP), so that each stays a square of whole pixels. */
void
Menu::follow_scale()
{
	int w = 0, h = 0;

	SDL_GetWindowSize(m_window, &w, &h);

	const auto [size, made] = menu_size(static_cast<float>(w), static_cast<float>(h));
	const float scale = std::max(1.0f, std::min(size.x / made.x, size.y / made.y));
	const float density = std::max(1.0f, SDL_GetWindowPixelDensity(m_window));

	if (scale == m_scale && density == m_density)
		return;

	ImGuiStyle &style = ImGui::GetStyle();

	style = m_style;
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = std::max(1.0f, std::floor(scale * density + 1.0f - LETTERS_UP)) / density;
	m_scale = scale;
	m_density = density;
}

/* Once, where Dear ImGui would take its own font: Cozette (menu_fonts.S),
 * drawn by pixels, 13 high, each letter on whole pixels; behind it, for the
 * letters it lacks (it has Latin, Greek, Cyrillic and Vietnamese), the
 * fonts of the system: those of most other languages (a folder named in
 * Japanese...) */
void
Menu::add_fonts()
{
	static constexpr float COZETTE_SIZE = 13.0f;
	ImFontAtlas &fonts = *ImGui::GetIO().Fonts;
	ImFontConfig pixels;
	std::string names;

	if (fonts.Sources.Size)
		return;
	pixels.FontDataOwnedByAtlas = false;
	pixels.PixelSnapH = true;
	pixels.OversampleH = pixels.OversampleV = 1;
	fonts.AddFontFromMemoryTTF(const_cast<unsigned char *>(odin_font_cozette),
	                           static_cast<int>(odin_font_cozette_end - odin_font_cozette), COZETTE_SIZE, &pixels);
	m_fonts = std::make_unique<SystemFonts>();
	for (const SystemFonts::Font &font : m_fonts->fonts()) {
		ImFontConfig more;

		more.MergeMode = true;
		more.FontDataOwnedByAtlas = false;
		fonts.AddFontFromMemoryTTF(const_cast<unsigned char *>(font.data), static_cast<int>(font.size), 0.0f, &more);
		names += (names.empty() ? "" : ", ") + font.path;
	}
	std::printf("menu: fonts for other alphabets: %s\n",
	            names.empty() ? "none found on this system" : names.c_str());
}

/* The frame in the middle of the window: the tabs at the top, the page
 * under them, what it says and what the keys do at the bottom */
void
Menu::build(MenuState &state)
{
	begin_frame(state.theme, &state);

	MenuKit kit(*m_palette, m_scale, &m_focus);
	const MenuPalette &palette = kit.palette();
	const ImVec2 room = ImGui::GetIO().DisplaySize;
	const ImVec2 size = menu_size(room.x, room.y).first;
	const ImVec2 a = { std::round((room.x - size.x) / 2.0f), std::round((room.y - size.y) / 2.0f) };
	const ImVec2 b = { a.x + size.x, a.y + size.y };
	const float w = size.x, line = ImGui::GetTextLineHeight(), pad = kit.px(PADDING), foot_pad = kit.px(6.0f);

	ImGui::SetNextWindowPos(a);
	ImGui::SetNextWindowSize(size);
	/* The frame stays where it is; its pages scroll */
	ImGui::Begin("Odin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

	ImDrawList *const list = ImGui::GetWindowDrawList();

	kit.frame(list, a, b, kit.px(CUT), palette.frame, palette.outline);
	/* The tabs are the first row of the frame: left and right change the
	 * page there, the frame staying on them (the keyboard has no LB and
	 * RB); on the page Input without the pills of LB and RB, which are
	 * tested there as controls. A tab clicked: no frame on the page shown */
	const bool pills = m_page != INPUT, by_pad = m_focus.at == 0 && m_focus.side;
	/* The tip of the row the gamepad framed in the last picture */
	const std::string framed_tip = m_focus.tip;

	m_focus.count = 0;
	m_focus.tip.clear();
	if (kit.tabs(TABS, TAB_ICONS, room.x < WIDE * room.y, m_page,
	             pills ? std::span<const std::string>(state.pills.data(), 2) : std::span<const std::string>(), true)) {
		m_focus = {};
		if (by_pad)
			m_focus.at = 0;
	}
	MenuKit::fading_line(list, a.x + w * LINE_ENDS, b.x - w * LINE_ENDS,
	                     ImGui::GetCursorScreenPos().y + kit.px(LINE_UNDER), kit.stroke(),
	                     MenuKit::alpha(palette.accent, LINE_ALPHA), LINE_FADE);
	ImGui::Dummy({ 0.0f, kit.px(LINE_ROOM) });

	/* What the page says at the bottom, beside the keys, on as many lines
	 * as it takes, under a line: the page above it; the tip of the row the
	 * gamepad frames in its place. Input says it with the names and inputs
	 * of its controls. While an input is caught, Input says what to do
	 * there, keys and all, and while it says a control, all of it. The
	 * keys are those the user set for the menu, to close the game (over
	 * one) and to quit. */
	const std::string back = first_key(state, Hotkeys::Action::MENU), quit = first_key(state, Hotkeys::Action::QUIT);
	const std::string close = state.library ? std::string() : first_key(state, Hotkeys::Action::CLOSE_GAME);
	std::string keys_said;

	if (!back.empty())
		keys_said = back + (state.testing ? ": stop the test" :
		                    state.library ? ": back to the library" : ": back to the game");
	if (!close.empty())
		keys_said += (keys_said.empty() ? "" : "    ") + close + ": close game";
	if (!quit.empty())
		keys_said += (keys_said.empty() ? "" : "    ") + quit + ": quit";

	const char *const keys = keys_said.c_str();
	const bool keys_shown = m_page != INPUT || (!state.catching && !input_tells(state));
	const float keys_w = ImGui::CalcTextSize(keys).x, help_w = std::max(0.0f, w - 3.0f * pad - keys_w);
	const std::string help = m_page == INPUT ? std::string() :
	                         framed_tip.empty() ? this->help(m_page, state) :
	                         state.kept ? framed_tip : framed_tip + " " + NOT_SAVED;
	const float help_h = m_page == INPUT ? 0.0f : ImGui::CalcTextSize(help.c_str(), nullptr, false, help_w).y;
	const float foot = m_page == INPUT ? input_foot(kit, state, {}, help_w, 0.0f, false) : std::max(line, help_h);
	const float foot_top = b.y - foot_pad - foot;

	/* Each page its windows, and so where they are rolled: one page rolled
	 * down does not open the next rolled down */
	ImGui::PushID(m_page);
	ImGui::BeginChild("page", { 0.0f, foot_top - foot_pad - ImGui::GetCursorScreenPos().y }, ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoBackground);
	if (m_page == INPUT)
		page_input(kit, state);
	else if (m_page == DISPLAY)
		page_display(kit, state);
	else if (m_page == AUDIO)
		page_audio(kit, state);
	else if (m_page == HOTKEYS)
		page_hotkeys(kit, state);
	else if (m_page == OUTPUTS)
		page_outputs(kit, state);
	else if (m_page == LINK)
		page_link(kit, state);
	else if (m_page == PATHS)
		page_paths(kit, state);
	else
		page_about(kit);
	ImGui::Dummy({ 0.0f, kit.px(MenuKit::PAGE_FOOT) });
	ImGui::EndChild();
	ImGui::PopID();
	m_focus.last = m_focus.count;
	/* The advanced options and the test of the page Input end with it */
	if (m_page != INPUT)
		state.advanced = state.testing = false;

	list->AddLine({ a.x, foot_top - foot_pad }, { b.x, foot_top - foot_pad }, palette.line, kit.stroke());
	if (m_page == INPUT)
		input_foot(kit, state, { a.x + pad, foot_top }, keys_shown ? help_w : w - 2.0f * pad, foot, true);
	else
		list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { a.x + pad, foot_top + (foot - help_h) / 2.0f },
		              palette.dim, help.c_str(), nullptr, help_w);
	if (keys_shown)
		list->AddText({ b.x - pad - keys_w, foot_top + (foot - line) / 2.0f }, palette.faint, keys);

	ImGui::End();
	/* A click beside the frame goes back to the game or the library, as
	 * Escape does: not the one that closes a list dropped open (it closes
	 * after this picture), nor while a key, a button or a folder is asked
	 * for */
	if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
	    !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup |
	                            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
	    !popup_open() && !state.catching && !state.mapping && !state.hotkey_catching && !state.choosing)
		state.ask = MenuState::Ask::BACK;
	ImGui::Render();
}

/* Display: what draws the picture, how fine, where, how fast. At the left,
 * one at a time, what the row the mouse is on does, else the row of the
 * frame of the gamepad, else the last one shown: real captures of Odin. */
void
Menu::page_display(MenuKit &kit, MenuState &state) const
{
	static constexpr float SHARE = 0.45f;	/* of the width, the left's */
	static constexpr double FADE_S = 0.15;
	char about[160];
	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float gap = kit.px(14.0f), left = std::floor((room.x - gap) * SHARE), high = room.y - kit.px(MenuKit::PAGE_FOOT);
	std::optional<int> pointed, framed;
	/* A row, and whether the mouse or the frame is on it */
	const auto watch = [&](int which, const auto &row) {
		const int from = kit.focus_count();
		const ImVec2 a = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;

		row();
		if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, { a.x + w, ImGui::GetCursorScreenPos().y }))
			pointed = which;
		if (kit.focus_at() >= from && kit.focus_at() < kit.focus_count())
			framed = which;
	};

	ImGui::SetCursorScreenPos({ top.x + left + gap, top.y });
	ImGui::BeginChild("settings", { room.x - left - gap, high }, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

	const bool narrow = ImGui::GetContentRegionAvail().x < kit.px(SIZES_WHOLE);

	kit.begin_group("RENDERER");
	int api = static_cast<int>(state.api);

	kit.next_tip("What draws the picture. If Vulkan cannot start on this computer, OpenGL draws instead.");
	watch(PREVIEW_CARD, [&] {
		if (kit.choice("Graphics API", "at the next launch", APIS_SHOWN, api))
			state.api = static_cast<unsigned>(api);
	});

	if (state.picks_gpu()) {
		std::vector<std::string> cards = { "Automatic" };
		int card = 0;

		cards.insert(cards.end(), state.gpus.begin(), state.gpus.end());
		for (size_t i = 1; i < cards.size(); i++)
			if (cards[i] == state.gpu_chosen)
				card = static_cast<int>(i);
		/* Named in the settings, not in this computer */
		if (card == 0 && !state.gpu_chosen.empty()) {
			card = static_cast<int>(cards.size());
			cards.push_back(state.gpu_chosen);
		}
		kit.next_tip("The card that draws. Automatic takes the fastest kind.");
		watch(PREVIEW_CARD, [&] {
			if (kit.value_list("Graphics card", "at the next launch", cards[static_cast<size_t>(card)].c_str(),
			                   "Change...", cards, card))
				state.gpu_chosen = card == 0 ? "" : cards[static_cast<size_t>(card)];
		});
	}
	kit.next_tip(state.picks_gpu() ? "The card drawing now." : "Chosen by the system, not by Odin.");
	watch(PREVIEW_CARD, [&] {
		kit.text(state.picks_gpu() ? "In use" : "Graphics card", nullptr, state.gpu.c_str());
	});
	kit.end_group();

	kit.begin_group("3D QUALITY");
	int scale = static_cast<int>(state.scale) - 1;

	std::snprintf(about, sizeof(about), "The 3D is drawn at %u × %u%s.", 640 * state.scale, 480 * state.scale,
	              state.scale == 1 ? ", as on the cabinet" : "");
	kit.next_tip(about);
	watch(PREVIEW_UPSCALE, [&] {
		if (kit.choice("Internal resolution", nullptr, SCALES, scale,
		               state.scale_given ? "set on the command line: -x" : nullptr))
			state.scale = static_cast<unsigned>(scale) + 1;
	});

	/* How the flat pieces (the HUD, its text) are drawn when finer */
	static constexpr const char *UPSCALE_MODES_TIP[] = {
		"The HUD and text are upscaled like the 3D: thin seams may show between their pieces.",
		"The corners of flat pieces (HUD, text) snap to the cabinet's pixels: no seams, and slanted edges stay "
		"smooth.",
		"Flat pieces cover the same pixels as on the cabinet, with finer textures: no seams, and edges look as on "
		"the cabinet.",
	};
	int upscale_mode = static_cast<int>(std::min(state.upscale_mode, 2u));

	kit.next_tip(UPSCALE_MODES_TIP[upscale_mode]);
	watch(PREVIEW_MODE, [&] {
		if (kit.choice("Upscaling mode", state.scale == 1 ? "no effect at 1×" : nullptr, UPSCALE_MODES_SHOWN,
		               upscale_mode))
			state.upscale_mode = static_cast<unsigned>(upscale_mode);
	});

	int filter = anisotropy_index(state.anisotropy);

	kit.next_tip("Sharpens distant textures, such as roads and floors (NASCAR, Virtual-On). The cabinet has none.");
	watch(PREVIEW_FILTER, [&] {
		if (kit.choice("Anisotropic filtering", nullptr, FILTERS, filter,
		               state.anisotropy_given ? "set on the command line: -a" : nullptr))
			state.anisotropy = ANISOTROPY[filter];
	});
	kit.end_group();

	kit.begin_group("SCREEN");
	static constexpr const char *MODES[] = { "Window", "Full screen" };
	/* The keys of full screen, as the user set them */
	const Hotkeys::Binding &full = state.hotkeys[static_cast<size_t>(Hotkeys::Action::FULLSCREEN)];
	std::string shortcut;

	for (const HotKey &key : full.keys)
		if (key.key != SDLK_UNKNOWN)
			shortcut += (shortcut.empty() ? " Shortcut: " : " or ") + Hotkeys::text(key);
	std::snprintf(about, sizeof(about), "Full screen uses your display's size, %u × %u.%s%s", state.display_w,
	              state.display_h, shortcut.c_str(), shortcut.empty() ? "" : ".");
	kit.next_tip(about);
	watch(PREVIEW_SCREEN, [&] {
		kit.choice("Display mode", nullptr, MODES, state.fullscreen,
		           state.fullscreen_given ? "set on the command line: -f" : nullptr);
	});

	int window = state.custom_w ? CUSTOM : state.window <= CUSTOM ? static_cast<int>(state.window) - 1 : -1;
	char sizes[96] = "640 × 480 by default. Custom keeps the size you give the window by resizing it.";

	if (state.custom_w)
		std::snprintf(sizes, sizeof(sizes), "Custom: %u × %u, the size you gave the window by resizing it.",
		              state.custom_w, state.custom_h);
	kit.next_tip(sizes);
	watch(PREVIEW_SCREEN, [&] {
		if (!kit.choice("Window size", nullptr, narrow ? WINDOW_WIDTHS : WINDOW_SIZES, window,
		                state.window_given ? "set on the command line: -w" :
		                state.fullscreen   ? "not in full screen" :
		                                     nullptr,
		                state.fullscreen))
			return;
		/* Custom: the window as it is now */
		state.custom_w = window == CUSTOM ? state.window_w : 0;
		state.custom_h = window == CUSTOM ? state.window_h : 0;
		if (window != CUSTOM)
			state.window = static_cast<unsigned>(window) + 1;
	});

	/* Under the name, why a choice does not take; in the tip, what each
	 * does and the rate of the display */
	static constexpr const char *SYNCS_TIP[] = {
		"Frames do not wait for the display: the picture may tear.",
		"Each frame waits for the display: no tearing, but a late frame slows the game.",
		"Frames on time wait for the display; a late frame shows at once and may tear.",
	};
	int sync = static_cast<int>(state.vsync);
	const char *const why = state.vsync_off ? state.vsync_off :
	                        state.refresh_short && state.vsync == 1 ? "off: this display runs below the cabinet's 60 Hz" :
	                                                                  nullptr;

	if (state.refresh > 0.0f)
		std::snprintf(about, sizeof(about), "%s This display runs at %.2f Hz.", SYNCS_TIP[state.vsync],
		              static_cast<double>(state.refresh));
	else
		std::snprintf(about, sizeof(about), "%s", SYNCS_TIP[state.vsync]);
	kit.next_tip(about);
	watch(PREVIEW_VSYNC, [&] {
		if (kit.choice("Vertical sync", why, SYNCS, sync, nullptr, state.vsync_off != nullptr))
			state.vsync = static_cast<unsigned>(sync);
	});

	int sides = static_cast<int>(state.sides);

	kit.next_tip("What fills the sides of a wide window: Off (black), Blur (the game's edges blurred) or Bezel "
	             "(its bezel from the Artwork folder, Paths page).");
	watch(PREVIEW_SCREEN, [&] {
		if (kit.choice("Bezel", nullptr, SIDES_SHOWN, sides))
			state.sides = static_cast<unsigned>(sides);
	});
	kit.end_group();

	/* The screens of the cabinets (filters/lola-retro, Odin's presets on
	 * the shaders of Lola Retro); another filter odin.ini names is shown
	 * by its name */
	kit.begin_group("FILTER");
	std::vector<std::string> screens(std::begin(SCREENS), std::end(SCREENS));
	int screen = -1;

	for (size_t s = 0; s < std::size(PRESETS); s++)
		if (state.filter == PRESETS[s])
			screen = static_cast<int>(s);
	if (screen < 0) {
		screens.push_back(state.filter);
		screen = static_cast<int>(screens.size()) - 1;
	}
	kit.next_tip("Imitates a screen (Lola Retro filters): the cabinets' screens from their manuals, Sega's monitor for "
	             "Planet Harriers and NASCAR and projector for the other Hikaru games, Gaelco's 28-inch monitor for "
	             "Smashing Drive and Gaelco Football and 34-inch for ATV Track. "
	             "Demanding on the graphics card.");
	watch(PREVIEW_CRT, [&] {
		if (kit.dropdown("CRT screen", nullptr, screens, screen) && screen >= 0 &&
		    static_cast<size_t>(screen) < std::size(PRESETS))
			state.filter = PRESETS[screen];
	});
	kit.end_group();

	kit.begin_group("SPEED");
	kit.next_tip("Unlimited runs as fast as the computer can.");
	watch(PREVIEW_SCREEN, [&] {
		kit.choice("Speed limit", "unlimited: no sound · at the next launch", PACES, state.unthrottled,
		           state.unthrottled_given ? "set on the command line: -u" : nullptr);
	});

	kit.next_tip("Shows frames per second in the window title and the bar.");
	watch(PREVIEW_SCREEN, [&] { kit.choice("Show FPS", nullptr, OFF_ON, state.show_speed); });
	kit.end_group();

	/* Each palette by four of its colours: the screen, a drawing, the ink,
	 * the accent */
	kit.begin_group("THEME");
	std::vector<MenuKit::Item> themes;
	int theme = static_cast<int>(state.theme);

	for (const MenuTheme &one : THEMES) {
		const MenuPalette &p = *one.palette;

		themes.push_back({ one.name, one.about, one.kind, MenuKit::Icon::NONE, false,
		                   { p.screen, p.part, p.ink, p.accent } });
	}
	kit.next_tip("The colours of the menu, the bar and the library.");
	if (kit.dropdown("Menu theme", nullptr, themes, theme))
		state.theme = static_cast<unsigned>(theme);
	kit.end_group();
	ImGui::Dummy({ 0.0f, kit.px(MenuKit::PAGE_FOOT) });
	ImGui::EndChild();

	/* The left, fading from what it showed to what it shows now */
	const int wanted = pointed ? *pointed : framed ? *framed : state.preview;

	if (wanted != state.preview) {
		state.preview_from = state.preview;
		state.preview = wanted;
		state.preview_since = ImGui::GetTime();
	}

	const ImVec2 a = top, b = { top.x + left, top.y + high };
	const bool fades = state.animations && state.preview_from != state.preview;
	const float u = fades ? static_cast<float>(std::clamp((ImGui::GetTime() - state.preview_since) / FADE_S, 0.0, 1.0)) :
	                        1.0f;

	kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(6.0f), 0, kit.palette().line);
	if (u < 1.0f) {
		state.moving = true;
		preview(kit, state, state.preview_from, a, b, 1.0f - u);
	}
	preview(kit, state, state.preview, a, b, u);
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ room.x, high });
}

/* What a row of the page Display does, in the box from a to b, as much
 * there as `shown` (0 to 1) */
void
Menu::preview(const MenuKit &kit, const MenuState &state, int which, ImVec2 a, ImVec2 b, float shown) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const auto faded = [shown](ImU32 colour) { return MenuKit::alpha(colour, shown); };
	const float pad = kit.px(16.0f), caps = MenuKit::font(MenuKit::SMALL), note = MenuKit::font(MenuKit::NOTE);
	const ImVec2 lo = { a.x + pad, a.y + pad }, hi = { b.x - pad, b.y - pad };
	const float width = hi.x - lo.x, mid = (lo.x + hi.x) / 2.0f;
	/* A text centred where it fits on a line, else wrapped in the width;
	 * its height */
	const auto centred = [&](float y, const char *text, ImU32 colour, float text_size) {
		const ImVec2 at = ImGui::GetFont()->CalcTextSizeA(text_size, FLT_MAX, width, text);
		const float x = at.x < width ? mid - at.x / 2.0f : lo.x;

		list->AddText(ImGui::GetFont(), text_size, { x, y }, faded(colour), text, nullptr, width);
		return at.y;
	};
	const auto caps_centred = [&](float x, float y, const char *text) {
		MenuKit::small_caps(list, { x - MenuKit::small_caps_width(text) / 2.0f, y }, faded(palette.dim), text);
	};
	/* The cabinet's against the user's choice (none: the choice is the
	 * cabinet's), side by side or one above the other, as they are the
	 * biggest; what they are under them, where they come from at the foot */
	const auto pair = [&](const char *title, int cabinet, int chosen, const char *cabinet_name, const std::string &chosen_name,
	                      const char *source, const char *pick = "pick 2× or more to see the difference") {
		const float apart = kit.px(12.0f), under = caps + kit.px(16.0f);
		const float source_h = MenuKit::wrapped_h(source, note, width);
		const float room = hi.y - lo.y - caps - kit.px(10.0f) - source_h - kit.px(12.0f);
		const float beside = std::min((width - apart) / 2.0f * 0.75f, room - under);
		const float stacked = std::min(width * 0.75f, (room - 2.0f * under - apart) / 2.0f);
		const bool above = stacked > beside;
		const float h = std::max(above ? stacked : beside, 0.0f), w = h / 0.75f;
		const float block = caps + kit.px(10.0f) + (above ? 2.0f * (h + under) + apart : h + under);
		float y = lo.y + std::max(0.0f, (hi.y - lo.y - source_h - kit.px(12.0f) - block) / 2.0f);
		const std::string chosen_caption = "YOUR CHOICE: " + chosen_name;
		/* One picture, framed, and its name under it */
		const auto one = [&](int which_picture, ImVec2 at, const char *name) {
			const ImVec2 to = { at.x + w, at.y + h };

			if (which_picture >= 0) {
				picture(list, which_picture, at, to, shown);
				list->AddRect(at, to, faded(palette.line), 0.0f, ImDrawFlags_None, kit.stroke());
			} else {
				const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(note, FLT_MAX, w - kit.px(16.0f), pick);

				kit.dashed_frame(list, at, to, faded(palette.outline));
				list->AddText(ImGui::GetFont(), note, { at.x + kit.px(8.0f), at.y + (h - text.y) / 2.0f },
				              faded(palette.faint), pick, nullptr, w - kit.px(16.0f));
			}
			caps_centred(at.x + w / 2.0f, to.y + kit.px(8.0f), name);
		};

		caps_centred(mid, y, title);
		y += caps + kit.px(10.0f);
		if (above) {
			one(cabinet, { mid - w / 2.0f, y }, cabinet_name);
			one(chosen, { mid - w / 2.0f, y + h + under + apart }, chosen_caption.c_str());
		} else {
			one(cabinet, { mid - apart / 2.0f - w, y }, cabinet_name);
			one(chosen, { mid + apart / 2.0f, y }, chosen_caption.c_str());
		}
		list->AddText(ImGui::GetFont(), note, { lo.x, hi.y - source_h }, faded(palette.faint), source, nullptr, width);
	};

	switch (which) {
	case PREVIEW_CARD: {
		const char *const name = state.gpu.empty() ? "not known yet" : state.gpu.c_str();
		const float big = MenuKit::font(1.3f);
		const float name_h = MenuKit::wrapped_h(name, big, width);
		float y = lo.y + std::max(0.0f, (hi.y - lo.y - caps - name_h - note - kit.px(22.0f)) / 2.0f);

		caps_centred(mid, y, "GRAPHICS CARD");
		y += caps + kit.px(12.0f);
		y += centred(y, name, palette.ink, big) + kit.px(10.0f);
		centred(y, state.vulkan ? "Odin needs Vulkan 1.1" : NEEDS_OPENGL, palette.dim, note);
		break;
	}
	case PREVIEW_UPSCALE: {
		const unsigned scale = std::clamp(state.scale, 1u, 6u);
		char chosen[8];

		std::snprintf(chosen, sizeof(chosen), "%u×", scale);
		pair("INTERNAL RESOLUTION", UPSCALE, scale > 1 ? static_cast<int>(UPSCALE + scale - 1) : -1, "1× · THE CABINET", chosen,
		     "Captured in Odin: Brave Firefighters, the same picture at each scale.");
		break;
	}
	case PREVIEW_MODE: {
		static constexpr const char *NAMES[] = { "RAW", "SNAP 2D", "NATIVE 2D" };
		const unsigned mode = std::min(state.upscale_mode, 2u);

		pair("UPSCALING MODE", MODE, state.scale > 1 ? static_cast<int>(MODE + 1 + mode) : -1, "1× · THE CABINET",
		     NAMES[mode],
		     "Captured in Odin: the Virtual-On timer at 4×. Snap 2D and Native 2D may split a piece made of a flat "
		     "part and a tilted one (the lock-on reticle in Planet Harriers).");
		break;
	}
	case PREVIEW_FILTER: {
		const int filter = anisotropy_index(state.anisotropy);
		static constexpr const char *NAMES[] = { "OFF", "2×", "4×", "8×", "16×" };
		/* A level of odin.ini or -a that the menu does not list, as it is */
		const std::string chosen = filter >= 0 ? NAMES[filter] : std::to_string(state.anisotropy) + "×";

		pair("ANISOTROPIC FILTERING", FILTER, filter > 0 ? FILTER + filter : -1, "OFF · THE CABINET", chosen,
		     "Captured in Odin: the distant road in Planet Harriers, at 1×.");
		break;
	}
	case PREVIEW_CRT: {
		const auto at = std::find(std::begin(PRESETS), std::end(PRESETS), state.filter);
		const int screen = at == std::end(PRESETS) ? -1 : static_cast<int>(at - std::begin(PRESETS));

		std::string chosen = screen >= 0 ? SCREENS[screen] : state.filter;

		for (char &c : chosen)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		pair("CRT SCREEN", CRT, screen >= 0 ? CRT + screen : -1, "NONE", chosen,
		     "Captured in Odin: a close-up of the title of Planet Harriers in a 2560 × 1440 window. The look "
		     "changes with the size of your display.",
		     "no close-up of this filter");
		break;
	}
	case PREVIEW_VSYNC: {
		static constexpr const char *NAMES[] = { "OFF", "ON", "ADAPTIVE" };

		pair("VERTICAL SYNC", WHOLE, state.vsync == 0 ? TORN : WHOLE, "WHOLE · THE CABINET",
		     NAMES[std::min(state.vsync, 2u)],
		     "Captured in Odin: two frames in a row of Brave Firefighters; when torn, the top of one meets the "
		     "bottom of the next.");
		break;
	}
	case PREVIEW_SCREEN: {
		const float sw = static_cast<float>(state.display_w ? state.display_w : 1920);
		const float sh = static_cast<float>(state.display_h ? state.display_h : 1080);
		const float ww = state.fullscreen ? sw :
		                 state.custom_w ? static_cast<float>(state.custom_w) : 640.0f * static_cast<float>(state.window);
		const float wh = state.fullscreen ? sh :
		                 state.custom_h ? static_cast<float>(state.custom_h) : 480.0f * static_cast<float>(state.window);
		char caption[160];

		if (state.fullscreen)
			std::snprintf(caption, sizeof(caption), "Full screen, at your display's %u × %u.",
			              static_cast<unsigned>(sw), static_cast<unsigned>(sh));
		else
			std::snprintf(caption, sizeof(caption), "Window of %u × %u, on your %u × %u display%s",
			              static_cast<unsigned>(ww), static_cast<unsigned>(wh), static_cast<unsigned>(sw),
			              static_cast<unsigned>(sh), ww > sw || wh > sh ? ": larger than the display." : ".");

		const float caption_h = MenuKit::wrapped_h(caption, note, width);
		const float k = std::min(width / sw, (hi.y - lo.y - caps - caption_h - kit.px(26.0f)) / sh);
		float y = lo.y + std::max(0.0f, (hi.y - lo.y - caps - caption_h - kit.px(26.0f) - sh * k) / 2.0f);

		caps_centred(mid, y, "SCREEN");
		y += caps + kit.px(12.0f);

		/* The display, and the window on it with its title */
		const ImVec2 sa = { mid - sw * k / 2.0f, y }, sb = { mid + sw * k / 2.0f, y + sh * k };
		const ImVec2 wa = { mid - ww * k / 2.0f, (sa.y + sb.y) / 2.0f - wh * k / 2.0f }, wb = { wa.x + ww * k, wa.y + wh * k };
		const float bar = state.fullscreen ? 0.0f : std::max(kit.px(5.0f), 30.0f * k);

		list->AddRectFilled(sa, sb, faded(IM_COL32(0, 0, 0, 0x60)));
		list->PushClipRect(sa, sb, true);
		picture(list, SCREEN, { wa.x, wa.y + bar }, wb, shown);
		if (bar > 0.0f) {
			const char *const title = state.show_speed ? "Odin (60.0 FPS)" : "Odin";
			const float title_size = MenuKit::text_size(std::min(note, bar * 0.8f));

			list->AddRectFilled(wa, { wb.x, wa.y + bar }, faded(palette.part));
			if (title_size <= bar && MenuKit::measure(title, title_size) < wb.x - wa.x - kit.px(8.0f))
				list->AddText(ImGui::GetFont(), title_size, { wa.x + kit.px(4.0f), wa.y + (bar - title_size) / 2.0f },
				              faded(palette.dim), title);
			list->AddRect(wa, wb, faded(palette.outline), 0.0f, ImDrawFlags_None, kit.stroke());
		}
		list->PopClipRect();
		list->AddRect(sa, sb, faded(palette.line), 0.0f, ImDrawFlags_None, kit.stroke());
		centred(sb.y + kit.px(10.0f), caption, palette.dim, note);
		break;
	}
	default:
		break;
	}
}

/* A picture of the page Display, from a to b (none until the drawing
 * thread has made it) */
void
Menu::picture(ImDrawList *list, int which, ImVec2 a, ImVec2 b, float shown) const
{
	const HostImage image = m_pictures[static_cast<size_t>(which)];

	if (!image.id)
		return;
	m_graphics.ui_blocks(list, true);
	list->AddImage(ImTextureRef(static_cast<ImTextureID>(m_graphics.ui_image(image))), a, b, { 0.0f, 0.0f },
	               { 1.0f, 1.0f }, MenuKit::alpha(IM_COL32_WHITE, shown));
	m_graphics.ui_blocks(list, false);
}

std::string
Menu::help(int page, const MenuState &state)
{
	const char *const kept = state.kept ? "." : ", but are not saved (ODIN_CONFIG=none).";

	switch (page) {
	case DISPLAY:
		return std::string("These settings apply to every game") + kept;
	case AUDIO:
		return state.kept ? "Cabinet sound is set per game. Vibration is on the Input page." :
		                    std::string("Cabinet sound is set per game. Vibration is on the Input page. ") + NOT_SAVED;
	case HOTKEYS:
		return state.hotkey_catching || state.kept ? hotkeys_help(state) :
		       hotkeys_help(state) + " " + NOT_SAVED;
	case OUTPUTS:
		return state.kept ? "" : NOT_SAVED;
	case LINK:
		return std::string("These settings apply when a game starts") + kept;
	case PATHS:
		if (state.no_picker)
			return "No folder picker on this system: set the folders in the [paths] section of odin.ini.";
		return state.choosing ? "Pick the folder in the window that opened." :
		                        std::string("These paths apply to every game") + kept;
	default:
		return "";
	}
}

/* About: the rune in its gold, Odin and the commit it is built from, in
 * the middle; the libraries it is made with */
void
Menu::page_about(MenuKit &kit) const
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x, mid = at.x + width / 2.0f;
	const float rune = kit.px(34.0f), big = MenuKit::font(22.0f / 12.5f), letters = big * 0.2f;
	const float small = MenuKit::font(MenuKit::SMALL);
	const char *const what = WHAT;
	const char *const version = odin_version();
	float y = at.y + kit.px(10.0f);

	MenuKit::icon(list, MenuKit::Icon::RUNE, { mid - rune / 2.0f, y }, rune, LOGO_GOLD);
	y += rune + kit.px(6.0f);
	MenuKit::write(list, { mid - MenuKit::measure("ODIN", big, letters) / 2.0f, y }, palette.ink, "ODIN", big, letters);
	y += big + kit.px(6.0f);
	list->AddText({ mid - ImGui::CalcTextSize(what).x / 2.0f, y }, palette.dim, what);
	y += ImGui::GetTextLineHeight() + kit.px(4.0f);
	if (*version) {
		list->AddText(ImGui::GetFont(), small, { mid - MenuKit::measure(version, small) / 2.0f, y }, palette.dim, version);
		y += small;
	}
	ImGui::Dummy({ 0.0f, y + kit.px(6.0f) - at.y });

	kit.begin_group("LICENCES");
	for (const auto &[name, by, licence] : LICENCES)
		kit.text(name, by, licence);
	kit.end_group();
}

void
Menu::start_drawing()
{
	m_graphics.start_ui();
	/* The pictures: bigger than they are, in blocks, as Odin shows the
	 * picture of a game; smaller, smoothed */
	static_assert(std::size(PICTURE_FILES) == PICTURES);
	for (size_t p = 0; p < PICTURES; p++) {
		const auto [begin, end] = PICTURE_FILES[p];
		SDL_Surface *const png = SDL_LoadPNG_IO(SDL_IOFromConstMem(begin, static_cast<size_t>(end - begin)), true);
		SDL_Surface *const rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;

		SDL_DestroySurface(png);
		if (!rgba) {
			std::fprintf(stderr, "ERROR: menu: picture %zu: %s\n", p, SDL_GetError());
			continue;
		}
		m_pictures[p] = m_graphics.make_image(*rgba, false);
		SDL_DestroySurface(rgba);
	}
	m_drawing = true;
}

void
Menu::freeze(HostPicture picture)
{
	if (!m_drawing)
		start_drawing();
	m_frozen = picture.id != 0 && picture.w > 1 && picture.h > 1;
	m_frozen_window = false;
	if (m_frozen)
		m_graphics.blur_background(picture);
}

void
Menu::freeze_window(unsigned window_w, unsigned window_h)
{
	if (!m_drawing)
		start_drawing();
	m_frozen = m_frozen_window = false;
	draw(window_w, window_h);
	m_frozen = m_frozen_window = window_w > 1 && window_h > 1;
	if (m_frozen)
		m_graphics.blur_window(window_w, window_h);
}

void
Menu::draw(unsigned window_w, unsigned window_h)
{
	upload_library_pictures();
	m_graphics.clear_window(window_w, window_h);

	/* The picture of the game over the whole window, its middle kept
	 * where the window is of another shape; the window as it
	 * was over all of it */
	const PictureRect rect = picture_rect(window_w, window_h);
	if (m_frozen && rect.w && rect.h) {
		const double k = std::max(static_cast<double>(window_w) / rect.w, static_cast<double>(window_h) / rect.h);
		const unsigned w = m_frozen_window ? window_w : static_cast<unsigned>(std::lround(rect.w * k));
		const unsigned h = m_frozen_window ? window_h : static_cast<unsigned>(std::lround(rect.h * k));

		m_graphics.draw_background(window_w, window_h, w, h, LIGHT);
	}

	ImGui::SetCurrentContext(m_imgui);
	m_graphics.draw_ui(ImGui::GetDrawData(), window_w, window_h, "the menu");
}

void
Menu::draw_bar(unsigned window_w, unsigned window_h)
{
	ImGui::SetCurrentContext(m_imgui);
	m_graphics.draw_ui(ImGui::GetDrawData(), window_w, window_h, "the bar");
}

void
Menu::release()
{
	if (!m_drawing)
		return;
	ImGui::SetCurrentContext(m_imgui);
	m_graphics.stop_ui();
	for (HostImage &image : m_pictures)
		m_graphics.forget_image(std::exchange(image, {}));
	for (const auto &[game, picture] : m_library_pictures)
		m_graphics.forget_image(picture);
	m_library_pictures.clear();
	m_drawing = false;
}

} /* namespace odin */

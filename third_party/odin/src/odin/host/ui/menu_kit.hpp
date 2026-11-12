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

#ifndef ODIN_MENU_KIT_HPP
#define ODIN_MENU_KIT_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui/imgui.h"
#include "odin/host/ui/logo.hpp"

namespace odin {

/* The colours of a palette of the menu: the screen behind
 * the pages, the frame, the ink and its two softer tones, the lines of the
 * frames and between rows, the outline of a control and its hovered fill,
 * the accent for what is touched and the ink on it, the action colour for
 * what is clicked or chosen and the ink on it, the red of what clears. The
 * drawings: the body of a gamepad and a part on it, their strokes (the
 * details take the outline of a control), an arrow on a part; the lavender
 * of the cabinet (and of the titles of the groups), its strokes and its
 * details; the back of a tile of the library. Each is made of a few
 * base colours: the screen, the two of the drawings, the ink and
 * its two tones, the accent and the ink on it, the action, the cabinet and
 * the page; the lines and the outlines are the ink, fainter. */
struct MenuPalette {
	ImU32 screen, frame, ink, dim, faint, line, outline, hover, accent, on_accent, action, on_action, danger;
	ImU32 body, part, stroke, arrow;
	ImU32 cabinet, cabinet_stroke, cabinet_detail;
	ImU32 tile;
};

/* The gold of the logo (logo.hpp) */
inline constexpr ImU32 LOGO_GOLD = IM_COL32(RUNE_GOLD[0], RUNE_GOLD[1], RUNE_GOLD[2], 0xFF);

/* Rúnasteinn, the palette by default: granite and the red ochre of painted
 * runestones. The frame lets the game show through, at a fifth. */
inline constexpr MenuPalette RUNASTEINN = {
	IM_COL32(0x2A, 0x2A, 0x28, 0xFF), IM_COL32(0x2A, 0x2A, 0x28, 0xCC),
	IM_COL32(0xEE, 0xEA, 0xE2, 0xFF), IM_COL32(0xB0, 0xAB, 0xA0, 0xFF), IM_COL32(0x83, 0x7F, 0x75, 0xFF),
	IM_COL32(0xEE, 0xEA, 0xE2, 0x29), IM_COL32(0xEE, 0xEA, 0xE2, 0x38), IM_COL32(0xEE, 0xEA, 0xE2, 0x10),
	IM_COL32(0xE0, 0x66, 0x4A, 0xFF), IM_COL32(0x16, 0x06, 0x04, 0xFF), IM_COL32(0x6A, 0xA9, 0xB0, 0xFF),
	IM_COL32(0x2A, 0x2A, 0x28, 0xFF), IM_COL32(0xEF, 0x5A, 0x50, 0xFF),
	IM_COL32(0x2F, 0x2F, 0x2C, 0xFF), IM_COL32(0x38, 0x38, 0x34, 0xFF), IM_COL32(0xEE, 0xEA, 0xE2, 0x9E),
	IM_COL32(0xEE, 0xEA, 0xE2, 0x73),
	IM_COL32(0xBD, 0xB5, 0xD0, 0xFF), IM_COL32(0xBD, 0xB5, 0xD0, 0xCC), IM_COL32(0xBD, 0xB5, 0xD0, 0x66),
	IM_COL32(0x1D, 0x1D, 0x1B, 0xFF),
};

/* Edda: vellum and the red ink of manuscripts, the one light palette */
inline constexpr MenuPalette EDDA = {
	IM_COL32(0xFB, 0xF6, 0xE9, 0xFF), IM_COL32(0xFB, 0xF6, 0xE9, 0xCC),
	IM_COL32(0x23, 0x1C, 0x16, 0xFF), IM_COL32(0x5C, 0x53, 0x4A, 0xFF), IM_COL32(0x80, 0x77, 0x6C, 0xFF),
	IM_COL32(0x23, 0x1C, 0x16, 0x29), IM_COL32(0x23, 0x1C, 0x16, 0x38), IM_COL32(0x23, 0x1C, 0x16, 0x10),
	IM_COL32(0xB0, 0x2A, 0x1F, 0xFF), IM_COL32(0xFF, 0xF8, 0xF0, 0xFF), IM_COL32(0x3E, 0x5F, 0x9F, 0xFF),
	IM_COL32(0xFB, 0xF6, 0xE9, 0xFF), IM_COL32(0xB3, 0x26, 0x1E, 0xFF),
	IM_COL32(0xF3, 0xEC, 0xDC, 0xFF), IM_COL32(0xE9, 0xE0, 0xCC, 0xFF), IM_COL32(0x23, 0x1C, 0x16, 0x9E),
	IM_COL32(0x23, 0x1C, 0x16, 0x73),
	IM_COL32(0x59, 0x40, 0x2E, 0xFF), IM_COL32(0x59, 0x40, 0x2E, 0xCC), IM_COL32(0x59, 0x40, 0x2E, 0x66),
	IM_COL32(0xEF, 0xE7, 0xD6, 0xFF),
};

/* Raf: the amber glow of old terminals */
inline constexpr MenuPalette RAF = {
	IM_COL32(0x11, 0x0B, 0x04, 0xFF), IM_COL32(0x11, 0x0B, 0x04, 0xCC),
	IM_COL32(0xFF, 0xB5, 0x47, 0xFF), IM_COL32(0xC9, 0x8A, 0x2E, 0xFF), IM_COL32(0x8F, 0x62, 0x1F, 0xFF),
	IM_COL32(0xFF, 0xB5, 0x47, 0x29), IM_COL32(0xFF, 0xB5, 0x47, 0x38), IM_COL32(0xFF, 0xB5, 0x47, 0x10),
	IM_COL32(0xFF, 0xF1, 0xD0, 0xFF), IM_COL32(0x1A, 0x0F, 0x02, 0xFF), IM_COL32(0xC8, 0xB5, 0x90, 0xFF),
	IM_COL32(0x11, 0x0B, 0x04, 0xFF), IM_COL32(0xEF, 0x5A, 0x50, 0xFF),
	IM_COL32(0x1A, 0x12, 0x08, 0xFF), IM_COL32(0x22, 0x18, 0x0B, 0xFF), IM_COL32(0xFF, 0xB5, 0x47, 0x9E),
	IM_COL32(0xFF, 0xB5, 0x47, 0x73),
	IM_COL32(0xED, 0xCF, 0x6F, 0xFF), IM_COL32(0xED, 0xCF, 0x6F, 0xCC), IM_COL32(0xED, 0xCF, 0x6F, 0x66),
	IM_COL32(0x0B, 0x07, 0x02, 0xFF),
};

/* Heimdall: black, white and yellow, as legible as can be; its lines and
 * outlines clearer than the others' */
inline constexpr MenuPalette HEIMDALL = {
	IM_COL32(0x08, 0x08, 0x08, 0xFF), IM_COL32(0x08, 0x08, 0x08, 0xCC),
	IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), IM_COL32(0xD2, 0xD2, 0xD2, 0xFF), IM_COL32(0xA8, 0xA8, 0xA8, 0xFF),
	IM_COL32(0xFF, 0xFF, 0xFF, 0x66), IM_COL32(0xFF, 0xFF, 0xFF, 0x73), IM_COL32(0xFF, 0xFF, 0xFF, 0x10),
	IM_COL32(0xFF, 0xD6, 0x00, 0xFF), IM_COL32(0x00, 0x00, 0x00, 0xFF), IM_COL32(0x67, 0xC3, 0xE5, 0xFF),
	IM_COL32(0x08, 0x08, 0x08, 0xFF), IM_COL32(0xEF, 0x5A, 0x50, 0xFF),
	IM_COL32(0x12, 0x12, 0x12, 0xFF), IM_COL32(0x1C, 0x1C, 0x1C, 0xFF), IM_COL32(0xFF, 0xFF, 0xFF, 0x9E),
	IM_COL32(0xFF, 0xFF, 0xFF, 0x73),
	IM_COL32(0xC4, 0xB6, 0xD8, 0xFF), IM_COL32(0xC4, 0xB6, 0xD8, 0xCC), IM_COL32(0xC4, 0xB6, 0xD8, 0x66),
	IM_COL32(0x00, 0x00, 0x00, 0xFF),
};

/* The themes the user picks from (page Display, [menu] theme of
 * odin.ini): the word there, the name shown, its kind and what it is, its
 * palette */
struct MenuTheme {
	const char *key, *name, *kind, *about;
	const MenuPalette *palette;
};

inline constexpr MenuTheme THEMES[] = {
	{ "runasteinn", "Rúnasteinn", "", "granite and the red ochre of runestones", &RUNASTEINN },
	{ "edda", "Edda", "paper", "vellum and the red ink of manuscripts", &EDDA },
	{ "raf", "Raf", "retro", "the amber glow of old terminals", &RAF },
	{ "heimdall", "Heimdall", "high contrast", "black, white and yellow: the most legible", &HEIMDALL },
};

/* The theme of that word of odin.ini; -1 for none */
inline int
theme_named(std::string_view key)
{
	for (size_t t = 0; t < std::size(THEMES); t++)
		if (key == THEMES[t].key)
			return static_cast<int>(t);
	return -1;
}

/* The frame of focus a gamepad moves on a page (menu_nav.hpp): the item it
 * is on, in the order the page draws them (-1: none, until a button moves
 * it), how many the page drew at the last picture and draws now, whether
 * it moved there now (the page scrolls to it); what the gamepad asks of
 * that item now: a choice or a slider moved, pressed; the part of the
 * item chosen (a slot of a shortcut); the tip of the row it is on, which
 * the foot of the menu shows (a gamepad has no mouse to point at a (?)) */
struct MenuFocus {
	int at = -1, last = 0, count = 0;
	bool moved = false;
	int side = 0;
	bool press = false;
	int part = 0;
	std::string tip;
};

/* The dressed widgets of the menu, on Dear ImGui: corners cut at 45
 * degrees, one stroke of one pixel, the action colour on what is clicked
 * and chosen. Sizes are in points, times the scale of the display. One is
 * made for each picture of the menu. */
class MenuKit {
public:
	/* The sizes of the smaller texts, against the font (rows of 12.5
	 * pixels at the size it is drawn for): what a row does and a key cap (10.5); a
	 * title, a cell, a button, a pill and a caption (11); a word in small
	 * capitals (10) */
	static constexpr float NOTE = 10.5f / 12.5f;
	static constexpr float SMALL = 11.0f / 12.5f;
	static constexpr float TINY = 10.0f / 12.5f;
	/* What is left of the action colour on the outline of a control and of
	 * the tab chosen; full on a button hovered or on */
	static constexpr float ACTION_LINE = 0.6f;
	/* The box of a page: the cut of its corners and the room inside it, in
	 * points */
	static constexpr float BOX_CUT = 5.0f;
	static constexpr float BOX_INSIDE = 8.0f;
	/* The room under a page, in points, that every layout of a page
	 * leaves (end_settings) */
	static constexpr float PAGE_FOOT = 4.0f;

	MenuKit(const MenuPalette &palette, float scale, MenuFocus *focus = nullptr)
		: m_palette(palette), m_scale(scale), m_focus(focus)
	{
	}

	const MenuPalette &palette() const { return m_palette; }
	float px(float points) const { return points * m_scale; }
	/* A stroke of one point, in whole pixels: as thick as the menu grows */
	float stroke() const { return std::max(1.0f, std::round(m_scale)); }
	/* That share of the font, in pixels. Its letters are drawn by pixels:
	 * a size is a whole number of times the font's, at least once (the
	 * smaller and larger texts are drawn at its size or
	 * twice it; their colours tell them apart). */
	static float font(float share) { return text_size(ImGui::GetFontSize() * share); }
	static float text_size(float size)
	{
		const float font = ImGui::GetFontSize();

		return font * std::max(1.0f, std::round(size / font));
	}
	/* A colour with that share of its opacity */
	static ImU32 alpha(ImU32 colour, float share);

	/* The icons of the menu, drawn in a square of that side:
	 * a gamepad, a gamepad with its cross, a screen, a speaker, a keyboard
	 * and a smaller one, a light with its rays, a folder, an i; the outputs
	 * of a cabinet: a lamp, a shaker, a lock, a speaker crossed out; those
	 * of the buttons: a list, two arrows that swap, an arrow that turns
	 * back, a cross, a start and its bar, an arrow back; the rune ᚨ; a
	 * toothed wheel (the settings), a steering wheel, four cabinets in a
	 * ring (the link) */
	enum class Icon {
		NONE, PAD, INPUT, DISPLAY, AUDIO, KEYBOARD, HOTKEYS, OUTPUTS, FOLDER, ABOUT, LAMP, RUMBLE, LOCK, MUTE,
		COIN, LIST, SWAP, DEFAULTS, CLEAR, MAP, UNDO, RUNE, SETTINGS, WHEEL, LINK,
	};
	static void icon(ImDrawList *list, Icon icon, ImVec2 at, float side, ImU32 colour);

	/* Texts with the signs the font of the menu has not, drawn: the arrows
	 * ▲ ▼ ◀ ▶ ← → ↑ ↓ ↔ ↕ ▾ ‹ ›, Enter ⏎ and Backspace ⌫, the marks of a
	 * PlayStation gamepad ✕ ○ □ △, the minus of a Switch −. Their width, at
	 * that size (0: the font's), the letters that far apart (small
	 * capitals). */
	static float measure(const char *text, float size = 0.0f, float spacing = 0.0f);
	static void write(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text, float size = 0.0f,
	                  float spacing = 0.0f);
	/* A text cut to that width, three dots where it is cut */
	static std::string fitted(const std::string &text, float width, float size = 0.0f);
	/* The height of a text at that size, on as many lines as that width
	 * takes; the ASCII letters of a text in capitals */
	static float wrapped_h(const char *text, float size, float width)
	{
		return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, width, text).y;
	}
	static std::string capitals(std::string text)
	{
		for (char &c : text)
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
		return text;
	}

	/* An item the frame of focus moves on, from a to b, the frame that much
	 * outside it (inside: below 0): true when the frame is on it; then what
	 * the gamepad asks of it */
	bool focus(ImVec2 a, ImVec2 b, float out = -1.0f) const;
	/* One that draws itself as it is focused: the same, no frame */
	bool focus_mark() const { return m_focus && m_focus->count++ == m_focus->at; }
	/* The item the frame is on, and how many are so far: a row of
	 * widgets has the frame when it is on one of those it counted */
	int focus_at() const { return m_focus ? m_focus->at : -1; }
	int focus_count() const { return m_focus ? m_focus->count : 0; }
	int side() const { return m_focus ? m_focus->side : 0; }
	bool pressed() const { return m_focus && m_focus->press; }

	/* A frame with its corners cut; a colour of 0 is none; its border
	 * that many strokes thick */
	void frame(ImDrawList *list, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 border, float strokes = 1.0f) const;
	/* A line from left to right, `thick` high, that fades in and out at
	 * its ends (that share of its length each) */
	static void fading_line(ImDrawList *list, float left, float right, float y, float thick, ImU32 colour, float fade);
	/* A line of dashes along points, `on` long and `off` apart; a frame of
	 * them with its corners cut */
	static void dashes(ImDrawList *list, std::span<const ImVec2> points, bool closed, ImU32 colour, float width,
	                   float on, float off);
	void dashed_frame(ImDrawList *list, ImVec2 a, ImVec2 b, ImU32 colour) const;

	/* The tabs in the middle of the width, each its icon and its name (the
	 * one chosen only, when `narrow`), between the pills of the buttons of
	 * a gamepad that change them (none: empty): a click picks one, and left
	 * or right of the gamepad where they are a row of the frame
	 * (`focusable`). True when the choice changed. */
	bool tabs(std::span<const char *const> names, std::span<const Icon> icons, bool narrow, int &chosen,
	          std::span<const std::string> pills = {}, bool focusable = false) const;

	/* A group of settings: its title, then its rows in a frame */
	void begin_group(const char *title);
	void end_group();
	/* What the next row says in a tip, on a (?) after its name: the mouse
	 * on its name shows it, and the foot of the menu while the frame of the
	 * gamepad is on the row */
	void next_tip(std::string text) { m_next_tip = std::move(text); }

	/* A row: its name, what it does under it, and at its right a choice
	 * among labels (true when it changed; -1 is none of them) or a text.
	 * A row the command line sets says so (`locked`) and does not change;
	 * a row that cannot change now is `off`; a `warning`, in red, says what
	 * the user must do in place of what it does. */
	bool choice(const char *name, const char *about, std::span<const char *const> labels, int &chosen,
	            const char *locked = nullptr, bool off = false, const char *warning = nullptr);
	/* The same between two labels, `on` the second one */
	bool choice(const char *name, const char *about, std::span<const char *const> labels, bool &on,
	            const char *locked = nullptr, bool off = false, const char *warning = nullptr);
	/* The choice alone, at that place: its size first */
	ImVec2 segments_size(std::span<const char *const> labels) const;
	bool segments(ImVec2 at, std::span<const char *const> labels, int &chosen, bool still = false) const;
	/* A slider alone at that place, as high as a line: its track from `low`
	 * to `high` by `step`, `shown` after it, which takes the room of the
	 * wider of it and `widest`; left and right of the gamepad move it a
	 * twentieth of the way while `focused`. True when it moved. */
	ImVec2 track_size(const char *shown, const char *widest) const;
	bool track(ImVec2 at, float &value, float low, float high, float step, const char *shown, bool focused,
	           bool still = false) const;
	void text(const char *name, const char *about, const char *value);
	/* A row with a value at its right and after it a button (true on a
	 * click), or a word in small capitals when there is no button; one the
	 * command line sets says so (`locked`), with no button */
	bool value(const char *name, const char *about, const char *value, const char *action, const char *word = nullptr,
	           const char *locked = nullptr);
	/* A row with a field at its right that the keyboard writes in, `most`
	 * characters at most, `hint` faint while it is empty (A of the gamepad
	 * puts the keyboard in it): true when the writing ends, by Enter or
	 * leaving the field, with the text changed */
	bool edit(const char *name, const char *about, std::string &text, const char *hint, size_t most,
	          const char *locked = nullptr);
	/* The same, its button opening a list under it: true when one is
	 * chosen */
	bool value_list(const char *name, const char *about, const char *value, const char *action,
	                std::span<const std::string> labels, int &chosen, const char *locked = nullptr);
	/* A row with a slider from `low` to `high` by `step`, `shown` after it
	 * (the value and its unit): true when it moved */
	bool slider(const char *name, const char *about, float &value, float low, float high, float step, const char *shown,
	            const char *locked = nullptr, bool off = false);
	/* A row whose control the caller draws, `control` wide and `control_h`
	 * high: the top left corner it gets, beside the texts or under them */
	ImVec2 custom_row(const char *name, const char *about, float control, float control_h);
	/* A title and a text under it, with no frame */
	void paragraph(const char *title, const char *text);
	/* A key drawn: its name in a cap, the outline clear when `used`, lit
	 * when held (`lit`); its size first */
	ImVec2 key_size(const char *name) const;
	void key(ImDrawList *list, ImVec2 at, const char *name, bool used, bool lit) const;
	/* A button of a gamepad drawn as a pill; its size first */
	ImVec2 pill_size(const char *name) const;
	void pill(ImDrawList *list, ImVec2 at, const char *name) const;
	/* What the page is doing, the size of a button, filled with the
	 * accent colour (Test mode) */
	void badge(ImDrawList *list, ImVec2 at, const char *label) const;
	/* A row of an action of Odin: its name, its keys (each the caps of a
	 * combination), its gamepad buttons as pills (none: a dash); those
	 * caught now (0 and 1 the keys, 2 the buttons, -1 none) say
	 * "press...". What is clicked: 0 to 2, -1 for none. */
	int shortcut(const char *name, std::span<const std::vector<std::string>> keys, std::span<const std::string> pad,
	             int catching);
	/* A line of such a list: its name with an icon before it, or colours
	 * in squares (a palette: none when see-through), what it is under it,
	 * dimmer, a word at its right (in a pill when `strong`) */
	struct Item {
		std::string name, about, tag;
		Icon icon = Icon::NONE;
		bool strong = false;
		std::array<ImU32, 4> colours{};
		bool off = false;	/* shown faint, not to be picked */
	};
	/* A row with a list that drops down from its right: true when the
	 * choice changed */
	bool dropdown(const char *name, const char *about, std::span<const std::string> labels, int &chosen);
	bool dropdown(const char *name, const char *about, std::span<const Item> items, int &chosen);
	/* The same list from a box alone at that place, that wide, an icon
	 * before what is chosen, a text at the foot of the list; its height */
	bool picker(const char *id, ImVec2 at, float width, std::span<const Item> items, int &chosen, Icon icon,
	            const char *foot) const;
	float picker_height() const;

	/* What a button is: plain (in the action colour, its outline fainter
	 * until hovered), Undo (in the ink, its outline clearer, until
	 * hovered), one that clears (as plain, in red) */
	enum class Tone { PLAIN, STRONG, DANGER };
	/* A button of a page, after the one before on its line (`same_line`),
	 * its icon before its label. True on a click. */
	bool button(const char *label, bool same_line = false, Icon icon = Icon::NONE, Tone tone = Tone::PLAIN) const;
	/* A button at that place, filled with the action colour while on; its
	 * size first. True on a click. */
	ImVec2 button_size(const char *label, Icon icon = Icon::NONE) const;
	bool button(const char *label, ImVec2 at, bool on, Icon icon = Icon::NONE, Tone tone = Tone::PLAIN) const;
	/* A round button with ‹ or › (`right`); its size first. True on a
	 * click. */
	ImVec2 arrow_size() const;
	bool arrow(const char *id, ImVec2 at, bool right) const;
	/* A word beside the mouse, framed as the lists are */
	void tip(const char *text) const;
	/* A word in small capitals, as the titles of the groups are; its width */
	static void small_caps(ImDrawList *list, ImVec2 at, ImU32 colour, const char *text);
	static float small_caps_width(const char *text);

private:
	/* The title of a group or of a paragraph, in small capitals, its room
	 * above and below */
	void group_title(const char *title);
	/* The top of the row, its line above it when it is not the first of
	 * its group, and the room left for its texts */
	ImVec2 begin_row(float &width);
	float texts(ImVec2 at, float width, const char *name, const char *about, const char *locked, bool off = false,
	            const char *warning = nullptr) const;
	/* The room the (?) of a row with a tip takes after its name */
	float ask_width() const;
	/* The texts of a row drawn, and the place of its control, `control`
	 * wide and `control_h` high: beside them, or under them where they
	 * would have too little room. The height of the row and the top of
	 * the control. */
	struct Placed {
		float h, top;
	};
	Placed place(ImVec2 at, float width, float control, float control_h, const char *name, const char *about,
	             const char *locked, bool off = false, const char *warning = nullptr) const;
	/* A button drawn and clicked, the gamepad aside */
	bool press_button(const char *label, ImVec2 at, bool on, Icon icon, Tone tone) const;
	/* The list that drops from a box or a button, at that place, as wide
	 * at least; over it (`above`: its top) where the window ends first */
	bool list_popup(ImVec2 at, float width, std::span<const Item> items, int &chosen, const char *foot = nullptr,
	                float above = -1.0f) const;
	/* The colours of a line of a list, from that place on a line: their
	 * width first (0: none), and what is left before the name */
	float colours_width(const Item &item) const;
	float before_name(const Item &item, Icon icon_shown) const;
	void colours(ImDrawList *list, ImVec2 at, const Item &item) const;

	const MenuPalette &m_palette;
	float m_scale;
	MenuFocus *m_focus;
	ImVec2 m_group_top = {};
	bool m_first_row = true;
	/* The tip next_tip gave, and the one of the row being drawn */
	std::string m_next_tip, m_row_tip;
	/* The last button a value row drew */
	ImVec2 m_button_a = {}, m_button_b = {};
};

} /* namespace odin */

#endif /* ODIN_MENU_KIT_HPP */

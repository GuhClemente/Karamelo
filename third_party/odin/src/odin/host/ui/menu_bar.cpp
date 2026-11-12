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

/* The bar over the game and the library in the
 * palette of the menu: the seven menus, the game and its speed at the right;
 * the rune and the name of Odin are those of the window.
 * The menu open has the accent under its name. */

#include "odin/host/ui/menu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "imgui/imgui.h"
/* Its menus closed from outside them, Dear ImGui's own code */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "imgui/imgui_internal.h"
#pragma GCC diagnostic pop
#include "odin/machine/chips/card_reader.hpp"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/app/version.hpp"
#include "odin/machine/cabinet/netbd_ring.hpp"

namespace odin {

namespace {

/* The width of a slider of a menu of the bar, in points */
constexpr float SLIDER_W = 150.0f;

/* In points: the room around a name of the bar, between two, around the
 * items of a menu; the cut of the corners of a menu; the line of the
 * accent under the menu open */
constexpr float NAME_WIDE = 7.0f;
constexpr float NAME_HIGH = 5.0f;
constexpr float NAME_GAP = 10.0f;
constexpr float ITEMS_INSIDE = 5.0f;
constexpr float ITEM_HIGH = 3.0f;
constexpr float MENU_CUT = 6.0f;
constexpr float ACCENT = 2.0f;
/* The bar lets the game show through, a little */
constexpr float BAR_ALPHA = 0.96f;
/* A note: its letters the same share of the height of the window at every
 * size (in steps of whole pixels, as the menus are, it looked a third
 * smaller at 1280 by 720 than at 1920 by 1080), never smaller than
 * those of the menus; from the bottom-left corner, the room around its
 * words and the cut of its corners, in letters */
constexpr float NOTE_LETTERS = 0.024f;
constexpr float NOTE_SMALLEST = 13.0f;
constexpr float NOTE_MARGIN = 0.92f;
constexpr float NOTE_WIDE = 0.77f;
constexpr float NOTE_HIGH = 0.46f;
constexpr float NOTE_CUT = 0.46f;

using Ask = BarState::Ask;
using Action = Hotkeys::Action;

/* The kinds of device whose inputs go back to their defaults or are
 * cleared, in the order of MenuState::Device */
constexpr const char *DEVICE_KINDS[] = { "Keyboard and mouse", "Controller", "Joystick" };

/* An item that asks something: true when clicked */
bool
item(const char *label, const std::string &key = {}, bool checked = false, bool enabled = true)
{
	return ImGui::MenuItem(label, key.empty() ? nullptr : key.c_str(), checked, enabled);
}

/* An item checked while `on`, which a click turns over */
void
toggle(const char *label, const std::string &key, bool &on, bool enabled = true)
{
	if (item(label, key, on, enabled))
		on = !on;
}

} /* namespace */

void
Menu::forget_input()
{
	ImGui::SetCurrentContext(m_imgui);
	ImGui::GetIO().ClearInputKeys();
	ImGui::GetIO().ClearInputMouse();
}

void
Menu::hide_bar()
{
	m_bar_hidden = true;
}

void
Menu::close_bar_menus()
{
	if (std::exchange(m_bar_hidden, false))
		ImGui::ClosePopupsExceptModals();
}

void
Menu::build_bar(BarState &state)
{
	begin_frame(state.theme);
	bar(state);
	ImGui::Render();
}

void
Menu::build_note(BarState &state)
{
	ImGui::SetCurrentContext(m_imgui);
	use_palette(state.theme);
	follow_scale();

	ImGuiIO &io = ImGui::GetIO();
	int w = 0, h = 0, pixels_w = 0, pixels_h = 0;
	const Uint64 now = SDL_GetTicksNS();

	SDL_GetWindowSize(m_window, &w, &h);
	SDL_GetWindowSizeInPixels(m_window, &pixels_w, &pixels_h);
	io.DisplaySize = { static_cast<float>(w), static_cast<float>(h) };
	if (w > 0 && h > 0)
		io.DisplayFramebufferScale = { static_cast<float>(pixels_w) / static_cast<float>(w),
		                               static_cast<float>(pixels_h) / static_cast<float>(h) };
	io.DeltaTime = m_note_time && now > m_note_time ? std::max(static_cast<float>(now - m_note_time) * 1e-9f, 1e-4f) :
	                                                  1.0f / 60.0f;
	m_note_time = now;
	add_fonts();
	ImGui::NewFrame();
	close_bar_menus();
	note(state, MenuKit(*m_palette, m_scale));
	ImGui::Render();
}

/* Over everything, in the bottom-left corner: its words in a frame as the
 * menus of the bar have, fading out */
void
Menu::note(const BarState &state, const MenuKit &kit)
{
	if (state.note.empty() || state.note_alpha <= 0.0f)
		return;

	const MenuPalette &p = kit.palette();
	const ImVec2 room = ImGui::GetIO().DisplaySize;
	const float size = std::max(NOTE_SMALLEST, std::round(room.y * NOTE_LETTERS));
	const float margin = std::round(size * NOTE_MARGIN), wide = std::round(size * NOTE_WIDE);
	const float high = std::round(size * NOTE_HIGH);
	const std::string shown = MenuKit::fitted(state.note, room.x - 2.0f * (margin + wide), size);
	const ImVec2 b = { margin + std::round(MenuKit::measure(shown.c_str(), size)) + 2.0f * wide, room.y - margin };
	const ImVec2 a = { margin, b.y - size - 2.0f * high };
	ImDrawList *const list = ImGui::GetForegroundDrawList();

	kit.frame(list, a, b, std::round(size * NOTE_CUT), MenuKit::alpha(p.screen, state.note_alpha),
	          MenuKit::alpha(p.outline, state.note_alpha));
	list->AddText(ImGui::GetFont(), size, { a.x + wide, a.y + high }, MenuKit::alpha(p.ink, state.note_alpha),
	              shown.c_str());
}

/* What the menus of the bar draw with */
struct Menu::BarItems {
	BarState &state;
	const MenuKit &kit;

	/* What is asked, and on which page */
	void ask(Ask what, int page = -1) const
	{
		state.ask = what;
		state.page = page;
	}
	/* The first key of an action, as odin.ini says it */
	const std::string &key(Action action) const { return state.keys[static_cast<size_t>(action)]; }
	/* A menu: a frame with its corners cut, behind its items */
	void framed() const
	{
		const ImVec2 a = ImGui::GetWindowPos(), b = { a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight() };

		kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(MENU_CUT), kit.palette().screen, kit.palette().outline);
	}
	/* A list in a menu of its own, the one chosen checked: whether another
	 * was chosen */
	bool choose(const char *label, std::span<const char *const> labels, int &chosen, bool enabled = true) const
	{
		bool changed = false;

		if (ImGui::BeginMenu(label, enabled)) {
			framed();
			for (size_t c = 0; c < labels.size(); c++)
				if (item(labels[c], {}, chosen == static_cast<int>(c)) && chosen != static_cast<int>(c)) {
					chosen = static_cast<int>(c);
					changed = true;
				}
			ImGui::EndMenu();
		}
		return changed;
	}
	/* A value in %, from 0 to `most`, its name above it: whether it moved */
	bool slider(const char *name, const char *id, int &value, int most) const
	{
		ImGui::TextUnformatted(name);
		ImGui::SetNextItemWidth(kit.px(SLIDER_W));
		return ImGui::SliderInt(id, &value, 0, most, "%d %%", ImGuiSliderFlags_AlwaysClamp);
	}
	/* A line to read only: what, and what it is */
	static void greyed(const char *what, const std::string &value) { ImGui::TextDisabled("%s: %s", what, value.c_str()); }
};

void
Menu::bar(BarState &state)
{
	const MenuKit kit(*m_palette, m_scale);
	const MenuPalette &p = kit.palette();
	ImGuiStyle &style = ImGui::GetStyle();
	const ImVec2 safe = style.DisplaySafeAreaPadding;
	const float line = ImGui::GetTextLineHeight();
	BarItems items{ state, kit };
	/* The menus of the bar, in their order */
	static constexpr struct {
		const char *name;
		void (Menu::*draw)(BarItems &);
	} MENUS[] = { { "FILE", &Menu::bar_file },   { "INPUT", &Menu::bar_input }, { "DISPLAY", &Menu::bar_display },
	              { "AUDIO", &Menu::bar_audio }, { "LINK", &Menu::bar_link },   { "DEBUG", &Menu::bar_debug },
	              { "ABOUT", &Menu::bar_about } };

	style.DisplaySafeAreaPadding = { 0.0f, 0.0f };
	const std::pair<ImGuiCol, ImU32> colours[] = {
		{ ImGuiCol_WindowBg, IM_COL32_BLACK_TRANS }, { ImGuiCol_MenuBarBg, MenuKit::alpha(p.screen, BAR_ALPHA) },
		{ ImGuiCol_PopupBg, IM_COL32_BLACK_TRANS }, { ImGuiCol_Border, IM_COL32_BLACK_TRANS },
		{ ImGuiCol_Header, p.hover }, { ImGuiCol_HeaderHovered, p.hover }, { ImGuiCol_HeaderActive, p.hover },
		{ ImGuiCol_Text, p.ink }, { ImGuiCol_TextDisabled, p.faint }, { ImGuiCol_Separator, p.line },
		{ ImGuiCol_CheckMark, p.accent }, { ImGuiCol_FrameBg, p.hover }, { ImGuiCol_FrameBgHovered, p.hover },
		{ ImGuiCol_FrameBgActive, p.hover }, { ImGuiCol_SliderGrab, p.accent },
		{ ImGuiCol_SliderGrabActive, p.accent }, { ImGuiCol_NavCursor, IM_COL32_BLACK_TRANS },
	};

	for (const auto &[which, colour] : colours)
		ImGui::PushStyleColor(which, colour);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { kit.px(NAME_WIDE), kit.px(NAME_HIGH) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { kit.px(NAME_GAP), kit.px(ITEM_HIGH) });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { std::round(kit.px(ITEMS_INSIDE)), std::round(kit.px(ITEMS_INSIDE)) });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);

	if (ImGui::BeginMainMenuBar()) {
		ImDrawList *const list = ImGui::GetWindowDrawList();
		const ImVec2 top = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
		const float bottom = top.y + size.y;
		int open = -1;

		list->AddLine({ top.x, bottom - kit.stroke() / 2.0f }, { top.x + size.x, bottom - kit.stroke() / 2.0f }, p.line,
		              kit.stroke());

		for (int m = 0; m < static_cast<int>(std::size(MENUS)); m++) {
			const float at = ImGui::GetCursorScreenPos().x + std::trunc(style.ItemSpacing.x * 0.5f);
			const float w = ImGui::CalcTextSize(MENUS[m].name).x;

			if (m == state.open_test) {
				state.open_test = -1;
				ImGui::OpenPopup(MENUS[m].name);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, p.dim);
			const bool shown = ImGui::BeginMenu(MENUS[m].name);
			ImGui::PopStyleColor();
			if (!shown)
				continue;
			open = m;
			list->AddLine({ at - style.ItemSpacing.x, bottom - kit.px(ACCENT) / 2.0f },
			              { at + w + style.ItemSpacing.x, bottom - kit.px(ACCENT) / 2.0f }, p.accent, kit.px(ACCENT));

			items.framed();
			(this->*MENUS[m].draw)(items);
			ImGui::EndMenu();
		}

		/* The game and its speed */
		char right[256];

		if (state.paused)
			std::snprintf(right, sizeof(right), "%s  ·  paused", state.title.c_str());
		else if (state.show_speed && state.fps > 0.0f)
			std::snprintf(right, sizeof(right), "%s  ·  %.0f fps", state.title.c_str(), static_cast<double>(state.fps));
		else
			std::snprintf(right, sizeof(right), "%s", state.title.c_str());

		const float room = top.x + size.x - kit.px(NAME_WIDE) - ImGui::GetCursorScreenPos().x;
		const std::string shown = MenuKit::fitted(right, room);

		list->AddText({ top.x + size.x - kit.px(NAME_WIDE) - MenuKit::measure(shown.c_str()),
		                top.y + (size.y - line) / 2.0f }, p.faint, shown.c_str());
		state.open = open >= 0;
		state.bottom = bottom;
		ImGui::EndMainMenuBar();
	}
	new_card(state, kit);
	note(state, kit);
	ImGui::PopStyleVar(8);
	ImGui::PopStyleColor(static_cast<int>(std::size(colours)));
	style.DisplaySafeAreaPadding = safe;
	state.wants_mouse = ImGui::GetIO().WantCaptureMouse;
}

void
Menu::bar_file(BarItems &b)
{
	BarState &state = b.state;

	if (item("Open game..."))
		b.ask(Ask::OPEN);
	if (item("Close game", b.key(Action::CLOSE_GAME), false, state.game))
		b.ask(Ask::CLOSE);
	if (item("Reset", b.key(Action::RESET), false, state.game))
		b.ask(Ask::RESET);
	ImGui::Separator();
	if (item("Save state", b.key(Action::SAVE_STATE), false,
	         state.game))
		b.ask(Ask::SAVE_STATE);
	if (item("Load state", b.key(Action::LOAD_STATE), false,
	         state.game && state.saved))
		b.ask(Ask::LOAD_STATE);
	ImGui::Separator();
	/* The player's card: in or out as at the cabinet; the
	 * one in hand while it is not in the reader */
	if (state.game && state.card_reader) {
		using Where = CardReader::Where;
		const auto where = static_cast<Where>(state.card_where);
		const bool at_slot = where == Where::AT_SLOT;

		if (item(at_slot ? "Remove card" : "Insert card", b.key(Action::CARD),
		         false, at_slot || (where == Where::IN_HAND && state.card_slot_open)))
			b.ask(Ask::CARD);
		if (ImGui::BeginMenu("Card in hand", where != Where::INSIDE && !at_slot)) {
			b.framed();
			if (item("None", {}, state.card < 0)) {
				state.card_chosen = -1;
				b.ask(Ask::CARD_CHOOSE);
			}
			for (size_t c = 0; c < state.cards.size(); c++)
				if (item(state.cards[c].c_str(), {}, state.card == static_cast<int>(c))) {
					state.card_chosen = static_cast<int>(c);
					b.ask(Ask::CARD_CHOOSE);
				}
			ImGui::EndMenu();
		}
		if (!state.card_keys.empty() &&
		    item("New card...", {}, false, where != Where::INSIDE && !at_slot))
			m_new_card = true;
		ImGui::Separator();
	}
	if (item("Settings...", b.key(Action::MENU)))
		b.ask(Ask::SETTINGS);
	ImGui::Separator();
	if (item("Quit", b.key(Action::QUIT)))
		b.ask(Ask::QUIT);
}

void
Menu::bar_input(BarItems &b)
{
	BarState &state = b.state;

	/* The inputs of the game that runs, for each of its
	 * players: the device they play with (checked: it alone
	 * shakes), the tuning of their gamepad, their inputs back
	 * to their defaults or cleared, kind by kind as the page
	 * Input shows them; the keys of each control stay in
	 * Settings */
	for (unsigned player = 0; player < state.players; player++) {
		char name[16];

		std::snprintf(name, sizeof(name), "Player %u", player + 1);
		if (!ImGui::BeginMenu(name, state.game))
			continue;
		b.framed();
		if (item(DEVICE_KINDS[0], {}, state.plays_with[player] == InputDevice::KEYBOARD) &&
		    state.plays_with[player] != InputDevice::KEYBOARD) {
			state.asked_player = player;
			b.ask(Ask::KEYBOARD);
		}
		if (state.pads.empty())
			ImGui::TextDisabled("No controller plugged in");
		for (size_t i = 0; i < state.pads.size(); i++) {
			const BarState::Pad &pad = state.pads[i];
			const bool mine = pad.player == static_cast<int>(player) &&
			                  state.plays_with[player] == (pad.joystick ? InputDevice::JOYSTICK : InputDevice::PAD);

			if (item(pad.label.c_str(), pad.off ? "not for this game" : "", mine, !pad.off) && !mine) {
				state.asked_player = player;
				state.asked_pad = static_cast<int>(i);
				b.ask(Ask::PAD);
			}
		}
		if (state.dead_zone[player] >= 0 && state.plays_with[player] == InputDevice::PAD) {
			ImGui::Separator();
			b.slider("Stick dead zone", "##dead", state.dead_zone[player], PadTuning::MOST_DEAD_ZONE);
			b.slider("Trigger threshold", "##trigger", state.trigger_threshold[player],
			       PadTuning::MOST_TRIGGER_THRESHOLD);
		}
		ImGui::Separator();
		/* All the inputs of the player, of every device; Clear all leaves
		 * the menu open: Undo shows at once, for its few seconds */
		for (const bool to_defaults : { true, false }) {
			ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, to_defaults);
			if (item(to_defaults ? "Restore defaults" : "Clear all")) {
				state.asked_player = player;
				b.ask(to_defaults ? Ask::INPUTS_DEFAULTS : Ask::INPUTS_CLEAR);
			}
			ImGui::PopItemFlag();
		}
		if (state.undo && item("Undo clear all"))
			b.ask(Ask::INPUTS_UNDO);
		ImGui::EndMenu();
	}
	for (unsigned player = 0; player < state.players; player++) {
		char name[16];

		std::snprintf(name, sizeof(name), "Light gun %u", player + 1);
		if (!state.sights[player] || !ImGui::BeginMenu(name, state.game))
			continue;
		b.framed();
		if (item(player ? "None: player 2 does not aim" : "None: the mouse aims", {},
		         state.gun[player].empty()) && !state.gun[player].empty()) {
			state.asked_player = player;
			state.asked_gun = -1;
			b.ask(Ask::GUN);
		}
		for (size_t g = 0; g < state.guns.size(); g++) {
			const LightGun &gun = state.guns[g];
			const std::string label = gun.name + (gun.aims ? "" : " · mouse") +
			                          (gun.present ? "" : " · unplugged");
			const bool mine = gun.identity == state.gun[player];

			if (item(label.c_str(), {}, mine, gun.present || mine) && !mine) {
				state.asked_player = player;
				state.asked_gun = static_cast<int>(g);
				b.ask(Ask::GUN);
			}
		}
		ImGui::EndMenu();
	}
	if (state.can_shake)
		toggle("Vibration", {}, state.shake, state.game);
	toggle("Hide cursor", b.key(Action::LIGHT_GUN), state.hide_cursor, state.game);
	ImGui::Separator();
	if (ImGui::BeginMenu("Hotkeys")) {
		b.framed();
		if (item("Restore defaults"))
			b.ask(Ask::HOTKEYS_DEFAULTS);
		ImGui::EndMenu();
	}
	toggle("Send outputs", {}, state.network);
}

void
Menu::bar_display(BarItems &b)
{
	BarState &state = b.state;

	int window = state.custom_window ? CUSTOM : static_cast<int>(state.window) - 1;
	int scale = static_cast<int>(state.scale) - 1, mode = static_cast<int>(state.upscale_mode);
	int filter = anisotropy_index(state.anisotropy), sync = static_cast<int>(state.vsync);
	int pace = state.unthrottled ? 1 : 0, api = static_cast<int>(state.api);
	int theme = static_cast<int>(state.theme), sides = static_cast<int>(state.sides);

	if (item("Full screen", b.key(Action::FULLSCREEN), state.fullscreen))
		b.ask(Ask::FULLSCREEN);
	/* The sizes of the window; one given by hand is kept,
	 * shown, and set in Settings */
	if (ImGui::BeginMenu("Window size", !state.window_given && !state.fullscreen)) {
		b.framed();
		for (int sized = 0; sized < CUSTOM; sized++)
			if (item(WINDOW_SIZES[sized], {}, window == sized) && window != sized) {
				state.window = static_cast<unsigned>(sized) + 1;
				state.custom_window = false;
			}
		if (state.custom_window)
			item("Custom (in Settings)", {}, true, false);
		ImGui::EndMenu();
	}
	ImGui::Separator();
	if (b.choose("Graphics API", APIS_SHOWN, api))
		state.api = static_cast<unsigned>(api);
	if (state.picks_gpu() && ImGui::BeginMenu("Graphics card")) {
		const bool known = std::find(state.gpus.begin(), state.gpus.end(), state.gpu_chosen) != state.gpus.end();

		b.framed();
		if (item("Automatic", {}, state.gpu_chosen.empty()))
			state.gpu_chosen.clear();
		for (const std::string &card : state.gpus)
			if (item(card.c_str(), {}, state.gpu_chosen == card))
				state.gpu_chosen = card;
		/* Named in the settings, not in this computer */
		if (!known && !state.gpu_chosen.empty())
			item(state.gpu_chosen.c_str(), {}, true, false);
		ImGui::EndMenu();
	}
	if (b.choose("Internal resolution", SCALES, scale, !state.scale_given))
		state.scale = static_cast<unsigned>(scale) + 1;
	if (b.choose("Upscaling mode", UPSCALE_MODES_SHOWN, mode))
		state.upscale_mode = static_cast<unsigned>(mode);
	if (b.choose("Anisotropic filtering", FILTERS, filter, !state.anisotropy_given))
		state.anisotropy = ANISOTROPY[filter];
	if (b.choose("Vertical sync", SYNCS, sync, !state.vsync_locked))
		state.vsync = static_cast<unsigned>(sync);
	ImGui::Separator();
	if (b.choose("Bezel", SIDES_SHOWN, sides))
		state.sides = static_cast<unsigned>(sides);
	/* The screens of the cabinets; another filter odin.ini
	 * names is shown by its name, taken before a click
	 * changes it ("" for None has no name to show) */
	if (ImGui::BeginMenu("CRT screen")) {
		bool known = false;

		for (const char *preset : PRESETS)
			known = known || state.filter == preset;
		const std::string other = known ? std::string() : state.filter;

		b.framed();
		for (size_t s = 0; s < std::size(PRESETS); s++)
			if (item(SCREENS[s], {}, state.filter == PRESETS[s]))
				state.filter = PRESETS[s];
		if (!other.empty())
			item(other.c_str(), {}, true, false);
		ImGui::EndMenu();
	}
	if (b.choose("Speed limit (next launch)", PACES, pace, !state.unthrottled_given))
		state.unthrottled = pace == 1;
	toggle("Show FPS", {}, state.show_speed);
	ImGui::Separator();
	if (ImGui::BeginMenu("Menu theme")) {
		b.framed();
		for (size_t t = 0; t < std::size(THEMES); t++)
			if (item(THEMES[t].name, {}, theme == static_cast<int>(t)))
				state.theme = static_cast<unsigned>(t);
		ImGui::EndMenu();
	}
	ImGui::Separator();
	b.greyed(state.picks_gpu() ? "In use" : "Graphics card", state.gpu);
}

void
Menu::bar_audio(BarItems &b)
{
	BarState &state = b.state;
	const MenuKit &kit = b.kit;

	int volume = static_cast<int>(state.volume);

	ImGui::BeginDisabled(state.silent);
	if (b.slider("Volume", "##volume", volume, 100))
		state.volume = static_cast<unsigned>(volume);
	toggle("Sound in the background", {}, state.background);
	ImGui::EndDisabled();
	ImGui::Separator();
	/* Where the sound plays: the system's device, or one
	 * of those it has (one kept and gone stays listed,
	 * taken before a click changes it) */
	if (ImGui::BeginMenu("Device", !state.silent)) {
		bool listed = state.device.empty();

		for (const std::string &device : state.devices)
			listed = listed || device == state.device;
		const std::string gone = listed ? std::string() : state.device;

		b.framed();
		if (item("System default", {}, state.device.empty()))
			state.device.clear();
		for (const std::string &device : state.devices)
			if (item(device.c_str(), {}, device == state.device))
				state.device = device;
		if (!gone.empty())
			item(gone.c_str(), {}, true, false);
		ImGui::EndMenu();
	}
	/* The game that runs, when it has an optional sound
	 * board or a woofer, that game alone: how it plays, and
	 * its level, with Both */
	for (SoundGame &game : state.sound_games) {
		if (!ImGui::BeginMenu(game.title.c_str()))
			continue;
		b.framed();
		ImGui::BeginDisabled(game.locked);
		for (int how = 0; how < static_cast<int>(std::size(PLAYED)); how++)
			if (item(PLAYED[how], {}, game.played == how))
				game.played = how;
		ImGui::EndDisabled();
		/* To or from Quad waits for the game to start again */
		if (game.later)
			ImGui::TextDisabled("Reset the game to apply");
		ImGui::Separator();
		ImGui::BeginDisabled(game.played != 1);
		ImGui::TextUnformatted(game.level_name);
		ImGui::SetNextItemWidth(kit.px(SLIDER_W));
		/* The level as the page says it: "-12 dB", "0 dB", "+3 dB" */
		char db[16];

		std::snprintf(db, sizeof(db), "%s%.0f dB", game.level > 0.5f ? "+" : "", static_cast<double>(game.level) + 0.0);
		if (ImGui::SliderFloat("##level", &game.level, -60.0f, 12.0f, db, ImGuiSliderFlags_AlwaysClamp))
			game.level = std::round(game.level);
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}
}

void
Menu::bar_link(BarItems &b)
{
	BarState &state = b.state;

	/* The network board, a menu of its own, at the next
	 * launch; the address of the next cabinet is typed in
	 * Settings */
	using Board = NetLinkSettings::Board;
	const bool on = state.link_board == static_cast<int>(Board::RING);

	if (item("Link cabinets", {}, on, !state.link_given))
		state.link_board = static_cast<int>(on ? Board::NONE : Board::RING);
	if (ImGui::BeginMenu("This cabinet", on && !state.link_given)) {
		b.framed();
		for (unsigned n = 1; n <= NetLinkSettings::MOST_CABINETS; n++) {
			const std::string number = cabinet_number(n);

			if (item(number.c_str(), {}, state.link_cabinet == n))
				state.link_cabinet = n;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Port", on && !state.link_given)) {
		b.framed();
		for (unsigned port = NetCable::PORT; port < NetCable::PORT + LINK_PORTS; port++)
			if (item(std::to_string(port).c_str(), {}, state.link_port == port))
				state.link_port = port;
		/* Another port odin.ini gives, shown */
		if (state.link_port < NetCable::PORT || state.link_port >= NetCable::PORT + LINK_PORTS)
			item(std::to_string(state.link_port).c_str(), {}, true, false);
		ImGui::EndMenu();
	}
	ImGui::Separator();
	b.greyed("Next cabinet", state.link_next.empty() ? std::string("none (set it in Settings)") : state.link_next);
	ImGui::TextDisabled(state.link_given ? "Set by ODIN_NETBD" :
	                    state.gaelco     ? "Applies to the games that link: this one plays alone" :
	                                       "Applies when a game starts");
}

void
Menu::bar_debug(BarItems &b)
{
	BarState &state = b.state;

	if (item("Pause", b.key(Action::PAUSE), state.paused, state.game))
		b.ask(Ask::PAUSE);
	toggle("Detailed log (next launch)", {}, state.verbose, !state.verbose_given);
}

void
Menu::bar_about(BarItems &b)
{
	/* The page About, as Settings */
	if (item("About Odin..."))
		b.ask(Ask::SETTINGS, ABOUT);
	if (*odin_version()) {
		ImGui::Separator();
		ImGui::TextDisabled("%s", odin_version());
	}
}

/* New card...: the pilot's name as the keyboard of the terminal of the game
 * types it (BarState::card_keys, the space, capitals), then the card made
 * (CARD_NEW). While it is open, the bar stays and the keys are its own. */
void
Menu::new_card(BarState &state, const MenuKit &kit)
{
	const MenuPalette &p = kit.palette();
	/* The characters the field takes */
	const ImGuiInputTextCallback typed = [](ImGuiInputTextCallbackData *data) {
		const std::string &keys = *static_cast<const std::string *>(data->UserData);
		const ImWchar c = data->EventChar;

		return c == ' ' || (c < 0x80 && keys.find(static_cast<char>(c)) != std::string::npos) ? 0 : 1;
	};

	if (std::exchange(m_new_card, false)) {
		m_card_name.fill(0);
		ImGui::OpenPopup("New card");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
	ImGui::PushStyleColor(ImGuiCol_Button, p.hover);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.outline);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.accent);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { kit.px(14.0f), kit.px(12.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { kit.px(8.0f), kit.px(8.0f) });
	if (ImGui::BeginPopupModal("New card", nullptr,
	                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) {
		const ImVec2 a = ImGui::GetWindowPos(), b = { a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight() };
		std::string symbols;

		for (const char c : state.card_keys)
			if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'Z'))
				symbols += c;
		kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(MENU_CUT), p.screen, p.outline);
		ImGui::TextUnformatted("New card");
		ImGui::TextDisabled("The pilot's name, as the game's terminal accepts it:");
		ImGui::TextDisabled("Up to 8 characters: A-Z, 0-9, space and %s", symbols.c_str());
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(kit.px(180.0f));

		const bool entered = ImGui::InputText("##name", m_card_name.data(), m_card_name.size(),
		                                      ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CallbackCharFilter |
		                                              ImGuiInputTextFlags_EnterReturnsTrue,
		                                      typed, &state.card_keys);
		const std::string_view name(m_card_name.data());
		const bool named = name.find_first_not_of(' ') != std::string_view::npos;

		ImGui::BeginDisabled(!named);
		if (ImGui::Button("Create card") || (entered && named)) {
			state.card_name = name;
			state.ask = BarState::Ask::CARD_NEW;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		state.open = true;
	}
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(3);
}

} /* namespace odin */

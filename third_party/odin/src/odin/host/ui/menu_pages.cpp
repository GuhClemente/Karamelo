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

/* The pages Audio, Hotkeys, Outputs, Link and Paths of the menu */

#include "odin/host/ui/menu.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui/imgui.h"
#include "odin/common/env.hpp"
#include "odin/host/input/gun_serial.hpp"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/ui/sound_view.hpp"
#include "odin/machine/cabinet/netbd_ring.hpp"

namespace odin {

namespace {

/* A page in two: what it shows in a box at the left, its settings at the
 * right, as 1 to 1.15; the room between them and below the
 * row of the games */
constexpr float LEFT_SHARE = 1.0f / 2.15f;
constexpr float SPLIT_GAP = 10.0f;
constexpr float ROW_GAP = 6.0f;

/* An output: its width at the least, the side of its icon, the room in it
 * and between two */
constexpr float TILE_LEAST = 110.0f;
constexpr float TILE_ICON = 26.0f;
constexpr float TILE_PAD = 8.0f;
constexpr float TILE_GAP = 8.0f;

/* The box at the left, from the top of the page to its bottom, framed:
 * its corners. The settings then go into begin_settings, at its right. */
struct Box {
	ImVec2 a, b;
};

Box
left_box(const MenuKit &kit, ImVec2 top, ImVec2 room)
{
	const Box box = { top, { top.x + (room.x - kit.px(SPLIT_GAP)) * LEFT_SHARE, top.y + room.y } };

	kit.frame(ImGui::GetWindowDrawList(), box.a, box.b, kit.px(MenuKit::BOX_CUT), 0, kit.palette().line);
	return box;
}

void
begin_settings(const MenuKit &kit, const Box &box, ImVec2 top, ImVec2 room)
{
	const float x = box.b.x + kit.px(SPLIT_GAP);

	ImGui::SetCursorScreenPos({ x, top.y });
	ImGui::BeginChild("settings", { top.x + room.x - x, room.y }, ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoBackground);
}

/* The page under them is as high as the room, with the room Menu::build
 * leaves under a page: it does not scroll */
void
end_settings(const MenuKit &kit, ImVec2 top, ImVec2 room)
{
	ImGui::Dummy({ 0.0f, kit.px(MenuKit::PAGE_FOOT) });
	ImGui::EndChild();
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ room.x, room.y - kit.px(MenuKit::PAGE_FOOT) - ImGui::GetStyle().ItemSpacing.y });
}

/* A text on as many lines as that width takes, each in its middle, from
 * `top`: its height. Drawn when `colour` is not 0. */
float
centred(ImVec2 top, float width, float size, const char *text, ImU32 colour)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	ImFont *const font = ImGui::GetFont();
	const char *const end = text + std::strlen(text);
	float y = top.y;

	for (const char *line = text; line < end;) {
		const char *cut = font->CalcWordWrapPosition(size, line, end, width);

		if (cut == line)
			cut++;

		const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line, cut).x;

		if (colour)
			list->AddText(font, size, { top.x + (width - w) / 2.0f, y }, colour, line, cut);
		y += size;
		line = cut;
		while (line < end && *line == ' ')
			line++;
	}
	return y - top.y;
}

/* What a box shows, `high` high: in its middle, or from its top in a
 * child that scrolls where it is higher than the box.
 * `draw` takes the top, and draws in the list of the window it is in. */
template <typename Draw>
void
in_box(const MenuKit &kit, const Box &box, float high, Draw draw)
{
	const float inside = kit.px(MenuKit::BOX_INSIDE);
	const ImVec2 a = { box.a.x + 1.0f, box.a.y + 1.0f }, size = { box.b.x - box.a.x - 2.0f, box.b.y - box.a.y - 2.0f };

	if (high <= size.y - 2.0f * inside) {
		ImGui::GetWindowDrawList()->PushClipRect(box.a, box.b, true);
		draw((box.a.y + box.b.y - high) / 2.0f);
		ImGui::GetWindowDrawList()->PopClipRect();
		return;
	}
	ImGui::SetCursorScreenPos(a);
	ImGui::BeginChild("drawing", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

	const float top = ImGui::GetCursorScreenPos().y + inside;

	draw(top);
	ImGui::SetCursorScreenPos({ a.x, top });
	ImGui::Dummy({ 1.0f, high + inside });
	ImGui::EndChild();
}

MenuKit::Icon
icon_of(GameOutput::Role role)
{
	switch (role) {
	case GameOutput::Role::LOCK:
		return MenuKit::Icon::LOCK;
	case GameOutput::Role::RUMBLE:
		return MenuKit::Icon::RUMBLE;
	case GameOutput::Role::MUTE:
		return MenuKit::Icon::MUTE;
	case GameOutput::Role::COUNTER:
		return MenuKit::Icon::COIN;
	default:
		return MenuKit::Icon::LAMP;
	}
}

/* The outputs of the cabinet as tiles, as many in a row as fit, an output
 * on lit with the accent; what they are under them; all in the middle of
 * the box */
void
output_tiles(const MenuKit &kit, const MenuState &state, const Box &box)
{
	const MenuPalette &palette = kit.palette();
	const float inside = kit.px(MenuKit::BOX_INSIDE), gap = kit.px(TILE_GAP), pad = kit.px(TILE_PAD), side = kit.px(TILE_ICON);
	const float width = box.b.x - box.a.x - 2.0f * inside, size = MenuKit::font(MenuKit::TINY);
	const float small = MenuKit::font(MenuKit::SMALL);
	const size_t columns = std::max<size_t>(1, static_cast<size_t>((width + gap) / (kit.px(TILE_LEAST) + gap)));
	const size_t rows = (state.outputs.size() + columns - 1) / columns;
	const float tile_w = (width - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns);
	const char caption[] = "The cabinet's outputs, lit when the game turns them on.";
	float names = 0.0f;

	for (const MenuState::Output &output : state.outputs)
		names = std::max(names, centred({}, tile_w - 2.0f * pad, size, output.name.c_str(), 0));

	const float tile_h = 2.0f * pad + side + kit.px(4.0f) + names;
	const float tiles_h = static_cast<float>(rows) * tile_h + static_cast<float>(rows ? rows - 1 : 0) * gap;
	const float caption_h = centred({}, width, small, caption, 0);
	const float all_h = tiles_h + (rows ? gap : 0.0f) + caption_h;

	in_box(kit, box, all_h, [&](float top) {
		ImDrawList *const list = ImGui::GetWindowDrawList();

		for (size_t i = 0; i < state.outputs.size(); i++) {
			const MenuState::Output &output = state.outputs[i];
			const ImVec2 a = { box.a.x + inside + static_cast<float>(i % columns) * (tile_w + gap),
			                   top + static_cast<float>(i / columns) * (tile_h + gap) };
			kit.frame(list, a, { a.x + tile_w, a.y + tile_h }, kit.px(MenuKit::BOX_CUT), output.on ? palette.hover : 0,
			               output.on ? palette.accent : palette.line);
			MenuKit::icon(list, icon_of(output.role), { a.x + (tile_w - side) / 2.0f, a.y + pad }, side,
			              output.on ? palette.accent : palette.stroke);
			centred({ a.x + pad, a.y + pad + side + kit.px(4.0f) }, tile_w - 2.0f * pad, size, output.name.c_str(),
			        output.on ? palette.accent : palette.dim);
		}
		centred({ box.a.x + inside, top + tiles_h + (rows ? gap : 0.0f) }, width, small, caption, palette.faint);
	});
}

/* What Odin sets in the game shown for this cabinet as it starts
 * (Hikaru::take_cabinet), or why it cannot */
std::string
link_set(const MenuState &state, bool on)
{
	if (!on)
		return "The link is off: Odin sets NO LINK.";
	if (state.link_cabinet > state.link_shown_cabinets)
		return "This game links up to " + std::to_string(state.link_shown_cabinets) + " cabinets: choose 1 to " +
		       std::to_string(state.link_shown_cabinets) + ".";

	std::string set = state.link_cabinet == 1 ? "This Odin: master" : "This Odin: slave";

	if (state.link_shown_seat == MenuState::SEAT_LETTER)
		set += ", seat " + std::string(1, static_cast<char>('A' + state.link_cabinet - 1));
	else if (state.link_shown_seat == MenuState::SEAT_NUMBER)
		set += ", seat " + std::to_string(state.link_cabinet);
	return set + ".";
}

/* The cabinets of a link of the game shown as tiles: each its number,
 * master or slave, its seat; this Odin's in the colour of a choice when
 * the link is on, all dim when it is off. Under them what Odin sets in
 * this cabinet; the frames go from each to the next, from the last back
 * to the first. A game that links none says so. */
void
link_tiles(const MenuKit &kit, const MenuState &state, const Box &box, bool on)
{
	const MenuPalette &palette = kit.palette();
	const unsigned count = state.link_shown_cabinets;
	const float inside = kit.px(MenuKit::BOX_INSIDE), gap = kit.px(TILE_GAP), pad = kit.px(TILE_PAD);
	const float width = box.b.x - box.a.x - 2.0f * inside, size = MenuKit::font(MenuKit::SMALL);
	const float tiny = MenuKit::font(MenuKit::TINY);

	if (count == 0) {
		const std::string none = (state.link_shown_title.empty() ? std::string("This game") : state.link_shown_title) +
		                         " cannot be linked: it has no network board.";

		in_box(kit, box, centred({}, width, size, none.c_str(), 0), [&](float top) {
			centred({ box.a.x + inside, top }, width, size, none.c_str(), palette.dim);
		});
		return;
	}

	/* As many in each row as the rows take: 4 as 2 and 2, not 3 and 1 */
	const size_t most = std::min<size_t>(count, std::max<size_t>(1, static_cast<size_t>((width + gap) /
	                                                                                       (kit.px(TILE_LEAST) + gap))));
	const size_t rows = (count + most - 1) / most, columns = (count + rows - 1) / rows;
	const float tile_w = (width - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns);
	const float line = ImGui::GetTextLineHeight();
	const float tile_h = 2.0f * pad + line + 2.0f * tiny * 1.3f;
	const std::string set = link_set(state, on);
	const char ring[] = "Each cabinet sends its data to the next, and the last one to cabinet 1.";
	const float tiles_h = static_cast<float>(rows) * tile_h + static_cast<float>(rows - 1) * gap;
	const float set_h = centred({}, width, size, set.c_str(), 0), ring_h = centred({}, width, tiny, ring, 0);

	in_box(kit, box, tiles_h + gap + set_h + ring_h, [&](float top) {
		ImDrawList *const list = ImGui::GetWindowDrawList();

		for (unsigned n = 1; n <= count; n++) {
			const size_t i = n - 1;
			const bool mine = on && n == state.link_cabinet;
			const ImVec2 a = { box.a.x + inside + static_cast<float>(i % columns) * (tile_w + gap),
			                   top + static_cast<float>(i / columns) * (tile_h + gap) };
			const std::string number = "Cabinet " + std::to_string(n);
			std::string seat = n == 1 ? "master" : "slave";

			if (state.link_shown_seat == MenuState::SEAT_LETTER)
				seat += " · seat " + std::string(1, static_cast<char>('A' + n - 1));
			else if (state.link_shown_seat == MenuState::SEAT_NUMBER)
				seat += " · seat " + std::to_string(n);
			kit.frame(list, a, { a.x + tile_w, a.y + tile_h }, kit.px(MenuKit::BOX_CUT), mine ? palette.hover : 0,
			          mine ? palette.action : palette.line);
			centred({ a.x + pad, a.y + pad }, tile_w - 2.0f * pad, MenuKit::font(1.0f), number.c_str(),
			        mine ? palette.action : on ? palette.ink : palette.faint);
			centred({ a.x + pad, a.y + pad + line }, tile_w - 2.0f * pad, tiny, seat.c_str(),
			        on ? palette.dim : palette.faint);
			if (mine)
				centred({ a.x + pad, a.y + pad + line + tiny * 1.3f }, tile_w - 2.0f * pad, tiny, "this Odin",
				        palette.action);
		}
		centred({ box.a.x + inside, top + tiles_h + gap }, width, size, set.c_str(), on ? palette.ink : palette.dim);
		centred({ box.a.x + inside, top + tiles_h + gap + set_h }, width, tiny, ring, palette.faint);
	});
}

} /* namespace */

std::string
shown_path(const std::string &path, const char *none)
{
	if (path.empty())
		return none;
#ifdef _WIN32
	std::string out = path;

	std::replace(out.begin(), out.end(), '/', '\\');
	return out;
#else
	const std::string home = home_dir();

	if (!home.empty() && path.starts_with(home) && (path.size() == home.size() || path[home.size()] == '/'))
		return "~" + path.substr(home.size());
	return path;
#endif
}

namespace {

/* The room between the frame of the drawing and its title */
constexpr float CARD_PAD = 8.0f;

/* What sets the level of the optional board on a cabinet, said in the tip
 * of its level (the manuals and wiring diagrams of SEGA) */
struct Knob {
	const char *game, *knob;
};

constexpr Knob KNOBS[] = {
	{ "sgnascar", ", like the cabinet's REAR volume knob" },
	{ "sgnascaro", ", like the cabinet's REAR volume knob" },
	{ "swracer", ". On the cabinet it only shakes the panel: you feel it but don't hear it, so Main is the default" },
};

const char *
knob_of(const std::string &game)
{
	for (const Knob &knob : KNOBS)
		if (game == knob.game)
			return knob.knob;
	return "";
}

/* A level as the cabinet says it: "-12 dB", "0 dB" (never "-0"), "+3 dB" */
std::string
decibels(float db)
{
	char text[16];

	std::snprintf(text, sizeof(text), "%s%.0f dB", db > 0.5f ? "+" : "", static_cast<double>(db) + 0.0);
	return text;
}

/* The sound the game shown has to set, if any */
SoundGame *
sound_shown(MenuState &state)
{
	if (state.shown >= state.games.size())
		return nullptr;
	for (SoundGame &game : state.sound_games)
		if (game.name == state.games[state.shown].first)
			return &game;
	return nullptr;
}

/* The game shown in the drawing: the parts of its cabinet, of the model
 * chosen on the page Input; how it is played; what was pointed at */
SoundScene
scene_of(const MenuState &state, const SoundGame *game)
{
	SoundScene scene;

	if (state.shown < state.game_audio.size()) {
		const MenuState::GameAudio &audio = state.game_audio[state.shown];
		const bool chosen = state.cabinet >= 0 && static_cast<size_t>(state.cabinet) < state.cabinet_ids.size();
		const std::string cabinet = chosen ? state.cabinet_ids[static_cast<size_t>(state.cabinet)] : "standard";

		for (const SoundPart &part : audio.parts)
			if (part.cabinet.empty() || part.cabinet == cabinet)
				scene.parts.push_back(part);
		scene.seated = audio.seated && std::find(audio.standing.begin(), audio.standing.end(), cabinet) ==
		                                   audio.standing.end();
		scene.family = audio.family;
	}
	if (game) {
		const bool unwired = std::any_of(scene.parts.begin(), scene.parts.end(), [](const SoundPart &part) {
			return part.option && part.kind == SoundPart::Kind::UNWIRED;
		});

		scene.played = unwired && !game->locked ? 0 : game->played;
		scene.lows = game->woofer && game->played == 1 && std::string_view(WOOFER_CUTOFFS[game->cutoff]) != "off";
	}
	scene.pointed = state.audio_pointed < static_cast<int>(scene.parts.size()) ? state.audio_pointed : -1;
	scene.pointed_speakers = state.audio_pointed_speakers;
	return scene;
}

/* The cabinet of the game shown with the parts of its sound, the model of
 * the cabinet on its right; under it, the name of the part pointed at, or
 * what the user hears */
void
sound_drawing(const MenuKit &kit, MenuState &state, const Box &box, const SoundScene &scene)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float inside = kit.px(MenuKit::BOX_INSIDE), pad = kit.px(CARD_PAD), size = MenuKit::font(MenuKit::SMALL);
	const float tiny = MenuKit::font(MenuKit::TINY), width = box.b.x - box.a.x - 2.0f * inside;
	const bool pointing = scene.pointed >= 0;
	const std::string caption =
		pointing ? sound_part_name(scene.parts[static_cast<size_t>(scene.pointed)]) : sound_heard(scene);
	/* Room for three lines, so that the drawing stays where it is */
	const float caption_h = std::max(centred({}, width, size, caption.c_str(), 0), 3.0f * size);
	const float title_h = kit.px(6.0f) + MenuKit::font(MenuKit::SMALL);

	MenuKit::write(list, { box.a.x + pad, box.a.y + kit.px(6.0f) + size - tiny }, palette.cabinet, "THE CABINET", tiny,
	               tiny * 0.14f);
	if (state.cabinet_names.size() > 1 && state.cabinet >= 0 &&
	    static_cast<size_t>(state.cabinet) < state.cabinet_names.size()) {
		const std::string model = MenuKit::capitals(state.cabinet_names[static_cast<size_t>(state.cabinet)]);
		const float w = MenuKit::measure(model.c_str(), tiny, tiny * 0.14f);

		MenuKit::write(list, { box.b.x - pad - w, box.a.y + kit.px(6.0f) + size - tiny }, palette.dim, model.c_str(),
		               tiny, tiny * 0.14f);
	}

	const ImVec2 a = { box.a.x + inside, box.a.y + title_h };
	const ImVec2 b = { box.b.x - inside, box.b.y - inside - caption_h };

	state.audio_pointed = draw_sound_cabinet(kit, a, b, scene);
	centred({ a.x, b.y }, width, size, caption.c_str(), pointing ? palette.ink : palette.dim);
}

/* The sound of the game shown: how it is played, the level of its
 * optional board or woofer, the cutoff of the woofer, the volume of a new
 * Smashing Drive board */
void
cabinet_sound(MenuKit &kit, SoundGame &game, const SoundScene &scene)
{
	static constexpr const char *CUTOFFS[] = { "80 Hz", "120 Hz", "160 Hz", "200 Hz", "Off" };
	static_assert(std::size(CUTOFFS) == std::size(WOOFER_CUTOFFS));
	static constexpr const char *STARTS[] = { "0 (silent)", "25", "50", "75", "96 (max)" };
	static_assert(std::size(STARTS) == std::size(START_VOLUMES));
	const char *const only_both = game.played != 1 ? "Both mode only" : nullptr;
	const std::string level = decibels(game.level);

	ImGui::PushID(game.name.c_str());
	kit.begin_group("CABINET SOUND");
	if (game.mixed) {
		const auto has = [&](auto is) { return std::any_of(scene.parts.begin(), scene.parts.end(), is); };
		const bool speakers = has([](const SoundPart &part) {
			return part.option && part.kind == SoundPart::Kind::SPEAKER;
		});
		const bool fourth = has([](const SoundPart &part) { return part.kind == SoundPart::Kind::UNWIRED; });
		/* A cabinet that does not wire the optional board plays the main one */
		const bool unwired = has([](const SoundPart &part) {
			return part.option && part.kind == SoundPart::Kind::UNWIRED;
		});
		const std::string tip =
			unwired ? "This cabinet does not wire the optional board: only the main board is heard." :
			game.woofer ? std::string("Main: without the woofer. Both: with its lows, mixed into your two speakers. "
			                          "Quad: with the woofer") +
			                      (fourth ? " and the fourth channel" : "") + ", on a rear pair of speakers." :
			speakers ? "Main: without the seat speakers. Both: with them, mixed into your two speakers. Quad: with "
			           "them, on a rear pair of speakers." :
			           "Main: without the panel shaker's signal. Both: with it, mixed into your two speakers. Quad: "
			           "with it, on a rear pair of speakers.";

		int main_only = 0;
		int &played = unwired && !game.locked ? main_only : game.played;
		const char *const locked = game.locked ? "set on the command line: -o" :
		                           unwired     ? "not wired on this cabinet" :
		                                         nullptr;

		kit.next_tip(tip.c_str());
		kit.choice("Mode", nullptr, Menu::PLAYED, played, locked, false,
		           game.later ? "Reset the game to apply" : nullptr);
		if (game.woofer) {
			kit.next_tip("How loud the woofer is in your two speakers, from -60 to +12 dB.");
			kit.slider("Woofer", only_both, game.level, -60.0f, 12.0f, 1.0f, level.c_str(), nullptr, game.played != 1);
			kit.next_tip("Only the woofer's sounds below this frequency are mixed in, as the cabinet's woofer plays "
			             "only its lows. No manual gives the frequency: 120 Hz is a guess. Off: the whole woofer "
			             "channel, which carries much of the game's sound.");
			kit.choice("Woofer cutoff", only_both, CUTOFFS, game.cutoff, nullptr, game.played != 1);
		} else if (!unwired) {
			const std::string about =
				std::string(speakers ? "How loud the seat speakers are" : "How loud the panel shaker's signal is") +
				" in your two speakers, from -60 to +12 dB" + knob_of(game.name) + ".";

			kit.next_tip(about.c_str());
			kit.slider(speakers ? "Seat speakers" : "Panel shaker", only_both, game.level, -60.0f, 12.0f, 1.0f,
			           level.c_str(), nullptr, game.played != 1);
		}
	}
	if (game.start >= 0) {
		kit.next_tip("Smashing Drive keeps its own volume, silent on a new board. At each launch, Odin holds "
		             "VOLUME - then VOLUME + until the game reaches this level, as an operator would.");
		kit.choice("Starting volume", "each launch", STARTS, game.start, nullptr);
	}
	kit.end_group();
	ImGui::PopID();
}

/* Where Odin's sound goes: the device, the speakers of the user as the
 * game shown reaches them, the volume, the sound in the background */
void
audio_output(MenuKit &kit, MenuState &state, const SoundScene &scene)
{
	kit.begin_group("AUDIO OUTPUT");
	std::vector<std::string> devices = { "System default" };
	int device = 0;

	for (const std::string &name : state.devices) {
		if (name == state.device)
			device = static_cast<int>(devices.size());
		devices.push_back(name);
	}
	if (device == 0 && !state.device.empty()) {
		device = static_cast<int>(devices.size());
		devices.push_back(state.device);
	}
	if (kit.value_list("Device", nullptr, devices[static_cast<size_t>(device)].c_str(),
	                   "Change...", devices, device, state.silent))
		state.device = device == 0 ? "" : devices[static_cast<size_t>(device)];

	const ImVec2 speakers = your_speakers_size(kit, scene);
	const bool four = your_speakers(scene.family, scene.played) & YOUR_REAR_LEFT;

	kit.next_tip("Point at one: the parts of the cabinet you hear in it light up.");
	state.audio_pointed_speakers =
		draw_your_speakers(kit, kit.custom_row("Your speakers", four ? "4 channels" : "stereo", speakers.x, speakers.y),
		                   scene);

	float volume = static_cast<float>(state.volume);
	char shown[16];

	std::snprintf(shown, sizeof(shown), "%u %%", state.volume);
	kit.next_tip("Odin's own volume, not the system's. 0 is silent.");
	if (kit.slider("Volume", nullptr, volume, 0.0f, 100.0f, 1.0f, shown, state.silent))
		state.volume = static_cast<unsigned>(std::lround(volume));

	static constexpr const char *KEEP_MUTE[] = { "Keep", "Mute" };
	int background = state.background ? 0 : 1;

	kit.next_tip("Whether Odin keeps its sound when another window is active.");
	if (kit.choice("Sound in the background", nullptr, KEEP_MUTE, background, state.silent))
		state.background = background == 0;
	kit.end_group();
}

} /* namespace */

/* Audio, a page per game: its cabinet and the parts of its sound, lit as
 * the user hears them; its sound to set; where Odin's sound goes */
void
Menu::page_audio(MenuKit &kit, MenuState &state) const
{
	ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float row = games_row(kit, state, top, room.x) + kit.px(ROW_GAP);

	room.y -= row - top.y;
	top.y = row;

	const Box box = left_box(kit, top, room);
	SoundGame *const game = sound_shown(state);
	const SoundScene scene = scene_of(state, game);

	sound_drawing(kit, state, box, scene);
	begin_settings(kit, box, top, room);
	if (game)
		cabinet_sound(kit, *game, scene);
	audio_output(kit, state, scene);
	end_settings(kit, top, room);
}

/* The actions of Odin as the page Hotkeys says them, in the order of
 * Hotkeys::Action */
constexpr const char *HOTKEY_NAMES[Hotkeys::ACTIONS] = {
	"Settings (pauses the game)", "Pause", "Save state", "Load state", "Reset", "Full screen",
	"Insert / remove card (Virtual-On)", "Hide cursor (on / off)", "Test (the cabinet, every game)",
	"Service (player 1, every game)", "Close game", "Quit",
};

/* The modifiers as their caps say them */
constexpr std::pair<SDL_Keymod, const char *> MODIFIER_CAPS[] = {
	{ SDL_KMOD_CTRL, "Ctrl" }, { SDL_KMOD_ALT, "Alt" }, { SDL_KMOD_SHIFT, "Shift" }, { SDL_KMOD_GUI, "Gui" },
};

namespace {

/* The row of the function keys of a keyboard, a gap (0) between groups */
constexpr SDL_Keycode FUNCTION_ROW[] = {
	SDLK_ESCAPE, 0, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, 0, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, 0,
	SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
};

/* A key as its cap says it */
std::string
cap_name(SDL_Keycode key)
{
	switch (key) {
	case SDLK_ESCAPE:
		return "Esc";
	case SDLK_RETURN:
		return "Enter";
	default:
		return SDL_GetKeyName(key);
	}
}

/* The caps of a combination: its modifiers, then its key (Enter after
 * them as its sign) */
std::vector<std::string>
caps(const HotKey &key)
{
	std::vector<std::string> out;

	for (const auto &[mod, name] : MODIFIER_CAPS)
		if (key.mods & mod)
			out.push_back(name);
	out.push_back(!out.empty() && key.key == SDLK_RETURN ? "⏎" : cap_name(key.key));
	return out;
}

/* Whether a key, or a modifier of either side, is held */
bool
held(const MenuState &state, SDL_Keycode key)
{
	const auto is = [&](SDL_Keycode k) {
		return std::find(state.keys_held.begin(), state.keys_held.end(), k) != state.keys_held.end();
	};

	switch (key) {
	case SDLK_LCTRL:
		return is(SDLK_LCTRL) || is(SDLK_RCTRL);
	case SDLK_LALT:
		return is(SDLK_LALT) || is(SDLK_RALT);
	case SDLK_LSHIFT:
		return is(SDLK_LSHIFT) || is(SDLK_RSHIFT);
	case SDLK_LGUI:
		return is(SDLK_LGUI) || is(SDLK_RGUI);
	default:
		return is(key);
	}
}

/* The keyboard drawn: the function row, Odin's keys clear and lit while
 * held (in a narrow box its keys alone), then its other keys and the
 * modifiers they take; how it reads; Guide on a gamepad, as its maker
 * names it. In the middle of the box. */
void
hotkeys_keyboard(const MenuKit &kit, const MenuState &state, const Box &box, const std::string &guide_name)
{
	const MenuPalette &palette = kit.palette();
	const float inside = kit.px(MenuKit::BOX_INSIDE), gap = kit.px(3.0f), group = kit.px(6.0f), size = MenuKit::font(MenuKit::SMALL);
	const float width = box.b.x - box.a.x - 2.0f * inside, cap_h = kit.key_size("x").y;
	std::vector<std::pair<SDL_Keycode, std::string>> others;
	const auto used = [&](SDL_Keycode key) {
		for (const Hotkeys::Binding &binding : state.hotkeys)
			for (const HotKey &k : binding.keys)
				if (k.key == key && key != SDLK_UNKNOWN)
					return true;
		return false;
	};
	const auto add = [&](SDL_Keycode key, const std::string &name) {
		for (const auto &[k, n] : others)
			if (k == key)
				return;
		others.emplace_back(key, name);
	};

	for (const Hotkeys::Binding &binding : state.hotkeys) {
		for (const HotKey &k : binding.keys) {
			if (k.key == SDLK_UNKNOWN)
				continue;
			static constexpr SDL_Keycode LEFT[] = { SDLK_LCTRL, SDLK_LALT, SDLK_LSHIFT, SDLK_LGUI };

			for (size_t m = 0; m < std::size(MODIFIER_CAPS); m++)
				if (k.mods & MODIFIER_CAPS[m].first)
					add(LEFT[m], MODIFIER_CAPS[m].second);
			if (std::find(std::begin(FUNCTION_ROW), std::end(FUNCTION_ROW), k.key) == std::end(FUNCTION_ROW))
				add(k.key, cap_name(k.key));
		}
	}

	/* The function row, or Odin's keys of it alone where it does not fit */
	const auto row_width = [&](bool all) {
		float w = 0.0f;
		bool first = true;

		for (const SDL_Keycode key : FUNCTION_ROW) {
			if (!key) {
				w += all ? group : 0.0f;
				continue;
			}
			if (!all && !used(key))
				continue;
			w += (first ? 0.0f : gap) + kit.key_size(cap_name(key).c_str()).x;
			first = false;
		}
		return w;
	};
	const bool all = row_width(true) <= width;
	float others_w = -gap;

	for (const auto &[key, name] : others)
		others_w += kit.key_size(name.c_str()).x + gap;

	const char caption[] = "Bright outline: Odin's keys. Press one to see it light up.";
	const char guide[] = "held: the hotkey button";
	const float text_w = std::min(width, kit.px(260.0f));
	const float caption_h = centred({}, width, size, caption, 0);
	const float guide_w = kit.pill_size(guide_name.c_str()).x, guide_text_w = text_w - guide_w - kit.px(8.0f);
	const float guide_h = std::max(cap_h, MenuKit::wrapped_h(guide, size, guide_text_w));
	const float all_h = cap_h + gap + (others.empty() ? 0.0f : cap_h + gap) + kit.px(6.0f) + caption_h +
	                    kit.px(10.0f) + guide_h;
	in_box(kit, box, all_h, [&](float y) {
		ImDrawList *const list = ImGui::GetWindowDrawList();
		float x = (box.a.x + box.b.x - row_width(all)) / 2.0f;
		bool first = true;

		for (const SDL_Keycode key : FUNCTION_ROW) {
			if (!key) {
				x += all ? group : 0.0f;
				continue;
			}
			if (!all && !used(key))
				continue;

			const std::string name = cap_name(key);

			x += first ? 0.0f : gap;
			kit.key(list, { x, y }, name.c_str(), used(key), held(state, key));
			x += kit.key_size(name.c_str()).x;
			first = false;
		}
		y += cap_h + gap;
		if (!others.empty()) {
			x = (box.a.x + box.b.x - others_w) / 2.0f;
			for (const auto &[key, name] : others) {
				kit.key(list, { x, y }, name.c_str(), true, held(state, key));
				x += kit.key_size(name.c_str()).x + gap;
			}
			y += cap_h + gap;
		}
		y += kit.px(6.0f);
		y += centred({ box.a.x + inside, y }, width, size, caption, palette.faint) + kit.px(10.0f);
		x = (box.a.x + box.b.x - text_w) / 2.0f;
		kit.pill(list, { x, y }, guide_name.c_str());
		list->AddText(ImGui::GetFont(), size, { x + guide_w + kit.px(8.0f), y + (cap_h - size) / 2.0f }, palette.dim, guide,
		              nullptr, guide_text_w);
	});
}

} /* namespace */

/* Hotkeys: what Odin does itself on a key or with the hotkey button held,
 * each changed by a click and the next key or button; that button */
void
Menu::page_hotkeys(MenuKit &kit, MenuState &state) const
{
	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const Box box = left_box(kit, top, room);
	const int type = state.pad_types[0];
	const std::string held = button_name(state.hotkey_button, type);

	hotkeys_keyboard(kit, state, box, held);
	begin_settings(kit, box, top, room);

	kit.begin_group("GAMEPAD");

	const std::span<const int> buttons = Hotkeys::buttons();
	std::vector<std::string> names;
	int chosen = 0;

	for (size_t b = 0; b < buttons.size(); b++) {
		names.push_back(button_name(buttons[b], type));
		if (buttons[b] == state.hotkey_button)
			chosen = static_cast<int>(b);
	}
	if (kit.dropdown("Hotkey button", "held with another: a shortcut", names, chosen) && chosen >= 0 &&
	    buttons[static_cast<size_t>(chosen)] != state.hotkey_button) {
		state.hotkey_button_asked = buttons[static_cast<size_t>(chosen)];
		state.hotkey_catching.reset();
	}
	kit.end_group();
	kit.begin_group("KEYS");
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const Hotkeys::Binding &binding = state.hotkeys[a];
		std::vector<std::vector<std::string>> keys;
		std::vector<std::string> pad;
		std::vector<size_t> slots;
		const auto caught = [&](size_t slot) {
			return state.hotkey_catching && state.hotkey_catching->first == a && state.hotkey_catching->second == slot;
		};
		int catching = -1;

		for (size_t slot = 0; slot < Hotkeys::KEYS; slot++) {
			if (binding.keys[slot].key == SDLK_UNKNOWN)
				continue;
			if (caught(slot))
				catching = static_cast<int>(keys.size());
			keys.push_back(caps(binding.keys[slot]));
			slots.push_back(slot);
		}
		/* An action without a key takes one in its first place */
		if (keys.empty()) {
			if (caught(0))
				catching = 0;
			keys.push_back({ "-" });
			slots.push_back(0);
		}
		if (caught(Hotkeys::KEYS))
			catching = 2;
		if (binding.pad != Hotkeys::NO_PAD)
			pad.push_back(held);
		if (binding.pad != Hotkeys::NO_PAD && binding.pad != Hotkeys::GUIDE_ALONE)
			pad.push_back(button_name(binding.pad, type));

		const int clicked = kit.shortcut(HOTKEY_NAMES[a], keys, pad, catching);

		if (clicked >= 0)
			state.hotkey_moved.reset();
		if (clicked == 2)
			state.hotkey_catching = { a, Hotkeys::KEYS };
		else if (clicked >= 0)
			state.hotkey_catching = { a, slots[static_cast<size_t>(clicked)] };
	}
	kit.end_group();
	ImGui::Dummy({ 0.0f, kit.px(6.0f) });
	if (kit.button("Defaults", false, MenuKit::Icon::DEFAULTS)) {
		state.hotkeys_default = true;
		state.hotkey_catching.reset();
		state.hotkey_moved.reset();
	}

	end_settings(kit, top, room);
}

/* What the page says at the bottom: what the key or button caught does and
 * how to give it, else how it works */
std::string
Menu::hotkeys_help(const MenuState &state)
{
	/* An action by its name, without what is said after it */
	const auto named = [](size_t a) {
		const std::string name = HOTKEY_NAMES[a];

		return name.substr(0, name.find(" ("));
	};

	if (!state.hotkey_catching && state.hotkey_moved) {
		const MenuState::HotkeyMoved &moved = *state.hotkey_moved;
		const int type = state.pad_types[0];
		const std::string guide = button_name(state.hotkey_button, type);
		const std::string what = moved.key ? Hotkeys::text(*moved.key) :
		                         moved.pad == Hotkeys::GUIDE_ALONE ? guide :
		                                                             guide + " + " + button_name(moved.pad, type);

		return what + " moved from " + named(moved.from) + " to " + named(moved.to) + ".";
	}
	if (!state.hotkey_catching)
		return "Click a key or a combination to change it.";

	const std::string name = HOTKEY_NAMES[state.hotkey_catching->first];

	if (state.hotkey_catching->second == Hotkeys::KEYS) {
		const std::string guide = button_name(state.hotkey_button, state.pad_types[0]);
		const std::string alone =
			state.hotkey_button == SDL_GAMEPAD_BUTTON_GUIDE ? ", or tap " + guide + " alone. " : ". ";

		return name + ": hold " + guide + " and press a button" + alone +
		       button_name(SDL_GAMEPAD_BUTTON_WEST, state.pad_types[0]) + " or Delete empties it. Esc cancels.";
	}
	if (Hotkeys::held(static_cast<Hotkeys::Action>(state.hotkey_catching->first)))
		return name + ": press the new key, held as the button is (no Ctrl, Alt or Shift). Delete empties it. "
		              "Esc cancels, held a second it is the key.";
	return name + ": press the new key, with Ctrl, Alt or Shift if you like. Delete empties it. Esc cancels, "
	              "held a second it is the key.";
}

namespace {

/* The light guns a vibration of the cabinet can go to, in the order listed
 * (one player or two), and how the list says each: one only if the player
 * has a gun that takes the game's orders (an XGunner) */
std::vector<OutputRoute>
vibration_routes(bool two)
{
	using To = OutputRoute::To;
	std::vector<OutputRoute> out = { {} };

	for (unsigned player = 0; player < (two ? 2u : 1u); player++)
		out.insert(out.end(), { { To::GUN, player }, { To::GUN_RECOIL, player } });
	return out;
}

MenuKit::Item
route_item(const MenuState &state, const OutputRoute &route)
{
	const std::string player = route.player ? "P2" : "P1";
	const LightGun *const gun = route.to != OutputRoute::To::NOTHING ? state.gun_of(route.player) : nullptr;
	MenuKit::Item item;

	switch (route.to) {
	case OutputRoute::To::NOTHING: item.name = "Nothing"; break;
	case OutputRoute::To::GUN: item.name = "Light gun of " + player + " · motor"; break;
	case OutputRoute::To::GUN_RECOIL: item.name = "Light gun of " + player + " · recoil"; break;
	}
	if (route.to != OutputRoute::To::NOTHING) {
		item.off = !gun || !GunSerial::shakes(gun->identity, gun->name);
		item.about = !gun ? "no light gun chosen (Input page)" :
		             item.off ? gun->name + ": no vibration or recoil control" : gun->name;
	}
	return item;
}

} /* namespace */

/* Outputs: those of the cabinet of the game shown, lit as the game lights
 * them; its motors running or not, or in a light-gun game the gun each
 * vibration goes to (a gamepad shakes as its options on the page Input
 * say); sent to the programs of lamps, for every game */
void
Menu::page_outputs(MenuKit &kit, MenuState &state) const
{
	ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float row = games_row(kit, state, top, room.x) + kit.px(ROW_GAP);

	room.y -= row - top.y;
	top.y = row;

	const Box box = left_box(kit, top, room);

	output_tiles(kit, state, box);
	begin_settings(kit, box, top, room);

	const bool shakes = std::any_of(state.outputs.begin(), state.outputs.end(), [](const MenuState::Output &output) {
		return output.role == GameOutput::Role::RUMBLE;
	});

	/* A light-gun game's vibrations may go to its guns; the motors of a
	 * game played without guns shake the controller alone */
	if (shakes && !state.sights[0] && !state.sights[1]) {
		kit.begin_group("MOTORS");
		for (const MenuState::Output &output : state.outputs) {
			if (output.role != GameOutput::Role::RUMBLE)
				continue;
			kit.next_tip("A motor of the cabinet, as the game drives it. The controller shakes as hard as it runs "
			             "(page Input, Vibration), and Send outputs passes it on to other programs.");
			kit.text(output.name.c_str(), nullptr, output.on ? "running" : "stopped");
		}
		kit.end_group();
	} else if (shakes) {
		const std::vector<OutputRoute> routes = vibration_routes(state.panel && state.panel->two_players());
		std::vector<MenuKit::Item> items;

		for (const OutputRoute &route : routes)
			items.push_back(route_item(state, route));
		kit.begin_group("LIGHT GUNS");
		for (MenuState::Output &output : state.outputs) {
			if (output.role != GameOutput::Role::RUMBLE)
				continue;

			int chosen = static_cast<int>(std::find(routes.begin(), routes.end(), output.route) - routes.begin());

			kit.next_tip("Which light gun gets the game's vibration, as on the cabinet: its motor or "
			             "its recoil. Needs a gun that accepts commands, such as an XGunner. Controller "
			             "vibration is set on the Input page.");
			if (kit.dropdown(output.name.c_str(), nullptr, items, chosen) && chosen >= 0 &&
			    static_cast<size_t>(chosen) < routes.size() && !items[static_cast<size_t>(chosen)].off)
				output.route = routes[static_cast<size_t>(chosen)];
		}
		kit.end_group();
	}

	kit.begin_group("OUTPUT PROGRAMS");
	kit.next_tip("Sends the cabinet's outputs to LEDBlinky, MAMEHooker or DOFLinx, as MAME's network output does: "
	             "this computer, port 8000.");
	kit.choice("Send outputs", nullptr, OFF_ON, state.network);
	if (state.network)
		kit.text("Connected programs", state.network_failed ? "port 8000 is used by another program: nothing is sent" :
		                                                      nullptr,
		         state.network_failed ? "-" : std::to_string(state.programs).c_str());
	kit.end_group();

	end_settings(kit, top, room);
}

/* Link: which cabinet of a link of Odins this one is, as the next game
 * started takes it (Hikaru::take_cabinet): on the left the cabinets of
 * the game shown in their ring, on the right the settings, what Odin sets
 * in the game, how the game that runs is linked now */
void
Menu::page_link(MenuKit &kit, MenuState &state) const
{
	const char *const locked = state.link_given ? "set by ODIN_NETBD" : nullptr;
	const bool looped = state.link_board == static_cast<int>(NetLinkSettings::Board::LOOP);
	const bool on = state.link_board == static_cast<int>(NetLinkSettings::Board::RING);
	ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float row = games_row(kit, state, top, room.x, false, true) + kit.px(ROW_GAP);

	room.y -= row - top.y;
	top.y = row;

	const Box box = left_box(kit, top, room);

	link_tiles(kit, state, box, on);
	begin_settings(kit, box, top, room);

	if (state.running < state.game_gaelco.size() && state.game_gaelco[state.running] &&
	    !state.game_links[state.running])
		kit.paragraph("NO LINK", "The game that runs does not link cabinets: it plays alone. These settings "
		                         "are for the games that link.");
	kit.begin_group("LINK");
	int chosen = on ? 1 : looped ? -1 : 0;

	kit.next_tip("Each cabinet is an Odin on its own computer.");
	if (kit.choice("Link cabinets", looped ? "alone: its cable is looped back (odin.ini: board = loop)" : nullptr,
	               OFF_ON, chosen, locked) && chosen >= 0)
		state.link_board = static_cast<int>(chosen ? NetLinkSettings::Board::RING : NetLinkSettings::Board::NONE);
	if (on) {
		std::vector<std::string> numbers;
		int cabinet = static_cast<int>(state.link_cabinet) - 1;

		for (unsigned n = 1; n <= NetLinkSettings::MOST_CABINETS; n++)
			numbers.push_back(cabinet_number(n));
		if (locked)
			kit.text("This cabinet", locked, cabinet >= 0 ? numbers[static_cast<size_t>(cabinet)].c_str() : "as it is");
		else {
			kit.next_tip("Cabinet 1 is the master: it leads the others.");
			if (kit.dropdown("This cabinet", nullptr, numbers, cabinet) && cabinet >= 0)
				state.link_cabinet = static_cast<unsigned>(cabinet) + 1;
		}

		const bool last = state.link_cabinet == NetLinkSettings::MOST_CABINETS ||
		                  state.link_cabinet == state.link_shown_cabinets;
		const std::string about = state.link_next_bad ? "not a valid address or address:port: not saved" :
		                          last ? "the address of cabinet 1, as this one is the last" :
		                          "the address of cabinet " + std::to_string(state.link_cabinet + 1);

		if (kit.edit("Next cabinet", about.c_str(), state.link_next_typed, "192.168.1.20", 63, locked)) {
			state.link_next_bad = !NetLinkSettings::next_right(state.link_next_typed);
			if (!state.link_next_bad)
				state.link_next = state.link_next_typed;
		}

		std::vector<std::string> ports;
		int port = -1;

		for (unsigned p = 0; p < LINK_PORTS; p++)
			ports.push_back(std::to_string(NetCable::PORT + p));
		if (state.link_port < NetCable::PORT || state.link_port >= NetCable::PORT + LINK_PORTS)
			ports.push_back(std::to_string(state.link_port));
		for (size_t p = 0; p < ports.size(); p++)
			if (ports[p] == std::to_string(state.link_port))
				port = static_cast<int>(p);
		if (locked)
			kit.text("Port", locked, std::to_string(state.link_port).c_str());
		else {
			kit.next_tip("Change it only to run two Odins on one computer.");
			if (kit.dropdown("Port", nullptr, ports, port) && port >= 0)
				state.link_port = static_cast<unsigned>(std::stoul(ports[static_cast<size_t>(port)]));
		}
	}
	kit.end_group();

	/* The cables of the game that runs, when it is the one shown */
	if (state.link_running && state.shown == state.running && (state.link_board_now || state.link_failed)) {
		kit.begin_group("CABLES");
		if (state.link_failed) {
			kit.text("Port", "this cabinet is not linked", "in use by another program");
		} else if (!state.link_linked) {
			kit.text("Cable", nullptr, "looped back on this cabinet");
		} else {
			kit.text("Previous cabinet", nullptr, state.link_previous ? "connected" : "waiting");
			kit.text("Next cabinet", nullptr, state.link_next_plugged ? "connected" : "waiting");
		}
		kit.end_group();
	}
	kit.paragraph("HOW TO LINK",
	              "1. One computer per cabinet, all on the same wired network: Wi-Fi is too slow.\n"
	              "2. On each one, set Link cabinets to On and give it its own number: 1, 2, 3...\n"
	              "3. In Next cabinet, type the address of the next number; on the last one, the address of "
	              "cabinet 1.\n"
	              "4. Start the same game on all of them. Odin sets the master, the slaves and the seats; a game run "
	              "for the first time restarts once by itself to take its seat.");
	end_settings(kit, top, room);
}

/* Paths: where the games are, where Odin writes, its own files */
void
Menu::page_paths(MenuKit &kit, MenuState &state) const
{
	const char *const change = state.choosing ? nullptr : "Change...";

	kit.begin_group("GAMES");
	kit.next_tip("One zip, 7z or folder per game, named as in MAME.");
	if (kit.value("Games (ROMs)", "at the next launch", shown_path(state.roms, "not set").c_str(), change, nullptr,
	              state.roms_given ? "set on the command line: -R" : nullptr))
		state.choose = MenuState::Folder::ROMS;
	kit.next_tip("hikaru and mie, named as in MAME; jvs13551, 18wheelr and srally2 are optional.");
	if (kit.value("BIOS", "at the next launch", shown_path(state.bios, "the games folder").c_str(), change, nullptr,
	              state.bios_given ? "set on the command line: -B" : nullptr))
		state.choose = MenuState::Folder::BIOS;
	kit.next_tip("Files named after the game's MAME name: swracer_title.png for the library (swracer_logo.png "
	             "instead, if there is one), swracer_bezel.png for the bezel.");
	if (kit.value("Artwork", nullptr, shown_path(state.artwork, "none").c_str(), change, nullptr,
	              state.artwork_given ? "set by ODIN_ARTWORK" : nullptr))
		state.choose = MenuState::Folder::ARTWORK;
	kit.end_group();

	kit.begin_group("SAVES");
	kit.next_tip("EEPROM, cards and operator settings, one folder per game.");
	if (kit.value("Game saves", "at the next launch", shown_path(state.saves, "none").c_str(), change, nullptr,
	              state.saves_given ? "set by ODIN_SAVES" : nullptr))
		state.choose = MenuState::Folder::SAVES;

	/* The keys of the states, as the user set them */
	const std::string save = first_key(state, Hotkeys::Action::SAVE_STATE),
	                  load = first_key(state, Hotkeys::Action::LOAD_STATE);
	std::string states;

	if (!save.empty())
		states = save + " saves a state";
	if (!load.empty())
		states += (states.empty() ? "" : ", ") + load + (save.empty() ? " loads a state" : " loads it");
	kit.next_tip(states.empty() ? std::string() : states + ".");
	if (kit.value("Save states", nullptr, shown_path(state.states, "the folder Odin was started from").c_str(),
	              change))
		state.choose = MenuState::Folder::STATES;
	kit.end_group();

	kit.begin_group("ODIN FILES");
	kit.next_tip("odin.ini, where this menu saves your settings. Another one: start Odin with -c <file>.");
	kit.value("Configuration", nullptr, shown_path(state.settings_file, "none: ODIN_CONFIG=none").c_str(), nullptr);
	kit.next_tip("The shaders each game uses, listed so Odin can prepare them when the game starts.");
	kit.value("Shader lists", nullptr, shown_path(state.shader_lists, "none").c_str(), nullptr, "READ ONLY");
	kit.end_group();

	kit.begin_group("LOG");
	kit.next_tip("Odin's messages since it started. The previous run's are in odin-previous.log.");
	/* The file of this launch; the one of the folder chosen for the next */
	const std::string next_log = state.log_folder.empty() ? std::string() : state.log_folder + "/odin.log";
	const bool log_moves = !state.log_given && !next_log.empty() && next_log != state.log_file;

	if (kit.value("Log file", log_moves ? "at the next launch" : nullptr,
	              shown_path(log_moves ? next_log : state.log_file, "none: ODIN_LOG=none").c_str(), change, nullptr,
	              state.log_given ? "set by ODIN_LOG" : nullptr))
		state.choose = MenuState::Folder::LOG;

	kit.next_tip("Logs every access of the board, to the terminal and the log file.");
	kit.choice("Detailed log", "much slower · at the next launch", OFF_ON, state.verbose,
	           state.verbose_given ? "set on the command line: -v" : nullptr);
	kit.end_group();
}

} /* namespace odin */

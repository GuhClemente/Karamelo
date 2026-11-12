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

/* The library: the games to choose from when Odin starts without one, as
 * the game picker of sm2-emu lays them out, in the palette of
 * the menu, under the bar. Tiles at the left, the game chosen framed; its
 * sheet at the right. Arrows or a gamepad choose, Enter, its button that
 * presses or a click on the game chosen plays it. */

#include "odin/host/ui/menu.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "imgui/imgui.h"
#include "odin/host/ui/menu_kit.hpp"

namespace odin {

namespace {

/* In points: the margin of the window, the gap between the tiles and
 * before the sheet, what is inside a tile and the sheet; the share of the
 * width the sheet takes; a tile as the screen of the game is (4:3), three
 * to eight on a row; the cuts of the corners */
constexpr float MARGIN = 16.0f;
constexpr float GAP = 12.0f;
constexpr float TILE_INSIDE = 7.0f;
constexpr float SHEET_INSIDE = 14.0f;
constexpr float SHEET_SHARE = 0.31f;
constexpr float TILE_W = 4.0f;
constexpr float TILE_H = 3.0f;
constexpr int COLUMNS = 3;
constexpr int MOST_COLUMNS = 8;
constexpr float TILE_CUT = 5.0f;
constexpr float SHEET_CUT = 8.0f;
/* A game that cannot be played: its picture that dim */
constexpr ImU32 DIMMED = IM_COL32(0xFF, 0xFF, 0xFF, 0x50);
/* The title of the head, its letters a fifth of the font apart; the line
 * of the accent under the head, its opacity, the share of it that fades
 * at each end; the title of the sheet, larger than the font */
constexpr float TITLE_SPACING = 0.2f;
constexpr float LINE_ALPHA = 0.7f;
constexpr float LINE_FADE = 0.2f;
constexpr float SHEET_TITLE = 1.35f;

/* A system of LibraryState::systems as the library names it */
std::string
system_name(const std::string &system)
{
	if (system == "hikaru")
		return "SEGA Hikaru";

	std::string name = system;

	if (!name.empty())
		name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
	return name;
}

/* The game chosen to be played, when its files are all there */
void
play_chosen(LibraryState &state)
{
	const LibraryState::Game &game = state.games[static_cast<size_t>(state.selected)];

	if (game.missing.empty())
		state.play = game.name;
}

/* A text from `at`, wrapped to that width: the height it took */
float
wrapped(ImDrawList *list, ImVec2 at, float width, float size, ImU32 colour, const std::string &text)
{
	ImFont *const font = ImGui::GetFont();

	list->AddText(font, size, at, colour, text.c_str(), nullptr, width);
	return font->CalcTextSizeA(size, FLT_MAX, width, text.c_str()).y;
}

/* The button of the filter, lit while it hides some games, its right edge
 * at `right`; under it, while open, what the library shows: only the games
 * whose files are all there, the versions of a game too, the games of each
 * system when the list has several (one at least shown); how many it
 * hides. Its width. */
float
filter_button(const MenuKit &kit, LibraryState &state, ImVec2 at, float right)
{
	static constexpr const char *OFF_ON[] = { "Off", "On" }, *HIDE_SHOW[] = { "Hide", "Show" };
	static constexpr char LABEL[] = "Filter";
	const MenuPalette &palette = kit.palette();
	LibraryFilter &filter = state.filter;
	const ImVec2 size = kit.button_size(LABEL), cells = kit.segments_size(HIDE_SHOW);
	const float line = ImGui::GetTextLineHeight(), row = std::max(line, cells.y), apart = kit.px(8.0f);
	const float pad = kit.px(12.0f), gap = kit.px(24.0f);
	const bool systems = state.systems.size() > 1;
	const auto shown = [&filter](const std::string &system) {
		return filter.systems.empty() || std::find(filter.systems.begin(), filter.systems.end(), system) !=
		                                 filter.systems.end();
	};
	const auto title = [](std::string name) {
		if (!name.empty())
			name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
		return name;
	};
	std::vector<std::string> names = { "Owned only", "Variants" };

	at.x = right - size.x;
	if (kit.button(LABEL, at, filter != LibraryFilter()))
		ImGui::OpenPopup("filter");
	if (systems)
		for (const std::string &system : state.systems)
			names.push_back(title(system));

	float widest = 0.0f;

	for (const std::string &name : names)
		widest = std::max(widest, ImGui::CalcTextSize(name.c_str()).x);

	const float width = 2.0f * pad + widest + gap + cells.x;

	ImGui::SetNextWindowPos({ right - width, at.y + size.y + kit.px(6.0f) });
	ImGui::SetNextWindowSize({ width, 0.0f });
	ImGui::PushStyleColor(ImGuiCol_PopupBg, palette.screen);
	ImGui::PushStyleColor(ImGuiCol_Border, palette.stroke);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { pad, pad });
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, kit.stroke());
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
	if (ImGui::BeginPopup("filter")) {
		ImDrawList *const list = ImGui::GetWindowDrawList();
		const ImVec2 top = ImGui::GetCursorScreenPos();
		float y = top.y;
		/* A row: its name at the left, its two choices at the right */
		const auto choose = [&](const std::string &name, std::span<const char *const> labels, int &chosen,
		                        bool still) {
			ImGui::PushID(name.c_str());
			list->AddText({ top.x, y + (row - line) / 2.0f }, still ? palette.faint : palette.ink,
			              name.c_str());

			const bool changed = kit.segments({ top.x + width - 2.0f * pad - kit.segments_size(labels).x,
			                                    y + (row - cells.y) / 2.0f }, labels, chosen, still);

			ImGui::PopID();
			y += row + apart;
			return changed;
		};
		int owned = filter.owned_only ? 1 : 0, variants = filter.variants ? 1 : 0;

		if (choose(names[0], OFF_ON, owned, false))
			filter.owned_only = owned == 1;
		if (choose(names[1], HIDE_SHOW, variants, false))
			filter.variants = variants == 1;
		for (size_t s = 0; systems && s < state.systems.size(); s++) {
			const std::string &system = state.systems[s];
			const auto on = std::count_if(state.systems.begin(), state.systems.end(), shown);
			int chosen = shown(system) ? 1 : 0;

			if (s == 0) {
				y += apart;
				MenuKit::small_caps(list, { top.x, y }, palette.dim, "SYSTEMS");
				y += line + apart;
			}
			if (!choose(names[2 + s], HIDE_SHOW, chosen, chosen && on == 1))
				continue;

			std::vector<std::string> now;

			for (const std::string &other : state.systems)
				if (other == system ? chosen == 1 : shown(other))
					now.push_back(other);
			filter.systems = now.size() == state.systems.size() ? std::vector<std::string>() : now;
		}
		if (state.hidden) {
			const std::string told =
				std::to_string(state.hidden) + (state.hidden == 1 ? " game hidden" : " games hidden");

			list->AddText({ top.x, y }, palette.dim, told.c_str());
			y += line;
		}
		ImGui::SetCursorScreenPos(top);
		ImGui::Dummy({ width - 2.0f * pad, y - top.y - (state.hidden ? 0.0f : apart) });
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
	return size.x;
}

} /* namespace */

void
Menu::add_library_picture(const std::string &game, SDL_Surface *rgba)
{
	m_library_handed.emplace_back(game, rgba);
}

void
Menu::upload_library_pictures()
{
	for (const auto &[game, rgba] : m_library_handed) {
		const auto old = m_library_pictures.find(game);

		if (old != m_library_pictures.end()) {
			m_graphics.forget_image(old->second);
			m_library_pictures.erase(old);
		}
		if (!rgba)
			continue;
		m_library_pictures[game] = m_graphics.make_image(*rgba, true);
		SDL_DestroySurface(rgba);
	}
	m_library_handed.clear();
}

void
Menu::build_library(LibraryState &state, BarState &bar_state)
{
	begin_frame(bar_state.theme);
	bar(bar_state);

	const MenuKit kit(*m_palette, m_scale);
	const MenuPalette &palette = kit.palette();
	const ImVec2 room = ImGui::GetIO().DisplaySize;
	const float margin = kit.px(MARGIN), gap = kit.px(GAP), line = ImGui::GetTextLineHeight();
	const float size = ImGui::GetFontSize(), below = bar_state.bottom;
	const int count = static_cast<int>(state.games.size());

	/* Under the bar. The library reads the keys itself: Dear ImGui moves
	 * nothing */
	ImGui::SetNextWindowPos({ 0.0f, below });
	ImGui::SetNextWindowSize({ room.x, room.y - below });
	ImGui::Begin("Library", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);

	ImDrawList *const list = ImGui::GetWindowDrawList();

	list->AddRectFilled({ 0.0f, below }, room, palette.screen);

	/* The head: what to do, the keys at its right (and the button of the
	 * gamepad), the line of the accent under them */
	const std::string &quit = bar_state.keys[static_cast<size_t>(Hotkeys::Action::QUIT)];
	const std::string keys = "Enter" + (state.press.empty() ? "" : " or " + state.press) + ": play" +
	                         (quit.empty() ? "" : "    " + quit + ": quit");

	MenuKit::write(list, { margin, below + margin }, palette.cabinet, "SELECT A GAME", size, size * TITLE_SPACING);

	const float keys_x = room.x - margin - ImGui::CalcTextSize(keys.c_str()).x;
	const float filter_h = kit.button_size("Filter").y;

	list->AddText({ keys_x, below + margin }, palette.faint, keys.c_str());
	filter_button(kit, state, { 0.0f, below + margin + (line - filter_h) / 2.0f }, keys_x - kit.px(20.0f));

	const float under = below + margin + line + kit.px(8.0f);

	MenuKit::fading_line(list, margin, room.x - margin, under, kit.stroke() * 2.0f,
	                     MenuKit::alpha(palette.accent, LINE_ALPHA), LINE_FADE);

	/* The tiles at the left, each system on rows of its own under its name
	 * when the list has several, as many on a row (COLUMNS at least) as
	 * makes them largest in the room the rows and names leave. The sheet at
	 * the right. */
	struct Group {
		std::string system;
		int first_row, games;
	};
	const bool grouped = state.systems.size() > 1;
	const float top = under + kit.px(14.0f), bottom = room.y - margin;
	const float sheet_w = std::round((room.x - 2.0f * margin) * SHEET_SHARE);
	const float grid_w = room.x - 2.0f * margin - sheet_w - gap;
	const float head_h = std::round(line + kit.px(12.0f));
	std::vector<Group> groups;
	std::vector<std::pair<int, int>> spots;	/* row, column of each tile */
	/* The tiles laid that many on a row; how wide each is then */
	const auto lay = [&](int columns) {
		int row = -1, column = columns;

		groups.clear();
		spots.clear();
		for (const LibraryState::Game &game : state.games) {
			if (grouped && (groups.empty() || groups.back().system != game.system)) {
				groups.push_back({ game.system, row + 1, 0 });
				column = columns;
			}
			if (column == columns) {
				row++;
				column = 0;
			}
			spots.emplace_back(row, column++);
			if (grouped)
				groups.back().games++;
		}

		const float rows = static_cast<float>(std::max(1, row + 1)), across = static_cast<float>(columns);
		const float heads = static_cast<float>(groups.size()) * head_h;
		const float high = (bottom - top - heads - (rows - 1.0f) * gap) / rows;

		return std::floor(std::min((grid_w - (across - 1.0f) * gap) / across, high * TILE_W / TILE_H));
	};
	int columns = COLUMNS;

	for (int more = COLUMNS + 1; more <= MOST_COLUMNS; more++)
		if (lay(more) > lay(columns))
			columns = more;

	const float tile_w = lay(columns), tile_h = std::floor(tile_w * TILE_H / TILE_W);

	/* The top of a row, under the names of the systems that start there or
	 * above */
	const auto row_y = [&](int r) {
		const auto named =
			std::count_if(groups.begin(), groups.end(), [r](const Group &g) { return g.first_row <= r; });

		return top + static_cast<float>(r) * (tile_h + gap) + static_cast<float>(named) * head_h;
	};

	/* The arrows and the gamepad move the choice, as the tiles lie; Enter
	 * or its button plays. With a menu of the bar open, they are its own. */
	if (const LibraryState::Nav nav = std::exchange(state.nav, {});
	    count && !bar_state.open && !ImGui::IsPopupOpen("filter")) {
		const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true) ? 1 : 0; };
		const int across = pressed(ImGuiKey_RightArrow) - pressed(ImGuiKey_LeftArrow) + nav.across;
		const int down = pressed(ImGuiKey_DownArrow) - pressed(ImGuiKey_UpArrow) + nav.down;
		int chosen = std::clamp(state.selected + across, 0, count - 1);
		const auto [from_row, from_column] = spots[static_cast<size_t>(chosen)];
		const int to = from_row + (down > 0) - (down < 0);

		/* Up and down: the tile of the row next to it, in its column or
		 * the last of a shorter row */
		for (int i = 0; down && i < count; i++)
			if (const auto [r, c] = spots[static_cast<size_t>(i)]; r == to && c <= from_column)
				chosen = i;
		state.selected = chosen;
		if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || nav.press)
			play_chosen(state);
	}

	for (int i = 0; i < count; i++) {
		const LibraryState::Game &game = state.games[static_cast<size_t>(i)];
		const auto [tile_row, tile_column] = spots[static_cast<size_t>(i)];
		const ImVec2 a = { margin + static_cast<float>(tile_column) * (tile_w + gap), row_y(tile_row) };
		const ImVec2 b = { a.x + tile_w, a.y + tile_h };
		const float inside = kit.px(TILE_INSIDE);

		/* A click chooses a game; on the game chosen, plays it */
		ImGui::SetCursorScreenPos(a);
		ImGui::PushID(i);
		if (ImGui::InvisibleButton("tile", { tile_w, tile_h })) {
			if (state.selected == i)
				play_chosen(state);
			state.selected = i;
		}
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		const bool chosen = i == state.selected, playable = game.missing.empty();

		const auto picture = m_library_pictures.find(game.name);
		const float cut = kit.px(TILE_CUT);

		kit.frame(list, a, b, cut, palette.tile, 0);
		list->PushClipRect(a, b, true);
		if (picture != m_library_pictures.end()) {
			/* Its picture whole in the tile, the corners cut again over
			 * it */
			const float k = std::min(tile_w / picture->second.w, tile_h / picture->second.h);
			const ImVec2 pa = { std::round(a.x + (tile_w - picture->second.w * k) / 2.0f),
				            std::round(a.y + (tile_h - picture->second.h * k) / 2.0f) };

			m_graphics.ui_levels(list, true);
			list->AddImage(ImTextureRef(static_cast<ImTextureID>(m_graphics.ui_image(picture->second))), pa,
			               { pa.x + std::round(picture->second.w * k), pa.y + std::round(picture->second.h * k) },
			               { 0.0f, 0.0f }, { 1.0f, 1.0f }, playable ? IM_COL32_WHITE : DIMMED);
			m_graphics.ui_levels(list, false);
			list->AddTriangleFilled(a, { a.x + cut, a.y }, { a.x, a.y + cut }, palette.screen);
			list->AddTriangleFilled({ b.x, a.y }, { b.x, a.y + cut }, { b.x - cut, a.y }, palette.screen);
			list->AddTriangleFilled(b, { b.x - cut, b.y }, { b.x, b.y - cut }, palette.screen);
			list->AddTriangleFilled({ a.x, b.y }, { a.x, b.y - cut }, { a.x + cut, b.y }, palette.screen);
		} else {
			wrapped(list, { a.x + inside, a.y + inside }, tile_w - 2.0f * inside, size,
			        playable ? palette.dim : palette.faint, game.title);
		}
		if (!playable) {
			list->AddRectFilled({ a.x, b.y - inside - line - kit.px(4.0f) }, b, MenuKit::alpha(palette.tile, 0.85f));
			/* Its corners cut again, as those of the tile */
			list->AddTriangleFilled(b, { b.x - cut, b.y }, { b.x, b.y - cut }, palette.screen);
			list->AddTriangleFilled({ a.x, b.y }, { a.x, b.y - cut }, { a.x + cut, b.y }, palette.screen);
			MenuKit::small_caps(list, { a.x + inside, b.y - inside - line }, palette.accent, "MISSING");
		}
		list->PopClipRect();
		kit.frame(list, a, b, kit.px(TILE_CUT), 0, chosen ? palette.action : hovered ? palette.dim : palette.outline,
		          chosen ? 3.0f : 1.0f);
	}

	/* The name of each system over its rows, how many games it shows at
	 * the right, a line under them */
	for (const Group &group : groups) {
		const float y = row_y(group.first_row) - head_h;
		const float right = margin + static_cast<float>(columns) * (tile_w + gap) - gap;
		std::string name = system_name(group.system);
		const std::string games = std::to_string(group.games) + (group.games == 1 ? " game" : " games");

		std::transform(name.begin(), name.end(), name.begin(),
		               [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
		MenuKit::small_caps(list, { margin, y }, palette.dim, name.c_str());
		list->AddText({ right - ImGui::CalcTextSize(games.c_str()).x, y }, palette.faint, games.c_str());
		list->AddLine({ margin, y + line + kit.px(3.0f) }, { right, y + line + kit.px(3.0f) }, palette.line,
		              kit.stroke());
	}

	/* The sheet of the game chosen: its title, its name in MAME, its
	 * system, what is known of it; or what it lacks, where it was looked for, the folders
	 * to choose; how to play it at its foot */
	const ImVec2 sa = { room.x - margin - sheet_w, top }, sb = { room.x - margin, bottom };
	const float inside = kit.px(SHEET_INSIDE), x = sa.x + inside, width = sheet_w - 2.0f * inside;
	const std::string play = "Enter" + (state.press.empty() ? "" : ", " + state.press) + " or click again: play";
	const char enter[] = "Enter";
	const float foot = sb.y - inside - MenuKit::wrapped_h(play.c_str(), size, width);
	float y = sa.y + inside;

	kit.frame(list, sa, sb, kit.px(SHEET_CUT), palette.body, 0);
	if (!count && state.hidden)
		wrapped(list, { x, y }, width, size, palette.dim, "The filter hides every game.");
	if (count) {
		const LibraryState::Game &game = state.games[static_cast<size_t>(state.selected)];

		y += wrapped(list, { x, y }, width, MenuKit::font(SHEET_TITLE), palette.ink, game.title) + kit.px(4.0f);
		list->AddText({ x, y }, palette.faint, game.name.c_str());
		y += line + kit.px(2.0f);
		list->AddText({ x, y }, palette.dim, system_name(game.system).c_str());
		y += line + kit.px(8.0f);
		list->AddLine({ x, y }, { x + width, y }, palette.line, kit.stroke());
		y += kit.px(10.0f);

		/* Under a game that lacks files, the folders to choose, one
		 * above the other, unless the command line gives them */
		const char games[] = "Games folder...";
		const char bios[] = "BIOS folder...";
		const bool lacks = !game.missing.empty();
		const bool choose_games = lacks && !state.roms_given && !state.choosing;
		const bool choose_bios =
			lacks && !game.gaelco && !state.bios_given && !state.choosing && !state.roms.empty();
		const float apart = kit.px(6.0f);
		const float buttons = (choose_games ? kit.button_size(games).y : 0.0f) +
		                      (choose_bios ? kit.button_size(bios).y : 0.0f) +
		                      (choose_games && choose_bios ? apart : 0.0f);
		const float end = lacks ? sb.y - inside - buttons : foot;

		/* What it lacks scrolls when taller than the sheet, its bar
		 * in the room at its right; each game its own scroll */
		ImGui::SetCursorScreenPos({ x, y });
		ImGui::PushID(game.name.c_str());
		ImGui::BeginChild("sheet", { sb.x - kit.px(3.0f) - x, std::max(1.0f, end - apart - y) }, ImGuiChildFlags_None,
		                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);

		ImDrawList *const text = ImGui::GetWindowDrawList();
		const float start = ImGui::GetCursorScreenPos().y;
		float at = start;

		if (state.failed == game.name)
			at += wrapped(text, { x, at }, width, size, palette.danger,
			              "It failed to start: see odin.log for why.") + line;
		const std::string &roms = state.roms;

		if (roms.empty()) {
			at += wrapped(text, { x, at }, width, size, palette.ink,
			              "Choose your games folder: one folder, zip or 7z per game, named as in MAME.") + line;
		} else if (lacks) {
			at += wrapped(text, { x, at }, width, size, palette.ink, "Missing files:") + kit.px(4.0f);
			for (const std::string &file : game.missing)
				at += wrapped(text, { x, at }, width, size, palette.dim, file);
			at += line;
			at += wrapped(text, { x, at }, width, size, palette.faint,
			              "Searched in " + shown_path(roms, "") +
			              (game.gaelco || state.bios.empty() ? std::string() : " and " + shown_path(state.bios, "")));
		}
		ImGui::SetCursorScreenPos({ x, start });
		ImGui::Dummy({ width, at - start });
		ImGui::EndChild();
		ImGui::PopID();

		float button_y = end;

		if (choose_games) {
			if (kit.button(games, { x, button_y }, false))
				state.choose = LibraryState::Folder::ROMS;
			button_y += kit.button_size(games).y + apart;
		}
		if (choose_bios && kit.button(bios, { x, button_y }, false))
			state.choose = LibraryState::Folder::BIOS;

		/* How to play it, on as many lines as it takes: Enter in the
		 * action colour */
		if (!lacks) {
			wrapped(list, { x, foot }, width, size, palette.faint, play);
			list->AddText({ x, foot }, palette.action, enter);
		}
	}

	ImGui::End();
	ImGui::Render();
}

} /* namespace odin */

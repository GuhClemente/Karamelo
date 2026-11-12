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

/* The page Input of the menu: the games, the device of each
 * player, and the gamepad drawn with the name of each control beside the
 * part that works it, or the controls as a list (Classic). */

#include "odin/host/ui/menu.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <utility>

#include "imgui/imgui.h"
#include "odin/host/app/options.hpp"
#include "odin/host/ui/cabinet_view.hpp"
#include "odin/host/ui/gun_drawing.hpp"
#include "odin/host/input/control_panel.hpp"
#include "odin/host/ui/menu_kit.hpp"
#include "odin/host/ui/pad_view.hpp"

namespace odin {

namespace {

/* The room between the rows of the page, in points */
constexpr float ROW_GAP = 6.0f;
/* Start mapping waits so long for each control */
constexpr uint64_t MAPPING_MS = 6000;
/* A window this many times wider than high has the cabinet beside the
 * device, which takes this share of the width (1.4 to 1) */
constexpr float WIDE = 1.5f;
constexpr float PAD_SHARE = 1.4f / 2.4f;
/* A label column at its narrowest (at its widest 0.3 of the box), a label
 * from the next, the room the labels keep from the box's edges */
constexpr float COLUMN_MIN = 90.0f;
constexpr float LABEL_GAP = 19.0f;
constexpr float LABEL_EDGE = 10.0f;
/* The accent on the share of a chip an amount fills */
constexpr float AMOUNT_ALPHA = 0.55f;
/* A stick touched from there, a trigger from there */
constexpr float STICK_TOUCHED = 0.3f;
constexpr int16_t TRIGGER_TOUCHED = 6554;

/* The type of SDL of a kind of gamepad (pad_drawing.hpp): the Steam
 * Controller and an unknown one are standard */
SDL_GamepadType
sdl_type(int kind)
{
	return kind == PAD_STEAM_CONTROLLER || kind == SDL_GAMEPAD_TYPE_UNKNOWN ? SDL_GAMEPAD_TYPE_STANDARD :
	                                                                         static_cast<SDL_GamepadType>(kind);
}

bool
is_switch(int kind)
{
	return kind >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && kind <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

/* The names a maker prints on the buttons that differ: Back, Start, the
 * sticks pressed, Guide, the shoulders, the triggers and misc1 (Share of an
 * Xbox Series, the mic of a PS5, Capture of a Switch); the D-pad and the
 * right stick (the pads of the Steam Controller) */
struct Family {
	const char *back, *start, *left_stick, *right_stick, *guide, *lb, *rb, *lt, *rt, *dpad, *right_axes, *misc;
};

const Family &
family(int kind)
{
	static constexpr Family ODIN = { "Select", "Start", "L3", "R3", "Guide", "LB", "RB", "LT", "RT", "D-pad", "R stick", "Misc" };
	static constexpr Family XBOX_360 = { "Back", "Start", "LS", "RS", "Guide", "LB", "RB", "LT", "RT", "D-pad", "R stick", "Misc" };
	static constexpr Family XBOX_ONE = { "View", "Menu", "LS", "RS", "Guide", "LB", "RB", "LT", "RT", "D-pad", "R stick", "Share" };
	static constexpr Family PS3 = { "Select", "Start", "L3", "R3", "PS", "L1", "R1", "L2", "R2", "D-pad", "R stick", "Misc" };
	static constexpr Family PS4 = { "Share", "Options", "L3", "R3", "PS", "L1", "R1", "L2", "R2", "D-pad", "R stick", "Misc" };
	static constexpr Family PS5 = { "Create", "Options", "L3", "R3", "PS", "L1", "R1", "L2", "R2", "D-pad", "R stick", "Mic" };
	static constexpr Family SWITCH = { "−", "+", "LS", "RS", "Home", "L", "R", "ZL", "ZR", "D-pad", "R stick", "Capture" };
	static constexpr Family STEAM = { "◀", "▶", "LS", "R pad", "Steam", "LB", "RB", "LT", "RT", "L pad", "R pad", "Misc" };

	if (kind == PAD_STEAM_CONTROLLER)
		return STEAM;
	switch (static_cast<SDL_GamepadType>(kind)) {
	case SDL_GAMEPAD_TYPE_XBOX360: return XBOX_360;
	case SDL_GAMEPAD_TYPE_XBOXONE: return XBOX_ONE;
	case SDL_GAMEPAD_TYPE_PS3: return PS3;
	case SDL_GAMEPAD_TYPE_PS4: return PS4;
	case SDL_GAMEPAD_TYPE_PS5: return PS5;
	default: return is_switch(kind) ? SWITCH : ODIN;
	}
}

/* The name of an input of a gamepad of that kind, as its maker prints it:
 * the marks of a PlayStation, a way of a stick or of the D-pad as an
 * arrow */
std::string
pad_label(const PadInput &pad, int kind)
{
	const Family &names = family(kind);
	const std::string dpad = std::string(names.dpad) + " ";

	if (pad.type == PadInput::Type::BUTTON) {
		const auto button = static_cast<SDL_GamepadButton>(pad.index);

		switch (button) {
		case SDL_GAMEPAD_BUTTON_SOUTH:
		case SDL_GAMEPAD_BUTTON_EAST:
		case SDL_GAMEPAD_BUTTON_WEST:
		case SDL_GAMEPAD_BUTTON_NORTH: {
			static const char *const FACES[] = { "?", "A", "B", "X", "Y", "✕", "○", "□", "△" };
			const SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabelForType(sdl_type(kind), button);

			return FACES[static_cast<size_t>(label) < std::size(FACES) ? static_cast<size_t>(label) : 0];
		}
		case SDL_GAMEPAD_BUTTON_BACK: return names.back;
		case SDL_GAMEPAD_BUTTON_GUIDE: return names.guide;
		case SDL_GAMEPAD_BUTTON_START: return names.start;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return names.left_stick;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return names.right_stick;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return names.lb;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return names.rb;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return dpad + "↑";
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return dpad + "↓";
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return dpad + "←";
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return dpad + "→";
		case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
		case SDL_GAMEPAD_BUTTON_MISC1: return names.misc;
		default: {
			/* The grips of the Steam Controller, under it */
			if (kind == PAD_STEAM_CONTROLLER && button == SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1)
				return "R grip";
			if (kind == PAD_STEAM_CONTROLLER && button == SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)
				return "L grip";

			const char *const name = SDL_GetGamepadStringForButton(button);

			return name ? name : "?";
		}
		}
	}

	const auto axis = static_cast<SDL_GamepadAxis>(pad.index);

	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
		return names.lt;
	if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
		return names.rt;

	const bool left = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY;
	const bool across = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_RIGHTX;
	const bool positive = pad.type == PadInput::Type::AXIS_POSITIVE;
	std::string out = left ? "L stick " : std::string(names.right_axes) + " ";

	if (pad.type == PadInput::Type::AXIS)
		out += across ? "↔" : "↕";
	else if (across)
		out += positive ? "→" : "←";
	else
		out += positive ? "↓" : "↑";
	return pad.inverted ? out + " (inverted)" : out;
}

/* "Axis 2 (inverted)", "Axis 1 −", "Button 4", "Hat 0 ↑", "Gas" (a part
 * of a known wheel: it is the wheel's) */
std::string
joy_label(const JoyInput &joy)
{
	static const char *const PARTS[WHEEL_PARTS] = { "Gas", "Brake", "Left paddle", "Right paddle", "Start", "Select" };
	const std::string index = std::to_string(joy.index);

	switch (joy.type) {
	case JoyInput::Type::PART: return PARTS[joy.part % WHEEL_PARTS];
	case JoyInput::Type::BUTTON: return "Button " + index;
	case JoyInput::Type::AXIS_NEGATIVE: return "Axis " + index + " −";
	case JoyInput::Type::AXIS_POSITIVE: return "Axis " + index + " +";
	case JoyInput::Type::HAT:
		return "Hat " + index + (joy.way == SDL_HAT_UP ? " ↑" : joy.way == SDL_HAT_DOWN ? " ↓" :
		                         joy.way == SDL_HAT_LEFT ? " ←" : " →");
	case JoyInput::Type::AXIS: break;
	}
	return "Axis " + index + (joy.inverted ? " (inverted)" : "");
}

/* ① to ⑨ (drawn by menu_kit.cpp), then (10)... */
std::string
joystick_number(size_t n)
{
	if (n >= 9)
		return "(" + std::to_string(n + 1) + ")";
	return { static_cast<char>(0xE2), static_cast<char>(0x91), static_cast<char>(0xA0 + n) };
}

/* The name of a joystick by its GUID, "unplugged" when it is not there */
std::string
joystick_name(const std::string &guid, std::span<const MenuState::Plugged> joysticks)
{
	const auto one = std::find_if(joysticks.begin(), joysticks.end(),
	                              [&guid](const MenuState::Plugged &joy) { return joy.guid == guid; });

	return one == joysticks.end() ? std::string("unplugged") : one->name;
}

/* How the inputs of another joystick than the player's are said: by their
 * number on the page ("① Button 1"), else by the joystick's name ("Button
 * 1 · TH8A") */
struct Others {
	std::span<const std::string> numbered;
	std::span<const MenuState::Plugged> joysticks;
};

/* An input as the page says it */
std::string
label(const HostInput &input, int kind, const Others &others = {})
{
	static const char *const MOUSE[] = { "Mouse left", "Mouse middle", "Mouse right", "Mouse side", "Mouse extra" };

	switch (input.kind) {
	case HostInput::Kind::KEY:
		switch (input.key) {
		case SDLK_LEFT: return "←";
		case SDLK_RIGHT: return "→";
		case SDLK_UP: return "↑";
		case SDLK_DOWN: return "↓";
		case SDLK_RETURN: return "Enter";
		default: return SDL_GetKeyName(input.key);
		}
	case HostInput::Kind::PAD:
		if (input.pad.with >= 0) {
			const PadInput with = { PadInput::Type::BUTTON, static_cast<uint8_t>(input.pad.with) };

			return pad_label(with, kind) + " + " + pad_label(input.pad, kind);
		}
		return pad_label(input.pad, kind);
	case HostInput::Kind::JOY: {
		if (input.joy.device.empty())
			return joy_label(input.joy);

		const auto n = std::find(others.numbered.begin(), others.numbered.end(), input.joy.device);

		if (n != others.numbered.end())
			return joystick_number(static_cast<size_t>(n - others.numbered.begin())) + " " + joy_label(input.joy);
		return joy_label(input.joy) + " · " + joystick_name(input.joy.device, others.joysticks);
	}
	case HostInput::Kind::MOUSE:
		break;
	}
	switch (input.mouse.type) {
	case MouseInput::Type::X: return "Mouse X";
	case MouseInput::Type::Y: return "Mouse Y";
	case MouseInput::Type::OFF: return "Mouse off-screen";
	case MouseInput::Type::BUTTON: break;
	}
	return MOUSE[input.mouse.button % MOUSE_BUTTONS];
}

std::string
labels(const std::vector<HostInput> &inputs, int kind, const Others &others)
{
	std::string out;

	for (const HostInput &input : inputs)
		out += (out.empty() ? "" : ", ") + label(input, kind, others);
	return out;
}

bool
same(const std::optional<MenuSlot> &a, const MenuSlot &b)
{
	return a && a->control == b.control && a->part == b.part && a->role == b.role;
}

/* A control of the player shown, or a part of one: its name, what changes
 * it, its inputs on the device shown, lit while it is worked; a half of an
 * analog control on a gamepad, which the whole axis works already */
struct InputRow {
	std::string name;
	MenuSlot slot;
	std::vector<HostInput> inputs;
	bool lit, catchable, half = false;
};

/* The joysticks other than the player's that the inputs of these rows
 * name, in the order they first come: ① the first, ② the next... */
std::vector<std::string>
other_joysticks(std::span<const InputRow> rows)
{
	std::vector<std::string> out;

	for (const InputRow &row : rows)
		for (const HostInput &input : row.inputs)
			if (input.kind == HostInput::Kind::JOY && !input.joy.device.empty() &&
			    std::find(out.begin(), out.end(), input.joy.device) == out.end())
				out.push_back(input.joy.device);
	return out;
}

/* The controls of the player in the order of the game, then the coin, the
 * SERVICE of the player and TEST (player 1): an axis on a gamepad or a
 * joystick only (or the mouse that aims) */
std::vector<InputRow>
input_rows(const MenuState &state)
{
	const ControlPanel &panel = *state.panel;
	const Bindings &bindings = panel.bindings();
	const std::vector<GameControl> &controls = panel.controls();
	/* A gamepad or a joystick has axes: the whole of an analog control */
	const HostInput::Kind kind = state.shown_kind();
	const bool pad = kind != HostInput::Kind::KEY;
	/* A light gun: a mouse, keys, a joystick's buttons, and what the user
	 * put on another device (the gamepad) */
	const bool gun = !pad && state.gun_of(state.player);
	std::vector<InputRow> rows;
	std::optional<InputRow> service;
	const auto add = [&](std::string name, MenuSlot slot, std::vector<HostInput> inputs, bool lit, bool catchable) {
		std::erase_if(inputs, [kind, gun](const HostInput &in) {
			const bool keys = in.kind == HostInput::Kind::MOUSE || (gun && in.kind != HostInput::Kind::KEY);

			return (keys ? HostInput::Kind::KEY : in.kind) != kind;
		});
		rows.push_back({ std::move(name), slot, std::move(inputs), lit, catchable });
	};
	const auto worked = [&state, &controls](size_t i) {
		return state.touched.size() == controls.size() + MenuState::TOUCHED_ROLES && state.touched[i];
	};

	for (size_t c = 0; c < controls.size(); c++) {
		const GameControl &control = controls[c];
		const int i = static_cast<int>(c);
		const bool lit = worked(c);

		/* Away from the picture works itself (Bindings::read_settings) */
		if (control.player != state.player || control.role == Role::GUN_OFFSCREEN)
			continue;
		if (control.role == Role::SERVICE) {
			add("SERVICE", { i, 0, Role::SERVICE }, bindings.part(c, 0), lit, true);
			service = rows.back();
			rows.pop_back();
			continue;
		}
		switch (control.type) {
		case GameControl::Type::BUTTON:
			add(control.name, { i, 0, control.role }, bindings.part(c, 0), lit, true);
			break;
		case GameControl::Type::POSITIONS:
			/* Its two ways, as in odin.ini (gear_up: GEAR UP) */
			add(MenuKit::capitals(control.id) + " UP", { i, 2, control.role }, bindings.part(c, 2), lit, true);
			add(MenuKit::capitals(control.id) + " DOWN", { i, 1, control.role }, bindings.part(c, 1), lit, true);
			break;
		case GameControl::Type::ANALOG: {
			const char *const left = GameControl::half(control.low, false);
			const char *const right = GameControl::half(control.low, true);
			const bool mouse = std::any_of(bindings.part(c, 0).begin(), bindings.part(c, 0).end(),
			                               [](const HostInput &in) { return in.kind == HostInput::Kind::MOUSE; });
			/* A sight: an axis whole, which the mouse or the light gun aims */
			const bool whole = pad || control.role == Role::GUN_X || control.role == Role::GUN_Y;

			if (whole || mouse)
				add(control.name, { i, 0, control.role }, bindings.part(c, 0), lit, whole);
			if (!left || !right) {
				add(pad ? control.name + " (button)" : control.name, { i, 2, control.role }, bindings.part(c, 2), lit,
				    true);
				rows.back().half = pad;
				break;
			}
			add(control.name + " " + MenuKit::capitals(left + 1), { i, 1, control.role }, bindings.part(c, 1), lit, true);
			rows.back().half = whole;
			add(control.name + " " + MenuKit::capitals(right + 1), { i, 2, control.role }, bindings.part(c, 2), lit, true);
			rows.back().half = whole;
			break;
		}
		}
	}

	const Role coin = state.player ? Role::COIN2 : Role::COIN1;

	add("COIN", { -1, 0, coin }, bindings.cabinet(coin),
	    worked(controls.size() + MenuState::TOUCHED_COIN1 + state.player), true);
	if (service)
		rows.push_back(*service);
	if (state.player == 0)
		add("TEST", { -1, 0, Role::TEST }, bindings.cabinet(Role::TEST),
		    worked(controls.size() + MenuState::TOUCHED_TEST), true);
	return rows;
}

/* Whether two parts are of the same control (or role of the cabinet) */
bool
same_control(const MenuSlot &a, const MenuSlot &b)
{
	return a.control == b.control && (a.control >= 0 || a.role == b.role);
}

/* The next input of these parts is caught, each in turn; none when the
 * first is caught already */
void
catch_parts(MenuState &state, const std::vector<MenuSlot> &parts)
{
	if (parts.empty() || same(state.catching, parts.front())) {
		state.catching.reset();
		state.catch_next.clear();
		return;
	}
	state.catching = parts.front();
	state.catch_next.assign(parts.begin() + 1, parts.end());
}

/* A part caught, or else the one the frame of the gamepad is on: drawn in
 * the accent */
bool
is_chosen(const MenuState &state, const MenuSlot &slot)
{
	return state.catching ? same(state.catching, slot) : same(state.picked, slot);
}

/* A control of the player as Classic and the line at the bottom show it:
 * its name, its parts (rows of input_rows) each after its sign (· before
 * a half on a gamepad), how far it is worked now (0 to 1) */
struct Group {
	struct Part {
		size_t row;
		const char *sign;
	};

	std::string name;
	std::vector<Part> parts;
	bool lit = false, chosen = false;
	float amount = 0.0f;

	/* Its parts caught in turn on a click: not the halves of an axis on a
	 * gamepad, which a button pressed for the whole takes */
	std::vector<MenuSlot> to_catch(std::span<const InputRow> rows) const
	{
		std::vector<MenuSlot> out;

		for (const Part &part : parts)
			if (rows[part.row].catchable && !rows[part.row].half)
				out.push_back(rows[part.row].slot);
		return out;
	}
};

std::vector<Group>
groups(const MenuState &state, std::span<const InputRow> rows)
{
	const std::vector<GameControl> &controls = state.panel->controls();
	std::vector<Group> out;

	for (size_t r = 0; r < rows.size(); r++) {
		const InputRow &row = rows[r];
		const int c = row.slot.control;
		const bool picked = is_chosen(state, row.slot);

		/* A half on a gamepad, empty, says nothing */
		if (row.half && row.inputs.empty() && !picked)
			continue;
		/* A lever's two ways are two controls: GEAR UP, GEAR DOWN */
		const bool lever = c >= 0 && controls[static_cast<size_t>(c)].type == GameControl::Type::POSITIONS;

		if (out.empty() || c < 0 || lever || rows[out.back().parts.front().row].slot.control != c) {
			Group group;

			group.name = c >= 0 && controls[static_cast<size_t>(c)].type == GameControl::Type::ANALOG ?
			             controls[static_cast<size_t>(c)].name : row.name;
			if (c >= 0 && static_cast<size_t>(c) < state.now.size()) {
				const ControlNow &now = state.now[static_cast<size_t>(c)];

				group.amount = controls[static_cast<size_t>(c)].type == GameControl::Type::ANALOG ?
				               std::min(1.0f, std::abs(now.value)) : now.on ? 1.0f : 0.0f;
			}
			out.push_back(std::move(group));
		}

		Group &group = out.back();
		const char *sign = "";

		if (c >= 0 && row.half)
			sign = "·";
		group.parts.push_back({ r, sign });
		group.lit = group.lit || row.lit;
		group.chosen = group.chosen || picked;
		if (c < 0 || lever)
			group.amount = row.lit ? 1.0f : 0.0f;
	}
	return out;
}

/* A part as a chip says it: its sign, then its inputs, "press..." while
 * caught, "-" for none */
std::string
part_text(const MenuState &state, std::span<const InputRow> rows, const Group::Part &part, int kind,
          const Others &others)
{
	const InputRow &row = rows[part.row];
	const std::string inputs = same(state.catching, row.slot) ? "press..." :
	                           row.inputs.empty() ? "-" : labels(row.inputs, kind, others);

	return *part.sign ? std::string(part.sign) + " " + inputs : inputs;
}

/* What part of the drawing an input of a gamepad is, and the key of its
 * label (the inputs of one label: a button, a stick, a trigger); -1: none */
std::pair<int, int>
part_of(const PadDrawing &drawing, const PadInput &pad)
{
	PadPartKind kind = PadPartKind::BUTTON;
	unsigned input = pad.index;
	int key = pad.index;

	if (pad.type == PadInput::Type::BUTTON) {
		if (pad.index == SDL_GAMEPAD_BUTTON_LEFT_STICK || pad.index == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
			kind = PadPartKind::STICK;
			input = pad.index - SDL_GAMEPAD_BUTTON_LEFT_STICK;
		}
	} else if (pad.is_trigger()) {
		kind = PadPartKind::TRIGGER;
		key = 200 + pad.index;
	} else {
		input = pad.index / 2;
		key = 100 + static_cast<int>(input);
		/* A stick's direction points at its well, if it has one */
		for (size_t p = 0; p < drawing.parts.size(); p++)
			if (drawing.parts[p].kind == PadPartKind::WELL && drawing.parts[p].input == input)
				return { static_cast<int>(p), key };
		kind = PadPartKind::STICK;
	}
	for (size_t p = 0; p < drawing.parts.size(); p++)
		if (drawing.parts[p].kind == kind && drawing.parts[p].input == input)
			return { static_cast<int>(p), key };
	return { -1, key };
}

/* Whether the inputs of a label are worked now */
bool
touched(int key, const PadState &now)
{
	if (key >= 200)
		return now.axes[static_cast<size_t>(key - 200)] > TRIGGER_TOUCHED;
	if (key >= 100) {
		const size_t s = static_cast<size_t>(key - 100);
		const float x = static_cast<float>(now.axes[2 * s]) / 32767.0f;
		const float y = static_cast<float>(now.axes[2 * s + 1]) / 32767.0f;

		return std::hypot(x, y) > STICK_TOUCHED || now.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK + s];
	}
	return now.buttons[static_cast<size_t>(key)];
}

/* Whether the game takes that input of a label now, as ControlPanel reads
 * it: a button of a shortcut, pressed with the hotkey button held, is
 * Odin's (PadState::shortcut), unless the input is that pair */
bool
taken(const PadInput &input, int key, const PadState &now)
{
	const bool on = touched(key, now);

	if (!on || key >= 100)
		return on;
	return input.with >= 0 ? now.buttons[static_cast<size_t>(input.with)] : !now.shortcut[static_cast<size_t>(key)];
}

/* A name beside the drawing: the controls on one input of it, their text,
 * the first of them caught by a click; the box of its part, the side it is
 * on, its height */
struct Label {
	std::vector<std::string> names;
	size_t row;
	int key;
	size_t part;
	std::string text = {};
	ImVec2 lo = {}, hi = {};
	bool left = false, chosen = false, hot = false;
	float y = 0.0f;
};

/* The text of a label: its names, the words they begin with said once
 * ("1P START · WATER", "1P X AXIS · Y AXIS"); names that differ only by
 * their last word are one control, its directions say nothing more, when
 * there are three of them or more ("LEFT-LEVER") or when that word is a
 * letter or a sign ("VR", for VR L and VR R) */
std::string
label_text(std::span<const std::string> names)
{
	const auto stem = [](const std::string &name) {
		const size_t space = name.rfind(' ');

		return space == std::string::npos ? std::string() : name.substr(0, space);
	};
	/* One character, of one byte or of a sign */
	const auto mark = [](const std::string &name) {
		const size_t space = name.rfind(' ');
		const size_t length = space == std::string::npos ? 0 : name.size() - space - 1;

		return length == 1 || (length == 3 && static_cast<unsigned char>(name[space + 1]) >= 0xE0);
	};
	const std::string first = stem(names[0]);

	if (names.size() >= 2 && !first.empty() &&
	    std::all_of(names.begin(), names.end(), [&](const std::string &n) { return stem(n) == first; }) &&
	    (names.size() >= 3 || std::all_of(names.begin(), names.end(), mark)))
		return first;

	/* How much of a name, whole words, the one before begins with too */
	const auto shared = [](const std::string &before, const std::string &now) {
		size_t words = 0;

		for (size_t i = 0; i < std::min(before.size(), now.size()) && before[i] == now[i]; i++)
			if (now[i] == ' ')
				words = i + 1;
		return words;
	};
	std::string text = names[0];

	for (size_t i = 1; i < names.size(); i++)
		text += " · " + names[i].substr(shared(names[i - 1], names[i]));
	return text;
}

} /* namespace */

std::string
Menu::input_name(const PadInput &pad, int kind)
{
	return pad_label(pad, kind);
}

std::string
Menu::button_name(int button, int kind)
{
	PadInput pad{};

	pad.type = PadInput::Type::BUTTON;
	pad.index = static_cast<uint8_t>(button);
	return pad_label(pad, kind);
}

/* The games the row shows: all, or with `links_only` those that link
 * cabinets */
std::vector<size_t>
Menu::row_games(const MenuState &state, bool links_only)
{
	std::vector<size_t> games;

	for (size_t g = 0; g < state.games.size(); g++)
		if (!links_only || (g < state.game_links.size() && state.game_links[g]))
			games.push_back(g);
	return games;
}

/* The games between two arrows: the one shown by its name in the middle,
 * marked, the one before and the one after it by theirs (only one at an end
 * of the list), the others as dots beyond them, as many as come before and
 * after (a ▶ before the name of the one that runs, a ring for its dot), over
 * a dot its name, a click going there, a change sliding the names and the
 * dots (unless [menu] animations = off). The pills of the buttons of a gamepad
 * that change it at the ends, or else a row of the frame, left and right
 * changing it (`focusable`). Only the games that link cabinets with
 * `links_only`: a game shown that is not one of them gives its place to
 * the game that runs, or else to the first. Its bottom. */
float
Menu::games_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width, bool focusable, bool links_only)
{
	static constexpr double MOVE_S = 0.18;
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const std::vector<size_t> games = row_games(state, links_only);
	const size_t n = games.size();

	if (!n)
		return at.y;

	/* The place of a game in the row; n when it is not in it */
	const auto in_row = [&games](size_t game) {
		return static_cast<size_t>(std::find(games.begin(), games.end(), game) - games.begin());
	};
	size_t shown = in_row(state.shown);

	if (shown == n) {
		shown = in_row(state.running) < n ? in_row(state.running) : 0;
		if (!state.show)
			state.show = games[shown];
	}

	const float size = ImGui::GetFontSize(), line = ImGui::GetTextLineHeight(), spacing = kit.px(14.0f);
	const float dot = kit.px(2.5f), pitch = kit.px(9.0f), letters = size * 0.1f;
	const ImVec2 arrow = kit.arrow_size();
	/* The game that runs: ▶ */
	const auto play = [list](ImVec2 c, float r, ImU32 colour) {
		list->AddTriangleFilled({ c.x - 0.8f * r, c.y - r }, { c.x - 0.8f * r, c.y + r }, { c.x + r, c.y }, colour);
	};
	std::vector<std::string> names;

	for (const size_t g : games)
		names.push_back(MenuKit::capitals(state.games[g].second));

	static const std::string none;
	const std::string &before = focusable ? none : state.pills[MenuState::PILL_GAME_BEFORE];
	const std::string &after = focusable ? none : state.pills[MenuState::PILL_GAME_AFTER];
	const float before_w = before.empty() ? 0.0f : kit.pill_size(before.c_str()).x + spacing;
	const float after_w = after.empty() ? 0.0f : kit.pill_size(after.c_str()).x + spacing;
	std::vector<float> widths;

	for (const std::string &name : names)
		widths.push_back(MenuKit::measure(name.c_str(), 0.0f, letters) + 4.0f * dot);
	/* The arrows at the ends of the row, the names and the dots between
	 * them; where the row is short, the neighbours' names are cut first,
	 * down to a few letters, then the name shown */
	const float h = arrow.y, mid = at.y + h / 2.0f;
	const float x0 = at.x + before_w;
	const float middle = std::max(width - before_w - after_w - 2.0f * (arrow.x + spacing), 0.0f);
	const float centre = x0 + arrow.x + spacing + middle / 2.0f;

	if (focusable && kit.focus({ at.x, at.y }, { at.x + width, at.y + h }, kit.px(3.0f)) && kit.side())
		state.show = games[(shown + n + (kit.side() < 0 ? n - 1 : 1)) % n];
	if (!before.empty())
		kit.pill(list, { at.x, mid - kit.pill_size(before.c_str()).y / 2.0f }, before.c_str());
	ImGui::PushID("games");
	if (kit.arrow("before", { x0, at.y }, false))
		state.show = games[(shown + n - 1) % n];
	if (kit.arrow("after", { x0 + arrow.x + 2.0f * spacing + middle, at.y }, true))
		state.show = games[(shown + 1) % n];
	if (!after.empty())
		kit.pill(list, { x0 + 2.0f * (arrow.x + spacing) + middle + spacing,
		                 mid - kit.pill_size(after.c_str()).y / 2.0f }, after.c_str());

	/* The names when `c` is the one shown: the one before, it, the one after */
	struct Named {
		std::string text[3];
		float width[3] = {};
	};
	const auto fit = [&](size_t g, float room, std::string &text, float &w) {
		const float letters_w = letters * static_cast<float>(names[g].size() - 1);

		text = widths[g] <= room ? names[g] : MenuKit::fitted(names[g], std::max(room - 4.0f * dot - letters_w, 0.0f));
		w = std::min(MenuKit::measure(text.c_str(), 0.0f, letters) + 4.0f * dot, std::max(room, 0.0f));
	};
	/* A neighbour keeps a few letters; the dots beyond it take what room is
	 * left, those past the arrows unseen */
	const auto least = [&](size_t g) { return std::min(widths[g], 6.0f * size); };
	const auto dots_w = [&](size_t k) { return k ? spacing + static_cast<float>(k) * pitch : 0.0f; };
	const auto named_at = [&](size_t c) {
		Named out;
		const float left = c > 0 ? spacing + least(c - 1) : 0.0f, right = c + 1 < n ? spacing + least(c + 1) : 0.0f;

		fit(c, middle - 2.0f * std::max(left, right), out.text[1], out.width[1]);

		const float half = (middle - out.width[1]) / 2.0f;

		if (c > 0)
			fit(c - 1, std::max(half - spacing - dots_w(c > 1 ? c - 1 : 0), least(c - 1)), out.text[0], out.width[0]);
		if (c + 1 < n)
			fit(c + 1, std::max(half - spacing - dots_w(c + 2 < n ? n - c - 2 : 0), least(c + 1)), out.text[2],
			    out.width[2]);
		return out;
	};
	/* Whether game `g` is a name when `c` is the one shown, and where it is */
	const auto is_name = [](size_t c, size_t g) { return g + 1 >= c && g <= c + 1; };
	const auto place = [&](size_t c, const Named &at_c, size_t g) {
		const float w = at_c.width[1] / 2.0f;

		if (g == c)
			return centre;
		if (g + 1 == c)
			return centre - w - spacing - at_c.width[0] / 2.0f;
		if (g == c + 1)
			return centre + w + spacing + at_c.width[2] / 2.0f;
		if (g < c)
			return centre - w - spacing - at_c.width[0] - spacing - (static_cast<float>(c - 1 - g) - 0.5f) * pitch;
		return centre + w + spacing + at_c.width[2] + spacing + (static_cast<float>(g - c - 1) - 0.5f) * pitch;
	};

	/* The move from the game shown at the last picture, eased out */
	if (!state.games_was || in_row(*state.games_was) == n)
		state.games_was = state.games_from = games[shown];
	if (*state.games_was != games[shown]) {
		state.games_from = *state.games_was;
		state.games_since = ImGui::GetTime();
		state.games_was = games[shown];
	}

	const size_t was = in_row(state.games_from);
	const bool moves = state.animations && was < n && was != shown;
	const double t = moves ? (ImGui::GetTime() - state.games_since) / MOVE_S : 1.0;
	const float u = 1.0f - std::pow(1.0f - static_cast<float>(std::clamp(t, 0.0, 1.0)), 3.0f);
	const size_t from = u < 1.0f ? was : shown;
	const Named at_from = named_at(from), at_shown = named_at(shown);

	state.moving = state.moving || from != shown;
	for (size_t g = 0; g < n; g++) {
		const float gx = place(from, at_from, g) + (place(shown, at_shown, g) - place(from, at_from, g)) * u;
		/* How far it is a name: those around the game shown come to it, those
		 * around the one before go from it */
		const float named = (is_name(from, g) ? 1.0f - u : 0.0f) + (is_name(shown, g) ? u : 0.0f);

		if (named < 1.0f && std::abs(gx - centre) <= middle / 2.0f) {
			const float a = 1.0f - named;

			if (games[g] == state.running)
				list->AddCircle({ gx, mid }, dot, MenuKit::alpha(palette.dim, a), 0, kit.stroke());
			else
				list->AddCircleFilled({ gx, mid }, dot, MenuKit::alpha(palette.outline, a));
			if (!is_name(shown, g)) {
				ImGui::SetCursorScreenPos({ gx - pitch / 2.0f, at.y });
				ImGui::PushID(static_cast<int>(g));
				if (ImGui::InvisibleButton("game", { pitch, h }))
					state.show = games[g];
				if (ImGui::IsItemHovered())
					kit.tip(state.games[games[g]].second.c_str());
				ImGui::PopID();
			}
		}
		if (named > 0.0f) {
			/* Smaller and fainter as it comes from a dot or goes to one */
			const bool now = is_name(shown, g);
			const std::string &text = (now ? at_shown : at_from).text[g + 1 - (now ? shown : from)];
			const float k = 0.3f + 0.7f * std::min(named, 1.0f), s = size * k;
			const float w = MenuKit::measure(text.c_str(), s, letters * k), left = gx - (w + 4.0f * dot * k) / 2.0f;
			const bool chosen = u >= 0.5f ? g == shown : g == from;
			const ImU32 colour = MenuKit::alpha(chosen ? palette.action : palette.dim, named * named);

			MenuKit::write(list, { left, mid - line * k / 2.0f }, colour, text.c_str(), s, letters * k);
			if (chosen)
				list->AddLine({ left, mid + line * k / 2.0f + kit.px(2.0f) },
				              { left + w, mid + line * k / 2.0f + kit.px(2.0f) }, colour, kit.stroke());
			if (games[g] == state.running)
				play({ left - kit.px(8.0f), mid }, 1.6f * dot * k, MenuKit::alpha(colour, 0.9f));
			if (now && g != shown) {
				ImGui::SetCursorScreenPos({ left, at.y });
				ImGui::PushID(static_cast<int>(g));
				if (ImGui::InvisibleButton("name", { std::max(w, 1.0f), h }))
					state.show = games[g];
				ImGui::PopID();
			}
		}
	}
	ImGui::PopID();
	return at.y + h;
}

namespace {

/* The device of each player: the keyboard or a gamepad plugged in, the one
 * that plays chosen, each with the drawing it shows and whether it has a
 * wire; in a game of two, a click on P1 or P2 shows that player. Its
 * bottom. */
float
devices_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width, bool two)
{
	static constexpr char FOOT[] = "Each game remembers its own devices. The keyboard and mouse always work.";
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float h = kit.picker_height(), spacing = kit.px(10.0f);
	const unsigned players = two ? 2 : 1;
	const float each = (width - spacing * static_cast<float>(players - 1)) / static_cast<float>(players);

	for (unsigned player = 0; player < players; player++) {
		/* The keyboard, then each gamepad plugged in (the same name twice
		 * counted) */
		std::vector<MenuKit::Item> devices = {
			{ "Keyboard and mouse", "Keyboard + mouse", {}, MenuKit::Icon::KEYBOARD, false },
		};
		int device = 0;
		const auto add = [&](const std::vector<MenuState::Plugged> &plugged, MenuState::Device shown, bool pads) {
			for (size_t i = 0; i < plugged.size(); i++) {
				const MenuState::Plugged &one = plugged[i];
				const auto before = std::count_if(plugged.begin(), plugged.begin() + static_cast<std::ptrdiff_t>(i),
				                                  [&one](const MenuState::Plugged &other) { return other.name == one.name; });
				const auto type = static_cast<SDL_JoystickType>(one.kind);
				const bool off = !pads && type == SDL_JOYSTICK_TYPE_WHEEL && !state.steers;

				if (state.input_device[player] == shown && one.player == static_cast<int>(player))
					device = static_cast<int>(devices.size());
				devices.push_back({ before ? one.name + " (" + std::to_string(before + 1) + ")" : one.name,
				                    pads ? std::string("Controller · ") + pad_drawing_name(one.kind) :
				                    type == SDL_JOYSTICK_TYPE_WHEEL        ? "Wheel" :
				                    type == SDL_JOYSTICK_TYPE_FLIGHT_STICK ? "Flight stick" : "Joystick",
				                    off                                                ? "not for this game" :
				                    one.connection == SDL_JOYSTICK_CONNECTION_WIRED    ? "WIRED" :
				                    one.connection == SDL_JOYSTICK_CONNECTION_WIRELESS ? "WIRELESS" : "",
				                    pads || type != SDL_JOYSTICK_TYPE_WHEEL ? MenuKit::Icon::PAD : MenuKit::Icon::WHEEL,
				                    !off, {}, off });
			}
		};

		add(state.plugged, MenuState::Device::PAD, true);
		add(state.joysticks, MenuState::Device::JOYSTICK, false);

		float x = at.x + static_cast<float>(player) * (each + spacing);
		float w = each;

		ImGui::PushID(static_cast<int>(player));
		if (two) {
			const char *const name = player ? "P2" : "P1";
			const ImVec2 size = ImGui::CalcTextSize(name);
			const bool shown = state.player == player;
			const bool focused = kit.focus({ x - kit.px(4.0f), at.y }, { x + size.x + kit.px(4.0f), at.y + h });

			ImGui::SetCursorScreenPos({ x, at.y });
			if (ImGui::InvisibleButton("player", { size.x + kit.px(8.0f), h }) || (focused && kit.pressed())) {
				state.player = player;
				state.catching.reset();
				state.catch_next.clear();
			}
			list->AddText({ x, at.y + (h - size.y) / 2.0f }, shown ? palette.ink : palette.dim, name);
			x += size.x + kit.px(8.0f);
			w -= size.x + kit.px(8.0f);
		}
		const MenuKit::Icon kind = devices[static_cast<size_t>(device)].icon;

		if (kit.picker("device", { x, at.y }, w, devices, device, kind, FOOT)) {
			const size_t chosen = static_cast<size_t>(device), pads = state.plugged.size();

			state.player = player;
			state.input_device[player] = chosen == 0 ? MenuState::Device::KEYBOARD :
			                             chosen <= pads ? MenuState::Device::PAD : MenuState::Device::JOYSTICK;
			state.device_picked = { player, state.input_device[player] };
			state.catching.reset();
			/* A gamepad or a joystick takes the place of the light gun */
			if (chosen >= 1 && !state.gun[player].empty())
				state.want_gun = std::pair(player, LightGun());
			if (chosen >= 1 && chosen <= pads)
				state.want = state.plugged[chosen - 1];
			else if (chosen > pads && chosen - pads - 1 < state.joysticks.size())
				state.want_joystick = state.joysticks[chosen - pads - 1];
		}
		ImGui::PopID();
	}
	return at.y + h;
}

/* The light gun of each player with a sight, under their device: none, or
 * one of those seen (one unplugged stays listed). Its bottom. */
float
guns_row(const MenuKit &kit, MenuState &state, ImVec2 at, float width, bool two)
{
	static constexpr char FOOT[] = "Each light gun, or each mouse read separately, aims for one player only.";
	const float h = kit.picker_height(), spacing = kit.px(10.0f);
	const unsigned players = two ? 2 : 1;
	const float each = (width - spacing * static_cast<float>(players - 1)) / static_cast<float>(players);
	/* Under the picker of the device, past the name of the player */
	const float past = two ? ImGui::CalcTextSize("P1").x + kit.px(8.0f) : 0.0f;

	for (unsigned player = 0; player < players; player++) {
		if (!state.sights[player])
			continue;

		std::vector<MenuKit::Item> items = {
			{ "No light gun", player ? "player 2 does not aim" : "the mouse aims", {}, MenuKit::Icon::NONE, false },
		};
		int chosen = 0;

		for (const LightGun &gun : state.guns) {
			if (gun.identity == state.gun[player])
				chosen = static_cast<int>(items.size());
			items.push_back({ gun.name, gun.aims ? "light gun" : "mouse", gun.present ? "" : "UNPLUGGED",
			                  MenuKit::Icon::NONE, !gun.present });
		}
		ImGui::PushID(static_cast<int>(player));
		if (kit.picker("gun", { at.x + static_cast<float>(player) * (each + spacing) + past, at.y }, each - past, items,
		               chosen, MenuKit::Icon::NONE, FOOT)) {
			/* That player shown, as for a device */
			state.player = player;
			state.catching.reset();
			state.want_gun = std::pair(player, chosen ? state.guns[static_cast<size_t>(chosen) - 1] : LightGun());
		}
		ImGui::PopID();
	}
	return at.y + h;
}

/* The width of a label column: its longest name, within bounds */
float
column_width(const MenuKit &kit, std::span<const Label> labels, float box)
{
	float longest = 0.0f;

	for (const Label &l : labels)
		longest = std::max(longest, MenuKit::measure(l.text.c_str()));
	return std::clamp(longest + kit.px(16.0f), kit.px(COLUMN_MIN), box * 0.3f);
}

/* The row of the controls with no input, centred at the bottom of the box:
 * its height; drawn and clicked when `draw` */
float
not_mapped(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, std::span<const size_t> empty, ImVec2 a,
           ImVec2 b, bool draw)
{
	if (empty.empty())
		return 0.0f;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const char *const title = "NOT MAPPED";
	const float line = ImGui::GetTextLineHeight(), wide = kit.px(9.0f), high = kit.px(2.0f);
	const float h = line + 2.0f * high, gap = kit.px(6.0f), room = b.x - a.x - 2.0f * kit.px(MenuKit::BOX_INSIDE);
	const float tiny = MenuKit::font(MenuKit::TINY), letters = tiny * 0.12f;
	/* The lines of chips: the title first, then each name */
	std::vector<std::vector<std::pair<int, float>>> lines(1);
	float used = MenuKit::measure(title, tiny, letters) + gap;

	lines[0].push_back({ -1, used - gap });
	for (const size_t r : empty) {
		const float w = MenuKit::measure(rows[r].name.c_str()) + 2.0f * wide;

		if (used + w > room && !lines.back().empty()) {
			lines.emplace_back();
			used = 0.0f;
		}
		lines.back().push_back({ static_cast<int>(r), w });
		used += w + gap;
	}

	const float height = static_cast<float>(lines.size()) * (h + gap);

	if (!draw)
		return height;

	float y = b.y - kit.px(MenuKit::BOX_INSIDE) - height + gap;

	for (const auto &chips : lines) {
		float total = -gap;

		for (const auto &[r, w] : chips)
			total += w + gap;

		float x = a.x + (b.x - a.x - total) / 2.0f;

		for (const auto &[r, w] : chips) {
			if (r < 0) {
				MenuKit::write(list, { x, y + (h - tiny) / 2.0f }, palette.faint, title, tiny, letters);
				x += w + gap;
				continue;
			}

			const InputRow &row = rows[static_cast<size_t>(r)];
			const bool picked = is_chosen(state, row.slot);

			ImGui::SetCursorScreenPos({ x, y });
			ImGui::PushID(r);
			if (ImGui::InvisibleButton("empty", { w, h }) && row.catchable)
				catch_parts(state, { row.slot });

			const bool hovered = ImGui::IsItemHovered();

			ImGui::PopID();
			if (picked)
				kit.frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, palette.accent);
			else if (hovered)
				kit.frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, palette.action);
			else
				kit.frame(list, { x, y }, { x + w, y + h }, kit.px(3.0f), 0, MenuKit::alpha(palette.action, MenuKit::ACTION_LINE));
			MenuKit::write(list, { x + wide, y + high }, picked ? palette.accent : palette.action, row.name.c_str());
			x += w + gap;
		}
		y += h + gap;
	}
	return height;
}

/* Start mapping: once a control is caught or its time is over, the next
 * one of the player (not a half the whole axis works); the end after the
 * last, or when the user catches another one */
void
next_mapping(MenuState &state, std::span<const InputRow> rows)
{
	if (!state.mapping)
		return;

	/* The control caught now; past the rows: none yet */
	size_t &i = *state.mapping;
	const uint64_t now = SDL_GetTicks();

	if (state.catching) {
		if (i >= rows.size() || !same_control(*state.catching, rows[i].slot)) {
			state.mapping.reset();
			return;
		}
		if (now - state.mapping_since < MAPPING_MS)
			return;
	}
	for (i = i >= rows.size() ? 0 : i + 1; i < rows.size(); i++)
		if (rows[i].catchable && !rows[i].half)
			break;
	state.catch_next.clear();
	if (i >= rows.size()) {
		state.mapping.reset();
		state.catching.reset();
		return;
	}
	state.catching = rows[i].slot;
	state.mapping_since = now;
}

/* The cabinet of the game shown: in its own box, its name and the game's
 * at the top (`framed`), or in the box of the device; at the top, the
 * cabinets the game was sold in in place of its name, when there are
 * more than one (NASCAR: Standard, Deluxe), the game that runs taking the
 * one chosen when it is reset */
void
cabinet_box(const MenuKit &kit, MenuState &state, ImVec2 a, ImVec2 b, bool framed)
{
	if (!state.panel || state.shown >= state.games.size())
		return;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const std::vector<GameControl> &controls = state.panel->controls();
	const size_t n = controls.size();
	const auto service = std::find_if(controls.begin(), controls.end(),
	                                  [](const GameControl &c) { return c.role == Role::SERVICE && c.player == 0; });
	CabinetNow now{ controls, state.now };

	if (state.touched.size() == n + MenuState::TOUCHED_ROLES) {
		now.coin = state.touched[n + MenuState::TOUCHED_COIN1];
		now.test = state.touched[n + MenuState::TOUCHED_TEST];
	}
	if (service != controls.end() && static_cast<size_t>(service - controls.begin()) < state.now.size())
		now.service = state.now[static_cast<size_t>(service - controls.begin())].on;
	/* Caught, or else picked */
	if (const std::optional<MenuSlot> slot = state.catching ? state.catching : state.picked) {
		if (slot->control < 0)
			now.chosen = slot->role == Role::COIN1 ? "coin" : slot->role == Role::TEST ? "test" : "";
		else if (static_cast<size_t>(slot->control) < n)
			now.chosen = controls.begin() + slot->control == service ?
			             "service" : std::string_view(controls[static_cast<size_t>(slot->control)].id);
	}

	float top = a.y + kit.px(MenuKit::BOX_INSIDE);

	if (framed) {
		const std::string &title = state.games[state.shown].second;
		const float pad = kit.px(8.0f);

		const float tiny = MenuKit::font(MenuKit::TINY), small = MenuKit::font(MenuKit::SMALL);

		kit.frame(list, a, b, kit.px(MenuKit::BOX_CUT), 0, palette.line);
		MenuKit::write(list, { a.x + pad, a.y + kit.px(6.0f) + small - tiny }, palette.cabinet, "THE CABINET", tiny,
		               tiny * 0.14f);
		top = a.y + kit.px(6.0f) + small;
		if (state.cabinet_names.size() > 1) {
			std::vector<const char *> names;

			for (const std::string &name : state.cabinet_names)
				names.push_back(name.c_str());

			const ImVec2 cells = kit.segments_size(names);
			const ImVec2 at = { b.x - pad - cells.x, a.y + kit.px(6.0f) };

			/* The gamepad: left or right, or A, takes the next one */
			if (kit.focus(at, { at.x + cells.x, at.y + cells.y }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
				state.cabinet = (state.cabinet + (kit.side() < 0 ? static_cast<int>(names.size()) - 1 : 1)) %
				                static_cast<int>(names.size());
			kit.segments(at, names, state.cabinet);
			top = at.y + cells.y;
			if (state.cabinet_later) {
				static constexpr char LATER[] = "Reset the game to apply";
				const float note = MenuKit::font(MenuKit::NOTE);

				MenuKit::write(list, { b.x - pad - MenuKit::measure(LATER, note), top + kit.px(3.0f) }, palette.dim,
				               LATER, note);
				top += kit.px(3.0f) + note;
			}
		} else {
			MenuKit::write(list, { b.x - pad - MenuKit::measure(title.c_str(), small), a.y + kit.px(6.0f) },
			               palette.dim, title.c_str(), small);
		}
	}
	draw_cabinet(kit, state.games[state.shown].first, now, { a.x + kit.px(4.0f), top },
	             { b.x - kit.px(4.0f), b.y - kit.px(6.0f) });
}

/* The controls of the player sorted for the drawing: those on a part of it,
 * one label for each part's input; those on an input it has not; those on
 * nothing */
struct Sorted {
	std::vector<Label> labels;
	std::vector<std::pair<size_t, std::string>> elsewhere;
	std::vector<size_t> empty;
};

Sorted
sort_rows(const MenuState &state, std::span<const InputRow> rows, const PadDrawing &drawing, int kind)
{
	const PadState &now = state.pad_states[state.player & 1];
	Sorted out;

	for (size_t r = 0; r < rows.size(); r++) {
		if (rows[r].inputs.empty()) {
			if (!rows[r].half)
				out.empty.push_back(r);
			continue;
		}
		for (const HostInput &input : rows[r].inputs) {
			const auto [part, key] = part_of(drawing, input.pad);
			std::string name = rows[r].name;

			if (input.pad.with >= 0)
				name += " + " + MenuKit::capitals(pad_label({ PadInput::Type::BUTTON, static_cast<uint8_t>(input.pad.with) }, kind));
			if (part < 0) {
				out.elsewhere.push_back({ r, std::move(name) + " · " + label(input, kind) });
				continue;
			}

			const auto it = std::find_if(out.labels.begin(), out.labels.end(), [key](const Label &l) { return l.key == key; });

			if (it != out.labels.end()) {
				it->names.push_back(name);
				it->chosen |= is_chosen(state, rows[r].slot);
				continue;
			}

			Label l{ { name }, r, key, static_cast<size_t>(part) };

			l.chosen = is_chosen(state, rows[r].slot);
			l.hot = taken(input.pad, key, now);
			out.labels.push_back(std::move(l));
		}
	}
	for (Label &l : out.labels)
		l.text = label_text(l.names);
	return out;
}

/* The controls of the player sorted for the drawing of their light gun:
 * on a button of it, an input it sends as it comes; where it aims, and
 * what it does not send that the user added (a pedal, a button of the
 * gamepad), said elsewhere; the keys and the buttons of the gamepad a
 * control has by default, and the halves of an axis (the gun aims it
 * whole), not at all (the list shows them) */
Sorted
sort_gun_rows(const MenuState &state, std::span<const InputRow> rows, const PadDrawing &drawing,
              std::span<const std::optional<HostInput>> sends)
{
	const PadState &now = state.gun_states[state.player & 1];
	const std::vector<GameControl> &controls = state.panel->controls();
	const Bindings &bindings = state.panel->bindings();
	std::vector<std::string> aims;
	std::optional<size_t> aim_row;
	Sorted out;

	for (size_t r = 0; r < rows.size(); r++) {
		const InputRow &row = rows[r];
		const int c = row.slot.control;
		const std::vector<HostInput> &standard = c >= 0 ? bindings.standard(static_cast<size_t>(c), row.slot.part) :
		                                         bindings.cabinet_standard(row.slot.role);
		bool shown = false;

		if (c >= 0 && controls[static_cast<size_t>(c)].type == GameControl::Type::ANALOG && row.slot.part != 0)
			continue;
		for (const HostInput &input : row.inputs) {
			const auto sent = std::find(sends.begin(), sends.end(), input);
			const int key = static_cast<int>(sent - sends.begin());
			const auto part = std::find_if(drawing.parts.begin(), drawing.parts.end(),
			                               [key](const PadPart &p) { return p.input == key; });

			if (input.kind == HostInput::Kind::MOUSE && input.mouse.is_axis()) {
				aims.push_back(row.name);
				aim_row = aim_row.value_or(r);
				shown = true;
				continue;
			}
			if (sent == sends.end() || part == drawing.parts.end()) {
				const bool standing = input.kind == HostInput::Kind::KEY || input.kind == HostInput::Kind::PAD;

				if (!standing || std::find(standard.begin(), standard.end(), input) == standard.end()) {
					out.elsewhere.push_back({ r, row.name + " · " +
					                                 label(input, state.pad_types[state.player & 1],
					                                       Others{ {}, state.joysticks }) });
					shown = true;
				}
				continue;
			}
			shown = true;

			const auto it = std::find_if(out.labels.begin(), out.labels.end(), [key](const Label &l) { return l.key == key; });

			if (it != out.labels.end()) {
				it->names.push_back(row.name);
				it->chosen |= is_chosen(state, row.slot);
				continue;
			}

			Label l{ { row.name }, r, key, static_cast<size_t>(part - drawing.parts.begin()) };

			l.chosen = is_chosen(state, row.slot);
			l.hot = now.buttons[static_cast<size_t>(key)];
			out.labels.push_back(std::move(l));
		}
		if (!shown)
			out.empty.push_back(r);
	}
	if (aim_row)
		out.elsewhere.insert(out.elsewhere.begin(), { *aim_row, label_text(aims) + " · aim" });
	for (Label &l : out.labels)
		l.text = label_text(l.names);
	return out;
}

/* Each column from the top down, each label at the height of its part or
 * under the one above, then from the bottom up; the right one starts under
 * the corner of the box, the left one too in a game of two (1P and 2P) or
 * while the controls are tried (Test mode) */
void
place_labels(const MenuKit &kit, std::span<Label> labels, float top, float corner, float left_corner, float bottom)
{
	const float gap = kit.px(LABEL_GAP);

	for (const bool left : { true, false }) {
		std::vector<Label *> side;

		for (Label &l : labels)
			if (l.left == left)
				side.push_back(&l);
		std::sort(side.begin(), side.end(), [](const Label *p, const Label *q) { return p->lo.y + p->hi.y < q->lo.y + q->hi.y; });

		float y = -1e9f;

		for (Label *l : side)
			y = l->y = std::max({ (l->lo.y + l->hi.y) / 2.0f, y + gap, top + (left ? left_corner : corner) });
		y = 1e9f;
		for (auto it = side.rbegin(); it != side.rend(); ++it)
			y = (*it)->y = std::min({ (*it)->y, y - gap, bottom });
	}
}

/* A label: its name at the edge of its column, as wide as the room; a
 * click on it catches */
void
draw_label(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, const Label &l, float edge, float room)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const std::string text = MenuKit::fitted(l.text, room);
	const float line = ImGui::GetTextLineHeight(), w = MenuKit::measure(text.c_str());
	const ImVec2 at = { l.left ? edge - kit.px(6.0f) - w : edge + kit.px(6.0f), l.y - line / 2.0f };

	ImGui::SetCursorScreenPos(at);
	ImGui::PushID(l.key);
	if (ImGui::InvisibleButton("label", { w, line }) && rows[l.row].catchable)
		catch_parts(state, l.chosen ? std::vector<MenuSlot>() : std::vector<MenuSlot>{ rows[l.row].slot });
	ImGui::PopID();

	const ImU32 colour = l.hot || l.chosen ? palette.accent : palette.ink;

	MenuKit::write(list, at, colour, text.c_str());
	if (l.hot)
		list->AddLine({ at.x, at.y + line + kit.px(1.0f) }, { at.x + w, at.y + line + kit.px(1.0f) }, colour, kit.stroke());
}

/* The controls on an input the drawing has not, at the foot of the column
 * with fewer names; a click on one catches */
void
draw_elsewhere(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, const Sorted &sorted,
               ImVec2 a, ImVec2 b, float bottom)
{
	if (sorted.elsewhere.empty())
		return;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const float gap = kit.px(LABEL_GAP), line = ImGui::GetTextLineHeight(), w = (b.x - a.x) / 2.0f - kit.px(10.0f);
	const auto last = [&](bool left) {
		float y = a.y + kit.px(LABEL_EDGE) - gap;

		for (const Label &l : sorted.labels)
			if (l.left == left)
				y = std::max(y, l.y);
		return y;
	};
	const bool left = last(true) <= last(false);
	float y = std::max(last(left) + gap * 1.6f, bottom - static_cast<float>(sorted.elsewhere.size() - 1) * gap);

	for (const auto &[r, whole] : sorted.elsewhere) {
		const std::string text = MenuKit::fitted(whole, w);
		const float tw = MenuKit::measure(text.c_str());
		const ImVec2 at = { left ? a.x + kit.px(6.0f) : b.x - kit.px(6.0f) - tw, y - line / 2.0f };
		const bool chosen = is_chosen(state, rows[r].slot);

		ImGui::SetCursorScreenPos(at);
		ImGui::PushID(static_cast<int>(1000 + r));
		if (ImGui::InvisibleButton("elsewhere", { tw, line }) && rows[r].catchable)
			catch_parts(state, chosen ? std::vector<MenuSlot>() : std::vector<MenuSlot>{ rows[r].slot });

		const bool hovered = ImGui::IsItemHovered() && rows[r].catchable;

		ImGui::PopID();
		MenuKit::write(list, at,
		               chosen || rows[r].lit ? kit.palette().accent : hovered ? kit.palette().action : kit.palette().dim,
		               text.c_str());
		y += gap;
	}
}

/* A device drawn in the box (a gamepad, a light gun), the name of each
 * control beside the part that works it, both in the accent while it is
 * worked. A control on an input the drawing has not is said at the foot of
 * a column; a control on nothing waits in the row at the bottom (Not
 * mapped). A click on a name, or in that row, catches its next input. */
void
drawn_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner,
          const PadDrawing &drawing, Sorted &sorted, const PadState &now)
{
	const float tray = not_mapped(kit, state, rows, sorted.empty, a, b, false);
	const float column = column_width(kit, sorted.labels, b.x - a.x), inside = kit.px(MenuKit::BOX_INSIDE);
	const float bottom = b.y - kit.px(LABEL_EDGE) - tray;
	/* Under the drawing, the room of what is said at the foot */
	const float foot = static_cast<float>(sorted.elsewhere.size()) * kit.px(LABEL_GAP);
	const PadView view(drawing, { a.x + column, a.y + inside }, { b.x - column, b.y - inside - tray - foot });
	uint32_t chosen = 0;

	for (Label &l : sorted.labels) {
		const auto [lo, hi] = view.box(l.part);

		l.lo = lo, l.hi = hi;
		l.left = (lo.x + hi.x) / 2.0f < (a.x + b.x) / 2.0f;
		if (l.chosen)
			chosen |= 1u << l.part;
	}
	view.draw(ImGui::GetWindowDrawList(), kit.palette(), now, chosen, kit.px(1.0f));
	place_labels(kit, sorted.labels, a.y + kit.px(LABEL_EDGE), corner,
	             state.panel->two_players() || state.testing ? corner : 0.0f, bottom);
	for (Label &l : sorted.labels)
		draw_label(kit, state, rows, l, l.left ? a.x + column : b.x - column, column - kit.px(10.0f));
	draw_elsewhere(kit, state, rows, sorted, a, b, bottom);
	not_mapped(kit, state, rows, sorted.empty, a, b, true);
}

/* The gamepad of the player */
void
pad_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	const unsigned me = state.player & 1;
	const int kind = state.pad_types[me];
	const PadDrawing &drawing = pad_drawing(kind);
	Sorted sorted = sort_rows(state, rows, drawing, kind);

	drawn_box(kit, state, rows, a, b, corner, drawing, sorted, state.pad_states[me]);
}

/* The light gun of the player, by its kind */
void
gun_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	const unsigned me = state.player & 1;
	const LightGun &gun = *state.gun_of(me);
	const GunKind kind = gun_kind(gun.name, gun.identity);
	const PadDrawing &drawing = gun_drawing(kind);
	const auto sends = gun_inputs(kind, gun.name, gun.identity);
	Sorted sorted = sort_gun_rows(state, rows, drawing, sends);

	drawn_box(kit, state, rows, a, b, corner, drawing, sorted, state.gun_states[me]);
}

/* The keys of the drawing of the keyboard, row by row: the key, its name,
 * its width in keys */
struct DrawnKey {
	SDL_Keycode key;
	const char *name;
	float width;
};

constexpr DrawnKey KEY_ROWS[5][14] = {
	{ { SDLK_ESCAPE, "Esc", 1 }, { SDLK_1, "1", 1 }, { SDLK_2, "2", 1 }, { SDLK_3, "3", 1 }, { SDLK_4, "4", 1 },
	  { SDLK_5, "5", 1 }, { SDLK_6, "6", 1 }, { SDLK_7, "7", 1 }, { SDLK_8, "8", 1 }, { SDLK_9, "9", 1 },
	  { SDLK_0, "0", 1 }, { SDLK_MINUS, "-", 1 }, { SDLK_EQUALS, "=", 1 }, { SDLK_BACKSPACE, "⌫", 2 } },
	{ { SDLK_TAB, "Tab", 1.5f }, { SDLK_Q, "Q", 1 }, { SDLK_W, "W", 1 }, { SDLK_E, "E", 1 }, { SDLK_R, "R", 1 },
	  { SDLK_T, "T", 1 }, { SDLK_Y, "Y", 1 }, { SDLK_U, "U", 1 }, { SDLK_I, "I", 1 }, { SDLK_O, "O", 1 },
	  { SDLK_P, "P", 1 }, { SDLK_LEFTBRACKET, "[", 1 }, { SDLK_RIGHTBRACKET, "]", 1 }, { SDLK_BACKSLASH, "\\", 1.5f } },
	{ { SDLK_CAPSLOCK, "Caps", 1.75f }, { SDLK_A, "A", 1 }, { SDLK_S, "S", 1 }, { SDLK_D, "D", 1 }, { SDLK_F, "F", 1 },
	  { SDLK_G, "G", 1 }, { SDLK_H, "H", 1 }, { SDLK_J, "J", 1 }, { SDLK_K, "K", 1 }, { SDLK_L, "L", 1 },
	  { SDLK_SEMICOLON, ";", 1 }, { SDLK_APOSTROPHE, "'", 1 }, { SDLK_RETURN, "⏎", 2.25f }, {} },
	{ { SDLK_LSHIFT, "Shift", 2.25f }, { SDLK_Z, "Z", 1 }, { SDLK_X, "X", 1 }, { SDLK_C, "C", 1 }, { SDLK_V, "V", 1 },
	  { SDLK_B, "B", 1 }, { SDLK_N, "N", 1 }, { SDLK_M, "M", 1 }, { SDLK_COMMA, ",", 1 }, { SDLK_PERIOD, ".", 1 },
	  { SDLK_SLASH, "/", 1 }, { SDLK_RSHIFT, "Shift", 2.75f }, {}, {} },
	{ { SDLK_LCTRL, "Ctrl", 1.5f }, { SDLK_LGUI, "", 1.25f }, { SDLK_LALT, "Alt", 1.25f }, { SDLK_SPACE, "", 6.25f },
	  { SDLK_RALT, "Alt", 1.25f }, { SDLK_RCTRL, "Ctrl", 1.5f }, {}, {}, {}, {}, {}, {}, {}, {} },
};

/* A quadratic curve from p, through the control c, to q */
void
curve_to(std::vector<ImVec2> &points, ImVec2 c, ImVec2 q)
{
	const ImVec2 p = points.back();

	for (int k = 1; k <= 8; k++) {
		const float t = static_cast<float>(k) / 8.0f, u = 1.0f - t;

		points.push_back({ u * u * p.x + 2 * u * t * c.x + t * t * q.x, u * u * p.y + 2 * u * t * c.y + t * t * q.y });
	}
}

/* The keyboard drawn (and the mouse): the keys of the controls of the
 * player clear, the others faint, a key held lit; a mouse button pressed
 * lit. A click on a key of a control catches the parts it works, in turn;
 * a control without a key changes in Classic. */
void
keyboard_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	/* The drawing, in its own units: 640 by 220 from (0, 96) */
	constexpr float W = 640.0f, H = 220.0f, TOP = 96.0f, U = 28.0f, X0 = 14.0f, Y0 = 130.0f, GAP = 3.0f;
	const ImVec2 lo = { a.x + kit.px(MenuKit::BOX_INSIDE), a.y + corner + kit.px(MenuKit::BOX_INSIDE) };
	const ImVec2 hi = { b.x - kit.px(MenuKit::BOX_INSIDE), b.y - kit.px(MenuKit::BOX_INSIDE) };
	const float k = std::max(0.0f, std::min((hi.x - lo.x) / W, (hi.y - lo.y) / H));
	const ImVec2 o = { (lo.x + hi.x - W * k) / 2.0f, (lo.y + hi.y - H * k) / 2.0f - TOP * k };
	const auto at = [&](float x, float y) { return ImVec2{ o.x + x * k, o.y + y * k }; };
	const ImU32 faint = (palette.ink & ~IM_COL32_A_MASK) | (0x2Eu << IM_COL32_A_SHIFT);
	std::vector<SDL_Keycode> bound;

	for (const InputRow &row : rows)
		for (const HostInput &input : row.inputs)
			if (input.kind == HostInput::Kind::KEY)
				bound.push_back(input.key);

	kit.frame(list, at(X0 - 8, Y0 - 10), at(X0 - 8 + 18 * U + 30, Y0 - 10 + 5 * U + 18), kit.px(4.0f), palette.body,
	               palette.stroke);

	const float size = 10.0f * k;
	const auto key = [&](const DrawnKey &dk, float x, float y, float w) {
		const bool on = std::find(bound.begin(), bound.end(), dk.key) != bound.end();
		const bool down = std::find(state.keys_held.begin(), state.keys_held.end(), dk.key) != state.keys_held.end();
		const float dy = down ? 1.5f : 0.0f;
		const ImVec2 p = at(x, y + dy), q = at(x + w, y + dy + U - GAP - 1);
		const float text = MenuKit::measure(dk.name, size);
		bool hovered = false;

		if (on) {
			ImGui::SetCursorScreenPos(p);
			ImGui::PushID(static_cast<int>(dk.key));
			if (ImGui::InvisibleButton("key", { q.x - p.x, q.y - p.y })) {
				std::vector<MenuSlot> parts;

				for (const InputRow &row : rows)
					if (row.catchable && std::any_of(row.inputs.begin(), row.inputs.end(), [&dk](const HostInput &in) {
						    return in.kind == HostInput::Kind::KEY && in.key == dk.key;
					    }))
						parts.push_back(row.slot);
				catch_parts(state, parts);
			}
			hovered = ImGui::IsItemHovered();
			ImGui::PopID();
		}
		list->AddRectFilled(p, q, down ? palette.accent : palette.part, 4.0f * k);
		list->AddRect(p, q, hovered ? palette.accent : on ? palette.stroke : faint, 4.0f * k, ImDrawFlags_None,
		              kit.px(1.0f));
		MenuKit::write(list, { (p.x + q.x - text) / 2.0f, (p.y + q.y - size) / 2.0f },
		               down ? palette.on_accent : on ? palette.ink : palette.faint, dk.name, size);
	};

	for (size_t r = 0; r < 5; r++) {
		float x = X0;

		for (const DrawnKey &dk : KEY_ROWS[r]) {
			if (!dk.name)
				break;
			key(dk, x, Y0 + static_cast<float>(r) * U, dk.width * U - GAP);
			x += dk.width * U;
		}
	}

	const float ax = X0 + 15 * U + 8;

	key({ SDLK_UP, "↑", 1 }, ax + U, Y0 + 3 * U, U - GAP);
	key({ SDLK_LEFT, "←", 1 }, ax, Y0 + 4 * U, U - GAP);
	key({ SDLK_DOWN, "↓", 1 }, ax + U, Y0 + 4 * U, U - GAP);
	key({ SDLK_RIGHT, "→", 1 }, ax + 2 * U, Y0 + 4 * U, U - GAP);

	/* The mouse: its outline, its two buttons, its wheel */
	std::vector<ImVec2> body = { { 584, 116 } }, left = { { 582, 118 } }, right = { { 586, 118 } };

	curve_to(body, { 620, 116 }, { 622, 160 });
	body.push_back({ 622, 230 });
	curve_to(body, { 622, 282 }, { 584, 282 });
	curve_to(body, { 546, 282 }, { 546, 230 });
	body.push_back({ 546, 160 });
	curve_to(body, { 548, 116 }, { 584, 116 });
	curve_to(left, { 550, 120 }, { 548, 160 });
	left.insert(left.end(), { { 548, 176 }, { 582, 176 } });
	curve_to(right, { 618, 120 }, { 620, 160 });
	right.insert(right.end(), { { 620, 176 }, { 586, 176 } });
	for (std::vector<ImVec2> *shape : { &body, &left, &right })
		for (ImVec2 &p : *shape)
			p = at(p.x, p.y);
	list->AddConvexPolyFilled(body.data(), static_cast<int>(body.size()), palette.body);
	for (const auto &[half, button] : { std::pair{ &left, 0 }, std::pair{ &right, 1 } })
		if (ImGui::IsMouseDown(button))
			list->AddConvexPolyFilled(half->data(), static_cast<int>(half->size()), palette.accent);
	list->AddPolyline(body.data(), static_cast<int>(body.size()), palette.stroke, ImDrawFlags_Closed, kit.px(1.1f));
	list->AddLine(at(584, 116), at(584, 176), palette.outline, kit.px(0.8f));
	list->AddLine(at(547, 176), at(621, 176), palette.outline, kit.px(0.8f));
	list->AddRectFilled(at(580, 132), at(588, 154), ImGui::IsMouseDown(2) ? palette.accent : palette.part, 4.0f * k);
	list->AddRect(at(580, 132), at(588, 154), palette.stroke, 4.0f * k, ImDrawFlags_None, kit.px(1.0f));
}

/* What the page asks while the next input of a part is caught */
std::string
prompt(const MenuState &state, const MenuSlot &slot)
{
	const bool joystick = state.on_joystick();
	const std::string thing = state.on_pad() || joystick || !state.gun[state.player & 1].empty() ? "button" : "key";
	const GameControl *const control = slot.control >= 0 ? &state.panel->controls()[static_cast<size_t>(slot.control)] :
	                                   nullptr;

	if (!control || control->type == GameControl::Type::BUTTON)
		return "Press the " + thing + " you want";
	if (control->type == GameControl::Type::POSITIONS)
		return "Press the " + thing + (slot.part == 2 ? " that shifts up" : " that shifts down");

	const bool two_ways = GameControl::half(control->low, false);
	const bool vertical = control->low == GameControl::Low::UP || control->low == GameControl::Low::DOWN;

	if (slot.part == 0 && !two_ways)
		return "Press the trigger or pedal you want";
	if (slot.part == 0)
		return joystick        ? "Move an axis the same way, or press a button for each direction" :
		       state.on_pad()  ? "Move a stick the same way, or press a button for each direction" :
		       thing == "key"  ? "Move the mouse the same way, or press a key for each direction" :
		                         "Aim the light gun the same way, or press a button for each direction";
	if (!two_ways)
		return "Press the " + thing + " you want";
	return "Press the " + thing + " for " +
	       (slot.part == 1 ? (vertical ? "forward" : "left") : (vertical ? "back" : "right"));
}

/* The controls as a list in two columns (Classic): each name, and its
 * inputs on the device shown in a pill, filled as far as the control is
 * worked; a click on the name catches its parts in turn, on a part that
 * part */
void
list_box(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 a, ImVec2 b, float corner)
{
	ImDrawList *list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const int kind = state.pad_types[state.player & 1];
	const std::vector<Group> all = groups(state, rows);
	const std::vector<std::string> numbered = other_joysticks(rows);
	const Others others{ numbered, state.joysticks };
	const float line = ImGui::GetTextLineHeight(), gap = kit.px(28.0f), wide = kit.px(8.0f), high = kit.px(2.0f);
	const float apart = kit.px(8.0f);
	ImVec2 lo = { a.x + kit.px(14.0f), a.y + kit.px(MenuKit::BOX_INSIDE) + corner };
	const ImVec2 hi = { b.x - kit.px(14.0f), b.y - kit.px(MenuKit::BOX_INSIDE) };
	const size_t half = (all.size() + 1) / 2;
	/* As high as the room leaves them, a line and 16 points at most; when
	 * they do not fit, the list scrolls, its bar beside the columns */
	const float h = std::clamp((hi.y - lo.y) / static_cast<float>(std::max<size_t>(half, 1)), line + 2.0f * high + kit.px(4.0f),
	                           line + kit.px(16.0f));
	const bool scrolls = static_cast<float>(half) * h > hi.y - lo.y;
	const float column = (hi.x - lo.x - gap - (scrolls ? ImGui::GetStyle().ScrollbarSize : 0.0f)) / 2.0f;

	if (scrolls) {
		ImGui::SetCursorScreenPos(lo);
		ImGui::BeginChild("list", { hi.x - lo.x, hi.y - lo.y }, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		lo = ImGui::GetCursorScreenPos();
		list = ImGui::GetWindowDrawList();
	}

	const float top = scrolls ? lo.y : std::max(lo.y, (lo.y + hi.y - static_cast<float>(half) * h) / 2.0f);

	for (size_t g = 0; g < all.size(); g++) {
		const Group &group = all[g];
		const bool second = g >= half;
		const ImVec2 at = { lo.x + (second ? column + gap : 0.0f), top + static_cast<float>(second ? g - half : g) * h };
		/* The pill: its parts, the longest cut first where they are
		 * too long together for the room the name leaves (60 % of the
		 * line at least) */
		const size_t n = group.parts.size();
		const float room =
			std::max(column * 0.6f, column - MenuKit::measure(group.name.c_str()) - kit.px(18.0f)) - 2.0f * wide;
		std::vector<std::string> texts;
		std::vector<float> widths;
		bool empty = true;

		for (const Group::Part &part : group.parts) {
			texts.push_back(part_text(state, rows, part, kind, others));
			widths.push_back(MenuKit::measure(texts.back().c_str()));
			empty = empty && rows[part.row].inputs.empty() && !same(state.catching, rows[part.row].slot);
		}

		const auto sum = [&] {
			float together = static_cast<float>(n - 1) * apart;

			for (const float w : widths)
				together += w;
			return together;
		};

		for (size_t k = 0; k < n && sum() > room; k++) {
			const size_t longest = static_cast<size_t>(std::max_element(widths.begin(), widths.end()) - widths.begin());

			texts[longest] = MenuKit::fitted(texts[longest], std::max(0.0f, widths[longest] - (sum() - room)));
			widths[longest] = MenuKit::measure(texts[longest].c_str());
		}

		const float total = sum();

		const float chip_w = std::max(kit.px(34.0f), total + 2.0f * wide);
		const ImVec2 ca = { at.x + column - chip_w, at.y + (h - line) / 2.0f - high };
		const ImVec2 cb = { at.x + column, ca.y + line + 2.0f * high };
		ImGui::PushID(static_cast<int>(g));
		ImGui::SetCursorScreenPos(at);
		if (ImGui::InvisibleButton("name", { ca.x - at.x, h }))
			catch_parts(state, group.chosen ? std::vector<MenuSlot>() : group.to_catch(rows));

		bool hovered = ImGui::IsItemHovered();
		float x = ca.x + wide + (chip_w - 2.0f * wide - total) / 2.0f;

		for (size_t p = 0; p < n; p++) {
			const InputRow &row = rows[group.parts[p].row];
			const float w = MenuKit::measure(texts[p].c_str());
			const float from = p ? x - apart / 2.0f : ca.x, to = p + 1 < n ? x + w + apart / 2.0f : cb.x;

			ImGui::SetCursorScreenPos({ from, at.y });
			ImGui::PushID(static_cast<int>(p));
			if (ImGui::InvisibleButton("part", { to - from, h }) && row.catchable)
				catch_parts(state, same(state.catching, row.slot) ? std::vector<MenuSlot>() :
				                                                     std::vector<MenuSlot>{ row.slot });
			hovered = hovered || ImGui::IsItemHovered();
			ImGui::PopID();
			x += w + apart;
		}
		ImGui::PopID();

		if (hovered) {
			list->AddRectFilled(at, { at.x + column, at.y + h }, palette.hover);
			state.hovered_control = static_cast<int>(g);
		}
		list->AddLine({ at.x, at.y + h }, { at.x + column, at.y + h }, palette.line, kit.stroke());
		MenuKit::write(list, { at.x + kit.px(4.0f), at.y + (h - line) / 2.0f },
		               group.lit || group.chosen ? palette.accent : palette.ink,
		               MenuKit::fitted(group.name, ca.x - at.x - kit.px(14.0f)).c_str());
		if (group.amount > 0.01f) {
			list->PushClipRect(ca, { ca.x + chip_w * group.amount, cb.y }, true);
			list->AddRectFilled(ca, cb, MenuKit::alpha(palette.accent, AMOUNT_ALPHA), (cb.y - ca.y) / 2.0f);
			list->PopClipRect();
		}
		list->AddRect(ca, cb, group.chosen || group.lit ? palette.accent : palette.outline, (cb.y - ca.y) / 2.0f,
		              ImDrawFlags_None, kit.stroke());
		x = ca.x + wide + (chip_w - 2.0f * wide - total) / 2.0f;
		for (size_t p = 0; p < n; p++) {
			const InputRow &row = rows[group.parts[p].row];
			const ImU32 colour = same(state.catching, row.slot) ? palette.accent :
			                     row.inputs.empty() ? palette.faint : palette.ink;

			MenuKit::write(list, { x, ca.y + high }, colour, texts[p].c_str());
			x += MenuKit::measure(texts[p].c_str()) + apart;
		}
	}
	if (scrolls) {
		ImGui::SetCursorScreenPos(lo);
		ImGui::Dummy({ hi.x - lo.x, static_cast<float>(half) * h });
		ImGui::EndChild();
	}
}

/* The buttons under the drawing, as many on a line as fit: Test, the
 * options of the device shown, Defaults, Clear all (Undo for a while
 * after), Map all. Their height, with the room around them;
 * drawn and clicked when `draw`. */
float
buttons_bar(const MenuKit &kit, MenuState &state, std::span<const InputRow> rows, ImVec2 at, float width, bool draw)
{
	using Icon = MenuKit::Icon;
	using Tone = MenuKit::Tone;
	const float across = kit.px(12.0f), down = kit.px(5.0f), h = kit.button_size("Defaults", Icon::DEFAULTS).y;
	const char *const clear = state.undo ? "Undo" : "Clear all";
	std::vector<float> widths = { kit.button_size("Test", Icon::PAD).x, kit.button_size("Options", Icon::SETTINGS).x };

	widths.push_back(kit.button_size("Defaults", Icon::DEFAULTS).x);
	widths.push_back(kit.button_size(clear, state.undo ? Icon::UNDO : Icon::CLEAR).x);
	if (!state.mapping)
		widths.push_back(kit.button_size("Map all", Icon::MAP).x);

	/* Each at its place, left to right, a line under the other */
	std::vector<ImVec2> places;
	float x = 0.0f, y = 0.0f;

	for (const float w : widths) {
		if (x > 0.0f && x + w > width) {
			x = 0.0f;
			y += h + down;
		}
		places.push_back({ at.x + x, at.y + kit.px(5.0f) + y });
		x += w + across;
	}

	const float height = kit.px(5.0f) + y + h + kit.px(6.0f);

	if (!draw)
		return height;

	size_t i = 0;

	if (kit.button("Test", places[i++], state.testing, Icon::PAD)) {
		state.testing = !state.testing;
		state.stop_catching();
	}
	if (kit.button("Options", places[i++], false, Icon::SETTINGS)) {
		state.advanced = true;
		state.testing = false;
		state.stop_catching();
	}
	if (kit.button("Defaults", places[i++], false, Icon::DEFAULTS))
		state.ask = MenuState::Ask::DEFAULTS;
	if (kit.button(clear, places[i++], false, state.undo ? Icon::UNDO : Icon::CLEAR,
	               state.undo ? Tone::STRONG : Tone::DANGER))
		state.ask = state.undo ? MenuState::Ask::UNDO : MenuState::Ask::CLEAR;
	if (!state.mapping && kit.button("Map all", places[i++], false, Icon::MAP)) {
		/* The first control: the one before it is none */
		state.mapping = rows.size();
		state.catching.reset();
		state.catch_next.clear();
	}
	return height;
}

/* The advanced options of the device shown, over its box: the vibration
 * of the game; for a gamepad how hard it shakes, for a joystick the force
 * of the wheel and how far it turns for the steering; the controls the
 * player may turn over; for the keyboard and the mouse (and in a game
 * where player 1 has a sight) Hide cursor; with a sight, the white border
 * of a light gun; the dead zone and the trigger threshold of the make and
 * model of a gamepad; Close (B, Escape) */
void
advanced_box(const MenuKit &kit, MenuState &state, ImVec2 a, ImVec2 b)
{
	using Icon = MenuKit::Icon;
	static constexpr char NO_SHAKE[] = "this game has no vibration";
	static constexpr const char *TURNS[] = { "Full", "270°", "360°", "540°", "900°", "1080°" };
	static_assert(std::size(TURNS) == std::size(WHEEL_ROTATIONS) && WHEEL_ROTATIONS[0] == WHEEL_ROTATION);
	const bool pad = state.on_pad(), keys = !pad && !state.on_joystick();
	const bool turns = state.steers && !pad && !keys, gun = state.sights[0];
	/* What applies to this game only, as the rows below show it */
	std::vector<std::string> game_rows;

	if (!keys)
		game_rows.push_back("vibration");
	if (pad && state.can_shake)
		game_rows.push_back("its force");
	if (state.can_force && turns)
		game_rows.push_back("wheel force");
	if (turns)
		game_rows.push_back("wheel rotation");
	for (std::string name : state.invert_names) {
		name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
		game_rows.push_back(name);
	}
	if (keys || gun)
		game_rows.push_back("hide cursor");
	if (gun)
		game_rows.push_back("Sinden border");

	std::string foot;

	for (size_t i = 0; i < game_rows.size(); i++)
		foot += (i == 0 ? "" : i + 1 == game_rows.size() ? " and " : ", ") + game_rows[i];
	if (!foot.empty())
		foot[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(foot[0])));
	foot += game_rows.size() > 1 ? " apply to this game only" : " applies to this game only";
	foot += pad ? "; the dead zone and trigger threshold follow this controller to every game." : ".";
	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const float line = ImGui::GetTextLineHeight(), note = MenuKit::font(MenuKit::NOTE), apart = kit.px(16.0f);
	const ImVec2 lo = { a.x + kit.px(14.0f), a.y + kit.px(MenuKit::BOX_INSIDE) };
	const ImVec2 hi = { b.x - kit.px(14.0f), b.y - kit.px(MenuKit::BOX_INSIDE) };
	const ImVec2 close = kit.button_size("Close", Icon::CLEAR);
	/* A joystick is named as the list of devices names it */
	std::string device = pad  ? "Controller · " + std::string(pad_drawing_name(state.pad_types[state.player & 1])) :
	                     keys ? "Keyboard and mouse" : "Joystick";

	for (const MenuState::Plugged &one : state.joysticks)
		if (!pad && !keys && one.player == static_cast<int>(state.player & 1))
			device = static_cast<SDL_JoystickType>(one.kind) == SDL_JOYSTICK_TYPE_WHEEL        ? "Wheel" :
			         static_cast<SDL_JoystickType>(one.kind) == SDL_JOYSTICK_TYPE_FLIGHT_STICK ? "Flight stick" :
			                                                                                     "Joystick";

	const std::string title = "Options · " + device +
	                          (state.shown < state.games.size() ? " · " + state.games[state.shown].second : "");
	const float rule = lo.y + close.y + kit.px(8.0f);
	float y = rule + kit.px(14.0f);

	MenuKit::write(list, { lo.x, lo.y + (close.y - line) / 2.0f }, palette.ink,
	               MenuKit::fitted(title, hi.x - lo.x - close.x - kit.px(12.0f)).c_str());
	list->AddLine({ lo.x, rule }, { hi.x, rule }, palette.line, kit.stroke());

	/* The vibration, with what shakes on the cabinet under it (none for
	 * the keyboard and the mouse) */
	if (!keys) {
		const ImVec2 cells = kit.segments_size(Menu::OFF_ON);
		const float h = std::max(line + (state.shake_from.empty() ? 0.0f : kit.px(2.0f) + note), cells.y);

		list->AddText({ lo.x, y }, state.can_shake ? palette.ink : palette.faint,
		              state.shake_experimental ? "Vibration (experimental)" : "Vibration");
		if (!state.shake_from.empty())
			MenuKit::write(list, { lo.x, y + line + kit.px(2.0f) }, palette.dim, state.shake_from.c_str(), note);
		if (!state.can_shake) {
			list->AddText({ hi.x - ImGui::CalcTextSize(NO_SHAKE).x, y }, palette.faint, NO_SHAKE);
		} else {
			/* The gamepad: left (Off) or right (On), or A, switches it */
			if (kit.focus({ lo.x, y }, { hi.x, y + h }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
				state.shake = kit.side() ? kit.side() > 0 : !state.shake;

			int shake = state.shake ? 1 : 0;

			if (kit.segments({ hi.x - cells.x, y + (h - cells.y) / 2.0f }, Menu::OFF_ON, shake))
				state.shake = shake == 1;
		}
		y += h + apart;
	}

	/* A share of the way, in %, from 0 to `most` */
	const auto percent = [&](const char *name, int &value, int most) {
		char shown[16];

		std::snprintf(shown, sizeof(shown), "%d %%", value);

		const float control = kit.track_size(shown, "50 %").x;
		const bool focused = kit.focus({ lo.x, y }, { hi.x, y + line }, kit.px(4.0f));
		float moved = static_cast<float>(value);

		list->AddText({ lo.x, y }, palette.ink, name);
		ImGui::PushID(name);
		if (kit.track({ hi.x - control, y }, moved, 0.0f, static_cast<float>(most), 1.0f, shown, focused))
			value = static_cast<int>(std::lround(moved));
		ImGui::PopID();
		y += line + apart;
	};

	if (state.can_shake && pad)
		percent("Vibration force", state.vibration_force, MOST_VIBRATION_FORCE);
	if (state.can_force && !pad)
		percent("Wheel force", state.wheel_force, MOST_WHEEL_FORCE);
	/* The degrees of the wheel from one end of the steering to the other;
	 * the gamepad: left or right, or A, takes the next one */
	if (turns) {
		const ImVec2 cells = kit.segments_size(TURNS);
		const float h = std::max(line, cells.y);
		const auto at = std::find(std::begin(WHEEL_ROTATIONS), std::end(WHEEL_ROTATIONS), state.wheel_rotation);
		const int n = static_cast<int>(std::size(TURNS));
		int turn = at == std::end(WHEEL_ROTATIONS) ? 0 : static_cast<int>(at - std::begin(WHEEL_ROTATIONS));

		list->AddText({ lo.x, y + (h - line) / 2.0f }, palette.ink, "Wheel rotation");
		if (kit.focus({ lo.x, y }, { hi.x, y + h }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
			turn = (turn + (kit.side() < 0 ? n - 1 : 1)) % n;
		kit.segments({ hi.x - cells.x, y + (h - cells.y) / 2.0f }, TURNS, turn);
		state.wheel_rotation = WHEEL_ROTATIONS[turn];
		y += h + apart;
	}
	/* A control the player turns over, on every device (the gamepad:
	 * left, right or A switches it) */
	for (size_t c = 0; c < state.invert_names.size(); c++) {
		const ImVec2 cells = kit.segments_size(Menu::OFF_ON);
		const float h = std::max(line, cells.y);
		int on = state.inverted[c] ? 1 : 0;

		ImGui::PushID(state.invert_names[c].c_str());
		list->AddText({ lo.x, y + (h - line) / 2.0f }, palette.ink, state.invert_names[c].c_str());
		if (kit.focus({ lo.x, y }, { hi.x, y + h }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
			on = kit.side() ? kit.side() > 0 : !on;
		kit.segments({ hi.x - cells.x, y + (h - cells.y) / 2.0f }, Menu::OFF_ON, on);
		state.inverted[c] = on == 1;
		ImGui::PopID();
		y += h + apart;
	}
	/* The cursor hidden, the mouse controlling the game for player 1 and
	 * no bar, or a normal pointer (the gamepad: left, right or A switches
	 * it) */
	if (keys || gun) {
		static constexpr char UNDER[] = "On: the mouse controls the game, no cursor, no bar. Off: a normal pointer.";
		const ImVec2 cells = kit.segments_size(Menu::OFF_ON);
		const float h = std::max(line + kit.px(2.0f) + note, cells.y);

		ImGui::PushID("Hide cursor");
		list->AddText({ lo.x, y }, palette.ink, "Hide cursor");
		MenuKit::write(list, { lo.x, y + line + kit.px(2.0f) }, palette.dim, UNDER, note);
		if (kit.focus({ lo.x, y }, { hi.x, y + h }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
			state.hide_cursor = kit.side() ? kit.side() > 0 : !state.hide_cursor;

		int on = state.hide_cursor ? 1 : 0;

		if (kit.segments({ hi.x - cells.x, y + (h - cells.y) / 2.0f }, Menu::OFF_ON, on))
			state.hide_cursor = on == 1;
		ImGui::PopID();
		y += h + apart;
	}
	/* The white border of a light gun that finds the screen by it; once
	 * on, around the picture of the game (4:3) or the whole window, and
	 * how wide (the gamepad: left, right or A switches it or takes the
	 * other one) */
	if (gun) {
		static constexpr char UNDER[] = "white, for a light gun that finds the screen by it";
		static constexpr const char *AROUND[] = { "4:3", "Full" };
		SindenBorder &border = state.sinden_border;
		const ImVec2 cells = kit.segments_size(Menu::OFF_ON);
		const float h = std::max(line + kit.px(2.0f) + note, cells.y);
		int on = border.on ? 1 : 0;

		ImGui::PushID("Sinden border");
		list->AddText({ lo.x, y }, palette.ink, "Sinden border");
		MenuKit::write(list, { lo.x, y + line + kit.px(2.0f) }, palette.dim, UNDER, note);
		if (kit.focus({ lo.x, y }, { hi.x, y + h }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
			on = kit.side() ? kit.side() > 0 : !on;
		kit.segments({ hi.x - cells.x, y + (h - cells.y) / 2.0f }, Menu::OFF_ON, on);
		border.on = on == 1;
		y += h + apart;
		if (border.on) {
			const ImVec2 sides = kit.segments_size(AROUND);
			const float sh = std::max(line, sides.y);
			int window = border.window ? 1 : 0;
			char shown[16];

			list->AddText({ lo.x, y + (sh - line) / 2.0f }, palette.ink, "Border style");
			if (kit.focus({ lo.x, y }, { hi.x, y + sh }, kit.px(4.0f)) && (kit.side() || kit.pressed()))
				window = kit.side() ? kit.side() > 0 : !window;
			kit.segments({ hi.x - sides.x, y + (sh - sides.y) / 2.0f }, AROUND, window);
			border.window = window == 1;
			y += sh + apart;

			std::snprintf(shown, sizeof(shown), "%u px", border.size);

			const float control = kit.track_size(shown, "30 px").x;
			const bool focused = kit.focus({ lo.x, y }, { hi.x, y + line }, kit.px(4.0f));
			float moved = static_cast<float>(border.size);

			list->AddText({ lo.x, y }, palette.ink, "Border size");
			if (kit.track({ hi.x - control, y }, moved, static_cast<float>(LEAST_SINDEN_BORDER_SIZE),
			              static_cast<float>(MOST_SINDEN_BORDER_SIZE), 1.0f, shown, focused))
				border.size = static_cast<unsigned>(std::lround(moved));
			y += line + apart;
		}
		ImGui::PopID();
	}
	if (pad) {
		percent("Stick dead zone", state.tuning.dead_zone, PadTuning::MOST_DEAD_ZONE);
		percent("Trigger threshold", state.tuning.trigger_threshold, PadTuning::MOST_TRIGGER_THRESHOLD);
	}

	const float foot_h = MenuKit::wrapped_h(foot.c_str(), note, hi.x - lo.x);

	list->AddText(ImGui::GetFont(), note, { lo.x, hi.y - foot_h }, palette.faint, foot.c_str(), nullptr, hi.x - lo.x);
	if (kit.button("Close", { hi.x - close.x, lo.y }, false, Icon::CLEAR))
		state.advanced = false;
}

} /* namespace */

void
Menu::page_input(MenuKit &kit, MenuState &state) const
{
	if (!state.panel)
		return;

	using Icon = MenuKit::Icon;
	const MenuPalette &palette = kit.palette();
	const bool two = state.panel->two_players();

	if (!two)
		state.player = 0;
	if (!state.catching)
		state.catch_next.clear();

	const ImVec2 top = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
	const float width = room.x, gap = kit.px(ROW_GAP);
	float y = top.y;

	y = games_row(kit, state, { top.x, y }, width, true) + gap;
	y = devices_row(kit, state, { top.x, y }, width, two) + gap;
	if (state.sights[0] || state.sights[1])
		y = guns_row(kit, state, { top.x, y }, width, two) + gap;

	const std::vector<InputRow> rows = input_rows(state);
	/* A window wide enough (16:9) has the cabinet beside the device; a
	 * narrower one shows either. The options of the device shown take the
	 * place of its box. */
	const ImVec2 window = ImGui::GetIO().DisplaySize;
	const bool wide = window.x >= WIDE * window.y;
	const bool cabinet = !wide && state.cabinet_shown;

	const bool options = state.advanced && !cabinet;

	next_mapping(state, rows);
	/* The gamepad: each part shown in turn, drawn as if caught; A changes
	 * it, X clears it, Y sets it back to its default */
	state.picked.reset();
	for (const InputRow &row : rows) {
		if (options || (row.half && row.inputs.empty()) || !kit.focus_mark())
			continue;
		state.picked = row.slot;
		if (kit.pressed() && row.catchable)
			catch_parts(state, { row.slot });
		else if (state.nav.clear)
			state.ask = MenuState::Ask::PART_CLEAR;
		else if (state.nav.reset)
			state.ask = MenuState::Ask::PART_DEFAULTS;
	}
	const float bottom = top.y + room.y - kit.px(MenuKit::PAGE_FOOT);
	const float split = wide ? top.x + (width - gap) * PAD_SHARE : top.x + width;
	const ImVec2 a = { top.x, y }, b = { split, bottom };
	/* The buttons at the bottom of the box, under a line (none while the
	 * cabinet is in it); the drawing above them */
	const ImVec2 bar_at = { a.x + kit.px(10.0f), 0.0f };
	const float bar_w = b.x - a.x - kit.px(20.0f);
	const float bar_top = cabinet || options ? b.y : b.y - buttons_bar(kit, state, rows, bar_at, bar_w, false);
	const ImVec2 inside = { b.x, bar_top };
	/* The buttons in the corners of the box: List (the classic view) for a
	 * gamepad or the keyboard (a joystick has a list alone), the swap of the
	 * device and the cabinet in a narrow window; in a game of two, the
	 * player shown; while the controls are tried, Test mode */
	const char *const swap = cabinet ? "Device" : "Cabinet";
	const bool classic = !cabinet && !options && !state.on_joystick();
	const bool players = two && !cabinet && !options, testing = state.testing && !cabinet && !options;
	const ImVec2 classic_size = kit.button_size("List", Icon::LIST), swap_size = kit.button_size(swap, Icon::SWAP);
	/* The other joysticks the list numbers, told in the corner */
	const std::vector<std::string> numbered = cabinet || options ? std::vector<std::string>() : other_joysticks(rows);
	const float corner = classic || players || testing || !numbered.empty() || !wide ?
	                     std::max(classic_size.y, swap_size.y) + kit.px(5.0f) : 0.0f;

	state.hovered_control = -1;
	kit.frame(ImGui::GetWindowDrawList(), a, b, kit.px(MenuKit::BOX_CUT), 0, palette.line);
	if (cabinet)
		cabinet_box(kit, state, a, b, false);
	else if (options)
		advanced_box(kit, state, a, b);
	else if (state.classic || state.on_joystick())
		list_box(kit, state, rows, a, inside, corner);
	else if (state.on_pad())
		pad_box(kit, state, rows, a, inside, corner);
	else if (state.gun_of(state.player))
		gun_box(kit, state, rows, a, inside, corner);
	else
		keyboard_box(kit, state, rows, a, inside, corner);
	if (wide)
		cabinet_box(kit, state, { split + gap, y }, { top.x + width, bottom }, true);

	float x = b.x - kit.px(6.0f);

	if (!wide && !options) {
		x -= swap_size.x;
		if (kit.button(swap, { x, a.y + kit.px(5.0f) }, false, Icon::SWAP))
			state.cabinet_shown = !state.cabinet_shown;
		x -= kit.px(6.0f);
	}
	if (classic && kit.button("List", { x - classic_size.x, a.y + kit.px(5.0f) }, state.classic, Icon::LIST))
		state.classic = !state.classic;

	const float edge = classic ? x - classic_size.x : x;

	x = a.x + kit.px(6.0f);
	for (unsigned player = 0; players && player < 2; player++) {
		const char *const name = player ? "2P" : "1P";

		if (kit.button(name, { x, a.y + kit.px(5.0f) }, state.player == player)) {
			state.player = player;
			state.catching.reset();
			state.catch_next.clear();
		}
		x += kit.button_size(name).x + kit.px(4.0f);
	}
	if (testing) {
		kit.badge(ImGui::GetWindowDrawList(), { x, a.y + kit.px(5.0f) }, "Test mode");
		x += kit.button_size("Test mode").x + kit.px(8.0f);
	}
	if (!numbered.empty()) {
		std::string told;

		for (size_t n = 0; n < numbered.size(); n++)
			told += (n ? "   " : "") + joystick_number(n) + " " + joystick_name(numbered[n], state.joysticks);
		MenuKit::write(ImGui::GetWindowDrawList(),
		               { x, a.y + kit.px(5.0f) + (classic_size.y - ImGui::GetTextLineHeight()) / 2.0f }, palette.dim,
		               MenuKit::fitted(told, std::max(0.0f, edge - x - kit.px(8.0f))).c_str());
	}
	if (!cabinet && !options) {
		ImGui::GetWindowDrawList()->AddLine({ a.x + 1.0f, bar_top }, { b.x - 1.0f, bar_top }, palette.line, kit.stroke());
		buttons_bar(kit, state, rows, { bar_at.x, bar_top }, bar_w, true);
	}
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy({ width, room.y - kit.px(MenuKit::PAGE_FOOT) });
}

/* What the page says at the bottom: the part caught, what to
 * do, Skip and Stop while each control is caught in turn; else the control
 * worked now and its inputs, filled as far as it is; else how it works */
bool
Menu::input_tells(const MenuState &state)
{
	if (!state.panel)
		return false;

	const std::vector<InputRow> rows = input_rows(state);
	const std::vector<Group> all = groups(state, rows);

	return (state.hovered_control >= 0 && static_cast<size_t>(state.hovered_control) < all.size()) ||
	       std::any_of(all.begin(), all.end(), [](const Group &group) { return group.chosen || group.lit; });
}

float
Menu::input_foot(const MenuKit &kit, MenuState &state, ImVec2 at, float width, float height, bool draw)
{
	/* How it works: on the keyboard drawn, its keys are clicked */
	const char *const told = state.testing ? "Testing: each control lights up, the menu stays still." :
	                         state.classic || state.on_pad() || state.on_joystick() || state.gun_of(state.player) ?
	                         "Press or move a control to see it light up. Click a name to change it." :
	                         "Press or move a control to see it light up. Click a key to change it.";
	const float line = ImGui::GetTextLineHeight();

	if (!draw)
		return std::max({ line, kit.button_size("Stop").y, ImGui::CalcTextSize(told, nullptr, false, width).y });
	if (!state.panel)
		return height;

	ImDrawList *const list = ImGui::GetWindowDrawList();
	const MenuPalette &palette = kit.palette();
	const std::vector<InputRow> rows = input_rows(state);
	const int kind = state.pad_types[state.player & 1];
	const float y = at.y + (height - line) / 2.0f, space = kit.px(10.0f), right = at.x + width;
	float x = at.x;
	const auto say = [&](const std::string &text, ImU32 colour) {
		const std::string shown = MenuKit::fitted(text, std::max(0.0f, right - x));

		MenuKit::write(list, { x, y }, colour, shown.c_str());
		x += MenuKit::measure(shown.c_str()) + space;
	};
	const auto caught = std::find_if(rows.begin(), rows.end(),
	                                 [&](const InputRow &row) { return same(state.catching, row.slot); });

	if (caught != rows.end()) {
		const double beat = static_cast<double>(SDL_GetTicks() % 1600) / 1600.0;
		const float beating = 0.725f + 0.275f * static_cast<float>(std::cos(beat * 2.0 * std::numbers::pi));
		const ImU32 pulse = MenuKit::alpha(palette.accent, beating);
		const bool mapping = state.mapping && *state.mapping < rows.size();
		/* What comes after the prompt: the seconds left and two buttons,
		 * or how to let it be */
		float after = ImGui::CalcTextSize("Esc cancels").x;

		if (mapping) {
			size_t done = 0, all = 0;

			for (size_t r = 0; r < rows.size(); r++)
				if (rows[r].catchable && !rows[r].half)
					all++, done += r <= *state.mapping;
			say(std::to_string(done) + " / " + std::to_string(all), palette.accent);
			after = ImGui::CalcTextSize("6 s").x + space + kit.button_size("Skip ›").x + space + kit.button_size("Stop").x;
			if (state.on_pad() && !state.pills[MenuState::PILL_PRESS].empty())
				after += kit.pill_size(state.pills[MenuState::PILL_PRESS].c_str()).x + kit.px(4.0f);
		}
		say(caught->name, palette.accent);

		const std::string asked = prompt(state, caught->slot);
		const float room = std::max(0.0f, right - x - space - after);
		const std::string shown = MenuKit::fitted(asked, room);

		MenuKit::write(list, { x, y }, pulse, shown.c_str());
		x += MenuKit::measure(shown.c_str()) + space;
		if (!mapping) {
			say("Esc cancels", palette.faint);
			return height;
		}

		const uint64_t gone = SDL_GetTicks() - state.mapping_since;
		const uint64_t left = gone >= MAPPING_MS ? 0 : (MAPPING_MS - gone + 999) / 1000;
		const float button_y = at.y + (height - kit.button_size("Stop").y) / 2.0f;

		say(std::to_string(left) + " s", palette.faint);
		/* On a gamepad, the button that presses held is Skip: its pill,
		 * and the button filling as it is held */
		if (state.on_pad() && !state.pills[MenuState::PILL_PRESS].empty()) {
			const ImVec2 pill = kit.pill_size(state.pills[MenuState::PILL_PRESS].c_str());

			kit.pill(list, { x, at.y + (height - pill.y) / 2.0f }, state.pills[MenuState::PILL_PRESS].c_str());
			x += pill.x + kit.px(4.0f);
		}

		const ImVec2 skip = kit.button_size("Skip ›");

		if (kit.button("Skip ›", { x, button_y }, false))
			state.mapping_since = 0;
		if (state.skip_hold > 0.0f)
			list->AddRectFilled({ x, button_y }, { x + skip.x * state.skip_hold, button_y + skip.y },
			                    MenuKit::alpha(palette.action, 0.35f));
		x += skip.x + space;
		if (kit.button("Stop", { x, button_y }, false))
			state.stop_catching();
		return height;
	}

	/* The control the frame of the gamepad is on, else the one under the
	 * mouse in the list, else the one worked now: its name and its inputs
	 * (the other joysticks by name), filled as far as it is worked */
	const std::vector<Group> all = groups(state, rows);
	const auto picked = std::find_if(all.begin(), all.end(), [](const Group &group) { return group.chosen; });
	const auto worked = std::find_if(all.begin(), all.end(), [](const Group &group) { return group.lit; });
	const auto pointed = state.hovered_control >= 0 && static_cast<size_t>(state.hovered_control) < all.size() ?
	                     all.begin() + state.hovered_control : all.end();
	const auto told_of = picked != all.end() ? picked : pointed != all.end() ? pointed : worked;

	if (told_of != all.end()) {
		const Group &group = *told_of;
		std::string text;

		for (const Group::Part &part : group.parts)
			text += (text.empty() ? "" : "  ") + part_text(state, rows, part, kind, Others{ {}, state.joysticks });
		say(group.name, palette.accent);

		const float chip_w = std::min(MenuKit::measure(text.c_str()) + 2.0f * kit.px(7.0f), std::max(0.0f, right - x));
		const ImVec2 ca = { x, y - kit.px(2.0f) }, cb = { x + chip_w, y + line + kit.px(2.0f) };

		if (group.amount > 0.01f) {
			list->PushClipRect(ca, { ca.x + chip_w * group.amount, cb.y }, true);
			list->AddRectFilled(ca, cb, MenuKit::alpha(palette.accent, AMOUNT_ALPHA), kit.px(5.0f));
			list->PopClipRect();
		}
		list->AddRect(ca, cb, palette.stroke, kit.px(5.0f), ImDrawFlags_None, kit.stroke());
		MenuKit::write(list, { x + kit.px(7.0f), y }, palette.ink,
		               MenuKit::fitted(text, chip_w - 2.0f * kit.px(7.0f)).c_str());
		x += chip_w + space;
		if (picked == all.end())
			return height;
		/* What the buttons do to it, PILL_PRESS to PILL_RESET */
		static constexpr const char *WORDS[] = { "change", "clear", "reset" };

		for (size_t i = 0; i < std::size(WORDS); i++) {
			const std::string &name = state.pills[MenuState::PILL_PRESS + i];
			const float pill_w = kit.pill_size(name.c_str()).x, word_w = ImGui::CalcTextSize(WORDS[i]).x;

			if (name.empty() || x + pill_w + kit.px(4.0f) + word_w > right)
				continue;
			kit.pill(list, { x, at.y + (height - kit.pill_size(name.c_str()).y) / 2.0f }, name.c_str());
			x += pill_w + kit.px(4.0f);
			list->AddText({ x, y }, palette.dim, WORDS[i]);
			x += word_w + space;
		}
		return height;
	}
	const float rest_h = ImGui::CalcTextSize(told, nullptr, false, width).y;

	list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { at.x, at.y + (height - rest_h) / 2.0f }, palette.dim, told,
	              nullptr, width);
	return height;
}

} /* namespace odin */

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

#include "odin/host/input/bindings.hpp"

#include "odin/common/repeats.hpp"
#include "odin/host/app/hotkeys.hpp"
#include "odin/host/input/keyboard.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace odin {

namespace {

/* The keys of the cabinet as OPTIONS tells them, and the gamepad of each
 * player: a stick on the keys of the directions, the second stick of
 * player 1 on those of player 2, the gears on the keys of LB and RB.
 * TEST and SERVICE, the buttons of the cabinet on its I/O board, which
 * the BOOTROM and the games all read, are those [hotkeys] gives them
 * (held_inputs); the switches of the main board have no key. The coin of
 * a player is also the SELECT of their pad. */
struct Default {
	Role role;
	const char *key1;	/* the key of player 1, or of the cabinet */
	const char *key2;	/* the key of player 2 */
	const char *pad;
};

const Default DEFAULTS[] = {
	{ Role::START, "1", "2", "start" },
	{ Role::UP, "Up", "R", "dpup" },
	{ Role::DOWN, "Down", "F", "dpdown" },
	{ Role::LEFT, "Left", "D", "dpleft" },
	{ Role::RIGHT, "Right", "G", "dpright" },
	{ Role::BUTTON1, "Left Ctrl", "A", "a" },
	{ Role::BUTTON2, "Left Alt", "S", "b" },
	{ Role::BUTTON3, "Left Shift", "Q", "x" },
	{ Role::BUTTON4, "Z", "W", "y" },
	{ Role::BUTTON5, "X", "E", "leftshoulder" },
	{ Role::BUTTON6, "C", "T", "rightshoulder" },
	{ Role::BUTTON7, "V", nullptr, "lefttrigger" },
	{ Role::BUTTON8, "B", nullptr, "righttrigger" },
	{ Role::STICK_UP, "Up", "R", "-lefty" },
	{ Role::STICK_DOWN, "Down", "F", "+lefty" },
	{ Role::STICK_LEFT, "Left", "D", "-leftx" },
	{ Role::STICK_RIGHT, "Right", "G", "+leftx" },
	{ Role::STICK2_UP, "R", nullptr, "-righty" },
	{ Role::STICK2_DOWN, "F", nullptr, "+righty" },
	{ Role::STICK2_LEFT, "D", nullptr, "-rightx" },
	{ Role::STICK2_RIGHT, "G", nullptr, "+rightx" },
	{ Role::GEAR_UP, "C", nullptr, "rightshoulder" },
	{ Role::GEAR_DOWN, "X", nullptr, "leftshoulder" },
	{ Role::STEER, nullptr, nullptr, "leftx" },
	{ Role::ACCEL, nullptr, nullptr, "righttrigger" },
	{ Role::BRAKE, nullptr, nullptr, "lefttrigger" },
	{ Role::STICK_X, nullptr, nullptr, "leftx" },
	{ Role::STICK_Y, nullptr, nullptr, "lefty" },
	{ Role::STICK2_X, nullptr, nullptr, "rightx" },
	{ Role::STICK2_Y, nullptr, nullptr, "righty" },
	{ Role::GUN_X, nullptr, nullptr, "leftx" },
	{ Role::GUN_Y, nullptr, nullptr, "lefty" },
	{ Role::COIN1, "5", nullptr, "back" },
	{ Role::COIN2, "6", nullptr, "back" },
	{ Role::BOARD_TEST, nullptr, nullptr, nullptr },
	{ Role::BOARD_SERVICE, nullptr, nullptr, nullptr },
};

/* The keys that move an analog control: left or up, right or down, for
 * player 1 then player 2; those of the directions of the player, the
 * second stick of player 1 on those of player 2. The gas on Up and the
 * brake on Down, as xemu's Chihiro has them. */
struct KeysToward {
	Role role;
	const char *keys[2][2];	/* [player][positive] */
};

const KeysToward KEYS_TOWARD[] = {
	{ Role::STEER, { { "Left", "Right" }, { "D", "G" } } },
	{ Role::ACCEL, { { nullptr, "Up" }, { nullptr, "R" } } },
	{ Role::BRAKE, { { nullptr, "Down" }, { nullptr, "F" } } },
	{ Role::STICK_X, { { "Left", "Right" }, { "D", "G" } } },
	{ Role::STICK_Y, { { "Up", "Down" }, { "R", "F" } } },
	{ Role::STICK2_X, { { "D", "G" }, { nullptr, nullptr } } },
	{ Role::STICK2_Y, { { "R", "F" }, { nullptr, nullptr } } },
	{ Role::GUN_X, { { "Left", "Right" }, { "D", "G" } } },
	{ Role::GUN_Y, { { "Up", "Down" }, { "R", "F" } } },
};

/* The mouse, in a game where player 1 has a sight: it aims for player 1,
 * the left button fires, the right one is the second button, the middle
 * one Start. */
const std::pair<Role, const char *> MOUSE_DEFAULTS[] = {
	{ Role::GUN_X, "x" }, { Role::GUN_Y, "y" }, { Role::GUN_OFFSCREEN, "off" },
	{ Role::BUTTON1, "left" }, { Role::BUTTON2, "right" }, { Role::START, "middle" },
};

/* A joystick that is no gamepad: its first axis steers, that of the wheel
 * on every wheel of the mapping of EmulationStation (Batocera's
 * es_input.cfg: "joystick1left" on axis 0 for all 29). Its pedals, paddles,
 * Start and Select are the parts of a known wheel (wheels.cpp); on another
 * joystick, the user takes those on the page Input. */
const std::pair<Role, const char *> JOY_DEFAULTS[] = {
	{ Role::STEER, "axis0" }, { Role::ACCEL, "gas" }, { Role::BRAKE, "brake" }, { Role::GEAR_DOWN, "paddle_left" },
	{ Role::GEAR_UP, "paddle_right" }, { Role::START, "start" }, { Role::COIN1, "select" },
};

/* What a line takes: what is pressed or not (a key, a button, half a
 * stick, a trigger), the whole axis of a pad, or both (a pedal) */
enum class Takes { PRESSED, AXIS, BOTH };

/* A list of inputs, "key:A, pad:b"; nothing when one is not an input, or
 * not of the kind the line takes. */
std::optional<std::vector<HostInput>>
parse_list(std::string_view value, Takes takes)
{
	std::vector<HostInput> inputs;

	for (const std::string_view item : list_items(value)) {
		const auto input = HostInput::parse(item);
		const bool pressed = input && (input->kind == HostInput::Kind::PAD ? input->pad.is_digital() :
		                               input->kind == HostInput::Kind::JOY ? input->joy.is_digital() :
		                                                                     !input->is_axis());

		if (!input || (takes == Takes::PRESSED && !pressed) || (takes == Takes::AXIS && !input->is_axis()))
			return std::nullopt;
		inputs.push_back(*input);
	}
	return inputs;
}

Role
cabinet_role(size_t index)
{
	return static_cast<Role>(static_cast<size_t>(Role::COIN1) + index);
}

size_t
cabinet_index(Role role)
{
	return static_cast<size_t>(role) - static_cast<size_t>(Role::COIN1);
}

/* The inputs of a held hotkey (Hotkeys::held): its keys, with player 1
 * alone (`keys`), then its button with the hotkey button held, or Guide
 * alone */
std::vector<HostInput>
held_inputs(const Hotkeys::Binding &binding, bool keys, int hotkey)
{
	std::vector<HostInput> out;

	for (const HotKey &key : binding.keys)
		if (keys && key.key != SDLK_UNKNOWN) {
			HostInput input{};

			input.kind = HostInput::Kind::KEY;
			input.key = key.key;
			out.push_back(input);
		}
	if (binding.pad != Hotkeys::NO_PAD) {
		HostInput input{};

		input.kind = HostInput::Kind::PAD;
		input.pad = { PadInput::Type::BUTTON, static_cast<uint8_t>(binding.pad) };
		if (binding.pad != Hotkeys::GUIDE_ALONE)
			input.pad.with = static_cast<int8_t>(hotkey);
		out.push_back(input);
	}
	return out;
}

/* The inputs of a button: the keys, then the inputs of the gamepad, of its
 * role or those the list of games gives it in their place, then those of
 * the joystick of its role or the part of a wheel the list gives it in
 * their place; a name that is not one is said on stderr */
std::vector<HostInput>
own_inputs(const std::vector<HostInput> &of_role, const GameControl &control, const std::string &game)
{
	using Kind = HostInput::Kind;
	std::vector<HostInput> out;

	for (const auto &[kind, names] : { std::pair{ Kind::KEY, &control.keys }, std::pair{ Kind::PAD, &control.pad } }) {
		if (names->empty()) {
			std::copy_if(of_role.begin(), of_role.end(), std::back_inserter(out),
			             [kind](const HostInput &input) { return input.kind == kind; });
			continue;
		}
		for (const std::string &name : *names) {
			const auto input = HostInput::parse((kind == Kind::KEY ? "key:" : "pad:") + name);

			if (input && (kind == Kind::KEY || input->pad.is_digital()))
				out.push_back(*input);
			else
				repeats::print(stderr, "ERROR: controls: %s: %s: not a %s: %s\n", game.c_str(), control.id.c_str(),
				             kind == Kind::KEY ? "key" : "button of a gamepad", name.c_str());
		}
	}
	/* A part of a wheel the list of games gives it in place of its role's */
	if (!control.wheel.empty()) {
		const auto input = HostInput::parse("joy:" + control.wheel);

		if (input && input->joy.type == JoyInput::Type::PART)
			out.push_back(*input);
		else
			repeats::print(stderr, "ERROR: controls: %s: %s: not a part of a wheel: %s\n", game.c_str(),
			               control.id.c_str(), control.wheel.c_str());
		return out;
	}
	std::copy_if(of_role.begin(), of_role.end(), std::back_inserter(out),
	             [](const HostInput &input) { return input.kind == Kind::JOY; });
	return out;
}

/* An analog control that only goes one way */
bool
is_pedal(const GameControl &control)
{
	return control.type == GameControl::Type::ANALOG && !GameControl::half(control.low, false);
}

/* The end of the key of a half of a control: a lever down or up, an analog
 * control as GameControl::half() names it */
const char *
half_of(const GameControl &control, bool positive)
{
	if (control.type == GameControl::Type::POSITIONS)
		return positive ? "_up" : "_down";
	return GameControl::half(control.low, positive);
}

/* The value of a line of [controls.<game>]: none for the defaults */
std::optional<std::string>
value_of(const std::vector<HostInput> &inputs, const std::vector<HostInput> &standard)
{
	return inputs == standard ? std::nullopt : std::optional<std::string>(HostInput::text(inputs));
}

constexpr std::pair<std::string_view, uint8_t> HAT_WAYS[] = {
	{ "_up", SDL_HAT_UP }, { "_right", SDL_HAT_RIGHT }, { "_down", SDL_HAT_DOWN }, { "_left", SDL_HAT_LEFT },
};

/* A number of an input of a joystick, 0 to 255, alone at the start of
 * the text: what follows it, nothing when there is no number */
std::optional<std::string_view>
joy_number(std::string_view text, uint8_t &number)
{
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);

	if (error != std::errc() || end == text.data() || (text.size() > 1 && text.front() == '0' &&
	                                                    std::isdigit(static_cast<unsigned char>(text[1]))))
		return std::nullopt;
	return text.substr(static_cast<size_t>(end - text.data()));
}

/* "button12", "axis0", "axis2:invert", "-axis1", "+axis1", "hat0_up", "gas" */
std::optional<HostInput>
parse_joy(std::string_view text)
{
	HostInput out{};
	JoyInput &joy = out.joy;

	out.kind = HostInput::Kind::JOY;
	for (size_t p = 0; p < WHEEL_PARTS; p++)
		if (text == WHEEL_PART_NAMES[p]) {
			joy.type = JoyInput::Type::PART;
			joy.part = static_cast<uint8_t>(p);
			return out;
		}
	if (text.substr(0, 6) == "button") {
		joy.type = JoyInput::Type::BUTTON;
		if (joy_number(text.substr(6), joy.index) != std::string_view())
			return std::nullopt;
		return out;
	}
	if (text.substr(0, 3) == "hat") {
		const auto way = joy_number(text.substr(3), joy.index);

		joy.type = JoyInput::Type::HAT;
		for (const auto &[name, bit] : HAT_WAYS)
			if (way == name)
				joy.way = bit;
		if (!joy.way)
			return std::nullopt;
		return out;
	}
	joy.type = JoyInput::Type::AXIS;
	if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
		joy.type = text.front() == '-' ? JoyInput::Type::AXIS_NEGATIVE : JoyInput::Type::AXIS_POSITIVE;
		text.remove_prefix(1);
	}
	if (text.substr(0, 4) != "axis")
		return std::nullopt;

	const auto rest = joy_number(text.substr(4), joy.index);

	joy.inverted = rest == ":invert";
	if (!rest || (!rest->empty() && (!joy.inverted || joy.type != JoyInput::Type::AXIS)))
		return std::nullopt;
	return out;
}

std::string
joy_text(const JoyInput &joy)
{
	const std::string index = std::to_string(joy.index);

	switch (joy.type) {
	case JoyInput::Type::BUTTON:
		return "button" + index;
	case JoyInput::Type::HAT:
		for (const auto &[name, bit] : HAT_WAYS)
			if (joy.way == bit)
				return "hat" + index + std::string(name);
		return "hat" + index;
	case JoyInput::Type::AXIS_NEGATIVE:
		return "-axis" + index;
	case JoyInput::Type::AXIS_POSITIVE:
		return "+axis" + index;
	case JoyInput::Type::PART:
		return WHEEL_PART_NAMES[joy.part % WHEEL_PARTS];
	case JoyInput::Type::AXIS:
		break;
	}
	return "axis" + index + (joy.inverted ? ":invert" : "");
}

/* "left", "middle", "right", "side", "extra", "x", "y", "off" */
std::optional<HostInput>
parse_mouse(std::string_view text)
{
	static const std::pair<std::string_view, MouseInput> NAMES[] = {
		{ "left", { MouseInput::Type::BUTTON, 0 } }, { "middle", { MouseInput::Type::BUTTON, 1 } },
		{ "right", { MouseInput::Type::BUTTON, 2 } }, { "side", { MouseInput::Type::BUTTON, 3 } },
		{ "extra", { MouseInput::Type::BUTTON, 4 } }, { "x", { MouseInput::Type::X, 0 } },
		{ "y", { MouseInput::Type::Y, 0 } }, { "off", { MouseInput::Type::OFF, 0 } },
	};
	HostInput out{};

	out.kind = HostInput::Kind::MOUSE;
	for (const auto &[name, mouse] : NAMES)
		if (text == name) {
			out.mouse = mouse;
			return out;
		}
	return std::nullopt;
}

/* "a", "leftx", "-lefty", "rightx:invert", "back+start" */
std::optional<HostInput>
parse_pad(std::string_view text)
{
	HostInput out{};

	/* A button held first, then another */
	if (const size_t plus = text.find('+', 1); plus != std::string_view::npos) {
		const std::string first(text.substr(0, plus)), second(text.substr(plus + 1));
		const SDL_GamepadButton with = SDL_GetGamepadButtonFromString(first.c_str());
		const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(second.c_str());

		if (with == SDL_GAMEPAD_BUTTON_INVALID || button == SDL_GAMEPAD_BUTTON_INVALID || with == button)
			return std::nullopt;
		out.kind = HostInput::Kind::PAD;
		out.pad = PadInput{ PadInput::Type::BUTTON, static_cast<uint8_t>(button) };
		out.pad.with = static_cast<int8_t>(with);
		return out;
	}

	constexpr std::string_view INVERT = ":invert";
	const bool inverted = text.size() > INVERT.size() && text.substr(text.size() - INVERT.size()) == INVERT;

	if (inverted)
		text.remove_suffix(INVERT.size());

	PadInput::Type half = PadInput::Type::AXIS;

	if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
		half = (text.front() == '-') ? PadInput::Type::AXIS_NEGATIVE : PadInput::Type::AXIS_POSITIVE;
		text.remove_prefix(1);
	}

	const std::string name(text);
	const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.c_str());
	const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());

	out.kind = HostInput::Kind::PAD;
	if (axis != SDL_GAMEPAD_AXIS_INVALID) {
		/* Half a trigger is no input, a stick is turned over whole */
		if (half != PadInput::Type::AXIS && axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
			return std::nullopt;
		if (inverted && (half != PadInput::Type::AXIS || axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER))
			return std::nullopt;
		out.pad = PadInput{ half, static_cast<uint8_t>(axis), inverted };
		return out;
	}
	if (inverted || half != PadInput::Type::AXIS || button == SDL_GAMEPAD_BUTTON_INVALID)
		return std::nullopt;
	out.pad = PadInput{ PadInput::Type::BUTTON, static_cast<uint8_t>(button) };
	return out;
}

} /* namespace */

bool
PadInput::is_trigger() const
{
	return type == Type::AXIS && index >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
}

std::optional<HostInput>
HostInput::parse(std::string_view text)
{
	HostInput out{};

	if (text.substr(0, 4) == "key:") {
		const std::string name(text.substr(4));

		out.kind = Kind::KEY;
		out.key = key_named(name.c_str());
		if (out.key == SDLK_UNKNOWN)
			return std::nullopt;
		return out;
	}
	if (text.substr(0, 6) == "mouse:")
		return parse_mouse(text.substr(6));
	if (text.substr(0, 4) == "joy:")
		return parse_joy(text.substr(4));
	/* Another joystick than the player's: its GUID, 32 hex digits */
	if (text.substr(0, 4) == "joy@") {
		const std::string_view guid = text.substr(4, 32);
		std::optional<HostInput> other;

		if (guid.size() == 32 && text.substr(36, 1) == ":" &&
		    std::all_of(guid.begin(), guid.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
			other = parse_joy(text.substr(37));
		if (other)
			std::transform(guid.begin(), guid.end(), std::back_inserter(other->joy.device),
			               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		return other;
	}
	if (text.substr(0, 4) != "pad:")
		return std::nullopt;
	return parse_pad(text.substr(4));
}

std::string
HostInput::text() const
{
	static const char *const BUTTONS[] = { "left", "middle", "right", "side", "extra" };
	static_assert(std::size(BUTTONS) == MOUSE_BUTTONS);

	switch (kind) {
	case Kind::KEY:
		return std::string("key:") + SDL_GetKeyName(key);
	case Kind::MOUSE:
		return std::string("mouse:") + (mouse.type == MouseInput::Type::X   ? "x" :
		                                mouse.type == MouseInput::Type::Y   ? "y" :
		                                mouse.type == MouseInput::Type::OFF ? "off" :
		                                                                      BUTTONS[mouse.button % MOUSE_BUTTONS]);
	case Kind::JOY:
		return (joy.device.empty() ? "joy:" : "joy@" + joy.device + ":") + joy_text(joy);
	case Kind::PAD:
		break;
	}

	const char *const name = pad.type == PadInput::Type::BUTTON ?
	                         SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(pad.index)) :
	                         SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(pad.index));
	std::string out = "pad:";

	if (pad.with >= 0)
		out = out + SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(pad.with)) + "+";
	if (pad.type == PadInput::Type::AXIS_NEGATIVE)
		out += '-';
	else if (pad.type == PadInput::Type::AXIS_POSITIVE)
		out += '+';
	out += name ? name : "";
	if (pad.inverted)
		out += ":invert";
	return out;
}

std::string
HostInput::text(const std::vector<HostInput> &inputs)
{
	std::string out;

	for (const HostInput &input : inputs)
		out += (out.empty() ? "" : ", ") + input.text();
	return out;
}

std::vector<HostInput>
Bindings::defaults(Role role, unsigned player)
{
	std::vector<HostInput> out;

	for (const Default &entry : DEFAULTS) {
		if (entry.role != role)
			continue;
		const char *const key = (player && !role_of_cabinet(role)) ? entry.key2 : entry.key1;

		if (const auto input = key ? HostInput::parse(std::string("key:") + key) : std::nullopt)
			out.push_back(*input);
		if (const auto input = entry.pad ? HostInput::parse(std::string("pad:") + entry.pad) : std::nullopt)
			out.push_back(*input);
	}
	for (const auto &[of, name] : JOY_DEFAULTS)
		if (of == role)
			out.push_back(*HostInput::parse(std::string("joy:") + name));
	return out;
}

std::vector<HostInput>
Bindings::keys_toward(Role role, unsigned player, bool positive)
{
	std::vector<HostInput> out;

	for (const KeysToward &entry : KEYS_TOWARD) {
		const char *const key = entry.keys[player & 1][positive];

		if (entry.role != role || !key)
			continue;
		if (const auto input = HostInput::parse(std::string("key:") + key))
			out.push_back(*input);
	}
	return out;
}

std::vector<HostInput>
Bindings::with_kinds(const std::vector<HostInput> &inputs, const std::vector<HostInput> &standard,
                     std::span<const HostInput::Kind> kinds, bool to_defaults)
{
	const auto has = [](const std::vector<HostInput> &list, const HostInput &input) {
		return std::find(list.begin(), list.end(), input) != list.end();
	};
	const auto of = [kinds](const HostInput &input) {
		return std::find(kinds.begin(), kinds.end(), input.kind) != kinds.end();
	};
	std::vector<HostInput> out;

	if (to_defaults)
		for (const HostInput &input : standard)
			if (of(input) || has(inputs, input))
				out.push_back(input);
	for (const HostInput &input : inputs)
		if (!of(input) && !has(out, input))
			out.push_back(input);
	return out;
}

Bindings::Bindings(const std::vector<GameControl> &panel, const Settings &settings, const std::string &game)
	: m_panel(panel), m_controls(panel.size())
{
	set_defaults(settings, game);
	read_settings(settings, game);
}

/* The inputs of each control and of the cabinet when odin.ini says
 * nothing, kept as such: TEST and the SERVICE of each player those of the
 * page Hotkeys ([hotkeys] test, service), the SERVICE of player 2 on its
 * gamepad alone */
void
Bindings::set_defaults(const Settings &settings, const std::string &game)
{
	const std::vector<GameControl> &panel = m_panel;
	const Hotkeys hotkeys(settings, false);
	/* The players with a sight: the mouse aims for them (player 2 with
	 * their light gun) */
	const std::array<bool, 2> aims = sights_of(panel);

	for (size_t c = 0; c < panel.size(); c++) {
		const GameControl &control = panel[c];

		if (control.type == GameControl::Type::POSITIONS) {
			m_controls[c][1] = defaults(Role::GEAR_DOWN, control.player);
			m_controls[c][2] = defaults(Role::GEAR_UP, control.player);
			continue;
		}
		m_controls[c][0] = own_inputs(control.role == Role::SERVICE ?
		                                  held_inputs(hotkeys.of(Hotkeys::Action::SERVICE), control.player == 0,
		                                              hotkeys.button()) :
		                                  defaults(control.role, control.player),
		                              control, game);
		for (const auto &[role, name] : MOUSE_DEFAULTS)
			if (aims[control.player & 1] && role == control.role)
				m_controls[c][0].push_back(*HostInput::parse(std::string("mouse:") + name));
		if (control.type == GameControl::Type::ANALOG) {
			m_controls[c][1] = keys_toward(control.role, control.player, false);
			m_controls[c][2] = keys_toward(control.role, control.player, true);
		}
	}
	for (size_t r = 0; r < m_cabinet.size(); r++)
		m_cabinet[r] = cabinet_role(r) == Role::TEST ?
		                   held_inputs(hotkeys.of(Hotkeys::Action::TEST), true, hotkeys.button()) :
		                   defaults(cabinet_role(r), 0);
	m_defaults = m_controls;
	m_cabinet_defaults = m_cabinet;
}

/* [controls.<game>]: what the user chose instead, line by line */
void
Bindings::read_settings(const Settings &settings, const std::string &game)
{
	const std::vector<GameControl> &panel = m_panel;

	for (const auto &[key, value] : settings.section("controls." + game)) {
		std::vector<HostInput> *target = nullptr;
		std::vector<HostInput> *pedal_down = nullptr;	/* a pedal: where what is pressed goes */
		Takes takes = Takes::PRESSED;

		/* Away from the picture is the sight's own: the light gun or the
		 * mouse aiming off it presses it, whatever the line says */
		if (std::any_of(panel.begin(), panel.end(), [&key](const GameControl &control) {
			    return control.role == Role::GUN_OFFSCREEN && key == control.id;
		    }))
			continue;
		for (size_t c = 0; c < panel.size() && !target; c++) {
			const GameControl &control = panel[c];
			const bool lever = control.type == GameControl::Type::POSITIONS;
			const bool analog = control.type == GameControl::Type::ANALOG;
			const bool pedal = is_pedal(control);

			if (lever || (analog && !pedal))
				for (const bool positive : { false, true }) {
					const char *const half = half_of(control, positive);

					if (key == control.id + half)
						target = &m_controls[c][1 + positive];
				}
			if (target || lever || key != control.id)
				continue;
			target = &m_controls[c][0];
			takes = pedal ? Takes::BOTH : analog ? Takes::AXIS : Takes::PRESSED;
			if (pedal)
				pedal_down = &m_controls[c][2];
		}
		for (size_t r = 0; r < m_cabinet.size() && !target; r++)
			if (key == role_name(cabinet_role(r)))
				target = &m_cabinet[r];

		const auto inputs = target ? parse_list(value, takes) : std::nullopt;

		if (!inputs) {
			repeats::print(stderr, "ERROR: controls: [controls.%s]: not right: %s = %s\n", game.c_str(),
			             key.c_str(), value.c_str());
			continue;
		}
		if (!pedal_down) {
			*target = *inputs;
			continue;
		}
		target->clear();
		pedal_down->clear();
		for (const HostInput &input : *inputs)
			(input.is_axis() ? target : pedal_down)->push_back(input);
	}
}


const std::vector<HostInput> &
Bindings::cabinet(Role role) const
{
	return m_cabinet[cabinet_index(role)];
}

const std::vector<HostInput> &
Bindings::cabinet_standard(Role role) const
{
	return m_cabinet_defaults[cabinet_index(role)];
}

Bindings::Line
Bindings::line(size_t control, unsigned part, const std::vector<HostInput> &inputs) const
{
	const GameControl &c = m_panel[control];

	if (is_pedal(c)) {
		std::vector<HostInput> both = part == 0 ? inputs : m_controls[control][0];
		std::vector<HostInput> standard = m_defaults[control][0];

		for (const HostInput &input : part == 2 ? inputs : m_controls[control][2])
			both.push_back(input);
		for (const HostInput &input : m_defaults[control][2])
			standard.push_back(input);
		return { c.id, value_of(both, standard) };
	}

	std::string key = c.id;

	if (part)
		key += half_of(c, part == 2);
	return { key, value_of(inputs, m_defaults[control][part]) };
}

std::vector<std::pair<std::string, std::string>>
Bindings::standard_lines() const
{
	std::vector<std::pair<std::string, std::string>> out;

	for (size_t c = 0; c < m_panel.size(); c++) {
		const GameControl &control = m_panel[c];
		const bool whole = control.type == GameControl::Type::BUTTON || is_pedal(control);
		const unsigned first = control.type == GameControl::Type::POSITIONS ? 1 : 0;

		/* Away from the picture has no line (read_settings) */
		if (control.role == Role::GUN_OFFSCREEN)
			continue;

		for (unsigned part = first; part <= (whole ? 0u : 2u); part++) {
			std::vector<HostInput> inputs = m_defaults[c][part];

			/* A pedal: what presses it on the line of its axes */
			if (is_pedal(control))
				inputs.insert(inputs.end(), m_defaults[c][2].begin(), m_defaults[c][2].end());
			out.emplace_back(line(c, part, m_defaults[c][part]).key, HostInput::text(inputs));
		}
	}
	for (size_t r = 0; r < m_cabinet_defaults.size(); r++)
		out.emplace_back(role_name(cabinet_role(r)), HostInput::text(m_cabinet_defaults[r]));
	return out;
}

Bindings::Line
Bindings::cabinet_line(Role role, const std::vector<HostInput> &inputs) const
{
	const std::vector<HostInput> &standard = m_cabinet_defaults[cabinet_index(role)];

	return { role_name(role), value_of(inputs, standard) };
}

} /* namespace odin */

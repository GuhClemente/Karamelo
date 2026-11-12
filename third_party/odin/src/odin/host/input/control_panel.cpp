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

#include "odin/host/input/control_panel.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace odin {

namespace {

GameControl
button(const char *name, const char *id, uint8_t player, uint16_t mask, Role role)
{
	GameControl control{};

	control.name = name;
	control.id = id;
	control.type = GameControl::Type::BUTTON;
	control.button = JvsSwitch{ player, mask };
	control.role = role;
	control.player = player;
	return control;
}

/* START, the directions and PUSH1 to PUSH6 of both players, in the words
 * of CabinetInputs */
std::vector<GameControl>
standard_panel()
{
	static const char *const IDS[2][11] = {
		{ "1p_start", "1p_up", "1p_down", "1p_left", "1p_right", "1p_push1", "1p_push2", "1p_push3", "1p_push4",
		  "1p_push5", "1p_push6" },
		{ "2p_start", "2p_up", "2p_down", "2p_left", "2p_right", "2p_push1", "2p_push2", "2p_push3", "2p_push4",
		  "2p_push5", "2p_push6" },
	};
	static const char *const NAMES[2][11] = {
		{ "1P START", "1P UP", "1P DOWN", "1P LEFT", "1P RIGHT", "1P PUSH1", "1P PUSH2", "1P PUSH3", "1P PUSH4",
		  "1P PUSH5", "1P PUSH6" },
		{ "2P START", "2P UP", "2P DOWN", "2P LEFT", "2P RIGHT", "2P PUSH1", "2P PUSH2", "2P PUSH3", "2P PUSH4",
		  "2P PUSH5", "2P PUSH6" },
	};
	static const uint16_t MASKS[11] = { 0x8000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040,
	                                    0x0020, 0x0010 };
	static const Role ROLES[11] = { Role::START, Role::UP, Role::DOWN, Role::LEFT, Role::RIGHT, Role::BUTTON1,
	                                Role::BUTTON2, Role::BUTTON3, Role::BUTTON4, Role::BUTTON5, Role::BUTTON6 };
	std::vector<GameControl> panel;

	for (uint8_t player = 0; player < 2; player++)
		for (unsigned i = 0; i < 11; i++)
			panel.push_back(button(NAMES[player][i], IDS[player][i], player, MASKS[i], ROLES[i]));
	return panel;
}

/* Every control of the panel where it rests; a channel no control takes
 * at 8000 */
CabinetInputs
idle_inputs(const std::vector<GameControl> &panel)
{
	CabinetInputs inputs = {};

	for (uint16_t &channel : inputs.jvs_analog)
		channel = 0x8000;
	for (const GameControl &control : panel)
		if (control.type == GameControl::Type::ANALOG)
			inputs.jvs_analog[control.analog] = ControlPanel::rest(control.low);
	return inputs;
}

/* The controls of the game, or the standard panel, and the SERVICE switch
 * of each player; those the player turns over ([players.<game>] invert)
 * the other way */
std::vector<GameControl>
panel_of(const std::vector<GameControl> &controls, const Settings &settings, const std::string &game)
{
	using Low = GameControl::Low;
	std::vector<GameControl> panel = controls.empty() ? standard_panel() : controls;
	const std::vector<std::string> inverted = inverted_of(settings, game);

	for (GameControl &c : panel)
		if (c.invertible && std::find(inverted.begin(), inverted.end(), c.id) != inverted.end())
			c.low = c.low == Low::LEFT ? Low::RIGHT : c.low == Low::RIGHT ? Low::LEFT :
			        c.low == Low::UP   ? Low::DOWN  : Low::UP;

	panel.push_back(button("1P SERVICE", GameControl::CABINET_IDS[0], 0, 0x4000, Role::SERVICE));
	panel.push_back(button("2P SERVICE", GameControl::CABINET_IDS[1], 1, 0x4000, Role::SERVICE));
	return panel;
}

/* Rounded to the nearest, held to the range of an axis */
int16_t
axis_value(double value)
{
	return static_cast<int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
}

} /* namespace */

std::vector<std::string>
inverted_of(const Settings &settings, const std::string &game)
{
	std::vector<std::string> out;

	for (const auto &[key, value] : settings.section("players." + game))
		if (key == "invert") {
			out.clear();
			for (const std::string_view id : list_items(value))
				out.emplace_back(id);
		}
	return out;
}

PadTuning
PadTuning::of(const Settings &settings, const std::string &guid, bool tell)
{
	PadTuning out;

	for (const std::string &section : { std::string("gamepads"), "gamepad." + guid }) {
		for (const auto &[key, value] : settings.section(section)) {
			const bool dead = key == "dead_zone";
			int *const field = dead ? &out.dead_zone : key == "trigger_threshold" ? &out.trigger_threshold : nullptr;
			int percent = -1;
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), percent);

			if (field && error == std::errc() && end == value.data() + value.size() && percent >= 0 &&
			    percent <= (dead ? MOST_DEAD_ZONE : MOST_TRIGGER_THRESHOLD))
				*field = percent;
			else if (tell)
				refuse_setting(section, key, value);
		}
	}
	return out;
}

void
PadTuning::shape(int16_t &x, int16_t &y) const
{
	if (dead_zone <= 0)
		return;

	const double fx = x / 32767.0, fy = y / 32767.0, length = std::hypot(fx, fy), edge = dead_zone / 100.0;
	const double k = length <= edge ? 0.0 : (length - edge) / (1.0 - edge) / length;

	x = axis_value(fx * k * 32767.0);
	y = axis_value(fy * k * 32767.0);
}

ControlPanel::ControlPanel(const std::vector<GameControl> &controls, const Settings &settings, const std::string &game)
	: m_controls(panel_of(controls, settings, game)), m_bindings(m_controls, settings, game),
	  m_inputs(idle_inputs(m_controls)), m_coins_were{}, m_coins{}, m_mouse_aims{}, m_mouse_moved{}, m_mouse_seen{},
	  m_mouse_x{ 0.5, 0.5 }, m_mouse_y{ 0.5, 0.5 }
{
	m_positions.assign(m_controls.size(), 0);
	m_levers_were.assign(m_controls.size(), { false, false });
	m_owner.assign(m_controls.size(), 0);
	for (size_t c = 0; c < m_controls.size(); c++) {
		m_keyed.push_back(rest(m_controls[c].low));
		m_kept.emplace_back(m_bindings.of(c).size() + 1, rest(m_controls[c].low));
		m_presence.emplace_back(m_bindings.of(c).size() + 1, NEVER);
	}
}

/* A stick turned over keeps its centre at 8000. */
uint16_t
ControlPanel::channel(const PadInput &input, GameControl::Low low, int16_t value)
{
	const bool over = GameControl::turned_over(low);

	if (input.is_trigger()) {
		const int word = std::max<int>(value, 0) * 0xFFFF / 0x7FFF;

		return static_cast<uint16_t>(over ? 0xFFFF - word : word);
	}
	const int toward = input.inverted ? -value : value;

	return static_cast<uint16_t>(std::clamp(over ? 0x8000 - toward : 0x8000 + toward, 0, 0xFFFF));
}

/* A whole axis of a joystick as that of a stick, turned over end for end */
uint16_t
ControlPanel::channel(const JoyInput &input, GameControl::Low low, int16_t value)
{
	const PadInput axis{ PadInput::Type::AXIS, static_cast<uint8_t>(SDL_GAMEPAD_AXIS_LEFTX) };

	return channel(axis, low, input.inverted ? static_cast<int16_t>(~value) : value);
}

/* At an end: further than half its way from the centre (a pedal rests a
 * little off its very end, two pedals on one axis a little off its
 * centre) */
int16_t
ControlPanel::pedal(std::optional<int16_t> rest, bool inverted, int16_t value)
{
	if (!rest)
		return inverted ? static_cast<int16_t>(~value) : value;
	if (*rest >= PRESSED)
		return static_cast<int16_t>(~value);
	if (*rest <= -PRESSED)
		return value;

	const int from = *rest, span = inverted ? from + 32768 : 32767 - from;
	const int half = std::max(inverted ? from - value : value - from, 0);

	return static_cast<int16_t>(static_cast<int64_t>(half) * 65535 / span - 32768);
}

namespace {

/* An input of a joystick as it is on that one: a part of a known wheel as
 * that wheel's own input, nullptr where the wheel has none */
const JoyInput *
own_input(const JoyState &joy, const JoyInput &input)
{
	if (input.type != JoyInput::Type::PART)
		return &input;

	const auto &part = joy.parts[input.part % WHEEL_PARTS];

	return part ? &*part : nullptr;
}

/* Where an axis of a joystick rests, once it has a value */
std::optional<int16_t>
rest_of(const JoyState &joy, size_t axis)
{
	if (axis >= joy.rest.size() || axis >= joy.known.size() || !joy.known[axis])
		return std::nullopt;
	return joy.rest[axis];
}

/* The joystick of an input: another one by its GUID (nullptr when it is
 * not plugged in; two of a make share it: another than the player's own
 * first), else that of the player */
const JoyState *
joy_of(const HostState &host, const JoyInput &input, unsigned player)
{
	const JoyState *found = nullptr;

	if (input.device.empty())
		return host.joys[player & 1];
	if (host.all_joys)
		for (const PluggedJoy &one : *host.all_joys)
			if (one.guid == input.device && (!found || found == host.joys[player & 1]))
				found = one.state;
	return found;
}

} /* namespace */

std::optional<uint16_t>
ControlPanel::joy_channel(const JoyState *joy, const JoyInput &input, const GameControl &control) const
{
	const JoyInput *const own = joy ? own_input(*joy, input) : nullptr;

	/* A pedal that is a button (some Thrustmaster wheels): all its way */
	if (own && own->type == JoyInput::Type::BUTTON && input.type == JoyInput::Type::PART &&
	    control.low == GameControl::Low::RELEASED)
		return own->index < joy->buttons.size() && joy->buttons[own->index] ? 0xFFFF : 0x0000;
	if (!own || own->type != JoyInput::Type::AXIS || own->index >= joy->axes.size() || !joy->known[own->index])
		return std::nullopt;

	const int16_t value = joy->axes[own->index];

	if (!GameControl::half(control.low, false)) {
		const PadInput whole{ PadInput::Type::AXIS, static_cast<uint8_t>(SDL_GAMEPAD_AXIS_LEFTX) };

		return channel(whole, control.low, pedal(rest_of(*joy, own->index), own->inverted, value));
	}
	if (control.role != Role::STEER)
		return channel(*own, control.low, value);
	return channel(*own, control.low, axis_value(value * m_steer_scale));
}

uint16_t
ControlPanel::rest(GameControl::Low low)
{
	if (low == GameControl::Low::RELEASED)
		return 0x0000;
	return low == GameControl::Low::PRESSED ? 0xFFFF : 0x8000;
}

uint16_t
ControlPanel::pointed(GameControl::Low low, double at)
{
	const PadInput axis{ PadInput::Type::AXIS, static_cast<uint8_t>(SDL_GAMEPAD_AXIS_LEFTX) };
	const long value = std::lround(std::clamp(at, 0.0, 1.0) * 65535.0) - 32768;

	return channel(axis, low, static_cast<int16_t>(value));
}

uint16_t
ControlPanel::end(GameControl::Low low, bool positive)
{
	const bool pedal = !GameControl::half(low, positive);
	const PadInput axis{ PadInput::Type::AXIS,
	                     static_cast<uint8_t>(pedal ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_LEFTX) };

	return channel(axis, low, positive ? 32767 : -32768);
}

bool
ControlPanel::held(const HostState &host, const HostInput &input, unsigned player) const
{
	if (input.kind == HostInput::Kind::KEY)
		return host.keys && std::find(host.keys->begin(), host.keys->end(), input.key) != host.keys->end();
	if (input.kind == HostInput::Kind::MOUSE) {
		/* Away from the picture counts while the mouse aims */
		const MouseState *const mouse = host.mice[player & 1];

		if (!mouse)
			return false;
		if (input.mouse.type == MouseInput::Type::OFF)
			return mouse->off && m_mouse_aims[player & 1];
		return input.mouse.type == MouseInput::Type::BUTTON && mouse->buttons[input.mouse.button];
	}
	if (input.kind == HostInput::Kind::JOY) {
		const JoyState *const joy = joy_of(host, input.joy, player);
		if (!joy)
			return false;

		const JoyInput *const own = own_input(*joy, input.joy);

		if (!own)
			return false;

		const JoyInput &in = *own;
		const bool axis = in.index < joy->axes.size() && joy->known[in.index];
		switch (in.type) {
		case JoyInput::Type::BUTTON:
			return in.index < joy->buttons.size() && joy->buttons[in.index];
		case JoyInput::Type::HAT:
			return in.index < joy->hats.size() && (joy->hats[in.index] & in.way);
		case JoyInput::Type::AXIS_NEGATIVE:
			return axis && joy->axes[in.index] <= -PRESSED;
		case JoyInput::Type::AXIS_POSITIVE:
			return axis && joy->axes[in.index] >= PRESSED;
		case JoyInput::Type::AXIS: {
			/* A pedal of a wheel pressed from half its way (HEURISTIC:
			 * where the cabinet's switches close is not known); else the
			 * bindings give no whole axis to what is pressed */
			return axis && input.joy.type == JoyInput::Type::PART &&
			       pedal(rest_of(*joy, in.index), in.inverted, joy->axes[in.index]) >= 0;
		}
		case JoyInput::Type::PART:
			break;
		}
		return false;
	}

	const PadState *const pad = host.pads[player & 1];

	if (!pad || (input.pad.with >= 0 && !pad->buttons[static_cast<size_t>(input.pad.with)]))
		return false;
	switch (input.pad.type) {
	case PadInput::Type::BUTTON:
		return pad->buttons[input.pad.index] && (input.pad.with >= 0 || !pad->shortcut[input.pad.index]);
	case PadInput::Type::AXIS_NEGATIVE:
		return pad->axes[input.pad.index] <= -PRESSED;
	case PadInput::Type::AXIS_POSITIVE:
		return pad->axes[input.pad.index] >= PRESSED;
	case PadInput::Type::AXIS:
		/* A trigger: the bindings give no whole stick to what is pressed */
		return pad->axes[input.pad.index] >= pad->trigger_pressed;
	}
	return false;
}

bool
ControlPanel::pressed(const HostState &host, const std::vector<HostInput> &inputs, unsigned player) const
{
	for (const HostInput &input : inputs)
		if (held(host, input, player))
			return true;
	return false;
}

bool
ControlPanel::touched(const HostState &host, size_t control) const
{
	const GameControl &c = m_controls[control];
	const PadState *const pad = host.pads[c.player & 1];

	if (pressed(host, m_bindings.lever(control, false), c.player) ||
	    pressed(host, m_bindings.lever(control, true), c.player))
		return true;
	if (c.type != GameControl::Type::ANALOG)
		return pressed(host, m_bindings.of(control), c.player);
	for (const HostInput &input : m_bindings.of(control)) {
		if (input.kind == HostInput::Kind::PAD && pad &&
		    std::abs(static_cast<int>(pad->axes[input.pad.index])) >= PRESSED)
			return true;
		/* An axis of a joystick from half its way, as a stick or a trigger */
		const auto at = input.kind == HostInput::Kind::JOY ? joy_channel(joy_of(host, input.joy, c.player), input.joy, c) :
		                                                     std::nullopt;

		if (at && std::abs(*at - rest(c.low)) >= (GameControl::half(c.low, false) ? 0x4000 : 0x8000))
			return true;
	}
	return false;
}

ControlNow
ControlPanel::now(const HostState &host, size_t control) const
{
	const GameControl &c = m_controls[control];
	const PadState *const pad = host.pads[c.player & 1];
	ControlNow out{ touched(host, control), 0.0f, m_positions[control] };

	if (c.type != GameControl::Type::ANALOG)
		return out;

	const bool back = pressed(host, m_bindings.toward(control, false), c.player);
	const bool on = pressed(host, m_bindings.toward(control, true), c.player);

	if (back != on)
		out.value = on ? 1.0f : GameControl::half(c.low, false) ? -1.0f : 0.0f;
	for (const HostInput &input : m_bindings.of(control)) {
		const JoyState *const joy = joy_of(host, input.joy, c.player);
		const MouseState *const aim = host.mice[c.player & 1];
		float v = 0.0f;

		if (input.kind == HostInput::Kind::PAD && input.pad.type == PadInput::Type::AXIS && pad) {
			v = std::clamp(static_cast<float>(pad->axes[input.pad.index]) / 32767.0f, -1.0f, 1.0f);
			v = input.pad.inverted ? -v : v;
		} else if (const auto at = input.kind == HostInput::Kind::JOY ? joy_channel(joy, input.joy, c) :
		                           input.kind == HostInput::Kind::MOUSE && input.mouse.is_axis() && aim ?
		                           std::optional<uint16_t>(pointed(c.low, input.mouse.type == MouseInput::Type::X ? aim->x : aim->y)) :
		                                                               std::nullopt) {
			/* From its channel (a joystick, where the mouse or the light
			 * gun points): a pedal from 0, the others from -1, the way the
			 * game takes them turned back */
			const bool over = GameControl::turned_over(c.low);
			const float word = static_cast<float>(over ? 0xFFFF - *at : *at) / 65535.0f;

			v = GameControl::half(c.low, false) ? word * 2.0f - 1.0f : word;
		}
		if (std::abs(v) > std::abs(out.value))
			out.value = v;
	}
	return out;
}

/* The keys take an analog control a step a frame toward the end they push
 * to, or back to rest. Of the keys and the axes bound, the last that moved
 * works it: one that went further than MOVED from where it was last kept
 * takes it, the one that went furthest if more than one did. */
uint16_t
ControlPanel::analog(const HostState &host, size_t c)
{
	const GameControl &control = m_controls[c];
	const bool back = pressed(host, m_bindings.toward(c, false), control.player);
	const bool on = pressed(host, m_bindings.toward(c, true), control.player);
	const int target = back == on ? rest(control.low) : end(control.low, on);
	int keyed = m_keyed[c];

	keyed = std::abs(target - keyed) <= KEY_STEP ? target : keyed + (target > keyed ? KEY_STEP : -KEY_STEP);
	m_keyed[c] = static_cast<uint16_t>(keyed);

	const std::vector<HostInput> &axes = m_bindings.of(c);
	const PadState *const pad = host.pads[control.player & 1];
	const MouseState *const aim = host.mice[control.player & 1];
	std::vector<uint16_t> &kept = m_kept[c];
	std::vector<uint16_t> now(axes.size() + 1, 0);
	std::vector<bool> there(axes.size() + 1, false);
	int moved = MOVED;

	now[0] = m_keyed[c];
	there[0] = true;
	for (size_t s = 1; s < now.size(); s++) {
		const HostInput &input = axes[s - 1];

		if (input.kind == HostInput::Kind::MOUSE && aim) {
			now[s] = pointed(control.low, input.mouse.type == MouseInput::Type::X ? aim->x : aim->y);
			there[s] = true;
		} else if (input.kind == HostInput::Kind::PAD && pad) {
			now[s] = channel(input.pad, control.low, pad->axes[input.pad.index]);
			there[s] = true;
		} else if (const auto at = input.kind == HostInput::Kind::JOY ?
		                           joy_channel(joy_of(host, input.joy, control.player), input.joy, control) :
		                           std::nullopt) {
			now[s] = *at;
			there[s] = true;
		}
	}
	/* An input back after it was gone (a pad plugged in again) starts
	 * from where it is; one seen for the first time, from rest, but an
	 * axis of a joystick from where it is: a pedal at rest at the top of
	 * its axis, or a wheel off its centre, takes nothing before it moves */
	for (size_t s = 0; s < now.size(); s++) {
		uint8_t &presence = m_presence[c][s];
		const bool joy = s > 0 && axes[s - 1].kind == HostInput::Kind::JOY;

		if (there[s] && (presence == GONE || (presence == NEVER && joy)))
			kept[s] = now[s];
		presence = there[s] ? THERE : presence == NEVER ? NEVER : GONE;
	}
	if (!there[m_owner[c]])
		m_owner[c] = 0;
	for (size_t s = 0; s < now.size(); s++) {
		const bool mouse = s > 0 && axes[s - 1].kind == HostInput::Kind::MOUSE;

		if (there[s] && mouse && m_mouse_moved[control.player & 1]) {
			m_owner[c] = s;
			break;
		}
		if (there[s] && std::abs(now[s] - kept[s]) > moved) {
			moved = std::abs(now[s] - kept[s]);
			m_owner[c] = s;
		}
	}
	kept[m_owner[c]] = now[m_owner[c]];
	return now[m_owner[c]];
}

void
ControlPanel::update(const HostState &host)
{
	CabinetInputs inputs = idle_inputs(m_controls);

	/* The mouse of a player is one pointer: when it moves, it takes every
	 * channel of the player it is bound to */
	for (unsigned player = 0; player < 2; player++) {
		const MouseState *const mouse = host.mice[player];

		m_mouse_moved[player] = mouse && (!m_mouse_seen[player] ||
		                                  std::abs(mouse->x - m_mouse_x[player]) * 0x10000 > MOVED ||
		                                  std::abs(mouse->y - m_mouse_y[player]) * 0x10000 > MOVED);
		if (m_mouse_moved[player]) {
			m_mouse_x[player] = mouse->x;
			m_mouse_y[player] = mouse->y;
		}
		m_mouse_seen[player] = m_mouse_seen[player] || mouse;
	}

	inputs.mainboard_test = pressed(host, m_bindings.cabinet(Role::BOARD_TEST), 0);
	inputs.mainboard_service = pressed(host, m_bindings.cabinet(Role::BOARD_SERVICE), 0);
	inputs.jvs_system = pressed(host, m_bindings.cabinet(Role::TEST), 0) ? 0x80 : 0;

	for (size_t c = 0; c < m_controls.size(); c++) {
		const GameControl &control = m_controls[c];

		switch (control.type) {
		case GameControl::Type::BUTTON:
			if (pressed(host, m_bindings.of(c), control.player))
				inputs.jvs_player[control.button.player] |= control.button.mask;
			break;
		case GameControl::Type::POSITIONS: {
			const bool down = pressed(host, m_bindings.lever(c, false), control.player);
			const bool up = pressed(host, m_bindings.lever(c, true), control.player);

			if (down && !m_levers_were[c][0] && m_positions[c] > 0)
				m_positions[c]--;
			if (up && !m_levers_were[c][1] && m_positions[c] + 1 < control.positions.size())
				m_positions[c]++;
			m_levers_were[c] = { down, up };
			for (const JvsSwitch &sw : control.positions[m_positions[c]].switches)
				inputs.jvs_player[sw.player] |= sw.mask;
			break;
		}
		case GameControl::Type::ANALOG:
			inputs.jvs_analog[control.analog] = analog(host, c);
			break;
		}
	}
	/* The mouse of a player aims while it works every channel of the
	 * player it is bound to */
	std::array<bool, 2> works_one{}, works_all{ true, true };

	for (size_t c = 0; c < m_controls.size(); c++) {
		const std::vector<HostInput> &axes = m_bindings.of(c);
		const bool bound = std::any_of(axes.begin(), axes.end(), [](const HostInput &input) {
			return input.kind == HostInput::Kind::MOUSE;
		});

		if (m_controls[c].type != GameControl::Type::ANALOG || !bound)
			continue;
		const bool works = m_owner[c] > 0 && axes[m_owner[c] - 1].kind == HostInput::Kind::MOUSE;
		const unsigned player = m_controls[c].player & 1;

		works_one[player] = works_one[player] || works;
		works_all[player] = works_all[player] && works;
	}
	for (unsigned player = 0; player < 2; player++)
		m_mouse_aims[player] = works_one[player] && works_all[player];
	for (unsigned slot = 0; slot < 2; slot++) {
		/* A coin of player 2 from the pad of player 2 */
		const bool now = pressed(host, m_bindings.cabinet(slot ? Role::COIN2 : Role::COIN1), slot);

		m_coins[slot] = m_coins[slot] || (now && !m_coins_were[slot]);
		m_coins_were[slot] = now;
	}
	m_inputs = inputs;
}

void
ControlPanel::fill(CabinetInputs &inputs)
{
	inputs = m_inputs;
	for (unsigned slot = 0; slot < 2; slot++) {
		inputs.jvs_coin[slot] = m_coins[slot];
		m_coins[slot] = false;
	}
}

} /* namespace odin */

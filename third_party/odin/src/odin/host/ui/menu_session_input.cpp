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

#include "odin/host/ui/menu_session.hpp"

#include <algorithm>
#include <cmath>

namespace odin {

/* The page Input waits for an input, whatever device it shows (a light gun
 * clicks, sends keys or is a gamepad; a pedal is a key or a joystick): Escape
 * lets it go; a key, or a button of the mouse (or of a light gun that moves
 * the pointer) clicked where the menu has nothing (a click on a button of it,
 * Skip, Stop, a tab or a row, works that), or on the player's gamepad a
 * button (Guide held, then another: both), a stick or a trigger from half its
 * way, or on a joystick (catch_joystick) a button, a hat or an axis from half
 * its way off where it was as the catch began, or for a sight the way the
 * mouse or the light gun aims, is the one. True when the event was taken. */
bool
MenuSession::catch_input(const SDL_Event &event)
{
	MenuState &state = m_menu_state;

	/* The buttons of the menu follow what is held, as ever */
	m_nav.event(event);

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP: {
		m_keys.set(event.key.key, event.key.down);
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.stop_catching();
			return true;
		}
		/* The keys Odin works itself go to no control of the game; those
		 * of quitting and of the whole display work as in the rest of the
		 * menu */
		if (m_hotkeys.takes(event.key.key)) {
			const auto action = m_hotkeys.key_action(event.key.key, event.key.mod);

			return action != Hotkeys::Action::QUIT && action != Hotkeys::Action::FULLSCREEN;
		}

		HostInput input{};

		input.kind = HostInput::Kind::KEY;
		input.key = event.key.key;
		button_on_analog();
		caught(input);
		return true;
	}
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		m_controls.event(event);
		m_controls.guide_tapped();
		m_controls.guide_combo();
		if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)
			m_catch_guide = false;
		/* The button that presses, let go before it skipped: caught */
		if (m_held && m_held->button == event.gbutton.button) {
			const Held held = *m_held;

			m_held.reset();
			if (!held.skipped)
				catch_pad(false, held.button, 0);
		}
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);
		m_controls.guide_combo();

		const bool axis = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
		const int value = axis ? event.gaxis.value : 0;

		if (axis && std::abs(value) < ControlPanel::PRESSED)
			return true;
		if (!axis && event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
			m_catch_guide = true;
			return true;
		}
		/* Start mapping: the button that presses, held, skips the control
		 * (draw_menu); let go sooner, it is caught then */
		if (!axis && state.mapping && !m_catch_guide && presses(event.gbutton.button)) {
			m_held = Held{ event.gbutton.button, SDL_GetTicks(), false };
			return true;
		}
		catch_pad(axis, axis ? event.gaxis.axis : event.gbutton.button, value);
		return true;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		const int button = mouse_button_of(event.button.button);

		if (button < 0)
			return false;

		/* What is there, on a copy that moves nothing (the moves of the
		 * gamepad wait for the frame drawn) */
		MenuState probe = state;

		probe.nav = {};
		if (m_display.menu_item_at(probe, event.button.x, event.button.y))
			return false;

		HostInput input{};

		input.kind = HostInput::Kind::MOUSE;
		input.mouse = { MouseInput::Type::BUTTON, static_cast<uint8_t>(button) };
		button_on_analog();
		caught(input);
		return true;
	}
	case SDL_EVENT_MOUSE_MOTION:
		return catch_aim(event);
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
	case SDL_EVENT_JOYSTICK_HAT_MOTION:
	case SDL_EVENT_JOYSTICK_ADDED:
	case SDL_EVENT_JOYSTICK_REMOVED: {
		/* Where the axes were as the catch (or Map all) began, none
		 * if not known yet, and where this one was before this move;
		 * the axes caught before and let go since, free again */
		if (!m_catch_from) {
			m_catch_from.emplace();
			for (const Controls::Plugged &one : m_controls.joysticks())
				if (const JoyState *const state_of = m_controls.joystick_state(one.id))
					for (size_t a = 0; a < state_of->axes.size(); a++)
						(*m_catch_from)[one.id].push_back(state_of->known[a] ? std::optional(state_of->axes[a]) :
						                                                       std::nullopt);
			std::erase_if(m_catch_held, [this](const auto &held) {
				const auto [id, axis] = held.first;
				const JoyState *const state_of = m_controls.joystick_state(id);

				return !state_of || axis >= state_of->axes.size() ||
				       std::abs(state_of->axes[axis] - held.second) < ControlPanel::PRESSED;
			});
		}

		const JoyState *const joy = m_controls.joystick_state(event.jaxis.which);
		const size_t axis = event.jaxis.axis;
		const int was = event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION && joy && axis < joy->axes.size() ? joy->axes[axis] : 0;

		m_controls.event(event);
		catch_joystick(event, was);
		return true;
	}
	default:
		return false;
	}
}

/* A button on an analog control goes on its half: of a pedal, or of each
 * way in turn */
void
MenuSession::button_on_analog()
{
	MenuState &state = m_menu_state;

	if (state.catching->control < 0 || state.catching->part != 0)
		return;

	const auto c = static_cast<size_t>(state.catching->control);
	const GameControl &control = panel_shown().controls()[c];

	if (control.type == GameControl::Type::ANALOG) {
		const bool two_ways = GameControl::half(control.low, false);
		const MenuSlot other_way = { state.catching->control, 2, state.catching->role };

		state.catching->part = two_ways ? 1 : 2;
		if (two_ways)
			state.catch_next.insert(state.catch_next.begin(), other_way);
	}
}

/* An input of a joystick caught: a button pressed, a way of a hat, an axis
 * moving away from where it was as the catch began, from half its way (one
 * coming back, a pedal let go after it was caught, is none): whole on an
 * analog control, turned over for a pedal pressed toward the bottom of its
 * axis (a G29), else its half. Another joystick than the player's (a shifter,
 * pedals of their own) is named by its GUID; that of the other player of a
 * game of two is theirs. */
void
MenuSession::catch_joystick(const SDL_Event &event, int was)
{
	MenuState &state = m_menu_state;
	const SDL_JoystickID which = event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION ? event.jaxis.which :
	                             event.type == SDL_EVENT_JOYSTICK_HAT_MOTION  ? event.jhat.which :
	                                                                            event.jbutton.which;
	const SDL_JoystickID mine = joystick_shown(state.player);
	const JoyState *const joy = m_controls.joystick_state(which);
	HostInput input{};
	JoyInput &in = input.joy;

	if (!state.catching || !joy)
		return;
	if (which != mine && panel_shown().two_players() && which == joystick_shown((state.player & 1) ^ 1))
		return;
	input.kind = HostInput::Kind::JOY;
	if (which != mine)
		in.device = m_controls.joystick_guid(which);
	switch (event.type) {
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		in.type = JoyInput::Type::BUTTON;
		in.index = event.jbutton.button;
		button_on_analog();
		break;
	case SDL_EVENT_JOYSTICK_HAT_MOTION: {
		const uint8_t value = event.jhat.value;

		if (!value)
			return;
		in.type = JoyInput::Type::HAT;
		in.index = event.jhat.hat;
		in.way = static_cast<uint8_t>(value & -value);
		button_on_analog();
		break;
	}
	case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
		const size_t axis = event.jaxis.axis;

		if (axis >= joy->rest.size() || !joy->known[axis])
			return;

		/* From where it was as the catch began, else from its rest,
		 * set by its first value (unknown then, or plugged since) */
		int from = joy->rest[axis];
		const auto start = m_catch_from->find(which);

		if (start != m_catch_from->end() && axis < start->second.size() && start->second[axis])
			from = *start->second[axis];

		/* An axis just caught, as Map all goes on at once or a click
		 * starts another catch: none until back near where it was */
		if (const auto held = m_catch_held.find({ which, axis }); held != m_catch_held.end()) {
			if (std::abs(event.jaxis.value - held->second) >= ControlPanel::PRESSED)
				return;
			m_catch_held.erase(held);
		}

		const int moved = event.jaxis.value - from;

		if (std::abs(moved) < ControlPanel::PRESSED || std::abs(moved) <= std::abs(was - from))
			return;
		m_catch_held[{ which, axis }] = from;
		in.index = event.jaxis.axis;
		in.type = moved < 0 ? JoyInput::Type::AXIS_NEGATIVE : JoyInput::Type::AXIS_POSITIVE;
		const GameControl *const control =
			state.catching->control >= 0 ? &panel_shown().controls()[static_cast<size_t>(state.catching->control)] :
			                               nullptr;

		if (control && control->type == GameControl::Type::ANALOG && state.catching->part == 0) {
			in.type = JoyInput::Type::AXIS;
			in.inverted = !GameControl::half(control->low, false) && moved < 0;
		}
		break;
	}
	default:
		return;
	}
	caught(input);
}

/* A sight caught whole on the keyboard or a light gun: the way the mouse
 * or the gun then goes far enough (a fifth of the window), across or up
 * and down, is its axis. True when the event was taken. */
bool
MenuSession::catch_aim(const SDL_Event &event)
{
	static constexpr float AIM_MOVED = 0.2f;
	const MenuState &state = m_menu_state;
	const MenuSlot slot = *state.catching;

	if (slot.control < 0 || slot.part != 0 || state.on_pad() || state.on_joystick())
		return false;

	const Role role = panel_shown().controls()[static_cast<size_t>(slot.control)].role;

	if (role != Role::GUN_X && role != Role::GUN_Y)
		return false;
	if (!m_aim_from || m_aim_from->slot.control != slot.control || m_aim_from->slot.part != slot.part) {
		m_aim_from = AimFrom{ slot, event.motion.x, event.motion.y };
		return false;
	}

	int w = 0, h = 0;

	SDL_GetWindowSize(m_display.window(), &w, &h);

	const float across = std::abs(event.motion.x - m_aim_from->x) / static_cast<float>(std::max(w, 1));
	const float down = std::abs(event.motion.y - m_aim_from->y) / static_cast<float>(std::max(h, 1));

	if (std::max(across, down) < AIM_MOVED)
		return false;

	HostInput input{};

	input.kind = HostInput::Kind::MOUSE;
	input.mouse = { across >= down ? MouseInput::Type::X : MouseInput::Type::Y, 0 };
	m_aim_from.reset();
	caught(input);
	return true;
}

/* Whether a button of a gamepad presses in the menu (menu_nav.hpp) */
bool
MenuSession::presses(int button) const
{
	const std::vector<HostInput> &inputs = m_nav.inputs(MenuNav::PRESS);

	return std::any_of(inputs.begin(), inputs.end(), [button](const HostInput &in) {
		return in.kind == HostInput::Kind::PAD && in.pad.type == PadInput::Type::BUTTON &&
		       in.pad.index == button && in.pad.with < 0;
	});
}

/* An input of a gamepad caught: a button (held with Guide when it is), or
 * an axis past half its way */
void
MenuSession::catch_pad(bool axis, int index, int value)
{
	MenuState &state = m_menu_state;
	HostInput input{};

	if (!state.catching)
		return;
	input.kind = HostInput::Kind::PAD;
	input.pad.index = static_cast<uint8_t>(index);
	input.pad.type = !axis ? PadInput::Type::BUTTON : PadInput::Type::AXIS;
	if (!axis && m_catch_guide)
		input.pad.with = SDL_GAMEPAD_BUTTON_GUIDE;
	/* A stick on a part that is pressed: its half */
	if (axis && index < SDL_GAMEPAD_AXIS_LEFT_TRIGGER && state.catching->part != 0)
		input.pad.type = value < 0 ? PadInput::Type::AXIS_NEGATIVE : PadInput::Type::AXIS_POSITIVE;
	if (!axis)
		button_on_analog();
	caught(input);
}

/* The page Hotkeys waits for a key (Ctrl, Alt, Shift or Gui held with it
 * are its own) or for a button pressed with the hotkey button held, or
 * Guide tapped alone when it is that button; Delete, Backspace or the Clear
 * of the gamepad (X) empties the place; Escape tapped lets it be, held
 * ESCAPE_HOLD_MS it is the key. True when the event was taken. */
bool
MenuSession::catch_hotkey(const SDL_Event &event)
{
	MenuState &state = m_menu_state;
	const auto action = static_cast<Hotkeys::Action>(state.hotkey_catching->first);
	const size_t slot = state.hotkey_catching->second;
	const std::vector<MenuNav::Action> nav = m_nav.event(event);

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		m_keys.set(event.key.key, event.key.down);
		if (event.key.key == SDLK_ESCAPE && slot < Hotkeys::KEYS) {
			/* Held long enough: seen as the key repeats, else as
			 * it is let go; one down as another place waited (a
			 * click ended that catch) is let be */
			if (m_escape_down && m_escape_down->place != *state.hotkey_catching)
				m_escape_down.reset();

			const bool held = m_escape_down && SDL_GetTicks() - m_escape_down->since >= ESCAPE_HOLD_MS;

			if (event.key.down && !event.key.repeat)
				m_escape_down = { SDL_GetTicks(), event.key.mod, *state.hotkey_catching };
			else if (held)
				set_hotkey_key(action, slot, { SDLK_ESCAPE, Hotkeys::modifiers(m_escape_down->mods) });
			else if (m_escape_down && !event.key.down)
				state.hotkey_catching.reset();
			if (!state.hotkey_catching)
				m_escape_down.reset();
			return true;
		}
		if (!event.key.down || event.key.repeat)
			return true;
		if (event.key.key == SDLK_ESCAPE) {
			state.hotkey_catching.reset();
			return true;
		}
		if (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE) {
			clear_hotkey(action, slot);
			return true;
		}
		if (slot < Hotkeys::KEYS && !Hotkeys::is_modifier(event.key.key))
			set_hotkey_key(action, slot, { event.key.key, Hotkeys::modifiers(event.key.mod) });
		return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
		m_controls.event(event);

		const int combo = m_controls.guide_combo();
		const bool tapped = m_controls.guide_tapped();

		if (slot == Hotkeys::KEYS && (combo >= 0 || tapped)) {
			const int pad = combo >= 0 ? combo : Hotkeys::GUIDE_ALONE;

			state.hotkey_moved.reset();
			if (const auto from = m_hotkeys.set_pad(action, pad))
				state.hotkey_moved = { static_cast<size_t>(*from), static_cast<size_t>(action), std::nullopt, pad };
			state.hotkey_catching.reset();
			keep_hotkeys();
		} else if (std::find(nav.begin(), nav.end(), MenuNav::BACK) != nav.end()) {
			/* B alone is no shortcut: it lets the catch go */
			state.hotkey_catching.reset();
		} else if (std::find(nav.begin(), nav.end(), MenuNav::CLEAR) != nav.end()) {
			clear_hotkey(action, slot);
		}
		if (!state.hotkey_catching)
			m_escape_down.reset();
		return true;
	}
	default:
		return false;
	}
}

/* The key caught for a place of an action; the action that had it told,
 * with the key as the action took it (TEST and SERVICE leave the
 * modifiers) */
void
MenuSession::set_hotkey_key(Hotkeys::Action action, size_t slot, const HotKey &key)
{
	MenuState &state = m_menu_state;

	state.hotkey_moved.reset();
	if (const auto from = m_hotkeys.set_key(action, slot, key))
		state.hotkey_moved = { static_cast<size_t>(*from), static_cast<size_t>(action),
		                       m_hotkeys.of(action).keys[slot], Hotkeys::NO_PAD };
	state.hotkey_catching.reset();
	m_escape_down.reset();
	keep_hotkeys();
}

/* The key (slot 0 or 1) or the gamepad (slot KEYS) of an action emptied */
void
MenuSession::clear_hotkey(Hotkeys::Action action, size_t slot)
{
	MenuState &state = m_menu_state;

	if (slot < Hotkeys::KEYS)
		m_hotkeys.set_key(action, slot, {});
	else
		m_hotkeys.set_pad(action, Hotkeys::NO_PAD);
	state.hotkey_moved.reset();
	state.hotkey_catching.reset();
	m_escape_down.reset();
	keep_hotkeys();
}

/* [hotkeys]: the line of the hotkey button and one for each action not as
 * by default with it, the others back to their comment by default with it;
 * the menu follows the button, the panels are made again (they take TEST
 * and SERVICE from there) */
void
MenuSession::keep_hotkeys()
{
	const int button = m_hotkeys.button();

	m_settings.know(known_settings(m_games, button));
	keep("hotkeys", "button",
	     button == SDL_GAMEPAD_BUTTON_GUIDE ? std::nullopt : std::optional(Hotkeys::button_text(button)));
	for (size_t a = 0; a < Hotkeys::ACTIONS; a++) {
		const auto action = static_cast<Hotkeys::Action>(a);
		const Hotkeys::Binding &binding = m_hotkeys.of(action);
		const std::optional<std::string> line = binding == Hotkeys::standard(action, button) ? std::nullopt :
		                                        std::optional(Hotkeys::line(binding));

		keep("hotkeys", Hotkeys::id(action), line);
	}
	m_nav.hotkey(button, m_hotkeys.combos());
	rebind_panels();
}

/* The input caught takes the place of those of its kind on that part (of
 * a joystick, those of the same joystick); the next part waiting is caught
 * then */
void
MenuSession::caught(const HostInput &input)
{
	const MenuSlot slot = *m_menu_state.catching;
	const Bindings &bindings = panel_shown().bindings();
	const size_t c = static_cast<size_t>(slot.control);
	std::vector<HostInput> inputs = slot.control < 0 ? bindings.cabinet(slot.role) : bindings.part(c, slot.part);

	m_menu_state.catching.reset();
	std::erase_if(inputs, [&input](const HostInput &in) {
		return in.kind == input.kind && (in.kind != HostInput::Kind::JOY || in.joy.device == input.joy.device);
	});
	inputs.push_back(input);
	keep_binding(slot.control < 0 ? bindings.cabinet_line(slot.role, inputs) : bindings.line(c, slot.part, inputs));
	if (!m_menu_state.catch_next.empty()) {
		m_menu_state.catching = m_menu_state.catch_next.front();
		m_menu_state.catch_next.erase(m_menu_state.catch_next.begin());
	}
}

/* Defaults and Clear mapping: all the inputs, of every device, of every
 * part of the controls of the player and their coin (and TEST for player
 * 1), back to their defaults or none */
void
MenuSession::change_inputs(bool to_defaults)
{
	static constexpr HostInput::Kind ALL[] = { HostInput::Kind::KEY, HostInput::Kind::PAD, HostInput::Kind::MOUSE,
		                                    HostInput::Kind::JOY };
	const MenuState &state = m_menu_state;
	const auto changed = [&](const std::vector<HostInput> &inputs, const std::vector<HostInput> &standard) {
		return Bindings::with_kinds(inputs, standard, ALL, to_defaults);
	};
	std::vector<Bindings::Line> lines;
	{
		const Bindings &bindings = panel_shown().bindings();
		const std::vector<GameControl> &controls = panel_shown().controls();

		for (size_t c = 0; c < controls.size(); c++) {
			if (controls[c].player != state.player || controls[c].role == Role::GUN_OFFSCREEN)
				continue;
			const bool parts = controls[c].type != GameControl::Type::BUTTON;

			for (unsigned part = parts ? 1 : 0; part <= (parts ? 2u : 0u); part++)
				lines.push_back(bindings.line(c, part, changed(bindings.part(c, part), bindings.standard(c, part))));
			if (controls[c].type == GameControl::Type::ANALOG && GameControl::half(controls[c].low, false))
				lines.push_back(bindings.line(c, 0, changed(bindings.part(c, 0), bindings.standard(c, 0))));
		}
		for (const Role role : { state.player ? Role::COIN2 : Role::COIN1, Role::TEST }) {
			if (role == Role::TEST && state.player)
				continue;
			lines.push_back(bindings.cabinet_line(role, changed(bindings.cabinet(role), bindings.cabinet_standard(role))));
		}
	}
	for (const Bindings::Line &line : lines)
		keep_binding(line);

	/* The whole axis of a pedal, on the line of its button kept just now */
	const std::vector<GameControl> &controls = panel_shown().controls();

	for (size_t c = 0; c < controls.size(); c++)
		if (controls[c].player == state.player && controls[c].type == GameControl::Type::ANALOG &&
		    !GameControl::half(controls[c].low, false)) {
			const Bindings &bindings = panel_shown().bindings();

			keep_binding(bindings.line(c, 0, changed(bindings.part(c, 0), bindings.standard(c, 0))));
		}
}

/* X and Y of a gamepad on the part picked: its inputs of the device shown
 * cleared, or back to their defaults */
void
MenuSession::change_part(const MenuSlot &slot, bool to_defaults)
{
	const Bindings &bindings = panel_shown().bindings();

	if (slot.control < 0) {
		keep_binding(bindings.cabinet_line(slot.role, Bindings::with_kinds(bindings.cabinet(slot.role),
		                                                                   bindings.cabinet_standard(slot.role),
		                                                                   m_menu_state.shown_kinds(), to_defaults)));
		return;
	}

	const size_t c = static_cast<size_t>(slot.control);

	keep_binding(bindings.line(c, slot.part, Bindings::with_kinds(bindings.part(c, slot.part), bindings.standard(c, slot.part),
	                                                               m_menu_state.shown_kinds(), to_defaults)));
}

/* The inputs back as Clear mapping found them */
void
MenuSession::undo()
{
	const std::string section = "controls." + m_cleared_game;
	std::vector<std::string> keys;

	for (const auto &[key, value] : m_settings.section(section))
		keys.push_back(key);
	for (const auto &[key, value] : m_cleared)
		keys.push_back(key);
	for (const std::string &key : keys) {
		std::optional<std::string> was;

		for (const auto &[k, value] : m_cleared)
			if (k == key)
				was = value;
		keep_binding({ key, was }, m_cleared_game);
	}
	m_undo_until = 0;
}

/* The panels of the game that runs and of the one shown made again from
 * the settings */
void
MenuSession::rebind_panels()
{
	m_controls.rebind();
	if (m_other) {
		m_other.emplace(m_other_controls, m_settings, game_shown());
		m_other->set_wheel_rotation(wheel_rotation_of(m_settings, game_shown()));
	}
}

void
MenuSession::keep_binding(const Bindings::Line &line, const std::string &game)
{
	const std::string section = "controls." + (game.empty() ? game_shown() : game);

	keep(section, line.key, line.value);
	rebind_panels();
}

} /* namespace odin */

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

#ifndef ODIN_SEATS_HPP
#define ODIN_SEATS_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>

namespace odin {

/* The device a line of [players.<game>] names (p1, joy1, gun1...): the
 * text before its first space, a name of it after */
inline std::string
identity_of(const std::string &value)
{
	return value.substr(0, value.find(' '));
}

/* A text made fit to be such an identity: its spaces as '_', since the
 * identity ends at the first space */
inline std::string
identity_text(std::string text)
{
	std::replace(text.begin(), text.end(), ' ', '_');
	return text;
}

/* A gamepad open: its id, the GUID of its make and model, and its path as
 * SDL tells it (empty: none), which tells two of a make apart */
struct OpenPad {
	SDL_JoystickID id;
	std::string guid;
	std::string path = {};
};

/* A path of [players.<game>] (p1_path...) as SDL tells that of a device: a
 * link (Linux: /dev/input/by-path/..., by-id/...) the node it points to */
inline std::string
device_path(const std::string &path)
{
	std::error_code error;

	if (path.empty() || path.front() != '/')
		return path;

	const std::filesystem::path node = std::filesystem::canonical(path, error);

	return error ? path : node.string();
}

/* Whether a path asked is that of a device, in any case (Windows) */
inline bool
same_path(const std::string &asked, const std::string &path)
{
	return !asked.empty() && SDL_strcasecmp(asked.c_str(), path.c_str()) == 0;
}

/* The player a device open is seated for, 0 or 1; -1 for none */
inline int
player_seated(SDL_JoystickID id, const std::array<SDL_JoystickID, 2> &seats)
{
	return id == seats[0] ? 0 : id == seats[1] ? 1 : -1;
}

/* Whether a joystick of that kind (SDL_JoystickType) may seat a player: a
 * wheel only in a game that steers with one */
inline bool
joystick_fits(int kind, bool steers)
{
	return steers || kind != SDL_JOYSTICK_TYPE_WHEEL;
}

/* In a game steered by a wheel, the wheels first (kinds: SDL_JoystickType
 * of each): a shifter or pedals of their own plugged in before it take no
 * player's place from it */
inline void
wheels_first(std::vector<OpenPad> &joys, const std::vector<int> &kinds)
{
	std::vector<OpenPad> wheels, others;

	for (size_t i = 0; i < joys.size() && i < kinds.size(); i++)
		(kinds[i] == SDL_JOYSTICK_TYPE_WHEEL ? wheels : others).push_back(joys[i]);
	wheels.insert(wheels.end(), others.begin(), others.end());
	joys = std::move(wheels);
}

/* The gamepad of each player (0: none), from those open in the order they
 * were plugged in, the GUID each player wants (empty: any), the very pad
 * each one picked (0: none; two of a make share their GUID), the ones they
 * had and the path each one names (empty: none): the one picked if it is
 * open, else the one of that path of the kind they want, else the one they
 * want if they have it, else the first free one of that kind, else the one
 * they had (a player keeps their pad when another goes), else the first
 * free one. */
inline std::array<SDL_JoystickID, 2>
seat_pads(const std::vector<OpenPad> &pads, const std::array<std::string, 2> &wanted,
          const std::array<SDL_JoystickID, 2> &picked, const std::array<SDL_JoystickID, 2> &had,
          const std::array<std::string, 2> &paths = {})
{
	std::array<SDL_JoystickID, 2> out{};
	std::vector<bool> taken(pads.size());
	const auto take = [&](unsigned player, const auto &fits) {
		for (size_t i = 0; i < pads.size() && !out[player]; i++) {
			if (!taken[i] && fits(pads[i])) {
				out[player] = pads[i].id;
				taken[i] = true;
			}
		}
	};

	for (unsigned player = 0; player < 2; player++)
		if (picked[player])
			take(player, [&](const OpenPad &pad) { return pad.id == picked[player]; });
	for (unsigned player = 0; player < 2; player++)
		take(player, [&](const OpenPad &pad) {
			return same_path(paths[player], pad.path) && (wanted[player].empty() || pad.guid == wanted[player]);
		});
	for (unsigned player = 0; player < 2; player++)
		if (!wanted[player].empty())
			take(player, [&](const OpenPad &pad) { return pad.id == had[player] && pad.guid == wanted[player]; });
	for (unsigned player = 0; player < 2; player++)
		if (!wanted[player].empty())
			take(player, [&](const OpenPad &pad) { return pad.guid == wanted[player]; });
	for (unsigned player = 0; player < 2; player++)
		take(player, [&](const OpenPad &pad) { return pad.id == had[player]; });
	for (unsigned player = 0; player < 2; player++)
		take(player, [](const OpenPad &) { return true; });
	return out;
}

/* The kind of device a player plays with, the only one that shakes, and
 * turns if it is a wheel: the one they chose on the page Input or in the
 * bar ([players.<game>] device1, device2, DEVICE_NAMES) */
enum class InputDevice : uint8_t { KEYBOARD, PAD, JOYSTICK };
inline constexpr const char *DEVICE_NAMES[] = { "keyboard", "pad", "joystick" };

/* The device of a player: the one chosen if they have it, else their
 * joystick in a game with a steering wheel, else their gamepad, else their
 * joystick, else the keyboard */
inline InputDevice
device_of(std::optional<InputDevice> chosen, bool steers, bool pad, bool joystick)
{
	if (chosen && (*chosen == InputDevice::KEYBOARD || (*chosen == InputDevice::PAD ? pad : joystick)))
		return *chosen;
	return joystick && (steers || !pad) ? InputDevice::JOYSTICK : pad ? InputDevice::PAD : InputDevice::KEYBOARD;
}

} /* namespace odin */

#endif /* ODIN_SEATS_HPP */

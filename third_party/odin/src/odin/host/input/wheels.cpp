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

#include "odin/host/input/wheels.hpp"

#include <string>

namespace odin {

namespace {

/* The wheels Batocera's EmulationStation knows (its controllers/es_input.cfg:
 * r2, l2, pageup, pagedown, start and select of each), in SDL's numbers:
 * the gas, the brake, the left and the right paddle, Start, Select. Which
 * way a pedal goes is not taken from here (ControlPanel::pedal), as it
 * changes with the driver; ":invert" only picks the half of an axis the
 * pedals share (Windows gives one to those of the older Logitech wheels).
 * The G923 Xbox, as the G920, has ":invert", which es_input.cfg lacks */
struct KnownWheel {
	uint16_t vendor, product;
	const char *parts[WHEEL_PARTS];
};

const KnownWheel WHEELS[] = {
	{ 0x046d, 0xc24f, { "axis2:invert", "axis3:invert", "button5", "button4", "button8", "button9" } },	/* G29 */
	{ 0x046d, 0xc266, { "axis2:invert", "axis3:invert", "button5", "button4", "button8", "button9" } },	/* G923 PS4 */
	{ 0x046d, 0xc26e, { "axis1:invert", "axis2:invert", "button5", "button4", "button6", "button7" } },	/* G923 Xbox */
	{ 0x046d, 0xc262, { "axis1:invert", "axis2:invert", "button5", "button4", "button6", "button7" } },	/* G920 */
	{ 0x046d, 0xc29b, { "axis2:invert", "axis3:invert", "button5", "button4", "button6", "button7" } },	/* G27 */
	{ 0x046d, 0xc299, { "axis2:invert", "axis3:invert", "button5", "button4", "button11", "button8" } },	/* G25 */
	{ 0x046d, 0xc29a, { "axis1:invert", "axis2:invert", "button5", "button4", "button9", "button8" } },	/* Driving Force GT */
	{ 0x046d, 0xc298, { "axis1:invert", "axis2:invert", "button5", "button4", "button9", "button8" } },	/* Driving Force Pro */
	{ 0x046d, 0xc294, { "axis1:invert", "axis2:invert", "button11", "button10", "button9", "button8" } },	/* Driving Force */
	{ 0x046d, 0xca03, { "axis1:invert", "axis2:invert", "button8", "button9", "button3", "button2" } },	/* MOMO Racing */
	{ 0x046d, 0xc272, { "axis2", "axis3", "button5", "button4", "button9", "button8" } },	/* PRO Racing Wheel */
	{ 0x044f, 0xb65d, { "button6", "button7", "button4", "button5", "button12", "button8" } },	/* Thrustmaster FFB Wheel */
	{ 0x044f, 0xb696, { "axis5:invert", "axis1:invert", "button0", "button1", "button7", "button6" } },	/* Thrustmaster Advance Racer */
	{ 0x044f, 0xb66e, { "axis2:invert", "axis1:invert", "button0", "button1", "button7", "button6" } },	/* Thrustmaster T300RS */
	{ 0x044f, 0xb677, { "axis2:invert", "axis1:invert", "button0", "button1", "button7", "button6" } },	/* Thrustmaster T150RS */
	{ 0x346e, 0x0006, { "axis2", "axis5", "button12", "button13", "button34", "button21" } },	/* MOZA R12 */
};

} /* namespace */

WheelParts
wheel_parts(uint16_t vendor, uint16_t product)
{
	WheelParts out{};

	for (const KnownWheel &wheel : WHEELS) {
		if (wheel.vendor != vendor || wheel.product != product)
			continue;
		for (size_t p = 0; p < WHEEL_PARTS; p++)
			if (const auto input = HostInput::parse(std::string("joy:") + wheel.parts[p]))
				out[p] = input->joy;
		break;
	}
	return out;
}

} /* namespace odin */

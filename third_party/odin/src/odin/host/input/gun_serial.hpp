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

#ifndef ODIN_GUN_SERIAL_HPP
#define ODIN_GUN_SERIAL_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace odin {

/* The player of an XGunner, 1 to 4, the PID of its receiver: by its
 * identity ("vidpid:1209:0003"), else by the number in its name
 * ("XGUNNER-P3", Batocera's "XGunner P3"); none for another gun */
std::optional<unsigned> xgunner_player(std::string_view identity, std::string_view name);

/* The serial port of a light gun that the game shakes. The receiver of an
 * XGunner (VID 1209, PID 0001 to 0004 for its players 1 to 4) is a USB
 * serial port, 9600 bauds 8N1: "S6" hands its motor and its solenoid to
 * the game, "F1" runs the motor once (as long as the gun is set to, some
 * 0.4 s; no other F1 is taken until then), "F0" kicks the solenoid once,
 * "E" gives them back to the gun (the X-Gunner documentation, COM Serial
 * Commands). */
class GunSerial {
public:
	/* The port of the XGunner of that identity ("vidpid:1209:0003") or
	 * name ("XGunner P3", Batocera's), in the hands of the game; nullptr,
	 * said on stderr, when there is none or it cannot be opened */
	static std::unique_ptr<GunSerial> open(std::string_view identity, std::string_view name);
	/* Whether such a gun takes the game's orders */
	static bool shakes(std::string_view identity, std::string_view name);
	/* The motor and the solenoid given back */
	~GunSerial();
	GunSerial(const GunSerial &) = delete;
	GunSerial &operator=(const GunSerial &) = delete;

	/* The motor, once; the solenoid, once */
	void rumble() { send("F1"); }
	void recoil() { send("F0"); }

private:
	explicit GunSerial(intptr_t handle) : m_handle(handle) {}
	void send(const char *command);

	intptr_t m_handle;
};

} /* namespace odin */

#endif /* ODIN_GUN_SERIAL_HPP */

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

#ifndef ODIN_WHEEL_FORCE_HPP
#define ODIN_WHEEL_FORCE_HPP

#include <memory>

#include <SDL3/SDL.h>

namespace odin {

/* The force a wheel of the host turns with, as the drive board of the
 * cabinet drives its motor: one constant force, updated in place and sent
 * only when it changes. On Linux through the wheel's own event node,
 * opened again to write to it (SDL gives no effect to a wheel it takes
 * for a gamepad); elsewhere through SDL's haptic. While it holds the
 * wheel, the device's gain is at its highest (the force is scaled here)
 * and its own centring spring is off, as SDL leaves them: the drive board
 * centres the wheel. */
class WheelForce {
public:
	explicit WheelForce(SDL_Joystick *joystick);
	~WheelForce();
	WheelForce(const WheelForce &) = delete;
	WheelForce &operator=(const WheelForce &) = delete;

	/* -1 (all the way to the left) to 1 (all the way to the right) */
	void push(double force);
	/* No force: the emulation stops, the menu opens */
	void release() { push(0.0); }

	/* The levels each way, as the devices take them */
	static constexpr int MOST = 32767;

private:
	struct Backend;

	std::unique_ptr<Backend> m_backend;
	int m_level = 0;	/* the last level sent, right positive */
};

} /* namespace odin */

#endif /* ODIN_WHEEL_FORCE_HPP */

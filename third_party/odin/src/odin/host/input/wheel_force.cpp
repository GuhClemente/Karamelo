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

#include "odin/host/input/wheel_force.hpp"

#include "odin/common/repeats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#ifdef __linux__
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#include "odin/host/input/evdev_bits.hpp"
#include <unistd.h>
#endif

namespace odin {

namespace {

#ifdef __linux__
bool
write_ff(int fd, uint16_t code, int32_t value)
{
	input_event event{};

	event.type = EV_FF;
	event.code = code;
	event.value = value;
	return ::write(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
}

/* Along the axis of the wheel, 0x4000, which input.h calls left: the
 * drivers take the level times the sine of the direction, a positive
 * level pulls to the left. Played until stopped; id -1 uploads it, its id
 * changes it in place. */
ff_effect
constant(int id, int left)
{
	ff_effect effect{};

	effect.type = FF_CONSTANT;
	effect.id = static_cast<int16_t>(id);
	effect.direction = 0x4000;
	effect.u.constant.level = static_cast<int16_t>(left);
	return effect;
}
#endif

/* The same on SDL's steering axis: a positive level to the left, as SDL
 * gives it to the drivers on Linux. HEURISTIC elsewhere: no wheel with
 * force feedback has been tried on Windows yet. */
SDL_HapticEffect
sdl_constant(int left)
{
	SDL_HapticEffect effect{};

	effect.type = SDL_HAPTIC_CONSTANT;
	effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
	effect.constant.length = SDL_HAPTIC_INFINITY;
	effect.constant.level = static_cast<Sint16>(left);
	return effect;
}

} /* namespace */

struct WheelForce::Backend {
	const char *name = nullptr;
#ifdef __linux__
	int fd = -1;
#endif
	SDL_Haptic *haptic = nullptr;
	int effect = -1;
};

WheelForce::WheelForce(SDL_Joystick *joystick) : m_backend(std::make_unique<Backend>())
{
	Backend &b = *m_backend;
	const char *const wheel = SDL_GetJoystickName(joystick);

#ifdef __linux__
	/* The event node SDL reads the wheel from; HIDAPI and virtual
	 * joysticks have none */
	if (const char *const path = SDL_GetJoystickPath(joystick);
	    path && std::string_view(path).starts_with("/dev/input/")) {
		unsigned long bits[longs_for(FF_MAX)] = {};
		const int fd = ::open(path, O_RDWR | O_CLOEXEC);
		ff_effect effect = constant(-1, 0);

		if (fd < 0)
			std::fprintf(stderr, "ERROR: wheel force: %s: %s\n", path, std::strerror(errno));
		else if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(bits)), bits) < 0 || !has_bit(bits, FF_CONSTANT) ||
		         ioctl(fd, EVIOCSFF, &effect) < 0)
			::close(fd);
		else {
			b.fd = fd;
			b.effect = effect.id;
			b.name = "evdev";
			if (has_bit(bits, FF_GAIN))
				write_ff(fd, FF_GAIN, 0xFFFF);
			if (has_bit(bits, FF_AUTOCENTER))
				write_ff(fd, FF_AUTOCENTER, 0);
			write_ff(fd, static_cast<uint16_t>(b.effect), 1);
		}
	}
#endif
	if (!b.name && SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
		b.haptic = SDL_OpenHapticFromJoystick(joystick);
		if (b.haptic && (SDL_GetHapticFeatures(b.haptic) & SDL_HAPTIC_CONSTANT)) {
			const SDL_HapticEffect effect = sdl_constant(0);

			b.effect = SDL_CreateHapticEffect(b.haptic, &effect);
		}
		if (b.effect >= 0 && SDL_RunHapticEffect(b.haptic, b.effect, 1)) {
			b.name = "SDL haptic";
		} else {
			if (b.haptic)
				SDL_CloseHaptic(b.haptic);
			b.haptic = nullptr;
			b.effect = -1;
			SDL_QuitSubSystem(SDL_INIT_HAPTIC);
		}
	}
	std::printf("wheel force: '%s' %s%s\n", wheel ? wheel : "", b.name ? "by " : "has none",
	            b.name ? b.name : "");
}

WheelForce::~WheelForce()
{
	Backend &b = *m_backend;

#ifdef __linux__
	if (b.fd >= 0) {
		write_ff(b.fd, static_cast<uint16_t>(b.effect), 0);
		ioctl(b.fd, EVIOCRMFF, b.effect);
		::close(b.fd);
	}
#endif
	if (b.haptic) {
		SDL_DestroyHapticEffect(b.haptic, b.effect);
		SDL_CloseHaptic(b.haptic);
		SDL_QuitSubSystem(SDL_INIT_HAPTIC);
	}
}

void
WheelForce::push(double force)
{
	Backend &b = *m_backend;
	const int level = static_cast<int>(std::lround(std::clamp(force, -1.0, 1.0) * MOST));

	if (!b.name || level == m_level)
		return;
	m_level = level;
#ifdef __linux__
	if (b.fd >= 0) {
		ff_effect effect = constant(b.effect, -level);

		if (ioctl(b.fd, EVIOCSFF, &effect) < 0)
			repeats::print(stderr, "ERROR: wheel force: %s\n", std::strerror(errno));
		return;
	}
#endif
	SDL_HapticEffect effect = sdl_constant(-level);

	if (!SDL_UpdateHapticEffect(b.haptic, b.effect, &effect))
		repeats::print(stderr, "ERROR: wheel force: %s\n", SDL_GetError());
}

} /* namespace odin */

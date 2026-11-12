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

#ifndef ODIN_EVDEV_BITS_HPP
#define ODIN_EVDEV_BITS_HPP

#include <cstddef>
#include <span>

namespace odin {

/* The bits the ioctls of evdev fill (EVIOCGBIT, EVIOCGKEY): unsigned longs,
 * bit n in the long n / LONG_BITS; as many longs as the bits 0 to `most`
 * take (most: KEY_MAX, ABS_MAX...) */
inline constexpr size_t LONG_BITS = 8 * sizeof(unsigned long);

constexpr size_t
longs_for(size_t most)
{
	return (most + LONG_BITS) / LONG_BITS;
}

inline bool
has_bit(std::span<const unsigned long> bits, unsigned bit)
{
	return (bits[bit / LONG_BITS] >> (bit % LONG_BITS)) & 1;
}

} /* namespace odin */

#endif /* ODIN_EVDEV_BITS_HPP */

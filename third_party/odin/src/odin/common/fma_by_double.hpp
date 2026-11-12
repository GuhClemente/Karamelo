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

#ifndef ODIN_FMA_BY_DOUBLE_HPP
#define ODIN_FMA_BY_DOUBLE_HPP

#include <bit>
#include <cmath>
#include <cstdint>

namespace odin {

/* x * y + z rounded once, as an FMA does, without one and without the
 * library: the same on every host. The product is exact in a double (24 and
 * 24 bits), the sum is rounded to odd there (of the two doubles around it,
 * the one with its last bit set), then to the nearest float: a double has
 * more than 24 + 2 bits, so the float is the sum rounded once (Boldo and
 * Melquiond, "Emulation of FMA and correctly rounded sums: proved
 * algorithms using rounding to odd", IEEE Transactions on Computers, 2008).
 * Built without a product fused with a sum (-ffp-contract=off, CMakeLists.txt). */
inline float
fma_by_double(float x, float y, float z)
{
	const double p = static_cast<double>(x) * static_cast<double>(y);
	const double s = p + static_cast<double>(z);

	/* What s leaves out of the sum (TwoSum, Knuth); none when the sum is
	 * not finite, or a double holds it */
	if (std::isfinite(s)) {
		const double back = s - p;
		const double left = (p - (s - back)) + (static_cast<double>(z) - back);
		uint64_t bits = std::bit_cast<uint64_t>(s);

		/* Even: the other neighbour, towards the sum, is the odd one.
		 * A sum that rounds to zero is zero. */
		if (left != 0.0 && !(bits & 1)) {
			bits = ((left > 0.0) == (s > 0.0)) ? bits + 1 : bits - 1;
			return static_cast<float>(std::bit_cast<double>(bits));
		}
	}
	return static_cast<float>(s);
}

} /* namespace odin */

#endif /* ODIN_FMA_BY_DOUBLE_HPP */

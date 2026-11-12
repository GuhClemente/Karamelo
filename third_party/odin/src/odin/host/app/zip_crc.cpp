/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
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

/* miniz's CRC-32 (USE_EXTERNAL_MZCRC): the CRC instructions of ARMv8 where
 * the build has them, else eight bytes per step from eight tables. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <miniz.h>
#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

namespace {

#if !defined(__ARM_FEATURE_CRC32)
constexpr std::array<std::array<uint32_t, 256>, 8>
make_tables()
{
	std::array<std::array<uint32_t, 256>, 8> t{};
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
		t[0][i] = c;
	}
	for (size_t n = 1; n < 8; n++)
		for (size_t i = 0; i < 256; i++)
			t[n][i] = (t[n - 1][i] >> 8) ^ t[0][t[n - 1][i] & 0xFF];
	return t;
}

constexpr auto tables = make_tables();

uint32_t
le32(const uint8_t *p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
#endif

} /* namespace */

extern "C" mz_ulong
mz_crc32(mz_ulong crc, const mz_uint8 *ptr, size_t buf_len)
{
	uint32_t c = static_cast<uint32_t>(crc) ^ 0xFFFFFFFFu;
#if defined(__ARM_FEATURE_CRC32)
	for (; buf_len >= 8; ptr += 8, buf_len -= 8) {
		uint64_t v;
		std::memcpy(&v, ptr, 8);
		c = __crc32d(c, v);
	}
	for (; buf_len; ptr++, buf_len--)
		c = __crc32b(c, *ptr);
#else
	for (; buf_len >= 8; ptr += 8, buf_len -= 8) {
		const uint32_t lo = c ^ le32(ptr);
		const uint32_t hi = le32(ptr + 4);
		c = tables[7][lo & 0xFF] ^ tables[6][(lo >> 8) & 0xFF] ^
		    tables[5][(lo >> 16) & 0xFF] ^ tables[4][lo >> 24] ^
		    tables[3][hi & 0xFF] ^ tables[2][(hi >> 8) & 0xFF] ^
		    tables[1][(hi >> 16) & 0xFF] ^ tables[0][hi >> 24];
	}
	for (; buf_len; ptr++, buf_len--)
		c = (c >> 8) ^ tables[0][(c ^ *ptr) & 0xFF];
#endif
	return ~c;
}

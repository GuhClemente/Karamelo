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

#ifndef ODIN_BYTES_HPP
#define ODIN_BYTES_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace odin {

/* A value of `size` bytes at a place of a memory of the board, the least
 * significant byte first. The sizes of the bus, 1, 2, 4 and 8, are told
 * apart so that each is a plain load or store: a copy of a size the compiler
 * does not know is a call into the C library, and the ROMs of a game put
 * together that way took a third of a second at each start. */
inline uint64_t
load_bytes(const uint8_t *at, unsigned size)
{
	switch (size) {
	case 1:
		return *at;
	case 2: {
		uint16_t value;

		std::memcpy(&value, at, sizeof(value));
		return value;
	}
	case 4: {
		uint32_t value;

		std::memcpy(&value, at, sizeof(value));
		return value;
	}
	case 8: {
		uint64_t value;

		std::memcpy(&value, at, sizeof(value));
		return value;
	}
	default: {
		uint64_t value = 0;

		std::memcpy(&value, at, size);
		return value;
	}
	}
}

inline void
store_bytes(uint8_t *at, unsigned size, uint64_t value)
{
	switch (size) {
	case 1:
		*at = static_cast<uint8_t>(value);
		break;
	case 2: {
		const uint16_t low = static_cast<uint16_t>(value);

		std::memcpy(at, &low, sizeof(low));
		break;
	}
	case 4: {
		const uint32_t low = static_cast<uint32_t>(value);

		std::memcpy(at, &low, sizeof(low));
		break;
	}
	case 8:
		std::memcpy(at, &value, sizeof(value));
		break;
	default:
		std::memcpy(at, &value, size);
		break;
	}
}

/* 16 and 32 bits from those bytes, the least significant first, whatever
 * the order of the host */
inline uint16_t
le16(const uint8_t *at)
{
	return static_cast<uint16_t>(at[0] | at[1] << 8);
}

inline uint32_t
le32(const uint8_t *at)
{
	return static_cast<uint32_t>(at[0]) | static_cast<uint32_t>(at[1]) << 8 | static_cast<uint32_t>(at[2]) << 16 |
	       static_cast<uint32_t>(at[3]) << 24;
}

/* And back: those bytes from 16 and 32 bits */
inline void
put_le16(uint8_t *at, uint16_t value)
{
	at[0] = static_cast<uint8_t>(value);
	at[1] = static_cast<uint8_t>(value >> 8);
}

inline void
put_le32(uint8_t *at, uint32_t value)
{
	put_le16(at, static_cast<uint16_t>(value));
	put_le16(at + 2, static_cast<uint16_t>(value >> 16));
}

/* The same, the most significant byte first */
inline uint16_t
be16(const uint8_t *at)
{
	return static_cast<uint16_t>(at[0] << 8 | at[1]);
}

inline uint32_t
be32(const uint8_t *at)
{
	return static_cast<uint32_t>(at[0]) << 24 | static_cast<uint32_t>(at[1]) << 16 | static_cast<uint32_t>(at[2]) << 8 |
	       static_cast<uint32_t>(at[3]);
}

inline void
put_be16(uint8_t *at, uint16_t value)
{
	at[0] = static_cast<uint8_t>(value >> 8);
	at[1] = static_cast<uint8_t>(value);
}

/* The two bytes of 16 bits swapped */
inline constexpr uint16_t
swap16(uint16_t value)
{
	return static_cast<uint16_t>(value << 8 | value >> 8);
}

/* The four bytes of 32 bits the other way round */
inline constexpr uint32_t
swap32(uint32_t value)
{
	return static_cast<uint32_t>(swap16(static_cast<uint16_t>(value))) << 16 | swap16(static_cast<uint16_t>(value >> 16));
}

/* A word read as the float it holds */
inline float
f32(uint32_t word)
{
	return std::bit_cast<float>(word);
}

/* FNV-1a, 64 bits: the hash of nothing, and a hash going on with those
 * bytes */
inline constexpr uint64_t FNV_START = 0xCBF29CE484222325ull;

inline uint64_t
fnv1a(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *const bytes = static_cast<const uint8_t *>(data);

	for (size_t i = 0; i < size; i++)
		hash = (hash ^ bytes[i]) * 0x100000001B3ull;
	return hash;
}

/* Whether two objects hold the same bytes. It is compiled in place whatever
 * their size, for what is compared at every mesh: memcmp is a call into the
 * C library on some hosts. */
template <typename T>
inline bool
same_bytes(const T &a, const T &b)
{
	const void *const first = &a, *const second = &b;
	const uint8_t *const x = static_cast<const uint8_t *>(first);
	const uint8_t *const y = static_cast<const uint8_t *>(second);
	uint64_t differ = 0;
	size_t i = 0;

	for (; i + 8 <= sizeof(T); i += 8)
		differ |= load_bytes(x + i, 8) ^ load_bytes(y + i, 8);
	for (; i < sizeof(T); i++)
		differ |= static_cast<uint64_t>(x[i] ^ y[i]);
	return differ == 0;
}

} /* namespace odin */

#endif /* ODIN_BYTES_HPP */

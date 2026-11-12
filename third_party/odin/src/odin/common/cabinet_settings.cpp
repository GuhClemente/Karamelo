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

#include "odin/common/cabinet_settings.hpp"
#include "odin/common/bytes.hpp"

#include <algorithm>

namespace odin::cabinet {

namespace {

uint32_t turn(uint32_t r)
{
	return (r & 0x80000000u) ? (r << 1) ^ 0x10210000u : r << 1;
}

uint8_t with_bits(uint8_t byte, unsigned shift, unsigned bits, unsigned value)
{
	const unsigned mask = ((1u << bits) - 1) << shift;

	return static_cast<uint8_t>((byte & ~mask) | ((value << shift) & mask));
}

} /* namespace */

uint16_t
crc(std::span<const uint8_t> bytes)
{
	uint32_t r = 0x01A18104u;

	for (const uint8_t b : bytes) {
		r |= b;
		for (int i = 0; i < 8; i++)
			r = turn(r);
	}
	for (int i = 0; i < 8; i++)
		r = turn(r);
	return static_cast<uint16_t>(r >> 16);
}

bool
set_mie(std::span<uint8_t> chip, size_t at, unsigned shift, unsigned bits, unsigned value)
{
	if (chip.size() != 2 * MIE_COPY || at >= MIE_SETTINGS || bits == 0 || shift + bits > 8)
		return false;
	for (size_t copy = 0; copy < chip.size(); copy += MIE_COPY) {
		chip[copy + at] = with_bits(chip[copy + at], shift, bits, value);

		const uint16_t sum = crc(chip.subspan(copy, MIE_SETTINGS));

		put_le16(&chip[copy + MIE_SETTINGS], sum);
	}
	return true;
}

size_t
backup_block(std::span<const uint8_t> bram)
{
	if (bram.size() < BACKUP_BLOCK)
		return 0;

	const uint32_t head = le32(&bram[BACKUP_HEAD + 4]);
	const size_t size = head >> 16;

	if (size == 0 || BACKUP_BLOCK + 2 * size > bram.size())
		return 0;
	return crc(bram.subspan(BACKUP_BLOCK, size)) == (head & 0xFFFF) ? size : 0;
}

bool
backup_at_rest(std::span<const uint8_t> bram)
{
	const size_t size = backup_block(bram);
	const std::span<const uint8_t> block = bram.subspan(BACKUP_BLOCK, size);

	return size && std::equal(block.begin(), block.end(), bram.begin() + static_cast<ptrdiff_t>(BACKUP_BLOCK + size));
}

bool
set_backup(std::span<uint8_t> bram, size_t at, unsigned shift, unsigned bits, unsigned value)
{
	const size_t size = backup_block(bram);

	if (size == 0 || at >= size || bits == 0 || shift + bits > 8)
		return false;
	for (const size_t block : { BACKUP_BLOCK, BACKUP_BLOCK + size })
		bram[block + at] = with_bits(bram[block + at], shift, bits, value);

	const uint16_t sum = crc(bram.subspan(BACKUP_BLOCK, size));

	put_le16(&bram[BACKUP_HEAD + 4], sum);
	return true;
}

} /* namespace odin::cabinet */

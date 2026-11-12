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

#include "odin/machine/core/host_code.hpp"

#include <cstring>

#if defined(ODIN_HOST_AARCH64)

#include "odin/machine/core/arm64_encode.hpp"

namespace odin {

using namespace arm64;

void HostCode::emit(uint32_t instruction)
{
	std::memcpy(m_code + m_used, &instruction, 4);
	m_used += 4;
}

namespace {

uint32_t get(const uint8_t *code, uint32_t at)
{
	uint32_t instruction;

	std::memcpy(&instruction, code + at, 4);
	return instruction;
}

/* The branch at `at` sent to `to` */
void patch(uint8_t *code, uint32_t at, uint32_t to)
{
	const int64_t words = (static_cast<int64_t>(to) - static_cast<int64_t>(at)) / 4;
	uint32_t instruction = get(code, at);

	if ((instruction & 0x7C000000) == 0x14000000) {			/* B, BL */
		instruction = (instruction & 0xFC000000) | (static_cast<uint32_t>(words) & 0x03FFFFFF);
	} else if ((instruction & 0x7E000000) == 0x36000000) {		/* TBZ, TBNZ */
		instruction = (instruction & 0xFFF8001F) | ((static_cast<uint32_t>(words) & 0x3FFF) << 5);
	} else {							/* B.cond, CBZ, CBNZ */
		instruction = (instruction & 0xFF00001F) | ((static_cast<uint32_t>(words) & 0x7FFFF) << 5);
	}
	std::memcpy(code + at, &instruction, 4);
}

bool near19(int64_t distance)
{
	return distance >= -(1 << 20) && distance < (1 << 20);
}

} /* namespace */

void HostCode::bind(Jump jump)
{
	patch(m_code, jump.from, m_used);
}

void HostCode::jump_back(uint32_t place)
{
	emit(B);
	patch(m_code, m_used - 4, place);
}

void HostCode::jump_to(uint32_t place)
{
	jump_back(place);
}

void HostCode::jump_to_if(Cond cond, uint32_t place)
{
	const int64_t distance = static_cast<int64_t>(place) - static_cast<int64_t>(m_used);

	if (near19(distance)) {
		emit(B_COND | cond);
		patch(m_code, m_used - 4, place);
	} else {
		emit(B_COND | (cond ^ 1) | (2 << 5));		/* over the branch */
		jump_back(place);
	}
}

void HostCode::jump_to_if_zero(uint8_t reg, bool zero, uint32_t place)
{
	const int64_t distance = static_cast<int64_t>(place) - static_cast<int64_t>(m_used);

	if (near19(distance)) {
		emit((zero ? CBZ : CBNZ) | WIDE | reg);
		patch(m_code, m_used - 4, place);
	} else {
		emit((zero ? CBNZ : CBZ) | WIDE | reg | (2 << 5));
		jump_back(place);
	}
}

HostCode::Jump HostCode::jump_if(Cond cond)
{
	emit(B_COND | cond);
	return Jump{m_used - 4};
}

HostCode::Jump HostCode::jump_if_zero(uint8_t reg, bool zero, bool wide)
{
	emit((zero ? CBZ : CBNZ) | (wide ? WIDE : 0) | reg);
	return Jump{m_used - 4};
}

void HostCode::relink(uint32_t at, uint32_t place)
{
	patch(m_code, at, place);
	if (at < m_written) {
		m_written = at;
	}
}

HostCode::Jump HostCode::jump_ahead()
{
	emit(B);
	return Jump{m_used - 4};
}

void HostCode::call_to(uintptr_t target)
{
	const int64_t distance = static_cast<int64_t>(target - (reinterpret_cast<uintptr_t>(m_code) + m_used));

	if (distance >= -(int64_t{1} << 27) && distance < (int64_t{1} << 27)) {
		emit(BL | (static_cast<uint32_t>(distance / 4) & 0x03FFFFFF));
	} else {
		move(X16, target, true);
		emit(BLR | (X16 << 5));
		m_far_calls++;
	}
}

void HostCode::move(uint8_t reg, uint64_t value, bool wide)
{
	const uint32_t halves = wide ? 4 : 2;
	uint32_t zeros = 0, ones = 0, n, r, s;

	if (!wide) {
		value &= 0xFFFFFFFF;
	}
	if (logical_mask(value, wide, n, r, s)) {
		emit(ORR_VALUE | (wide ? WIDE : 0) | (n << 22) | (r << 16) | (s << 10) | (ZR << 5) | reg);
		return;
	}
	for (uint32_t h = 0; h < halves; h++) {
		const uint32_t half = (value >> (16 * h)) & 0xFFFF;

		zeros += half == 0;
		ones += half == 0xFFFF;
	}
	/* From every bit set, the halves not all set put in; else from none */
	const bool inverted = ones > zeros;
	const uint32_t blank = inverted ? 0xFFFF : 0;
	bool first = true;

	for (uint32_t h = 0; h < halves; h++) {
		const uint32_t half = (value >> (16 * h)) & 0xFFFF;

		if (half == blank) {
			continue;
		}
		if (first) {
			emit((inverted ? MOVN : MOVZ) | (wide ? WIDE : 0) | (h << 21) |
			     ((inverted ? ~half & 0xFFFF : half) << 5) | reg);
			first = false;
		} else {
			emit(MOVK | (wide ? WIDE : 0) | (h << 21) | (half << 5) | reg);
		}
	}
	if (first) {
		emit((inverted ? MOVN : MOVZ) | (wide ? WIDE : 0) | reg);
	}
}

void HostCode::copy(uint8_t dst, uint8_t src, bool wide)
{
	emit(ORR | (wide ? WIDE : 0) | (src << 16) | (ZR << 5) | dst);
}

void HostCode::add_value(uint8_t dst, uint8_t src, int64_t value, bool wide)
{
	const uint32_t w = wide ? WIDE : 0;

	if (!wide) {
		value = static_cast<int32_t>(value);
	}
	if (value >= 0 && value < 4096) {
		emit(ADD_VALUE | w | (static_cast<uint32_t>(value) << 10) | (src << 5) | dst);
	} else if (value < 0 && value > -4096) {
		emit(SUB_VALUE | w | (static_cast<uint32_t>(-value) << 10) | (src << 5) | dst);
	} else if (value > 0 && (value & 0xFFF) == 0 && value < (1 << 24)) {
		emit(ADD_VALUE | w | (1 << 22) | (static_cast<uint32_t>(value >> 12) << 10) | (src << 5) | dst);
	} else {
		move(X17, static_cast<uint64_t>(value), wide);
		emit(ADD | w | (X17 << 16) | (src << 5) | dst);
	}
}

void HostCode::and_value(uint8_t dst, uint8_t src, uint32_t value, uint8_t spare)
{
	uint32_t n, r, s;

	if (logical_mask(value, false, n, r, s)) {
		emit(AND_VALUE | (r << 16) | (s << 10) | (src << 5) | dst);
	} else {
		move(spare, value);
		emit(AND | (spare << 16) | (src << 5) | dst);
	}
}

namespace {

/* The loads and stores of 1, 2, 4 and 8 bytes at an offset scaled by
 * their size; a load with its sign extends to 32 bits. */
uint32_t access_code(bool load, uint32_t bytes, bool sign)
{
	switch (bytes) {
	case 1: return load ? (sign ? LDRSB : LDRB) : STRB;
	case 2: return load ? (sign ? LDRSH : LDRH) : STRH;
	case 4: return load ? LDR_W : STR_W;
	default: return load ? LDR_X : STR_X;
	}
}

} /* namespace */

/* An offset scaled within reach, else one of 9 bits with no scale, else
 * the offset in x17 */
void HostCode::load(uint8_t reg, uint8_t base, int64_t offset, uint32_t bytes, bool sign)
{
	const uint32_t code = access_code(true, bytes, sign);

	if (offset >= 0 && offset % bytes == 0 && offset / bytes < 4096) {
		emit(code | (static_cast<uint32_t>(offset / bytes) << 10) | (base << 5) | reg);
	} else if (offset >= -256 && offset < 256) {
		emit(unscaled(code) | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) | reg);
	} else {
		move(X17, static_cast<uint64_t>(offset), true);
		emit(indexed(code) | (X17 << 16) | (base << 5) | reg);
	}
}

void HostCode::store(uint8_t reg, uint8_t base, int64_t offset, uint32_t bytes)
{
	const uint32_t code = access_code(false, bytes, false);

	if (offset >= 0 && offset % bytes == 0 && offset / bytes < 4096) {
		emit(code | (static_cast<uint32_t>(offset / bytes) << 10) | (base << 5) | reg);
	} else if (offset >= -256 && offset < 256) {
		emit(unscaled(code) | ((static_cast<uint32_t>(offset) & 0x1FF) << 12) | (base << 5) | reg);
	} else {
		move(X17, static_cast<uint64_t>(offset), true);
		emit(indexed(code) | (X17 << 16) | (base << 5) | reg);
	}
}

void HostCode::load_float(uint8_t f, uint8_t base, int64_t offset)
{
	if (offset >= 0 && offset % 4 == 0 && offset / 4 < 4096) {
		emit(LDR_S | (static_cast<uint32_t>(offset / 4) << 10) | (base << 5) | f);
	} else {
		move(X17, static_cast<uint64_t>(offset), true);
		emit(indexed(LDR_S) | (X17 << 16) | (base << 5) | f);
	}
}

void HostCode::store_float(uint8_t f, uint8_t base, int64_t offset)
{
	if (offset >= 0 && offset % 4 == 0 && offset / 4 < 4096) {
		emit(STR_S | (static_cast<uint32_t>(offset / 4) << 10) | (base << 5) | f);
	} else {
		move(X17, static_cast<uint64_t>(offset), true);
		emit(indexed(STR_S) | (X17 << 16) | (base << 5) | f);
	}
}

/* x29 and x30 first, then x19 to x28, 96 bytes that keep sp aligned */
void HostCode::way_in()
{
	m_used = 0;
	m_written = 0;
	emit(STP_PRE | ((static_cast<uint32_t>(-96 / 8) & 0x7F) << 15) | (30 << 10) | (SP << 5) | 29);
	emit(ADD_VALUE | WIDE | (SP << 5) | 29);			/* mov x29, sp */
	for (uint32_t pair = 0; pair < 5; pair++) {
		const uint32_t first = 19 + 2 * pair;

		emit(STP | ((2 + 2 * pair) << 15) | ((first + 1) << 10) | (SP << 5) | first);
	}
	copy(X19, X0, true);
}

void HostCode::way_in_end()
{
	emit(BR | (X1 << 5));
	m_leave = m_used;
}

void HostCode::way_out()
{
	for (uint32_t pair = 0; pair < 5; pair++) {
		const uint32_t first = 19 + 2 * pair;

		emit(LDP | ((2 + 2 * pair) << 15) | ((first + 1) << 10) | (SP << 5) | first);
	}
	emit(LDP_POST | ((96 / 8) << 15) | (30 << 10) | (SP << 5) | 29);
	emit(RET);
}

} /* namespace odin */

#endif

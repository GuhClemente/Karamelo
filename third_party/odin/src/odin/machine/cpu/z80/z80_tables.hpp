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

/* Flag and timing tables shared by z80.cpp and z80_ops.cpp. Private to
 * the core; machines include z80.hpp only. */

#ifndef ODIN_Z80_TABLES_HPP
#define ODIN_Z80_TABLES_HPP

#include <array>
#include <bit>
#include <cstdint>

namespace odin {

constexpr uint8_t CF = 0x01;
constexpr uint8_t NF = 0x02;
constexpr uint8_t PF = 0x04;
constexpr uint8_t XF = 0x08;
constexpr uint8_t HF = 0x10;
constexpr uint8_t YF = 0x20;
constexpr uint8_t ZF = 0x40;
constexpr uint8_t SF = 0x80;
constexpr uint8_t XYF = XF | YF;

struct FlagTables {
	std::array<uint8_t, 256> sz{};
	std::array<uint8_t, 256> szp{};
	std::array<uint8_t, 256> inc{};
	std::array<uint8_t, 256> dec{};
};

constexpr FlagTables make_flag_tables()
{
	FlagTables t{};
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t sz = (i == 0 ? ZF : 0) | (i & (SF | XYF));
		uint32_t parity = (std::popcount(i) & 1) ? 0 : PF;
		t.sz[i] = static_cast<uint8_t>(sz);
		t.szp[i] = static_cast<uint8_t>(sz | parity);
		t.inc[i] = static_cast<uint8_t>(sz | (i == 0x80 ? PF : 0) | ((i & 0x0F) == 0x00 ? HF : 0));
		t.dec[i] = static_cast<uint8_t>(sz | NF | (i == 0x7F ? PF : 0) | ((i & 0x0F) == 0x0F ? HF : 0));
	}
	return t;
}

constexpr FlagTables FLAG_TABLES = make_flag_tables();
constexpr std::array<uint8_t, 256> SZ = FLAG_TABLES.sz;
constexpr std::array<uint8_t, 256> SZP = FLAG_TABLES.szp;
constexpr std::array<uint8_t, 256> SZHV_INC = FLAG_TABLES.inc;
constexpr std::array<uint8_t, 256> SZHV_DEC = FLAG_TABLES.dec;

/* T-states per unprefixed opcode (Z80 CPU User Manual, instruction
 * tables). CB, DD, ED and FD cost 0 here because their pages carry the
 * whole count. Conditional jumps, calls, returns and DJNZ list the
 * not-taken count; the decoder adds the rest when taken. */
constexpr std::array<uint8_t, 256> CYCLES_MAIN = {{
	 4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
	 8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
	 7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
	 7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
	 5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
	 5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
	 5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11,
}};

/* True for the opcodes whose (HL) operand turns into (IX+d) under a
 * DD or FD prefix: INC/DEC/LD (HL),n, LD r,(HL), LD (HL),r, ALU (HL). */
constexpr bool uses_hl_mem(uint32_t op)
{
	uint32_t x = op >> 6;
	uint32_t y = (op >> 3) & 7;
	uint32_t z = op & 7;
	if (op == 0x76) {
		return false;
	}
	if (x == 0) {
		return z >= 4 && y == 6;
	}
	if (x == 1) {
		return y == 6 || z == 6;
	}
	return x == 2 && z == 6;
}

/* CB page: 8 for registers, 15 for (HL), 12 for BIT n,(HL). */
constexpr std::array<uint8_t, 256> make_cb_cycles()
{
	std::array<uint8_t, 256> t{};
	for (uint32_t op = 0; op < 256; op++) {
		bool mem = (op & 7) == 6;
		bool is_bit = (op >> 6) == 1;
		t[op] = static_cast<uint8_t>(!mem ? 8 : (is_bit ? 12 : 15));
	}
	return t;
}

/* ED page: 8 for every undefined slot (two M1 cycles, no effect). */
constexpr uint8_t ed_cycles(uint32_t op)
{
	uint32_t x = op >> 6;
	uint32_t y = (op >> 3) & 7;
	uint32_t z = op & 7;
	if (x == 1) {
		switch (z) {
		case 0: return 12;
		case 1: return 12;
		case 2: return 15;
		case 3: return 20;
		case 5: return 14;
		case 7: return y < 4 ? 9 : (y < 6 ? 18 : 8);
		default: return 8;
		}
	}
	if (x == 2 && z < 4 && y >= 4) {
		return 16;
	}
	return 8;
}

constexpr std::array<uint8_t, 256> make_ed_cycles()
{
	std::array<uint8_t, 256> t{};
	for (uint32_t op = 0; op < 256; op++) {
		t[op] = ed_cycles(op);
	}
	return t;
}

/* DD/FD page: the prefix adds 4; a displacement adds 8 more (3 to read
 * it, 5 to add it), except LD (IX+d),n which overlaps the add with the
 * immediate read and stays at 19. */
constexpr std::array<uint8_t, 256> make_xy_cycles()
{
	std::array<uint8_t, 256> t{};
	for (uint32_t op = 0; op < 256; op++) {
		if (uses_hl_mem(op)) {
			t[op] = static_cast<uint8_t>((op == 0x34 || op == 0x35) ? 23 : 19);
		} else {
			t[op] = static_cast<uint8_t>(CYCLES_MAIN[op] + 4);
		}
	}
	return t;
}

/* DD CB d op page: 23, or 20 for BIT. */
constexpr std::array<uint8_t, 256> make_xycb_cycles()
{
	std::array<uint8_t, 256> t{};
	for (uint32_t op = 0; op < 256; op++) {
		t[op] = static_cast<uint8_t>((op >> 6) == 1 ? 20 : 23);
	}
	return t;
}

constexpr std::array<uint8_t, 256> CYCLES_CB = make_cb_cycles();
constexpr std::array<uint8_t, 256> CYCLES_ED = make_ed_cycles();
constexpr std::array<uint8_t, 256> CYCLES_XY = make_xy_cycles();
constexpr std::array<uint8_t, 256> CYCLES_XYCB = make_xycb_cycles();

static_assert(CYCLES_XY[0x21] == 14 && CYCLES_XY[0x36] == 19 && CYCLES_XY[0x34] == 23);
static_assert(CYCLES_XY[0xE3] == 23 && CYCLES_XY[0xE9] == 8 && CYCLES_XY[0x7E] == 19);
static_assert(CYCLES_ED[0x43] == 20 && CYCLES_ED[0x67] == 18 && CYCLES_ED[0xB0] == 16);
static_assert(CYCLES_CB[0x46] == 12 && CYCLES_CB[0x06] == 15 && CYCLES_CB[0xC6] == 15);

} /* namespace odin */

#endif /* ODIN_Z80_TABLES_HPP */

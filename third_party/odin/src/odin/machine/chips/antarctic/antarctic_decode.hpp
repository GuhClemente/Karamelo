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

#ifndef ODIN_ANTARCTIC_DECODE_HPP
#define ODIN_ANTARCTIC_DECODE_HPP

#include <cstdint>

namespace odin {

/* What the instructions of the CP and their disassembly both read */

/* The size of an instruction, in bytes: bits 4-5 of its first word */
inline uint32_t
insn_size(uint32_t word)
{
	return 1u << (((word >> 4) & 3) + 2);
}

/* Where a jump or a call goes: its second word in words, from the
 * instruction itself when bit 11 is set */
inline uint32_t
jump_address(uint32_t pc, const uint32_t *inst)
{
	uint32_t addr = inst[1] * 4;

	if (inst[0] & 0x800)
		addr += pc;
	return addr;
}

/* The low 16 bits of a word as a signed number */
inline float
s16(uint32_t word)
{
	return static_cast<float>(static_cast<int16_t>(word));
}

} /* namespace odin */

#endif /* ODIN_ANTARCTIC_DECODE_HPP */

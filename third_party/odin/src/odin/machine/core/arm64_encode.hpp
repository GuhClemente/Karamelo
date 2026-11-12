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

#ifndef ODIN_ARM64_ENCODE_HPP
#define ODIN_ARM64_ENCODE_HPP

/* The instructions of AArch64 the host code is written with, each without
 * its registers and values: 32 bits wide unless WIDE is or'ed in. */

#include <cstdint>

namespace odin::arm64 {

inline constexpr uint32_t WIDE = 1u << 31;
inline constexpr uint8_t SP = 31;

/* Branches */
inline constexpr uint32_t B = 0x14000000, BL = 0x94000000, B_COND = 0x54000000, CBZ = 0x34000000,
	CBNZ = 0x35000000, BR = 0xD61F0000, BLR = 0xD63F0000, RET = 0xD65F03C0;

/* Arithmetic and logic on registers (Rm << 16, Rn << 5, Rd), or on a
 * value of 12 bits (<< 10, shifted by 12 with 1 << 22) */
inline constexpr uint32_t ADD = 0x0B000000, ADDS = 0x2B000000, SUB = 0x4B000000, SUBS = 0x6B000000,
	ADCS = 0x3A000000, SBCS = 0x7A000000, AND = 0x0A000000, ANDS = 0x6A000000, ORR = 0x2A000000,
	EOR = 0x4A000000, ORN = 0x2A200000, BICS = 0x6A200000;
inline constexpr uint32_t ADD_VALUE = 0x11000000, SUB_VALUE = 0x51000000, SUBS_VALUE = 0x71000000;
/* With a mask (N << 22, immr << 16, imms << 10), see logical_mask() */
inline constexpr uint32_t AND_VALUE = 0x12000000, ORR_VALUE = 0x32000000, EOR_VALUE = 0x52000000,
	ANDS_VALUE = 0x72000000;
/* Moves of 16 bits (hw << 21, the value << 5) */
inline constexpr uint32_t MOVZ = 0x52800000, MOVN = 0x12800000, MOVK = 0x72800000;
/* Bit fields (immr << 16, imms << 10), rotations, shifts by a register,
 * conditional selects (cond << 12) */
inline constexpr uint32_t UBFM = 0x53000000, SBFM = 0x13000000, EXTR = 0x13800000, RORV = 0x1AC02C00,
	LSRV = 0x1AC02400, CSEL = 0x1A800000, CSINC = 0x1A800400;
inline constexpr uint32_t MRS_NZCV = 0xD53B4200, MSR_NZCV = 0xD51B4200;
/* Xd = Xa + Wn * Wm, unsigned (Rm << 16, Ra << 10) */
inline constexpr uint32_t UMADDL = 0x9BA00000;
/* x9 compared with w9 extended by its sign: SUBS xzr, x9, w9, SXTW */
inline constexpr uint32_t CMP_SXTW = 0xEB20C000;

/* Loads and stores at an offset scaled by their size (<< 10) */
inline constexpr uint32_t STRB = 0x39000000, LDRB = 0x39400000, LDRSB = 0x39C00000, STRH = 0x79000000,
	LDRH = 0x79400000, LDRSH = 0x79C00000, STR_W = 0xB9000000, LDR_W = 0xB9400000, STR_X = 0xF9000000,
	LDR_X = 0xF9400000, STR_S = 0xBD000000, LDR_S = 0xBD400000;
/* The same with an offset of 9 bits not scaled (<< 12), or a register
 * (Rm << 16; UXTW: Rm of 32 bits, 2 << 13 for LSL of 64) */
constexpr uint32_t unscaled(uint32_t code) { return code & ~0x01000000u; }
constexpr uint32_t indexed(uint32_t code) { return (code & ~0x01000000u) | 0x00206800; }
constexpr uint32_t indexed_uxtw(uint32_t code) { return (code & ~0x01000000u) | 0x00204800; }
/* Pairs of 64 bits (imm7 << 15 in eights, Rt2 << 10) */
inline constexpr uint32_t STP = 0xA9000000, STP_PRE = 0xA9800000, LDP = 0xA9400000, LDP_POST = 0xA8C00000;

/* Floats of single precision */
inline constexpr uint32_t FADD = 0x1E202800, FSUB = 0x1E203800, FMUL = 0x1E200800, FCMP = 0x1E202000,
	FMADD = 0x1F000000, SCVTF = 0x1E220000, FCVTZS_X = 0x9E380000;

/* Vectors of four singles: loads and stores of 128 bits (<< 10 in
 * sixteens), a lane broadcast (lane << 19), products and fused sums */
inline constexpr uint32_t LDR_Q = 0x3DC00000, STR_Q = 0x3D800000, DUP_S = 0x4E040400, FMUL_4S = 0x6E20DC00,
	FMLA_4S = 0x4E20CC00;

/* Rd = Rn shifted by a count of bits, on 32 bits */
constexpr uint32_t lsl(uint32_t count) { return UBFM | (((32 - count) & 31) << 16) | ((31 - count) << 10); }
constexpr uint32_t lsr(uint32_t count) { return UBFM | (count << 16) | (31 << 10); }
constexpr uint32_t asr(uint32_t count) { return SBFM | (count << 16) | (31 << 10); }
/* Rd = `width` bits of Rn from `low` */
constexpr uint32_t ubfx(uint32_t low, uint32_t width) { return UBFM | (low << 16) | ((low + width - 1) << 10); }

/* The mask a logical step takes in place of a register: a run of ones,
 * rotated, repeated over the register. False for any other value. */
inline bool logical_mask(uint64_t value, bool wide, uint32_t &n, uint32_t &immr, uint32_t &imms)
{
	uint32_t size = wide ? 64 : 32;

	if (!wide) {
		value &= 0xFFFFFFFF;
	}
	const uint64_t all = wide ? ~uint64_t{0} : 0xFFFFFFFF;
	if (value == 0 || value == all) {
		return false;
	}
	while (size > 2) {
		const uint32_t half = size / 2;
		const uint64_t mask = (uint64_t{1} << half) - 1;

		if ((value & mask) != ((value >> half) & mask)) {
			break;
		}
		size = half;
	}
	const uint64_t mask = size == 64 ? ~uint64_t{0} : (uint64_t{1} << size) - 1;
	const uint64_t element = value & mask;
	const uint32_t ones = static_cast<uint32_t>(__builtin_popcountll(element));
	const uint64_t run = ones == 64 ? ~uint64_t{0} : (uint64_t{1} << ones) - 1;

	for (uint32_t r = 0; r < size; r++) {
		const uint64_t rotated = r == 0 ? element : ((element >> r) | (element << (size - r))) & mask;

		if (rotated == run) {
			n = size == 64;
			immr = (size - r) % size;
			imms = ((~(size - 1) << 1) & 0x3F) | (ones - 1);
			return true;
		}
	}
	return false;
}

} /* namespace odin::arm64 */

#endif /* ODIN_ARM64_ENCODE_HPP */

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
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_PMX1_TA_HPP
#define ODIN_PMX1_TA_HPP

#include <cstdint>
#include <functional>
#include <span>

namespace odin {

/* The tile accelerator of the PMX1: the primary core gives it 64-bit
 * instructions, it reads a control list of triangle strips, copies each
 * strip with its ISP/TSP words to the copythrough area, and answers with
 * one status word per strip and 32x16 tile it touches, then an end of
 * list. The core builds the tiles' object lists from those words.
 *
 * Emulated: the copythrough mode the games use, without culling, user
 * clipping or macrotile overlap (said once if asked). A strip is binned by
 * the bounding box of each of its triangles (INFERRED). The words come as
 * the core reads them, one strip at a time. */
class Pmx1Ta {
public:
	/* What a save state keeps, in 32-bit words */
	struct State {
		uint32_t pages[4];		/* the control list's pages (Inst 0a, 0b) */
		uint32_t screen[2], clip[2];	/* Inst 2 and Inst 3 */
		uint32_t copy_to, copy_size;	/* Inst 1: where, and how many words */
		uint32_t copied;		/* words copied since */
		uint32_t words, read;		/* of the control list started, and read */
		uint32_t current_page, next_page;
		uint32_t isp[5], isp_words;	/* the ISP/TSP words of the strips */
		uint32_t running;		/* started, its end of list not given */
		/* The strip being binned: its output pointer, its triangles'
		 * tiles (x0, x1, y0, y1, empty when x0 > x1) and the next tile */
		uint32_t binning, pointer;
		int32_t boxes[6][4];
		int32_t x0, x1, y0, y1, x, y;
	};

	/* `vram` is the frame buffer, `top` its address bits 31-26, read when
	 * the TA reads or writes; `written` is told what it writes there */
	Pmx1Ta(std::span<uint8_t> vram, State &state, const uint32_t &top,
	       std::function<void(uint32_t offset, uint32_t size)> written);

	void instruction(uint32_t low, uint32_t high);
	/* The next status word: low = end of list (bit 31), copythrough
	 * overflow (30) and tile number (11-0); high = the object pointer.
	 * False when no start is running. */
	bool status(uint32_t &low, uint32_t &high);

private:
	bool read(uint32_t address, uint32_t &value);
	bool write(uint32_t address, uint32_t value);
	uint8_t *words_at(uint32_t address, uint32_t count);
	bool control(uint32_t &value);
	bool next_strip(uint32_t &low, uint32_t &high);
	bool next_tile(uint32_t &low, uint32_t &high);
	void end(uint32_t &low, uint32_t &high, bool overflow);
	void say(unsigned what, const char *text);

	std::span<uint8_t> m_vram;
	State &m_s;
	const uint32_t &m_top;
	std::function<void(uint32_t, uint32_t)> m_written;
	uint32_t m_said = 0;
};

} /* namespace odin */

#endif

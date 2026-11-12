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
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Power-on, the launch of a list and the loop that executes it. */

#include "odin/machine/chips/antarctic/antarctic.hpp"
#include "odin/machine/chips/antarctic/antarctic_decode.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>

#include "odin/machine/chips/antarctic/antarctic_disasm.hpp"
#include "odin/common/repeats.hpp"
#include "odin/common/stop_message.hpp"

namespace odin {

Antarctic::Antarctic(std::span<uint8_t> cmdram, std::span<uint8_t> ram_s,
                     std::span<uint8_t> texram0, std::span<uint8_t> texram1)
	: m_cmdram(cmdram), m_ram_s(ram_s), m_texram{texram0, texram1},
	  m_lists{std::make_unique<List>(), std::make_unique<List>(), std::make_unique<List>()},
	  m_building(0), m_list(m_lists[0].get()),
	  m_before_texture_write(nullptr), m_before_texture_write_ctx(nullptr),
	  m_disasm(nullptr), m_log(stderr), m_transfer_log(nullptr),
	  m_types_told(0)
{
	reset();
}

void
Antarctic::reset()
{
	std::memset(&m_s, 0, sizeof(m_s));
	/* No cause interrupts until the mask is written: after its reset of
	 * the chips VON4 waits for the frame cause by reading 1A000018, its
	 * interrupts open, and writes the mask next (@0C711F4A). HEURISTIC:
	 * bits 0 to 6, only bit 7 is seen. */
	reg(0x84) = 0xFF;
	m_push.num_verts = 0;
	m_push.num_tris = 0;
	std::memset(m_push.ring, 0, sizeof(m_push.ring));
	for (Vertex &v : m_push.ring)
		v.changed();
	for (unsigned i = 0; i < 3; i++)
		m_push.at[i] = static_cast<uint8_t>(i);
	/* 0 makes 0.0 */
	std::memset(m_push.ambient_of, 0, sizeof(m_push.ambient_of));
	std::memset(m_push.ambient, 0, sizeof(m_push.ambient));
	m_mesh = nullptr;
	/* The other lists are the renderer's until their turn comes. */
	m_list->clear();
}

void
Antarctic::load(const AntarcticState &in)
{
	m_s = in;
	m_mesh = nullptr;
}

void
Antarctic::fatal(const char *what, uint32_t value) const
{
	stop_fatal("antarctic: %s (%08X) at %08X", what, value, m_s.pc);
}

/* A list sends the same thing at every frame: on stderr, what repeats is
 * told once (repeats.hpp) */
void
Antarctic::error(const char *fmt, ...) const
{
	char what[512];
	va_list args;

	if (!m_log)
		return;
	va_start(args, fmt);
	std::vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);
	if (m_log == stderr && !repeats::say(fmt, what))
		return;
	std::fputs(("ERROR: " + std::string(what) + "\n").c_str(), m_log);
}

/*
 * CP Execution
 * ============
 *
 * The games start a list by writing 15000058 = 3 (a write of 1A000024 never
 * starts one again, hikaru_gpu.cpp). It ends at a 1C2.
 *
 * A CP program is uploaded to two different areas in CMDRAM on odd and even
 * frames.
 */

void
Antarctic::start(uint32_t pc, uint32_t sp0, uint32_t sp1)
{
	if (m_disasm)
		std::fputs(" ==== CP BEGIN ==== \n", m_disasm);

	/* A launch over a list that has not ended leaves its mesh there */
	end_open_mesh();

	m_s.running = 1;
	m_s.pc = pc;
	m_s.sp[0] = sp0;
	m_s.sp[1] = sp1;
	m_s.patterns = 0;

	/* HEURISTIC: a strip does not go on from a list to the next. */
	m_push.num_verts = 0;

	/* HEURISTIC: a list starts with the stacks and the bases cleared, and
	 * the scratch objects empty; the current viewport, the matrices and
	 * the tables stay. Cleared at every vertical blanking instead, a list
	 * running or not, the six attract loops send the same lists. */

	/* The static mesh scale (0x901) is a register of the CP that outlives
	 * the list, like the X360 port's global 82E02B60. */
	m_s.poly.type = 0;
	m_s.poly.alpha = 0.0f;

	m_s.lod.value = 0.0f;
	m_s.lod.radius = 0.0f;
	m_s.lod.cond = 0;
	m_s.lod.compared = 0;
	m_s.lod.branch_id = 0xFFFF;

	m_s.vp.depth = 0;
	m_s.vp.scratch.flags = 0;

	m_s.mat.base = 0;
	m_s.mat.scratch.flags = 0;

	m_s.tex.base = 0;
	m_s.tex.scratch.flags = 0;

	m_s.lit.base = 0;
	m_s.lit.scratch.flags = 0;
	m_s.lit.scratchset.flags = 0;
}

void
Antarctic::end_open_mesh()
{
	if (m_s.in_mesh) {
		end_mesh(m_s.pc);
		m_s.in_mesh = 0;
	}
}

/* Reads the instruction at the program counter: its words, zeroes after
 * them. False where there is no program. */
bool
Antarctic::fetch(uint32_t inst[8]) const
{
	std::span<const uint8_t> mem;
	uint32_t offs;

	/* The program has been seen in the command RAM and in the RAM of
	 * the slave only. The command RAM answers at 4C too: once at start
	 * the system library calls, as 4C3FB900 (052 130FEE40), the four
	 * NOP and the return it keeps at 483FB900, and nothing else calls
	 * them. */
	switch (m_s.pc >> 24) {
	case 0x40:
	case 0x41:
		mem = m_ram_s;
		offs = m_s.pc & 0x01FFFFFF;
		break;
	case 0x48:
	case 0x4C:
		mem = m_cmdram;
		offs = m_s.pc & 0x003FFFFF;
		break;
	default:
		return false;
	}
	if (offs + 4 > mem.size())
		return false;

	std::memset(inst, 0, 8 * sizeof(uint32_t));
	std::memcpy(inst, &mem[offs], 4);
	const uint32_t size = insn_size(inst[0]);
	if (offs + size > mem.size()) {
		std::memcpy(inst, &mem[offs], mem.size() - offs);
		return true;
	}
	/* Fixed sizes, copied inline */
	switch (size) {
	case 8:
		std::memcpy(inst, &mem[offs], 8);
		break;
	case 16:
		std::memcpy(inst, &mem[offs], 16);
		break;
	case 32:
		std::memcpy(inst, &mem[offs], 32);
		break;
	}
	return true;
}

bool
Antarctic::run(uint32_t &budget)
{
	const auto &table = insns();

	if (!m_s.running)
		return false;

	while (budget > 0 && m_s.running) {
		uint32_t inst[8];

		if (!fetch(inst)) {
			error("CP %08X: invalid PC, skipping CS", m_s.pc);
			end_open_mesh();
			m_s.running = 0;
			budget--;
			break;
		}

		const Insn &insn = table[inst[0] & 0x1FF];
		if (!insn.handler) {
			error("CP @%08X: invalid instruction [%08X]", m_s.pc, inst[0]);
			end_open_mesh();
			m_s.running = 0;
			budget--;
			break;
		}

		/* A vertex opens a mesh; anything that is not part of a
		 * mesh closes it. */
		if (!m_s.in_mesh && (insn.flags & OPENS_MESH)) {
			begin_mesh(m_s.pc);
			m_s.in_mesh = 1;
		} else if (m_s.in_mesh && !(insn.flags & KEEPS_MESH)) {
			end_mesh(m_s.pc);
			m_s.in_mesh = 0;
		}

		if (m_disasm) {
			bool unhandled = false;
			const std::string text =
				antarctic_disassemble(m_s.pc, inst, m_s.in_mesh != 0, unhandled);

			std::fprintf(m_disasm, "%s\n", text.c_str());
			if (unhandled)
				error("CP @%08X : unhandled instruction", m_s.pc);
		}

		(this->*insn.handler)(inst);

		if (!(insn.flags & SETS_PC))
			m_s.pc += insn_size(inst[0]);

		budget--;
	}

	if (!m_s.running && m_disasm)
		std::fputs(" ==== CP END ==== \n", m_disasm);

	return !m_s.running;
}

/* The call stack, in the command RAM */

void
Antarctic::push_pc()
{
	std::memcpy(&m_cmdram[stack_offset()], &m_s.pc, 4);
	m_s.sp[0] -= 4;
}

void
Antarctic::pop_pc()
{
	m_s.sp[0] += 4;
	std::memcpy(&m_s.pc, &m_cmdram[stack_offset()], 4);
	m_s.pc += 8;
}

/* Where the stack pointer is in the command RAM, the only place for it
 * known; anywhere else stops Odin */
uint32_t
Antarctic::stack_offset() const
{
	const uint32_t offs = m_s.sp[0] & 0x00FFFFFF;

	if ((m_s.sp[0] >> 24) != 0x48 || offs + 4 > m_cmdram.size())
		fatal("stack out of the command RAM", m_s.sp[0]);
	return offs;
}

} /* namespace odin */

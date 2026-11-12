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

#ifndef ODIN_HIKARU_GPU_HPP
#define ODIN_HIKARU_GPU_HPP

#include <cstdint>
#include <span>
#include <string>

#include "odin/machine/chips/antarctic/antarctic.hpp"
#include "odin/machine/chips/eurasia/eurasia.hpp"

namespace odin {

/* The graphics chips of the Hikaru and their wiring.
 *
 * All of them are SEGA parts. Two are seen on the bus:
 *
 *  - the command processor, 315-6083 "Antarctic" (antarctic.hpp), at
 *    15000000 for the master SH-4, PCI 17C3:11DB on the side of the slave.
 *    It executes the display lists the games leave in the command RAM
 *    (48000000 on the external bus, 14000000 for the master SH-4, 10000000
 *    for the slave) and moves their textures into the texture RAM;
 *
 *  - the display chip, 315-6087 "Eurasia" (eurasia.hpp), at 1A000000, PCI
 *    17C7:11DB at 18001000: the screen, the 2D layers and a blitter in the
 *    framebuffer RAM.
 *
 * Between them on the board sit the chips that plot the polygons (315-6086
 * "Africa", two 315-6197 "Atlantis", 315-6085 "America" with the two texture
 * RAM chips 315-6084 "Australia"): the renderer stands for them.
 *
 * HYPOTHESIS, nothing relies on it: there may be two command processors.
 * Many graphics chips of the board come in pairs, and many registers have
 * a mirror filled with the same values plus an offset, as 150000{18+,38+}
 * and 1500007{4,8}.
 *
 * There are two main boards, 837-13402 and 837-14097: the bootrom tells them
 * apart with bit 0 of register 15002000, see Antarctic::read_register.
 *
 * The wiring done here: bit 7 of 15000088 follows the causes of the display
 * chip; the end of a list ends the plot too and clears the busy bit of
 * 1A000024; a write to 1A000024 while no list is asked clears that bit at
 * once (it never starts a list again); the two reset lines. */
class HikaruGpu {
public:
	/* The memories the chips work on */
	struct Memory {
		std::span<uint8_t> cmdram;	/* command RAM, 4 MB */
		std::span<uint8_t> ram_s;	/* RAM of the slave SH-4, 32 MB */
		std::span<uint8_t> fb;		/* framebuffer RAM, 8 MB */
		std::span<uint8_t> texram[2];	/* texture RAM, two banks of 4 MB */
	};
	/* What the board is told */
	struct Hooks {
		void *ctx;
		void (*irq_changed)(void *ctx);		/* the interrupt line may have moved */
		void (*list_started)(void *ctx);	/* a display list was launched */
		/* a rectangle of texels of a bank is about to be written */
		Antarctic::TexelsWritten before_texture_write;
		/* bytes of the framebuffer RAM a blit wrote */
		Eurasia::FbWritten fb_written = nullptr;
	};

	HikaruGpu(const Memory &memory, const Hooks &hooks);

	void reset();

	/* The reset lines of the chips, on port D of the MIE; true while a
	 * line is low. GP_RES holds every chip, PCI1_RES the display chip. A
	 * chip goes back to its power-on state when its line falls and does
	 * nothing until the line rises. */
	void set_reset_lines(bool gp_res, bool pci1_res);

	/* The bus: size is 2 or 4 bytes. False for a register that is not
	 * known. */
	bool read(uint32_t addr, unsigned size, uint32_t &value) const;
	bool write(uint32_t addr, unsigned size, uint32_t value);

	/* Runs the display list from now, one instruction a cycle of the
	 * SH-4, for cycles at most. True when it ends in them: cycles is then
	 * the time it took, at the end of which the board calls
	 * list_ended(). */
	bool run_list(uint32_t &cycles);
	/* The list has ended: the busy bits go off and the two chips tell
	 * that they are done. */
	void list_ended();

	/* The screen: the end of a line, which also moves the texture
	 * transfer by one entry, the two ends of the vertical blanking. */
	void end_of_line(uint32_t line);
	void vblank_in();
	void vblank_out();

	/* ANT_INT, the line of the command processor to the interrupt
	 * controller of the board. */
	bool irq_line() const { return m_antarctic.irq_line(); }

	Antarctic &antarctic() { return m_antarctic; }
	Eurasia &eurasia() { return m_eurasia; }

	/* Everything the chips keep, the word at 00400000 with them. Plain
	 * data. */
	struct State {
		AntarcticState antarctic;
		EurasiaState eurasia;
		uint32_t unknown_00400000;
	};
	void save(State &out) const;
	void load(const State &in);

	/* Where the command processor is, in a line, for the logs */
	std::string debug_string() const;

private:
	bool display_held() const { return m_gp_res || m_pci1_res; }
	void update_irq();
	void list_control_written(bool launch);

	Antarctic m_antarctic;
	Eurasia m_eurasia;
	Hooks m_hooks;
	uint16_t m_00400000;
	bool m_gp_res;
	bool m_pci1_res;
};

} /* namespace odin */

#endif /* ODIN_HIKARU_GPU_HPP */

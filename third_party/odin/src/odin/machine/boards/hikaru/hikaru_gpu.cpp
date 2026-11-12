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

#include "odin/machine/boards/hikaru/hikaru_gpu.hpp"

#include <cstdio>

namespace odin {

HikaruGpu::HikaruGpu(const Memory &memory, const Hooks &hooks)
	: m_antarctic(memory.cmdram, memory.ram_s, memory.texram[0], memory.texram[1]),
	  m_eurasia(memory.fb),
	  m_hooks(hooks),
	  m_00400000(0),
	  m_gp_res(false),
	  m_pci1_res(false)
{
	m_antarctic.set_before_texture_write(m_hooks.before_texture_write, m_hooks.ctx);
	m_eurasia.set_fb_written(m_hooks.fb_written, m_hooks.ctx);
}

void
HikaruGpu::save(State &out) const
{
	out.antarctic = m_antarctic.state();
	out.eurasia = m_eurasia.state();
	out.unknown_00400000 = m_00400000;
}

void
HikaruGpu::load(const State &in)
{
	m_antarctic.load(in.antarctic);
	m_eurasia.load(in.eurasia);
	m_00400000 = static_cast<uint16_t>(in.unknown_00400000);
}

std::string
HikaruGpu::debug_string() const
{
	char out[256];
	uint32_t status = 0, busy = 0, causes = 0;
	/* 15000058 and 15000084 are not readable on the bus */
	const uint32_t control = m_antarctic.state().regs[0x58 / 4];
	const uint32_t mask = m_antarctic.state().regs[0x84 / 4];

	m_antarctic.read_register(0x15000088, status);
	m_eurasia.read(0x1A000024, busy);
	m_eurasia.read(0x1A000018, causes);

	std::snprintf(out, sizeof(out), "@%08X %u 15:58=%u 1A:24=%u 15:84=%X 15:88=%X 1A:18=%X",
	              m_antarctic.pc(), m_antarctic.running() ? 1u : 0u, control, busy, mask, status, causes);
	return out;
}

void
HikaruGpu::reset()
{
	m_antarctic.reset();
	m_eurasia.reset();
}

/* The names are those of the schematics, as MAME gives them (hikaru.cpp, MIE
 * GPIO ports): GP_RES, "all GPU ASICs reset", and PCI1_RES, "Eurasia". VON4
 * pulses both when two lists in a row did not end (@0C711C30): PCI1_RES for
 * a few instructions, then GP_RES for a loop of 524288 turns. The slave then
 * sets the PCI side of the command processor again, the master the display
 * chip and the registers. */
void
HikaruGpu::set_reset_lines(bool gp_res, bool pci1_res)
{
	const bool chips_fell = gp_res && !m_gp_res;
	const bool display_fell = pci1_res && !m_pci1_res;

	m_gp_res = gp_res;
	m_pci1_res = pci1_res;

	if (chips_fell)
		m_antarctic.reset();
	if (chips_fell || display_fell) {
		m_eurasia.reset();
		update_irq();
	}
}

/* GPU IRQs
 * ========
 *
 * IRQs are signalled to the main SH-4 through IRL2: priority 7, INTEVT 0x300,
 * vector 0x220. At the same time, bit 6 of the master SH-4 Port A is low
 * (see Hikaru::update_irqs).
 *
 * The two registers 15000088 and 1A000018 signal 8 and 4 different IRQs
 * causes, respectively. When any of the IRQs in 1A000018 is raised, bit 7 of
 * 15000088 is set. 15000084 is the IRQ mask register, and is applied to
 * 15000088: a cause whose bit is SET there does not interrupt. The system
 * library (PHARRIER @0C010AB6, the same in the BOOTROM) writes 0x72 to
 * 15000088, which clears the causes 0, 2, 3 and 7, then 0x72 to 15000084:
 * it clears what it lets through.
 *
 * Bits in 15000088:
 *
 *  01, bit 0 = GPU 15 indirect DMA done
 *  02, bit 1 = an error
 *  04, bit 2 = GPU 15 command analysis done
 *  08, bit 3 = an error
 *  10, bit 4 = an error
 *  20, bit 5 = an error
 *  40, bit 6 = an error
 *  80, bit 7 = GPU 1A
 *
 * PHARRIER logs bits 1, 3, 4, 5 and 6 as errors at each interrupt
 * (@0C01B020, mask 0x7A) and then goes through a recovery that writes 0x02
 * to 15000084 (@0C012752). None of them is raised here.
 *
 * Bit 7 follows the causes of 1A000018: the HEURISTIC at
 * Antarctic::set_display_irq.
 *
 * Bits in 1A000018:
 *
 *  01, bit 0 = Unknown
 *  02, bit 1 = Vblank[1]
 *  04, bit 2 = GPU 1A plot done
 *  08, bit 3 = Unknown
 *
 * When raised, they set bit 7 in 15000088.
 *
 * [1] This bit is checked at @0C001C08 and updates (0, GBR) and implies
 *     1A000000 = 1.
 */

void
HikaruGpu::update_irq()
{
	m_antarctic.set_display_irq(m_eurasia.irq());
	m_hooks.irq_changed(m_hooks.ctx);
}

/*
 * Command Processor
 * =================
 *
 * A list starts when 15000058 = 3; see
 * src/odin/machine/chips/antarctic/antarctic.hpp for what it does. When it
 * ends (list_ended): bit 0 of 1A000024, the plot busy, is cleared; bit 2 of
 * 1A000018 (plot done) is raised, and with it bit 7 of 15000088; bit 2 of
 * 15000088 (analysis done) is raised; a GPU IRQ follows unless 15000084 masks
 * them.
 */

/* 15000058 or 1A000024 was written. 15000058 = 3 launches the list;
 * 1A000024 does not launch it again: BRAVEFF writes it the instruction after
 * 15000058, and a launch by both would run its list twice; SGNASCAR writes it
 * the other way round. */
void
HikaruGpu::list_control_written(bool launch)
{
	if (m_antarctic.list_asked()) {
		if (launch) {
			m_antarctic.start_list();
			m_hooks.list_started(m_hooks.ctx);
		}
	} else {
		/* 1A000024 written with no list asked: nothing to wait for.
		 * SGNASCAR writes it before 15000058, so it does not read busy
		 * during its lists (HEURISTIC, as Valkyrie had it). */
		m_eurasia.set_busy(0);
	}
}

void
HikaruGpu::list_ended()
{
	m_antarctic.list_ended();
	m_eurasia.set_busy(m_eurasia.busy() & ~1u);

	m_eurasia.raise(Eurasia::IRQ_PLOT_END);
	m_antarctic.raise(Antarctic::IRQ_ANALYSIS_END);
	update_irq();
}

bool
HikaruGpu::run_list(uint32_t &cycles)
{
	uint32_t left = cycles;

	if (m_gp_res || !m_antarctic.list_asked() || !m_antarctic.run(left))
		return false;
	cycles -= left;
	return true;
}

void
HikaruGpu::end_of_line(uint32_t line)
{
	if (!m_gp_res && m_antarctic.step_texture_transfer())
		update_irq();

	if (display_held())
		return;

	m_eurasia.end_of_line(line);
}

void
HikaruGpu::vblank_in()
{
	if (!display_held())
		m_eurasia.vblank_in();
}

void
HikaruGpu::vblank_out()
{
	if (display_held())
		return;

	m_eurasia.vblank_out();
	update_irq();
}

/* The bus. The command processor is at 15000000, the display chip at
 * 1A000000 with its PCI configuration at 18001000. */

bool
HikaruGpu::read(uint32_t addr, unsigned size, uint32_t &value) const
{
	bool known;

	if (addr == 0x00400000) {
		value = m_00400000;
		return true;
	}

	if ((addr >> 24) == 0x15)
		known = m_antarctic.read_register(addr, value);
	else
		known = m_eurasia.read(addr, value);

	if (size == 2)
		value &= 0xFFFF;
	return known;
}

bool
HikaruGpu::write(uint32_t addr, unsigned, uint32_t value)
{
	if (addr == 0x00400000) {
		m_00400000 = static_cast<uint16_t>(value);
		return true;
	}

	if ((addr >> 24) == 0x15) {
		switch (m_antarctic.write_register(addr, value)) {
		case Antarctic::Written::UNKNOWN:
			return false;
		case Antarctic::Written::CONTROL:
			list_control_written(true);
			break;
		case Antarctic::Written::CAUSES:
			update_irq();
			break;
		case Antarctic::Written::DONE:
			break;
		}
		return true;
	}

	switch (m_eurasia.write(addr, value)) {
	case Eurasia::Written::UNKNOWN:
		return false;
	case Eurasia::Written::BUSY:
		list_control_written(false);
		break;
	case Eurasia::Written::CAUSES:
		update_irq();
		break;
	case Eurasia::Written::DONE:
		break;
	}
	return true;
}

} /* namespace odin */

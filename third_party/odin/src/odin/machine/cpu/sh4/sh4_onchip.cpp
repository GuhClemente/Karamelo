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

/* SH-4 on-chip modules: the registers at FF000000 (and 1F000000), with the
 * interrupt controller, the DMA controller, the timers, port A, the
 * performance counters and the store queue addresses. A register that
 * does nothing here only keeps what is written to it.
 *
 * Addresses: SH7750 hardware manual, appendix A. The registers live in a
 * 64 KB image: bits 16-23 and 0-7 of the address. */

#include "odin/machine/cpu/sh4/sh4.hpp"
#include "odin/machine/cpu/sh4/sh4_onchip.hpp"
#include "odin/common/bytes.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace odin {

namespace {

/* time / period, avoiding a variable division for common periods: a game may
 * read a timer thousands of times a frame. */
uint64_t periods(uint64_t time, uint64_t period)
{
	const unsigned shift = static_cast<unsigned>(std::countr_zero(period));
	const uint64_t odd = period >> shift;

	return odd == 1 ? time >> shift : odd == 3 ? (time >> shift) / 3 : time / period;
}

/* Bytes per transfer for CHCR.TS */
constexpr uint32_t DMAC_UNIT[8] = {8, 1, 2, 4, 32, 0, 0, 0};

/* The dividers of the CPU and bus clocks in FRQCR, IFC and BFC (section 10.2) */
constexpr uint8_t FRQCR_DIVIDER[8] = { 1, 2, 3, 4, 6, 8, 0, 0 };

} /* namespace */

uint64_t Sh4::reg_get(uint32_t size, uint32_t addr) const
{
	uint32_t at = shrink(addr);
	uint64_t value = 0;

	if (at + size > m_onchip.size()) {
		fatal("on-chip register read past the image", addr);
	}
	for (uint32_t i = 0; i < size; i++) {
		value |= static_cast<uint64_t>(m_onchip[at + i]) << (8 * i);
	}
	return value;
}

void Sh4::reg_put(uint32_t size, uint32_t addr, uint64_t value)
{
	uint32_t at = shrink(addr);

	if (at + size > m_onchip.size()) {
		fatal("on-chip register write past the image", addr);
	}
	for (uint32_t i = 0; i < size; i++) {
		m_onchip[at + i] = static_cast<uint8_t>(value >> (8 * i));
	}
}

void Sh4::onchip_reset(bool hard)
{
	/* The level of the NMI pin is not the chip's to reset. */
	const uint64_t nmil = reg_get(2, INTC_ICR) & ICR_NMIL;

	std::fill(m_onchip.begin(), m_onchip.end(), uint8_t{0});
	reg_put(2, INTC_ICR, nmil);

	reg_put(4, CCN_EXPEVT, hard ? 0 : 0x20);
	/* HEURISTIC: bit 31 alone, endian, whichever CPU it is. The master
	 * also has bit 30 set on the chip (BCR1.MASTER). */
	reg_put(4, BSC_BCR1, (m_master || m_little_endian) ? 0x80000000 : 0);
	reg_put(2, BSC_BCR2, 0x3FFC);
	reg_put(4, BSC_WCR1, 0x77777777);
	reg_put(4, BSC_WCR2, 0xFFFEEFFF);
	reg_put(4, BSC_WCR3, 0x07777777);
	/* Hardware manual, table 10.3 (1). */
	static constexpr uint16_t FRQCR_OF_MODE[6] = { 0x0E1A, 0x0E23, 0x0E13, 0x0E13, 0x0E0A, 0x0E0A };
	if (m_clock_mode >= 6) {
		fatal("clock operating mode that is not implemented", m_clock_mode);
	}
	reg_put(2, CPG_FRQCR, FRQCR_OF_MODE[m_clock_mode]);
	for (uint32_t ch = 0; ch < 3; ch++) {
		reg_put(4, tcor(ch), 0xFFFFFFFF);
		reg_put(4, tcnt(ch), 0xFFFFFFFF);
	}
	reg_put(1, SCI_SCBRR1, 0xFF);
	reg_put(1, SCI_SCTDR1, 0xFF);
	reg_put(1, SCI_SCSSR1, 0x84);
	reg_put(1, SCIF_SCBRR2, 0xFF);
	reg_put(2, SCIF_SCFSR2, 0x0060);
	reg_put(2, UDI_SDIR, 0xFFFF);
}

/* Port A: PCTRA has two bits per pin, the low one set for an output. */

uint16_t Sh4::port_a_get()
{
	uint32_t pins = static_cast<uint32_t>(reg_get(2, BSC_PDTRA));
	uint32_t pctra = static_cast<uint32_t>(reg_get(4, BSC_PCTRA));

	outside();
	uint32_t wires = m_bus.port_a_read();

	for (uint32_t i = 0; i < 16; i++) {
		if (!((pctra >> (i * 2)) & 1)) {
			pins = (pins & ~(1u << i)) | (wires & (1u << i));
		}
	}
	return static_cast<uint16_t>(pins);
}

/* The machine is given the pins as the chip leaves them: one that is not an
 * output is high, pulled up (the pull-ups are on at reset; HEURISTIC for a
 * pin whose pull-up is off). */
void Sh4::port_a_put(uint16_t value)
{
	uint32_t pins = 0xFFFF;
	uint32_t pctra = static_cast<uint32_t>(reg_get(4, BSC_PCTRA));

	for (uint32_t i = 0; i < 16; i++) {
		if (((pctra >> (i * 2)) & 1) && !(value & (1u << i))) {
			pins &= ~(1u << i);
		}
	}
	outside();
	m_bus.port_a_write(static_cast<uint16_t>(pins));
}

uint16_t Sh4::port_a_outputs() const
{
	const uint32_t pctra = static_cast<uint32_t>(reg_get(4, BSC_PCTRA));
	uint16_t outputs = 0;

	for (uint32_t i = 0; i < 16; i++)
		if ((pctra >> (i * 2)) & 1)
			outputs = static_cast<uint16_t>(outputs | 1u << i);
	return outputs;
}

/* Interrupt controller: the priorities of the on-chip sources (table
 * 19.5). */

void Sh4::set_irq_priority(uint32_t source, uint32_t priority)
{
	if (m_intc.source[source].raised && m_intc.source[source].priority > priority) {
		fatal("priority of a raised interrupt lowered", source);
	}
	m_intc.source[source].priority = priority;
}

void Sh4::update_irq_priorities()
{
	uint32_t ipra = static_cast<uint32_t>(reg_get(2, INTC_IPRA));
	uint32_t iprb = static_cast<uint32_t>(reg_get(2, INTC_IPRB));
	uint32_t iprc = static_cast<uint32_t>(reg_get(2, INTC_IPRC));

	set_irq_priority(Sh4Source::UDI, iprc & 15);
	set_irq_priority(Sh4Source::GPIOI, (iprc >> 12) & 15);
	for (uint32_t s = Sh4Source::DMTE0; s <= Sh4Source::DMAE; s++) {
		set_irq_priority(s, (iprc >> 8) & 15);
	}
	set_irq_priority(Sh4Source::TUNI0, (ipra >> 12) & 15);
	set_irq_priority(Sh4Source::TUNI1, (ipra >> 8) & 15);
	set_irq_priority(Sh4Source::TUNI2, (ipra >> 4) & 15);
	set_irq_priority(Sh4Source::TICPI2, (ipra >> 4) & 15);
	for (uint32_t s = Sh4Source::ATI; s <= Sh4Source::CUI; s++) {
		set_irq_priority(s, ipra & 15);
	}
	for (uint32_t s = Sh4Source::ERI; s <= Sh4Source::TEI; s++) {
		set_irq_priority(s, (iprb >> 4) & 15);
	}
	for (uint32_t s = Sh4Source::ERIF; s <= Sh4Source::TXIF; s++) {
		set_irq_priority(s, (iprc >> 4) & 15);
	}
	set_irq_priority(Sh4Source::ITI, (iprb >> 12) & 15);
	set_irq_priority(Sh4Source::RCMI, (iprb >> 8) & 15);
	set_irq_priority(Sh4Source::ROVI, (iprb >> 8) & 15);

	update_irqs();
}

/* DMA controller. HEURISTIC: a channel that may run moves its whole count
 * at once, within the write that starts it. DDT mode, the request source
 * and the channel priorities are not looked at. */

uint64_t Sh4::dmac_get(uint32_t addr, uint32_t size)
{
	const uint8_t *p = m_rd[addr >> PAGE_BITS];
	uint64_t value = 0;

	if (p && !(addr & (size - 1)))
		return load_bytes(p + (addr & (PAGE_SIZE - 1)), size);
	m_touched = true;
	if (!read_any(addr, size, value)) {
		value = 0;
		m_bus.unanswered(false, addr, size, 0);
	}
	return value;
}

void Sh4::dmac_put(uint32_t addr, uint32_t size, uint64_t value)
{
	uint8_t *p = m_wr[addr >> PAGE_BITS];

	if (!p) {
		p = unwatch(addr);
	}
	m_touched = true;
	if (p && !(addr & (size - 1))) {
		store_bytes(p + (addr & (PAGE_SIZE - 1)), size, value);
		return;
	}
	if (!write_any(addr, size, value)) {
		m_bus.unanswered(true, addr, size, value);
	}
}

/* Up to count units from sar to dar, both going up, where each side is a
 * memory read or written in place: a page of the CPU, or, for one side at
 * most, what the bus gives. The bytes go in the order of dmac_get() and
 * dmac_put(), unit after unit; the bus is told of its side before the
 * first of them, which is what each access there would tell it again.
 * Both addresses are on a whole unit (dmac_update_channel): no unit
 * straddles two pages. Returns how many units it moved, 0 for none. */
uint32_t Sh4::dmac_copy(uint32_t sar, uint32_t dar, uint32_t unit, uint32_t count)
{
	const uint8_t *rp = m_rd[sar >> PAGE_BITS];
	uint8_t *wp = m_wr[dar >> PAGE_BITS];

	if (!wp) {
		wp = unwatch(dar);
	}
	std::span<const uint8_t> from;
	std::span<uint8_t> to;

	/* Spans of the bus on both sides would tell of the writes before
	 * the first unit is read. */
	if (!rp && !wp) {
		return 0;
	}
	if (rp) {
		from = {rp + (sar & (PAGE_SIZE - 1)), PAGE_SIZE - (sar & (PAGE_SIZE - 1))};
	} else if (sar < 0xE0000000 && (sar >> 24) != 0x1F) {
		from = m_bus.read_span(sar & PHYSICAL);
	}
	uint32_t n = static_cast<uint32_t>(std::min<size_t>(count, from.size() / unit));
	if (n == 0) {
		return 0;
	}
	if (wp) {
		to = {wp + (dar & (PAGE_SIZE - 1)), PAGE_SIZE - (dar & (PAGE_SIZE - 1))};
	} else if (dar < 0xE0000000 && (dar >> 24) != 0x1F) {
		to = m_bus.write_span(dar & PHYSICAL);
	}
	n = static_cast<uint32_t>(std::min<size_t>(n, to.size() / unit));
	if (n == 0) {
		return 0;
	}

	const size_t bytes = static_cast<size_t>(n) * unit;
	const uintptr_t a = reinterpret_cast<uintptr_t>(from.data()), b = reinterpret_cast<uintptr_t>(to.data());

	m_touched = true;
	if (a + bytes <= b || b + bytes <= a) {
		std::memcpy(to.data(), from.data(), bytes);
	} else {
		/* Over itself: a piece at a time, as dmac_get() and dmac_put() */
		const size_t piece = unit < 8 ? unit : 8;

		for (size_t i = 0; i < bytes; i += piece) {
			uint64_t value;

			std::memcpy(&value, from.data() + i, piece);
			std::memcpy(to.data() + i, &value, piece);
		}
	}
	if (!wp) {
		m_bus.span_written(dar & PHYSICAL, bytes);
	}
	return n;
}

void Sh4::dmac_run_channel(uint32_t ch)
{
	uint32_t offs = ch * 0x10;

	/* It writes the memory behind the back of the blocks. */
	outside();

	uint32_t sar = static_cast<uint32_t>(reg_get(4, DMAC_SAR0 + offs));
	uint32_t dar = static_cast<uint32_t>(reg_get(4, DMAC_DAR0 + offs));
	uint32_t tcr = static_cast<uint32_t>(reg_get(4, DMAC_TCR0 + offs));
	uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));
	uint32_t unit = DMAC_UNIT[(chcr >> 4) & 7];
	uint32_t sm = (chcr >> 12) & 3, dm = (chcr >> 14) & 3;

	if (unit == 0) {
		fatal("DMAC transfer size", chcr);
	}
	for (uint32_t budget = 0x7FFFFFFF; budget > 0 && tcr > 0;) {
		uint32_t n = (sm == 1 && dm == 1) ? dmac_copy(sar, dar, unit, std::min(budget, tcr)) : 0;

		if (n > 0) {
			sar += n * unit;
			dar += n * unit;
			budget -= n;
			tcr -= n;
			continue;
		}
		if (sm == 2) {
			sar -= unit;
		}
		if (dm == 2) {
			dar -= unit;
		}
		if (unit == 32) {
			for (uint32_t i = 0; i < 32; i += 8) {
				dmac_put(dar + i, 8, dmac_get(sar + i, 8));
			}
		} else {
			dmac_put(dar, unit, dmac_get(sar, unit));
		}
		if (sm == 1) {
			sar += unit;
		}
		if (dm == 1) {
			dar += unit;
		}
		budget--;
		tcr--;
	}

	reg_put(4, DMAC_SAR0 + offs, sar);
	reg_put(4, DMAC_DAR0 + offs, dar);
	reg_put(4, DMAC_TCR0 + offs, tcr);

	if (tcr == 0) {
		chcr |= 2;	/* TE */
		m_dmac_running[ch] = false;
		reg_put(4, DMAC_CHCR0 + offs, chcr);
		dmac_update_irqs();
	}
}

/* DMTE is asked for while a channel has TE and IE, DMAE while DMAOR has
 * AE (section 14.2). */
void Sh4::dmac_update_irqs()
{
	for (uint32_t ch = 0; ch < 4; ch++) {
		const uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + ch * 0x10));
		set_irq(Sh4Source::DMTE0 + ch, (chcr & 6) == 6);
	}
	set_irq(Sh4Source::DMAE, (reg_get(4, DMAC_DMAOR) & 4) != 0);
}

void Sh4::dmac_run()
{
	for (uint32_t ch = 0; ch < 4; ch++) {
		if (m_dmac_running[ch]) {
			dmac_run_channel(ch);
		}
	}
}

/* A channel runs when DMAOR.DME and CHCR.DE are set, no error or end flag
 * is, and its request source is of the kind asked: 1 for auto-request. */
void Sh4::dmac_update_channel(uint32_t ch, uint32_t request)
{
	uint32_t offs = ch * 0x10;
	uint32_t dmaor = static_cast<uint32_t>(reg_get(4, DMAC_DMAOR));
	uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));

	m_dmac_running[ch] = false;
	if (!(dmaor & chcr & 1)) {
		return;
	}

	uint32_t sar = static_cast<uint32_t>(reg_get(4, DMAC_SAR0 + offs));
	uint32_t dar = static_cast<uint32_t>(reg_get(4, DMAC_DAR0 + offs));
	uint32_t ts = (chcr >> 4) & 7, rs = (chcr >> 8) & 15;
	uint32_t sm = (chcr >> 12) & 3, dm = (chcr >> 14) & 3;

	if (ts >= 5 || sm == 3 || dm == 3 || rs == 1 || rs == 7 || rs == 15) {
		fatal("DMAC channel setting", chcr);
	}
	if ((sar | dar) & (DMAC_UNIT[ts] - 1)) {
		reg_put(4, DMAC_DMAOR, dmaor | 4);	/* AE */
		dmac_update_irqs();
		return;
	}
	/* NMIF, AE or TE */
	if ((dmaor & 6) || (chcr & 2)) {
		return;
	}
	if ((rs >> 2) == request) {
		m_dmac_running[ch] = true;
	}
}

void Sh4::dmac_update(uint32_t request)
{
	for (uint32_t ch = 0; ch < 4; ch++) {
		dmac_update_channel(ch, request);
	}
}

/* External requests in single address mode (hardware manual, 14.3.2):
 * the device on DACK gives or takes each unit, the memory side is DAR (RS
 * 0011) or SAR (RS 0010); one unit a request, by cycle steal; TE and DMTE
 * as for the other modes when the count runs out. HEURISTIC: the unit
 * moves at once, its bus cycles not counted. Units of 1, 2 and 4 bytes. */
bool Sh4::dreq_ready(uint32_t ch) const
{
	if (ch >= 4) {
		return false;
	}

	const uint32_t offs = ch * 0x10;
	const uint32_t dmaor = static_cast<uint32_t>(reg_get(4, DMAC_DMAOR));
	const uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));
	const uint32_t rs = (chcr >> 8) & 15, ts = (chcr >> 4) & 7;

	/* DME and DE; neither NMIF, AE nor TE */
	return (dmaor & chcr & 1) && !(dmaor & 6) && !(chcr & 2) && (rs == 2 || rs == 3) && ts >= 1 && ts <= 3 &&
	       reg_get(4, DMAC_TCR0 + offs) != 0;
}

/* The unit has moved: one fewer to go, the end told when none is left */
static void dreq_counted(uint32_t &tcr, uint32_t &chcr)
{
	tcr--;
	if (tcr == 0) {
		chcr |= 2;	/* TE */
	}
}

void Sh4::dreq_to_memory(uint32_t ch, uint32_t value)
{
	if (!dreq_ready(ch) || ((reg_get(4, DMAC_CHCR0 + ch * 0x10) >> 8) & 15) != 3) {
		return;
	}
	/* It writes the memory behind the back of the blocks. */
	outside();

	const uint32_t offs = ch * 0x10;
	uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));
	uint32_t dar = static_cast<uint32_t>(reg_get(4, DMAC_DAR0 + offs));
	uint32_t tcr = static_cast<uint32_t>(reg_get(4, DMAC_TCR0 + offs));
	const uint32_t unit = DMAC_UNIT[(chcr >> 4) & 7], dm = (chcr >> 14) & 3;

	if (dm == 2) {
		dar -= unit;
	}
	dmac_put(dar, unit, unit == 4 ? value : value & ((1u << (8 * unit)) - 1));
	if (dm == 1) {
		dar += unit;
	}
	dreq_counted(tcr, chcr);
	reg_put(4, DMAC_DAR0 + offs, dar);
	reg_put(4, DMAC_TCR0 + offs, tcr);
	reg_put(4, DMAC_CHCR0 + offs, chcr);
	dmac_update_irqs();
}

std::optional<uint32_t> Sh4::dreq_from_memory(uint32_t ch)
{
	if (!dreq_ready(ch) || ((reg_get(4, DMAC_CHCR0 + ch * 0x10) >> 8) & 15) != 2) {
		return std::nullopt;
	}

	const uint32_t offs = ch * 0x10;
	uint32_t chcr = static_cast<uint32_t>(reg_get(4, DMAC_CHCR0 + offs));
	uint32_t sar = static_cast<uint32_t>(reg_get(4, DMAC_SAR0 + offs));
	uint32_t tcr = static_cast<uint32_t>(reg_get(4, DMAC_TCR0 + offs));
	const uint32_t unit = DMAC_UNIT[(chcr >> 4) & 7], sm = (chcr >> 12) & 3;

	if (sm == 2) {
		sar -= unit;
	}

	const uint32_t value = static_cast<uint32_t>(dmac_get(sar, unit));

	if (sm == 1) {
		sar += unit;
	}
	dreq_counted(tcr, chcr);
	reg_put(4, DMAC_SAR0 + offs, sar);
	reg_put(4, DMAC_TCR0 + offs, tcr);
	reg_put(4, DMAC_CHCR0 + offs, chcr);
	dmac_update_irqs();
	return value;
}

/* Timers (hardware manual, section 12). A channel counts down on the
 * peripheral clock divided by 4, 16, 64, 256 or 1024 (TCR.TPSC). The count
 * after zero is the underflow: it sets TCR.UNF, takes TCOR again and, with
 * TCR.UNIE, raises the interrupt. One divider feeds every channel and
 * never stops: a count falls on a multiple of its period (HEURISTIC: the
 * divider is taken to start with the reset; the manual does not say). */

/* The cycles of the CPU clock in one of the peripheral clock: both are
 * divided from the same clock by FRQCR (section 10.2). */
uint32_t Sh4::pck_cycles() const
{
	static constexpr uint8_t PFC[8] = { 2, 3, 4, 6, 8, 0, 0, 0 };
	/* The peripheral divider over the CPU's, 0 where it is not whole */
	static constexpr auto RATIO = [] {
		std::array<std::array<uint8_t, 8>, 8> ratio{};

		for (uint32_t i = 0; i < 8; i++) {
			for (uint32_t p = 0; p < 8; p++) {
				if (FRQCR_DIVIDER[i] && PFC[p] && PFC[p] % FRQCR_DIVIDER[i] == 0) {
					ratio[i][p] = static_cast<uint8_t>(PFC[p] / FRQCR_DIVIDER[i]);
				}
			}
		}
		return ratio;
	}();
	const uint32_t frqcr = static_cast<uint32_t>(reg_get(2, CPG_FRQCR));
	const uint32_t ratio = RATIO[(frqcr >> 6) & 7][frqcr & 7];

	if (ratio == 0) {
		fatal("FRQCR with clock ratios that are not implemented", frqcr);
	}
	return ratio;
}

/* The CPU cycles a read or write of the CPU at addr takes at least on the
 * MPX bus (BCR1: A0MPX for area 0, MEMMPX for areas 1 to 6 that are not
 * DRAM, SDRAM or PCMCIA by DRAMTP and A56PCM); 0 for any other area. An
 * address phase, the waits of WCR2 (table 13.6: a read has one more at
 * AnW 0 and 4), one data phase (section 13.3.8, figures 13.58 and 13.60),
 * in bus clocks (FRQCR, section 10.2).
 * HEURISTIC: the waits the device adds by RDY are not known and taken as
 * none, and a 64-bit access as one data phase (BCR2 sets a bus width the
 * manual calls reserved). */
uint32_t Sh4::mpx_cycles(uint32_t addr, bool write) const
{
	static constexpr uint8_t READ_WAITS[8] = { 1, 1, 2, 3, 1, 1, 2, 3 };
	static constexpr uint8_t WRITE_WAITS[8] = { 0, 1, 2, 3, 0, 1, 2, 3 };
	static constexpr uint8_t ANW_SHIFT[7] = { 3, 7, 10, 0, 17, 23, 29 };
	const uint32_t area = (addr & PHYSICAL) >> 26;
	const uint32_t bcr1 = static_cast<uint32_t>(reg_get(4, BSC_BCR1));
	const uint32_t dramtp = (bcr1 >> 2) & 7;
	const bool memory = (area == 2 && (dramtp == 3 || dramtp == 6)) || area == 3 ||
	                    ((area == 5 || area == 6) && (bcr1 & 1));
	const bool mpx = area == 0 ? (bcr1 & (1u << 29)) != 0 : area < 7 && !memory && (bcr1 & (1u << 17)) != 0;

	if (!mpx) {
		return 0;
	}
	const uint32_t anw = (static_cast<uint32_t>(reg_get(4, BSC_WCR2)) >> ANW_SHIFT[area]) & 7;
	const uint32_t frqcr = static_cast<uint32_t>(reg_get(2, CPG_FRQCR));
	const uint32_t cpu = FRQCR_DIVIDER[(frqcr >> 6) & 7], bus = FRQCR_DIVIDER[(frqcr >> 3) & 7];

	if (cpu == 0 || bus == 0 || bus % cpu != 0) {
		fatal("FRQCR with clock ratios that are not implemented", frqcr);
	}
	return (2u + (write ? WRITE_WAITS[anw] : READ_WAITS[anw])) * (bus / cpu);
}

/* The cycles of one count of a channel. */
uint32_t Sh4::tmu_period(uint32_t ch) const
{
	const uint32_t tpsc = static_cast<uint32_t>(reg_get(2, tcr(ch))) & 7;

	if (tpsc > 4) {
		fatal("timer on the RTC or the external clock, not implemented", tpsc);
	}
	return pck_cycles() << (2 * tpsc + 2);
}

void Sh4::tmu_count(uint32_t ch, uint64_t counts)
{
	const uint64_t counter = m_tmu_counter[ch];

	if (counts <= counter) {
		m_tmu_counter[ch] = static_cast<uint32_t>(counter - counts);
		return;
	}

	const uint64_t reload = reg_get(4, tcor(ch));
	const uint32_t control = static_cast<uint32_t>(reg_get(2, tcr(ch)));

	/* Back at TCOR every TCOR + 1 counts. */
	counts -= counter + 1;
	m_tmu_counter[ch] = static_cast<uint32_t>(reload - counts % (reload + 1));
	reg_put(2, tcr(ch), control | 0x100);
	tmu_update_irq(ch);
}

/* TUNI is asked for while TCR has UNF and UNIE (section 12.2.6). */
void Sh4::tmu_update_irq(uint32_t ch)
{
	set_irq(Sh4Source::TUNI0 + ch, (reg_get(2, tcr(ch)) & 0x120) == 0x120);
}

/* Counts the timers up to `now`. */
void Sh4::tmu_sync(uint64_t now)
{
	if (now <= m_tmu_time) {
		return;
	}
	for (uint32_t ch = 0; ch < 3; ch++) {
		if (m_tmu_running[ch]) {
			const uint64_t period = tmu_period(ch);
			const uint64_t counts = periods(now, period) - periods(m_tmu_time, period);
			if (counts) {
				tmu_count(ch, counts);
			}
		}
	}
	m_tmu_time = now;
}

/* What TCNT of the channel would read at `when`, the timers left as they
 * are: tmu_sync() and tmu_count() for that channel, without their effects.
 * Counted up to a date in one go or in several, a channel ends the same. */
uint32_t Sh4::tmu_count_at(uint32_t ch, uint64_t when) const
{
	const uint64_t counter = m_tmu_counter[ch];

	if (!m_tmu_running[ch] || when <= m_tmu_time) {
		return static_cast<uint32_t>(counter);
	}
	const uint64_t period = tmu_period(ch);
	uint64_t counts = periods(when, period) - periods(m_tmu_time, period);

	if (counts <= counter) {
		return static_cast<uint32_t>(counter - counts);
	}
	const uint64_t reload = reg_get(4, tcor(ch));

	counts -= counter + 1;
	return static_cast<uint32_t>(reload - counts % (reload + 1));
}

uint64_t Sh4::tmu_counts_between(uint32_t ch, uint64_t from, uint64_t to) const
{
	if (!m_tmu_running[ch] || to <= from) {
		return 0;
	}
	const uint64_t period = tmu_period(ch);

	return periods(to, period) - periods(from, period);
}

/* The cycles from the start of the slice to the next underflow that raises
 * an interrupt. */
int32_t Sh4::tmu_next_irq() const
{
	uint64_t soonest = INT32_MAX;

	for (uint32_t ch = 0; ch < 3; ch++) {
		if (m_tmu_running[ch] && (reg_get(2, tcr(ch)) & 0x20)) {
			const uint64_t period = tmu_period(ch);
			const uint64_t date = (periods(m_tmu_time, period) + m_tmu_counter[ch] + 1) * period;
			soonest = std::min(soonest, std::max<uint64_t>(date - std::min(date, m_cycles), 1));
		}
	}
	return static_cast<int32_t>(soonest);
}

/* Store queues: bits 2-4 of QACR0 or QACR1 are the area of the target. */
uint32_t Sh4::store_queue_addr(uint32_t addr) const
{
	uint32_t qacr = static_cast<uint32_t>(reg_get(4, (addr & 0x20) ? CCN_QACR1 : CCN_QACR0));

	return ((qacr & 0x1C) << 24) | (addr & 0x03FFFFE0);
}

bool Sh4::onchip_read(uint32_t addr, uint32_t size, uint64_t &value)
{
	uint32_t reg = addr & 0xFFFFFF;
	auto sized = [&](uint32_t expected) {
		if (size != expected) {
			fatal("on-chip register read with the wrong size", addr);
		}
	};

	value = reg_get(size, addr);

	if (reg >= DMAC_SAR0 && reg <= DMAC_DMAOR) {
		sized(4);
		return true;
	}
	/* The serial ports that send at once: their transmitters are always
	 * empty */
	if (m_serial_at_once &&
	    ((reg >= SCI_SCBRR1 - 4 && reg < SCI_END) || (reg >= SCIF_SCBRR2 - 4 && reg < SCIF_END)))
		return true;
	switch (reg) {
	/* HEURISTIC: RFCR and WTCSR also read as port A. */
	case BSC_RFCR:
	case BSC_PDTRA:
	case CPG_WTCSR:
		sized(2);
		value = port_a_get();
		return true;
	case CCN_CCR:
	case CCN_INTEVT:
	case BSC_PCTRA:
		sized(4);
		return true;
	case INTC_IPRA:
	case INTC_IPRB:
	case INTC_IPRC:
	case PM_PMCR1:
	case PM_PMCR2:
		sized(2);
		return true;
	case TMU_TOCR:
	case TMU_TSTR:
		sized(1);
		return true;
	case tcor(0):
	case tcor(1):
	case tcor(2):
		sized(4);
		return true;
	case tcnt(0):
	case tcnt(1):
	case tcnt(2):
		sized(4);
		tmu_sync(m_at);
		value = m_tmu_counter[(reg - TMU_TCNT0) / 12];
		m_timer_read = true;
		return true;
	/* HEURISTIC: read as 32 bits, the register is the low half */
	case tcr(0):
	case tcr(1):
	case tcr(2):
		if (size != 4)
			sized(2);
		tmu_sync(m_at);
		value = reg_get(2, addr);
		return true;
	case PM_PMCTR1H:
		value = (m_pm_counter[0] >> 32) & 0xFFFF;
		return true;
	case PM_PMCTR1L:
		value = m_pm_counter[0] & 0xFFFFFFFF;
		return true;
	case PM_PMCTR2H:
		value = (m_pm_counter[1] >> 32) & 0xFFFF;
		return true;
	case PM_PMCTR2L:
		value = m_pm_counter[1] & 0xFFFFFFFF;
		return true;
	default:
		return false;
	}
}

bool Sh4::onchip_write(uint32_t addr, uint32_t size, uint64_t value)
{
	uint32_t reg = addr & 0xFFFFFF;
	uint32_t v = static_cast<uint32_t>(value);
	auto sized = [&](uint32_t expected) {
		if (size != expected) {
			fatal("on-chip register write with the wrong size", addr);
		}
	};
	auto only = [&](uint32_t allowed) {
		if (v & ~allowed) {
			fatal("on-chip register write with bits that are not implemented", addr);
		}
	};

	/* SDMR2 and SDMR3: the SDRAM mode is in the address. */
	if ((reg >= 0x900000 && reg <= 0x90FFFF) || (reg >= 0x940000 && reg <= 0x94FFFF)) {
		sized(1);
		reg_put(size, addr, value);
		return true;
	}
	if (m_serial_at_once && reg >= SCI_SCBRR1 - 4 && reg < SCI_END) {
		/* TDRE and TEND stay set */
		reg_put(size, addr, reg == SCI_SCSSR1 ? (v | 0x84) : v);
		return true;
	}
	if (m_serial_at_once && reg >= SCIF_SCBRR2 - 4 && reg < SCIF_END) {
		/* TEND and TDFE stay set */
		reg_put(size, addr, reg == SCIF_SCFSR2 ? (v | 0x60) : v);
		return true;
	}

	switch (reg) {
	case CPG_STBCR:
		sized(1);
		break;
	case BSC_BCR2:
	case BSC_PCR:
	case BSC_RTCSR:
	case BSC_RTCNT:
	case BSC_RTCOR:
	case BSC_RFCR:
	case CPG_WTCSR:
	case UBC_BBRA:
	case UBC_BBRB:
		sized(2);
		break;
	case BSC_PDTRA:
		sized(2);
		port_a_put(static_cast<uint16_t>(v));
		break;
	case CCN_MMUCR:
	case CCN_CCR:
	case CCN_QACR0:
	case CCN_QACR1:
	case BSC_BCR1:
	case BSC_WCR1:
	case BSC_WCR2:
	case BSC_WCR3:
	case BSC_MCR:
	case BSC_PCTRA:
		sized(4);
		break;

	case INTC_ICR: {
		uint32_t old = static_cast<uint32_t>(reg_get(2, addr));
		sized(2);
		/* HEURISTIC: reserved bits 13-10 are dropped */
		v &= ~0x3C00u;
		only(ICR_NMIL | ICR_MAI | ICR_NMIB | ICR_NMIE | ICR_IRLM);
		/* NMIL is read only */
		reg_put(2, addr, (old & ICR_NMIL) | (v & ~ICR_NMIL));
		update_irqs();
		return true;
	}
	/* HEURISTIC: written as 32 bits, the low half is used */
	case INTC_IPRA:
	case INTC_IPRC:
		if (size != 4)
			sized(2);
		v &= 0xFFFF;
		reg_put(2, addr, v);
		update_irq_priorities();
		return true;
	case INTC_IPRB:
		if (size != 4)
			sized(2);
		v &= 0xFFFF;
		only(0xFFF0);
		reg_put(2, addr, v);
		update_irq_priorities();
		return true;

	case DMAC_SAR0: case DMAC_SAR0 + 0x10: case DMAC_SAR0 + 0x20: case DMAC_SAR0 + 0x30:
	case DMAC_DAR0: case DMAC_DAR0 + 0x10: case DMAC_DAR0 + 0x20: case DMAC_DAR0 + 0x30:
	case DMAC_TCR0: case DMAC_TCR0 + 0x10: case DMAC_TCR0 + 0x20: case DMAC_TCR0 + 0x30: {
		uint32_t ch = (addr >> 4) & 3;
		/* Channel 0 is not written in DDT mode. */
		if ((reg_get(4, DMAC_DMAOR) & 0x8000) && ch == 0) {
			return true;
		}
		sized(4);
		if (m_dmac_running[ch]) {
			fatal("DMAC register written while the channel runs", addr);
		}
		break;
	}
	case DMAC_CHCR0: case DMAC_CHCR0 + 0x10: case DMAC_CHCR0 + 0x20: case DMAC_CHCR0 + 0x30: {
		uint32_t ch = (addr >> 4) & 3;
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		if ((reg_get(4, DMAC_DMAOR) & 0x8000) && ch == 0) {
			return true;
		}
		sized(4);
		only(ch < 2 ? ~0x00F00008u : ~0x00F50008u);
		/* TE can only be cleared */
		reg_put(size, addr, (v & ~2u) | (old & v & 2));
		dmac_update_irqs();
		dmac_update_channel(ch, 1);
		dmac_run();
		return true;
	}
	case DMAC_DMAOR: {
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		sized(4);
		only(0x00008307);
		/* AE and NMIF can only be cleared */
		reg_put(size, addr, (v & ~6u) | (old & v & 6));
		dmac_update_irqs();
		dmac_update(1);
		dmac_run();
		return true;
	}

	/* PMCLR (bit 13) clears the counter and reads back clear; PMEN (15)
	 * and PMST (14) together run it. */
	case PM_PMCR1:
	case PM_PMCR2: {
		uint32_t n = (reg == PM_PMCR1) ? 0 : 1;
		sized(2);
		if (v & 0x2000) {
			m_pm_counter[n] = 0;
		}
		m_pm_running[n] = (v & 0xC000) == 0xC000;
		reg_put(size, addr, v & ~0x2000u);
		return true;
	}
	case PM_PMCTR1H:
	case PM_PMCTR1L:
	case PM_PMCTR2H:
	case PM_PMCTR2L:
		return true;

	case TMU_TOCR:
		sized(1);
		only(0x01);
		break;
	case TMU_TSTR:
		sized(1);
		only(0x07);
		tmu_sync(m_at);
		m_event = m_retime = true;
		reg_put(size, addr, v);
		for (uint32_t ch = 0; ch < 3; ch++) {
			m_tmu_running[ch] = (v >> ch) & 1;
		}
		return true;
	case tcr(0):
	case tcr(1): {
		tmu_sync(m_at);
		m_event = m_retime = true;
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		sized(2);
		only(0x013F);
		/* UNF can only be cleared */
		reg_put(size, addr, (v & 0x00FF) | (old & v & 0x0100));
		tmu_update_irq(reg == TMU_TCR0 ? 0 : 1);
		return true;
	}
	case tcr(2): {
		tmu_sync(m_at);
		m_event = m_retime = true;
		uint32_t old = static_cast<uint32_t>(reg_get(size, addr));
		sized(2);
		/* Input capture is not implemented. */
		only(0x037F);
		reg_put(size, addr, (v & 0x00FF) | (old & v & 0x0300));
		tmu_update_irq(2);
		return true;
	}
	case tcor(0):
	case tcor(1):
	case tcor(2):
		sized(4);
		tmu_sync(m_at);
		m_event = m_retime = true;
		break;
	case tcnt(0):
	case tcnt(1):
	case tcnt(2):
		sized(4);
		tmu_sync(m_at);
		m_event = m_retime = true;
		m_tmu_counter[(reg - TMU_TCNT0) / 12] = v;
		return true;
	case CPG_FRQCR:
		sized(2);
		reg_put(size, addr, v);
		pck_cycles();
		return true;

	default:
		return false;
	}

	reg_put(size, addr, value);
	return true;
}

} /* namespace odin */

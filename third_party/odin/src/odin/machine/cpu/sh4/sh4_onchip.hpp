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

/* The registers of the on-chip modules of the SH-4 by their place in the
 * 64 KB image of FF000000 (bits 16-23 and 0-7 of the address): SH7750
 * hardware manual, appendix A. */

#ifndef ODIN_SH4_ONCHIP_HPP
#define ODIN_SH4_ONCHIP_HPP

#include <cstdint>

namespace odin {

constexpr uint32_t CCN_MMUCR = 0x000010, CCN_CCR = 0x00001C, CCN_EXPEVT = 0x000024, CCN_INTEVT = 0x000028,
	CCN_QACR0 = 0x000038, CCN_QACR1 = 0x00003C;
/* Performance counters: not in the manual, see Linux
 * arch/sh/kernel/cpu/sh4/perf_event.c (PM_CR_BASE, PM_CTR_BASE). */
constexpr uint32_t PM_PMCR1 = 0x000084, PM_PMCR2 = 0x000088, PM_PMCTR1H = 0x100004, PM_PMCTR1L = 0x100008,
	PM_PMCTR2H = 0x10000C, PM_PMCTR2L = 0x100010;
constexpr uint32_t UBC_BBRA = 0x200008, UBC_BBRB = 0x200014;
constexpr uint32_t BSC_BCR1 = 0x800000, BSC_BCR2 = 0x800004, BSC_WCR1 = 0x800008, BSC_WCR2 = 0x80000C,
	BSC_WCR3 = 0x800010, BSC_MCR = 0x800014, BSC_PCR = 0x800018, BSC_RTCSR = 0x80001C, BSC_RTCNT = 0x800020,
	BSC_RTCOR = 0x800024, BSC_RFCR = 0x800028, BSC_PCTRA = 0x80002C, BSC_PDTRA = 0x800030;
constexpr uint32_t DMAC_SAR0 = 0xA00000, DMAC_DAR0 = 0xA00004, DMAC_TCR0 = 0xA00008, DMAC_CHCR0 = 0xA0000C,
	DMAC_DMAOR = 0xA00040;
constexpr uint32_t CPG_FRQCR = 0xC00000, CPG_STBCR = 0xC00004, CPG_WTCSR = 0xC0000C;
constexpr uint32_t INTC_ICR = 0xD00000, INTC_IPRA = 0xD00004, INTC_IPRB = 0xD00008, INTC_IPRC = 0xD0000C;
constexpr uint32_t TMU_TOCR = 0xD80000, TMU_TSTR = 0xD80004, TMU_TCOR0 = 0xD80008, TMU_TCNT0 = 0xD8000C,
	TMU_TCR0 = 0xD80010;
/* The three timer channels are 12 bytes apart */
constexpr uint32_t tcor(uint32_t ch) { return TMU_TCOR0 + ch * 12; }
constexpr uint32_t tcnt(uint32_t ch) { return TMU_TCNT0 + ch * 12; }
constexpr uint32_t tcr(uint32_t ch) { return TMU_TCR0 + ch * 12; }
constexpr uint32_t SCI_SCBRR1 = 0xE00004, SCI_SCTDR1 = 0xE0000C, SCI_SCSSR1 = 0xE00010;
constexpr uint32_t SCIF_SCBRR2 = 0xE80004, SCIF_SCFSR2 = 0xE80010;
constexpr uint32_t SCI_END = 0xE00020, SCIF_END = 0xE80028;
constexpr uint32_t UDI_SDIR = 0xF00000;
/* ICR (hardware manual, 19.3.2): the IRL pins as four requests (IRLM), the
 * edge of the NMI pin that asks (NMIE), an NMI taken even with SR.BL set
 * (NMIB), every interrupt masked while the NMI pin is low (MAI), the level of
 * the NMI pin (NMIL) */
constexpr uint32_t ICR_IRLM = 1u << 7, ICR_NMIE = 1u << 8, ICR_NMIB = 1u << 9;
constexpr uint32_t ICR_MAI = 1u << 14, ICR_NMIL = 1u << 15;

} /* namespace odin */

#endif

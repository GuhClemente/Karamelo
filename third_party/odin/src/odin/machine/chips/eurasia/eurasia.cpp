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

#include "odin/machine/chips/eurasia/eurasia.hpp"

#include <cstdlib>
#include <cstring>

#include "odin/machine/chips/antarctic/antarctic_list.hpp"
#include "odin/common/stop_message.hpp"

namespace odin {

/*
 * MMIOs at 18001000
 * =================
 *
 * NOTE: these ports are always read twice.
 *
 * 18001000  R		PCI ID: 17C7:11DB, a SEGA ID. See @0C0019AE
 * 18001004   W		Unknown; = 2
 *
 * 18001010   W		Unknown; = F2000000
 * 18001014   W		Unknown; = F2040000
 * 18001018   W		Unknown; = F2080000
 * 1800101C   W		Unknown; = F3000000
 */

/*
 * MMIOs at 1A000000
 * =================
 *
 * NOTE: these ports are always read twice.
 *
 * Unknown
 * -------
 *
 * 1A000000   W 	GPU 1A Enable A; b0 = enable; See @0C0069E0, @0C006AFC
 * 1A000004   W 	GPU 1A Enable B; b0 = enable;
 *			See @0C0069E0, @0C006AFC, PH:@0C01276E.
 *
 * Interrupt Control
 * -----------------
 *
 * 1A000008   W		IRQ 0 Source
 * 1A00000C   W		IRQ 1 Source (GPU is done/ready)
 * 1A000010   W		IRQ 2 Source (v-blank occurred)
 * 1A000014   W		IRQ 3 Source
 *
 *			-------- -------- -------- -------i
 *
 *			i = IRQ n is raised
 *
 * 1A000018  RW		IRQ Status
 *
 *			-------- -------- -------- ----3210
 *
 *			n = IRQ n is raised
 *
 *			NOTE: when any of the IRQs is raised, bit 7 of
 *			15000088 is set.
 *
 *			NOTE: it may be related to 1A0000C4, see @0C001ED0.
 *
 * Unknown
 * -------
 *
 * 1A00001C  R		Current Raster Position
 *
 *	   		-------- -------- -----xxx xxxxxxxx
 *	   		-------- --YYYYYY YYYYY--- --------
 *	   		-------U U------- -------- --------
 *
 *	   		X = Current X position (postulated)
 *	   		Y = Current Y position
 *	   		    See PH:@0C01C106
 *	   		U = Unknown; Even/odd field status
 *	   		    - Gets stored in [0C00F070].w, (56, GBR)
 *	   		      in particular, it replaces the whole
 *	   		      bitmask (101) in sync (); See e.g.
 *	   		      @0C006A7A, PH:@0C01C158.
 *	   		    - Affects the argument to command 781
 *	   		    - Affects how much data is sent to the
 *	   		      texture-to-texture DMA in PH.
 *
 * 1A000020  R		Even/Odd Frame Status?
 *
 *	   		-------- -------- -------- -------U
 *
 *	   		U = Unknown
 *
 *	   		It is stored in [0C00F070].w, (56, GBR) in
 *	   		install_gpu_subroutine (), @0C0080E0.
 *	   		Notably, only bit 0 of the GBR var can
 *	   		possibly be set; bit 2 is always cleared.
 *
 *	   		It affects which set of GPRs is used for
 *	   		GPU upload. See @0C008130.
 *
 * 1A000024  R		Unknown Status
 *
 *	   		-------- -------- -------- -------U
 *
 *	   		U = Unknown (in vblank?)
 *
 *	   		NOTEs:
 *	   		- 15000058 bits 0,1 and GPU jump instructions, see @0C0018B4
 *	   		- 15002000 bit 0
 *	   		- HW version
 *	   		- @0C0069E8 loops while the bit is set
 *	   		- it is set on frame change
 *	   		- Also related to GPU texture upload (acts as a busy bit); see SN-ROM:@0C070C9C
 *
 * Display Config
 * --------------
 *                                           ----------------------
 *                                            640x480      496x377	AIRTRIX
 *                                           ----------------------
 * 1A000080             l  W    = 0000027F   639          818		00000332
 * 1A000084             l  W    = 000001A0   416          528		00000210
 * 1A000088             l  W    = 02680078   616 | 120    798 | 158	031E009E
 * 1A00008C             l  W    = 0196001D \ 406 |  29    516 |  36	02040024
 * 1A000090             l  W    = 02400000 | 576 |   0    728 |   0	02D80000
 * 1A000094             l  W    = 00000040 |   0 |  64      0 |  91	0000005B
 * 1A000098             l  W    = 00000003 |   0 |   3      0 |   3	00000003
 * 1A00009C             l  W    = 00000075 |   0 | 117      0 | 155	0000009B
 * 1A0000A0             l  W    = 00000198 /   0 | 408      0 | 574	0000023E
 * 1A0000A4             l  W    = 001D0194 \  29 | 404     36 | 514	00240202
 * 1A0000A8             l  W    = 00000195 |   0 | 405      0 | 515	00000203
 * 1A0000AC             l  W    = 00000000 |   0 |   0      0 |   0	00000000
 * 1A0000B0             l  W    = 00000000 |   0 |   0      0 |   0	00000000
 * 1A0000B4             l  W    = 00000000 |   0 |   0      0 |   0	00000000
 * 1A0000B8             l  W    = 00000179 /   0 | 377      0 | 416	000001A0
 * 1A0000BC             l  W    = 00000008     0 |   8      0 |   8	00000008
 * 1A0000C0             l  W    = 01960000   406 |   0      0 | 516	02040000
 *
 * Unknown
 * -------
 *
 * 1A0000C4   W		Unknown control
 *
 *			-------- -------- -------- -----uu-
 *
 *			u = Unknown
 *
 * 1A0000D0   W		Unknown control
 *
 *			-------- -------- -------- -------u
 *
 *			u = Unknown
 *
 * Framebuffer/2D Layer Control
 * ----------------------------
 *
 * 1A000100  RW		Unknown control
 *
 *			-------- -------- -------- ----uuuu
 *
 *			u = Unknown; a bitfield
 *
 *			See @0C007D00, PH:@0C01A0F8, PH@0C01A10C.
 *
 *			NOTE: perhaps it selects between FB unit 0 and unit 1
 *			for on a per-layer basis?
 *
 * 1A000180-1A0001BF	FB Unit 0 MMIOs (four banks)
 * 1A000200-1A00023F	FB Unit 1 MMIOs (four banks)
 *
 * Unit 0 and 1 MMIOs are identical. The values stored in unit 0 MMIOs are
 * copied into unit 1 MMIOs in @0C00689E. Each bank supposedly specifies a
 * layer or a framebuffer(/backbuffer?) location in the FB. No idea why there
 * are two units though.
 *
 * See '2D Layers' below.
 *
 * Unknown
 * -------
 *
 * 1A020000   W		"SEGA" is written here; see @0C001A58
 *
 * GPU DMA
 * -------
 *
 * 1A04000{0,4,8,C}	DMA MMIOs
 *
 * See 'GPU DMA' below.
 *
 * Unknown
 * -------
 *
 * 1A08006C  R		Unknown, See AT:@0C697A5E.
 *
 * 1A0A1600   W		Unknown (seems related 15040E00, see PHARRIER)
 *
 * 1A0A1FFC   W		Unknown; = 83709075 or 83709076 (VON4 @0C716780)
 */

Eurasia::Eurasia(std::span<uint8_t> fb)
	: m_fb(fb), m_log(nullptr), m_fb_written(nullptr), m_fb_written_ctx(nullptr)
{
	reset();
}

void
Eurasia::reset()
{
	std::memset(&m_s, 0, sizeof(m_s));
}

void
Eurasia::fatal(const char *what, uint32_t addr, uint32_t value) const
{
	stop_fatal("eurasia: %s (%08X = %08X)", what, addr, value);
}

bool
Eurasia::read(uint32_t addr, uint32_t &value) const
{
	value = 0;

	if (addr == 0x18001000) {
		/* SEGA PCI ID */
		value = 0x17C711DB;
	} else if (addr >= 0x1A000000 && addr < 0x1A000140) {
		switch (addr & 0x1FF) {
		case 0x18: /* IRQ status */
		case 0x1C:
		case 0x20: /* read once at start (@0C0080E0); stays 0 (HEURISTIC) */
		case 0x24: /* busy, bit 0 */
		case 0x100:
			value = reg(addr);
			break;
		default:
			return false;
		}
	} else if (addr >= 0x1A000180 && addr < 0x1A0001C0) {
		value = unit(0, addr);
	} else if (addr >= 0x1A000200 && addr < 0x1A000240) {
		value = unit(1, addr);
	} else if (addr == 0x1A08006C) {
		/* HEURISTIC: 0, what the frame swap of AIRTRIX and VON4
		 * waits for before it starts a list (VON4 @0C711AFC), where
		 * the older one waits for the end of the plot. */
	} else {
		return false;
	}

	return true;
}

Eurasia::Written
Eurasia::write(uint32_t addr, uint32_t value)
{
	if (addr >= 0x18001000 && addr < 0x18001020) {
		m_s.pci[(addr & 0x1F) / 4] = value;
	} else if (addr >= 0x1A000000 && addr < 0x1A000104) {
		const uint32_t offs = addr & 0x1FF;

		switch (offs) {
		case 0x00:
		case 0x04:
			/* 1 before each launch; kept, and nothing here reads them */
			break;
		case 0x08:
		case 0x0C:
		case 0x10:
		case 0x14:
			/* Bit 0 is ANDNOT'ed on write: a 1 clears the cause,
			 * a 0 leaves it (VON4 writes each bit of 1A000018
			 * back to its register, @0C711F62). The other
			 * bits are not known (they stop the emulator). */
			if (value > 1)
				fatal("interrupt acknowledge not known", addr, value);
			reg(offs) &= ~value;
			update_status();
			return Written::CAUSES;
		case 0x24:
			reg(offs) = value;
			return Written::BUSY;
		case 0xC4: /* Unknown control */
		case 0xD0: /* Unknown control */
		case 0x100:
			break;
		default:
			/* Display Config? */
			if (offs < 0x80 || offs > 0xC0)
				return Written::UNKNOWN;
			break;
		}
		reg(offs) = value;
	} else if (addr >= 0x1A000180 && addr < 0x1A0001C0) {
		m_s.unit[0][(addr & 0x3F) / 4] = value;
	} else if (addr >= 0x1A000200 && addr < 0x1A000240) {
		m_s.unit[1][(addr & 0x3F) / 4] = value;
	} else if (addr == 0x1A020000) {
		/* "SEGA" */
		if (value != 0x53454741)
			fatal("write not known", addr, value);
	} else if (addr >= 0x1A040000 && addr < 0x1A040010) {
		m_s.blit[(addr & 0xF) / 4] = value;
		if (addr == 0x1A04000C && (value & 1))
			blit();
	} else if (addr == 0x1A0A1600) {
		if (value != 1)
			fatal("write not known", addr, value);
	} else if (addr == 0x1A0A1FFC) {
		/* VON4 alone, once: 83709075, or 83709076 when the argument
		 * of its routine is 0 (@0C716780). */
		if (value != 0x83709075 && value != 0x83709076)
			fatal("write not known", addr, value);
	} else {
		return Written::UNKNOWN;
	}

	return Written::DONE;
}

/* The four causes: bit 0 of 1A000008, 0C, 10 and 14, gathered in the low
 * bits of 1A000018. */

void
Eurasia::update_status()
{
	reg(0x18) = (reg(0x18) & ~0xFu) |
	            (reg(0x08) & 1) |
	            ((reg(0x0C) & 1) << 1) |
	            ((reg(0x10) & 1) << 2) |
	            ((reg(0x14) & 1) << 3);
}

void
Eurasia::raise(uint32_t causes)
{
	if (causes & 1)
		reg(0x08) |= 1;
	if (causes & 2)
		reg(0x0C) |= 1;
	if (causes & 4)
		reg(0x10) |= 1;
	if (causes & 8)
		reg(0x14) |= 1;
	update_status();
}

/* The screen */

void
Eurasia::end_of_line(uint32_t line)
{
	/* The line counter; a pixel counter, if there is one, is left out (no
	 * game was seen reading it). */
	reg(0x1C) = (reg(0x1C) & ~0x003FF800u) | (line << 11);
}

void
Eurasia::vblank_in()
{
	reg(0x1C) ^= 0x01800000;
}

void
Eurasia::vblank_out()
{
	raise(IRQ_VBLANK);
	read_layers();
}

/* The frame as the games set it: dots per line (1A000080), lines
 * (1A000084), last and first line shown (1A00008C). The six games write
 * 818, 528, 516 and 36 some thirty frames after power-on and never again.
 * HEURISTIC: before that the frame is taken to be the same. */
void
Eurasia::frame_size(uint32_t &dots, uint32_t &lines, uint32_t &shown) const
{
	if (reg(0x80) == 0 && reg(0x84) == 0 && reg(0x8C) == 0) {
		dots = 818;
		lines = 528;
		shown = 480;
		return;
	}
	dots = reg(0x80);
	lines = reg(0x84);
	shown = (reg(0x8C) >> 16) - (reg(0x8C) & 0xFFFF);
}

/*
 * Framebuffer and Texture RAM
 * ===========================
 *
 * The framebuffer RAM (FB) is located at 1B000000-1B7FFFFF in the master
 * SH-4 address space. It acts as a single 2048x2048 sheet. It holds the
 * framebuffer and the 2D layers, plus tile data. It is accessible both
 * directly by the CPU or indirectly through the GPU DMA device. It is used
 * for 2D only.
 *
 * Framebuffer data can hold both ABGR1555? and A2BGR10 data.
 *
 * The two texture RAM (TEXRAM) areas are located at 04000000-043FFFFF
 * (bank 0) and 06000000-063FFFFF (bank 1) of the external address space,
 * respectively. Each bank acts as a 2048x1024 sheet. The two banks hold
 * texture data and are directly accessible by the GPU. They are mainly
 * accessed by the CPU through the GPU IDMA device. They are used for 3D
 * only.
 *
 * Textures can be ABGR1555, ABGR4444, I4, and A8?; other formats
 * may be possible. Textures may or may not include a complete mipmap tree.
 *
 *
 *
 * Framebuffer Configuration
 * =========================
 *
 * The GPU is able to specify four regions of FB to be used as either
 * framebuffer (front and back, likely) or 2D bitmap layers. The mapping
 * looks like this:
 *
 * Bank 0 = Framebuffer?
 * Bank 1 = Backbuffer?
 * Bank 2 = Layer 1
 * Bank 3 = Layer 2
 *
 * The number of bitmap layers here conforms to the system16.com specs.
 *
 * The device is configured through the following MMIOs:
 *
 * 1A000180,4		Unit 0, Front Buffer Coords
 * 1A000188,C		Unit 0, Back Buffer Coords
 * 1A000190,4		Unit 0, Layer 1 Coords
 * 1A000198,C		Unit 0, Layer 2 Coords
 *
 *			-------- -----yyy yyyyyyyx xxxxxxxx
 *			-------- -----YYY YYYYYYYX XXXXXXXX
 *
 *			x,y = Coordinates of upper right corner
 *			X,Y = Coordinates of lower left corner
 *
 *			The actual scale of x and X is determined by the layer
 *			format.
 *
 *			See PH:@0C01A1A6, PH:@0C01A860.
 *
 * 1A0001B0  RW		FB/Layer Format
 *
 *			-------- -------- -------- -----21F
 *
 *			F = framebuffer/backbuffer format
 *			1 = layer 1 format
 *			2 = layer 2 format
 *
 *			A layer whose bit is 0 is ABGR1555, 1 A2BGR10
 *			(read_layers).
 *
 *			See PH:@0C01A1EA.
 *
 * 1A0001B4  RW		Layer Enable
 *
 *			-------- -------- -------- ------21
 *
 *			n = enable layer n
 *
 *			See PH:@0C01A124, PH:@0C01A142.
 *
 * 1A0001B8  RW		Layer Priority
 *
 *			-------- -------- -------- ------21
 *
 *			n = layer n is drawn above the 3D framebuffer if 1,
 *			    below it if 0. AIRTRIX puts both layers below for
 *			    its logo screens, PHARRIER and SGNASCAR layer 2.
 *
 * 1A0001BC  RW		Transparency
 *
 *			-------- -------- -------- -----21F
 *
 *			n = the pixels of layer n without their alpha bit
 *			    are transparent if 1; the layer covers everything
 *			    if 0.
 *			F = the 3D framebuffer shows what is below it where
 *			    nothing was drawn. Set whenever a layer is below.
 *
 *			See PH:@0C01A162, PH:@0C01A171.
 *
 * The MMIOs at 1A000200-1A00023F have the very same layout. Sometimes they
 * are written with the contents of the unit 0 MMIOs. Their purpose is still
 * unknown.
 *
 * NOTE: it may be the case that unit 1 is used for a second GPU/screen.
 */

void
Eurasia::read_layers()
{
	m_s.layers.enabled = 1;
	m_s.layers.fb_keyed = unit(0, 0x3C) & 1;

	for (uint32_t n = 0; n < 2; n++) {
		for (uint32_t bank = 2; bank < 4; bank++) {
			odin_layer_t &layer = m_s.layers.layer[n][bank - 2];
			const uint32_t lo = unit(n, bank * 8 + 0);
			const uint32_t hi = unit(n, bank * 8 + 4);
			uint32_t shift;

			layer.enabled = (unit(n, 0x34) >> (bank - 2)) & 1;

			/* Above the 3D, or below it (the white sheet and the
			 * logo of the AIRTRIX logo screens, unit 38 = 0). */
			layer.above = (unit(n, 0x38) >> (bank - 2)) & 1;

			if (((unit(n, 0x30) >> (bank - 1)) & 1) == 0) {
				layer.format = ODIN_FORMAT_ABGR1555;
				shift = 1;
			} else {
				layer.format = ODIN_FORMAT_A2BGR10;
				shift = 2;
			}

			/* Without this bit the layer covers everything (the white
			 * SEGA screen of von4 on the cabinet, unit 3C = 2). */
			layer.keyed = (unit(n, 0x3C) >> (bank - 1)) & 1;

			/* Where the layer is in the FB. */
			layer.x0 = (lo & 0x1FF) << shift;
			layer.y0 = lo >> 9;
			layer.x1 = (hi & 0x1FF) << shift;
			layer.y1 = hi >> 9;
		}
	}
}

/*
 * GPU DMA
 * =======
 *
 * Copies texture data around within the FB. It's used to compose the intro
 * text of AIRTRIX and PHARRIER, for instance. It is controlled by the ports
 * at 1A04000{0,4,8,C} as follows:
 *
 * 1A040000   W		Source coords
 *
 *			-------- --yyyyyy yyyyyxxx xxxxxxxx 
 *
 *			x,y = Coordinates in pixels
 *
 * 1A040004   W		Destination coords
 *
 *			-------- --yyyyyy yyyyyxxx xxxxxxxx 
 *
 *			x,y = Coordinates in pixels
 *
 * 1A040008   W		Size
 *
 *			hhhhhhhh hhhhhhhh wwwwwwww wwwwwwww
 *
 *			w = Width in pixels
 *			h = height in pixels
 *
 * 1A04000C   W		Control
 *
 *			-------- -------- -------- -------E
 *
 *			E = Process the DMA request when set
 *
 * See AT:@0C697D48, PH:@0C0CD320.
 *
 * NOTE: 1A000024 bit 0 seems to signal when the device is busy.
 *
 */

/* The copy is done at once (HEURISTIC): PHARRIER waits for it as if each
 * texel took a few cycles. */
void
Eurasia::blit()
{
	const uint32_t src_x = m_s.blit[0] & 0x7FF, src_y = m_s.blit[0] >> 11;
	const uint32_t dst_x = m_s.blit[1] & 0x7FF, dst_y = m_s.blit[1] >> 11;
	const uint32_t w = m_s.blit[2] & 0xFFFF, h = m_s.blit[2] >> 16;

	if (m_log)
		std::fprintf(m_log, "GPU DMA: [%08X %08X %08X %08X] { %u %u } --> { %u %u }, %ux%u\n",
		             m_s.blit[0], m_s.blit[1], m_s.blit[2], m_s.blit[3],
		             src_x, src_y, dst_x, dst_y, w, h);

	/* The two 16-bit pixels of each 32-bit FB word are stored swapped
	 * (renderer_layers.cpp): a copy between columns of different parity
	 * must follow the pixels, not the memory order (the small print of
	 * the AIRTRIX title). */
	for (uint32_t i = 0; i < h; i++) {
		for (uint32_t j = 0; j < w; j++) {
			const size_t src = size_t{src_y + i} * 4096 + ((src_x + j) ^ 1) * 2;
			const size_t dst = size_t{dst_y + i} * 4096 + ((dst_x + j) ^ 1) * 2;

			if (src + 2 > m_fb.size() || dst + 2 > m_fb.size())
				fatal("blit out of the framebuffer RAM", m_s.blit[0], m_s.blit[1]);
			std::memcpy(&m_fb[dst], &m_fb[src], 2);
		}
	}
	if (m_fb_written && h != 0 && w != 0) {
		const size_t end = size_t{dst_y + h - 1} * 4096 + size_t{(dst_x + w - 1) | 1} * 2 + 2;

		m_fb_written(m_fb_written_ctx, size_t{dst_y} * 4096, end - size_t{dst_y} * 4096);
	}

	/* The copy is immediate, so the busy bit (1A000024 bit 0) is never
	 * left set: games poll it before queueing the next blit. */
	reg(0x24) &= ~1u;
}

} /* namespace odin */

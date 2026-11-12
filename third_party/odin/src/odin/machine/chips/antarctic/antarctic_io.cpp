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

/* The registers of the command processor and its texture transfer. */

#include "odin/machine/chips/antarctic/antarctic.hpp"

#include <algorithm>
#include <cstring>

namespace odin {

/*
 * MMIOs at 15000000
 * =================
 *
 * NOTE: all ports are 32-bit wide, unless otherwise noted.
 *
 * Display Config (Likely)
 * -----------------------
 *
 * 15000000   W		Unknown; = 0
 * 15000004   W		Display mode
 *			 0 = hi-res (640x480, 31 KHz)
 *			 1 = lo-res (496x384, 24 KHz)
 *			See @0C001AD8, @0C00792C.
 * 15000008   W		Unknown; = 0
 *
 * GPU IDMA
 * --------
 *
 * 150000{0C,10,14}	Indirect DMA (IDMA) MMIOs
 *
 * See 'GPU IDMA' below.
 *
 * GPU 15 Unknown Config A
 * -----------------------
 *
 * 15000018   W		Unknown; = 0       ; ~ 48000000
 * 1500001C   W		Unknown; = 00040000; ~ 48100000
 * 15000020   W		Unknown; = 00048000; ~ 48120000
 * 15000024   W		Unknown; = 00058000; ~ 48160000
 * 15000028   W		Unknown; = 00007800; ~ 4801E000
 * 1500002C   W		Unknown; = 0007FE00; ~ 481FF800
 * 15000030   W		Unknown; = 0       ; ~ 48000000
 * 15000034   W		Unknown; = 00005000; ~ 48014000
 *
 * GPU 15 Unknown Config B
 * -----------------------
 *
 * 15000038   W		Unknown; = 00080000; ~ 48200000
 * 1500003C   W		Unknown; = 000C0000; ~ 48300000
 * 15000040   W		Unknown; = 000C8000; ~ 48320000
 * 15000044   W		Unknown; = 000D8000; ~ 48360000
 * 15000048   W		Unknown; = 0000F800; ~ 4803E000
 * 1500004C   W		Unknown; = 000FFE00; ~ 483FF800
 * 15000050   W		Unknown; = 00008000; ~ 48020000
 * 15000054   W		Unknown; = 0000D000; ~ 48034000
 *
 * NOTE: same as Config A, plus an offset of +80000 or +8000.
 *
 * Command Processor Control
 * -------------------------
 *
 * 15000058   W		CP Control; = 3
 *			If both bits 0 and 1 are set, start CP execution
 * 15000070   W		CP Start Address
 * 15000074   W		CP Processor 0 Stack Pointer?
 * 15000078   W		CP Processor 1 Stack Pointer?
 * 1500007C   W		CP Abort Execution when 0-then-1 are written?
 *			 See @0C006AFC.
 *
 * Unknown
 * -------
 *
 * 15000080   W		Unknown; Control; = 6	
 *
 * Interrupt Control
 * -----------------
 *
 * 15000084   W		GPU IRQ Mask: a set bit keeps its cause from
 *			interrupting; all set after a reset
 * 15000088  RW		GPU IRQ Status
 *			 80 = GPU 1A IRQ fired
 *			 40 = Unknown
 *			 20 = Unknown
 *			 10 = Unknown
 *			 08 = Unknown
 *			 04 = GPU 15 is ready/done; see @0C0018B4
 *			 02 = Unknown; possibly VBLANK
 *			 01 = IDMA done; see @0C006C04
 *			All bits are AND'ed on write
 *
 * Memory Config
 * -------------
 *
 * 1500008C   W		TEXRAM Access Mode
 *
 *			01010101 = Auto-twiddling disabled
 *			02020202 = Auto-twiddling enabled
 *
 *			When auto-twiddling is enabled, data written to TEXRAM
 *			(via the external BUS) is automatically twiddled prior
 *			to being stored. This is used by most games so far.
 *
 *			When it is disabled, data is written in TEXRAM as-is.
 *			BRAVEFF disables auto-twiddling and transfers texture
 *			data from MASKROM to TEXRAM (banks 0 and 1) via
 *			MEMCTL DMA.
 *
 *			Since we do *not* emulate auto-twiddling, when mode is
 *			02020202 the emulated TEXRAM gets filled with
 *			ready-to-use, untwiddled texture data; alas, when the
 *			mode is 01010101 (as for BRAVEFF), data is written
 *			directly in twiddled format, so we need to untwiddle
 *			it manually.
 *
 *			See BF:@0C0D5520.
 *
 * 15000090   W		0
 * 15000094   W		0
 * 15000098   W		02020202
 *			See @0C001A82
 *
 * Unknown
 * -------
 *
 * 15002000  R		Unknown; Status
 *			Used to:
 *			 - determine if the GPU is done doing FOO (together
 *			   with bit 0 of 1A000024), see @0C0069E0 and
 *			   sync() in basically all games.
 *			 - determine the HARDWARE VERSION:
 *				 - 0=older
 *				 - 1=newer
 *			   See @0C001AC4, PH:@0C01220C
 *
 * Performance Counters
 * --------------------
 *
 * These ports return the number of primitives of a certain type pushed/
 * traversed/rendered by the hardware.
 *
 * 15002800  R		# of Opaque Primitives
 * 15002804  R		# of 'Shadow A' Primitives
 * 15002808  R		# of 'Shadow B' Primitives
 * 1500280C  R		# of Transparent Primitive
 * 15002810  R		# of Background Primitives
 * 15002814  R		# of Translucent Primitives
 * 15002820  R		# of 'Set Primitive' Operations
 * 15002824  R		# of 'Render Primitive' Operations
 * 15002840  R		# of Texture Heads
 * 15002844  R		# of Materials
 * 15002848  R		# of Lights
 *
 * See PH:@0C0127B8 and the menu list at PH:@0C1D4EA4. Counter names are taken
 * from the latter.
 *
 * They read 0 here: nothing says what each counts exactly. The libraries of
 * AIRTRIX, BRAVEFF, SWRACER and SGNASCAR copy the eleven to their memory at
 * every list; a debug text is all that is seen made of them (BRAVEFF
 * @0C040A66), and the six attract loops send the same lists when they read
 * 1000000.
 *
 * Unknown
 * -------
 *
 * 1502C100   W		Unknown; = 9
 * 1502C104   W		Unknown; = 6
 *
 * 15040E00   W		Unknown; = 0
 */

bool
Antarctic::read_register(uint32_t addr, uint32_t &value) const
{
	value = 0;

	if (addr >= 0x15000000 && addr < 0x15000100) {
		switch (addr & 0xFF) {
		case 0x10:
		case 0x14:
		case 0x88:
			value = reg(addr);
			break;
		default:
			return false;
		}
	} else if (addr == 0x15002000) {
		/* Bit 0 tells the two main boards apart: the system library
		 * starts a list, waits for a vertical blanking and reads it
		 * (PHARRIER @0C010790); the BOOTROM then names the board
		 * 837-13402 when it was set, 837-14097 when it was clear.
		 * This is the second one. On the first, the library also
		 * takes the bit, with bit 0 of 1A000024 clear, for a list
		 * that is done (@0C0126BA). */
	} else if (addr >= 0x15002800 && addr < 0x1500284C) {
		/* The counters, see above */
	} else {
		return false;
	}

	return true;
}

Antarctic::Written
Antarctic::write_register(uint32_t addr, uint32_t value)
{
	if (addr >= 0x15000000 && addr < 0x15000100) {
		const uint32_t offs = addr & 0xFF;

		switch (offs) {
		case 0x14: /* Texture transfer control */
			/* A transfer runs to its end whatever is written here,
			 * and reads busy until then. SGNASCAR polls this bit
			 * with the end interrupt masked; the boot ROM clears
			 * the register when it meets the latched status bit,
			 * at a later interrupt (@0C010364): that fell in the
			 * middle of the next transfer and lost 266 textures. */
			if (value & 1)
				m_s.transfer_left = reg(0x10);
			reg(0x14) = m_s.transfer_left ? 1 : 0;
			return Written::DONE;
		case 0x58: /* CP Control */
			reg(offs) = value;
			return Written::CONTROL;
		case 0x84: /* IRQ mask */
			reg(offs) = value;
			return Written::CAUSES;
		case 0x88: /* IRQ status */
			reg(offs) &= value;
			return Written::CAUSES;
		case 0x8C: /* TEXRAM twiddling mode */
			if (value != 0x01010101 && value != 0x02020202)
				fatal("texture RAM mode not known", value);
			break;
		case 0x80:
		case 0x90:
		case 0x94:
		case 0x98:
			break;
		default:
			if (!(offs <= 0x10 || (offs >= 0x18 && offs <= 0x34) ||
			      (offs >= 0x38 && offs <= 0x54) || (offs >= 0x70 && offs <= 0x78)))
				return Written::UNKNOWN;
			break;
		}
		reg(offs) = value;
	} else if (addr == 0x1502C100) {
		if (value != 9)
			fatal("write to 1502C100 not known", value);
	} else if (addr == 0x1502C104) {
		if (value != 6)
			fatal("write to 1502C104 not known", value);
	} else if (addr == 0x15040E00) {
		if (value != 0)
			fatal("write to 15040E00 not known", value);
	} else {
		return Written::UNKNOWN;
	}

	return Written::DONE;
}

/* HEURISTIC: bit 7 of 15000088 follows the causes of the display chip.
 * The handler of the games writes it back before it acknowledges them
 * (PHARRIER @0C0102C0): were it to stay set until written, every
 * interrupt of the display chip would be taken twice. */
void
Antarctic::set_display_irq(bool on)
{
	reg(0x88) = (reg(0x88) & ~uint32_t{IRQ_DISPLAY}) | (on ? uint32_t{IRQ_DISPLAY} : 0u);
}

/*
 * GPU Indirect DMA (IDMA)
 * =======================
 *
 * This device is used to transfer texture data from the BUS to the TEXRAM.
 * It is controlled by the MMIOs at 150000{0C,10,14} as follows:
 *
 * 1500000C   W		Indirect DMA address
 *
 *			aaaaaaaa aaaaaaaa aaaaaaaa aaaaaaaa
 *
 *			a = BUS address of the IDMA table
 *
 * 15000010  RW		Indirect DMA length
 *
 *			-------- -------- nnnnnnnn nnnnnnnn
 *
 *			n = Number of entries to process; reads back
 *			    unchanged after the transfer
 *
 * 15000014  RW		Indirect DMA Control
 *
 *			-------- -------- -------- -------E
 *
 *			E = Process the DMA request when set; signal busy
 *			    or ready when read. Clearing it does not stop
 *			    the transfer.
 *
 * 1500000C points to a table in CMDRAM, defaulting to 483FC000. Each entry
 * is 4 words long, and has the following format:
 *
 *         MSB                               LSB
 *	+00 AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *	+04 -------- -------- SSSSSSSS SSSSSSSS
 *	+08 ---fff-- --hhhwww yyyyyyyy xxxxxxxx
 *	+0C                            -------b
 *
 * 	A = a BUS address; it specifies where the texels are stored. Valid
 *          locations seen so far are the CMDRAM (48xxxxxx) and slave RAM
 *          (41xxxxxx).
 *
 *	S = texture size in bytes; it includes the size of the whole
 *          mipmap tree (if present.) That is, the size for a 64x64 texture
 *	    is:
 *
 *	    (64*64+32*32+16*16+8*8+4*4+2*2+1)*2 = 0x2AAA bytes
 *
 *	    Without a mipmap it would just be: 64*64*2 = 0x2000 bytes.
 *
 *	    The tree of a texture that is not square ends when its short
 *	    side is 1: BRAVEFF gives 0x1554 for 64x32 (down to 2x1), 0x2AA8
 *	    for 128x32 (down to 4x1), 0x5550 for 256x32 (down to 8x1).
 *
 *	y = Destination slot y
 *
 *	x = Destination slot x
 *
 *	    Each slot is 16x16 pixels; the origin (i.e., the lowest
 *	    addressable slot coords) seems to be (80, C0). The slot determines
 *	    the TEXRAM coords within the selected bank (see below.)
 *
 *	w = log_16 of width
 *
 *	h = log_16 of height 
 *
 *		0 = 16
 *		1 = 32
 *		2 = 64
 *		3 = 128
 *		4 = 256
 *		5,6,7 = Unused?
 *
 *	f = Format:
 *
 *		0 = ABGR1555
 *		1 = ABGR4444
 *		2 = I4 (4 bits a texel, 2 x 2 texels a word)
 *		4 = LA8 (HEURISTIC: as the renderer reads it, ODIN_FORMAT_LA8)
 *
 *	b = Destination TEXRAM bank
 *
 * The IDMA fires GPU 15 IRQ 1 when done.
 *
 * NOTE: the upper half of +08 likely uses the same format as instruction
 * 2C1; the lower half likely uses the same format as 4C1. If this is the
 * case, then bits 22-26 and 29-31 may not be unused --- in fact, there's
 * evidence that they are used within the 2C1 instruction.
 *
 * NOTE: AIRTRIX uploads textures as blocks of 512x512 pixels. The odd thing
 * is that format=0 while not all textures in the block are ABGR1555.
 *
 * NOTE: IDMA it may be related to vblank timing, see PH:@0C0128E6 and
 * PH:@0C01290A.
 */

bool
Antarctic::step_texture_transfer()
{
	uint32_t entry[4];

	/* The length register keeps its value: once the transfer is over the
	 * library reads it back, adds it to its count of copied entries and
	 * clears it (BOOTROM @0C006C04). BRAVEFF waits on that count every
	 * 1024 entries (@0C042F68). */

	if (!m_s.transfer_left)
		return false;

	/* The table, in the command RAM */
	const uint32_t table = reg(0x0C) & 0xFFFFFF;

	if ((reg(0x0C) >> 24) != 0x48 || table + sizeof(entry) > m_cmdram.size())
		fatal("texture transfer table out of the command RAM", reg(0x0C));
	std::memcpy(entry, &m_cmdram[table], sizeof(entry));

	if (entry[1]) {
		transfer_texture(entry);
		reg(0x0C) += 0x10;
		m_s.transfer_left--;
	}

	if (m_s.transfer_left == 0) {
		reg(0x14) = 0;
		raise(IRQ_TRANSFER_END);
		return true;
	}
	return false;
}

void
Antarctic::transfer_texture(const uint32_t entry[4])
{
	const odin_texhead_t texhead =
		odin_tex_make(entry[3] & 1,
		              entry[2] & 0xFF, (entry[2] >> 8) & 0xFF,
		              (entry[2] >> 16) & 7, (entry[2] >> 19) & 7,
		              (entry[2] >> 26) & 7);
	const uint32_t format = odin_tex_format(&texhead);
	const uint32_t slotx = odin_tex_slotx(&texhead);
	const uint32_t sloty = odin_tex_sloty(&texhead);
	const uint32_t bus_addr = entry[0];
	const uint32_t size = entry[1];

	if (m_transfer_log)
		std::fprintf(m_transfer_log, "GPU IDMA %08X %08X %08X %08X : %s\n",
		             entry[0], entry[1], entry[2], entry[3],
		             odin_texhead_str(&texhead));

	if ((entry[2] & 0xE3C00000) ||
	    (entry[3] & 0xFFFFFFFE)) {
		error("GPU IDMA: unhandled bits in texture entry: %08X %08X %08X %08X",
		      entry[0], entry[1], entry[2], entry[3]);
	}

	if (format != ODIN_FORMAT_ABGR1555 &&
	    format != ODIN_FORMAT_ABGR4444 &&
	    format != ODIN_FORMAT_I4 &&
	    format != ODIN_FORMAT_LA8) {
		error("GPU IDMA: unknown texhead format: %s", odin_texhead_str(&texhead));
	}

	if (slotx < 0x80 || sloty < 0xC0) {
		error("GPU IDMA: unknown texhead slot, skipping: %s", odin_texhead_str(&texhead));
		return;
	}

	if (slotx > 0xC0 && sloty > 0xE0)
		error("GPU IDMA: mipmap texhead slot: %s", odin_texhead_str(&texhead));

	if ((bus_addr & 0xFE000000) != 0x40000000 &&
	    (bus_addr & 0xFF000000) != 0x48000000) {
		error("GPU IDMA: unknown texhead address, skipping: %s", odin_texhead_str(&texhead));
		return;
	}

	/* An entry may ask for more bytes than its levels hold: BRAVEFF gives
	 * 39 of its 64x32 textures the bytes of 64x64 (its files keep 0x2AC0
	 * bytes a texture) and draws them as 64x32. HEURISTIC: the levels of
	 * the size the entry gives are copied and the rest is left; what the
	 * board does with the rest is not known. */
	const uint32_t size_copied = copy_texture(bus_addr, size, texhead);
	if (size_copied != size) {
		error("GPU IDMA: requested vs. copied sizes mismatch: %u vs %u", size, size_copied);
	}
}

/* Copies the levels of a texture, returns the bytes copied. */
uint32_t
Antarctic::copy_texture(uint32_t bus_addr, uint32_t size, const odin_texhead_t &texhead)
{
	const std::span<const uint8_t> src = ((bus_addr >> 24) == 0x48) ? m_cmdram : m_ram_s;
	const uint32_t max_level = std::max(odin_tex_logw(&texhead), odin_tex_logh(&texhead)) + 4;
	uint32_t level_w = 16u << odin_tex_logw(&texhead);
	uint32_t level_h = 16u << odin_tex_logh(&texhead);
	uint32_t bank = odin_tex_bank(&texhead);
	uint32_t dstx, dsty, level = 0, size_copied = 0;

	/* The pyramid goes down to 1x1 (X360 825C6A00) and its levels
	 * alternate between the two banks: the pages BRAVEFF writes to the
	 * texture RAM itself hold the half-size copy of a bank 0 texture in
	 * bank 1, and SWRACER keeps textures where the level 1 of the same
	 * bank would land. The X360 port keeps the levels in the texhead
	 * bank (825C68D8). */
	odin_tex_coords(&texhead, &dstx, &dsty);

	while (size_copied < size && level <= max_level) {
		if (dstx + level_w > TEXRAM_WIDTH || dsty + level_h > TEXRAM_HEIGHT) {
			error("GPU IDMA: out-of-bounds transfer: %s", odin_texhead_str(&texhead));
			break;
		}

		/* copy_level swaps the two texels of each word: whole pairs */
		if (m_before_texture_write) {
			const uint32_t x = dstx & ~1u;

			m_before_texture_write(m_before_texture_write_ctx, bank, x, dsty,
			                       ((dstx + level_w - 1) | 1u) + 1 - x, level_h);
		}
		copy_level(src, m_texram[bank], bus_addr + size_copied,
		           dstx, dsty, level_w, level_h);

		size_copied += level_w * level_h * 2;

		/* The next level: halfway to the right and bottom edges of the
		 * texture RAM, in the other bank */
		dstx += (TEXRAM_WIDTH - dstx) / 2;
		dsty += (TEXRAM_HEIGHT - dsty) / 2;
		bank ^= 1;

		level_w = std::max(level_w >> 1, 1u);
		level_h = std::max(level_h >> 1, 1u);
		level += 1;
	}

	return size_copied;
}

void
Antarctic::copy_level(std::span<const uint8_t> src, std::span<uint8_t> texram, uint32_t bus_addr,
                      uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
	size_t offs = bus_addr & (src.size() - 1);

	if (offs + size_t{w} * h * 2 > src.size())
		fatal("texture transfer past the end of its memory", bus_addr);

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++, offs += 2) {
			const uint32_t dst = ((y0 + y) * TEXRAM_PITCH + (x0 + x) * 2) ^ 2;

			std::memcpy(&texram[dst], &src[offs], 2);
		}
	}
}

} /* namespace odin */

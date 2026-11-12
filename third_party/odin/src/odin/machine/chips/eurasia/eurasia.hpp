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

#ifndef ODIN_EURASIA_HPP
#define ODIN_EURASIA_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "odin/machine/chips/eurasia/eurasia_layers.hpp"

namespace odin {

/* Everything the display chip holds. Plain data. */
struct EurasiaState {
	uint32_t regs[0x104 / 4];	/* 1A000000 - 1A000103 */
	uint32_t unit[2][0x40 / 4];	/* 1A000180 and 1A000200 */
	uint32_t blit[4];		/* 1A040000 - 1A04000F */
	uint32_t pci[0x20 / 4];		/* 18001000 - 1800101F */
	odin_layers_t layers;
};

/* The display chip of the Hikaru, 315-6087 "Eurasia": the screen and its
 * timing, the two 2D layers it shows with the 3D picture, a blitter in the
 * framebuffer RAM, and four interrupt causes. Its registers are at 1A000000
 * for the master SH-4, its PCI configuration at 18001000. See eurasia.cpp
 * for the registers. */
class Eurasia {
public:
	/* The interrupt causes, as raise() takes them */
	enum : uint32_t {
		IRQ_VBLANK = 0x02,
		IRQ_PLOT_END = 0x04
	};
	/* What the board has to do after a write */
	enum class Written {
		UNKNOWN,	/* not a register the chip is known to have */
		DONE,
		CAUSES,		/* the interrupt causes changed */
		BUSY		/* 1A000024 was written */
	};

	/* fb: the 8 MB of the framebuffer RAM */
	explicit Eurasia(std::span<uint8_t> fb);

	void reset();

	/* The registers. read is false for a register the chip is not
	 * known to have. */
	bool read(uint32_t addr, uint32_t &value) const;
	Written write(uint32_t addr, uint32_t value);

	/* Interrupt causes: raised here, acknowledged by the game through
	 * 1A000008 - 1A000014. */
	void raise(uint32_t causes);
	bool irq() const { return (reg(0x18) & 0xF) != 0; }

	/* The screen */
	void end_of_line(uint32_t line);
	void vblank_in();
	void vblank_out();
	/* The frame as the game set it: dots a line, lines, lines shown. */
	void frame_size(uint32_t &dots, uint32_t &lines, uint32_t &shown) const;

	/* 1A000024, which the games read as busy */
	uint32_t busy() const { return reg(0x24); }
	void set_busy(uint32_t value) { reg(0x24) = value; }

	const odin_layers_t &layers() const { return m_s.layers; }

	const EurasiaState &state() const { return m_s; }
	void load(const EurasiaState &in) { m_s = in; }

	/* Each blit is told there (GPU_LOG_DMA). */
	void set_log(FILE *out) { m_log = out; }
	/* The bytes of the framebuffer RAM a blit wrote are told there. */
	using FbWritten = void (*)(void *ctx, size_t offs, size_t size);
	void set_fb_written(FbWritten fn, void *ctx)
	{
		m_fb_written = fn;
		m_fb_written_ctx = ctx;
	}

private:
	[[noreturn]] void fatal(const char *what, uint32_t addr, uint32_t value) const;

	uint32_t &reg(uint32_t offs) { return m_s.regs[(offs & 0x1FF) / 4]; }
	uint32_t reg(uint32_t offs) const { return m_s.regs[(offs & 0x1FF) / 4]; }
	uint32_t unit(uint32_t n, uint32_t offs) const { return m_s.unit[n][(offs & 0x3F) / 4]; }

	void update_status();
	void read_layers();
	void blit();

	std::span<uint8_t> m_fb;
	EurasiaState m_s;
	FILE *m_log;
	FbWritten m_fb_written;
	void *m_fb_written_ctx;
};

} /* namespace odin */

#endif /* ODIN_EURASIA_HPP */

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

#ifndef ODIN_AICA_HPP
#define ODIN_AICA_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

#include "odin/machine/chips/aica/aica_dsp.hpp"
#include "odin/machine/chips/aica/aica_voice.hpp"

namespace odin {

/* Yamaha AICA: the control side of the chip. Register file, the three
 * timers, the two interrupt controllers (one towards the ARM7 inside the
 * chip, one towards the host CPU), the internal DMA, the ARM reset bit and
 * the RTC; the 64 voices (aica_voice.hpp), the effect processor
 * (aica_dsp.hpp) and the mix of both.
 *
 * Register layout: 32-bit words, 16 significant bits each, byte offsets
 * 0x0000-0x7FFF. Sources: MAME src/devices/sound/aica.cpp (r16/w16 and
 * UpdateReg), the Saturn SCSP user's manual section 4 (same register
 * block), and the Sega AM2 driver uploaded by the games. */

class AicaHost {
public:
	virtual ~AicaHost() = default;
	/* FIQ pin of the ARM7 inside the chip. */
	virtual void arm_fiq(bool level) = 0;
	/* ARM7 reset, register 0x2C00 bit 0 (1 = held in reset). */
	virtual void arm_reset(bool level) = 0;
	/* Interrupt towards the host CPU (SZK_INT on Hikaru). */
	virtual void main_irq(bool level) = 0;
};

struct AicaState {
	std::array<uint8_t, 0x8000> regs;
	std::array<uint16_t, 3> timer_count;
	std::array<uint32_t, 3> timer_step;
	uint16_t mcipd;
	uint16_t irql;
	uint8_t arm_reset;
	uint8_t fiq;
	uint8_t main_irq;
	uint8_t rtc_we;
	uint32_t rtc_seconds;
	uint32_t rtc_samples;
	std::array<AicaVoice::State, 64> voices;
	AicaDsp::State dsp;
	uint32_t odometer;
	uint32_t noise;
};

class Aica {
public:
	static constexpr uint32_t RAM_SIZE = 8u << 20;
	static constexpr uint32_t REG_SIZE = 0x8000;
	static constexpr uint32_t SAMPLE_RATE = 44100;
	static constexpr uint32_t SLOTS = 64;
	static constexpr uint32_t SLOT_SIZE = 0x80;

	Aica(AicaHost &host, std::span<uint8_t> ram);

	/* Power-on, or the reset pin of the board: the clock, on its battery,
	 * is set at power-on only. */
	void reset(bool power_on = true);

	/* Register file, byte offset 0x0000-0x7FFF, size 1, 2 or 4. The same
	 * window is seen by the ARM7 at 0x00800000 and by the host CPU. */
	uint32_t read_reg(uint32_t offset, uint32_t size) { return reg_value(offset, size, true); }
	void write_reg(uint32_t offset, uint32_t size, uint32_t value);
	/* What a read would give, without what it does besides: LP of 0x2810
	 * stays, an unknown register is not logged */
	uint32_t peek_reg(uint32_t offset, uint32_t size) { return reg_value(offset, size, false); }

	/* Wave memory, byte address inside the 8 MB; size 1, 2, 4 or 8 (the
	 * SH-4 DMAC moves 64-bit units through the bridge). */
	uint64_t read_ram(uint32_t addr, uint32_t size) const;
	void write_ram(uint32_t addr, uint32_t size, uint64_t value);

	/* RTC: offset 0 = high 16 bits, 4 = low 16 bits, 8 = write enable.
	 * Seconds, which the software of the Hikaru counts from 1964-01-01
	 * (see rtc_of_host_clock in aica.cpp). */
	uint32_t read_rtc(uint32_t offset) const;
	void write_rtc(uint32_t offset, uint32_t value);
	/* For the battery of the RTC, which the board keeps: by how many
	 * seconds the clock is ahead of the local time of the host, and
	 * whether the software has set the count since the last call. */
	int32_t rtc_ahead() const;
	void set_rtc_ahead(int32_t seconds);
	bool take_rtc_set();

	/* Advances the chip by `n` sample periods (1/44100 s each). */
	void tick(uint32_t n);
	/* EXTS, the two external inputs, for the sample periods to come */
	void set_external(int16_t left, int16_t right) { m_dsp.set_external(left, right); }
	/* The output of the last sample period, left and right, 16 bits */
	int16_t left() const { return m_left; }
	int16_t right() const { return m_right; }

	/* For the tests */
	bool arm_in_reset() const { return m_arm_reset; }
	/* The interrupt line to the host CPU. */
	bool main_irq() const { return m_main_irq; }
	uint32_t ram_mask() const { return m_mem8mb ? 0x7FFFFF : 0x1FFFFF; }
	/* The pages of the wave memory the board watches, for the reads and
	 * the writes of the voices and the DSP (WaveMemory) */
	void watch(const WaveMemory::Watch &watch) { m_watch = watch; }

	void save(AicaState &out) const;
	void load(const AicaState &in);

	/* Lines for unknown or unsupported accesses go to `out`. */
	void set_log(FILE *out) { m_log = out; }

private:
	enum : uint32_t {
		REG_CONTROL = 0x2800,
		REG_MIDI = 0x2808,
		REG_MONITOR_SELECT = 0x280C,
		REG_EG_MONITOR = 0x2810,
		REG_CA_MONITOR = 0x2814,
		REG_DMEA_HI = 0x2880,
		REG_DMEA_LO = 0x2884,
		REG_DRGA = 0x2888,
		REG_DLG = 0x288C,
		REG_TIMA = 0x2890,
		REG_TIMB = 0x2894,
		REG_TIMC = 0x2898,
		REG_SCIEB = 0x289C,
		REG_SCIPD = 0x28A0,
		REG_SCIRE = 0x28A4,
		REG_SCILV0 = 0x28A8,
		REG_SCILV1 = 0x28AC,
		REG_SCILV2 = 0x28B0,
		REG_MCIEB = 0x28B4,
		REG_MCIPD = 0x28B8,
		REG_MCIRE = 0x28BC,
		REG_ARM_RESET = 0x2C00,
		REG_INT_LEVEL = 0x2D00,
		REG_INT_CLEAR = 0x2D04,
		REG_EFFECT_SEND = 0x2000	/* EFSDL, EFPAN: 16 for EFREG, 2 for EXTS */
	};

	/* Interrupt sources, bit numbers shared by SCIxx and MCIxx
	 * (SCSP manual 4.1, MAME aica.cpp SCIEX0..SCITMB, Flycast
	 * InterruptInfo). */
	enum : uint16_t {
		INT_EXTERNAL = 1u << 0,
		INT_MIDI_IN = 1u << 3,
		INT_DMA_END = 1u << 4,
		INT_CPU = 1u << 5,
		INT_TIMER_A = 1u << 6,
		INT_TIMER_B = 1u << 7,
		INT_TIMER_C = 1u << 8,
		INT_MIDI_OUT = 1u << 9,
		INT_SAMPLE = 1u << 10,
		INT_MASK = 0x07FF
	};

	uint16_t rd16(uint32_t offset) const;
	WaveMemory wave() const { return WaveMemory(m_ram.data(), ram_mask(), m_watch); }
	void wr16(uint32_t offset, uint16_t value);
	uint32_t reg_value(uint32_t offset, uint32_t size, bool effects);
	uint16_t read_common(uint32_t offset, bool effects);
	void write_common(uint32_t offset, uint16_t value);
	/* KYONEX: each slot starts or releases as its KYONB bit says */
	void key_slots();
	/* The slot MSLC names for the monitors */
	AicaVoice &monitored();
	/* The voices, direct and through the DSP, to the 16 bits of the DAC */
	void mix();
	uint32_t timer_period(uint32_t index) const;
	void timer_write(uint32_t index);
	void timer_tick(uint32_t index);
	void raise(uint16_t sources);
	void update_arm_irq();
	void update_main_irq();
	uint8_t level_of(uint32_t source) const;
	void run_dma();
	void log_unknown(char rw, uint32_t offset, uint32_t size, uint32_t value);

	AicaHost &m_host;
	std::span<uint8_t> m_ram;
	FILE *m_log;

	std::array<uint8_t, REG_SIZE> m_regs;
	std::array<uint16_t, 3> m_timer_count;
	std::array<uint32_t, 3> m_timer_step;
	uint16_t m_mcipd;
	uint16_t m_irql;
	bool m_mem8mb;
	bool m_arm_reset;
	bool m_fiq;
	bool m_main_irq;
	bool m_rtc_we;
	bool m_rtc_set;
	uint32_t m_rtc_seconds;
	uint32_t m_rtc_samples;
	uint32_t m_unknown_logged;
	std::array<AicaVoice, SLOTS> m_voices;
	AicaDsp m_dsp;
	WaveMemory::Watch m_watch;
	uint32_t m_odometer;
	uint32_t m_noise;
	int16_t m_left;
	int16_t m_right;
};

} /* namespace odin */

#endif /* ODIN_AICA_HPP */

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

#ifndef ODIN_SOUND_BOARD_HPP
#define ODIN_SOUND_BOARD_HPP

#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <vector>

#include "odin/machine/chips/aica/aica.hpp"
#include "odin/machine/cpu/arm7/arm7.hpp"
#include "odin/machine/chips/aica/wave_memory.hpp"

namespace odin {

/* One sound board of the Hikaru: the AICA, its wave memory and the ARM7
 * that runs the sound driver. Time comes in as cycles of the main
 * processor and leaves as sample periods. */
class SoundBoard : public AicaHost, public Arm7Bus {
public:
	/* The ARM7DI runs on the bus clock, 22.5792 MHz (33.8688 MHz * 2 / 3):
	 * 512 cycles per sample period. MAME gives it one cycle out of eight,
	 * to stand for wait states; SGNASCAR loads the sound driver again at
	 * each change of scene and waits for it, and on its cabinet the driver
	 * answers within a frame: 328000 cycles, 20 MHz at least. HEURISTIC:
	 * no wait state. */
	static constexpr uint32_t ARM_CYCLES_PER_SAMPLE = 512;

	/* What a write of the main processor did */
	enum class Written {
		NOWHERE,	/* the board decodes nothing there */
		DONE,
		CLOCK_SET	/* the count of the RTC was set */
	};

	/* The whole board. Plain data. */
	struct State {
		uint64_t sample_acc;
		uint32_t arm_ahead;
		Arm7State arm;
		AicaState aica;
		uint8_t arm_held;
	};

	/* ram: the wave memory, Aica::RAM_SIZE bytes, owned by the caller.
	 * clock_hz: the clock of the cycles given to run. main_irq: called
	 * when the board asks for the interrupt of the main processor, or
	 * drops it. */
	SoundBoard(std::span<uint8_t> ram, uint32_t clock_hz, std::function<void(bool)> main_irq);

	void reset();
	/* The turns of a program that waits are counted without being run
	 * (Arm7::set_turn_replay): on at first. Off is for the tests: the
	 * memory and the states must come out the same. */
	void set_turn_replay(bool on) { m_arm.set_turn_replay(on); }
	/* The ARM7 runs what it decoded once (Arm7::set_decoded): on at
	 * first. */
	void set_arm_decoded(bool on) { m_arm.set_decoded(on); }
	/* Its blocks run as host code (Arm7::set_translate): off until the
	 * machine says. */
	void set_arm_translate(bool on) { m_arm.set_translate(on); }
	uint32_t arm_host_bytes() const { return m_arm.host_bytes(); }
	/* The reset pin of the board: as reset, but the clock keeps its
	 * time. */
	void reset_pin();
	/* The level of the line to the main processor */
	bool main_irq() const { return m_aica.main_irq(); }

	/* The battery of the RTC: the seconds the clock of the board is
	 * ahead of the local time of the host. */
	int32_t rtc_ahead() const { return m_aica.rtc_ahead(); }
	void set_rtc_ahead(int32_t seconds) { m_aica.set_rtc_ahead(seconds); }

	/* The 16 MB page of the board on the bus of the main processor:
	 * registers at 700000, RTC at 710000, wave memory from 800000; 1, 2,
	 * 4 or, in the wave memory, 8 bytes. A read tells whether the board
	 * decodes that offset. */
	bool host_read(uint32_t offset, unsigned size, uint64_t &value);
	Written host_write(uint32_t offset, unsigned size, uint64_t value);
	/* The wave memory from offset on, as host_read() or host_write() of
	 * the page sees it, to the end of a page the ARM7 watches; the ARM7
	 * is told of a read, or of a write, there. Empty below the wave
	 * memory. */
	std::span<const uint8_t> host_ram_read(uint32_t offset);
	std::span<uint8_t> host_ram_write(uint32_t offset);

	/* Advances the board by that many cycles of the main processor. */
	void run(uint32_t cycles);
	/* The same while the reset pin of the board is low: no sample period
	 * passes for the chip, the output stays at 0 for that time. */
	void silence(uint32_t cycles);
	/* What silence() keeps of its count from one call to the next, which
	 * the state of the board carries apart from State */
	uint64_t silence_rest() const { return m_silence_acc; }
	void set_silence_rest(uint64_t rest) { m_silence_acc = rest; }
	/* The output since the last clear: left and right, 16 bits each,
	 * 44 100 pairs a second */
	const std::vector<int16_t> &samples() const { return m_samples; }
	/* The board whose output comes to the external inputs (EXTS) of this
	 * one, sample period for sample period: to be run first. */
	void set_source(const SoundBoard *source) { m_source = source; }
	void clear_samples() { m_samples.clear(); }

	/* The ARM7 only counts the turns of a program that waits: this puts
	 * in the wave memory what it has not written yet. To be called
	 * before the memory is read from outside the board. */
	void settle() { m_arm.settle(); }

	void set_log(FILE *out) { m_aica.set_log(out); }
	/* ARM7 instruction and bus trace in the charter format; nullptr
	 * turns it off. */
	void set_arm_trace(FILE *out) { m_arm.set_trace(out); }

	void save(State &out);
	void load(const State &in);

	/* AicaHost */
	void arm_fiq(bool level) override { m_arm.set_fiq(level); }
	void arm_reset(bool level) override;
	void main_irq(bool level) override;

	/* Arm7Bus: wave memory below 800000, registers at 800000, nothing
	 * else (MAME arm0_map: unmap_value_high). */
	uint8_t read8(uint32_t addr) override { return static_cast<uint8_t>(read(addr, 1)); }
	uint16_t read16(uint32_t addr) override { return static_cast<uint16_t>(read(addr, 2)); }
	uint32_t read32(uint32_t addr) override { return read(addr, 4); }
	void write8(uint32_t addr, uint8_t value) override { write(addr, 1, value); }
	void write16(uint32_t addr, uint16_t value) override { write(addr, 2, value); }
	void write32(uint32_t addr, uint32_t value) override { write(addr, 4, value); }
	uint32_t peek(uint32_t addr, uint32_t size) override;

private:
	/* The voices and the DSP tell the ARM7 they use the memory */
	class ArmWatcher : public WaveMemory::Watcher {
	public:
		explicit ArmWatcher(Arm7 &arm) : m_arm(arm) {}
		void reading(uint32_t addr, uint32_t size) override { m_arm.ram_read(addr, size); }
		void writing(uint32_t addr, uint32_t size) override { m_arm.ram_write(addr, size); }

	private:
		Arm7 &m_arm;
	};

	uint32_t read(uint32_t addr, uint32_t size);
	void write(uint32_t addr, uint32_t size, uint32_t value);
	void map_ram();

	std::function<void(bool)> m_main_irq;
	uint32_t m_clock_hz;
	uint64_t m_sample_acc;
	uint64_t m_silence_acc;
	bool m_arm_held;
	/* The cycles the last instruction of the ARM7 ran past the end of the
	 * last sample period: they come off the next one */
	uint32_t m_arm_ahead;
	uint8_t *m_ram;
	Arm7 m_arm;
	ArmWatcher m_watcher;
	Aica m_aica;
	std::vector<int16_t> m_samples;
	const SoundBoard *m_source;
};

} /* namespace odin */

#endif /* ODIN_SOUND_BOARD_HPP */

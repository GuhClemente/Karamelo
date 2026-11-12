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

#include "odin/machine/chips/aica/aica.hpp"
#include "odin/common/bytes.hpp"
#include "odin/common/repeats.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <ctime>

namespace odin {

namespace {

/* The count of the RTC on a board whose clock was set to the local time of
 * the host. The chip counts seconds; the BOOTROM, which sets the count in
 * its CLOCK SETTING screen, takes it from 1964-01-01 00:00 (its date routine
 * @0C008464, the same in the libraries of the games: four years of 1461
 * days from a leap one, year 1964 + ..., day of the week (days + 3) mod 7).
 * Right until 2100, as that routine is. */
uint32_t
rtc_of_host_clock()
{
	const time_t now = time(nullptr);
	const std::tm *local = std::localtime(&now);
	const uint32_t years = local->tm_year > 64 ? static_cast<uint32_t>(local->tm_year - 64) : 0;
	const uint32_t days = years * 365 + (years + 3) / 4 + static_cast<uint32_t>(local->tm_yday);

	return days * 86400 +
	       static_cast<uint32_t>(local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec);
}

} /* namespace */

Aica::Aica(AicaHost &host, std::span<uint8_t> ram)
	: m_host(host), m_ram(ram), m_log(nullptr), m_watch{}
{
	assert(ram.size() == RAM_SIZE);
	reset();
}

void
Aica::reset(bool power_on)
{
	m_regs.fill(0);
	m_timer_count.fill(0);
	m_timer_step.fill(1);
	m_mcipd = 0;
	m_irql = 0;
	m_mem8mb = false;
	/* The ARM7 waits in reset until the host clears bit 0 of 0x2C00:
	 * the AM2 driver upload sets the bit, copies the code, clears it
	 * (von4 snd_upload_driver_reset_arm, 0x0C1B8F88). */
	m_arm_reset = true;
	m_fiq = false;
	m_main_irq = false;
	m_rtc_we = false;
	m_rtc_set = false;
	m_unknown_logged = 0;
	for (AicaVoice &voice : m_voices)
		voice.reset();
	m_dsp.reset();
	m_odometer = 0;
	m_noise = 0;
	m_left = 0;
	m_right = 0;

	if (power_on) {
		m_rtc_seconds = rtc_of_host_clock();
		m_rtc_samples = 0;
	}

	m_host.arm_reset(true);
	m_host.arm_fiq(false);
	m_host.main_irq(false);
}

/* -- register file ------------------------------------------------------- */

uint16_t
Aica::rd16(uint32_t offset) const
{
	return le16(&m_regs[offset]);
}

void
Aica::wr16(uint32_t offset, uint16_t value)
{
	m_regs[offset] = static_cast<uint8_t>(value);
	m_regs[offset + 1] = static_cast<uint8_t>(value >> 8);
}

uint32_t
Aica::reg_value(uint32_t offset, uint32_t size, bool effects)
{
	offset &= REG_SIZE - 1;
	/* Only the low 16 bits of every 32-bit word exist. */
	const uint32_t word = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	uint32_t value;

	if (word == REG_EG_MONITOR) {
		/* Reading the high byte clears LP, as a whole read does (Flycast);
		 * AFSEL shows the filter envelope (Corlett) */
		const bool clear = effects && (size != 1 || (offset & 1) != 0);
		value = monitored().monitor((rd16(REG_MONITOR_SELECT) & 0x4000) != 0, clear);
	}
	else if (word == REG_CA_MONITOR)
		value = monitored().position();
	else if (word >= REG_CONTROL && word < AicaDsp::REG_COEF)
		value = read_common(word, effects);
	else if (word >= AicaDsp::REG_TEMP && word < AicaDsp::REG_END)
		value = m_dsp.read(word);
	else
		value = rd16(word);

	value >>= shift;
	return size == 1 ? value & 0xFF : value & 0xFFFF;
}

void
Aica::write_reg(uint32_t offset, uint32_t size, uint32_t value)
{
	offset &= REG_SIZE - 1;
	const uint32_t word = offset & ~3u;
	const uint32_t shift = (offset & 3) * 8;
	const bool work = word >= AicaDsp::REG_TEMP && word < AicaDsp::REG_END;
	uint16_t merged = work ? m_dsp.read(word) : rd16(word);

	if (shift >= 16)
		return;
	if (size == 1) {
		const uint16_t mask = static_cast<uint16_t>(0xFF << shift);
		merged = static_cast<uint16_t>((merged & ~mask) | ((value << shift) & mask));
	} else {
		merged = static_cast<uint16_t>(value & 0xFFFF);
	}

	if (word < SLOTS * SLOT_SIZE) {
		/* KYONEX acts and is not kept (MAME, Flycast) */
		if ((word & (SLOT_SIZE - 1)) == 0 && (merged & 0x8000)) {
			wr16(word, static_cast<uint16_t>(merged & 0x7FFF));
			key_slots();
		} else {
			wr16(word, merged);
		}
	} else if (word < REG_EFFECT_SEND + 18 * 4) {
		/* EFSDL/EFPAN, 18 words. */
		wr16(word, merged);
	} else if (word >= REG_CONTROL && word < AicaDsp::REG_COEF) {
		write_common(word, merged);
	} else if (work) {
		m_dsp.write(word, merged);
	} else if (word < AicaDsp::REG_END) {
		/* DSP coefficients, addresses and program */
		wr16(word, merged);
		if (word >= AicaDsp::REG_MPRO && word < AicaDsp::REG_MPRO_END)
			m_dsp.program_written(m_regs.data());
	} else {
		log_unknown('W', offset, size, value);
	}
}

uint16_t
Aica::read_common(uint32_t word, bool effects)
{
	switch (word) {
	case REG_MCIPD:
		return m_mcipd;
	case REG_INT_LEVEL:
		return m_irql;
	case REG_INT_CLEAR:
		return 0;
	case REG_ARM_RESET:
		return static_cast<uint16_t>(rd16(word) | (m_arm_reset ? 1 : 0));
	default:
		if (word < 0x28C0)
			return rd16(word);
		if (effects)
			log_unknown('R', word, 2, 0);
		return 0;
	}
}

void
Aica::write_common(uint32_t word, uint16_t value)
{
	switch (word) {
	case REG_CONTROL:
		wr16(word, value);
		m_mem8mb = (value & 0x0200) != 0;
		break;
	case AicaDsp::REG_RING:
	case REG_MIDI:
	case REG_MONITOR_SELECT:
	case REG_DMEA_HI:
	case REG_DMEA_LO:
	case REG_DRGA:
		wr16(word, value);
		break;
	case REG_DLG:
		wr16(word, value);
		if (value & 1)
			run_dma();
		break;
	case REG_TIMA:
	case REG_TIMB:
	case REG_TIMC:
		wr16(word, value);
		timer_write((word - REG_TIMA) / 4);
		break;
	case REG_SCIEB:
		wr16(word, value & INT_MASK);
		update_arm_irq();
		break;
	case REG_SCIPD:
		/* Only the CPU-to-CPU bit can be set by software
		 * (SCSP manual: "only 1 can be written"). */
		if (value & INT_CPU)
			raise(INT_CPU);
		break;
	case REG_SCIRE:
		wr16(REG_SCIPD, rd16(REG_SCIPD) & static_cast<uint16_t>(~value));
		update_arm_irq();
		break;
	case REG_SCILV0:
	case REG_SCILV1:
	case REG_SCILV2:
		wr16(word, value & 0xFF);
		update_arm_irq();
		break;
	case REG_MCIEB:
		wr16(word, value & INT_MASK);
		update_main_irq();
		break;
	case REG_MCIPD:
		if (value & INT_CPU) {
			m_mcipd |= INT_CPU;
			update_main_irq();
		}
		break;
	case REG_MCIRE:
		m_mcipd &= static_cast<uint16_t>(~value);
		update_main_irq();
		break;
	case REG_ARM_RESET:
		wr16(word, value);
		if (((value & 1) != 0) != m_arm_reset) {
			m_arm_reset = (value & 1) != 0;
			m_host.arm_reset(m_arm_reset);
		}
		break;
	case REG_INT_LEVEL:
		m_irql = value;
		break;
	case REG_INT_CLEAR:
		/* End of the ARM FIQ handler: drops the request. Pending bits
		 * stay until SCIRE (MAME aica.cpp w16 0x2D04). */
		if (value & 1) {
			m_fiq = false;
			m_host.arm_fiq(false);
		}
		break;
	default:
		/* 0x28C0-0x2FFF: no register there, writes are dropped (MAME w16
		 * stores up to 0x28BD only; the drivers clear the whole block). */
		if (word < 0x28C0)
			wr16(word, value);
		break;
	}
}

/* -- voices -------------------------------------------------------------- */

void
Aica::key_slots()
{
	const WaveMemory memory = wave();

	for (uint32_t slot = 0; slot < SLOTS; slot++) {
		if (rd16(slot * SLOT_SIZE) & 0x4000)
			m_voices[slot].key_on(&m_regs[slot * SLOT_SIZE], memory, m_noise);
		else
			m_voices[slot].key_off();
	}
}

AicaVoice &
Aica::monitored()
{
	return m_voices[(rd16(REG_MONITOR_SELECT) >> 8) & 0x3F];
}

namespace {

/* A send level (0: none, then -3 dB a step from 15) and a pan, as the
 * tables of the manual (4.2.7) give them, in a factor of 4 or 3 and a
 * shift for each side (yam.c convert_stereo_send_level); 0x0F and 0x1F
 * silence one side. */
constexpr uint8_t PAN_LEFT[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 32 };
constexpr uint8_t PAN_RIGHT[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 32 };

int32_t
sent(int32_t sample, uint32_t attenuation)
{
	const uint32_t shift = (attenuation >> 1) + 2;

	return shift >= 16 ? 0 : (sample * (4 - static_cast<int32_t>(attenuation & 1))) >> shift;
}

} /* namespace */

void
Aica::mix()
{
	const uint16_t control = rd16(REG_CONTROL);
	const WaveMemory memory = wave();
	int32_t left = 0, right = 0;

	for (uint32_t slot = 0; slot < SLOTS; slot++) {
		AicaVoice &voice = m_voices[slot];

		if (voice.silent())
			continue;

		const uint8_t *const regs = &m_regs[slot * SLOT_SIZE];
		/* 20 bits from here to MVOL */
		const int32_t sample = voice.output(regs, m_noise);
		const uint16_t direct = rd16(slot * SLOT_SIZE + 0x24);
		const uint16_t effect = rd16(slot * SLOT_SIZE + 0x20);
		const uint32_t level = (direct >> 8) & 0xF;
		/* MONO centres every pan */
		const uint32_t pan = (control & 0x8000) ? 0 : direct & 0x1F;

		if (level) {
			left += sent(sample, (level ^ 0xF) + PAN_LEFT[pan]);
			right += sent(sample, (level ^ 0xF) + PAN_RIGHT[pan]);
		}
		/* IMXL to the MIXS ISEL names */
		if (effect & 0xF0)
			m_dsp.send(effect & 0xF, sent(sample, ((effect >> 4) & 0xF) ^ 0xF));
		voice.step(regs, memory, m_odometer, m_noise);
	}

	/* The 16 outputs of the DSP, then the 2 external inputs, through EFSDL
	 * and EFPAN */
	m_dsp.run(m_regs.data(), memory);
	for (uint32_t n = 0; n < 18; n++) {
		const uint16_t send = rd16(REG_EFFECT_SEND + n * 4);
		const uint32_t level = (send >> 8) & 0xF;

		if (!level)
			continue;

		const int32_t sample = (n < 16 ? m_dsp.effect(n) : m_dsp.external(n - 16)) * 16;
		const uint32_t pan = (control & 0x8000) ? 0 : send & 0x1F;

		left += sent(sample, (level ^ 0xF) + PAN_LEFT[pan]);
		right += sent(sample, (level ^ 0xF) + PAN_RIGHT[pan]);
	}

	/* MVOL, then the 16 bits of the DAC */
	const uint32_t master = (control & 0xF) ^ 0xF;
	const int32_t factor = 4 - static_cast<int32_t>(master & 1);
	const uint32_t shift = (master >> 1) + 6;

	m_left = static_cast<int16_t>(std::clamp((left * factor) >> shift, -0x8000, 0x7FFF));
	m_right = static_cast<int16_t>(std::clamp((right * factor) >> shift, -0x8000, 0x7FFF));
}

/* -- wave memory --------------------------------------------------------- */

uint64_t
Aica::read_ram(uint32_t addr, uint32_t size) const
{
	addr &= ram_mask();
	uint64_t value = 0;
	for (uint32_t i = 0; i < size && i < 8; i++)
		value |= static_cast<uint64_t>(m_ram[(addr + i) & ram_mask()]) << (8 * i);
	return value;
}

void
Aica::write_ram(uint32_t addr, uint32_t size, uint64_t value)
{
	addr &= ram_mask();
	for (uint32_t i = 0; i < size && i < 8; i++)
		m_ram[(addr + i) & ram_mask()] = static_cast<uint8_t>(value >> (8 * i));
}

/* -- RTC ----------------------------------------------------------------- */

uint32_t
Aica::read_rtc(uint32_t offset) const
{
	switch (offset & 0xC) {
	case 0:
		return m_rtc_seconds >> 16;
	case 4:
		return m_rtc_seconds & 0xFFFF;
	default:
		return 0;
	}
}

int32_t
Aica::rtc_ahead() const
{
	return static_cast<int32_t>(m_rtc_seconds - rtc_of_host_clock());
}

void
Aica::set_rtc_ahead(int32_t seconds)
{
	m_rtc_seconds = rtc_of_host_clock() + static_cast<uint32_t>(seconds);
	m_rtc_samples = 0;
}

bool
Aica::take_rtc_set()
{
	const bool set = m_rtc_set;

	m_rtc_set = false;
	return set;
}

void
Aica::write_rtc(uint32_t offset, uint32_t value)
{
	switch (offset & 0xC) {
	case 0:
		if (m_rtc_we) {
			m_rtc_seconds = (m_rtc_seconds & 0xFFFF) | ((value & 0xFFFF) << 16);
			m_rtc_we = false;
			m_rtc_set = true;
		}
		break;
	case 4:
		if (m_rtc_we) {
			m_rtc_seconds = (m_rtc_seconds & 0xFFFF0000) | (value & 0xFFFF);
			m_rtc_samples = 0;
			m_rtc_set = true;
		}
		break;
	case 8:
		m_rtc_we = (value & 1) != 0;
		break;
	default:
		break;
	}
}

/* -- timers and interrupts ----------------------------------------------- */

uint32_t
Aica::timer_period(uint32_t index) const
{
	return 1u << ((rd16(REG_TIMA + index * 4) >> 8) & 7);
}

void
Aica::timer_write(uint32_t index)
{
	/* Bits 7:0 start value, bits 10:8 prescaler: the counter advances
	 * once every 2^prescaler samples and interrupts when it overflows
	 * (SCSP manual TIMA/TACTL; Flycast AicaTimer). MAME fires one
	 * increment earlier. */
	m_timer_count[index] = rd16(REG_TIMA + index * 4) & 0xFF;
	m_timer_step[index] = timer_period(index);
}

void
Aica::timer_tick(uint32_t index)
{
	static constexpr uint16_t sources[3] = { INT_TIMER_A, INT_TIMER_B, INT_TIMER_C };
	const uint32_t word = REG_TIMA + index * 4;
	uint16_t count = static_cast<uint16_t>(m_timer_count[index] + 1);

	if (count > 0xFF) {
		count = 0;
		raise(sources[index]);
	}
	m_timer_count[index] = count;
	wr16(word, static_cast<uint16_t>((rd16(word) & 0xFF00) | count));
}

void
Aica::raise(uint16_t sources)
{
	wr16(REG_SCIPD, rd16(REG_SCIPD) | sources);
	m_mcipd |= sources;
	update_arm_irq();
	update_main_irq();
}

uint8_t
Aica::level_of(uint32_t source) const
{
	/* SCILV0..2 give bit 0..2 of the level for sources 0..7; sources
	 * 8 and above share bit 7 (MAME DecodeSCI, Flycast GetL). */
	const uint32_t bit = 1u << (source > 7 ? 7 : source);
	uint8_t level = 0;
	if (rd16(REG_SCILV0) & bit)
		level |= 1;
	if (rd16(REG_SCILV1) & bit)
		level |= 2;
	if (rd16(REG_SCILV2) & bit)
		level |= 4;
	return level;
}

void
Aica::update_arm_irq()
{
	const uint16_t pending = static_cast<uint16_t>(rd16(REG_SCIPD) & rd16(REG_SCIEB) & INT_MASK);
	if (pending == 0)
		return;
	for (uint32_t source = 0; source < 11; source++) {
		if (pending & (1u << source)) {
			m_irql = level_of(source);
			break;
		}
	}
	if (!m_fiq) {
		m_fiq = true;
		m_host.arm_fiq(true);
	}
}

void
Aica::update_main_irq()
{
	const bool level = (m_mcipd & rd16(REG_MCIEB) & INT_MASK) != 0;
	if (level != m_main_irq) {
		m_main_irq = level;
		m_host.main_irq(level);
	}
}

void
Aica::tick(uint32_t n)
{
	for (uint32_t s = 0; s < n; s++) {
		mix();
		m_odometer++;
		for (uint32_t t = 0; t < 3; t++) {
			if (--m_timer_step[t] == 0) {
				m_timer_step[t] = timer_period(t);
				timer_tick(t);
			}
		}
		/* One interrupt per sample period, when enabled
		 * (SCSP manual bit 10; Flycast SAMPLE_DONE). */
		if ((rd16(REG_SCIEB) | rd16(REG_MCIEB)) & INT_SAMPLE)
			raise(INT_SAMPLE);
		if (++m_rtc_samples == SAMPLE_RATE) {
			m_rtc_samples = 0;
			m_rtc_seconds++;
		}
	}
}

/* -- internal DMA (wave memory <-> registers) ---------------------------- */

void
Aica::run_dma()
{
	/* DMEA: wave memory address (bits 22:16 in 0x2880, 15:2 in 0x2884);
	 * DRGA: register address; DLG: length in words; DDIR 1 = registers
	 * to memory; DGATE 1 = write zeros (MAME exec_dma, Flycast
	 * AicaInternalDMA). */
	uint32_t dmea = ((rd16(REG_DMEA_HI) & 0xFE00) << 7) | (rd16(REG_DMEA_LO) & 0xFFFC);
	uint32_t drga = rd16(REG_DRGA) & 0x7FFC;
	const uint32_t gate = (rd16(REG_DRGA) >> 15) & 1;
	const uint32_t dlg = rd16(REG_DLG) & 0x7FFC;
	const uint32_t dir = (rd16(REG_DLG) >> 15) & 1;

	for (uint32_t i = 0; i < dlg; i += 2) {
		if (dir) {
			const uint32_t v = gate ? 0 : read_reg(drga, 2);
			write_ram(dmea, 2, v);
		} else {
			const uint32_t v = gate ? 0 : static_cast<uint32_t>(read_ram(dmea, 2));
			write_reg(drga, 2, v);
		}
		dmea += 4;
		drga += 4;
	}
	wr16(REG_DLG, rd16(REG_DLG) & 0xFFFE);
	raise(INT_DMA_END);
}

/* -- state --------------------------------------------------------------- */

void
Aica::save(AicaState &out) const
{
	out.regs = m_regs;
	out.timer_count = m_timer_count;
	out.timer_step = m_timer_step;
	out.mcipd = m_mcipd;
	out.irql = m_irql;
	out.arm_reset = m_arm_reset;
	out.fiq = m_fiq;
	out.main_irq = m_main_irq;
	out.rtc_we = m_rtc_we;
	out.rtc_seconds = m_rtc_seconds;
	out.rtc_samples = m_rtc_samples;
	for (uint32_t slot = 0; slot < SLOTS; slot++)
		out.voices[slot] = m_voices[slot].state();
	out.dsp = m_dsp.state();
	out.odometer = m_odometer;
	out.noise = m_noise;
}

void
Aica::load(const AicaState &in)
{
	m_regs = in.regs;
	m_timer_count = in.timer_count;
	m_timer_step = in.timer_step;
	m_mcipd = in.mcipd;
	m_irql = in.irql;
	m_mem8mb = (rd16(REG_CONTROL) & 0x0200) != 0;
	m_arm_reset = in.arm_reset != 0;
	m_fiq = in.fiq != 0;
	m_main_irq = in.main_irq != 0;
	m_rtc_we = in.rtc_we != 0;
	m_rtc_seconds = in.rtc_seconds;
	m_rtc_samples = in.rtc_samples;
	for (uint32_t slot = 0; slot < SLOTS; slot++)
		m_voices[slot].load(in.voices[slot]);
	m_dsp.load(in.dsp, m_regs.data());
	m_odometer = in.odometer;
	m_noise = in.noise;
	m_host.arm_reset(m_arm_reset);
	m_host.arm_fiq(m_fiq);
	m_host.main_irq(m_main_irq);
}

void
Aica::log_unknown(char rw, uint32_t offset, uint32_t size, uint32_t value)
{
	static const char FORMAT[] = "aica: unknown register %c offset %04X size %u value %08X\n";
	char line[96];

	if (!m_log || m_unknown_logged >= 64)
		return;
	m_unknown_logged++;
	std::snprintf(line, sizeof(line), FORMAT, rw, offset, size, value);
	repeats::put(m_log, FORMAT, line);
}

} /* namespace odin */

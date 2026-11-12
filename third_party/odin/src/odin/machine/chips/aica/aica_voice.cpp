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

#include "odin/machine/chips/aica/aica_voice.hpp"

#include <algorithm>
#include <array>

namespace odin {

namespace {

/* The registers of a slot, at their offset in it */
enum : uint32_t {
	REG_PLAY = 0x00,	/* KYONB, SSCTL, LPCTL, PCMS, SA 22:16 */
	REG_SA = 0x04,		/* SA 15:0 */
	REG_LSA = 0x08,
	REG_LEA = 0x0C,
	REG_RATES = 0x10,	/* D2R, D1R, AR */
	REG_DECAY = 0x14,	/* LPSLNK, KRS, DL, RR */
	REG_PITCH = 0x18,	/* OCT, FNS */
	REG_LFO = 0x1C,		/* LFORE, LFOF, PLFOWS, PLFOS, ALFOWS, ALFOS */
	REG_LEVEL = 0x28,	/* TL, VOFF, LPOFF, Q */
	REG_FLV0 = 0x2C,	/* FLV0 to FLV4, a word apart */
	REG_CUTOFF_RATES = 0x40,	/* FAR, FD1R */
	REG_CUTOFF_RELEASE = 0x44	/* FD2R, FRR */
};

enum : uint32_t {
	PLAY_NOISE = 0x0400,	/* SSCTL */
	PLAY_LOOP = 0x0200,	/* LPCTL */
	DECAY_LINK = 0x4000,	/* LPSLNK */
	LFO_RESET = 0x8000,	/* LFORE */
	LEVEL_OFF = 0x0040,	/* VOFF: no TL, envelope or amplitude LFO */
	LEVEL_NO_FILTER = 0x0020	/* LPOFF */
};

/* Q, the resonance: what it adds to the cutoff, in 4096ths (Flycast) */
constexpr int64_t Q_TABLE[32] = {
	2048, 1536, 1024, 512, 0, -256, -512, -768, -1024, -1280, -1536, -1792, -2048, -2176, -2304, -2432,
	-2560, -2688, -2816, -2944, -3072, -3136, -3200, -3264, -3328, -3392, -3456, -3520, -3584, -3648, -3712, -3776
};

enum : uint32_t { PCM16, PCM8, ADPCM, ADPCM_STREAM };

/* One sample of the sound, in the phase */
constexpr uint32_t ONE = 0x40000;
/* The level of a key on; the level from which the voice is silent. With
 * these two, the steps below take the times of the table of the manual. */
constexpr uint32_t ATTACK_START = 0x280;
constexpr uint32_t SILENT = 0x3C0;
/* The fastest rate: those above it are as fast */
constexpr uint32_t FASTEST = 0x3C;
/* From rate 0x30 up, by the sample period modulo 4: the shift of an attack
 * step and the size of a decay step (yam.c envattackshift and
 * envdecayvalue). Below, 4 and 1. */
constexpr uint8_t FAST_SHIFT[FASTEST - 0x30 + 1][4] = {
	{ 4, 4, 4, 4 }, { 3, 4, 4, 4 }, { 3, 4, 3, 4 }, { 3, 3, 3, 4 }, { 3, 3, 3, 3 },
	{ 2, 3, 3, 3 }, { 2, 3, 2, 3 }, { 2, 2, 2, 3 }, { 2, 2, 2, 2 }, { 1, 2, 2, 2 },
	{ 1, 2, 1, 2 }, { 1, 1, 1, 2 }, { 1, 1, 1, 1 }
};
constexpr uint8_t FAST_DECAY[FASTEST - 0x30 + 1][4] = {
	{ 1, 1, 1, 1 }, { 2, 1, 1, 1 }, { 2, 1, 2, 1 }, { 2, 2, 2, 1 }, { 2, 2, 2, 2 },
	{ 4, 2, 2, 2 }, { 4, 2, 4, 2 }, { 4, 4, 4, 2 }, { 4, 4, 4, 4 }, { 8, 4, 4, 4 },
	{ 8, 4, 8, 4 }, { 8, 8, 8, 4 }, { 8, 8, 8, 8 }
};

/* Yamaha ADPCM, as MAME, Flycast and Corlett agree: a code moves the sample
 * by a multiple of the step, and the step grows or shrinks with it. */
constexpr int32_t ADPCM_SIZE[8] = { 1, 3, 5, 7, 9, 11, 13, 15 };
constexpr uint32_t ADPCM_SCALE[8] = { 0xE6, 0xE6, 0xE6, 0xE6, 0x133, 0x199, 0x200, 0x266 };

/* The LFO steps through 256 values, one every O(LFOF) sample periods
 * (Flycast; the frequencies of the manual, 0.17 to 172.3 Hz): its phase
 * moves by 2^32 / (256 O) a period. */
constexpr uint32_t lfo_step(uint32_t lfof)
{
	const uint32_t g = 128u >> (lfof >> 2);
	const uint32_t o = ((g - 1) << 2) + g * (((~lfof) & 3) + 1);

	return static_cast<uint32_t>((1ull << 32) / (256ull * o));
}

constexpr auto LFO_STEP = [] {
	std::array<uint32_t, 32> steps{};
	for (uint32_t lfof = 0; lfof < 32; lfof++)
		steps[lfof] = lfo_step(lfof);
	return steps;
}();

int32_t
adpcm(uint32_t code, int32_t last, uint32_t &step)
{
	int32_t delta = std::min(static_cast<int32_t>(step) * ADPCM_SIZE[code & 7] / 8, 0x7FFF);

	if (code & 8)
		delta = -delta;
	step = std::clamp((step * ADPCM_SCALE[code & 7]) >> 8, 0x7Fu, 0x6000u);
	return std::clamp(last + delta, -0x8000, 0x7FFF);
}

/* The rate of a segment, raised with the key by KRS: twice the octave plus
 * KRS, never below the rate itself, and the top bit of FNS. */
uint32_t
effective_rate(uint32_t rate, const uint8_t *slot)
{
	const uint32_t decay = aica_reg(slot, REG_DECAY);
	const uint32_t pitch = aica_reg(slot, REG_PITCH);
	const uint32_t krs = (decay >> 10) & 0xF;
	uint32_t effective = rate * 2;

	if (krs != 0xF) {
		const int32_t octave = static_cast<int32_t>(((pitch >> 11) & 0xF) ^ 8) - 8;

		effective += (pitch >> 9) & 1;
		effective += static_cast<uint32_t>(std::max(0, (static_cast<int32_t>(krs) + octave) * 2));
	}
	return std::min(effective, FASTEST);
}

/* How far a decay step moves the level at this rate */
uint32_t
decay_size(uint32_t effective, uint32_t odometer)
{
	return effective >= 0x30 ? FAST_DECAY[effective - 0x30][odometer & 3] : 1;
}

/* Whether the envelope moves at this sample period: below rate 0x30, once
 * in 2^n periods along a pattern of eight; from 0x30, every second one. */
bool
steps(uint32_t rate, uint32_t odometer)
{
	if (rate <= 1)
		return false;
	if (rate >= 0x30)
		return (odometer & 1) == 0;

	const uint32_t shift = 12 - ((rate - 1) >> 2);

	if (odometer & ((1u << shift) - 1))
		return false;
	return ((0xFFFDDDD5u >> (((rate - 1) & 3) * 8 + ((odometer >> shift) & 7))) & 1) != 0;
}

/* The last sample of the sound: an ADPCM stream ends on a group of four */
uint32_t
loop_end(uint32_t play, const uint8_t *slot)
{
	const uint32_t lea = aica_reg(slot, REG_LEA);

	return ((play >> 7) & 3) == ADPCM_STREAM ? (lea + 3) & ~3u : lea;
}

uint32_t
noise_word(uint32_t &noise)
{
	noise = noise * 0x41C64E6Du + 0x3039u;
	return noise >> 16;
}

int32_t
noise_sample(uint32_t &noise)
{
	return static_cast<int16_t>(noise_word(noise));
}

} /* namespace */

void
AicaVoice::reset()
{
	m_state = {};
	m_state.level = OFF;
	m_state.segment = RELEASE;
}

void
AicaVoice::key_on(const uint8_t *slot, const WaveMemory &memory, uint32_t &noise)
{
	if (m_state.segment != RELEASE)
		return;
	m_state = {};
	m_state.adpcm_step = 0x7F;
	m_state.level = ATTACK_START;
	m_state.segment = ATTACK;
	m_state.playing = 1;
	m_state.cutoff = aica_reg(slot, REG_FLV0) & 0x1FFF;
	m_state.cutoff_segment = ATTACK;
	fetch(slot, memory, noise);
}

int32_t
AicaVoice::output(const uint8_t *slot, uint32_t &noise)
{
	const int32_t f = static_cast<int32_t>((m_state.phase >> 4) & 0x3FFF);
	const int32_t s = filter(slot, ((m_state.next * f + m_state.sample * (0x4000 - f)) >> 14) * 16);
	const uint32_t level = aica_reg(slot, REG_LEVEL);
	const uint32_t lfo = aica_reg(slot, REG_LFO);

	if (level & LEVEL_OFF)
		return s;

	/* The attenuation: TL, the envelope and the amplitude LFO, all in
	 * steps of 6 dB / 64 */
	uint32_t attenuation = ((level >> 8) << 2) + m_state.level;

	if (lfo & 7) {
		const uint32_t phase = m_state.lfo_phase;
		uint32_t wave;

		switch ((lfo >> 3) & 3) {
		case 0:		/* sawtooth, down from the top */
			wave = phase >> 24;
			break;
		case 1:		/* square */
			wave = (phase & 0x80000000u) ? 0xFF : 0;
			break;
		case 2:		/* triangle */
			wave = ((phase >> 23) & 0xFF) ^ ((phase & 0x80000000u) ? 0xFF : 0);
			break;
		default:
			wave = noise_word(noise) & 0xFF;
			break;
		}
		attenuation += wave >> (7 - (lfo & 7));
	}
	if (attenuation >= SILENT)
		return 0;
	return (s * static_cast<int32_t>(((attenuation & 0x3F) ^ 0x7F) + 1)) >> (7 + (attenuation >> 6));
}

/* A two-pole low-pass on 20 bits: y = -a0 x + b1 y1 - b2 y2, its
 * coefficients from the cutoff (4 bits of exponent, 9 of mantissa) and Q
 * (lowPassFilter of Flycast). Left out while it is wide open. */
int32_t
AicaVoice::filter(const uint8_t *slot, int32_t sample)
{
	const uint32_t level = aica_reg(slot, REG_LEVEL);
	const uint32_t q = level & 0x1F;

	if (level & LEVEL_NO_FILTER)
		return sample;

	bool open = q == 4;
	for (uint32_t n = 0; n < 5 && open; n++)
		open = (aica_reg(slot, REG_FLV0 + n * 4) & 0x1FFF) >= 0x1FF8;
	if (open)
		return sample;

	const uint32_t exponent = m_state.cutoff >> 9;
	const uint64_t key = (static_cast<uint64_t>(m_state.cutoff) << 5) | q;
	if (key != m_coef_key) {
		const uint64_t mantissa = (m_state.cutoff & 0x1FF) | 0x200;
		const int64_t a0 = static_cast<int64_t>(((mantissa << 30) >> ((15 - exponent) * 2)) * ((mantissa - 1) / 8) >> 17);
		int64_t f = static_cast<int64_t>(mantissa << exponent) << 5;

		f += Q_TABLE[q] * f / 4096;

		m_coef_key = key;
		m_a0 = a0;
		m_b1 = (1ll << 31) - (f + a0);
		m_b2 = (1ll << 30) - f;
	}

	if (exponent == 0)
		m_state.rest = 0;

	const int64_t sum = -m_a0 * sample + m_b1 * m_state.filtered[0] - m_b2 * m_state.filtered[1] - m_state.rest;
	const int32_t out = static_cast<int32_t>(sum >> 30);

	m_state.rest = static_cast<int32_t>((static_cast<int64_t>(out) << 30) - sum);
	m_state.filtered[1] = m_state.filtered[0];
	m_state.filtered[0] = std::clamp(out, -0x80000, 0x7FFFF);
	return m_state.filtered[0];
}

void
AicaVoice::key_off()
{
	m_state.segment = RELEASE;
	m_state.cutoff_segment = RELEASE;
}

void
AicaVoice::step(const uint8_t *slot, const WaveMemory &memory, uint32_t odometer, uint32_t &noise)
{
	if (silent())
		return;
	envelope(slot, odometer);
	cutoff_envelope(slot, odometer);
	if (!m_state.playing)
		return;

	const uint32_t lfo = aica_reg(slot, REG_LFO);

	m_state.phase += increment(slot, noise);
	/* LFORE holds the LFO at its start (manual, 4.2.6) */
	m_state.lfo_phase = (lfo & LFO_RESET) ? 0 : m_state.lfo_phase + LFO_STEP[(lfo >> 10) & 0x1F];
	while (m_state.phase >= ONE) {
		m_state.phase -= ONE;
		if (!advance(slot))
			return;
		fetch(slot, memory, noise);
	}
}

uint16_t
AicaVoice::monitor(bool filter, bool clear)
{
	const uint32_t segment = filter ? m_state.cutoff_segment : m_state.segment;
	const uint16_t value = static_cast<uint16_t>((m_state.looped ? 0x8000 : 0) | (segment << 13) |
	                                             (filter ? m_state.cutoff : m_state.level));

	if (clear)
		m_state.looped = 0;
	return value;
}

uint32_t
AicaVoice::increment(const uint8_t *slot, uint32_t &noise) const
{
	const uint32_t pitch = aica_reg(slot, REG_PITCH);
	const uint32_t lfo = aica_reg(slot, REG_LFO);
	const uint32_t base = ((pitch & 0x3FF) | 0x400) << (((pitch >> 11) & 0xF) ^ 8);
	const uint32_t depth = (lfo >> 5) & 7;

	if (depth == 0)
		return base;

	/* The pitch LFO moves the step by up to base / 2^(10 - PLFOS) either
	 * way, along a wave from 0 to 2^32 */
	const uint32_t phase = m_state.lfo_phase;
	uint32_t wave;

	switch ((lfo >> 8) & 3) {
	case 0:
		wave = phase ^ 0x80000000u;
		break;
	case 1:
		wave = (phase & 0x80000000u) ? 0 : 0xFFFFFFFFu;
		break;
	case 2:
		wave = (phase << 1) + 0x80000000u;
		if (phase >= 0x40000000u && phase < 0xC0000000u)
			wave = ~wave;
		break;
	default:
		wave = noise_word(noise) << 16;
		break;
	}

	const uint32_t vary = base >> (10 - depth);

	return base + static_cast<uint32_t>((static_cast<uint64_t>(vary) * 2 * wave) >> 32) - vary;
}

void
AicaVoice::envelope(const uint8_t *slot, uint32_t odometer)
{
	const uint32_t rates = aica_reg(slot, REG_RATES);
	const uint32_t decay = aica_reg(slot, REG_DECAY);
	uint32_t rate;

	switch (m_state.segment) {
	case ATTACK:
		rate = rates & 0x1F;
		break;
	case DECAY1:
		rate = (rates >> 6) & 0x1F;
		break;
	case DECAY2:
		rate = (rates >> 11) & 0x1F;
		break;
	default:
		rate = decay & 0x1F;
		break;
	}
	if (rate == 0)
		return;

	const uint32_t effective = effective_rate(rate, slot);

	if (!steps(effective, odometer))
		return;
	if (m_state.segment == ATTACK) {
		/* LPSLNK holds the top until the loop starts */
		if (m_state.level == 0)
			return;

		const uint32_t shift = effective >= 0x30 ? FAST_SHIFT[effective - 0x30][odometer & 3] : 4;

		m_state.level -= (m_state.level >> shift) + 1;
		if (m_state.level == 0 && !(decay & DECAY_LINK))
			m_state.segment = DECAY1;
		return;
	}
	m_state.level += decay_size(effective, odometer);
	if (m_state.segment == DECAY1 && (m_state.level >> 5) >= ((decay >> 5) & 0x1F))
		m_state.segment = DECAY2;
	if (m_state.level >= SILENT)
		stop();
}

/* The cutoff moves to FLV1 in the attack, to FLV2, FLV3, then to FLV4
 * from the key off, by the steps of the level at the rates FAR, FD1R,
 * FD2R and FRR (Corlett; Flycast the same speed) */
void
AicaVoice::cutoff_envelope(const uint8_t *slot, uint32_t odometer)
{
	const uint32_t rates = aica_reg(slot, REG_CUTOFF_RATES);
	const uint32_t release = aica_reg(slot, REG_CUTOFF_RELEASE);
	uint32_t rate;

	switch (m_state.cutoff_segment) {
	case ATTACK:
		rate = (rates >> 8) & 0x1F;
		break;
	case DECAY1:
		rate = rates & 0x1F;
		break;
	case DECAY2:
		rate = (release >> 8) & 0x1F;
		break;
	default:
		rate = release & 0x1F;
		break;
	}
	if (rate == 0)
		return;

	const uint32_t effective = effective_rate(rate, slot);

	if (!steps(effective, odometer))
		return;

	const uint32_t size = decay_size(effective, odometer);
	/* FLV1 to FLV4 */
	const uint32_t target = aica_reg(slot, REG_FLV0 + (m_state.cutoff_segment + 1u) * 4) & 0x1FFF;
	uint32_t &cutoff = m_state.cutoff;

	if (cutoff < target)
		cutoff += std::min(size, target - cutoff);
	else if (cutoff > target)
		cutoff -= std::min(size, cutoff - target);
	else if (m_state.cutoff_segment < DECAY2)
		m_state.cutoff_segment++;
}

bool
AicaVoice::advance(const uint8_t *slot)
{
	const uint32_t play = aica_reg(slot, REG_PLAY);
	const uint32_t lsa = aica_reg(slot, REG_LSA);
	const uint32_t lea = loop_end(play, slot);
	uint32_t ca = m_state.position + 1;

	if ((aica_reg(slot, REG_DECAY) & DECAY_LINK) && m_state.segment == ATTACK && ca >= lsa)
		m_state.segment = DECAY1;
	if (ca >= lea) {
		if (lsa > lea) {
			/* On past LEA up to LSA, then the end, looping or not */
			if (ca >= lsa) {
				m_state.looped = 1;
				stop();
				return false;
			}
		} else {
			m_state.looped = 1;
			if (!(play & PLAY_LOOP)) {
				stop();
				return false;
			}
			ca = lsa;
		}
	}
	m_state.position = ca;
	return true;
}

void
AicaVoice::fetch(const uint8_t *slot, const WaveMemory &memory, uint32_t &noise)
{
	const uint32_t play = aica_reg(slot, REG_PLAY);
	const uint32_t format = (play >> 7) & 3;
	const uint32_t sa = ((play & 0x7F) << 16) | aica_reg(slot, REG_SA);
	const uint32_t lsa = aica_reg(slot, REG_LSA);
	const uint32_t lea = loop_end(play, slot);
	const uint32_t ca = m_state.position;
	/* The sample after the last one of a loop is its first */
	const uint32_t after = (ca + 1 >= lea && lea > lsa) ? lsa : ca + 1;

	if (play & PLAY_NOISE) {
		m_state.sample = noise_sample(noise);
		m_state.next = noise_sample(noise);
		return;
	}
	switch (format) {
	case PCM16:
		m_state.sample = static_cast<int16_t>(memory.read16((sa & ~1u) + ca * 2));
		m_state.next = static_cast<int16_t>(memory.read16((sa & ~1u) + after * 2));
		return;
	case PCM8:
		m_state.sample = static_cast<int8_t>(memory.read8(sa + ca)) * 256;
		m_state.next = static_cast<int8_t>(memory.read8(sa + after)) * 256;
		return;
	default:
		break;
	}

	const auto code = [&](uint32_t n) {
		return static_cast<uint32_t>(memory.read8(sa + (n >> 1)) >> ((n & 1) * 4)) & 0xF;
	};

	/* The decoder is kept as it was at LSA the first time, and plain ADPCM
	 * takes it back at each turn of the loop; a stream goes on. */
	if (ca == lsa) {
		if (!m_state.in_loop) {
			m_state.in_loop = 1;
			m_state.loop_step = m_state.adpcm_step;
			m_state.loop_sample = m_state.sample;
		} else if (format == ADPCM) {
			m_state.adpcm_step = m_state.loop_step;
			m_state.sample = m_state.loop_sample;
		}
	}
	m_state.sample = adpcm(code(ca), m_state.sample, m_state.adpcm_step);

	uint32_t step = m_state.adpcm_step;
	int32_t last = m_state.sample;

	if (format == ADPCM && after == lsa && m_state.in_loop) {
		step = m_state.loop_step;
		last = m_state.loop_sample;
	}
	m_state.next = adpcm(code(after), last, step);
}

void
AicaVoice::stop()
{
	m_state.playing = 0;
	m_state.position = 0;
	m_state.phase = 0;
	m_state.level = OFF;
	m_state.segment = RELEASE;
}

} /* namespace odin */

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

#ifndef ODIN_AUDIO_OUT_HPP
#define ODIN_AUDIO_OUT_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace odin {

/* The lows of a channel, as a woofer plays them: a second-order Butterworth
 * low-pass at hz (0: the channel as it is). HEURISTIC: no manual gives the
 * crossover of the Gaelco cabinets. */
class LowPass {
public:
	LowPass(double hz = 0, int rate = 1)
	{
		if (hz <= 0 || hz * 2 >= rate)
			return;

		const double w = 2.0 * std::numbers::pi * hz / rate, alpha = std::sin(w) / std::sqrt(2.0), a0 = 1.0 + alpha;

		m_b0 = m_b2 = (1.0 - std::cos(w)) / 2.0 / a0;
		m_b1 = (1.0 - std::cos(w)) / a0;
		m_a1 = -2.0 * std::cos(w) / a0;
		m_a2 = (1.0 - alpha) / a0;
	}

	int16_t operator()(int16_t x)
	{
		const double y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;

		m_x2 = m_x1;
		m_x1 = x;
		m_y2 = m_y1;
		m_y1 = y;
		return static_cast<int16_t>(std::clamp<long>(std::lround(y), -0x8000, 0x7FFF));
	}

private:
	double m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
	double m_x1 = 0, m_x2 = 0, m_y1 = 0, m_y2 = 0;
};

/* The sound of the two boards to the speakers of the host, as the user
 * chooses: the main board, the optional one, both on two channels, or the
 * four channels of the cabinet. */
class AudioOut {
public:
	enum class Mode : uint8_t { MAIN, OPTION, MIX, QUAD };

	/* A gain of 1, the optional board as it is: gains count in 2^-15 */
	static constexpr int32_t UNITY = 1 << 15;

	/* The gain of a level in dB */
	static int32_t gain_of(double db) { return static_cast<int32_t>(std::lround(UNITY * std::pow(10.0, db / 20.0))); }

	/* Left and right pairs of each board into the frames of the mode; in
	 * the mix the optional board at option_gain, as the volume of its
	 * speakers on the cabinet */
	static void interleave(Mode mode, std::span<const int16_t> main, std::span<const int16_t> option,
	                       int32_t option_gain, std::vector<int16_t> &out)
	{
		const auto at = [](std::span<const int16_t> s, size_t i) { return i < s.size() ? s[i] : int16_t{ 0 }; };
		const auto sum = [option_gain](int16_t a, int16_t b) {
			const int64_t lowered = (static_cast<int64_t>(b) * option_gain) >> 15;

			return static_cast<int16_t>(std::clamp<int64_t>(a + lowered, -0x8000, 0x7FFF));
		};

		out.clear();
		for (size_t i = 0; i + 1 < main.size(); i += 2) {
			const int16_t ml = main[i], mr = main[i + 1], ol = at(option, i), orr = at(option, i + 1);

			switch (mode) {
			case Mode::MAIN:
				out.insert(out.end(), { ml, mr });
				break;
			case Mode::OPTION:
				out.insert(out.end(), { ol, orr });
				break;
			case Mode::MIX:
				out.insert(out.end(), { sum(ml, ol), sum(mr, orr) });
				break;
			case Mode::QUAD:
				out.insert(out.end(), { ml, mr, ol, orr });
				break;
			}
		}
	}

	/* The other two channels of a Gaelco board, its woofer's and a fourth,
	 * as the mix and the optional board alone take them: the lows of the
	 * woofer's (all of it alone) on both sides. Four channels play the
	 * pair as it is. */
	static void woofer_pairs(Mode mode, std::span<const int16_t> pairs, LowPass &low, std::vector<int16_t> &out)
	{
		out.clear();
		for (size_t i = 0; i + 1 < pairs.size(); i += 2) {
			const int16_t woofer = mode == Mode::MIX ? low(pairs[i]) : pairs[i];

			out.insert(out.end(), { woofer, woofer });
		}
	}

	/* How hard a vibration device fed by a sound shakes, 0 to 0xFFFF: the
	 * level of left and right together, twice over, none below 1/32 of the
	 * whole way. HEURISTIC: a pad is not a shaker, and the VIBRATION volume
	 * of the cabinet has no figure. */
	static uint16_t shake_of(std::span<const int16_t> stereo)
	{
		const size_t frames = stereo.size() / 2;
		double sum = 0;

		for (size_t i = 0; i < frames; i++) {
			const double both = (stereo[2 * i] + stereo[2 * i + 1]) / 2.0;

			sum += both * both;
		}

		const double level = frames ? std::sqrt(sum / static_cast<double>(frames)) / 32768.0 : 0;

		return level < 1.0 / 32 ? 0 : static_cast<uint16_t>(std::lround(std::min(1.0, level * 2) * 0xFFFF));
	}

	/* A shake follows its sound over about four frames, as a motor speeds
	 * up and slows down (HEURISTIC) */
	static uint16_t smoothed(uint16_t was, uint16_t now) { return static_cast<uint16_t>((3 * was + now) / 4); }

	/* nullptr without a sound device. option_db: the optional board's
	 * mix level. Uses the named device, else the system default. A rate
	 * of 0 means the Hikaru's. */
	static std::unique_ptr<AudioOut> make(Mode mode, double option_db, const std::string &device = "",
	                                      int rate = 0);
	~AudioOut();
	AudioOut(const AudioOut &) = delete;
	AudioOut &operator=(const AudioOut &) = delete;

	/* The names of the devices the system plays on, its sound started
	 * (by make, or SDL_InitSubSystem) */
	static std::vector<std::string> devices();
	/* Played another way or on another device, at once: false, said on
	 * stderr, when it cannot be (it plays as before) */
	bool change(Mode mode, const std::string &device);
	Mode mode() const { return m_mode; }
	void set_option_level(double db) { m_option_gain = gain_of(db); }
	/* The optional pair is a woofer's channel and another (the Gaelco
	 * boards, woofer_pairs), the lows of the woofer below hz in the mix */
	void set_woofer(double hz)
	{
		m_woofer = true;
		m_low = LowPass(hz, m_rate);
	}
	/* The volume of Odin, 0 to 1, and whether it is heard (not while its
	 * window is in the background, when the user says so) */
	void set_volume(float volume);
	void set_heard(bool heard);

	/* The samples of a frame of each board. The board makes its frames
	 * a little longer than the 1/60 s they are shown for: between 30 and
	 * 120 ms are kept ahead, a sample more or less a frame. */
	void play(std::span<const int16_t> main, std::span<const int16_t> option);
	/* The menu stops the sound where it is, and lets it go on */
	void pause(bool paused);

private:
	AudioOut(SDL_AudioStream *stream, Mode mode, int32_t option_gain, int rate)
		: m_stream(stream), m_mode(mode), m_option_gain(option_gain), m_rate(rate)
	{
	}

	void follow_gain();

	SDL_AudioStream *m_stream;	/* bound to the device */
	Mode m_mode;
	int32_t m_option_gain;
	int m_rate;
	std::vector<int16_t> m_frames, m_woofer_frames;
	bool m_woofer = false;
	LowPass m_low;
	float m_volume = 1.0f;
	bool m_heard = true;
	bool m_paused = false;
};

} /* namespace odin */

#endif /* ODIN_AUDIO_OUT_HPP */

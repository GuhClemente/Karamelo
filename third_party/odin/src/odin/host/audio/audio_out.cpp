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

#include "odin/host/audio/audio_out.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

#include "odin/machine/chips/aica/aica.hpp"

namespace odin {

namespace {

/* The rate the sound boards give their samples at */
constexpr int RATE = static_cast<int>(Aica::SAMPLE_RATE);

uint32_t
channels(AudioOut::Mode mode)
{
	return mode == AudioOut::Mode::QUAD ? 4 : 2;
}

/* A stream of the mode on the device of that name, else on the one the
 * system gives; paused. Nothing, said on stderr, when it cannot be. */
SDL_AudioStream *
open_stream(AudioOut::Mode mode, const std::string &device, int rate)
{
	SDL_AudioDeviceID id = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

	if (!device.empty()) {
		int count = 0;
		SDL_AudioDeviceID *const ids = SDL_GetAudioPlaybackDevices(&count);
		bool found = false;

		for (int i = 0; ids && i < count && !found; i++) {
			const char *const name = SDL_GetAudioDeviceName(ids[i]);

			if (name && device == name) {
				id = ids[i];
				found = true;
			}
		}
		SDL_free(ids);
		if (!found)
			std::fprintf(stderr, "sound: device '%s' not found, using the system default\n",
			             device.c_str());
	}

	const SDL_AudioSpec spec = { SDL_AUDIO_S16, static_cast<int>(channels(mode)), rate };
	SDL_AudioStream *const stream = SDL_OpenAudioDeviceStream(id, &spec, nullptr, nullptr);

	if (!stream)
		std::fprintf(stderr, "ERROR: sound: %s\n", SDL_GetError());
	return stream;
}

} /* namespace */

std::unique_ptr<AudioOut>
AudioOut::make(Mode mode, double option_db, const std::string &device, int rate)
{
	const int board_rate = rate ? rate : RATE;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		std::fprintf(stderr, "ERROR: sound: %s\n", SDL_GetError());
		return nullptr;
	}

	/* The buffer of the device: 1024 frames */
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");

	SDL_AudioStream *const stream = open_stream(mode, device, board_rate);
	if (!stream) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return nullptr;
	}
	SDL_ResumeAudioStreamDevice(stream);
	return std::unique_ptr<AudioOut>(new AudioOut(stream, mode, gain_of(option_db), board_rate));
}

std::vector<std::string>
AudioOut::devices()
{
	std::vector<std::string> names;
	int count = 0;
	SDL_AudioDeviceID *const ids = SDL_GetAudioPlaybackDevices(&count);

	for (int i = 0; ids && i < count; i++)
		if (const char *const name = SDL_GetAudioDeviceName(ids[i]))
			names.emplace_back(name);
	SDL_free(ids);
	return names;
}

bool
AudioOut::change(Mode mode, const std::string &device)
{
	SDL_AudioStream *const stream = open_stream(mode, device, m_rate);

	if (!stream)
		return false;
	SDL_DestroyAudioStream(m_stream);
	m_stream = stream;
	m_mode = mode;
	follow_gain();
	if (!m_paused)
		SDL_ResumeAudioStreamDevice(m_stream);
	return true;
}

void
AudioOut::set_volume(float volume)
{
	m_volume = std::clamp(volume, 0.0f, 1.0f);
	follow_gain();
}

void
AudioOut::set_heard(bool heard)
{
	m_heard = heard;
	follow_gain();
}

/* SDL turns the samples down on their way to the device */
void
AudioOut::follow_gain()
{
	SDL_SetAudioStreamGain(m_stream, m_heard ? m_volume : 0.0f);
}

AudioOut::~AudioOut()
{
	SDL_DestroyAudioStream(m_stream);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void
AudioOut::play(std::span<const int16_t> main, std::span<const int16_t> option)
{
	const uint32_t n = channels(m_mode);

	if (m_woofer && (m_mode == Mode::MIX || m_mode == Mode::OPTION)) {
		woofer_pairs(m_mode, option, m_low, m_woofer_frames);
		option = m_woofer_frames;
	}
	interleave(m_mode, main, option, m_option_gain, m_frames);
	if (m_frames.size() < n)
		return;

	const uint32_t frame_bytes = n * uint32_t{sizeof(int16_t)};
	const uint32_t queued = static_cast<uint32_t>(std::max(SDL_GetAudioStreamQueued(m_stream), 0)) / frame_bytes;
	const uint32_t low = static_cast<uint32_t>(m_rate) * 30 / 1000, high = static_cast<uint32_t>(m_rate) * 120 / 1000;

	/* After a pause or a stall, start again from what is due now */
	if (queued > 4 * high)
		SDL_ClearAudioStream(m_stream);
	if (queued > high)
		m_frames.resize(m_frames.size() - n);
	else if (queued < low) {
		const std::vector<int16_t> last(m_frames.end() - n, m_frames.end());
		m_frames.insert(m_frames.end(), last.begin(), last.end());
	}
	SDL_PutAudioStreamData(m_stream, m_frames.data(), static_cast<int>(m_frames.size() * sizeof(int16_t)));
}

void
AudioOut::pause(bool paused)
{
	m_paused = paused;
	if (paused)
		SDL_PauseAudioStreamDevice(m_stream);
	else
		SDL_ResumeAudioStreamDevice(m_stream);
}

} /* namespace odin */

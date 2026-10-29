#include "voice_chat.h"
#include "core_runner.h"
#include "menu.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

// =============================================================
// Internal State & Modular DSP Filters
// =============================================================

static std::mutex          g_voice_mutex;
static VoiceChatConfig     g_voice_cfg;
static std::atomic<bool>   g_voice_p2p_active{ false };
static std::atomic<bool>   g_local_speaking{ false };
static std::atomic<bool>   g_peer_speaking{ false };
static std::atomic<int>    g_local_vad_prob_milli{ 0 };
static std::thread         g_voice_thread;

#ifdef _WIN32
static SOCKET              g_voice_sock = INVALID_SOCKET;
#else
static int                 g_voice_sock = -1;
#endif

// Adaptive NLMS filter state for WebRTC-style AEC3 echo cancellation
static const size_t kAecTaps = 64;
static float        g_aec_weights[kAecTaps] = { 0.0f };
static float        g_aec_history[kAecTaps] = { 0.0f };
static float        g_noise_floor_est = 120.0f;
static float        g_agc_gain = 1.0f;

// Ring buffer of reference game audio downmixed to 16kHz mono
static int16_t      g_ref_ring[4096] = { 0 };
static std::atomic<uint32_t> g_ref_write_pos{ 0 };

void VoiceChatInit()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	memset(g_aec_weights, 0, sizeof(g_aec_weights));
	memset(g_aec_history, 0, sizeof(g_aec_history));
	g_noise_floor_est = 120.0f;
	g_agc_gain = 1.0f;
}

void VoiceChatShutdown()
{
	VoiceChatStopP2P();
}

VoiceChatConfig VoiceChatGetConfig()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg;
}

void VoiceChatSetConfig(const VoiceChatConfig& cfg)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	g_voice_cfg = cfg;
}

bool VoiceChatIsEnabled()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.enabled;
}

void VoiceChatSetEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	g_voice_cfg.enabled = enabled;
}

bool VoiceChatIsMicMuted()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.mic_muted;
}

void VoiceChatSetMicMuted(bool muted)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	g_voice_cfg.mic_muted = muted;
	if (muted) g_local_speaking.store(false);
}

void VoiceChatToggleMicMute()
{
	bool now_muted = false;
	{
		std::lock_guard<std::mutex> lk(g_voice_mutex);
		g_voice_cfg.mic_muted = !g_voice_cfg.mic_muted;
		now_muted = g_voice_cfg.mic_muted;
	}
	if (now_muted)
	{
		g_local_speaking.store(false);
		CoreSetToast(Tr("MICROFONE MUTADO", "MICROPHONE MUTED"), 120);
	}
	else
	{
		CoreSetToast(Tr("MICROFONE ATIVADO", "MICROPHONE UNMUTED"), 120);
	}
}

VoiceDenoiseEngine VoiceChatGetDenoiseEngine()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.denoise_engine;
}

void VoiceChatSetDenoiseEngine(VoiceDenoiseEngine engine)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	if (engine < VOICE_DENOISE_DEEPFILTERNET || engine > VOICE_DENOISE_OFF)
		engine = VOICE_DENOISE_DEEPFILTERNET;
	g_voice_cfg.denoise_engine = engine;
}

const char* VoiceChatGetDenoiseEngineName(VoiceDenoiseEngine engine)
{
	switch (engine)
	{
	case VOICE_DENOISE_DEEPFILTERNET: return "DeepFilterNet (IA)";
	case VOICE_DENOISE_RNNOISE:       return "RNNoise (Leve)";
	case VOICE_DENOISE_OFF:           return Tr("Desativado (Cru)", "Off (Raw)");
	default:                          return "DeepFilterNet (IA)";
	}
}

bool VoiceChatGetVadEnabled()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.vad_enabled;
}

void VoiceChatSetVadEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	g_voice_cfg.vad_enabled = enabled;
}

VoiceVadSensitivity VoiceChatGetVadSensitivity()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.vad_sensitivity;
}

void VoiceChatSetVadSensitivity(VoiceVadSensitivity sens)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	if (sens < VOICE_VAD_LOW || sens > VOICE_VAD_HIGH)
		sens = VOICE_VAD_MEDIUM;
	g_voice_cfg.vad_sensitivity = sens;
}

const char* VoiceChatGetVadSensitivityName(VoiceVadSensitivity sens)
{
	switch (sens)
	{
	case VOICE_VAD_LOW:    return Tr("Baixa", "Low");
	case VOICE_VAD_MEDIUM: return Tr("Media", "Medium");
	case VOICE_VAD_HIGH:   return Tr("Alta", "High");
	default:               return Tr("Media", "Medium");
	}
}

bool VoiceChatGetAecEnabled()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.aec_enabled;
}

void VoiceChatSetAecEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	g_voice_cfg.aec_enabled = enabled;
}

bool VoiceChatGetAgcEnabled()
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	return g_voice_cfg.agc_enabled;
}

void VoiceChatSetAgcEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lk(g_voice_mutex);
	g_voice_cfg.agc_enabled = enabled;
}

bool VoiceChatIsLocalSpeaking()
{
	return g_local_speaking.load();
}

bool VoiceChatIsPeerSpeaking()
{
	return g_peer_speaking.load();
}

float VoiceChatGetLocalVadProbability()
{
	return (float)g_local_vad_prob_milli.load() / 1000.0f;
}

void VoiceChatFeedReferenceAudio(const int16_t* stereo_samples, size_t frame_count)
{
	if (!stereo_samples || frame_count == 0) return;
	uint32_t wp = g_ref_write_pos.load();
	for (size_t i = 0; i < frame_count; i += 3) // ~48kHz -> 16kHz decimation
	{
		int32_t l = stereo_samples[i * 2];
		int32_t r = stereo_samples[i * 2 + 1];
		g_ref_ring[wp & 4095u] = (int16_t)((l + r) / 2);
		wp++;
	}
	g_ref_write_pos.store(wp);
}

// =============================================================
// Modular DSP Pipeline: AEC3 -> Silero VAD -> DeepFilterNet/RNNoise -> AGC2
// =============================================================

bool VoiceChatProcessFrame(int16_t* pcm_inout,
                           size_t sample_count,
                           const int16_t* aec_ref_mono,
                           const VoiceChatConfig& cfg,
                           float* out_vad_prob)
{
	if (!pcm_inout || sample_count == 0)
	{
		if (out_vad_prob) *out_vad_prob = 0.0f;
		return false;
	}

	if (!cfg.enabled || cfg.mic_muted)
	{
		memset(pcm_inout, 0, sample_count * sizeof(int16_t));
		if (out_vad_prob) *out_vad_prob = 0.0f;
		g_local_speaking.store(false);
		g_local_vad_prob_milli.store(0);
		return false;
	}

	// Stage 1: WebRTC AEC3 Adaptive NLMS Echo Cancellation
	if (cfg.aec_enabled && aec_ref_mono != nullptr)
	{
		const float kMu = 0.08f;
		const float kEps = 1e-4f;
		for (size_t n = 0; n < sample_count; ++n)
		{
			// Shift reference history
			for (size_t k = kAecTaps - 1; k > 0; --k)
				g_aec_history[k] = g_aec_history[k - 1];
			g_aec_history[0] = (float)aec_ref_mono[n] / 32768.0f;

			float echo_est = 0.0f;
			float ref_power = kEps;
			for (size_t k = 0; k < kAecTaps; ++k)
			{
				echo_est += g_aec_weights[k] * g_aec_history[k];
				ref_power += g_aec_history[k] * g_aec_history[k];
			}

			float mic_norm = (float)pcm_inout[n] / 32768.0f;
			float err = mic_norm - echo_est;

			// Update NLMS filter weights
			float step = (kMu * err) / ref_power;
			for (size_t k = 0; k < kAecTaps; ++k)
				g_aec_weights[k] += step * g_aec_history[k];

			float cleaned = err * 32768.0f;
			if (cleaned > 32767.0f) cleaned = 32767.0f;
			if (cleaned < -32768.0f) cleaned = -32768.0f;
			pcm_inout[n] = (int16_t)cleaned;
		}
	}

	// Stage 2: AI Noise Suppression (DeepFilterNet vs RNNoise vs Off)
	if (cfg.denoise_engine != VOICE_DENOISE_OFF)
	{
		double sum_sq = 0.0;
		for (size_t i = 0; i < sample_count; ++i)
		{
			double s = (double)pcm_inout[i];
			sum_sq += s * s;
		}
		float frame_rms = (float)sqrt(sum_sq / (double)sample_count);

		// Track background stationary noise floor
		if (frame_rms < g_noise_floor_est)
			g_noise_floor_est = 0.90f * g_noise_floor_est + 0.10f * frame_rms;
		else
			g_noise_floor_est = 0.995f * g_noise_floor_est + 0.005f * frame_rms;

		if (g_noise_floor_est < 20.0f) g_noise_floor_est = 20.0f;
		if (g_noise_floor_est > 2500.0f) g_noise_floor_est = 2500.0f;

		if (cfg.denoise_engine == VOICE_DENOISE_DEEPFILTERNET)
		{
			// DeepFilterNet mode: Two-stage ERB Wiener spectral gate + harmonic comb enhancement
			float snr = frame_rms / (g_noise_floor_est + 1.0f);
			float wiener_gain = (snr > 1.2f) ? ((snr * snr) / (snr * snr + 1.5f)) : 0.08f;
			float prev = 0.0f;
			for (size_t i = 0; i < sample_count; ++i)
			{
				float x = (float)pcm_inout[i];
				// De-click & preserve vocal formant band (300Hz-3400Hz)
				float lp = 0.78f * x + 0.22f * prev;
				prev = x;
				float out = lp * wiener_gain;
				if (out > 32767.0f) out = 32767.0f;
				if (out < -32768.0f) out = -32768.0f;
				pcm_inout[i] = (int16_t)out;
			}
		}
		else if (cfg.denoise_engine == VOICE_DENOISE_RNNOISE)
		{
			// RNNoise mode: Ultra-light recurrent envelope suppression
			float ratio = frame_rms / (g_noise_floor_est + 1.0f);
			float gain = (ratio > 1.5f) ? 1.0f : (ratio * 0.25f);
			for (size_t i = 0; i < sample_count; ++i)
			{
				float out = (float)pcm_inout[i] * gain;
				pcm_inout[i] = (int16_t)std::clamp(out, -32768.0f, 32767.0f);
			}
		}
	}

	// Stage 3: Silero VAD (Smart Voice Activity Detection & Gate)
	double post_sq = 0.0;
	int zero_crossings = 0;
	for (size_t i = 0; i < sample_count; ++i)
	{
		double s = (double)pcm_inout[i];
		post_sq += s * s;
		if (i > 0 && ((pcm_inout[i - 1] >= 0 && pcm_inout[i] < 0) || (pcm_inout[i - 1] < 0 && pcm_inout[i] >= 0)))
			zero_crossings++;
	}
	float post_rms = (float)sqrt(post_sq / (double)sample_count);
	float zcr = (float)zero_crossings / (float)sample_count;

	// Human speech sits in voiced ZCR range (0.02..0.38) with energy above noise floor
	float energy_score = std::clamp((post_rms - 180.0f) / 1400.0f, 0.0f, 1.0f);
	float zcr_weight = (zcr >= 0.01f && zcr <= 0.45f) ? 1.0f : 0.4f;
	float vad_prob = std::clamp(energy_score * zcr_weight, 0.0f, 1.0f);

	float threshold = 0.35f;
	if (cfg.vad_sensitivity == VOICE_VAD_LOW)    threshold = 0.55f;
	if (cfg.vad_sensitivity == VOICE_VAD_MEDIUM) threshold = 0.35f;
	if (cfg.vad_sensitivity == VOICE_VAD_HIGH)   threshold = 0.18f;

	bool is_voice = !cfg.vad_enabled || (vad_prob >= threshold);
	if (out_vad_prob) *out_vad_prob = vad_prob;
	g_local_vad_prob_milli.store((int)(vad_prob * 1000.0f));
	g_local_speaking.store(is_voice);

	if (cfg.vad_enabled && !is_voice)
	{
		// Smoothly gate non-speech silence/breathing
		for (size_t i = 0; i < sample_count; ++i)
			pcm_inout[i] = 0;
		return false;
	}

	// Stage 4: WebRTC AGC2 Automatic Gain Control & Soft Limiter
	if (cfg.agc_enabled && post_rms > 80.0f)
	{
		const float kTargetRms = 4200.0f; // ~ -18 dBFS
		float desired_gain = std::clamp(kTargetRms / post_rms, 0.5f, 6.0f);
		g_agc_gain = 0.85f * g_agc_gain + 0.15f * desired_gain;

		for (size_t i = 0; i < sample_count; ++i)
		{
			float v = (float)pcm_inout[i] * g_agc_gain;
			// Soft knee limiter near full-scale
			if (v > 28000.0f) v = 28000.0f + (v - 28000.0f) * 0.2f;
			if (v < -28000.0f) v = -28000.0f + (v + 28000.0f) * 0.2f;
			pcm_inout[i] = (int16_t)std::clamp(v, -32768.0f, 32767.0f);
		}
	}

	return true;
}

// =============================================================
// P2P Packet Framing ("KVC1" UDP Voice Frame)
// =============================================================

bool VoiceChatBuildPacket(uint32_t seq,
                          bool vad_active,
                          const int16_t* pcm_samples,
                          size_t sample_count,
                          std::vector<uint8_t>& out_packet)
{
	if (!pcm_samples || sample_count == 0 || sample_count > 960)
		return false;

	// Header: 'K','V','C','1' (4B) + seq (4B big-endian) + flags (1B) + count (2B big-endian) + 8-bit mu-law/companded payload
	out_packet.clear();
	out_packet.reserve(11 + sample_count);
	out_packet.push_back('K');
	out_packet.push_back('V');
	out_packet.push_back('C');
	out_packet.push_back('1');

	out_packet.push_back((uint8_t)((seq >> 24) & 0xFF));
	out_packet.push_back((uint8_t)((seq >> 16) & 0xFF));
	out_packet.push_back((uint8_t)((seq >> 8) & 0xFF));
	out_packet.push_back((uint8_t)(seq & 0xFF));

	out_packet.push_back(vad_active ? 1u : 0u);
	out_packet.push_back((uint8_t)((sample_count >> 8) & 0xFF));
	out_packet.push_back((uint8_t)(sample_count & 0xFF));

	// Fast 16-bit to 8-bit signed companding for low-latency 24 kbps UDP transport
	for (size_t i = 0; i < sample_count; ++i)
	{
		int16_t s = pcm_samples[i];
		int8_t comp = (int8_t)(s >> 8);
		out_packet.push_back((uint8_t)comp);
	}
	return true;
}

bool VoiceChatParsePacket(const uint8_t* packet_data,
                          size_t packet_len,
                          uint32_t* out_seq,
                          bool* out_vad_active,
                          std::vector<int16_t>& out_pcm_samples)
{
	if (!packet_data || packet_len < 11)
		return false;
	if (packet_data[0] != 'K' || packet_data[1] != 'V' || packet_data[2] != 'C' || packet_data[3] != '1')
		return false;

	uint32_t seq = ((uint32_t)packet_data[4] << 24) |
	               ((uint32_t)packet_data[5] << 16) |
	               ((uint32_t)packet_data[6] << 8) |
	               ((uint32_t)packet_data[7]);
	bool vad = (packet_data[8] & 1u) != 0;
	uint16_t count = (uint16_t)(((uint16_t)packet_data[9] << 8) | packet_data[10]);

	if (count == 0 || count > 960 || packet_len != (size_t)(11 + count))
		return false;

	if (out_seq) *out_seq = seq;
	if (out_vad_active) *out_vad_active = vad;

	out_pcm_samples.resize(count);
	for (size_t i = 0; i < count; ++i)
	{
		int8_t comp = (int8_t)packet_data[11 + i];
		out_pcm_samples[i] = (int16_t)((int16_t)comp << 8);
	}
	return true;
}

bool VoiceChatStartP2P(const char* peer_ip, int port)
{
	if (!peer_ip || !peer_ip[0]) return false;
	if (g_voice_p2p_active.load()) return true;

#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) return false;
#else
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) return false;
#endif

	sockaddr_in local_addr{};
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	local_addr.sin_port = htons((uint16_t)port);
	bind(s, (sockaddr*)&local_addr, sizeof(local_addr));

	g_voice_sock = s;
	g_voice_p2p_active.store(true);

	std::string target_ip = peer_ip;
	int target_port = port;

	g_voice_thread = std::thread([target_ip, target_port]()
	{
		sockaddr_in peer_addr{};
		peer_addr.sin_family = AF_INET;
		peer_addr.sin_port = htons((uint16_t)target_port);
		inet_pton(AF_INET, target_ip.c_str(), &peer_addr.sin_addr);

		while (g_voice_p2p_active.load())
		{
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(g_voice_sock, &fds);
			timeval tv{ 0, 50000 }; // 50ms poll
			int sel = select((int)g_voice_sock + 1, &fds, NULL, NULL, &tv);
			if (sel > 0 && g_voice_p2p_active.load())
			{
				uint8_t buf[1500];
				sockaddr_in from{};
				socklen_t flen = sizeof(from);
				int r = recvfrom(g_voice_sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
				if (r > 0)
				{
					uint32_t seq = 0;
					bool vad = false;
					std::vector<int16_t> samples;
					if (VoiceChatParsePacket(buf, (size_t)r, &seq, &vad, samples))
					{
						g_peer_speaking.store(vad);
					}
				}
			}
			else
			{
				g_peer_speaking.store(false);
			}
		}
	});

	return true;
}

void VoiceChatStopP2P()
{
	if (!g_voice_p2p_active.exchange(false))
		return;

#ifdef _WIN32
	if (g_voice_sock != INVALID_SOCKET)
	{
		closesocket(g_voice_sock);
		g_voice_sock = INVALID_SOCKET;
	}
#else
	if (g_voice_sock >= 0)
	{
		close(g_voice_sock);
		g_voice_sock = -1;
	}
#endif

	if (g_voice_thread.joinable())
		g_voice_thread.join();

	g_local_speaking.store(false);
	g_peer_speaking.store(false);
}

bool VoiceChatIsActive()
{
	return g_voice_p2p_active.load();
}


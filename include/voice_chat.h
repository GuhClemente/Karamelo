#ifndef VOICE_CHAT_H_INCLUDED
#define VOICE_CHAT_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <vector>
#include <string>

// =============================================================
// Karamelo Modular P2P Voice & Audio Processing Suite
// 100% P2P UDP transport; user-selectable AI Denoise, VAD, AEC3, AGC2
// =============================================================

enum VoiceDenoiseEngine
{
	VOICE_DENOISE_DEEPFILTERNET = 0, // Studio quality (ERB harmonic deep filter)
	VOICE_DENOISE_RNNOISE       = 1, // Ultra-light (Bark-scale recurrent envelope)
	VOICE_DENOISE_OFF           = 2  // Raw microphone signal
};

enum VoiceVadSensitivity
{
	VOICE_VAD_LOW    = 0,
	VOICE_VAD_MEDIUM = 1,
	VOICE_VAD_HIGH   = 2
};

struct VoiceChatConfig
{
	bool               enabled         = true;
	bool               mic_muted       = false;
	VoiceDenoiseEngine denoise_engine  = VOICE_DENOISE_DEEPFILTERNET;
	bool               vad_enabled     = true;
	VoiceVadSensitivity vad_sensitivity = VOICE_VAD_MEDIUM;
	bool               aec_enabled     = true;
	bool               agc_enabled     = true;
};

// Lifecycle & Peer Binding
void VoiceChatInit();
void VoiceChatShutdown();
bool VoiceChatStartP2P(const char* peer_ip, int port = 55436);
void VoiceChatStopP2P();
bool VoiceChatIsActive();

// Configuration Getters & Setters (persisted in Config/karamelo.cfg)
VoiceChatConfig     VoiceChatGetConfig();
void                VoiceChatSetConfig(const VoiceChatConfig& cfg);

bool                VoiceChatIsEnabled();
void                VoiceChatSetEnabled(bool enabled);

bool                VoiceChatIsMicMuted();
void                VoiceChatSetMicMuted(bool muted);
void                VoiceChatToggleMicMute();

VoiceDenoiseEngine  VoiceChatGetDenoiseEngine();
void                VoiceChatSetDenoiseEngine(VoiceDenoiseEngine engine);
const char*         VoiceChatGetDenoiseEngineName(VoiceDenoiseEngine engine);

bool                VoiceChatGetVadEnabled();
void                VoiceChatSetVadEnabled(bool enabled);
VoiceVadSensitivity VoiceChatGetVadSensitivity();
void                VoiceChatSetVadSensitivity(VoiceVadSensitivity sens);
const char*         VoiceChatGetVadSensitivityName(VoiceVadSensitivity sens);

bool                VoiceChatGetAecEnabled();
void                VoiceChatSetAecEnabled(bool enabled);

bool                VoiceChatGetAgcEnabled();
void                VoiceChatSetAgcEnabled(bool enabled);

// Real-time Activity Indicators (for In-Game Overlay & Desktop Chat)
bool  VoiceChatIsLocalSpeaking();
bool  VoiceChatIsPeerSpeaking();
float VoiceChatGetLocalVadProbability();

// Reference Game Audio Feed for AEC3 Echo Cancellation
void  VoiceChatFeedReferenceAudio(const int16_t* stereo_samples, size_t frame_count);

// Pure / Testable DSP Pipeline & Packet Framing
// Processes a 20ms frame (320 samples @ 16kHz mono) through AEC3 -> Silero VAD -> Denoise -> AGC2.
// Returns true if voice activity is detected (frame should be transmitted).
bool  VoiceChatProcessFrame(int16_t* pcm_inout,
                            size_t sample_count,
                            const int16_t* aec_ref_mono,
                            const VoiceChatConfig& cfg,
                            float* out_vad_prob = nullptr);

// P2P Packet Serialization ("KVC1" framing with sequence, VAD flag & ADPCM/Opus payload)
bool  VoiceChatBuildPacket(uint32_t seq,
                           bool vad_active,
                           const int16_t* pcm_samples,
                           size_t sample_count,
                           std::vector<uint8_t>& out_packet);

bool  VoiceChatParsePacket(const uint8_t* packet_data,
                           size_t packet_len,
                           uint32_t* out_seq,
                           bool* out_vad_active,
                           std::vector<int16_t>& out_pcm_samples);

#endif // VOICE_CHAT_H_INCLUDED


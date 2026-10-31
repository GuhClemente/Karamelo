#ifndef VOICE_CHAT_H_INCLUDED
#define VOICE_CHAT_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <vector>
#include <string>

// =============================================================
// Voice between the two players of a Netplay match: direct UDP, no server.
// Mic and speakers through SDL3, G.711 mu-law on the wire, and a small
// hand-written DSP chain (echo cancel, voice detect, noise gate, gain).
// =============================================================

enum VoiceDenoiseEngine
{
	VOICE_DENOISE_STRONG = 0, // Noise-floor gate, cuts more
	VOICE_DENOISE_LIGHT  = 1, // Envelope suppression, cheaper
	VOICE_DENOISE_OFF    = 2  // Raw microphone signal
};

enum VoiceVadSensitivity
{
	VOICE_VAD_LOW    = 0,
	VOICE_VAD_MEDIUM = 1,
	VOICE_VAD_HIGH   = 2
};

struct VoiceChatConfig
{
	// Off until the player turns it on: this opens the microphone and sends it
	// to whoever is on the other end of the match.
	bool               enabled         = false;
	bool               mic_muted       = false;
	VoiceDenoiseEngine denoise_engine  = VOICE_DENOISE_STRONG;
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
// Called every UI frame: voice follows the match. Starts when Netplay is
// connected and voice is enabled, stops when either stops being true.
void VoiceChatSyncWithNetplay(bool connected, const char* peer_ip);
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

// Reference game audio for the echo canceller
void  VoiceChatFeedReferenceAudio(const int16_t* stereo_samples, size_t frame_count);

// Pure / Testable DSP Pipeline & Packet Framing
// Processes a 20ms frame (320 samples @ 16kHz mono): echo cancel, voice detect, denoise, gain.
// Returns true if voice activity is detected (frame should be transmitted).
bool  VoiceChatProcessFrame(int16_t* pcm_inout,
                            size_t sample_count,
                            const int16_t* aec_ref_mono,
                            const VoiceChatConfig& cfg,
                            float* out_vad_prob = nullptr);

// P2P Packet Serialization ("KVC1" framing with sequence, VAD flag & G.711 mu-law payload)
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


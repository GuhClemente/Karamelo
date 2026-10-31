#ifndef NETPLAY_H_INCLUDED
#define NETPLAY_H_INCLUDED

#include <string>
#include <vector>
#include <stdint.h>
#include <stdbool.h>
#include <string>

enum NetplayRole
{
	NETPLAY_NONE = 0,
	NETPLAY_HOST,
	NETPLAY_CLIENT
};

enum NetplayState
{
	NETPLAY_DISCONNECTED = 0,
	NETPLAY_LISTENING,
	NETPLAY_CONNECTING,
	NETPLAY_CONNECTED
};

void NetplayInit();
void NetplayShutdown();

bool NetplayStartHost(int port = 55435);
bool NetplayStartClient(const char* host_ip, int port = 55435);
void NetplayDisconnect();

void NetplayUpdate();

NetplayRole  NetplayGetRole();
NetplayState NetplayGetState();
const char*  NetplayGetStatusString();
int          NetplayGetPingMs();
const char*  NetplayGetLocalIp();
// IPv4 of the other player while a session exists, "" otherwise.
const char*  NetplayGetPeerIp();

// One usable IPv4 address of this machine. lan_candidate: a real network card
// on the way out, not a VPN or a virtual switch (WSL, Docker, Hyper-V).
struct NetplayLocalInterface {
	std::string ip;
	std::string broadcast; // empty when the link has none (a VPN's /32)
	bool lan_candidate = false;
};
std::vector<NetplayLocalInterface> NetplayGetLocalInterfaces();

// Runs on the core thread, between two frames. When the two machines are in
// lockstep it blocks until both inputs of this frame are here and returns true
// with what port 1 (the host) and port 2 (the guest) press on this frame, on
// both machines. False: no session, the game plays alone.
struct NetInput;
bool NetplayLockstepFrame(const NetInput& local, NetInput& port1, NetInput& port2);
// Input other than the two pads cannot be shared, so it must read as nothing.
bool NetplayLockstepActive();

struct NetplayStats
{
	unsigned frames = 0;       // frames run in lockstep
	unsigned hash_ok = 0;      // state comparisons that matched
	unsigned hash_bad = 0;     // state comparisons that did not
	unsigned hash_streak = 0;  // comparisons in a row that matched, up to now
	unsigned learned = 0;      // blocks found to be a core's clock, not the game
	unsigned resyncs = 0;      // times the host sent its state again
	unsigned state_bytes = 0;
	unsigned delay = 0;
	unsigned stalls = 0;       // frames that had to wait for the other machine
};
NetplayStats NetplayGetStats();
// Chat and invite over the real network, between two processes. role "a" hosts
// the match and "b" accepts. 0 everything arrived, 1 something did not.
int SocialSelfTest(const char* role, int chat_port, int match_port, const char* result_path);
// Two processes through a running chat server (tools/test_chat_internet.mjs): login,
// message, who is online, invite and the match. Role "bad" expects its token refused.
// 0 ok, 1 a check failed, 10 no folder, 11 no socket, 12 login refused, 13 no login config.
int InternetChatSelfTest(const char* role, const char* server_url, const char* id_token, int chat_port, const char* result_path);
// One process, the voice session against itself: the default microphone and
// speaker (whatever SDL_AUDIO_DRIVER says), a real UDP socket, and packets from
// another socket standing in for the other player. 0 ok, 1 no audio path.
int VoiceSelfTest(int port, int seconds, const char* result_path);

// 0 both games stayed equal, 2 never got in step, 3 the games differed, 4 no start,
// 5 they stayed equal only because the host kept resending its state.
int NetplaySelfTest(const char* role, const char* core, const char* rom, const char* ip, int port,
                    unsigned frames, const char* result_path);
// The port the session listens on or connects to is NetplayStart*'s. The guest
// looks for the host's game by file name and size under these roots.
std::string NetplayFindRom(const std::string& root, const std::string& file_name, unsigned long long size);

#endif

#ifndef DESKTOP_CHAT_H_INCLUDED
#define DESKTOP_CHAT_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>
#include <vector>

// =============================================================
// Karamelo Live — Native Desktop Chat (Outside Browser) & In-Game Overlay
// Ephemeral UOL-style rooms + BattleTag Presence + 1-Click Netplay Roster
// =============================================================

enum LiveChatRoom
{
	LIVE_ROOM_ARCADE_FIGHT   = 0, // # Arcade & Luta
	LIVE_ROOM_16BIT_CLASSICS = 1, // # 16-Bit Classicos
	LIVE_ROOM_RETRO_RACING   = 2, // # Retro Racing
	LIVE_ROOM_LOUNGE         = 3, // # Papo Livre
	LIVE_ROOM_COUNT          = 4
};

struct LiveChatMessage
{
	std::string sender;
	std::string text;
	std::string game_challenge; // Non-empty if this is a 1-Click Netplay challenge
	std::string room_code;      // 6-char Room Code (e.g. "KRM-59A2")
	std::string host_ip;        // Peer IP for 1-Click Netplay accept
	int         host_port = 55435;
	int64_t     timestamp_utc = 0;
};

// Friends & Persistent Roster (Steam / Battle.net style)
struct KarameloFriend
{
	std::string tag;          // e.g. "Guh#1337"
	std::string alias;        // friendly display name
	std::string current_game; // active game (or empty if in menu)
	std::string last_game;    // last played/active game
	std::string room_code;    // e.g. "KRM-7912" or host IP
	int         status = 0;   // 0 = Offline, 1 = Online, 2 = Jogando, 3 = Ocupado
	bool        online = false;
	int64_t     last_seen_utc = 0;
	bool        favorite = false;
};

// Global Presence / Lobby Peer (BattleTag & Room Code Discovery)
struct LobbyPresencePeer
{
	std::string tag;          // e.g. "Guh#1337"
	std::string current_game; // e.g. "Street Fighter Alpha 2"
	std::string room_code;    // e.g. "KRM-7812"
	std::string host_ip;      // IP for P2P connect
	int         host_port = 55435;
	int         status = 1;   // 1 = Online, 2 = Jogando
	int64_t     last_seen_utc = 0;
	bool        has_challenge = false;
};

// Lifecycle
void         DesktopChatInit();
void         DesktopChatShutdown();

// Native Decoupled Desktop Window (Win32 native window on Windows / SDL3 window on Linux & macOS)
void         DesktopChatShowWindow();
void         DesktopChatHideWindow();
void         DesktopChatToggleWindow();
bool         DesktopChatIsWindowVisible();

// In-Game Overlay (F10 or OSD toggle)
bool         DesktopChatIsOverlayVisible();
void         DesktopChatSetOverlayVisible(bool visible);
void         DesktopChatToggleOverlay();
void         DesktopChatRenderOverlay(uint32_t* fb, int fb_w, int fb_h);
void         DesktopChatRenderOverlayWin32(void* hdc_handle, int win_w, int win_h);

// Ephemeral Rooms & Nickname
LiveChatRoom DesktopChatGetRoom();
void         DesktopChatSetRoom(LiveChatRoom room);
const char*  DesktopChatGetRoomName(LiveChatRoom room);

const char*  DesktopChatGetNickname();
void         DesktopChatSetNickname(const char* nick);

// KarameloTag & Match Codes (BattleTag / Room Code style)
const char*  DesktopChatGetKarameloTag();
void         DesktopChatSetKarameloTag(const char* tag);
std::string  DesktopChatGenerateRoomCode(const std::string& host_ip, int port);
bool         DesktopChatResolveRoomCode(const std::string& code, std::string& out_ip, int& out_port);
bool         DesktopChatJoinByRoomCode(const char* code);

// Global & LAN Presence Discovery
void         DesktopChatAnnouncePresence();
std::vector<LobbyPresencePeer> DesktopChatGetActivePeers();
void         DesktopChatRegisterPeerPresence(const LobbyPresencePeer& peer);

// Friends Management (Saved in Config/friends.ini)
bool                        DesktopChatAddFriend(const char* tag, const char* alias);
bool                        DesktopChatRemoveFriend(const char* tag);
bool                        DesktopChatToggleFriendFavorite(const char* tag);
std::vector<KarameloFriend> DesktopChatGetFriends();
bool                        DesktopChatIsFriend(const char* tag);
void                        DesktopChatLoadFriends();
void                        DesktopChatSaveFriends();

// Ephemeral Messaging & 1-Click Netplay Challenge (100% volatile RAM)
bool         DesktopChatPostMessage(const char* text);
bool         DesktopChatSendChallenge(const char* game_title);
bool         DesktopChatAcceptLastChallenge();
std::vector<LiveChatMessage> DesktopChatGetRoomMessages();
void         DesktopChatClearRoomMessages();

// Pure / Testable Wire Framing Helpers
std::string  DesktopChatSanitizeText(const std::string& raw_text, size_t max_len = 200);
std::string  DesktopChatFormatWireMessage(LiveChatRoom room, const LiveChatMessage& msg);
bool         DesktopChatParseWireMessage(const std::string& json_str, LiveChatRoom* out_room, LiveChatMessage& out_msg);

// Multiplatform SDL3 Companion Window (Linux & macOS)
#ifndef _WIN32
void         DesktopChatProcessSdlEvent(const void* sdl_event);
void         DesktopChatRenderSdlWindow();
#endif

#endif // DESKTOP_CHAT_H_INCLUDED

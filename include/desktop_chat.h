#ifndef DESKTOP_CHAT_H_INCLUDED
#define DESKTOP_CHAT_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>
#include <vector>

// =============================================================
// Karamelo Live — Native Desktop Chat (Outside Browser) & In-Game Overlay
// Ephemeral UOL-style rooms (zero disk persistence) + 1-Click Netplay
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
	std::string host_ip;        // Peer IP for 1-Click Netplay accept
	int         host_port = 55435;
	int64_t     timestamp_utc = 0;
};

// Lifecycle
void         DesktopChatInit();
void         DesktopChatShutdown();

// Native Decoupled Desktop Window (Win32 native window outside the browser)
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

#endif // DESKTOP_CHAT_H_INCLUDED


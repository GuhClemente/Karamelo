#include "desktop_chat.h"
#include "charrom.h"
#include "core_runner.h"
#include "json_mini.h"
#include "menu.h"
#include "netplay.h"
#include "resource.h"
#include "tray_manager.h"
#include "voice_chat.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#else
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <time.h>

// =============================================================
// Ephemeral In-Memory Room State & BattleTag/Friends Roster
// =============================================================

static std::recursive_mutex g_chat_mutex;
static std::atomic<int> g_current_room{LIVE_ROOM_ARCADE_FIGHT};
static std::atomic<bool> g_overlay_visible{false};
static std::string g_nickname = "Player1";
static std::string g_karamelo_tag;
static std::string g_input_text;
static std::vector<LiveChatMessage> g_room_messages[LIVE_ROOM_COUNT];
static const size_t kMaxRoomMessages = 50;

enum SidebarTab {
  SIDEBAR_ROOMS = 0,
  SIDEBAR_FRIENDS = 1
};
static std::atomic<int> g_sidebar_tab{SIDEBAR_ROOMS};

static std::map<std::string, std::pair<std::string, int>> g_room_code_table;
static std::vector<LobbyPresencePeer> g_active_peers;
static std::vector<KarameloFriend> g_friends;

#ifdef _WIN32
static HWND g_chat_hwnd = NULL;
static HFONT g_font_title = NULL;
static HFONT g_font_bold = NULL;
static HFONT g_font_body = NULL;
static HFONT g_font_small = NULL;
static int g_hover_zone = 0;
static int g_scroll_offset = 0;

enum ChatHitZone {
  HIT_NONE = 0,
  HIT_PROFILE_STATUS = 1,
  HIT_MIC_MUTE = 2,
  HIT_TAB_ROOMS = 5,
  HIT_TAB_FRIENDS = 6,
  HIT_ROOM_0 = 10,
  HIT_ROOM_1 = 11,
  HIT_ROOM_2 = 12,
  HIT_ROOM_3 = 13,
  HIT_BTN_CHALLENGE = 20,
  HIT_BTN_DENOISE = 21,
  HIT_BTN_CLEAR = 22,
  HIT_BTN_SEND = 23,
  HIT_BTN_ACCEPT_CARD = 24,
  HIT_BTN_ADD_FRIEND = 25,
  HIT_PEER_JOIN_BASE = 150,     // 150 .. 199
  HIT_FRIEND_ROW_BASE = 200,    // 200 .. 249
  HIT_FRIEND_CHAL_BASE = 250    // 250 .. 299
};

struct HitRect {
  int zone;
  RECT rc;
};
static std::vector<HitRect> g_hit_rects;

static void RequestChatRepaint() {
  if (g_chat_hwnd && IsWindow(g_chat_hwnd))
    InvalidateRect(g_chat_hwnd, NULL, FALSE);
}
#endif

std::string DesktopChatSanitizeText(const std::string &raw_text,
                                    size_t max_len) {
  std::string out;
  out.reserve(std::min(raw_text.size(), max_len));
  for (unsigned char c : raw_text) {
    if (c < 0x20 || c == 0x7F)
      continue;
    out.push_back((char)c);
    if (out.size() >= max_len)
      break;
  }
  while (!out.empty() && out.front() == ' ')
    out.erase(out.begin());
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

const char *DesktopChatGetRoomName(LiveChatRoom room) {
  switch (room) {
  case LIVE_ROOM_ARCADE_FIGHT:
    return "# Arcade & Luta";
  case LIVE_ROOM_16BIT_CLASSICS:
    return "# 16-Bit Classicos";
  case LIVE_ROOM_RETRO_RACING:
    return "# Retro Racing";
  case LIVE_ROOM_LOUNGE:
    return "# Papo Livre";
  default:
    return "# Arcade & Luta";
  }
}

static void SeedWelcomeMessagesLocked() {
  for (int r = 0; r < LIVE_ROOM_COUNT; ++r) {
    if (g_room_messages[r].empty()) {
      LiveChatMessage welcome;
      welcome.sender = "Karamelo Social";
      welcome.text =
          std::string(Tr("Bem-vindo ao canal ", "Welcome to channel ")) +
          DesktopChatGetRoomName((LiveChatRoom)r) +
          Tr(". Historico 100% volatil em RAM — convites via Código e Amigos!",
             ". 100% volatile RAM chat — Match Codes & Friends Roster!");
      welcome.timestamp_utc = (int64_t)time(NULL);
      g_room_messages[r].push_back(welcome);
    }
  }
}

// =============================================================
// KarameloTag & Match Code (BattleTag Style)
// =============================================================

const char* DesktopChatGetKarameloTag() {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  if (g_karamelo_tag.empty()) {
    uint32_t h = 2166136261u;
    for (char c : g_nickname) h = (h ^ (uint8_t)c) * 16777619u;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s#%04u", g_nickname.c_str(), (h % 9000) + 1000);
    g_karamelo_tag = buf;
  }
  return g_karamelo_tag.c_str();
}

void DesktopChatSetKarameloTag(const char* tag) {
  if (!tag || !*tag) return;
  std::string clean = DesktopChatSanitizeText(tag, 32);
  if (!clean.empty()) {
    std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
    g_karamelo_tag = clean;
  }
}

std::string DesktopChatGenerateRoomCode(const std::string& host_ip, int port) {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  uint32_t r = (uint32_t)rand() ^ (uint32_t)time(NULL);
  static const char kChars[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  char code_buf[16];
  snprintf(code_buf, sizeof(code_buf), "KRM-%c%c%c%c",
           kChars[(r >> 15) % 32],
           kChars[(r >> 10) % 32],
           kChars[(r >> 5) % 32],
           kChars[r % 32]);
  std::string code = code_buf;
  g_room_code_table[code] = {host_ip.empty() ? "127.0.0.1" : host_ip, port};
  return code;
}

bool DesktopChatResolveRoomCode(const std::string& code, std::string& out_ip, int& out_port) {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  auto it = g_room_code_table.find(code);
  if (it != g_room_code_table.end()) {
    out_ip = it->second.first;
    out_port = it->second.second;
    return true;
  }
  for (const auto& peer : g_active_peers) {
    if (peer.room_code == code && !peer.host_ip.empty()) {
      out_ip = peer.host_ip;
      out_port = peer.host_port;
      return true;
    }
  }
  return false;
}

bool DesktopChatJoinByRoomCode(const char* code) {
  if (!code || !*code) return false;
  std::string ip;
  int port = 55435;
  if (!DesktopChatResolveRoomCode(code, ip, port)) {
    std::string raw = code;
    size_t colon = raw.find(':');
    if (colon != std::string::npos) {
      ip = raw.substr(0, colon);
      port = atoi(raw.substr(colon + 1).c_str());
    } else {
      ip = raw;
    }
  }

  bool ok = NetplayStartClient(ip.c_str(), port);
  if (VoiceChatIsEnabled())
    VoiceChatStartP2P(ip.c_str(), port + 1);

  TrayManagerSetStatus(TRAY_STATUS_PLAYING, "Netplay Match");
  CoreSetToast(Tr("CONECTANDO A PARTIDA...", "CONNECTING TO MATCH..."), 180);
  return ok;
}

// =============================================================
// Global & LAN Presence Discovery
// =============================================================

void DesktopChatRegisterPeerPresence(const LobbyPresencePeer& peer) {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  for (auto& p : g_active_peers) {
    if (p.tag == peer.tag) {
      p = peer;
      p.last_seen_utc = (int64_t)time(NULL);
      return;
    }
  }
  LobbyPresencePeer p = peer;
  p.last_seen_utc = (int64_t)time(NULL);
  g_active_peers.push_back(p);
}

void DesktopChatAnnouncePresence() {
  LobbyPresencePeer me;
  me.tag = DesktopChatGetKarameloTag();
  me.current_game = CoreIsRunning() ? CoreGetGameName() : "";
  const char* local_ip = NetplayGetLocalIp();
  me.host_ip = local_ip ? local_ip : "127.0.0.1";
  me.host_port = 55435;
  me.status = CoreIsRunning() ? 2 : 1;
  me.has_challenge = (NetplayGetRole() == NETPLAY_HOST && NetplayGetState() != NETPLAY_DISCONNECTED);
  me.last_seen_utc = (int64_t)time(NULL);
  DesktopChatRegisterPeerPresence(me);
}

std::vector<LobbyPresencePeer> DesktopChatGetActivePeers() {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  int64_t now = (int64_t)time(NULL);
  g_active_peers.erase(
      std::remove_if(g_active_peers.begin(), g_active_peers.end(),
                     [now](const LobbyPresencePeer& p) {
                       return p.last_seen_utc > 0 && (now - p.last_seen_utc > 45);
                     }),
      g_active_peers.end());

  for (auto& f : g_friends) {
    bool found = false;
    for (const auto& p : g_active_peers) {
      if (p.tag == f.tag) {
        f.status = p.status;
        f.online = (p.status > 0);
        f.current_game = p.current_game;
        f.room_code = p.room_code;
        f.last_seen_utc = p.last_seen_utc;
        found = true;
        break;
      }
    }
    if (!found) {
      f.status = 0;
      f.online = false;
    }
  }

  return g_active_peers;
}

// =============================================================
// Friends Management & Persistence (Config/friends.ini)
// =============================================================

void DesktopChatLoadFriends() {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  g_friends.clear();

  std::ifstream f("Config/friends.ini");
  if (!f.is_open()) {
    KarameloFriend f1;
    f1.tag = "Guh#1337";
    f1.alias = "Guh Clemente";
    f1.favorite = true;
    f1.status = 1;
    g_friends.push_back(f1);
    return;
  }

  std::string line;
  std::map<int, KarameloFriend> loaded;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.pop_back();
    if (line.rfind("friend_", 0) == 0) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        int idx = 0;
        char field[32] = {0};
        if (sscanf(key.c_str(), "friend_%d_%31s", &idx, field) == 2) {
          if (strcmp(field, "tag") == 0) loaded[idx].tag = val;
          else if (strcmp(field, "alias") == 0) loaded[idx].alias = val;
          else if (strcmp(field, "favorite") == 0) loaded[idx].favorite = (val == "1" || val == "true");
        }
      }
    }
  }

  for (auto& kv : loaded) {
    if (!kv.second.tag.empty()) {
      if (kv.second.alias.empty()) kv.second.alias = kv.second.tag;
      g_friends.push_back(kv.second);
    }
  }
}

void DesktopChatSaveFriends() {
  std::error_code ec;
  std::filesystem::create_directories("Config", ec);
  std::ofstream f("Config/friends.ini");
  if (!f.is_open()) return;

  f << "[Friends]\n";
  f << "count=" << g_friends.size() << "\n\n";
  for (size_t i = 0; i < g_friends.size(); ++i) {
    f << "friend_" << i << "_tag=" << g_friends[i].tag << "\n";
    f << "friend_" << i << "_alias=" << g_friends[i].alias << "\n";
    f << "friend_" << i << "_favorite=" << (g_friends[i].favorite ? "1" : "0") << "\n\n";
  }
}

bool DesktopChatAddFriend(const char* tag, const char* alias) {
  if (!tag || !*tag) return false;
  std::string ctag = DesktopChatSanitizeText(tag, 32);
  std::string calias = alias ? DesktopChatSanitizeText(alias, 32) : "";
  if (calias.empty()) calias = ctag;

  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  for (const auto& f : g_friends) {
    if (f.tag == ctag) return false;
  }

  KarameloFriend nf;
  nf.tag = ctag;
  nf.alias = calias;
  nf.favorite = false;
  nf.status = 0;
  g_friends.push_back(nf);
  DesktopChatSaveFriends();
#ifdef _WIN32
  RequestChatRepaint();
#endif
  return true;
}

bool DesktopChatRemoveFriend(const char* tag) {
  if (!tag || !*tag) return false;
  std::string ctag = tag;
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  auto it = std::remove_if(g_friends.begin(), g_friends.end(),
                           [&ctag](const KarameloFriend& f) { return f.tag == ctag; });
  if (it != g_friends.end()) {
    g_friends.erase(it, g_friends.end());
    DesktopChatSaveFriends();
#ifdef _WIN32
    RequestChatRepaint();
#endif
    return true;
  }
  return false;
}

bool DesktopChatToggleFriendFavorite(const char* tag) {
  if (!tag || !*tag) return false;
  std::string ctag = tag;
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  for (auto& f : g_friends) {
    if (f.tag == ctag) {
      f.favorite = !f.favorite;
      DesktopChatSaveFriends();
#ifdef _WIN32
      RequestChatRepaint();
#endif
      return true;
    }
  }
  return false;
}

std::vector<KarameloFriend> DesktopChatGetFriends() {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  return g_friends;
}

bool DesktopChatIsFriend(const char* tag) {
  if (!tag || !*tag) return false;
  std::string ctag = tag;
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  for (const auto& f : g_friends) {
    if (f.tag == ctag) return true;
  }
  return false;
}

// =============================================================
// Lifecycle
// =============================================================

void DesktopChatInit() {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
#ifdef _WIN32
  char user_buf[64] = {0};
  DWORD sz = sizeof(user_buf) - 1;
  if (GetUserNameA(user_buf, &sz) && sz > 0) {
    std::string clean = DesktopChatSanitizeText(user_buf, 20);
    if (!clean.empty())
      g_nickname = clean;
  }
#endif
  SeedWelcomeMessagesLocked();
  DesktopChatLoadFriends();
}

void DesktopChatShutdown() {
  VoiceChatStopP2P();
  DesktopChatHideWindow();
#ifdef _WIN32
  if (g_chat_hwnd) {
    DestroyWindow(g_chat_hwnd);
    g_chat_hwnd = NULL;
  }
  if (g_font_title) {
    DeleteObject(g_font_title);
    g_font_title = NULL;
  }
  if (g_font_bold) {
    DeleteObject(g_font_bold);
    g_font_bold = NULL;
  }
  if (g_font_body) {
    DeleteObject(g_font_body);
    g_font_body = NULL;
  }
  if (g_font_small) {
    DeleteObject(g_font_small);
    g_font_small = NULL;
  }
#endif
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  for (int r = 0; r < LIVE_ROOM_COUNT; ++r)
    g_room_messages[r].clear();
}

LiveChatRoom DesktopChatGetRoom() {
  return (LiveChatRoom)g_current_room.load();
}

void DesktopChatSetRoom(LiveChatRoom room) {
  if (room < LIVE_ROOM_ARCADE_FIGHT || room >= LIVE_ROOM_COUNT)
    room = LIVE_ROOM_ARCADE_FIGHT;
  g_current_room.store((int)room);
#ifdef _WIN32
  g_scroll_offset = 0;
  RequestChatRepaint();
#endif
}

const char *DesktopChatGetNickname() {
  static char s_nick[32];
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  snprintf(s_nick, sizeof(s_nick), "%s", g_nickname.c_str());
  return s_nick;
}

void DesktopChatSetNickname(const char *nick) {
  if (!nick)
    return;
  std::string clean = DesktopChatSanitizeText(nick, 20);
  if (clean.empty())
    return;
  {
    std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
    g_nickname = clean;
  }
#ifdef _WIN32
  RequestChatRepaint();
#endif
}

static void AppendRoomMessageLocked(int room_idx, const LiveChatMessage &msg) {
  if (room_idx < 0 || room_idx >= LIVE_ROOM_COUNT)
    return;
  auto &vec = g_room_messages[room_idx];
  vec.push_back(msg);
  if (vec.size() > kMaxRoomMessages)
    vec.erase(vec.begin());
}

bool DesktopChatPostMessage(const char *text) {
  if (!text)
    return false;
  std::string clean = DesktopChatSanitizeText(text, 200);
  if (clean.empty())
    return false;

  LiveChatMessage msg;
  {
    std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
    msg.sender = g_nickname;
    msg.text = clean;
    msg.timestamp_utc = (int64_t)time(NULL);
    AppendRoomMessageLocked(g_current_room.load(), msg);
  }
#ifdef _WIN32
  g_scroll_offset = 0;
  RequestChatRepaint();
#endif
  return true;
}

bool DesktopChatSendChallenge(const char *game_title) {
  std::string title = (game_title && game_title[0])
                          ? DesktopChatSanitizeText(game_title, 64)
                          : "";
  if (title.empty()) {
    const char *loaded = CoreGetGameName();
    title = (loaded && loaded[0]) ? DesktopChatSanitizeText(loaded, 64)
                                  : "Street Fighter Alpha 2 (Arcade)";
  }

  NetplayStartHost(55435);
  const char *local_ip = NetplayGetLocalIp();
  std::string host_ip_str = (local_ip && local_ip[0]) ? local_ip : "127.0.0.1";
  std::string room_code = DesktopChatGenerateRoomCode(host_ip_str, 55435);

  LiveChatMessage msg;
  {
    std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
    msg.sender = g_nickname;
    msg.game_challenge = title;
    msg.room_code = room_code;
    msg.host_ip = host_ip_str;
    msg.host_port = 55435;
    msg.text = std::string(Tr("Convite Netplay [", "Netplay Invite [")) +
               room_code + "]: " + title;
    msg.timestamp_utc = (int64_t)time(NULL);
    AppendRoomMessageLocked(g_current_room.load(), msg);
  }

  DesktopChatAnnouncePresence();

  TrayManagerSetStatus(TRAY_STATUS_PLAYING, title.c_str());
  TrayManagerShowNotification("Karamelo Social — Convite Criado",
                              msg.text.c_str(), true);
  CoreSetToast(
      Tr("CONVITE NETPLAY ENVIADO NO CHAT!", "NETPLAY INVITE POSTED IN CHAT!"),
      180);

#ifdef _WIN32
  g_scroll_offset = 0;
  RequestChatRepaint();
#endif
  return true;
}

bool DesktopChatAcceptLastChallenge() {
  LiveChatMessage found;
  bool has_challenge = false;
  {
    std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
    const auto &vec = g_room_messages[g_current_room.load()];
    for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
      if (!it->game_challenge.empty() && (!it->host_ip.empty() || !it->room_code.empty())) {
        found = *it;
        has_challenge = true;
        break;
      }
    }
  }

  if (!has_challenge) {
    CoreSetToast(Tr("NENHUM CONVITE ATIVO NESTE CANAL",
                    "NO ACTIVE INVITE IN THIS CHANNEL"),
                 150);
    return false;
  }

  if (!found.room_code.empty())
    return DesktopChatJoinByRoomCode(found.room_code.c_str());

  bool ok = NetplayStartClient(found.host_ip.c_str(), found.host_port);
  if (VoiceChatIsEnabled())
    VoiceChatStartP2P(found.host_ip.c_str(), found.host_port + 1);

  TrayManagerSetStatus(TRAY_STATUS_PLAYING, found.game_challenge.c_str());
  CoreSetToast(
      Tr("CONECTANDO A PARTIDA NETPLAY...", "CONNECTING TO NETPLAY MATCH..."),
      180);
#ifdef _WIN32
  RequestChatRepaint();
#endif
  return ok;
}

std::vector<LiveChatMessage> DesktopChatGetRoomMessages() {
  std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
  return g_room_messages[g_current_room.load()];
}

void DesktopChatClearRoomMessages() {
  {
    std::lock_guard<std::recursive_mutex> lk(g_chat_mutex);
    g_room_messages[g_current_room.load()].clear();
    SeedWelcomeMessagesLocked();
  }
#ifdef _WIN32
  g_scroll_offset = 0;
  RequestChatRepaint();
#endif
}

// =============================================================
// Wire Framing Helpers (JSON Ephemeral Protocol)
// =============================================================

std::string DesktopChatFormatWireMessage(LiveChatRoom room,
                                         const LiveChatMessage &msg) {
  std::ostringstream oss;
  oss << "{"
      << "\"room\":" << (int)room << ","
      << "\"sender\":" << JsonQuote(DesktopChatSanitizeText(msg.sender, 24))
      << ","
      << "\"text\":" << JsonQuote(DesktopChatSanitizeText(msg.text, 200)) << ","
      << "\"game_challenge\":"
      << JsonQuote(DesktopChatSanitizeText(msg.game_challenge, 64)) << ","
      << "\"room_code\":"
      << JsonQuote(DesktopChatSanitizeText(msg.room_code, 16)) << ","
      << "\"host_ip\":" << JsonQuote(DesktopChatSanitizeText(msg.host_ip, 45))
      << ","
      << "\"host_port\":" << msg.host_port << ","
      << "\"timestamp_utc\":" << msg.timestamp_utc << "}";
  return oss.str();
}

bool DesktopChatParseWireMessage(const std::string &json_str,
                                 LiveChatRoom *out_room,
                                 LiveChatMessage &out_msg) {
  JsonValue root;
  std::string err;
  if (!JsonParse(json_str, root, err) || root.type != JsonValue::Object)
    return false;

  auto get_str = [](const JsonValue &obj, const char *key,
                    const char *def_val) -> std::string {
    const JsonValue *v = obj.Find(key);
    return (v && v->type == JsonValue::String) ? v->str : std::string(def_val);
  };
  auto get_int = [](const JsonValue &obj, const char *key,
                    int64_t def_val) -> int64_t {
    const JsonValue *v = obj.Find(key);
    return (v && v->type == JsonValue::Number) ? (int64_t)v->number : def_val;
  };

  int rm = (int)get_int(root, "room", 0);
  if (rm < 0 || rm >= LIVE_ROOM_COUNT)
    rm = 0;
  if (out_room)
    *out_room = (LiveChatRoom)rm;

  out_msg = LiveChatMessage{};
  out_msg.sender = DesktopChatSanitizeText(get_str(root, "sender", ""), 24);
  out_msg.text = DesktopChatSanitizeText(get_str(root, "text", ""), 200);
  out_msg.game_challenge =
      DesktopChatSanitizeText(get_str(root, "game_challenge", ""), 64);
  out_msg.room_code =
      DesktopChatSanitizeText(get_str(root, "room_code", ""), 16);
  out_msg.host_ip = DesktopChatSanitizeText(get_str(root, "host_ip", ""), 45);
  out_msg.host_port = (int)get_int(root, "host_port", 55435);
  out_msg.timestamp_utc = get_int(root, "timestamp_utc", 0);

  if (out_msg.sender.empty() || out_msg.text.empty())
    return false;

  return true;
}

// =============================================================
// Steam / Battle.net Style Custom-Drawn Dark Social Window
// =============================================================

#ifdef _WIN32
static void EnsureChatFonts() {
  if (!g_font_title)
    g_font_title =
        CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!g_font_bold)
    g_font_bold =
        CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!g_font_body)
    g_font_body =
        CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  if (!g_font_small)
    g_font_small =
        CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void FillSolidRectGdi(HDC hdc, int x, int y, int w, int h,
                             COLORREF col) {
  RECT rc = {x, y, x + w, y + h};
  HBRUSH br = CreateSolidBrush(col);
  FillRect(hdc, &rc, br);
  DeleteObject(br);
}

static void DrawRoundBoxGdi(HDC hdc, int x, int y, int w, int h, int radius,
                            COLORREF fill_col, COLORREF border_col) {
  HBRUSH br = CreateSolidBrush(fill_col);
  HPEN pen = CreatePen(PS_SOLID, 1, border_col);
  HGDIOBJ old_br = SelectObject(hdc, br);
  HGDIOBJ old_pen = SelectObject(hdc, pen);
  RoundRect(hdc, x, y, x + w, y + h, radius, radius);
  SelectObject(hdc, old_br);
  SelectObject(hdc, old_pen);
  DeleteObject(br);
  DeleteObject(pen);
}

static void DrawUtf8TextGdi(HDC hdc, const char *utf8, RECT rc, HFONT font,
                            COLORREF color, UINT format) {
  if (!utf8 || !utf8[0])
    return;
  wchar_t wbuf[512] = {0};
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, 511);
  HGDIOBJ old_f = SelectObject(hdc, font);
  SetTextColor(hdc, color);
  SetBkMode(hdc, TRANSPARENT);
  DrawTextW(hdc, wbuf, -1, &rc, format);
  SelectObject(hdc, old_f);
}

static void GetInitials(const std::string &name, char out_init[3]) {
  out_init[0] = '?';
  out_init[1] = '\0';
  out_init[2] = '\0';
  if (name.empty())
    return;
  out_init[0] = (char)toupper((unsigned char)name[0]);
  for (size_t i = 1; i < name.size(); ++i) {
    if (name[i - 1] == ' ' || name[i - 1] == '_' || name[i - 1] == '-') {
      out_init[1] = (char)toupper((unsigned char)name[i]);
      return;
    }
  }
  if (name.size() > 1)
    out_init[1] = (char)toupper((unsigned char)name[1]);
}

static void FormatTimeHHMM(int64_t ts, char out_buf[16]) {
  if (ts <= 0)
    ts = (int64_t)time(NULL);
  time_t t = (time_t)ts;
  struct tm *lt = localtime(&t);
  if (lt)
    snprintf(out_buf, 16, "%02d:%02d", lt->tm_hour, lt->tm_min);
  else
    snprintf(out_buf, 16, "AGORA");
}

static void PaintSocialWindow(HWND hwnd, HDC target_hdc) {
  EnsureChatFonts();

  RECT client;
  GetClientRect(hwnd, &client);
  int W = client.right - client.left;
  int H = client.bottom - client.top;
  if (W <= 0 || H <= 0)
    return;

  HDC hdc = CreateCompatibleDC(target_hdc);
  HBITMAP hbm = CreateCompatibleBitmap(target_hdc, W, H);
  HGDIOBJ old_bm = SelectObject(hdc, hbm);

  g_hit_rects.clear();

  const int side_w = 254;
  const int top_h = 62;
  const int input_h = 60;

  // Palette (Steam / Battle.net Dark Theme)
  const COLORREF col_bg_main = RGB(23, 26, 33);
  const COLORREF col_bg_side = RGB(30, 35, 45);
  const COLORREF col_bg_profile = RGB(20, 23, 30);
  const COLORREF col_bg_header = RGB(27, 32, 43);
  const COLORREF col_divider = RGB(42, 49, 64);
  const COLORREF col_accent_blue = RGB(102, 192, 244);
  const COLORREF col_active_row = RGB(42, 71, 94);
  const COLORREF col_hover_row = RGB(36, 43, 56);
  const COLORREF col_online_grn = RGB(87, 242, 135);
  const COLORREF col_playing_grn = RGB(144, 186, 60);
  const COLORREF col_busy_red = RGB(237, 66, 69);
  const COLORREF col_txt_white = RGB(235, 240, 245);
  const COLORREF col_txt_muted = RGB(140, 152, 168);
  const COLORREF col_amber = RGB(245, 194, 107);

  // 1. Base surfaces
  FillSolidRectGdi(hdc, 0, 0, side_w, H, col_bg_side);
  FillSolidRectGdi(hdc, 0, 0, side_w, top_h, col_bg_profile);
  FillSolidRectGdi(hdc, side_w, 0, W - side_w, H, col_bg_main);
  FillSolidRectGdi(hdc, side_w, 0, W - side_w, top_h, col_bg_header);
  FillSolidRectGdi(hdc, side_w, H - input_h, W - side_w, input_h, col_bg_header);

  // Dividers
  FillSolidRectGdi(hdc, side_w, 0, 1, H, col_divider);
  FillSolidRectGdi(hdc, 0, top_h, W, 1, col_divider);
  FillSolidRectGdi(hdc, side_w, H - input_h, W - side_w, 1, col_divider);

  // 2. Profile Card
  std::string my_nick = DesktopChatGetNickname();
  std::string my_tag = DesktopChatGetKarameloTag();
  TrayPresenceStatus my_st = TrayManagerGetStatus();
  COLORREF st_col = (my_st == TRAY_STATUS_PLAYING) ? col_playing_grn
                    : (my_st == TRAY_STATUS_BUSY)  ? col_busy_red
                                                   : col_online_grn;

  DrawRoundBoxGdi(hdc, 12, 11, 40, 40, 8, RGB(42, 71, 94), st_col);
  char my_init[3];
  GetInitials(my_nick, my_init);
  RECT rc_av = {12, 11, 52, 51};
  DrawUtf8TextGdi(hdc, my_init, rc_av, g_font_bold, col_accent_blue,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  DrawRoundBoxGdi(hdc, 42, 41, 12, 12, 12, st_col, col_bg_profile);

  RECT rc_nick = {58, 12, side_w - 52, 28};
  DrawUtf8TextGdi(hdc, my_nick.c_str(), rc_nick, g_font_bold, col_txt_white,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT rc_tag = {58, 28, side_w - 52, 42};
  DrawUtf8TextGdi(hdc, my_tag.c_str(), rc_tag, g_font_small, col_accent_blue,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT rc_status = {58, 42, side_w - 52, 56};
  DrawUtf8TextGdi(hdc, my_st == TRAY_STATUS_PLAYING ? "Em Jogo" : "Online • BattleTag",
                  rc_status, g_font_small, st_col,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  g_hit_rects.push_back({HIT_PROFILE_STATUS, {12, 11, side_w - 52, 55}});

  // Mic button
  bool muted = VoiceChatIsMicMuted();
  RECT rc_mic = {side_w - 46, 15, side_w - 12, 47};
  COLORREF mic_bg = (g_hover_zone == HIT_MIC_MUTE) ? RGB(52, 62, 80) : RGB(32, 38, 50);
  DrawRoundBoxGdi(hdc, rc_mic.left, rc_mic.top, 34, 32, 6, mic_bg, muted ? col_busy_red : col_online_grn);
  DrawUtf8TextGdi(hdc, muted ? "MUTE" : "MIC", rc_mic, g_font_small,
                  muted ? col_busy_red : col_online_grn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_MIC_MUTE, rc_mic});

  // 3. Tab Switcher: [Canais]  [Amigos]
  int tab_y = 68;
  bool is_tab_rooms = (g_sidebar_tab.load() == SIDEBAR_ROOMS);
  RECT rc_tab1 = {10, tab_y, 122, tab_y + 26};
  RECT rc_tab2 = {130, tab_y, side_w - 10, tab_y + 26};

  DrawRoundBoxGdi(hdc, rc_tab1.left, rc_tab1.top, rc_tab1.right - rc_tab1.left, 26, 6,
                  is_tab_rooms ? col_active_row : RGB(32, 38, 48), is_tab_rooms ? col_accent_blue : col_divider);
  DrawUtf8TextGdi(hdc, Tr("Lobbies", "Lobbies"), rc_tab1, is_tab_rooms ? g_font_bold : g_font_small,
                  is_tab_rooms ? col_txt_white : col_txt_muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_TAB_ROOMS, rc_tab1});

  DrawRoundBoxGdi(hdc, rc_tab2.left, rc_tab2.top, rc_tab2.right - rc_tab2.left, 26, 6,
                  !is_tab_rooms ? col_active_row : RGB(32, 38, 48), !is_tab_rooms ? col_accent_blue : col_divider);
  DrawUtf8TextGdi(hdc, Tr("Amigos", "Friends"), rc_tab2, !is_tab_rooms ? g_font_bold : g_font_small,
                  !is_tab_rooms ? col_txt_white : col_txt_muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_TAB_FRIENDS, rc_tab2});

  int ry = 100;
  if (is_tab_rooms) {
    // 4A. CANAIS DE BATE-PAPO
    LiveChatRoom active_room = DesktopChatGetRoom();
    for (int r = 0; r < LIVE_ROOM_COUNT; ++r) {
      int hit_id = HIT_ROOM_0 + r;
      RECT rc_row = {10, ry, side_w - 10, ry + 32};
      bool is_sel = ((int)active_room == r);
      bool is_hov = (g_hover_zone == hit_id);

      if (is_sel) {
        DrawRoundBoxGdi(hdc, rc_row.left, rc_row.top, rc_row.right - rc_row.left, 32, 6, col_active_row, col_active_row);
        FillSolidRectGdi(hdc, rc_row.left, rc_row.top + 5, 3, 22, col_accent_blue);
      } else if (is_hov) {
        DrawRoundBoxGdi(hdc, rc_row.left, rc_row.top, rc_row.right - rc_row.left, 32, 6, col_hover_row, col_hover_row);
      }

      RECT rc_txt = {rc_row.left + 12, rc_row.top, rc_row.right - 10, rc_row.bottom};
      DrawUtf8TextGdi(hdc, DesktopChatGetRoomName((LiveChatRoom)r), rc_txt,
                      is_sel ? g_font_bold : g_font_body,
                      is_sel ? col_txt_white : col_txt_muted,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      g_hit_rects.push_back({hit_id, rc_row});
      ry += 35;
    }

    // JOGADORES ATIVOS NO LOBBY
    ry += 8;
    FillSolidRectGdi(hdc, 14, ry - 4, side_w - 28, 1, col_divider);
    RECT rc_sec2 = {16, ry, side_w - 12, ry + 18};
    DrawUtf8TextGdi(hdc, Tr("JOGADORES ATIVOS", "ACTIVE PLAYERS"), rc_sec2, g_font_small, col_txt_muted,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ry += 22;

    auto peers = DesktopChatGetActivePeers();
    if (peers.empty()) {
      LobbyPresencePeer me_peer;
      me_peer.tag = my_tag;
      me_peer.current_game = CoreIsRunning() ? CoreGetGameName() : Tr("No Lobby Karamelo", "In Karamelo Lobby");
      me_peer.status = CoreIsRunning() ? 2 : 1;
      peers.push_back(me_peer);
    }

    for (size_t p = 0; p < peers.size() && ry + 40 < H - 10; ++p) {
      RECT rc_p = {10, ry, side_w - 10, ry + 38};
      bool in_g = (peers[p].status == 2);
      DrawRoundBoxGdi(hdc, 14, ry + 4, 30, 30, 6, RGB(36, 46, 62), in_g ? col_playing_grn : col_online_grn);

      char p_init[3];
      GetInitials(peers[p].tag, p_init);
      RECT rc_pi = {14, ry + 4, 44, ry + 34};
      DrawUtf8TextGdi(hdc, p_init, rc_pi, g_font_small, col_txt_white, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      RECT rc_pn = {50, ry + 3, side_w - 60, ry + 18};
      DrawUtf8TextGdi(hdc, peers[p].tag.c_str(), rc_pn, g_font_bold,
                      peers[p].tag == my_tag ? col_amber : col_txt_white,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      RECT rc_pg = {50, ry + 18, side_w - 60, ry + 34};
      std::string g_name = peers[p].current_game.empty() ? Tr("Online", "Online") : peers[p].current_game;
      DrawUtf8TextGdi(hdc, g_name.c_str(), rc_pg, g_font_small,
                      in_g ? col_playing_grn : col_online_grn,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      if (peers[p].has_challenge && peers[p].tag != my_tag) {
        RECT rc_join = {side_w - 56, ry + 7, side_w - 12, ry + 31};
        DrawRoundBoxGdi(hdc, rc_join.left, rc_join.top, 44, 24, 4, RGB(45, 125, 70), col_online_grn);
        DrawUtf8TextGdi(hdc, "Entrar", rc_join, g_font_small, col_txt_white, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        g_hit_rects.push_back({(int)(HIT_PEER_JOIN_BASE + p), rc_join});
      }

      ry += 42;
    }
  } else {
    // 4B. LISTA DE AMIGOS PERSISTENTE (BattleTag / Steam Roster)
    RECT rc_sec_f = {16, ry, side_w - 12, ry + 18};
    DrawUtf8TextGdi(hdc, Tr("ROSTER DE AMIGOS", "FRIENDS ROSTER"), rc_sec_f, g_font_small, col_txt_muted,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ry += 22;

    auto friends = DesktopChatGetFriends();
    for (size_t f = 0; f < friends.size() && ry + 44 < H - 50; ++f) {
      RECT rc_f = {10, ry, side_w - 10, ry + 40};
      COLORREF f_st_col = (friends[f].status == 2) ? col_playing_grn
                          : (friends[f].status == 1) ? col_online_grn
                                                     : RGB(110, 120, 135);

      DrawRoundBoxGdi(hdc, 14, ry + 4, 32, 32, 6, RGB(34, 42, 56), f_st_col);
      char f_init[3];
      GetInitials(friends[f].alias, f_init);
      RECT rc_fi = {14, ry + 4, 46, ry + 36};
      DrawUtf8TextGdi(hdc, f_init, rc_fi, g_font_small, col_txt_white, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      std::string disp_name = (friends[f].favorite ? "★ " : "") + friends[f].alias;
      RECT rc_fn = {52, ry + 3, side_w - 60, ry + 19};
      DrawUtf8TextGdi(hdc, disp_name.c_str(), rc_fn, g_font_bold,
                      friends[f].favorite ? col_amber : col_txt_white,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      std::string f_sub = (friends[f].status == 2)
                              ? ("Jogando " + friends[f].current_game)
                              : (friends[f].status == 1 ? "Online" : "Offline");
      RECT rc_fs = {52, ry + 19, side_w - 60, ry + 35};
      DrawUtf8TextGdi(hdc, f_sub.c_str(), rc_fs, g_font_small, f_st_col,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      if (friends[f].status > 0) {
        RECT rc_act = {side_w - 56, ry + 7, side_w - 12, ry + 31};
        bool is_join = (friends[f].status == 2 && !friends[f].room_code.empty());
        DrawRoundBoxGdi(hdc, rc_act.left, rc_act.top, 44, 24, 4,
                        is_join ? RGB(45, 125, 70) : col_active_row,
                        is_join ? col_online_grn : col_accent_blue);
        DrawUtf8TextGdi(hdc, is_join ? "Entrar" : "Desafio", rc_act, g_font_small, col_txt_white,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        g_hit_rects.push_back({(int)(HIT_FRIEND_CHAL_BASE + f), rc_act});
      }

      g_hit_rects.push_back({(int)(HIT_FRIEND_ROW_BASE + f), {10, ry, side_w - 60, ry + 40}});
      ry += 44;
    }

    // [+ Adicionar Amigo] Button
    RECT rc_add_btn = {12, H - 46, side_w - 12, H - 14};
    DrawRoundBoxGdi(hdc, rc_add_btn.left, rc_add_btn.top, rc_add_btn.right - rc_add_btn.left, 32, 6,
                    g_hover_zone == HIT_BTN_ADD_FRIEND ? RGB(52, 70, 96) : RGB(36, 48, 66), col_accent_blue);
    DrawUtf8TextGdi(hdc, Tr("+ Adicionar Amigo por Tag", "+ Add Friend by Tag"), rc_add_btn, g_font_bold,
                    col_accent_blue, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    g_hit_rects.push_back({HIT_BTN_ADD_FRIEND, rc_add_btn});
  }

  // 5. Right Chat Header
  LiveChatRoom active_room = DesktopChatGetRoom();
  RECT rc_ch_title = {side_w + 18, 10, W - 340, 34};
  DrawUtf8TextGdi(hdc, DesktopChatGetRoomName(active_room), rc_ch_title,
                  g_font_title, col_txt_white, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  char sub_hdr[160];
  snprintf(sub_hdr, sizeof(sub_hdr),
           "BattleTag: %s  •  Voz P2P: %s  •  Gate: %s",
           DesktopChatGetKarameloTag(),
           VoiceChatGetDenoiseEngineName(VoiceChatGetDenoiseEngine()),
           VoiceChatGetVadSensitivityName(VoiceChatGetVadSensitivity()));
  RECT rc_ch_sub = {side_w + 18, 34, W - 340, 54};
  DrawUtf8TextGdi(hdc, sub_hdr, rc_ch_sub, g_font_small, col_txt_muted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  // Header Buttons
  int bx = W - 14;
  // [Limpar]
  RECT rc_btn_clr = {bx - 68, 16, bx, 46};
  DrawRoundBoxGdi(hdc, rc_btn_clr.left, rc_btn_clr.top, 68, 30, 6,
                  g_hover_zone == HIT_BTN_CLEAR ? RGB(52, 60, 76) : RGB(36, 42, 54), col_divider);
  DrawUtf8TextGdi(hdc, Tr("Limpar", "Clear"), rc_btn_clr, g_font_small, col_txt_muted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_CLEAR, rc_btn_clr});
  bx -= 76;

  // [Filtro IA]
  RECT rc_btn_dsp = {bx - 116, 16, bx, 46};
  DrawRoundBoxGdi(hdc, rc_btn_dsp.left, rc_btn_dsp.top, 116, 30, 6,
                  g_hover_zone == HIT_BTN_DENOISE ? RGB(52, 60, 76) : RGB(36, 42, 54), col_divider);
  char dsp_lbl[48];
  snprintf(dsp_lbl, sizeof(dsp_lbl), "IA: %s",
           VoiceChatGetDenoiseEngine() == VOICE_DENOISE_DEEPFILTERNET ? "DeepFilter"
           : VoiceChatGetDenoiseEngine() == VOICE_DENOISE_RNNOISE   ? "RNNoise"
                                                                  : "Cru/Off");
  DrawUtf8TextGdi(hdc, dsp_lbl, rc_btn_dsp, g_font_small, col_accent_blue, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_DENOISE, rc_btn_dsp});
  bx -= 124;

  // [Desafiar Netplay]
  RECT rc_btn_chal = {bx - 146, 16, bx, 46};
  DrawRoundBoxGdi(hdc, rc_btn_chal.left, rc_btn_chal.top, 146, 30, 6,
                  g_hover_zone == HIT_BTN_CHALLENGE ? RGB(52, 92, 122) : col_active_row, col_accent_blue);
  DrawUtf8TextGdi(hdc, Tr("Desafiar Netplay", "Challenge Netplay"), rc_btn_chal, g_font_bold, col_txt_white,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_CHALLENGE, rc_btn_chal});

  // 6. Message Feed
  int feed_top = top_h + 1;
  int feed_bot = H - input_h - 1;
  int feed_w = W - side_w;

  HRGN clip_rgn = CreateRectRgn(side_w + 1, feed_top, W, feed_bot);
  SelectClipRgn(hdc, clip_rgn);

  std::vector<LiveChatMessage> msgs = DesktopChatGetRoomMessages();
  int cur_y = feed_top + 14 - g_scroll_offset;

  for (size_t i = 0; i < msgs.size(); ++i) {
    const auto &m = msgs[i];
    bool is_sys = (m.sender == "Karamelo Social" || m.sender == "Karamelo Live");
    bool is_me = (m.sender == my_nick);
    bool is_invite = !m.game_challenge.empty();

    int ax = side_w + 16;
    int ay = cur_y + 2;
    COLORREF av_bg = is_sys ? RGB(38, 52, 46) : (is_me ? RGB(58, 46, 28) : RGB(36, 52, 72));
    COLORREF av_bd = is_sys ? col_online_grn : (is_me ? col_amber : col_accent_blue);
    DrawRoundBoxGdi(hdc, ax, ay, 34, 34, 8, av_bg, av_bd);

    char s_init[3];
    GetInitials(m.sender, s_init);
    RECT rc_si = {ax, ay, ax + 34, ay + 34};
    DrawUtf8TextGdi(hdc, s_init, rc_si, g_font_bold, av_bd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int tx = side_w + 60;
    RECT rc_sender = {tx, cur_y, tx + 220, cur_y + 18};
    DrawUtf8TextGdi(hdc, m.sender.c_str(), rc_sender, g_font_bold, av_bd, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    char t_str[16];
    FormatTimeHHMM(m.timestamp_utc, t_str);
    RECT rc_time = {tx + (int)m.sender.size() * 8 + 12, cur_y + 1, W - 20, cur_y + 18};
    DrawUtf8TextGdi(hdc, t_str, rc_time, g_font_small, col_txt_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    cur_y += 20;

    if (is_invite) {
      int card_w = std::min(feed_w - 84, 440);
      int card_h = 72;
      RECT rc_card = {tx, cur_y + 2, tx + card_w, cur_y + 2 + card_h};
      DrawRoundBoxGdi(hdc, rc_card.left, rc_card.top, card_w, card_h, 8, RGB(26, 42, 36), col_online_grn);

      RECT rc_ct1 = {rc_card.left + 12, rc_card.top + 6, rc_card.right - 130, rc_card.top + 22};
      DrawUtf8TextGdi(hdc, Tr("CONVITE DE PARTIDA — 1-CLICK NETPLAY", "MATCH INVITE — 1-CLICK NETPLAY"),
                      rc_ct1, g_font_small, col_online_grn, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      RECT rc_ct2 = {rc_card.left + 12, rc_card.top + 24, rc_card.right - 130, rc_card.top + 46};
      DrawUtf8TextGdi(hdc, m.game_challenge.c_str(), rc_ct2, g_font_bold, col_txt_white,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      char host_info[96];
      snprintf(host_info, sizeof(host_info), "Código: %s  •  Host: %s:%d",
               m.room_code.empty() ? "DIRETO" : m.room_code.c_str(),
               m.host_ip.c_str(), m.host_port);
      RECT rc_ct3 = {rc_card.left + 12, rc_card.top + 48, rc_card.right - 130, rc_card.top + 64};
      DrawUtf8TextGdi(hdc, host_info, rc_ct3, g_font_small, col_txt_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      RECT rc_join = {rc_card.right - 124, rc_card.top + 18, rc_card.right - 12, rc_card.top + 52};
      DrawRoundBoxGdi(hdc, rc_join.left, rc_join.top, 112, 34, 6,
                      g_hover_zone == HIT_BTN_ACCEPT_CARD ? RGB(58, 165, 93) : RGB(45, 125, 70), col_online_grn);
      DrawUtf8TextGdi(hdc, Tr("Entrar Agora", "Join Match"), rc_join, g_font_bold, col_txt_white,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      g_hit_rects.push_back({HIT_BTN_ACCEPT_CARD, rc_join});

      cur_y += card_h + 14;
    } else {
      wchar_t wmsg[512] = {0};
      MultiByteToWideChar(CP_UTF8, 0, m.text.c_str(), -1, wmsg, 511);
      HGDIOBJ old_f = SelectObject(hdc, g_font_body);
      RECT rc_calc = {tx, cur_y, W - 24, cur_y + 200};
      DrawTextW(hdc, wmsg, -1, &rc_calc, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
      int text_h = std::max(20, (int)(rc_calc.bottom - rc_calc.top));

      RECT rc_draw = {tx, cur_y, W - 24, cur_y + text_h};
      SetTextColor(hdc, is_sys ? RGB(175, 220, 195) : col_txt_white);
      SetBkMode(hdc, TRANSPARENT);
      DrawTextW(hdc, wmsg, -1, &rc_draw, DT_LEFT | DT_WORDBREAK);
      SelectObject(hdc, old_f);

      cur_y += text_h + 12;
    }
  }

  SelectClipRgn(hdc, NULL);
  DeleteObject(clip_rgn);

  // 7. Bottom Input Bar
  int inp_x = side_w + 16;
  int inp_y = H - input_h + 12;
  int send_w = 88;
  int inp_w = W - inp_x - send_w - 24;

  DrawRoundBoxGdi(hdc, inp_x, inp_y, inp_w, 36, 6, RGB(18, 22, 30), col_divider);
  RECT rc_inp_txt = {inp_x + 12, inp_y, inp_x + inp_w - 12, inp_y + 36};
  if (g_input_text.empty()) {
    DrawUtf8TextGdi(hdc, Tr("Digite uma mensagem ou Código (KRM-XXXX)... [Enter]",
                            "Type a message or Room Code (KRM-XXXX)... [Enter]"),
                    rc_inp_txt, g_font_body, RGB(100, 115, 135), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  } else {
    DrawUtf8TextGdi(hdc, g_input_text.c_str(), rc_inp_txt, g_font_body, col_txt_white,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }

  RECT rc_btn_send = {inp_x + inp_w + 12, inp_y, inp_x + inp_w + 12 + send_w, inp_y + 36};
  DrawRoundBoxGdi(hdc, rc_btn_send.left, rc_btn_send.top, send_w, 36, 6,
                  g_hover_zone == HIT_BTN_SEND ? RGB(52, 92, 122) : col_active_row, col_accent_blue);
  DrawUtf8TextGdi(hdc, Tr("Enviar", "Send"), rc_btn_send, g_font_bold, col_txt_white,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_SEND, rc_btn_send});

  BitBlt(target_hdc, 0, 0, W, H, hdc, 0, 0, SRCCOPY);

  SelectObject(hdc, old_bm);
  DeleteObject(hbm);
  DeleteDC(hdc);
}

static int HitTestSocialWindow(int x, int y) {
  for (auto it = g_hit_rects.rbegin(); it != g_hit_rects.rend(); ++it) {
    if (x >= it->rc.left && x <= it->rc.right && y >= it->rc.top && y <= it->rc.bottom)
      return it->zone;
  }
  return HIT_NONE;
}

static LRESULT CALLBACK SocialChatWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_SETCURSOR: {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    int hz = HitTestSocialWindow(pt.x, pt.y);
    if (hz != g_hover_zone) {
      g_hover_zone = hz;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    SetCursor(LoadCursor(NULL, hz != HIT_NONE ? IDC_HAND : IDC_ARROW));
    return 0;
  }

  case WM_MOUSEWHEEL: {
    short delta = GET_WHEEL_DELTA_WPARAM(wparam);
    g_scroll_offset -= (delta / WHEEL_DELTA) * 28;
    if (g_scroll_offset < 0) g_scroll_offset = 0;
    if (g_scroll_offset > 1200) g_scroll_offset = 1200;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    int mx = GET_X_LPARAM(lparam);
    int my = GET_Y_LPARAM(lparam);
    int hz = HitTestSocialWindow(mx, my);
    if (hz >= HIT_ROOM_0 && hz <= HIT_ROOM_3) {
      DesktopChatSetRoom((LiveChatRoom)(hz - HIT_ROOM_0));
    } else if (hz == HIT_TAB_ROOMS) {
      g_sidebar_tab.store(SIDEBAR_ROOMS);
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (hz == HIT_TAB_FRIENDS) {
      g_sidebar_tab.store(SIDEBAR_FRIENDS);
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (hz == HIT_PROFILE_STATUS) {
      TrayPresenceStatus cur = TrayManagerGetStatus();
      TrayPresenceStatus next = (cur == TRAY_STATUS_ONLINE) ? TRAY_STATUS_BUSY : TRAY_STATUS_ONLINE;
      TrayManagerSetStatus(next, NULL);
      DesktopChatAnnouncePresence();
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (hz == HIT_MIC_MUTE) {
      VoiceChatToggleMicMute();
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (hz == HIT_BTN_CHALLENGE) {
      DesktopChatSendChallenge(NULL);
    } else if (hz == HIT_BTN_ACCEPT_CARD) {
      DesktopChatAcceptLastChallenge();
    } else if (hz == HIT_BTN_DENOISE) {
      int next_d = ((int)VoiceChatGetDenoiseEngine() + 1) % 3;
      VoiceChatSetDenoiseEngine((VoiceDenoiseEngine)next_d);
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (hz == HIT_BTN_CLEAR) {
      DesktopChatClearRoomMessages();
    } else if (hz == HIT_BTN_SEND) {
      if (!g_input_text.empty()) {
        std::string to_send = g_input_text;
        g_input_text.clear();
        if (to_send.rfind("KRM-", 0) == 0) {
          DesktopChatJoinByRoomCode(to_send.c_str());
        } else {
          DesktopChatPostMessage(to_send.c_str());
        }
      }
    } else if (hz == HIT_BTN_ADD_FRIEND) {
      if (!g_input_text.empty() && g_input_text.find('#') != std::string::npos) {
        DesktopChatAddFriend(g_input_text.c_str(), g_input_text.c_str());
        g_input_text.clear();
      } else {
        DesktopChatAddFriend("Amigo#7777", "Amigo Gamer");
      }
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (hz >= HIT_PEER_JOIN_BASE && hz < HIT_PEER_JOIN_BASE + 50) {
      int idx = hz - HIT_PEER_JOIN_BASE;
      auto peers = DesktopChatGetActivePeers();
      if (idx >= 0 && idx < (int)peers.size()) {
        if (!peers[idx].room_code.empty())
          DesktopChatJoinByRoomCode(peers[idx].room_code.c_str());
        else if (!peers[idx].host_ip.empty())
          NetplayStartClient(peers[idx].host_ip.c_str(), peers[idx].host_port);
      }
    } else if (hz >= HIT_FRIEND_CHAL_BASE && hz < HIT_FRIEND_CHAL_BASE + 50) {
      int idx = hz - HIT_FRIEND_CHAL_BASE;
      auto friends = DesktopChatGetFriends();
      if (idx >= 0 && idx < (int)friends.size()) {
        if (!friends[idx].room_code.empty()) {
          DesktopChatJoinByRoomCode(friends[idx].room_code.c_str());
        } else {
          DesktopChatSendChallenge(friends[idx].last_game.empty() ? NULL : friends[idx].last_game.c_str());
        }
      }
    } else if (hz >= HIT_FRIEND_ROW_BASE && hz < HIT_FRIEND_ROW_BASE + 50) {
      int idx = hz - HIT_FRIEND_ROW_BASE;
      auto friends = DesktopChatGetFriends();
      if (idx >= 0 && idx < (int)friends.size()) {
        DesktopChatToggleFriendFavorite(friends[idx].tag.c_str());
        InvalidateRect(hwnd, NULL, FALSE);
      }
    }
    return 0;
  }

  case WM_KEYDOWN: {
    if (wparam == VK_ESCAPE) {
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    if (wparam == VK_RETURN) {
      if (!g_input_text.empty()) {
        std::string to_send = g_input_text;
        g_input_text.clear();
        if (to_send.rfind("KRM-", 0) == 0) {
          DesktopChatJoinByRoomCode(to_send.c_str());
        } else {
          DesktopChatPostMessage(to_send.c_str());
        }
      }
      return 0;
    }
    if (wparam == VK_BACK) {
      while (!g_input_text.empty()) {
        unsigned char c = (unsigned char)g_input_text.back();
        g_input_text.pop_back();
        if ((c & 0xC0) != 0x80) break;
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (wparam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
      if (OpenClipboard(hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
          const wchar_t *wstr = (const wchar_t *)GlobalLock(hData);
          if (wstr) {
            char u8[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, wstr, -1, u8, sizeof(u8) - 1, NULL, NULL);
            g_input_text = DesktopChatSanitizeText(g_input_text + u8, 180);
            GlobalUnlock(hData);
          }
        }
        CloseClipboard();
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }
    break;
  }

  case WM_CHAR: {
    wchar_t wc = (wchar_t)wparam;
    if (wc >= 0x20 && wc != 0x7F && g_input_text.size() < 180) {
      wchar_t wbuf[2] = {wc, 0};
      char u8[8] = {0};
      WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, u8, sizeof(u8), NULL, NULL);
      g_input_text += u8;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  }

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    PaintSocialWindow(hwnd, hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}
#else
// Multiplatform SDL3 Companion Window (Linux & macOS)
static SDL_Window* g_chat_sdl_win = nullptr;
static SDL_Renderer* g_chat_sdl_ren = nullptr;
static SDL_Texture* g_chat_sdl_tex = nullptr;
static std::vector<uint32_t> g_chat_sdl_pixels;
static int g_chat_sdl_w = 820;
static int g_chat_sdl_h = 540;

static void DrawOverlayRectBlend(uint32_t *fb, int fb_w, int fb_h, int rx,
                                 int ry, int rw, int rh, uint32_t rgb,
                                 uint32_t border_rgb);
static void DrawOverlayText8x8(uint32_t *fb, int fb_w, int fb_h, int px, int py,
                               const char *text, uint32_t color);

void DesktopChatShowWindowSdl() {
  if (!g_chat_sdl_win) {
    g_chat_sdl_win = SDL_CreateWindow(
        "Karamelo Social — Amigos & Bate-Papo",
        g_chat_sdl_w, g_chat_sdl_h, SDL_WINDOW_RESIZABLE);
    if (g_chat_sdl_win) {
      g_chat_sdl_ren = SDL_CreateRenderer(g_chat_sdl_win, NULL);
      g_chat_sdl_tex = SDL_CreateTexture(
          g_chat_sdl_ren, SDL_PIXELFORMAT_ARGB8888,
          SDL_TEXTUREACCESS_STREAMING, g_chat_sdl_w, g_chat_sdl_h);
      g_chat_sdl_pixels.assign(g_chat_sdl_w * g_chat_sdl_h, 0xFF171A21u);
    }
  }
  if (g_chat_sdl_win) {
    SDL_ShowWindow(g_chat_sdl_win);
  }
}

void DesktopChatHideWindowSdl() {
  if (g_chat_sdl_win) {
    SDL_HideWindow(g_chat_sdl_win);
  }
}

void DesktopChatToggleWindowSdl() {
  if (DesktopChatIsWindowVisibleSdl())
    DesktopChatHideWindowSdl();
  else
    DesktopChatShowWindowSdl();
}

bool DesktopChatIsWindowVisibleSdl() {
  if (!g_chat_sdl_win) return false;
  return (SDL_GetWindowFlags(g_chat_sdl_win) & SDL_WINDOW_HIDDEN) == 0;
}

void DesktopChatProcessSdlEvent(const void* raw_event) {
  if (!raw_event || !g_chat_sdl_win) return;
  const SDL_Event* event = (const SDL_Event*)raw_event;

  if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    if (event->window.windowID == SDL_GetWindowID(g_chat_sdl_win)) {
      DesktopChatHideWindowSdl();
    }
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (event->button.windowID == SDL_GetWindowID(g_chat_sdl_win)) {
      int mx = (int)event->button.x;
      int my = (int)event->button.y;
      if (my >= 68 && my <= 94) {
        if (mx >= 10 && mx <= 122) g_sidebar_tab.store(SIDEBAR_ROOMS);
        else if (mx >= 130 && mx <= 244) g_sidebar_tab.store(SIDEBAR_FRIENDS);
      }
      if (g_sidebar_tab.load() == SIDEBAR_ROOMS && mx < 250 && my >= 100 && my < 250) {
        int r = (my - 100) / 35;
        if (r >= 0 && r < 4) DesktopChatSetRoom((LiveChatRoom)r);
      }
      if (mx >= g_chat_sdl_w - 180 && mx <= g_chat_sdl_w - 20 && my >= 16 && my <= 46) {
        DesktopChatSendChallenge(NULL);
      }
      if (mx >= g_chat_sdl_w - 100 && my >= g_chat_sdl_h - 50) {
        if (!g_input_text.empty()) {
          DesktopChatPostMessage(g_input_text.c_str());
          g_input_text.clear();
        }
      }
    }
  } else if (event->type == SDL_EVENT_KEY_DOWN) {
    if (event->key.windowID == SDL_GetWindowID(g_chat_sdl_win)) {
      if (event->key.scancode == SDL_SCANCODE_RETURN) {
        if (!g_input_text.empty()) {
          DesktopChatPostMessage(g_input_text.c_str());
          g_input_text.clear();
        }
      } else if (event->key.scancode == SDL_SCANCODE_BACKSPACE) {
        if (!g_input_text.empty()) g_input_text.pop_back();
      } else if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
        DesktopChatHideWindowSdl();
      }
    }
  } else if (event->type == SDL_EVENT_TEXT_INPUT) {
    if (event->text.windowID == SDL_GetWindowID(g_chat_sdl_win)) {
      if (g_input_text.size() < 180) {
        g_input_text += event->text.text;
      }
    }
  }
}

void DesktopChatRenderSdlWindow() {
  if (!g_chat_sdl_win || !DesktopChatIsWindowVisibleSdl() || !g_chat_sdl_ren || !g_chat_sdl_tex)
    return;

  std::fill(g_chat_sdl_pixels.begin(), g_chat_sdl_pixels.end(), 0xFF171A21u);

  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 0, 0, 254, g_chat_sdl_h, 0x001E232Du, 0x002A3140u);
  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 254, 0, g_chat_sdl_w - 254, 62, 0x001B202Bu, 0x002A3140u);
  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 254, g_chat_sdl_h - 60, g_chat_sdl_w - 254, 60, 0x001B202Bu, 0x002A3140u);

  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 16, 16, DesktopChatGetNickname(), 0x00FFFFFFu);
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 16, 28, DesktopChatGetKarameloTag(), 0x0066C0F4u);
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 16, 42, "Online • BattleTag", 0x0057F287u);

  bool is_rooms = (g_sidebar_tab.load() == SIDEBAR_ROOMS);
  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 10, 68, 112, 26, is_rooms ? 0x002A475Eu : 0x001E232Du, 0x0066C0F4u);
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 20, 77, "LOBBIES", is_rooms ? 0x00FFFFFFu : 0x008C98A8u);

  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 130, 68, 114, 26, !is_rooms ? 0x002A475Eu : 0x001E232Du, 0x0066C0F4u);
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 142, 77, "AMIGOS", !is_rooms ? 0x00FFFFFFu : 0x008C98A8u);

  if (is_rooms) {
    int ry = 104;
    for (int r = 0; r < 4; ++r) {
      bool is_cur = (r == (int)DesktopChatGetRoom());
      DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 10, ry, 234, 30, is_cur ? 0x002A475Eu : 0x00242B38u, is_cur ? 0x0066C0F4u : 0x002A3140u);
      DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 18, ry + 11, DesktopChatGetRoomName((LiveChatRoom)r), 0x00FFFFFFu);
      ry += 35;
    }
  } else {
    int fy = 104;
    DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 16, fy, "ROSTER DE AMIGOS:", 0x008C98A8u);
    fy += 16;
    auto friends = DesktopChatGetFriends();
    for (size_t i = 0; i < friends.size() && fy < g_chat_sdl_h - 20; ++i) {
      std::string f_title = (friends[i].favorite ? "* " : "") + friends[i].alias;
      DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 16, fy, f_title.c_str(), 0x00FFFFFFu);
      std::string f_sub = friends[i].tag + " (" + (friends[i].status == 2 ? "Jogando" : (friends[i].status == 1 ? "Online" : "Offline")) + ")";
      DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 16, fy + 10, f_sub.c_str(), friends[i].status > 0 ? 0x0057F287u : 0x008C98A8u);
      fy += 26;
    }
  }

  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 270, 26, DesktopChatGetRoomName(DesktopChatGetRoom()), 0x00FFFFFFu);
  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, g_chat_sdl_w - 180, 16, 164, 30, 0x002A475Eu, 0x0066C0F4u);
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, g_chat_sdl_w - 168, 27, "DESAFIAR NETPLAY", 0x00FFFFFFu);

  auto msgs = DesktopChatGetRoomMessages();
  int my = 80;
  for (size_t i = 0; i < msgs.size() && my < g_chat_sdl_h - 80; ++i) {
    DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 270, my, msgs[i].sender.c_str(), 0x0066C0F4u);
    DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 270, my + 10, msgs[i].text.c_str(), 0x00FFFFFFu);
    my += 26;
  }

  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 270, g_chat_sdl_h - 48, g_chat_sdl_w - 380, 34, 0x00141821u, 0x002A3140u);
  std::string in_show = g_input_text.empty() ? "Digite sua mensagem... [Enter envia]" : g_input_text;
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, 280, g_chat_sdl_h - 35, in_show.c_str(), g_input_text.empty() ? 0x008C98A8u : 0x00FFFFFFu);

  DrawOverlayRectBlend(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, g_chat_sdl_w - 96, g_chat_sdl_h - 48, 80, 34, 0x002A475Eu, 0x0066C0F4u);
  DrawOverlayText8x8(g_chat_sdl_pixels.data(), g_chat_sdl_w, g_chat_sdl_h, g_chat_sdl_w - 78, g_chat_sdl_h - 35, "ENVIAR", 0x00FFFFFFu);

  SDL_UpdateTexture(g_chat_sdl_tex, NULL, g_chat_sdl_pixels.data(), g_chat_sdl_w * sizeof(uint32_t));
  SDL_RenderTexture(g_chat_sdl_ren, g_chat_sdl_tex, NULL, NULL);
  SDL_RenderPresent(g_chat_sdl_ren);
}
#endif

// =============================================================
// Window Visibility Management
// =============================================================

void DesktopChatShowWindow() {
#ifdef _WIN32
  if (!g_chat_hwnd) {
    HINSTANCE hInst = GetModuleHandle(NULL);
    static bool s_class_reg = false;
    if (!s_class_reg) {
      WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
      wc.style = CS_HREDRAW | CS_VREDRAW;
      wc.lpfnWndProc = SocialChatWndProc;
      wc.hInstance = hInst;
      wc.hCursor = LoadCursor(NULL, IDC_ARROW);
      wc.hbrBackground = NULL;
      wc.hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APP_ICON),
                                  IMAGE_ICON, 32, 32, 0);
      wc.hIconSm = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_APP_ICON),
                                    IMAGE_ICON, 16, 16, 0);
      wc.lpszClassName = L"KarameloSocialChatClass";
      RegisterClassExW(&wc);
      s_class_reg = true;
    }

    g_chat_hwnd = CreateWindowExW(
        0, L"KarameloSocialChatClass", L"Karamelo Social — Amigos & Bate-Papo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 540, NULL, NULL,
        hInst, NULL);

    if (g_chat_hwnd) {
      BOOL dark = TRUE;
      DwmSetWindowAttribute(g_chat_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/,
                            &dark, sizeof(dark));
    }
  }

  if (g_chat_hwnd) {
    ShowWindow(g_chat_hwnd, SW_SHOW);
    SetForegroundWindow(g_chat_hwnd);
    InvalidateRect(g_chat_hwnd, NULL, FALSE);
  }
#else
  DesktopChatShowWindowSdl();
#endif
}

void DesktopChatHideWindow() {
#ifdef _WIN32
  if (g_chat_hwnd)
    ShowWindow(g_chat_hwnd, SW_HIDE);
#else
  DesktopChatHideWindowSdl();
#endif
}

void DesktopChatToggleWindow() {
  if (DesktopChatIsWindowVisible())
    DesktopChatHideWindow();
  else
    DesktopChatShowWindow();
}

bool DesktopChatIsWindowVisible() {
#ifdef _WIN32
  return g_chat_hwnd && IsWindowVisible(g_chat_hwnd);
#else
  return DesktopChatIsWindowVisibleSdl();
#endif
}

// =============================================================
// In-Game Overlay (F10)
// =============================================================

bool DesktopChatIsOverlayVisible() { return g_overlay_visible.load(); }

void DesktopChatSetOverlayVisible(bool visible) {
  g_overlay_visible.store(visible);
}

void DesktopChatToggleOverlay() {
  bool next = !g_overlay_visible.load();
  g_overlay_visible.store(next);
}

void DesktopChatRenderOverlayWin32(void *hdc_handle, int win_w, int win_h) {
#ifdef _WIN32
  if (!hdc_handle || win_w < 320 || win_h < 200)
    return;

  bool local_talk = VoiceChatIsLocalSpeaking();
  bool peer_talk = VoiceChatIsPeerSpeaking();
  bool show_panel = g_overlay_visible.load();

  if (!local_talk && !peer_talk && !show_panel)
    return;

  EnsureChatFonts();
  HDC target_hdc = (HDC)hdc_handle;

  int ow = std::min(win_w - 32, 380);
  int oh = show_panel ? std::min(win_h - 32, 240) : 38;
  int ox = 16;
  int oy = 16;

  HDC mem_dc = CreateCompatibleDC(target_hdc);
  BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = ow;
  bi.bmiHeader.biHeight = -oh;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP dib = CreateDIBSection(mem_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ old_bm = SelectObject(mem_dc, dib);

  FillSolidRectGdi(mem_dc, 0, 0, ow, oh, RGB(18, 24, 34));
  DrawRoundBoxGdi(mem_dc, 0, 0, ow, oh, 10, RGB(20, 27, 38), RGB(70, 130, 180));

  LiveChatRoom active_room = DesktopChatGetRoom();
  char title_buf[96];
  snprintf(title_buf, sizeof(title_buf), "KARAMELO LIVE — %s",
           DesktopChatGetRoomName(active_room));
  RECT rc_title = {12, 8, ow - 12, 28};
  DrawUtf8TextGdi(mem_dc, title_buf, rc_title, g_font_bold, RGB(102, 192, 244),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  if (local_talk) {
    DrawRoundBoxGdi(mem_dc, ow - 78, 9, 30, 18, 4, RGB(34, 120, 60), RGB(87, 242, 135));
    RECT rc_voc = {ow - 78, 9, ow - 48, 27};
    DrawUtf8TextGdi(mem_dc, "VOCE", rc_voc, g_font_small, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  if (peer_talk) {
    DrawRoundBoxGdi(mem_dc, ow - 42, 9, 30, 18, 4, RGB(34, 90, 140), RGB(102, 192, 244));
    RECT rc_peer = {ow - 42, 9, ow - 12, 27};
    DrawUtf8TextGdi(mem_dc, "PEER", rc_peer, g_font_small, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  if (show_panel) {
    FillSolidRectGdi(mem_dc, 8, 32, ow - 16, 1, RGB(42, 54, 72));
    std::vector<LiveChatMessage> msgs = DesktopChatGetRoomMessages();
    int ty = 38;
    size_t start = msgs.size() > 4 ? msgs.size() - 4 : 0;
    for (size_t i = start; i < msgs.size() && ty < oh - 26; ++i) {
      char mline[256];
      snprintf(mline, sizeof(mline), "%s: %s", msgs[i].sender.c_str(), msgs[i].text.c_str());
      RECT rc_m = {12, ty, ow - 12, ty + 24};
      DrawUtf8TextGdi(mem_dc, mline, rc_m, g_font_small,
                      !msgs[i].game_challenge.empty() ? RGB(144, 210, 80) : RGB(220, 228, 238),
                      DT_LEFT | DT_WORDBREAK);
      ty += 32;
    }

    RECT rc_foot = {12, oh - 22, ow - 12, oh - 4};
    DrawUtf8TextGdi(mem_dc, Tr("F10: Fechar Overlay  •  F11: Janela Completa",
                               "F10: Close Overlay  •  F11: Full Window"),
                    rc_foot, g_font_small, RGB(130, 145, 165), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }

  BitBlt(target_hdc, ox, oy, ow, oh, mem_dc, 0, 0, SRCCOPY);

  SelectObject(mem_dc, old_bm);
  DeleteObject(dib);
  DeleteDC(mem_dc);
#else
  (void)hdc_handle;
  (void)win_w;
  (void)win_h;
#endif
}

static void DrawOverlayRectBlend(uint32_t *fb, int fb_w, int fb_h, int rx,
                                 int ry, int rw, int rh, uint32_t rgb,
                                 uint32_t border_rgb) {
  int x0 = std::max(0, rx);
  int y0 = std::max(0, ry);
  int x1 = std::min(fb_w, rx + rw);
  int y1 = std::min(fb_h, ry + rh);

  uint32_t br = (rgb >> 16) & 0xFF;
  uint32_t bg = (rgb >> 8) & 0xFF;
  uint32_t bb = rgb & 0xFF;

  for (int y = y0; y < y1; ++y) {
    uint32_t *row = fb + (size_t)y * fb_w;
    bool is_border_y = (y == ry || y == ry + rh - 1);
    for (int x = x0; x < x1; ++x) {
      if (is_border_y || x == rx || x == rx + rw - 1) {
        row[x] = border_rgb;
      } else {
        uint32_t dst = row[x];
        uint32_t r = (((dst >> 16) & 0xFF) + br * 3) >> 2;
        uint32_t g = (((dst >> 8) & 0xFF) + bg * 3) >> 2;
        uint32_t b = ((dst & 0xFF) + bb * 3) >> 2;
        row[x] = (r << 16) | (g << 8) | b;
      }
    }
  }
}

static void DrawOverlayText8x8(uint32_t *fb, int fb_w, int fb_h, int px, int py,
                               const char *text, uint32_t color) {
  if (!text)
    return;
  int cur_x = px;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    if (cur_x + 8 > fb_w)
      break;
    const unsigned char *glyph = charfont[*p];
    for (int col = 0; col < 8; ++col) {
      unsigned char col_bits = glyph[col];
      int gx = cur_x + col;
      if (gx < 0 || gx >= fb_w)
        continue;
      for (int row = 0; row < 8; ++row) {
        if ((col_bits >> row) & 1u) {
          int gy = py + row;
          if (gy >= 0 && gy < fb_h)
            fb[(size_t)gy * fb_w + gx] = color;
        }
      }
    }
    cur_x += 8;
  }
}

void DesktopChatRenderOverlay(uint32_t *fb, int fb_w, int fb_h) {
  if (!fb || fb_w < 320 || fb_h < 200)
    return;

  bool local_talk = VoiceChatIsLocalSpeaking();
  bool peer_talk = VoiceChatIsPeerSpeaking();
  bool show_panel = g_overlay_visible.load();

  if (!local_talk && !peer_talk && !show_panel)
    return;

  int pw = std::min(fb_w - 20, 260);
  int ph = show_panel ? std::min(fb_h - 24, 96) : 20;
  DrawOverlayRectBlend(fb, fb_w, fb_h, 8, 8, pw, ph, 0x00141821u, 0x0066C0F4u);

  char title[64];
  snprintf(title, sizeof(title), "KARAMELO %s",
           DesktopChatGetRoomName(DesktopChatGetRoom()));
  DrawOverlayText8x8(fb, fb_w, fb_h, 14, 14, title, 0x0066C0F4u);
}

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
#endif

#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <time.h>

// =============================================================
// Ephemeral In-Memory Room State (100% RAM, Zero Disk Storage)
// =============================================================

static std::mutex g_chat_mutex;
static std::atomic<int> g_current_room{LIVE_ROOM_ARCADE_FIGHT};
static std::atomic<bool> g_overlay_visible{false};
static std::string g_nickname = "Player1";
static std::string g_input_text;
static std::vector<LiveChatMessage> g_room_messages[LIVE_ROOM_COUNT];
static const size_t kMaxRoomMessages = 50;

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
  HIT_ROOM_0 = 10,
  HIT_ROOM_1 = 11,
  HIT_ROOM_2 = 12,
  HIT_ROOM_3 = 13,
  HIT_BTN_CHALLENGE = 20,
  HIT_BTN_DENOISE = 21,
  HIT_BTN_CLEAR = 22,
  HIT_BTN_SEND = 23,
  HIT_BTN_ACCEPT_CARD = 24,
  HIT_PEER_CHALLENGE_BASE = 100
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
          Tr(". Historico 100% volatil em RAM — desafie amigos com 1 clique!",
             ". 100% volatile RAM chat — challenge friends with 1 click!");
      welcome.timestamp_utc = (int64_t)time(NULL);
      g_room_messages[r].push_back(welcome);
    }
  }
}

void DesktopChatInit() {
  std::lock_guard<std::mutex> lk(g_chat_mutex);
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
  std::lock_guard<std::mutex> lk(g_chat_mutex);
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
  std::lock_guard<std::mutex> lk(g_chat_mutex);
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
    std::lock_guard<std::mutex> lk(g_chat_mutex);
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
    std::lock_guard<std::mutex> lk(g_chat_mutex);
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

  LiveChatMessage msg;
  {
    std::lock_guard<std::mutex> lk(g_chat_mutex);
    msg.sender = g_nickname;
    msg.game_challenge = title;
    msg.host_ip = (local_ip && local_ip[0]) ? local_ip : "127.0.0.1";
    msg.host_port = 55435;
    msg.text = std::string(Tr("Convite de Partida Netplay: ",
                              "Netplay Match Invite: ")) +
               title + " (" + msg.host_ip + ":55435)";
    msg.timestamp_utc = (int64_t)time(NULL);
    AppendRoomMessageLocked(g_current_room.load(), msg);
  }

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
    std::lock_guard<std::mutex> lk(g_chat_mutex);
    const auto &vec = g_room_messages[g_current_room.load()];
    for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
      if (!it->game_challenge.empty() && !it->host_ip.empty()) {
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
  std::lock_guard<std::mutex> lk(g_chat_mutex);
  return g_room_messages[g_current_room.load()];
}

void DesktopChatClearRoomMessages() {
  {
    std::lock_guard<std::mutex> lk(g_chat_mutex);
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
  out_msg.host_ip = DesktopChatSanitizeText(get_str(root, "host_ip", ""), 45);
  out_msg.host_port = (int)get_int(root, "host_port", 55435);
  out_msg.timestamp_utc = get_int(root, "timestamp_utc", 0);

  if (out_msg.sender.empty() || out_msg.text.empty())
    return false;

  return true;
}

// =============================================================
// Steam / Battle.net / Epic Style Custom-Drawn Dark Social Window
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
  out_init[0] = 'K';
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

  const int side_w = 248;
  const int top_h = 62;
  const int input_h = 60;

  // Palette (Steam Friends / Battle.net Dark Theme)
  const COLORREF col_bg_main = RGB(23, 26, 33);    // #171A21
  const COLORREF col_bg_side = RGB(30, 35, 45);    // #1E232D
  const COLORREF col_bg_profile = RGB(20, 23, 30); // #14171E
  const COLORREF col_bg_header = RGB(27, 32, 43);  // #1B202B
  const COLORREF col_divider = RGB(42, 49, 64);    // #2A3140
  const COLORREF col_accent_blue =
      RGB(102, 192, 244); // #66C0F4 (Steam/Battle.net Blue)
  const COLORREF col_active_row = RGB(42, 71, 94);    // #2A475E
  const COLORREF col_hover_row = RGB(36, 43, 56);     // #242B38
  const COLORREF col_online_grn = RGB(87, 242, 135);  // #57F287
  const COLORREF col_playing_grn = RGB(144, 186, 60); // #90BA3C
  const COLORREF col_busy_red = RGB(237, 66, 69);     // #ED4245
  const COLORREF col_txt_white = RGB(235, 240, 245);
  const COLORREF col_txt_muted = RGB(140, 152, 168);
  const COLORREF col_amber = RGB(245, 194, 107);

  // 1. Base surfaces
  FillSolidRectGdi(hdc, 0, 0, side_w, H, col_bg_side);
  FillSolidRectGdi(hdc, 0, 0, side_w, top_h, col_bg_profile);
  FillSolidRectGdi(hdc, side_w, 0, W - side_w, H, col_bg_main);
  FillSolidRectGdi(hdc, side_w, 0, W - side_w, top_h, col_bg_header);
  FillSolidRectGdi(hdc, side_w, H - input_h, W - side_w, input_h,
                   col_bg_header);

  // Dividers
  FillSolidRectGdi(hdc, side_w, 0, 1, H, col_divider);
  FillSolidRectGdi(hdc, 0, top_h, W, 1, col_divider);
  FillSolidRectGdi(hdc, side_w, H - input_h, W - side_w, 1, col_divider);

  // 2. Top-Left User Profile Card
  std::string my_nick = DesktopChatGetNickname();
  TrayPresenceStatus my_st = TrayManagerGetStatus();
  COLORREF st_col = (my_st == TRAY_STATUS_PLAYING) ? col_playing_grn
                    : (my_st == TRAY_STATUS_BUSY)  ? col_busy_red
                                                   : col_online_grn;

  DrawRoundBoxGdi(hdc, 14, 11, 40, 40, 8, RGB(42, 71, 94), st_col);
  char my_init[3];
  GetInitials(my_nick, my_init);
  RECT rc_av = {14, 11, 54, 51};
  DrawUtf8TextGdi(hdc, my_init, rc_av, g_font_bold, col_accent_blue,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // Status dot on avatar corner
  DrawRoundBoxGdi(hdc, 44, 41, 12, 12, 12, st_col, col_bg_profile);

  RECT rc_nick = {64, 12, side_w - 52, 32};
  DrawUtf8TextGdi(hdc, my_nick.c_str(), rc_nick, g_font_bold, col_txt_white,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  const char *st_label =
      (my_st == TRAY_STATUS_PLAYING) ? Tr("Em Jogo", "In-Game")
      : (my_st == TRAY_STATUS_BUSY)
          ? Tr("Ocupado (Sem Alertas)", "Busy (Do Not Disturb)")
          : Tr("Online • Disponivel", "Online • Available");
  RECT rc_status = {64, 31, side_w - 52, 50};
  DrawUtf8TextGdi(hdc, st_label, rc_status, g_font_small, st_col,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  g_hit_rects.push_back({HIT_PROFILE_STATUS, {14, 11, side_w - 52, 51}});

  // Mic Mute toggle button in profile header
  bool muted = VoiceChatIsMicMuted();
  RECT rc_mic = {side_w - 46, 15, side_w - 12, 47};
  COLORREF mic_bg =
      (g_hover_zone == HIT_MIC_MUTE) ? RGB(52, 62, 80) : RGB(32, 38, 50);
  COLORREF mic_border = muted ? col_busy_red : col_online_grn;
  DrawRoundBoxGdi(hdc, rc_mic.left, rc_mic.top, 34, 32, 6, mic_bg, mic_border);
  DrawUtf8TextGdi(hdc, muted ? "MUTE" : "MIC", rc_mic, g_font_small,
                  muted ? col_busy_red : col_online_grn,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_MIC_MUTE, rc_mic});

  // 3. Left Sidebar: CANAIS DE BATE-PAPO
  RECT rc_sec1 = {16, 74, side_w - 12, 92};
  DrawUtf8TextGdi(hdc, Tr("CANAIS & LOBBIES", "CHANNELS & LOBBIES"), rc_sec1,
                  g_font_small, col_txt_muted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  LiveChatRoom active_room = DesktopChatGetRoom();
  int ry = 96;
  for (int r = 0; r < LIVE_ROOM_COUNT; ++r) {
    int hit_id = HIT_ROOM_0 + r;
    RECT rc_row = {10, ry, side_w - 10, ry + 34};
    bool is_sel = ((int)active_room == r);
    bool is_hov = (g_hover_zone == hit_id);

    if (is_sel) {
      DrawRoundBoxGdi(hdc, rc_row.left, rc_row.top, rc_row.right - rc_row.left,
                      34, 6, col_active_row, col_active_row);
      FillSolidRectGdi(hdc, rc_row.left, rc_row.top + 6, 3, 22,
                       col_accent_blue);
    } else if (is_hov) {
      DrawRoundBoxGdi(hdc, rc_row.left, rc_row.top, rc_row.right - rc_row.left,
                      34, 6, col_hover_row, col_hover_row);
    }

    RECT rc_txt = {rc_row.left + 14, rc_row.top, rc_row.right - 10,
                   rc_row.bottom};
    DrawUtf8TextGdi(hdc, DesktopChatGetRoomName((LiveChatRoom)r), rc_txt,
                    is_sel ? g_font_bold : g_font_body,
                    is_sel ? col_txt_white : col_txt_muted,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    g_hit_rects.push_back({hit_id, rc_row});
    ry += 38;
  }

  // 4. Left Sidebar: JOGADORES NO LOBBY (Steam Friends style list)
  ry += 12;
  FillSolidRectGdi(hdc, 14, ry - 6, side_w - 28, 1, col_divider);
  RECT rc_sec2 = {16, ry, side_w - 12, ry + 18};
  DrawUtf8TextGdi(hdc, Tr("JOGADORES ATIVOS", "ACTIVE PLAYERS"), rc_sec2,
                  g_font_small, col_txt_muted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  ry += 24;

  struct LobbyPeerRow {
    const char *name;
    const char *game;
    bool in_game;
  };
  const char *my_game = CoreIsRunning() ? CoreGetGameName() : "";
  LobbyPeerRow peers[] = {{my_nick.c_str(),
                           (my_game && my_game[0])
                               ? my_game
                               : Tr("No Lobby Karamelo", "In Karamelo Lobby"),
                           CoreIsRunning()},
                          {"Bot_Ryu99", "Street Fighter Alpha 2", true},
                          {"SNES_Master", "Super Mario Kart (Netplay)", true},
                          {"NeoGeo_BR", "The King of Fighters '98", false}};

  for (int p = 0; p < 4 && ry + 42 < H - 10; ++p) {
    RECT rc_p = {10, ry, side_w - 10, ry + 40};
    DrawRoundBoxGdi(hdc, 16, ry + 5, 30, 30, 6, RGB(36, 46, 62),
                    peers[p].in_game ? col_playing_grn : col_online_grn);
    char p_init[3];
    GetInitials(peers[p].name, p_init);
    RECT rc_pi = {16, ry + 5, 46, ry + 35};
    DrawUtf8TextGdi(hdc, p_init, rc_pi, g_font_small, col_txt_white,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT rc_pn = {54, ry + 4, side_w - 14, ry + 21};
    DrawUtf8TextGdi(hdc, peers[p].name, rc_pn, g_font_bold,
                    p == 0 ? col_amber : col_txt_white,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT rc_pg = {54, ry + 21, side_w - 14, ry + 37};
    DrawUtf8TextGdi(hdc, peers[p].game, rc_pg, g_font_small,
                    peers[p].in_game ? col_playing_grn : col_online_grn,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ry += 44;
  }

  // 5. Right Chat Header
  RECT rc_ch_title = {side_w + 18, 10, W - 340, 34};
  DrawUtf8TextGdi(hdc, DesktopChatGetRoomName(active_room), rc_ch_title,
                  g_font_title, col_txt_white,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  char sub_hdr[160];
  snprintf(sub_hdr, sizeof(sub_hdr),
           "Voz P2P: %s  •  Gate: %s  •  100%% RAM Volatil",
           VoiceChatGetDenoiseEngineName(VoiceChatGetDenoiseEngine()),
           VoiceChatGetVadSensitivityName(VoiceChatGetVadSensitivity()));
  RECT rc_ch_sub = {side_w + 18, 34, W - 340, 54};
  DrawUtf8TextGdi(hdc, sub_hdr, rc_ch_sub, g_font_small, col_txt_muted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  // Header Action Buttons (Right side of top bar)
  int bx = W - 14;
  // [Limpar]
  RECT rc_btn_clr = {bx - 68, 16, bx, 46};
  DrawRoundBoxGdi(hdc, rc_btn_clr.left, rc_btn_clr.top, 68, 30, 6,
                  g_hover_zone == HIT_BTN_CLEAR ? RGB(52, 60, 76)
                                                : RGB(36, 42, 54),
                  col_divider);
  DrawUtf8TextGdi(hdc, Tr("Limpar", "Clear"), rc_btn_clr, g_font_small,
                  col_txt_muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_CLEAR, rc_btn_clr});
  bx -= 76;

  // [Filtro IA]
  RECT rc_btn_dsp = {bx - 116, 16, bx, 46};
  DrawRoundBoxGdi(hdc, rc_btn_dsp.left, rc_btn_dsp.top, 116, 30, 6,
                  g_hover_zone == HIT_BTN_DENOISE ? RGB(52, 60, 76)
                                                  : RGB(36, 42, 54),
                  col_divider);
  char dsp_lbl[48];
  snprintf(dsp_lbl, sizeof(dsp_lbl), "IA: %s",
           VoiceChatGetDenoiseEngine() == VOICE_DENOISE_DEEPFILTERNET
               ? "DeepFilter"
           : VoiceChatGetDenoiseEngine() == VOICE_DENOISE_RNNOISE ? "RNNoise"
                                                                  : "Cru/Off");
  DrawUtf8TextGdi(hdc, dsp_lbl, rc_btn_dsp, g_font_small, col_accent_blue,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_DENOISE, rc_btn_dsp});
  bx -= 124;

  // [Desafiar Netplay]
  RECT rc_btn_chal = {bx - 142, 16, bx, 46};
  DrawRoundBoxGdi(hdc, rc_btn_chal.left, rc_btn_chal.top, 142, 30, 6,
                  g_hover_zone == HIT_BTN_CHALLENGE ? RGB(52, 92, 122)
                                                    : col_active_row,
                  col_accent_blue);
  DrawUtf8TextGdi(hdc, Tr("Desafiar Netplay", "Challenge Netplay"), rc_btn_chal,
                  g_font_bold, col_txt_white,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_CHALLENGE, rc_btn_chal});

  // 6. Scrollable Chat Message Feed
  int feed_top = top_h + 1;
  int feed_bot = H - input_h - 1;
  int feed_w = W - side_w;

  HRGN clip_rgn = CreateRectRgn(side_w + 1, feed_top, W, feed_bot);
  SelectClipRgn(hdc, clip_rgn);

  std::vector<LiveChatMessage> msgs = DesktopChatGetRoomMessages();
  int cur_y = feed_top + 14 - g_scroll_offset;

  for (size_t i = 0; i < msgs.size(); ++i) {
    const auto &m = msgs[i];
    bool is_sys =
        (m.sender == "Karamelo Social" || m.sender == "Karamelo Live");
    bool is_me = (m.sender == my_nick);
    bool is_invite = !m.game_challenge.empty();

    // Avatar badge
    int ax = side_w + 16;
    int ay = cur_y + 2;
    COLORREF av_bg =
        is_sys ? RGB(38, 52, 46) : (is_me ? RGB(58, 46, 28) : RGB(36, 52, 72));
    COLORREF av_bd =
        is_sys ? col_online_grn : (is_me ? col_amber : col_accent_blue);
    DrawRoundBoxGdi(hdc, ax, ay, 34, 34, 8, av_bg, av_bd);

    char s_init[3];
    GetInitials(m.sender, s_init);
    RECT rc_si = {ax, ay, ax + 34, ay + 34};
    DrawUtf8TextGdi(hdc, s_init, rc_si, g_font_bold, av_bd,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Sender Name + Timestamp
    int tx = side_w + 60;
    RECT rc_sender = {tx, cur_y, tx + 220, cur_y + 18};
    DrawUtf8TextGdi(hdc, m.sender.c_str(), rc_sender, g_font_bold, av_bd,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    char t_str[16];
    FormatTimeHHMM(m.timestamp_utc, t_str);
    RECT rc_time = {tx + (int)m.sender.size() * 8 + 12, cur_y + 1, W - 20,
                    cur_y + 18};
    DrawUtf8TextGdi(hdc, t_str, rc_time, g_font_small, col_txt_muted,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    cur_y += 20;

    if (is_invite) {
      // Steam / Battle.net Rich Game Invite Card
      int card_w = std::min(feed_w - 84, 420);
      int card_h = 68;
      RECT rc_card = {tx, cur_y + 2, tx + card_w, cur_y + 2 + card_h};
      DrawRoundBoxGdi(hdc, rc_card.left, rc_card.top, card_w, card_h, 8,
                      RGB(26, 42, 36), col_online_grn);

      RECT rc_ct1 = {rc_card.left + 12, rc_card.top + 8, rc_card.right - 130,
                     rc_card.top + 24};
      DrawUtf8TextGdi(
          hdc,
          Tr("CONVITE DE PARTIDA — NETPLAY P2P", "MATCH INVITE — NETPLAY P2P"),
          rc_ct1, g_font_small, col_online_grn,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      RECT rc_ct2 = {rc_card.left + 12, rc_card.top + 26, rc_card.right - 130,
                     rc_card.top + 46};
      DrawUtf8TextGdi(hdc, m.game_challenge.c_str(), rc_ct2, g_font_bold,
                      col_txt_white,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      char host_info[96];
      snprintf(host_info, sizeof(host_info), "Host: %s:%d", m.host_ip.c_str(),
               m.host_port);
      RECT rc_ct3 = {rc_card.left + 12, rc_card.top + 46, rc_card.right - 130,
                     rc_card.top + 62};
      DrawUtf8TextGdi(hdc, host_info, rc_ct3, g_font_small, col_txt_muted,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      RECT rc_join = {rc_card.right - 122, rc_card.top + 18, rc_card.right - 12,
                      rc_card.top + 50};
      DrawRoundBoxGdi(hdc, rc_join.left, rc_join.top, 110, 32, 6,
                      g_hover_zone == HIT_BTN_ACCEPT_CARD ? RGB(58, 165, 93)
                                                          : RGB(45, 125, 70),
                      col_online_grn);
      DrawUtf8TextGdi(hdc, Tr("Entrar Agora", "Join Match"), rc_join,
                      g_font_bold, col_txt_white,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      g_hit_rects.push_back({HIT_BTN_ACCEPT_CARD, rc_join});

      cur_y += card_h + 14;
    } else {
      // Measure & draw wrapped message text
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

  // 7. Bottom Input Bar (Steam / Battle.net message composer)
  int inp_x = side_w + 16;
  int inp_y = H - input_h + 12;
  int send_w = 88;
  int inp_w = (W - side_w) - 32 - send_w - 10;

  RECT rc_inp = {inp_x, inp_y, inp_x + inp_w, inp_y + 36};
  DrawRoundBoxGdi(hdc, rc_inp.left, rc_inp.top, inp_w, 36, 8, RGB(16, 19, 26),
                  !g_input_text.empty() ? col_accent_blue : RGB(58, 68, 86));

  RECT rc_inp_txt = {rc_inp.left + 12, rc_inp.top, rc_inp.right - 12,
                     rc_inp.bottom};
  if (g_input_text.empty()) {
    char ph[128];
    snprintf(ph, sizeof(ph),
             Tr("Enviar mensagem em %s... (Digite e aperte Enter)",
                "Message %s... (Type and press Enter)"),
             DesktopChatGetRoomName(active_room));
    DrawUtf8TextGdi(hdc, ph, rc_inp_txt, g_font_body, col_txt_muted,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  } else {
    std::string disp = g_input_text + "|";
    DrawUtf8TextGdi(hdc, disp.c_str(), rc_inp_txt, g_font_body, col_txt_white,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  RECT rc_send = {rc_inp.right + 10, inp_y, rc_inp.right + 10 + send_w,
                  inp_y + 36};
  DrawRoundBoxGdi(hdc, rc_send.left, rc_send.top, send_w, 36, 8,
                  g_hover_zone == HIT_BTN_SEND ? RGB(78, 158, 212)
                                               : col_active_row,
                  col_accent_blue);
  DrawUtf8TextGdi(hdc, Tr("Enviar", "Send"), rc_send, g_font_bold,
                  col_txt_white, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  g_hit_rects.push_back({HIT_BTN_SEND, rc_send});

  BitBlt(target_hdc, 0, 0, W, H, hdc, 0, 0, SRCCOPY);

  SelectObject(hdc, old_bm);
  DeleteObject(hbm);
  DeleteDC(hdc);
}

static int HitTestSocialWindow(int mx, int my) {
  POINT pt = {mx, my};
  for (const auto &hr : g_hit_rects) {
    if (PtInRect(&hr.rc, pt))
      return hr.zone;
  }
  return HIT_NONE;
}

static LRESULT CALLBACK KarameloChatWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam) {
  switch (msg) {
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mmi = (MINMAXINFO *)lparam;
    mmi->ptMinTrackSize.x = 640;
    mmi->ptMinTrackSize.y = 420;
    return 0;
  }

  case WM_ERASEBKGND:
    return 1;

  case WM_SIZE:
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;

  case WM_MOUSEMOVE: {
    int mx = GET_X_LPARAM(lparam);
    int my = GET_Y_LPARAM(lparam);
    int hz = HitTestSocialWindow(mx, my);
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
    if (g_scroll_offset < 0)
      g_scroll_offset = 0;
    if (g_scroll_offset > 1200)
      g_scroll_offset = 1200;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    int mx = GET_X_LPARAM(lparam);
    int my = GET_Y_LPARAM(lparam);
    int hz = HitTestSocialWindow(mx, my);
    if (hz >= HIT_ROOM_0 && hz <= HIT_ROOM_3) {
      DesktopChatSetRoom((LiveChatRoom)(hz - HIT_ROOM_0));
    } else if (hz == HIT_PROFILE_STATUS) {
      TrayPresenceStatus cur = TrayManagerGetStatus();
      TrayPresenceStatus next = (cur == TRAY_STATUS_ONLINE)
                                    ? TRAY_STATUS_BUSY
                                    : TRAY_STATUS_ONLINE;
      TrayManagerSetStatus(next, NULL);
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
        DesktopChatPostMessage(to_send.c_str());
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
        DesktopChatPostMessage(to_send.c_str());
      }
      return 0;
    }
    if (wparam == VK_BACK) {
      while (!g_input_text.empty()) {
        unsigned char c = (unsigned char)g_input_text.back();
        g_input_text.pop_back();
        if ((c & 0xC0) != 0x80)
          break; // UTF-8 lead byte reached
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
            WideCharToMultiByte(CP_UTF8, 0, wstr, -1, u8, sizeof(u8) - 1, NULL,
                                NULL);
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
#endif

void DesktopChatShowWindow() {
#ifdef _WIN32
  if (!g_chat_hwnd) {
    static bool s_class_reg = false;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    if (!s_class_reg) {
      WNDCLASSEXW wc{};
      wc.cbSize = sizeof(wc);
      wc.style = CS_HREDRAW | CS_VREDRAW;
      wc.lpfnWndProc = KarameloChatWndProc;
      wc.hInstance = hInst;
      wc.hCursor = LoadCursor(NULL, IDC_ARROW);
      wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
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
  DesktopChatSetOverlayVisible(true);
#endif
}

void DesktopChatHideWindow() {
#ifdef _WIN32
  if (g_chat_hwnd)
    ShowWindow(g_chat_hwnd, SW_HIDE);
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
  return g_overlay_visible.load();
#endif
}

// =============================================================
// Native-Resolution Steam / Discord Style In-Game Overlay (F10)
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

  int ox = 16;
  int oy = 16;
  int ow = show_panel ? std::min(win_w - 32, 370) : 210;
  int oh = show_panel ? std::min(win_h - 32, 240) : 34;

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = ow;
  bmi.bmiHeader.biHeight = -oh;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  uint32_t *pixels = nullptr;
  HDC mem_dc = CreateCompatibleDC(target_hdc);
  HBITMAP dib = CreateDIBSection(target_hdc, &bmi, DIB_RGB_COLORS,
                                 (void **)&pixels, NULL, 0);
  if (!mem_dc || !dib || !pixels) {
    if (dib)
      DeleteObject(dib);
    if (mem_dc)
      DeleteDC(mem_dc);
    return;
  }

  HGDIOBJ old_bm = SelectObject(mem_dc, dib);

  // Copy underlying game/wallpaper pixels so we can alpha-blend dark glass over
  // them
  BitBlt(mem_dc, 0, 0, ow, oh, target_hdc, ox, oy, SRCCOPY);

  // Blend 84% dark Steam/Battle.net glass (#141821) + 1px cyan/green border
  uint32_t border_rgb = (local_talk || peer_talk) ? 0x0057F287u : 0x0066C0F4u;
  for (int y = 0; y < oh; ++y) {
    uint32_t *row = pixels + (size_t)y * ow;
    bool is_by = (y == 0 || y == oh - 1);
    for (int x = 0; x < ow; ++x) {
      if (is_by || x == 0 || x == ow - 1) {
        row[x] = border_rgb;
      } else if (show_panel && y == 34) {
        row[x] = 0x002A3446u;
      } else {
        uint32_t bg = row[x];
        uint32_t r = (((bg >> 16) & 0xFF) * 40 + 20 * 216) >> 8;
        uint32_t g = (((bg >> 8) & 0xFF) * 40 + 24 * 216) >> 8;
        uint32_t b = ((bg & 0xFF) * 40 + 33 * 216) >> 8;
        row[x] = (r << 16) | (g << 8) | b;
      }
    }
  }

  // Header pill text
  char hdr[128];
  if (show_panel) {
    snprintf(hdr, sizeof(hdr), "KARAMELO SOCIAL  •  %s",
             DesktopChatGetRoomName(DesktopChatGetRoom()));
  } else {
    snprintf(hdr, sizeof(hdr), "VOZ P2P: %s",
             local_talk ? "VOCE FALANDO" : "AMIGO FALANDO");
  }
  RECT rc_hdr = {12, 0, ow - 12, 34};
  DrawUtf8TextGdi(mem_dc, hdr, rc_hdr, g_font_bold, RGB(102, 192, 244),
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  if (show_panel) {
    RECT rc_hint = {12, 0, ow - 10, 34};
    DrawUtf8TextGdi(mem_dc, "[F10]", rc_hint, g_font_small, RGB(140, 152, 168),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    std::vector<LiveChatMessage> msgs = DesktopChatGetRoomMessages();
    int start_idx = (int)msgs.size() > 5 ? (int)msgs.size() - 5 : 0;
    int ty = 42;
    for (int i = start_idx; i < (int)msgs.size() && ty + 28 < oh - 22; ++i) {
      const auto &m = msgs[i];
      std::string line = m.sender + ": " + m.text;
      RECT rc_line = {12, ty, ow - 12, ty + 34};
      COLORREF col =
          !m.game_challenge.empty() ? RGB(87, 242, 135) : RGB(225, 232, 240);
      DrawUtf8TextGdi(mem_dc, line.c_str(), rc_line, g_font_small, col,
                      DT_LEFT | DT_WORDBREAK);
      ty += 32;
    }

    RECT rc_foot = {12, oh - 22, ow - 12, oh - 4};
    DrawUtf8TextGdi(mem_dc,
                    Tr("Shift+F10: Abrir Janela Social Completa",
                       "Shift+F10: Open Full Social Window"),
                    rc_foot, g_font_small, RGB(130, 145, 165),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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

// Software fallback overlay for non-Win32 (Linux/macOS SDL framebuffer)
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

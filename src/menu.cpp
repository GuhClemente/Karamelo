#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <sstream>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include "compat_win32.h"
#endif

#include "app_info.h"
#include "archive_helper.h"
#include "core_runner.h"
#include "gamepad_sdl.h" // pulls in <xinput.h> for the XINPUT_STATE shape
#include "hw_render.h"
#include "hw_render_vulkan.h"
#include "hw_render_d3d11.h"
#include "input_map.h"
#include "menu.h"
#include "system_registry.h"
#include <functional>
#include "karamelo_math.h"
#include "netplay.h"
#include "osd.h"
#include "retroachievements.h"
#include "updater.h"
#include "port_runner.h"
#include "crash_reporter.h"
#include "cloud_sync.h"
#include "web_admin.h"
#include "voice_chat.h"
#include "tray_manager.h"
#include "desktop_chat.h"

namespace fs = std::filesystem;

enum MenuState {
  STATE_MAIN,
  STATE_BROWSE,
  STATE_SETTINGS,
  STATE_VIDEO,
  STATE_AUDIO,
  STATE_CONTROLLER,
  STATE_NETPLAY,
  STATE_ABOUT,
  STATE_UPDATE,
  STATE_CORE_OPTIONS,
  STATE_RESET_CONFIRM,
  STATE_ABOUT_CORES,
  STATE_SAVESTATES,
  STATE_SHADER_LIST,
  STATE_DIAGNOSTICS,
  STATE_FAVORITES,
  STATE_RECENT,
  STATE_SYSTEMS,
  STATE_CLOUD,
  STATE_WEB_ADMIN,
  STATE_LIVE_CHAT,
  STATE_MANAGE_SYSTEMS
};

struct MenuItem {
  std::string label;
  std::string value;
  bool is_folder;
  bool is_action;
  int action_id;
};

static MenuState current_state = STATE_MAIN;
static std::vector<MenuItem> items;
static int selected_idx = 0;
static int scroll_top = 0;

// Marquee-scroll state for the selected row's name, when it is too long to
// fit and setting_name_scroll hasn't disabled it. Reset whenever the tracked
// item index changes - including back to one visited earlier - which is
// exactly "the selection just landed here."
static int s_scroll_item_idx = -1;
// O indice sozinho nao identifica a linha. Entrar numa pasta e cair de novo
// sobre o mesmo indice mantinha s_scroll_item_idx igual, o cronometro nao era
// zerado e o nome ja aparecia rolando no instante em que a tela abria, sem
// respeitar o tempo de Video > Rolagem Nome. Guardar tambem o texto resolve:
// trocar de tela troca o texto.
// ".md" is both a Mega Drive ROM and a Markdown file. Every cartridge carries
// "SEGA" at 0x100 (some dumps pad it to 0x101), so that decides which one the
// file browser is looking at.
static bool IsMegaDriveRom(const std::filesystem::path &path) {
  std::ifstream f(path, std::ios::binary);
  char hdr[0x105] = {};
  if (!f.read(hdr, sizeof(hdr)))
    return false;
  return memcmp(hdr + 0x100, "SEGA", 4) == 0 || memcmp(hdr + 0x101, "SEGA", 4) == 0;
}

static std::string s_scroll_item_label;
static uint32_t s_scroll_since = 0;

// Same idea for the vertical title band on the left edge (OsdSetTitle) -
// a title longer than the 14 characters that fit in the fixed-height card
// used to just cut off mid-word with no indication, same as the row names.
static std::string s_title_scroll_last;
static uint32_t s_title_scroll_since = 0;

// Milliseconds on a monotonic clock, truncated to 32 bits like the old
// GetTickCount() this replaces - every use here is a "how long since X" delta,
// so wraparound is harmless.
static uint32_t TickCountMs()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static int main_menu_saved_idx = 0;
static std::string current_title = "Karamelo";
static std::string current_dir = "roms";
static std::string status_msg = "Karamelo - karamelo-emu.com - @GuhClemente";

static int setting_language = 0; // 0=Portugues, 1=English

const char* Tr(const char* pt, const char* en) {
  return (setting_language == 1) ? en : pt;
}

// Settings variables matching screenshots
static int setting_aspect = 0; // 0=Original, 1=4:3, 2=16:9
// The scanline / display-simulation modes, in menu order. The ids are the
// renderer's own and are left unchanged, so a config saved earlier still
// selects the same picture.
//
// Nothing is dropped: the display simulations (composite TV, LCD grid,
// curved tube, aperture grille, shadow mask) each reproduce a different
// kind of screen and are the point of having a shader list at all. Only
// the order changed - the gentle ones come first so the softest look is
// one press away, and the heavier ones follow.
//
// Beam strengths are deliberately untouched here; they live in
// core_runner.cpp and each mode is tuned to the hardware it imitates.
struct FilterChoice {
  int mode;
  const char *label_pt;
  const char *label_en;
};
static const FilterChoice kFilters[] = {
    {0, "Desligado (Raw)", "Off (Raw)"},
    {8, "Scanlines Leve",  "Scanlines Light"},
    {9, "CRT Suave (TV)",  "Soft CRT (TV)"},
    {4, "PVM Pro 600TVL",  "PVM Pro 600TVL"},
    {1, "Sony Trinitron",  "Sony Trinitron"},
    {2, "Arcade Shadow",   "Arcade Shadow"},
    {5, "Scanlines 50%",   "Scanlines 50%"},
    {6, "NTSC Composto",   "NTSC Composite"},
    {7, "Matriz LCD",      "LCD Matrix"},
    {3, "Tubo Curvado 3D", "Curved CRT 3D"},
};
static const int kFilterCount = (int)(sizeof(kFilters) / sizeof(kFilters[0]));

static int FilterIndex(int mode) {
  for (int i = 0; i < kFilterCount; i++)
    if (kFilters[i].mode == mode)
      return i;
  return 0;
}
static const char *FilterLabel(int mode) {
  int idx = FilterIndex(mode);
  return (setting_language == 1) ? kFilters[idx].label_en : kFilters[idx].label_pt;
}

// Guards against a config naming a mode the renderer does not implement.
static int MigrateFilter(int mode) {
  for (int i = 0; i < kFilterCount; i++)
    if (kFilters[i].mode == mode)
      return mode;
  return 0;
}

static int setting_filter = 0; // renderer filter id; see kFilters

// >= 0 while the Controller page is waiting for the player to press the key or
// button they want bound. Holds the binding being rebound.
static int capture_bind = -1;
static bool capture_armed = false;
static bool wizard_active = false;
static int wizard_step = 0;
static int setting_pad_device = 0; // 0=Gamepad, 1=Keyboard

// The Controller page row of binding `bind`. Looked up by action id (700 +
// bind) rather than counted from a fixed first row: the rows above the
// bindings change with whether a game is running and has a remap profile,
// and the old constant (4) was already one short with no game loaded, so
// every capture left the cursor on the binding above the one just set.
static void SelectByAction(int action_id, int fallback_idx);
static void SelectBindRow(int bind) { SelectByAction(700 + bind, 0); }

// Persists a rebind: karamelo.cfg for the global set, and the remap file
// too when a system profile is the set being edited.
static void MenuSaveSettings();
static void SaveBindings() {
  MenuSaveSettings();
  if (const char *profile = InputBindActiveProfile())
    InputBindSaveSystemProfile(profile);
}

static const int kOsdTimeouts[] = {0, 5, 10, 15, 30, 60};
static const int kOsdTimeoutCount = 6;

// 0 = never marquee-scroll a long name, just leave it clipped as before.
static const int kScrollDelays[] = {0, 2, 3, 5, 8, 10};
static const int kScrollDelayCount = 6;

// Refreshed on every key the menu handles; MenuRun compares against it.
static uint32_t g_last_input_tick = 0;

// melonds_screen_layout, verbatim from the core's own declaration.
static const char *kNdsLayoutVals[] = {
    "Top/Bottom", "Bottom/Top",  "Left/Right", "Right/Left",
    "Top Only",   "Bottom Only", "Hybrid Top", "Hybrid Bottom"};
// Shortened: the OSD value column cuts off around 12 characters.
static const char *kNdsLayoutNames[] = {"Cima/Baixo", "Baixo/Cima", "Esq/Dir",
                                        "Dir/Esq",    "So Cima",    "So Baixo",
                                        "Hibrido C",  "Hibrido B"};
static const int kNdsLayoutCount = 8;

// The core takes any value from 0 to 126. Stepping through 127 entries one
// at a time is unusable, so the menu offers a spread.
static const char *kNdsGapVals[] = {"0",  "8",  "16", "24", "32",
                                    "48", "64", "80", "96", "126"};
static const int kNdsGapCount = 10;

static const char *kNdsHybridVals[] = {"Bottom", "Top", "Duplicate"};
static const char *kNdsHybridNames[] = {"Inferior", "Superior", "Duplicar"};
static const int kNdsHybridCount = 3;

// citra_layout_option, verbatim from the core's own declaration.
static const char *kCitraLayoutVals[] = {"Default Top-Bottom Screen",
                                         "Single Screen Only",
                                         "Large Screen, Small Screen",
                                         "Side by Side"};
static const char *kCitraLayoutNames[] = {"Cima/Baixo", "So Uma Tela",
                                          "Grande/Peq", "Lado a Lado"};
static const int kCitraLayoutCount = 4;

static int setting_wallpaper =
    1; // 0=None, 1=Static Noise, 2=Parallax Stars, 3=Cyber Grid
static bool setting_fullscreen = false;
// -1 sentinel = never saved yet (fresh install, or an old config from before
// this existed) - main_win32.cpp keeps its own built-in default in that case
// instead of restoring a bogus 0x0 window. Only setting_win_w/h are actually
// read as that sentinel (see MenuGetWindowRect): x and y are legitimately
// negative on any display placed left of, or above, the primary one.
static int setting_win_x = -1;
static int setting_win_y = -1;
static int setting_win_w = -1;
static int setting_win_h = -1;
static int setting_theme =
    0; // 0=Red/Burgundy, 1=Blue, 2=Green, 3=Amber, 4=Gray, 5=Dark
static int setting_deadzone = 1; // 0=5%, 1=10%, 2=15%, 3=20%
// Controle (analogico, D-Pad, A/B, gatilhos) e Espaco/Enter/setas emulando o
// mouse. Desligado por padrao: ligado para todo core, cada tiro tambem virava
// clique - e o FM Towns tem o mouse na porta do jogador 2, entao atirar no
// Tatsujin apertava o start do 2P. O mouse de verdade funciona sempre.
static int setting_pad_mouse = 0;
static int setting_latency = 1;  // index into kAudioLatencyMs below; default 128ms
// Audio buffer depth. Below 64ms the audio stream queue cannot stay ahead of
// the mixer on a loaded machine; above 512ms the delay is audible against input.
static const int kAudioLatencyMs[4] = { 64, 128, 256, 512 };

enum VideoDriverChoice {
  VIDEO_DRIVER_AUTO = 0,     // Auto (GPU 3D)
  VIDEO_DRIVER_OPENGL = 1,   // OpenGL (GPU 3D)
  VIDEO_DRIVER_VULKAN = 2,   // Vulkan (GPU 3D)
  VIDEO_DRIVER_D3D11 = 3,    // DirectX 11 (GPU)
  VIDEO_DRIVER_SOFTWARE = 4  // Software (CPU)
};

struct AvailableDriver {
  int id;
  const char* label;
  const char* toast_name;
};

static std::vector<AvailableDriver> GetAvailableDrivers() {
  std::vector<AvailableDriver> list;
  list.push_back({ VIDEO_DRIVER_AUTO, "Auto (GPU 3D)", "Auto (GPU 3D)" });
  if (HwIsAvailable()) {
    list.push_back({ VIDEO_DRIVER_OPENGL, "OpenGL (GPU 3D)", "OpenGL" });
  }
  if (VkHwIsAvailable()) {
    list.push_back({ VIDEO_DRIVER_VULKAN, "Vulkan (GPU 3D)", "Vulkan" });
  }
  if (D3D11HwIsAvailable()) {
    list.push_back({ VIDEO_DRIVER_D3D11, "DirectX 11 (GPU)", "DirectX 11" });
  }
  list.push_back({ VIDEO_DRIVER_SOFTWARE, "Software (CPU)", "Software (CPU)" });
  return list;
}

static int setting_driver = 0; // Default to Auto (GPU 3D)

static const char* GetCurrentDriverLabel() {
  auto list = GetAvailableDrivers();
  for (const auto& d : list) {
    if (d.id == setting_driver) return d.label;
  }
  return list[0].label;
}

static const char* GetCurrentDriverToast() {
  auto list = GetAvailableDrivers();
  for (const auto& d : list) {
    if (d.id == setting_driver) return d.toast_name;
  }
  return list[0].toast_name;
}

static void CycleDriver(int delta) {
  auto list = GetAvailableDrivers();
  if (list.empty()) return;

  int current_pos = 0;
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i].id == setting_driver) {
      current_pos = (int)i;
      break;
    }
  }

  int count = (int)list.size();
  int next_pos = (current_pos + delta + count) % count;
  setting_driver = list[next_pos].id;
}

static int setting_sync = 1;  // 0=Native (Game Rate), 1=Sync to Display
static int setting_vsync = 1; // 0=Disabled, 1=Enabled
// ParaLLEl N64 is the primary robust core with Ari64 Dynarec and RetroAchievements.
// Defaulting to it provides 60fps locked emulation and rock-solid stability.
static int setting_n64_core = 0;
static int setting_osd_timeout = 0; // index into kOsdTimeouts
static int setting_name_scroll = 3; // index into kScrollDelays; default 5s
static int setting_sms_fm = 0; // 0=auto, 1=desligado, 2=ligado
// melonDS screen arrangement. The stored values are exactly the strings the
// core declares; anything else leaves it on its default silently.
static int setting_nds_layout = 0;
static int setting_nds_gap = 0;
static int setting_nds_hybrid = 0;
static int setting_citra_layout = 0;

// Filled in by PopulateMainMenu from the list it actually builds, so the
// About page can never disagree with the menu the way it used to.
static int g_system_count = 0;
// Mega CD loading speed. Genesis Plus GX ships both off by default, which is
// the accurate behaviour and also the slowest.
static int setting_cd_precache = 0; // 0=disabled, 1=enabled
static int setting_cd_latency = 0;  // 0=enabled (real), 1=disabled (fast)

// ApplyPersistedCoreOptions()/ApplyMsxMachineTypeOption() are defined below,
// after the neo_* dip-switch/system-type globals they also push - see the
// comment on ApplyPersistedCoreOptions() itself.
static void ApplyPersistedCoreOptions();
static void ApplyMsxMachineTypeOption(const std::string &core_dll,
                                      const std::string &ext);


static void MenuSaveSettings();

void MenuSetLanguage(int lang) {
  setting_language = (lang == 1) ? 1 : 0;
  MenuSaveSettings();
}

// -------------------------------------------------------------
// Favoritos e Recentes
// -------------------------------------------------------------
static std::vector<std::string> g_favorites;
static std::vector<std::string> g_recent;
static bool g_fav_recent_loaded = false;

// Favorites and recents written before the UTF-8 code page was switched on
// hold paths in the old ANSI code page (cp1252 on a Portuguese Windows). Such
// a line is not valid UTF-8; re-encode it so the path opens again. Valid
// UTF-8 - including every plain ASCII path - is left as it is.
static bool IsValidUtf8(const std::string &s) {
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!len || i + len > s.size()) return false;
    for (size_t k = 1; k < len; k++)
      if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
    i += len;
  }
  return true;
}

static std::string LegacyToUtf8(const std::string &s) {
#ifdef _WIN32
  if (IsValidUtf8(s)) return s;
  // With the UTF-8 manifest GetACP() answers 65001, so the code page the file
  // was written in has to come from where Windows keeps it.
  UINT legacy_cp = 1252;
  char buf[16] = {0};
  DWORD sz = sizeof(buf);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage", "ACP",
                   RRF_RT_REG_SZ, NULL, buf, &sz) == ERROR_SUCCESS && atoi(buf) > 0)
    legacy_cp = (UINT)atoi(buf);
  int wn = MultiByteToWideChar(legacy_cp, 0, s.c_str(), (int)s.size(), NULL, 0);
  if (wn <= 0) return s;
  std::wstring w(wn, L'\0');
  MultiByteToWideChar(legacy_cp, 0, s.c_str(), (int)s.size(), &w[0], wn);
  int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wn, NULL, 0, NULL, NULL);
  if (un <= 0) return s;
  std::string u(un, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wn, &u[0], un, NULL, NULL);
  return u;
#else
  return s;
#endif
}

static void LoadFavoritesAndRecent() {
  if (g_fav_recent_loaded) return;
  g_fav_recent_loaded = true;
  g_favorites.clear();
  g_recent.clear();

  FILE *f = fopen("Config/favorites.txt", "rb");
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
        line[--len] = '\0';
      if (len > 0 && line[0] != '#')
        g_favorites.push_back(LegacyToUtf8(line));
    }
    fclose(f);
  }

  f = fopen("Config/recent.txt", "rb");
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
        line[--len] = '\0';
      if (len > 0 && line[0] != '#')
        g_recent.push_back(LegacyToUtf8(line));
    }
    fclose(f);
  }
}

// Writes next to the target and renames over it once the write succeeded.
// Truncating the real file first meant a crash, a power cut or a full disk in
// the middle of a save left it empty - every setting (or every favorite)
// silently back to default on the next launch.
static bool WriteFileAtomic(const char *path, const std::string &content) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::string tmp = std::string(path) + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(content.data(), 1, content.size(), f) == content.size();
  ok = (fclose(f) == 0) && ok;
  if (ok) {
    fs::rename(tmp, path, ec);
    ok = !ec;
  }
  if (!ok) fs::remove(tmp, ec);
  return ok;
}

static void SaveFavorites() {
  std::string out = "# Karamelo - Favorites\n";
  for (const auto &p : g_favorites) out += p + "\n";
  WriteFileAtomic("Config/favorites.txt", out);
}

static void SaveRecent() {
  std::string out = "# Karamelo - Recent Games\n";
  for (const auto &p : g_recent) out += p + "\n";
  WriteFileAtomic("Config/recent.txt", out);
}

bool MenuIsFavorite(const std::string &filepath) {
  LoadFavoritesAndRecent();
  for (const auto &fav : g_favorites) {
    if (fav == filepath) return true;
  }
  return false;
}

void MenuToggleFavorite(const std::string &filepath) {
  LoadFavoritesAndRecent();
  auto it = std::find(g_favorites.begin(), g_favorites.end(), filepath);
  if (it != g_favorites.end()) {
    g_favorites.erase(it);
    SaveFavorites();
    CoreSetToast(Tr("REMOVIDO DOS FAVORITOS", "REMOVED FROM FAVORITES"), 90);
  } else {
    g_favorites.push_back(filepath);
    SaveFavorites();
    CoreSetToast(Tr("ADICIONADO AOS FAVORITOS", "ADDED TO FAVORITES"), 90);
  }
}

void MenuAddRecent(const std::string &filepath) {
  if (filepath.empty()) return;
  LoadFavoritesAndRecent();
  auto it = std::find(g_recent.begin(), g_recent.end(), filepath);
  if (it != g_recent.end()) {
    g_recent.erase(it);
  }
  g_recent.insert(g_recent.begin(), filepath);
  if (g_recent.size() > 15) {
    g_recent.resize(15);
  }
  SaveRecent();
}

std::vector<std::string> MenuGetFavorites() {
  LoadFavoritesAndRecent();
  return g_favorites;
}

std::vector<std::string> MenuGetRecent() {
  LoadFavoritesAndRecent();
  return g_recent;
}

void PopulateFavoritesMenu();
void PopulateRecentMenu();

static std::string join_ip_input = "127.0.0.1";

static void MenuProcessKeyImpl(MenuKey key);
static void MenuSaveSettings();

// A player's choice on the generic Core Options page (PopulateCoreOptionsSettings/
// CoreOption* in core_runner.cpp), keyed by the core's own option key so it
// survives a restart - unlike CoreSetOption's own g_core_options, which is
// in-memory only and forgotten the moment the process exits. Persisted as
// "coreopt:<key>=<value>" lines (see SettingsSnapshot/MenuLoadSettings) since
// the existing settings format is otherwise strictly int-keyed and these are
// core-declared strings. Flat, not scoped per-core: two cores sharing an
// option name (rare) would share this too, same tradeoff g_core_options
// itself already makes.
static std::map<std::string, std::string> g_persisted_core_options;

const char* MenuGetPersistedCoreOption(const char* key) {
  if (!key) return NULL;
  auto it = g_persisted_core_options.find(key);
  return (it != g_persisted_core_options.end()) ? it->second.c_str() : NULL;
}
void MenuSetPersistedCoreOption(const char* key, const char* value) {
  if (!key || !value) return;
  g_persisted_core_options[key] = value;
  MenuSaveSettings();
}

int MenuGetAspectMode() { return setting_aspect; }
int MenuGetFilterMode() { return setting_filter; }
int MenuGetWallpaperMode() { return setting_wallpaper; }
void MenuSetAspectMode(int aspect) {
  setting_aspect = ClampInt(aspect, 0, 2);
  MenuSaveSettings();
}
void MenuSetFilterMode(int filter) {
  setting_filter = MigrateFilter(ClampInt(filter, 0, 9));
  MenuSaveSettings();
}
void MenuSetVsync(int vsync) {
  setting_vsync = ClampInt(vsync, 0, 1);
  MenuSaveSettings();
}
void MenuSaveSettingsExport() {
  MenuSaveSettings();
}

// Every .raw file in wallpapers/ becomes a selectable "Custom" wallpaper slot,
// on top of the 4 built-in procedural ones (None/Static Noise/Parallax
// Stars/Cyber Grid). Scanned once and sorted so the same index always means
// the same file for both the menu label and main_win32.cpp's pixel loader.
static std::vector<std::string> g_wallpaper_custom_files;
static bool g_wallpaper_scanned = false;

static void ScanCustomWallpapers() {
  if (g_wallpaper_scanned) return;
  g_wallpaper_scanned = true;
  g_wallpaper_custom_files.clear();
  std::error_code ec;

  const char *search_dirs[] = {"Wallpapers", "wallpapers", "app/Wallpapers"};
  std::string target_dir;
  for (const char *dir : search_dirs) {
    if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
      target_dir = dir;
      break;
    }
  }

  if (!target_dir.empty()) {
    for (auto &entry : fs::directory_iterator(target_dir, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      std::string ext = entry.path().extension().string();
      for (auto &c : ext) c = (char)tolower((unsigned char)c);
      if (ext == ".raw") g_wallpaper_custom_files.push_back(entry.path().string());
    }
    std::sort(g_wallpaper_custom_files.begin(), g_wallpaper_custom_files.end());
  }
}

int MenuGetWallpaperCustomCount() {
  ScanCustomWallpapers();
  return (int)g_wallpaper_custom_files.size();
}

const char *MenuGetWallpaperCustomPath(int index) {
  ScanCustomWallpapers();
  if (index < 0 || index >= (int)g_wallpaper_custom_files.size()) return nullptr;
  return g_wallpaper_custom_files[index].c_str();
}

static const char *WallpaperDisplayLabel(int mode) {
  static const char *kBasePt[] = {"Nenhum", "Ruido Estatico", "Estrelas", "Grade Cyber"};
  static const char *kBaseEn[] = {"None", "Static Noise", "Parallax Stars", "Cyber Grid"};
  if (mode >= 0 && mode < 4) return (setting_language == 1) ? kBaseEn[mode] : kBasePt[mode];
  const char *path = MenuGetWallpaperCustomPath(mode - 4);
  if (!path) return Tr("Nenhum", "None");
  static char buf[32];
  std::string stem = fs::path(path).stem().string();
  if (stem == "karamelo_chef") {
    snprintf(buf, sizeof(buf), "Chef");
  } else if (stem == "karamelo_arcade") {
    snprintf(buf, sizeof(buf), "Arcade");
  } else {
    for (char &c : stem) {
      if (c == '_') c = ' ';
    }
    if (!stem.empty() && stem[0] >= 'a' && stem[0] <= 'z') stem[0] = (char)toupper(stem[0]);
    if (stem.size() > 14) stem = stem.substr(0, 14);
    snprintf(buf, sizeof(buf), "%s", stem.c_str());
  }
  return buf;
}

// 3 = last built-in procedural mode's index; the custom slots start at 4.
static int WallpaperMaxMode() { return 3 + MenuGetWallpaperCustomCount(); }
int MenuGetOsdTheme() { return setting_theme; }
bool MenuGetFullscreen() { return setting_fullscreen; }
void MenuSetFullscreen(bool fs) {
  bool changed = (setting_fullscreen != fs);
  setting_fullscreen = fs;
  if (changed)
    MenuSaveSettings();
}

// Called once at startup (before the window is shown) and once at shutdown -
// not on every SDL_EVENT_WINDOW_MOVED/RESIZED, which would mean a disk write
// per pixel dragged. Returns false (and leaves x/y/w/h untouched) if nothing
// has ever been saved, so the caller can fall back to its own built-in default
// instead of restoring a sentinel -1 as a real position or a 0x0 window.
bool MenuGetWindowRect(int *x, int *y, int *w, int *h) {
  // Only w/h decide "never saved". A negative x or y is a perfectly real
  // position - any display placed to the left of, or above, the primary one
  // has negative coordinates - and treating it as the sentinel meant a window
  // on such a monitor was saved correctly and then silently refused on the way
  // back in, landing centered on the primary display every launch instead.
  // Width and height can never legitimately be <= 0, so they carry the
  // sentinel on their own; the two are always written together by
  // MenuSetWindowRect, so there is no config where one is set and the other
  // is not.
  if (setting_win_w <= 0 || setting_win_h <= 0)
    return false;
  *x = setting_win_x;
  *y = setting_win_y;
  *w = setting_win_w;
  *h = setting_win_h;
  return true;
}
void MenuSetWindowRect(int x, int y, int w, int h) {
  bool changed = (setting_win_x != x || setting_win_y != y ||
                   setting_win_w != w || setting_win_h != h);
  setting_win_x = x;
  setting_win_y = y;
  setting_win_w = w;
  setting_win_h = h;
  if (changed)
    MenuSaveSettings();
}
int MenuGetDeadzone() { return (setting_deadzone + 1) * 5; }
int MenuGetPadAsMouse() { return setting_pad_mouse; }
int MenuGetAudioLatencyMs() { return kAudioLatencyMs[ClampInt(setting_latency, 0, 3)]; }
int MenuGetVideoDriver() { return setting_driver; }
void MenuSetVideoDriverForSelftest(int driver) {
  setting_driver = ClampInt(driver, 0, 4);
}
int MenuGetSyncMode() { return setting_sync; }
int MenuGetVsync() { return setting_vsync; }
int MenuGetN64Core() { return setting_n64_core; }
// Arcade routing is automatic and deliberately has no setting. There used to
// be one, offering "FinalBurn Neo", "MAME 2003", "MAME 2010" and "Flycast".
// Picking wrong was not a harmless mistake: handing a romset to a MAME build
// that expects a different one made the core fault hard enough to corrupt the
// process heap (0xC0000374), and Windows kills that instantly - no exception
// raised, so no crash handler, no log entry, the window just vanishes. A
// player has no way to know which of four cores their dump was built for, so
// the setting was a loaded gun pointed at the application.
static const char *GetArcadeCoreDll(const std::string &file_path = "") {
  // Route each board to the core that actually emulates it
  if (!file_path.empty()) {
    std::string stem = fs::path(file_path).stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);

    // 1. Sega NAOMI / Sammy Atomiswave 3D arcade games -> Flycast
    if (stem == "mvsc2" || stem == "cvs2" || stem == "cvs2mf" || stem == "cvs2gd" || stem == "cvs2gd-chd" ||
        stem == "mslug6" || stem == "slasho" || stem == "hokuto" || stem == "fotns" ||
        stem == "ikaruga" || stem == "dolphinblue" || stem == "kofnw" || stem == "kofxi" ||
        stem == "ngbc" || stem == "ggx" || stem == "ggxx" || stem == "ggxxac" ||
        stem == "monkeyba" || stem == "jambo" || stem == "dybb99" || stem == "samba" ||
        stem == "hotd2" || stem == "csmash" || stem == "senko" || stem == "radirgy" ||
        stem == "karous" || stem == "underdef" || stem == "trizeal" || stem == "mamoru" ||
        stem == "illvelo" || stem == "spkrnch" || stem == "virtuafg" || stem == "vf4" ||
        stem == "vf4evo" || stem == "vf4tuned" || stem == "vf4final" || stem == "ctrhunt" ||
        stem == "gwing2" || stem == "zerogun2" || stem == "spawn" || stem == "claychal" ||
        stem == "heavybox" || stem == "deathcml" || stem == "smarine" || stem == "alienfnt" ||
        stem == "pstone" || stem == "pstone2" || stem == "gundmvg" || stem == "gundmfl" ||
        stem == "meltyb" || stem == "meltyba" || stem == "capcsv" || stem == "toukon" ||
        stem == "demolish" || stem == "dirtdvls" || stem == "slashout" || stem == "crzytaxi" ||
        stem == "zombrvn" || stem == "18wheel" || stem == "airline" || stem == "alpilot" ||
        stem == "clubk" || stem == "wldkicks" || stem == "ringout" || stem == "giantgr") {
      return "cores/dreamcast.dll";
    }

    // 2. Midway Y/T-Unit & Williams games not supported in FBNeo -> MAME 2003
    if (stem == "mk" || stem == "mk2" || stem == "mk2r14" || stem == "mk2r20" ||
        stem == "mk2r21" || stem == "mk2r30" || stem == "mk2r31" || stem == "mk2r32" ||
        stem == "mk2r42" || stem == "mk2r91" || stem == "mk3" || stem == "mk3r10" ||
        stem == "mk3r20" || stem == "mk3r21" || stem == "mk3r22" || stem == "umk3" ||
        stem == "umk3r10" || stem == "umk3r11" || stem == "umk3r12" || stem == "nbajam" ||
        stem == "nbajamte" || stem == "nbajamr1" || stem == "nbajamr2" || stem == "nbahangt" ||
        stem == "kinst" || stem == "kinst2" || stem == "openice" || stem == "wwfmania" ||
        stem == "rampage" || stem == "ramprt" || stem == "tmnt" || stem == "tmnt2" ||
        stem == "trog" || stem == "smash_tv" || stem == "smashtv" || stem == "archrivl" ||
        stem == "crusnusa" || stem == "crusnwld" || stem == "crusnu40" || stem == "carnevil" ||
        stem == "mace" || stem == "wargods" || stem == "nbaonfl" || stem == "nflblitz" ||
        stem == "nflblitz99" || stem == "hydro" || stem == "offroad" || stem == "paperboy" ||
        stem == "gauntlet" || stem == "gaunt2" || stem == "marble" || stem == "joust" ||
        stem == "defender" || stem == "sinistar" || stem == "robotron" || stem == "tapper" ||
        stem == "timber" || stem == "rootbeer" || stem == "spyhunt" || stem == "twotigers" ||
        stem == "xenophobe" || stem == "pigskin" || stem == "highimp" || stem == "strkfc" ||
        stem == "blasted" || stem == "bmaster" || stem == "clowns") {
      return "cores/mame2003.dll";
    }

    // 3. Namco System 11/12 3D games -> MAME 2010
    if (stem == "tekken" || stem == "tekken2" || stem == "tekken3" || stem == "tekkenub" ||
        stem == "soulclbr" || stem == "souledge" || stem == "ridge4" || stem == "pointblk" ||
        stem == "pointbl2" || stem == "gunbarl" || stem == "timecris" || stem == "timecrs2" ||
        stem == "cryptkpr" || stem == "outfxies" || stem == "machbrkr" || stem == "sws97" ||
        stem == "aquarush" || stem == "liblrn" || stem == "tenkomor" || stem == "derbyqd" ||
        stem == "pacrev" || stem == "ehrgeiz" || stem == "dunkmnia") {
      return "cores/mame2010.dll";
    }
  }

  return "cores/arcade_fbneo.dll";
}

// CoreBinaryExists is declared in core_runner.h

static const char *GetN64CoreDll() {
  if (setting_n64_core == 0 && CoreBinaryExists("cores/n64_parallel.dll"))
    return "cores/n64_parallel.dll";
  if (setting_n64_core == 1 && CoreBinaryExists("cores/n64_mupen.dll"))
    return "cores/n64_mupen.dll";
  if (setting_n64_core == 2 && CoreBinaryExists("cores/n64_gopher.dll"))
    return "cores/n64_gopher.dll";
  if (CoreBinaryExists("cores/n64_parallel.dll"))
    return "cores/n64_parallel.dll";
  if (CoreBinaryExists("cores/n64_mupen.dll"))
    return "cores/n64_mupen.dll";
  if (CoreBinaryExists("cores/n64.dll"))
    return "cores/n64.dll";
  return "cores/n64_parallel.dll";
}
int MenuGetHwRender() { return (setting_driver != VIDEO_DRIVER_SOFTWARE) ? 1 : 0; }
int MenuGetLanguage() { return setting_language; }
const char *MenuGetStatus() { return status_msg.c_str(); }
const char *MenuGetTitle() { return current_title.c_str(); }
static bool s_menu_quit_requested = false;
bool MenuIsQuitRequested() { return s_menu_quit_requested; }
void MenuRequestQuit() { s_menu_quit_requested = true; }

void PopulateMainMenu();
void PopulateSystemsMenu();
// Picks the core DLL for a ROM, from its extension and from whatever the path
// says about which system it belongs to.
//
// This logic used to exist twice - once in PopulateBrowse for the OSD and once
// in MenuLaunchGamePath for the command line - and the two had already drifted:
// .a78 and .bin were handled only by the menu, .iso was grouped with discs in
// one and with GameCube images in the other, and an ambiguous .zip resolved to
// neogeo.dll from the menu but arcade_fbneo.dll from the command line. Same
// file, different core, depending on how you opened it.
//
// dir_hint is the folder the file was browsed from; when there is none, the
// full path serves, since roms/<System>/game.ext carries the same information.
std::string MenuResolveCoreForPath(const std::string &file_path,
                                  const std::string &dir_hint) {
  std::string ext = fs::path(file_path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  const std::string &hint = dir_hint.empty() ? file_path : dir_hint;
  auto in = [&hint](const char *needle) {
    return hint.find(needle) != std::string::npos;
  };

  // Unambiguous extensions: one system owns them.
  if (ext == ".z64" || ext == ".n64" || ext == ".v64")
    return GetN64CoreDll();
  if (ext == ".nes" || ext == ".fds")
    return "cores/nes.dll";
  if (ext == ".sfc" || ext == ".smc")
    return "cores/snes.dll";
  if (ext == ".md" || ext == ".gen")
    return "cores/genesis.dll";
  if (ext == ".sms" || ext == ".gg" || ext == ".sg")
    return "cores/sms.dll";
  if (ext == ".pce" || ext == ".sgx")
    return "cores/pce.dll";
  if (ext == ".neo")
    return "cores/neogeo.dll";
  if (ext == ".a26")
    return "cores/atari2600.dll";
  if (ext == ".a52")
    return "cores/atari5200.dll";
  if (ext == ".a78")
    return "cores/atari7800.dll";
  if (ext == ".nds")
    return "cores/nds.dll";
  if (ext == ".3ds" || ext == ".cia")
    return "cores/3ds.dll";
  if (ext == ".gdi" || ext == ".cdi")
    return "cores/dreamcast.dll";
  if (ext == ".gcm" || ext == ".rvz" || ext == ".wbfs")
    return "cores/gamecube.dll";
  if (ext == ".gba")
    return "cores/gba.dll";
  if (ext == ".gb" || ext == ".gbc")
    return "cores/gb.dll";
  if (ext == ".ngp" || ext == ".ngc")
    return "cores/ngp.dll";
  if (ext == ".ws" || ext == ".wsc")
    return "cores/wswan.dll";
  if (ext == ".lnx")
    return "cores/lynx.dll";
  if (ext == ".32x")
    return "cores/32x.dll";
  if (ext == ".j64" || ext == ".jag")
    return "cores/jaguar.dll";
  if (ext == ".col")
    return "cores/coleco.dll";
  if (ext == ".adf" || ext == ".hdf" || ext == ".lha")
    return "cores/amiga.dll";
  if (ext == ".d64" || ext == ".t64" || ext == ".prg" || ext == ".crt")
    return "cores/c64.dll";
  if (ext == ".tzx" || ext == ".tap" || ext == ".z80" || ext == ".sna")
    return "cores/spectrum.dll";
  if (ext == ".pcfx")
    return "cores/pcfx.dll";
  if (ext == ".cso")
    return "cores/psp.dll";
  if (ext == ".mx1" || ext == ".mx2")
    return "cores/msx.dll";
  // .hdd is a hard-disk image on both Japanese computers, so the folder
  // decides it. It used to sit in both lists below: the PC-98 one matched
  // first, so an FM Towns .hdd opened in the PC-98 core from the menu and in
  // the FM Towns core when it came out of a .zip.
  if (ext == ".hdd")
    return (in("FMTowns") || in("Towns") || in("Marty")) ? "cores/fmtowns.dll" : "cores/pc98.dll";
  if (ext == ".hdi" || ext == ".nhd" || ext == ".thd" ||
      ext == ".fdi" || ext == ".fdd" || ext == ".d88" || ext == ".88d" ||
      ext == ".2hd" || ext == ".tfd" || ext == ".hdm" || ext == ".xdf" ||
      ext == ".dup" || ext == ".cmd" || ext == ".d98" || ext == ".98d" || ext == ".nfd")
    return "cores/pc98.dll";
  if (ext == ".d77" || ext == ".h0" || ext == ".h1" || ext == ".h2" || ext == ".h3" ||
      ext == ".h4" || ext == ".h5" || ext == ".h6")
    return "cores/fmtowns.dll";

  // Disc images. The extension says nothing about the console, so the folder
  // decides. .iso lives here rather than with the GameCube extensions: it is
  // used by PS1, PS2, PSP, 3DO, Saturn, Dreamcast, Amiga and GameCube alike.
  if (ext == ".chd" || ext == ".cue" || ext == ".iso" || ext == ".m3u" ||
      ext == ".pbp" || ext == ".toc") {
    if (in("Arcade")) {
      std::string f_lower = file_path;
      std::transform(f_lower.begin(), f_lower.end(), f_lower.begin(), ::tolower);
      if (f_lower.find("gdl-") != std::string::npos || f_lower.find("cvs2") != std::string::npos ||
          f_lower.find("mvsc2") != std::string::npos || f_lower.find("naomi") != std::string::npos) {
        return "cores/dreamcast.dll";
      }
      return "cores/arcade_fbneo.dll";
    }
    // Neo Geo CD discs go to the dedicated NeoCD core. Geolith is a
    // cartridge emulator; its CD mode emits nothing but black frames.
    if (in("NeoGeo"))
      return "cores/neocd_alt.dll";
    if (in("Saturn"))
      return "cores/saturn.dll";
    if (in("MegaCD"))
      return "cores/genesis.dll";
    if (in("TurboGrafx") || in("PCE"))
      return "cores/pce.dll";
    if (in("Dreamcast"))
      return "cores/dreamcast.dll";
    if (in("GameCube"))
      return "cores/gamecube.dll";
    if (in("PlayStation2") || in("PS2"))
      return "cores/ps2.dll";
    if (in("PSP"))
      return "cores/psp.dll";
    if (in("3DO"))
      return "cores/3do.dll";
    if (in("Amiga") || in("CD32"))
      return "cores/amiga.dll";
    if (in("PCFX") || in("PC-FX"))
      return "cores/pcfx.dll";
    if (in("PC98") || in("PC-98"))
      return "cores/pc98.dll";
    if (in("FMTowns") || in("Towns") || in("Marty"))
      return "cores/fmtowns.dll";
    return "cores/psx.dll";
  }

  // Ambiguous raw binary / disk formats - folder decides.
  if (ext == ".bin" || ext == ".rom" || ext == ".dsk" || ext == ".cas") {
    if (in("NeoGeo"))
      return "cores/neogeo.dll";
    if (in("Atari5200"))
      return "cores/atari5200.dll";
    if (in("Atari7800"))
      return "cores/atari7800.dll";
    if (in("Atari"))
      return "cores/atari2600.dll";
    if (in("Genesis"))
      return "cores/genesis.dll";
    if (in("32X"))
      return "cores/32x.dll";
    if (in("NES"))
      return "cores/nes.dll";
    if (in("Saturn"))
      return "cores/saturn.dll";
    if (in("PlayStation2") || in("PS2"))
      return "cores/ps2.dll";
    if (in("PlayStation"))
      return "cores/psx.dll";
    if (in("Dreamcast"))
      return "cores/dreamcast.dll";
    if (in("MSX"))
      return "cores/msx.dll";
    if (in("Amiga"))
      return "cores/amiga.dll";
    if (in("3DO"))
      return "cores/3do.dll";
    if (in("Coleco"))
      return "cores/coleco.dll";
    if (in("PC98") || in("PC-98"))
      return "cores/pc98.dll";
    if (in("FMTowns") || in("Towns") || in("Marty"))
      return "cores/fmtowns.dll";
    return "cores/genesis.dll";
  }

  // Archives: the folder is the only clue until the thing is unpacked. The
  // fallback is FBNeo, because a zip that names no system at all is far more
  // likely to be an arcade set than a Neo Geo one.
  if (ext == ".zip" || ext == ".7z" || ext == ".rar") {
    if (in("Arcade"))
      return GetArcadeCoreDll(file_path);
    // Checked before the plain "NeoGeo" match below - "NeoGeoPocket" contains
    // "NeoGeo" as a substring, so the generic check would always win first.
    if (in("NGP") || in("NeoGeoPocket"))
      return "cores/ngp.dll";
    if (in("NeoGeo"))
      return "cores/neogeo.dll";
    if (in("Atari5200"))
      return "cores/atari5200.dll";
    if (in("Atari7800"))
      return "cores/atari7800.dll";
    if (in("Atari"))
      return "cores/atari2600.dll";
    if (in("Jaguar"))
      return "cores/jaguar.dll";
    if (in("Lynx"))
      return "cores/lynx.dll";
    if (in("Coleco"))
      return "cores/coleco.dll";
    if (in("MasterSystem"))
      return "cores/sms.dll";
    if (in("MegaCD"))
      return "cores/genesis.dll";
    if (in("32X"))
      return "cores/32x.dll";
    if (in("Genesis"))
      return "cores/genesis.dll";
    if (in("SNES"))
      return "cores/snes.dll";
    if (in("NES"))
      return "cores/nes.dll";
    if (in("Nintendo64") || in("N64"))
      return GetN64CoreDll();
    if (in("GBA"))
      return "cores/gba.dll";
    if (in("GameBoy") || in("GB"))
      return "cores/gb.dll";
    if (in("NDS"))
      return "cores/nds.dll";
    if (in("3DS"))
      return "cores/3ds.dll";
    if (in("PSP"))
      return "cores/psp.dll";
    if (in("DOS") || in("MSDOS"))
      return "cores/dosbox_pure.dll";
    if (in("MSX"))
      return "cores/msx.dll";
    if (in("Amiga"))
      return "cores/amiga.dll";
    if (in("C64") || in("Commodore"))
      return "cores/c64.dll";
    if (in("Spectrum") || in("ZXSpectrum"))
      return "cores/spectrum.dll";
    if (in("3DO"))
      return "cores/3do.dll";
    if (in("WonderSwan") || in("WSwan"))
      return "cores/wswan.dll";
    if (in("PCFX") || in("PC-FX"))
      return "cores/pcfx.dll";
    if (in("Saturn"))
      return "cores/saturn.dll";
    if (in("TurboGrafx"))
      return "cores/pce.dll";
    if (in("Dreamcast"))
      return "cores/dreamcast.dll";
    if (in("GameCube"))
      return "cores/gamecube.dll";
    if (in("PlayStation2") || in("PS2"))
      return "cores/ps2.dll";
    if (in("PlayStation"))
      return "cores/psx.dll";
    if (in("PC98") || in("PC-98"))
      return "cores/pc98.dll";
    if (in("FMTowns") || in("Towns") || in("Marty"))
      return "cores/fmtowns.dll";
    return GetArcadeCoreDll(file_path);
  }

  return "";
}

void PopulateBrowse(const std::string &dirpath);
static void BrowseGoUp();
void PopulateSettings();
void PopulateVideoSettings();
void PopulateAudioSettings();
void PopulateControllerSettings();
void PopulateNetplay();
void PopulateAbout();
void PopulateRetroAchievements();
void PopulateCoreOptionsSettings();
void PopulateAchievementList();
void PopulateUpdate();
void PopulateResetConfirm();
void PopulateCoresList();
void PopulateSaveStates();
void PopulateShaderList();
void PopulateDiagnostics();
void PopulateManageSystems();
static MenuState shader_list_previous_state = STATE_VIDEO;

static int neo_sys = 0;          // 0: AES, 1: MVS, 2: CDZ
static int neo_bios = 0;         // 0: Original, 1: UniBIOS
static int neo_cd_type = 0;      // 0: CDZ, 1: Top-Load, 2: Front-Load
static int neo_cd_region = 0;    // 0: US, 1: Japan, 2: Europe
static int neo_memcard = 0;      // 0: Plugged, 1: Unplugged
static int neo_dip_settings = 0; // 0: OFF, 1: ON
static int neo_dip_freeplay = 0; // 0: OFF, 1: ON

// Pushes the saved per-core options into the core. Called on load as well as
// on change: without the load-time call a setting restored from the config
// only took effect if the player toggled it again by hand - true of every
// value below (found the hard way for NeoGeo's own dip switches, which
// persisted correctly to karamelo.cfg and displayed correctly in this
// menu on the very next launch, but silently never reached Geolith unless
// re-toggled in that session, since none of them were being re-applied here).
static void ApplyPersistedCoreOptions() {
  // Two different cores can end up running a Master System game and they use
  // different option keys: cores/sms.dll is Gearsystem, cores/genesis.dll is
  // Genesis Plus GX. Setting only the Genesis one meant the FM toggle did
  // nothing at all for .sms files, which load Gearsystem. Each core ignores
  // the key it does not know.
  const char *fm_gpgx[] = {"auto", "disabled", "enabled"};
  // Gearsystem offers only Auto and Disabled - it has no force-on - so the
  // menu's "Ligado" maps to Auto there. Genesis Plus GX has all three.
  const char *fm_gears[] = {"Auto", "Disabled", "Auto"};
  CoreSetOption("genesis_plus_gx_ym2413", fm_gpgx[setting_sms_fm]);
  CoreSetOption("gearsystem_ym2413", fm_gears[setting_sms_fm]);

  CoreSetOption("melonds_screen_layout", kNdsLayoutVals[setting_nds_layout]);
  CoreSetOption("melonds_screen_gap", kNdsGapVals[setting_nds_gap]);
  CoreSetOption("melonds_hybrid_small_screen",
                kNdsHybridVals[setting_nds_hybrid]);

  // melonDS defaults touch mode to "Mouse", which is its RETRO_DEVICE_MOUSE
  // path: relative movement for a captured cursor. What we feed it is
  // RETRO_DEVICE_POINTER, an absolute position, and only "Touch" reads that.
  // On the default the stylus did nothing at all.
  CoreSetOption("melonds_touch_mode", "Touch");

  CoreSetOption("citra_layout_option", kCitraLayoutVals[setting_citra_layout]);

  const char *onoff[] = {"disabled", "enabled"};
  CoreSetOption("genesis_plus_gx_cd_precache", onoff[setting_cd_precache]);
  CoreSetOption("genesis_plus_gx_cd_latency",
                onoff[setting_cd_latency ? 0 : 1]);

  // NeoGeo/Geolith - value tables kept identical to the interactive
  // KEY_LEFT/KEY_RIGHT handlers for these (action_id 501-507) on purpose,
  // since a mismatch here would mean the menu and the actual running core
  // disagree about what e.g. "MVS" maps to.
  const char *sys_vals[] = {"aes", "mvs", "uni"};
  CoreSetOption("geolith_system_type", sys_vals[neo_sys]);
  const char *bios_vals[] = {"aes", "mvs"};
  CoreSetOption("geolith_unibios_hw", bios_vals[neo_bios]);
  const char *cd_vals[] = {"cdz", "cd_top", "cd_front", "cdz_unibios"};
  CoreSetOption("geolith_cd_system_type", cd_vals[neo_cd_type]);
  const char *reg_vals[] = {"us", "jp", "as", "eu"};
  CoreSetOption("geolith_region", reg_vals[neo_cd_region]);
  const char *mc_vals[] = {"on", "off"};
  CoreSetOption("geolith_memcard", mc_vals[neo_memcard]);
  const char *dip_vals[] = {"off", "on"};
  CoreSetOption("geolith_settingmode", dip_vals[neo_dip_settings]);
  const char *fp_vals[] = {"off", "on"};
  CoreSetOption("geolith_freeplay", fp_vals[neo_dip_freeplay]);
}

// Only reached for a loose (non-archived) MSX file - archived content gets
// the equivalent check in core_runner.cpp after extraction, since only then
// is the real inner extension known. See the comment there for why fMSX's
// "MSX2+" default is not trusted.
static void ApplyMsxMachineTypeOption(const std::string &core_dll,
                                      const std::string &ext) {
  if (core_dll.find("msx.dll") == std::string::npos) return;
  CoreSetOption("fmsx_mode", ext == ".mx1" ? "MSX1" : "MSX2");
}

// Which core each system entry needs, so an entry with no DLL behind it can be
// left out of the menu instead of promising a system that cannot load.
//
// The menu had grown to 35 entries while cores/ held 23 files: Game Boy, GBA,
// PSP, MS-DOS, MSX, Amiga, C64, ZX Spectrum, 32X, 3DO, Jaguar, Lynx, Atari
// 5200, ColecoVision, Neo Geo Pocket, WonderSwan and PC-FX all pointed at DLLs
// nobody had put there. Clicking any of them opened a folder and failed.
//
// Driven off the files on disk rather than a hardcoded count, so dropping a new
// core into cores/ makes its system appear on its own.
struct SystemCore {
  int action_id;
  const char *dll;
};
static const SystemCore kSystemCores[] = {
    {101, "cores/atari2600.dll"},
    {102, "cores/genesis.dll"},
    {103, "cores/sms.dll"},
    {104, "cores/genesis.dll"},
    {105, "cores/nes.dll"},
    {106, "cores/neogeo.dll"},
    {107, NULL},
    {108, "cores/psx.dll"},
    {109, "cores/snes.dll"},
    {110, "cores/saturn.dll"},
    {111, "cores/pce.dll"},
    {112, "cores/dreamcast.dll"},
    {113, "cores/gamecube.dll"},
    {114, "cores/nds.dll"},
    {115, "cores/3ds.dll"},
    {116, "cores/ps2.dll"},
    {117, NULL},
    {118, "cores/gba.dll"},
    {119, "cores/gb.dll"},
    {120, "cores/psp.dll"},
    {121, "cores/dosbox_pure.dll"},
    {122, "cores/msx.dll"},
    {123, "cores/amiga.dll"},
    {124, "cores/c64.dll"},
    {125, "cores/spectrum.dll"},
    {126, "cores/32x.dll"},
    {127, "cores/3do.dll"},
    {128, "cores/jaguar.dll"},
    {129, "cores/atari5200.dll"},
    {130, "cores/atari7800.dll"},
    {131, "cores/coleco.dll"},
    {132, "cores/ngp.dll"},
    {133, "cores/wswan.dll"},
    {134, "cores/lynx.dll"},
    {135, "cores/pcfx.dll"},
    {136, "cores/pc98.dll"},
    {137, "cores/fmtowns.dll"}
};

// NULL above means the system has its own core selector; ask that instead.
static bool SystemHasCore(int action_id) {
  static std::unordered_map<int, bool> s_cache;
  auto it = s_cache.find(action_id);
  if (it != s_cache.end()) return it->second;

  if (action_id == 140) {
    s_cache[action_id] = true;
    return true;
  }

  if (action_id == 107) {
    bool has_n64 = CoreBinaryExists("cores/n64_gopher.dll") ||
                   CoreBinaryExists("cores/n64_parallel.dll") ||
                   CoreBinaryExists("cores/n64_mupen.dll") ||
                   CoreBinaryExists("cores/n64.dll");
    s_cache[action_id] = has_n64;
    return has_n64;
  }

  const char *dll = NULL;
  for (const auto &e : kSystemCores)
    if (e.action_id == action_id) {
      dll = e.dll;
      break;
    }

  if (action_id == 117)
    dll = GetArcadeCoreDll();

  if (!dll) {
    s_cache[action_id] = true;
    return true;
  }

  bool exists = CoreBinaryExists(dll);
  s_cache[action_id] = exists;
  return exists;
}

void PopulateMainMenu() {
  items.clear();
  current_title = APP_NAME;

  if (CoreIsRunning()) {
    std::string core_name = CoreGetCoreName();
    current_title = core_name.empty() ? APP_NAME : core_name;

    const char *aspects[] = {"Original", "4:3", "16:9"};

    // 1. Header: Load Game, Systems, and Reset
    items.push_back({Tr("Carregar Jogo", "Load Game"), ">", true, true, 1});
    items.push_back({Tr("Navegar Sistemas", "Browse Systems"), ">", true, true, 195});
    items.push_back({Tr("Reiniciar", "Reset"), "", false, true, 6});
    items.push_back({" ", "", false, false, 0});

    // 2. NeoGeo Authentic MiSTer Options
    if (core_name == "NeoGeo" || core_name.find("Neo") != std::string::npos) {
      // Labels line up 1:1 with the value tables in the handlers below.
      // Geolith has no "CD" system type - a CD is selected by loading a
      // disc image, and its hardware is chosen through CD Type.
      const char *sys_types_pt[] = {"Console(AES)", "Arcade(MVS)", "UniBIOS"};
      const char *sys_types_en[] = {"Console(AES)", "Arcade(MVS)", "UniBIOS"};
      const char *bios_types[] = {"AES", "MVS"};
      const char *cd_types[] = {"CDZ", "Top-Loading", "Front-Loading",
                                "CDZ UniBIOS"};
      const char *regions_pt[] = {"EUA", "Japao", "Asia", "Europa"};
      const char *regions_en[] = {"US", "Japan", "Asia", "Europe"};
      const char *memcards_pt[] = {"Inserido", "Removido"};
      const char *memcards_en[] = {"Plugged", "Unplugged"};
      const char *dip_states[] = {"OFF", "ON"};

      items.push_back({Tr("Tipo de Sistema", "System Type"), (setting_language == 1) ? sys_types_en[neo_sys] : sys_types_pt[neo_sys], false, false, 501});
      items.push_back({Tr("Hardware UniBIOS", "UniBIOS HW"), bios_types[neo_bios], false, false, 502});
      items.push_back({Tr("Tipo de CD", "CD Type"), cd_types[neo_cd_type], false, false, 503});
      items.push_back({Tr("Regiao CD", "CD Region"), (setting_language == 1) ? regions_en[neo_cd_region] : regions_pt[neo_cd_region], false, false, 504});
      items.push_back(
          {Tr("Cartao Memoria", "Memory Card"), (setting_language == 1) ? memcards_en[neo_memcard] : memcards_pt[neo_memcard], false, false, 505});
      items.push_back(
          {Tr("[DIP] Ajustes", "[DIP] Settings"), dip_states[neo_dip_settings], false, false, 506});
      items.push_back(
          {Tr("[DIP] Freeplay", "[DIP] Freeplay"), dip_states[neo_dip_freeplay], false, false, 507});
    }

    // 3. Display & Shaders
    const char *aspects_pt[] = {"Original", "4:3", "16:9"};
    const char *aspects_en[] = {"Original", "4:3", "16:9"};
    items.push_back(
        {Tr("Proporcao", "Aspect Ratio"), (setting_language == 1) ? aspects_en[setting_aspect] : aspects_pt[setting_aspect], false, false, 301});
    items.push_back(
        {Tr("Shader CRT", "CRT Shader"), FilterLabel(setting_filter), false, true, 302});

    // Master System FM: the YM2413 sound chip a handful of Japanese games
    // use. Genesis Plus GX exposes it, but nothing here ever offered it.
    if (core_name.find("Master System") != std::string::npos) {
      const char *fm_pt[] = {"Auto", "Desligado", "Ligado"};
      const char *fm_en[] = {"Auto", "Disabled", "Enabled"};
      items.push_back(
          {Tr("Audio FM SMS", "FM Audio (YM2413)"), (setting_language == 1) ? fm_en[setting_sms_fm] : fm_pt[setting_sms_fm], false, false, 509});
    }

    // The DS has two screens and how they sit is a player preference, not a
    // fixed one. melonDS exposes it; nothing here ever offered it.
    if (core_name.find("Nintendo DS") != std::string::npos) {
      items.push_back({Tr("Layout Telas", "Screen Layout"), kNdsLayoutNames[setting_nds_layout],
                       false, false, 510});
      items.push_back(
          {Tr("Espaco Telas", "Screen Gap"), kNdsGapVals[setting_nds_gap], false, false, 511});
      if (setting_nds_layout >= 6)
        items.push_back({Tr("Tela Pequena", "Small Screen"), kNdsHybridNames[setting_nds_hybrid],
                         false, false, 512});
    }

    // Same deal as the DS: two screens, layout is a preference. Citra
    // exposes it as a hidden keyboard hotkey (C) with no menu entry at all.
    if (core_name.find("Nintendo 3DS") != std::string::npos) {
      items.push_back({Tr("Layout Telas", "Screen Layout"), kCitraLayoutNames[setting_citra_layout],
                       false, false, 515});
    }

    // Mega CD load times are long because the core emulates a 1x drive.
    // Only meaningful for a disc: the same core runs Genesis cartridges.
    if (core_name.find("Genesis") != std::string::npos && CoreIsDiscGame()) {
      const char *od_pt[] = {"Desligado", "Ligado"};
      const char *od_en[] = {"Disabled", "Enabled"};
      const char *acc_pt[] = {"Real", "Rapido"};
      const char *acc_en[] = {"Real", "Fast"};
      items.push_back(
          {Tr("Cache do CD", "CD Precache"), (setting_language == 1) ? od_en[setting_cd_precache] : od_pt[setting_cd_precache], false, false, 513});
      items.push_back(
          {Tr("Acesso do CD", "CD Access"), (setting_language == 1) ? acc_en[setting_cd_latency] : acc_pt[setting_cd_latency], false, false, 514});
    }

    // 3b. Core Options: whatever the core itself declared via SET_VARIABLES
    // (internal resolution, region, DSP, ...), generically - only shown when
    // there is actually something to show, same as every other conditional
    // block in this menu.
    if (CoreOptionCount() > 0) {
      items.push_back({Tr("Opcoes Core", "Core Options"), ">", false, true, 221});
    }

    // 4. RetroAchievements, if the integration is configured. None of this
    //    reached the screen before: the status and counters existed as
    //    accessors that nothing called.
    if (RaIsEnabled()) {
      char ra_val[40];

      if (!RaIsLoggedIn()) {
        snprintf(ra_val, sizeof(ra_val), "%s", Tr("Conectando...", "Connecting..."));
      } else if (RaGetAchievementCount() > 0) {
        snprintf(ra_val, sizeof(ra_val), "%d/%d%s", RaGetAchievementsUnlocked(),
                 RaGetAchievementCount(), RaHasPendingUnlocks() ? " !" : "");
      } else {
        snprintf(ra_val, sizeof(ra_val), "%s", Tr("Sem conquistas", "No achievements"));
      }

      items.push_back({Tr("Conquistas", "Achievements"), ra_val, false, true, 207});
    }

    // 5. Savestates & Controls
    items.push_back({" ", "", false, false, 0});
    int cur_slot = CoreGetSelectedSlot();
    bool slot_has_state = false;
    char slot_time[32];
    snprintf(slot_time, sizeof(slot_time), "%s", Tr("Vazio", "Empty"));
    int64_t slot_sz = 0;
    CoreGetStateSlotInfo(cur_slot, &slot_has_state, slot_time, sizeof(slot_time), &slot_sz);

    char slot_display[40];
    snprintf(slot_display, sizeof(slot_display), "< %d: %s >", cur_slot, slot_time);
    items.push_back({Tr("Slot de Save", "Save Slot"), slot_display, false, false, 5});
    items.push_back({Tr("Salvar Estado (F2)", "Save State (F2)"), "", false, true, 2});
    items.push_back({Tr("Carregar Estado (F4)", "Load State (F4)"), slot_has_state ? "OK" : "-", false, true, 3});
    items.push_back({Tr("Gerenciador Slots", "State Slots"), ">", false, true, 4});
    items.push_back({Tr("Definir Botoes", "Define Buttons"), ">", false, true, 203});
    items.push_back({Tr("Screenshot (F9)", "Screenshot (F9)"), "", false, true, 8});
    items.push_back({" ", "", false, false, 0});
    items.push_back({Tr("Fechar Jogo", "Close Game"), "", false, true, 7});

    OsdSetSize((int)items.size());
  } else {
    PopulateSystemsMenu();
  }

  selected_idx = 0;
  scroll_top = 0;
}

// Thread-safe task queue for operations dispatched from background threads (e.g. Web Studio)
static std::mutex s_menu_task_mutex;
static std::vector<std::function<void()>> s_menu_task_queue;

void MenuEnqueueTask(std::function<void()> task) {
  if (!task) return;
  std::lock_guard<std::mutex> lk(s_menu_task_mutex);
  s_menu_task_queue.push_back(std::move(task));
}

// Sistemas ocultos pelo jogador (Configuracoes > Gerenciar Sistemas, ou pelo
// Web Studio). As funcoes abaixo sao chamadas tambem pela thread do servidor
// web, entao o conjunto fica atras de um mutex e nada aqui grava arquivo nem
// mexe em items[]: so marca s_systems_dirty, e MenuRun() - na thread principal -
// salva o karamelo.cfg e redesenha a tela aberta.
static std::unordered_set<int> s_hidden_systems;
static std::mutex s_hidden_mutex;
static std::atomic<bool> s_systems_dirty{false};

bool MenuIsSystemVisible(int action_id) {
  std::lock_guard<std::mutex> lk(s_hidden_mutex);
  return s_hidden_systems.find(action_id) == s_hidden_systems.end();
}

void MenuSetSystemVisible(int action_id, bool visible) {
  {
    std::lock_guard<std::mutex> lk(s_hidden_mutex);
    if (visible)
      s_hidden_systems.erase(action_id);
    else
      s_hidden_systems.insert(action_id);
  }
  s_systems_dirty = true;
}

void MenuSetAllSystemsVisible(bool visible) {
  {
    std::lock_guard<std::mutex> lk(s_hidden_mutex);
    s_hidden_systems.clear();
    if (!visible)
      for (size_t i = 0; i < kSystemRegistryCount; i++)
        s_hidden_systems.insert(kSystemRegistry[i].action_id);
  }
  s_systems_dirty = true;
}

void MenuHideCategory(const char* category) {
  if (!category) return;
  {
    std::lock_guard<std::mutex> lk(s_hidden_mutex);
    for (size_t i = 0; i < kSystemRegistryCount; i++)
      if (strcmp(kSystemRegistry[i].category, category) == 0)
        s_hidden_systems.insert(kSystemRegistry[i].action_id);
  }
  s_systems_dirty = true;
}

// Oculta sistemas cujo diretorio de roms esteja vazio. O PC Ports (140) fica
// de fora - a lista e o catalogo, instalado pelo proprio menu, e nunca esta
// "vazia" no sentido que importa aqui. Usa contagem canonica de roms de system_registry.h.
void MenuHideEmptySystems() {
  std::vector<int> empty_ids, filled_ids;
  for (size_t i = 0; i < kSystemRegistryCount; i++) {
    int id = kSystemRegistry[i].action_id;
    if (id == 140) continue;

    fs::path dir = ResolveSystemRomDir(kSystemRegistry[i].id);
    int count = CountGamesInDirectory(dir, false);
    if (count > 0) filled_ids.push_back(id);
    else empty_ids.push_back(id);
  }
  {
    std::lock_guard<std::mutex> lk(s_hidden_mutex);
    for (int id : empty_ids) s_hidden_systems.insert(id);
    for (int id : filled_ids) s_hidden_systems.erase(id);
  }
  s_systems_dirty = true;
}

void PopulateSystemsMenu() {
  items.clear();
  current_title = APP_NAME;

  if (CoreIsRunning()) {
    std::string core_name = CoreGetCoreName();
    std::string back_lbl = std::string("< ") + Tr("Voltar ao Core", "Back to Core");
    if (!core_name.empty()) back_lbl += " (" + core_name + ")";
    items.push_back({back_lbl, "", false, true, 196});
    items.push_back({" ", "", false, false, 0});
  }

  // Quick Launch: Favoritos & Recentes
  items.push_back({Tr("Favoritos", "Favorites"), ">", true, true, 190});
  items.push_back({Tr("Recentes", "Recent Games"), ">", true, true, 191});
  items.push_back({" ", "", false, false, 0});

  // Main Systems Menu (Alphabetical)
  if (SystemHasCore(117) && MenuIsSystemVisible(117))
    items.push_back({"Arcade", ">", true, true, 117});
  if (SystemHasCore(101) && MenuIsSystemVisible(101))
    items.push_back({"Atari 2600", ">", true, true, 101});
  if (SystemHasCore(129) && MenuIsSystemVisible(129))
    items.push_back({"Atari 5200", ">", true, true, 129});
  if (SystemHasCore(130) && MenuIsSystemVisible(130))
    items.push_back({"Atari 7800", ">", true, true, 130});
  if (SystemHasCore(128) && MenuIsSystemVisible(128))
    items.push_back({"Atari Jaguar", ">", true, true, 128});
  if (SystemHasCore(134) && MenuIsSystemVisible(134))
    items.push_back({"Atari Lynx", ">", true, true, 134});
  if (SystemHasCore(131) && MenuIsSystemVisible(131))
    items.push_back({"ColecoVision", ">", true, true, 131});
  if (SystemHasCore(124) && MenuIsSystemVisible(124))
    items.push_back({"Commodore 64", ">", true, true, 124});
  if (SystemHasCore(123) && MenuIsSystemVisible(123))
    items.push_back({"Commodore Amiga", ">", true, true, 123});
  if (SystemHasCore(112) && MenuIsSystemVisible(112))
    items.push_back({"Dreamcast", ">", true, true, 112});
  if (SystemHasCore(137) && MenuIsSystemVisible(137))
    items.push_back({"FM Towns / Marty", ">", true, true, 137});
  if (SystemHasCore(119) && MenuIsSystemVisible(119))
    items.push_back({"Game Boy / Color", ">", true, true, 119});
  if (SystemHasCore(118) && MenuIsSystemVisible(118))
    items.push_back({"Game Boy Advance", ">", true, true, 118});
  if (SystemHasCore(113) && MenuIsSystemVisible(113))
    items.push_back({"GameCube", ">", true, true, 113});
  if (SystemHasCore(102) && MenuIsSystemVisible(102))
    items.push_back({"Genesis / Mega Drive", ">", true, true, 102});
  if (SystemHasCore(103) && MenuIsSystemVisible(103))
    items.push_back({"Master System", ">", true, true, 103});
  if (SystemHasCore(104) && MenuIsSystemVisible(104))
    items.push_back({"Mega CD", ">", true, true, 104});
  if (SystemHasCore(121) && MenuIsSystemVisible(121))
    items.push_back({"MS-DOS", ">", true, true, 121});
  if (SystemHasCore(122) && MenuIsSystemVisible(122))
    items.push_back({"MSX / MSX2", ">", true, true, 122});
  if (SystemHasCore(136) && MenuIsSystemVisible(136))
    items.push_back({"NEC PC-98", ">", true, true, 136});
  if (SystemHasCore(105) && MenuIsSystemVisible(105))
    items.push_back({"NES / Famicom", ">", true, true, 105});
  if (SystemHasCore(132) && MenuIsSystemVisible(132))
    items.push_back({"Neo Geo Pocket", ">", true, true, 132});
  if (SystemHasCore(106) && MenuIsSystemVisible(106))
    items.push_back({"NeoGeo / CD", ">", true, true, 106});
  if (SystemHasCore(115) && MenuIsSystemVisible(115))
    items.push_back({"Nintendo 3DS", ">", true, true, 115});
  if (SystemHasCore(107) && MenuIsSystemVisible(107))
    items.push_back({"Nintendo 64", ">", true, true, 107});
  if (SystemHasCore(114) && MenuIsSystemVisible(114))
    items.push_back({"Nintendo DS", ">", true, true, 114});
  if (SystemHasCore(127) && MenuIsSystemVisible(127))
    items.push_back({"Panasonic 3DO", ">", true, true, 127});
  if (SystemHasCore(135) && MenuIsSystemVisible(135))
    items.push_back({"PC-FX", ">", true, true, 135});
  if (SystemHasCore(108) && MenuIsSystemVisible(108))
    items.push_back({"PlayStation 1", ">", true, true, 108});
  if (SystemHasCore(116) && MenuIsSystemVisible(116))
    items.push_back({"PlayStation 2 (PS2)", ">", true, true, 116});
  if (SystemHasCore(120) && MenuIsSystemVisible(120))
    items.push_back({"PlayStation Portable (PSP)", ">", true, true, 120});
  if (SystemHasCore(140) && MenuIsSystemVisible(140))
    items.push_back({"Ports & Recomp", ">", true, true, 140});
  if (SystemHasCore(110) && MenuIsSystemVisible(110))
    items.push_back({"Saturn", ">", true, true, 110});
  if (SystemHasCore(126) && MenuIsSystemVisible(126))
    items.push_back({"Sega 32X", ">", true, true, 126});
  if (SystemHasCore(109) && MenuIsSystemVisible(109))
    items.push_back({"SNES / Super Famicom", ">", true, true, 109});
  if (SystemHasCore(111) && MenuIsSystemVisible(111))
    items.push_back({"TurboGrafx 16 / PCE", ">", true, true, 111});
  if (SystemHasCore(133) && MenuIsSystemVisible(133))
    items.push_back({"WonderSwan", ">", true, true, 133});
  if (SystemHasCore(125) && MenuIsSystemVisible(125))
    items.push_back({"ZX Spectrum", ">", true, true, 125});
  // Blank row before the app entries, the way the MiSTer OSD separates the
  // system list from Settings / Update / Close.
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Configuracoes", "Settings"), ">", false, true, 20});
  items.push_back({Tr("Atualizacao", "Update"), ">", false, true, 30});
  items.push_back({std::string(Tr("Sair do ", "Exit ")) + APP_NAME, "", false, true, 99});

  // Counted here rather than written down somewhere else: this is the list.
  g_system_count = 0;
  for (const auto &it : items)
    if ((it.action_id >= 101 && it.action_id <= 137) || it.action_id == 140)
      g_system_count++;

  if (g_system_count == 0) {
    items.push_back({Tr("(Todos sistemas ocultos)", "(All systems hidden)"), "", false, false, 0});
    items.push_back({Tr("Configurar Gerenciar Sistemas", "Configure Manage Systems"), ">", false, true, 225});
  }

  OsdSetSize((int)items.size());
}

// Entering the page with no pad plugged in should show the keyboard bindings.
// Otherwise the page silently lists gamepad buttons to someone playing on the
// keyboard - which is exactly how a player ends up unable to find the control
// they are looking for. Only on entry, so toggling Dispositivo still works.
static void ControllerPageOnEnter() {
  int pads = 0;
  for (DWORD i = 0; i < 4; i++) {
    XINPUT_STATE st;
    if (GamepadGetState((int)i, &st))
      pads++;
  }
  if (pads == 0)
    setting_pad_device = 1;
}

void PopulateControllerSettings() {
  items.clear();
  current_title = Tr("Controles", "Controller");
  OsdSetSize(16);

  int pads = 0;
  for (DWORD i = 0; i < 4; i++) {
    XINPUT_STATE st;
    if (GamepadGetState((int)i, &st))
      pads++;
  }

  char pad_str[48];
  if (pads == 0) {
    snprintf(pad_str, sizeof(pad_str), "%s", Tr("Nenhum", "None"));
  } else {
    const char *dtype = GamepadGetDeviceTypeName(0);
    snprintf(pad_str, sizeof(pad_str), "%d (%s)", pads, dtype);
  }
  items.push_back({Tr("Gamepads", "Gamepads"), pad_str, false, false, 0});

  const char *dev_names[] = {"Gamepad", Tr("Teclado", "Keyboard")};
  items.push_back(
      {Tr("Dispositivo", "Device"), dev_names[setting_pad_device], false, false, 500});

  // Presets de botoes
  int current_preset_idx = InputBindGetPreset();
  if (current_preset_idx < 0 || current_preset_idx >= 4) current_preset_idx = 0;
  const char *preset_names[] = {
    Tr("MiSTer/Nintendo", "MiSTer/Nintendo"),
    Tr("Xbox Nativo", "Xbox Native"),
    Tr("PlayStation", "PlayStation"),
    Tr("Arcade 6-Botoes", "Arcade 6-Button")
  };
  items.push_back({Tr("Preset Botoes", "Button Preset"), preset_names[current_preset_idx], false, false, 530});

  // Per-System Remap:
  if (CoreIsRunning()) {
    std::string sys_name = CoreGetCoreName();
    bool has_prof = InputBindHasSystemProfile(sys_name.c_str());
    items.push_back({Tr("Perfil Remap", "Remap Profile"), has_prof ? Tr("Personalizado", "Custom") : Tr("Padrao Global", "Default Global"), false, false, 0});
    items.push_back({Tr("Salvar p/ Sistema", "Save for System"), sys_name.length() > 10 ? sys_name.substr(0, 10) + ".." : sys_name, false, true, 796});
    if (has_prof) {
      items.push_back({Tr("Apagar Perfil Sistema", "Delete System Profile"), "", false, true, 797});
    }
  }

  const char *deadzones[] = {"5%", "10%", "15%", "20%"};
  items.push_back({Tr("Deadzone", "Deadzone"), deadzones[setting_deadzone], false, false, 508});
  items.push_back({Tr("Controle como Mouse", "Pad as Mouse"),
                   setting_pad_mouse ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 541});
  items.push_back({Tr("Mapear Tudo (Passo a Passo)", "Map All (Step by Step)"), ">", false, true, 798});

  // Labels come from the running core when it supplied them, so the page reads
  // in that system's own terms - "Cross" on PSP, not a generic "Botao B".
  // Falls back to the generic name with no core loaded or a core that stayed
  // silent. The buffers are static because the item list holds the pointers.
  static char bind_labels[BIND_COUNT][28];

  for (int i = 0; i < BIND_COUNT; i++) {
    const char *val = (capture_bind == i)
                          ? Tr("<pressione>", "<press key>")
                          : (setting_pad_device == 0 ? InputBindPadName(i)
                                                     : InputBindKeyName(i));

    // An unassigned analog row on a gamepad is not a gap to fill: the physical
    // stick already drives that axis. A bare "-" there reads as broken, so say
    // what is actually happening.
    if (capture_bind != i && setting_pad_device == 0 && InputBindIsAnalog(i) &&
        InputBindGetPad(i) <= 0)
      val = "Stick";

    const char *label = InputBindLabel(i);
    // Buttons take the core's name; the sticks do not. A row has 22 columns
    // for label and value together and the label is what gets truncated, so
    // "Right Analog Baixo" arrives clipped to "Right Analog Baix". The core's
    // axis names carry no information our own compact ones lack - both say
    // left or right stick - so the analog rows keep theirs.
    const char *from_core =
        InputBindIsAnalog(i) ? NULL : CoreGetButtonLabel(InputBindRetroId(i));

    if (from_core && from_core[0]) {
      if (InputBindIsAnalog(i)) {
        // An axis label names the whole axis ("Analog X"), so the direction
        // still has to be spelled out or two rows would read identically.
        const int sign = InputBindAnalogSign(i);
        const int axis = InputBindAnalogAxis(i);
        const char *dir = axis == 1 ? (sign > 0 ? Tr("Cima", "Up") : Tr("Baixo", "Down"))
                                    : (sign > 0 ? Tr("Dir", "Right") : Tr("Esq", "Left"));

        // Cores name the axis, not the direction ("Left Analog X"). Appending
        // the direction to that gives "Left Analog X Dir", which is both
        // redundant and too wide for the OSD, so the trailing axis letter goes.
        char base[24];
        snprintf(base, sizeof(base), "%s", from_core);
        size_t bl = strlen(base);
        if (bl > 2 && base[bl - 2] == ' ' && (base[bl - 1] == 'X' || base[bl - 1] == 'Y'))
          base[bl - 2] = '\0';

        snprintf(bind_labels[i], sizeof(bind_labels[i]), "%s %s", base, dir);
      } else {
        snprintf(bind_labels[i], sizeof(bind_labels[i]), "%s", from_core);
      }
      label = bind_labels[i];
    }

    items.push_back({label, val, false, true, 700 + i});
  }

  items.push_back({Tr("Restaurar", "Restore"), Tr("Padrao", "Default"), false, true, 799});

  if (capture_bind < 0 && selected_idx < 1)
    selected_idx = 1;
  scroll_top = 0;
}

// Puts every persisted setting (video/audio/controller/per-system options)
// back to the same value it has at first launch, then saves. Netplay's saved
// IP and RetroAchievements login are deliberately left alone - those are
// account/network info, not display or gameplay settings, and clearing them
// would log the user out or lose their friend's address as a side effect of
// "fix my messed-up video settings."
static void ResetAllSettingsToDefault() {
  MenuSetAllSystemsVisible(true);
  setting_aspect = 0;
  setting_filter = 0;
  setting_pad_device = 0;
  setting_wallpaper = 1;
  setting_fullscreen = false;
  setting_theme = 0;
  setting_deadzone = 1;
  setting_pad_mouse = 0;
  setting_latency = 1;
  setting_driver = 0;
  setting_sync = 1;
  setting_vsync = 1;
  setting_n64_core = 0; // ParaLLEl N64 - see the comment on its declaration
  setting_osd_timeout = 0;
  setting_name_scroll = 3;
  setting_sms_fm = 0;
  setting_nds_layout = 0;
  setting_nds_gap = 0;
  setting_nds_hybrid = 0;
  setting_citra_layout = 0;
  setting_cd_precache = 0;
  setting_cd_latency = 0;
  neo_sys = 0;
  neo_bios = 0;
  neo_cd_type = 0;
  neo_cd_region = 0;
  neo_memcard = 0;
  neo_dip_settings = 0;
  neo_dip_freeplay = 0;

  InputBindRestoreGlobal();
  InputBindResetDefaults();
  CoreSetVolume(100);
  CoreSetMute(false);
  VoiceChatSetEnabled(false);
  VoiceChatSetDenoiseEngine(VOICE_DENOISE_STRONG);
  VoiceChatSetVadEnabled(true);
  VoiceChatSetVadSensitivity(VOICE_VAD_MEDIUM);
  VoiceChatSetAecEnabled(true);
  VoiceChatSetAgcEnabled(true);
  TrayManagerSetEnabled(true);

  MenuSaveSettings();
}

// Definida mais abaixo, junto do comentario que explica por que a selecao
// e escolhida pela acao e nao pelo indice. Declarada aqui porque as telas
// desta secao vem antes dela no arquivo.
static void SelectByAction(int action_id, int fallback_idx);

void PopulateSettings() {
  items.clear();
  current_title = Tr("Configuracoes", "Settings");
  OsdSetSize(13);

  items.push_back({Tr("Idioma", "Language"), setting_language == 0 ? "Portugues" : "English", false, false, 216});
  items.push_back({Tr("Video", "Video"), ">", false, true, 201});
  items.push_back({Tr("Audio & Voz", "Audio & Voice"), ">", false, true, 202});
  items.push_back({Tr("Controles", "Controller"), ">", false, true, 203});
  items.push_back({Tr("Netplay (Online)", "Netplay (Online)"), ">", false, true, 204});
  items.push_back({"Karamelo Cloud", ">", false, true, 217});
  items.push_back({"Karamelo Live (Chat)", ">", false, true, 218});
  items.push_back({"Web Admin (Wi-Fi)", ">", false, true, 219});
  items.push_back({"RetroAchievements", ">", false, true, 206});
  items.push_back({Tr("Gerenciar Sistemas", "Manage Systems"), ">", false, true, 225});
  items.push_back({Tr("Diagnostico & Falhas", "Diagnostics & Crash"), ">", false, true, 215});
  items.push_back({Tr("Sobre", "About"), ">", false, true, 205});
  items.push_back({Tr("Restaurar Padroes", "Restore Defaults"), ">", false, true, 210});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateCloudSettings() {
  items.clear();
  current_title = "Karamelo Cloud";
  OsdSetSize(10);

  // No "Status" label: these messages need the whole row.
  items.push_back({CloudSyncGetStatusString(), "", false, false, 0});
  char cnt_buf[32];
  snprintf(cnt_buf, sizeof(cnt_buf), "%d", CloudSyncGetSyncedFileCount());
  items.push_back({Tr("Arquivos Salvos", "Saved Files"), cnt_buf, false, false, 0});
  items.push_back({"Sync", CloudSyncGetLastSyncTimeStr(), false, false, 0});
  items.push_back({Tr("Sync Auto", "Auto Sync"), CloudSyncGetAutoSync() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 901});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Sincronizar Agora", "Sync Now"), ">", false, true, 902});
  if (CloudSyncIsConnected()) {
    items.push_back({Tr("Desconectar Google", "Disconnect Google"), ">", false, true, 904});
  } else {
    items.push_back({Tr("Conectar Google Drive", "Connect Google Drive"), ">", false, true, 903});
  }
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 999});

  SelectByAction(902, 5);
  scroll_top = 0;
}

void PopulateManageSystems() {
  items.clear();
  current_title = Tr("Gerenciar Sistemas", "Manage Systems");

  items.push_back({Tr("Exibir Todos os Sistemas", "Show All Systems"), ">", false, true, 1850});
  items.push_back({Tr("Ocultar Sistemas Sem Jogos", "Hide Empty Systems"), ">", false, true, 1851});
  items.push_back({Tr("Ocultar Computadores", "Hide Computers"), ">", false, true, 1852});
  items.push_back({Tr("Ocultar Todos os Sistemas", "Hide All Systems"), ">", false, true, 1853});
  items.push_back({" ", "", false, false, 0});

  for (size_t i = 0; i < kSystemRegistryCount; i++) {
    const auto &sys = kSystemRegistry[i];
    bool vis = MenuIsSystemVisible(sys.action_id);
    std::string mark = vis ? Tr("[Visivel]", "[Visible]") : Tr("[Oculto]", "[Hidden]");
    items.push_back({sys.name, mark, false, true, 8000 + sys.action_id});
  }

  items.push_back({" ", "", false, false, 0});
  items.push_back({std::string("< ") + Tr("Voltar", "Back"), "", false, true, 999});

  OsdSetSize((int)items.size());
}

void PopulateWebAdminSettings() {
  items.clear();
  current_title = "Web Admin Wi-Fi";
  OsdSetSize(10);

  bool running = WebAdminIsRunning();
  items.push_back({Tr("Servidor", "Server"), running ? Tr("Ativo (LAN)", "Active (LAN)") : Tr("Desligado", "Stopped"), false, true, 910});
  items.push_back({Tr("Abrir no Navegador", "Open in Browser"), ">", false, true, 912});
  // The address the phone needs takes the whole row: it is what the player reads.
  items.push_back({running ? WebAdminGetUrl() : Tr("Endereco: servidor parado", "Address: server stopped"), "", false, false, 0});
  items.push_back({Tr("PIN de Acesso", "Access PIN"), WebAdminGetPin(), false, false, 0});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Gerar Novo PIN", "Regenerate PIN"), ">", false, true, 911});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 999});

  SelectByAction(910, 0);
  scroll_top = 0;
}

void PopulateLiveChatSettings() {
  items.clear();
  current_title = "Karamelo Live";
  OsdSetSize(11);

  items.push_back({Tr("Sala", "Room"), DesktopChatGetRoomName(DesktopChatGetRoom()), false, false, 920});
  items.push_back({"Status", TrayManagerGetStatusName(TrayManagerGetStatus()), false, false, 921});
#ifdef _WIN32
  items.push_back({Tr("Bandeja", "Tray"), TrayManagerIsEnabled() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 922});
#endif
  items.push_back({"Overlay", DesktopChatIsOverlayVisible() ? Tr("Visivel [F10]", "Visible [F10]") : Tr("Oculto [F10]", "Hidden [F10]"), false, true, 924});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Abrir Janela Chat", "Open Chat Window"), ">", false, true, 923});
  items.push_back({Tr("Convidar p/ Partida", "Invite to Match"), ">", false, true, 925});
  items.push_back({Tr("Aceitar Convite", "Accept Invite"), ">", false, true, 926});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 999});

  SelectByAction(923, 5);
  scroll_top = 0;
}

void PopulateDiagnostics() {
  items.clear();
  current_title = Tr("Diagnostico", "Diagnostics");
  OsdSetSize(6);

  bool enabled = CrashReporterIsEnabled();
  items.push_back({Tr("Relatorio de Falhas", "Crash Report"), enabled ? Tr("Ativado", "Enabled") : Tr("Desativado", "Disabled"), false, false, 520});
  items.push_back({Tr("[INFO] Envia falhas p/ correcao", "[INFO] Sends crash reports"), "", false, false, 0});
  items.push_back({Tr("[INFO] Se desligar nao envia", "[INFO] If disabled none sent"), "", false, false, 0});
  items.push_back({Tr("Testar Envio (Diagnostico)", "Test Send (Diagnostics)"), ">", false, true, 521});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 999});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateResetConfirm() {
  items.clear();
  current_title = Tr("Restaurar", "Restore");
  OsdSetSize(5);

  items.push_back({Tr("Restaurar Padroes?", "Restore Defaults?"), "", false, false, 0});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Cancelar", "Cancel"), ">", false, true, 211});
  items.push_back({Tr("Confirmar Restauracao", "Confirm Restore"), "", false, true, 212});

  SelectByAction(211, 2); // "Cancelar" por seguranca: nunca o Confirmar
  scroll_top = 0;
}

// Repopular uma tela reconstroi items[] inteiro, e a selecao precisa voltar
// para onde estava. Fazer isso por indice fixo quebra em silencio no dia em que
// alguem insere ou remove uma linha no meio: ao remover "Arcade Core" de
// Settings > Video, quatro indices tiveram de ser corrigidos na mao, e nada
// teria avisado se um passasse batido - a tela so escolheria a linha errada.
//
// Procurar pelo action_id nao tem esse problema. O indice continua aceito como
// segunda opcao porque nem toda chamada quer a propria linha de volta: algumas
// navegam para outra tela e escolhem uma linha de la, onde o action_id de
// origem nao existe.
static void SelectByAction(int action_id, int fallback_idx) {
  for (size_t i = 0; i < items.size(); i++) {
    if (action_id != 0 && items[i].action_id == action_id) {
      selected_idx = (int)i;
      return;
    }
  }
  selected_idx = fallback_idx;
}

void PopulateVideoSettings() {
  items.clear();
  current_title = Tr("Video", "Video");
  OsdSetSize(13);

  const char *aspects_pt[] = {"Original", "4:3", "16:9"};
  const char *aspects_en[] = {"Original", "4:3", "16:9"};
  const char *displays_pt[] = {"Janela", "Tela Cheia"};
  const char *displays_en[] = {"Windowed", "Fullscreen"};
  const char *themes_pt[] = {"Vermelho", "Azul", "Verde", "Ambar", "Cinza", "Escuro"};
  const char *themes_en[] = {"Red", "Blue", "Green", "Amber", "Gray", "Dark"};

  items.push_back({Tr("Proporcao", "Aspect Ratio"), (setting_language == 1) ? aspects_en[setting_aspect] : aspects_pt[setting_aspect], false, false, 301});
  items.push_back(
      {Tr("Shader CRT", "CRT Shader"), FilterLabel(setting_filter), false, true, 302});
  items.push_back(
      {Tr("Papel Parede", "Wallpaper"), WallpaperDisplayLabel(setting_wallpaper), false, false, 303});
  items.push_back(
      {Tr("Modo de Tela", "Display"), (setting_language == 1) ? displays_en[setting_fullscreen ? 1 : 0] : displays_pt[setting_fullscreen ? 1 : 0], false, false, 304});
  items.push_back({Tr("Cor do OSD", "OSD Color"), (setting_language == 1) ? themes_en[setting_theme] : themes_pt[setting_theme], false, false, 305});
  items.push_back({Tr("Driver Video", "Video Driver"), GetCurrentDriverLabel(), false, false, 306});

  // Named after what each core reports through retro_get_system_info, not
  // after the filename. cores/n64.dll identifies itself only as "Nintendo 64"
  // v1.0 - a rebuilt binary that names no engine and exports no memory, which
  // is why it earns no achievements. Labels are kept under the 13 characters
  // the OSD value column shows.
  const char *n64_cores[] = {"ParaLLEl N64", "Mupen64+ Next", "Gopher64"};
  items.push_back({Tr("Core do N64", "N64 Core"), n64_cores[setting_n64_core], false, false, 309});

  char osd_to[16];
  if (kOsdTimeouts[setting_osd_timeout] == 0)
    snprintf(osd_to, sizeof(osd_to), "%s", Tr("Nunca", "Never"));
  else
    snprintf(osd_to, sizeof(osd_to), "%ds", kOsdTimeouts[setting_osd_timeout]);
  items.push_back({Tr("Ocultar Menu", "Hide Menu"), osd_to, false, false, 312});

  const char *vsyncs_pt[] = {"Desativado", "Ativado"};
  const char *vsyncs_en[] = {"Disabled", "Enabled"};
  items.push_back({"V-Sync", (setting_language == 1) ? vsyncs_en[setting_vsync] : vsyncs_pt[setting_vsync], false, false, 308});

  const char *syncs_pt[] = {"Nativo (Jogo)", "Sinc. Monitor"};
  const char *syncs_en[] = {"Native (Game)", "Sync to Display"};
  items.push_back({Tr("Sincronizacao", "Display Sync"), (setting_language == 1) ? syncs_en[setting_sync] : syncs_pt[setting_sync], false, false, 307});

  // Read-only: what the app actually detected, and whether the sync could be
  // applied. On a 144Hz panel there is no usable divisor for 60fps content.
  char hz[48];
  if (setting_sync == 1 && !CoreDisplaySyncActive())
    snprintf(hz, sizeof(hz), "%.2fHz (%s)", CoreGetDisplayFps(), Tr("sem divisor", "no divisor"));
  else
    snprintf(hz, sizeof(hz), "%.2f Hz", CoreGetDisplayFps());
  items.push_back({Tr("Monitor", "Display"), hz, false, false, 0});

  char scroll_lbl[16];
  if (kScrollDelays[setting_name_scroll] == 0)
    snprintf(scroll_lbl, sizeof(scroll_lbl), "%s", Tr("Desativado", "Disabled"));
  else
    snprintf(scroll_lbl, sizeof(scroll_lbl), "%ds", kScrollDelays[setting_name_scroll]);
  items.push_back({Tr("Rolagem Nome", "Name Scroll"), scroll_lbl, false, false, 313});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateShaderList() {
  items.clear();
  current_title = Tr("Shaders CRT", "CRT Shaders");
  OsdSetSize(12);

  for (int i = 0; i < kFilterCount; i++) {
    bool is_active = (kFilters[i].mode == setting_filter);
    char label_buf[32];
    const char *lbl = (setting_language == 1) ? kFilters[i].label_en : kFilters[i].label_pt;
    snprintf(label_buf, sizeof(label_buf), "%s%s", is_active ? "* " : "  ", lbl);

    const char* desc = "";
    switch (kFilters[i].mode) {
      case 0: desc = Tr("Pixel Puro", "Pixel Raw"); break;
      case 8: desc = "Scanlines"; break;
      case 9: desc = Tr("TV Tubo", "CRT TV"); break;
      case 4: desc = "PVM 600TVL"; break;
      case 1: desc = "Trinitron"; break;
      case 2: desc = "Shadow Mask"; break;
      case 5: desc = Tr("Grade 50%", "50% Grid"); break;
      case 6: desc = Tr("NTSC Composto", "Composite"); break;
      case 7: desc = Tr("Matriz LCD", "LCD Matrix"); break;
      case 3: desc = Tr("Tubo Curvo", "Curved CRT"); break;
      default: desc = ""; break;
    }

    items.push_back({label_buf, desc, false, true, 650 + i});
  }

  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 699});

  selected_idx = FilterIndex(setting_filter);
  scroll_top = 0;
}

void PopulateSaveStates() {
  items.clear();
  current_title = Tr("Save States", "Save States");
  OsdSetSize(12);

  int active_slot = CoreGetSelectedSlot();

  for (int s = 0; s < 10; s++) {
    bool has_state = false;
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%s", Tr("Vazio", "Empty"));
    int64_t bytes = 0;
    CoreGetStateSlotInfo(s, &has_state, time_str, sizeof(time_str), &bytes);

    char slot_label[32];
    snprintf(slot_label, sizeof(slot_label), "%sSlot %d", (s == active_slot ? "> " : "  "), s);

    char slot_val[64];
    if (has_state) {
      if (bytes > 1024 * 1024)
        snprintf(slot_val, sizeof(slot_val), "%s (%.1fMB)", time_str, bytes / (1024.0 * 1024.0));
      else if (bytes > 1024)
        snprintf(slot_val, sizeof(slot_val), "%s (%dKB)", time_str, (int)(bytes / 1024));
      else
        snprintf(slot_val, sizeof(slot_val), "%s (%dB)", time_str, (int)bytes);
    } else {
      snprintf(slot_val, sizeof(slot_val), "%s", Tr("Vazio", "Empty"));
    }

    items.push_back({slot_label, slot_val, false, true, 850 + s});
  }

  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 899});

  selected_idx = active_slot;
  scroll_top = 0;
}

void PopulateAudioSettings() {
  items.clear();
  current_title = Tr("Audio & Voz", "Audio & Voice");
  OsdSetSize(13);

  const char *latencies[] = {"64 ms", "128 ms", "256 ms", "512 ms"};

  items.push_back({Tr("Mudo", "Mute"), CoreGetMute() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 401});
  items.push_back({Tr("Latencia", "Latency"), latencies[setting_latency], false, false, 402});

  char vol_str[32];
  snprintf(vol_str, sizeof(vol_str), "%d%%", CoreGetVolume());
  items.push_back({Tr("Volume", "Volume"), vol_str, false, false, 403});

  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Chat de Voz", "Voice Chat"), VoiceChatIsEnabled() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 930});
  items.push_back({Tr("Microfone", "Microphone"), VoiceChatIsMicMuted() ? Tr("Mutado", "Muted") : Tr("Ativo", "Active"), false, false, 931});
  items.push_back({Tr("Reducao Ruido", "Noise Filter"), VoiceChatGetDenoiseEngineName(VoiceChatGetDenoiseEngine()), false, false, 932});
  items.push_back({Tr("Corte de Som", "Silence Gate"), VoiceChatGetVadEnabled() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 933});
  items.push_back({Tr("Sensib. do Corte", "Gate Sens."), VoiceChatGetVadSensitivityName(VoiceChatGetVadSensitivity()), false, false, 934});
  items.push_back({Tr("Cancelar Eco", "Echo Cancel"), VoiceChatGetAecEnabled() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 935});
  items.push_back({Tr("Ganho Auto", "Auto Gain"), VoiceChatGetAgcEnabled() ? Tr("Ligado", "On") : Tr("Desligado", "Off"), false, false, 936});
  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "<", false, true, 999});

  selected_idx = 0;
  scroll_top = 0;
}

// Generic "Core Options" page (like RetroArch's Quick Menu > Options): lists
// whatever options the currently loaded core declared through
// RETRO_ENVIRONMENT_SET_VARIABLES - internal resolution, region, DSP,
// whatever - with zero core-specific code here. Reachable only from the
// in-game main menu (CoreOptionCount() > 0 there), not from Settings, so its
// own Back row (898) returns to STATE_MAIN/PopulateMainMenu() directly
// instead of going through the shared 999 handler, which always lands on
// STATE_SETTINGS - wrong for a page nothing routes through Settings to reach.
//
// Rows use action_id 800 + option_index, capped at 98 options (800-897) so
// the range can never collide with 898 (Back) - two full orders of magnitude
// past what any real core declares, this is headroom, not a real limit.
static const int kCoreOptionsMaxRows = 98;
void PopulateCoreOptionsSettings() {
  items.clear();
  current_title = Tr("Opcoes Core", "Core Options");

  int n = CoreOptionCount();
  if (n > kCoreOptionsMaxRows) n = kCoreOptionsMaxRows;
  for (int i = 0; i < n; i++) {
    std::string label = CoreOptionLabel(i);
    int cur = CoreOptionCurrentChoiceIndex(i);
    std::string val = CoreOptionChoiceAt(i, cur);
    items.push_back({label, val, false, false, 800 + i});
  }
  if (n == 0)
    items.push_back({Tr("Sem opcoes para este core", "No options for this core"), "", false, false, 0});

  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "", false, true, 898});

  OsdSetSize((int)items.size());
  selected_idx = 0;
  scroll_top = 0;
}

void PopulateNetplay() {
  items.clear();
  current_title = Tr("Netplay", "Netplay");
  OsdSetSize(9);

  const char *role_str = Tr("Desconectado", "Disconnected");
  if (NetplayGetState() == NETPLAY_LISTENING)
    role_str = Tr("Hospedando...", "Hosting...");
  else if (NetplayGetState() == NETPLAY_CONNECTING)
    role_str = Tr("Conectando...", "Connecting...");
  else if (NetplayGetState() == NETPLAY_CONNECTED) {
    role_str = (NetplayGetRole() == NETPLAY_HOST) ? Tr("Conectado (Host)", "Connected (Host)")
                                                  : Tr("Conectado (Cliente)", "Connected (Client)");
  }

  items.push_back({Tr("Rede", "Network"), role_str, false, false, 0});
  items.push_back({Tr("IP Local", "Local IP"), NetplayGetLocalIp(), false, false, 0});
  items.push_back({Tr("Hospedar", "Host"), Tr("Porta 55435", "Port 55435"), false, true, 601});
  items.push_back({Tr("Conectar", "Connect"), join_ip_input.c_str(), false, true, 602});
  items.push_back({Tr("Desconectar Netplay", "Disconnect Netplay"), "", false, true, 603});
  items.push_back({"Karamelo Live (Chat)", ">", false, true, 218});
  items.push_back({Tr("Porta Netplay", "Netplay Port"), "55435", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "", false, true, 999});

  SelectByAction(601, 2); // "Host Game"
  scroll_top = 0;
}

void PopulateAchievementList() {
  items.clear();
  current_title = Tr("Conquistas", "Achievements");

  RaAchievementInfo list[64];
  int n = RaGetAchievements(list, 64);

  if (n <= 0) {
    items.push_back({Tr("Nenhuma conquista", "No achievements"), "", false, false, 0});
    items.push_back({Tr("para este jogo", "for this game"), "", false, false, 0});
  } else {
    for (int i = 0; i < n; i++) {
      // A locked achievement in progress shows its counter; an unlocked
      // one shows a tick. Points otherwise.
      char val[24];
      if (list[i].unlocked)
        snprintf(val, sizeof(val), "OK %d", list[i].points);
      else if (list[i].progress[0])
        snprintf(val, sizeof(val), "%s", list[i].progress);
      else
        snprintf(val, sizeof(val), "%d pts", list[i].points);

      std::string title = list[i].title;
      if (title.length() > 20)
        title = title.substr(0, 18) + "..";

      items.push_back({title, val, false, false, 0});
    }
  }

  OsdSetSize(items.size() > 12 ? 12 : (int)items.size());
  selected_idx = 0;
  scroll_top = 0;
}

void PopulateRetroAchievements() {
  items.clear();
  current_title = "RetroAchievements";
  OsdSetSize(9);

  // Esta tela e somente leitura, e o arquivo e o unico editor que existe. Ela
  // dizia onde ele fica so quando a integracao estava DESATIVADA - quem ja
  // tinha logado uma vez e depois quis trocar de conta nao tinha por onde
  // comecar. O aviso passou a valer nos dois estados.
  //
  // A linha do token nao e detalhe: com token preenchido o login usa o token e
  // ignora a senha (RaInit, em retroachievements.cpp). Trocar a senha no
  // arquivo sem esvaziar o token nao muda absolutamente nada, e o sintoma e o
  // app seguir entrando na conta antiga sem explicar por que.
  //
  // Os rotulos param em 22 caracteres porque e o que a linha do OSD comporta;
  // o "Config/retroachievements" que estava aqui chegava cortado na tela.
  auto PushLoginHint = [&]() {
    items.push_back({Tr("Editar em Config/", "Edit in Config/"), "", false, false, 0});
    items.push_back({"retroachievements.cfg", "", false, false, 0});
    items.push_back({Tr("Trocar conta: apague", "Change acc: remove"), "", false, false, 0});
    items.push_back({Tr("a linha token=", "token= line"), "", false, false, 0});
  };

  if (!RaIsEnabled()) {
    items.push_back({Tr("Estado", "Status"), Tr("Desativado", "Disabled"), false, false, 0});
    PushLoginHint();
  } else {
    items.push_back(
        {Tr("Usuario", "User"), RaIsLoggedIn() ? RaGetUserName() : "-", false, false, 0});
    items.push_back({Tr("Modo", "Mode"), RaIsHardcoreActive() ? "Hardcore" : "Softcore",
                     false, false, 0});

    if (RaGetAchievementCount() > 0) {
      char v[40];
      snprintf(v, sizeof(v), "%d/%d", RaGetAchievementsUnlocked(),
               RaGetAchievementCount());
      items.push_back({Tr("Conquistas", "Achievements"), v, false, false, 0});
    }

    if (RaHasPendingUnlocks())
      items.push_back({Tr("Pendentes", "Pending"), Tr("sem conexao", "offline"), false, false, 0});

    items.push_back({" ", "", false, false, 0});
    PushLoginHint();
  }

  // The status line carries the real detail - why a login failed, or why a
  // game was not recognised. It used to exist only as an unread variable.
  //
  // Ela entra DEPOIS do aviso do arquivo e so ate encher a tela. A OSD tem
  // 15 linhas fixas e nao rola sozinha - nenhum item desta tela e
  // selecionavel, entao scroll_top nunca sai de zero. Sem esse limite, um
  // status longo (o buffer aceita 192 caracteres, 8 linhas) empurraria a
  // instrucao de onde fica o login para fora da tela, justo quando algo deu
  // errado e ela e mais necessaria. O aviso e permanente; o status e que
  // cede espaco.
  std::string st = RaGetStatus();
  int livres = OsdGetSize() - (int)items.size();
  for (size_t i = 0; i < st.size() && livres > 0; i += 24, livres--)
    items.push_back({st.substr(i, 24), "", false, false, 0});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateAbout() {
  items.clear();
  current_title = Tr("Sobre", "About");
  OsdSetSize(10);

  // The figures shown are the ones app_info.h publishes, which are the same
  // ones the website carries. The cores folder is counted for real and any
  // disagreement goes to the log, so this page cannot quietly go stale the
  // way its hardcoded "11 Consoles" did while the menu offered seventeen.
  int core_files = 0;
  {
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator("cores", ec)) {
      if (ec)
        break;
      std::string x = entry.path().extension().string();
      std::transform(x.begin(), x.end(), x.begin(), ::tolower);
      if (x == ".dll" || x == ".dylib" || x == ".so")
        core_files++;
    }
  }
  if (core_files > 0 && core_files != APP_CORE_FILES) {
    FILE *lf = fopen("karamelo.log", "a");
    if (lf) {
      fprintf(lf,
              "[WARN] [ABOUT] cores/ tem %d DLLs, mas APP_CORE_FILES diz %d - "
              "atualize app_info.h e o site\n",
              core_files, APP_CORE_FILES);
      fclose(lf);
    }
  }

  char sys_str[32], core_str[32], ports_str[32];
  snprintf(sys_str, sizeof(sys_str), "%d %s", APP_SYSTEM_COUNT, Tr("Sistemas", "Systems"));
  snprintf(core_str, sizeof(core_str), "%d Cores", APP_CORE_ENGINES);
  // Counted the same way Systems/Cores are - queried for real rather than a
  // second hardcoded figure that this list would just as quietly outgrow.
  snprintf(ports_str, sizeof(ports_str), "%d %s", (int)PortGetAvailableList().size(), Tr("Jogos", "Games"));

  items.push_back({APP_NAME, "v" APP_VERSION " " APP_ARCH, false, false, 0});
  items.push_back({Tr("Autor", "Author"), "Guh Clemente", false, false, 0});
  items.push_back({"YouTube", "@GuhClemente", false, false, 0});
  items.push_back({"Site", APP_SITE, false, false, 0});
  items.push_back({Tr("Motor", "Engine"), "Libretro", false, false, 0});
  items.push_back({Tr("Sistemas", "Systems"), sys_str, false, false, 0});
  items.push_back({Tr("Cores", "Cores"), std::string(core_str) + " >", false, true, 209});
  items.push_back({"Ports & Recomp", std::string(ports_str) + " >", false, true, 140});
  items.push_back({"Netplay", "2P TCP/IP", false, false, 0});
  items.push_back({Tr("Conquistas", "Achievements"), RaIsEnabled() ? Tr("Ativado", "Enabled") : Tr("Desativado", "Disabled"),
                   false, false, 0});
  items.push_back({Tr("Licenca", "License"), "GPL-3.0", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "", false, true, 999});

  SelectByAction(209, 6); // linha "Cores"
  scroll_top = 0;
}

void PopulateCoresList() {
  items.clear();
  current_title = Tr("Cores", "Cores");
  OsdSetSize(15);

  struct CoreEntry {
    const char* system;
    const char* engine;
    const char* dll;
  };

  static const CoreEntry kAllCores[] = {
    {"Nintendo 64", "ParaLLEl N64", "cores/n64_parallel.dll"},
    {"Nintendo 64", "Gopher64", "cores/n64_gopher.dll"},
    {"Nintendo 64", "Mupen64+ Next", "cores/n64_mupen.dll"},
    {"PlayStation 2", "PCSX2", "cores/ps2.dll"},
    {"PlayStation 2", "Play!", "cores/ps2_play.dll"},
    {"PlayStation Port.", "PPSSPP", "cores/psp.dll"},
    {"PlayStation 1", "Beetle PSX", "cores/psx.dll"},
    {"Dreamcast", "Flycast", "cores/dreamcast.dll"},
    {"GameCube", "Dolphin", "cores/gamecube.dll"},
    {"Nintendo 3DS", "Citra", "cores/3ds.dll"},
    {"Nintendo DS", "melonDS", "cores/nds.dll"},
    {"SNES", "Snes9x", "cores/snes.dll"},
    {"NES / Famicom", "Mesen / FCEU", "cores/nes.dll"},
    {"Game Boy Advance", "mGBA", "cores/gba.dll"},
    {"Game Boy / Color", "Gambatte", "cores/gb.dll"},
    {"Arcade", "FinalBurn Neo", "cores/arcade_fbneo.dll"},
    {"Arcade", "MAME 2003", "cores/mame2003.dll"},
    {"Arcade", "MAME 2010", "cores/mame2010.dll"},
    {"Genesis / MD", "Genesis Plus", "cores/genesis.dll"},
    {"Sega Saturn", "Beetle Saturn", "cores/saturn.dll"},
    {"Sega 32X", "PicoDrive", "cores/32x.dll"},
    {"Mega CD", "Genesis Plus", "cores/genesis.dll"},
    {"Master System", "Gearsystem", "cores/sms.dll"},
    {"NeoGeo / CD", "Geolith", "cores/neogeo.dll"},
    {"Neo Geo Pocket", "Beetle NeoPop", "cores/ngp.dll"},
    {"TurboGrafx-16", "Beetle PCE", "cores/pce.dll"},
    {"PC-FX", "Beetle PC-FX", "cores/pcfx.dll"},
    {"Panasonic 3DO", "Opera", "cores/3do.dll"},
    {"Atari 2600", "Stella", "cores/atari2600.dll"},
    {"Atari 5200", "Atari800", "cores/atari5200.dll"},
    {"Atari 7800", "ProSystem", "cores/atari7800.dll"},
    {"Atari Jaguar", "Virt. Jaguar", "cores/jaguar.dll"},
    {"Atari Lynx", "Handy", "cores/lynx.dll"},
    {"ColecoVision", "Gearcoleco", "cores/coleco.dll"},
    {"Commodore 64", "VICE x64", "cores/c64.dll"},
    {"Commodore Amiga", "PUAE", "cores/amiga.dll"},
    {"MS-DOS", "DOSBox-Pure", "cores/dosbox_pure.dll"},
    {"MSX / MSX2", "fMSX/BlueMSX", "cores/msx.dll"},
    {"NEC PC-98", "NP2kai", "cores/pc98.dll"},
    {"FM Towns", "Geartowns", "cores/fmtowns.dll"},
    {"ZX Spectrum", "Fuse", "cores/spectrum.dll"},
    {"WonderSwan", "Beetle Cygne", "cores/wswan.dll"}
  };

  for (const auto& c : kAllCores) {
    bool present = CoreBinaryExists(c.dll);
    std::string status = present ? c.engine : Tr("(Ausente)", "(Missing)");
    items.push_back({c.system, status, false, false, 0});
  }

  items.push_back({" ", "", false, false, 0});
  items.push_back({Tr("Voltar", "Back"), "", false, true, 997});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateUpdate() {
  // This screen's action rows use ids 391-393, not the 3xx range Video
  // Settings already owns - KEY_LEFT/KEY_RIGHT's dispatch chain matches
  // action_id alone with no current_state check, so reusing 301/302/303
  // here (as this used to) let pressing Left/Right instead of Select on
  // "Retry"/"Download"/"Restart" silently cycle Aspect Ratio/CRT Shader/
  // Wallpaper and switch the screen to Video Settings out from under
  // STATE_UPDATE.
  items.clear();
  current_title = Tr("Atualizacao", "Update");
  OsdSetSize(10);

  UpdaterState state = UpdaterGetState();
  const UpdateInfo &info = UpdaterGetInfo();

  items.push_back({Tr("Versao Atual", "Current Version"), "v" APP_VERSION, false, false, 0});

  if (!info.version.empty()) {
    items.push_back({Tr("Versao Remota", "Remote Version"), "v" + info.version, false, false, 0});
  } else {
    items.push_back({Tr("Versao Remota", "Remote Version"), "...", false, false, 0});
  }

  items.push_back({" ", "", false, false, 0});

  if (state == UPDATER_STATE_CHECKING) {
    items.push_back({Tr("Status", "Status"), Tr("Verificando...", "Checking..."), false, false, 0});
    items.push_back({" ", "", false, false, 0});
  } else if (state == UPDATER_STATE_UP_TO_DATE) {
    items.push_back({Tr("Status", "Status"), Tr("Atualizado!", "Up to date!"), false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({Tr("Verificar Novamente", "Check Again"), ">", false, true, 391});
  } else if (state == UPDATER_STATE_AVAILABLE) {
    items.push_back({Tr("Status", "Status"), Tr("Nova Versao!", "New Version!"), false, false, 0});
    if (!info.notes.empty()) {
      std::string note = info.notes;
      if (note.length() > 24) note = note.substr(0, 22) + "..";
      items.push_back({Tr("Novidades", "Changelog"), note, false, false, 0});
    }
    items.push_back({" ", "", false, false, 0});
    items.push_back({Tr("Baixar e Atualizar", "Download & Update"), ">", false, true, 392});
  } else if (state == UPDATER_STATE_DOWNLOADING) {
    int prog = UpdaterGetProgress();
    char prog_str[32];
    snprintf(prog_str, sizeof(prog_str), "%d%%", prog);
    items.push_back({Tr("Status", "Status"), Tr("Baixando...", "Downloading..."), false, false, 0});
    items.push_back({Tr("Progresso", "Progress"), prog_str, false, false, 0});

    char bar[26];
    int filled = (prog * 16) / 100;
    bar[0] = '[';
    for (int b = 0; b < 16; b++) bar[b + 1] = (b < filled) ? '=' : ' ';
    bar[17] = ']';
    bar[18] = '\0';
    items.push_back({bar, prog_str, false, false, 0});
  } else if (state == UPDATER_STATE_READY) {
    items.push_back({Tr("Status", "Status"), Tr("Pronto!", "Ready!"), false, false, 0});
    items.push_back({Tr("Download Concluido", "Download Complete"), "", false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({Tr("Reiniciar Agora", "Restart Now"), ">", false, true, 393});
  } else if (state == UPDATER_STATE_ERROR) {
    items.push_back({Tr("Status", "Status"), Tr("Erro de conexao", "Connection error"), false, false, 0});
    std::string err_msg = UpdaterGetStatusMessage();
    if (err_msg.length() > 24) err_msg = err_msg.substr(0, 22) + "..";
    items.push_back({err_msg, "", false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({Tr("Tentar Novamente", "Try Again"), ">", false, true, 391});
  } else {
    items.push_back({Tr("Status", "Status"), Tr("Pronto", "Ready"), false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({Tr("Verificar Atualizacoes", "Check for Updates"), ">", false, true, 391});
  }

  items.push_back({Tr("Voltar", "Back"), "", false, true, 399});

  if (selected_idx >= (int)items.size()) selected_idx = (int)items.size() - 1;
  if (selected_idx < 0) selected_idx = 0;
}

static std::vector<std::string> s_favorites_paths;
static std::vector<std::string> s_recent_paths;

void PopulateFavoritesMenu() {
  items.clear();
  current_title = Tr("Favoritos", "Favorites");
  LoadFavoritesAndRecent();
  s_favorites_paths.clear();

  items.push_back({"<..>", "", true, false, 0});

  for (size_t i = 0; i < g_favorites.size(); i++) {
    const auto &fav = g_favorites[i];
    if (fav.rfind("port:", 0) == 0) {
      std::string pid = fav.substr(5);
      auto plist = PortGetAvailableList();
      std::string label = pid;
      for (const auto &p : plist) {
        if (p.id == pid) { label = p.name; break; }
      }
      s_favorites_paths.push_back(fav);
      items.push_back({label, "[Port]", false, true, 1850});
    } else {
      fs::path p(fav);
      std::string fname = p.stem().string();
      std::string sys = p.parent_path().filename().string();
      s_favorites_paths.push_back(fav);
      items.push_back({fname, sys, false, true, 1850});
    }
  }

  if (s_favorites_paths.empty()) {
    items.push_back({Tr("[Nenhum Favorito]", "[No Favorites]"), "", false, false, 0});
  }

  OsdSetSize((int)items.size() > 12 ? 12 : (int)items.size());
  selected_idx = s_favorites_paths.empty() ? 0 : 1;
  scroll_top = 0;
}

void PopulateRecentMenu() {
  items.clear();
  current_title = Tr("Recentes", "Recent Games");
  LoadFavoritesAndRecent();
  s_recent_paths.clear();

  items.push_back({"<..>", "", true, false, 0});

  for (size_t i = 0; i < g_recent.size(); i++) {
    const auto &rec = g_recent[i];
    if (rec.rfind("port:", 0) == 0) {
      std::string pid = rec.substr(5);
      auto plist = PortGetAvailableList();
      std::string label = pid;
      for (const auto &p : plist) {
        if (p.id == pid) { label = p.name; break; }
      }
      s_recent_paths.push_back(rec);
      items.push_back({label, "[Port]", false, true, 870});
    } else {
      fs::path p(rec);
      std::string fname = p.stem().string();
      std::string sys = p.parent_path().filename().string();
      s_recent_paths.push_back(rec);
      items.push_back({fname, sys, false, true, 870});
    }
  }

  if (s_recent_paths.empty()) {
    items.push_back({Tr("[Nenhum Jogo Recente]", "[No Recent Games]"), "", false, false, 0});
  }

  OsdSetSize((int)items.size() > 12 ? 12 : (int)items.size());
  selected_idx = s_recent_paths.empty() ? 0 : 1;
  scroll_top = 0;
}

void PopulateBrowse(const std::string &dirpath) {
  items.clear();

  // Safety: allow roms/ and ports/
  std::string lower_dir = dirpath;
  std::transform(lower_dir.begin(), lower_dir.end(), lower_dir.begin(), ::tolower);
  if (lower_dir.find("cache") != std::string::npos || lower_dir == "." || lower_dir == "app" ||
      (lower_dir.find("roms") == std::string::npos && lower_dir.find("ports") == std::string::npos && !dirpath.empty())) {
    current_state = STATE_MAIN;
    PopulateMainMenu();
    return;
  }

  current_dir = dirpath;

  if (dirpath == "ports" || lower_dir == "ports") {
    current_title = "Ports & Recomp";
    items.push_back({"<..>", "", true, false, 0});
    auto plist = PortGetAvailableList();
    for (const auto& p : plist) {
      std::string disp = p.name;
      if (MenuIsFavorite("port:" + p.id)) {
        disp = "* " + p.name;
      }
      items.push_back({disp, "", false, true, 800});
    }
    if (plist.empty()) {
      items.push_back({Tr("[Nenhum Port Encontrado]", "[No Ports Found]"), "", false, false, 0});
    }
    OsdSetSize((int)items.size());
    SelectByAction(800, 1);
    scroll_top = 0;
    return;
  }

  fs::path p(dirpath);
  std::string folder_name = p.filename().string();
  if (folder_name.empty() || folder_name == "." || folder_name == "roms") {
    current_title = "Cores";
  } else {
    if (!folder_name.empty() && folder_name[0] == '_')
      current_title = folder_name.substr(1);
    else
      current_title = folder_name;
  }

  OsdSetSize(12);

  if (CoreIsRunning() || (dirpath != "roms" && dirpath != "." && dirpath != "/" &&
      dirpath != "\\")) {
    items.push_back({"<..>", "", true, false, 0});
  }

  try {
    std::vector<MenuItem> dirs;
    std::vector<MenuItem> files;

    if (fs::exists(dirpath) && fs::is_directory(dirpath)) {
      std::vector<fs::directory_entry> all_entries;
      for (const auto &entry : fs::directory_iterator(dirpath)) {
        all_entries.push_back(entry);
      }

      std::unordered_set<std::string> hidden_companion_files;

      // Pass 1: Parse .cue, .m3u, .gdi, .toc to hide companion raw binary tracks
      for (const auto &entry : all_entries) {
        try {
          if (entry.is_directory()) continue;
          std::string ext = entry.path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

          if (ext == ".cue" || ext == ".gdi" || ext == ".toc") {
            std::string stem = entry.path().stem().string();
            std::string lower_stem = stem;
            std::transform(lower_stem.begin(), lower_stem.end(), lower_stem.begin(), ::tolower);

            // Read the descriptor file to find referenced tracks
            FILE* f = fopen(entry.path().string().c_str(), "r");
            if (f) {
              char line[1024];
              while (fgets(line, sizeof(line), f)) {
                char* file_kw = strstr(line, "FILE ");
                if (file_kw) {
                  char* p_start = file_kw + 5;
                  while (*p_start == ' ' || *p_start == '\t') p_start++;
                  if (*p_start == '\"') {
                    p_start++;
                    char* p_end = strchr(p_start, '\"');
                    if (p_end) {
                      std::string ref_fn(p_start, p_end);
                      std::string lower_ref = fs::path(ref_fn).filename().string();
                      std::transform(lower_ref.begin(), lower_ref.end(), lower_ref.begin(), ::tolower);
                      hidden_companion_files.insert(lower_ref);
                    }
                  }
                }
              }
              fclose(f);
            }

            // Also hide same-stem companion tracks
            hidden_companion_files.insert(lower_stem + ".bin");
            hidden_companion_files.insert(lower_stem + ".iso");
            hidden_companion_files.insert(lower_stem + ".img");
            hidden_companion_files.insert(lower_stem + ".raw");
          }
          else if (ext == ".m3u") {
            FILE* f = fopen(entry.path().string().c_str(), "r");
            if (f) {
              char line[1024];
              while (fgets(line, sizeof(line), f)) {
                std::string l = line;
                while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ' || l.back() == '\t'))
                  l.pop_back();
                size_t start = 0;
                while (start < l.size() && (l[start] == ' ' || l[start] == '\t')) start++;
                if (start < l.size() && l[start] != '#') {
                  std::string ref_fn = fs::path(l.substr(start)).filename().string();
                  std::transform(ref_fn.begin(), ref_fn.end(), ref_fn.begin(), ::tolower);
                  hidden_companion_files.insert(ref_fn);
                }
              }
              fclose(f);
            }
          }
        } catch (...) {}  // see the note on pass 2
      }

      // Pass 2: Populate list with directories and valid playable files only
      for (const auto &entry : all_entries) {
        try {
          std::string filename = entry.path().filename().string();
          if (filename.empty() || filename[0] == '.')
            continue;

          std::string lower_fn = filename;
          std::transform(lower_fn.begin(), lower_fn.end(), lower_fn.begin(), ::tolower);

          if (hidden_companion_files.count(lower_fn)) {
            continue;
          }

          if (lower_fn == "cache" || lower_fn == "config" || lower_fn == "scripts" ||
              lower_fn == "wallpapers" || lower_fn == "bios" || lower_fn == "cores" ||
              lower_fn == "saves" || lower_fn == "cheats" ||
              lower_fn.ends_with(".exe") || lower_fn.ends_with(".pdb") ||
              lower_fn.ends_with(".dll") || lower_fn.ends_with(".log") ||
              lower_fn.ends_with(".txt") || lower_fn.ends_with(".nfo") ||
              (lower_fn.ends_with(".md") && !IsMegaDriveRom(entry.path())) ||
              lower_fn.ends_with(".ini") ||
              lower_fn.ends_with(".cfg") || lower_fn.ends_with(".pdf") ||
              lower_fn.ends_with(".doc") || lower_fn.ends_with(".docx") ||
              lower_fn.ends_with(".png") || lower_fn.ends_with(".jpg") ||
              lower_fn.ends_with(".jpeg") || lower_fn.ends_with(".url") ||
              lower_fn.ends_with(".bat") || lower_fn.ends_with(".json") ||
              lower_fn.ends_with(".xml")) {
            continue;
          }

          if (entry.is_directory()) {
            // Hide arcade CHD data folders (e.g. cvs2, cvs2mf, sfiii3) from the game list
            if (dirpath.find("Arcade") != std::string::npos) {
              bool has_chd = false;
              std::error_code ec_sub;
              for (const auto &sub : fs::directory_iterator(entry.path(), ec_sub)) {
                if (sub.path().extension() == ".chd") {
                  has_chd = true;
                  break;
                }
              }
              if (has_chd) continue;
            }
            dirs.push_back({filename, ">", true, false, 0});
          } else {
            std::string full_rom = (fs::path(dirpath) / filename).string();
            std::string disp = filename;
            if (MenuIsFavorite(full_rom)) {
              disp = "* " + filename;
            }
            files.push_back({disp, "", false, false, 0});
          }
        } catch (...) {
          // On Windows a file name with characters outside the ANSI code page
          // (a Japanese title, say) makes path::string() throw. One such file
          // used to abort the whole listing and show the folder as empty;
          // now only that entry is skipped.
        }
      }
    }

    std::sort(
        dirs.begin(), dirs.end(),
        [](const MenuItem &a, const MenuItem &b) { return a.label < b.label; });
    std::sort(
        files.begin(), files.end(),
        [](const MenuItem &a, const MenuItem &b) { return a.label < b.label; });

    for (const auto &d : dirs)
      items.push_back(d);
    for (const auto &f : files)
      items.push_back(f);
  } catch (...) {
    items.push_back({Tr("[Diretorio Vazio]", "[Empty Directory]"), "", false, false, 0});
  }

  if (items.empty()) {
    items.push_back({Tr("[Diretorio Vazio]", "[Empty Directory]"), "", false, false, 0});
  }

  selected_idx = 0;
  scroll_top = 0;
}

// Going up one level in the browser. Both the "<..>" row and Escape land here.
static void BrowseGoUp() {
  fs::path p(current_dir);
  std::string parent = p.parent_path().string();

  std::string lower_cur = current_dir;
  std::transform(lower_cur.begin(), lower_cur.end(), lower_cur.begin(), ::tolower);

  std::string lower_parent = parent;
  std::transform(lower_parent.begin(), lower_parent.end(), lower_parent.begin(), ::tolower);

  // If we are at the top of roms/ or ports/ or in any invalid/cache dir, return to Main Menu
  if (lower_cur == "ports" || lower_cur.find("ports") != std::string::npos ||
      parent.empty() || parent == "roms" || parent == "." || parent == "app" ||
      lower_cur == "roms" || lower_cur.find("cache") != std::string::npos ||
      (lower_cur.find("roms") == std::string::npos && lower_cur.find("ports") == std::string::npos) ||
      (lower_parent.find("roms") == std::string::npos && lower_parent.find("ports") == std::string::npos)) {
    current_state = STATE_MAIN;
    PopulateMainMenu();
    selected_idx = CoreIsRunning() ? 0 : main_menu_saved_idx;
    if (selected_idx < 0 || selected_idx >= (int)items.size())
      selected_idx = 0;
    return;
  }

  const std::string old_sub = p.filename().string();
  PopulateBrowse(parent);
  for (size_t k = 0; k < items.size(); k++) {
    if (items[k].label == old_sub) {
      selected_idx = (int)k;
      break;
    }
  }
}

// -------------------------------------------------------------
// Settings persistence
//
// Every setting lived only in memory, so aspect, filter, theme, wallpaper and
// volume reset on every launch. Written as plain key=value text next to the
// executable, under Config/.
// -------------------------------------------------------------
static const char *SETTINGS_PATH = "Config/karamelo.cfg";
// Older filenames this project shipped under, newest first. Read-only: the
// first one that exists is loaded, and the very next MenuSaveSettings() writes
// everything back out to SETTINGS_PATH above, so an upgrading player keeps
// every binding, video/audio choice and Core Option instead of being silently
// reset to defaults. These are the names the project shipped under before the
// rename to Karamelo, and they stay here verbatim: they are the on-disk files
// an existing install actually has, so renaming them here would silently drop
// everyone's settings on the first launch after upgrading.
static const char *LEGACY_SETTINGS_PATHS[] = {
    "Config/mister4all.cfg",
    "Config/mister_flavor.cfg",
    "Config/sabor_mister.cfg",
};

// One key per call. The previous version was a single snprintf with twenty
// arguments, and adding a setting to the argument list without adding it to
// the format string silently shifted every later specifier - the trailing %s
// ended up reading an int as a pointer. That crashed on a keypress whenever
// the stale stack slot happened to hold non-zero garbage.
static void AppendSetting(std::string &out, const char *key, int value) {
  char line[128];
  snprintf(line, sizeof(line), "%s=%d\n", key, value);
  out += line;
}

static std::string SettingsSnapshot() {
  std::string out;

  AppendSetting(out, "aspect", setting_aspect);
  AppendSetting(out, "language", setting_language);
  AppendSetting(out, "filter", setting_filter);
  AppendSetting(out, "pad_device", setting_pad_device);
  for (int i = 0; i < BIND_COUNT; i++) {
    AppendSetting(out, InputBindConfigKey(i), InputBindGetGlobalKey(i));
    AppendSetting(out, InputBindPadConfigKey(i), InputBindGetGlobalPad(i));
  }
  AppendSetting(out, "wallpaper", setting_wallpaper);
  AppendSetting(out, "fullscreen", setting_fullscreen ? 1 : 0);
  AppendSetting(out, "win_x", setting_win_x);
  AppendSetting(out, "win_y", setting_win_y);
  AppendSetting(out, "win_w", setting_win_w);
  AppendSetting(out, "win_h", setting_win_h);
  AppendSetting(out, "theme", setting_theme);
  AppendSetting(out, "deadzone", setting_deadzone);
  AppendSetting(out, "pad_mouse", setting_pad_mouse);
  AppendSetting(out, "latency", setting_latency);
  AppendSetting(out, "driver", setting_driver);
  AppendSetting(out, "sync", setting_sync);
  AppendSetting(out, "vsync", setting_vsync);
  AppendSetting(out, "n64_core", setting_n64_core);
  AppendSetting(out, "osd_timeout", setting_osd_timeout);
  AppendSetting(out, "name_scroll", setting_name_scroll);
  AppendSetting(out, "hw_render", (setting_driver != VIDEO_DRIVER_SOFTWARE) ? 1 : 0);
  AppendSetting(out, "sms_fm", setting_sms_fm);
  AppendSetting(out, "nds_layout", setting_nds_layout);
  AppendSetting(out, "nds_gap", setting_nds_gap);
  AppendSetting(out, "nds_hybrid", setting_nds_hybrid);
  AppendSetting(out, "citra_layout", setting_citra_layout);
  AppendSetting(out, "cd_precache", setting_cd_precache);
  AppendSetting(out, "cd_latency", setting_cd_latency);
  AppendSetting(out, "volume", CoreGetVolume());
  AppendSetting(out, "mute", CoreGetMute() ? 1 : 0);
  AppendSetting(out, "neo_sys", neo_sys);
  AppendSetting(out, "neo_bios", neo_bios);
  AppendSetting(out, "neo_cd_type", neo_cd_type);
  AppendSetting(out, "neo_cd_region", neo_cd_region);
  AppendSetting(out, "neo_memcard", neo_memcard);
  AppendSetting(out, "neo_dip_settings", neo_dip_settings);
  AppendSetting(out, "neo_dip_freeplay", neo_dip_freeplay);
  AppendSetting(out, "voice_enabled", VoiceChatIsEnabled() ? 1 : 0);
  AppendSetting(out, "voice_denoise", VoiceChatGetDenoiseEngine());
  AppendSetting(out, "voice_vad", VoiceChatGetVadEnabled() ? 1 : 0);
  AppendSetting(out, "voice_vad_sens", VoiceChatGetVadSensitivity());
  AppendSetting(out, "voice_aec", VoiceChatGetAecEnabled() ? 1 : 0);
  AppendSetting(out, "voice_agc", VoiceChatGetAgcEnabled() ? 1 : 0);
  AppendSetting(out, "tray_enabled", TrayManagerIsEnabled() ? 1 : 0);
  std::vector<int> hidden_copy;
  {
    std::lock_guard<std::mutex> lk(s_hidden_mutex);
    hidden_copy.assign(s_hidden_systems.begin(), s_hidden_systems.end());
  }
  std::sort(hidden_copy.begin(), hidden_copy.end()); // ordem estavel no arquivo
  if (!hidden_copy.empty()) {
    std::string hstr = "hidden_systems=";
    bool first = true;
    for (int hid : hidden_copy) {
      if (!first) hstr += ",";
      first = false;
      hstr += std::to_string(hid);
    }
    hstr += "\n";
    out += hstr;
  }

  out += "netplay_ip=";
  out += join_ip_input;
  out += "\n";

  // Core Options page choices - see g_persisted_core_options's own comment.
  // "=" cannot appear in a libretro option key, so this splits back apart
  // the same simple way every other line in this file does.
  for (const auto &kv : g_persisted_core_options) {
    out += "coreopt:";
    out += kv.first;
    out += "=";
    out += kv.second;
    out += "\n";
  }

  return out;
}

static void MenuSaveSettings() {
  WriteFileAtomic(SETTINGS_PATH, "# Karamelo - Configuration\n" + SettingsSnapshot());
}

static void MenuLoadSettings() {
  FILE *f = fopen(SETTINGS_PATH, "rb");
  bool from_legacy = false;
  for (size_t i = 0; !f && i < sizeof(LEGACY_SETTINGS_PATHS) / sizeof(LEGACY_SETTINGS_PATHS[0]); i++) {
    f = fopen(LEGACY_SETTINGS_PATHS[i], "rb");
    if (f) from_legacy = true;
  }
  if (!f) {
    // Seed default settings file on first launch
    MenuSaveSettings();
    return;
  }

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#')
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';

    const char *key = line;
    char *val = eq + 1;

    // strip the newline
    size_t n = strlen(val);
    while (n > 0 && (val[n - 1] == '\n' || val[n - 1] == '\r'))
      val[--n] = '\0';

    int iv = atoi(val);

    if (!strcmp(key, "aspect"))
      setting_aspect = ClampInt(iv, 0, 2);
    else if (!strcmp(key, "language"))
      setting_language = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "filter"))
      setting_filter = MigrateFilter(ClampInt(iv, 0, 9));
    else if (!strcmp(key, "wallpaper"))
      setting_wallpaper = ClampInt(iv, 0, WallpaperMaxMode());
    else if (!strcmp(key, "fullscreen"))
      setting_fullscreen = (iv != 0);
    else if (!strcmp(key, "win_x"))
      setting_win_x = iv;
    else if (!strcmp(key, "win_y"))
      setting_win_y = iv;
    else if (!strcmp(key, "win_w"))
      setting_win_w = iv;
    else if (!strcmp(key, "win_h"))
      setting_win_h = iv;
    else if (!strcmp(key, "theme"))
      setting_theme = ClampInt(iv, 0, 5);
    else if (!strcmp(key, "deadzone"))
      setting_deadzone = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "pad_mouse"))
      setting_pad_mouse = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "pad_device"))
      setting_pad_device = ClampInt(iv, 0, 1);
    else if (InputBindFromConfigKey(key) >= 0)
      InputBindSetKey(InputBindFromConfigKey(key), iv);
    else if (InputBindFromPadConfigKey(key) >= 0)
      InputBindSetPad(InputBindFromPadConfigKey(key), iv);
    else if (!strcmp(key, "latency"))
      setting_latency = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "driver"))
      setting_driver = ClampInt(iv, 0, 4);
    else if (!strcmp(key, "sync"))
      setting_sync = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "vsync"))
      setting_vsync = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "n64_core"))
      setting_n64_core = ClampInt(iv, 0, 2);
    else if (!strcmp(key, "osd_timeout"))
      setting_osd_timeout = ClampInt(iv, 0, kOsdTimeoutCount - 1);
    else if (!strcmp(key, "name_scroll"))
      setting_name_scroll = ClampInt(iv, 0, kScrollDelayCount - 1);
    else if (!strcmp(key, "hw_render"))
    {
      if (iv == 0) setting_driver = VIDEO_DRIVER_SOFTWARE;
    }
    else if (!strcmp(key, "sms_fm"))
      setting_sms_fm = ClampInt(iv, 0, 2);
    else if (!strcmp(key, "nds_layout"))
      setting_nds_layout = ClampInt(iv, 0, kNdsLayoutCount - 1);
    else if (!strcmp(key, "nds_gap"))
      setting_nds_gap = ClampInt(iv, 0, kNdsGapCount - 1);
    else if (!strcmp(key, "nds_hybrid"))
      setting_nds_hybrid = ClampInt(iv, 0, kNdsHybridCount - 1);
    else if (!strcmp(key, "citra_layout"))
      setting_citra_layout = ClampInt(iv, 0, kCitraLayoutCount - 1);
    else if (!strcmp(key, "cd_precache"))
      setting_cd_precache = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "cd_latency"))
      setting_cd_latency = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "volume"))
      CoreSetVolume(ClampInt(iv, 0, 100));
    else if (!strcmp(key, "mute"))
      CoreSetMute(iv != 0);
    else if (!strcmp(key, "neo_sys"))
      neo_sys = ClampInt(iv, 0, 2);
    else if (!strcmp(key, "neo_bios"))
      neo_bios = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "neo_cd_type"))
      neo_cd_type = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "neo_cd_region"))
      neo_cd_region = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "neo_memcard"))
      neo_memcard = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "neo_dip_settings"))
      neo_dip_settings = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "neo_dip_freeplay"))
      neo_dip_freeplay = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "voice_enabled"))
      VoiceChatSetEnabled(iv != 0);
    else if (!strcmp(key, "voice_denoise"))
      VoiceChatSetDenoiseEngine((VoiceDenoiseEngine)ClampInt(iv, 0, 2));
    else if (!strcmp(key, "voice_vad"))
      VoiceChatSetVadEnabled(iv != 0);
    else if (!strcmp(key, "voice_vad_sens"))
      VoiceChatSetVadSensitivity((VoiceVadSensitivity)ClampInt(iv, 0, 2));
    else if (!strcmp(key, "voice_aec"))
      VoiceChatSetAecEnabled(iv != 0);
    else if (!strcmp(key, "voice_agc"))
      VoiceChatSetAgcEnabled(iv != 0);
    else if (!strcmp(key, "tray_enabled"))
      TrayManagerSetEnabled(iv != 0);
    else if (!strcmp(key, "hidden_systems")) {
      std::lock_guard<std::mutex> lk(s_hidden_mutex);
      s_hidden_systems.clear();
      std::stringstream ss(val);
      std::string token;
      while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
          int hid = atoi(token.c_str());
          if (hid > 0) s_hidden_systems.insert(hid);
        }
      }
    }
    else if (!strcmp(key, "netplay_ip") && val[0])
      join_ip_input = val;
    else if (!strncmp(key, "coreopt:", 8) && key[8] && val[0])
      // See g_persisted_core_options's own comment - key/val are raw
      // strings here, not the atoi()'d iv used by every setting above.
      g_persisted_core_options[key + 8] = val;
  }
  fclose(f);

  // Migrate now, not "whenever the player next changes something". Without
  // this the new file is only created on the first MenuSaveSettings(), so a
  // player who upgrades and never touches a setting keeps running off the old
  // filename indefinitely - and the two files then silently diverge the moment
  // they do change one. Writing immediately makes the rename a one-launch
  // event. The old file is deliberately left on disk: it costs nothing, and it
  // is the way back if the player downgrades.
  if (from_legacy)
    MenuSaveSettings();
}

static std::string CurrentBindingsSnapshot() {
  std::string out;
  for (int i = 0; i < BIND_COUNT; i++)
    out += std::to_string(InputBindGetKey(i)) + "," + std::to_string(InputBindGetPad(i)) + ";";
  return out;
}

void MenuProcessKey(MenuKey key) {
  std::string before = SettingsSnapshot();
  std::string binds_before = CurrentBindingsSnapshot();
  const char *profile_before = InputBindActiveProfile();
  std::string profile_name = profile_before ? profile_before : "";
  MenuProcessKeyImpl(key);
  if (SettingsSnapshot() != before)
    MenuSaveSettings();
  // karamelo.cfg only holds the global set, so a rebind made while a system
  // profile is swapped in is written back to that profile instead of being
  // lost when the game closes.
  const char *profile_after = InputBindActiveProfile();
  if (profile_after && profile_name == profile_after &&
      CurrentBindingsSnapshot() != binds_before)
    InputBindSaveSystemProfile(profile_after);
}

void MenuJumpToLetter(char c) {
  if (!OsdIsEnabled() || items.empty())
    return;
  if (capture_bind >= 0)
    return;

  c = (char)toupper((unsigned char)c);
  if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
    return;

  int total = (int)items.size();
  if (total <= 0)
    return;

  int start = (selected_idx + 1) % total;
  for (int step = 0; step < total; ++step) {
    int idx = (start + step) % total;
    const auto &it = items[idx];
    if (it.label.empty() || it.label == " " || it.label == "<..>")
      continue;

    // Skip favorite prefix "* " if present
    size_t offset = 0;
    if (it.label.rfind("* ", 0) == 0)
      offset = 2;

    // Skip leading punctuation, brackets or spaces (e.g. "[SNES] Mario", "(USA) Zelda")
    while (offset < it.label.size() &&
           (it.label[offset] == ' ' || it.label[offset] == '[' ||
            it.label[offset] == '(' || it.label[offset] == '_' ||
            it.label[offset] == '-' || it.label[offset] == '{')) {
      offset++;
    }
    if (offset >= it.label.size())
      continue;

    char first = (char)toupper((unsigned char)it.label[offset]);
    if (first == c) {
      selected_idx = idx;
      int max_visible = OsdGetSize();
      if (max_visible <= 0)
        max_visible = 8;
      // Position the matched item nicely near the top with surrounding context
      scroll_top = ClampInt(idx - 1, 0, total > max_visible ? total - max_visible : 0);
      g_last_input_tick = TickCountMs();
      s_scroll_since = TickCountMs();
      s_scroll_item_idx = -1;
      return;
    }
  }
}

void MenuInit() {
  CloudSyncInit();
  WebAdminInit();
  WebAdminHooks web_hooks{};
  web_hooks.process_key = [](int k) {
    MenuEnqueueTask([k]() {
      MenuProcessKey((MenuKey)k);
    });
  };
  web_hooks.launch_game = [](const std::string& p) {
    MenuEnqueueTask([p]() {
      MenuLaunchGamePath(p);
    });
    return true;
  };
  web_hooks.is_running = CoreIsRunning;
  web_hooks.get_game_name = CoreGetGameName;
  web_hooks.get_core_name = CoreGetCoreName;
  web_hooks.reset = []() {
    MenuEnqueueTask([]() {
      CoreReset();
    });
  };
  web_hooks.save_state = [](int slot) -> bool {
    MenuEnqueueTask([slot]() {
      CoreSaveState(slot);
    });
    return true;
  };
  web_hooks.load_state = [](int slot) -> bool {
    MenuEnqueueTask([slot]() {
      CoreLoadState(slot);
    });
    return true;
  };
  web_hooks.screenshot = []() -> bool {
    MenuEnqueueTask([]() {
      CoreTakeScreenshot();
    });
    return true;
  };
  web_hooks.get_volume = CoreGetVolume;
  web_hooks.set_volume = [](int v) {
    MenuEnqueueTask([v]() {
      CoreSetVolume(v);
    });
  };
  web_hooks.get_mute = CoreGetMute;
  web_hooks.set_mute = [](bool m) {
    MenuEnqueueTask([m]() {
      CoreSetMute(m);
    });
  };
  web_hooks.get_recents = MenuGetRecent;

  web_hooks.get_aspect = MenuGetAspectMode;
  web_hooks.set_aspect = [](int a) {
    MenuEnqueueTask([a]() {
      MenuSetAspectMode(a);
    });
  };
  web_hooks.get_filter = MenuGetFilterMode;
  web_hooks.set_filter = [](int f) {
    MenuEnqueueTask([f]() {
      MenuSetFilterMode(f);
    });
  };
  web_hooks.get_vsync = MenuGetVsync;
  web_hooks.set_vsync = [](int v) {
    MenuEnqueueTask([v]() {
      MenuSetVsync(v);
    });
  };
  web_hooks.get_slot = CoreGetSelectedSlot;
  web_hooks.set_slot = [](int s) {
    MenuEnqueueTask([s]() {
      CoreSetSelectedSlot(s);
    });
  };
  web_hooks.save_settings = []() {
    MenuEnqueueTask([]() {
      MenuSaveSettingsExport();
    });
  };

  web_hooks.ra_get_user = RaGetUserName;
  web_hooks.ra_get_status = RaGetStatus;
  web_hooks.ra_get_score = RaGetScore;
  web_hooks.ra_get_hardcore = RaGetHardcore;
  web_hooks.ra_has_token = RaHasToken;
  web_hooks.ra_is_logged_in = RaIsLoggedIn;
  web_hooks.ra_is_enabled = RaIsEnabled;
  web_hooks.ra_get_ach_total = RaGetAchievementCount;
  web_hooks.ra_get_ach_unlocked = RaGetAchievementsUnlocked;
  web_hooks.ra_set_credentials = RaSetCredentials;
  web_hooks.ra_update = RaUpdate;
  web_hooks.ra_get_game_title = RaGetGameTitle;
  web_hooks.ra_get_achievements = RaGetAchievements;
  web_hooks.is_system_visible = MenuIsSystemVisible;
  web_hooks.set_system_visible = MenuSetSystemVisible;
  web_hooks.set_all_systems_visible = MenuSetAllSystemsVisible;
  web_hooks.hide_empty_systems = MenuHideEmptySystems;
  web_hooks.hide_category = MenuHideCategory;

  WebAdminRegisterHooks(web_hooks);
  VoiceChatInit();
  DesktopChatInit();
  MenuLoadSettings();
  LoadFavoritesAndRecent();
  NetplayInit();
  StarsInit(640, 360);
  current_state = STATE_MAIN;
  PopulateMainMenu();
}

void MenuSetStatus(const char *status) {
  if (status)
    status_msg = status;
}

void MenuRun() {
  NetplayUpdate();
  VoiceChatSyncWithNetplay(NetplayGetState() == NETPLAY_CONNECTED, NetplayGetPeerIp());
  RaUpdate();

  // Process tasks enqueued from background threads (Web Studio LAN, RPC, etc.)
  {
    std::vector<std::function<void()>> tasks;
    {
      std::lock_guard<std::mutex> lk(s_menu_task_mutex);
      tasks.swap(s_menu_task_queue);
    }
    for (auto &t : tasks) {
      if (t) t();
    }
  }

  if (s_systems_dirty.exchange(false)) {
    MenuSaveSettings();
    int keep = selected_idx;
    if (current_state == STATE_MANAGE_SYSTEMS) {
      PopulateManageSystems();
      selected_idx = keep;
    } else if (current_state == STATE_SYSTEMS ||
               (current_state == STATE_MAIN && !CoreIsRunning())) {
      PopulateSystemsMenu();
      selected_idx = keep;
    }
    if (!items.empty())
      selected_idx = ClampInt(selected_idx, 0, (int)items.size() - 1);
  }

  bool core_active_now = CoreIsRunning() || CoreIsLoading();
  static bool s_was_core_active = false;
  if (s_was_core_active && !core_active_now) {
    current_state = STATE_MAIN;
    PopulateMainMenu();
    selected_idx = 0;
    scroll_top = 0;
    OsdEnable(0);
    InputBindRestoreGlobal();
  }
  s_was_core_active = core_active_now;

  // A system's remap profile follows the core that is actually running. It
  // used to be picked right after CoreRequestLoad(), which only queues the
  // load - CoreGetCoreName() still named the PREVIOUS core there, so a PSX
  // game got the SNES profile, and a system without one kept whatever the
  // last profile had swapped in.
  static bool s_was_core_running = false;
  bool core_running_now = CoreIsRunning();
  if (core_running_now && !s_was_core_running) {
    InputBindRestoreGlobal();
    std::string sys_name = CoreGetCoreName();
    if (!sys_name.empty() && InputBindHasSystemProfile(sys_name.c_str()))
      InputBindLoadSystemProfile(sys_name.c_str());
  }
  s_was_core_running = core_running_now;

  // Close the OSD after a spell of no input, so it does not sit over the game
  // forever if the player wandered off. Only while a game is actually running:
  // hiding it on the main menu would leave nothing on screen at all.
  if (OsdIsEnabled() && CoreIsRunning() && capture_bind < 0) {
    int secs = kOsdTimeouts[setting_osd_timeout];
    if (secs > 0) {
      if (g_last_input_tick == 0)
        g_last_input_tick = TickCountMs();
      if (TickCountMs() - g_last_input_tick >= (uint32_t)secs * 1000) {
        OsdDisable();
        g_last_input_tick = TickCountMs();
      }
    }
  }

  // Key / Button capture for "Define Buttons". Runs here rather than in the key
  // handler because binding needs the raw input code, and by the time
  // a press reaches MenuProcessKey it has been translated to a MenuKey.
  if (capture_bind >= 0) {
    // Arm only once every key and button is up, otherwise the same press that
    // opened the capture binds itself instantly.
    if (!capture_armed) {
      if (InputCaptureIsAllReleased()) {
        InputCaptureFlush();
        capture_armed = true;
      }
    } else {
      int vk = InputCaptureScanKey();
      int pad = InputCaptureScanPad();

      if (vk == VK_ESCAPE) {
        wizard_active = false;
        capture_bind = -1;
        PopulateControllerSettings();
        CoreSetToast(Tr("CONFIGURACAO CANCELADA", "SETUP CANCELLED"), 90);
      } else if (pad != 0) {
        int target = capture_bind;
        InputBindSetPad(target, pad);
        setting_pad_device = 0;
        if (wizard_active) {
          wizard_step++;
          int max_wizard_steps = 16;
          if (wizard_step < max_wizard_steps) {
            capture_bind = wizard_step;
            capture_armed = false;
            char toast_buf[64];
            snprintf(toast_buf, sizeof(toast_buf), "[%d/%d] %s: %s",
                     wizard_step + 1, max_wizard_steps, Tr("PRESSIONE", "PRESS"), InputBindLabel(wizard_step));
            CoreSetToast(toast_buf, 120);
            PopulateControllerSettings();
            SelectBindRow(wizard_step);
          } else {
            wizard_active = false;
            capture_bind = -1;
            PopulateControllerSettings();
            SaveBindings();
            CoreSetToast(Tr("CONTROLE MAPEADO COM SUCESSO!", "GAMEPAD MAPPED SUCCESSFULLY!"), 150);
          }
        } else {
          capture_bind = -1;
          PopulateControllerSettings();
          SelectBindRow(target);
          SaveBindings();
        }
      } else if (vk != 0) {
        int target = capture_bind;
        bool ok = InputBindSetKey(target, vk);
        setting_pad_device = 1;
        if (ok) {
          if (wizard_active) {
            wizard_step++;
            int max_wizard_steps = BIND_COUNT;
            if (wizard_step < max_wizard_steps) {
              capture_bind = wizard_step;
              capture_armed = false;
              char toast_buf[64];
              snprintf(toast_buf, sizeof(toast_buf), "[%d/%d] %s: %s",
                       wizard_step + 1, max_wizard_steps, Tr("PRESSIONE", "PRESS"), InputBindLabel(wizard_step));
              CoreSetToast(toast_buf, 120);
              PopulateControllerSettings();
              SelectBindRow(wizard_step);
            } else {
              wizard_active = false;
              capture_bind = -1;
              PopulateControllerSettings();
              SaveBindings();
              CoreSetToast(Tr("TECLADO MAPEADO COM SUCESSO!", "KEYBOARD MAPPED SUCCESSFULLY!"), 150);
            }
          } else {
            capture_bind = -1;
            PopulateControllerSettings();
            SelectBindRow(target);
            SaveBindings();
          }
        } else {
          CoreSetToast(Tr("ESSA TECLA E RESERVADA PARA O MENU", "THIS KEY IS RESERVED FOR THE MENU"), 120);
        }
      }
    }
  }

  if (!CoreIsRunning()) {
    StarsUpdate(640, 360);
  }

  if (current_state == STATE_UPDATE) {
    static UpdaterState s_last_updater_state = UPDATER_STATE_IDLE;
    static int s_last_updater_progress = -1;
    UpdaterState cur_s = UpdaterGetState();
    int cur_p = UpdaterGetProgress();
    if (cur_s != s_last_updater_state || cur_p != s_last_updater_progress) {
      s_last_updater_state = cur_s;
      s_last_updater_progress = cur_p;
      PopulateUpdate();
    }
  }

  if (!OsdIsEnabled()) {
    return;
  }

  // Safety: If OSD is enabled but items is empty, restore menu state immediately
  // so the player is never trapped staring at an empty card.
  if (items.empty()) {
    current_state = STATE_MAIN;
    PopulateMainMenu();
    selected_idx = 0;
    scroll_top = 0;
  }

  OsdClear();

  // The vertical title band fits ~14 rotated characters top to bottom before
  // OsdSetTitle's own bounds check silently drops the rest - marquee-scroll
  // it the same way the row names below scroll, instead of leaving longer
  // titles permanently cut off.
  const int kVisibleCharsVert = 14;
  if (current_title != s_title_scroll_last) {
    s_title_scroll_last = current_title;
    s_title_scroll_since = TickCountMs();
  }
  if (kScrollDelays[setting_name_scroll] > 0 &&
      (int)current_title.size() > kVisibleCharsVert) {
    uint32_t delay_ms = (uint32_t)kScrollDelays[setting_name_scroll] * 1000;
    uint32_t elapsed = TickCountMs() - s_title_scroll_since;
    if (elapsed > delay_ms) {
      int gap = kVisibleCharsVert / 2;
      int cycle = (int)current_title.size() - kVisibleCharsVert + gap;
      int step = (int)((elapsed - delay_ms) / 180) % cycle;
      std::string padded = current_title + std::string(gap, ' ');
      OsdSetTitle(padded.substr(step, kVisibleCharsVert).c_str(),
                  (current_state != STATE_MAIN) ? OSD_ARROW_LEFT : 0);
    } else {
      OsdSetTitle(current_title.c_str(),
                  (current_state != STATE_MAIN) ? OSD_ARROW_LEFT : 0);
    }
  } else {
    OsdSetTitle(current_title.c_str(),
                (current_state != STATE_MAIN) ? OSD_ARROW_LEFT : 0);
  }

  int max_visible = OsdGetSize();
  if (max_visible <= 0)
    max_visible = 8;

  // Same guard as in MenuProcessKeyImpl: a stale index from a longer list
  // would otherwise drag scroll_top past the end (or below zero).
  selected_idx = ClampInt(selected_idx, 0, (int)items.size() - 1);
  if (scroll_top < 0)
    scroll_top = 0;

  if (selected_idx < scroll_top) {
    scroll_top = selected_idx;
  }
  if (selected_idx >= scroll_top + max_visible) {
    scroll_top = selected_idx - max_visible + 1;
  }

  for (int i = 0; i < max_visible; i++) {
    int item_idx = scroll_top + i;

    char line_buf[256] = {0};

    // Past the end of the list we still write an empty line. The vertical
    // title is composed inside OsdWrite, byte 0 of every row, so breaking
    // out early left the rows below the last item with no title band - and
    // once the card became a fixed 16 rows, that erased most of the name.
    if (item_idx >= (int)items.size()) {
      OsdWrite((unsigned char)i, "", 0, 0, 0, 30, 0);
      continue;
    }

    // Drawn through OsdDisplayText so accented names keep their column width
    // (one letter per character, not two or three UTF-8 bytes).
    MenuItem item = items[item_idx];
    item.label = OsdDisplayText(item.label);
    item.value = OsdDisplayText(item.value);

    bool is_selected = (item_idx == selected_idx);

    if (item.label == " " || item.label.empty()) {
      line_buf[0] = '\0';
    } else if (!item.value.empty()) {
      if (item.value == ">" || item.value == "\x16" || item.value == "\x10") {
        snprintf(line_buf, sizeof(line_buf), " %-22.22s \x16",
                 item.label.c_str());
      } else {
        int val_len = (int)item.value.length();
        int max_lbl = 22 - val_len;
        if (max_lbl < 1)
          max_lbl = 1;
        snprintf(line_buf, sizeof(line_buf), " %-*.*s %s", max_lbl, max_lbl,
                 item.label.c_str(), item.value.c_str());
      }
    } else {
      if (item.is_folder) {
        snprintf(line_buf, sizeof(line_buf), " %-22.22s \x16",
                 item.label.c_str());
      } else {
        // The OSD row is a fixed pixel width - a name longer than it just got
        // silently clipped before, extension included, with no way to read
        // the rest. Marquee-scroll the SELECTED row once it has sat still for
        // setting_name_scroll seconds, instead of leaving it truncated.
        //
        // A largura vale para TODAS as linhas de nome, nao so para a que rola.
        // Sem ela o " %s" escrevia o nome inteiro: a linha do OSD tem 256
        // pixels e aceita cerca de 30 caracteres, entao um nome comprido ia
        // ate a borda do painel e era cortado no meio de uma letra, sem
        // margem - enquanto pasta e opcao, que usam %-22.22s, paravam bem
        // antes. E o texto encolhia de 30 para 26 no instante em que a
        // rolagem comecava. Agora e a mesma largura nos tres casos.
        const int kVisibleChars = 26;
        if (is_selected && kScrollDelays[setting_name_scroll] > 0 &&
            (int)item.label.size() > kVisibleChars) {
          if (item_idx != s_scroll_item_idx ||
              item.label != s_scroll_item_label) {
            s_scroll_item_idx = item_idx;
            s_scroll_item_label = item.label;
            s_scroll_since = TickCountMs();
          }
          uint32_t delay_ms = (uint32_t)kScrollDelays[setting_name_scroll] * 1000;
          uint32_t elapsed = TickCountMs() - s_scroll_since;
          if (elapsed > delay_ms) {
            // A blank gap half a screen wide between the tail and the loop
            // back to the start, so the seam reads as a pause, not a glitch.
            int gap = kVisibleChars / 2;
            int cycle = (int)item.label.size() - kVisibleChars + gap;
            int step = (int)((elapsed - delay_ms) / 180) % cycle;
            std::string padded = item.label + std::string(gap, ' ');
            snprintf(line_buf, sizeof(line_buf), " %s",
                     padded.substr(step, kVisibleChars).c_str());
          } else {
            snprintf(line_buf, sizeof(line_buf), " %-*.*s", kVisibleChars,
                     kVisibleChars, item.label.c_str());
          }
        } else {
          snprintf(line_buf, sizeof(line_buf), " %-*.*s", kVisibleChars,
                   kVisibleChars, item.label.c_str());
        }
      }
    }

    OsdWrite((unsigned char)i, line_buf, is_selected ? 1 : 0, 0, 0, 30, 0);
  }
}

static void MenuProcessKeyImpl(MenuKey key) {
  g_last_input_tick = TickCountMs();

  // While waiting for a key to bind, the menu must not also act on it,
  // except for KEY_CANCEL (ESC) which aborts the capture / wizard.
  if (capture_bind >= 0) {
    if (key == KEY_CANCEL) {
      wizard_active = false;
      capture_bind = -1;
      PopulateControllerSettings();
      CoreSetToast(Tr("CONFIGURACAO CANCELADA", "SETUP CANCELLED"), 90);
    }
    return;
  }

  if (key == KEY_MENU_TOGGLE) {
    if (OsdIsEnabled()) {
      OsdDisable();
    } else {
      OsdEnable(0);
      s_scroll_since = TickCountMs();
      s_title_scroll_since = TickCountMs();
      if (items.empty() || current_state == STATE_MAIN ||
          (CoreIsRunning() && (current_state == STATE_BROWSE || current_state == STATE_SYSTEMS))) {
        current_state = STATE_MAIN;
        PopulateMainMenu();
        selected_idx = 0;
        scroll_top = 0;
      }
    }
    return;
  }

  if (!OsdIsEnabled()) {
    return;
  }

  // Safety: If items is empty for any reason, repopulate immediately instead of silently dropping keys
  if (items.empty()) {
    current_state = STATE_MAIN;
    PopulateMainMenu();
    selected_idx = 0;
    scroll_top = 0;
  }

  int total = (int)items.size();
  if (total == 0)
    return;

  // Several screens restore the previous row by number after rebuilding the
  // list ("selected_idx = cur", a SelectByAction fallback). When the rebuilt
  // list came out shorter, the index pointed past its end, and the
  // items[selected_idx] copy in KEY_LEFT/KEY_RIGHT/KEY_SELECT read out of
  // bounds.
  selected_idx = ClampInt(selected_idx, 0, total - 1);

  switch (key) {
  case KEY_UP:
    if (total > 0) {
      int tries = 0;
      do {
        if (selected_idx > 0)
          selected_idx--;
        else
          selected_idx = total - 1;
        tries++;
      } while (selected_idx < total &&
               (items[selected_idx].label == " " ||
                (!items[selected_idx].is_action && items[selected_idx].action_id == 0 &&
                 current_state == STATE_RESET_CONFIRM)) &&
               tries < total);
    }
    break;

  case KEY_DOWN:
    if (total > 0) {
      int tries = 0;
      do {
        if (selected_idx < total - 1)
          selected_idx++;
        else
          selected_idx = 0;
        tries++;
      } while (selected_idx < total &&
               (items[selected_idx].label == " " ||
                (!items[selected_idx].is_action && items[selected_idx].action_id == 0 &&
                 current_state == STATE_RESET_CONFIRM)) &&
               tries < total);
    }
    break;

  case KEY_PAGEUP:
    selected_idx = (selected_idx > 5) ? selected_idx - 5 : 0;
    break;

  case KEY_PAGEDOWN:
    selected_idx = (selected_idx + 5 < total) ? selected_idx + 5 : total - 1;
    break;

  case KEY_HOME:
    if (total > 0) {
      selected_idx = 0;
      while (selected_idx < total && items[selected_idx].label == " ")
        selected_idx++;
      if (selected_idx >= total)
        selected_idx = 0;
    }
    break;

  case KEY_END:
    if (total > 0) {
      selected_idx = total - 1;
      while (selected_idx > 0 && items[selected_idx].label == " ")
        selected_idx--;
    }
    break;

  case KEY_LEFT:
  case KEY_RIGHT: {
    int delta = (key == KEY_RIGHT) ? 1 : -1;
    // A copy, not a reference: several branches below call PopulateXxx(),
    // which clear()s/rebuilds `items` and can reallocate its backing storage -
    // a reference held across that call would dangle for any code reading it
    // afterward.
    const auto item = items[selected_idx];

    if (item.action_id == 301) // Aspect
    {
      int cur = selected_idx;
      setting_aspect = (setting_aspect + delta + 3) % 3;
      if (CoreIsRunning() && current_state == STATE_MAIN)
        PopulateMainMenu();
      else
        PopulateVideoSettings();
      selected_idx = cur;
    } else if (item.action_id == 5) // Slot de Save
    {
      int cur = selected_idx;
      int s = (CoreGetSelectedSlot() + delta + 10) % 10;
      CoreSetSelectedSlot(s);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (current_state == STATE_MANAGE_SYSTEMS && item.action_id >= 8100 && item.action_id <= 8200) {
      int sys_id = item.action_id - 8000;
      MenuSetSystemVisible(sys_id, !MenuIsSystemVisible(sys_id));
      PopulateManageSystems();
      SelectByAction(item.action_id, 0);
    } else if (current_state == STATE_SAVESTATES && item.action_id >= 850 && item.action_id <= 859) {
      int s = item.action_id - 850;
      CoreSetSelectedSlot(s);
      PopulateSaveStates();
    } else if (item.action_id == 302) // CRT Shaders
    {
      int cur = selected_idx;
      setting_filter =
          kFilters[(FilterIndex(setting_filter) + delta + kFilterCount) %
                   kFilterCount]
              .mode;
      char msg[64];
      snprintf(msg, sizeof(msg), "SHADER: %s", FilterLabel(setting_filter));
      CoreSetToast(msg, 90);
      if (CoreIsRunning() && current_state == STATE_MAIN)
        PopulateMainMenu();
      else
        PopulateVideoSettings();
      selected_idx = cur;
    } else if (item.action_id == 303) // Wallpaper
    {
      int count = WallpaperMaxMode() + 1;
      if (count > 0)
        setting_wallpaper = (setting_wallpaper + delta + count) % count;
      PopulateVideoSettings();
      SelectByAction(item.action_id, 2);
    } else if (item.action_id == 304) // Fullscreen
    {
      setting_fullscreen = !setting_fullscreen;
      PopulateVideoSettings();
      SelectByAction(item.action_id, 3);
    } else if (item.action_id == 305) // OSD Color
    {
      setting_theme = (setting_theme + delta + 6) % 6;
      PopulateVideoSettings();
      SelectByAction(item.action_id, 4);
    } else if (item.action_id == 509) // Master System FM
    {
      int cur = selected_idx;
      setting_sms_fm = (setting_sms_fm + delta + 3) % 3;
      const char *gpgx[] = {"auto", "disabled", "enabled"};
      const char *gears[] = {"Auto", "Disabled", "Auto"};
      CoreSetOption("genesis_plus_gx_ym2413", gpgx[setting_sms_fm]);
      CoreSetOption("gearsystem_ym2413", gears[setting_sms_fm]);

      const char *names[] = {"AUTO", "DESLIGADO", "LIGADO"};
      char msg[64];
      snprintf(msg, sizeof(msg), Tr("FM AUDIO: %s (recarregue o jogo)", "FM AUDIO: %s (reload the game)"),
               names[setting_sms_fm]);
      CoreSetToast(msg, 120);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 510) // DS screen layout
    {
      int cur = selected_idx;
      setting_nds_layout =
          (setting_nds_layout + delta + kNdsLayoutCount) % kNdsLayoutCount;
      CoreSetOption("melonds_screen_layout",
                    kNdsLayoutVals[setting_nds_layout]);
      char msg[64];
      snprintf(msg, sizeof(msg), "LAYOUT: %s",
               kNdsLayoutNames[setting_nds_layout]);
      CoreSetToast(msg, 120);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 511) // DS screen gap
    {
      int cur = selected_idx;
      setting_nds_gap = (setting_nds_gap + delta + kNdsGapCount) % kNdsGapCount;
      CoreSetOption("melonds_screen_gap", kNdsGapVals[setting_nds_gap]);
      char msg[64];
      snprintf(msg, sizeof(msg), Tr("ESPACO: %s", "GAP: %s"), kNdsGapVals[setting_nds_gap]);
      CoreSetToast(msg, 120);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 512) // DS hybrid small screen
    {
      int cur = selected_idx;
      setting_nds_hybrid =
          (setting_nds_hybrid + delta + kNdsHybridCount) % kNdsHybridCount;
      CoreSetOption("melonds_hybrid_small_screen",
                    kNdsHybridVals[setting_nds_hybrid]);
      char msg[64];
      snprintf(msg, sizeof(msg), Tr("TELA PEQUENA: %s", "SMALL SCREEN: %s"),
               kNdsHybridNames[setting_nds_hybrid]);
      CoreSetToast(msg, 120);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 515) // 3DS screen layout
    {
      int cur = selected_idx;
      setting_citra_layout =
          (setting_citra_layout + delta + kCitraLayoutCount) % kCitraLayoutCount;
      CoreSetOption("citra_layout_option", kCitraLayoutVals[setting_citra_layout]);
      char msg[64];
      snprintf(msg, sizeof(msg), "LAYOUT: %s",
               kCitraLayoutNames[setting_citra_layout]);
      CoreSetToast(msg, 120);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 513) // Mega CD image cache
    {
      int cur = selected_idx;
      setting_cd_precache = (setting_cd_precache + delta + 2) % 2;
      CoreSetOption("genesis_plus_gx_cd_precache",
                    setting_cd_precache ? "enabled" : "disabled");
      CoreSetToast(setting_cd_precache
                       ? Tr("CACHE DO CD: LIGADO (recarregue o jogo)", "CD PRECACHE: ON (reload game)")
                       : Tr("CACHE DO CD: DESLIGADO", "CD PRECACHE: OFF"),
                   180);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 514) // Mega CD access time
    {
      int cur = selected_idx;
      setting_cd_latency = (setting_cd_latency + delta + 2) % 2;
      CoreSetOption("genesis_plus_gx_cd_latency",
                    setting_cd_latency ? "disabled" : "enabled");
      // The core warns a few games crash when CD data arrives too early,
      // so this is offered, not defaulted.
      CoreSetToast(setting_cd_latency
                       ? Tr("ACESSO DO CD: RAPIDO (pode falhar)", "CD ACCESS: FAST (may fail in games)")
                       : Tr("ACESSO DO CD: REAL", "CD ACCESS: REAL"),
                   200);
      PopulateMainMenu();
      selected_idx = cur;
    } else if (item.action_id == 309) // N64 core
    {
      setting_n64_core = (setting_n64_core + delta + 3) % 3;
      const char *names[] = {"PARALLEL-N64", "MUPEN64PLUS",
                             "GOPHER64 (VULKAN)"};
      char msg[80];
      snprintf(msg, sizeof(msg), "N64: %s%s", names[setting_n64_core],
               (setting_n64_core < 2 && !HwIsAvailable()) ? " (NO GL!)" : "");
      CoreSetToast(msg, 150);
      PopulateVideoSettings();
      SelectByAction(item.action_id, 6);
    } else if (item.action_id == 312) // OSD auto-hide
    {
      setting_osd_timeout =
          (setting_osd_timeout + delta + kOsdTimeoutCount) % kOsdTimeoutCount;
      char msg[64];
      if (kOsdTimeouts[setting_osd_timeout] == 0)
        snprintf(msg, sizeof(msg), "%s", Tr("MENU: NAO OCULTA SOZINHO", "MENU: NEVER HIDES"));
      else
        snprintf(msg, sizeof(msg), "%s %ds %s", Tr("MENU OCULTA APOS", "MENU HIDES AFTER"),
                 kOsdTimeouts[setting_osd_timeout], Tr("SEM USO", "IDLE"));
      CoreSetToast(msg, 150);
      PopulateVideoSettings();
      SelectByAction(item.action_id, 7);
    } else if (item.action_id == 313) // Name scroll delay
    {
      setting_name_scroll =
          (setting_name_scroll + delta + kScrollDelayCount) % kScrollDelayCount;
      char msg[64];
      if (kScrollDelays[setting_name_scroll] == 0)
        snprintf(msg, sizeof(msg), "%s", Tr("ROLAGEM DE NOME: DESATIVADA", "NAME SCROLL: DISABLED"));
      else
        snprintf(msg, sizeof(msg), "%s %ds %s", Tr("ROLAGEM DE NOME APOS", "NAME SCROLL AFTER"),
                 kScrollDelays[setting_name_scroll], Tr("PARADO", "IDLE"));
      CoreSetToast(msg, 150);
      PopulateVideoSettings();
      SelectByAction(item.action_id, 11);
    } else if (item.action_id == 308) // V-Sync
    {
      setting_vsync = (setting_vsync + delta + 2) % 2;
      CoreSetToast(setting_vsync == 1 ? Tr("V-SYNC: ATIVADO", "V-SYNC: ENABLED")
                                      : Tr("V-SYNC: DESATIVADO", "V-SYNC: DISABLED"),
                   120);
      PopulateVideoSettings();
      SelectByAction(item.action_id, 8);
    } else if (item.action_id == 307) // Sincronia
    {
      setting_sync = (setting_sync + delta + 2) % 2;
      CoreSetDisplaySync(setting_sync == 1, CoreGetDisplayFps());
      CoreSetToast(setting_sync == 1 ? Tr("SINCRONIZACAO: MONITOR", "SYNC: DISPLAY")
                                     : Tr("SINCRONIZACAO: NATIVO", "SYNC: NATIVE"), 120);
      PopulateVideoSettings();
      SelectByAction(item.action_id, 9);
    } else if (item.action_id == 306) // Video Driver
    {
      CycleDriver(delta);
      char msg[64];
      if (setting_driver == VIDEO_DRIVER_SOFTWARE) {
        snprintf(msg, sizeof(msg), "DRIVER: SOFTWARE (CPU)");
      } else if (setting_driver == VIDEO_DRIVER_AUTO) {
        snprintf(msg, sizeof(msg), "%s", Tr("DRIVER: AUTO (DETECTA GPU CONFORME O JOGO)", "DRIVER: AUTO (PICKS THE GPU PER GAME)"));
      } else {
        snprintf(msg, sizeof(msg), Tr("DRIVER: %s (3D GPU ATIVADO)", "DRIVER: %s (3D GPU ENABLED)"), GetCurrentDriverToast());
      }
      CoreSetToast(msg, 90);
      PopulateVideoSettings();
      SelectByAction(item.action_id, 5);
    } else if (item.action_id == 401) // Mute
    {
      CoreSetMute(!CoreGetMute());
      PopulateAudioSettings();
      SelectByAction(item.action_id, 0);
    } else if (item.action_id == 402) // Latency
    {
      setting_latency = (setting_latency + delta + 4) % 4;
      PopulateAudioSettings();
      SelectByAction(item.action_id, 1);
    } else if (item.action_id == 403) // Volume
    {
      int v = std::clamp(CoreGetVolume() + delta * 5, 0, 100);
      CoreSetVolume(v);
      PopulateAudioSettings();
      SelectByAction(item.action_id, 2);
    } else if (item.action_id == 930) // Voice Chat P2P Toggle
    {
      VoiceChatSetEnabled(!VoiceChatIsEnabled());
      PopulateAudioSettings();
      SelectByAction(item.action_id, 4);
    } else if (item.action_id == 931) // Mic Mute
    {
      VoiceChatToggleMicMute();
      PopulateAudioSettings();
      SelectByAction(item.action_id, 5);
    } else if (item.action_id == 932) // AI Denoise Engine
    {
      VoiceChatSetDenoiseEngine((VoiceDenoiseEngine)(((int)VoiceChatGetDenoiseEngine() + delta + 3) % 3));
      PopulateAudioSettings();
      SelectByAction(item.action_id, 6);
    } else if (item.action_id == 933) // Corte de silencio
    {
      VoiceChatSetVadEnabled(!VoiceChatGetVadEnabled());
      PopulateAudioSettings();
      SelectByAction(item.action_id, 7);
    } else if (item.action_id == 934) // VAD Sensitivity
    {
      VoiceChatSetVadSensitivity((VoiceVadSensitivity)(((int)VoiceChatGetVadSensitivity() + delta + 3) % 3));
      PopulateAudioSettings();
      SelectByAction(item.action_id, 8);
    } else if (item.action_id == 935) // Cancelar eco
    {
      VoiceChatSetAecEnabled(!VoiceChatGetAecEnabled());
      PopulateAudioSettings();
      SelectByAction(item.action_id, 9);
    } else if (item.action_id == 936) // Ganho automatico
    {
      VoiceChatSetAgcEnabled(!VoiceChatGetAgcEnabled());
      PopulateAudioSettings();
      SelectByAction(item.action_id, 10);
    } else if (item.action_id == 901) // Cloud Auto Sync
    {
      CloudSyncSetAutoSync(!CloudSyncGetAutoSync());
      PopulateCloudSettings();
      SelectByAction(item.action_id, 3);
    } else if (item.action_id == 910) // Web Admin Toggle
    {
      if (WebAdminIsRunning()) WebAdminStop();
      else WebAdminStart(8080, false);
      PopulateWebAdminSettings();
      SelectByAction(item.action_id, 0);
    } else if (item.action_id == 920) // Live Chat Room
    {
      DesktopChatSetRoom((LiveChatRoom)(((int)DesktopChatGetRoom() + delta + 4) % 4));
      PopulateLiveChatSettings();
      SelectByAction(item.action_id, 0);
    } else if (item.action_id == 921) // Presence Status
    {
      TrayManagerSetStatus((TrayPresenceStatus)(((int)TrayManagerGetStatus() + delta + 3) % 3));
      PopulateLiveChatSettings();
      SelectByAction(item.action_id, 1);
    } else if (item.action_id == 922) // System Tray Toggle
    {
      TrayManagerSetEnabled(!TrayManagerIsEnabled());
      PopulateLiveChatSettings();
      SelectByAction(item.action_id, 2);
    } else if (item.action_id == 924) // In-Game Overlay Toggle
    {
      DesktopChatToggleOverlay();
      PopulateLiveChatSettings();
      SelectByAction(item.action_id, 3);
    } else if (item.action_id == 501) // System Type
    {
      neo_sys = (neo_sys + delta + 3) % 3;
      // Geolith accepts aes | mvs | uni. "cdz" is not a system type and
      // left the core undefined - this is the Neo Geo CD black screen.
      const char *sys_vals[] = {"aes", "mvs", "uni"};
      CoreSetOption("geolith_system_type", sys_vals[neo_sys]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 2);
    } else if (item.action_id == 502) // BIOS
    {
      neo_bios = (neo_bios + delta + 2) % 2;
      // Tells the Universe BIOS which hardware to detect.
      const char *bios_vals[] = {"aes", "mvs"};
      CoreSetOption("geolith_unibios_hw", bios_vals[neo_bios]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 3);
    } else if (item.action_id == 503) // CD Type
    {
      neo_cd_type = (neo_cd_type + delta + 4) % 4;
      const char *cd_vals[] = {"cdz", "cd_top", "cd_front", "cdz_unibios"};
      CoreSetOption("geolith_cd_system_type", cd_vals[neo_cd_type]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 4);
    } else if (item.action_id == 504) // CD Region
    {
      neo_cd_region = (neo_cd_region + delta + 4) % 4;
      const char *reg_vals[] = {"us", "jp", "as", "eu"};
      CoreSetOption("geolith_region", reg_vals[neo_cd_region]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 5);
    } else if (item.action_id == 505) // Memory Card
    {
      neo_memcard = (neo_memcard + delta + 2) % 2;
      const char *mc_vals[] = {"on", "off"};
      CoreSetOption("geolith_memcard", mc_vals[neo_memcard]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 6);
    } else if (item.action_id == 506) // [DIP] Settings
    {
      neo_dip_settings = (neo_dip_settings + delta + 2) % 2;
      const char *dip_vals[] = {"off", "on"};
      CoreSetOption("geolith_settingmode", dip_vals[neo_dip_settings]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 7);
    } else if (item.action_id == 507) // [DIP] Freeplay
    {
      neo_dip_freeplay = (neo_dip_freeplay + delta + 2) % 2;
      const char *fp_vals[] = {"off", "on"};
      CoreSetOption("geolith_freeplay", fp_vals[neo_dip_freeplay]);
      PopulateMainMenu();
      SelectByAction(item.action_id, 8);
    } else if (item.action_id == 500) // Dispositivo (Gamepad vs Teclado)
    {
      setting_pad_device = (setting_pad_device + delta + 2) % 2;
      PopulateControllerSettings();
      SelectByAction(item.action_id, 1);
    } else if (item.action_id == 508) // Deadzone
    {
      setting_deadzone = (setting_deadzone + delta + 4) % 4;
      PopulateControllerSettings();
      SelectByAction(item.action_id, 2);
    } else if (item.action_id == 541) // Controle como Mouse
    {
      setting_pad_mouse = !setting_pad_mouse;
      PopulateControllerSettings();
      SelectByAction(item.action_id, 3);
    } else if (item.action_id == 216) // Idioma
    {
      setting_language = (setting_language + delta + 2) % 2;
      MenuSetLanguage(setting_language);
      PopulateSettings();
      SelectByAction(item.action_id, 0);
    } else if (item.action_id == 530) // Preset Botoes
    {
      int cur_preset = InputBindGetPreset();
      int next_preset = (cur_preset + delta + 4) % 4;
      InputBindApplyPreset(next_preset);
      MenuSaveSettings();
      PopulateControllerSettings();
      SelectByAction(item.action_id, 2);
    } else if (item.action_id == 520) // Toggle crash reporting
    {
      int cur = selected_idx;
      CrashReporterSetEnabled(!CrashReporterIsEnabled());
      PopulateDiagnostics();
      selected_idx = cur;
    } else if (item.action_id >= 800 && item.action_id < 800 + kCoreOptionsMaxRows) // Core Options
    {
      int opt_index = item.action_id - 800;
      int choice_count = CoreOptionChoiceCount(opt_index);
      if (choice_count > 0) {
        int cur = CoreOptionCurrentChoiceIndex(opt_index);
        int next = (cur + delta + choice_count) % choice_count;
        CoreOptionSetChoiceIndex(opt_index, next);
        // Persist so this choice survives a restart - without this, the
        // very bug ApplyPersistedCoreOptions() exists to avoid for the
        // hand-picked settings would apply here too: the value would look
        // chosen but silently revert to the core's own default next launch.
        MenuSetPersistedCoreOption(CoreOptionKey(opt_index), CoreOptionChoiceAt(opt_index, next));
      }
      int cur_row = selected_idx;
      PopulateCoreOptionsSettings();
      selected_idx = cur_row;
    }
    break;
  }

  case KEY_SELECT: {
    // A copy, not a reference - see the identical note on KEY_LEFT/KEY_RIGHT
    // above: branches below rebuild `items` via PopulateXxx(), which can
    // reallocate it and leave a reference dangling.
    const auto item = items[selected_idx];

    if (item.action_id == 99) // Exit
    {
      MenuRequestQuit();
    } else if (item.action_id == 997) // Back from Cores List -> About
    {
      current_state = STATE_ABOUT;
      PopulateAbout();
      SelectByAction(209, 6); // linha "Cores" na About, de onde se veio
    } else if (item.action_id == 999) // Back
    {
      current_state = STATE_SETTINGS;
      PopulateSettings();
    } else if (item.action_id == 898) // Back (Core Options -> in-game menu, not Settings)
    {
      current_state = STATE_MAIN;
      PopulateMainMenu();
    } else if (current_state == STATE_MAIN || current_state == STATE_SYSTEMS) {
      if (item.action_id == 195) // Browse Systems
      {
        current_state = STATE_SYSTEMS;
        PopulateSystemsMenu();
        selected_idx = 0;
        scroll_top = 0;
      } else if (item.action_id == 196) // Back to Core
      {
        current_state = STATE_MAIN;
        PopulateMainMenu();
        selected_idx = 0;
        scroll_top = 0;
      } else if (item.action_id == 1) // Load another game from the same folder
      {
        // The row is labelled "Load" and shows the game name, but it used to
        // just close the OSD - it was Resume wearing Load's label. It now
        // reopens the folder the current game came from, so another one can
        // be picked without closing the core first.
        std::string dir = CoreGetRomDir();
        std::error_code ec;
        if (!dir.empty() && fs::is_directory(dir, ec)) {
          current_state = STATE_BROWSE;
          PopulateBrowse(dir);
        } else {
          OsdDisable();
        }
      } else if (item.action_id == 2) // Save State
      {
        CoreSaveState(CoreGetSelectedSlot());
        PopulateMainMenu();
      } else if (item.action_id == 3) // Load State
      {
        CoreLoadState(CoreGetSelectedSlot());
        OsdDisable();
      } else if (item.action_id == 4) // Gerenciador Slots
      {
        current_state = STATE_SAVESTATES;
        PopulateSaveStates();
      } else if (item.action_id == 302) // CRT Shaders
      {
        shader_list_previous_state = STATE_MAIN;
        current_state = STATE_SHADER_LIST;
        PopulateShaderList();
      } else if (item.action_id == 5) // Slot de Save: Enter avanca para o proximo slot
      {
        int cur = selected_idx;
        int s = (CoreGetSelectedSlot() + 1) % 10;
        CoreSetSelectedSlot(s);
        PopulateMainMenu();
        selected_idx = cur;
      } else if (item.action_id == 8) // Screenshot
      {
        CoreTakeScreenshot();
        OsdDisable();
      } else if (item.action_id == 6) // Reset Core
      {
        CoreReset();
        OsdDisable();
        items.clear();
      } else if (item.action_id == 207) // Achievement list
      {
        current_state = STATE_ABOUT;
        PopulateAchievementList();
      } else if (item.action_id == 7) // Close Game
      {
        CoreShutdown();
        PopulateMainMenu();
      } else if (item.action_id == 20) // Settings
      {
        main_menu_saved_idx = selected_idx;
        current_state = STATE_SETTINGS;
        PopulateSettings();
      } else if (item.action_id == 30) // Update
      {
        main_menu_saved_idx = selected_idx;
        current_state = STATE_UPDATE;
        selected_idx = 0;
        scroll_top = 0;
        UpdaterState s = UpdaterGetState();
        if (s == UPDATER_STATE_IDLE || s == UPDATER_STATE_ERROR) {
          UpdaterCheckAsync(true);
        }
        PopulateUpdate();
      } else if (item.action_id >= 101 && item.action_id <= 137) {
        main_menu_saved_idx = selected_idx;
        if (item.action_id == 101) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Atari2600");
        } else if (item.action_id == 102) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Genesis");
        } else if (item.action_id == 103) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/MasterSystem");
        } else if (item.action_id == 104) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/MegaCD");
        } else if (item.action_id == 105) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/NES");
        } else if (item.action_id == 106) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/NeoGeo");
        } else if (item.action_id == 107) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Nintendo64");
        } else if (item.action_id == 108) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/PlayStation");
        } else if (item.action_id == 109) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/SNES");
        } else if (item.action_id == 110) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Saturn");
        } else if (item.action_id == 111) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/TurboGrafx16");
        } else if (item.action_id == 112) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Dreamcast");
        } else if (item.action_id == 113) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/GameCube");
        } else if (item.action_id == 114) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/NDS");
        } else if (item.action_id == 115) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/3DS");
        } else if (item.action_id == 116) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/PlayStation2");
        } else if (item.action_id == 117) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Arcade");
        } else if (item.action_id == 118) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/GBA");
        } else if (item.action_id == 119) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/GameBoy");
        } else if (item.action_id == 120) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/PSP");
        } else if (item.action_id == 121) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/DOS");
        } else if (item.action_id == 122) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/MSX");
        } else if (item.action_id == 123) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Amiga");
        } else if (item.action_id == 124) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/C64");
        } else if (item.action_id == 125) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/ZXSpectrum");
        } else if (item.action_id == 126) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/32X");
        } else if (item.action_id == 127) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/3DO");
        } else if (item.action_id == 128) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Jaguar");
        } else if (item.action_id == 129) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Atari5200");
        } else if (item.action_id == 130) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Atari7800");
        } else if (item.action_id == 131) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/ColecoVision");
        } else if (item.action_id == 132) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/NGP");
        } else if (item.action_id == 133) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/WonderSwan");
        } else if (item.action_id == 134) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/Lynx");
        } else if (item.action_id == 135) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/PCFX");
        } else if (item.action_id == 136) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/PC98");
        } else if (item.action_id == 137) {
          current_state = STATE_BROWSE;
          PopulateBrowse("roms/FMTowns");
        }
      }
      // 140 (Ports & Recomp) is not in the 101-135 system range above, so it
      // has to be its own sibling branch here - nested inside that block (as
      // it was before) it could never be reached, because the block's own
      // "item.action_id >= 101 && <= 135" guard is false for 140 and the
      // whole chain inside it, 140 included, never runs. Pressing Enter on
      // the menu entry did nothing at all, which is what this fixes.
      else if (item.action_id == 140) {
        main_menu_saved_idx = selected_idx;
        current_state = STATE_BROWSE;
        PopulateBrowse("ports");
      }
      else if (item.action_id == 190) {
        main_menu_saved_idx = selected_idx;
        current_state = STATE_FAVORITES;
        PopulateFavoritesMenu();
      }
      else if (item.action_id == 191) {
        main_menu_saved_idx = selected_idx;
        current_state = STATE_RECENT;
        PopulateRecentMenu();
      }
      else if (item.action_id == 225) {
        current_state = STATE_MANAGE_SYSTEMS;
        PopulateManageSystems();
        selected_idx = 0;
        scroll_top = 0;
      }
      // "Define Buttons" is on the in-game menu, but its handler only existed
      // under STATE_SETTINGS, so pressing it there did nothing at all.
      else if (item.action_id == 203) {
        current_state = STATE_CONTROLLER;
        ControllerPageOnEnter();
        PopulateControllerSettings();
      }
      else if (item.action_id == 221) {
        current_state = STATE_CORE_OPTIONS;
        PopulateCoreOptionsSettings();
      }
    } else if (current_state == STATE_SETTINGS) {
      if (item.action_id == 225) {
        current_state = STATE_MANAGE_SYSTEMS;
        PopulateManageSystems();
        selected_idx = 0;
        scroll_top = 0;
      } else if (item.action_id == 216) {
        setting_language = (setting_language == 0) ? 1 : 0;
        MenuSetLanguage(setting_language);
        PopulateSettings();
        SelectByAction(216, 0);
      } else if (item.action_id == 201) {
        current_state = STATE_VIDEO;
        PopulateVideoSettings();
      } else if (item.action_id == 202) {
        current_state = STATE_AUDIO;
        PopulateAudioSettings();
      } else if (item.action_id == 203) {
        current_state = STATE_CONTROLLER;
        ControllerPageOnEnter();
        PopulateControllerSettings();
      } else if (item.action_id == 204) {
        current_state = STATE_NETPLAY;
        PopulateNetplay();
      } else if (item.action_id == 217) {
        current_state = STATE_CLOUD;
        PopulateCloudSettings();
      } else if (item.action_id == 218) {
        current_state = STATE_LIVE_CHAT;
        PopulateLiveChatSettings();
      } else if (item.action_id == 219) {
        current_state = STATE_WEB_ADMIN;
        PopulateWebAdminSettings();
      } else if (item.action_id == 206) {
        current_state = STATE_ABOUT;
        PopulateRetroAchievements();
      } else if (item.action_id == 205) {
        current_state = STATE_ABOUT;
        PopulateAbout();
      } else if (item.action_id == 215) {
        current_state = STATE_DIAGNOSTICS;
        PopulateDiagnostics();
      } else if (item.action_id == 210) {
        current_state = STATE_RESET_CONFIRM;
        PopulateResetConfirm();
      }
    } else if (current_state == STATE_CLOUD) {
      if (item.action_id == 901) {
        CloudSyncSetAutoSync(!CloudSyncGetAutoSync());
        PopulateCloudSettings();
        SelectByAction(901, 3);
      } else if (item.action_id == 902) {
        CloudSyncTriggerAsync();
        PopulateCloudSettings();
        SelectByAction(902, 5);
      } else if (item.action_id == 903) {
        CloudSyncStartOAuthFlow();
        PopulateCloudSettings();
      } else if (item.action_id == 904) {
        CloudSyncDisconnect();
        PopulateCloudSettings();
      }
    } else if (current_state == STATE_MANAGE_SYSTEMS) {
      if (item.action_id == 1850) {
        MenuSetAllSystemsVisible(true);
        PopulateManageSystems();
        SelectByAction(1850, 0);
      } else if (item.action_id == 1851) {
        MenuHideEmptySystems();
        PopulateManageSystems();
        SelectByAction(1851, 1);
      } else if (item.action_id == 1852) {
        MenuHideCategory("Computadores");
        PopulateManageSystems();
        SelectByAction(1852, 2);
      } else if (item.action_id == 1853) {
        MenuSetAllSystemsVisible(false);
        PopulateManageSystems();
        SelectByAction(1853, 3);
      } else if (item.action_id >= 8100 && item.action_id <= 8200) {
        int sys_id = item.action_id - 8000;
        MenuSetSystemVisible(sys_id, !MenuIsSystemVisible(sys_id));
        PopulateManageSystems();
        SelectByAction(item.action_id, 0);
      }
    } else if (current_state == STATE_WEB_ADMIN) {
      if (item.action_id == 910) {
        if (WebAdminIsRunning()) WebAdminStop();
        else WebAdminStart(8080, false);
        PopulateWebAdminSettings();
        SelectByAction(910, 0);
      } else if (item.action_id == 911) {
        WebAdminRegeneratePin();
        PopulateWebAdminSettings();
        SelectByAction(911, 4);
      } else if (item.action_id == 912) {
        WebAdminOpenInBrowser();
        PopulateWebAdminSettings();
        SelectByAction(912, 1);
      }
    } else if (current_state == STATE_LIVE_CHAT) {
      if (item.action_id == 920) {
        DesktopChatSetRoom((LiveChatRoom)(((int)DesktopChatGetRoom() + 1) % 4));
        PopulateLiveChatSettings();
        SelectByAction(920, 0);
      } else if (item.action_id == 921) {
        TrayManagerSetStatus((TrayPresenceStatus)(((int)TrayManagerGetStatus() + 1) % 3));
        PopulateLiveChatSettings();
        SelectByAction(921, 1);
      } else if (item.action_id == 922) {
        TrayManagerSetEnabled(!TrayManagerIsEnabled());
        PopulateLiveChatSettings();
        SelectByAction(922, 2);
      } else if (item.action_id == 923) {
        DesktopChatShowWindow();
        OsdDisable();
      } else if (item.action_id == 924) {
        DesktopChatToggleOverlay();
        PopulateLiveChatSettings();
        SelectByAction(924, 3);
      } else if (item.action_id == 925) {
        DesktopChatSendChallenge(NULL);
        PopulateLiveChatSettings();
        SelectByAction(925, 6);
      } else if (item.action_id == 926) {
        DesktopChatAcceptLastChallenge();
        PopulateLiveChatSettings();
        SelectByAction(926, 7);
      }
    } else if (current_state == STATE_DIAGNOSTICS) {
      if (item.action_id == 520) {
        int cur = selected_idx;
        CrashReporterSetEnabled(!CrashReporterIsEnabled());
        PopulateDiagnostics();
        selected_idx = cur;
      } else if (item.action_id == 521) {
        CrashReporterSendTest();
      }
    } else if (current_state == STATE_ABOUT) {
      if (item.action_id == 209) {
        current_state = STATE_ABOUT_CORES;
        PopulateCoresList();
      } else if (item.action_id == 140) {
        current_state = STATE_BROWSE;
        PopulateBrowse("ports");
      }
    } else if (current_state == STATE_RESET_CONFIRM) {
      if (item.action_id == 211) {
        current_state = STATE_SETTINGS;
        PopulateSettings();
        SelectByAction(item.action_id, 6);
      } else if (item.action_id == 212) {
        ResetAllSettingsToDefault();
        current_state = STATE_SETTINGS;
        PopulateSettings();
        SelectByAction(item.action_id, 6);
        CoreSetToast(Tr("CONFIGURACOES RESTAURADAS AO PADRAO", "SETTINGS RESTORED TO DEFAULT"), 180);
      }
    } else if (current_state == STATE_CONTROLLER) {
      if (item.action_id == 530) {
        int cur_preset = InputBindGetPreset();
        int next_preset = (cur_preset + 1) % 4;
        InputBindApplyPreset(next_preset);
        MenuSaveSettings();
        PopulateControllerSettings();
        SelectByAction(item.action_id, 2);
      } else if (item.action_id == 796) {
        if (CoreIsRunning()) {
          std::string sys_name = CoreGetCoreName();
          if (InputBindSaveSystemProfile(sys_name.c_str())) {
            CoreSetToast(Tr("PERFIL SALVO PARA O SISTEMA", "REMAP SAVED FOR SYSTEM"), 90);
          }
          PopulateControllerSettings();
          SelectByAction(item.action_id, 3);
        }
      } else if (item.action_id == 797) {
        if (CoreIsRunning()) {
          std::string sys_name = CoreGetCoreName();
          if (InputBindDeleteSystemProfile(sys_name.c_str())) {
            InputBindRestoreGlobal();
            CoreSetToast(Tr("PERFIL REMOVIDO", "SYSTEM REMAP DELETED"), 90);
          }
          PopulateControllerSettings();
          SelectByAction(530, 2);
        }
      } else if (item.action_id == 500) {
        setting_pad_device = (setting_pad_device + 1) % 2;
        PopulateControllerSettings();
        SelectByAction(item.action_id, 1);
      } else if (item.action_id == 798) {
        wizard_active = true;
        wizard_step = 0;
        capture_bind = wizard_step;
        capture_armed = false;
        char toast_buf[64];
        snprintf(toast_buf, sizeof(toast_buf), "[1/%d] %s: %s",
                 (setting_pad_device == 0 ? 16 : BIND_COUNT),
                 Tr("PRESSIONE", "PRESS"),
                 InputBindLabel(wizard_step));
        CoreSetToast(toast_buf, 120);
        PopulateControllerSettings();
        SelectBindRow(wizard_step);
      } else if (item.action_id >= 700 && item.action_id < 700 + BIND_COUNT) {
        capture_bind = item.action_id - 700;
        capture_armed = false;
        PopulateControllerSettings();
        SelectBindRow(capture_bind);
      } else if (item.action_id == 799) {
        InputBindResetDefaults();
        PopulateControllerSettings();
        MenuSaveSettings();
        CoreSetToast(Tr("CONTROLES RESTAURADOS AO PADRAO", "CONTROLS RESTORED TO DEFAULT"), 120);
      }
    } else if (current_state == STATE_SAVESTATES) {
      if (item.action_id >= 850 && item.action_id <= 859) {
        int s = item.action_id - 850;
        CoreSetSelectedSlot(s);
        bool has_state = false;
        char time_str[32];
        int64_t bytes = 0;
        CoreGetStateSlotInfo(s, &has_state, time_str, sizeof(time_str), &bytes);
        if (has_state) {
          CoreLoadState(s);
          current_state = STATE_MAIN;
          PopulateMainMenu();
        } else {
          CoreSaveState(s);
          PopulateSaveStates();
        }
      } else if (item.action_id == 899) {
        current_state = STATE_MAIN;
        PopulateMainMenu();
      }
    } else if (current_state == STATE_SHADER_LIST) {
      if (item.action_id >= 650 && item.action_id < 650 + kFilterCount) {
        int idx = item.action_id - 650;
        setting_filter = kFilters[idx].mode;
        char msg[64];
        snprintf(msg, sizeof(msg), "SHADER: %s", FilterLabel(setting_filter));
        CoreSetToast(msg, 90);
        MenuSaveSettings();
        PopulateShaderList();
      } else if (item.action_id == 699) {
        current_state = shader_list_previous_state;
        if (current_state == STATE_MAIN)
          PopulateMainMenu();
        else
          PopulateVideoSettings();
      }
    } else if (current_state == STATE_NETPLAY) {
      if (item.action_id == 601) // Host
      {
        NetplayStartHost(55435);
        PopulateNetplay();
      } else if (item.action_id == 602) // Connect
      {
        NetplayStartClient(join_ip_input.c_str(), 55435);
        PopulateNetplay();
      } else if (item.action_id == 603) // Disconnect
      {
        NetplayDisconnect();
        PopulateNetplay();
      } else if (item.action_id == 218) // Karamelo Live (Chat)
      {
        current_state = STATE_LIVE_CHAT;
        PopulateLiveChatSettings();
      }
    } else if (current_state == STATE_VIDEO) {
      if (item.action_id == 302) {
        shader_list_previous_state = STATE_VIDEO;
        current_state = STATE_SHADER_LIST;
        PopulateShaderList();
      } else if (item.action_id == 304) {
        setting_fullscreen = !setting_fullscreen;
        PopulateVideoSettings();
        SelectByAction(item.action_id, 3);
      } else if (item.action_id == 305) {
        setting_theme = (setting_theme + 1) % 6;
        PopulateVideoSettings();
        SelectByAction(item.action_id, 4);
      }
    } else if (current_state == STATE_AUDIO) {
      if (item.action_id == 401) {
        CoreSetMute(!CoreGetMute());
        PopulateAudioSettings();
        SelectByAction(item.action_id, 0);
      } else if (item.action_id == 930) {
        VoiceChatSetEnabled(!VoiceChatIsEnabled());
        PopulateAudioSettings();
        SelectByAction(item.action_id, 4);
      } else if (item.action_id == 931) {
        VoiceChatToggleMicMute();
        PopulateAudioSettings();
        SelectByAction(item.action_id, 5);
      } else if (item.action_id == 932) {
        VoiceChatSetDenoiseEngine((VoiceDenoiseEngine)(((int)VoiceChatGetDenoiseEngine() + 1) % 3));
        PopulateAudioSettings();
        SelectByAction(item.action_id, 6);
      } else if (item.action_id == 933) {
        VoiceChatSetVadEnabled(!VoiceChatGetVadEnabled());
        PopulateAudioSettings();
        SelectByAction(item.action_id, 7);
      } else if (item.action_id == 934) {
        VoiceChatSetVadSensitivity((VoiceVadSensitivity)(((int)VoiceChatGetVadSensitivity() + 1) % 3));
        PopulateAudioSettings();
        SelectByAction(item.action_id, 8);
      } else if (item.action_id == 935) {
        VoiceChatSetAecEnabled(!VoiceChatGetAecEnabled());
        PopulateAudioSettings();
        SelectByAction(item.action_id, 9);
      } else if (item.action_id == 936) {
        VoiceChatSetAgcEnabled(!VoiceChatGetAgcEnabled());
        PopulateAudioSettings();
        SelectByAction(item.action_id, 10);
      }
    } else if (current_state == STATE_UPDATE) {
      if (item.action_id == 391) {
        UpdaterCheckAsync(true);
        PopulateUpdate();
      } else if (item.action_id == 392) {
        UpdaterStartDownload();
        PopulateUpdate();
      } else if (item.action_id == 393) {
        CoreSetToast(Tr("REINICIANDO PARA ATUALIZAR...", "RESTARTING TO UPDATE..."), 300);
        UpdaterApplyAndRestart();
      } else if (item.action_id == 399) {
        current_state = STATE_MAIN;
        PopulateMainMenu();
        selected_idx = main_menu_saved_idx;
      }
    } else if (current_state == STATE_FAVORITES) {
      if (item.label == "<..>" || s_favorites_paths.empty() || selected_idx == 0) {
        current_state = STATE_MAIN;
        PopulateMainMenu();
        selected_idx = main_menu_saved_idx;
      } else {
        size_t fidx = (size_t)(selected_idx - 1);
        if (fidx < s_favorites_paths.size()) {
          std::string path = s_favorites_paths[fidx];
          if (path.rfind("port:", 0) == 0) {
            MenuAddRecent(path);
            PortLaunch(path.substr(5));
          } else {
            MenuLaunchGamePath(path);
          }
        }
      }
    } else if (current_state == STATE_RECENT) {
      if (item.label == "<..>" || s_recent_paths.empty() || selected_idx == 0) {
        current_state = STATE_MAIN;
        PopulateMainMenu();
        selected_idx = main_menu_saved_idx;
      } else {
        size_t ridx = (size_t)(selected_idx - 1);
        if (ridx < s_recent_paths.size()) {
          std::string path = s_recent_paths[ridx];
          if (path.rfind("port:", 0) == 0) {
            MenuAddRecent(path);
            PortLaunch(path.substr(5));
          } else {
            MenuLaunchGamePath(path);
          }
        }
      }
    } else if (current_state == STATE_BROWSE) {
      if (item.label == "<..>") {
        BrowseGoUp();
      } else if (current_dir == "ports") {
        auto plist = PortGetAvailableList();
        std::string raw_lbl = item.label;
        if (raw_lbl.rfind("* ", 0) == 0) raw_lbl = raw_lbl.substr(2);
        for (const auto &p : plist) {
          if (p.name == raw_lbl) {
            MenuAddRecent("port:" + p.id);
            PortLaunch(p.id);
            break;
          }
        }
      } else if (item.is_folder) {
        fs::path next_p = fs::path(current_dir) / item.label;
        PopulateBrowse(next_p.string());
      } else {
        std::string raw_fn = item.label;
        if (raw_fn.rfind("* ", 0) == 0) raw_fn = raw_fn.substr(2);
        std::string full_rom_path =
            (fs::path(current_dir) / raw_fn).string();
        std::string ext = fs::path(raw_fn).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::string target_rom_path = full_rom_path;
        std::string core_dll = MenuResolveCoreForPath(full_rom_path, current_dir);
        ApplyMsxMachineTypeOption(core_dll, ext);
        ApplyPersistedCoreOptions();

        if (CoreRequestLoad(target_rom_path.c_str(), core_dll.c_str())) {
          // The system's remap profile is applied by MenuRun once the core
          // is actually up - see ApplySystemProfileForRunningCore.
          MenuAddRecent(target_rom_path);
          OsdDisable();
          current_state = STATE_MAIN;
          items.clear();
          selected_idx = 0;
          scroll_top = 0;
        }
      }
    }
    break;
  }
  case KEY_INFO: {
    if (current_state == STATE_BROWSE) {
      if (selected_idx >= 0 && selected_idx < (int)items.size()) {
        const auto &it = items[selected_idx];
        if (it.label != "<..>" && !it.is_folder) {
          if (current_dir == "ports") {
            std::string raw_lbl = it.label;
            if (raw_lbl.rfind("* ", 0) == 0) raw_lbl = raw_lbl.substr(2);
            auto plist = PortGetAvailableList();
            for (const auto &p : plist) {
              if (p.name == raw_lbl) {
                MenuToggleFavorite("port:" + p.id);
                int cur = selected_idx;
                PopulateBrowse(current_dir);
                selected_idx = cur;
                break;
              }
            }
          } else {
            std::string raw_fn = it.label;
            if (raw_fn.rfind("* ", 0) == 0) raw_fn = raw_fn.substr(2);
            std::string full_rom = (fs::path(current_dir) / raw_fn).string();
            MenuToggleFavorite(full_rom);
            int cur = selected_idx;
            PopulateBrowse(current_dir);
            selected_idx = cur;
          }
        }
      }
    } else if (current_state == STATE_FAVORITES) {
      if (selected_idx > 0 && (size_t)(selected_idx - 1) < s_favorites_paths.size()) {
        std::string path = s_favorites_paths[selected_idx - 1];
        MenuToggleFavorite(path);
        int cur = selected_idx;
        PopulateFavoritesMenu();
        if (cur >= (int)items.size()) cur = (int)items.size() - 1;
        selected_idx = cur;
      }
    }
    break;
  }
  case KEY_CANCEL:
    if (current_state == STATE_BROWSE) {
      BrowseGoUp();
    } else if (current_state == STATE_SYSTEMS) {
      current_state = STATE_MAIN;
      PopulateMainMenu();
      selected_idx = 0;
      scroll_top = 0;
    } else if (current_state == STATE_FAVORITES || current_state == STATE_RECENT) {
      current_state = STATE_MAIN;
      PopulateMainMenu();
      selected_idx = CoreIsRunning() ? 0 : main_menu_saved_idx;
    } else if (current_state == STATE_ABOUT_CORES) {
      current_state = STATE_ABOUT;
      PopulateAbout();
      SelectByAction(209, 6); // linha "Cores" na About, de onde se veio
    } else if (current_state == STATE_RESET_CONFIRM) {
      current_state = STATE_SETTINGS;
      PopulateSettings();
      SelectByAction(210, 6); // "Restaurar Padroes", de onde se veio
    } else if (current_state == STATE_SHADER_LIST) {
      current_state = shader_list_previous_state;
      if (current_state == STATE_MAIN)
        PopulateMainMenu();
      else
        PopulateVideoSettings();
    } else if (current_state == STATE_SAVESTATES) {
      current_state = STATE_MAIN;
      PopulateMainMenu();
    } else if (current_state == STATE_CONTROLLER && wizard_active) {
      wizard_active = false;
      capture_bind = -1;
      PopulateControllerSettings();
      CoreSetToast(Tr("CONFIGURACAO CANCELADA", "SETUP CANCELLED"), 90);
    } else if (current_state == STATE_VIDEO || current_state == STATE_AUDIO ||
               current_state == STATE_CONTROLLER ||
               current_state == STATE_NETPLAY || current_state == STATE_ABOUT ||
               current_state == STATE_DIAGNOSTICS ||
               current_state == STATE_CLOUD ||
               current_state == STATE_WEB_ADMIN ||
               current_state == STATE_LIVE_CHAT ||
               current_state == STATE_MANAGE_SYSTEMS) {
      current_state = STATE_SETTINGS;
      PopulateSettings();
    } else if (current_state == STATE_UPDATE) {
      current_state = STATE_MAIN;
      PopulateMainMenu();
      selected_idx = main_menu_saved_idx;
    } else if (current_state == STATE_SETTINGS) {
      current_state = STATE_MAIN;
      PopulateMainMenu();
      selected_idx = main_menu_saved_idx;
    } else if (CoreIsRunning()) {
      OsdDisable();
    }
    break;

  default:
    break;
  }
}

bool MenuLaunchGamePath(const std::string &full_rom_path) {
  fs::path p(full_rom_path);
  std::string path_str = full_rom_path;
  std::string core_dll = MenuResolveCoreForPath(full_rom_path, path_str);

  if (core_dll.empty())
    return false;

  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  ApplyMsxMachineTypeOption(core_dll, ext);

  ApplyPersistedCoreOptions();

  if (CoreRequestLoad(full_rom_path.c_str(), core_dll.c_str())) {
    MenuAddRecent(full_rom_path);
    OsdDisable();
    current_state = STATE_MAIN;
    items.clear();
    selected_idx = 0;
    scroll_top = 0;
    return true;
  }
  return false;
}

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <xinput.h>

#include "app_info.h"
#include "archive_helper.h"
#include "core_runner.h"
#include "hw_render.h"
#include "input_map.h"
#include "menu.h"
#include "mister_math.h"
#include "netplay.h"
#include "osd.h"
#include "retroachievements.h"
#include "updater.h"
#include "port_runner.h"

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
  STATE_UPDATE
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
static DWORD s_scroll_since = 0;

// Same idea for the vertical title band on the left edge (OsdSetTitle) -
// a title longer than the 14 characters that fit in the fixed-height card
// used to just cut off mid-word with no indication, same as the row names.
static std::string s_title_scroll_last;
static DWORD s_title_scroll_since = 0;
static int main_menu_saved_idx = 0;
static std::string current_title = "MiSTer 4 ALL";
static std::string current_dir = "roms";
static std::string status_msg = "MiSTer 4 ALL - mister4all.com - @GuhClemente";

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
  const char *label;
};
static const FilterChoice kFilters[] = {
    {0, "Off (Raw)"},       {8, "Scanlines Leve"}, {9, "CRT Suave (TV)"},
    {4, "PVM Pro 600TVL"},  {1, "Sony Trinitron"}, {2, "Arcade Shadow"},
    {5, "Scanlines 50%"},   {6, "NTSC Composite"}, {7, "LCD Matrix"},
    {3, "Tubo Curvado 3D"},
};
static const int kFilterCount = (int)(sizeof(kFilters) / sizeof(kFilters[0]));

static int FilterIndex(int mode) {
  for (int i = 0; i < kFilterCount; i++)
    if (kFilters[i].mode == mode)
      return i;
  return 0;
}
static const char *FilterLabel(int mode) {
  return kFilters[FilterIndex(mode)].label;
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
static int setting_pad_device = 0; // 0=Gamepad, 1=Keyboard

// Row index of the first key/pad binding on the Controller page.
static const int kBindRow0 = 3;

static const int kOsdTimeouts[] = {0, 5, 10, 15, 30, 60};
static const int kOsdTimeoutCount = 6;

// 0 = never marquee-scroll a long name, just leave it clipped as before.
static const int kScrollDelays[] = {0, 2, 3, 5, 8, 10};
static const int kScrollDelayCount = 6;

// Refreshed on every key the menu handles; MenuRun compares against it.
static DWORD g_last_input_tick = 0;

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
static int setting_theme =
    0; // 0=Red/Burgundy, 1=Blue, 2=Green, 3=Amber, 4=Gray, 5=Dark
static int setting_deadzone = 1; // 0=5%, 1=10%, 2=15%, 3=20%
static int setting_latency = 1;  // index into kAudioLatencyMs below; default 128ms
// Audio buffer depth. Below 64ms the waveOut queue cannot stay ahead of the
// mixer on a loaded machine; above 512ms the delay is audible against input.
static const int kAudioLatencyMs[4] = { 64, 128, 256, 512 };
// Vulkan and DirectX 11 used to be listed here too, but hw_render.cpp only
// ever implements OpenGL/WGL - picking either one silently ran OpenGL
// anyway, with no indication anything different had happened. Only list
// backends that actually exist until a real Vulkan/D3D11 backend lands.
static const char *kVideoDrivers[] = {
    "Software (CPU)",
    "OpenGL (GPU 3D)"
};
static const int kVideoDriverCount = 2;
static int setting_driver = 1; // Default to OpenGL (GPU 3D)
static int setting_sync = 1;  // 0=Native (Game Rate), 1=Sync to Display
static int setting_vsync = 1; // 0=Disabled, 1=Enabled
// ParaLLEl N64 is the primary robust core with Ari64 Dynarec and RetroAchievements.
// Defaulting to it provides 60fps locked emulation and rock-solid stability.
static int setting_n64_core = 0;
static int setting_arcade_core = 0; // 0=FBNeo, 1=MAME 2003, 2=MAME 2010
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

// Pushes the saved per-core options into the core. Called on load as well as
// on change: without the load-time call a layout restored from the config
// only took effect if the player toggled it again by hand.
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
static int setting_language = 0; // 0=Português, 1=English
static std::string join_ip_input = "127.0.0.1";

static void MenuProcessKeyImpl(MenuKey key);
static void MenuSaveSettings();

int MenuGetAspectMode() { return setting_aspect; }
int MenuGetFilterMode() { return setting_filter; }
int MenuGetWallpaperMode() { return setting_wallpaper; }

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
  static const char *kBase[] = {"None", "Static Noise", "Parallax Stars", "Cyber Grid"};
  if (mode >= 0 && mode < 4) return kBase[mode];
  const char *path = MenuGetWallpaperCustomPath(mode - 4);
  if (!path) return "None";
  static char buf[32];
  std::string stem = fs::path(path).stem().string();
  if (stem == "sabor_mister_chef") {
    snprintf(buf, sizeof(buf), "Flavor Chef");
  } else if (stem == "sabor_mister_arcade") {
    snprintf(buf, sizeof(buf), "Flavor Arcade");
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
int MenuGetDeadzone() { return (setting_deadzone + 1) * 5; }
int MenuGetAudioLatencyMs() { return kAudioLatencyMs[ClampInt(setting_latency, 0, 3)]; }
int MenuGetVideoDriver() { return setting_driver; }
int MenuGetSyncMode() { return setting_sync; }
int MenuGetVsync() { return setting_vsync; }
int MenuGetN64Core() { return setting_n64_core; }
static const char *GetArcadeCoreDll(const std::string &file_path = "") {
  if (setting_arcade_core == 1)
    return "cores/mame2003.dll";
  if (setting_arcade_core == 2)
    return "cores/mame2010.dll";
  if (setting_arcade_core == 3)
    return "cores/dreamcast.dll";

  // Auto mode (setting_arcade_core == 0): intelligently route arcade boards
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

static const char *GetN64CoreDll() {
  std::error_code ec;
  if (setting_n64_core == 0 && fs::exists("cores/n64_parallel.dll", ec))
    return "cores/n64_parallel.dll";
  if (setting_n64_core == 1 && fs::exists("cores/n64_mupen.dll", ec))
    return "cores/n64_mupen.dll";
  if (setting_n64_core == 2 && fs::exists("cores/n64_gopher.dll", ec))
    return "cores/n64_gopher.dll";
  if (fs::exists("cores/n64_parallel.dll", ec))
    return "cores/n64_parallel.dll";
  if (fs::exists("cores/n64.dll", ec))
    return "cores/n64.dll";
  return "cores/n64_parallel.dll";
}
int MenuGetHwRender() { return (setting_driver != 0) ? 1 : 0; }
int MenuGetLanguage() { return setting_language; }
const char *MenuGetStatus() { return status_msg.c_str(); }
const char *MenuGetTitle() { return current_title.c_str(); }

void PopulateMainMenu();
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
void PopulateAchievementList();
void PopulateUpdate();

static int neo_sys = 0;          // 0: AES, 1: MVS, 2: CDZ
static int neo_bios = 0;         // 0: Original, 1: UniBIOS
static int neo_cd_type = 0;      // 0: CDZ, 1: Top-Load, 2: Front-Load
static int neo_cd_region = 0;    // 0: US, 1: Japan, 2: Europe
static int neo_memcard = 0;      // 0: Plugged, 1: Unplugged
static int neo_dip_settings = 0; // 0: OFF, 1: ON
static int neo_dip_freeplay = 0; // 0: OFF, 1: ON

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
};

// NULL above means the system has its own core selector; ask that instead.
static bool SystemHasCore(int action_id) {
  const char *dll = NULL;
  bool known = false;
  for (const auto &e : kSystemCores)
    if (e.action_id == action_id) {
      dll = e.dll;
      known = true;
      break;
    }

  if (action_id == 107) {
    std::error_code ec;
    return fs::exists("cores/n64_gopher.dll", ec) ||
           fs::exists("cores/n64.dll", ec) ||
           fs::exists("cores/n64_parallel.dll", ec) ||
           fs::exists("cores/n64_mupen.dll", ec);
  }
  if (action_id == 117)
    dll = GetArcadeCoreDll();
  if (action_id == 140) {
    // Same list PopulateBrowse() and the About screen's count use, rather
    // than a second, looser check (the old fs::exists("ports") was always
    // true anyway - PortInit() creates the folder unconditionally on
    // startup, whether or not anything has been downloaded into it yet).
    return !PortGetAvailableList().empty();
  }
  if (!dll)
    return true;

  std::error_code ec;
  return fs::exists(dll, ec);
}

void PopulateMainMenu() {
  items.clear();
  current_title = "MiSTer";

  if (CoreIsRunning()) {
    std::string core_name = CoreGetCoreName();
    current_title = core_name.empty() ? "MiSTer" : core_name;

    const char *aspects[] = {"Original", "4:3", "16:9"};

    // 1. Header: Load [Game Name] and Reset
    std::string load_label = "Load";
    std::string game_name = CoreGetGameName();
    if (game_name.length() > 16)
      game_name = game_name.substr(0, 14) + "..";
    items.push_back(
        {load_label, game_name.empty() ? ">" : game_name, false, true, 1});
    items.push_back({"Reset", "", false, true, 6});
    items.push_back({" ", "", false, false, 0});

    // 2. NeoGeo Authentic MiSTer Options
    if (core_name == "NeoGeo" || core_name.find("Neo") != std::string::npos) {
      // Labels line up 1:1 with the value tables in the handlers below.
      // Geolith has no "CD" system type - a CD is selected by loading a
      // disc image, and its hardware is chosen through CD Type.
      const char *sys_types[] = {"Console(AES)", "Arcade(MVS)", "UniBIOS"};
      const char *bios_types[] = {"AES", "MVS"};
      const char *cd_types[] = {"CDZ", "Top-Loading", "Front-Loading",
                                "CDZ UniBIOS"};
      const char *regions[] = {"US", "Japan", "Asia", "Europe"};
      const char *memcards[] = {"Plugged", "Unplugged"};
      const char *dip_states[] = {"OFF", "ON"};

      items.push_back({"System Type", sys_types[neo_sys], false, false, 501});
      items.push_back({"UniBIOS HW", bios_types[neo_bios], false, false, 502});
      items.push_back({"CD Type", cd_types[neo_cd_type], false, false, 503});
      items.push_back({"CD Region", regions[neo_cd_region], false, false, 504});
      items.push_back(
          {"Memory Card", memcards[neo_memcard], false, false, 505});
      items.push_back(
          {"[DIP] Settings", dip_states[neo_dip_settings], false, false, 506});
      items.push_back(
          {"[DIP] Freeplay", dip_states[neo_dip_freeplay], false, false, 507});
    }

    // 3. Display & Shaders
    items.push_back(
        {"Aspect Ratio", aspects[setting_aspect], false, false, 301});
    items.push_back(
        {"Scanlines", FilterLabel(setting_filter), false, false, 302});

    // Master System FM: the YM2413 sound chip a handful of Japanese games
    // use. Genesis Plus GX exposes it, but nothing here ever offered it.
    if (core_name.find("Master System") != std::string::npos) {
      const char *fm[] = {"Auto", "Desligado", "Ligado"};
      items.push_back(
          {"FM Audio (YM2413)", fm[setting_sms_fm], false, false, 509});
    }

    // The DS has two screens and how they sit is a player preference, not a
    // fixed one. melonDS exposes it; nothing here ever offered it.
    if (core_name.find("Nintendo DS") != std::string::npos) {
      items.push_back({"Layout Telas", kNdsLayoutNames[setting_nds_layout],
                       false, false, 510});
      items.push_back(
          {"Espaco Telas", kNdsGapVals[setting_nds_gap], false, false, 511});
      if (setting_nds_layout >= 6)
        items.push_back({"Tela Pequena", kNdsHybridNames[setting_nds_hybrid],
                         false, false, 512});
    }

    // Same deal as the DS: two screens, layout is a preference. Citra
    // exposes it as a hidden keyboard hotkey (C) with no menu entry at all.
    if (core_name.find("Nintendo 3DS") != std::string::npos) {
      items.push_back({"Layout Telas", kCitraLayoutNames[setting_citra_layout],
                       false, false, 515});
    }

    // Mega CD load times are long because the core emulates a 1x drive.
    // Only meaningful for a disc: the same core runs Genesis cartridges.
    if (core_name.find("Genesis") != std::string::npos && CoreIsDiscGame()) {
      const char *od[] = {"Desligado", "Ligado"};
      const char *acc[] = {"Real", "Rapido"};
      items.push_back(
          {"Cache do CD", od[setting_cd_precache], false, false, 513});
      items.push_back(
          {"Acesso do CD", acc[setting_cd_latency], false, false, 514});
    }

    // 4. RetroAchievements, if the integration is configured. None of this
    //    reached the screen before: the status and counters existed as
    //    accessors that nothing called.
    if (RaIsEnabled()) {
      char ra_val[40];

      if (!RaIsLoggedIn()) {
        snprintf(ra_val, sizeof(ra_val), "Conectando...");
      } else if (RaGetAchievementCount() > 0) {
        snprintf(ra_val, sizeof(ra_val), "%d/%d%s", RaGetAchievementsUnlocked(),
                 RaGetAchievementCount(), RaHasPendingUnlocks() ? " !" : "");
      } else {
        snprintf(ra_val, sizeof(ra_val), "Sem conquistas");
      }

      items.push_back({"Conquistas", ra_val, false, true, 207});
    }

    // 5. Savestates & Controls
    items.push_back({" ", "", false, false, 0});
    char save_str[32];
    snprintf(save_str, sizeof(save_str), "Slot %d", CoreGetSelectedSlot());
    items.push_back({"Save State", save_str, false, true, 2});
    items.push_back({"Load State", save_str, false, true, 3});
    items.push_back({"Define Buttons", ">", false, true, 203});
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Close Game", "", false, true, 7});

    OsdSetSize((int)items.size());
  } else {
    OsdSetSize((int)items.size());
    // Main Systems Menu (Alphabetical)
    if (SystemHasCore(117))
      items.push_back({"Arcade", ">", true, true, 117});
    if (SystemHasCore(101))
      items.push_back({"Atari 2600", ">", true, true, 101});
    if (SystemHasCore(129))
      items.push_back({"Atari 5200", ">", true, true, 129});
    if (SystemHasCore(130))
      items.push_back({"Atari 7800", ">", true, true, 130});
    if (SystemHasCore(128))
      items.push_back({"Atari Jaguar", ">", true, true, 128});
    if (SystemHasCore(134))
      items.push_back({"Atari Lynx", ">", true, true, 134});
    if (SystemHasCore(131))
      items.push_back({"ColecoVision", ">", true, true, 131});
    if (SystemHasCore(124))
      items.push_back({"Commodore 64", ">", true, true, 124});
    if (SystemHasCore(123))
      items.push_back({"Commodore Amiga", ">", true, true, 123});
    if (SystemHasCore(112))
      items.push_back({"Dreamcast", ">", true, true, 112});
    if (SystemHasCore(119))
      items.push_back({"Game Boy / Color", ">", true, true, 119});
    if (SystemHasCore(118))
      items.push_back({"Game Boy Advance", ">", true, true, 118});
    if (SystemHasCore(113))
      items.push_back({"GameCube", ">", true, true, 113});
    if (SystemHasCore(102))
      items.push_back({"Genesis / Mega Drive", ">", true, true, 102});
    if (SystemHasCore(103))
      items.push_back({"Master System", ">", true, true, 103});
    if (SystemHasCore(104))
      items.push_back({"Mega CD", ">", true, true, 104});
    if (SystemHasCore(121))
      items.push_back({"MS-DOS", ">", true, true, 121});
    if (SystemHasCore(122))
      items.push_back({"MSX / MSX2", ">", true, true, 122});
    if (SystemHasCore(105))
      items.push_back({"NES / Famicom", ">", true, true, 105});
    if (SystemHasCore(132))
      items.push_back({"Neo Geo Pocket", ">", true, true, 132});
    if (SystemHasCore(106))
      items.push_back({"NeoGeo / CD", ">", true, true, 106});
    if (SystemHasCore(115))
      items.push_back({"Nintendo 3DS", ">", true, true, 115});
    if (SystemHasCore(107))
      items.push_back({"Nintendo 64", ">", true, true, 107});
    if (SystemHasCore(114))
      items.push_back({"Nintendo DS", ">", true, true, 114});
    if (SystemHasCore(127))
      items.push_back({"Panasonic 3DO", ">", true, true, 127});
    if (SystemHasCore(135))
      items.push_back({"PC-FX", ">", true, true, 135});
    if (SystemHasCore(108))
      items.push_back({"PlayStation 1", ">", true, true, 108});
    if (SystemHasCore(116))
      items.push_back({"PlayStation 2 (PS2)", ">", true, true, 116});
    if (SystemHasCore(120))
      items.push_back({"PlayStation Portable (PSP)", ">", true, true, 120});
    if (SystemHasCore(140))
      items.push_back({"Ports & Recomp", ">", true, true, 140});
    if (SystemHasCore(110))
      items.push_back({"Saturn", ">", true, true, 110});
    if (SystemHasCore(126))
      items.push_back({"Sega 32X", ">", true, true, 126});
    if (SystemHasCore(109))
      items.push_back({"SNES / Super Famicom", ">", true, true, 109});
    if (SystemHasCore(111))
      items.push_back({"TurboGrafx 16 / PCE", ">", true, true, 111});
    if (SystemHasCore(133))
      items.push_back({"WonderSwan", ">", true, true, 133});
    if (SystemHasCore(125))
      items.push_back({"ZX Spectrum", ">", true, true, 125});
    // Blank row before the app entries, the way the MiSTer OSD separates the
    // system list from Settings / Update / Close.
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Settings", ">", false, true, 20});
    items.push_back({"Update", ">", false, true, 30});
    items.push_back({"Exit " APP_NAME, "", false, true, 99});

    // Counted here rather than written down somewhere else: this is the list.
    g_system_count = 0;
    for (const auto &it : items)
      if ((it.action_id >= 101 && it.action_id <= 135) || it.action_id == 140)
        g_system_count++;
  }

  selected_idx = 0;
  scroll_top = 0;
}

// Entering the page with no pad plugged in should show the keyboard bindings.
// Otherwise the page silently lists gamepad buttons to someone playing on the
// keyboard - which is exactly how a player ends up unable to find the control
// they are looking for. Only on entry, so toggling Dispositivo still works.
static void ControllerPageOnEnter() {
  int pads = 0;
  for (DWORD i = 0; i < 4; i++) {
    XINPUT_STATE st;
    if (XInputGetState(i, &st) == ERROR_SUCCESS)
      pads++;
  }
  if (pads == 0)
    setting_pad_device = 1;
}

void PopulateControllerSettings() {
  items.clear();
  current_title = "Controller";
  OsdSetSize(16);

  int pads = 0;
  for (DWORD i = 0; i < 4; i++) {
    XINPUT_STATE st;
    if (XInputGetState(i, &st) == ERROR_SUCCESS)
      pads++;
  }

  char pad_str[24];
  snprintf(pad_str, sizeof(pad_str), "%d conectado%s", pads,
           pads == 1 ? "" : "s");
  items.push_back({"Gamepads", pad_str, false, false, 0});

  const char *dev_names[] = {"Gamepad", "Teclado"};
  items.push_back(
      {"Dispositivo", dev_names[setting_pad_device], false, false, 500});

  const char *deadzones[] = {"5%", "10%", "15%", "20%"};
  items.push_back({"Deadzone", deadzones[setting_deadzone], false, false, 508});

  // Labels come from the running core when it supplied them, so the page reads
  // in that system's own terms - "Cross" on PSP, not a generic "Botao B".
  // Falls back to the generic name with no core loaded or a core that stayed
  // silent. The buffers are static because the item list holds the pointers.
  static char bind_labels[BIND_COUNT][28];

  for (int i = 0; i < BIND_COUNT; i++) {
    const char *val = (capture_bind == i)
                          ? "<pressione>"
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
        const char *dir = axis == 1 ? (sign > 0 ? "Cima" : "Baixo")
                                    : (sign > 0 ? "Dir" : "Esq");

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

  items.push_back({"Restaurar", "Padrao", false, true, 799});

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
  setting_aspect = 0;
  setting_filter = 0;
  setting_pad_device = 0;
  setting_wallpaper = 1;
  setting_fullscreen = false;
  setting_theme = 0;
  setting_deadzone = 1;
  setting_latency = 1;
  setting_driver = 1;
  setting_sync = 1;
  setting_vsync = 1;
  setting_n64_core = 0; // ParaLLEl N64 - see the comment on its declaration
  setting_arcade_core = 0;
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

  InputBindResetDefaults();
  CoreSetVolume(100);
  CoreSetMute(false);

  MenuSaveSettings();
}

void PopulateSettings() {
  items.clear();
  current_title = "Settings";
  OsdSetSize(8);

  items.push_back({"Video", ">", false, true, 201});
  items.push_back({"Audio", ">", false, true, 202});
  items.push_back({"Controller", ">", false, true, 203});
  items.push_back({"Netplay (Online)", ">", false, true, 204});
  items.push_back({"RetroAchievements", ">", false, true, 206});
  items.push_back({"About", ">", false, true, 205});
  items.push_back({"Restaurar Padroes de Fabrica", ">", false, true, 210});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateVideoSettings() {
  items.clear();
  current_title = "Video";
  OsdSetSize(13);

  const char *aspects[] = {"Original", "4:3", "16:9"};
  const char *displays[] = {"Windowed", "Fullscreen"};
  const char *themes[] = {"Red", "Blue", "Green", "Amber", "Gray", "Dark"};

  items.push_back({"Aspect Ratio", aspects[setting_aspect], false, false, 301});
  items.push_back(
      {"CRT Shader", FilterLabel(setting_filter), false, false, 302});
  items.push_back(
      {"Wallpaper", WallpaperDisplayLabel(setting_wallpaper), false, false, 303});
  items.push_back(
      {"Display", displays[setting_fullscreen ? 1 : 0], false, false, 304});
  items.push_back({"OSD Color", themes[setting_theme], false, false, 305});
  items.push_back({"Video Driver", kVideoDrivers[setting_driver], false, false, 306});

  // Named after what each core reports through retro_get_system_info, not
  // after the filename. cores/n64.dll identifies itself only as "Nintendo 64"
  // v1.0 - a rebuilt binary that names no engine and exports no memory, which
  // is why it earns no achievements. Labels are kept under the 13 characters
  // the OSD value column shows.
  const char *n64_cores[] = {"ParaLLEl N64", "Mupen64+ Next", "Gopher64"};
  items.push_back({"N64 Core", n64_cores[setting_n64_core], false, false, 309});

  const char *arcade_cores[] = {"FinalBurn Neo", "MAME 2003", "MAME 2010",
                                "Flycast Naomi"};
  items.push_back(
      {"Arcade Core", arcade_cores[setting_arcade_core], false, false, 311});

  char osd_to[16];
  if (kOsdTimeouts[setting_osd_timeout] == 0)
    snprintf(osd_to, sizeof(osd_to), "Nunca");
  else
    snprintf(osd_to, sizeof(osd_to), "%ds", kOsdTimeouts[setting_osd_timeout]);
  items.push_back({"Ocultar Menu", osd_to, false, false, 312});

  const char *vsyncs[] = {"Disabled", "Enabled"};
  items.push_back({"V-Sync", vsyncs[setting_vsync], false, false, 308});

  const char *syncs[] = {"Native (Game Rate)", "Sync to Display"};
  items.push_back({"Display Sync", syncs[setting_sync], false, false, 307});

  // Read-only: what the app actually detected, and whether the sync could be
  // applied. On a 144Hz panel there is no usable divisor for 60fps content.
  char hz[48];
  if (setting_sync == 1 && !CoreDisplaySyncActive())
    snprintf(hz, sizeof(hz), "%.2fHz (no divisor)", CoreGetDisplayFps());
  else
    snprintf(hz, sizeof(hz), "%.2f Hz", CoreGetDisplayFps());
  items.push_back({"Monitor", hz, false, false, 0});

  char scroll_lbl[16];
  if (kScrollDelays[setting_name_scroll] == 0)
    snprintf(scroll_lbl, sizeof(scroll_lbl), "Desativado");
  else
    snprintf(scroll_lbl, sizeof(scroll_lbl), "%ds", kScrollDelays[setting_name_scroll]);
  items.push_back({"Rolagem Nome", scroll_lbl, false, false, 313});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateAudioSettings() {
  items.clear();
  current_title = "Audio";
  OsdSetSize(5);

  const char *latencies[] = {"64 ms", "128 ms", "256 ms", "512 ms"};

  items.push_back({"Mute", CoreGetMute() ? "On" : "Off", false, false, 401});
  items.push_back({"Latency", latencies[setting_latency], false, false, 402});

  char vol_str[32];
  snprintf(vol_str, sizeof(vol_str), "%d%%", CoreGetVolume());
  items.push_back({"Volume", vol_str, false, false, 403});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateNetplay() {
  items.clear();
  current_title = "Netplay";
  OsdSetSize(9);

  const char *role_str = "Disconnected";
  if (NetplayGetState() == NETPLAY_LISTENING)
    role_str = "Hosting...";
  else if (NetplayGetState() == NETPLAY_CONNECTING)
    role_str = "Connecting...";
  else if (NetplayGetState() == NETPLAY_CONNECTED) {
    role_str = (NetplayGetRole() == NETPLAY_HOST) ? "Connected (Host)"
                                                  : "Connected (Client)";
  }

  items.push_back({"Network Status", role_str, false, false, 0});
  items.push_back({"Local IP", NetplayGetLocalIp(), false, false, 0});
  items.push_back({"Host Game", "Port 55435", false, true, 601});
  items.push_back({"Connect to Host", join_ip_input.c_str(), false, true, 602});
  items.push_back({"Disconnect Netplay", "", false, true, 603});
  items.push_back({"Netplay Port", "55435", false, false, 0});
  items.push_back({"Back", "", false, true, 999});

  selected_idx = 2;
  scroll_top = 0;
}

void PopulateAchievementList() {
  items.clear();
  current_title = "Conquistas";

  RaAchievementInfo list[64];
  int n = RaGetAchievements(list, 64);

  if (n <= 0) {
    items.push_back({"Nenhuma conquista", "", false, false, 0});
    items.push_back({"para este jogo", "", false, false, 0});
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

  if (!RaIsEnabled()) {
    items.push_back({"Estado", "Desativado", false, false, 0});
    items.push_back({"Config/retroachievements", ".cfg", false, false, 0});
    items.push_back({"username e password", "", false, false, 0});
  } else {
    items.push_back(
        {"Usuario", RaIsLoggedIn() ? RaGetUserName() : "-", false, false, 0});
    items.push_back({"Modo", RaIsHardcoreActive() ? "Hardcore" : "Softcore",
                     false, false, 0});

    if (RaGetAchievementCount() > 0) {
      char v[40];
      snprintf(v, sizeof(v), "%d/%d", RaGetAchievementsUnlocked(),
               RaGetAchievementCount());
      items.push_back({"Conquistas", v, false, false, 0});
    }

    if (RaHasPendingUnlocks())
      items.push_back({"Pendentes", "sem conexao", false, false, 0});
  }

  // The status line carries the real detail - why a login failed, or why a
  // game was not recognised. It used to exist only as an unread variable.
  std::string st = RaGetStatus();
  for (size_t i = 0; i < st.size(); i += 24)
    items.push_back({st.substr(i, 24), "", false, false, 0});

  selected_idx = 0;
  scroll_top = 0;
}

void PopulateAbout() {
  items.clear();
  current_title = "About";
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
      if (x == ".dll")
        core_files++;
    }
  }
  if (core_files > 0 && core_files != APP_CORE_FILES) {
    FILE *lf = fopen("mister_flavor.log", "a");
    if (lf) {
      fprintf(lf,
              "[WARN] [ABOUT] cores/ tem %d DLLs, mas APP_CORE_FILES diz %d - "
              "atualize app_info.h e o site\n",
              core_files, APP_CORE_FILES);
      fclose(lf);
    }
  }

  char sys_str[32], core_str[32], ports_str[32];
  snprintf(sys_str, sizeof(sys_str), "%d Sistemas", APP_SYSTEM_COUNT);
  snprintf(core_str, sizeof(core_str), "%d Cores", APP_CORE_ENGINES);
  // Counted the same way Systems/Cores are - queried for real rather than a
  // second hardcoded figure that this list would just as quietly outgrow.
  snprintf(ports_str, sizeof(ports_str), "%d Jogos", (int)PortGetAvailableList().size());

  items.push_back({APP_NAME, "v" APP_VERSION " " APP_ARCH, false, false, 0});
  items.push_back({"Author", "Guh Clemente", false, false, 0});
  items.push_back({"YouTube", "@GuhClemente", false, false, 0});
  items.push_back({"Site", "mister4all", false, false, 0});
  items.push_back({"Engine", "Libretro", false, false, 0});
  items.push_back({"Systems", sys_str, false, false, 0});
  items.push_back({"Cores", core_str, false, false, 0});
  items.push_back({"Ports & Recomp", ports_str, false, false, 0});
  items.push_back({"Netplay", "2P TCP/IP", false, false, 0});
  items.push_back({"Conquistas", RaIsEnabled() ? "Ativado" : "Desativado",
                   false, false, 0});
  items.push_back({"License", "Open Source", false, false, 0});
  items.push_back({"Back", "", false, true, 999});

  selected_idx = 11;
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
  current_title = "Update";
  OsdSetSize(10);

  UpdaterState state = UpdaterGetState();
  const UpdateInfo &info = UpdaterGetInfo();

  items.push_back({"Versao Atual", "v" APP_VERSION, false, false, 0});

  if (!info.version.empty()) {
    items.push_back({"Versao Remota", "v" + info.version, false, false, 0});
  } else {
    items.push_back({"Versao Remota", "...", false, false, 0});
  }

  items.push_back({" ", "", false, false, 0});

  if (state == UPDATER_STATE_CHECKING) {
    items.push_back({"Status", "Verificando...", false, false, 0});
    items.push_back({"Consultando servidor", "Aguarde", false, false, 0});
  } else if (state == UPDATER_STATE_UP_TO_DATE) {
    items.push_back({"Status", "Atualizado!", false, false, 0});
    items.push_back({"Voce ja possui a", "versao mais recente", false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Verificar Novamente", ">", false, true, 391});
  } else if (state == UPDATER_STATE_AVAILABLE) {
    items.push_back({"Status", "Nova Versao!", false, false, 0});
    if (!info.notes.empty()) {
      std::string note = info.notes;
      if (note.length() > 24) note = note.substr(0, 22) + "..";
      items.push_back({"Novidades", note, false, false, 0});
    }
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Baixar e Atualizar", ">", false, true, 392});
  } else if (state == UPDATER_STATE_DOWNLOADING) {
    int prog = UpdaterGetProgress();
    char prog_str[32];
    snprintf(prog_str, sizeof(prog_str), "%d%%", prog);
    items.push_back({"Status", "Baixando...", false, false, 0});
    items.push_back({"Progresso", prog_str, false, false, 0});

    char bar[26];
    int filled = (prog * 16) / 100;
    bar[0] = '[';
    for (int b = 0; b < 16; b++) bar[b + 1] = (b < filled) ? '=' : ' ';
    bar[17] = ']';
    bar[18] = '\0';
    items.push_back({bar, prog_str, false, false, 0});
  } else if (state == UPDATER_STATE_READY) {
    items.push_back({"Status", "Pronto!", false, false, 0});
    items.push_back({"Download Concluido", "", false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Reiniciar Agora", ">", false, true, 393});
  } else if (state == UPDATER_STATE_ERROR) {
    items.push_back({"Status", "Erro de conexao", false, false, 0});
    std::string err_msg = UpdaterGetStatusMessage();
    if (err_msg.length() > 24) err_msg = err_msg.substr(0, 22) + "..";
    items.push_back({err_msg, "", false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Tentar Novamente", ">", false, true, 391});
  } else {
    items.push_back({"Status", "Pronto", false, false, 0});
    items.push_back({" ", "", false, false, 0});
    items.push_back({"Verificar Atualizacoes", ">", false, true, 391});
  }

  items.push_back({"Voltar", "", false, true, 399});

  if (selected_idx >= (int)items.size()) selected_idx = (int)items.size() - 1;
  if (selected_idx < 0) selected_idx = 0;
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
    // PortGetAvailableList() only returns titles already resolved to a real
    // executable on disk, so every row here is ready to play right now -
    // no "[JOGAR]"/"[BAIXAR]" status needed, just the game name.
    auto plist = PortGetAvailableList();
    for (const auto& p : plist) {
      items.push_back({p.name, "", false, true, 800});
    }
    if (plist.empty()) {
      items.push_back({"[Nenhum Port Encontrado]", "", false, false, 0});
    }
    OsdSetSize((int)items.size());
    selected_idx = 1;
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

  if (dirpath != "roms" && dirpath != "." && dirpath != "/" &&
      dirpath != "\\") {
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
      }

      // Pass 2: Populate list with directories and valid playable files only
      for (const auto &entry : all_entries) {
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
            lower_fn.ends_with(".md") || lower_fn.ends_with(".ini") ||
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
          files.push_back({filename, "", false, false, 0});
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
    items.push_back({"[Empty Directory]", "", false, false, 0});
  }

  if (items.empty()) {
    items.push_back({"[Empty Directory]", "", false, false, 0});
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
    selected_idx = main_menu_saved_idx;
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
static const char *SETTINGS_PATH = "Config/mister_flavor.cfg";
static const char *LEGACY_SETTINGS_PATH = "Config/sabor_mister.cfg";

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
  AppendSetting(out, "filter", setting_filter);
  AppendSetting(out, "pad_device", setting_pad_device);
  for (int i = 0; i < BIND_COUNT; i++) {
    AppendSetting(out, InputBindConfigKey(i), InputBindGetKey(i));
    AppendSetting(out, InputBindPadConfigKey(i), InputBindGetPad(i));
  }
  AppendSetting(out, "wallpaper", setting_wallpaper);
  AppendSetting(out, "fullscreen", setting_fullscreen ? 1 : 0);
  AppendSetting(out, "theme", setting_theme);
  AppendSetting(out, "deadzone", setting_deadzone);
  AppendSetting(out, "latency", setting_latency);
  AppendSetting(out, "driver", setting_driver);
  AppendSetting(out, "sync", setting_sync);
  AppendSetting(out, "vsync", setting_vsync);
  AppendSetting(out, "n64_core", setting_n64_core);
  AppendSetting(out, "arcade_core", setting_arcade_core);
  AppendSetting(out, "osd_timeout", setting_osd_timeout);
  AppendSetting(out, "name_scroll", setting_name_scroll);
  AppendSetting(out, "hw_render", (setting_driver != 0) ? 1 : 0);
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

  out += "netplay_ip=";
  out += join_ip_input;
  out += "\n";

  return out;
}

static void MenuSaveSettings() {
  std::error_code ec;
  fs::create_directories("Config", ec);

  FILE *f = fopen(SETTINGS_PATH, "wb");
  if (!f)
    return;

  fprintf(f, "# MiSTer Flavor - Configuration\n");
  std::string snap = SettingsSnapshot();
  fwrite(snap.data(), 1, snap.size(), f);
  fclose(f);
}

static void MenuLoadSettings() {
  FILE *f = fopen(SETTINGS_PATH, "rb");
  if (!f)
    f = fopen(LEGACY_SETTINGS_PATH, "rb");
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
    else if (!strcmp(key, "filter"))
      setting_filter = MigrateFilter(ClampInt(iv, 0, 9));
    else if (!strcmp(key, "wallpaper"))
      setting_wallpaper = ClampInt(iv, 0, WallpaperMaxMode());
    else if (!strcmp(key, "fullscreen"))
      setting_fullscreen = (iv != 0);
    else if (!strcmp(key, "theme"))
      setting_theme = ClampInt(iv, 0, 5);
    else if (!strcmp(key, "deadzone"))
      setting_deadzone = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "pad_device"))
      setting_pad_device = ClampInt(iv, 0, 1);
    else if (InputBindFromConfigKey(key) >= 0)
      InputBindSetKey(InputBindFromConfigKey(key), iv);
    else if (InputBindFromPadConfigKey(key) >= 0)
      InputBindSetPad(InputBindFromPadConfigKey(key), iv);
    else if (!strcmp(key, "latency"))
      setting_latency = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "driver"))
      // Clamped to the real range, not the old 0-4: a config saved before
      // Vulkan/DirectX were removed from the menu could still have driver=2
      // or 3 on disk, which would index kVideoDrivers[] out of bounds.
      setting_driver = ClampInt(iv, 0, kVideoDriverCount - 1);
    else if (!strcmp(key, "sync"))
      setting_sync = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "vsync"))
      setting_vsync = ClampInt(iv, 0, 1);
    else if (!strcmp(key, "n64_core"))
      setting_n64_core = ClampInt(iv, 0, 2);
    else if (!strcmp(key, "arcade_core"))
      setting_arcade_core = ClampInt(iv, 0, 3);
    else if (!strcmp(key, "osd_timeout"))
      setting_osd_timeout = ClampInt(iv, 0, kOsdTimeoutCount - 1);
    else if (!strcmp(key, "name_scroll"))
      setting_name_scroll = ClampInt(iv, 0, kScrollDelayCount - 1);
    else if (!strcmp(key, "hw_render"))
    {
      if (iv != 0 && setting_driver == 0) setting_driver = 1;
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
    else if (!strcmp(key, "netplay_ip") && val[0])
      join_ip_input = val;
  }
  fclose(f);
}

void MenuProcessKey(MenuKey key) {
  std::string before = SettingsSnapshot();
  MenuProcessKeyImpl(key);
  if (SettingsSnapshot() != before)
    MenuSaveSettings();
}

void MenuInit() {
  MenuLoadSettings();
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

  // Close the OSD after a spell of no input, so it does not sit over the game
  // forever if the player wandered off. Only while a game is actually running:
  // hiding it on the main menu would leave nothing on screen at all.
  if (OsdIsEnabled() && CoreIsRunning() && capture_bind < 0) {
    int secs = kOsdTimeouts[setting_osd_timeout];
    if (secs > 0) {
      if (g_last_input_tick == 0)
        g_last_input_tick = GetTickCount();
      if (GetTickCount() - g_last_input_tick >= (DWORD)secs * 1000) {
        OsdDisable();
        g_last_input_tick = GetTickCount();
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
        capture_bind = -1;
        PopulateControllerSettings();
      } else if (pad != 0) {
        int target = capture_bind;
        InputBindSetPad(target, pad);
        setting_pad_device = 0;
        capture_bind = -1;
        PopulateControllerSettings();
        selected_idx = kBindRow0 + target;
        MenuSaveSettings();
      } else if (vk != 0) {
        int target = capture_bind;
        bool ok = InputBindSetKey(target, vk);
        setting_pad_device = 1;
        capture_bind = -1;
        PopulateControllerSettings();
        selected_idx = kBindRow0 + target;
        if (ok)
          MenuSaveSettings();
        else
          CoreSetToast("ESSA TECLA E RESERVADA PARA O MENU", 120);
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

  OsdClear();

  // The vertical title band fits ~14 rotated characters top to bottom before
  // OsdSetTitle's own bounds check silently drops the rest - marquee-scroll
  // it the same way the row names below scroll, instead of leaving longer
  // titles permanently cut off.
  const int kVisibleCharsVert = 14;
  if (current_title != s_title_scroll_last) {
    s_title_scroll_last = current_title;
    s_title_scroll_since = GetTickCount();
  }
  if (kScrollDelays[setting_name_scroll] > 0 &&
      (int)current_title.size() > kVisibleCharsVert) {
    DWORD delay_ms = (DWORD)kScrollDelays[setting_name_scroll] * 1000;
    DWORD elapsed = GetTickCount() - s_title_scroll_since;
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

    const auto &item = items[item_idx];

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
        const int kVisibleChars = 26;
        if (is_selected && kScrollDelays[setting_name_scroll] > 0 &&
            (int)item.label.size() > kVisibleChars) {
          if (item_idx != s_scroll_item_idx) {
            s_scroll_item_idx = item_idx;
            s_scroll_since = GetTickCount();
          }
          DWORD delay_ms = (DWORD)kScrollDelays[setting_name_scroll] * 1000;
          DWORD elapsed = GetTickCount() - s_scroll_since;
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
            snprintf(line_buf, sizeof(line_buf), " %s", item.label.c_str());
          }
        } else {
          snprintf(line_buf, sizeof(line_buf), " %s", item.label.c_str());
        }
      }
    }

    OsdWrite((unsigned char)i, line_buf, is_selected ? 1 : 0, 0, 0, 30, 0);
  }
}

static void MenuProcessKeyImpl(MenuKey key) {
  g_last_input_tick = GetTickCount();

  // While waiting for a key to bind, the menu must not also act on it.
  if (capture_bind >= 0)
    return;

  if (key == KEY_MENU_TOGGLE) {
    if (OsdIsEnabled()) {
      if (CoreIsRunning())
        OsdDisable();
    } else {
      OsdEnable(0);
      current_state = STATE_MAIN;
      PopulateMainMenu();
    }
    return;
  }

  if (!OsdIsEnabled()) {
    OsdEnable(0);
    current_state = STATE_MAIN;
    PopulateMainMenu();
    return;
  }

  int total = (int)items.size();
  if (total == 0)
    return;

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
      } while (selected_idx < total && items[selected_idx].label == " " &&
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
      } while (selected_idx < total && items[selected_idx].label == " " &&
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
    const auto &item = items[selected_idx];

    if (item.action_id == 301) // Aspect
    {
      int cur = selected_idx;
      setting_aspect = (setting_aspect + delta + 3) % 3;
      if (CoreIsRunning() && current_state == STATE_MAIN)
        PopulateMainMenu();
      else
        PopulateVideoSettings();
      selected_idx = cur;
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
      selected_idx = 2;
    } else if (item.action_id == 304) // Fullscreen
    {
      setting_fullscreen = !setting_fullscreen;
      PopulateVideoSettings();
      selected_idx = 3;
    } else if (item.action_id == 305) // OSD Color
    {
      setting_theme = (setting_theme + delta + 6) % 6;
      PopulateVideoSettings();
      selected_idx = 4;
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
      snprintf(msg, sizeof(msg), "FM AUDIO: %s (recarregue o jogo)",
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
      snprintf(msg, sizeof(msg), "ESPACO: %s", kNdsGapVals[setting_nds_gap]);
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
      snprintf(msg, sizeof(msg), "TELA PEQUENA: %s",
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
                       ? "CACHE DO CD: LIGADO (recarregue o jogo)"
                       : "CACHE DO CD: DESLIGADO",
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
                       ? "ACESSO DO CD: RAPIDO (pode falhar em alguns jogos)"
                       : "ACESSO DO CD: REAL",
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
      selected_idx = 6;
    } else if (item.action_id == 312) // OSD auto-hide
    {
      setting_osd_timeout =
          (setting_osd_timeout + delta + kOsdTimeoutCount) % kOsdTimeoutCount;
      char msg[64];
      if (kOsdTimeouts[setting_osd_timeout] == 0)
        snprintf(msg, sizeof(msg), "MENU: NAO OCULTA SOZINHO");
      else
        snprintf(msg, sizeof(msg), "MENU OCULTA APOS %ds SEM USO",
                 kOsdTimeouts[setting_osd_timeout]);
      CoreSetToast(msg, 150);
      PopulateVideoSettings();
      selected_idx = 8;
    } else if (item.action_id == 313) // Name scroll delay
    {
      setting_name_scroll =
          (setting_name_scroll + delta + kScrollDelayCount) % kScrollDelayCount;
      char msg[64];
      if (kScrollDelays[setting_name_scroll] == 0)
        snprintf(msg, sizeof(msg), "ROLAGEM DE NOME: DESATIVADA");
      else
        snprintf(msg, sizeof(msg), "ROLAGEM DE NOME APOS %ds PARADO",
                 kScrollDelays[setting_name_scroll]);
      CoreSetToast(msg, 150);
      PopulateVideoSettings();
      selected_idx = 12;
    } else if (item.action_id == 311) // Arcade core
    {
      setting_arcade_core = (setting_arcade_core + delta + 4) % 4;
      const char *names[] = {"FINALBURN NEO", "MAME 2003 (romset 0.78)",
                             "MAME 2010 (romset 0.139)",
                             "FLYCAST (NAOMI / ATOMISWAVE)"};
      char msg[80];
      snprintf(msg, sizeof(msg), "ARCADE: %s", names[setting_arcade_core]);
      CoreSetToast(msg, 180);
      PopulateVideoSettings();
      selected_idx = 7;
    } else if (item.action_id == 308) // V-Sync
    {
      setting_vsync = (setting_vsync + delta + 2) % 2;
      CoreSetToast(setting_vsync == 1 ? "V-SYNC ENABLED" : "V-SYNC DISABLED",
                   120);
      PopulateVideoSettings();
      selected_idx = 9;
    } else if (item.action_id == 307) // Sincronia
    {
      setting_sync = (setting_sync + delta + 2) % 2;
      CoreSetDisplaySync(setting_sync == 1, CoreGetDisplayFps());
      CoreSetToast(setting_sync == 1 ? "SYNC: DISPLAY" : "SYNC: NATIVE", 120);
      PopulateVideoSettings();
      selected_idx = 10;
    } else if (item.action_id == 306) // Video Driver
    {
      setting_driver = (setting_driver + delta + kVideoDriverCount) % kVideoDriverCount;
      char msg[64];
      if (setting_driver == 0) {
        snprintf(msg, sizeof(msg), "DRIVER: SOFTWARE (CPU)");
      } else {
        snprintf(msg, sizeof(msg), "DRIVER: %s (3D GPU ATIVADO)", kVideoDrivers[setting_driver]);
      }
      CoreSetToast(msg, 90);
      PopulateVideoSettings();
      selected_idx = 5;
    } else if (item.action_id == 401) // Mute
    {
      CoreSetMute(!CoreGetMute());
      PopulateAudioSettings();
      selected_idx = 0;
    } else if (item.action_id == 402) // Latency
    {
      setting_latency = (setting_latency + delta + 4) % 4;
      PopulateAudioSettings();
      selected_idx = 1;
    } else if (item.action_id == 403) // Volume
    {
      int v = std::clamp(CoreGetVolume() + delta * 5, 0, 100);
      CoreSetVolume(v);
      PopulateAudioSettings();
      selected_idx = 2;
    } else if (item.action_id == 501) // System Type
    {
      neo_sys = (neo_sys + delta + 3) % 3;
      // Geolith accepts aes | mvs | uni. "cdz" is not a system type and
      // left the core undefined - this is the Neo Geo CD black screen.
      const char *sys_vals[] = {"aes", "mvs", "uni"};
      CoreSetOption("geolith_system_type", sys_vals[neo_sys]);
      PopulateMainMenu();
      selected_idx = 2;
    } else if (item.action_id == 502) // BIOS
    {
      neo_bios = (neo_bios + delta + 2) % 2;
      // Tells the Universe BIOS which hardware to detect.
      const char *bios_vals[] = {"aes", "mvs"};
      CoreSetOption("geolith_unibios_hw", bios_vals[neo_bios]);
      PopulateMainMenu();
      selected_idx = 3;
    } else if (item.action_id == 503) // CD Type
    {
      neo_cd_type = (neo_cd_type + delta + 4) % 4;
      const char *cd_vals[] = {"cdz", "cd_top", "cd_front", "cdz_unibios"};
      CoreSetOption("geolith_cd_system_type", cd_vals[neo_cd_type]);
      PopulateMainMenu();
      selected_idx = 4;
    } else if (item.action_id == 504) // CD Region
    {
      neo_cd_region = (neo_cd_region + delta + 4) % 4;
      const char *reg_vals[] = {"us", "jp", "as", "eu"};
      CoreSetOption("geolith_region", reg_vals[neo_cd_region]);
      PopulateMainMenu();
      selected_idx = 5;
    } else if (item.action_id == 505) // Memory Card
    {
      neo_memcard = (neo_memcard + delta + 2) % 2;
      const char *mc_vals[] = {"on", "off"};
      CoreSetOption("geolith_memcard", mc_vals[neo_memcard]);
      PopulateMainMenu();
      selected_idx = 6;
    } else if (item.action_id == 506) // [DIP] Settings
    {
      neo_dip_settings = (neo_dip_settings + delta + 2) % 2;
      const char *dip_vals[] = {"off", "on"};
      CoreSetOption("geolith_settingmode", dip_vals[neo_dip_settings]);
      PopulateMainMenu();
      selected_idx = 7;
    } else if (item.action_id == 507) // [DIP] Freeplay
    {
      neo_dip_freeplay = (neo_dip_freeplay + delta + 2) % 2;
      const char *fp_vals[] = {"off", "on"};
      CoreSetOption("geolith_freeplay", fp_vals[neo_dip_freeplay]);
      PopulateMainMenu();
      selected_idx = 8;
    } else if (item.action_id == 500) // Dispositivo (Gamepad vs Teclado)
    {
      setting_pad_device = (setting_pad_device + delta + 2) % 2;
      PopulateControllerSettings();
      selected_idx = 1;
    } else if (item.action_id == 508) // Deadzone
    {
      setting_deadzone = (setting_deadzone + delta + 4) % 4;
      PopulateControllerSettings();
      selected_idx = 2;
    }
    break;
  }

  case KEY_SELECT: {
    const auto &item = items[selected_idx];

    if (item.action_id == 99) // Exit
    {
      exit(0);
    } else if (item.action_id == 999) // Back
    {
      current_state = STATE_SETTINGS;
      PopulateSettings();
    } else if (current_state == STATE_MAIN) {
      if (item.action_id == 1) // Load another game from the same folder
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
        OsdDisable();
      } else if (item.action_id == 3) // Load State
      {
        CoreLoadState(CoreGetSelectedSlot());
        OsdDisable();
      } else if (item.action_id == 5) // Screenshot
      {
        CoreTakeScreenshot();
        OsdDisable();
      } else if (item.action_id == 6) // Reset Core
      {
        CoreReset();
        OsdDisable();
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
      } else if (item.action_id >= 101 && item.action_id <= 135) {
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
      // "Define Buttons" is on the in-game menu, but its handler only existed
      // under STATE_SETTINGS, so pressing it there did nothing at all.
      else if (item.action_id == 203) {
        current_state = STATE_CONTROLLER;
        ControllerPageOnEnter();
        PopulateControllerSettings();
      }
    } else if (current_state == STATE_SETTINGS) {
      if (item.action_id == 201) {
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
      } else if (item.action_id == 206) {
        current_state = STATE_ABOUT;
        PopulateRetroAchievements();
      } else if (item.action_id == 205) {
        current_state = STATE_ABOUT;
        PopulateAbout();
      } else if (item.action_id == 210) {
        ResetAllSettingsToDefault();
        PopulateSettings();
        CoreSetToast("CONFIGURACOES RESTAURADAS AO PADRAO", 180);
      }
    } else if (current_state == STATE_CONTROLLER) {
      if (item.action_id == 500) {
        setting_pad_device = (setting_pad_device + 1) % 2;
        PopulateControllerSettings();
        selected_idx = 1;
      } else if (item.action_id >= 700 && item.action_id < 700 + BIND_COUNT) {
        capture_bind = item.action_id - 700;
        capture_armed = false;
        PopulateControllerSettings();
        selected_idx = kBindRow0 + capture_bind;
      } else if (item.action_id == 799) {
        InputBindResetDefaults();
        PopulateControllerSettings();
        MenuSaveSettings();
        CoreSetToast("CONTROLES RESTAURADOS AO PADRAO", 120);
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
      }
    } else if (current_state == STATE_VIDEO) {
      if (item.action_id == 304) {
        setting_fullscreen = !setting_fullscreen;
        PopulateVideoSettings();
        selected_idx = 3;
      } else if (item.action_id == 305) {
        setting_theme = (setting_theme + 1) % 6;
        PopulateVideoSettings();
        selected_idx = 4;
      }
    } else if (current_state == STATE_AUDIO) {
      if (item.action_id == 401) {
        CoreSetMute(!CoreGetMute());
        PopulateAudioSettings();
        selected_idx = 0;
      }
    } else if (current_state == STATE_UPDATE) {
      if (item.action_id == 391) {
        UpdaterCheckAsync(true);
        PopulateUpdate();
      } else if (item.action_id == 392) {
        UpdaterStartDownload();
        PopulateUpdate();
      } else if (item.action_id == 393) {
        CoreSetToast("REINICIANDO PARA ATUALIZAR...", 300);
        UpdaterApplyAndRestart();
      } else if (item.action_id == 399) {
        current_state = STATE_MAIN;
        PopulateMainMenu();
        selected_idx = main_menu_saved_idx;
      }
    } else if (current_state == STATE_BROWSE) {
      if (item.label == "<..>") {
        BrowseGoUp();
      } else if (current_dir == "ports") {
        // Ports & Recomp entries are not filesystem paths under roms/ - the
        // label is the port's display name, matched back against the same
        // list PopulateBrowse built it from to find the launchable id.
        //
        // Every row here already resolved to a real executable (see
        // PortGetAvailableList()), so this always takes PortLaunch()'s
        // launch-directly path in practice. It is still called
        // unconditionally rather than re-checking is_installed here too -
        // PortLaunch() is the single place that decides download-then-launch
        // vs. launch directly, and duplicating that check in the UI is how
        // an earlier version of this handler ended up calling it wrong.
        auto plist = PortGetAvailableList();
        for (const auto &p : plist) {
          if (p.name == item.label) {
            PortLaunch(p.id);
            break;
          }
        }
      } else if (item.is_folder) {
        fs::path next_p = fs::path(current_dir) / item.label;
        PopulateBrowse(next_p.string());
      } else {
        std::string full_rom_path =
            (fs::path(current_dir) / item.label).string();
        std::string ext = fs::path(item.label).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::string target_rom_path = full_rom_path;

        // Archives are extracted on the core thread now - doing it here
        // blocked the message pump for as long as the extraction took.
        // The core is resolved the same way for every entry point.
        std::string core_dll = MenuResolveCoreForPath(full_rom_path, current_dir);
        ApplyMsxMachineTypeOption(core_dll, ext);

        // Returns immediately; the worker thread reports back through
        // CoreIsLoading() / CoreIsRunning().
        if (CoreRequestLoad(target_rom_path.c_str(), core_dll.c_str())) {
          OsdDisable();
        }
      }
    }
    break;
  }
  case KEY_CANCEL:
    if (current_state == STATE_BROWSE) {
      BrowseGoUp();
    } else if (current_state == STATE_VIDEO || current_state == STATE_AUDIO ||
               current_state == STATE_CONTROLLER ||
               current_state == STATE_NETPLAY || current_state == STATE_ABOUT) {
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
  // One resolver for the OSD and for the command line: the two used to carry
  // separate copies of this and had already diverged.
  std::string core_dll = MenuResolveCoreForPath(full_rom_path, path_str);

  if (core_dll.empty())
    return false;

  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  ApplyMsxMachineTypeOption(core_dll, ext);

  ApplyPersistedCoreOptions();

  if (CoreRequestLoad(full_rom_path.c_str(), core_dll.c_str())) {
    OsdDisable();
    return true;
  }
  return false;
}

#ifndef SYSTEM_REGISTRY_H_INCLUDED
#define SYSTEM_REGISTRY_H_INCLUDED

#include <stddef.h>
#include <string>
#include <filesystem>
#include <algorithm>

struct SystemDef {
  int         action_id;
  const char* id;           // Unique system identifier code (e.g. "SNES", "Genesis")
  const char* name;         // Canonical display name
  const char* rom_dir;      // Standard relative roms directory (e.g. "roms/SNES" or "ports")
  const char* alt_dir;      // Subdirectory name under roms/ (e.g. "SNES", "PSX", "N64")
  const char* core_name;    // Canonical engine name from CREDITS.md
  const char* category;     // "Consoles", "Portateis", "Computadores", "Arcade e Ports"
};

// Canonical list of all 38 systems (37 emulated consoles/computers + PC Ports).
// Single source of truth for menu.cpp, web_admin.cpp, and tests.
inline constexpr SystemDef kSystemRegistry[] = {
  { 117, "Arcade",        "Arcade",                 "roms/Arcade",        "Arcade",        "MAME 0.289 / 2003 / 2010", "Arcade e Ports" },
  { 101, "Atari2600",     "Atari 2600",             "roms/Atari2600",     "Atari2600",     "Stella",                "Consoles" },
  { 129, "Atari5200",     "Atari 5200",             "roms/Atari5200",     "Atari5200",     "Atari800",              "Consoles" },
  { 130, "Atari7800",     "Atari 7800",             "roms/Atari7800",     "Atari7800",     "ProSystem",             "Consoles" },
  { 128, "Jaguar",        "Atari Jaguar",           "roms/Jaguar",        "Jaguar",        "Virtual Jaguar",        "Consoles" },
  { 134, "Lynx",          "Atari Lynx",             "roms/Lynx",          "Lynx",          "Handy",                 "Portateis" },
  { 131, "ColecoVision",  "ColecoVision",           "roms/ColecoVision",  "ColecoVision",  "(nao identificado)",    "Consoles" },
  { 124, "C64",           "Commodore 64",           "roms/C64",           "C64",           "VICE",                  "Computadores" },
  { 123, "Amiga",         "Commodore Amiga",        "roms/Amiga",         "Amiga",         "PUAE",                  "Computadores" },
  { 112, "Dreamcast",     "Dreamcast",              "roms/Dreamcast",     "Dreamcast",     "Flycast",               "Consoles" },
  { 137, "FMTowns",       "FM Towns / Marty",       "roms/FMTowns",       "FMTowns",       "Geartowns",             "Computadores" },
  { 119, "GameBoy",       "Game Boy / Color",       "roms/GameBoy",       "GB",            "Gambatte",              "Portateis" },
  { 118, "GBA",           "Game Boy Advance",       "roms/GBA",           "GBA",           "mGBA",                  "Portateis" },
  { 113, "GameCube",      "GameCube",               "roms/GameCube",      "GameCube",      "Dolphin",               "Consoles" },
  { 102, "Genesis",       "Genesis / Mega Drive",   "roms/Genesis",       "Genesis",       "Genesis Plus GX",       "Consoles" },
  { 103, "MasterSystem",  "Master System",          "roms/MasterSystem",  "MasterSystem",  "Gearsystem",            "Consoles" },
  { 104, "MegaCD",        "Mega CD",                "roms/MegaCD",        "MegaCD",        "Genesis Plus GX",       "Consoles" },
  { 121, "DOS",           "MS-DOS",                 "roms/DOS",           "DOS",           "DOSBox Pure",           "Computadores" },
  { 122, "MSX",           "MSX / MSX2",             "roms/MSX",           "MSX",           "fMSX",                  "Computadores" },
  { 136, "PC98",          "NEC PC-98",              "roms/PC98",          "PC98",          "NP2kai",                "Computadores" },
  { 105, "NES",           "NES / Famicom",          "roms/NES",           "NES",           "Mesen",                 "Consoles" },
  { 132, "NGP",           "Neo Geo Pocket",         "roms/NGP",           "NGP",           "Beetle NeoPop",         "Portateis" },
  { 106, "NeoGeo",        "NeoGeo / CD",            "roms/NeoGeo",        "NeoGeo",        "Geolith / NeoCD",       "Arcade e Ports" },
  { 115, "3DS",           "Nintendo 3DS",           "roms/3DS",           "3DS",           "Citra",                 "Portateis" },
  { 107, "Nintendo64",    "Nintendo 64",            "roms/Nintendo64",    "N64",           "ParaLLEl / Gopher64 / Mupen64Plus-Next", "Consoles" },
  { 114, "NDS",           "Nintendo DS",            "roms/NDS",           "NDS",           "melonDS",               "Portateis" },
  { 127, "3DO",           "Panasonic 3DO",          "roms/3DO",           "3DO",           "Opera",                 "Consoles" },
  { 135, "PCFX",          "PC-FX",                  "roms/PCFX",          "PCFX",          "Beetle PC-FX",          "Consoles" },
  { 108, "PlayStation",   "PlayStation 1",          "roms/PlayStation",   "PSX",           "Beetle PSX",            "Consoles" },
  { 116, "PlayStation2",  "PlayStation 2 (PS2)",    "roms/PlayStation2",  "PS2",           "LRPS2 (PCSX2) / Play!", "Consoles" },
  { 120, "PSP",           "PlayStation Portable",   "roms/PSP",           "PSP",           "PPSSPP",                "Portateis" },
  { 140, "Ports",         "Ports & Recomp",         "ports",              "ports",         "Ports nativos",         "Arcade e Ports" },
  { 110, "Saturn",        "Saturn",                 "roms/Saturn",        "Saturn",        "Beetle Saturn",         "Consoles" },
  { 126, "32X",           "Sega 32X",               "roms/32X",           "32X",           "PicoDrive",             "Consoles" },
  { 109, "SNES",          "Super Nintendo (SNES)",   "roms/SNES",          "SNES",          "bsnes",                 "Consoles" },
  { 111, "TurboGrafx16",  "TurboGrafx 16 / PCE",    "roms/TurboGrafx16",  "PCEngine",      "Beetle PCE",            "Consoles" },
  { 133, "WonderSwan",    "WonderSwan",             "roms/WonderSwan",    "WonderSwan",    "Beetle WonderSwan",     "Portateis" },
  { 125, "ZXSpectrum",    "ZX Spectrum",            "roms/ZXSpectrum",    "ZXSpectrum",    "Fuse",                  "Computadores" }
};

inline constexpr size_t kSystemRegistryCount = sizeof(kSystemRegistry) / sizeof(kSystemRegistry[0]);

// Find system definition by Libretro action ID (101..137, 140)
inline const SystemDef* FindSystemByActionId(int action_id) {
  for (size_t i = 0; i < kSystemRegistryCount; ++i) {
    if (kSystemRegistry[i].action_id == action_id) return &kSystemRegistry[i];
  }
  return nullptr;
}

// Find system definition by identifier code or folder name
inline const SystemDef* FindSystemById(const std::string& id_or_alt) {
  for (size_t i = 0; i < kSystemRegistryCount; ++i) {
    if (id_or_alt == kSystemRegistry[i].id ||
        id_or_alt == kSystemRegistry[i].alt_dir ||
        id_or_alt == kSystemRegistry[i].rom_dir)
      return &kSystemRegistry[i];
  }
  return nullptr;
}

// Canonical resolution of system ROM directory across root and app/
inline std::filesystem::path ResolveSystemRomDir(const std::string& id_or_dir) {
  namespace fs = std::filesystem;
  std::error_code ec;

  if (id_or_dir == "Ports" || id_or_dir == "ports") {
    fs::path p1 = fs::path("ports");
    if (fs::exists(p1, ec)) return p1;
    fs::path p2 = fs::path("app/ports");
    if (fs::exists(p2, ec)) return p2;
    return p1;
  }

  // 1. Check direct matches in roms/<dir> and app/roms/<dir>
  fs::path direct1 = fs::path("roms") / id_or_dir;
  if (fs::is_directory(direct1, ec)) return direct1;
  fs::path direct2 = fs::path("app/roms") / id_or_dir;
  if (fs::is_directory(direct2, ec)) return direct2;

  // 2. Check matched entry in registry
  const SystemDef* sys = FindSystemById(id_or_dir);
  if (sys) {
    fs::path p1 = fs::path(sys->rom_dir);
    if (fs::is_directory(p1, ec)) return p1;
    fs::path p2 = fs::path("app") / sys->rom_dir;
    if (fs::is_directory(p2, ec)) return p2;

    fs::path pa1 = fs::path("roms") / sys->alt_dir;
    if (fs::is_directory(pa1, ec)) return pa1;
    fs::path pa2 = fs::path("app/roms") / sys->alt_dir;
    if (fs::is_directory(pa2, ec)) return pa2;

    return p1;
  }

  return direct1;
}

// Canonical rule for whether a directory entry qualifies as an executable game
inline bool IsValidRomEntry(const std::filesystem::directory_entry& entry, bool is_ports = false) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::string fn = entry.path().filename().string();
  if (fn.empty() || fn[0] == '.' || fn == "desktop.ini" || fn == "Thumbs.db" ||
      fn == "gamelist.xml" || fn == "gamelist.json")
    return false;

  if (entry.is_directory(ec)) {
    // Exclude system and operational subfolders
    std::string lower_dir = fn;
    for (char &c : lower_dir) c = (char)tolower((unsigned char)c);
    if (lower_dir == "cache" || lower_dir == "config" || lower_dir == "scripts" ||
        lower_dir == "wallpapers" || lower_dir == "bios" || lower_dir == "cores" ||
        lower_dir == "saves" || lower_dir == "cheats") {
      return false;
    }
    // Arcade CHD data folders (e.g. sfiii3, cvs2) companion to parent ROMs
    std::string parent_name = entry.path().parent_path().filename().string();
    if (parent_name == "Arcade" || parent_name == "arcade") {
      std::error_code ec_sub;
      for (const auto &sub : fs::directory_iterator(entry.path(), ec_sub)) {
        if (ec_sub) break;
        std::string sub_ext = sub.path().extension().string();
        for (char &c : sub_ext) c = (char)tolower((unsigned char)c);
        if (sub_ext == ".chd") return false;
      }
    }
    // Valid subdirectories count as games (DOS folders, multidisc, ScummVM, Ports, etc.)
    return true;
  }

  if (entry.is_regular_file(ec)) {
    if (is_ports) return true;
    size_t dot = fn.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = fn.substr(dot);
    for (char &c : ext) c = (char)tolower((unsigned char)c);

    // Canonical extension list supported by all Karamelo cores
    static const char* const kExts[] = {
      ".sfc", ".smc", ".nes", ".fds", ".md", ".gen", ".smd", ".bin", ".32x",
      ".sms", ".gg", ".gba", ".gb", ".gbc", ".z64", ".n64", ".v64", ".nds",
      ".3ds", ".cci", ".cxi", ".gcm", ".rvz", ".iso", ".chd", ".cue", ".pbp",
      ".cso", ".zip", ".7z", ".rar", ".pce", ".rom", ".img", ".col", ".lnx",
      ".j64", ".jag", ".ws", ".wsc", ".ngp", ".ngc", ".mx1", ".mx2", ".dsk",
      ".adf", ".d64", ".prg", ".tap", ".tzx", ".z80", ".sna", ".hdi", ".fdi",
      ".d88", ".mec", ".nvm", ".wav", ".exe", ".com", ".bat", ".m3u", ".gdi",
      ".toc"
    };
    for (const char* a : kExts) {
      if (ext == a) return true;
    }
  }
  return false;
}

// Unified game counting logic across OSD menu and Web Studio LAN
inline int CountGamesInDirectory(const std::filesystem::path& dir, bool is_ports = false) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return 0;
  int count = 0;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    if (IsValidRomEntry(entry, is_ports)) {
      count++;
    }
  }
  return count;
}

#endif // SYSTEM_REGISTRY_H_INCLUDED


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
extern char **environ;
#endif
#include <SDL3/SDL.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <system_error>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>
#include <stdio.h>

#include "port_runner.h"
#include "port_catalog.h"
#include "core_runner.h"
#include "menu.h"
#include "osd.h"
#include "archive_helper.h"
#include "updater.h"
#include "x360_dump.h"

namespace fs = std::filesystem;

// Minimize/restore/raise the main window through SDL3, which works the same
// on every platform SDL3 supports - a raw HWND (MainGetHwnd(), still used
// elsewhere in this project) only exists on Windows.
extern SDL_Window* MainGetSdlWindow();

static std::atomic<bool> s_port_running(false);
static std::atomic<bool> s_installing(false);

// Every detached background thread this file spawns (the download-install
// thread, the process-exit-monitor thread) increments this at the top and
// decrements it right before returning - after its last touch of any shared
// state, CoreSetToast() included. Nothing previously waited for these
// threads before the app could tear down toast_lock (RecoverAfterKilledCore,
// on a hung-core kill happening at the same moment) or exit the process
// while one was still mid-callback. PortShutdown() polls this to drain.
static std::atomic<int> s_active_bg_threads(0);

// A finished background install used to hand off to the launch sequence by
// calling PortLaunch() recursively from the install thread itself. That put
// CoreShutdown(), the window minimize/restore calls, and CreateProcessW all
// on a worker thread while the UI thread can independently call the very
// same CoreShutdown()/CoreRequestLoad() at any moment (e.g. the user backs
// out and loads a different ROM while the download is still running) -
// h_core_thread inside core_runner.cpp is plain, unlocked state, so that was
// a real cross-thread race, not just a theoretical one. The install thread
// now only ever sets this instead, and PortPumpPendingLaunch() - called once
// per frame from the main loop, i.e. always the UI thread - is the only
// place that actually performs the launch.
static std::mutex       s_pending_lock;
static std::string      s_pending_launch_id;
static std::atomic<bool> s_has_pending_launch(false);
static std::atomic<bool> s_port_needs_restore(false);
// Nome do port que esta baixando, para o aviso de progresso (ver o topo de
// PortPumpPendingLaunch). Protegido pelo mesmo s_pending_lock.
static std::string      s_installing_name;
// "BAIXANDO " or "PREPARANDO " (Xbox 360 dump preparation) - same lock.
static std::string      s_installing_verb = Tr("BAIXANDO ", "DOWNLOADING ");

#ifdef _WIN32
// One argument quoted per the CommandLineToArgvW / MSVC CRT rules, trailing
// backslashes included - the same routine upstream's own launcher uses.
static std::wstring QuoteWinArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
#endif

static std::string ToLowerStr(const std::string& s) {
    std::string out = s;
    // unsigned char first: a UTF-8 byte is negative as a plain char, and
    // tolower() of a negative value other than EOF is undefined.
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

// -------------------------------------------------------------
// Known ports registry
// -------------------------------------------------------------
// PortDefinition and the catalog format live in port_catalog.h.

static bool NameHasKeyword(const std::string& lower_name, const std::string& kw, bool whole_word) {
    if (!whole_word) return lower_name.find(kw) != std::string::npos;
    auto is_word = [](char c) { return std::isalnum((unsigned char)c) != 0; };
    for (size_t at = lower_name.find(kw); at != std::string::npos; at = lower_name.find(kw, at + 1)) {
        bool starts = at == 0 || !is_word(lower_name[at - 1]);
        size_t end = at + kw.size();
        bool ends = end >= lower_name.size() || !is_word(lower_name[end]);
        if (starts && ends) return true;
    }
    return false;
}

// Every rom_keywords entry present and no rom_exclude_keywords entry.
static bool RomNameMatches(const PortDefinition& def, const std::string& lower_name) {
    for (const auto& kw : def.rom_keywords)
        if (lower_name.find(kw) == std::string::npos) return false;
    for (const auto& kw : def.rom_exclude_keywords)
        if (lower_name.find(kw) != std::string::npos) return false;
    return true;
}

// -------------------------------------------------------------
// Catalog loading
// -------------------------------------------------------------
// The URL the app checks for a newer catalog. Published by the deploy
// scripts next to version.json (see docs/PORTS_CATALOG.md).
static const char* kPortsCatalogUrl = "https://karamelo-emu.com/downloads/ports.json";
// A newer catalog downloaded in an earlier run. Read once at startup; the
// download itself never changes the list the running app is showing.
static const char* kPortsCatalogCache = "Config/ports_cache.json";

static void CatalogLog(const std::string& line) {
    FILE* lf = fopen("karamelo.log", "a");
    if (!lf) return;
    fprintf(lf, "[INFO] [PORT-CATALOG] %s\n", line.c_str());
    fclose(lf);
}

// The embedded copy always parses (the unit tests guarantee it). A cached
// download wins only when it parses, validates and is newer - a damaged or
// older cache is ignored, never trusted over what shipped in the binary.
static PortCatalog LoadActiveCatalog() {
    PortCatalog embedded;
    std::string err;
    if (!PortCatalogParse(PortCatalogEmbeddedJson(), embedded, err))
        CatalogLog("catalogo embutido invalido: " + err);

    std::ifstream f(kPortsCatalogCache, std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        PortCatalog cached;
        if (!PortCatalogParse(text, cached, err)) {
            CatalogLog(std::string(kPortsCatalogCache) + " ignorado: " + err);
        } else if (cached.catalog_version > embedded.catalog_version) {
            CatalogLog("usando catalogo baixado v" + std::to_string(cached.catalog_version) +
                       " (embutido v" + std::to_string(embedded.catalog_version) + "), " +
                       std::to_string(cached.ports.size()) + " ports");
            return cached;
        }
    }
    CatalogLog("usando catalogo embutido v" + std::to_string(embedded.catalog_version) + ", " +
               std::to_string(embedded.ports.size()) + " ports");
    return embedded;
}

// Loaded once, on first use, and never replaced while the app runs: callers
// keep PortDefinition pointers (FindPortDef) for as long as they like.
static const PortCatalog& ActiveCatalog() {
    static const PortCatalog cat = LoadActiveCatalog();
    return cat;
}

static const std::vector<PortDefinition>& KnownPortDefs() {
    return ActiveCatalog().ports;
}

static const PortDefinition* FindPortDef(const std::string& id) {
    for (const auto& d : KnownPortDefs())
        if (d.id == id) return &d;
    return nullptr;
}

static std::string FindBestExecutable(const std::string& dir);

// The executable a port folder resolves to: the exact exe_relpath when the
// definition pins one (empty if that file is not there yet, so the port reads
// as "not installed" and gets downloaded), otherwise checks exe_hint if present,
// falling back to the usual guess.
static std::string ResolvePortExe(const PortDefinition* def, const std::string& dir) {
    if (dir.empty()) return "";
    if (def && !def->exe_relpath.empty()) {
        std::error_code ec;
        fs::path p = fs::path(dir) / def->exe_relpath;
        return fs::is_regular_file(p, ec) ? p.string() : "";
    }
    if (def && !def->exe_hint.empty()) {
        std::error_code ec;
        fs::path p = fs::path(dir) / def->exe_hint;
        if (fs::is_regular_file(p, ec)) return p.string();
#ifndef _WIN32
        std::string hint = def->exe_hint;
        if (hint.size() > 4 && hint.substr(hint.size() - 4) == ".exe") {
            hint = hint.substr(0, hint.size() - 4);
            fs::path p_unix = fs::path(dir) / hint;
            if (fs::is_regular_file(p_unix, ec)) return p_unix.string();
#ifdef __APPLE__
            for (const auto& sdir : fs::directory_iterator(dir, ec)) {
                if (ToLowerStr(sdir.path().extension().string()) == ".app") {
                    fs::path app_exe = sdir.path() / "Contents" / "MacOS" / hint;
                    if (fs::is_regular_file(app_exe, ec)) return app_exe.string();
                }
            }
#endif
        }
#endif
    }
    return FindBestExecutable(dir);
}

// -------------------------------------------------------------
// Filesystem helpers
// -------------------------------------------------------------

// Picks the executable to launch when more than one candidate qualifies.
// Alphabetical is an arbitrary but stable tie-break - same idea as the
// reference launcher's ThenBy(filename), just without pulling in a full
// natural-sort so a real ambiguous case is at least deterministic rather
// than "whatever the filesystem enumerated first".
static std::string BestExeAmong(const std::vector<fs::path>& candidates) {
    std::string best;
    for (const auto& p : candidates) {
        std::string s = p.string();
        if (best.empty() || s < best) best = s;
    }
    return best;
}

// Finds the .exe to launch, preferring the shallowest match over the
// largest file. Ported from GitHubLauncher.Core's FindExecutableCandidates:
// a release zip that wraps its real exe in one folder gets flattened before
// this runs (see FlattenSingleSubfolder), so by the time there is a choice
// to make, a top-level file is the game and anything only found nested -
// redistributables, uninstallers, crash handlers - is not. "Largest file
// anywhere" was the guess this replaced; it happened to work for Dr. Mario
// 64 but had no real basis once other projects' folder layouts differ.
#ifndef _WIN32
// Verified 11/09/2026 against 16 real Linux releases downloaded and
// extracted from the ports this app actually offers: every single one
// failed against a "no extension at all" rule, for reasons discovered by
// that pass, not assumed in advance - Godot exports keep the target triple
// on the binary name ("SMB1R.x86_64"), several projects ship a single-file
// AppImage (".appimage" is not empty either, and it's the one extension
// that genuinely is a runnable binary), and the same applies to whatever
// macOS command-line binaries or naming conventions turn up next - trying
// to keep an allowlist of every extension a real release might use is a
// losing game. A Linux or macOS binary having *some* extension is normal,
// so the executable permission bit is what actually decides here;
// extension is used only to reject file kinds no port ever launches even
// when marked executable (shared libraries, data packs, build/setup
// scripts, docs).
static bool IsUnixNeverExecutable(const std::string& lower_ext) {
    static const std::vector<std::string> never = {
        ".so", ".dll", ".dylib", ".pck", ".pak", ".dat",
        ".py", ".sh", ".pl", ".rb",
        ".txt", ".md", ".yml", ".yaml", ".json", ".cfg", ".ini", ".toml",
        ".zip", ".tar", ".gz", ".xz", ".7z",
        ".log", ".pdb", ".map", ".a", ".o"
    };
    for (const auto& e : never) if (lower_ext == e) return true;
    return false;
}

// Verified 21/09/2026 running `--list-ports` for real: the extension
// denylist plus the executable permission bit above picks LICENSE, COPYING,
// a .sav, a .pdf and even a ROM as "the executable" the moment ports/ sits
// on a filesystem that does not carry real Unix permission bits - a Windows
// drive mounted into WSL (9p/drvfs) reports every file as rwxrwxrwx
// regardless of content, and the same is true of exFAT/NTFS drives some
// people keep their game library on for cross-platform use. The permission
// bit was never the wrong idea, just not sufficient on its own - this reads
// the file's own first bytes and requires the real magic number of a
// native binary (ELF on Linux, Mach-O - plain or fat/universal - on macOS),
// which no filesystem can fake. An AppImage passes this too: it is a real
// ELF with a filesystem image appended, not a different format.
static bool LooksLikeRealBinary(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char magic[4] = { 0, 0, 0, 0 };
    size_t n = fread(magic, 1, 4, f);
    fclose(f);
    if (n < 4) return false;
#if defined(__APPLE__)
    // Mach-O:
    // 64-bit little-endian (Apple Silicon ARM64 & x86_64): CF FA ED FE
    // 32-bit little-endian: CE FA ED FE
    // 64-bit big-endian (PowerPC): FE ED FA CF
    // 32-bit big-endian (PowerPC): FE ED FA CE
    // Fat/universal binary: CA FE BA BE or BE BA FE CA (or CA FE BA BF / BF BA FE CA)
    if (magic[0] == 0xCF && magic[1] == 0xFA && magic[2] == 0xED && magic[3] == 0xFE) return true;
    if (magic[0] == 0xCE && magic[1] == 0xFA && magic[2] == 0xED && magic[3] == 0xFE) return true;
    if (magic[0] == 0xFE && magic[1] == 0xED && magic[2] == 0xFA &&
        (magic[3] == 0xCE || magic[3] == 0xCF)) return true;
    if (magic[0] == 0xCA && magic[1] == 0xFE && magic[2] == 0xBA &&
        (magic[3] == 0xBE || magic[3] == 0xBF)) return true;
    if ((magic[0] == 0xBE || magic[0] == 0xBF) && magic[1] == 0xBA && magic[2] == 0xFE && magic[3] == 0xCA) return true;
    return false;
#else
    return magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
#endif
}
#endif

static std::string FindBestExecutable(const std::string& dir) {
    static const std::vector<std::string> blacklist = {
        "unins000.exe", "uninstall.exe", "vc_redist", "dxwebsetup", "dxsetup",
        "crashpad_handler.exe", "crashhandler.exe", "vcredist", "updater.exe",
        "update.exe", "patcher.exe",
        // One-time asset-extraction/build tools bundled alongside the actual
        // game runtime (OpenGOAL's Jak & Daxter port ships "extractor.exe";
        // Star Fox Enhanced's own Windows zip ships "starfox_asset_builder.
        // exe" *inside* the same archive as the real "starfox_pc.exe", not
        // just as a separate release asset) - never the thing to launch by
        // default, even though each is a perfectly normal top-level .exe
        // that would otherwise win the alphabetical tie-break.
        "extractor.exe", "asset_builder.exe", "assetbuilder.exe",
        // Bare (no ".exe") duplicates of the same intent, for Linux/macOS -
        // a release that ships "starfox_asset_builder" or "coopdx_updater"
        // with no extension at all sorts before the real game alphabetically
        // (SM64 Coop Deluxe's own Linux build does exactly this: the update
        // helper "coopdx_updater" would otherwise win over "sm64coopdx"),
        // and neither matches any of the ".exe"-suffixed entries above.
        "updater", "asset_builder", "assetbuilder", "extractor"
    };
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return "";

    std::vector<fs::path> top_level;
    std::vector<fs::path> subdirs;

    // Fast Pass 1: Check top level only
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) continue;
        if (e.is_directory(ec)) {
            subdirs.push_back(e.path());
            continue;
        }
        if (!e.is_regular_file(ec)) continue;

#ifdef _WIN32
        if (ToLowerStr(e.path().extension().string()) != ".exe") continue;
#else
        std::string ext = ToLowerStr(e.path().extension().string());
        if (IsUnixNeverExecutable(ext)) continue;
        auto perms = e.status(ec).permissions();
        if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) == fs::perms::none)
            continue;
        if (!LooksLikeRealBinary(e.path().string())) continue;
#endif

        std::string fname = ToLowerStr(e.path().filename().string());
        bool skip = false;
        for (const auto& b : blacklist)
            if (fname.find(b) != std::string::npos) { skip = true; break; }
        if (skip) continue;

        top_level.push_back(e.path());
    }

    if (!top_level.empty()) return BestExeAmong(top_level);

    // Root-level "launch.bat" (or "launch.sh" on Linux)
    std::error_code lb_ec;
    fs::path launch_bat = fs::path(dir) / "launch.bat";
    if (fs::exists(launch_bat, lb_ec)) return launch_bat.string();
#ifndef _WIN32
    fs::path launch_sh = fs::path(dir) / "launch.sh";
    if (fs::exists(launch_sh, lb_ec)) return launch_sh.string();
#endif

    // Fallback: Check only 1 level deep in immediate subdirs (skip assets/data)
    std::vector<fs::path> nested;
    for (const auto& sdir : subdirs) {
        std::string sname = ToLowerStr(sdir.filename().string());
        if (sname == "assets" || sname == "sound" || sname == "audio" || sname == "roms" ||
            sname == "textures" || sname == "music" || sname == "data")
            continue;

        for (const auto& e : fs::directory_iterator(sdir, ec)) {
            if (ec || !e.is_regular_file(ec)) continue;
#ifdef _WIN32
            if (ToLowerStr(e.path().extension().string()) != ".exe") continue;
#else
            std::string ext = ToLowerStr(e.path().extension().string());
            if (IsUnixNeverExecutable(ext)) continue;
            auto perms = e.status(ec).permissions();
            if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) == fs::perms::none)
                continue;
            if (!LooksLikeRealBinary(e.path().string())) continue;
#endif
            std::string fname = ToLowerStr(e.path().filename().string());
            bool skip = false;
            for (const auto& b : blacklist)
                if (fname.find(b) != std::string::npos) { skip = true; break; }
            if (skip) continue;

            nested.push_back(e.path());
        }
    }

#ifdef __APPLE__
    fs::path direct_macos = fs::path(dir) / "Contents" / "MacOS";
    if (fs::exists(direct_macos, ec)) {
        for (const auto& e : fs::directory_iterator(direct_macos, ec)) {
            if (ec || !e.is_regular_file(ec)) continue;
            std::string fname = e.path().filename().string();
            if (fname.rfind("._", 0) == 0) continue;
            auto perms = e.status(ec).permissions();
            if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none) {
                return e.path().string();
            }
        }
    }
    for (const auto& sdir : subdirs) {
        if (ToLowerStr(sdir.extension().string()) == ".app") {
            fs::path macos_dir = sdir / "Contents" / "MacOS";
            if (fs::exists(macos_dir, ec)) {
                for (const auto& e : fs::directory_iterator(macos_dir, ec)) {
                    if (ec || !e.is_regular_file(ec)) continue;
                    std::string fname = e.path().filename().string();
                    if (fname.rfind("._", 0) == 0) continue;
                    auto perms = e.status(ec).permissions();
                    if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none) {
                        return e.path().string();
                    }
                }
            }
        }
    }
#endif

    // No .exe anywhere and no launch.bat at root: a JVM-based port might
    // still use a differently-named batch launcher. Root-level only - a
    // batch script found several folders deep is far more likely to be a
    // build/setup helper than the real launcher.
    std::vector<fs::path> scripts;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec || !e.is_regular_file(ec)) continue;
        std::string ext = ToLowerStr(e.path().extension().string());
        if (ext == ".bat" || ext == ".cmd") scripts.push_back(e.path());
    }
    return BestExeAmong(scripts);
}

// Resolves ports/<id> or app/ports/<id>, whichever exists, so this keeps
// working whether the process's cwd is the repo root or app/.
static std::string ResolvePortDir(const std::string& id) {
    std::error_code ec;
    if (fs::exists("ports/" + id, ec)) return "ports/" + id;
    if (fs::exists("app/ports/" + id, ec)) return "app/ports/" + id;
    return "";
}

// A release zip almost always wraps its contents in one top-level folder
// (GitHub's own "Source code" archives always do this, and most CI-built
// release zips copy the habit). Without flattening, ports/<id>/ would hold
// only that one subfolder and every path this file assumes - exe, working
// dir, auto-copied ROM - would be one level too shallow.
static void FlattenSingleSubfolder(const std::string& dir) {
    std::error_code ec;
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(dir, ec)) entries.push_back(e.path());
    if (ec || entries.size() != 1) return;
    if (!fs::is_directory(entries[0], ec)) return;

    fs::path inner = entries[0];
    for (const auto& e : fs::directory_iterator(inner, ec)) {
        fs::path target = fs::path(dir) / e.path().filename();
        fs::rename(e.path(), target, ec);
    }
    fs::remove(inner, ec);
}

// -------------------------------------------------------------
// GitHub releases API (minimal, string-scan parsing - same style as the
// self-updater's manifest parser, no JSON library pulled in for one field)
// -------------------------------------------------------------
struct GhAsset { std::string name; std::string url; };

static std::vector<GhAsset> ParseReleaseAssets(const std::string& json) {
    std::vector<GhAsset> out;
    size_t pos = 0;
    while (true) {
        size_t dl_pos = json.find("\"browser_download_url\"", pos);
        if (dl_pos == std::string::npos) break;

        size_t colon = json.find(':', dl_pos);
        size_t q1 = (colon == std::string::npos) ? std::string::npos : json.find('"', colon + 1);
        size_t q2 = (q1 == std::string::npos) ? std::string::npos : json.find('"', q1 + 1);
        if (q2 == std::string::npos) break;

        GhAsset asset;
        asset.url = json.substr(q1 + 1, q2 - q1 - 1);

        // GitHub's asset objects list "name" well before "browser_download_url",
        // so the nearest earlier occurrence belongs to this same asset.
        size_t name_pos = json.rfind("\"name\"", dl_pos);
        if (name_pos != std::string::npos) {
            size_t ncolon = json.find(':', name_pos);
            size_t nq1 = (ncolon == std::string::npos) ? std::string::npos : json.find('"', ncolon + 1);
            size_t nq2 = (nq1 == std::string::npos) ? std::string::npos : json.find('"', nq1 + 1);
            if (nq2 != std::string::npos)
                asset.name = json.substr(nq1 + 1, nq2 - nq1 - 1);
        }

        out.push_back(asset);
        pos = q2 + 1;
    }
    return out;
}

// /releases (plural) returns every release, newest first, as one JSON array.
// ParseReleaseAssets does not care which release object an asset came from,
// so scanning the whole array would mix in every past version's assets too -
// harmless for PickWindowsAsset's "first windows-looking zip" scan only
// because the newest release happens to serialize first, which is fragile to
// rely on. Truncating to just the first release object (every release has
// exactly one "tag_name", which no asset object carries) makes it correct
// instead of merely lucky.
static std::string FirstReleaseObjectOnly(const std::string& array_json) {
    size_t first = array_json.find("\"tag_name\"");
    if (first == std::string::npos) return array_json;
    size_t second = array_json.find("\"tag_name\"", first + 1);
    return (second == std::string::npos) ? array_json : array_json.substr(0, second);
}

static bool ContainsAny(const std::string& s, std::initializer_list<const char*> needles) {
    for (auto n : needles) if (s.find(n) != std::string::npos) return true;
    return false;
}

// Ported from the reference launcher's PlatformAssetMatcher.IsWindowsAsset
// (GitHubLauncher.Core/Services/PlatformAssetMatcher.cs) rather than
// reinvented, since asset-naming conventions across dozens of independent
// recomp projects are exactly the kind of thing worth copying from an
// implementation that already handles many of them correctly.
static bool IsWindowsAssetName(const std::string& lower_name) {
    // "appimage"/"flatpak" without requiring the leading dot: a release can
    // name the asset "..-AppImage-X64-Release.zip" (word in the middle of
    // the name, not a literal .AppImage extension) and still mean Linux -
    // BMHeroRecomp does exactly this, and the bare "-x64" in that name would
    // otherwise pass the generic-architecture fallback below as if it were a
    // Windows build.
    if (ContainsAny(lower_name, { "linux", "macos", "osx", "darwin", "apple", ".deb", ".rpm",
                                   "appimage", "flatpak", ".dmg", ".pkg", "switch", "android", "source" }))
        return false;
    if (ContainsAny(lower_name, { "windows", "win64", "win32", "win-x64", "win-x86",
                                   "-win.", "_win.", ".exe", ".msi", "msvc", "mingw", "cannonball" }))
        return true;
    // A handful of projects (Sonic 3 A.I.R. among them) tag their Windows zip
    // with just an architecture - "..._64bit.zip", "...-x64.zip" - and no
    // "win" marker at all. Safe to accept once every non-Windows OS marker
    // above has already ruled itself out.
    return ContainsAny(lower_name, { "64bit", "32bit", "-x64", "_x64" });
}

// Lower is more likely to actually run on the machine downloading it: almost
// every Windows PC is x64. 0 for an x64 (or unmarked/generic) build, 1 for a
// 32-bit build (i686/x86/win32 - still x64-compatible via WOW64, just not
// preferred), 2 for arm64 - a real architecture mismatch, not merely
// suboptimal. Symphony of the Night's recomp shipped exactly this trap: a
// "-windows-arm64.zip" and a "-windows-x64.zip" side by side, with arm64
// listed first, and picking whichever came first in the API's asset order
// downloaded a binary that failed to even start with a Windows architecture-
// mismatch error on this ordinary x64 test machine.
static int ArchPreferenceRank(const std::string& lower_name) {
    bool is_64 = ContainsAny(lower_name, { "x86_64", "x86-64" });
    if (!is_64 && ContainsAny(lower_name, { "arm64", "aarch64" })) return 2;
    if (!is_64 && ContainsAny(lower_name, { "i686", "x86", "win32", "32bit" })) return 1;
    return 0;
}

// A release can ship a companion dev/build tool as a second, equally
// "Windows" zip alongside the actual game - Star Fox Enhanced's release has
// both "StarFoxEnhanced-...-windows-x64.zip" and "StarFoxAssetBuilder-...-
// windows-x64.zip" side by side. Same shape of mistake as extractor.exe in
// FindBestExecutable's blacklist below, just one level earlier: this decides
// which *archive* to download in the first place, before there is anything
// on disk to inspect.
static bool LooksLikeCompanionTool(const std::string& lower_name) {
    return ContainsAny(lower_name, { "assetbuilder", "asset-builder", "toolkit", "buildtools",
                                      "build-tools", "sdk", "debug-symbols", "debug_symbols",
                                      "-pdb", "_pdb", "symbols", "-mods", "_mods", "mods.zip" });
}

// Prefers an archive over a bare .exe/.msi when a release offers both -
// a zip sibling is more likely to be the complete, current build, whereas a
// lone top-level .exe asset is sometimes a legacy or partial upload. Among
// several matching zips (Perfect Dark ships one per architecture, one per
// region; Star Fox Enhanced ships a companion tool alongside the real game),
// prefers whichever looks like the actual game over a 32-bit build or a
// build/dev tool, rather than just the first one found in whatever order the
// API happened to list them.
static GhAsset PickWindowsAsset(const std::vector<GhAsset>& assets) {
    auto is_archive = [](const std::string& n) {
        return (n.size() >= 4 && n.substr(n.size() - 4) == ".zip") ||
               (n.size() >= 4 && n.substr(n.size() - 4) == ".rar") ||
               (n.size() >= 3 && n.substr(n.size() - 3) == ".7z");
    };
    auto is_exe = [](const std::string& n) { return n.size() >= 4 && n.substr(n.size() - 4) == ".exe"; };

    std::vector<GhAsset> zip_matches;
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        if (is_archive(n) && IsWindowsAssetName(n) && !LooksLikeCompanionTool(n)) zip_matches.push_back(a);
    }
    if (!zip_matches.empty()) {
        const GhAsset* best = &zip_matches.front();
        int best_rank = ArchPreferenceRank(ToLowerStr(best->name));
        for (const auto& a : zip_matches) {
            int rank = ArchPreferenceRank(ToLowerStr(a.name));
            if (rank < best_rank) { best = &a; best_rank = rank; }
            if (best_rank == 0) break;
        }
        return *best;
    }

    for (const auto& a : assets) {
        if (IsWindowsAssetName(ToLowerStr(a.name))) return a;
    }

    // Untagged archive when all other assets are explicitly non-Windows/companion tools.
    // E.g. Chameleon Twist / Quest 64 (single .zip), ProjectCane (0.4.0-beta.1.rar alongside a Linux rar).
    std::vector<GhAsset> non_other;
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        bool other_os = ContainsAny(n, { "linux", "macos", "osx", "darwin", "apple", ".deb", ".rpm",
                                          ".appimage", ".dmg", ".pkg", "switch", "android" });
        if (!other_os && !LooksLikeCompanionTool(n) && (is_archive(n) || is_exe(n))) {
            non_other.push_back(a);
        }
    }
    if (non_other.size() == 1) {
        return non_other[0];
    }
    return GhAsset{};
}

#ifndef _WIN32
// Mirrors IsWindowsAssetName's structure but deliberately does not fall back
// to an unmarked/generic-architecture asset the way that one does: an
// untagged single .zip on a small hobby project's release is, in practice,
// almost always the Windows build (the default most of these recomp/native-
// port projects target first), not a Linux one, so guessing there would
// routinely download a Windows binary that can't run at all. Requiring an
// explicit Linux marker means a release with no Linux build simply returns
// no match - PickLinuxAsset then returns empty, DownloadAndInstall reports
// "no Linux build", and the caller can leave that port out of the list
// instead of offering something that will not run.
static bool IsLinuxAssetName(const std::string& lower_name) {
    if (ContainsAny(lower_name, { "windows", "win64", "win32", "win-x64", "win-x86",
                                   "-win.", "_win.", ".exe", ".msi", "msvc", "mingw",
                                   "macos", "osx", "darwin", "apple", ".dmg", ".pkg",
                                   "switch", "android", "source" }))
        return false;
    return ContainsAny(lower_name, { "linux", ".deb", ".rpm", "appimage", "flatpak", ".tar.gz", ".tar.xz" });
}

// Same shape as PickWindowsAsset (arch preference, companion-tool rejection,
// prefer an archive over a bare binary) but built on IsLinuxAssetName and
// without that function's "single untagged asset, just take it" fallback -
// see IsLinuxAssetName's own comment for why that fallback is Windows-only.
//
// Restricted to ".zip" even though IsLinuxAssetName also flags .tar.gz/
// .tar.xz/AppImage as Linux-shaped names: DownloadAndInstall's extraction
// step below only knows how to unpack a zip (ArchiveExtractAll's Linux
// backend is "unzip", nothing else). Picking a .tar.gz here would download
// something the pipeline then fails to extract - worse than just not
// offering the port. A release that ships only a .tar.gz/AppImage correctly
// falls through to "no Linux build" until extraction grows those formats too.
static GhAsset PickLinuxAsset(const std::vector<GhAsset>& assets) {
    auto is_archive = [](const std::string& n) {
        return (n.size() >= 4 && n.substr(n.size() - 4) == ".zip") ||
               (n.size() >= 4 && n.substr(n.size() - 4) == ".rar") ||
               (n.size() >= 3 && n.substr(n.size() - 3) == ".7z") ||
               (n.size() >= 7 && (n.substr(n.size() - 7) == ".tar.gz" || n.substr(n.size() - 7) == ".tar.xz")) ||
               (n.size() >= 9 && n.substr(n.size() - 9) == ".appimage") ||
               // A bare "linux" name is a loose binary - but not a checksum or
               // notes file that merely names the platform (CPS2Recomp ships
               // update-linux-x86_64.txt, F-Zero X a .tar.gz.sha256 sidecar).
               (n.find("linux") != std::string::npos && !ContainsAny(n, { ".apk", ".deb", ".rpm", "flatpak" }) &&
                !ContainsAny(n, { ".txt", ".sha256", ".sha512", ".md5", ".sig", ".asc", ".json", ".md", ".yml" }));
    };

    std::vector<GhAsset> matches;
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        if (is_archive(n) && IsLinuxAssetName(n) && !LooksLikeCompanionTool(n)) matches.push_back(a);
    }
    if (!matches.empty()) {
        // A name that says "linux" beats one that only matched by being a
        // bare tarball: a release can carry a third-party source archive next
        // to the game (Yakumo ships ffmpeg-7.1.5.tar.xz before its
        // yakumo-...-linux-x86_64.tar.gz), and first-in-list used to win.
        // Architecture still decides first.
        auto rank = [](const std::string& n) {
            return ArchPreferenceRank(n) * 100 + (n.find("linux") != std::string::npos ? 0 : 10);
        };
        const GhAsset* best = &matches.front();
        int best_rank = rank(ToLowerStr(best->name));
        for (const auto& a : matches) {
            int r = rank(ToLowerStr(a.name));
            if (r < best_rank) { best = &a; best_rank = r; }
            if (best_rank == 0) break;
        }
        return *best;
    }

    // No fallback loop over non-archive names here, unlike PickWindowsAsset:
    // that one exists so a Windows .msi still gets identified (and rejected
    // with a specific "installer, unsupported" error downstream). There is
    // no equivalent Linux special case, so matching a non-.zip Linux-looking
    // name here would just fail extraction later with a more confusing
    // error - better to fall through to "no Linux build" now.
    return GhAsset{};
}
#endif

#ifdef __APPLE__
static bool IsMacOsAssetName(const std::string& lower_name) {
    if (ContainsAny(lower_name, { "windows", "win64", "win32", "win-x64", "win-x86",
                                   "-win.", "_win.", ".exe", ".msi", "msvc", "mingw",
                                   "linux", ".deb", ".rpm", "appimage", "flatpak",
                                   "switch", "android", "source" }))
        return false;
    return ContainsAny(lower_name, { "macos", "osx", "darwin", "apple", "-mac", "_mac", ".mac", "mac." });
}

static int MacOsArchPreferenceRank(const std::string& lower_name) {
#if defined(__arm64__) || defined(__aarch64__)
    if (ContainsAny(lower_name, { "arm64", "apple-silicon", "arm", "_m1", "-m1", "m1.", "_m1.", "aarch64" })) return 0;
    if (ContainsAny(lower_name, { "universal" })) return 1;
    if (ContainsAny(lower_name, { "x64", "x86_64", "intel" })) return 2;
#else
    if (ContainsAny(lower_name, { "x64", "x86_64", "intel" })) return 0;
    if (ContainsAny(lower_name, { "universal" })) return 1;
    if (ContainsAny(lower_name, { "arm64", "apple-silicon" })) return 2;
#endif
    return 3;
}

static GhAsset PickMacOsAsset(const std::vector<GhAsset>& assets) {
    auto is_archive = [](const std::string& n) {
        return (n.size() >= 4 && (n.substr(n.size() - 4) == ".zip" || n.substr(n.size() - 4) == ".dmg")) ||
               (n.size() >= 7 && (n.substr(n.size() - 7) == ".tar.gz" || n.substr(n.size() - 7) == ".tar.xz"));
    };

    std::vector<GhAsset> matches;
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        if (is_archive(n) && IsMacOsAssetName(n) && !LooksLikeCompanionTool(n)) matches.push_back(a);
    }
    if (!matches.empty()) {
        const GhAsset* best = &matches.front();
        int best_rank = MacOsArchPreferenceRank(ToLowerStr(best->name));
        for (const auto& a : matches) {
            int rank = MacOsArchPreferenceRank(ToLowerStr(a.name));
            if (rank < best_rank) { best = &a; best_rank = rank; }
            if (best_rank == 0) break;
        }
        return *best;
    }
    return GhAsset{};
}
#endif

// Downloads the latest release of def, extracts it into ports/<id>/ and
// flattens it. Runs on a worker thread - network + disk I/O, seconds to
// minutes depending on the release size and the user's connection.
// The newest release's download for this OS from a project's own feed (see
// PortDefinition::downloads_json_url). The first "downloads" object in the
// document belongs to the first - newest - release.
static GhAsset PickFromDownloadsFeed(const std::string& json) {
#ifdef _WIN32
    const std::string key = "\"windows\"";
#elif defined(__APPLE__)
    const std::string key = "\"macos\"";
#else
    const std::string key = "\"linux\"";
#endif
    size_t d = json.find("\"downloads\"");
    if (d == std::string::npos) return GhAsset{};
    size_t open = json.find('{', d), close = json.find('}', d);
    if (open == std::string::npos || close == std::string::npos || close < open) return GhAsset{};
    size_t k = json.find(key, open);
    if (k == std::string::npos || k > close) return GhAsset{};
    size_t q1 = json.find('"', json.find(':', k + key.size()));
    size_t q2 = q1 == std::string::npos ? q1 : json.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 > close) return GhAsset{};
    std::string url;
    for (size_t i = q1 + 1; i < q2; ++i)
        if (json[i] != '\\') url += json[i];   // JSON may escape '/' as "\/"
    if (url.rfind("https://", 0) != 0) return GhAsset{};
    GhAsset a;
    a.url = url;
    a.name = url.substr(url.find_last_of('/') + 1);
    return a;
}

static bool DownloadAndInstall(const PortDefinition& def, std::string& out_error) {
    if (def.repo.empty() && def.downloads_json_url.empty()) { out_error = Tr("sem repositorio configurado", "no repository configured"); return false; }

    GhAsset asset;
    if (!def.downloads_json_url.empty()) {
        std::string feed;
        if (!UpdaterHttpGetString(def.downloads_json_url, feed)) {
            out_error = Tr("falha ao consultar o servidor do port", "could not reach the port's server");
            return false;
        }
        asset = PickFromDownloadsFeed(feed);
        if (asset.url.empty()) { out_error = Tr("nenhum build para este sistema no servidor do port", "no build for this system on the port's server"); return false; }
    } else {
    std::string releases_url = "https://api.github.com/repos/" + def.repo + "/releases/latest";
    std::string json;
    bool got_latest = UpdaterHttpGetString(releases_url, json);
    if (!got_latest || ParseReleaseAssets(json).empty()) {
        // /latest 404s whenever a repo's releases are all marked prerelease,
        // or returns 200 with an empty assets list if the latest tag didn't attach binaries.
        // The plural endpoint returns every release with nothing excluded, newest first.
        std::string list_json;
        if (UpdaterHttpGetString("https://api.github.com/repos/" + def.repo + "/releases", list_json)) {
            json = FirstReleaseObjectOnly(list_json);
        } else if (!got_latest) {
            out_error = Tr("falha ao consultar o GitHub", "could not reach GitHub");
            return false;
        }
    }

    if (json.find("\"assets\"") == std::string::npos && json.find("rate limit exceeded") != std::string::npos) {
        out_error = Tr("limite de requisicoes da API do GitHub atingido (tente mais tarde)", "GitHub API rate limit reached (try again later)");
        return false;
    }

    std::vector<GhAsset> assets = ParseReleaseAssets(json);
#ifdef _WIN32
    asset = PickWindowsAsset(assets);
    if (asset.url.empty()) { out_error = Tr("nenhum build Windows na release", "no Windows build in the release"); return false; }
#elif defined(__APPLE__)
    asset = PickMacOsAsset(assets);
    if (asset.url.empty()) { out_error = Tr("nenhum build macOS na release deste port", "no macOS build in this port's release"); return false; }
#else
    asset = PickLinuxAsset(assets);
    if (asset.url.empty()) { out_error = Tr("nenhum build Linux na release deste port", "no Linux build in this port's release"); return false; }
#endif
    }

    std::string lower_name = ToLowerStr(asset.name);

    // IsWindowsAssetName treats ".msi" as a valid Windows marker (some
    // releases have no other option), but nothing past this point can do
    // anything useful with one: it is neither a bare exe nor an archive
    // ArchiveExtractAll's tar.exe/Expand-Archive backends can open, so it
    // would previously fall into the zip branch below and fail extraction
    // with a confusing "falha ao extrair o pacote". Running an installer
    // silently (and tracking it well enough to uninstall later) is a
    // different feature than "download and unzip" - reject it up front with
    // an error that says what is actually true, instead of pretending to
    // extract it.
    if (lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".msi") {
        out_error = Tr("release so oferece instalador (.msi), sem suporte", "release only offers an installer (.msi), not supported");
        return false;
    }

    std::error_code ec;
    fs::create_directories("cache", ec);
    std::string dest_dir = "ports/" + def.id;
    fs::create_directories(dest_dir, ec);

#ifndef _WIN32
    bool is_bare_exe = (lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".exe") ||
                       (!ContainsAny(lower_name, { ".zip", ".tar.gz", ".tar.xz", ".7z", ".rar", ".msi", ".dmg", ".pkg", ".apk", ".deb", ".rpm" }));
#else
    bool is_bare_exe = lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".exe";
#endif

    if (is_bare_exe) {
        // Some releases ship the whole game as one executable with no
        // archive around it - nothing to extract, just place it.
        // asset.name comes straight from the GitHub Releases API JSON with no
        // validation of its own; .filename() keeps only the last path
        // component, so a "../"-laden or absolute name can't write outside
        // dest_dir (the same intent as ArchiveHasUnsafeEntry for archive
        // extraction, applied here to a single downloaded file's name).
        std::string safe_name = fs::path(asset.name).filename().string();
        std::string exe_name = safe_name.empty() ? (def.id + ".exe") : safe_name;
        std::string exe_path = dest_dir + "/" + exe_name;
        if (!UpdaterHttpDownloadToFile(asset.url, exe_path)) { out_error = Tr("falha no download", "download failed"); return false; }
#ifndef _WIN32
        chmod(exe_path.c_str(), 0755);
#endif
    } else {
        // Match the local cache file's extension to what was actually picked
        // instead of assuming .zip: PickMacOsAsset (unlike PickWindowsAsset/
        // PickLinuxAsset) also matches .tar.gz/.tar.xz, and ArchiveExtractAll
        // decides how to open a file by looking at *this* path's own
        // extension - a real .tar.gz saved as "..._download.zip" made it
        // call unzip on a tar.gz, which unzip cannot read, then fall back to
        // 7z (not installed on a stock Mac), failing extraction outright.
        std::string cache_ext = ".zip";
        if (lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".dmg") cache_ext = ".dmg";
        else if (lower_name.size() >= 7 && lower_name.substr(lower_name.size() - 7) == ".tar.gz") cache_ext = ".tar.gz";
        else if (lower_name.size() >= 7 && lower_name.substr(lower_name.size() - 7) == ".tar.xz") cache_ext = ".tar.xz";
        else if (lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".rar") cache_ext = ".rar";
        else if (lower_name.size() >= 3 && lower_name.substr(lower_name.size() - 3) == ".7z") cache_ext = ".7z";
        std::string zip_path = "cache/" + def.id + "_download" + cache_ext;
        if (!UpdaterHttpDownloadToFile(asset.url, zip_path)) { out_error = Tr("falha no download", "download failed"); return false; }

        bool extracted = ArchiveExtractAll(zip_path, dest_dir);
        fs::remove(zip_path, ec);
        if (!extracted) { out_error = Tr("falha ao extrair o pacote", "could not extract the package"); return false; }

        FlattenSingleSubfolder(dest_dir);

        // If the extracted folder contains no executable but contains a nested
        // archive, extract that too - common in macOS releases where Game.app
        // sits inside a nested .zip, and in several real Linux releases (Zelda
        // 64: Recompiled among them) that wrap their actual payload as a
        // single .tar.gz *inside* the outer .zip instead of shipping the game
        // directly. ArchiveExtractAll already knows how to open any of these
        // three by extension - this just has to notice one was left behind.
        if (ResolvePortExe(&def, dest_dir).empty()) {
            for (const auto& e : fs::directory_iterator(dest_dir, ec)) {
                if (ec || !e.is_regular_file(ec)) continue;
                std::string fname = ToLowerStr(e.path().filename().string());
                if (fname.rfind("._", 0) == 0) continue;
                bool is_nested_archive =
                    (fname.size() >= 4 && (fname.substr(fname.size() - 4) == ".zip" ||
                                            fname.substr(fname.size() - 4) == ".dmg")) ||
                    (fname.size() >= 7 && (fname.substr(fname.size() - 7) == ".tar.gz" ||
                                            fname.substr(fname.size() - 7) == ".tar.xz"));
                if (is_nested_archive) {
                    ArchiveExtractAll(e.path().string(), dest_dir);
                    fs::remove(e.path(), ec);
                    FlattenSingleSubfolder(dest_dir);
                    break;
                }
            }
        }
    }

    if (ResolvePortExe(&def, dest_dir).empty()) { out_error = Tr("pacote instalado sem executavel", "installed package has no executable"); return false; }

    return true;
}

// -------------------------------------------------------------
// Public API
// -------------------------------------------------------------

void PortInit() {
    std::error_code ec;
    if (!fs::exists("ports", ec)) {
        fs::create_directories("ports", ec);
    }
}

std::vector<PortGameInfo> PortGetAvailableList() {
    std::vector<PortGameInfo> list;

    for (const auto& def : KnownPortDefs()) {
        if (def.hidden) continue;
        PortGameInfo info;
        info.id = def.id;
        info.name = def.display_name;
        info.rom_pattern = def.rom_keywords.empty() ? "" : def.rom_keywords.front();

        std::string dir = ResolvePortDir(def.id);
        std::string exe = ResolvePortExe(&def, dir);
        if (exe.empty() && !dir.empty() && !def.exe_hint.empty() && def.exe_relpath.empty()) {
            std::string hinted = dir + "/" + def.exe_hint;
            if (fs::exists(hinted)) exe = hinted;
        }

#ifdef _WIN32
        // Every entry in this table is offered on Windows
#elif defined(__APPLE__)
        // On macOS, hide titles with no macOS build unless already present on disk.
        if (!def.has_macos_build && exe.empty()) continue;
#else
        // On Linux, hide titles with no Linux build unless already present on disk.
        if (!def.has_linux_build && exe.empty()) continue;
#endif

        // Games are never bundled with the app itself (no ROMs, no ports/
        // shipped in the project or the release zip - see .gitignore) - the
        // whole point of this list is that PortLaunch() downloads whatever
        // isn't here yet the moment it's selected. Every entry stays visible
        // whether or not it's on disk right now; is_installed only exists so
        // callers know not to bother showing a status label for it (there
        // is none anymore - selecting an entry just works, download or not).
        info.exe_path = exe;
        info.working_dir = dir.empty() ? ("ports/" + def.id) : dir;
        info.is_installed = !exe.empty();
        list.push_back(info);
    }

    // Anything the user dropped into ports/ by hand that isn't one of the
    // known entries above still shows up, launchable by whatever .exe is in
    // its folder - this is the same behaviour the very first version of this
    // file had for "some port I haven't added to the table yet".
    std::error_code ec;
    if (fs::exists("ports", ec) && fs::is_directory("ports", ec)) {
        for (const auto& entry : fs::directory_iterator("ports", ec)) {
            if (!entry.is_directory(ec)) continue;
            std::string folder_name = entry.path().filename().string();
            if (FindPortDef(folder_name)) continue; // already listed above

            std::string exe = FindBestExecutable(entry.path().string());
            if (exe.empty()) continue;

            PortGameInfo custom;
            custom.id = folder_name;
            custom.name = folder_name + " (PC Port)";
            custom.exe_path = exe;
            custom.working_dir = entry.path().string();
            custom.is_installed = true;
            list.push_back(custom);
        }
    }

    return list;
}

bool PortIsInstalled(const std::string& port_id) {
    std::string dir = ResolvePortDir(port_id);
    if (dir.empty()) return false;
    return !ResolvePortExe(FindPortDef(port_id), dir).empty();
}

bool PortAutoSetupRom(const std::string& port_id) {
    const PortDefinition* def = FindPortDef(port_id);
    if (!def || !def->needs_rom || def->rom_keywords.empty()) return false;

    std::string target_dir = ResolvePortDir(port_id);
    if (target_dir.empty()) target_dir = "ports/" + port_id;
    std::error_code ec;
    fs::create_directories(target_dir, ec);

    // 0. Special case: CPS-2 Recomp (Capcom Arcade Collection)
    // Copies available CPS-1/CPS-2 rom sets (sfa2u, dino, avspu, spf2t, vsavj, vsav2, vhunt2, etc.)
    // from roms/Arcade (and other arcade folders) into ports/CPS2Recomp/ and ports/CPS2Recomp/rom/
    if (port_id == "CPS2Recomp") {
        static const std::vector<std::string> cps_roms = {
            "sfa2u.zip", "sfa2.zip",
            "dino.zip",
            "avspu.zip", "avsp.zip",
            "spf2t.zip", "spf2.zip",
            "vsavj.zip", "vsav.zip",
            "vsav2.zip",
            "vhunt2.zip"
        };
        static const std::vector<std::string> arcade_dirs = {
            "roms/Arcade", "app/roms/Arcade", "roms", "app/roms",
            "roms/MAME", "app/roms/MAME", "roms/FBNeo", "app/roms/FBNeo",
            "roms/CPS2", "app/roms/CPS2", "roms/CPS1", "app/roms/CPS1"
        };

        fs::path rom_sub = fs::path(target_dir) / "rom";
        fs::create_directories(rom_sub, ec);

        bool any_copied = false;
        for (const auto& rom_name : cps_roms) {
            fs::path target_file1 = fs::path(target_dir) / rom_name;
            fs::path target_file2 = rom_sub / rom_name;
            if (fs::exists(target_file1, ec) || fs::exists(target_file2, ec)) {
                any_copied = true;
                continue;
            }
            for (const auto& sdir : arcade_dirs) {
                if (!fs::exists(sdir, ec)) continue;
                fs::path src = fs::path(sdir) / rom_name;
                if (fs::is_regular_file(src, ec)) {
                    fs::copy_file(src, target_file1, fs::copy_options::overwrite_existing, ec);
                    fs::copy_file(src, target_file2, fs::copy_options::overwrite_existing, ec);
                    any_copied = true;
                    break;
                }
            }
        }
        return any_copied;
    }

    // 1. Special case: Cannonball DX (OutRun Arcade)
    // Cannonball DX reads MAME's merged outrun.zip inside ports/CannonballDX/roms/
    if (port_id == "CannonballDX") {
        fs::path cb_rom_dir = fs::path(target_dir) / "roms";
        if (fs::exists(cb_rom_dir / "outrun.zip", ec)) return true;
        if (fs::exists(cb_rom_dir, ec)) {
            for (const auto& f : fs::directory_iterator(cb_rom_dir, ec)) {
                if (ToLowerStr(f.path().filename().string()) == "outrun.zip") return true;
            }
        }

        static const std::vector<std::string> arcade_dirs = {
            "roms/Arcade", "app/roms/Arcade", "roms", "app/roms", "roms/MAME", "app/roms/MAME"
        };
        for (const auto& sdir : arcade_dirs) {
            if (!fs::exists(sdir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(sdir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                std::string fname = ToLowerStr(entry.path().filename().string());
                if (fname.find("outrun") != std::string::npos && fname.size() >= 4 && fname.substr(fname.size() - 4) == ".zip") {
                    fs::create_directories(cb_rom_dir, ec);
                    fs::copy_file(entry.path(), cb_rom_dir / "outrun.zip", fs::copy_options::overwrite_existing, ec);
                    return !ec;
                }
            }
        }
        return false;
    }

    // 2. SNES Recomps (SuperMetroidRecomp, SuperMarioWorldRecomp, StarFoxEnhanced, JungleStrikeSNES)
    if (port_id.find("SuperMetroid") != std::string::npos ||
        port_id.find("SuperMarioWorld") != std::string::npos ||
        port_id.find("StarFox") != std::string::npos ||
        port_id.find("JungleStrike") != std::string::npos) {
        for (const auto& f : fs::directory_iterator(target_dir, ec)) {
            std::string ext = ToLowerStr(f.path().extension().string());
            if (ext == ".sfc" || ext == ".smc") return true;
        }
        static const std::vector<std::string> snes_dirs = {
            "roms/SNES", "app/roms/SNES", "roms", "app/roms"
        };
        for (const auto& sdir : snes_dirs) {
            if (!fs::exists(sdir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(sdir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                std::string fname = ToLowerStr(entry.path().filename().string());
                std::string ext = ToLowerStr(entry.path().extension().string());
                if (ext != ".sfc" && ext != ".smc") continue;
                if (!RomNameMatches(*def, fname)) continue;
                fs::path dest = fs::path(target_dir) / entry.path().filename();
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                if (port_id == "JungleStrikeSNES") {
                    fs::path rom_dir = fs::path(target_dir) / "Rom";
                    fs::create_directories(rom_dir, ec);
                    fs::copy_file(entry.path(), rom_dir / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
                }
                return !ec;
            }
        }
    }

    // 3. Genesis / Mega Drive Ports (Sonic1Forever, Sonic3AIR)
    if (port_id.find("Sonic") != std::string::npos) {
        for (const auto& f : fs::directory_iterator(target_dir, ec)) {
            std::string ext = ToLowerStr(f.path().extension().string());
            if (ext == ".md" || ext == ".gen" || ext == ".bin") return true;
        }
        static const std::vector<std::string> gen_dirs = {
            "roms/Genesis", "app/roms/Genesis", "roms/MegaDrive", "app/roms/MegaDrive", "roms", "app/roms"
        };
        for (const auto& sdir : gen_dirs) {
            if (!fs::exists(sdir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(sdir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                std::string fname = ToLowerStr(entry.path().filename().string());
                std::string ext = ToLowerStr(entry.path().extension().string());
                if (ext != ".md" && ext != ".gen" && ext != ".bin") continue;
                if (!RomNameMatches(*def, fname)) continue;
                fs::path dest = fs::path(target_dir) / entry.path().filename();
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                return !ec;
            }
        }
    }

    // 4. Default: Nintendo 64 ports (.z64, .n64, .v64)
    for (const auto& f : fs::directory_iterator(target_dir, ec)) {
        std::string ext = ToLowerStr(f.path().extension().string());
        if (ext == ".z64" || ext == ".n64" || ext == ".v64") {
            if (port_id == "PokemonStadiumRecomp") {
                fs::path cfg_path = fs::path(target_dir) / "rom.cfg";
                if (!fs::exists(cfg_path, ec)) {
                    std::ofstream cfg(cfg_path);
                    if (cfg) cfg << f.path().filename().string() << "\n";
                }
            }
            if (port_id == "LodRecomp") {
                fs::path rz = fs::path(target_dir) / "rom.z64";
                if (!fs::exists(rz, ec)) {
                    fs::copy_file(f.path(), rz, fs::copy_options::overwrite_existing, ec);
                }
            }
            if (port_id == "WG3DRecomp") {
                fs::path rz = fs::path(target_dir) / "baserom.us.z64";
                if (!fs::exists(rz, ec)) {
                    fs::copy_file(f.path(), rz, fs::copy_options::overwrite_existing, ec);
                }
            }
            return true;
        }
    }

    static const std::vector<std::string> search_dirs = {
        "roms/Nintendo64", "roms/N64", "roms",
        "app/roms/Nintendo64", "app/roms/N64", "app/roms"
    };

    for (const auto& sdir : search_dirs) {
        if (!fs::exists(sdir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(sdir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string fname = ToLowerStr(entry.path().filename().string());
            std::string ext = ToLowerStr(entry.path().extension().string());
            if (ext != ".z64" && ext != ".n64" && ext != ".v64") continue;

            if (port_id == "PokemonStadiumRecomp" && fname.find("2") != std::string::npos) continue;

            if (!RomNameMatches(*def, fname)) continue;

            fs::path dest = fs::path(target_dir) / entry.path().filename();
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);

            if (port_id == "PokemonStadiumRecomp") {
                std::ofstream cfg(fs::path(target_dir) / "rom.cfg");
                if (cfg) cfg << entry.path().filename().string() << "\n";
            }
            if (port_id == "LodRecomp") {
                fs::copy_file(dest, fs::path(target_dir) / "rom.z64", fs::copy_options::overwrite_existing, ec);
            }
            if (port_id == "WG3DRecomp") {
                fs::copy_file(dest, fs::path(target_dir) / "baserom.us.z64", fs::copy_options::overwrite_existing, ec);
            }
            return !ec;
        }
    }
    return false;
}

// -------------------------------------------------------------
// Xbox 360 dump preparation (The Darkness Recomp and future 360 recomps)
// -------------------------------------------------------------
// Upstream asks each user to extract their disc with "Xbox 360 Image
// Browser" and run xorloser's XexTool twice by hand. Karamelo does both
// itself (x360_dump.cpp): the user only drops their own ISO into
// roms/Xbox360/. Every output is accepted only if its SHA-256 is one the
// installed recomp executable itself embeds - the same digests it enforces
// at startup - so a different game revision is reported here, in words,
// instead of as a game window that opens and closes.

static void PortLog(const std::string& line) {
    FILE* lf = fopen("karamelo.log", "a");
    if (!lf) return;
    fprintf(lf, "[INFO] [PORT-X360] %s\n", line.c_str());
    fclose(lf);
}

static bool X360GameReady(const PortDefinition& def, const std::string& port_dir) {
    if (def.x360_game_dir.empty()) return true;
    std::error_code ec;
    fs::path g = fs::path(port_dir) / def.x360_game_dir;
    return fs::is_regular_file(g / "basefile.exe", ec) && fs::is_regular_file(g / "_uncrypted.xex", ec);
}

// True when the image's GameCube/Wii header matches what the definition
// asks for (or when it asks for nothing).
static bool IsoHeaderMatches(const PortDefinition& def, const fs::path& iso) {
    if (def.iso_disc_id.empty()) return true;
    // Only a raw image starts with the disc header; a compressed one
    // (.rvz etc.) cannot be checked here and is left to the game.
    std::string ext = ToLowerStr(iso.extension().string());
    if (ext != ".iso" && ext != ".gcm") return true;
    std::ifstream f(iso, std::ios::binary);
    char hdr[8] = {};
    if (!f.read(hdr, sizeof(hdr))) return false;
    if (std::string(hdr, 6) != def.iso_disc_id) return false;
    return def.iso_disc_rev < 0 || (unsigned char)hdr[7] == (unsigned)def.iso_disc_rev;
}

// The user's own disc image: the port folder itself, then roms/<d> for each
// of the definition's iso_dirs (the app's roms/<system> convention), then
// the roms/ root. Every keyword must appear in the lowercased file name, and
// the disc header must match when the definition pins one. out_wrong_disc
// reports that a name matched but no header did, so the toast can say
// "wrong disc" instead of "no disc".
static std::string FindUserIso(const PortDefinition& def, const std::string& port_dir,
                               bool* out_wrong_disc = nullptr) {
    if (out_wrong_disc) *out_wrong_disc = false;
    if (def.iso_keywords.empty()) return "";
    std::vector<std::string> search;
    if (!port_dir.empty()) search.push_back(port_dir);
    for (const auto& d : def.iso_dirs) { search.push_back("roms/" + d); search.push_back("app/roms/" + d); }
    search.push_back("roms");
    search.push_back("app/roms");
    std::error_code ec;
    for (const auto& d : search) {
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            if (ec || !e.is_regular_file(ec)) continue;
            std::string ext = ToLowerStr(e.path().extension().string());
            bool ext_ok = def.iso_exts.empty() ? ext == ".iso"
                        : std::find(def.iso_exts.begin(), def.iso_exts.end(), ext) != def.iso_exts.end();
            if (!ext_ok) continue;
            std::string name = ToLowerStr(e.path().filename().string());
            bool all = true;
            for (const auto& kw : def.iso_keywords)
                if (!NameHasKeyword(name, kw, def.iso_keywords_whole_word)) { all = false; break; }
            if (!all) continue;
            if (IsoHeaderMatches(def, e.path())) return e.path().string();
            if (out_wrong_disc) *out_wrong_disc = true;
        }
    }
    return "";
}

static std::string FindUserRom(const PortDefinition& def, const std::string& port_dir) {
    if (def.rom_keywords.empty()) return "";
    std::error_code ec;
    // 1. In port folder itself (where PortAutoSetupRom copies it)
    if (!port_dir.empty() && fs::is_directory(port_dir, ec)) {
        for (const auto& e : fs::directory_iterator(port_dir, ec)) {
            if (ec || !e.is_regular_file(ec)) continue;
            std::string ext = ToLowerStr(e.path().extension().string());
            if (ext == ".z64" || ext == ".n64" || ext == ".v64") {
                return e.path().string();
            }
        }
    }
    // 2. In ROM folders
    static const std::vector<std::string> search = {
        "roms/Nintendo64", "roms/N64", "roms",
        "app/roms/Nintendo64", "app/roms/N64", "app/roms"
    };
    for (const auto& d : search) {
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            if (ec || !e.is_regular_file(ec)) continue;
            std::string ext = ToLowerStr(e.path().extension().string());
            if (ext != ".z64" && ext != ".n64" && ext != ".v64") continue;
            std::string name = ToLowerStr(e.path().filename().string());
            if (RomNameMatches(def, name)) return e.path().string();
        }
    }
    return "";
}

static bool ReadWholeFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

// Written under a temporary name first so a crash can never leave a
// half-written basefile.exe that X360GameReady() would then trust.
static bool WriteWholeFile(const fs::path& p, const std::vector<uint8_t>& data) {
    fs::path part = p;
    part += ".part";
    std::error_code ec;
    {
        std::ofstream o(part, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!o) { o.close(); fs::remove(part, ec); return false; }
    }
    fs::rename(part, p, ec);
    return !ec;
}

static bool DigestAccepted(const std::vector<uint8_t>& v, const std::vector<std::string>& accepted) {
    std::string h = x360::Sha256Hex(v.data(), v.size());
    for (const auto& a : accepted) if (a == h) return true;
    return false;
}

#ifdef _WIN32
// Optional fallback: if the user already has xorloser's XexTool (it is not
// ours to redistribute), its output is used - still under the same digest
// gate - when the native rewrite did not match.
static bool RunXexTool(const fs::path& xextool, const fs::path& game_dir, const std::wstring& args) {
    std::wstring cmd = L"\"" + xextool.wstring() + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(xextool.wstring().c_str(), buf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, game_dir.wstring().c_str(), &si, &pi))
        return false;
    DWORD rc = 1;
    if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &rc);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return rc == 0;
}
#endif

// Runs on a worker thread (the ISO copy is several GB). Toasts progress.
// out_error is short enough for a toast; the details go to karamelo.log.
static bool PrepareX360Game(const PortDefinition& def, const std::string& port_dir, std::string& out_error) {
#ifndef _WIN32
    (void)def; (void)port_dir;
    out_error = Tr("PREPARO DE DUMP XBOX 360 SO EXISTE NO WINDOWS", "XBOX 360 DUMP PREPARATION IS WINDOWS ONLY");
    return false;
#else
    std::error_code ec;
    fs::path game = fs::path(port_dir) / def.x360_game_dir;
    fs::create_directories(game, ec);

    // 1. Game files: extract the ISO unless the dump is already there (a
    //    user who extracted it by hand keeps working exactly as before).
    if (!fs::is_regular_file(game / "default.xex", ec)) {
        std::string iso = FindUserIso(def, port_dir);
        if (iso.empty()) {
            out_error = Tr("COLOQUE SEU ISO DE ", "PUT YOUR ISO OF ") + def.display_name + Tr(" EM roms/Xbox360", " IN roms/Xbox360");
            PortLog(def.id + ": nenhum .iso com o nome contendo as palavras-chave do port");
            return false;
        }
        PortLog(def.id + ": extraindo " + iso + " -> " + game.string());
        int last_pct = -1;
        std::string x_err;
        bool ok = x360::XisoExtract(iso, game.string(), x_err, [&](uint64_t done, uint64_t total) {
            int pct = total ? int(done * 100 / total) : 100;
            if (pct != last_pct) {
                last_pct = pct;
                CoreSetToast((Tr("EXTRAINDO ISO... ", "EXTRACTING ISO... ") + std::to_string(pct) + "%").c_str(), 600);
            }
        });
        if (!ok) {
            out_error = Tr("FALHA AO EXTRAIR O ISO - VEJA karamelo.log", "ISO EXTRACTION FAILED - SEE karamelo.log");
            PortLog(def.id + ": extracao falhou: " + x_err);
            return false;
        }
        if (!fs::is_regular_file(game / "default.xex", ec)) {
            out_error = Tr("ISO SEM default.xex - E O JOGO CERTO?", "ISO HAS NO default.xex - IS IT THE RIGHT GAME?");
            PortLog(def.id + ": ISO extraido, mas sem default.xex na raiz");
            return false;
        }
    }

    // 2. The digests the installed recomp enforces.
    std::vector<std::string> accepted;
    if (!def.exe_relpath.empty())
        accepted = x360::FindEmbeddedSha256((fs::path(port_dir) / def.exe_relpath).string());
    if (accepted.empty()) {
        out_error = Tr("NAO FOI POSSIVEL LER OS HASHES DO PORT", "COULD NOT READ THE PORT'S HASHES");
        PortLog(def.id + ": nenhum SHA-256 embutido em " + def.exe_relpath);
        return false;
    }

    // 3. basefile.exe + _uncrypted.xex from default.xex.
    CoreSetToast(Tr("GERANDO ARQUIVOS DO JOGO...", "GENERATING GAME FILES..."), 600);
    std::vector<uint8_t> xex, basefile, uncrypted;
    if (!ReadWholeFile(game / "default.xex", xex)) {
        out_error = Tr("default.xex ILEGIVEL", "default.xex UNREADABLE");
        return false;
    }
    std::string report, b_err;
    bool built = x360::X360BuildRecompInputs(xex, accepted, basefile, uncrypted, report, b_err);
    PortLog(def.id + ": " + report + (built ? " -> OK" : " -> " + b_err));
    if (built) {
        if (!WriteWholeFile(game / "basefile.exe", basefile) ||
            !WriteWholeFile(game / "_uncrypted.xex", uncrypted)) {
            out_error = Tr("FALHA AO GRAVAR OS ARQUIVOS DO JOGO (DISCO CHEIO?)", "COULD NOT WRITE THE GAME FILES (DISK FULL?)");
            return false;
        }
        return true;
    }

    // 4. Fallback: the user's own XexTool, under the same digest gate.
    fs::path tool;
    for (const fs::path& c : { game / "xextool.exe", fs::path(port_dir) / "xextool.exe",
                               fs::path("tools") / "xextool.exe", fs::path("app/tools") / "xextool.exe" })
        if (fs::is_regular_file(c, ec)) { tool = fs::absolute(c, ec); break; }
    if (!tool.empty()) {
        PortLog(def.id + ": tentando o XexTool do usuario em " + tool.string());
        bool ran = RunXexTool(tool, game, L"-b basefile.exe default.xex") &&
                   RunXexTool(tool, game, L"-e u -c u -o _uncrypted.xex default.xex");
        std::vector<uint8_t> b2, u2;
        if (ran && ReadWholeFile(game / "basefile.exe", b2) && ReadWholeFile(game / "_uncrypted.xex", u2) &&
            DigestAccepted(b2, accepted) && DigestAccepted(u2, accepted)) {
            PortLog(def.id + ": XexTool gerou arquivos validos");
            return true;
        }
        // Our own outputs (X360GameReady() only ran us because they did not
        // exist) - never leave a mismatching pair behind to be launched.
        fs::remove(game / "basefile.exe", ec);
        fs::remove(game / "_uncrypted.xex", ec);
        PortLog(def.id + ": saida do XexTool nao confere com o port");
    }

    out_error = Tr("DUMP NAO CONFERE COM O PORT (REVISAO DIFERENTE?) - VEJA karamelo.log", "DUMP DOES NOT MATCH THE PORT (DIFFERENT REVISION?) - SEE karamelo.log");
    return false;
#endif
}

// -------------------------------------------------------------
// PSP disc preparation (Renegade Squadron)
// -------------------------------------------------------------
// The recomp reads the disc's files, not the image: upstream tells each user
// to extract PSP_GAME/ from their ISO by hand into data/. tar.exe (libarchive)
// reads ISO9660, so ArchiveExtractAll does it. Extraction goes to a staging
// folder and only a tree with SYSDIR/BOOT.BIN in it is moved into place, so a
// half-finished extraction is never mistaken for a ready game.
static bool PspGameReady(const PortDefinition& def, const std::string& port_dir) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(port_dir) / def.psp_game_dir / "PSP_GAME" / "SYSDIR" / "BOOT.BIN", ec);
}

static bool PreparePspGame(const PortDefinition& def, const std::string& port_dir, std::string& out_error) {
    if (PspGameReady(def, port_dir)) { out_error.clear(); return true; }
    std::string iso = FindUserIso(def, port_dir);
    if (iso.empty()) {
        out_error = Tr("COLOQUE SEU ISO DE ", "PUT YOUR ISO OF ") + def.display_name + Tr(" EM roms/", " IN roms/") +
                    (def.iso_dirs.empty() ? std::string("PSP") : def.iso_dirs.front());
        return false;
    }
    std::error_code ec;
    fs::path staging = fs::path(port_dir) / "_psp_extract";
    fs::remove_all(staging, ec);
    if (!ArchiveExtractAll(iso, staging.string())) {
        fs::remove_all(staging, ec);
        out_error = Tr("FALHA AO EXTRAIR O ISO DE ", "COULD NOT EXTRACT THE ISO OF ") + def.display_name;
        return false;
    }
    fs::path extracted = staging / "PSP_GAME";
    if (!fs::is_regular_file(extracted / "SYSDIR" / "BOOT.BIN", ec)) {
        fs::remove_all(staging, ec);
        out_error = Tr("ISO SEM PSP_GAME/SYSDIR/BOOT.BIN - NAO E O DISCO DE ", "ISO HAS NO PSP_GAME/SYSDIR/BOOT.BIN - NOT THE DISC OF ") + def.display_name;
        return false;
    }
    fs::path target = fs::path(port_dir) / def.psp_game_dir / "PSP_GAME";
    fs::create_directories(target.parent_path(), ec);
    fs::remove_all(target, ec);
    fs::rename(extracted, target, ec);
    fs::remove_all(staging, ec);
    if (ec || !PspGameReady(def, port_dir)) {
        out_error = Tr("FALHA AO PREPARAR ", "COULD NOT PREPARE ") + def.display_name;
        return false;
    }
    out_error.clear();
    return true;
}

// BLACK static recomp helper: checks if ports/BlackRecomp/assets/ already has
// an extracted default.xbe or an original Xbox .iso. If not, searches roms/Xbox
// (or roms/) for the user's ISO and links/copies it into assets/.
static bool SetupBlackGame(const PortDefinition& def, const std::string& port_dir, std::string& out_error) {
    if (port_dir.empty()) {
        out_error = Tr("diretorio do port invalido", "invalid port folder");
        return false;
    }
    fs::path assets_dir = fs::path(port_dir) / "assets";
    std::error_code aec;
    fs::create_directories(assets_dir, aec);

    // 1. Check if game files already exist in assets/ (extracted default.xbe or any .iso)
    if (fs::exists(assets_dir / "default.xbe", aec)) {
        out_error.clear();
        return true;
    }
    if (fs::is_directory(assets_dir, aec)) {
        for (const auto& e : fs::directory_iterator(assets_dir, aec)) {
            if (aec || !e.is_regular_file(aec)) continue;
            std::string ext = ToLowerStr(e.path().extension().string());
            if (ext == ".iso") {
                out_error.clear();
                return true;
            }
        }
    }

    // 2. Look for user's ISO in roms/Xbox, roms/, etc.
    std::string iso = FindUserIso(def, port_dir);
    if (iso.empty()) {
        out_error = Tr("COLOQUE SEU ISO DE BLACK EM roms/Xbox", "PUT YOUR BLACK ISO IN roms/Xbox");
        return false;
    }

    // 3. Link or copy the ISO into assets/ so BLACK.exe finds it directly
    fs::path target_iso = assets_dir / fs::path(iso).filename();
    std::error_code link_ec;
    fs::create_hard_link(iso, target_iso, link_ec);
    if (link_ec) {
        link_ec.clear();
        fs::create_symlink(iso, target_iso, link_ec);
        if (link_ec) {
            link_ec.clear();
            fs::copy_file(iso, target_iso, fs::copy_options::overwrite_existing, link_ec);
            if (link_ec) {
                out_error = Tr("FALHA AO COPIAR ISO DE BLACK: ", "COULD NOT COPY THE BLACK ISO: ") + link_ec.message();
                return false;
            }
        }
    }
    out_error.clear();
    return true;
}

// Does the actual OS-level work of launching an already-installed port:
// ROM auto-copy, stopping any running libretro core, minimizing the window,
// launching the process, and spawning the exit-monitor thread. Must only
// ever be called from the UI thread - it touches CoreShutdown()/window state
// that core_runner.cpp and main_win32.cpp otherwise only ever touch from
// there.
static bool LaunchResolvedExecutable(const std::string& exe_path, const PortDefinition* def,
                                      const std::string& port_id) {
    // 1. Auto-copy ROM if this port needs one and doesn't have it yet.
    if (def && def->needs_rom) PortAutoSetupRom(port_id);

    // 1b. Resolve "{iso}" in the launch arguments before anything is torn
    // down or minimized - a missing disc image has to leave the menu as it was.
    std::vector<std::string> extra_args = def ? def->launch_args : std::vector<std::string>{};
    for (auto& a : extra_args) {
        size_t at = a.find("{iso}");
        if (at == std::string::npos) continue;
        bool wrong_disc = false;
        std::string iso = FindUserIso(*def, ResolvePortDir(port_id), &wrong_disc);
        if (iso.empty()) {
            std::string where = def->iso_dirs.empty() ? "roms" : "roms/" + def->iso_dirs.front();
            if (wrong_disc && !def->iso_disc_label.empty())
                CoreSetToast((Tr("ISO ENCONTRADO NAO E ", "ISO FOUND IS NOT ") + def->iso_disc_label + Tr(" - VEJA ", " - SEE ") + where).c_str(), 360);
            else
                CoreSetToast((Tr("COLOQUE SEU ISO DE ", "PUT YOUR ISO OF ") + def->display_name + Tr(" EM ", " IN ") + where).c_str(), 300);
            return false;
        }
        std::error_code aec;
        a.replace(at, 5, fs::absolute(iso, aec).string());
    }

    // 1c. Resolve "{rom}" in the launch arguments
    for (auto& a : extra_args) {
        size_t at = a.find("{rom}");
        if (at == std::string::npos) continue;
        std::string rom = FindUserRom(*def, ResolvePortDir(port_id));
        if (rom.empty()) {
            CoreSetToast((Tr("COLOQUE A ROM DE ", "PUT THE ROM OF ") + (def ? def->display_name : port_id) + Tr(" EM roms/Nintendo64", " IN roms/Nintendo64")).c_str(), 300);
            return false;
        }
        std::error_code rec;
        a.replace(at, 5, fs::absolute(rom, rec).string());
    }

    // 1d. Special first-run helper for ProjectCane: if Worlds/ data directory does not
    // exist yet, automatically launch the bundled File Extractor GUI so the user can
    // pick their NTSC PS2 ISO and extract the required game assets.
    if (def && def->id == "ProjectCane") {
        std::error_code wec;
        std::string port_dir = ResolvePortDir(def->id);
        if (!port_dir.empty() && !fs::exists(port_dir + "/Worlds", wec)) {
#ifdef _WIN32
            std::string ext_exe = port_dir + "/File Extractor GUI/File Extractor GUI.exe";
            if (fs::exists(ext_exe, wec)) {
                CoreSetToast(Tr("EXTRAIA SUA ISO NO FILE EXTRACTOR", "EXTRACT YOUR ISO IN THE FILE EXTRACTOR"), 360);
                return LaunchResolvedExecutable(ext_exe, nullptr, port_id);
            }
#else
            std::string ext_sh = port_dir + "/Run File Extractor.sh";
            if (fs::exists(ext_sh, wec)) {
                CoreSetToast(Tr("EXTRAIA SUA ISO NO FILE EXTRACTOR", "EXTRACT YOUR ISO IN THE FILE EXTRACTOR"), 360);
                return LaunchResolvedExecutable(ext_sh, nullptr, port_id);
            }
#endif
        }
    }

    // 1e. Special helper for BlackRecomp: if assets/ does not contain an ISO or default.xbe,
    // look for a BLACK Xbox ISO in roms/Xbox/ and link/copy it into assets/.
    if (def && def->id == "BlackRecomp") {
        std::string err;
        if (!SetupBlackGame(*def, ResolvePortDir(def->id), err)) {
            CoreSetToast(err.c_str(), 300);
            return false;
        }
    }

    // 2. Stop any active Libretro core. CoreIsRunning() alone is false while
    // a ROM/core is still on its way up (CORE_STATE_LOADING) - checking only
    // that let a native port launch alongside a core load still running on
    // its own thread in the background, instead of stopping it first like
    // every other path into this function assumes happens here.
    if (CoreIsRunning() || CoreIsLoading()) {
        CoreShutdown();
    }

    // 3. Minimize the Karamelo window for a clean seamless console transition. SDL3
    // rather than a raw HWND, so this works the same on every platform SDL3
    // supports.
    SDL_Window* window = MainGetSdlWindow();
    if (window) {
        SDL_MinimizeWindow(window);
    }

    // 4. Launch process
    fs::path abs_exe = fs::absolute(exe_path);
    fs::path abs_dir = abs_exe.parent_path();
    // A pinned exe_relpath runs from the port folder itself, the way the
    // project's own launcher script does (paths in its arguments and its
    // settings/saves are relative to there, not to the exe's subfolder).
    if (def && !def->exe_relpath.empty()) {
        std::string port_dir = ResolvePortDir(port_id);
        if (!port_dir.empty()) abs_dir = fs::absolute(port_dir);
    }

#ifdef _WIN32
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = { 0 };

    std::wstring wexe = abs_exe.wstring();
    std::wstring wdir = abs_dir.wstring();

    // .bat/.cmd (Severed Chains among them) aren't real PE executables -
    // CreateProcessW can't run one as lpApplicationName directly, it has to
    // go through cmd.exe /c. lpCommandLine must be a writable buffer per the
    // Win32 docs (CreateProcessW may modify it while splitting arguments),
    // so this builds one instead of handing it a std::wstring's own storage.
    std::string ext_lower = ToLowerStr(abs_exe.extension().string());
    bool is_script = (ext_lower == ".bat" || ext_lower == ".cmd");

    BOOL ok;
    if (is_script) {
        // Doubled quotes, not a single pair: cmd.exe's own quote-stripping-and-
        // reparse rule for `/c "..."` (it strips exactly one outer pair when the
        // whole argument starts and ends with a quote) would otherwise leave a
        // filename containing &, |, or ^ able to inject a second command. The
        // doubled quotes survive that one strip, so the path stays inside its
        // own quoted token instead of being reparsed as bare command text.
        std::wstring cmdline = L"cmd.exe /c \"\"" + wexe + L"\"\"";
        std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
        buf.push_back(L'\0');
        ok = CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, 0, NULL, wdir.c_str(), &si, &pi);
    } else if (extra_args.empty() && (!def || def->console_log_relpath.empty())) {
        ok = CreateProcessW(wexe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, wdir.c_str(), &si, &pi);
    } else {
        std::wstring cmdline = QuoteWinArg(wexe);
        for (const auto& a : extra_args) cmdline += L" " + QuoteWinArg(fs::path(a).wstring());
        std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
        buf.push_back(L'\0');

        if (def && !def->console_log_relpath.empty()) {
            // Console-subsystem game: no console window, stdout/stderr to a
            // log file in the port folder. Only these two handles are
            // inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST) - bInheritHandles
            // alone would hand the game every inheritable handle Karamelo has.
            fs::path log_path = abs_dir / def->console_log_relpath;
            std::error_code lec;
            fs::create_directories(log_path.parent_path(), lec);
            SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
            HANDLE hlog = CreateFileW(log_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            HANDLE hnul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            ok = FALSE;
            if (hlog != INVALID_HANDLE_VALUE && hnul != INVALID_HANDLE_VALUE) {
                HANDLE inherit[2] = { hlog, hnul };
                SIZE_T attr_size = 0;
                InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
                std::vector<uint8_t> attr_buf(attr_size);
                auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
                if (InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
                    if (UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                  inherit, sizeof(inherit), NULL, NULL)) {
                        STARTUPINFOEXW six = {};
                        six.StartupInfo.cb = sizeof(six);
                        six.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                        six.StartupInfo.hStdInput = hnul;
                        six.StartupInfo.hStdOutput = hlog;
                        six.StartupInfo.hStdError = hlog;
                        six.lpAttributeList = attrs;
                        ok = CreateProcessW(wexe.c_str(), buf.data(), NULL, NULL, TRUE,
                                            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                            NULL, wdir.c_str(), &six.StartupInfo, &pi);
                    }
                    DeleteProcThreadAttributeList(attrs);
                }
            }
            // The child holds its own duplicates; ours are no longer needed.
            if (hlog != INVALID_HANDLE_VALUE) CloseHandle(hlog);
            if (hnul != INVALID_HANDLE_VALUE) CloseHandle(hnul);
        } else {
            ok = CreateProcessW(wexe.c_str(), buf.data(), NULL, NULL, FALSE, 0, NULL, wdir.c_str(), &si, &pi);
        }
    }

    if (!ok) {
        if (window) SDL_RestoreWindow(window);
        CoreSetToast(Tr("FALHA AO INICIAR PORT", "COULD NOT START PORT"), 180);
        return false;
    }

    HANDLE hProcess = pi.hProcess;
    HANDLE hThread = pi.hThread;
#else
    // No .bat/.cmd-style script launcher exists here - every port definition
    // in KnownPortDefs() resolves to a real binary (FindBestExecutable() only
    // ever picks an .exe today; its Linux equivalent would pick the
    // extracted binary the same way), so a plain fork+exec covers every case
    // this app actually installs.
    std::string sexe = abs_exe.string();
    std::string sdir = abs_dir.string();

    chmod(sexe.c_str(), 0755);
    std::error_code ec;
    std::string rom_arg;
    for (const auto& f : fs::directory_iterator(abs_dir, ec)) {
        std::string ext = ToLowerStr(f.path().extension().string());
        if (ext == ".z64" || ext == ".n64" || ext == ".v64") {
            rom_arg = f.path().string();
            break;
        }
    }

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(sexe.c_str()));
    bool has_custom_rom = false;
    for (const auto& a : extra_args) {
        std::string alow = ToLowerStr(a);
        if (alow.find(".z64") != std::string::npos || alow.find(".n64") != std::string::npos || alow.find(".v64") != std::string::npos) {
            has_custom_rom = true;
            break;
        }
    }
    if (!rom_arg.empty() && !has_custom_rom) {
        argv.push_back(const_cast<char*>(rom_arg.c_str()));
    }
    for (const auto& a : extra_args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        if (window) SDL_RestoreWindow(window);
        CoreSetToast(Tr("FALHA AO INICIAR PORT", "COULD NOT START PORT"), 180);
        return false;
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls until exec, per fork()'s
        // contract in a multithreaded process. chdir, execv, and _exit
        // are strictly async-signal-safe and perform no heap allocations.
        if (chdir(sdir.c_str()) != 0) {}
        execv(sexe.c_str(), argv.data());
        _exit(127); // exec itself failed (not found / not executable)
    }
#endif

    s_port_running.store(true);
    CoreSetToast(Tr("INICIANDO PORT NATIVO...", "STARTING NATIVE PORT..."), 300); // 5 s

    // 5. Monitor in background thread
    try {
    std::thread([
#ifdef _WIN32
        hProcess, hThread
#else
        pid
#endif
        ]() {
        // The wait itself can run for as long as the user plays (hours) - it
        // is not counted in s_active_bg_threads, or PortShutdown() would
        // block app shutdown on the game still being open instead of just
        // waiting out this thread's own brief cleanup tail below, which is
        // the only part that touches shared state (OsdEnable(), and
        // indirectly toast_lock through it) a hung-core recovery could race.
#ifdef _WIN32
        WaitForSingleObject(hProcess, INFINITE);
        s_active_bg_threads.fetch_add(1);
        CloseHandle(hThread);
        CloseHandle(hProcess);
#else
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
        s_active_bg_threads.fetch_add(1);
#endif

        s_port_running.store(false);

        // Window restoration must happen on the main UI thread (via PortPumpPendingLaunch).
        // On macOS (Cocoa / AppKit), calling SDL_RestoreWindow or SDL_RaiseWindow
        // from a secondary thread causes an AppKit thread safety violation (SIGTRAP / trace trap 133).
        s_port_needs_restore.store(true);
        s_active_bg_threads.fetch_sub(1);
    }).detach();
    } catch (const std::system_error&) {
        // Thread creation itself failed (resource exhaustion) - the process
        // we just launched would otherwise go completely untracked (no one
        // left to wait on it or restore the window) and s_port_running would
        // stay stuck at true forever, refusing every future PortLaunch() for
        // the rest of the session.
#ifdef _WIN32
        CloseHandle(hThread);
        CloseHandle(hProcess);
#else
        // Windows just stops tracking the process here and lets it keep
        // running independently (closing a handle does not kill it) - that
        // is fine on Windows, which has no reaping requirement. POSIX does:
        // an un-waited exited child stays a zombie process-table entry until
        // something calls waitpid() on it, which would otherwise never
        // happen here since the thread meant to do that never got created.
        // The exhaustion that made std::thread throw is almost always
        // transient (a burst of other short-lived threads elsewhere in the
        // app), so one retry after a brief pause is worth it before settling
        // for a bounded, non-blocking reap attempt and then - matching the
        // Windows trade-off above - giving up and letting the child run on,
        // untracked, rather than blocking here indefinitely.
        bool reaped = false;
        for (int attempt = 0; attempt < 2 && !reaped; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) reaped = true;
        }
        if (!reaped) {
            try {
                std::thread([pid]() {
                    int status = 0;
                    waitpid(pid, &status, 0);
                }).detach();
            } catch (const std::system_error&) {
                // Still exhausted - give up exactly like the Windows path
                // does. The child keeps running untracked; it will finally
                // be reaped when this app process itself exits.
            }
        }
#endif
        s_port_running.store(false);
        CoreSetToast(Tr("FALHA AO MONITORAR PORT - TENTE NOVAMENTE", "COULD NOT MONITOR PORT - TRY AGAIN"), 200);
        return false;
    }

    return true;
}

bool PortLaunch(const std::string& port_id) {
    // Unlike the s_installing check right below, this one used to return
    // false with no toast at all - selecting any port while another one was
    // still open (or its "still running" bookkeeping hadn't cleared yet)
    // looked exactly like the menu doing nothing, with no way to tell why.
    if (s_port_running.load()) {
        CoreSetToast(Tr("JA HA UM PORT ABERTO - FECHE-O PRIMEIRO", "A PORT IS ALREADY OPEN - CLOSE IT FIRST"), 150);
        return false;
    }

    const PortDefinition* def = FindPortDef(port_id);
    std::string dir = ResolvePortDir(port_id);
    std::string exe_path = ResolvePortExe(def, dir);
    // An Xbox 360 recomp is installed but not playable until the user's
    // dump has been turned into its inputs - that work (an ISO extraction of
    // several GB) goes on the same background slot as a download.
    bool needs_x360_prep = def && !def->x360_game_dir.empty() &&
                           (exe_path.empty() || !X360GameReady(*def, dir));
    // Same background slot for a PSP disc extraction - but with no ISO at all
    // there is nothing to prepare, so say where it goes before downloading.
    bool needs_psp_prep = def && !def->psp_game_dir.empty() && !PspGameReady(*def, dir);
    if (needs_psp_prep && FindUserIso(*def, dir).empty()) {
        CoreSetToast((Tr("COLOQUE SEU ISO DE ", "PUT YOUR ISO OF ") + def->display_name + Tr(" EM roms/", " IN roms/") +
                      (def->iso_dirs.empty() ? std::string("PSP") : def->iso_dirs.front())).c_str(), 300);
        return false;
    }

    // Not installed yet: if this is a known port with a repository, fetch it
    // in the background and launch automatically once it lands. Anything
    // else (an unknown id, or a known one with no repo configured) has
    // nothing to download - say so and stop.
    if (exe_path.empty() || needs_x360_prep || needs_psp_prep) {
        if (exe_path.empty() && (!def || def->repo.empty())) {
            CoreSetToast(Tr("PORT NAO INSTALADO EM ports/", "PORT NOT INSTALLED IN ports/"), 200);
            return false;
        }

        // A trava de download mora aqui dentro, e nao no topo da funcao. No
        // topo ela barrava QUALQUER port enquanto outro baixava - inclusive os
        // ja instalados, que nao precisam baixar nada. So existe uma vaga de
        // download; um port que ja esta no disco nao disputa essa vaga.
        if (s_installing.load()) {
            CoreSetToast(Tr("JA HA UM DOWNLOAD DE PORT EM ANDAMENTO", "A PORT DOWNLOAD IS ALREADY RUNNING"), 150);
            return false;
        }

        bool needs_download = exe_path.empty();
        {
            std::lock_guard<std::mutex> lock(s_pending_lock);
            s_installing_name = def->display_name;
            s_installing_verb = needs_download ? Tr("BAIXANDO ", "DOWNLOADING ") : Tr("PREPARANDO ", "PREPARING ");
        }
        s_installing.store(true);
        CoreSetToast(((needs_download ? Tr("BAIXANDO ", "DOWNLOADING ") : Tr("PREPARANDO ", "PREPARING ")) + def->display_name + "...").c_str(), 600);

        PortDefinition def_copy = *def;
        s_active_bg_threads.fetch_add(1);
        std::thread([def_copy, needs_download]() {
            std::string error;
            if (needs_download) {
                bool ok = DownloadAndInstall(def_copy, error);
                if (!ok) {
                    s_installing.store(false);
                    CoreSetToast((Tr("FALHA AO BAIXAR PORT: ", "PORT DOWNLOAD FAILED: ") + error).c_str(), 240);
                    s_active_bg_threads.fetch_sub(1);
                    return;
                }
                if (def_copy.needs_rom) PortAutoSetupRom(def_copy.id);
            }

            if (!def_copy.x360_game_dir.empty()) {
                std::string port_dir = ResolvePortDir(def_copy.id);
                if (port_dir.empty()) port_dir = "ports/" + def_copy.id;
                if (!X360GameReady(def_copy, port_dir)) {
                    {
                        std::lock_guard<std::mutex> lock(s_pending_lock);
                        s_installing_verb = Tr("PREPARANDO ", "PREPARING ");
                    }
                    if (!PrepareX360Game(def_copy, port_dir, error)) {
                        s_installing.store(false);
                        CoreSetToast(error.c_str(), 420);
                        s_active_bg_threads.fetch_sub(1);
                        return;
                    }
                }
            }
            if (!def_copy.psp_game_dir.empty()) {
                std::string port_dir = ResolvePortDir(def_copy.id);
                if (port_dir.empty()) port_dir = "ports/" + def_copy.id;
                if (!PspGameReady(def_copy, port_dir)) {
                    {
                        std::lock_guard<std::mutex> lock(s_pending_lock);
                        s_installing_verb = Tr("PREPARANDO ", "PREPARING ");
                    }
                    if (!PreparePspGame(def_copy, port_dir, error)) {
                        s_installing.store(false);
                        CoreSetToast(error.c_str(), 420);
                        s_active_bg_threads.fetch_sub(1);
                        return;
                    }
                }
            }
            s_installing.store(false);

            CoreSetToast(needs_download ? Tr("PORT INSTALADO! INICIANDO...", "PORT INSTALLED! STARTING...") : Tr("JOGO PREPARADO! INICIANDO...", "GAME READY! STARTING..."), 150);

            // Hand off to the UI thread instead of launching from here - see
            // the comment on s_pending_lock above for why.
            {
                std::lock_guard<std::mutex> lock(s_pending_lock);
                s_pending_launch_id = def_copy.id;
            }
            s_has_pending_launch.store(true);
            s_active_bg_threads.fetch_sub(1);
        }).detach();

        return true;
    }

    return LaunchResolvedExecutable(exe_path, def, port_id);
}

void PortPumpPendingLaunch() {
    // Process window restoration from the main thread if a port just exited
    if (s_port_needs_restore.exchange(false)) {
        SDL_Window* window = MainGetSdlWindow();
        if (window) {
            SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
        }
        OsdEnable();
    }

    // Mantem o "BAIXANDO..." na tela enquanto o download durar. Os toasts
    // passaram a expirar por relogio, e um download de centenas de MB dura
    // bem mais que os 10 s da mensagem. Antes ela ficava por acidente - o
    // contador so descia com core rodando, e no menu de ports nao ha core.
    // So reexibe quando nenhuma outra mensagem esta na tela, para nao
    // atropelar um aviso mais importante que tenha acabado de sair.
    if (s_installing.load() && !CoreIsToastActive()) {
        std::string name, verb;
        {
            std::lock_guard<std::mutex> lock(s_pending_lock);
            name = s_installing_name;
            verb = s_installing_verb;
        }
        if (!name.empty())
            CoreSetToast((verb + name + "...").c_str(), 300);
    }

    if (!s_has_pending_launch.exchange(false)) return;

    std::string id;
    {
        std::lock_guard<std::mutex> lock(s_pending_lock);
        id = s_pending_launch_id;
    }

    // Re-resolve rather than trust anything captured before the download
    // finished - the files just landed on disk and this is the first look
    // at them from the thread that's actually allowed to act on it.
    std::string dir = ResolvePortDir(id);
    std::string exe_path = ResolvePortExe(FindPortDef(id), dir);
    if (exe_path.empty()) {
        CoreSetToast(Tr("PORT BAIXADO MAS SEM EXECUTAVEL", "PORT DOWNLOADED BUT HAS NO EXECUTABLE"), 200);
        return;
    }

    // Agora da para abrir um port instalado enquanto outro baixa, entao quando
    // o download termina pode haver um port em execucao. LaunchResolvedExecutable
    // nao confere isso - PortLaunch e que conferia -, e abriria o segundo por
    // cima do primeiro. Nesse caso so avisa: o port ja esta no disco e abre
    // pelo menu na hora que o jogador quiser.
    if (s_port_running.load()) {
        CoreSetToast(Tr("DOWNLOAD CONCLUIDO - ABRA O PORT PELO MENU", "DOWNLOAD DONE - OPEN THE PORT FROM THE MENU"), 300);
        return;
    }

    LaunchResolvedExecutable(exe_path, FindPortDef(id), id);
}

bool PortIsRunning() {
    return s_port_running.load();
}

// Waits (briefly) for any in-flight download-install or just-exited-process
// cleanup thread to finish, so the app doesn't tear down shared state (most
// importantly toast_lock, via RecoverAfterKilledCore() on a simultaneous
// hung-core kill) while one of them could still be calling CoreSetToast()
// through it. Does NOT wait for a still-open native port to close - see the
// comment on the exit-monitor thread in LaunchResolvedExecutable() for why
// that would block shutdown on the user's game instead of on this file's own
// brief cleanup work.
void PortShutdown() {
    unsigned waited_ms = 0;
    while (s_active_bg_threads.load() > 0 && waited_ms < 5000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited_ms += 50;
    }
}

bool PortInstallOnly(const std::string& port_id, std::string& out_error) {
    const PortDefinition* def = FindPortDef(port_id);
    if (!def) { out_error = Tr("id de port desconhecido: ", "unknown port id: ") + port_id; return false; }

    std::string dir = ResolvePortDir(port_id);
    if (!ResolvePortExe(def, dir).empty()) {
        out_error.clear();
        return true; // already installed
    }

    bool ok = DownloadAndInstall(*def, out_error);
    if (ok && def->needs_rom) PortAutoSetupRom(def->id);
    return ok;
}

bool PortPrepareOnly(const std::string& port_id, std::string& out_error) {
    const PortDefinition* def = FindPortDef(port_id);
    if (!def) { out_error = Tr("id de port desconhecido: ", "unknown port id: ") + port_id; return false; }
    if (!PortInstallOnly(port_id, out_error)) return false;
    if (def->id == "BlackRecomp") {
        std::string dir = ResolvePortDir(port_id);
        return SetupBlackGame(*def, dir, out_error);
    }
    if (!def->psp_game_dir.empty()) {
        std::string dir = ResolvePortDir(port_id);
        return PreparePspGame(*def, dir, out_error);
    }
    if (def->x360_game_dir.empty()) { out_error.clear(); return true; }
    std::string dir = ResolvePortDir(port_id);
    if (X360GameReady(*def, dir)) { out_error.clear(); return true; }
    return PrepareX360Game(*def, dir, out_error);
}

// Writes the catalog this binary is running to path, in the data/ports.json
// format. --dump-port-catalog: shows exactly what the app loaded, and how
// data/ports.json was first produced from the old built-in table.
bool PortDumpCatalog(const char* path) {
    const PortCatalog& cat = ActiveCatalog();
    std::string json = PortCatalogToJson(cat);
    FILE* f = path ? fopen(path, "wb") : nullptr;
    if (!f) return false;
    bool ok = fwrite(json.data(), 1, json.size(), f) == json.size();
    ok = (fclose(f) == 0) && ok;
    return ok;
}

// Checks the site for a newer catalog in the background and keeps it in
// Config/ports_cache.json for the next start. Only from the windowed app -
// the command-line modes exit before a download could finish.
void PortStartCatalogRefresh() {
    int current = ActiveCatalog().catalog_version;
    s_active_bg_threads.fetch_add(1);
    try {
        std::thread([current]() {
            std::string text, err;
            PortCatalog fresh;
            if (!UpdaterHttpGetString(kPortsCatalogUrl, text)) {
                CatalogLog("sem acesso ao catalogo online - seguindo com o atual");
            } else if (!PortCatalogParse(text, fresh, err)) {
                CatalogLog("catalogo online rejeitado: " + err);
            } else if (fresh.catalog_version > current) {
                std::error_code ec;
                fs::create_directories("Config", ec);
                std::string tmp = std::string(kPortsCatalogCache) + ".tmp";
                FILE* f = fopen(tmp.c_str(), "wb");
                bool ok = f && fwrite(text.data(), 1, text.size(), f) == text.size();
                if (f) ok = (fclose(f) == 0) && ok;
                if (ok) fs::rename(tmp, kPortsCatalogCache, ec);
                if (!ok || ec) {
                    fs::remove(tmp, ec);
                    CatalogLog("falha ao gravar o catalogo baixado");
                } else {
                    CatalogLog("catalogo v" + std::to_string(fresh.catalog_version) +
                               " baixado - vale a partir do proximo inicio");
                }
            }
            s_active_bg_threads.fetch_sub(1);
        }).detach();
    } catch (const std::system_error&) {
        s_active_bg_threads.fetch_sub(1);
    }
}

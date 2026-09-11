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
#include <thread>
#include <chrono>
#include <system_error>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <stdio.h>

#include "port_runner.h"
#include "core_runner.h"
#include "menu.h"
#include "osd.h"
#include "archive_helper.h"
#include "updater.h"

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
// Nome do port que esta baixando, para o aviso de progresso (ver o topo de
// PortPumpPendingLaunch). Protegido pelo mesmo s_pending_lock.
static std::string      s_installing_name;

static std::string ToLowerStr(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

// -------------------------------------------------------------
// Known ports registry
// -------------------------------------------------------------
// Adding a new N64Recomp-style port is one entry here: id (also the folder
// name under ports/), display name, "owner/repo" for its GitHub releases,
// and - if the game needs the original ROM present to extract its assets -
// the keywords a candidate ROM filename must all contain. exe_hint is only
// an optimization; leave it empty and FindBestExecutable() below picks the
// largest .exe in the extracted tree, which is enough to launch a port whose
// exact executable name was never verified against a real release.
//
// DrMario64 is the one entry this file has actually launched end-to-end,
// ROM auto-copy included. Zelda64Recomp's repo and release asset naming are
// confirmed against the real GitHub release, but nobody has run a full
// download-to-launch pass on it here - it is the worked example for "how do
// I add the next one", not a second verified port.
struct PortDefinition {
    std::string id;
    std::string display_name;
    std::string repo;        // "owner/repo" on GitHub; empty = not auto-installable
    std::string exe_hint;    // expected exe filename, best-effort only
    bool needs_rom;
    std::vector<std::string> rom_keywords; // all must appear (lowercased) in a ROM filename
};

static const std::vector<PortDefinition>& KnownPortDefs() {
    static const std::vector<PortDefinition> defs = {
        // Verified against theboy181/drmario64_recomp_plus's actual release -
        // the one entry here launched end-to-end (download, extract, ROM
        // auto-copy, launch) rather than only checked against the API.
        { "DrMario64", "Dr. Mario 64",
          "theboy181/drmario64_recomp_plus", "drmario64_recomp.exe",
          true, { "dr", "mario" } },

        // Everything below is copied from the shipping N64Recomp Launcher's
        // own default game list (github.com/SirDiabo/N64RecompLauncher,
        // Services/N64RecompLauncherProfile.cs) - real repos its own users
        // download from today, not guessed. rom_keywords is left empty
        // wherever the title is too generic to safely auto-pick a ROM out of
        // roms/Nintendo64/ (e.g. "mario"+"64" would also match Mario Kart
        // 64's own ROM); those ports show their own ROM picker on first run
        // instead, which is the normal flow for most of them anyway.
        { "Zelda64Recomp", "Zelda64Recomp",
          "Zelda64Recomp/Zelda64Recomp", "", true, { "zelda", "majora" } },
        { "Goemon64Recomp", "Goemon 64",
          "klorfmorf/Goemon64Recomp", "", true, { "goemon" } },
        { "DinosaurPlanet", "Dinosaur Planet",
          "DinosaurPlanetRecomp/dino-recomp", "", true, { "dinosaur" } },
        { "HarvestMoon64Recomp", "Harvest Moon 64",
          "HarvestMoon64Recomp/HarvestMoon64Recomp", "", true, { "harvest", "moon" } },
        { "SnowboardKids2Recomp", "Snowboard Kids 2",
          "cdlewis/snowboardkids2-recomp", "", true, { "snowboard", "kids", "2" } },
        // No rom_keywords: "pokemon"+"stadium" alone would also match a
        // Pokemon Stadium *2* ROM, and this recomp (per the repo's own
        // description) targets Stadium 1 (US v1.0) specifically. Wrong game
        // copied in silently is worse than just letting the port ask.
        { "PokemonStadiumRecomp", "Pokemon Stadium",
          "mstan/PokemonStadiumRecomp", "", true, {} },
        // "Duke Nukem: Zero Hour" (sonicdcer/DNZHRecomp) was here but the
        // GitHub account that hosted it (sonicdcer) has been deleted -
        // confirmed via the API, a 404 on the user itself, not just the
        // repo. It always failed on install with no path to fix from our
        // side, so removed outright at the user's request on 2026-09-01
        // rather than kept as a permanently-broken menu entry.
        // sonicdcer/MarioKart64Recomp and sonicdcer/Starfox64Recomp hit the
        // same dead account; harbourmasters/spaghettikart and harbourmasters/
        // starship below already cover Mario Kart 64 and Star Fox 64 instead.
        { "Banjo64Recomp", "Banjo 64",
          "BanjoRecomp/BanjoRecomp", "", true, { "banjo" } },
        { "BM64Recomp", "Bomberman 64",
          "RevoSucks/BM64Recomp", "", true, { "bomberman" } },
        { "ChameleonTwistRecomp", "Chameleon Twist",
          "Rainchus/ChameleonTwist1-JP-Recomp", "", true, { "chameleon" } },
        { "MegaMan64Recomp", "Mega Man 64",
          "MegaMan64Recomp/MegaMan64Recompiled", "", true, { "mega", "man" } },
        { "Quest64Recomp", "Quest 64",
          "Rainchus/Quest64-Recomp", "", true, { "quest" } },
        { "BMHeroRecomp", "Bomberman Hero",
          "RevoSucks/BMHeroRecomp", "", true, { "bomberman", "hero" } },
        { "ShipOfHarkinian", "Ship of Harkinian",
          "harbourmasters/shipwright", "", true, { "zelda", "ocarina" } },
        { "2Ship2Harkinian", "2 Ship 2 Harkinian",
          "harbourmasters/2ship2harkinian", "", true, { "zelda", "majora" } },
        { "Starship", "Starship",
          "harbourmasters/starship", "", true, { "star", "fox" } },
        // Not the same game as the two Star Fox 64 (N64) entries above -
        // "built from the UltraStarFox codebase" per the repo's own
        // description, i.e. a source port of the original 1993 SNES Star
        // Fox, not the N64 sequel. Confirmed by its release also offering an
        // optional MSU1 pack (an SNES-emulation audio format, meaningless
        // for anything N64). rom_keywords is empty for two reasons, not one:
        // the usual "name too generic" risk, and because any real SNES ROM
        // would live in roms/SNES/, a folder PortAutoSetupRom does not search
        // at all (it only looks under roms/Nintendo64 and roms/N64) - the
        // scan would find nothing here regardless of keywords.
        { "StarFoxEnhanced", "Star Fox Enhanced",
          "kandowontu/starfox-enhanced", "", true, {} },
        { "SpaghettiKart", "SpaghettiKart",
          "harbourmasters/spaghettikart", "", true, { "mario", "kart" } },
        { "Ghostship", "Ghostship",
          "harbourmasters/ghostship", "", true, {} },
        // fgsfdsfgs/perfect_dark was the original repo; it has since moved to
        // this org. GitHub's API currently still resolves the old name via
        // redirect, but that isn't guaranteed to keep working.
        { "PerfectDark", "Perfect Dark",
          "perfect-dark-pc-port/perfect_dark", "", true, { "perfect", "dark" } },
        { "SM64CoopDX", "Super Mario 64 CoopDX",
          "coop-deluxe/sm64coopdx", "", true, {} },
        { "CannonballDX", "OutRun (CannonBall DX)",
          "Endprodukt/cannonball-dx", "cannonball-dx.exe",
          true, { "outrun" } },

        // -----------------------------------------------------------------
        // Not part of the launcher's own default list - these are the rest of
        // the titles from the user's library screenshot, each individually
        // verified against a live GitHub Releases API response (repo exists,
        // has a downloadable Windows-runnable asset) rather than guessed.
        // Most need a ROM/disc image this app has no way to source (PS1/PS2/
        // Xbox 360 dumps, SNES ROMs) - rom_keywords is only set where the
        // dump would plausibly live in roms/Nintendo64/ next to everything
        // else this app already manages; everywhere else the port's own
        // first-run picker is the real flow, same as several N64 ones above.
        //
        // Two titles from the screenshot are deliberately absent:
        //   - "Link's Awakening DX HD" (Phantop/LADXHD) ships a .7z, and this
        //     file's extractor only handles tar.exe/Expand-Archive formats
        //     (zip). Its GitHub mirror is also unofficial and last released
        //     in Dec 2023, after Nintendo DMCA'd the original itch.io page.
        //   - "Super Metroid Launcher" (RadzPrower/Super-Metroid-Launcher) is
        //     not a game - it is a small tool that downloads/compiles a
        //     Super Metroid port from source and needs a build toolchain.
        //     mstan/SuperMetroidRecomp below is an actual playable port.
        { "AnimalCrossingGC", "Animal Crossing",
          "flyngmt/ACGC-PC-Port", "", true, {} },
        { "NutsAndBolts", "Banjo-Kazooie: Nuts & Bolts",
          "masterspike52/reNut", "", true, {} },
        { "DBZBudokai", "Dragon Ball Z Budokai",
          "WistfulHopes/DBZ1", "", true, {} },
        { "InfiniteMario64", "Infinite Mario 64",
          "Brawmario/infinite-mario-64-ever", "", true, {} },
        { "JakAndDaxter", "Jak & Daxter",
          "open-goal/jak-project", "", true, {} },
        { "SeveredChains", "Severed Chains",
          "Legend-of-Dragoon-Modding/Severed-Chains", "", true, {} },
        { "REDRIVER2", "REDRIVER 2",
          "OpenDriver2/REDRIVER2", "", true, {} },
        { "SymphonyRecomp", "Castlevania: Symphony of the Night",
          "GuhClemente/SymphonyRecomp", "", true, {} },
        { "Sonic1Forever", "Sonic 1 Forever",
          "ElspethThePict/S1Forever", "", true, {} },
        { "Sonic3AIR", "Sonic 3 A.I.R.",
          "Eukaryot/sonic3air", "", true, {} },
        { "SonicUnleashedRecomp", "Sonic Unleashed",
          "hedge-dev/UnleashedRecomp", "", true, {} },
        { "SpaceStationSiliconValley", "Space Station Silicon Valley",
          "Cellenseres/SSSV_Recomp", "", true, { "silicon", "valley" } },
        { "SMBRemastered", "Super Mario Bros. Remastered",
          "JHDev2006/Super-Mario-Bros.-Remastered-Public", "", true, {} },
        { "SuperMarioWorldRecomp", "Super Mario World",
          "mstan/SuperMarioWorldRecomp", "", true, {} },
        { "SuperMetroidRecomp", "Super Metroid",
          "mstan/SuperMetroidRecomp", "", true, { "metroid" } },
        { "VivaPinataTiP", "Viva Pinata: Trouble in Paradise",
          "SolarCookies/TiP-Recomp", "", true, {} },
        { "WipeoutPhantomEdition", "WipEout Phantom Edition",
          "wipeout-phantom-edition/wipeout-phantom-edition", "", true, {} },

        // PS1 (Valkyrie Profile, 2 discs), built on PSXRecomp instead of
        // N64Recomp. Verified against the real v1.0.1 release: the Windows
        // zip is flat at the root (ValkyrieRecomp.exe alongside assets/,
        // mods/, psxrecomp/ - no wrapper folder, so FlattenSingleSubfolder
        // is a no-op here) and FindBestExecutable would pick the right exe
        // on its own, but exe_hint is set anyway since it was confirmed.
        // rom_keywords is empty on purpose, same reasoning as SymphonyRecomp/
        // REDRIVER2 above: PortAutoSetupRom only searches roms/Nintendo64,
        // so a PS1 disc image was never going to be auto-copied regardless -
        // this port asks for its own disc pair on first run either way.
        // Two things make this entry heavier than every other row here, and
        // worth knowing before pointing someone at it: (1) first launch runs
        // a "Generate & rebuild" wizard that needs Python 3 already on PATH
        // and downloads a clang/cmake toolchain the first time - there's no
        // prebuilt game binary inside the zip, only the tool that builds one
        // locally from the discs; (2) the exe itself has no license file in
        // its own repo, but the psxrecomp engine it embeds is PolyForm
        // Noncommercial 1.0.0 (c) Matthew Stan - same license family Karamelo
        // itself moved off of. That doesn't reach back into Karamelo's own
        // GPL-3.0, since this is a separate binary downloaded on demand like
        // every other port, never bundled - but it does mean the port itself
        // isn't free for commercial use the way Karamelo now is.
        { "ValkyrieRecomp", "Valkyrie Profile",
          "Ed1z19/ValkyrieRecomp", "ValkyrieRecomp.exe", true, {} },
    };
    return defs;
}

static const PortDefinition* FindPortDef(const std::string& id) {
    for (const auto& d : KnownPortDefs())
        if (d.id == id) return &d;
    return nullptr;
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
        "extractor.exe", "asset_builder.exe", "assetbuilder.exe"
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
        if (!e.path().extension().empty()) continue;
        auto perms = e.status(ec).permissions();
        if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) == fs::perms::none)
            continue;
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
            if (!e.path().extension().empty()) continue;
            auto perms = e.status(ec).permissions();
            if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) == fs::perms::none)
                continue;
#endif
            std::string fname = ToLowerStr(e.path().filename().string());
            bool skip = false;
            for (const auto& b : blacklist)
                if (fname.find(b) != std::string::npos) { skip = true; break; }
            if (skip) continue;

            nested.push_back(e.path());
        }
    }

    if (!nested.empty()) return BestExeAmong(nested);

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
                                      "build-tools", "sdk" });
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
    auto is_zip = [](const std::string& n) { return n.size() >= 4 && n.substr(n.size() - 4) == ".zip"; };
    auto is_exe = [](const std::string& n) { return n.size() >= 4 && n.substr(n.size() - 4) == ".exe"; };

    std::vector<GhAsset> zip_matches;
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        if (is_zip(n) && IsWindowsAssetName(n) && !LooksLikeCompanionTool(n)) zip_matches.push_back(a);
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

    // Smaller single-platform projects sometimes ship one .zip with no OS
    // marker anywhere in the name - untagged, not ambiguous. Chameleon Twist
    // and Quest 64's recomps both do exactly this. Nothing to choose between,
    // so take it rather than refuse outright.
    if (assets.size() == 1) {
        std::string n = ToLowerStr(assets[0].name);
        bool other_os = ContainsAny(n, { "linux", "macos", "osx", "darwin", "apple", ".deb", ".rpm",
                                          ".appimage", ".dmg", ".pkg", "switch", "android" });
        if (!other_os && (is_zip(n) || is_exe(n))) return assets[0];
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
        return n.size() >= 4 && n.substr(n.size() - 4) == ".zip";
    };

    std::vector<GhAsset> matches;
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        if (is_archive(n) && IsLinuxAssetName(n) && !LooksLikeCompanionTool(n)) matches.push_back(a);
    }
    if (!matches.empty()) {
        const GhAsset* best = &matches.front();
        int best_rank = ArchPreferenceRank(ToLowerStr(best->name));
        for (const auto& a : matches) {
            int rank = ArchPreferenceRank(ToLowerStr(a.name));
            if (rank < best_rank) { best = &a; best_rank = rank; }
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

// Downloads the latest release of def, extracts it into ports/<id>/ and
// flattens it. Runs on a worker thread - network + disk I/O, seconds to
// minutes depending on the release size and the user's connection.
static bool DownloadAndInstall(const PortDefinition& def, std::string& out_error) {
    if (def.repo.empty()) { out_error = "sem repositorio configurado"; return false; }

    std::string releases_url = "https://api.github.com/repos/" + def.repo + "/releases/latest";
    std::string json;
    if (!UpdaterHttpGetString(releases_url, json)) {
        // /latest 404s whenever a repo's releases are all marked prerelease -
        // GitHub excludes those from it by definition. Several actively
        // developed recomp projects (Bomberman Hero among them) never cut a
        // stable tag and only ever ship "nightly" prereleases, which are
        // still real, downloadable builds. The plural endpoint returns every
        // release with nothing excluded, newest first.
        std::string list_json;
        if (!UpdaterHttpGetString("https://api.github.com/repos/" + def.repo + "/releases", list_json)) {
            out_error = "falha ao consultar o GitHub";
            return false;
        }
        json = FirstReleaseObjectOnly(list_json);
    }

    std::vector<GhAsset> assets = ParseReleaseAssets(json);
#ifdef _WIN32
    GhAsset asset = PickWindowsAsset(assets);
    if (asset.url.empty()) { out_error = "nenhum build Windows na release"; return false; }
#else
    GhAsset asset = PickLinuxAsset(assets);
    if (asset.url.empty()) { out_error = "nenhum build Linux na release deste port"; return false; }
#endif

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
        out_error = "release so oferece instalador (.msi), sem suporte";
        return false;
    }

    std::error_code ec;
    fs::create_directories("cache", ec);
    std::string dest_dir = "ports/" + def.id;
    fs::create_directories(dest_dir, ec);

    bool is_bare_exe = lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".exe";

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
        if (!UpdaterHttpDownloadToFile(asset.url, exe_path)) { out_error = "falha no download"; return false; }
    } else {
        std::string zip_path = "cache/" + def.id + "_download.zip";
        if (!UpdaterHttpDownloadToFile(asset.url, zip_path)) { out_error = "falha no download"; return false; }

        bool extracted = ArchiveExtractAll(zip_path, dest_dir);
        fs::remove(zip_path, ec);
        if (!extracted) { out_error = "falha ao extrair o pacote"; return false; }

        FlattenSingleSubfolder(dest_dir);
    }

    if (FindBestExecutable(dest_dir).empty()) { out_error = "pacote instalado sem executavel"; return false; }

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
        PortGameInfo info;
        info.id = def.id;
        info.name = def.display_name;
        info.rom_pattern = def.rom_keywords.empty() ? "" : def.rom_keywords.front();

        std::string dir = ResolvePortDir(def.id);
        std::string exe = dir.empty() ? "" : FindBestExecutable(dir);
        if (exe.empty() && !dir.empty() && !def.exe_hint.empty()) {
            std::string hinted = dir + "/" + def.exe_hint;
            if (fs::exists(hinted)) exe = hinted;
        }

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
    return !FindBestExecutable(dir).empty();
}

bool PortAutoSetupRom(const std::string& port_id) {
    const PortDefinition* def = FindPortDef(port_id);
    if (!def || !def->needs_rom || def->rom_keywords.empty()) return false;

    std::string target_dir = ResolvePortDir(port_id);
    if (target_dir.empty()) target_dir = "ports/" + port_id;
    std::error_code ec;
    fs::create_directories(target_dir, ec);

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

    // 2. SNES Recomps (SuperMetroidRecomp, SuperMarioWorldRecomp, StarFoxEnhanced)
    if (port_id.find("SuperMetroid") != std::string::npos ||
        port_id.find("SuperMarioWorld") != std::string::npos ||
        port_id.find("StarFox") != std::string::npos) {
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
                bool all_match = true;
                for (const auto& kw : def->rom_keywords)
                    if (fname.find(kw) == std::string::npos) { all_match = false; break; }
                if (!all_match) continue;
                fs::path dest = fs::path(target_dir) / entry.path().filename();
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
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
                bool all_match = true;
                for (const auto& kw : def->rom_keywords)
                    if (fname.find(kw) == std::string::npos) { all_match = false; break; }
                if (!all_match) continue;
                fs::path dest = fs::path(target_dir) / entry.path().filename();
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                return !ec;
            }
        }
    }

    // 4. Default: Nintendo 64 ports (.z64, .n64, .v64)
    for (const auto& f : fs::directory_iterator(target_dir, ec)) {
        std::string ext = ToLowerStr(f.path().extension().string());
        if (ext == ".z64" || ext == ".n64" || ext == ".v64") return true;
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

            bool all_match = true;
            for (const auto& kw : def->rom_keywords)
                if (fname.find(kw) == std::string::npos) { all_match = false; break; }
            if (!all_match) continue;

            fs::path dest = fs::path(target_dir) / entry.path().filename();
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            return !ec;
        }
    }
    return false;
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
    } else {
        ok = CreateProcessW(wexe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, wdir.c_str(), &si, &pi);
    }

    if (!ok) {
        if (window) SDL_RestoreWindow(window);
        CoreSetToast("FALHA AO INICIAR PORT", 180);
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

    // A freshly-extracted zip does not preserve the Unix execute bit, so the
    // binary would otherwise refuse to run at all.
    chmod(sexe.c_str(), 0755);

    pid_t pid = fork();
    if (pid < 0) {
        if (window) SDL_RestoreWindow(window);
        CoreSetToast("FALHA AO INICIAR PORT", 180);
        return false;
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls until exec, per fork()'s
        // contract in a multithreaded process.
        chdir(sdir.c_str());
        char* argv[] = { const_cast<char*>(sexe.c_str()), nullptr };
        execv(sexe.c_str(), argv);
        _exit(127); // exec itself failed (not found / not executable)
    }
#endif

    s_port_running.store(true);
    CoreSetToast("INICIANDO PORT NATIVO...", 300); // 5 s

    // 5. Monitor in background thread
    try {
    std::thread([
#ifdef _WIN32
        hProcess, hThread,
#else
        pid,
#endif
        window]() {
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
        waitpid(pid, &status, 0);
        s_active_bg_threads.fetch_add(1);
#endif

        s_port_running.store(false);

        // Restore the Karamelo window with full focus
        if (window) {
            SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
        }
        OsdEnable();
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
        CoreSetToast("FALHA AO MONITORAR PORT - TENTE NOVAMENTE", 200);
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
        CoreSetToast("JA HA UM PORT ABERTO - FECHE-O PRIMEIRO", 150);
        return false;
    }

    const PortDefinition* def = FindPortDef(port_id);
    std::string dir = ResolvePortDir(port_id);
    std::string exe_path = dir.empty() ? "" : FindBestExecutable(dir);

    // Not installed yet: if this is a known port with a repository, fetch it
    // in the background and launch automatically once it lands. Anything
    // else (an unknown id, or a known one with no repo configured) has
    // nothing to download - say so and stop.
    if (exe_path.empty()) {
        if (!def || def->repo.empty()) {
            CoreSetToast("PORT NAO INSTALADO EM ports/", 200);
            return false;
        }

        // A trava de download mora aqui dentro, e nao no topo da funcao. No
        // topo ela barrava QUALQUER port enquanto outro baixava - inclusive os
        // ja instalados, que nao precisam baixar nada. So existe uma vaga de
        // download; um port que ja esta no disco nao disputa essa vaga.
        if (s_installing.load()) {
            CoreSetToast("JA HA UM DOWNLOAD DE PORT EM ANDAMENTO", 150);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(s_pending_lock);
            s_installing_name = def->display_name;
        }
        s_installing.store(true);
        CoreSetToast(("BAIXANDO " + def->display_name + "...").c_str(), 600);

        PortDefinition def_copy = *def;
        s_active_bg_threads.fetch_add(1);
        std::thread([def_copy]() {
            std::string error;
            bool ok = DownloadAndInstall(def_copy, error);
            s_installing.store(false);

            if (!ok) {
                CoreSetToast(("FALHA AO BAIXAR PORT: " + error).c_str(), 240);
                s_active_bg_threads.fetch_sub(1);
                return;
            }

            if (def_copy.needs_rom) PortAutoSetupRom(def_copy.id);

            CoreSetToast("PORT INSTALADO! INICIANDO...", 150);

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
    // Mantem o "BAIXANDO..." na tela enquanto o download durar. Os toasts
    // passaram a expirar por relogio, e um download de centenas de MB dura
    // bem mais que os 10 s da mensagem. Antes ela ficava por acidente - o
    // contador so descia com core rodando, e no menu de ports nao ha core.
    // So reexibe quando nenhuma outra mensagem esta na tela, para nao
    // atropelar um aviso mais importante que tenha acabado de sair.
    if (s_installing.load() && !CoreIsToastActive()) {
        std::string name;
        {
            std::lock_guard<std::mutex> lock(s_pending_lock);
            name = s_installing_name;
        }
        if (!name.empty())
            CoreSetToast(("BAIXANDO " + name + "...").c_str(), 300);
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
    std::string exe_path = dir.empty() ? "" : FindBestExecutable(dir);
    if (exe_path.empty()) {
        CoreSetToast("PORT BAIXADO MAS SEM EXECUTAVEL", 200);
        return;
    }

    // Agora da para abrir um port instalado enquanto outro baixa, entao quando
    // o download termina pode haver um port em execucao. LaunchResolvedExecutable
    // nao confere isso - PortLaunch e que conferia -, e abriria o segundo por
    // cima do primeiro. Nesse caso so avisa: o port ja esta no disco e abre
    // pelo menu na hora que o jogador quiser.
    if (s_port_running.load()) {
        CoreSetToast("DOWNLOAD CONCLUIDO - ABRA O PORT PELO MENU", 300);
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
    if (!def) { out_error = "id de port desconhecido: " + port_id; return false; }

    std::string dir = ResolvePortDir(port_id);
    if (!dir.empty() && !FindBestExecutable(dir).empty()) {
        out_error.clear();
        return true; // already installed
    }

    bool ok = DownloadAndInstall(*def, out_error);
    if (ok && def->needs_rom) PortAutoSetupRom(def->id);
    return ok;
}

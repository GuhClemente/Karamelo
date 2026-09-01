#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
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

extern HWND MainGetHwnd();

static std::atomic<bool> s_port_running(false);
static std::atomic<bool> s_installing(false);

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
static CRITICAL_SECTION s_pending_lock;
static std::string      s_pending_launch_id;
static std::atomic<bool> s_has_pending_launch(false);

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
          "BlackLabelHQ/SymphonyRecomp", "", true, {} },
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

    std::vector<fs::path> top_level, nested;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (ec || !e.is_regular_file(ec)) continue;
        if (ToLowerStr(e.path().extension().string()) != ".exe") continue;

        std::string fname = ToLowerStr(e.path().filename().string());
        bool skip = false;
        for (const auto& b : blacklist)
            if (fname.find(b) != std::string::npos) { skip = true; break; }
        if (skip) continue;

        std::error_code rel_ec;
        fs::path rel = fs::relative(e.path(), dir, rel_ec);
        auto rel_it = rel.begin();
        bool is_top = !rel_ec && rel_it != rel.end() && (++rel_it == rel.end()); // just a filename, no subdirectory components
        (is_top ? top_level : nested).push_back(e.path());
    }

    if (!top_level.empty()) return BestExeAmong(top_level);

    // A root-level "launch.bat" outranks any *nested* .exe, checked before
    // falling into the recursive fallback below. This is what Severed Chains
    // needs in practice: its launch.bat downloads a JDK into .\jdk25\bin\ on
    // first run, which is a folder full of nested .exe (java.exe among
    // dozens of other JDK CLI tools) that would otherwise win by depth over
    // the one file that is actually meant to be launched.
    std::error_code lb_ec;
    fs::path launch_bat = fs::path(dir) / "launch.bat";
    if (fs::exists(launch_bat, lb_ec)) return launch_bat.string();

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
                                   "-win.", "_win.", ".exe", ".msi", "msvc", "mingw" }))
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
    GhAsset asset = PickWindowsAsset(assets);
    if (asset.url.empty()) { out_error = "nenhum build Windows na release"; return false; }

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
        std::string exe_name = asset.name.empty() ? (def.id + ".exe") : asset.name;
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
    InitializeCriticalSection(&s_pending_lock);

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

    // Already has a ROM? Nothing to do.
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
// CreateProcessW, and spawning the exit-monitor thread. Must only ever be
// called from the UI thread - it touches CoreShutdown()/window state that
// core_runner.cpp and main_win32.cpp otherwise only ever touch from there.
static bool LaunchResolvedExecutable(const std::string& exe_path, const PortDefinition* def,
                                      const std::string& port_id) {
    // 1. Auto-copy ROM if this port needs one and doesn't have it yet.
    if (def && def->needs_rom) PortAutoSetupRom(port_id);

    // 2. Stop any active Libretro core
    if (CoreIsRunning()) {
        CoreShutdown();
    }

    // 3. Minimize MiSTer Window for clean seamless console transition
    HWND hwnd = MainGetHwnd();
    if (hwnd) {
        ShowWindow(hwnd, SW_MINIMIZE);
    }

    // 4. Launch process
    fs::path abs_exe = fs::absolute(exe_path);
    fs::path abs_dir = abs_exe.parent_path();

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
        std::wstring cmdline = L"cmd.exe /c \"" + wexe + L"\"";
        std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
        buf.push_back(L'\0');
        ok = CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, 0, NULL, wdir.c_str(), &si, &pi);
    } else {
        ok = CreateProcessW(wexe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, wdir.c_str(), &si, &pi);
    }

    if (!ok) {
        if (hwnd) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        CoreSetToast("FALHA AO INICIAR PORT", 180);
        return false;
    }

    s_port_running.store(true);
    CoreSetToast("INICIANDO PORT NATIVO...", 120);

    // 5. Monitor in background thread
    HANDLE hProcess = pi.hProcess;
    HANDLE hThread = pi.hThread;

    std::thread([hProcess, hThread, hwnd]() {
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hThread);
        CloseHandle(hProcess);

        s_port_running.store(false);

        // Restore MiSTer window with full focus
        if (hwnd) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
        }
        OsdEnable();
    }).detach();

    return true;
}

bool PortLaunch(const std::string& port_id) {
    if (s_port_running.load()) return false;

    if (s_installing.load()) {
        CoreSetToast("JA HA UM DOWNLOAD DE PORT EM ANDAMENTO", 150);
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

        s_installing.store(true);
        CoreSetToast(("BAIXANDO " + def->display_name + "...").c_str(), 600);

        PortDefinition def_copy = *def;
        std::thread([def_copy]() {
            std::string error;
            bool ok = DownloadAndInstall(def_copy, error);
            s_installing.store(false);

            if (!ok) {
                CoreSetToast(("FALHA AO BAIXAR PORT: " + error).c_str(), 240);
                return;
            }

            if (def_copy.needs_rom) PortAutoSetupRom(def_copy.id);

            CoreSetToast("PORT INSTALADO! INICIANDO...", 150);

            // Hand off to the UI thread instead of launching from here - see
            // the comment on s_pending_lock above for why.
            EnterCriticalSection(&s_pending_lock);
            s_pending_launch_id = def_copy.id;
            LeaveCriticalSection(&s_pending_lock);
            s_has_pending_launch.store(true);
        }).detach();

        return true;
    }

    return LaunchResolvedExecutable(exe_path, def, port_id);
}

void PortPumpPendingLaunch() {
    if (!s_has_pending_launch.exchange(false)) return;

    std::string id;
    EnterCriticalSection(&s_pending_lock);
    id = s_pending_launch_id;
    LeaveCriticalSection(&s_pending_lock);

    // Re-resolve rather than trust anything captured before the download
    // finished - the files just landed on disk and this is the first look
    // at them from the thread that's actually allowed to act on it.
    std::string dir = ResolvePortDir(id);
    std::string exe_path = dir.empty() ? "" : FindBestExecutable(dir);
    if (exe_path.empty()) {
        CoreSetToast("PORT BAIXADO MAS SEM EXECUTAVEL", 200);
        return;
    }

    LaunchResolvedExecutable(exe_path, FindPortDef(id), id);
}

bool PortIsRunning() {
    return s_port_running.load();
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

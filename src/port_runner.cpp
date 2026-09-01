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
        {
            "DrMario64", "Dr. Mario 64 (Recompiled 60 FPS)",
            "theboy181/drmario64_recomp_plus", "drmario64_recomp.exe",
            true, { "dr", "mario" }
        },
        {
            "Zelda64Recomp", "Zelda 64: Recompiled (OoT/MM)",
            "Zelda64Recomp/Zelda64Recomp", "",
            true, { "zelda" }
        },
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

// Largest .exe under dir that isn't an obvious installer/helper tool. Used
// both to confirm a port is installed (exe_hint may be stale or unknown)
// and to resolve the launch target after a fresh download.
static std::string FindBestExecutable(const std::string& dir) {
    static const std::vector<std::string> blacklist = {
        "unins000.exe", "uninstall.exe", "vc_redist", "dxwebsetup", "dxsetup",
        "crashpad_handler.exe", "crashhandler.exe", "vcredist"
    };
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return "";

    std::string best;
    uintmax_t best_size = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (ec || !e.is_regular_file(ec)) continue;
        if (ToLowerStr(e.path().extension().string()) != ".exe") continue;

        std::string fname = ToLowerStr(e.path().filename().string());
        bool skip = false;
        for (const auto& b : blacklist)
            if (fname.find(b) != std::string::npos) { skip = true; break; }
        if (skip) continue;

        std::error_code sz_ec;
        uintmax_t sz = e.file_size(sz_ec);
        if (sz_ec) continue;
        if (best.empty() || sz > best_size) { best = e.path().string(); best_size = sz; }
    }
    return best;
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

static std::string PickWindowsAssetUrl(const std::vector<GhAsset>& assets) {
    auto is_zip = [](const std::string& n) { return n.size() >= 4 && n.substr(n.size() - 4) == ".zip"; };

    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        if (is_zip(n) && n.find("windows") != std::string::npos) return a.url;
    }
    // No asset says "Windows" explicitly - take any zip that doesn't say it's
    // for somewhere else, rather than refusing outright.
    for (const auto& a : assets) {
        std::string n = ToLowerStr(a.name);
        bool other_os = n.find("linux") != std::string::npos || n.find("macos") != std::string::npos ||
                         n.find("darwin") != std::string::npos || n.find("android") != std::string::npos ||
                         n.find("source") != std::string::npos;
        if (is_zip(n) && !other_os) return a.url;
    }
    return "";
}

// Downloads the latest release of def, extracts it into ports/<id>/ and
// flattens it. Runs on a worker thread - network + disk I/O, seconds to
// minutes depending on the release size and the user's connection.
static bool DownloadAndInstall(const PortDefinition& def, std::string& out_error) {
    if (def.repo.empty()) { out_error = "sem repositorio configurado"; return false; }

    std::string releases_url = "https://api.github.com/repos/" + def.repo + "/releases/latest";
    std::string json;
    if (!UpdaterHttpGetString(releases_url, json)) { out_error = "falha ao consultar o GitHub"; return false; }

    std::vector<GhAsset> assets = ParseReleaseAssets(json);
    std::string asset_url = PickWindowsAssetUrl(assets);
    if (asset_url.empty()) { out_error = "nenhum build Windows na release"; return false; }

    std::error_code ec;
    fs::create_directories("cache", ec);
    std::string zip_path = "cache/" + def.id + "_download.zip";
    if (!UpdaterHttpDownloadToFile(asset_url, zip_path)) { out_error = "falha no download"; return false; }

    std::string dest_dir = "ports/" + def.id;
    bool extracted = ArchiveExtractAll(zip_path, dest_dir);
    fs::remove(zip_path, ec);
    if (!extracted) { out_error = "falha ao extrair o pacote"; return false; }

    FlattenSingleSubfolder(dest_dir);

    if (FindBestExecutable(dest_dir).empty()) { out_error = "pacote extraido sem executavel"; return false; }

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
            PortLaunch(def_copy.id); // now installed - runs the block below
        }).detach();

        return true;
    }

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

    BOOL ok = CreateProcessW(
        wexe.c_str(),
        NULL,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        wdir.c_str(),
        &si,
        &pi
    );

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

bool PortIsRunning() {
    return s_port_running.load();
}

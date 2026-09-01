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

namespace fs = std::filesystem;

extern HWND MainGetHwnd();

static std::atomic<bool> s_port_running(false);

static std::string ToLowerStr(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

void PortInit() {
    std::error_code ec;
    if (!fs::exists("ports", ec)) {
        fs::create_directories("ports", ec);
    }
}

std::vector<PortGameInfo> PortGetAvailableList() {
    std::vector<PortGameInfo> list;

    // Dr. Mario 64 Recompiled Plus
    PortGameInfo drmario;
    drmario.id = "DrMario64";
    drmario.name = "Dr. Mario 64 (Recompiled 60 FPS)";
    drmario.exe_path = "ports/DrMario64/drmario64_recomp.exe";
    drmario.working_dir = "ports/DrMario64";
    drmario.rom_pattern = "mario";
    drmario.is_installed = fs::exists(drmario.exe_path);
    list.push_back(drmario);

    // Scan for any other custom ports placed in ports/
    std::error_code ec;
    if (fs::exists("ports", ec) && fs::is_directory("ports", ec)) {
        for (const auto& entry : fs::directory_iterator("ports", ec)) {
            if (entry.is_directory(ec)) {
                std::string folder_name = entry.path().filename().string();
                if (folder_name == "DrMario64") continue;

                // Look for .exe files inside
                for (const auto& sub : fs::directory_iterator(entry.path(), ec)) {
                    if (sub.is_regular_file(ec) && sub.path().extension() == ".exe") {
                        PortGameInfo custom;
                        custom.id = folder_name;
                        custom.name = folder_name + " (PC Port)";
                        custom.exe_path = sub.path().string();
                        custom.working_dir = entry.path().string();
                        custom.is_installed = true;
                        list.push_back(custom);
                        break;
                    }
                }
            }
        }
    }

    return list;
}

bool PortIsInstalled(const std::string& port_id) {
    if (port_id == "DrMario64") {
        return fs::exists("ports/DrMario64/drmario64_recomp.exe") ||
               fs::exists("app/ports/DrMario64/drmario64_recomp.exe");
    }

    std::string p1 = "ports/" + port_id;
    return fs::exists(p1);
}

bool PortAutoSetupRom(const std::string& port_id) {
    if (port_id == "DrMario64") {
        std::string target_dir = fs::exists("ports/DrMario64") ? "ports/DrMario64" : "app/ports/DrMario64";
        std::error_code ec;
        if (!fs::exists(target_dir, ec)) return false;

        // Check if a ROM already exists in the port folder
        bool has_rom_already = false;
        for (const auto& f : fs::directory_iterator(target_dir, ec)) {
            std::string ext = ToLowerStr(f.path().extension().string());
            if (ext == ".z64" || ext == ".n64" || ext == ".v64") {
                has_rom_already = true;
                break;
            }
        }
        if (has_rom_already) return true;

        // Search in roms/Nintendo64, roms/N64, roms/
        std::vector<std::string> search_dirs = {
            "roms/Nintendo64", "roms/N64", "roms",
            "app/roms/Nintendo64", "app/roms/N64", "app/roms"
        };

        for (const auto& sdir : search_dirs) {
            if (!fs::exists(sdir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(sdir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string fname = ToLowerStr(entry.path().filename().string());
                    std::string ext = ToLowerStr(entry.path().extension().string());
                    if ((ext == ".z64" || ext == ".n64" || ext == ".v64") &&
                        (fname.find("mario") != std::string::npos && (fname.find("dr") != std::string::npos || fname.find("drmario") != std::string::npos))) {
                        // Found Dr. Mario 64 ROM! Copy to port directory
                        fs::path dest = fs::path(target_dir) / entry.path().filename();
                        fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool PortLaunch(const std::string& port_id) {
    if (s_port_running.load()) return false;

    // 1. Auto-copy ROM if available
    PortAutoSetupRom(port_id);

    // 2. Resolve executable and working directory
    std::string exe_path = "";
    std::string work_dir = "";

    if (port_id == "DrMario64") {
        if (fs::exists("ports/DrMario64/drmario64_recomp.exe")) {
            exe_path = "ports/DrMario64/drmario64_recomp.exe";
            work_dir = "ports/DrMario64";
        } else if (fs::exists("app/ports/DrMario64/drmario64_recomp.exe")) {
            exe_path = "app/ports/DrMario64/drmario64_recomp.exe";
            work_dir = "app/ports/DrMario64";
        }
    } else {
        auto list = PortGetAvailableList();
        for (const auto& p : list) {
            if (p.id == port_id || p.name == port_id) {
                exe_path = p.exe_path;
                work_dir = p.working_dir;
                break;
            }
        }
    }

    if (exe_path.empty() || !fs::exists(exe_path)) {
        CoreSetToast("PORT NAO ENCONTRADO", 150);
        return false;
    }

    // 3. Stop any active Libretro core
    if (CoreIsRunning()) {
        CoreShutdown();
    }

    // 4. Minimize MiSTer Window for clean seamless console transition
    HWND hwnd = MainGetHwnd();
    if (hwnd) {
        ShowWindow(hwnd, SW_MINIMIZE);
    }

    // 5. Launch process
    fs::path abs_exe = fs::absolute(exe_path);
    fs::path abs_dir = fs::absolute(work_dir);

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

    // 6. Monitor in background thread
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

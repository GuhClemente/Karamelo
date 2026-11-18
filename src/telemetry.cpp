#include "telemetry.h"
#include "app_info.h"
#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

static const char* INI_PATH = "Config/karamelo.ini";
static const char* TELEMETRY_API_URL = "https://karamelo-emu.com/api/ping";

static bool g_telemetry_enabled = true;
static bool g_telemetry_loaded = false;
static bool g_telemetry_transient_disabled = false;
static bool g_telemetry_sent_this_session = false;

void TelemetrySetTransientDisable(bool disable) {
    g_telemetry_transient_disabled = disable;
}

static void LoadTelemetrySettings() {
    if (g_telemetry_loaded) return;
    g_telemetry_loaded = true;
    g_telemetry_enabled = true; // Enabled by default (opt-out)

    std::string path = INI_PATH;
    if (!fs::exists(path) && fs::exists("karamelo.ini")) {
        path = "karamelo.ini";
    }

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    bool in_diag = false;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            in_diag = (line == "[Diagnostics]");
            continue;
        }

        if (in_diag) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                k.erase(k.find_last_not_of(" \t") + 1);
                v.erase(0, v.find_first_not_of(" \t"));
                if (k == "telemetry") {
                    g_telemetry_enabled = (v != "0" && v != "false");
                }
            }
        }
    }
}

static void SaveTelemetrySettings() {
    std::string path = INI_PATH;
    fs::create_directories("Config");

    std::vector<std::string> lines;
    bool found_diag = false;
    bool key_written = false;

    if (fs::exists(path)) {
        std::ifstream file(path);
        std::string line;
        bool in_diag = false;
        while (std::getline(file, line)) {
            std::string trimmed = line;
            size_t s = trimmed.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) trimmed = trimmed.substr(s);

            if (!trimmed.empty() && trimmed[0] == '[') {
                if (in_diag && !key_written) {
                    lines.push_back("telemetry=" + std::string(g_telemetry_enabled ? "1" : "0"));
                    key_written = true;
                }
                in_diag = (trimmed.find("[Diagnostics]") == 0);
                if (in_diag) found_diag = true;
            } else if (in_diag && trimmed.find("telemetry") == 0) {
                line = "telemetry=" + std::string(g_telemetry_enabled ? "1" : "0");
                key_written = true;
            }
            lines.push_back(line);
        }
    }

    if (!found_diag) {
        lines.push_back("");
        lines.push_back("[Diagnostics]");
        lines.push_back("telemetry=" + std::string(g_telemetry_enabled ? "1" : "0"));
    } else if (!key_written) {
        lines.push_back("telemetry=" + std::string(g_telemetry_enabled ? "1" : "0"));
    }

    std::ofstream out(path, std::ios::trunc);
    for (const auto& l : lines) {
        out << l << "\n";
    }
}

bool TelemetryIsEnabled() {
    LoadTelemetrySettings();
    return g_telemetry_enabled;
}

void TelemetrySetEnabled(bool enabled) {
    g_telemetry_enabled = enabled;
    g_telemetry_loaded = true;
    SaveTelemetrySettings();
}

void TelemetryInit() {
    LoadTelemetrySettings();
}

// -------------------------------------------------------------
// HTTP POST Dispatch Engine (WinHttp / curl)
// -------------------------------------------------------------
static bool HttpPostTelemetry(const std::string& url, const std::string& json_payload) {
#ifdef _WIN32
    HINTERNET session = WinHttpOpen(L"Karamelo-Telemetry/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    // Fast 5-second timeout for telemetry pings so nothing hangs
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = { 0 };
    wchar_t path[1024] = { 0 };
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;

    wchar_t wurl[1024];
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl, 1024);
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", path, NULL,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    bool success = false;
    if (request) {
        const wchar_t* headers = L"Content-Type: application/json\r\n";
        BOOL sent = WinHttpSendRequest(request, headers, (DWORD)-1L,
                                       (LPVOID)json_payload.data(), (DWORD)json_payload.size(),
                                       (DWORD)json_payload.size(), 0);
        if (sent && WinHttpReceiveResponse(request, NULL)) {
            DWORD status = 0, status_len = sizeof(status);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len, WINHTTP_NO_HEADER_INDEX)) {
                success = (status >= 200 && status < 300);
            }
        }
        WinHttpCloseHandle(request);
    }

    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return success;
#else
    std::string tmp_json = "scratch_telemetry.json";
    FILE* tf = fopen(tmp_json.c_str(), "w");
    if (!tf) return false;
    fwrite(json_payload.data(), 1, json_payload.size(), tf);
    fclose(tf);

    std::vector<std::string> args = {
        "curl", "-s", "--max-time", "5", "-X", "POST",
        "-H", "Content-Type: application/json",
        "--data-binary", "@" + tmp_json,
        url, "-o", "/dev/null"
    };

    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid;
    int rc = posix_spawnp(&pid, "curl", NULL, NULL, argv.data(), environ);
    bool ok = false;
    if (rc == 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        ok = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    unlink(tmp_json.c_str());
    return ok;
#endif
}

static std::string EscapeJsonString(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if ((unsigned char)c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
            ss << buf;
        } else {
            ss << c;
        }
    }
    return ss.str();
}

static std::string GetSystemOsString() {
#if defined(_WIN32)
    std::string os = "Windows x64";
    typedef LONG(NTAPI* fnRtlGetVersion)(PRTL_OSVERSIONINFOW);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        fnRtlGetVersion pRtlGetVersion = (fnRtlGetVersion)GetProcAddress(hNt, "RtlGetVersion");
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW vi;
            memset(&vi, 0, sizeof(vi));
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (pRtlGetVersion(&vi) == 0) {
                if (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000) {
                    os = "Windows 11 Build " + std::to_string(vi.dwBuildNumber) + " x64";
                } else if (vi.dwMajorVersion == 10) {
                    os = "Windows 10 Build " + std::to_string(vi.dwBuildNumber) + " x64";
                } else {
                    os = "Windows " + std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion) + " x64";
                }
            }
        }
    }
    return os;
#elif defined(__APPLE__)
    return "macOS arm64";
#elif defined(__linux__)
    return "Linux x64";
#else
    return "Generic Unknown";
#endif
}

static std::string GetSystemGpuString() {
#if defined(_WIN32)
    std::string gpu = "Desconhecida";
    DISPLAY_DEVICEA dd;
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesA(NULL, 0, &dd, 0)) {
        if (dd.DeviceString[0]) {
            gpu = dd.DeviceString;
        }
    }
    return gpu;
#elif defined(__APPLE__)
    return "Apple Silicon";
#elif defined(__linux__)
    return "Linux GPU";
#else
    return "Unknown GPU";
#endif
}

static std::string GetSystemVideoDriver() {
#if defined(_WIN32)
    return "Direct3D 11";
#elif defined(__APPLE__)
    return "Metal / SDL3";
#elif defined(__linux__)
    return "OpenGL / SDL3";
#else
    return "Generic Renderer";
#endif
}

void TelemetrySendHeartbeat() {
    if (g_telemetry_transient_disabled) return;
    if (g_telemetry_sent_this_session) return;
    LoadTelemetrySettings();
    if (!g_telemetry_enabled) return;

    g_telemetry_sent_this_session = true;

    std::string os_str = GetSystemOsString();
    std::string gpu_str = GetSystemGpuString();
    std::string driver_str = GetSystemVideoDriver();
    std::string lang_str = (MenuGetLanguage() == 1) ? "en-US" : "pt-BR";

    std::ostringstream json;
    json << "{\n";
    json << "  \"event\": \"heartbeat\",\n";
    json << "  \"app\": \"Karamelo\",\n";
    json << "  \"version\": \"" << EscapeJsonString(APP_VERSION) << "\",\n";
    json << "  \"os\": \"" << EscapeJsonString(os_str) << "\",\n";
    json << "  \"video_driver\": \"" << EscapeJsonString(driver_str) << "\",\n";
    json << "  \"gpu\": \"" << EscapeJsonString(gpu_str) << "\",\n";
    json << "  \"lang\": \"" << EscapeJsonString(lang_str) << "\"\n";
    json << "}";

    std::string payload = json.str();

    std::thread([payload]() {
        bool ok = HttpPostTelemetry(TELEMETRY_API_URL, payload);
        FILE* lf = fopen("karamelo.log", "a");
        if (lf) {
            fprintf(lf, "[INFO] [TELEMETRY] Heartbeat enviado para %s: %s\n",
                    TELEMETRY_API_URL, ok ? "SUCESSO (HTTP 200)" : "FALHA / OFFLINE");
            fclose(lf);
        }
    }).detach();
}

#include "crash_reporter.h"
#include "app_info.h"
#include "core_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <signal.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;

static const char* CRASH_DUMP_PATH = "crash_dump.txt";
static const char* INI_PATH = "Config/karamelo.ini";
static const char* CRASH_API_URL = "https://karamelo-emu.com/api/crash-report";

static std::mutex g_crash_context_lock;
static std::string g_last_core_name = "Nenhum";
static std::string g_last_game_stem = "Nenhum";
static bool g_crash_reporting_enabled = true;
static bool g_settings_loaded = false;

// -------------------------------------------------------------
// Settings Management ([Diagnostics] in Config/karamelo.ini)
// -------------------------------------------------------------
static void LoadDiagnosticsSettings() {
    if (g_settings_loaded) return;
    g_settings_loaded = true;

    std::string path = INI_PATH;
    if (!fs::exists(path) && fs::exists("karamelo.ini")) {
        path = "karamelo.ini";
    }

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    bool in_diag = false;
    while (std::getline(file, line)) {
        // Trim whitespace
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
                // trim
                k.erase(k.find_last_not_of(" \t") + 1);
                v.erase(0, v.find_first_not_of(" \t"));
                if (k == "crash_reporting") {
                    g_crash_reporting_enabled = (v != "0" && v != "false");
                }
            }
        }
    }
}

static void SaveDiagnosticsSettings() {
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
                    lines.push_back("crash_reporting=" + std::string(g_crash_reporting_enabled ? "1" : "0"));
                    key_written = true;
                }
                in_diag = (trimmed.find("[Diagnostics]") == 0);
                if (in_diag) found_diag = true;
            } else if (in_diag && trimmed.find("crash_reporting") == 0) {
                line = "crash_reporting=" + std::string(g_crash_reporting_enabled ? "1" : "0");
                key_written = true;
            }
            lines.push_back(line);
        }
    }

    if (!found_diag) {
        lines.push_back("");
        lines.push_back("[Diagnostics]");
        lines.push_back("crash_reporting=" + std::string(g_crash_reporting_enabled ? "1" : "0"));
    } else if (!key_written) {
        lines.push_back("crash_reporting=" + std::string(g_crash_reporting_enabled ? "1" : "0"));
    }

    std::ofstream out(path, std::ios::trunc);
    for (const auto& l : lines) {
        out << l << "\n";
    }
}

bool CrashReporterIsEnabled() {
    LoadDiagnosticsSettings();
    return g_crash_reporting_enabled;
}

void CrashReporterSetEnabled(bool enabled) {
    g_crash_reporting_enabled = enabled;
    g_settings_loaded = true;
    SaveDiagnosticsSettings();
}

void CrashReporterSetLastGameInfo(const char* core_name, const char* game_stem) {
    std::lock_guard<std::mutex> lock(g_crash_context_lock);
    if (core_name && core_name[0]) g_last_core_name = core_name;
    else g_last_core_name = "Nenhum";

    if (game_stem && game_stem[0]) g_last_game_stem = game_stem;
    else g_last_game_stem = "Nenhum";
}

// -------------------------------------------------------------
// Privacy & Sanitization Engine
// -------------------------------------------------------------
std::string CrashReporterSanitizeString(const std::string& input) {
    std::string s = input;

    // 1. Sanitize Windows user directories: C:\Users\<Username>
    size_t pos = 0;
    while ((pos = s.find("Users\\", pos)) != std::string::npos) {
        size_t next_slash = s.find('\\', pos + 6);
        if (next_slash != std::string::npos) {
            s.replace(pos + 6, next_slash - (pos + 6), "[USER]");
            pos += 6 + 6;
        } else {
            break;
        }
    }

    // 2. Sanitize macOS / Linux user directories: /Users/<Username>/ or /home/<Username>/
    pos = 0;
    while ((pos = s.find("/Users/", pos)) != std::string::npos) {
        size_t next_slash = s.find('/', pos + 7);
        if (next_slash != std::string::npos) {
            s.replace(pos + 7, next_slash - (pos + 7), "[USER]");
            pos += 7 + 6;
        } else {
            break;
        }
    }
    pos = 0;
    while ((pos = s.find("/home/", pos)) != std::string::npos) {
        size_t next_slash = s.find('/', pos + 6);
        if (next_slash != std::string::npos) {
            s.replace(pos + 6, next_slash - (pos + 6), "[USER]");
            pos += 6 + 6;
        } else {
            break;
        }
    }

    // 3. Redact tokens, passwords, keys
    const char* sensitive_keys[] = { "token=", "Token=", "password=", "key=", "Bearer " };
    for (const char* pattern : sensitive_keys) {
        pos = 0;
        size_t plen = strlen(pattern);
        while ((pos = s.find(pattern, pos)) != std::string::npos) {
            size_t val_start = pos + plen;
            size_t val_end = s.find_first_of(" \t\r\n;&\"", val_start);
            if (val_end == std::string::npos) val_end = s.length();
            s.replace(val_start, val_end - val_start, "[REDACTED]");
            pos = val_start + 10;
        }
    }

    return s;
}

// -------------------------------------------------------------
// Low-Level Dump Recording
// -------------------------------------------------------------
void CrashReporterWriteDump(const char* fault_type, unsigned long code,
                           const char* module_name, uintptr_t fault_addr,
                           const std::string& stack_trace_text) {
    FILE* f = fopen(CRASH_DUMP_PATH, "w");
    if (!f) return;

    std::string core_name = "Nenhum";
    std::string game_stem = "Nenhum";
    {
        std::lock_guard<std::mutex> lock(g_crash_context_lock);
        core_name = g_last_core_name;
        game_stem = g_last_game_stem;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    char time_str[64] = { 0 };
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&tt));

    fprintf(f, "version=%s\n", APP_VERSION);
#if defined(_WIN32)
    fprintf(f, "os=Windows x64\n");
#elif defined(__APPLE__)
    fprintf(f, "os=macOS arm64\n");
#elif defined(__linux__)
    fprintf(f, "os=Linux x64\n");
#else
    fprintf(f, "os=Generic Unknown\n");
#endif
    fprintf(f, "timestamp=%s\n", time_str);
    fprintf(f, "core=%s\n", core_name.c_str());
    fprintf(f, "game=%s\n", game_stem.c_str());
    fprintf(f, "fault_type=%s\n", fault_type ? fault_type : "UNKNOWN");
    fprintf(f, "code=0x%08lX\n", code);
    fprintf(f, "module=%s\n", module_name ? module_name : "Karamelo");
    fprintf(f, "fault_addr=0x%llX\n", (unsigned long long)fault_addr);
    fprintf(f, "stack_trace_begin\n");
    fprintf(f, "%s\n", stack_trace_text.c_str());
    fprintf(f, "stack_trace_end\n");

    fclose(f);

    // Also record in karamelo.log
    FILE* lf = fopen("karamelo.log", "a");
    if (lf) {
        fprintf(lf, "[ERROR] [CRASH-REPORTER] Falha gravada em %s: tipo=%s codigo=0x%08lX core=%s game=%s\n",
                CRASH_DUMP_PATH, fault_type, code, core_name.c_str(), game_stem.c_str());
        fclose(lf);
    }
}

// -------------------------------------------------------------
// POSIX Signal Handlers (Linux & macOS)
// -------------------------------------------------------------
#ifndef _WIN32
static void PosixSignalHandler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;
    const char* sig_name = "SIGSEGV";
    if (sig == SIGSEGV) sig_name = "SIGSEGV (Segmentation Fault)";
    else if (sig == SIGBUS) sig_name = "SIGBUS (Bus Error)";
    else if (sig == SIGABRT) sig_name = "SIGABRT (Abort)";
    else if (sig == SIGFPE) sig_name = "SIGFPE (Arithmetic Exception)";
    else if (sig == SIGILL) sig_name = "SIGILL (Illegal Instruction)";

    uintptr_t addr = info ? (uintptr_t)info->si_addr : 0;

    std::string trace;
#if defined(__APPLE__) || defined(__linux__)
    void* callstack[64];
    int frames = backtrace(callstack, 64);
    char** strs = backtrace_symbols(callstack, frames);
    if (strs) {
        for (int i = 0; i < frames; i++) {
            trace += strs[i];
            trace += "\n";
        }
        free(strs);
    }
#endif

    CrashReporterWriteDump(sig_name, (unsigned long)sig, "Karamelo", addr, trace);

    // Reinstall default handler and re-raise so core dump / debugger behaves normally
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

void CrashReporterInit() {
    LoadDiagnosticsSettings();

#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sa.sa_sigaction = PosixSignalHandler;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
#endif
}

// -------------------------------------------------------------
// HTTP POST Dispatch Engine
// -------------------------------------------------------------
static bool HttpPostJson(const std::string& url, const std::string& json_payload) {
#ifdef _WIN32
    HINTERNET session = WinHttpOpen(L"Karamelo-CrashReporter/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);

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
    // POSIX curl execution
    std::string tmp_json = "scratch_crash.json";
    FILE* tf = fopen(tmp_json.c_str(), "w");
    if (!tf) return false;
    fwrite(json_payload.data(), 1, json_payload.size(), tf);
    fclose(tf);

    std::vector<std::string> args = {
        "curl", "-s", "-X", "POST",
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

// Builds a clean JSON string with escaped content
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

void CrashReporterCheckAndDispatch() {
    LoadDiagnosticsSettings();

    if (!fs::exists(CRASH_DUMP_PATH)) {
        return;
    }

    if (!g_crash_reporting_enabled) {
        // Silently remove dump without transmission
        std::error_code ec;
        fs::remove(CRASH_DUMP_PATH, ec);
        return;
    }

    // Read dump
    std::ifstream file(CRASH_DUMP_PATH);
    if (!file.is_open()) return;

    std::string line;
    std::string app_ver = APP_VERSION;
    std::string os_name = "Unknown";
    std::string timestamp = "";
    std::string core_name = "Nenhum";
    std::string game_stem = "Nenhum";
    std::string fault_type = "UNKNOWN";
    std::string code = "";
    std::string module_name = "";
    std::string fault_addr = "";
    std::string stack_trace = "";
    bool reading_trace = false;

    while (std::getline(file, line)) {
        if (line == "stack_trace_begin") {
            reading_trace = true;
            continue;
        }
        if (line == "stack_trace_end") {
            reading_trace = false;
            continue;
        }
        if (reading_trace) {
            stack_trace += line + "\n";
            continue;
        }

        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            if (k == "version") app_ver = v;
            else if (k == "os") os_name = v;
            else if (k == "timestamp") timestamp = v;
            else if (k == "core") core_name = v;
            else if (k == "game") game_stem = v;
            else if (k == "fault_type") fault_type = v;
            else if (k == "code") code = v;
            else if (k == "module") module_name = v;
            else if (k == "fault_addr") fault_addr = v;
        }
    }
    file.close();

    // Clean up dump file immediately so repeated launches don't loop
    std::error_code ec;
    fs::remove(CRASH_DUMP_PATH, ec);

    // Sanitize stack trace & paths
    stack_trace = CrashReporterSanitizeString(stack_trace);

    // Build JSON payload
    std::ostringstream json;
    json << "{\n";
    json << "  \"app\": \"Karamelo\",\n";
    json << "  \"version\": \"" << EscapeJsonString(app_ver) << "\",\n";
    json << "  \"os\": \"" << EscapeJsonString(os_name) << "\",\n";
    json << "  \"timestamp\": \"" << EscapeJsonString(timestamp) << "\",\n";
    json << "  \"core\": \"" << EscapeJsonString(core_name) << "\",\n";
    json << "  \"game\": \"" << EscapeJsonString(game_stem) << "\",\n";
    json << "  \"fault_type\": \"" << EscapeJsonString(fault_type) << "\",\n";
    json << "  \"code\": \"" << EscapeJsonString(code) << "\",\n";
    json << "  \"module\": \"" << EscapeJsonString(module_name) << "\",\n";
    json << "  \"fault_addr\": \"" << EscapeJsonString(fault_addr) << "\",\n";
    json << "  \"stack_trace\": \"" << EscapeJsonString(stack_trace) << "\"\n";
    json << "}";

    std::string payload = json.str();

    // Spawn detached thread for asynchronous delivery without freezing UI
    std::thread([payload]() {
        bool ok = HttpPostJson(CRASH_API_URL, payload);
        FILE* lf = fopen("karamelo.log", "a");
        if (lf) {
            fprintf(lf, "[INFO] [CRASH-REPORTER] Despacho assincrono para %s: %s\n",
                    CRASH_API_URL, ok ? "SUCESSO (HTTP 200)" : "FALHA / OFFLINE");
            fclose(lf);
        }
    }).detach();

    CoreSetToast("RELATORIO DE FALHA ENVIADO A EQUIPE", 150);
}

bool CrashReporterSendTest() {
    LoadDiagnosticsSettings();

    std::string core_name = "Nenhum";
    std::string game_stem = "Nenhum";
    {
        std::lock_guard<std::mutex> lock(g_crash_context_lock);
        core_name = g_last_core_name;
        game_stem = g_last_game_stem;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    char time_str[64] = { 0 };
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&tt));

    std::ostringstream json;
    json << "{\n";
    json << "  \"app\": \"Karamelo\",\n";
    json << "  \"version\": \"" << APP_VERSION << "\",\n";
#if defined(_WIN32)
    json << "  \"os\": \"Windows x64\",\n";
#elif defined(__APPLE__)
    json << "  \"os\": \"macOS arm64\",\n";
#else
    json << "  \"os\": \"Linux x64\",\n";
#endif
    json << "  \"timestamp\": \"" << time_str << "\",\n";
    json << "  \"core\": \"" << EscapeJsonString(core_name) << "\",\n";
    json << "  \"game\": \"" << EscapeJsonString(game_stem) << "\",\n";
    json << "  \"fault_type\": \"TEST_DIAGNOSTIC_PING\",\n";
    json << "  \"code\": \"0x00000000\",\n";
    json << "  \"module\": \"OSD_Settings_Test\",\n";
    json << "  \"fault_addr\": \"0x0\",\n";
    json << "  \"stack_trace\": \"Ping de teste manual executado pelo usuario no OSD.\"\n";
    json << "}";

    std::string payload = json.str();

    std::thread([payload]() {
        bool ok = HttpPostJson(CRASH_API_URL, payload);
        FILE* lf = fopen("karamelo.log", "a");
        if (lf) {
            fprintf(lf, "[INFO] [CRASH-REPORTER] Teste manual para %s: %s\n",
                    CRASH_API_URL, ok ? "SUCESSO" : "FALHA / OFFLINE");
            fclose(lf);
        }
    }).detach();

    CoreSetToast("TESTE DE DIAGNOSTICO ENVIADO!", 120);
    return true;
}

#pragma once

#ifdef __cplusplus
#include <string>
#include <cstdint>

// =====================================================================
//   Karamelo Crash Reporter — Native Multiplatform Diagnostics & Telemetry
// =====================================================================

// Initializes low-level fault handlers:
// - Windows: UnhandledExceptionFilter / SEH hook
// - Linux / macOS: sigaction() for SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL
void CrashReporterInit();

// Checks if a previous crash dump exists on startup.
// If enabled, sends the report asynchronously in the background and cleans up.
// If disabled, cleans up the dump immediately without transmitting.
void CrashReporterCheckAndDispatch();

// Returns whether crash reporting is enabled in Config/karamelo.ini
bool CrashReporterIsEnabled();

// Updates crash reporting preference and saves to Config/karamelo.ini
void CrashReporterSetEnabled(bool enabled);

// Sends a test diagnostic payload to verify endpoint connectivity
bool CrashReporterSendTest();

// Updates active game and core stem for crash contextual diagnostics
void CrashReporterSetLastGameInfo(const char* core_name, const char* game_stem);

// Sanitizes private info (user home paths, RetroAchievements tokens, passwords)
std::string CrashReporterSanitizeString(const std::string& input);

// Low-level write of a crash dump file (crash_dump.txt)
void CrashReporterWriteDump(const char* fault_type, unsigned long code,
                           const char* module_name, uintptr_t fault_addr,
                           const std::string& stack_trace_text);

#endif // __cplusplus

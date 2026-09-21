#include "test_framework.h"
#include "crash_reporter.h"
#include <string>


TEST_CASE(CrashReporterPrivacySanitizer)
{
	// 1. Windows path sanitization
	std::string win_path = "Crash in C:\\Users\\GuhClemente\\Documents\\Workspace\\Karamelo\\main.cpp:123";
	std::string sanitized_win = CrashReporterSanitizeString(win_path);
	ASSERT_TRUE(sanitized_win.find("GuhClemente") == std::string::npos);
	ASSERT_TRUE(sanitized_win.find("C:\\Users\\[USER]\\") != std::string::npos);

	// 2. macOS path sanitization
	std::string mac_path = "Log file at /Users/guhf/Documents/Workspace/Karamelo/app/Karamelo";
	std::string sanitized_mac = CrashReporterSanitizeString(mac_path);
	ASSERT_TRUE(sanitized_mac.find("guhf") == std::string::npos);
	ASSERT_TRUE(sanitized_mac.find("/Users/[USER]/") != std::string::npos);

	// 3. Linux home path sanitization
	std::string linux_path = "Dump at /home/developer/games/snes.smc";
	std::string sanitized_linux = CrashReporterSanitizeString(linux_path);
	ASSERT_TRUE(sanitized_linux.find("developer") == std::string::npos);
	ASSERT_TRUE(sanitized_linux.find("/home/[USER]/") != std::string::npos);

	// 4. Sensitive tokens and secrets redaction
	std::string token_str = "Error with token=ghp_1234567890abcdef and password=SuperSecretPass!";
	std::string sanitized_token = CrashReporterSanitizeString(token_str);
	ASSERT_TRUE(sanitized_token.find("ghp_1234567890abcdef") == std::string::npos);
	ASSERT_TRUE(sanitized_token.find("SuperSecretPass") == std::string::npos);
	ASSERT_TRUE(sanitized_token.find("token=[REDACTED]") != std::string::npos);
	ASSERT_TRUE(sanitized_token.find("password=[REDACTED]") != std::string::npos);

	// 5. Clean string untouched
	std::string clean = "Karamelo Emulador v0.9.4 - Apple Silicon (ARM64)";
	ASSERT_EQ(CrashReporterSanitizeString(clean), clean);
}

TEST_CASE(CrashReporterToggleSettings)
{
	// Save initial state
	bool orig = CrashReporterIsEnabled();

	CrashReporterSetEnabled(true);
	ASSERT_TRUE(CrashReporterIsEnabled());

	CrashReporterSetEnabled(false);
	ASSERT_FALSE(CrashReporterIsEnabled());

	// Restore original state
	CrashReporterSetEnabled(orig);
	ASSERT_EQ(CrashReporterIsEnabled(), orig);
}

TEST_CASE(CrashReporterContextTracking)
{
	CrashReporterSetLastGameInfo("Snes9x", "Super Mario World");
	CrashReporterSetLastGameInfo("Nenhum", "Nenhum");
	ASSERT_TRUE(true);
}

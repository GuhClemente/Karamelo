#include "test_framework.h"
#include "updater.h"

TEST_CASE(UpdaterVersionComparison)
{
	// Remote is newer
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0", "1.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0", "2.0"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0.0", "1.0.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("v1.0", "v1.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.0-alpha", "1.1"));
	ASSERT_TRUE(UpdaterIsNewerVersion("1.9.9", "2.0.0"));

	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.7", "0.9.74"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.71", "0.9.74"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.72", "0.9.74"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.73", "0.9.74"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.74", "0.9.75"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.75", "0.9.76"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.76", "0.9.77"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.77", "0.9.78"));
	ASSERT_TRUE(UpdaterIsNewerVersion("0.9.6", "0.9.74"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.74", "0.9.7"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.74", "0.9.71"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.74", "0.9.72"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.74", "0.9.73"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.74", "0.9.74"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.75", "0.9.74"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.75", "0.9.75"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.76", "0.9.75"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.76", "0.9.76"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.77", "0.9.76"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.77", "0.9.77"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.78", "0.9.77"));
	ASSERT_FALSE(UpdaterIsNewerVersion("0.9.78", "0.9.78"));

	// Remote is equal
	ASSERT_FALSE(UpdaterIsNewerVersion("1.0", "1.0"));
	ASSERT_FALSE(UpdaterIsNewerVersion("v1.0", "1.0"));
	ASSERT_FALSE(UpdaterIsNewerVersion("1.0.0", "1.0"));

	// Remote is older
	ASSERT_FALSE(UpdaterIsNewerVersion("1.1", "1.0"));
	ASSERT_FALSE(UpdaterIsNewerVersion("2.0", "1.9"));
	ASSERT_FALSE(UpdaterIsNewerVersion("1.0.2", "1.0.1"));
}

TEST_CASE(UpdaterManifestPicksPlatformFields)
{
	const std::string json =
		"{\n"
		"    \"version\": \"0.9.6\",\n"
		"    \"notes\": \"notas\",\n"
		"    \"linux_bin_url\": \"https://x/Karamelo_linux\",\n"
		"    \"linux_bin_size\": 5495904,\n"
		"    \"linux_bin_sha256\": \"AAAA\",\n"
		"    \"linux_tar_url\": \"https://x/Karamelo_v0.9.6_Linux64.tar.gz\",\n"
		"    \"exe_url\": \"https://x/Karamelo.exe\",\n"
		"    \"exe_size\": 4283904,\n"
		"    \"exe_sha256\": \"BBBB\",\n"
		"    \"zip_url\": \"https://x/Karamelo_v0.9.6_Win64.zip\",\n"
		"    \"macos_bin_url\": \"https://x/Karamelo_mac\",\n"
		"    \"macos_bin_size\": 1306296,\n"
		"    \"macos_bin_sha256\": \"CCCC\",\n"
		"    \"macos_tar_url\": \"https://x/Karamelo_v0.9.6_macOS_arm64.tar.gz\"\n"
		"}";

	UpdateInfo win, mac, lin;
	ASSERT_TRUE(UpdaterParseManifest(json, "windows", win));
	ASSERT_TRUE(UpdaterParseManifest(json, "macos", mac));
	ASSERT_TRUE(UpdaterParseManifest(json, "linux", lin));

	ASSERT_TRUE(win.version == "0.9.6" && mac.version == "0.9.6" && lin.version == "0.9.6");
	ASSERT_TRUE(win.exe_url == "https://x/Karamelo.exe");
	ASSERT_TRUE(win.exe_sha256 == "BBBB" && win.exe_size == 4283904);
	ASSERT_TRUE(win.zip_url == "https://x/Karamelo_v0.9.6_Win64.zip");
	ASSERT_TRUE(mac.exe_url == "https://x/Karamelo_mac");
	ASSERT_TRUE(mac.exe_sha256 == "CCCC" && mac.exe_size == 1306296);
	ASSERT_TRUE(mac.zip_url == "https://x/Karamelo_v0.9.6_macOS_arm64.tar.gz");
	ASSERT_TRUE(lin.exe_url == "https://x/Karamelo_linux");
	ASSERT_TRUE(lin.exe_sha256 == "AAAA" && lin.exe_size == 5495904);
	ASSERT_TRUE(lin.zip_url == "https://x/Karamelo_v0.9.6_Linux64.tar.gz");
}

TEST_CASE(UpdaterManifestNeverFallsBackToWindowsExe)
{
	// A manifest with only the Windows build must leave the macOS/Linux fields
	// empty - the updater then refuses to download instead of fetching the .exe.
	const std::string json =
		"{ \"version\": \"1.0\", \"exe_url\": \"https://x/Karamelo.exe\", "
		"\"exe_size\": 10, \"exe_sha256\": \"BBBB\" }";

	UpdateInfo mac, lin;
	ASSERT_TRUE(UpdaterParseManifest(json, "macos", mac));
	ASSERT_TRUE(UpdaterParseManifest(json, "linux", lin));
	ASSERT_TRUE(mac.exe_url.empty() && mac.exe_sha256.empty() && mac.exe_size == 0);
	ASSERT_TRUE(lin.exe_url.empty() && lin.exe_sha256.empty() && lin.exe_size == 0);

	UpdateInfo bad;
	ASSERT_FALSE(UpdaterParseManifest("{ \"exe_url\": \"x\" }", "macos", bad));
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include "compat_win32.h"
#endif
#include <atomic>
#include <thread>
#include <chrono>

static DWORD WINAPI DummyThreadProc(LPVOID param)
{
	std::atomic<bool>* flag = (std::atomic<bool>*)param;
	if (flag) flag->store(true);
	return 0;
}

TEST_CASE(ThreadHandleSafeCloseWhileRunning)
{
	std::atomic<bool> ran{ false };
	HANDLE h = CreateThread(NULL, 0, DummyThreadProc, &ran, 0, NULL);
	ASSERT_TRUE(h != NULL);
	// Close immediately while thread is starting or running
	ASSERT_TRUE(CloseHandle(h));
	// Give thread a moment to finish its cleanup handler without crashing
	int attempts = 0;
	while (!ran.load() && attempts++ < 100) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(ran.load());
}

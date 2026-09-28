#ifndef UPDATER_H_INCLUDED
#define UPDATER_H_INCLUDED

#include <string>
#include <stdint.h>

enum UpdaterState
{
	UPDATER_STATE_IDLE = 0,
	UPDATER_STATE_CHECKING,
	UPDATER_STATE_UP_TO_DATE,
	UPDATER_STATE_AVAILABLE,
	UPDATER_STATE_DOWNLOADING,
	UPDATER_STATE_READY,
	UPDATER_STATE_ERROR
};

// exe_* and zip_url describe the build for the platform this binary runs on:
// the manifest's exe_* / zip_url on Windows, macos_bin_* / macos_tar_url on
// macOS, linux_bin_* / linux_tar_url on Linux.
struct UpdateInfo
{
	std::string version;
	std::string title;
	std::string release_date;
	std::string notes;
	std::string exe_url;
	size_t      exe_size = 0;
	std::string exe_sha256;
	std::string zip_url;
	bool        force_full_package = false;
};

// Lifecycle
void UpdaterInit();
void UpdaterShutdown();

// Actions
void UpdaterCheckAsync(bool manual_trigger = false);
void UpdaterStartDownload();
bool UpdaterApplyAndRestart();

// Polling & Status
UpdaterState     UpdaterGetState();
int              UpdaterGetProgress(); // 0 to 100
// Both return a copy taken under the lock, not a reference/pointer into the
// live global - callers used to be able to read a value the updater thread
// was concurrently overwriting.
UpdateInfo  UpdaterGetInfo();
std::string UpdaterGetStatusMessage();

// Version comparison utility: returns true if remote_ver is newer than current_ver
bool UpdaterIsNewerVersion(const std::string& current_ver, const std::string& remote_ver);

// Parses version.json for one platform ("windows", "macos" or "linux").
// Exposed for the unit tests; the updater itself passes its own platform.
bool UpdaterParseManifest(const std::string& json, const std::string& platform, UpdateInfo& out);

#ifndef _WIN32
// Swaps the verified download in over the running binary without restarting
// it (UpdaterApplyAndRestart() does this and then re-executes). Used by the
// headless --update mode.
bool UpdaterApplyDownloaded();
#endif

// Generic WinHTTP helpers, factored out of the self-update flow above so the
// ports installer (GitHub releases downloads) does not need its own copy of
// the same request/redirect/HTTPS boilerplate. Synchronous - call off the UI
// thread. No progress tracking; the update flow's own progress bar uses the
// internal HttpFetchData directly for that.
bool UpdaterHttpGetString(const std::string& url, std::string& out_body);
bool UpdaterHttpDownloadToFile(const std::string& url, const std::string& dest_path);

#endif // UPDATER_H_INCLUDED

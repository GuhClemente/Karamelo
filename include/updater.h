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
const UpdateInfo& UpdaterGetInfo();
const char*      UpdaterGetStatusMessage();

// Version comparison utility: returns true if remote_ver is newer than current_ver
bool UpdaterIsNewerVersion(const std::string& current_ver, const std::string& remote_ver);

#endif // UPDATER_H_INCLUDED

#ifndef CLOUD_SYNC_H_INCLUDED
#define CLOUD_SYNC_H_INCLUDED

#include <atomic>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>
#include <vector>

// =============================================================
// Karamelo Cloud — Bring Your Own Storage (Google Drive appDataFolder)
// Zero-Knowledge architecture: zero user files stored on karamelo-emu.com
// =============================================================

enum CloudSyncState
{
	CLOUD_DISCONNECTED = 0,
	CLOUD_AUTHENTICATING = 1,
	CLOUD_IDLE = 2,
	CLOUD_SYNCING = 3,
	CLOUD_CONFLICT = 4,
	CLOUD_ERROR = 5
};

enum CloudConflictAction
{
	CLOUD_CONFLICT_NONE = 0,            // Identical SHA-256; no transfer needed
	CLOUD_CONFLICT_UPLOAD_LOCAL = 1,    // Local is strictly newer than remote
	CLOUD_CONFLICT_DOWNLOAD_REMOTE = 2, // Remote is strictly newer than local
	CLOUD_CONFLICT_DIVERGED = 3         // Both modified independently since last sync
};

struct CloudSaveEntry
{
	std::string rel_path;     // e.g. "saves/SNES/Chrono Trigger.srm" or "Config/favorites.txt"
	std::string system_dir;   // e.g. "SNES" or "Config"
	std::string game_stem;    // e.g. "Chrono Trigger"
	std::string sha256;       // Lowercase 64-char hex SHA-256
	uint64_t    size_bytes = 0;
	int64_t     modified_utc = 0;
	uint64_t    playtime_sec = 0;
};

struct CloudProfileManifest
{
	int                         schema_version = 1;
	std::string                 app_version;
	std::string                 device_name;
	int64_t                     updated_utc = 0;
	std::vector<CloudSaveEntry> entries;
};

// Lifecycle & Async Worker
void           CloudSyncInit();
void           CloudSyncShutdown();

// State & Settings
CloudSyncState CloudSyncGetState();
const char*    CloudSyncGetStatusString();
bool           CloudSyncIsConnected();
bool           CloudSyncGetAutoSync();
void           CloudSyncSetAutoSync(bool enabled);
int            CloudSyncGetSyncedFileCount();
const char*    CloudSyncGetLastSyncTimeStr();

// Actions
bool           CloudSyncStartOAuthFlow();
void           CloudSyncDisconnect();
void           CloudSyncTriggerAsync();

// For the chat rooms (desktop_chat.cpp): the same Google login and HTTP as the cloud.
void           CloudSyncGetLocalClient(std::string& out_id, std::string& out_secret); // from Config/, usually empty
bool           CloudSyncGoogleIdToken(const std::string& client_id, const std::string& client_secret,
                                      std::string& out_id_token, std::string& out_error, const std::atomic<bool>* cancel);
bool           CloudSyncHttpRequest(const char* method, const std::string& url, const std::vector<std::string>& headers,
                                    const std::string& body, int timeout_seconds, int* out_status, std::string* out_body);
// A login kept on disk: DPAPI on Windows, a 0600 file elsewhere.
std::string    CloudSyncLoadSecret(const std::string& path);
void           CloudSyncSaveSecret(const std::string& path, const std::string& value); // empty removes it

// One full sync, on the calling thread. False = it did not finish (error_out says
// why) and nothing may be reported as synced. root_dir "" is the app folder.
struct CloudSyncReport
{
	int uploaded = 0;
	int downloaded = 0;
	int conflicts = 0; // changed on two machines: both kept, other side as <save>.conflito
	int skipped = 0;   // left for later because a game is running
	int total = 0;
};
bool           CloudSyncRunNow(const std::string& root_dir, CloudSyncReport* report, std::string* error_out);
// Points the sync at a local test server.
void           CloudSyncSetEndpointsForTest(const std::string& token_url, const std::string& api_base,
                                            const std::string& refresh_token, int64_t last_sync_utc);

// Game Session Hooks (called by CoreRunner on load/unload)
void           CloudSyncOnGameLoad(const char* system_dir, const char* game_stem);
void           CloudSyncOnGameUnload(const char* system_dir, const char* game_stem, uint64_t session_seconds);

// Pure / Testable Manifest, Crypto & Conflict Helpers
std::string         CloudSyncSerializeManifest(const CloudProfileManifest& manifest);
bool                CloudSyncParseManifest(const std::string& json_str, CloudProfileManifest& out_manifest);
CloudConflictAction CloudSyncEvaluateConflict(const CloudSaveEntry& local_e, const CloudSaveEntry& remote_e, int64_t last_synced_utc);
std::string         CloudSyncComputeSha256Bytes(const uint8_t* data, size_t len);
std::string         CloudSyncComputeFileSha256(const std::string& filepath);
std::string         CloudSyncBase64UrlEncode(const uint8_t* data, size_t len);
std::string         CloudSyncBuildPkceChallenge(const std::string& verifier);
bool                CloudSyncScanLocalFiles(const std::string& root_dir, std::vector<CloudSaveEntry>& out_entries);
bool                CloudSyncIsAllowedSyncPath(const std::string& rel_path);

#endif // CLOUD_SYNC_H_INCLUDED


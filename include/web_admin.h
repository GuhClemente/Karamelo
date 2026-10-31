#ifndef WEB_ADMIN_H_INCLUDED
#define WEB_ADMIN_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>
#include <vector>

// =============================================================
// Karamelo Local Web Admin (Wi-Fi / LAN HTTP Server)
// Zero external dependencies; embedded SPA for ROM/BIOS management
// =============================================================

struct RaAchievementInfo;

struct BiosAuditItem
{
	std::string system_name;
	std::string filename;
	std::string expected_md5;
	std::string subdir;
	bool        required = true;
	bool        present = false;
	uint64_t    size_bytes = 0;
};

// Lifecycle
void        WebAdminInit();
void        WebAdminShutdown();

// Server Control (loopback_only=true binds strictly to 127.0.0.1 for testing)
bool        WebAdminStart(int port = 8080, bool loopback_only = false);
void        WebAdminStop();
bool        WebAdminIsRunning();
int         WebAdminGetPort();
const char* WebAdminGetUrl();
const char* WebAdminGetLocalUrl();
void        WebAdminOpenInBrowser();

// PIN Security
const char* WebAdminGetPin();
void        WebAdminRegeneratePin();

// Runtime Subsystem Hooks (Decoupled from Core/Menu for Clean Architecture & Unit Tests)
struct WebAdminHooks {
	void (*process_key)(int key) = nullptr;
	bool (*launch_game)(const std::string& path) = nullptr;
	bool (*is_running)() = nullptr;
	const char* (*get_game_name)() = nullptr;
	const char* (*get_core_name)() = nullptr;
	void (*reset)() = nullptr;
	bool (*save_state)(int slot) = nullptr;
	bool (*load_state)(int slot) = nullptr;
	bool (*screenshot)() = nullptr;
	int  (*get_volume)() = nullptr;
	void (*set_volume)(int v) = nullptr;
	bool (*get_mute)() = nullptr;
	std::vector<std::string> (*get_recents)() = nullptr;

	// Settings & Quick Menu Hooks
	int  (*get_aspect)() = nullptr;
	void (*set_aspect)(int aspect) = nullptr;
	int  (*get_filter)() = nullptr;
	void (*set_filter)(int filter) = nullptr;
	int  (*get_vsync)() = nullptr;
	void (*set_vsync)(int vsync) = nullptr;
	int  (*get_slot)() = nullptr;
	void (*set_slot)(int slot) = nullptr;
	void (*set_mute)(bool mute) = nullptr;
	void (*save_settings)() = nullptr;

	// RetroAchievements Hooks
	const char* (*ra_get_user)() = nullptr;
	const char* (*ra_get_status)() = nullptr;
	uint32_t    (*ra_get_score)() = nullptr;
	bool        (*ra_get_hardcore)() = nullptr;
	bool        (*ra_has_token)() = nullptr;
	bool        (*ra_is_logged_in)() = nullptr;
	bool        (*ra_is_enabled)() = nullptr;
	int         (*ra_get_ach_total)() = nullptr;
	int         (*ra_get_ach_unlocked)() = nullptr;
	bool        (*ra_set_credentials)(const char* user, const char* pass, bool is_tok, bool hc, bool logout) = nullptr;
	void        (*ra_update)() = nullptr;
	const char* (*ra_get_game_title)() = nullptr;
	int         (*ra_get_achievements)(struct RaAchievementInfo* out, int max_out) = nullptr;

	// System Visibility Hooks
	bool        (*is_system_visible)(int id) = nullptr;
	void        (*set_system_visible)(int id, bool visible) = nullptr;
	void        (*set_all_systems_visible)(bool visible) = nullptr;
	void        (*hide_empty_systems)() = nullptr;
	void        (*hide_category)(const char* category) = nullptr;
};

void        WebAdminRegisterHooks(const WebAdminHooks& hooks);

// Pure / Testable Helpers (Path Sanitization, BIOS Audit, Allowed Targets)
std::string                WebAdminSanitizeFilename(const std::string& raw_filename);
bool                       WebAdminIsAllowedTargetSystem(const std::string& system_dir);
bool                       WebAdminIsAllowedUploadExtension(const std::string& filename);
bool                       WebAdminValidateMagicAndContent(const std::string& filename, const uint8_t* data, size_t len);
std::vector<BiosAuditItem> WebAdminRunBiosAudit(const std::string& bios_root_dir = "");
std::string                WebAdminBuildBiosAuditJson(const std::string& bios_root_dir = "");

#endif // WEB_ADMIN_H_INCLUDED


#include "web_admin.h"
#include "json_mini.h"
#include "netplay.h"
#include "core_runner.h"
#include "menu.h"
#include <iterator>
#include "cloud_sync.h"
#include "tray_manager.h"
#include "app_info.h"
#include "system_registry.h"
#include "retroachievements.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <map>

namespace fs = std::filesystem;

// TODO(security): Local LAN embedded server uses cryptographic 4-digit PIN pairing + HttpOnly session + CSRF token.
// OAuth/MFA/leaked-password checks and external Antivirus/CDR cloud sandboxes are intentionally omitted for offline LAN ROM management.

namespace
{
static bool OsRandomBytes(uint8_t* buf, size_t len)
{
#ifdef _WIN32
	return BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) return false;
	size_t off = 0;
	while (off < len)
	{
		ssize_t r = read(fd, buf + off, len - off);
		if (r <= 0) { close(fd); return false; }
		off += (size_t)r;
	}
	close(fd);
	return true;
#endif
}

static std::string RandomHexToken(size_t num_bytes)
{
	std::vector<uint8_t> bytes(num_bytes, 0);
	if (!OsRandomBytes(bytes.data(), bytes.size()))
		return "";
	static const char kHex[] = "0123456789abcdef";
	std::string out;
	out.reserve(num_bytes * 2);
	for (uint8_t b : bytes)
	{
		out.push_back(kHex[(b >> 4) & 0xF]);
		out.push_back(kHex[b & 0xF]);
	}
	return out;
}

static bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size() || a.empty()) return false;
	uint8_t diff = 0;
	for (size_t i = 0; i < a.size(); ++i)
		diff |= (uint8_t)(a[i] ^ b[i]);
	return diff == 0;
}
} // namespace

// =============================================================
// Input Validation, Path Sanitization & BIOS Auditor
// =============================================================

std::string WebAdminSanitizeFilename(const std::string& raw_filename)
{
	if (raw_filename.empty() || raw_filename.size() > 180)
		return "";

	// Reject any path separators, drive letters, or traversal sequences
	if (raw_filename.find('/') != std::string::npos ||
	    raw_filename.find('\\') != std::string::npos ||
	    raw_filename.find("..") != std::string::npos ||
	    raw_filename.find(':') != std::string::npos)
	{
		return "";
	}

	std::string clean;
	clean.reserve(raw_filename.size());
	for (unsigned char c : raw_filename)
	{
		if (c < 0x20 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == ';')
			return "";
		clean.push_back((char)c);
	}

	while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.'))
		clean.pop_back();

	if (clean.empty() || clean[0] == '.')
		return "";

	return clean;
}

bool WebAdminIsAllowedTargetSystem(const std::string& system_dir)
{
	static const char* const kAllowedSystems[] = {
		"bios", "bios/np2kai", "bios/neocd", "bios/dc", "bios/fmtowns",
		"SNES", "NES", "Genesis", "MegaCD", "32X", "MasterSystem", "GameGear",
		"Saturn", "Dreamcast", "GBA", "GB", "GBC", "N64", "NDS", "3DS", "GameCube",
		"PSX", "PS2", "PSP", "Arcade", "NeoGeo", "NeoGeoCD", "PCEngine", "PCFX",
		"Atari2600", "Atari5200", "Atari7800", "Lynx", "Jaguar", "ColecoVision",
		"WonderSwan", "NGP", "3DO", "MSX", "Amiga", "C64", "ZXSpectrum", "DOS",
		"FMTowns", "PC98",
		"PlayStation", "PlayStation2", "Nintendo64", "GameBoy", "TurboGrafx16",
		"Wii", "Vectrex", "VirtualBoy", "Ports", "ports"
	};

	for (const char* allowed : kAllowedSystems)
	{
		if (system_dir == allowed)
			return true;
	}
	return false;
}

bool WebAdminIsAllowedUploadExtension(const std::string& filename)
{
	std::string clean = WebAdminSanitizeFilename(filename);
	if (clean.empty()) return false;

	size_t dot = clean.rfind('.');
	if (dot == std::string::npos) return false;
	std::string ext = clean.substr(dot);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });

	static const char* const kAllowedExts[] = {
		".sfc", ".smc", ".nes", ".fds", ".md", ".gen", ".smd", ".bin", ".32x",
		".sms", ".gg", ".gba", ".gb", ".gbc", ".z64", ".n64", ".v64", ".nds",
		".3ds", ".cci", ".cxi", ".gcm", ".rvz", ".iso", ".chd", ".cue", ".pbp",
		".cso", ".zip", ".7z", ".rar", ".pce", ".rom", ".img", ".col", ".lnx",
		".j64", ".jag", ".ws", ".wsc", ".ngp", ".ngc", ".mx1", ".mx2", ".dsk",
		".adf", ".d64", ".prg", ".tap", ".tzx", ".z80", ".sna", ".hdi", ".fdi",
		".d88", ".mec", ".nvm", ".bmp", ".wav"
	};

	for (const char* a : kAllowedExts)
	{
		if (ext == a) return true;
	}
	return false;
}

bool WebAdminValidateMagicAndContent(const std::string& filename, const uint8_t* data, size_t len)
{
	if (!data || len == 0 || len > 64u * 1024u * 1024u)
		return false;
	if (!WebAdminIsAllowedUploadExtension(filename))
		return false;

	// Reject Git LFS pointer files masquerading as ROMs/BIOS (per BIOS_NECESSARIOS.txt)
	static const char kLfsHeader[] = "version https://git-lfs.github.com/spec/v1";
	if (len >= sizeof(kLfsHeader) - 1 &&
	    memcmp(data, kLfsHeader, sizeof(kLfsHeader) - 1) == 0)
	{
		return false;
	}

	// Reject HTML error pages accidentally saved as binary ROMs/BIOS
	if (len >= 14 && (memcmp(data, "<!DOCTYPE html", 14) == 0 || memcmp(data, "<html", 5) == 0))
	{
		return false;
	}

	// Enforce container magic bytes for compressed archives & CHD discs
	size_t dot = filename.rfind('.');
	if (dot != std::string::npos)
	{
		std::string ext = filename.substr(dot);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
		if (ext == ".zip" && (len < 4 || memcmp(data, "PK\x03\x04", 4) != 0))
			return false;
		if (ext == ".chd" && (len < 8 || memcmp(data, "MComprHD", 8) != 0))
			return false;
		if (ext == ".7z" && (len < 6 || memcmp(data, "7z\xBC\xAF\x27\x1C", 6) != 0))
			return false;
	}

	return true;
}

std::vector<BiosAuditItem> WebAdminRunBiosAudit(const std::string& bios_root_dir)
{
	std::error_code ec;
	fs::path root;
	if (!bios_root_dir.empty())
	{
		root = fs::path(bios_root_dir);
	}
	else if (fs::exists("bios", ec))
	{
		root = fs::path("bios");
	}
	else if (fs::exists("app/bios", ec))
	{
		root = fs::path("app/bios");
	}
	else
	{
		root = fs::path("bios");
	}

	// Canonical list from packaging/bios-guide/BIOS_NECESSARIOS.txt
	std::vector<BiosAuditItem> items = {
		{ "PlayStation",       "scph5501.bin",                      "1e68c231d0896b7eadcad1d7d8e76129", "",       true,  false, 0 },
		{ "PlayStation",       "scph5500.bin",                      "8dd7d5296a650fac7319bce665a6a53c", "",       false, false, 0 },
		{ "PlayStation",       "scph5502.bin",                      "32736f17079d0b2b7024407c39bd3050", "",       false, false, 0 },
		{ "PlayStation 2",     "SCPH-70012_BIOS_V12_USA_200.BIN",   "d333558cc14561c1fdc334c75d5f37b7", "",       true,  false, 0 },
		{ "Dreamcast / Naomi", "dc_boot.bin",                       "e10c53c2f8b90bab96ead2d368858623", "dc",     true,  false, 0 },
		{ "Sega Saturn",       "sega_101.bin",                      "85ec9ca47d8f6807718151cbcca8b964", "",       true,  false, 0 },
		{ "Mega CD / Sega CD", "bios_CD_U.bin",                     "2efd74e3232ff260e371b99f84024f7f", "",       true,  false, 0 },
		{ "PC Engine CD",      "syscard3.pce",                      "38179df8f4ac870017db21ebcbf53114", "",       true,  false, 0 },
		{ "3DO",               "panafz1.bin",                       "f47264dd47fe30f73ab3c010015c155b", "",       true,  false, 0 },
		{ "Atari 5200",        "5200.rom",                          "281f20ea4320404ec820fb7ec0693b38", "",       true,  false, 0 },
		{ "Atari Lynx",        "lynxboot.img",                      "fcd403db69f54290b51035d82f835e7b", "",       true,  false, 0 },
		{ "PC-FX",             "pcfx.rom",                          "08e36edbea28a017f79f8d4f7ff9b6d7", "",       true,  false, 0 },
		{ "ColecoVision",      "coleco.rom",                        "2c66f5911e5b42b8ebe113403548eee7", "",       true,  false, 0 },
		{ "MSX / MSX2",        "MSX.ROM",                           "364a1a579fe5cb8dba54519bcfcdac0d", "",       true,  false, 0 },
		{ "MSX / MSX2",        "MSX2.ROM",                          "ec3a01c91f24fbddcbcab0ad301bc9ef", "",       true,  false, 0 },
		{ "FM Towns",          "FMT_SYS.ROM",                       "86fb6f7280689259f0ca839dd3dd6cde", "",       true,  false, 0 },
		{ "NEC PC-98",         "bios.rom",                          "cd237e16e7e77c06bb58540e9e9fca68", "np2kai", true,  false, 0 },
		{ "NeoGeo CD",         "neocd_z.rom",                       "",                                 "neocd",  true,  false, 0 },
		{ "Nintendo DS",       "bios7.bin",                         "df692a80a5b1bc90728bc3dfc76cd948", "",       false, false, 0 },
		{ "Nintendo DS",       "bios9.bin",                         "a392174eb3e572fed6447e956bde4b25", "",       false, false, 0 },
		{ "Nintendo DS",       "firmware.bin",                      "",                                 "",       false, false, 0 },
		{ "Amiga (A500)",      "kick34005.A500",                    "",                                 "",       false, false, 0 },
		{ "Amiga (CD32)",      "kick40060.CD32",                    "",                                 "",       false, false, 0 },
		{ "NeoGeo (AES/MVS)",  "neogeo.zip",                        "",                                 "",       false, false, 0 },
		{ "Master System",     "bios_U.sms",                        "",                                 "",       false, false, 0 },
		{ "Atari 7800",        "7800 BIOS (U).rom",                 "0763f1ffb006ddbe32e52d497ee848ae", "",       false, false, 0 }
	};

	for (auto& item : items)
	{
		fs::path candidate = item.subdir.empty()
			? (root / item.filename)
			: (root / item.subdir / item.filename);

		if (!fs::exists(candidate, ec) && !item.subdir.empty())
		{
			// Also check flat root fallback (e.g. dc_boot.bin directly in bios/)
			fs::path flat = root / item.filename;
			if (fs::exists(flat, ec)) candidate = flat;
		}

		if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
		{
			uint64_t sz = (uint64_t)fs::file_size(candidate, ec);
			// Ignore 131-byte Git LFS pointer stubs
			if (!ec && sz > 256)
			{
				item.present = true;
				item.size_bytes = sz;
			}
		}
	}

	return items;
}

std::string WebAdminBuildBiosAuditJson(const std::string& bios_root_dir)
{
	std::vector<BiosAuditItem> items = WebAdminRunBiosAudit(bios_root_dir);
	std::ostringstream oss;
	oss << "{\"items\":[";
	for (size_t i = 0; i < items.size(); ++i)
	{
		const auto& it = items[i];
		oss << "{"
		    << "\"system\":" << JsonQuote(it.system_name) << ","
		    << "\"filename\":" << JsonQuote(it.filename) << ","
		    << "\"expected_md5\":" << JsonQuote(it.expected_md5) << ","
		    << "\"subdir\":" << JsonQuote(it.subdir) << ","
		    << "\"required\":" << (it.required ? "true" : "false") << ","
		    << "\"present\":" << (it.present ? "true" : "false") << ","
		    << "\"size_bytes\":" << (int64_t)it.size_bytes
		    << "}" << (i + 1 < items.size() ? "," : "");
	}
	oss << "]}";
	return oss.str();
}

// =============================================================
// Embedded HTTP Server & Session State
// =============================================================

struct WebSession
{
	std::string session_id;
	std::string csrf_token;
	int64_t     expires_utc = 0;
};

static std::mutex              g_web_mutex;
static std::atomic<bool>       g_web_running{ false };
static std::atomic<int>        g_web_port{ 8080 };
static std::thread             g_web_thread;
static std::string             g_web_pin = "0000";
static std::string             g_web_url = "http://127.0.0.1:8080";
static std::map<std::string, WebSession> g_sessions;
static int                     g_failed_login_count = 0;
static int64_t                 g_lockout_until_utc = 0;
static int                     g_lockout_rounds = 0;

#ifdef _WIN32
static SOCKET                  g_listen_sock = INVALID_SOCKET;
#else
static int                     g_listen_sock = -1;
#endif

void WebAdminRegeneratePin()
{
	uint8_t rnd[2] = { 0, 0 };
	OsRandomBytes(rnd, sizeof(rnd));
	uint16_t val = (uint16_t)(((uint16_t)rnd[0] << 8) | rnd[1]) % 9000u + 1000u;
	char buf[8];
	snprintf(buf, sizeof(buf), "%04u", (unsigned)val);

	std::lock_guard<std::mutex> lk(g_web_mutex);
	g_web_pin = buf;
	g_sessions.clear();
}

static WebAdminHooks g_hooks;

void WebAdminRegisterHooks(const WebAdminHooks& hooks)
{
	g_hooks = hooks;
}

const char* WebAdminGetPin()
{
	static char s_pin[8];
	std::lock_guard<std::mutex> lk(g_web_mutex);
	snprintf(s_pin, sizeof(s_pin), "%s", g_web_pin.c_str());
	return s_pin;
}

bool WebAdminIsRunning()
{
	return g_web_running.load();
}

int WebAdminGetPort()
{
	return g_web_port.load();
}

const char* WebAdminGetUrl()
{
	static char s_url[128];
	std::lock_guard<std::mutex> lk(g_web_mutex);
	snprintf(s_url, sizeof(s_url), "%s", g_web_url.c_str());
	return s_url;
}

const char* WebAdminGetLocalUrl()
{
	static char s_local[128];
	int port = g_web_port.load();
	if (port <= 0) port = 8080;
	snprintf(s_local, sizeof(s_local), "http://localhost:%d", port);
	return s_local;
}

// kSystemRegistry, ResolveSystemRomDir, and CountGamesInDirectory are unified in system_registry.h

static std::string FormatBytes(uint64_t bytes)
{
	char buf[32];
	if (bytes < 1024) snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
	else if (bytes < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
	else if (bytes < 1024ULL * 1024ULL * 1024ULL) snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
	else snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
	return buf;
}

static std::string WebAdminBuildSystemsJson()
{
	std::ostringstream ss;
	ss << "{\"systems\":[";
	bool first = true;
	for (size_t i = 0; i < kSystemRegistryCount; ++i)
	{
		const auto& s = kSystemRegistry[i];
		if (!first) ss << ",";
		first = false;
		fs::path dir = ResolveSystemRomDir(s.id);
		int cnt = CountGamesInDirectory(dir, s.action_id == 140);
		ss << "{\"id\":" << JsonQuote(s.id)
		   << ",\"name\":" << JsonQuote(s.name)
		   << ",\"count\":" << cnt << "}";
	}
	ss << "]}";
	return ss.str();
}

static std::string WebAdminBuildSystemsVisibilityJson()
{
	std::ostringstream ss;
	int total = (int)kSystemRegistryCount;
	int vis_count = 0;
	for (size_t i = 0; i < kSystemRegistryCount; ++i) {
		const auto& s = kSystemRegistry[i];
		bool vis = g_hooks.is_system_visible ? g_hooks.is_system_visible(s.action_id) : true;
		if (vis) vis_count++;
	}
	int hid_count = total - vis_count;

	ss << "{\"total\":" << total
	   << ",\"visible_count\":" << vis_count
	   << ",\"hidden_count\":" << hid_count
	   << ",\"systems\":[";
	for (size_t i = 0; i < kSystemRegistryCount; ++i)
	{
		const auto& s = kSystemRegistry[i];
		fs::path dir = ResolveSystemRomDir(s.id);
		int cnt = CountGamesInDirectory(dir, s.action_id == 140);
		bool vis = g_hooks.is_system_visible ? g_hooks.is_system_visible(s.action_id) : true;

		if (i > 0) ss << ",";
		ss << "{\"id\":" << s.action_id
		   << ",\"code\":" << JsonQuote(s.id)
		   << ",\"name\":" << JsonQuote(s.name)
		   << ",\"category\":" << JsonQuote(s.category)
		   << ",\"core\":" << JsonQuote(s.core_name)
		   << ",\"count\":" << cnt
		   << ",\"visible\":" << (vis ? "true" : "false")
		   << "}";
	}
	ss << "]}";
	return ss.str();
}

static std::string WebAdminBuildRomsJson(const std::string& sys_id)
{
	if (!WebAdminIsAllowedTargetSystem(sys_id))
		return "{\"error\":\"invalid_system\",\"roms\":[]}";

	fs::path dir = ResolveSystemRomDir(sys_id);
	std::error_code ec;

	struct RomInfo {
		std::string name;
		uint64_t size_bytes;
	};
	std::vector<RomInfo> list;

	bool is_ports = (sys_id == "Ports" || sys_id == "ports");
	if (fs::is_directory(dir, ec))
	{
		for (const auto& entry : fs::directory_iterator(dir, ec))
		{
			if (ec) break;
			if (IsValidRomEntry(entry, is_ports))
			{
				std::string fn = entry.path().filename().string();
				uint64_t sz = 0;
				if (entry.is_regular_file(ec)) sz = (uint64_t)entry.file_size(ec);
				list.push_back({ fn, sz });
			}
		}
	}

	std::sort(list.begin(), list.end(), [](const RomInfo& a, const RomInfo& b) {
		return a.name < b.name;
	});

	std::ostringstream ss;
	ss << "{\"system\":" << JsonQuote(sys_id) << ",\"roms\":[";
	bool first = true;
	for (const auto& r : list)
	{
		if (!first) ss << ",";
		first = false;
		ss << "{\"name\":" << JsonQuote(r.name)
		   << ",\"size_bytes\":" << r.size_bytes
		   << ",\"size_str\":" << JsonQuote(FormatBytes(r.size_bytes))
		   << ",\"path\":" << JsonQuote((dir / r.name).generic_string())
		   << "}";
	}
	ss << "]}";
	return ss.str();
}

static std::string WebAdminBuildStatusJson(const std::string& csrf_token = "")
{
	std::ostringstream ss;
	ss << "{";

#if defined(_WIN32)
	ss << "\"platform\":\"Windows 64-bit\"";
#elif defined(__APPLE__)
	ss << "\"platform\":\"macOS (Apple Silicon)\"";
#elif defined(__linux__)
	ss << "\"platform\":\"Linux / Karamelo OS\"";
#else
	ss << "\"platform\":\"Karamelo\"";
#endif

	ss << ",\"version\":" << JsonQuote(APP_VERSION);
	ss << ",\"arch\":" << JsonQuote(APP_ARCH);
	ss << ",\"systems_count\":" << APP_SYSTEM_COUNT;
	ss << ",\"core_engines\":" << APP_CORE_ENGINES;
	ss << ",\"ports_count\":94";

	bool running = (g_hooks.is_running ? g_hooks.is_running() : false);
	ss << ",\"game_running\":" << (running ? "true" : "false");
	ss << ",\"game_name\":" << JsonQuote((running && g_hooks.get_game_name) ? g_hooks.get_game_name() : "");
	ss << ",\"core_name\":" << JsonQuote((running && g_hooks.get_core_name) ? g_hooks.get_core_name() : "");
	ss << ",\"volume\":" << (g_hooks.get_volume ? g_hooks.get_volume() : 100);
	ss << ",\"is_muted\":" << ((g_hooks.get_mute && g_hooks.get_mute()) ? "true" : "false");

	// Cloud status
	bool cloud_on = CloudSyncIsConnected();
	ss << ",\"cloud_connected\":" << (cloud_on ? "true" : "false");
	ss << ",\"cloud_synced\":" << CloudSyncGetSyncedFileCount();
	ss << ",\"cloud_last_sync\":" << JsonQuote(CloudSyncGetLastSyncTimeStr());

	// Presence
	ss << ",\"presence\":" << JsonQuote(TrayManagerGetStatusName((TrayPresenceStatus)TrayManagerGetStatus()));

	// Recents
	ss << ",\"recents\":[";
	auto recents = g_hooks.get_recents ? g_hooks.get_recents() : std::vector<std::string>{};
	bool first_rec = true;
	for (size_t i = 0; i < recents.size() && i < 8; ++i)
	{
		if (!first_rec) ss << ",";
		first_rec = false;
		ss << JsonQuote(recents[i]);
	}
	ss << "]";

	if (!csrf_token.empty())
	{
		ss << ",\"csrf_token\":" << JsonQuote(csrf_token);
	}

	ss << "}";
	return ss.str();
}

static std::string BuildEmbeddedSpaHtml(const std::string& csp_nonce)
{
	std::ostringstream h;
	h << R"raw(<!DOCTYPE html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Karamelo Studio &bull; Central de Controle LAN</title>
<style nonce=")raw" << csp_nonce << R"raw(">
)raw";
	h << R"raw(:root {
  --bg-primary: #050b12;
  --bg-sidebar: rgba(8, 14, 23, 0.94);
  --bg-card: rgba(13, 19, 31, 0.82);
  --bg-card-hover: rgba(18, 26, 42, 0.94);
  --border-subtle: rgba(148, 163, 184, 0.12);
  --border-glow: rgba(245, 158, 11, 0.35);
  --gold-primary: #fbbf24;
  --gold-amber: #f59e0b;
  --gold-dark: #b45309;
  --emerald: #10b981;
  --emerald-dim: rgba(16, 185, 129, 0.15);
  --cyan: #38bdf8;
  --red: #ef4444;
  --red-dim: rgba(239, 68, 68, 0.15);
  --text-main: #edf5ff;
  --text-muted: #94a3b8;
  --text-dim: #64748b;
  --font-sans: 'Outfit', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
  --font-mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}
*, *:before, *:after { box-sizing: border-box; margin: 0; padding: 0; }
html, body {
  width: 100%;
  min-height: 100vh;
  background-color: var(--bg-primary);
  color: var(--text-main);
  font-family: var(--font-sans);
  font-size: 14px;
  line-height: 1.5;
  overflow-x: hidden;
  background-image:
    radial-gradient(circle at 18% 0%, rgba(245, 158, 11, 0.12), transparent 32%),
    radial-gradient(circle at 82% 14%, rgba(217, 119, 6, 0.08), transparent 30%),
    linear-gradient(rgba(148, 163, 184, 0.02) 1px, transparent 1px),
    linear-gradient(90deg, rgba(148, 163, 184, 0.02) 1px, transparent 1px);
  background-size: 100% 100%, 100% 100%, 32px 32px, 32px 32px;
  background-attachment: fixed;
}
::-webkit-scrollbar { width: 6px; height: 6px; }
::-webkit-scrollbar-track { background: rgba(5, 11, 18, 0.6); }
::-webkit-scrollbar-thumb { background: rgba(148, 163, 184, 0.22); border-radius: 4px; }
::-webkit-scrollbar-thumb:hover { background: var(--gold-primary); }

/* App Shell Layout (Vertical Sidebar on Desktop, Zero Horizontal Scrollbars) */
.app-layout {
  display: flex;
  min-height: 100vh;
  width: 100%;
  overflow-x: hidden;
}
.sidebar {
  width: 290px;
  flex-shrink: 0;
  background: var(--bg-sidebar);
  border-right: 1px solid var(--border-subtle);
)raw";
	h << R"raw(  backdrop-filter: blur(20px);
  display: flex;
  flex-direction: column;
  position: sticky;
  top: 0;
  height: 100vh;
  overflow-y: auto;
  z-index: 100;
  padding: 20px 16px;
}
.main-content {
  flex: 1;
  min-width: 0;
  padding: 28px 36px;
  max-width: 1380px;
  margin: 0 auto;
  overflow-x: hidden;
}

/* Sidebar Brand Header */
.sidebar-brand {
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 18px;
  border-bottom: 1px solid var(--border-subtle);
  margin-bottom: 16px;
}
.brand-mascot {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.25) 0%, rgba(217, 119, 6, 0.1) 100%);
  border: 1px solid rgba(245, 158, 11, 0.4);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 20px;
  filter: drop-shadow(0 2px 8px rgba(245, 158, 11, 0.4));
  flex-shrink: 0;
}
.brand-text {
  display: flex;
  flex-direction: column;
}
.brand-title {
  font-size: 17px;
  font-weight: 800;
  letter-spacing: -0.3px;
  color: #fff;
  display: flex;
  align-items: center;
  gap: 5px;
}
.brand-title span {
  color: var(--gold-primary);
}
.brand-sub {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 3px;
  flex-wrap: wrap;
}

/* Badges */
.badge {
  background: rgba(148, 163, 184, 0.08);
  border: 1px solid var(--border-subtle);
  color: var(--text-muted);
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 700;
  padding: 2px 7px;
  border-radius: 6px;
}
.badge-gold {
  background: rgba(245, 158, 11, 0.12);
  border-color: rgba(245, 158, 11, 0.35);
  color: var(--gold-primary);
}
.badge-live {
  background: var(--emerald-dim);
  border-color: rgba(16, 185, 129, 0.35);
  color: var(--emerald);
  display: inline-flex;
  align-items: center;
  gap: 5px;
}
.live-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--emerald);
  display: inline-block;
}
.live-dot.pulse {
  box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7);
)raw";
	h << R"raw(  animation: pulseDot 2s infinite;
}
@keyframes pulseDot {
  0% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7); }
  70% { transform: scale(1); box-shadow: 0 0 0 6px rgba(16, 185, 129, 0); }
  100% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
}

/* Sidebar Live Game Box */
.sidebar-live-game {
  background: rgba(13, 19, 31, 0.7);
  border: 1px solid var(--border-subtle);
  border-radius: 12px;
  padding: 12px 14px;
  margin-bottom: 16px;
}
.live-game-caption {
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-dim);
  margin-bottom: 4px;
  display: flex;
  align-items: center;
  gap: 6px;
}
.side-game-title {
  font-size: 13.5px;
  font-weight: 700;
  color: var(--gold-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.side-game-sub {
  font-size: 11.5px;
  color: var(--text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-top: 1px;
}

/* Sidebar Vertical Nav */
.sidebar-nav {
  display: flex;
  flex-direction: column;
  gap: 5px;
  flex: 1;
}
.tab-btn {
  display: flex;
  align-items: center;
  gap: 12px;
  width: 100%;
  padding: 10px 14px;
  border-radius: 10px;
  background: transparent;
  border: 1px solid transparent;
  color: var(--text-muted);
  font-size: 13.5px;
  font-weight: 600;
  cursor: pointer;
  text-align: left;
  transition: all 0.18s ease;
  font-family: inherit;
}
.tab-btn:hover {
  background: rgba(148, 163, 184, 0.08);
  color: var(--text-main);
  border-color: rgba(148, 163, 184, 0.15);
  transform: translateX(2px);
}
.tab-btn.active {
  background: linear-gradient(90deg, rgba(245, 158, 11, 0.16) 0%, rgba(245, 158, 11, 0.04) 100%);
  border-color: rgba(245, 158, 11, 0.4);
  color: var(--gold-primary);
  font-weight: 700;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.2);
}
.tab-icon {
  font-size: 16px;
  width: 22px;
  display: flex;
  justify-content: center;
)raw";
	h << R"raw(  flex-shrink: 0;
}
.tab-title {
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

/* Sidebar Footer (PIN Auth & Credits) */
.sidebar-footer {
  padding-top: 16px;
  border-top: 1px solid var(--border-subtle);
  margin-top: auto;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.auth-bar {
  display: flex;
  gap: 6px;
  align-items: center;
}
.pin-field {
  width: 80px;
  text-align: center;
  font-family: var(--font-mono);
  letter-spacing: 2px;
  font-weight: 700;
}
.footer-meta {
  font-size: 11px;
  color: var(--text-dim);
  text-align: center;
}

/* Topbar in Main Content */
.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;
  flex-wrap: wrap;
  gap: 12px;
}
.page-title {
  font-size: 22px;
  font-weight: 800;
  letter-spacing: -0.5px;
  color: #fff;
}
.page-subtitle {
  font-size: 13px;
  color: var(--text-muted);
}
.status-pill {
  background: rgba(148, 163, 184, 0.08);
  border: 1px solid var(--border-subtle);
  padding: 6px 14px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: 600;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: var(--gold-primary);
}

/* Cards (Glassmorphism Inspired by karamelo-emu.com) */
.card {
  background: var(--bg-card);
  border: 1px solid var(--border-subtle);
  border-radius: 16px;
  padding: 22px;
  margin-bottom: 20px;
  backdrop-filter: blur(16px);
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
  transition: border-color 0.2s, box-shadow 0.2s;
}
.card:hover {
  border-color: rgba(245, 158, 11, 0.3);
  box-shadow: 0 12px 36px rgba(0, 0, 0, 0.45);
}
.card-title {
  color: #fff;
  font-size: 16px;
  font-weight: 700;
  letter-spacing: -0.3px;
  margin-bottom: 14px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
}
.card-desc {
  font-size: 13px;
  color: var(--text-muted);
  margin-bottom: 16px;
  line-height: 1.5;
}

/* Form Controls */
)raw";
	h << R"raw(input, select, button {
  font-family: inherit;
  font-size: 13.5px;
  border-radius: 10px;
}
input, select {
  background: rgba(8, 14, 23, 0.88);
  color: var(--text-main);
  border: 1px solid rgba(148, 163, 184, 0.18);
  padding: 9px 13px;
  outline: none;
  transition: all 0.15s ease;
}
input:focus, select:focus {
  border-color: var(--gold-primary);
  box-shadow: 0 0 0 3px rgba(245, 158, 11, 0.18);
}
button {
  cursor: pointer;
  font-weight: 700;
  padding: 9px 16px;
  border: none;
  transition: all 0.18s ease;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
}
.btn-primary {
  background: linear-gradient(135deg, #fbbf24 0%, #f59e0b 100%);
  color: #050b12;
  box-shadow: 0 4px 14px rgba(245, 158, 11, 0.25);
}
.btn-primary:hover {
  background: linear-gradient(135deg, #fde68a 0%, #fbbf24 100%);
  transform: translateY(-1px);
  box-shadow: 0 6px 18px rgba(245, 158, 11, 0.35);
}
.btn-secondary {
  background: rgba(148, 163, 184, 0.08);
  border: 1px solid rgba(148, 163, 184, 0.16);
  color: var(--text-main);
}
.btn-secondary:hover {
  background: rgba(245, 158, 11, 0.12);
  border-color: rgba(245, 158, 11, 0.4);
  color: var(--gold-primary);
  transform: translateY(-1px);
}
.btn-danger {
  background: var(--red-dim);
  border: 1px solid rgba(239, 68, 68, 0.35);
  color: #fca5a5;
}
.btn-danger:hover {
  background: var(--red);
  color: #fff;
  transform: translateY(-1px);
}
.btn-sm {
  padding: 5px 10px;
  font-size: 12px;
  border-radius: 8px;
}
.btn-action {
  background: rgba(148, 163, 184, 0.08);
  border: 1px solid rgba(148, 163, 184, 0.16);
  color: var(--text-main);
  padding: 7px 13px;
  border-radius: 8px;
  font-size: 12.5px;
}
.btn-action:hover {
  background: rgba(245, 158, 11, 0.15);
  border-color: rgba(245, 158, 11, 0.45);
  color: var(--gold-primary);
}
.btn-action.active {
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.3) 0%, rgba(217, 119, 6, 0.15) 100%);
  border-color: var(--gold-primary);
  color: var(--gold-primary);
  box-shadow: 0 0 12px rgba(245, 158, 11, 0.25);
  font-weight: 700;
}

/* Tabs & Content */
.tab-content { display: none; }
.tab-content.active { display: block; }

/* Controller Remote Touch */
.controller-box {
  display: flex;
)raw";
	h << R"raw(  gap: 32px;
  justify-content: center;
  align-items: center;
  flex-wrap: wrap;
  padding: 18px 0;
}
.dpad-grid {
  display: grid;
  grid-template-columns: repeat(3, 56px);
  grid-template-rows: repeat(3, 56px);
  gap: 6px;
  padding: 10px;
  background: rgba(8, 14, 23, 0.7);
  border: 1px solid var(--border-subtle);
  border-radius: 20px;
}
.dpad-btn {
  width: 56px;
  height: 56px;
  font-size: 18px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(18, 26, 42, 0.95);
  border: 1px solid rgba(148, 163, 184, 0.15);
  color: var(--gold-primary);
  border-radius: 12px;
  user-select: none;
  cursor: pointer;
  transition: all 0.12s ease;
  box-shadow: 0 4px 10px rgba(0, 0, 0, 0.25);
}
.dpad-btn:hover {
  background: rgba(245, 158, 11, 0.2);
  border-color: rgba(245, 158, 11, 0.5);
  transform: scale(1.03);
}
.dpad-btn:active {
  background: var(--gold-primary);
  color: #050b12;
  transform: scale(0.96);
}
.action-grid {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.action-grid button {
  padding: 13px 22px;
  font-size: 13.5px;
  border-radius: 12px;
  font-family: var(--font-mono);
  letter-spacing: 0.5px;
}

/* Systems & ROMs */
.sys-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(145px, 1fr));
  gap: 8px;
  margin-bottom: 16px;
  max-height: 240px;
  overflow-y: auto;
  padding: 8px;
  background: rgba(8, 14, 23, 0.6);
  border: 1px solid var(--border-subtle);
  border-radius: 12px;
}
.sys-pill {
  background: rgba(18, 26, 40, 0.75);
  border: 1px solid var(--border-subtle);
  padding: 8px 12px;
  border-radius: 8px;
  cursor: pointer;
  font-size: 12.5px;
  font-weight: 600;
  display: flex;
  justify-content: space-between;
  align-items: center;
  user-select: none;
  transition: all 0.15s ease;
}
.sys-pill:hover {
  border-color: rgba(245, 158, 11, 0.35);
  background: rgba(245, 158, 11, 0.08);
}
.sys-pill.active {
  border-color: var(--gold-primary);
)raw";
	h << R"raw(  background: linear-gradient(135deg, rgba(245, 158, 11, 0.22) 0%, rgba(217, 119, 6, 0.08) 100%);
  color: var(--gold-primary);
  font-weight: 700;
}
.drop-zone {
  border: 2px dashed rgba(245, 158, 11, 0.38);
  border-radius: 14px;
  padding: 26px;
  text-align: center;
  color: var(--text-muted);
  background: rgba(245, 158, 11, 0.02);
  cursor: pointer;
  transition: all 0.2s ease;
  margin-bottom: 16px;
}
.drop-zone:hover, .drop-zone.dragover {
  border-color: var(--emerald);
  background: rgba(16, 185, 129, 0.06);
  color: var(--emerald);
  transform: translateY(-1px);
}
.table-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 14px;
  border-bottom: 1px solid rgba(148, 163, 184, 0.08);
  font-size: 13.5px;
  gap: 12px;
  border-radius: 8px;
  transition: background 0.12s;
}
.table-row:hover {
  background: rgba(148, 163, 184, 0.04);
}
.badge-ok {
  color: #34d399;
  background: var(--emerald-dim);
  border: 1px solid rgba(16, 185, 129, 0.3);
  padding: 3px 8px;
  border-radius: 6px;
  font-size: 11px;
  font-weight: 700;
}
.badge-missing {
  color: #f87171;
  background: var(--red-dim);
  border: 1px solid rgba(239, 68, 68, 0.3);
  padding: 3px 8px;
  border-radius: 6px;
  font-size: 11px;
  font-weight: 700;
}
.progress-bar {
  width: 100%;
  height: 8px;
  background: rgba(8, 14, 23, 0.8);
  border-radius: 4px;
  overflow: hidden;
  margin-top: 10px;
}
.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #10b981, #34d399);
  width: 0%;
  transition: width 0.2s;
}

/* System Visibility Cards */
.sys-card {
  background: rgba(18, 26, 40, 0.7);
  border: 1px solid var(--border-subtle);
  border-radius: 12px;
  padding: 14px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  transition: all 0.2s;
}
.sys-card:hover {
  border-color: rgba(245, 158, 11, 0.35);
  background: rgba(18, 26, 40, 0.9);
}
.sys-card.hidden {
  opacity: 0.42;
  background: rgba(8, 14, 23, 0.5);
)raw";
	h << R"raw(  border-style: dashed;
}
.sys-card-info {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.sys-card-name {
  color: #fff;
  font-weight: 700;
  font-size: 14px;
}
.sys-card-core {
  color: var(--text-muted);
  font-size: 11.5px;
}
.sys-card-badges {
  display: flex;
  gap: 6px;
  align-items: center;
  margin-top: 4px;
}
.sys-badge-cat {
  background: rgba(245, 158, 11, 0.12);
  color: var(--gold-primary);
  border: 1px solid rgba(245, 158, 11, 0.25);
  padding: 2px 7px;
  border-radius: 5px;
  font-size: 10px;
  font-weight: 700;
}
.sys-badge-count {
  background: var(--emerald-dim);
  color: #34d399;
  border: 1px solid rgba(16, 185, 129, 0.25);
  padding: 2px 7px;
  border-radius: 5px;
  font-size: 10px;
  font-weight: 600;
}
.sys-badge-empty {
  background: rgba(148, 163, 184, 0.08);
  color: var(--text-dim);
  border: 1px solid var(--border-subtle);
  padding: 2px 7px;
  border-radius: 5px;
  font-size: 10px;
}
.filter-pill {
  background: rgba(18, 26, 40, 0.7);
  border: 1px solid var(--border-subtle);
  color: var(--text-muted);
  padding: 6px 13px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.18s;
}
.filter-pill:hover {
  border-color: rgba(245, 158, 11, 0.4);
  color: var(--gold-primary);
}
.filter-pill.active {
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.25) 0%, rgba(217, 119, 6, 0.1) 100%);
  border-color: var(--gold-primary);
  color: var(--gold-primary);
  font-weight: 700;
}

/* Switch Slider */
.switch {
  position: relative;
  display: inline-block;
  width: 46px;
  height: 24px;
  flex-shrink: 0;
}
.switch input { opacity: 0; width: 0; height: 0; }
.slider {
  position: absolute;
  cursor: pointer;
  top: 0; left: 0; right: 0; bottom: 0;
  background-color: rgba(148, 163, 184, 0.16);
  transition: .25s cubic-bezier(0.4, 0, 0.2, 1);
  border-radius: 24px;
  border: 1px solid var(--border-subtle);
}
.slider:before {
  position: absolute;
  content: "";
  height: 18px;
)raw";
	h << R"raw(  width: 18px;
  left: 2px;
  bottom: 2px;
  background-color: #94a3b8;
  transition: .25s cubic-bezier(0.4, 0, 0.2, 1);
  border-radius: 50%;
}
input:checked + .slider {
  background: linear-gradient(135deg, #fbbf24 0%, #f59e0b 100%);
  border-color: var(--gold-primary);
}
input:checked + .slider:before {
  transform: translateX(22px);
  background-color: #050b12;
}

/* Ecosystem Grid */
.eco-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 14px;
  margin-top: 12px;
}
.eco-card {
  background: rgba(18, 26, 40, 0.65);
  border: 1px solid var(--border-subtle);
  border-radius: 14px;
  padding: 16px;
  transition: all 0.2s ease;
}
.eco-card:hover {
  border-color: rgba(245, 158, 11, 0.35);
  transform: translateY(-2px);
}
.eco-card h4 {
  color: #fff;
  margin-bottom: 8px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 14.5px;
  font-weight: 700;
}
.eco-card p {
  font-size: 12.5px;
  color: var(--text-muted);
  line-height: 1.5;
}

/* RetroAchievements Trophy Items */
.ra-ach-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 14px;
  background: rgba(8, 14, 23, 0.55);
  border: 1px solid var(--border-subtle);
  border-radius: 8px;
  margin-bottom: 8px;
  transition: all 0.2s;
}
.ra-ach-item:hover {
  background: rgba(18, 26, 40, 0.85);
  border-color: rgba(245, 158, 11, 0.3);
}
.ra-ach-item.unlocked {
  border-color: rgba(245, 158, 11, 0.35);
  background: rgba(245, 158, 11, 0.05);
}
.ra-ach-left {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
  flex: 1;
}
.ra-ach-icon {
  font-size: 22px;
  line-height: 1;
  flex-shrink: 0;
}
.ra-ach-item:not(.unlocked) .ra-ach-icon {
  filter: grayscale(1) opacity(0.35);
}
.ra-ach-texts {
  min-width: 0;
}
.ra-ach-title {
  font-weight: 600;
  font-size: 13.5px;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.ra-ach-item.unlocked .ra-ach-title {
  color: var(--gold-primary);
}
.ra-ach-prog {
  font-size: 11.5px;
  color: var(--text-muted);
}
.ra-ach-right {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}
.badge-dim {
  background: rgba(255, 255, 255, 0.06);
  color: var(--text-muted);
  border: 1px solid var(--border-subtle);
}

/* Mobile & Tablet Responsive (< 900px) */
@media (max-width: 900px) {
  .app-layout {
    flex-direction: column;
  }
  .sidebar {
    width: 100%;
    height: auto;
    position: static;
    border-right: none;
    border-bottom: 1px solid var(--border-subtle);
    padding: 14px 16px;
  }
  .sidebar-nav {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    gap: 8px;
  }
  .tab-btn {
    flex: 1 1 calc(50% - 8px);
    min-width: 130px;
    padding: 8px 10px;
    font-size: 12.5px;
    justify-content: center;
  }
  .main-content {
    padding: 18px 14px;
    width: 100%;
  }
}
</style></head><body>

<div class="app-layout">
  <!-- Sidebar Navigation (Zero Horizontal Overflow) -->
  <aside class="sidebar">
    <div class="sidebar-brand">
      <div class="brand-mascot">🕹️</div>
      <div class="brand-text">
        <div class="brand-title">KARAMELO <span>STUDIO</span></div>
        <div class="brand-sub">
)raw";
	h << R"raw(          <span class="badge badge-gold" id="lbl-platform">LAN</span>
          <span class="badge" id="lbl-version">v0.98</span>
          <span class="badge badge-live"><span class="live-dot pulse"></span> ONLINE</span>
        </div>
      </div>
    </div>

    <!-- Live Console Quick Status -->
    <div class="sidebar-live-game">
      <div class="live-game-caption"><span class="live-dot pulse"></span> Console Ativo</div>
      <div id="side-game-name" class="side-game-title">Nenhum jogo ativo</div>
      <div id="side-game-core" class="side-game-sub">Menu Principal OSD</div>
    </div>

    <!-- Vertical Navigation (Stacked, Never Overflowing) -->
    <nav class="sidebar-nav">
      <button class="tab-btn active" data-tab="tab-dash">
        <span class="tab-icon">📊</span>
        <span class="tab-title">Painel &amp; Controle</span>
      </button>
      <button class="tab-btn" data-tab="tab-ra">
        <span class="tab-icon">🏆</span>
        <span class="tab-title">RetroAchievements</span>
      </button>
      <button class="tab-btn" data-tab="tab-video">
        <span class="tab-icon">📺</span>
        <span class="tab-title">Vídeo &amp; Ajustes</span>
      </button>
      <button class="tab-btn" data-tab="tab-systems">
        <span class="tab-icon">🎛️</span>
        <span class="tab-title">Gerenciar Sistemas</span>
      </button>
      <button class="tab-btn" data-tab="tab-roms">
        <span class="tab-icon">🎮</span>
        <span class="tab-title">ROMs &amp; Jogos (FTP)</span>
      </button>
      <button class="tab-btn" data-tab="tab-bios">
        <span class="tab-icon">🛡️</span>
        <span class="tab-title">Auditor de BIOS</span>
      </button>
      <button class="tab-btn" data-tab="tab-cloud">
        <span class="tab-icon">☁️</span>
        <span class="tab-title">Nuvem &amp; Saves</span>
      </button>
      <button class="tab-btn" data-tab="tab-info">
        <span class="tab-icon">🚀</span>
        <span class="tab-title">Ecossistema Karamelo</span>
      </button>
    </nav>

    <!-- Sidebar Footer -->
    <div class="sidebar-footer">
)raw";
	h << R"raw(      <div id="auth-header" class="auth-bar">
        <input type="password" id="pin-input" maxlength="4" placeholder="PIN" class="pin-field">
        <button id="btn-login" class="btn-primary btn-sm">Conectar</button>
        <button id="btn-logout" style="display:none;" class="btn-secondary btn-sm">Sair</button>
      </div>
      <div class="footer-meta">
        Clean-Room C++20 &bull; Licença GPL-3.0
      </div>
    </div>
  </aside>

  <!-- Main View Area -->
  <main class="main-content">
    <header class="topbar">
      <div>
        <h1 class="page-title" id="page-current-title">Painel &amp; Controle Remoto</h1>
        <span class="page-subtitle">Gerenciamento via Wi-Fi / Rede Local</span>
      </div>
      <span class="status-pill" id="lbl-game-status">Verificando...</span>
    </header>

    <!-- Auth Locked Notice -->
    <div id="auth-notice" class="card" style="text-align:center;padding:44px 20px;">
      <div style="font-size:36px;margin-bottom:12px;">🔒</div>
      <h3 style="color:var(--gold-primary);font-size:18px;margin-bottom:8px;">Autenticação Requerida</h3>
      <p style="color:var(--text-muted);max-width:480px;margin:0 auto 12px auto;">
        Digite o <strong>PIN de 4 dígitos</strong> exibido na sua TV ou monitor para liberar o acesso total ao painel.
      </p>
      <p style="color:var(--text-dim);font-size:12px;">
        Localize no Karamelo: <b>Configurações &gt; Web Admin Wi-Fi</b>
      </p>
    </div>

    <!-- App Container -->
    <div id="app-container" style="display:none;">
      <!-- TAB 1: Painel & Controle -->
      <div id="tab-dash" class="tab-content active">
        <div class="card">
          <div class="card-title">
            <span><span class="live-dot pulse" style="margin-right:6px;"></span>Jogo em Execução</span>
            <span class="badge badge-gold">DirectX 11 / WASAPI</span>
          </div>
          <div id="live-game-box" style="margin-bottom:16px;">
)raw";
	h << R"raw(            <h2 id="live-game-name" style="color:var(--gold-primary);margin-bottom:4px;font-size:20px;font-weight:800;">Nenhum jogo ativo</h2>
            <p id="live-game-core" style="color:var(--text-muted);font-size:13.5px;">Menu Principal OSD</p>
          </div>
          <div style="display:flex;gap:10px;flex-wrap:wrap;align-items:center;">
            <button class="btn-secondary" id="btn-act-pause">⏸ Pausar / Retomar</button>
            <button class="btn-secondary" id="btn-act-save">💾 Salvar F2</button>
            <button class="btn-secondary" id="btn-act-load">📂 Carregar F4</button>
            <button class="btn-secondary" id="btn-act-shot">📸 Screenshot</button>
            <button class="btn-danger" id="btn-act-reset">🔄 Reset</button>
          </div>
          <div style="margin-top:18px;padding-top:14px;border-top:1px solid var(--border-subtle);display:flex;align-items:center;gap:14px;flex-wrap:wrap;">
            <span style="color:var(--gold-primary);font-weight:700;">🔊 Volume Master:</span>
            <input type="range" id="vol-slider" min="0" max="100" value="100" style="flex:1;min-width:140px;cursor:pointer;">
            <span id="vol-val" style="width:48px;text-align:right;color:var(--gold-primary);font-weight:700;font-family:var(--font-mono);">100%</span>
            <button class="btn-secondary btn-sm" id="btn-act-mute">🔇 Mudo</button>
          </div>
        </div>

        <div class="card">
          <div class="card-title"><span>🎮 Controle Remoto Touch (Navegue na TV pelo Celular)</span></div>
          <div class="controller-box">
            <div class="dpad-grid">
              <div></div><button class="dpad-btn" data-key="up">▲</button><div></div>
              <button class="dpad-btn" data-key="left">◀</button>
              <button class="dpad-btn" data-key="enter" style="font-size:12px;font-weight:800;">OK</button>
              <button class="dpad-btn" data-key="right">▶</button>
)raw";
	h << R"raw(              <div></div><button class="dpad-btn" data-key="down">▼</button><div></div>
            </div>
            <div class="action-grid">
              <button class="btn-secondary" data-key="enter">[ A / SELECIONAR ]</button>
              <button class="btn-secondary" data-key="back">[ B / VOLTAR ]</button>
              <button class="btn-secondary" data-key="menu">[ MENU OSD (F1) ]</button>
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-title"><span>🕒 Jogos Recentes (Toque para Iniciar no PC)</span></div>
          <div id="recents-list"></div>
        </div>
      </div>

      <!-- TAB 2: RetroAchievements -->
      <div id="tab-ra" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>🏆 RetroAchievements (Conta &amp; Autenticação)</span>
            <span id="lbl-ra-badge" class="badge badge-gold">Verificando...</span>
          </div>
          <p class="card-desc">
            Conecte sua conta do <strong>RetroAchievements.org</strong> para desbloquear troféus, desafios e pontuações em tempo real nos seus jogos clássicos.
          </p>

          <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px;margin-bottom:14px;">
            <div>
              <label style="font-size:12.5px;color:var(--text-muted);display:block;margin-bottom:5px;">Nome de Usuário (Username):</label>
              <input type="text" id="ra-username" placeholder="Ex: GuhClemente" style="width:100%;">
            </div>
            <div>
              <label style="font-size:12.5px;color:var(--text-muted);display:block;margin-bottom:5px;">Senha ou Token / Web API Key:</label>
              <div style="display:flex;gap:6px;">
                <input type="password" id="ra-password" placeholder="Senha do site ou token..." style="flex:1;">
)raw";
	h << R"raw(                <button type="button" class="btn-secondary btn-sm" id="btn-ra-toggle-pass" title="Mostrar/Ocultar Senha ou Token">👁️</button>
              </div>
            </div>
          </div>

          <div style="display:flex;flex-wrap:wrap;gap:20px;align-items:center;margin-bottom:18px;">
            <label style="display:flex;align-items:center;gap:8px;font-size:13px;cursor:pointer;">
              <input type="checkbox" id="ra-is-token"> Este valor é um Token / Web API Key
            </label>
            <label style="display:flex;align-items:center;gap:8px;font-size:13px;cursor:pointer;">
              <input type="checkbox" id="ra-hardcore"> <strong>Modo Hardcore</strong> (Bloqueia savestates para pontuação oficial)
            </label>
          </div>

          <div style="display:flex;gap:10px;flex-wrap:wrap;align-items:center;">
            <button id="btn-ra-save" class="btn-primary">💾 Salvar Credenciais &amp; Fazer Login</button>
            <button id="btn-ra-logout" class="btn-danger btn-sm">🚪 Desconectar Conta</button>
            <span id="ra-save-status" style="font-size:12.5px;color:var(--emerald);"></span>
          </div>

          <div id="ra-live-box" style="margin-top:16px;background:rgba(8,14,23,0.7);border:1px solid var(--border-subtle);border-radius:10px;padding:14px;display:none;">
            <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;">
              <div>
                <strong style="color:var(--gold-primary);" id="ra-live-user">Jogador</strong>
                <span id="ra-live-mode" style="margin-left:8px;font-size:11.5px;color:var(--text-muted);"></span>
              </div>
              <span class="badge badge-live" id="ra-live-score">0 Pontos</span>
            </div>
            <div style="font-size:12.5px;color:var(--text-muted);" id="ra-live-summary">Aguardando jogo ativo.</div>
          </div>
        </div>

        <div class="card">
          <div class="card-title">
            <span>🎮 Troféus &amp; Conquistas do Jogo em Execução</span>
            <span id="ra-achievements-summary" class="badge badge-gold">0 / 0 Conquistas</span>
          </div>
          <p id="ra-active-game-title" class="card-desc" style="font-weight:600;color:var(--text-primary);margin-bottom:10px;">
            Nenhum jogo ativo no momento
          </p>
          <div style="background:rgba(255,255,255,0.06);border-radius:6px;height:8px;overflow:hidden;margin-bottom:16px;">
            <div id="ra-achievements-bar" style="height:100%;background:linear-gradient(90deg,var(--gold-primary),#10b981);width:0%;transition:width 0.4s ease;"></div>
          </div>
          <div id="ra-achievements-list">
            <div style="text-align:center;padding:24px;color:var(--text-muted);font-size:13px;">
              Inicie um jogo suportado no Karamelo para acompanhar os troféus aqui.
            </div>
          </div>
        </div>
      </div>

      <!-- TAB: Vídeo & Emulação -->
      <div id="tab-video" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>📺 Vídeo &amp; Shaders CRT (Estilo RetroArch)</span>
)raw";
	h << R"raw(            <button id="btn-apply-video" class="btn-primary btn-sm">💾 Aplicar Vídeo</button>
          </div>
          <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px;">
            <div>
              <label style="font-size:12.5px;color:var(--text-muted);display:block;margin-bottom:5px;">Proporção de Aspecto (Aspect Ratio):</label>
              <select id="cfg-aspect" style="width:100%;">
                <option value="0">Original (1:1 Pixel Aspect Ratio)</option>
                <option value="1">4:3 Clássico (TV CRT Tradicional)</option>
                <option value="2">16:9 Widescreen (Preencher Tela)</option>
              </select>
            </div>
            <div>
              <label style="font-size:12.5px;color:var(--text-muted);display:block;margin-bottom:5px;">Filtro / Shader CRT:</label>
              <select id="cfg-filter" style="width:100%;">
                <option value="0">Desligado (Raw Pixel Nítido)</option>
                <option value="8">Scanlines Leve (Arcade Clean)</option>
                <option value="9">CRT Suave (TV Tubo)</option>
                <option value="4">PVM Pro 600TVL (Monitor Sony PVM)</option>
                <option value="1">Sony Trinitron (Aperture Grille)</option>
                <option value="2">Arcade Shadow (Filtro Gabinete)</option>
                <option value="5">Scanlines 50% (Linhas Fortes)</option>
                <option value="6">NTSC Composto (Cores Vintage)</option>
                <option value="7">Matriz LCD (Portáteis GB/GBA/PSP)</option>
                <option value="3">Tubo Curvado 3D (Curvatura Real)</option>
              </select>
            </div>
            <div>
              <label style="font-size:12.5px;color:var(--text-muted);display:block;margin-bottom:5px;">Sincronização Vertical (V-Sync):</label>
              <select id="cfg-vsync" style="width:100%;">
)raw";
	h << R"raw(                <option value="1">Ativado (60 FPS Cimentados, Sem Rasgos)</option>
                <option value="0">Desativado (Menor Latência Possível)</option>
              </select>
            </div>
          </div>
          <div style="margin-top:12px;font-size:12px;color:var(--text-dim);">
            * Os shaders rodam no próprio Karamelo, na placa de vídeo deste computador.
          </div>
        </div>

        <div class="card">
          <div class="card-title">
            <span>💾 Gerenciador de Savestates &amp; Quick Menu</span>
            <span id="lbl-slot-info" class="badge badge-gold">Slot 0</span>
          </div>
          <div style="display:flex;flex-wrap:wrap;gap:12px;align-items:center;margin-bottom:14px;">
            <label style="font-size:13px;color:var(--text-muted);">Slot de Estado:</label>
            <select id="cfg-slot" style="min-width:130px;">
              <option value="0">Slot 0 (Padrão)</option>
              <option value="1">Slot 1</option>
              <option value="2">Slot 2</option>
              <option value="3">Slot 3</option>
              <option value="4">Slot 4</option>
              <option value="5">Slot 5</option>
              <option value="6">Slot 6</option>
              <option value="7">Slot 7</option>
              <option value="8">Slot 8</option>
              <option value="9">Slot 9</option>
            </select>
            <button id="btn-slot-save" class="btn-primary btn-sm">💾 Salvar no Slot</button>
            <button id="btn-slot-load" class="btn-secondary btn-sm">📂 Carregar do Slot</button>
          </div>
          <div style="display:flex;flex-wrap:wrap;gap:8px;">
            <button id="btn-quick-pause" class="btn-secondary btn-sm">⏸ Pausar / Retomar</button>
            <button id="btn-quick-reset" class="btn-secondary btn-sm">🔄 Reiniciar Jogo (Reset)</button>
)raw";
	h << R"raw(            <button id="btn-quick-screenshot" class="btn-secondary btn-sm">📸 Capturar Tela</button>
          </div>
        </div>
      </div>

      <!-- TAB 3: Gerenciar Sistemas -->
      <div id="tab-systems" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>🎛️ Gerenciador de Sistemas &amp; Cores Visíveis</span>
            <span id="lbl-sys-vis-summary" class="badge-ok">38 Sistemas</span>
          </div>
          <p class="card-desc">
            Personalize quais consoles, portáteis, computadores e arcades aparecem no Menu OSD da TV e no Karamelo.
          </p>

          <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;align-items:center;">
            <button id="btn-sys-show-all" class="btn-action">✨ Exibir Todos</button>
            <button id="btn-sys-hide-empty" class="btn-action">🧹 Ocultar Sem ROMs</button>
            <button id="btn-sys-hide-comp" class="btn-secondary btn-sm">💻 Ocultar Computadores</button>
            <button id="btn-sys-hide-all" class="btn-secondary btn-sm">🔕 Ocultar Todos</button>
            <span id="lbl-sys-action-msg" style="font-size:12.5px;font-weight:600;margin-left:6px;"></span>
            <input type="text" id="sys-filter-search" placeholder="🔍 Buscar console, core..." style="max-width:240px;margin-left:auto;">
          </div>

          <div class="filter-pills" id="sys-category-pills" style="margin-bottom:16px;display:flex;gap:8px;flex-wrap:wrap;">
            <button class="filter-pill active" data-cat="all">Todos (38)</button>
            <button class="filter-pill" data-cat="Consoles">Consoles (20)</button>
            <button class="filter-pill" data-cat="Portateis">Portáteis (8)</button>
            <button class="filter-pill" data-cat="Computadores">Computadores (7)</button>
            <button class="filter-pill" data-cat="Arcade e Ports">Arcade &amp; Ports (3)</button>
          </div>

          <div id="sys-visibility-grid" style="display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:12px;"></div>
        </div>
)raw";
	h << R"raw(      </div>

      <!-- TAB 4: ROMs & Jogos -->
      <div id="tab-roms" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>1. Escolha o Console / Sistema</span>
            <span id="lbl-selected-sys" style="color:var(--gold-primary);font-weight:700;">SNES</span>
          </div>
          <div class="sys-grid" id="sys-pills"></div>
          <div class="drop-zone" id="rom-drop">
            <p style="font-size:15px;color:var(--gold-primary);font-weight:700;margin-bottom:6px;">📥 Arraste arquivos de ROM aqui para enviar direto ao console!</p>
            <p style="font-size:12px;color:var(--text-muted);margin-bottom:12px;">Formatos suportados: .zip, .7z, .chd, .iso, .sfc, .nes, .bin, .cue, .gba, etc.</p>
            <div style="display:flex;justify-content:center;align-items:center;gap:10px;margin-bottom:12px;font-size:12.5px;">
              <span>Destino:</span>
              <select id="upload-dest-select" style="padding:5px 10px;font-size:12.5px;">
                <option value="system">roms/<span id="opt-cur-sys">SNES</span></option>
                <option value="bios">bios/ (Raiz)</option>
              </select>
            </div>
            <input type="file" id="rom-file-input" multiple style="display:none;">
            <button class="btn-primary" id="btn-browse-roms">📁 Selecionar Arquivos do Dispositivo</button>
            <div class="progress-bar" id="upload-pbar" style="display:none;"><div class="progress-fill" id="upload-pfill"></div></div>
            <p id="upload-status" style="font-size:12.5px;margin-top:8px;color:var(--gold-primary);"></p>
          </div>
          <div class="card-title" style="margin-top:20px;">
            <span>Jogos Instalados na Pasta</span>
            <span id="lbl-rom-count" class="badge badge-gold">0 jogos</span>
          </div>
          <div style="margin-bottom:12px;display:flex;gap:8px;align-items:center;">
)raw";
	h << R"raw(            <input type="text" id="rom-search" placeholder="🔍 Buscar jogo nesta pasta..." style="flex:1;">
            <button class="btn-secondary btn-sm" id="btn-clear-search">Limpar</button>
          </div>
          <div id="roms-table"></div>
        </div>
      </div>

      <!-- TAB 5: Auditor de BIOS -->
      <div id="tab-bios" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>🛡️ Auditor Canônico de BIOS &amp; Firmwares</span>
            <span id="lbl-bios-health" class="badge-ok">-</span>
          </div>
          <p class="card-desc">
            Confere se cada arquivo está na pasta e com o tamanho certo. Arraste qualquer BIOS ausente aqui para enviá-la para a pasta correta automaticamente.
          </p>
          <div style="background:rgba(245,158,11,0.06);border-left:3px solid var(--gold-primary);padding:10px 14px;border-radius:6px;margin-bottom:16px;font-size:12.5px;color:var(--text-main);">
            <strong>Importante:</strong> Consoles de cartucho puro (SNES, Mega Drive, NES, N64, Game Boy, GBA, Atari) não necessitam de BIOS. Apenas sistemas de CD/disco e computadores clássicos exigem firmware.
          </div>
          <div class="drop-zone" id="bios-drop" style="padding:18px;margin-bottom:16px;">
            <p style="color:var(--gold-primary);font-size:14px;font-weight:700;">📥 Solte um arquivo de BIOS aqui (.bin, .rom, .pce, .img)</p>
          </div>
          <div id="bios-table"></div>
        </div>
      </div>

      <!-- TAB 6: Nuvem & Saves -->
      <div id="tab-cloud" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>☁️ Karamelo Cloud Sync (Google Drive)</span>
            <span id="lbl-cloud-badge" class="badge-missing">-</span>
          </div>
          <div style="margin:14px 0;line-height:1.7;">
            <p>Destino Remoto: <b>Google Drive (appDataFolder segura e oculta)</b></p>
)raw";
	h << R"raw(            <p>Saves Sincronizados: <b id="lbl-cloud-count">-</b></p>
            <p>Última Sincronização: <b id="lbl-cloud-time">-</b></p>
            <p style="font-size:12px;color:var(--text-dim);margin-top:8px;">
              O token do Google fica neste computador (no Windows, protegido pelo DPAPI). Os saves vao direto para o seu Google Drive, sem passar pelo site do Karamelo.
            </p>
          </div>
          <button id="btn-sync-now" class="btn-primary" style="padding:11px 22px;">☁️ Sincronizar Saves Agora</button>
        </div>
      </div>

      <!-- TAB 7: Ecossistema Karamelo -->
      <div id="tab-info" class="tab-content">
        <div class="card">
          <div class="card-title"><span>🚀 Visão Geral do Ecossistema Karamelo</span></div>
          <div style="display:flex;gap:10px;flex-wrap:wrap;margin-bottom:16px;">
            <div class="badge badge-gold" style="padding:6px 14px;font-size:12.5px;">37 Sistemas Retrô</div>
            <div class="badge badge-gold" style="padding:6px 14px;font-size:12.5px;">42 Motores Libretro</div>
            <div class="badge badge-gold" style="padding:6px 14px;font-size:12.5px;">94 PC Ports &amp; Recompilados</div>
          </div>
          <p class="card-desc">
            Arquitetura C++20 limpa (clean-room), inspirada no visual e OSD do MiSTer FPGA, porém 100% nativa para PC e dispositivos modernos.
          </p>
          <div class="eco-grid">
            <div class="eco-card">
              <h4><span>🪟 Windows (x86_64)</span><span class="badge badge-ok">Ativo</span></h4>
              <p>Win32 API pura, Direct3D 11 swapchain, WASAPI áudio de baixíssima latência, XInput/DirectInput, System Tray com menu completo e Web Studio.</p>
            </div>
            <div class="eco-card">
              <h4><span>🍎 macOS (Apple Silicon)</span><span class="badge badge-ok">Pronto</span></h4>
              <p>Nativo M1/M2/M3/M4 ARM64, Cocoa, Metal/OpenGL, SDL3 estático sem Homebrew, e 42 PC Ports compilados em Mach-O nativo.</p>
)raw";
	h << R"raw(            </div>
            <div class="eco-card">
              <h4><span>🐧 Linux (x86_64 / Deck)</span><span class="badge badge-ok">Pronto</span></h4>
              <p>SDL3 nativo, OpenGL, PipeWire/ALSA, 59 PC Ports nativos ELF64, pronto para Steam Deck e mini PCs de sala de estar.</p>
            </div>
            <div class="eco-card">
              <h4><span>📀 Karamelo OS (Futuro)</span><span class="badge" style="color:var(--text-muted);border-color:var(--border-subtle);">Planejado</span></h4>
              <p>Ideia de uma distribuição Linux mínima que inicia direto no OSD do Karamelo, para portáteis e pendrives.</p>
            </div>
            <div class="eco-card">
              <h4><span>🍓 Raspberry Pi &amp; SBCs (Futuro)</span><span class="badge" style="color:var(--text-muted);border-color:var(--border-subtle);">Planejado</span></h4>
              <p>Ideia de um build ARM64 para Raspberry Pi 4/5 e placas parecidas. Ainda não existe.</p>
            </div>
            <div class="eco-card">
              <h4><span>📱 Android (Futuro)</span><span class="badge" style="color:var(--text-muted);border-color:var(--border-subtle);">Planejado</span></h4>
              <p>Ideia de um build via Android NDK, com controles na tela. Ainda não existe.</p>
            </div>
            <div class="eco-card">
              <h4><span>🕹️ Nintendo Switch (Futuro)</span><span class="badge" style="color:var(--text-muted);border-color:var(--border-subtle);">Planejado</span></h4>
              <p>Ideia de um build homebrew via libnx. Ainda não existe.</p>
            </div>
          </div>
          <div style="margin-top:20px;padding-top:14px;border-top:1px solid var(--border-subtle);display:flex;gap:18px;flex-wrap:wrap;font-size:13px;">
            <a href="https://karamelo-emu.com" target="_blank" style="color:var(--gold-primary);text-decoration:none;font-weight:600;">🌐 karamelo-emu.com</a>
)raw";
	h << R"raw(            <a href="https://github.com/GuhClemente/Karamelo" target="_blank" style="color:var(--gold-primary);text-decoration:none;font-weight:600;">🐙 Repositório GitHub</a>
            <a href="https://youtube.com/@GuhClemente" target="_blank" style="color:var(--gold-primary);text-decoration:none;font-weight:600;">📺 YouTube @GuhClemente</a>
          </div>
        </div>
      </div>
    </div>
  </main>
</div>
)raw";
	h << R"raw()raw";
	h << R"raw(<script nonce=")raw" << csp_nonce << R"raw(">
)raw";
	h << R"raw(let csrfToken='';
let curSystem='SNES';
let allCurrentRoms=[];
let statusTimer=null;

const kTabTitles = {
  'tab-dash': 'Painel & Controle Remoto',
  'tab-ra': 'RetroAchievements (Troféus & Conta)',
  'tab-video': 'Vídeo, Shaders CRT & Savestates',
  'tab-systems': 'Gerenciador de Sistemas & Visibilidade',
  'tab-roms': 'ROMs & Jogos (FTP / Web Manager)',
  'tab-bios': 'Auditor de BIOS em Tempo Real',
  'tab-cloud': 'Karamelo Cloud Sync (Google Drive)',
  'tab-info': 'Ecossistema & Arquitetura Karamelo'
};

function setAuthVisible(auth){
  document.getElementById('auth-notice').style.display=auth?'none':'block';
  document.getElementById('app-container').style.display=auth?'block':'none';
  document.getElementById('pin-input').style.display=auth?'none':'inline-block';
  document.getElementById('btn-login').style.display=auth?'none':'inline-block';
  document.getElementById('btn-logout').style.display=auth?'inline-block':'none';
}
document.querySelectorAll('.tab-btn').forEach(btn=>{
  btn.addEventListener('click',()=>{
    document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
    btn.classList.add('active');
    const tabId = btn.dataset.tab;
    const target = document.getElementById(tabId);
    if(target) target.classList.add('active');
    const titleEl = document.getElementById('page-current-title');
    if(titleEl && kTabTitles[tabId]) titleEl.textContent = kTabTitles[tabId];

    if(tabId==='tab-roms')loadSystems();
    if(tabId==='tab-bios')loadBios();
    if(tabId==='tab-dash')loadStatus();
    if(tabId==='tab-ra'){loadRaConfig();loadRaAchievements();}
    if(tabId==='tab-video'){loadEmulationConfig();}
    if(tabId==='tab-systems')loadSystemsVisibility();
  });
});
async function remoteAction(act,param){
  await fetch('/api/remote',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},body:JSON.stringify({action:act,param:param||''})});
  loadStatus();
}
)raw";
	h << R"raw(document.getElementById('btn-act-pause').addEventListener('click',()=>remoteAction('pause'));
document.getElementById('btn-act-save').addEventListener('click',()=>remoteAction('save'));
document.getElementById('btn-act-load').addEventListener('click',()=>remoteAction('load'));
document.getElementById('btn-act-shot').addEventListener('click',()=>remoteAction('screenshot'));
document.getElementById('btn-act-reset').addEventListener('click',()=>remoteAction('reset'));
document.getElementById('btn-sync-now').addEventListener('click',()=>remoteAction('sync_cloud'));
document.querySelectorAll('.dpad-btn, .action-grid button').forEach(b=>{
  b.addEventListener('click',()=>remoteAction('menu_key',b.dataset.key));
});
const volSlider=document.getElementById('vol-slider');
const volVal=document.getElementById('vol-val');
volSlider.addEventListener('input',e=>{volVal.textContent=e.target.value+'%';});
volSlider.addEventListener('change',e=>{remoteAction('volume',e.target.value);});
document.getElementById('btn-act-mute').addEventListener('click',()=>{
  const cur=parseInt(volSlider.value,10);
  remoteAction('volume',cur===0?'100':'0');
});

// RetroAchievements Controls & Trophies
async function loadRaConfig(){
  try{
    const r=await fetch('/api/ra/status');
    if(!r.ok)return;
    const d=await r.json();
    const uEl=document.getElementById('ra-username');
    if(uEl && document.activeElement !== uEl) uEl.value=d.username||'';
    const pEl=document.getElementById('ra-password');
    if(pEl) pEl.placeholder=d.has_token?'Credencial salva neste aparelho - digite so para trocar':'Senha do site ou token...';
    const hcEl=document.getElementById('ra-hardcore');
    if(hcEl) hcEl.checked=!!d.hardcore;
    const badge=document.getElementById('lbl-ra-badge');
    if(badge){
      badge.textContent=d.logged_in?'Conectado ('+(d.score||0)+' pts)':d.status||'Desconectado';
      badge.className=d.logged_in?'badge badge-live':'badge';
    }
    const liveBox=document.getElementById('ra-live-box');
    if(liveBox){
      if(d.logged_in){
        liveBox.style.display='block';
)raw";
	h << R"raw(        document.getElementById('ra-live-user').textContent=d.username||'Jogador';
        document.getElementById('ra-live-score').textContent=(d.score||0)+' Pontos';
        const mEl=document.getElementById('ra-live-mode');
        if(mEl) mEl.textContent=d.hardcore?'(Modo Hardcore)':'(Modo Softcore)';
        document.getElementById('ra-live-summary').textContent=
          d.achievements_total>0?`${d.achievements_unlocked}/${d.achievements_total} Conquistas Desbloqueadas`:'Aguardando jogo ativo com conquistas.';
      }else{
        liveBox.style.display='none';
      }
    }
  }catch(e){}
}
async function loadRaAchievements(){
  try{
    const r=await fetch('/api/ra/achievements');
    if(!r.ok)return;
    const d=await r.json();
    const gameEl=document.getElementById('ra-active-game-title');
    const listEl=document.getElementById('ra-achievements-list');
    const barEl=document.getElementById('ra-achievements-bar');
    const summaryEl=document.getElementById('ra-achievements-summary');
    if(!listEl)return;

    const gameTitle=d.game_title||'';
    if(gameEl) gameEl.textContent = gameTitle ? ('Jogo: ' + gameTitle) : 'Nenhum jogo ativo no momento';

    const achs = d.achievements || [];
    listEl.innerHTML = '';
    if(achs.length === 0){
      const empty = document.createElement('div');
      empty.style.textAlign = 'center';
      empty.style.padding = '28px 14px';
      empty.style.color = 'var(--text-muted)';
      empty.style.fontSize = '13px';
      empty.textContent = gameTitle ? 'Este jogo não possui conquistas cadastradas no RetroAchievements.' : 'Inicie um jogo compatível no Karamelo para visualizar e acompanhar seus troféus em tempo real.';
      listEl.appendChild(empty);
      if(summaryEl) summaryEl.textContent = '0 / 0 Conquistas';
      if(barEl) barEl.style.width = '0%';
      return;
    }

    let unl = 0;
    let totalPts = 0;
    let unlPts = 0;
    achs.forEach(a => {
      totalPts += a.points || 0;
      if(a.unlocked){
        unl++;
        unlPts += a.points || 0;
      }
    });

    const pct = achs.length > 0 ? Math.round((unl / achs.length) * 100) : 0;
    if(summaryEl) summaryEl.textContent = `${unl} de ${achs.length} desbloqueadas (${pct}%) • ${unlPts}/${totalPts} Pontos`;
    if(barEl) barEl.style.width = pct + '%';

    achs.forEach(a => {
      const item = document.createElement('div');
      item.className = 'ra-ach-item' + (a.unlocked ? ' unlocked' : '');

      const left = document.createElement('div');
      left.className = 'ra-ach-left';

      const ico = document.createElement('span');
      ico.className = 'ra-ach-icon';
      ico.textContent = a.unlocked ? '🏆' : '🔒';
      left.appendChild(ico);

      const texts = document.createElement('div');
      texts.className = 'ra-ach-texts';

      const title = document.createElement('div');
      title.className = 'ra-ach-title';
      title.textContent = a.title;
      texts.appendChild(title);

      if(a.progress){
        const prog = document.createElement('div');
        prog.className = 'ra-ach-prog';
        prog.textContent = 'Progresso: ' + a.progress;
        texts.appendChild(prog);
      }
      left.appendChild(texts);
      item.appendChild(left);

      const right = document.createElement('div');
      right.className = 'ra-ach-right';

      const pts = document.createElement('span');
      pts.className = 'badge ' + (a.unlocked ? 'badge-gold' : 'badge-dim');
      pts.textContent = a.points + ' pts';
      right.appendChild(pts);

      const st = document.createElement('span');
      st.className = 'badge ' + (a.unlocked ? 'badge-ok' : '');
      st.textContent = a.unlocked ? 'Desbloqueado' : 'Bloqueado';
      right.appendChild(st);

      item.appendChild(right);
      listEl.appendChild(item);
    });
  }catch(e){}
}
async function saveRaConfig(logout){
  const user=document.getElementById('ra-username')?.value||'';
  const pass=document.getElementById('ra-password')?.value||'';
  const is_token=document.getElementById('ra-is-token')?.checked||false;
  const hardcore=document.getElementById('ra-hardcore')?.checked||false;
  const st=document.getElementById('ra-save-status');
  if(st){ st.style.color='var(--gold-primary)'; st.textContent='Processando...'; }
  try{
    const r=await fetch('/api/ra/config',{
      method:'POST',
      headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},
      body:JSON.stringify({username:user,password:pass,is_token,hardcore,logout:!!logout})
    });
    const d=await r.json().catch(()=>({}));
    if(r.ok && d.ok){
      if(st){
        st.style.color='var(--emerald)';
        st.textContent=logout?'Conta desconectada com sucesso!':'Credenciais salvas! Conectando...';
      }
      if(logout){
        const p=document.getElementById('ra-password');if(p)p.value='';
        const u=document.getElementById('ra-username');if(u)u.value='';
      }
      setTimeout(()=>{if(st)st.textContent='';},3500);
      setTimeout(()=>{loadRaConfig();loadRaAchievements();}, 800);
    }else{
      if(st){
        st.style.color='#ef4444';
        st.textContent='Falha: '+(d.error||('HTTP '+r.status));
      }
    }
  }catch(e){if(st){ st.style.color='#ef4444'; st.textContent='Erro de conexão: '+e.message; }}
}
document.getElementById('btn-ra-save')?.addEventListener('click',()=>saveRaConfig(false));
document.getElementById('btn-ra-logout')?.addEventListener('click',()=>saveRaConfig(true));
document.getElementById('btn-ra-toggle-pass')?.addEventListener('click',()=>{
  const p=document.getElementById('ra-password');
  if(p) p.type=p.type==='password'?'text':'password';
});

async function loadEmulationConfig(){
  try{
    const r=await fetch('/api/emulation/config');
    if(!r.ok)return;
    const d=await r.json();
    const asp=document.getElementById('cfg-aspect');
)raw";
	h << R"raw(    if(asp&&d.aspect!==undefined) asp.value=d.aspect;
    const flt=document.getElementById('cfg-filter');
    if(flt&&d.filter!==undefined) flt.value=d.filter;
    const vs=document.getElementById('cfg-vsync');
    if(vs&&d.vsync!==undefined) vs.value=d.vsync;
    const sl=document.getElementById('cfg-slot');
    if(sl&&d.slot!==undefined) {
      sl.value=d.slot;
      const sinfo=document.getElementById('lbl-slot-info');
      if(sinfo) sinfo.textContent='Slot '+d.slot;
    }
  }catch(e){}
}
async function applyEmulationConfig(){
  const asp=parseInt(document.getElementById('cfg-aspect')?.value||'0',10);
  const flt=parseInt(document.getElementById('cfg-filter')?.value||'0',10);
  const vs=parseInt(document.getElementById('cfg-vsync')?.value||'1',10);
  const sl=parseInt(document.getElementById('cfg-slot')?.value||'0',10);
  await fetch('/api/emulation/config',{
    method:'POST',
    headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},
    body:JSON.stringify({aspect:asp,filter:flt,vsync:vs,slot:sl})
  });
  loadEmulationConfig();
}
document.getElementById('btn-apply-video')?.addEventListener('click',applyEmulationConfig);
document.getElementById('cfg-slot')?.addEventListener('change',applyEmulationConfig);
document.getElementById('btn-slot-save')?.addEventListener('click',()=>{
  const sl=parseInt(document.getElementById('cfg-slot')?.value||'0',10);
  remoteAction('save_slot',String(sl));
});
document.getElementById('btn-slot-load')?.addEventListener('click',()=>{
  const sl=parseInt(document.getElementById('cfg-slot')?.value||'0',10);
  remoteAction('load_slot',String(sl));
});
document.getElementById('btn-quick-pause')?.addEventListener('click',()=>remoteAction('pause'));
document.getElementById('btn-quick-reset')?.addEventListener('click',()=>remoteAction('reset'));
document.getElementById('btn-quick-screenshot')?.addEventListener('click',()=>remoteAction('screenshot'));

let g_systems_data=[];
let g_cur_sys_cat='all';
)raw";
	h << R"raw(async function loadSystemsVisibility(){
  try{
    const r=await fetch('/api/systems/visibility');
    if(!r.ok)return;
    const d=await r.json();
    g_systems_data=d.systems||[];
    const sum=document.getElementById('lbl-sys-vis-summary');
    if(sum)sum.textContent=`${d.visible_count}/${d.total} Ativos`;
    renderSystemsVisibility();
  }catch(e){}
}
function renderSystemsVisibility(){
  const g=document.getElementById('sys-visibility-grid');
  if(!g)return;
  const q=(document.getElementById('sys-filter-search')?.value||'').toLowerCase().trim();
  g.innerHTML='';
  const f=g_systems_data.filter(s=>{
    if(g_cur_sys_cat!=='all'&&s.category!==g_cur_sys_cat)return false;
    if(q&&!s.name.toLowerCase().includes(q)&&!s.core.toLowerCase().includes(q)&&!s.code.toLowerCase().includes(q))return false;
    return true;
  });
  f.forEach(s=>{
    const card=document.createElement('div');
    card.className='sys-card'+(s.visible?'':' hidden');

    const info=document.createElement('div');
    info.className='sys-card-info';

    const nm=document.createElement('div');
    nm.className='sys-card-name';
    nm.textContent=s.name;
    info.appendChild(nm);

    const cr=document.createElement('div');
    cr.className='sys-card-core';
    cr.textContent='Core: '+s.core;
    info.appendChild(cr);

    const badges=document.createElement('div');
    badges.className='sys-card-badges';

    const bcat=document.createElement('span');
    bcat.className='sys-badge-cat';
    bcat.textContent=s.category;
    badges.appendChild(bcat);

    const bcnt=document.createElement('span');
    bcnt.className=s.count>0?'sys-badge-count':'sys-badge-empty';
    bcnt.textContent=s.count+' '+(s.count===1?'jogo':'jogos');
    badges.appendChild(bcnt);

    info.appendChild(badges);
    card.appendChild(info);

    const lbl=document.createElement('label');
    lbl.className='switch';
    const chk=document.createElement('input');
    chk.type='checkbox';
    chk.checked=s.visible;
)raw";
	h << R"raw(    chk.dataset.id=s.id;
    const sld=document.createElement('span');
    sld.className='slider';
    lbl.appendChild(chk);
    lbl.appendChild(sld);
    card.appendChild(lbl);

    chk.addEventListener('change',async e=>{
      const isVis=e.target.checked;
      s.visible=isVis;
      if(isVis)card.classList.remove('hidden');else card.classList.add('hidden');
      await fetch('/api/systems/visibility',{
        method:'POST',
        headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},
        body:JSON.stringify({id:s.id,visible:isVis})
      });
      loadSystemsVisibility();
    });

    g.appendChild(card);
  });
}
async function batchSystemAction(payload, btn){
  const stat=document.getElementById('lbl-sys-action-msg');
  if(btn){ btn.disabled=true; }
  if(stat){ stat.textContent='Aplicando...'; stat.style.color='var(--gold-primary)'; }
  try{
    const r=await fetch('/api/systems/visibility',{
      method:'POST',
      headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},
      body:JSON.stringify(payload)
    });
    if(r.ok){
      if(stat){
        stat.textContent='Configuração aplicada!';
        stat.style.color='var(--emerald)';
        setTimeout(()=>{ if(stat) stat.textContent=''; },3000);
      }
    }else{
      if(stat){
        stat.textContent='Falha HTTP '+r.status+' (sessão ou token CSRF inválido)';
        stat.style.color='#ef4444';
      }
    }
  }catch(e){
    if(stat){ stat.textContent='Erro de rede: '+e.message; stat.style.color='#ef4444'; }
  }finally{
    if(btn){ btn.disabled=false; }
    await loadSystemsVisibility();
  }
}
async function loadStatus(){
  const r=await fetch('/api/status');
  if(!r.ok){
    setAuthVisible(false);
    return;
  }
  setAuthVisible(true);
  const d=await r.json();
  if(d.csrf_token) csrfToken=d.csrf_token;
  const pEl=document.getElementById('lbl-platform');if(pEl)pEl.textContent=d.platform||'LAN';
  const vEl=document.getElementById('lbl-version');if(vEl)vEl.textContent='v'+(d.version||'0.98');
  const gSt=document.getElementById('lbl-game-status');if(gSt)gSt.textContent=d.game_running?'RODANDO JOGO':'NO MENU OSD';
  
  const gName = d.game_running?(d.game_name||'Jogo Desconhecido'):'Menu Principal OSD';
  const gCore = d.game_running?('Core: '+(d.core_name||'Nativo')):'Navegando nos Sistemas';
  
  const lgn=document.getElementById('live-game-name');if(lgn)lgn.textContent=gName;
  const lgc=document.getElementById('live-game-core');if(lgc)lgc.textContent=gCore;
  const sgn=document.getElementById('side-game-name');if(sgn)sgn.textContent=gName;
  const sgc=document.getElementById('side-game-core');if(sgc)sgc.textContent=gCore;

  const cc=document.getElementById('lbl-cloud-count');if(cc)cc.textContent=(d.cloud_synced||0)+' arquivos salvos';
)raw";
	h << R"raw(  const ct=document.getElementById('lbl-cloud-time');if(ct)ct.textContent=d.cloud_last_sync||'Nunca';
  const sb=document.getElementById('btn-sync-now');if(sb){
    sb.disabled=!d.cloud_connected;
    sb.textContent=d.cloud_connected?'☁️ Sincronizar Saves Agora':'Conecte no Karamelo: Configurações > Karamelo Cloud';
  }
  const cb=document.getElementById('lbl-cloud-badge');if(cb){
    cb.textContent=d.cloud_connected?'Conectado':'Desconectado';
    cb.className=d.cloud_connected?'badge-ok':'badge-missing';
  }
  if(d.volume!==undefined&&document.activeElement!==volSlider){
    volSlider.value=d.volume;volVal.textContent=d.volume+'%';
  }
  const recList=document.getElementById('recents-list');if(recList){
    recList.replaceChildren();
    if(d.recents&&d.recents.length){
      d.recents.forEach(p=>{
        const row=document.createElement('div');row.className='table-row';
        const name=document.createElement('span');name.textContent=p;
        const playBtn=document.createElement('button');playBtn.className='btn-secondary btn-sm';playBtn.textContent='▶ Iniciar';
        playBtn.addEventListener('click',()=>remoteAction('launch',p));
        row.appendChild(name);row.appendChild(playBtn);recList.appendChild(row);
      });
    }else{
      const empty=document.createElement('p');empty.style.color='var(--text-dim)';empty.style.padding='8px 0';empty.textContent='Nenhum jogo recente registrado.';recList.appendChild(empty);
    }
  }
}
async function loadSystems(){
  const r=await fetch('/api/systems');if(!r.ok)return;const d=await r.json();
  const box=document.getElementById('sys-pills');if(!box)return;box.replaceChildren();
  d.systems.forEach(s=>{
    const p=document.createElement('div');p.className='sys-pill'+(s.id===curSystem?' active':'');
    const t=document.createElement('span');t.textContent=s.name;
    const c=document.createElement('span');c.style.color='var(--gold-primary)';c.textContent=s.count;
    p.appendChild(t);p.appendChild(c);
    p.addEventListener('click',()=>{
      curSystem=s.id;document.querySelectorAll('.sys-pill').forEach(el=>el.classList.remove('active'));
)raw";
	h << R"raw(      p.classList.add('active');document.getElementById('lbl-selected-sys').textContent=s.name;
      const opt=document.getElementById('opt-cur-sys');if(opt)opt.textContent=s.name;
      loadRoms(s.id);
    });
    box.appendChild(p);
  });
  loadRoms(curSystem);
}
function renderFilteredRoms(){
  const query=(document.getElementById('rom-search')?.value||'').toLowerCase().trim();
  const tbl=document.getElementById('roms-table');if(!tbl)return;tbl.replaceChildren();
  const filtered=allCurrentRoms.filter(r=>r.name.toLowerCase().includes(query));
  const cnt=document.getElementById('lbl-rom-count');if(cnt)cnt.textContent=filtered.length+' jogos'+(query?' (filtrados)':'');
  if(!filtered.length){
    const emp=document.createElement('p');emp.style.color='var(--text-dim)';emp.style.padding='14px';
    emp.textContent=query?'Nenhum jogo encontrado para "'+query+'".':'Nenhuma ROM encontrada nesta pasta.';
    tbl.appendChild(emp);return;
  }
  filtered.forEach(item=>{
    const row=document.createElement('div');row.className='table-row';
    const l=document.createElement('span');l.textContent=item.name+' ('+item.size_str+')';
    const acts=document.createElement('div');acts.style.display='flex';acts.style.gap='6px';
    const play=document.createElement('button');play.className='btn-primary btn-sm';play.textContent='▶ Jogar';
    play.addEventListener('click',()=>remoteAction('launch',item.path));
    const del=document.createElement('button');del.className='btn-danger btn-sm';del.textContent='🗑️';
    del.addEventListener('click',async()=>{
      if(confirm('Excluir ROM '+item.name+'?')){
        await fetch('/api/roms/delete',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},body:JSON.stringify({system:curSystem,filename:item.name})});
        loadRoms(curSystem);loadSystems();
      }
    });
    acts.appendChild(play);acts.appendChild(del);row.appendChild(l);row.appendChild(acts);tbl.appendChild(row);
  });
}
)raw";
	h << R"raw(document.getElementById('rom-search')?.addEventListener('input',renderFilteredRoms);
document.getElementById('btn-clear-search')?.addEventListener('click',()=>{
  const el=document.getElementById('rom-search');if(el)el.value='';renderFilteredRoms();
});
async function loadRoms(sys){
  const r=await fetch('/api/roms?system='+encodeURIComponent(sys));if(!r.ok)return;const d=await r.json();
  allCurrentRoms=d.roms||[];renderFilteredRoms();
}
async function loadBios(){
  const r=await fetch('/api/bios');if(!r.ok)return;const d=await r.json();
  const tbl=document.getElementById('bios-table');if(!tbl)return;tbl.replaceChildren();
  let ok=0;
  d.items.forEach(it=>{
    if(it.present)ok++;
    const row=document.createElement('div');row.className='table-row';
    const l=document.createElement('div');l.style.display='flex';l.style.alignItems='center';l.style.gap='8px';
    const name=document.createElement('span');name.textContent=it.system+' - '+it.filename;
    const req=document.createElement('span');req.className='badge';req.style.fontSize='10px';
    req.textContent=it.required?'Obrigatório':'Opcional';
    if(it.required){req.style.borderColor='rgba(245,158,11,0.4)';req.style.color='var(--gold-primary)';}
    l.appendChild(name);l.appendChild(req);
    const s=document.createElement('span');s.className=it.present?'badge-ok':'badge-missing';
    s.textContent=it.present?'PRESENTE':'AUSENTE';row.appendChild(l);row.appendChild(s);tbl.appendChild(row);
  });
  const pct=Math.round((ok/d.items.length)*100);
  const hl=document.getElementById('lbl-bios-health');if(hl){
    hl.textContent=pct+'% Saudável ('+ok+'/'+d.items.length+')';
    hl.className=pct===100?'badge-ok':'badge-missing';
  }
}
async function uploadFiles(files,destSys){
  const pbar=document.getElementById('upload-pbar');const pfill=document.getElementById('upload-pfill');
  const ustat=document.getElementById('upload-status');
  if(pbar)pbar.style.display='block';
  for(let i=0;i<files.length;i++){
)raw";
	h << R"raw(    const f=files[i];if(ustat)ustat.textContent='Enviando ('+(i+1)+'/'+files.length+'): '+f.name;
    const buf=await f.arrayBuffer();
    await fetch('/api/upload?system='+encodeURIComponent(destSys)+'&filename='+encodeURIComponent(f.name),
      {method:'POST',headers:{'X-CSRF-Token':csrfToken,'Content-Type':'application/octet-stream'},body:buf});
    if(pfill)pfill.style.width=Math.round(((i+1)/files.length)*100)+'%';
  }
  if(ustat)ustat.textContent='Upload concluído com sucesso!';
  setTimeout(()=>{if(pbar)pbar.style.display='none';if(pfill)pfill.style.width='0%';},1500);
  loadRoms(curSystem);loadSystems();loadBios();
}
function getChosenUploadTarget(){
  const sel=document.getElementById('upload-dest-select');
  return(sel&&sel.value==='bios')?'bios':curSystem;
}
const dropZ=document.getElementById('rom-drop');
if(dropZ){
  dropZ.addEventListener('dragover',e=>{e.preventDefault();dropZ.classList.add('dragover');});
  dropZ.addEventListener('dragleave',()=>dropZ.classList.remove('dragover'));
  dropZ.addEventListener('drop',e=>{e.preventDefault();dropZ.classList.remove('dragover');if(e.dataTransfer.files.length)uploadFiles(e.dataTransfer.files,getChosenUploadTarget());});
}
const biosDrop=document.getElementById('bios-drop');
if(biosDrop){
  biosDrop.addEventListener('dragover',e=>{e.preventDefault();biosDrop.classList.add('dragover');});
  biosDrop.addEventListener('dragleave',()=>biosDrop.classList.remove('dragover'));
  biosDrop.addEventListener('drop',e=>{e.preventDefault();biosDrop.classList.remove('dragover');if(e.dataTransfer.files.length)uploadFiles(e.dataTransfer.files,'bios');});
}
document.getElementById('btn-sys-show-all')?.addEventListener('click',e=>{
  document.getElementById('btn-sys-hide-empty')?.classList.remove('active');
  batchSystemAction({action:'show_all'},e.currentTarget);
});
document.getElementById('btn-sys-hide-empty')?.addEventListener('click',e=>{
  const b=e.currentTarget;
  b.classList.toggle('active');
  if(b.classList.contains('active')){
    batchSystemAction({action:'hide_empty'},b);
  } else {
    batchSystemAction({action:'show_all'},b);
  }
});
)raw";
	h << R"raw(document.getElementById('btn-sys-hide-comp')?.addEventListener('click',e=>batchSystemAction({action:'hide_category',category:'Computadores'},e.currentTarget));
document.getElementById('btn-sys-hide-all')?.addEventListener('click',e=>{
  document.getElementById('btn-sys-hide-empty')?.classList.remove('active');
  batchSystemAction({action:'hide_all'},e.currentTarget);
});
document.getElementById('sys-filter-search')?.addEventListener('input',renderSystemsVisibility);
document.querySelectorAll('#sys-category-pills .filter-pill').forEach(pill=>{
  pill.addEventListener('click',()=>{
    document.querySelectorAll('#sys-category-pills .filter-pill').forEach(p=>p.classList.remove('active'));
    pill.classList.add('active');
    g_cur_sys_cat=pill.dataset.cat;
    renderSystemsVisibility();
  });
});
document.getElementById('btn-browse-roms')?.addEventListener('click',()=>document.getElementById('rom-file-input')?.click());
document.getElementById('rom-file-input')?.addEventListener('change',e=>{if(e.target.files.length)uploadFiles(e.target.files,getChosenUploadTarget());});
document.getElementById('pin-input')?.addEventListener('keydown',e=>{if(e.key==='Enter')document.getElementById('btn-login').click();});
document.getElementById('btn-login')?.addEventListener('click',async()=>{
  const pin=document.getElementById('pin-input')?.value||'';
  const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pin})});
  if(r.ok){
    const d=await r.json();csrfToken=d.csrf_token||'';setAuthVisible(true);
    loadStatus();if(!statusTimer)statusTimer=setInterval(()=>{loadStatus();if(document.getElementById('tab-ra')?.classList.contains('active')){loadRaConfig();loadRaAchievements();}},2500);
  }else{alert('PIN incorreto ou expirado.');}
});
document.getElementById('btn-logout')?.addEventListener('click',async()=>{
  await fetch('/api/logout',{method:'POST',headers:{'X-CSRF-Token':csrfToken}});
  csrfToken='';setAuthVisible(false);if(statusTimer){clearInterval(statusTimer);statusTimer=null;}
});
loadStatus().then(()=>{
  loadRaConfig();
  loadEmulationConfig();
  loadSystemsVisibility();
  loadBios();
  loadSystems();
});
if(!statusTimer) statusTimer=setInterval(()=>{loadStatus();if(document.getElementById('tab-ra')?.classList.contains('active')){loadRaConfig();loadRaAchievements();}},2500);
</script></body></html>)raw";
	return h.str();
}

static std::string ExtractHeaderValue(const std::string& headers, const std::string& name)
{
	std::string lower_h = headers;
	std::string lower_n = name + ":";
	std::transform(lower_h.begin(), lower_h.end(), lower_h.begin(), [](unsigned char c) { return (char)tolower(c); });
	std::transform(lower_n.begin(), lower_n.end(), lower_n.begin(), [](unsigned char c) { return (char)tolower(c); });

	size_t pos = lower_h.find(lower_n);
	if (pos == std::string::npos) return "";
	pos += lower_n.size();
	while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) pos++;
	size_t end = headers.find_first_of("\r\n", pos);
	return headers.substr(pos, end == std::string::npos ? std::string::npos : (end - pos));
}

static std::string ExtractCookieSid(const std::string& headers)
{
	std::string cookie = ExtractHeaderValue(headers, "Cookie");
	size_t pos = cookie.find("karamelo_sid=");
	if (pos == std::string::npos) return "";
	pos += 13;
	size_t end = cookie.find_first_of("; \r\n", pos);
	return cookie.substr(pos, end == std::string::npos ? std::string::npos : (end - pos));
}

static std::string UrlDecodeParam(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] == '%' && i + 2 < in.size())
		{
			char hex[3] = { in[i + 1], in[i + 2], 0 };
			char* endp = NULL;
			long v = strtol(hex, &endp, 16);
			if (endp == hex + 2)
			{
				out.push_back((char)v);
				i += 2;
				continue;
			}
		}
		else if (in[i] == '+')
		{
			out.push_back(' ');
			continue;
		}
		out.push_back(in[i]);
	}
	return out;
}

static std::string GetQueryParam(const std::string& query, const std::string& key)
{
	std::string pattern = key + "=";
	size_t pos = query.find(pattern);
	if (pos == std::string::npos) return "";
	pos += pattern.size();
	size_t end = query.find('&', pos);
	return UrlDecodeParam(query.substr(pos, end == std::string::npos ? std::string::npos : (end - pos)));
}

static void SendHttpResponse(
#ifdef _WIN32
	SOCKET cli,
#else
	int cli,
#endif
	const char* status,
	const char* content_type,
	const std::string& body,
	const std::string& extra_headers = "",
	const std::string& csp_nonce = "")
{
	std::string nonce = csp_nonce.empty() ? RandomHexToken(12) : csp_nonce;
	std::ostringstream oss;
	oss << "HTTP/1.1 " << status << "\r\n"
	    << "Content-Type: " << content_type << "\r\n"
	    << "Content-Length: " << body.size() << "\r\n"
	    << "Content-Security-Policy: default-src 'self'; script-src 'self' 'nonce-" << nonce << "'; style-src 'self' 'unsafe-inline'; object-src 'none'; base-uri 'none'; frame-ancestors 'none';\r\n"
	    << "X-Content-Type-Options: nosniff\r\n"
	    << "X-Frame-Options: DENY\r\n"
	    << "Permissions-Policy: camera=(), microphone=(), geolocation=()\r\n"
	    << "Cache-Control: no-store\r\n"
	    << extra_headers
	    << "Connection: close\r\n\r\n"
	    << body;

	// send() may take only part of a large page, and on Linux writing to a
	// connection the phone already closed raises SIGPIPE, which ends the whole
	// app unless the call opts out.
	std::string raw = oss.str();
#ifdef MSG_NOSIGNAL
	const int send_flags = MSG_NOSIGNAL;
#else
	const int send_flags = 0;
#endif
	size_t sent = 0;
	while (sent < raw.size())
	{
		int n = send(cli, raw.data() + sent, (int)(raw.size() - sent), send_flags);
		if (n <= 0) break;
		sent += (size_t)n;
	}
}

static void HandleClientConnection(
#ifdef _WIN32
	SOCKET cli
#else
	int cli
#endif
)
{
	// One connection is served at a time, on the thread WebAdminStop() joins. A
	// device that connected and sent nothing used to hold recv() forever: the
	// panel stopped answering and closing the app hung. An idle connection now
	// gets 10 s, a whole request 3 min (a 64 MB ROM over slow Wi-Fi).
#ifdef _WIN32
	DWORD io_timeout = 10000;
#else
	timeval io_timeout{ 10, 0 };
#endif
	setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, (const char*)&io_timeout, sizeof(io_timeout));
	setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, (const char*)&io_timeout, sizeof(io_timeout));
#ifdef SO_NOSIGPIPE
	int no_sigpipe = 1;
	setsockopt(cli, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&no_sigpipe, sizeof(no_sigpipe));
#endif
	const int64_t request_deadline = (int64_t)time(NULL) + 180;

	std::string req;
	req.reserve(8192);
	char chunk[4096];
	size_t header_end = std::string::npos;
	size_t content_len = 0;

	while (req.size() < 65u * 1024u * 1024u && g_web_running.load())
	{
		if ((int64_t)time(NULL) > request_deadline) return;
		int n = recv(cli, chunk, sizeof(chunk), 0);
		if (n <= 0) break;
		req.append(chunk, (size_t)n);

		if (header_end == std::string::npos)
		{
			header_end = req.find("\r\n\r\n");
			if (header_end != std::string::npos)
			{
				std::string cl_str = ExtractHeaderValue(req.substr(0, header_end), "Content-Length");
				if (!cl_str.empty())
				{
					long long parsed = atoll(cl_str.c_str());
					if (parsed > 0 && parsed <= 64LL * 1024LL * 1024LL)
						content_len = (size_t)parsed;
					else if (parsed > 64LL * 1024LL * 1024LL)
					{
						SendHttpResponse(cli, "413 Payload Too Large", "application/json", "{\"error\":\"file_too_large\"}");
						return;
					}
				}
			}
		}

		if (header_end != std::string::npos && req.size() >= header_end + 4 + content_len)
			break;
	}

	if (header_end == std::string::npos)
		return;

	std::string headers = req.substr(0, header_end);
	std::string body = req.substr(header_end + 4);
	if (body.size() > content_len) body.resize(content_len);
	// A connection that dropped mid-upload must not be handled as if the part
	// that arrived were the whole file: it would land in roms/ as a broken game.
	if (body.size() < content_len)
	{
		SendHttpResponse(cli, "400 Bad Request", "application/json", "{\"error\":\"incomplete_request\"}");
		return;
	}

	size_t line_end = headers.find("\r\n");
	std::string req_line = headers.substr(0, line_end);
	std::istringstream iss(req_line);
	std::string method, target;
	iss >> method >> target;

	// Allow-list of HTTP methods: strictly GET and POST
	if (method != "GET" && method != "POST")
	{
		SendHttpResponse(cli, "405 Method Not Allowed", "application/json", "{\"error\":\"method_not_allowed\"}");
		return;
	}

	std::string path = target;
	std::string query;
	size_t qpos = target.find('?');
	if (qpos != std::string::npos)
	{
		path = target.substr(0, qpos);
		query = target.substr(qpos + 1);
	}

	if (path == "/favicon.ico")
	{
		SendHttpResponse(cli, "204 No Content", "image/x-icon", "");
		return;
	}

	if (method == "GET" && path == "/")
	{
		std::string nonce = RandomHexToken(12);
		std::string html = BuildEmbeddedSpaHtml(nonce);
		SendHttpResponse(cli, "200 OK", "text/html; charset=utf-8", html, "", nonce);
		return;
	}

	if (method == "POST" && path == "/api/login")
	{
		int64_t now_utc = (int64_t)time(NULL);
		std::string expected_pin;
		{
			std::lock_guard<std::mutex> lk(g_web_mutex);
			if (now_utc < g_lockout_until_utc)
			{
				SendHttpResponse(cli, "429 Too Many Requests", "application/json", "{\"error\":\"rate_limited\"}");
				return;
			}
			expected_pin = g_web_pin;
		}

		JsonValue jv;
		std::string err;
		std::string user_pin;
		if (JsonParse(body, jv, err) && jv.type == JsonValue::Object)
		{
			const JsonValue* pv = jv.Find("pin");
			if (pv && pv->type == JsonValue::String)
				user_pin = pv->str;
		}
		else if (body.rfind("pin=", 0) == 0)
		{
			user_pin = body.substr(4);
			while (!user_pin.empty() && (user_pin.back() == '\r' || user_pin.back() == '\n' || user_pin.back() == ' '))
				user_pin.pop_back();
		}

		if (!ConstantTimeEquals(user_pin, expected_pin))
		{
			std::lock_guard<std::mutex> lk(g_web_mutex);
			// A 4-digit PIN is only as good as this limit. A fixed 60 s per 5
			// tries still falls to a script overnight, so each lockout doubles,
			// up to an hour, until someone gets it right.
			g_failed_login_count++;
			if (g_failed_login_count >= 5)
			{
				int64_t wait = (int64_t)60 << (g_lockout_rounds < 6 ? g_lockout_rounds : 6);
				g_lockout_rounds++;
				g_lockout_until_utc = now_utc + wait;
				g_failed_login_count = 0;
			}
			SendHttpResponse(cli, "401 Unauthorized", "application/json", "{\"error\":\"invalid_pin\"}");
			return;
		}

		WebSession sess;
		sess.session_id = RandomHexToken(32);
		sess.csrf_token = RandomHexToken(32);
		sess.expires_utc = now_utc + 1800; // 30-min expiration

		{
			std::lock_guard<std::mutex> lk(g_web_mutex);
			g_failed_login_count = 0;
			g_lockout_rounds = 0;
			for (auto it = g_sessions.begin(); it != g_sessions.end();)
				it = (it->second.expires_utc <= now_utc) ? g_sessions.erase(it) : std::next(it);
			g_sessions[sess.session_id] = sess;
		}

		std::string cookie_hdr = "Set-Cookie: karamelo_sid=" + sess.session_id + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800\r\n";
		std::string resp_json = "{\"ok\":true,\"csrf_token\":" + JsonQuote(sess.csrf_token) + "}";
		SendHttpResponse(cli, "200 OK", "application/json", resp_json, cookie_hdr);
		return;
	}

	// Authenticate session for all protected endpoints
	std::string sid = ExtractCookieSid(headers);
	WebSession active_sess;
	bool session_valid = false;
	int64_t now_utc = (int64_t)time(NULL);
	{
		std::lock_guard<std::mutex> lk(g_web_mutex);
		auto it = g_sessions.find(sid);
		if (it != g_sessions.end())
		{
			if (now_utc < it->second.expires_utc)
			{
				it->second.expires_utc = now_utc + 1800;
				active_sess = it->second;
				session_valid = true;
			}
			else
			{
				g_sessions.erase(it);
			}
		}
	}

	if (!session_valid)
	{
		SendHttpResponse(cli, "401 Unauthorized", "application/json", "{\"error\":\"unauthorized\"}");
		return;
	}

	// Validate CSRF token on all state-changing POST requests
	if (method == "POST")
	{
		std::string csrf_hdr = ExtractHeaderValue(headers, "X-CSRF-Token");
		if (!ConstantTimeEquals(csrf_hdr, active_sess.csrf_token))
		{
			SendHttpResponse(cli, "403 Forbidden", "application/json", "{\"error\":\"csrf_invalid\"}");
			return;
		}
	}

	if (method == "POST" && path == "/api/logout")
	{
		{
			std::lock_guard<std::mutex> lk(g_web_mutex);
			g_sessions.erase(sid);
		}
		std::string clear_cookie = "Set-Cookie: karamelo_sid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n";
		SendHttpResponse(cli, "200 OK", "application/json", "{\"ok\":true}", clear_cookie);
		return;
	}

	if (method == "GET" && path == "/api/ra/status")
	{
		if (g_hooks.ra_update)
			g_hooks.ra_update();

		std::ostringstream ss;
		const char* user = g_hooks.ra_get_user ? g_hooks.ra_get_user() : "";
		const char* st = g_hooks.ra_get_status ? g_hooks.ra_get_status() : "Desconectado";
		bool hc = g_hooks.ra_get_hardcore ? g_hooks.ra_get_hardcore() : false;
		bool tok = g_hooks.ra_has_token ? g_hooks.ra_has_token() : false;
		bool in = g_hooks.ra_is_logged_in ? g_hooks.ra_is_logged_in() : false;
		bool en = g_hooks.ra_is_enabled ? g_hooks.ra_is_enabled() : false;
		uint32_t sc = g_hooks.ra_get_score ? g_hooks.ra_get_score() : 0;
		const char* gtitle = g_hooks.ra_get_game_title ? g_hooks.ra_get_game_title() : "";
		int total = g_hooks.ra_get_ach_total ? g_hooks.ra_get_ach_total() : 0;
		int unl = g_hooks.ra_get_ach_unlocked ? g_hooks.ra_get_ach_unlocked() : 0;

		ss << "{\"username\":" << JsonQuote(user ? user : "")
		   << ",\"hardcore\":" << (hc ? "true" : "false")
		   << ",\"has_token\":" << (tok ? "true" : "false")
		   << ",\"logged_in\":" << (in ? "true" : "false")
		   << ",\"enabled\":" << (en ? "true" : "false")
		   << ",\"status\":" << JsonQuote(st ? st : "")
		   << ",\"score\":" << sc
		   << ",\"game_title\":" << JsonQuote(gtitle ? gtitle : "")
		   << ",\"achievements_total\":" << total
		   << ",\"achievements_unlocked\":" << unl
		   << "}";
		SendHttpResponse(cli, "200 OK", "application/json; charset=utf-8", ss.str());
		return;
	}

	if (method == "GET" && path == "/api/ra/achievements")
	{
		std::ostringstream ss;
		const char* gtitle = g_hooks.ra_get_game_title ? g_hooks.ra_get_game_title() : "";
		ss << "{\"game_title\":" << JsonQuote(gtitle ? gtitle : "")
		   << ",\"achievements\":[";

		if (g_hooks.ra_get_achievements)
		{
			RaAchievementInfo items[256];
			int count = g_hooks.ra_get_achievements(items, 256);
			for (int i = 0; i < count; ++i)
			{
				if (i > 0) ss << ",";
				ss << "{"
				   << "\"title\":" << JsonQuote(items[i].title)
				   << ",\"progress\":" << JsonQuote(items[i].progress)
				   << ",\"points\":" << items[i].points
				   << ",\"unlocked\":" << (items[i].unlocked ? "true" : "false")
				   << "}";
			}
		}

		ss << "]}";
		SendHttpResponse(cli, "200 OK", "application/json; charset=utf-8", ss.str());
		return;
	}

	if (method == "POST" && path == "/api/ra/config")
	{
		JsonValue jv;
		std::string err;
		if (!JsonParse(body, jv, err) || jv.type != JsonValue::Object)
		{
			SendHttpResponse(cli, "400 Bad Request", "application/json", "{\"error\":\"invalid_json\"}");
			return;
		}

		std::string user;
		const JsonValue* uv = jv.Find("username");
		if (uv && uv->type == JsonValue::String) user = uv->str;

		std::string pass;
		const JsonValue* pv = jv.Find("password");
		if (pv && pv->type == JsonValue::String) pass = pv->str;

		bool is_token = false;
		const JsonValue* tv = jv.Find("is_token");
		if (tv && tv->type == JsonValue::Bool) is_token = tv->boolean;

		bool hardcore = false;
		const JsonValue* hv = jv.Find("hardcore");
		if (hv && hv->type == JsonValue::Bool) hardcore = hv->boolean;

		bool logout = false;
		const JsonValue* lv = jv.Find("logout");
		if (lv && lv->type == JsonValue::Bool) logout = lv->boolean;

		bool ok = false;
		if (g_hooks.ra_set_credentials)
			ok = g_hooks.ra_set_credentials(user.c_str(), pass.c_str(), is_token, hardcore, logout);
		else
			ok = true; // unit test mock stub

		SendHttpResponse(cli, ok ? "200 OK" : "400 Bad Request", "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"failed_or_missing_credentials\"}");
		return;
	}

	if (method == "GET" && path == "/api/emulation/config")
	{
		std::ostringstream ss;
		int asp = g_hooks.get_aspect ? g_hooks.get_aspect() : 0;
		int flt = g_hooks.get_filter ? g_hooks.get_filter() : 0;
		int vs = g_hooks.get_vsync ? g_hooks.get_vsync() : 1;
		int sl = g_hooks.get_slot ? g_hooks.get_slot() : 0;
		int vol = g_hooks.get_volume ? g_hooks.get_volume() : 100;
		bool mt = g_hooks.get_mute ? g_hooks.get_mute() : false;
		bool running = g_hooks.is_running ? g_hooks.is_running() : false;

		ss << "{\"aspect\":" << asp
		   << ",\"filter\":" << flt
		   << ",\"vsync\":" << vs
		   << ",\"volume\":" << vol
		   << ",\"mute\":" << (mt ? "true" : "false")
		   << ",\"slot\":" << sl
		   << ",\"is_running\":" << (running ? "true" : "false")
		   << "}";
		SendHttpResponse(cli, "200 OK", "application/json; charset=utf-8", ss.str());
		return;
	}

	if (method == "POST" && path == "/api/emulation/config")
	{
		JsonValue jv;
		std::string err;
		if (!JsonParse(body, jv, err) || jv.type != JsonValue::Object)
		{
			SendHttpResponse(cli, "400 Bad Request", "application/json", "{\"error\":\"invalid_json\"}");
			return;
		}

		const JsonValue* asp = jv.Find("aspect");
		if (asp && asp->type == JsonValue::Number && g_hooks.set_aspect) g_hooks.set_aspect((int)asp->number);

		const JsonValue* flt = jv.Find("filter");
		if (flt && flt->type == JsonValue::Number && g_hooks.set_filter) g_hooks.set_filter((int)flt->number);

		const JsonValue* vs = jv.Find("vsync");
		if (vs && vs->type == JsonValue::Number && g_hooks.set_vsync) g_hooks.set_vsync((int)vs->number);

		const JsonValue* sl = jv.Find("slot");
		if (sl && sl->type == JsonValue::Number && g_hooks.set_slot) g_hooks.set_slot((int)sl->number);

		const JsonValue* vol = jv.Find("volume");
		if (vol && vol->type == JsonValue::Number && g_hooks.set_volume) g_hooks.set_volume((int)vol->number);

		const JsonValue* mt = jv.Find("mute");
		if (mt && mt->type == JsonValue::Bool && g_hooks.set_mute) g_hooks.set_mute(mt->boolean);

		if (g_hooks.save_settings) g_hooks.save_settings();
		SendHttpResponse(cli, "200 OK", "application/json", "{\"ok\":true}");
		return;
	}

if (method == "GET" && (path == "/api/bios" || path == "/api/bios-audit"))
	{
		std::string audit_json = WebAdminBuildBiosAuditJson("");
		SendHttpResponse(cli, "200 OK", "application/json", audit_json);
		return;
	}

	if (method == "GET" && path == "/api/status")
	{
		std::string status_json = WebAdminBuildStatusJson(active_sess.csrf_token);
		SendHttpResponse(cli, "200 OK", "application/json", status_json);
		return;
	}

	if (method == "GET" && path == "/api/systems/visibility")
	{
		std::string v_json = WebAdminBuildSystemsVisibilityJson();
		SendHttpResponse(cli, "200 OK", "application/json", v_json);
		return;
	}

	if (method == "POST" && path == "/api/systems/visibility")
	{
		JsonValue jv;
		std::string err;
		if (JsonParse(body, jv, err) && jv.type == JsonValue::Object)
		{
			const JsonValue* act_v = jv.Find("action");
			if (act_v && act_v->type == JsonValue::String)
			{
				std::string act = act_v->str;
				if (act == "show_all" && g_hooks.set_all_systems_visible)
				{
					g_hooks.set_all_systems_visible(true);
				}
				else if (act == "hide_all" && g_hooks.set_all_systems_visible)
				{
					g_hooks.set_all_systems_visible(false);
				}
				else if (act == "hide_empty" && g_hooks.hide_empty_systems)
				{
					g_hooks.hide_empty_systems();
				}
				else if (act == "hide_category" && g_hooks.hide_category)
				{
					const JsonValue* cat_v = jv.Find("category");
					if (cat_v && cat_v->type == JsonValue::String)
						g_hooks.hide_category(cat_v->str.c_str());
				}
			}
			else
			{
				const JsonValue* id_v = jv.Find("id");
				const JsonValue* vis_v = jv.Find("visible");
				if (id_v && id_v->type == JsonValue::Number && vis_v && vis_v->type == JsonValue::Bool)
				{
					int sys_id = (int)id_v->number;
					bool visible = vis_v->boolean;
					if (g_hooks.set_system_visible)
						g_hooks.set_system_visible(sys_id, visible);
				}
			}
			SendHttpResponse(cli, "200 OK", "application/json", "{\"ok\":true}");
			return;
		}
		SendHttpResponse(cli, "400 Bad Request", "application/json", "{\"error\":\"invalid_payload\"}");
		return;
	}

	if (method == "GET" && path == "/api/systems")
	{
		std::string sys_json = WebAdminBuildSystemsJson();
		SendHttpResponse(cli, "200 OK", "application/json", sys_json);
		return;
	}

	if (method == "GET" && path == "/api/roms")
	{
		std::string sys = GetQueryParam(query, "system");
		std::string roms_json = WebAdminBuildRomsJson(sys);
		SendHttpResponse(cli, "200 OK", "application/json", roms_json);
		return;
	}

	if (method == "POST" && path == "/api/roms/delete")
	{
		JsonValue jv;
		std::string err;
		std::string sys, fn;
		if (JsonParse(body, jv, err) && jv.type == JsonValue::Object)
		{
			const JsonValue* sv = jv.Find("system");
			if (sv && sv->type == JsonValue::String) sys = sv->str;
			const JsonValue* fv = jv.Find("filename");
			if (fv && fv->type == JsonValue::String) fn = fv->str;
		}

		std::string safe_fn = WebAdminSanitizeFilename(fn);
		if (!WebAdminIsAllowedTargetSystem(sys) || safe_fn.empty())
		{
			SendHttpResponse(cli, "400 Bad Request", "application/json", "{\"error\":\"invalid_delete_params\"}");
			return;
		}

		fs::path dir = ResolveSystemRomDir(sys);
		fs::path target = dir / safe_fn;
		std::error_code ec;
		if (fs::exists(target, ec))
		{
			fs::remove(target, ec);
			SendHttpResponse(cli, "200 OK", "application/json", "{\"ok\":true}");
		}
		else
		{
			SendHttpResponse(cli, "404 Not Found", "application/json", "{\"error\":\"file_not_found\"}");
		}
		return;
	}

	if (method == "POST" && path == "/api/remote")
	{
		JsonValue jv;
		std::string err;
		std::string action, param;
		if (JsonParse(body, jv, err) && jv.type == JsonValue::Object)
		{
			const JsonValue* av = jv.Find("action");
			if (av && av->type == JsonValue::String) action = av->str;
			const JsonValue* pv = jv.Find("param");
			if (pv && pv->type == JsonValue::String) param = pv->str;
		}

		if (action == "pause")
		{
			if (g_hooks.process_key) g_hooks.process_key(KEY_MENU_TOGGLE);
			CoreSetToast(Tr("PAUSADO / RETOMADO VIA WEB", "PAUSED / RESUMED VIA WEB"), 120);
		}
		else if (action == "reset")
		{
			if (g_hooks.reset) g_hooks.reset();
			CoreSetToast(Tr("JOGO REINICIADO VIA WEB", "GAME RESET VIA WEB"), 120);
		}
		else if (action == "save")
		{
			if (g_hooks.save_state) g_hooks.save_state(0);
			CoreSetToast(Tr("ESTADO SALVO (SLOT 0)", "STATE SAVED (SLOT 0)"), 120);
		}
		else if (action == "load")
		{
			if (g_hooks.load_state) g_hooks.load_state(0);
			CoreSetToast(Tr("ESTADO CARREGADO (SLOT 0)", "STATE LOADED (SLOT 0)"), 120);
		}
		else if (action == "save_slot")
		{
			int s = atoi(param.c_str());
			if (s >= 0 && s <= 9)
			{
				if (g_hooks.set_slot) g_hooks.set_slot(s);
				if (g_hooks.save_state) g_hooks.save_state(s);
				CoreSetToast(Tr("ESTADO SALVO", "STATE SAVED"), 120);
			}
		}
		else if (action == "load_slot")
		{
			int s = atoi(param.c_str());
			if (s >= 0 && s <= 9)
			{
				if (g_hooks.set_slot) g_hooks.set_slot(s);
				if (g_hooks.load_state) g_hooks.load_state(s);
				CoreSetToast(Tr("ESTADO CARREGADO", "STATE LOADED"), 120);
			}
		}
		else if (action == "screenshot")
		{
			if (g_hooks.screenshot) g_hooks.screenshot();
			CoreSetToast(Tr("SCREENSHOT SALVA!", "SCREENSHOT SAVED!"), 120);
		}
		else if (action == "sync_cloud")
		{
			CloudSyncTriggerAsync();
		}
		else if (action == "volume")
		{
			int v = atoi(param.c_str());
			if (v >= 0 && v <= 100 && g_hooks.set_volume) g_hooks.set_volume(v);
		}
		else if (action == "menu_key")
		{
			if (g_hooks.process_key)
			{
				if (param == "up") g_hooks.process_key(KEY_UP);
				else if (param == "down") g_hooks.process_key(KEY_DOWN);
				else if (param == "left") g_hooks.process_key(KEY_LEFT);
				else if (param == "right") g_hooks.process_key(KEY_RIGHT);
				else if (param == "enter" || param == "select") g_hooks.process_key(KEY_SELECT);
				else if (param == "back") g_hooks.process_key(KEY_CANCEL);
				else if (param == "menu") g_hooks.process_key(KEY_MENU_TOGGLE);
			}
		}
		else if (action == "launch")
		{
			if (!param.empty() && g_hooks.launch_game)
			{
				g_hooks.launch_game(param);
			}
		}

		SendHttpResponse(cli, "200 OK", "application/json", "{\"ok\":true}");
		return;
	}

	if (method == "POST" && path == "/api/upload")
	{
		std::string sys_dir = GetQueryParam(query, "system");
		if (sys_dir.empty()) sys_dir = GetQueryParam(query, "target");
		std::string raw_fn = GetQueryParam(query, "filename");
		std::string safe_fn = WebAdminSanitizeFilename(raw_fn);

		if (!WebAdminIsAllowedTargetSystem(sys_dir) ||
		    safe_fn.empty() ||
		    !WebAdminValidateMagicAndContent(safe_fn, (const uint8_t*)body.data(), body.size()))
		{
			SendHttpResponse(cli, "400 Bad Request", "application/json", "{\"error\":\"invalid_upload_parameters\"}");
			return;
		}

		std::error_code ec;
		fs::path target_root = (sys_dir.rfind("bios", 0) == 0)
			? fs::path(sys_dir)
			: (fs::path("roms") / sys_dir);
		fs::create_directories(target_root, ec);

		fs::path canonical_root = fs::weakly_canonical(target_root, ec);
		fs::path canonical_dest = fs::weakly_canonical(target_root / safe_fn, ec);
		std::string root_prefix = canonical_root.string();
		if (!root_prefix.empty() && root_prefix.back() != '/' && root_prefix.back() != '\\')
			root_prefix.push_back((char)fs::path::preferred_separator);

		std::string dest_str = canonical_dest.string();
		if (ec || dest_str.rfind(root_prefix, 0) != 0)
		{
			SendHttpResponse(cli, "403 Forbidden", "application/json", "{\"error\":\"path_boundary_violation\"}");
			return;
		}

		FILE* f = fopen(dest_str.c_str(), "wb");
		if (!f)
		{
			SendHttpResponse(cli, "500 Internal Server Error", "application/json", "{\"error\":\"write_failed\"}");
			return;
		}
		size_t written = fwrite(body.data(), 1, body.size(), f);
		fclose(f);

		if (written != body.size())
		{
			fs::remove(canonical_dest, ec);
			SendHttpResponse(cli, "500 Internal Server Error", "application/json", "{\"error\":\"incomplete_write\"}");
			return;
		}

		CoreSetToast(Tr("ARQUIVO RECEBIDO VIA WI-FI!", "FILE RECEIVED OVER WI-FI!"), 150);
		SendHttpResponse(cli, "200 OK", "application/json", "{\"ok\":true}");
		return;
	}

	SendHttpResponse(cli, "404 Not Found", "application/json", "{\"error\":\"not_found\"}");
}

void WebAdminInit()
{
	WebAdminRegeneratePin();
}

void WebAdminShutdown()
{
	WebAdminStop();
}

bool WebAdminStart(int port, bool loopback_only)
{
	if (g_web_running.load()) return true;
	if (port <= 0 || port > 65535) port = 8080;

#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	int candidates[] = { port, 8080, 8081, 8082, 8088, 8888, 0 };
	int num_candidates = (int)(sizeof(candidates) / sizeof(candidates[0]));

#ifdef _WIN32
	SOCKET srv = INVALID_SOCKET;
#else
	int srv = -1;
#endif
	int bound_port = 0;

	for (int i = 0; i < num_candidates; i++)
	{
		int try_port = candidates[i];
		if (i > 0 && try_port == port) continue;

#ifdef _WIN32
		srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (srv == INVALID_SOCKET) continue;
#else
		srv = socket(AF_INET, SOCK_STREAM, 0);
		if (srv < 0) continue;
#endif

		int opt = 1;
#ifdef _WIN32
		// On Windows SO_REUSEADDR lets a bind take a port another program is
		// listening on: the browser could land on that program instead of here.
		setsockopt(srv, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&opt, sizeof(opt));
#else
		setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#endif

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
		addr.sin_port = htons((uint16_t)try_port);

		if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(srv, 8) == 0)
		{
			socklen_t alen = sizeof(addr);
			getsockname(srv, (sockaddr*)&addr, &alen);
			bound_port = ntohs(addr.sin_port);
			break;
		}

#ifdef _WIN32
		closesocket(srv);
		srv = INVALID_SOCKET;
#else
		close(srv);
		srv = -1;
#endif
	}

#ifdef _WIN32
	if (srv == INVALID_SOCKET) return false;
#else
	if (srv < 0) return false;
#endif

	g_listen_sock = srv;
	g_web_port.store(bound_port);
	g_web_running.store(true);

	{
		std::lock_guard<std::mutex> lk(g_web_mutex);
		const char* lan_ip = loopback_only ? "127.0.0.1" : NetplayGetLocalIp();
		if (!lan_ip || !lan_ip[0] || strcmp(lan_ip, "0.0.0.0") == 0)
			lan_ip = "127.0.0.1";
		g_web_url = "http://" + std::string(lan_ip) + ":" + std::to_string(bound_port);
	}

	g_web_thread = std::thread([]()
	{
		while (g_web_running.load())
		{
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(g_listen_sock, &fds);
			timeval tv{ 0, 250000 }; // 250ms poll slice
			int sel = select((int)g_listen_sock + 1, &fds, NULL, NULL, &tv);
			if (sel <= 0 || !g_web_running.load()) continue;

			sockaddr_in cli_addr{};
			socklen_t cli_len = sizeof(cli_addr);
			auto cli = accept(g_listen_sock, (sockaddr*)&cli_addr, &cli_len);
#ifdef _WIN32
			if (cli == INVALID_SOCKET) continue;
#else
			if (cli < 0) continue;
#endif
			HandleClientConnection(cli);
#ifdef _WIN32
			closesocket(cli);
#else
			close(cli);
#endif
		}
	});

	return true;
}

void WebAdminStop()
{
	if (!g_web_running.exchange(false))
		return;

#ifdef _WIN32
	if (g_listen_sock != INVALID_SOCKET)
	{
		closesocket(g_listen_sock);
		g_listen_sock = INVALID_SOCKET;
	}
#else
	if (g_listen_sock >= 0)
	{
		close(g_listen_sock);
		g_listen_sock = -1;
	}
#endif

	if (g_web_thread.joinable())
		g_web_thread.join();
}

void WebAdminOpenInBrowser()
{
	if (!WebAdminIsRunning())
		WebAdminStart(8080, false);

	const char* url = WebAdminGetLocalUrl();
	if (url && url[0])
	{
#ifdef _WIN32
		ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
		pid_t pid = fork();
		if (pid == 0) { execlp("open", "open", url, (char*)NULL); _exit(0); }
#else
		pid_t pid = fork();
		if (pid == 0) { execlp("xdg-open", "xdg-open", url, (char*)NULL); _exit(0); }
#endif
	}
}


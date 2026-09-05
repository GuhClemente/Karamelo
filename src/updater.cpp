#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

#include "updater.h"
#include "app_info.h"
#include "osd.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

// Configuration
static const char* UPDATE_MANIFEST_URL = "https://mister4all.com/downloads/version.json";
static const char* UPDATE_FALLBACK_URL = "https://raw.githubusercontent.com/gfdac/MiSTer-4-All/main/dist/version.json";
static const wchar_t* USER_AGENT = L"MiSTer-4-ALL-Updater/1.0";

// Internal State
static CRITICAL_SECTION g_updater_lock;
static HANDLE           g_updater_thread = NULL;
static HANDLE           g_updater_event = NULL;
static volatile LONG    g_updater_running = 0;

static UpdaterState     g_state = UPDATER_STATE_IDLE;
static UpdateInfo       g_info;
static int              g_progress = 0; // 0 to 100
static std::string      g_status_msg = "Pronto";
static bool             g_command_check = false;
static bool             g_command_download = false;
static bool             g_manual_check = false;

// Helpers for JSON Parsing
static std::string ExtractJsonString(const std::string& json, const std::string& key)
{
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos) return "";

	pos = json.find(':', pos + search.length());
	if (pos == std::string::npos) return "";

	pos = json.find('"', pos + 1);
	if (pos == std::string::npos) return "";

	size_t end = json.find('"', pos + 1);
	if (end == std::string::npos) return "";

	return json.substr(pos + 1, end - (pos + 1));
}

static size_t ExtractJsonNumber(const std::string& json, const std::string& key)
{
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos) return 0;

	pos = json.find(':', pos + search.length());
	if (pos == std::string::npos) return 0;

	pos = json.find_first_of("0123456789", pos + 1);
	if (pos == std::string::npos) return 0;

	size_t end = json.find_first_not_of("0123456789", pos);
	std::string num_str = json.substr(pos, (end == std::string::npos) ? std::string::npos : (end - pos));
	return (size_t)strtoull(num_str.c_str(), NULL, 10);
}

static bool ExtractJsonBool(const std::string& json, const std::string& key)
{
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos) return false;

	pos = json.find(':', pos + search.length());
	if (pos == std::string::npos) return false;

	size_t pos_true = json.find("true", pos);
	size_t pos_false = json.find("false", pos);
	size_t pos_comma = json.find_first_of(",}\n", pos);

	if (pos_true != std::string::npos && (pos_comma == std::string::npos || pos_true < pos_comma))
		return true;

	return false;
}

static bool ParseManifestJson(const std::string& json, UpdateInfo& out)
{
	out.version = ExtractJsonString(json, "version");
	if (out.version.empty()) return false;

	out.title = ExtractJsonString(json, "title");
	out.release_date = ExtractJsonString(json, "release_date");
	out.notes = ExtractJsonString(json, "notes");
	out.exe_url = ExtractJsonString(json, "exe_url");
	out.exe_sha256 = ExtractJsonString(json, "exe_sha256");
	out.zip_url = ExtractJsonString(json, "zip_url");
	out.exe_size = ExtractJsonNumber(json, "exe_size");
	out.force_full_package = ExtractJsonBool(json, "force_full_package");

	return true;
}

// Version Comparison
static std::vector<int> ParseVersionComponents(const std::string& ver)
{
	std::vector<int> parts;
	std::string clean;
	for (char c : ver)
	{
		if (isdigit((unsigned char)c) || c == '.') clean += c;
	}

	std::stringstream ss(clean);
	std::string item;
	while (std::getline(ss, item, '.'))
	{
		if (!item.empty())
		{
			parts.push_back(atoi(item.c_str()));
		}
	}
	while (parts.size() < 4) parts.push_back(0);
	return parts;
}

bool UpdaterIsNewerVersion(const std::string& current_ver, const std::string& remote_ver)
{
	std::vector<int> cur = ParseVersionComponents(current_ver);
	std::vector<int> rem = ParseVersionComponents(remote_ver);

	for (size_t i = 0; i < cur.size() && i < rem.size(); i++)
	{
		if (rem[i] > cur[i]) return true;
		if (rem[i] < cur[i]) return false;
	}
	return false;
}

// Path Helpers
static std::string GetExecutableDirectory()
{
	char path[MAX_PATH] = { 0 };
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string full_path(path);
	size_t last_slash = full_path.find_last_of("\\/");
	if (last_slash != std::string::npos)
		return full_path.substr(0, last_slash);
	return ".";
}

static std::string GetCurrentExecutablePath()
{
	char path[MAX_PATH] = { 0 };
	GetModuleFileNameA(NULL, path, MAX_PATH);
	return std::string(path);
}

// SHA-256 of a file on disk, lowercase hex - via BCrypt (CNG), built into
// Windows since Vista, no third-party crypto dependency needed. Returns
// empty on any failure (missing file, API error) so the caller treats that
// the same as "hash unknown" rather than crashing on a malformed digest.
static std::string Sha256File(const std::string& path)
{
	std::string result;

	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return result;

	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	std::vector<uint8_t> hash_obj;
	bool ok = false;

	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0)
	{
		DWORD obj_len = 0, cb = 0;
		BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len, sizeof(obj_len), &cb, 0);
		hash_obj.resize(obj_len ? obj_len : 256);

		if (BCryptCreateHash(alg, &hash, hash_obj.data(), (ULONG)hash_obj.size(), NULL, 0, 0) == 0)
		{
			std::vector<uint8_t> buf(65536);
			size_t n;
			ok = true;
			while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
			{
				if (BCryptHashData(hash, buf.data(), (ULONG)n, 0) != 0) { ok = false; break; }
			}

			if (ok)
			{
				uint8_t digest[32];
				if (BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
				{
					char hex[65];
					for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
					result.assign(hex, 64);
				}
			}
			BCryptDestroyHash(hash);
		}
	}
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	fclose(f);
	return result;
}

// WinHTTP Helper
static bool HttpFetchData(const std::string& url, std::string* out_str, std::vector<uint8_t>* out_bin,
                          size_t* total_size_out, size_t known_total_size, bool track_progress)
{
	wchar_t wurl[2048];
	if (MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl, 2048) <= 0)
		return false;

	URL_COMPONENTS uc = { 0 };
	wchar_t host[256] = { 0 };
	wchar_t path[1536] = { 0 };
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = host; uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;  uc.dwUrlPathLength = 1536;

	if (!WinHttpCrackUrl(wurl, 0, 0, &uc))
		return false;

	HINTERNET session = WinHttpOpen(USER_AGENT,
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;

	WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);

	bool success = false;
	HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
	if (connect)
	{
		DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, NULL,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

		if (request)
		{
			// Follow redirects automatically
			DWORD opt = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
			WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &opt, sizeof(opt));

			BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

			if (sent && WinHttpReceiveResponse(request, NULL))
			{
				DWORD status_code = 0, code_len = sizeof(status_code);
				WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &code_len, WINHTTP_NO_HEADER_INDEX);

				if (status_code == 200)
				{
					DWORD content_len = 0, clen_size = sizeof(content_len);
					if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &content_len, &clen_size, WINHTTP_NO_HEADER_INDEX))
					{
						if (total_size_out) *total_size_out = content_len;
					}
					else if (known_total_size > 0 && total_size_out)
					{
						*total_size_out = known_total_size;
					}

					size_t expected_total = total_size_out ? *total_size_out : known_total_size;
					size_t downloaded = 0;

					FILE* dest_file = NULL;
					std::string temp_new_path;
					bool need_dest_file = (out_bin == NULL && out_str == NULL && track_progress);
					if (need_dest_file)
					{
						temp_new_path = GetExecutableDirectory() + "\\MiSTer_4_ALL.new";
						dest_file = fopen(temp_new_path.c_str(), "wb");
						if (!dest_file)
						{
							// Nothing to write the download into (read-only install
							// dir, AV lock, full disk) - without this check every
							// downloaded byte below was silently discarded (none of
							// the dest_file/out_bin/out_str branches matched) while
							// the function still reported success at the end, since
							// dest_file being NULL also skips the size/read_ok check
							// that would otherwise have caught it.
							WinHttpCloseHandle(request);
							WinHttpCloseHandle(connect);
							WinHttpCloseHandle(session);
							return false;
						}
					}

					DWORD avail = 0;
					bool read_ok = true;
					while (WinHttpQueryDataAvailable(request, &avail) && avail > 0)
					{
						std::vector<char> buffer(avail);
						DWORD read_bytes = 0;
						if (!WinHttpReadData(request, buffer.data(), avail, &read_bytes) || read_bytes == 0)
						{
							read_ok = false;
							break;
						}

						if (dest_file)
						{
							fwrite(buffer.data(), 1, read_bytes, dest_file);
						}
						else if (out_bin)
						{
							out_bin->insert(out_bin->end(), buffer.begin(), buffer.begin() + read_bytes);
						}
						else if (out_str)
						{
							out_str->append(buffer.data(), read_bytes);
						}

						downloaded += read_bytes;

						if (track_progress && expected_total > 0)
						{
							int pct = (int)((downloaded * 100) / expected_total);
							if (pct > 99) pct = 99; // 100% when file finishes
							EnterCriticalSection(&g_updater_lock);
							g_progress = pct;
							LeaveCriticalSection(&g_updater_lock);
						}
					}

					if (dest_file)
					{
						fclose(dest_file);
						// A dropped connection just stops WinHttpQueryDataAvailable
						// returning more data - read_ok stays true and downloaded
						// was merely "more than 1024 bytes", which a truncated
						// download clears easily. Require it to match the size the
						// server (or the manifest) actually promised whenever that
						// size is known, so a partial file can't be applied as if
						// it were the complete update.
						bool size_ok = (expected_total > 0) ? (downloaded >= expected_total)
						                                     : (downloaded > 1024);
						if (read_ok && size_ok)
						{
							success = true;
							if (track_progress)
							{
								EnterCriticalSection(&g_updater_lock);
								g_progress = 100;
								LeaveCriticalSection(&g_updater_lock);
							}
						}
						else
						{
							remove(temp_new_path.c_str());
						}
					}
					else if (read_ok)
					{
						success = true;
					}
				}
			}
			WinHttpCloseHandle(request);
		}
		WinHttpCloseHandle(connect);
	}
	WinHttpCloseHandle(session);
	return success;
}

bool UpdaterHttpGetString(const std::string& url, std::string& out_body)
{
	out_body.clear();
	return HttpFetchData(url, &out_body, NULL, NULL, 0, false);
}

bool UpdaterHttpDownloadToFile(const std::string& url, const std::string& dest_path)
{
	std::vector<uint8_t> data;
	if (!HttpFetchData(url, NULL, &data, NULL, 0, false) || data.empty())
		return false;

	FILE* f = fopen(dest_path.c_str(), "wb");
	if (!f) return false;
	size_t written = fwrite(data.data(), 1, data.size(), f);
	fclose(f);
	return written == data.size();
}

// Background Worker Thread
static DWORD WINAPI UpdaterThreadProc(LPVOID)
{
	while (InterlockedCompareExchange(&g_updater_running, 0, 0))
	{
		WaitForSingleObject(g_updater_event, 100);

		bool do_check = false;
		bool do_download = false;
		bool manual = false;

		EnterCriticalSection(&g_updater_lock);
		if (g_command_check)
		{
			do_check = true;
			g_command_check = false;
			manual = g_manual_check;
		}
		if (g_command_download)
		{
			do_download = true;
			g_command_download = false;
		}
		LeaveCriticalSection(&g_updater_lock);

		if (do_check)
		{
			EnterCriticalSection(&g_updater_lock);
			g_state = UPDATER_STATE_CHECKING;
			g_status_msg = "Consultando servidor...";
			LeaveCriticalSection(&g_updater_lock);

			std::string json_body;
			bool ok = HttpFetchData(UPDATE_MANIFEST_URL, &json_body, NULL, NULL, 0, false);
			if (!ok)
			{
				// Fallback to GitHub raw
				ok = HttpFetchData(UPDATE_FALLBACK_URL, &json_body, NULL, NULL, 0, false);
			}

			if (ok)
			{
				UpdateInfo parsed_info;
				if (ParseManifestJson(json_body, parsed_info))
				{
					EnterCriticalSection(&g_updater_lock);
					g_info = parsed_info;
					if (UpdaterIsNewerVersion(APP_VERSION, parsed_info.version))
					{
						g_state = UPDATER_STATE_AVAILABLE;
						g_status_msg = "Nova versao " + parsed_info.version + " disponivel!";
					}
					else
					{
						g_state = UPDATER_STATE_UP_TO_DATE;
						g_status_msg = "Voce ja esta na versao mais recente (" APP_VERSION ")";
					}
					LeaveCriticalSection(&g_updater_lock);
				}
				else
				{
					EnterCriticalSection(&g_updater_lock);
					g_state = UPDATER_STATE_ERROR;
					g_status_msg = "Manifesto de versao corrompido.";
					LeaveCriticalSection(&g_updater_lock);
				}
			}
			else
			{
				EnterCriticalSection(&g_updater_lock);
				g_state = UPDATER_STATE_ERROR;
				g_status_msg = "Falha ao conectar com o servidor.";
				LeaveCriticalSection(&g_updater_lock);
			}
		}

		if (do_download)
		{
			EnterCriticalSection(&g_updater_lock);
			g_state = UPDATER_STATE_DOWNLOADING;
			g_progress = 0;
			g_status_msg = "Baixando nova versao...";
			std::string dl_url = g_info.exe_url;
			std::string expected_sha256 = g_info.exe_sha256;
			size_t expected_size = g_info.exe_size;
			LeaveCriticalSection(&g_updater_lock);

			if (dl_url.empty())
			{
				dl_url = "https://mister4all.com/downloads/MiSTer_4_ALL.exe";
			}

			size_t total_size = 0;
			bool dl_ok = HttpFetchData(dl_url, NULL, NULL, &total_size, expected_size, true);

			// The manifest's exe_sha256 was parsed but never checked against
			// what actually landed on disk - a MITM'd plain-HTTP fallback
			// manifest, or a compromised/misconfigured server, could point
			// exe_url at a tampered binary and this would apply it purely
			// because a file existed. Verify whenever the manifest supplied a
			// hash to check against.
			std::string new_exe_path;
			if (dl_ok && !expected_sha256.empty())
			{
				new_exe_path = GetExecutableDirectory() + "\\MiSTer_4_ALL.new";
				std::string actual_sha256 = Sha256File(new_exe_path);

				std::string expected_lower = expected_sha256, actual_lower = actual_sha256;
				std::transform(expected_lower.begin(), expected_lower.end(), expected_lower.begin(), ::tolower);
				std::transform(actual_lower.begin(), actual_lower.end(), actual_lower.begin(), ::tolower);

				if (actual_lower.empty() || actual_lower != expected_lower)
				{
					dl_ok = false;
					remove(new_exe_path.c_str());
				}
			}

			EnterCriticalSection(&g_updater_lock);
			if (dl_ok)
			{
				g_state = UPDATER_STATE_READY;
				g_progress = 100;
				g_status_msg = "Download concluido com sucesso!";
			}
			else
			{
				g_state = UPDATER_STATE_ERROR;
				g_status_msg = "Erro ao baixar arquivo executavel.";
			}
			LeaveCriticalSection(&g_updater_lock);
		}
	}
	return 0;
}

void UpdaterInit()
{
	InitializeCriticalSection(&g_updater_lock);

	// Clean up any leftover temporary files from a previous update
	std::string app_dir = GetExecutableDirectory();
	std::string old_bat = app_dir + "\\_update_apply.bat";
	std::string old_exe = app_dir + "\\MiSTer_4_ALL.exe.old";

	if (fs::exists(old_bat)) fs::remove(old_bat);
	if (fs::exists(old_exe)) fs::remove(old_exe);

	g_updater_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_updater_running = 1;
	g_updater_thread = CreateThread(NULL, 0, UpdaterThreadProc, NULL, 0, NULL);
}

void UpdaterShutdown()
{
	if (g_updater_running)
	{
		InterlockedExchange(&g_updater_running, 0);
		bool thread_exited = true;
		if (g_updater_event)
		{
			SetEvent(g_updater_event);
			// HttpFetchData sets a 30s WinHTTP send/receive timeout, so the
			// worker can legitimately still be inside a single WinHTTP call
			// (about to EnterCriticalSection(&g_updater_lock)) well past a
			// short wait. Cover that worst case rather than guessing low.
			thread_exited = (WaitForSingleObject(g_updater_thread, 31000) != WAIT_TIMEOUT);
			CloseHandle(g_updater_event);
			g_updater_event = NULL;
		}
		if (g_updater_thread)
		{
			CloseHandle(g_updater_thread);
			g_updater_thread = NULL;
		}
		// Deleting a CRITICAL_SECTION a still-running thread might enter is
		// undefined behavior. If the wait above timed out, leak it instead -
		// the process is exiting either way, and the OS reclaims it; the
		// alternative is a crash or hang during shutdown.
		if (thread_exited) DeleteCriticalSection(&g_updater_lock);
	}
}

void UpdaterCheckAsync(bool manual_trigger)
{
	EnterCriticalSection(&g_updater_lock);
	g_command_check = true;
	g_manual_check = manual_trigger;
	LeaveCriticalSection(&g_updater_lock);

	if (g_updater_event) SetEvent(g_updater_event);
}

void UpdaterStartDownload()
{
	EnterCriticalSection(&g_updater_lock);
	g_command_download = true;
	LeaveCriticalSection(&g_updater_lock);

	if (g_updater_event) SetEvent(g_updater_event);
}

bool UpdaterApplyAndRestart()
{
	std::string app_dir = GetExecutableDirectory();
	std::string target_exe = GetCurrentExecutablePath();
	std::string new_exe = app_dir + "\\MiSTer_4_ALL.new";

	if (!fs::exists(new_exe))
	{
		return false;
	}

	// Write self-replacing trampoline batch
	std::string bat_path = app_dir + "\\_update_apply.bat";
	FILE* f = fopen(bat_path.c_str(), "w");
	if (!f) return false;

	std::string old_exe = target_exe + ".old";

	fprintf(f, "@echo off\r\n");
	fprintf(f, "timeout /t 1 /nobreak >nul\r\n");
	// Rename (not copy) the live exe out of the way first, ONCE, outside the
	// retry loop below - a rename is a near-instant metadata change, not a
	// byte-by-byte overwrite, so a crash or power loss right after it leaves
	// the ORIGINAL exe intact and recoverable at "%s.old" instead of target_exe
	// itself ending up half-overwritten with no working copy anywhere.
	// Retrying this same del+ren every loop (the old structure) would delete
	// that backup again on iteration 2 while target_exe was already gone
	// (renamed on iteration 1), destroying the one safety net this exists
	// for the moment a single copy attempt failed.
	fprintf(f, ":retry_rename\r\n");
	fprintf(f, "if not exist \"%s\" goto renamed\r\n", target_exe.c_str());
	fprintf(f, "del /f /q \"%s\" >nul 2>&1\r\n", old_exe.c_str());
	fprintf(f, "ren \"%s\" \"%s\" >nul 2>&1\r\n", target_exe.c_str(),
		(fs::path(old_exe).filename().string()).c_str());
	fprintf(f, "if exist \"%s\" (\r\n", target_exe.c_str());
	fprintf(f, "    timeout /t 1 /nobreak >nul\r\n");
	fprintf(f, "    goto retry_rename\r\n");
	fprintf(f, ")\r\n");
	fprintf(f, ":renamed\r\n");
	fprintf(f, ":retry_copy\r\n");
	fprintf(f, "copy /y \"%s\" \"%s\" >nul 2>&1\r\n", new_exe.c_str(), target_exe.c_str());
	fprintf(f, "if errorlevel 1 (\r\n");
	fprintf(f, "    timeout /t 1 /nobreak >nul\r\n");
	fprintf(f, "    goto retry_copy\r\n");
	fprintf(f, ")\r\n");
	fprintf(f, "del /f /q \"%s\" >nul 2>&1\r\n", new_exe.c_str());
	fprintf(f, "start \"\" \"%s\"\r\n", target_exe.c_str());
	fprintf(f, "del \"%%~f0\" >nul 2>&1\r\n");
	fclose(f);

	// Launch trampoline silently and exit
	HINSTANCE res = ShellExecuteA(NULL, "open", bat_path.c_str(), NULL, app_dir.c_str(), SW_HIDE);
	if ((INT_PTR)res > 32)
	{
		ExitProcess(0);
		return true;
	}
	return false;
}

UpdaterState UpdaterGetState()
{
	EnterCriticalSection(&g_updater_lock);
	UpdaterState s = g_state;
	LeaveCriticalSection(&g_updater_lock);
	return s;
}

int UpdaterGetProgress()
{
	EnterCriticalSection(&g_updater_lock);
	int p = g_progress;
	LeaveCriticalSection(&g_updater_lock);
	return p;
}

UpdateInfo UpdaterGetInfo()
{
	// Copy while the lock is held and return the copy, not a reference/pointer
	// into the live global - the previous version released the lock and then
	// handed back exactly that, so the lock protected nothing at all: a
	// caller reading info.version/info.notes could still race
	// UpdaterThreadProc's "g_info = parsed_info;" and see a torn std::string.
	EnterCriticalSection(&g_updater_lock);
	UpdateInfo info = g_info;
	LeaveCriticalSection(&g_updater_lock);
	return info;
}

std::string UpdaterGetStatusMessage()
{
	// Same fix as UpdaterGetInfo() above: the old const char* pointed straight
	// into g_status_msg's buffer after the lock was already released, and a
	// concurrent reassignment of g_status_msg could free that buffer out from
	// under the caller. Returning a copy closes that window.
	EnterCriticalSection(&g_updater_lock);
	std::string msg = g_status_msg;
	LeaveCriticalSection(&g_updater_lock);
	return msg;
}

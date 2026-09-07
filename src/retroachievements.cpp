#define WIN32_LEAN_AND_MEAN
#include <stdarg.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <deque>
#include <vector>
#include <filesystem>

#include "retroachievements.h"
#include "core_runner.h"
#include "chd_reader.h"

extern "C" {
#include "libretro.h"
#include "rc_client.h"
#include "rc_libretro.h"
#include "rc_consoles.h"
}

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

static const char* RA_CONFIG_PATH = "Config/retroachievements.cfg";

static rc_client_t* g_client = NULL;
// rc_client is documented as single-threaded, but RaGetAchievements() is
// called from the UI thread (menu.cpp's Achievements page) while
// RaDoFrame()/RaOnGameLoad()/RaOnGameUnload() drive the same g_client from
// the core thread every frame - a real use-after-free if the UI thread walks
// achievement structures the core thread frees via rc_client_unload_game()
// at the same moment. This lock only needs to wrap the handful of entry
// points a *different* thread than the core thread can reach
// (RaGetAchievements from the UI thread, RaShutdown from the main thread at
// exit) plus the core-thread entry points they race against - nested calls
// rc_client makes back into this file (event callbacks, RaRefreshCounts)
// happen on the same thread that already holds it, and CRITICAL_SECTION is
// recursive, so that stays safe.
static CRITICAL_SECTION g_client_lock;
struct RaClientLockInit { RaClientLockInit() { InitializeCriticalSection(&g_client_lock); } };
static RaClientLockInit g_ra_client_lock_init;
static bool         g_enabled = false;
static bool         g_logged_in = false;
// Written from rcheevos callbacks on the core thread, read by the OSD on the
// UI thread. A std::string here is the same trap that crashed the toast: the
// reader can hold c_str() while the writer reallocates.
static CRITICAL_SECTION g_status_lock;
struct RaStatusLockInit { RaStatusLockInit() { InitializeCriticalSection(&g_status_lock); } };
static RaStatusLockInit g_ra_status_lock_init;

static char g_status[192] = "RetroAchievements: desativado";
static char g_status_readback[192] = "";

static void SetStatus(const char* text)
{
	if (!text) return;
	EnterCriticalSection(&g_status_lock);
	strncpy_s(g_status, sizeof(g_status), text, _TRUNCATE);
	LeaveCriticalSection(&g_status_lock);
}
static std::string  g_username;
static std::string  g_password;
static std::string  g_token;
static bool         g_hardcore = false;

// g_username itself is only ever touched on the core thread (config load at
// RaInit time, or RaLoginCallback replaying an HTTP completion) - same as
// g_status used to be before the fix above. RaGetUserName() is read from the
// UI thread (menu.cpp's Achievements settings page), so it needs the same
// lock-protected fixed-buffer mirror g_status already got, not a raw
// std::string::c_str() a UI-thread copy could catch mid-reallocation.
static CRITICAL_SECTION g_username_lock;
struct RaUsernameLockInit { RaUsernameLockInit() { InitializeCriticalSection(&g_username_lock); } };
static RaUsernameLockInit g_ra_username_lock_init;
static char g_username_mirror[64] = "";
static char g_username_readback[64] = "";

static void SyncUsernameMirror()
{
	EnterCriticalSection(&g_username_lock);
	strncpy_s(g_username_mirror, sizeof(g_username_mirror), g_username.c_str(), _TRUNCATE);
	LeaveCriticalSection(&g_username_lock);
}

static wchar_t g_user_agent[256] = L"";

static rc_libretro_memory_regions_t g_memory_regions;
static bool g_memory_ready = false;

static int g_ach_total = 0;
static int g_challenge_active = 0;
static uint32_t g_guessed_console = 0;
static bool g_server_pending = false;
static int g_ach_unlocked = 0;

// -------------------------------------------------------------
// Config
// -------------------------------------------------------------
static void RaLoadConfig()
{
	FILE* f = fopen(RA_CONFIG_PATH, "rb");
	if (!f) return;

	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		if (line[0] == '#') continue;
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';

		char* val = eq + 1;
		size_t n = strlen(val);
		while (n > 0 && (val[n - 1] == '\n' || val[n - 1] == '\r')) val[--n] = '\0';

		if (!strcmp(line, "username")) { g_username = val; SyncUsernameMirror(); }
		else if (!strcmp(line, "password")) g_password = val;
		else if (!strcmp(line, "token")) g_token = val;
		else if (!strcmp(line, "hardcore")) g_hardcore = (atoi(val) != 0);
	}
	fclose(f);
}

static void RaSaveConfig()
{
	std::error_code ec;
	fs::create_directories("Config", ec);

	FILE* f = fopen(RA_CONFIG_PATH, "wb");
	if (!f) return;

	fprintf(f,
		"# RetroAchievements - https://retroachievements.org\n"
		"#\n"
		"# Preencha username e password UMA vez. No primeiro login bem sucedido\n"
		"# o token e gravado aqui e a senha e apagada deste arquivo.\n"
		"#\n"
		"# hardcore=1 desativa savestates (regra do site). Comece com 0.\n"
		"username=%s\n"
		"password=%s\n"
		"token=%s\n"
		"hardcore=%d\n",
		g_username.c_str(), g_password.c_str(), g_token.c_str(), g_hardcore ? 1 : 0);
	fclose(f);
}

// -------------------------------------------------------------
// HTTP (WinHTTP on a worker thread)
//
// rc_client is single threaded by contract, so the worker only performs the
// transfer. Completions are queued and replayed on the core thread.
// -------------------------------------------------------------
struct HttpJob
{
	std::string url;
	std::string post_data;
	std::string content_type;
	rc_client_server_callback_t callback;
	void* callback_data;

	// filled by the worker
	std::string body;
	int status;
};

static CRITICAL_SECTION g_http_lock;
static HANDLE           g_http_event = NULL;
static HANDLE           g_http_thread = NULL;
static volatile LONG    g_http_running = 0;
static std::deque<HttpJob*> g_http_pending;
static std::deque<HttpJob*> g_http_done;

static void HttpPerform(HttpJob* job)
{
	job->status = 0;

	wchar_t wurl[2048];
	int wlen = MultiByteToWideChar(CP_UTF8, 0, job->url.c_str(), -1, wurl, 2048);
	if (wlen <= 0) return;

	URL_COMPONENTS uc = { 0 };
	wchar_t host[256] = { 0 };
	wchar_t path[1536] = { 0 };
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = host;  uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;   uc.dwUrlPathLength = 1536;

	if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return;

	// RetroAchievements identifies integrations by User-Agent, and rcheevos
	// supplies the clause naming itself and its version. Sending only our own
	// name left the client unidentifiable on their side.
	HINTERNET session = WinHttpOpen(g_user_agent[0] ? g_user_agent : L"MiSTer4ALL/2.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return;

	WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

	HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
	if (connect)
	{
		DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
		const wchar_t* verb = job->post_data.empty() ? L"GET" : L"POST";

		HINTERNET request = WinHttpOpenRequest(connect, verb, path, NULL,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

		if (request)
		{
			std::wstring headers;
			if (!job->post_data.empty())
			{
				const char* ct = job->content_type.empty()
					? "application/x-www-form-urlencoded" : job->content_type.c_str();
				wchar_t wct[256];
				MultiByteToWideChar(CP_UTF8, 0, ct, -1, wct, 256);
				headers = std::wstring(L"Content-Type: ") + wct;
			}

			BOOL ok = WinHttpSendRequest(request,
				headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
				headers.empty() ? 0 : (DWORD)-1L,
				job->post_data.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)job->post_data.data(),
				(DWORD)job->post_data.size(), (DWORD)job->post_data.size(), 0);

			if (ok && WinHttpReceiveResponse(request, NULL))
			{
				DWORD code = 0, codeLen = sizeof(code);
				WinHttpQueryHeaders(request,
					WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeLen, WINHTTP_NO_HEADER_INDEX);
				job->status = (int)code;

				DWORD avail = 0;
				while (WinHttpQueryDataAvailable(request, &avail) && avail > 0)
				{
					std::vector<char> chunk(avail);
					DWORD read = 0;
					if (!WinHttpReadData(request, chunk.data(), avail, &read) || read == 0) break;
					job->body.append(chunk.data(), read);
				}
			}
			WinHttpCloseHandle(request);
		}
		WinHttpCloseHandle(connect);
	}
	WinHttpCloseHandle(session);
}

static DWORD WINAPI HttpThreadProc(LPVOID)
{
	while (InterlockedCompareExchange(&g_http_running, 0, 0))
	{
		WaitForSingleObject(g_http_event, 200);

		for (;;)
		{
			HttpJob* job = NULL;

			EnterCriticalSection(&g_http_lock);
			if (!g_http_pending.empty())
			{
				job = g_http_pending.front();
				g_http_pending.pop_front();
			}
			LeaveCriticalSection(&g_http_lock);

			if (!job) break;

			HttpPerform(job);

			EnterCriticalSection(&g_http_lock);
			g_http_done.push_back(job);
			LeaveCriticalSection(&g_http_lock);
		}
	}
	return 0;
}

static void RC_CCONV RaServerCall(const rc_api_request_t* request,
	rc_client_server_callback_t callback, void* callback_data, rc_client_t* client)
{
	(void)client;

	HttpJob* job = new HttpJob();
	job->url = request->url ? request->url : "";
	job->post_data = request->post_data ? request->post_data : "";
	job->content_type = request->content_type ? request->content_type : "";
	job->callback = callback;
	job->callback_data = callback_data;
	job->status = 0;

	EnterCriticalSection(&g_http_lock);
	g_http_pending.push_back(job);
	LeaveCriticalSection(&g_http_lock);

	if (g_http_event) SetEvent(g_http_event);
}

// Core thread: hand finished transfers back to rcheevos.
static void RaPumpHttp()
{
	for (;;)
	{
		HttpJob* job = NULL;

		EnterCriticalSection(&g_http_lock);
		if (!g_http_done.empty())
		{
			job = g_http_done.front();
			g_http_done.pop_front();
		}
		LeaveCriticalSection(&g_http_lock);

		if (!job) break;

		rc_api_server_response_t response;
		memset(&response, 0, sizeof(response));
		response.body = job->body.c_str();
		response.body_length = job->body.size();
		response.http_status_code = job->status;

		if (job->callback) job->callback(&response, job->callback_data);
		delete job;
	}
}

// -------------------------------------------------------------
// Memory access
// -------------------------------------------------------------
static void RC_CCONV RaGetCoreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info);

// rc_client validates every achievement's addresses while it loads the game,
// which happens BEFORE our load callback runs. Initialising the memory map only
// in that callback meant reads returned nothing during validation and rcheevos
// disabled dozens of achievements with "Invalid address". So we have to guess
// the console from the core we are running and map memory up front.
static uint32_t ConsoleIdFromCoreName(const char* core_name)
{
	if (!core_name) return RC_CONSOLE_UNKNOWN;

	if (strstr(core_name, "NeoGeo CD"))     return RC_CONSOLE_NEO_GEO_CD;
	if (strstr(core_name, "NeoGeo"))        return RC_CONSOLE_ARCADE;
	if (strstr(core_name, "SNES"))          return RC_CONSOLE_SUPER_NINTENDO;
	if (strstr(core_name, "NES"))           return RC_CONSOLE_NINTENDO;
	if (strstr(core_name, "Genesis"))       return RC_CONSOLE_MEGA_DRIVE;
	if (strstr(core_name, "Master System")) return RC_CONSOLE_MASTER_SYSTEM;
	if (strstr(core_name, "TurboGrafx"))    return RC_CONSOLE_PC_ENGINE;
	if (strstr(core_name, "Atari 2600"))    return RC_CONSOLE_ATARI_2600;
	if (strstr(core_name, "PlayStation"))   return RC_CONSOLE_PLAYSTATION;
	if (strstr(core_name, "Saturn"))        return RC_CONSOLE_SATURN;
	if (strstr(core_name, "Nintendo 64"))   return RC_CONSOLE_NINTENDO_64;

	return RC_CONSOLE_UNKNOWN;
}

static bool InitMemoryForConsole(uint32_t console_id, const char* why)
{
	if (console_id == RC_CONSOLE_UNKNOWN) return false;

	// fMSX does not implement RETRO_ENVIRONMENT_GET_MEMORY_MAPS (confirmed by
	// "mapa=inferido" in every MSX [MEM] log line), so rc_libretro has to guess
	// the whole region from a single CoreGetMemoryData()/Size() pair. That
	// guess does not match fMSX's real layout: every MSX ROM the server
	// actually recognised (as opposed to "jogo fora do banco de dados", which
	// never reaches this function - see g_guessed_console staying UNKNOWN for
	// MSX in ConsoleIdFromCoreName) went on to either crash retro_run() deep
	// in msx.dll or render visibly corrupted sprites once achievement address
	// validation started reading through the inferred region. Until that
	// mapping is fixed, achievement memory tracking has to stay off for MSX -
	// a wrong reward system is a much smaller loss than the game itself.
	if (console_id == RC_CONSOLE_MSX)
	{
		FILE* lf = fopen("mister4all.log", "a");
		if (lf)
		{
			fprintf(lf, "[INFO] [MEM] %s: console=MSX mapa=inferido recusado (layout incompativel conhecido)\n", why);
			fclose(lf);
		}
		return false;
	}

	rc_libretro_memory_destroy(&g_memory_regions);

	const struct retro_memory_map* mmap =
		(const struct retro_memory_map*)CoreGetMemoryMap();

	bool ok = (rc_libretro_memory_init(&g_memory_regions, mmap,
		RaGetCoreMemoryInfo, console_id) != 0);

	FILE* lf = fopen("mister4all.log", "a");
	if (lf)
	{
		fprintf(lf, "[INFO] [MEM] %s: console=%u mapa=%s regioes=%u total=%u bytes%s\n",
			why, console_id, mmap ? "do core" : "inferido",
			g_memory_regions.count, (unsigned)g_memory_regions.total_size,
			ok ? "" : " FALHOU");
		fclose(lf);
	}
	return ok;
}

static void RC_CCONV RaGetCoreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info)
{
	info->data = (unsigned char*)CoreGetMemoryData(id);
	info->size = CoreGetMemorySize(id);
}

static uint32_t RC_CCONV RaReadMemory(uint32_t address, uint8_t* buffer,
	uint32_t num_bytes, rc_client_t* client)
{
	(void)client;
	if (!g_memory_ready) return 0;
	return rc_libretro_memory_read(&g_memory_regions, address, buffer, num_bytes);
}

// -------------------------------------------------------------
// Events and callbacks
// -------------------------------------------------------------
static void RaRefreshCounts()
{
	g_ach_total = 0;
	g_ach_unlocked = 0;
	if (!g_client) return;

	// Walking the achievement list by hand overcounted by exactly one on every
	// game. When RetroAchievements does not recognise the client it injects a
	// synthetic "Warning: Unknown Emulator" entry that arrives already
	// unlocked, so a game with 104 achievements that had never been played
	// reported 1/105. This summary is the figure the server itself stands
	// behind, and is what the "X of Y" message is meant to be built from.
	rc_client_user_game_summary_t s;
	memset(&s, 0, sizeof(s));
	rc_client_get_user_game_summary(g_client, &s);

	g_ach_total = (int)(s.num_core_achievements + s.num_unofficial_achievements);
	g_ach_unlocked = (int)s.num_unlocked_achievements;
	if (g_ach_unlocked > g_ach_total) g_ach_unlocked = g_ach_total;
}

// rcheevos had no voice at all before this. A login that fails server-side now
// says why in the log instead of just toasting "LOGIN FALHOU".
static void RC_CCONV RaClientLog(const char* message, const rc_client_t* client)
{
	(void)client;
	FILE* f = fopen("mister4all.log", "a");
	if (f)
	{
		fprintf(f, "[INFO] [RA] %s\n", message ? message : "");
		fclose(f);
	}
}

static void RC_CCONV RaEventHandler(const rc_client_event_t* event, rc_client_t* client)
{
	(void)client;

	switch (event->type)
	{
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
	{
		char msg[160];
		snprintf(msg, sizeof(msg), "CONQUISTA: %s",
			event->achievement && event->achievement->title ? event->achievement->title : "?");
		CoreSetToast(msg, 300);
		RaRefreshCounts();
		break;
	}
	case RC_CLIENT_EVENT_GAME_COMPLETED:
		CoreSetToast("JOGO 100% COMPLETO!", 420);
		break;

	case RC_CLIENT_EVENT_SUBSET_COMPLETED:
		CoreSetToast("CONJUNTO COMPLETO!", 360);
		break;

	case RC_CLIENT_EVENT_RESET:
		// Not cosmetic: rcheevos requires the machine to be reset when hardcore
		// is turned on mid-session, otherwise the run is not valid. Ignoring
		// this left the client and the emulator disagreeing about the state.
		CoreSetToast("RA: RESET EXIGIDO (HARDCORE)", 240);
		CoreReset();
		break;

	case RC_CLIENT_EVENT_DISCONNECTED:
		// Unlocks are queued locally and have not reached the server. The
		// player needs to know, or they will think a hard-won achievement was
		// registered when it was not.
		g_server_pending = true;
		SetStatus("RA: sem conexao - desbloqueios pendentes");
		CoreSetToast("RA: SEM CONEXAO (PENDENTES)", 300);
		break;

	case RC_CLIENT_EVENT_RECONNECTED:
		g_server_pending = false;
		SetStatus("RA: reconectado, pendencias enviadas");
		CoreSetToast("RA: RECONECTADO", 180);
		break;

	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		g_challenge_active++;
		break;

	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		if (g_challenge_active > 0) g_challenge_active--;
		break;

	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
		if (event->achievement && event->achievement->measured_progress[0])
		{
			char msg[160];
			snprintf(msg, sizeof(msg), "%s  %s",
				event->achievement->title ? event->achievement->title : "?",
				event->achievement->measured_progress);
			CoreSetToast(msg, 120);
		}
		break;

	case RC_CLIENT_EVENT_SERVER_ERROR:
		CoreSetToast("RETROACHIEVEMENTS: ERRO NO SERVIDOR", 240);
		if (event->server_error && event->server_error->error_message)
		{
			char buf[192];
			snprintf(buf, sizeof(buf), "RA: %s", event->server_error->error_message);
			SetStatus(buf);
		}
		break;

	// Leaderboards are phase 3 and deliberately unhandled; listing them here
	// keeps them from looking forgotten.
	case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
	case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
	case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
	case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD:
	default:
		break;
	}
}

static void RC_CCONV RaLoginCallback(int result, const char* error_message,
	rc_client_t* client, void* userdata)
{
	(void)client; (void)userdata;

	if (result != RC_OK)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "RA: falha no login (%s)",
			error_message ? error_message : "erro desconhecido");
		SetStatus(buf);
		CoreSetToast("RETROACHIEVEMENTS: LOGIN FALHOU", 300);
		return;
	}

	g_logged_in = true;

	const rc_client_user_t* user = rc_client_get_user_info(g_client);
	if (user)
	{
		g_username = user->username ? user->username : g_username;
		SyncUsernameMirror();

		// Store the token so the password never has to be kept on disk.
		if (user->token && *user->token)
		{
			g_token = user->token;
			g_password.clear();
			RaSaveConfig();
		}

		char buf[256];
		snprintf(buf, sizeof(buf), "RA: %s (%u pontos)", g_username.c_str(), user->score);
		SetStatus(buf);
	}
	else
	{
		SetStatus("RA: conectado");
	}

	CoreSetToast("RETROACHIEVEMENTS CONECTADO", 240);
}

static void RC_CCONV RaLoadGameCallback(int result, const char* error_message,
	rc_client_t* client, void* userdata)
{
	(void)client; (void)userdata;

	if (result != RC_OK)
	{
		// Every one of these used to be silent. The status string was set and
		// nothing displayed it, so the player had no way to tell an unsupported
		// game apart from a broken integration.
		if (result == RC_NO_GAME_LOADED)
		{
			// Normal for translations, hacks and repacks: identification is by
			// hash of the original content.
			SetStatus("RA: jogo fora do banco de dados");
			CoreSetToast("SEM CONQUISTAS: JOGO NAO RECONHECIDO", 240);
		}
		else
		{
			char buf[192];
			snprintf(buf, sizeof(buf), "RA: %s",
				error_message ? error_message : "falha ao carregar");
			SetStatus(buf);

			char toast[192];
			snprintf(toast, sizeof(toast), "RA: %s",
				error_message ? error_message : "FALHA AO CONSULTAR O JOGO");
			CoreSetToast(toast, 300);
		}
		return;
	}

	const rc_client_game_t* game = rc_client_get_game_info(g_client);
	if (!game) return;

	// The server is authoritative about which console this is. Re-map only if
	// it disagrees with the guess we made from the core name.
	if (game->console_id != g_guessed_console)
		g_memory_ready = InitMemoryForConsole(game->console_id, "pos-load");

	RaRefreshCounts();

	char buf[256];
	snprintf(buf, sizeof(buf), "RA: %s (%d/%d)",
		game->title ? game->title : "?", g_ach_unlocked, g_ach_total);
	SetStatus(buf);

	char toast[192];
	if (g_ach_total > 0)
	{
		snprintf(toast, sizeof(toast), "CONQUISTAS: %d/%d - %s",
			g_ach_unlocked, g_ach_total, game->title ? game->title : "?");

		// The memory map is what makes unlocks possible. A core that does not
		// export retro_get_memory_data (the bundled n64.dll, for one) gets
		// identified and then silently never unlocks anything.
		if (!g_memory_ready)
		{
			SetStatus("RA: core nao expoe memoria - sem conquistas");
			CoreSetToast("RA: ESTE CORE NAO SUPORTA CONQUISTAS", 300);
			return;
		}
	}
	else
	{
		// Recognised, but nobody has authored a set for it yet.
		snprintf(toast, sizeof(toast), "RA: %s SEM CONQUISTAS PUBLICADAS",
			game->title ? game->title : "JOGO");
	}

	CoreSetToast(toast, 240);
}

// -------------------------------------------------------------
// Public API
// -------------------------------------------------------------
void RaInit()
{
	InitializeCriticalSection(&g_http_lock);

	// Must happen before any hash is attempted.
	ChdReaderInstall();

	RaLoadConfig();

	if (g_username.empty() || (g_password.empty() && g_token.empty()))
	{
		// Nothing configured: write a commented template so the user can see
		// exactly what to fill in, and stay out of the way.
		if (!fs::exists(RA_CONFIG_PATH)) RaSaveConfig();
		SetStatus("RA: sem credenciais em Config/retroachievements.cfg");
		return;
	}

	g_http_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InterlockedExchange(&g_http_running, 1);
	g_http_thread = CreateThread(NULL, 0, HttpThreadProc, NULL, 0, NULL);

	g_client = rc_client_create(RaReadMemory, RaServerCall);
	if (!g_client)
	{
		SetStatus("RA: falha ao criar o cliente");
		return;
	}

	rc_client_set_event_handler(g_client, RaEventHandler);
	rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, RaClientLog);

	{
		// "MiSTer4ALL/2.0 rcheevos/12.4.0" - the clause is what RA reads.
		char clause[128] = { 0 };
		rc_client_get_user_agent_clause(g_client, clause, sizeof(clause));

		char agent[256];
		snprintf(agent, sizeof(agent), "MiSTer4ALL/2.0 %s", clause);
		MultiByteToWideChar(CP_UTF8, 0, agent, -1, g_user_agent, 256);
	}
	rc_client_set_hardcore_enabled(g_client, g_hardcore ? 1 : 0);

	g_enabled = true;
	SetStatus("RA: conectando...");

	if (!g_token.empty())
		rc_client_begin_login_with_token(g_client, g_username.c_str(), g_token.c_str(),
			RaLoginCallback, NULL);
	else
		rc_client_begin_login_with_password(g_client, g_username.c_str(), g_password.c_str(),
			RaLoginCallback, NULL);
}

void RaShutdown()
{
	// Stop the HTTP worker before touching g_client/the job queues it can
	// still be pushing into - RaServerCall's WinHttpSetTimeouts allows up to
	// 30s for a single send/receive, so a 5s wait routinely returned before
	// the thread actually stopped. It never deletes anything the thread
	// still touches (g_http_lock is never explicitly deleted, only leaked at
	// process exit like the OS reclaims everything else), but letting the
	// thread outlive this function risks it still writing into
	// g_http_pending/g_http_done while those deques are torn down by CRT
	// static destruction as the process actually exits.
	if (InterlockedExchange(&g_http_running, 0))
	{
		if (g_http_event) SetEvent(g_http_event);
		if (g_http_thread)
		{
			WaitForSingleObject(g_http_thread, 31000);
			CloseHandle(g_http_thread);
			g_http_thread = NULL;
		}
	}

	if (g_http_event) { CloseHandle(g_http_event); g_http_event = NULL; }

	// The worker thread has fully stopped by this point (waited above), so
	// nothing else can still be pushing into these deques - safe to drain
	// and free whatever job was in flight or completed-but-unconsumed.
	for (HttpJob* job : g_http_pending) delete job;
	g_http_pending.clear();
	for (HttpJob* job : g_http_done) delete job;
	g_http_done.clear();

	EnterCriticalSection(&g_client_lock);
	if (g_client)
	{
		rc_client_destroy(g_client);
		g_client = NULL;
	}
	LeaveCriticalSection(&g_client_lock);

	rc_libretro_memory_destroy(&g_memory_regions);
	g_memory_ready = false;
	g_enabled = false;
	g_logged_in = false;
}

void RaDoFrame()
{
	if (!g_enabled || !g_client) return;

	EnterCriticalSection(&g_client_lock);
	RaPumpHttp();

	// InitMemoryForConsole() only refuses the "inferred memory map" mechanism
	// for MSX, the one console it was confirmed to crash on - every other
	// console_id still trusts rc_libretro's inference to line up with what
	// the core actually exposes. A future core/console combination that
	// mismatches the same way would otherwise fault here, on the core
	// thread, with nothing between it and the top-level crash handler -
	// which can't even contain it (the fault address is inside our own exe,
	// not a core DLL, so CrashHandler's "kill just this thread" branch does
	// not apply). Catching it here turns a bad map into "achievements stop
	// for this session" instead of taking the whole app down.
	__try
	{
		if (g_memory_ready)
			rc_client_do_frame(g_client);
		else
			rc_client_idle(g_client);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		g_memory_ready = false;
		g_enabled = false;
		FILE* lf = fopen("mister4all.log", "a");
		if (lf)
		{
			fprintf(lf, "[ERROR] [RA] rc_client_do_frame excecao - RetroAchievements desativado nesta sessao\n");
			fclose(lf);
		}
	}
	LeaveCriticalSection(&g_client_lock);
}

void RaOnGameLoad(const char* rom_path, const char* core_name)
{
	if (!g_enabled || !g_client || !rom_path) return;

	g_ach_total = 0;
	g_ach_unlocked = 0;
	SetStatus("RA: identificando o jogo...");

	// Map memory now, from the core we are running. rc_client checks every
	// achievement's addresses during the load, long before it calls us back,
	// and anything it cannot read at that moment gets permanently disabled.
	g_guessed_console = ConsoleIdFromCoreName(core_name);
	g_memory_ready = InitMemoryForConsole(g_guessed_console, "pre-load");

	// console_id 0 lets rcheevos try every hashing rule it knows, which is what
	// we want since one core can serve several systems.
	EnterCriticalSection(&g_client_lock);
	rc_client_begin_identify_and_load_game(g_client, RC_CONSOLE_UNKNOWN,
		rom_path, NULL, 0, RaLoadGameCallback, NULL);
	LeaveCriticalSection(&g_client_lock);
}

void RaOnGameUnload()
{
	if (!g_enabled || !g_client) return;

	EnterCriticalSection(&g_client_lock);
	rc_client_unload_game(g_client);
	LeaveCriticalSection(&g_client_lock);
	rc_libretro_memory_destroy(&g_memory_regions);
	g_memory_ready = false;
	g_ach_total = 0;
	g_ach_unlocked = 0;
}

bool        RaIsEnabled() { return g_enabled; }
bool        RaIsLoggedIn() { return g_logged_in; }
bool        RaIsHardcoreActive() { return g_enabled && g_logged_in && g_hardcore; }
bool        RaHasPendingUnlocks() { return g_server_pending; }

int RaGetAchievements(RaAchievementInfo* out, int max_out)
{
	if (!out || max_out <= 0 || !g_client) return 0;

	// Called from the UI thread (menu.cpp's Achievements page) while the core
	// thread can concurrently unload the game (RaOnGameUnload) or advance
	// rc_client (RaDoFrame) - without g_client_lock this could walk buckets
	// the core thread is freeing at the same instant.
	EnterCriticalSection(&g_client_lock);

	rc_client_achievement_list_t* list = rc_client_create_achievement_list(g_client,
		RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
		RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
	if (!list) { LeaveCriticalSection(&g_client_lock); return 0; }

	int n = 0;
	for (uint32_t b = 0; b < list->num_buckets && n < max_out; b++)
	{
		for (uint32_t a = 0; a < list->buckets[b].num_achievements && n < max_out; a++)
		{
			const rc_client_achievement_t* ach = list->buckets[b].achievements[a];
			if (!ach) continue;

			strncpy_s(out[n].title, sizeof(out[n].title),
				ach->title ? ach->title : "?", _TRUNCATE);

			out[n].points = (int)ach->points;
			out[n].unlocked = (ach->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED);

			// measured_progress is "37/50" when the achievement tracks a count.
			strncpy_s(out[n].progress, sizeof(out[n].progress),
				ach->measured_progress[0] ? ach->measured_progress : "", _TRUNCATE);

			n++;
		}
	}

	rc_client_destroy_achievement_list(list);
	LeaveCriticalSection(&g_client_lock);
	return n;
}
int         RaGetActiveChallenges() { return g_challenge_active; }
const char* RaGetStatus()
{
	EnterCriticalSection(&g_status_lock);
	strncpy_s(g_status_readback, sizeof(g_status_readback), g_status, _TRUNCATE);
	LeaveCriticalSection(&g_status_lock);
	return g_status_readback;
}
const char* RaGetUserName()
{
	EnterCriticalSection(&g_username_lock);
	strncpy_s(g_username_readback, sizeof(g_username_readback), g_username_mirror, _TRUNCATE);
	LeaveCriticalSection(&g_username_lock);
	return g_username_readback;
}
int         RaGetAchievementCount() { return g_ach_total; }
int         RaGetAchievementsUnlocked() { return g_ach_unlocked; }

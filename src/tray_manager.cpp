#include "tray_manager.h"
#include "desktop_chat.h"
#include "cloud_sync.h"
#include "web_admin.h"
#include "core_runner.h"
#include "menu.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <string>
#include <atomic>
#include <mutex>

static std::mutex          g_tray_mutex;
static std::atomic<bool>   g_tray_enabled{ true };
static std::atomic<int>    g_tray_status{ TRAY_STATUS_ONLINE };
static std::string         g_tray_game_title;

#ifdef _WIN32
static HWND                g_tray_hwnd = NULL;
static bool                g_tray_added = false;
static const UINT          kTrayIconId = 1001;

enum TrayCmdId
{
	ID_TRAY_OPEN_APP     = 41001,
	ID_TRAY_OPEN_CHAT    = 41002,
	ID_TRAY_OPEN_WEB     = 41003,
	ID_TRAY_ST_ONLINE    = 41004,
	ID_TRAY_ST_PLAYING   = 41005,
	ID_TRAY_ST_BUSY      = 41006,
	ID_TRAY_CLOUD_SYNC   = 41007,
	ID_TRAY_FOLDER_ROMS  = 41008,
	ID_TRAY_FOLDER_BIOS  = 41009,
	ID_TRAY_FOLDER_SAVES = 41010,
	ID_TRAY_EXIT         = 41011
};

static void UpdateWin32TrayTooltip()
{
	if (!g_tray_hwnd || !g_tray_added) return;

	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_tray_hwnd;
	nid.uID = kTrayIconId;
	nid.uFlags = NIF_TIP;

	const char* st_str = TrayManagerGetStatusName((TrayPresenceStatus)g_tray_status.load());
	char tip_utf8[128];
	{
		std::lock_guard<std::mutex> lk(g_tray_mutex);
		if (!g_tray_game_title.empty() && g_tray_status.load() == TRAY_STATUS_PLAYING)
			snprintf(tip_utf8, sizeof(tip_utf8), "Karamelo - %s (%s)", st_str, g_tray_game_title.c_str());
		else
			snprintf(tip_utf8, sizeof(tip_utf8), "Karamelo - %s", st_str);
	}

	MultiByteToWideChar(CP_UTF8, 0, tip_utf8, -1, nid.szTip, (int)(sizeof(nid.szTip) / sizeof(wchar_t)));
	Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void AddWin32TrayIcon()
{
	if (!g_tray_hwnd || g_tray_added || !g_tray_enabled.load()) return;

	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_tray_hwnd;
	nid.uID = kTrayIconId;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_KARAMELO_TRAY;
	nid.hIcon = (HICON)GetClassLongPtrW(g_tray_hwnd, GCLP_HICON);
	if (!nid.hIcon)
		nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);

	wcscpy_s(nid.szTip, L"Karamelo Emulador");
	if (Shell_NotifyIconW(NIM_ADD, &nid))
	{
		g_tray_added = true;
		UpdateWin32TrayTooltip();
	}
}

static void RemoveWin32TrayIcon()
{
	if (!g_tray_hwnd || !g_tray_added) return;
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = g_tray_hwnd;
	nid.uID = kTrayIconId;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	g_tray_added = false;
}
#endif

void TrayManagerInit(void* native_hwnd)
{
#ifdef _WIN32
	g_tray_hwnd = (HWND)native_hwnd;
	if (g_tray_enabled.load() && g_tray_hwnd)
		AddWin32TrayIcon();
#else
	(void)native_hwnd;
#endif
}

void TrayManagerShutdown()
{
#ifdef _WIN32
	RemoveWin32TrayIcon();
	g_tray_hwnd = NULL;
#endif
}

bool TrayManagerIsEnabled()
{
	return g_tray_enabled.load();
}

void TrayManagerSetEnabled(bool enabled)
{
	g_tray_enabled.store(enabled);
#ifdef _WIN32
	if (enabled)
		AddWin32TrayIcon();
	else
		RemoveWin32TrayIcon();
#endif
}

TrayPresenceStatus TrayManagerGetStatus()
{
	return (TrayPresenceStatus)g_tray_status.load();
}

void TrayManagerSetStatus(TrayPresenceStatus status, const char* game_title)
{
	if (status < TRAY_STATUS_ONLINE || status > TRAY_STATUS_BUSY)
		status = TRAY_STATUS_ONLINE;
	g_tray_status.store((int)status);
	{
		std::lock_guard<std::mutex> lk(g_tray_mutex);
		g_tray_game_title = game_title ? game_title : "";
	}
#ifdef _WIN32
	UpdateWin32TrayTooltip();
#endif
}

const char* TrayManagerGetStatusName(TrayPresenceStatus status)
{
	switch (status)
	{
	case TRAY_STATUS_ONLINE:  return Tr("Disponivel", "Online");
	case TRAY_STATUS_PLAYING: return Tr("Jogando", "Playing");
	case TRAY_STATUS_BUSY:    return Tr("Ocupado", "Busy");
	default:                  return Tr("Disponivel", "Online");
	}
}

void TrayManagerShowNotification(const char* title, const char* message, bool play_chime)
{
	if (!title || !message) return;

#ifdef _WIN32
	if (g_tray_hwnd && g_tray_added && g_tray_enabled.load())
	{
		NOTIFYICONDATAW nid{};
		nid.cbSize = sizeof(nid);
		nid.hWnd = g_tray_hwnd;
		nid.uID = kTrayIconId;
		nid.uFlags = NIF_INFO;
		nid.dwInfoFlags = NIIF_INFO | (play_chime ? 0 : NIIF_NOSOUND);

		MultiByteToWideChar(CP_UTF8, 0, title, -1, nid.szInfoTitle, (int)(sizeof(nid.szInfoTitle) / sizeof(wchar_t)));
		MultiByteToWideChar(CP_UTF8, 0, message, -1, nid.szInfo, (int)(sizeof(nid.szInfo) / sizeof(wchar_t)));
		Shell_NotifyIconW(NIM_MODIFY, &nid);
	}
	else if (play_chime)
	{
		MessageBeep(MB_ICONINFORMATION);
	}
#else
	(void)play_chime;
#endif
}

bool TrayManagerHandleWin32Message(void* hwnd, unsigned int msg, uintptr_t wparam, intptr_t lparam)
{
#ifdef _WIN32
	(void)wparam;
	if (msg != WM_KARAMELO_TRAY)
		return false;

	UINT ev = (UINT)(lparam & 0xFFFF);
	if (ev == WM_LBUTTONDBLCLK)
	{
		if (g_tray_hwnd)
		{
			ShowWindow(g_tray_hwnd, SW_RESTORE);
			SetForegroundWindow(g_tray_hwnd);
		}
		return true;
	}
	else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU)
	{
		int cur_st = g_tray_status.load();

		HMENU hStatusMenu = CreatePopupMenu();
		AppendMenuW(hStatusMenu, MF_STRING | (cur_st == TRAY_STATUS_ONLINE ? MF_CHECKED : 0), ID_TRAY_ST_ONLINE, L"Disponivel");
		AppendMenuW(hStatusMenu, MF_STRING | (cur_st == TRAY_STATUS_PLAYING ? MF_CHECKED : 0), ID_TRAY_ST_PLAYING, L"Jogando");
		AppendMenuW(hStatusMenu, MF_STRING | (cur_st == TRAY_STATUS_BUSY ? MF_CHECKED : 0), ID_TRAY_ST_BUSY, L"Ocupado");

		HMENU hFoldersMenu = CreatePopupMenu();
		AppendMenuW(hFoldersMenu, MF_STRING, ID_TRAY_FOLDER_ROMS, L"Pasta de ROMs");
		AppendMenuW(hFoldersMenu, MF_STRING, ID_TRAY_FOLDER_BIOS, L"Pasta de BIOS");
		AppendMenuW(hFoldersMenu, MF_STRING, ID_TRAY_FOLDER_SAVES, L"Pasta de Saves");

		HMENU hMenu = CreatePopupMenu();
		if (!hMenu)
		{
			DestroyMenu(hStatusMenu);
			DestroyMenu(hFoldersMenu);
			return true;
		}

		AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_APP, L"Abrir Karamelo");
		AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_CHAT, L"Karamelo Live (Bate-Papo & Social)");
		AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_WEB, L"Web Admin (Painel Wi-Fi / LAN)");
		AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hStatusMenu, L"Presenca Social");
		AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

		wchar_t cloud_info[128];
		if (CloudSyncIsConnected())
			swprintf_s(cloud_info, L"Cloud: Conectado (%d saves)", CloudSyncGetSyncedFileCount());
		else
			swprintf_s(cloud_info, L"Cloud: Desconectado");
		AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, cloud_info);
		AppendMenuW(hMenu, MF_STRING | (CloudSyncIsConnected() ? 0 : MF_GRAYED), ID_TRAY_CLOUD_SYNC, L"Sincronizar Nuvem Agora");
		AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFoldersMenu, L"Explorador de Pastas");
		AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Sair");

		POINT pt;
		GetCursorPos(&pt);
		HWND target_hwnd = g_tray_hwnd ? g_tray_hwnd : (HWND)hwnd;
		SetForegroundWindow(target_hwnd);
		UINT cmd = (UINT)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, target_hwnd, NULL);
		DestroyMenu(hMenu);

		switch (cmd)
		{
		case ID_TRAY_OPEN_APP:
			if (target_hwnd)
			{
				ShowWindow(target_hwnd, SW_RESTORE);
				SetForegroundWindow(target_hwnd);
			}
			break;
		case ID_TRAY_OPEN_CHAT:
			DesktopChatShowWindow();
			break;
		case ID_TRAY_OPEN_WEB:
			WebAdminOpenInBrowser();
			TrayManagerShowNotification("Web Admin Ativo", (std::string("PC: ") + WebAdminGetLocalUrl() + " | PIN: " + WebAdminGetPin()).c_str(), true);
			break;
		case ID_TRAY_ST_ONLINE:
			TrayManagerSetStatus(TRAY_STATUS_ONLINE);
			break;
		case ID_TRAY_ST_PLAYING:
			TrayManagerSetStatus(TRAY_STATUS_PLAYING);
			break;
		case ID_TRAY_ST_BUSY:
			TrayManagerSetStatus(TRAY_STATUS_BUSY);
			break;
		case ID_TRAY_CLOUD_SYNC:
			CloudSyncTriggerAsync();
			break;
		case ID_TRAY_FOLDER_ROMS:
			ShellExecuteW(NULL, L"explore", L"roms", NULL, NULL, SW_SHOWNORMAL);
			break;
		case ID_TRAY_FOLDER_BIOS:
			ShellExecuteW(NULL, L"explore", L"bios", NULL, NULL, SW_SHOWNORMAL);
			break;
		case ID_TRAY_FOLDER_SAVES:
			ShellExecuteW(NULL, L"explore", L"saves", NULL, NULL, SW_SHOWNORMAL);
			break;
		case ID_TRAY_EXIT:
			if (target_hwnd)
				PostMessageW(target_hwnd, WM_CLOSE, 0, 0);
			break;
		default:
			break;
		}
		return true;
	}
	return false;
#else
	(void)hwnd;
	(void)msg;
	(void)wparam;
	(void)lparam;
	return false;
#endif
}


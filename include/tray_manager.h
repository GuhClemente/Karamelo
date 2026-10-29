#ifndef TRAY_MANAGER_H_INCLUDED
#define TRAY_MANAGER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

// =============================================================
// Karamelo System Tray & Native OS Notification Manager
// Win32 Shell_NotifyIconW + Native Toast Notifications
// =============================================================

#define WM_KARAMELO_TRAY (0x8000 + 0x4B) // WM_APP + 'K'

enum TrayPresenceStatus
{
	TRAY_STATUS_ONLINE  = 0, // Disponivel
	TRAY_STATUS_PLAYING = 1, // Jogando
	TRAY_STATUS_BUSY    = 2  // Ocupado
};

// Lifecycle
void               TrayManagerInit(void* native_hwnd);
void               TrayManagerShutdown();

// Visibility & Settings
bool               TrayManagerIsEnabled();
void               TrayManagerSetEnabled(bool enabled);

// Presence Status
TrayPresenceStatus TrayManagerGetStatus();
void               TrayManagerSetStatus(TrayPresenceStatus status, const char* game_title = nullptr);
const char*        TrayManagerGetStatusName(TrayPresenceStatus status);

// Native OS Toast Notification (with optional discrete chime)
void               TrayManagerShowNotification(const char* title, const char* message, bool play_chime = true);

// Win32 Message Hook Handler (called from SdlWindowsMsgHook in main_win32.cpp)
bool               TrayManagerHandleWin32Message(void* hwnd, unsigned int msg, uintptr_t wparam, intptr_t lparam);

#endif // TRAY_MANAGER_H_INCLUDED


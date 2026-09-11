#ifndef MENU_H_INCLUDED
#define MENU_H_INCLUDED

#include <stdint.h>
#include <string>
#include <vector>

enum MenuKey
{
	KEY_NONE = 0,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_PAGEUP,
	KEY_PAGEDOWN,
	KEY_HOME,
	KEY_END,
	KEY_SELECT,
	KEY_CANCEL,
	KEY_MENU_TOGGLE,
	KEY_INFO
};

void MenuInit();
void MenuRun();
void MenuProcessKey(MenuKey key);
void MenuSetStatus(const char* status);
const char* MenuGetStatus();
const char* MenuGetTitle();
bool MenuIsQuitRequested();
void MenuRequestQuit();

int  MenuGetAspectMode();
int  MenuGetFilterMode();
int  MenuGetWallpaperMode();

// Every .raw file found in wallpapers/, sorted, indexed 0..count-1. Used by
// main_win32.cpp to load the same file the menu's custom slots point to.
int  MenuGetWallpaperCustomCount();
const char* MenuGetWallpaperCustomPath(int index);
int  MenuGetOsdTheme();
bool MenuGetFullscreen();
void MenuSetFullscreen(bool fs);
// Windowed-mode position/size, persisted across restarts. Get returns false
// (leaving x/y/w/h untouched) if nothing has been saved yet - caller should
// keep its own built-in default in that case. Set is meant to be called once
// at shutdown with the current geometry, not on every move/resize event.
bool MenuGetWindowRect(int* x, int* y, int* w, int* h);
void MenuSetWindowRect(int x, int y, int w, int h);

// A player's choice on the generic Core Options page (menu.cpp), persisted
// across restarts by option key - unlike CoreGetOption/g_core_options, which
// only remembers a choice for the current run. Returns NULL if this key has
// never been set from that page. core_runner.cpp calls this once per
// declared key when a core reports SET_VARIABLES, to re-apply a saved choice
// before the core ever reads it back through GET_VARIABLE.
const char* MenuGetPersistedCoreOption(const char* key);
// Called by the Core Options page itself when the player changes a value -
// not meant to be called from outside menu.cpp.
void MenuSetPersistedCoreOption(const char* key, const char* value);
int  MenuGetDeadzone();
int  MenuGetAudioLatencyMs();
int  MenuGetVideoDriver();
// So para o autoteste headless (--core-selftest --driver N). A bateria rodava
// sempre com o driver no padrao "Auto", porque o autoteste nao carrega as
// configuracoes - ou seja, testava uma combinacao que nenhum jogador usa, e
// nunca exercitava Vulkan nem DirectX 11. Nao ha caminho de menu para isto:
// quem muda o driver pela interface passa por MenuProcessKey, que persiste.
void MenuSetVideoDriverForSelftest(int driver);
int  MenuGetSyncMode();
int  MenuGetVsync();
int  MenuGetN64Core();
int  MenuGetHwRender();
int  MenuGetLanguage();
std::string MenuResolveCoreForPath(const std::string& file_path, const std::string& dir_hint);
bool MenuLaunchGamePath(const std::string& filepath);

#endif

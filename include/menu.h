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

int  MenuGetAspectMode();
int  MenuGetFilterMode();
int  MenuGetWallpaperMode();
int  MenuGetOsdTheme();
bool MenuGetFullscreen();
void MenuSetFullscreen(bool fs);
int  MenuGetDeadzone();
int  MenuGetAudioLatencyMs();
int  MenuGetVideoDriver();
int  MenuGetSyncMode();
int  MenuGetVsync();
int  MenuGetN64Core();
int  MenuGetHwRender();
int  MenuGetLanguage();
std::string MenuResolveCoreForPath(const std::string& file_path, const std::string& dir_hint);
bool MenuLaunchGamePath(const std::string& filepath);

#endif

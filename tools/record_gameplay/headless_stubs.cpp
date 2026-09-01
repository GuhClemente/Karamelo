// No-op stand-ins for the subsystems core_runner.cpp normally talks to
// (menu settings, netplay, on-screen display, RetroAchievements). This tool
// only loads a core, drives input, and reads back rendered frames - none of
// those subsystems affect the recording, and pulling in their real
// implementations would drag the whole UI/network/achievements stack into a
// tool that has no window and no use for any of it.

#include <stdint.h>
#include "menu.h"
#include "netplay.h"
#include "osd.h"
#include "retroachievements.h"

int  MenuGetDeadzone() { return 0; }
int  MenuGetAudioLatencyMs() { return 64; }
int  MenuGetHwRender() { return 0; }

NetplayState NetplayGetState() { return NETPLAY_DISCONNECTED; }
void NetplaySyncInputs(int16_t[16], int16_t[2][2], int16_t out_p2_buttons[16], int16_t out_p2_analog[2][2])
{
	for (int i = 0; i < 16; i++) out_p2_buttons[i] = 0;
	for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) out_p2_analog[i][j] = 0;
}

bool OsdIsEnabled() { return false; }

void RaDoFrame() {}
void RaOnGameLoad(const char*, const char*) {}
void RaOnGameUnload() {}
bool RaIsHardcoreActive() { return false; }

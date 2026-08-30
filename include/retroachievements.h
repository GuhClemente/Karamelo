#ifndef RETROACHIEVEMENTS_H_INCLUDED
#define RETROACHIEVEMENTS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

// RetroAchievements integration, built on rcheevos (MIT) from
// https://github.com/RetroAchievements/rcheevos
//
// Threading contract: every rcheevos call happens on the core thread. HTTP runs
// on its own worker, and finished responses are handed back to the core thread
// by RaDoFrame, so rc_client is never touched from two threads at once.

// Called once at startup. Reads Config/retroachievements.cfg and, if credentials
// are present, begins signing in. Safe to call when no credentials are set.
void RaInit();
void RaShutdown();

// Core thread. Call right after retro_run(); drains HTTP completions, feeds the
// achievement runtime, and raises unlock notifications.
void RaDoFrame();

// Core thread, around the game lifecycle.
void RaOnGameLoad(const char* rom_path, const char* core_name);
void RaOnGameUnload();

// For the OSD.
bool        RaIsEnabled();
bool        RaIsLoggedIn();

// True when the account is in hardcore mode, where RetroAchievements forbids
// savestates. Using one anyway can get the account flagged.
bool        RaIsHardcoreActive();
const char* RaGetStatus();
const char* RaGetUserName();
int         RaGetAchievementCount();
int         RaGetAchievementsUnlocked();

// One achievement, flattened for the OSD so the menu never touches rcheevos
// structures (which are owned by the core thread).
struct RaAchievementInfo
{
	char title[64];
	char progress[24];   // "37/50" when the achievement tracks a count
	int  points;
	bool unlocked;
};

// Fills up to max_out entries, returns how many. Call from the UI thread.
int RaGetAchievements(struct RaAchievementInfo* out, int max_out);

// True while unlocks are queued locally because the server is unreachable.
bool        RaHasPendingUnlocks();
int         RaGetActiveChallenges();

#endif

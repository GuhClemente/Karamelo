#ifndef CORE_RUNNER_H_INCLUDED
#define CORE_RUNNER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string>

// Lifecycle Management
//
// The core lives entirely on its own thread: archive extraction, retro_load_game
// and retro_run all happen there. Loading a disc image takes seconds, and doing
// that on the UI thread stalled the message pump long enough for Windows to
// declare the window unresponsive.
bool CoreRequestLoad(const char* rom_path, const char* core_dll_hint);
bool CoreIsLoading();
void CoreShutdown();

// Recreates any of the frontend's internal locks (toast/name/options) that
// the given thread ID currently owns. For use right before force-terminating
// a core-spawned thread that crashed mid-callback: TerminateThread never runs
// that thread's LeaveCriticalSection, so without this any lock it held stays
// wedged forever.
void CoreRecoverLocksHeldByThread(unsigned long thread_id);
void CoreReset();
bool CoreIsRunning();
double CoreGetTargetFps();

// Slave the core's frame rate to the display's, so a 59.19fps core stops
// beating against a 59.94Hz panel. Audio is rescaled to match.
void   CoreSetDisplaySync(bool enable, double display_fps);
double CoreGetDisplayFps();
bool   CoreDisplaySyncActive();

// Emulated console memory, for RetroAchievements. id is RETRO_MEMORY_SYSTEM_RAM
// and friends. Returns NULL/0 when the core does not expose that region.
void*  CoreGetMemoryData(unsigned id);
size_t CoreGetMemorySize(unsigned id);

// The core's retro_memory_map, when it published one. NULL otherwise.
// RetroAchievements needs it to map a console's whole achievement address
// space; without it only the first region resolves.
const void* CoreGetMemoryMap();

// Presentation (UI thread; reads the shared framebuffer)
void CoreRender(uint32_t* dest_buffer, int dest_w, int dest_h, int aspect_mode, int filter_mode);

// High-Performance Input Dispatch
void CoreSetButtonState(int player, int button_id, bool pressed);
void CoreSetAnalogState(int player, int stick_id, int axis_id, int16_t value);

// Dynamic Savestates (0-9)
bool CoreSaveState(int slot = 0);
bool CoreLoadState(int slot = 0);
int  CoreGetSelectedSlot();
void CoreSetSelectedSlot(int slot);
bool CoreTakeScreenshot();

// Volume & Audio Controls
void CoreSetVolume(int volume_percent);
int  CoreGetVolume();
void CoreSetMute(bool mute);
bool CoreGetMute();

// HUD Toast Notification Engine
// frames_duration e convertido para tempo de relogio (quadros a 60 fps) e a
// mensagem expira sozinha nesse prazo - com ou sem core rodando. Nao depende
// mais de CoreUpdateToast() ser chamado a cada quadro.
void CoreSetToast(const char* message, int frames_duration = 120);
const char* CoreGetToast();
bool CoreIsToastActive();
void CoreUpdateToast();

// Core & Game Metadata & Options
const char* CoreGetGameName();
const char* CoreGetCoreName();
void CoreSetOption(const char* key, const char* value);

// Mouse as a stylus. Takes the cursor position as a fraction of the client
// area (0..1) and maps it through the current viewport onto the core's
// framebuffer, so the touch lands under the cursor whatever the aspect ratio
// or window size. A position outside the picture is reported as not touching.
void CoreSetPointer(double client_fx, double client_fy, bool pressed);

// True when the loaded game came from a disc image rather than a cartridge.
bool CoreIsDiscGame();

// Folder the loaded ROM came from, so the menu can reopen that list.
const char* CoreGetRomDir();
const char* CoreGetOption(const char* key);

// The currently loaded core's own options, as declared via
// RETRO_ENVIRONMENT_SET_VARIABLES - for the generic Core Options menu page.
// index ranges 0..CoreOptionCount()-1, in the order the core declared them.
// Choices set through CoreOptionSetChoiceIndex only last for the current run
// (same lifetime as every other CoreSetOption call), not saved to disk.
int  CoreOptionCount();
const char* CoreOptionKey(int index);
const char* CoreOptionLabel(int index);
int  CoreOptionChoiceCount(int index);
const char* CoreOptionChoiceAt(int index, int choice_index);
int  CoreOptionCurrentChoiceIndex(int index);
void CoreOptionSetChoiceIndex(int index, int choice_index);

// What the running core calls a control, or NULL when it never said. Lets the
// Controller page show "Cross" on PSP where it would otherwise say "Botao B".
const char* CoreGetButtonLabel(int retro_id);
const char* CoreGetAxisLabel(int stick, int axis);

#endif

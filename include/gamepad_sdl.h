#ifndef GAMEPAD_SDL_H_INCLUDED
#define GAMEPAD_SDL_H_INCLUDED

#ifdef _WIN32
#include <xinput.h>
#else
#include "compat_win32.h"
#endif

union SDL_Event;

// dev-sdl3: replaces XInputGetState() with SDL3's gamepad subsystem, behind
// an XINPUT_STATE-shaped interface so every existing call site (main_win32,
// core_runner, input_map) keeps working with only the function name changed -
// the PAD_BTN_* codes in input_map.h are XInput's own bitmask values, and
// stay that way so a player's saved bindings do not break. SDL_Gamepad also
// covers far more controllers than XInput ever did on Windows (DirectInput
// pads, Bluetooth, PlayStation, Switch Pro, Steam Input) through one API.

// Same contract as XInputGetState: returns true and fills out_state if a
// controller is connected in this slot (0-3), false otherwise. Slots are
// assigned in connection order (0 = first controller plugged in this
// session), tracked via GamepadHandleDeviceEvent - not the fixed hardware
// slot XInput used, so a player who unplugs/replugs may land on a different
// index than before. Same behavior SDL itself has everywhere else.
bool GamepadGetState(int slot, XINPUT_STATE* out_state);

// Feed every SDL event from the main loop here - it only acts on
// SDL_EVENT_GAMEPAD_ADDED/SDL_EVENT_GAMEPAD_REMOVED, ignoring the rest, so
// slot assignment stays in sync with what is actually plugged in.
void GamepadHandleDeviceEvent(const SDL_Event* event);

// Closes all open gamepad handles on shutdown.
void GamepadShutdown();

#endif

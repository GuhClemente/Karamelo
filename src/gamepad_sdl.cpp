// windows.h has to come first: xinput.h (pulled in by gamepad_sdl.h) needs
// the architecture macros windows.h's own preamble sets up before it
// includes winnt.h, or MSVC fails with "No Target Architecture" (C1189).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include "compat_win32.h"
#endif
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "gamepad_sdl.h"

static void GamepadLog(const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	FILE* f = fopen("karamelo.log", "a");
	if (f) { fprintf(f, "[INFO] [GAMEPAD] %s\n", buf); fclose(f); }
}

// Slots 0-3, assigned in connection order - see the header for why this
// differs from XInput's fixed hardware-slot model.
static SDL_Gamepad* s_pads[4] = { nullptr, nullptr, nullptr, nullptr };

// s_pads is written by GamepadHandleDeviceEvent on the main thread (SDL's
// event pump in WinMain) and read by GamepadGetState from the core thread
// (CB_InputPoll, inside retro_run) as well as the main thread. Without this,
// unplugging a controller mid-game could run SDL_CloseGamepad on one thread
// while the other was already inside SDL_GetGamepadButton on that same
// pointer - a use-after-free. XInputGetState was thread-safe by construction
// and needed nothing here; SDL_OpenGamepad/SDL_CloseGamepad are not, for the
// lifetime of the object they hand back.
//
// Shared for readers so two threads polling at once still don't serialize
// (SDL does its own internal locking for the actual button/axis reads),
// exclusive only for the open/close that changes what the slots point at.
// SRWLOCK_INIT is a static initializer, so there is no init function to call
// and no order-of-initialization hazard against the first poll.
static SRWLOCK s_pads_lock = SRWLOCK_INIT;

// Both helpers below assume the caller already holds s_pads_lock exclusively
// (GamepadHandleDeviceEvent is the only caller) - they must never take it
// themselves, or the event handler would deadlock against itself.
static int FindFreeSlot()
{
	for (int i = 0; i < 4; i++) if (!s_pads[i]) return i;
	return -1;
}

static int FindSlotByInstanceId(SDL_JoystickID id)
{
	for (int i = 0; i < 4; i++)
		if (s_pads[i] && SDL_GetGamepadID(s_pads[i]) == id) return i;
	return -1;
}

void GamepadHandleDeviceEvent(const SDL_Event* event)
{
	if (!event) return;

	if (event->type == SDL_EVENT_GAMEPAD_ADDED)
	{
		SDL_JoystickID id = event->gdevice.which;

		AcquireSRWLockExclusive(&s_pads_lock);
		if (FindSlotByInstanceId(id) >= 0) // already open
		{
			ReleaseSRWLockExclusive(&s_pads_lock);
			return;
		}
		int slot = FindFreeSlot();
		if (slot < 0)
		{
			ReleaseSRWLockExclusive(&s_pads_lock);
			GamepadLog("conectado mas os 4 slots ja estao ocupados - ignorado");
			return;
		}
		SDL_Gamepad* opened = SDL_OpenGamepad(id);
		s_pads[slot] = opened;
		ReleaseSRWLockExclusive(&s_pads_lock);

		// Logged outside the lock: this writes to disk, and GamepadGetState is
		// polled every frame from the core thread behind the same lock.
		// `opened` stays valid to read here because only this thread ever
		// closes a pad, and it is not inside another event right now.
		if (opened)
		{
			GamepadLog("slot %d: %s (tipo=%d)", slot,
				SDL_GetGamepadName(opened),
				(int)SDL_GetGamepadType(opened));
		}
		else
		{
			GamepadLog("slot %d: SDL_OpenGamepad falhou - %s", slot, SDL_GetError());
		}
	}
	else if (event->type == SDL_EVENT_GAMEPAD_REMOVED)
	{
		// The close itself has to happen INSIDE the exclusive section, not
		// after it: acquiring exclusive only waits for readers already holding
		// the shared lock to leave, so releasing first and closing after would
		// put the free right back in the window where a reader on the core
		// thread can be inside SDL_GetGamepadButton on that pointer - the
		// exact use-after-free this lock exists to close.
		AcquireSRWLockExclusive(&s_pads_lock);
		int slot = FindSlotByInstanceId(event->gdevice.which);
		if (slot >= 0)
		{
			SDL_CloseGamepad(s_pads[slot]);
			s_pads[slot] = nullptr;
		}
		ReleaseSRWLockExclusive(&s_pads_lock);

		if (slot >= 0) GamepadLog("slot %d desconectado", slot);
	}
}

// XInput's triggers are a BYTE (0-255); SDL reports the same physical travel
// as an axis in 0..32767 (SDL_JOYSTICK_AXIS_MAX). Rescaled, not just shifted,
// so a fully-pressed trigger still reads as a fully-pressed 255.
static BYTE ScaleTrigger(Sint16 v)
{
	if (v < 0) v = 0;
	int scaled = ((int)v * 255) / 32767;
	if (scaled > 255) scaled = 255;
	return (BYTE)scaled;
}

bool GamepadGetState(int slot, XINPUT_STATE* out_state)
{
	if (slot < 0 || slot >= 4) return false;

	// Shared for the whole read, not just to fetch the pointer: SDL_CloseGamepad
	// on the main thread frees the object, so the lock has to still be held
	// while the SDL_GetGamepad* calls below dereference it.
	AcquireSRWLockShared(&s_pads_lock);
	SDL_Gamepad* gp = s_pads[slot];
	if (!gp)
	{
		ReleaseSRWLockShared(&s_pads_lock);
		return false;
	}

	memset(out_state, 0, sizeof(XINPUT_STATE));
	WORD w = 0;

	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP))        w |= XINPUT_GAMEPAD_DPAD_UP;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN))      w |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT))      w |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))     w |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_START))          w |= XINPUT_GAMEPAD_START;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_BACK))           w |= XINPUT_GAMEPAD_BACK;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_STICK))     w |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_STICK))    w |= XINPUT_GAMEPAD_RIGHT_THUMB;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  w |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) w |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH))          w |= XINPUT_GAMEPAD_A;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST))           w |= XINPUT_GAMEPAD_B;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST))           w |= XINPUT_GAMEPAD_X;
	if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_NORTH))          w |= XINPUT_GAMEPAD_Y;

	out_state->Gamepad.wButtons = w;
	out_state->Gamepad.bLeftTrigger  = ScaleTrigger(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
	out_state->Gamepad.bRightTrigger = ScaleTrigger(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));

	// X matches between the two APIs; Y does not - XInput's is positive-up,
	// SDL's is positive-down. Negating here (once) keeps every existing
	// consumer of this struct, all written against XInput's own convention,
	// correct with no changes of their own.
	Sint16 lx = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX);
	Sint16 ly = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
	Sint16 rx = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX);
	Sint16 ry = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY);
	out_state->Gamepad.sThumbLX = lx;
	out_state->Gamepad.sThumbLY = (ly == -32768) ? 32767 : (SHORT)(-ly);
	out_state->Gamepad.sThumbRX = rx;
	out_state->Gamepad.sThumbRY = (ry == -32768) ? 32767 : (SHORT)(-ry);

	ReleaseSRWLockShared(&s_pads_lock);
	return true;
}

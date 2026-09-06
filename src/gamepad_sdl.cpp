// windows.h has to come first: xinput.h (pulled in by gamepad_sdl.h) needs
// the architecture macros windows.h's own preamble sets up before it
// includes winnt.h, or MSVC fails with "No Target Architecture" (C1189).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL3/SDL.h>
#include <string.h>

#include "gamepad_sdl.h"

// Slots 0-3, assigned in connection order - see the header for why this
// differs from XInput's fixed hardware-slot model.
static SDL_Gamepad* s_pads[4] = { nullptr, nullptr, nullptr, nullptr };

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
		if (FindSlotByInstanceId(id) >= 0) return; // already open
		int slot = FindFreeSlot();
		if (slot < 0) return; // four controllers already claimed
		s_pads[slot] = SDL_OpenGamepad(id);
	}
	else if (event->type == SDL_EVENT_GAMEPAD_REMOVED)
	{
		int slot = FindSlotByInstanceId(event->gdevice.which);
		if (slot >= 0)
		{
			SDL_CloseGamepad(s_pads[slot]);
			s_pads[slot] = nullptr;
		}
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
	if (slot < 0 || slot >= 4 || !s_pads[slot]) return false;
	SDL_Gamepad* gp = s_pads[slot];

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

	return true;
}

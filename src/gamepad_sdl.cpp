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

// A controller SDL has no mapping for arrives only as a joystick, and only
// SDL_EVENT_GAMEPAD_ADDED was handled: such a pad was never opened, so it did
// nothing in the menu or in games. Seen with 8BitDo pads paired over
// Bluetooth (D-input/Android/macOS modes), on Windows and on Linux, while the
// same pad over USB (X-input) worked. A plain default mapping is registered
// for it - first two buttons confirm/back, last two are Select/Start, hat or
// first two axes move - and SDL then announces it as a gamepad, which goes
// through the normal path below. The log line carries what is needed to write
// a proper mapping for that pad later.
static void MapUnknownJoystick(SDL_JoystickID id)
{
	if (SDL_IsGamepad(id)) return; // a real mapping exists: GAMEPAD_ADDED follows

	SDL_Joystick* js = SDL_OpenJoystick(id);
	if (!js)
	{
		GamepadLog("joystick %u sem mapeamento e nao abriu - %s", (unsigned)id, SDL_GetError());
		return;
	}
	const char* name = SDL_GetJoystickName(js);
	char guid[64] = {};
	SDL_GUIDToString(SDL_GetJoystickGUID(js), guid, sizeof(guid));
	const int axes = SDL_GetNumJoystickAxes(js);
	const int buttons = SDL_GetNumJoystickButtons(js);
	const int hats = SDL_GetNumJoystickHats(js);
	SDL_CloseJoystick(js);

	GamepadLog("joystick sem mapeamento: \"%s\" guid=%s eixos=%d botoes=%d hats=%d",
		name ? name : "?", guid, axes, buttons, hats);
	if (buttons < 2 || (hats < 1 && axes < 2))
	{
		GamepadLog("  poucos controles para servir de gamepad - ignorado");
		return;
	}

	char map[512];
	int n = snprintf(map, sizeof(map), "%s,%s,a:b0,b:b1", guid, "Karamelo generic");
	if (buttons > 2) n += snprintf(map + n, sizeof(map) - n, ",x:b2");
	if (buttons > 3) n += snprintf(map + n, sizeof(map) - n, ",y:b3");
	if (buttons > 5) n += snprintf(map + n, sizeof(map) - n, ",leftshoulder:b4,rightshoulder:b5");
	if (buttons > 7) n += snprintf(map + n, sizeof(map) - n, ",back:b%d,start:b%d", buttons - 2, buttons - 1);
	else if (buttons > 4) n += snprintf(map + n, sizeof(map) - n, ",start:b%d", buttons - 1);
	if (hats > 0) n += snprintf(map + n, sizeof(map) - n, ",dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8");
	if (axes >= 2) n += snprintf(map + n, sizeof(map) - n, ",leftx:a0,lefty:a1");
	if (axes >= 4) n += snprintf(map + n, sizeof(map) - n, ",rightx:a2,righty:a3");

	if (SDL_AddGamepadMapping(map) < 0)
	{
		GamepadLog("  mapeamento padrao recusado pelo SDL - %s", SDL_GetError());
		return;
	}
	GamepadLog("  mapeamento padrao aplicado: %s", map);

	// Open it right away through the normal path instead of relying on SDL to
	// announce it; if SDL does announce it later, the slot lookup sees it is
	// already open and nothing is opened twice.
	if (SDL_IsGamepad(id))
	{
		SDL_Event added;
		SDL_zero(added);
		added.type = SDL_EVENT_GAMEPAD_ADDED;
		added.gdevice.which = id;
		GamepadHandleDeviceEvent(&added);
	}
}

// ---------------------------------------------------------------------------
// Hot swap. Slots used to be handed out in connection order and kept for good:
// take the P1 pad away and the pad left in the player's hands stayed P2, which
// nothing reads in a one-player game. The rules now:
//  1. A pad that drops and comes back gets its old slot back (identified by
//     GUID + serial), so a flat battery or a Bluetooth blip does not reshuffle
//     players.
//  2. A pad that arrives takes the first empty slot - including the one a
//     dropped pad left - so plugging another pad in when P1 died makes it P1.
//  3. When only one pad is left and it is not P1, it becomes P1 at once.
//  4. With two or more pads left nobody moves on their own (a two-player match
//     must not swap characters); START on a pad claims an empty P1.
//  5. Every change is announced on screen through the notice callback.
// ---------------------------------------------------------------------------
static char s_slot_key[4][160] = {};   // identity of the pad that owns / owned the slot
static bool s_slot_reserved[4] = {};   // empty, but kept for that pad if it returns
static void (*s_notice_cb)(const char*) = nullptr;

void GamepadSetNoticeCallback(void (*cb)(const char* message))
{
	s_notice_cb = cb;
}

static void Notice(const char* fmt, ...)
{
	char buf[200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	GamepadLog("%s", buf);
	if (s_notice_cb) s_notice_cb(buf);
}

static void PadKey(SDL_Gamepad* gp, SDL_JoystickID id, char* out, size_t out_size)
{
	char guid[64] = {};
	SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
	const char* serial = gp ? SDL_GetGamepadSerial(gp) : nullptr;
	snprintf(out, out_size, "%s|%s", guid, serial ? serial : "");
}

static const char* PadName(SDL_Gamepad* gp)
{
	const char* n = gp ? SDL_GetGamepadName(gp) : nullptr;
	return (n && n[0]) ? n : "Controle";
}

// Moves the pad in `from` to `to` (which must be empty). Caller holds the lock.
static void MoveSlot(int from, int to)
{
	s_pads[to] = s_pads[from];
	s_pads[from] = nullptr;
	SDL_strlcpy(s_slot_key[to], s_slot_key[from], sizeof(s_slot_key[to]));
	s_slot_key[from][0] = '\0';
	s_slot_reserved[to] = false;
	s_slot_reserved[from] = false;
}

void GamepadHandleDeviceEvent(const SDL_Event* event)
{
	if (!event) return;

	if (event->type == SDL_EVENT_JOYSTICK_ADDED)
	{
		MapUnknownJoystick(event->jdevice.which);
		return;
	}

	if (event->type == SDL_EVENT_GAMEPAD_ADDED)
	{
		SDL_JoystickID id = event->gdevice.which;

		AcquireSRWLockExclusive(&s_pads_lock);
		if (FindSlotByInstanceId(id) >= 0) // already open
		{
			ReleaseSRWLockExclusive(&s_pads_lock);
			return;
		}
		if (FindFreeSlot() < 0)
		{
			ReleaseSRWLockExclusive(&s_pads_lock);
			GamepadLog("conectado mas os 4 slots ja estao ocupados - ignorado");
			return;
		}
		SDL_Gamepad* opened = SDL_OpenGamepad(id);
		int slot = -1;
		bool returned = false;
		if (opened)
		{
			char key[160];
			PadKey(opened, id, key, sizeof(key));
			for (int i = 0; i < 4 && slot < 0; i++)  // rule 1: its own slot
				if (!s_pads[i] && s_slot_reserved[i] && strcmp(s_slot_key[i], key) == 0) { slot = i; returned = true; }
			if (slot < 0) slot = FindFreeSlot();     // rule 2: first empty slot
			s_pads[slot] = opened;
			SDL_strlcpy(s_slot_key[slot], key, sizeof(s_slot_key[slot]));
			s_slot_reserved[slot] = false;
		}
		ReleaseSRWLockExclusive(&s_pads_lock);

		// Logged outside the lock: this writes to disk, and GamepadGetState is
		// polled every frame from the core thread behind the same lock.
		// `opened` stays valid to read here because only this thread ever
		// closes a pad, and it is not inside another event right now.
		if (opened)
		{
			GamepadLog("slot %d: %s (tipo=%d)", slot, SDL_GetGamepadName(opened), (int)SDL_GetGamepadType(opened));
			Notice(returned ? "%s voltou como P%d" : "%s conectado como P%d", PadName(opened), slot + 1);
		}
		else
		{
			char err[256];
			SDL_strlcpy(err, SDL_GetError(), sizeof(err));
			// The same device kept coming back and failing: one line per device,
			// not one per attempt (a single session grew karamelo.log to 29 MB).
			static SDL_JoystickID s_last_failed_id = 0;
			if (id != s_last_failed_id)
			{
				s_last_failed_id = id;
				GamepadLog("SDL_OpenGamepad falhou - %s", err);
			}
			// 8BitDo pads paired over Bluetooth in Switch mode present themselves
			// as a Nintendo Switch Pro Controller. SDL's own Switch driver then
			// tries to read the stick calibration the way a Nintendo pad stores
			// it, the clone does not answer, and the open fails - the pad worked
			// in Windows itself but never in Karamelo, on Windows or Linux.
			// Turning that driver off makes SDL re-detect the pad through the
			// generic path (DirectInput on Windows, evdev on Linux), which opens
			// it. Only done after such a failure, so a genuine Switch controller
			// that opens fine keeps the full driver.
			static bool s_switch_driver_off = false;
			if (!s_switch_driver_off && SDL_strcasestr(err, "calibration"))
			{
				s_switch_driver_off = true;
				GamepadLog("controle em modo Switch sem calibracao (8BitDo e outros): "
				           "abrindo pelo driver generico do SDL");
				SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "0");
			}
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
		char gone_name[96] = {};
		int promoted_from = -1;
		int left = 0;
		AcquireSRWLockExclusive(&s_pads_lock);
		int slot = FindSlotByInstanceId(event->gdevice.which);
		if (slot >= 0)
		{
			SDL_strlcpy(gone_name, PadName(s_pads[slot]), sizeof(gone_name));
			SDL_CloseGamepad(s_pads[slot]);
			s_pads[slot] = nullptr;
			s_slot_reserved[slot] = true; // rule 1: kept for this pad
			int only = -1;
			for (int i = 0; i < 4; i++) if (s_pads[i]) { left++; only = i; }
			if (left == 1 && only != 0 && !s_pads[0]) // rule 3
			{
				MoveSlot(only, 0);
				promoted_from = only;
			}
		}
		char promoted_name[96] = {};
		if (promoted_from >= 0) SDL_strlcpy(promoted_name, PadName(s_pads[0]), sizeof(promoted_name));
		ReleaseSRWLockExclusive(&s_pads_lock);

		if (slot >= 0)
		{
			GamepadLog("slot %d desconectado (%s)", slot, gone_name);
			if (promoted_from >= 0)
				Notice("%s agora e P1", promoted_name);
			else if (slot == 0 && left >= 2)
				Notice("P1 desconectado: reconecte ou aperte START no controle que vai ser o P1");
			else
				Notice("%s (P%d) desconectado", gone_name, slot + 1);
		}
	}
	else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event->gbutton.button == SDL_GAMEPAD_BUTTON_START)
	{
		// Rule 4: START claims an empty P1.
		char name[96] = {};
		bool moved = false;
		AcquireSRWLockExclusive(&s_pads_lock);
		int slot = FindSlotByInstanceId(event->gbutton.which);
		if (slot > 0 && !s_pads[0])
		{
			MoveSlot(slot, 0);
			SDL_strlcpy(name, PadName(s_pads[0]), sizeof(name));
			moved = true;
		}
		ReleaseSRWLockExclusive(&s_pads_lock);
		if (moved) Notice("%s agora e P1", name);
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

static SRWLOCK s_probe_lock = SRWLOCK_INIT;
static bool s_probe_on = false;
static int s_probe_slot = 0;
static XINPUT_STATE s_probe_state;

void GamepadSetProbeState(const XINPUT_STATE* state, int slot)
{
	AcquireSRWLockExclusive(&s_probe_lock);
	s_probe_on = state != nullptr;
	s_probe_slot = slot;
	if (state) s_probe_state = *state;
	ReleaseSRWLockExclusive(&s_probe_lock);
}

bool GamepadGetState(int slot, XINPUT_STATE* out_state)
{
	if (slot < 0 || slot >= 4) return false;

	AcquireSRWLockShared(&s_probe_lock);
	if (s_probe_on)
	{
		bool connected = slot == s_probe_slot;
		if (connected) *out_state = s_probe_state;
		ReleaseSRWLockShared(&s_probe_lock);
		return connected;
	}
	ReleaseSRWLockShared(&s_probe_lock);

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

bool GamepadGetGuideButton(int slot)
{
	if (slot < 0 || slot >= 4) return false;

	AcquireSRWLockShared(&s_probe_lock);
	if (s_probe_on)
	{
		ReleaseSRWLockShared(&s_probe_lock);
		return false;
	}
	ReleaseSRWLockShared(&s_probe_lock);

	AcquireSRWLockShared(&s_pads_lock);
	SDL_Gamepad* gp = s_pads[slot];
	bool pressed = false;
	if (gp)
	{
		pressed = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_GUIDE) ||
		          SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_MISC1) ||
		          SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_TOUCHPAD);
	}
	ReleaseSRWLockShared(&s_pads_lock);
	return pressed;
}

const char* GamepadGetDeviceName(int slot)
{
	if (slot < 0 || slot >= 4) return nullptr;
	AcquireSRWLockShared(&s_pads_lock);
	SDL_Gamepad* gp = s_pads[slot];
	const char* name = gp ? SDL_GetGamepadName(gp) : nullptr;
	ReleaseSRWLockShared(&s_pads_lock);
	return name;
}

int GamepadGetDeviceType(int slot)
{
	if (slot < 0 || slot >= 4) return 0;
	AcquireSRWLockShared(&s_pads_lock);
	SDL_Gamepad* gp = s_pads[slot];
	int t = gp ? (int)SDL_GetGamepadType(gp) : 0;
	ReleaseSRWLockShared(&s_pads_lock);
	return t;
}

const char* GamepadGetDeviceTypeName(int slot)
{
	int t = GamepadGetDeviceType(slot);
	switch (t)
	{
	case SDL_GAMEPAD_TYPE_PS5: return "DualSense (PS5)";
	case SDL_GAMEPAD_TYPE_PS4: return "DualShock 4";
	case SDL_GAMEPAD_TYPE_PS3: return "DualShock 3";
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return "Switch Pro";
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "Joy-Con (E)";
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "Joy-Con (D)";
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "Joy-Con Par";
	case SDL_GAMEPAD_TYPE_GAMECUBE: return "GameCube";
	case SDL_GAMEPAD_TYPE_XBOXONE: return "Xbox One/Series";
	case SDL_GAMEPAD_TYPE_XBOX360: return "Xbox 360";
	case SDL_GAMEPAD_TYPE_STANDARD: return "Padrao";
	default:
	{
		const char* name = GamepadGetDeviceName(slot);
		return name ? name : "Gamepad";
	}
	}
}

void GamepadShutdown()
{
	AcquireSRWLockExclusive(&s_pads_lock);
	for (int i = 0; i < 4; i++)
	{
		if (s_pads[i])
		{
			SDL_CloseGamepad(s_pads[i]);
			s_pads[i] = nullptr;
		}
	}
	ReleaseSRWLockExclusive(&s_pads_lock);
}


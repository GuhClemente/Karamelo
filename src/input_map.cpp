#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#else
#include "compat_win32.h"
#include <SDL3/SDL.h>

static SDL_Scancode VkToScancode(int vk)
{
	switch (vk)
	{
	case VK_BACK: return SDL_SCANCODE_BACKSPACE;
	case VK_TAB: return SDL_SCANCODE_TAB;
	case VK_RETURN: return SDL_SCANCODE_RETURN;
	case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
	case VK_SPACE: return SDL_SCANCODE_SPACE;
	case VK_LEFT: return SDL_SCANCODE_LEFT;
	case VK_UP: return SDL_SCANCODE_UP;
	case VK_RIGHT: return SDL_SCANCODE_RIGHT;
	case VK_DOWN: return SDL_SCANCODE_DOWN;
	case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
	case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
	case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
	case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
	case VK_MENU:
	case VK_LMENU: return SDL_SCANCODE_LALT;
	case VK_RMENU: return SDL_SCANCODE_RALT;
	case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
	case VK_PAUSE: return SDL_SCANCODE_PAUSE;
	case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
	case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
	case VK_END: return SDL_SCANCODE_END;
	case VK_HOME: return SDL_SCANCODE_HOME;
	case VK_INSERT: return SDL_SCANCODE_INSERT;
	case VK_DELETE: return SDL_SCANCODE_DELETE;
	case VK_NUMLOCK: return SDL_SCANCODE_NUMLOCKCLEAR;
	case VK_SCROLL: return SDL_SCANCODE_SCROLLLOCK;
	case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
	case VK_ADD: return SDL_SCANCODE_KP_PLUS;
	case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
	case VK_DECIMAL: return SDL_SCANCODE_KP_PERIOD;
	case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
	case VK_OEM_1: return SDL_SCANCODE_SEMICOLON;
	case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
	case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
	case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
	case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD;
	case VK_OEM_2: return SDL_SCANCODE_SLASH;
	case VK_OEM_3: return SDL_SCANCODE_GRAVE;
	case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET;
	case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
	case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET;
	case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
	case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
	case VK_NUMPAD1: return SDL_SCANCODE_KP_1;
	case VK_NUMPAD2: return SDL_SCANCODE_KP_2;
	case VK_NUMPAD3: return SDL_SCANCODE_KP_3;
	case VK_NUMPAD4: return SDL_SCANCODE_KP_4;
	case VK_NUMPAD5: return SDL_SCANCODE_KP_5;
	case VK_NUMPAD6: return SDL_SCANCODE_KP_6;
	case VK_NUMPAD7: return SDL_SCANCODE_KP_7;
	case VK_NUMPAD8: return SDL_SCANCODE_KP_8;
	case VK_NUMPAD9: return SDL_SCANCODE_KP_9;
	case VK_F1: return SDL_SCANCODE_F1;
	case VK_F2: return SDL_SCANCODE_F2;
	case VK_F3: return SDL_SCANCODE_F3;
	case VK_F4: return SDL_SCANCODE_F4;
	case VK_F5: return SDL_SCANCODE_F5;
	case VK_F6: return SDL_SCANCODE_F6;
	case VK_F7: return SDL_SCANCODE_F7;
	case VK_F8: return SDL_SCANCODE_F8;
	case VK_F9: return SDL_SCANCODE_F9;
	case VK_F10: return SDL_SCANCODE_F10;
	case VK_F11: return SDL_SCANCODE_F11;
	case VK_F12: return SDL_SCANCODE_F12;
	default: break;
	}
	if (vk >= 'A' && vk <= 'Z')
		return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
	if (vk >= '0' && vk <= '9')
		return (vk == '0') ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
	return SDL_SCANCODE_UNKNOWN;
}

extern "C" SHORT GetAsyncKeyState(int vk)
{
	if (vk == VK_LBUTTON)
	{
		SDL_MouseButtonFlags btn = SDL_GetMouseState(NULL, NULL);
		return (btn & SDL_BUTTON_LMASK) ? (SHORT)0x8000 : 0;
	}
	if (vk == VK_RBUTTON)
	{
		SDL_MouseButtonFlags btn = SDL_GetMouseState(NULL, NULL);
		return (btn & SDL_BUTTON_RMASK) ? (SHORT)0x8000 : 0;
	}
	if (vk == VK_MBUTTON)
	{
		SDL_MouseButtonFlags btn = SDL_GetMouseState(NULL, NULL);
		return (btn & SDL_BUTTON_MMASK) ? (SHORT)0x8000 : 0;
	}

	int numkeys = 0;
	const bool* state = SDL_GetKeyboardState(&numkeys);
	if (!state) return 0;
	if (vk == VK_SHIFT)
	{
		if ((SDL_SCANCODE_LSHIFT < numkeys && state[SDL_SCANCODE_LSHIFT]) ||
		    (SDL_SCANCODE_RSHIFT < numkeys && state[SDL_SCANCODE_RSHIFT]))
			return (SHORT)0x8000;
		return 0;
	}
	if (vk == VK_CONTROL)
	{
		if ((SDL_SCANCODE_LCTRL < numkeys && state[SDL_SCANCODE_LCTRL]) ||
		    (SDL_SCANCODE_RCTRL < numkeys && state[SDL_SCANCODE_RCTRL]))
			return (SHORT)0x8000;
		return 0;
	}
	if (vk == VK_MENU)
	{
		if ((SDL_SCANCODE_LALT < numkeys && state[SDL_SCANCODE_LALT]) ||
		    (SDL_SCANCODE_RALT < numkeys && state[SDL_SCANCODE_RALT]))
			return (SHORT)0x8000;
		return 0;
	}
	SDL_Scancode sc = VkToScancode(vk);
	if (sc != SDL_SCANCODE_UNKNOWN && sc < numkeys && state[sc])
		return (SHORT)0x8000;
	return 0;
}
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <string>
#include <filesystem>

#include "input_map.h"
#include "libretro.h"
#include "gamepad_sdl.h"

namespace fs = std::filesystem;

struct BindDef
{
	const char* label;
	const char* config_key;
	const char* pad_config_key;
	int retro_id;
	int default_vk;
	int default_pad;
	int stick;   // -1 = plain button, 0 = left stick, 1 = right stick
	int axis;    // 0 = X, 1 = Y
	int sign;    // +1 = right/up, -1 = left/down
};

// Defaults:
// Keyboard: Arrow keys for D-pad, Z=B, X=A, A=Y, S=X, Q=L, W=R, Enter=Start, Space=Select.
// Gamepad: Standard MiSTer / Nintendo RetroPad mapping on Xbox layout:
// Xbox A -> Retro B (Jump/Cancel)
// Xbox B -> Retro A (Confirm)
// Xbox X -> Retro Y (Special/Run)
// Xbox Y -> Retro X
// Xbox LB -> Retro L
// Xbox RB -> Retro R
// Xbox Start -> Retro Start
// Xbox Back -> Retro Select
static const BindDef s_defs[BIND_COUNT] = {
	{ "Cima",          "key_up",     "pad_up",     RETRO_DEVICE_ID_JOYPAD_UP,     VK_UP,     PAD_BTN_DPAD_UP, -1, 0, 0 },
	{ "Baixo",         "key_down",   "pad_down",   RETRO_DEVICE_ID_JOYPAD_DOWN,   VK_DOWN,   PAD_BTN_DPAD_DOWN, -1, 0, 0 },
	{ "Esquerda",      "key_left",   "pad_left",   RETRO_DEVICE_ID_JOYPAD_LEFT,   VK_LEFT,   PAD_BTN_DPAD_LEFT, -1, 0, 0 },
	{ "Direita",       "key_right",  "pad_right",  RETRO_DEVICE_ID_JOYPAD_RIGHT,  VK_RIGHT,  PAD_BTN_DPAD_RIGHT, -1, 0, 0 },
	{ "Botao B",       "key_b",      "pad_b",      RETRO_DEVICE_ID_JOYPAD_B,      'Z',       PAD_BTN_A, -1, 0, 0 },
	{ "Botao A",       "key_a",      "pad_a",      RETRO_DEVICE_ID_JOYPAD_A,      'X',       PAD_BTN_B, -1, 0, 0 },
	{ "Botao Y",       "key_y",      "pad_y",      RETRO_DEVICE_ID_JOYPAD_Y,      'A',       PAD_BTN_X, -1, 0, 0 },
	{ "Botao X",       "key_x",      "pad_x",      RETRO_DEVICE_ID_JOYPAD_X,      'S',       PAD_BTN_Y, -1, 0, 0 },
	{ "L (ombro)",     "key_l",      "pad_l",      RETRO_DEVICE_ID_JOYPAD_L,      'Q',       PAD_BTN_LB, -1, 0, 0 },
	{ "R (ombro)",     "key_r",      "pad_r",      RETRO_DEVICE_ID_JOYPAD_R,      'W',       PAD_BTN_RB, -1, 0, 0 },
	{ "Start",         "key_start",  "pad_start",  RETRO_DEVICE_ID_JOYPAD_START,  VK_RETURN, PAD_BTN_START, -1, 0, 0 },
	{ "Select",        "key_select", "pad_select", RETRO_DEVICE_ID_JOYPAD_SELECT, VK_SPACE,  PAD_BTN_BACK, -1, 0, 0 },
	{ "L2 (gatilho)",  "key_l2",     "pad_l2",     RETRO_DEVICE_ID_JOYPAD_L2,     'E',        PAD_BTN_LT,   -1, 0, 0 },
	{ "R2 (gatilho)",  "key_r2",     "pad_r2",     RETRO_DEVICE_ID_JOYPAD_R2,     'R',        PAD_BTN_RT,   -1, 0, 0 },
	{ "L3 (analog E)", "key_l3",     "pad_l3",     RETRO_DEVICE_ID_JOYPAD_L3,     'C',        PAD_BTN_L3,   -1, 0, 0 },
	{ "R3 (analog D)", "key_r3",     "pad_r3",     RETRO_DEVICE_ID_JOYPAD_R3,     'V',        PAD_BTN_R3,   -1, 0, 0 },
	// The left stick defaults to the arrow keys - the same keys as the d-pad.
	// That is deliberate: on a keyboard, a game that only walks on the analog
	// stick would otherwise leave the player unable to move at all. The
	// duplication is visible here in the menu and can be rebound.
	{ "Analog E Cima", "key_lsu",    "pad_lsu",    -1,                            VK_UP,      PAD_BTN_NONE,  0, 1,  1 },
	{ "Analog E Baixo","key_lsd",    "pad_lsd",    -1,                            VK_DOWN,    PAD_BTN_NONE,  0, 1, -1 },
	{ "Analog E Esq",  "key_lsl",    "pad_lsl",    -1,                            VK_LEFT,    PAD_BTN_NONE,  0, 0, -1 },
	{ "Analog E Dir",  "key_lsr",    "pad_lsr",    -1,                            VK_RIGHT,   PAD_BTN_NONE,  0, 0,  1 },
	{ "Analog D Cima", "key_rsu",    "pad_rsu",    -1,                            VK_NUMPAD8, PAD_BTN_NONE,  1, 1,  1 },
	{ "Analog D Baixo","key_rsd",    "pad_rsd",    -1,                            VK_NUMPAD2, PAD_BTN_NONE,  1, 1, -1 },
	{ "Analog D Esq",  "key_rsl",    "pad_rsl",    -1,                            VK_NUMPAD4, PAD_BTN_NONE,  1, 0, -1 },
	{ "Analog D Dir",  "key_rsr",    "pad_rsr",    -1,                            VK_NUMPAD6, PAD_BTN_NONE,  1, 0,  1 },
};

static int s_keys[BIND_COUNT] = {
	VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', VK_RETURN, VK_SPACE,
	'E', 'R', 'C', 'V',
	VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
	VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD4, VK_NUMPAD6
};

static int s_pads[BIND_COUNT] = {
	PAD_BTN_DPAD_UP, PAD_BTN_DPAD_DOWN, PAD_BTN_DPAD_LEFT, PAD_BTN_DPAD_RIGHT,
	PAD_BTN_A, PAD_BTN_B, PAD_BTN_X, PAD_BTN_Y,
	PAD_BTN_LB, PAD_BTN_RB, PAD_BTN_START, PAD_BTN_BACK,
	PAD_BTN_LT, PAD_BTN_RT, PAD_BTN_L3, PAD_BTN_R3,
	// The physical sticks feed the axes directly, so the analog bindings start
	// unassigned on a gamepad; they are there for anyone who wants to put a
	// stick direction on a button.
	PAD_BTN_NONE, PAD_BTN_NONE, PAD_BTN_NONE, PAD_BTN_NONE,
	PAD_BTN_NONE, PAD_BTN_NONE, PAD_BTN_NONE, PAD_BTN_NONE
};

// Escape and F12 open and close the menu. Letting either be bound to a game
// button would leave the player with no way back to the OSD.
static bool KeyIsReserved(int vk)
{
	return vk == VK_ESCAPE || vk == VK_F12;
}

const char* InputBindLabel(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return "?";
	return s_defs[bind].label;
}

int InputBindRetroId(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_defs[bind].retro_id;
}

bool InputBindIsAnalog(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return false;
	return s_defs[bind].stick >= 0;
}

int InputBindAnalogStick(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return -1;
	return s_defs[bind].stick;
}

int InputBindAnalogAxis(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_defs[bind].axis;
}

int InputBindAnalogSign(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_defs[bind].sign;
}

int InputBindGetKey(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_keys[bind];
}

int InputBindGetPad(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_pads[bind];
}

const char* InputBindConfigKey(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return "";
	return s_defs[bind].config_key;
}

int InputBindFromConfigKey(const char* key)
{
	if (!key) return -1;
	for (int i = 0; i < BIND_COUNT; i++)
		if (!strcmp(key, s_defs[i].config_key)) return i;
	return -1;
}

const char* InputBindPadConfigKey(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return "";
	return s_defs[bind].pad_config_key;
}

int InputBindFromPadConfigKey(const char* key)
{
	if (!key) return -1;
	for (int i = 0; i < BIND_COUNT; i++)
		if (!strcmp(key, s_defs[i].pad_config_key)) return i;
	return -1;
}

bool InputBindSetKey(int bind, int vk)
{
	if (bind < 0 || bind >= BIND_COUNT) return false;
	if (vk <= 0 || vk > 255) return false;
	if (KeyIsReserved(vk)) return false;

	// A key already used elsewhere is handed over rather than duplicated:
	// two buttons on one key means one of them can never be pressed alone.
	//
	// Only within the same group, though. A key on both a button and a stick
	// direction is not ambiguous - they drive different things - and the
	// default bindings rely on it: the arrows work the d-pad AND the left
	// stick, so a keyboard can move in games that only walk on the stick.
	const bool analog = InputBindIsAnalog(bind);
	for (int i = 0; i < BIND_COUNT; i++)
		if (i != bind && s_keys[i] == vk && InputBindIsAnalog(i) == analog)
			s_keys[i] = 0;

	s_keys[bind] = vk;
	return true;
}

bool InputBindSetPad(int bind, int pad_code)
{
	if (bind < 0 || bind >= BIND_COUNT) return false;
	if (pad_code <= 0) return false;

	// Transfer if duplicate, within the same group only - see InputBindSetKey.
	const bool analog = InputBindIsAnalog(bind);
	for (int i = 0; i < BIND_COUNT; i++)
		if (i != bind && s_pads[i] == pad_code && InputBindIsAnalog(i) == analog)
			s_pads[i] = 0;

	s_pads[bind] = pad_code;
	return true;
}

void InputBindResetDefaults()
{
	for (int i = 0; i < BIND_COUNT; i++)
	{
		s_keys[i] = s_defs[i].default_vk;
		s_pads[i] = s_defs[i].default_pad;
	}
}

const char* InputBindKeyName(int bind)
{
	static char name[BIND_COUNT][32];
	if (bind < 0 || bind >= BIND_COUNT) return "?";

	int vk = s_keys[bind];
	char* out = name[bind];

	if (vk == 0) { snprintf(out, 32, "-"); return out; }

	switch (vk)
	{
	case VK_UP:         snprintf(out, 32, "Cima");       return out;
	case VK_DOWN:       snprintf(out, 32, "Baixo");      return out;
	case VK_LEFT:       snprintf(out, 32, "Esq");        return out;
	case VK_RIGHT:      snprintf(out, 32, "Dir");        return out;
	case VK_RETURN:     snprintf(out, 32, "Enter");      return out;
	case VK_SPACE:      snprintf(out, 32, "Espaco");     return out;
	case VK_ESCAPE:     snprintf(out, 32, "Esc");        return out;
	case VK_LSHIFT:     snprintf(out, 32, "Shift Esq");  return out;
	case VK_RSHIFT:     snprintf(out, 32, "Shift Dir");  return out;
	case VK_LCONTROL:   snprintf(out, 32, "Ctrl Esq");   return out;
	case VK_RCONTROL:   snprintf(out, 32, "Ctrl Dir");   return out;
	case VK_MENU:
	case VK_LMENU:      snprintf(out, 32, "Alt Esq");    return out;
	case VK_RMENU:      snprintf(out, 32, "Alt Dir");    return out;
	case VK_TAB:        snprintf(out, 32, "Tab");        return out;
	case VK_BACK:       snprintf(out, 32, "Backspace");  return out;
	case VK_CAPITAL:    snprintf(out, 32, "Caps Lock");  return out;
	case VK_PAUSE:      snprintf(out, 32, "Pause");      return out;
	case VK_PRIOR:      snprintf(out, 32, "Page Up");    return out;
	case VK_NEXT:       snprintf(out, 32, "Page Down");  return out;
	case VK_HOME:       snprintf(out, 32, "Home");       return out;
	case VK_END:        snprintf(out, 32, "End");        return out;
	case VK_INSERT:     snprintf(out, 32, "Insert");     return out;
	case VK_DELETE:     snprintf(out, 32, "Delete");     return out;
	case VK_NUMLOCK:    snprintf(out, 32, "Num Lock");   return out;
	case VK_SCROLL:     snprintf(out, 32, "Scroll");     return out;
	case VK_MULTIPLY:   snprintf(out, 32, "Num *");      return out;
	case VK_ADD:        snprintf(out, 32, "Num +");      return out;
	case VK_SUBTRACT:   snprintf(out, 32, "Num -");      return out;
	case VK_DECIMAL:    snprintf(out, 32, "Num .");      return out;
	case VK_DIVIDE:     snprintf(out, 32, "Num /");      return out;
	case VK_OEM_1:      snprintf(out, 32, ";");          return out;
	case VK_OEM_PLUS:   snprintf(out, 32, "=");          return out;
	case VK_OEM_COMMA:  snprintf(out, 32, ",");          return out;
	case VK_OEM_MINUS:  snprintf(out, 32, "-");          return out;
	case VK_OEM_PERIOD: snprintf(out, 32, ".");          return out;
	case VK_OEM_2:      snprintf(out, 32, "/");          return out;
	case VK_OEM_3:      snprintf(out, 32, "`");          return out;
	case VK_OEM_4:      snprintf(out, 32, "[");          return out;
	case VK_OEM_5:      snprintf(out, 32, "\\");         return out;
	case VK_OEM_6:      snprintf(out, 32, "]");          return out;
	case VK_OEM_7:      snprintf(out, 32, "'");          return out;
	default: break;
	}

	if (vk >= VK_F1 && vk <= VK_F24) { snprintf(out, 32, "F%d", vk - VK_F1 + 1); return out; }
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(out, 32, "Num %d", vk - VK_NUMPAD0); return out; }
	if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { out[0] = (char)vk; out[1] = '\0'; return out; }

	snprintf(out, 32, "VK %d", vk);
	return out;
}

const char* InputBindPadName(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return "?";

	int code = s_pads[bind];
	switch (code)
	{
	case PAD_BTN_A:          return "Pad A";
	case PAD_BTN_B:          return "Pad B";
	case PAD_BTN_X:          return "Pad X";
	case PAD_BTN_Y:          return "Pad Y";
	case PAD_BTN_LB:         return "LB (L1)";
	case PAD_BTN_RB:         return "RB (R1)";
	case PAD_BTN_LT:         return "LT (L2)";
	case PAD_BTN_RT:         return "RT (R2)";
	case PAD_BTN_START:      return "Start";
	case PAD_BTN_BACK:       return "Select/Back";
	case PAD_BTN_L3:         return "L3 (LS)";
	case PAD_BTN_R3:         return "R3 (RS)";
	case PAD_BTN_DPAD_UP:    return "D-Pad Cima";
	case PAD_BTN_DPAD_DOWN:  return "D-Pad Baixo";
	case PAD_BTN_DPAD_LEFT:  return "D-Pad Esq";
	case PAD_BTN_DPAD_RIGHT: return "D-Pad Dir";
	case 0:                  return "-";
	default:                 return "Pad ?";
	}
}

// The skip list is shared by the flush and the scan.
static bool CaptureSkips(int vk)
{
	static const int skip[] = {
		VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2,
		VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, VK_CAPITAL
	};
	for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
		if (skip[i] == vk) return true;
	return false;
}

void InputCaptureFlush()
{
	for (int vk = 1; vk <= 255; vk++) GetAsyncKeyState(vk);
}

bool InputCaptureIsAllReleased()
{
	for (int vk = 1; vk <= 255; vk++)
	{
		if (CaptureSkips(vk)) continue;
		if (GetAsyncKeyState(vk) & 0x8000) return false;
	}

	for (int i = 0; i < 4; i++)
	{
		XINPUT_STATE st;
		if (GamepadGetState(i, &st))
		{
			if (st.Gamepad.wButtons != 0) return false;
			if (st.Gamepad.bLeftTrigger > 50 || st.Gamepad.bRightTrigger > 50) return false;
		}
	}

	return true;
}

int InputCaptureScanKey()
{
	for (int vk = 1; vk <= 255; vk++)
	{
		if (CaptureSkips(vk)) continue;
		SHORT st = GetAsyncKeyState(vk);
		if (st & 0x8000) return vk;
		if (st & 0x0001) return vk;
	}
	return 0;
}

int InputCaptureScanPad()
{
	for (int i = 0; i < 4; i++)
	{
		XINPUT_STATE st;
		if (!GamepadGetState(i, &st)) continue;

		WORD w = st.Gamepad.wButtons;
		if (w & XINPUT_GAMEPAD_A) return PAD_BTN_A;
		if (w & XINPUT_GAMEPAD_B) return PAD_BTN_B;
		if (w & XINPUT_GAMEPAD_X) return PAD_BTN_X;
		if (w & XINPUT_GAMEPAD_Y) return PAD_BTN_Y;
		if (w & XINPUT_GAMEPAD_LEFT_SHOULDER) return PAD_BTN_LB;
		if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) return PAD_BTN_RB;
		if (w & XINPUT_GAMEPAD_START) return PAD_BTN_START;
		if (w & XINPUT_GAMEPAD_BACK) return PAD_BTN_BACK;
		if (w & XINPUT_GAMEPAD_LEFT_THUMB) return PAD_BTN_L3;
		if (w & XINPUT_GAMEPAD_RIGHT_THUMB) return PAD_BTN_R3;
		if (w & XINPUT_GAMEPAD_DPAD_UP) return PAD_BTN_DPAD_UP;
		if (w & XINPUT_GAMEPAD_DPAD_DOWN) return PAD_BTN_DPAD_DOWN;
		if (w & XINPUT_GAMEPAD_DPAD_LEFT) return PAD_BTN_DPAD_LEFT;
		if (w & XINPUT_GAMEPAD_DPAD_RIGHT) return PAD_BTN_DPAD_RIGHT;

		if (st.Gamepad.bLeftTrigger > 50) return PAD_BTN_LT;
		if (st.Gamepad.bRightTrigger > 50) return PAD_BTN_RT;
	}
	return 0;
}

const char* InputPresetName(int preset)
{
	switch (preset)
	{
	case PRESET_MISTER_NINTENDO: return "MiSTer / Nintendo";
	case PRESET_XBOX_NATIVE:     return "Xbox Nativo";
	case PRESET_PLAYSTATION:     return "PlayStation";
	case PRESET_8BITDO_M30:      return "8BitDo M30 (Cabo/XInput)";
	case PRESET_ARCADE_6BTN:     return "Arcade 6-Botoes";
	default: return "Custom";
	}
}

static int s_current_preset = PRESET_MISTER_NINTENDO;

int InputBindGetPreset()
{
	return s_current_preset;
}

void InputBindSetPresetTag(int preset)
{
	if (preset >= 0 && preset < PRESET_COUNT) s_current_preset = preset;
}

void InputBindApplyPreset(int preset)
{
	s_current_preset = preset;
	if (preset == PRESET_MISTER_NINTENDO)
	{
		// Padrao MiSTer: Xbox A->Retro B, Xbox B->Retro A, Xbox X->Retro Y, Xbox Y->Retro X
		s_pads[BIND_B] = PAD_BTN_A;
		s_pads[BIND_A] = PAD_BTN_B;
		s_pads[BIND_Y] = PAD_BTN_X;
		s_pads[BIND_X] = PAD_BTN_Y;
		s_pads[BIND_L] = PAD_BTN_LB;
		s_pads[BIND_R] = PAD_BTN_RB;
		s_pads[BIND_L2] = PAD_BTN_LT;
		s_pads[BIND_R2] = PAD_BTN_RT;
	}
	else if (preset == PRESET_XBOX_NATIVE)
	{
		// Xbox Nativo: A->A, B->B, X->X, Y->Y
		s_pads[BIND_A] = PAD_BTN_A;
		s_pads[BIND_B] = PAD_BTN_B;
		s_pads[BIND_X] = PAD_BTN_X;
		s_pads[BIND_Y] = PAD_BTN_Y;
		s_pads[BIND_L] = PAD_BTN_LB;
		s_pads[BIND_R] = PAD_BTN_RB;
		s_pads[BIND_L2] = PAD_BTN_LT;
		s_pads[BIND_R2] = PAD_BTN_RT;
	}
	else if (preset == PRESET_PLAYSTATION)
	{
		// PlayStation: X(Sul)->Retro B, O(Leste)->Retro A, Square(Oeste)->Retro Y, Triangle(Norte)->Retro X
		s_pads[BIND_B] = PAD_BTN_A;
		s_pads[BIND_A] = PAD_BTN_B;
		s_pads[BIND_Y] = PAD_BTN_X;
		s_pads[BIND_X] = PAD_BTN_Y;
		s_pads[BIND_L] = PAD_BTN_LB;
		s_pads[BIND_R] = PAD_BTN_RB;
		s_pads[BIND_L2] = PAD_BTN_LT;
		s_pads[BIND_R2] = PAD_BTN_RT;
	}
	else if (preset == PRESET_8BITDO_M30)
	{
		// 8BitDo M30 (XInput / Cabo USB):
		// No firmware XInput da 8BitDo:
		//  - Botoes frontais: A=A, B=B, C=RT, X=X, Y=Y, Z=RB
		//  - Ombros fisicos:  L=LB, R=LT
		// Mapeamento correto para que os botoes fisicos correspondam a serigrafia:
		//  - A -> Retro B (Jump/Cancel)
		//  - B -> Retro A (Confirm/Action)
		//  - X -> Retro Y (Attack/Shoot)
		//  - Y -> Retro X
		//  - Ombro L (LB) -> Retro L, Ombro R (LT) -> Retro R
		//  - Z (RB) -> Retro L2, C (RT) -> Retro R2
		s_pads[BIND_B] = PAD_BTN_A;
		s_pads[BIND_A] = PAD_BTN_B;
		s_pads[BIND_Y] = PAD_BTN_X;
		s_pads[BIND_X] = PAD_BTN_Y;
		s_pads[BIND_L] = PAD_BTN_LB;
		s_pads[BIND_R] = PAD_BTN_LT;
		// L2/R2 on Z and C. They used to sit on the same LB/LT as L/R, so on
		// PlayStation or N64 every L1 press was also an L2 press.
		s_pads[BIND_L2] = PAD_BTN_RB;
		s_pads[BIND_R2] = PAD_BTN_RT;
	}
	else if (preset == PRESET_ARCADE_6BTN)
	{
		// Arcade 6-Botoes (Capcom / SNK):
		// LP(X), MP(Y), HP(RB)
		// LK(A), MK(B), HK(RT)
		s_pads[BIND_Y] = PAD_BTN_X;
		s_pads[BIND_X] = PAD_BTN_Y;
		s_pads[BIND_L] = PAD_BTN_LB;
		s_pads[BIND_R] = PAD_BTN_RB;
		s_pads[BIND_B] = PAD_BTN_A;
		s_pads[BIND_A] = PAD_BTN_B;
		s_pads[BIND_R2] = PAD_BTN_RT;
		s_pads[BIND_L2] = PAD_BTN_LT;
	}
}

static std::string SanitizeSystemName(const char* name)
{
	if (!name) return "";
	std::string s = name;
	for (char& c : s)
	{
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ' ')
			c = '_';
	}
	return s;
}

static std::string GetProfilePath(const char* system_name)
{
	std::string clean = SanitizeSystemName(system_name);
	if (clean.empty()) return "";
	return "Config/remaps/" + clean + ".cfg";
}

// The global bindings while a system profile is swapped in (see input_map.h).
static int s_global_keys[BIND_COUNT];
static int s_global_pads[BIND_COUNT];
static bool s_profile_active = false;
static std::string s_profile_name;

void InputBindRestoreGlobal()
{
	if (!s_profile_active) return;
	memcpy(s_keys, s_global_keys, sizeof(s_keys));
	memcpy(s_pads, s_global_pads, sizeof(s_pads));
	s_profile_active = false;
	s_profile_name.clear();
}

const char* InputBindActiveProfile()
{
	return s_profile_active ? s_profile_name.c_str() : NULL;
}

int InputBindGetGlobalKey(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_profile_active ? s_global_keys[bind] : s_keys[bind];
}

int InputBindGetGlobalPad(int bind)
{
	if (bind < 0 || bind >= BIND_COUNT) return 0;
	return s_profile_active ? s_global_pads[bind] : s_pads[bind];
}

bool InputBindSaveSystemProfile(const char* system_name)
{
	std::string path = GetProfilePath(system_name);
	if (path.empty()) return false;

	std::error_code ec;
	fs::create_directories("Config/remaps", ec);

	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return false;

	fprintf(f, "# Karamelo - Controller Remap for %s\n", system_name);
	for (int i = 0; i < BIND_COUNT; i++)
	{
		fprintf(f, "%s=%d\n", InputBindConfigKey(i), s_keys[i]);
		fprintf(f, "%s=%d\n", InputBindPadConfigKey(i), s_pads[i]);
	}
	fclose(f);
	return true;
}

bool InputBindLoadSystemProfile(const char* system_name)
{
	std::string path = GetProfilePath(system_name);
	if (path.empty()) return false;

	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;

	// Built on top of the global set and copied in verbatim, not through
	// InputBindSetKey/SetPad: those refuse 0, so a binding the profile left
	// unassigned kept whatever the previous profile had put there, and their
	// duplicate hand-over could move a key the profile placed on purpose.
	if (!s_profile_active)
	{
		memcpy(s_global_keys, s_keys, sizeof(s_keys));
		memcpy(s_global_pads, s_pads, sizeof(s_pads));
	}
	int keys[BIND_COUNT], pads[BIND_COUNT];
	memcpy(keys, s_global_keys, sizeof(keys));
	memcpy(pads, s_global_pads, sizeof(pads));

	char line[256];
	while (fgets(line, sizeof(line), f))
	{
		if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		const char* key = line;
		int iv = atoi(eq + 1);

		int bind_k = InputBindFromConfigKey(key);
		if (bind_k >= 0 && iv >= 0 && iv <= 255 && !KeyIsReserved(iv)) keys[bind_k] = iv;

		int bind_p = InputBindFromPadConfigKey(key);
		if (bind_p >= 0 && iv >= 0) pads[bind_p] = iv;
	}
	fclose(f);

	memcpy(s_keys, keys, sizeof(s_keys));
	memcpy(s_pads, pads, sizeof(s_pads));
	s_profile_active = true;
	s_profile_name = system_name;
	return true;
}

// ---------------------------------------------------------------------------
// 6-button pads. Where a pad's A B C / X Y Z / L R arrive is a SixPadMap:
// the player's calibration for that controller (Config/pads/), or one of two
// built-in tables. Firmwares disagree on C, Z and the shoulders, which is why
// calibration wins over any table:
//   Sega standard (Retro-Bit, Saturn USB, DInput): C=RB Z=LB L=LT R=RT
//   8BitDo M30 over XInput:                        C=RT Z=RB L=LB R=LT
// Each system's table comes from that core's own RetroPad mapping (libretro
// docs), so the console's A B C / X Y Z land where they sit on the real pad.
// ---------------------------------------------------------------------------
namespace {
int BindForRetroId(int id)
{
	switch (id)
	{
	case 0: return BIND_B;   case 1: return BIND_Y;
	case 8: return BIND_A;   case 9: return BIND_X;
	case 10: return BIND_L;  case 11: return BIND_R;
	case 12: return BIND_L2; case 13: return BIND_R2;
	default: return -1;
	}
}

bool LabelHas(const char* label, const char* word)
{
	if (!label || !word) return false;
	const size_t n = strlen(word);
	for (const char* p = label; *p; ++p)
	{
		size_t i = 0;
		while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)word[i])) i++;
		if (i == n) return true;
	}
	return false;
}

// 0 weak, 1 medium, 2 heavy; -1 when the label says neither. Capcom's own
// names call the medium punch "Strong", FBNeo calls the heavy one "Strong
// Punch" next to a "Medium Punch" - so "strong" means heavy only when the
// game also has a "medium" button.
int StrengthOf(const char* label, bool game_has_medium)
{
	if (LabelHas(label, "weak") || LabelHas(label, "light") || LabelHas(label, "jab") || LabelHas(label, "short") || LabelHas(label, "fraco")) return 0;
	if (LabelHas(label, "medium") || LabelHas(label, "forward") || LabelHas(label, "medio")) return 1;
	if (LabelHas(label, "heavy") || LabelHas(label, "hard") || LabelHas(label, "fierce") || LabelHas(label, "roundhouse") || LabelHas(label, "forte")) return 2;
	if (LabelHas(label, "strong")) return game_has_medium ? 2 : 1;
	return -1;
}
} // namespace

SixPadMap InputSixPadBuiltin(int kind)
{
	SixPadMap m;
	m.a = PAD_BTN_A; m.b = PAD_BTN_B; m.x = PAD_BTN_X; m.y = PAD_BTN_Y;
	if (kind == SIXPAD_M30_XINPUT)
	{
		m.c = PAD_BTN_RT; m.z = PAD_BTN_RB; m.l = PAD_BTN_LB; m.r = PAD_BTN_LT;
	}
	else
	{
		m.c = PAD_BTN_RB; m.z = PAD_BTN_LB; m.l = PAD_BTN_LT; m.r = PAD_BTN_RT;
	}
	return m;
}

bool InputNameLooksSixButton(const char* name)
{
	if (!name || !*name) return false;
	// Names, never a whole vendor: 8BitDo also makes the SN30 Pro and the
	// Ultimate, which have four face buttons. Over XInput an M30 calls itself
	// "Xbox 360 Controller" and cannot be told apart at all - that is what the
	// calibration (InputSixPadSave) is for.
	static const char* const kWords[] = {
		"m30", "saturn", "mega drive", "megadrive", "genesis", "md6", "6b ",
		"fighting commander", "fightstick", "fight stick", "arcade stick",
		"fighting stick", "arcade pad", "neo geo", "neogeo",
	};
	for (const char* w : kWords)
		if (LabelHas(name, w)) return true;
	return false;
}

// Calibrations live one per controller model, keyed by what SDL reports
// (vendor/product and name - see GamepadGetDeviceKey), like RetroArch's
// autoconfig profiles: the pad is recognised the next time it is plugged in.
static std::string SixPadPath(const char* device_key)
{
	std::string clean = SanitizeSystemName(device_key);
	if (clean.empty()) return "";
	for (char& c : clean)
		if (!isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
	if (clean.size() > 80) clean.resize(80);
	return "Config/pads/" + clean + ".cfg";
}

static const char* const kSixPadKeys[8] = { "six_a", "six_b", "six_c", "six_x", "six_y", "six_z", "six_l", "six_r" };

static int* SixPadField(SixPadMap& m, int i)
{
	int* f[8] = { &m.a, &m.b, &m.c, &m.x, &m.y, &m.z, &m.l, &m.r };
	return f[i];
}

bool InputSixPadSave(const char* device_key, const SixPadMap& map)
{
	std::string path = SixPadPath(device_key);
	if (path.empty()) return false;
	std::error_code ec;
	fs::create_directories("Config/pads", ec);
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return false;
	SixPadMap m = map;
	fprintf(f, "# Karamelo - 6-button calibration for %s\n", device_key);
	for (int i = 0; i < 8; i++) fprintf(f, "%s=%d\n", kSixPadKeys[i], *SixPadField(m, i));
	fclose(f);
	return true;
}

bool InputSixPadLoad(const char* device_key, SixPadMap* out)
{
	std::string path = SixPadPath(device_key);
	if (path.empty()) return false;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	SixPadMap m;
	int seen = 0;
	char line[128];
	while (fgets(line, sizeof(line), f))
	{
		char* eq = strchr(line, '=');
		if (line[0] == '#' || !eq) continue;
		*eq = '\0';
		const int iv = atoi(eq + 1);
		for (int i = 0; i < 8; i++)
		{
			if (strcmp(line, kSixPadKeys[i]) == 0 && iv >= 0 && iv <= 0xFFFF)
			{
				*SixPadField(m, i) = iv;
				seen |= 1 << i;
			}
		}
	}
	fclose(f);
	// The six face buttons are required; the shoulders may have been skipped.
	if ((seen & 0x3F) != 0x3F) return false;
	if (out) *out = m;
	return true;
}

bool InputSixPadHas(const char* device_key)
{
	return InputSixPadLoad(device_key, nullptr);
}

bool InputSixPadDelete(const char* device_key)
{
	std::string path = SixPadPath(device_key);
	if (path.empty()) return false;
	std::error_code ec;
	return fs::remove(path, ec);
}

bool InputBindApplySixButtonLayout(const char* system_name, const char* const labels[16], const SixPadMap* pad)
{
	if (!system_name) return false;
	const std::string sys = system_name;
	int layout[BIND_COUNT];
	for (int i = 0; i < BIND_COUNT; i++) layout[i] = -1;   // -1 = keep the global binding

	auto clear_face = [&]() {
		for (int b : {BIND_B, BIND_A, BIND_Y, BIND_X, BIND_L, BIND_R, BIND_L2, BIND_R2}) layout[b] = PAD_BTN_NONE;
	};

	// Where the pad's own A B C / X Y Z / L R are: a calibration, or one of the
	// built-in tables (Sega standard when nothing is given).
	const SixPadMap m = pad ? *pad : InputSixPadBuiltin(SIXPAD_SEGA_STANDARD);
	const int sixA = m.a, sixB = m.b, sixC = m.c;
	const int sixX = m.x, sixY = m.y, sixZ = m.z;
	const int sixL = m.l, sixR = m.r;

	if (sys == "Genesis" || sys == "Mega CD" || sys == "Sega 32X")
	{
		clear_face();
		// Genesis Plus GX / PicoDrive: Y=A, B=B, A=C, L=X, X=Y, R=Z
		layout[BIND_Y] = sixA; layout[BIND_B] = sixB; layout[BIND_A] = sixC;
		layout[BIND_L] = sixX; layout[BIND_X] = sixY; layout[BIND_R] = sixZ;
		layout[BIND_L2] = sixL; layout[BIND_R2] = sixR;
	}
	else if (sys == "Saturn")
	{
		clear_face();
		// Beetle Saturn: B=A, A=B, R=C, Y=X, X=Y, L=Z, L2=L, R2=R
		layout[BIND_B] = sixA; layout[BIND_A] = sixB; layout[BIND_R] = sixC;
		layout[BIND_Y] = sixX; layout[BIND_X] = sixY; layout[BIND_L] = sixZ;
		layout[BIND_L2] = sixL; layout[BIND_R2] = sixR;
	}
	else if (sys == "Dreamcast")
	{
		clear_face();
		// Flycast: B=A, A=B, Y=X, X=Y
		// L2 = Left Trigger (Physical Shoulder L)
		// R2 = Right Trigger (Physical Shoulder R)
		// L  = Arcade C (Face button C)
		// R  = Arcade Z (Face button Z)
		layout[BIND_B] = sixA;  // Face A -> DC A
		layout[BIND_A] = sixB;  // Face B -> DC B
		layout[BIND_Y] = sixX;  // Face X -> DC X
		layout[BIND_X] = sixY;  // Face Y -> DC Y
		layout[BIND_L2] = sixL; // Shoulder L -> DC Left Trigger (Flashlight / Brake)
		layout[BIND_R2] = sixR; // Shoulder R -> DC Right Trigger (Aim / Gas)
		layout[BIND_L] = sixC;  // Face C -> DC Arcade C (Heavy Kick)
		layout[BIND_R] = sixZ;  // Face Z -> DC Arcade Z (Heavy Punch)
	}
	else if (sys == "GameCube")
	{
		// Dolphin: the GameCube's L / R triggers are RetroPad L2 / R2, its Z is
		// RetroPad R, and RetroPad L is the Triforce test button. With the
		// global set an M30 had the triggers (zoom, aim) on its Z and C face
		// buttons and its shoulders on Z and nothing. A B X Y keep the global
		// bindings: only the shoulders move.
		layout[BIND_L2] = sixL;  // shoulder L -> GameCube L
		layout[BIND_R2] = sixR;  // shoulder R -> GameCube R
		layout[BIND_R]  = sixZ;  // Z -> GameCube Z
		layout[BIND_L]  = PAD_BTN_NONE;   // Triforce test: off the pad
	}
	else if (sys == "3DO")
	{
		clear_face();
		// Opera (3DO): Y=A, B=B, A=C, L=X, R=Z, L2/R2=shoulders
		layout[BIND_Y] = sixA; layout[BIND_B] = sixB; layout[BIND_A] = sixC;
		layout[BIND_L] = sixX; layout[BIND_R] = sixZ;
		layout[BIND_L2] = sixL; layout[BIND_R2] = sixR;
	}
	else if (sys == "Arcade")
	{
		if (!labels) return false;
		bool has_medium = false;
		for (int id = 0; id < 16; id++)
			if (labels[id] && (LabelHas(labels[id], "medium") || LabelHas(labels[id], "medio"))) has_medium = true;
		int punch[3] = {-1, -1, -1}, kick[3] = {-1, -1, -1};
		int found = 0;
		for (int id = 0; id < 16; id++)
		{
			const int bind = BindForRetroId(id);
			if (bind < 0 || !labels[id]) continue;
			const bool is_punch = LabelHas(labels[id], "punch") || LabelHas(labels[id], "soco");
			const bool is_kick = LabelHas(labels[id], "kick") || LabelHas(labels[id], "chute");
			const int s = StrengthOf(labels[id], has_medium);
			if (s < 0 || (!is_punch && !is_kick)) continue;
			int* row = is_punch ? punch : kick;
			if (row[s] < 0) { row[s] = bind; found++; }
		}
		if (found < 4) return false;   // not a punch/kick fighting game
		clear_face();
		const int top[3] = {sixX, sixY, sixZ}, bottom[3] = {sixA, sixB, sixC};
		for (int s = 0; s < 3; s++)
		{
			if (punch[s] >= 0) layout[punch[s]] = top[s];
			if (kick[s] >= 0) layout[kick[s]] = bottom[s];
		}
		// The shoulders take L2/R2 only where no punch or kick went: a game
		// can name its heavy kick on L2, and that must not be overwritten.
		if (layout[BIND_L2] == PAD_BTN_NONE) layout[BIND_L2] = sixL;
		if (layout[BIND_R2] == PAD_BTN_NONE) layout[BIND_R2] = sixR;
	}
	else
	{
		return false;
	}

	if (!s_profile_active)
	{
		memcpy(s_global_keys, s_keys, sizeof(s_keys));
		memcpy(s_global_pads, s_pads, sizeof(s_pads));
	}
	memcpy(s_keys, s_global_keys, sizeof(s_keys));
	memcpy(s_pads, s_global_pads, sizeof(s_pads));
	for (int b = 0; b < BIND_COUNT; b++)
		if (layout[b] >= 0) s_pads[b] = layout[b];
	s_profile_active = true;
	s_profile_name = sys + " (6 botoes)";
	return true;
}

bool InputBindHasSystemProfile(const char* system_name)
{
	std::string path = GetProfilePath(system_name);
	if (path.empty()) return false;
	std::error_code ec;
	return fs::exists(path, ec);
}

bool InputBindDeleteSystemProfile(const char* system_name)
{
	std::string path = GetProfilePath(system_name);
	if (path.empty()) return false;
	std::error_code ec;
	return fs::remove(path, ec);
}

int InputEmulatedMouseButtons(bool pad_as_mouse, unsigned pad_buttons, bool key_space, bool key_enter, bool touchpad_tap)
{
	if (!pad_as_mouse) return 0;
	int btns = 0;
	if (pad_buttons & (PAD_BTN_A | PAD_BTN_RT)) btns |= 1;
	if (pad_buttons & (PAD_BTN_B | PAD_BTN_LT)) btns |= 2;
	if (key_space || key_enter || touchpad_tap) btns |= 1;
	return btns;
}

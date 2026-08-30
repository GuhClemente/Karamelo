#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <string.h>

#include "input_map.h"
#include "libretro.h"

struct BindDef
{
	const char* label;
	const char* config_key;
	const char* pad_config_key;
	int retro_id;
	int default_vk;
	int default_pad;
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
	{ "Cima",          "key_up",     "pad_up",     RETRO_DEVICE_ID_JOYPAD_UP,     VK_UP,     PAD_BTN_DPAD_UP },
	{ "Baixo",         "key_down",   "pad_down",   RETRO_DEVICE_ID_JOYPAD_DOWN,   VK_DOWN,   PAD_BTN_DPAD_DOWN },
	{ "Esquerda",      "key_left",   "pad_left",   RETRO_DEVICE_ID_JOYPAD_LEFT,   VK_LEFT,   PAD_BTN_DPAD_LEFT },
	{ "Direita",       "key_right",  "pad_right",  RETRO_DEVICE_ID_JOYPAD_RIGHT,  VK_RIGHT,  PAD_BTN_DPAD_RIGHT },
	{ "Botao B",       "key_b",      "pad_b",      RETRO_DEVICE_ID_JOYPAD_B,      'Z',       PAD_BTN_A },
	{ "Botao A",       "key_a",      "pad_a",      RETRO_DEVICE_ID_JOYPAD_A,      'X',       PAD_BTN_B },
	{ "Botao Y",       "key_y",      "pad_y",      RETRO_DEVICE_ID_JOYPAD_Y,      'A',       PAD_BTN_X },
	{ "Botao X",       "key_x",      "pad_x",      RETRO_DEVICE_ID_JOYPAD_X,      'S',       PAD_BTN_Y },
	{ "L (ombro)",     "key_l",      "pad_l",      RETRO_DEVICE_ID_JOYPAD_L,      'Q',       PAD_BTN_LB },
	{ "R (ombro)",     "key_r",      "pad_r",      RETRO_DEVICE_ID_JOYPAD_R,      'W',       PAD_BTN_RB },
	{ "Start",         "key_start",  "pad_start",  RETRO_DEVICE_ID_JOYPAD_START,  VK_RETURN, PAD_BTN_START },
	{ "Select",        "key_select", "pad_select", RETRO_DEVICE_ID_JOYPAD_SELECT, VK_SPACE,  PAD_BTN_BACK },
};

static int s_keys[BIND_COUNT] = {
	VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', VK_RETURN, VK_SPACE
};

static int s_pads[BIND_COUNT] = {
	PAD_BTN_DPAD_UP, PAD_BTN_DPAD_DOWN, PAD_BTN_DPAD_LEFT, PAD_BTN_DPAD_RIGHT,
	PAD_BTN_A, PAD_BTN_B, PAD_BTN_X, PAD_BTN_Y,
	PAD_BTN_LB, PAD_BTN_RB, PAD_BTN_START, PAD_BTN_BACK
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
	for (int i = 0; i < BIND_COUNT; i++)
		if (i != bind && s_keys[i] == vk) s_keys[i] = 0;

	s_keys[bind] = vk;
	return true;
}

bool InputBindSetPad(int bind, int pad_code)
{
	if (bind < 0 || bind >= BIND_COUNT) return false;
	if (pad_code <= 0) return false;

	// Transfer if duplicate
	for (int i = 0; i < BIND_COUNT; i++)
		if (i != bind && s_pads[i] == pad_code) s_pads[i] = 0;

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

	if (vk == 0) { strcpy(out, "-"); return out; }

	switch (vk)
	{
	case VK_UP:      strcpy(out, "Seta Cima");  return out;
	case VK_DOWN:    strcpy(out, "Seta Baixo"); return out;
	case VK_LEFT:    strcpy(out, "Seta Esq");   return out;
	case VK_RIGHT:   strcpy(out, "Seta Dir");   return out;
	case VK_RETURN:  strcpy(out, "Enter");      return out;
	case VK_SPACE:   strcpy(out, "Espaco");     return out;
	case VK_LSHIFT:  strcpy(out, "Shift Esq");  return out;
	case VK_RSHIFT:  strcpy(out, "Shift Dir");  return out;
	case VK_LCONTROL:strcpy(out, "Ctrl Esq");   return out;
	case VK_RCONTROL:strcpy(out, "Ctrl Dir");   return out;
	case VK_TAB:     strcpy(out, "Tab");        return out;
	case VK_BACK:    strcpy(out, "Backspace");  return out;
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

	for (DWORD i = 0; i < 4; i++)
	{
		XINPUT_STATE st;
		if (XInputGetState(i, &st) == ERROR_SUCCESS)
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
	for (DWORD i = 0; i < 4; i++)
	{
		XINPUT_STATE st;
		if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;

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

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

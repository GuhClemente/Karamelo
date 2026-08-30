#ifndef INPUT_MAP_H_INCLUDED
#define INPUT_MAP_H_INCLUDED

// Player 1 keyboard and gamepad bindings.
//
// The keys and gamepad buttons live in tables here, which the input poll reads
// and the menu writes.

// Index into the table. Ordered the way the menu lists them.
enum InputBind
{
	BIND_UP = 0,
	BIND_DOWN,
	BIND_LEFT,
	BIND_RIGHT,
	BIND_B,
	BIND_A,
	BIND_Y,
	BIND_X,
	BIND_L,
	BIND_R,
	BIND_START,
	BIND_SELECT,
	BIND_COUNT
};

// Gamepad button bitmask codes (compatible with XInput button masks)
enum PadButtonCode
{
	PAD_BTN_NONE       = 0,
	PAD_BTN_DPAD_UP    = 0x0001,
	PAD_BTN_DPAD_DOWN  = 0x0002,
	PAD_BTN_DPAD_LEFT  = 0x0004,
	PAD_BTN_DPAD_RIGHT = 0x0008,
	PAD_BTN_START      = 0x0010,
	PAD_BTN_BACK       = 0x0020,
	PAD_BTN_L3         = 0x0040,
	PAD_BTN_R3         = 0x0080,
	PAD_BTN_LB         = 0x0100,
	PAD_BTN_RB         = 0x0200,
	PAD_BTN_LT         = 0x0400,
	PAD_BTN_RT         = 0x0800,
	PAD_BTN_A          = 0x1000,
	PAD_BTN_B          = 0x2000,
	PAD_BTN_X          = 0x4000,
	PAD_BTN_Y          = 0x8000
};

// Human label, e.g. "Cima" or "B (acao 1)".
const char* InputBindLabel(int bind);

// The libretro RETRO_DEVICE_ID_JOYPAD_* this binding drives.
int InputBindRetroId(int bind);

// Keyboard bindings: Virtual-Key code and printable name ("Z", "Seta Cima", "Enter").
int InputBindGetKey(int bind);
const char* InputBindKeyName(int bind);
bool InputBindSetKey(int bind, int vk);
const char* InputBindConfigKey(int bind);
int InputBindFromConfigKey(const char* key);

// Gamepad bindings: PadButtonCode and printable name ("Pad A", "LB (L1)", "D-Pad Cima").
int InputBindGetPad(int bind);
const char* InputBindPadName(int bind);
bool InputBindSetPad(int bind, int pad_code);
const char* InputBindPadConfigKey(int bind);
int InputBindFromPadConfigKey(const char* key);

void InputBindResetDefaults();

// Clears the "pressed since last call" bit for every key, so a press made
// before capture opened cannot be picked up as the binding.
void InputCaptureFlush();

// Checks if all keyboard keys and gamepad buttons/triggers are released.
bool InputCaptureIsAllReleased();

// Scans for a key pressed since the last flush, or held right now. Returns 0
// when nothing was pressed. Ignores modifiers on their own.
int InputCaptureScanKey();

// Scans for any gamepad button pressed across all connected XInput controllers.
int InputCaptureScanPad();

#endif

#include "test_framework.h"
#include "input_map.h"

// One press, one action. With "Controle como Mouse" off (the default) nothing
// on the pad or the keyboard may also click the emulated mouse: on FM Towns the
// mouse is on player 2's port, so every shot in Tatsujin also pressed player
// 2's start, and the core battery never noticed - it loads and closes games,
// it does not press buttons.
TEST_CASE(PadDoesNotClickMouseUnlessAsked)
{
	const unsigned every_button = 0xFFFF;
	ASSERT_EQ(InputEmulatedMouseButtons(false, every_button, true, true), 0);
	ASSERT_EQ(InputEmulatedMouseButtons(false, PAD_BTN_A, false, false), 0);
	ASSERT_EQ(InputEmulatedMouseButtons(false, PAD_BTN_RT, false, false), 0);
	ASSERT_EQ(InputEmulatedMouseButtons(false, 0, true, false), 0);  // Space
	ASSERT_EQ(InputEmulatedMouseButtons(false, 0, false, true), 0);  // Enter
}

TEST_CASE(PadAsMouseMapsClicks)
{
	ASSERT_EQ(InputEmulatedMouseButtons(true, PAD_BTN_A, false, false), 1);
	ASSERT_EQ(InputEmulatedMouseButtons(true, PAD_BTN_RT, false, false), 1);
	ASSERT_EQ(InputEmulatedMouseButtons(true, PAD_BTN_B, false, false), 2);
	ASSERT_EQ(InputEmulatedMouseButtons(true, PAD_BTN_LT, false, false), 2);
	ASSERT_EQ(InputEmulatedMouseButtons(true, 0, true, false), 1);
	ASSERT_EQ(InputEmulatedMouseButtons(true, 0, false, false), 0);
	// Directions and the other face buttons stay pad-only even in mouse mode.
	ASSERT_EQ(InputEmulatedMouseButtons(true, PAD_BTN_X | PAD_BTN_Y | PAD_BTN_START | PAD_BTN_DPAD_UP, false, false), 0);
}

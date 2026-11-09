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

// 6-button pads (8BitDo M30 and similar: bottom row A B C = Xbox A B RB, top
// row X Y Z = Xbox X Y LB). Each table follows that core's own RetroPad
// mapping, so the console's buttons land where they sit on the pad.
TEST_CASE(SixButtonLayoutPerSystem)
{
	InputBindResetDefaults();
	const char* none[16] = {};

	// Genesis Plus GX: Y=A, B=B, A=C, L=X, X=Y, R=Z
	ASSERT_TRUE(InputBindApplySixButtonLayout("Genesis", none));
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_A);
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_B);
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_RB);
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_X);
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_LB);
	InputBindRestoreGlobal();

	// Beetle Saturn: B=A, A=B, R=C, Y=X, X=Y, L=Z
	ASSERT_TRUE(InputBindApplySixButtonLayout("Saturn", none));
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A);
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B);
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_RB);
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_LB);
	InputBindRestoreGlobal();

	// Opera (3DO): Y=A, B=B, A=C, L=L on X, R=R on Z
	ASSERT_TRUE(InputBindApplySixButtonLayout("3DO", none));
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_A);
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_B);
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_RB);
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_X);
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_LB);
	InputBindRestoreGlobal();

	// Systems without a table change nothing.
	const int snes_b = InputBindGetPad(BIND_B);
	ASSERT_TRUE(!InputBindApplySixButtonLayout("SNES", none));
	ASSERT_EQ(InputBindGetPad(BIND_B), snes_b);
	InputBindResetDefaults();
}

TEST_CASE(SixButtonArcadeFollowsButtonNames)
{
	InputBindResetDefaults();
	// FBNeo names: "Strong" is the heavy one when a "Medium" exists.
	const char* fbneo[16] = {};
	fbneo[1] = "Weak Punch";    // Y
	fbneo[9] = "Medium Punch";  // X
	fbneo[10] = "Strong Punch"; // L
	fbneo[0] = "Weak Kick";     // B
	fbneo[8] = "Medium Kick";   // A
	fbneo[11] = "Strong Kick";  // R
	ASSERT_TRUE(InputBindApplySixButtonLayout("Arcade", fbneo));
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);   // weak punch: top left
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);   // medium punch: top middle
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_LB);  // heavy punch: top right
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A);   // weak kick: bottom left
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B);   // medium kick: bottom middle
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_RB);  // heavy kick: bottom right
	InputBindRestoreGlobal();

	// Capcom names: Jab / Strong / Fierce - here "Strong" is the medium one.
	const char* capcom[16] = {};
	capcom[0] = "Jab Punch"; capcom[8] = "Strong Punch"; capcom[1] = "Fierce Punch";
	capcom[9] = "Short Kick"; capcom[10] = "Forward Kick"; capcom[11] = "Roundhouse Kick";
	ASSERT_TRUE(InputBindApplySixButtonLayout("Arcade", capcom));
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_Y);   // Strong = medium punch
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_LB);  // Fierce = heavy punch
	InputBindRestoreGlobal();

	// A game with no punches or kicks keeps the global bindings.
	const char* shmup[16] = {};
	shmup[0] = "Shot"; shmup[8] = "Bomb";
	ASSERT_TRUE(!InputBindApplySixButtonLayout("Arcade", shmup));
	InputBindResetDefaults();
}

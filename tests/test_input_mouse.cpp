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

TEST_CASE(PadAsMouseTouchpadTap)
{
	ASSERT_EQ(InputEmulatedMouseButtons(false, 0, false, false, true), 0);
	ASSERT_EQ(InputEmulatedMouseButtons(true, 0, false, false, true), 1);
	ASSERT_EQ(InputEmulatedMouseButtons(true, PAD_BTN_B, false, false, true), 3); // Right click + left click
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

	// Flycast (Dreamcast): B=A, A=B, L=C, Y=X, X=Y, R=Z, L2=L-Trig, R2=R-Trig
	ASSERT_TRUE(InputBindApplySixButtonLayout("Dreamcast", none));
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A);
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B);
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_RB);
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_LB);
	ASSERT_EQ(InputBindGetPad(BIND_L2), PAD_BTN_LT);
	ASSERT_EQ(InputBindGetPad(BIND_R2), PAD_BTN_RT);
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

TEST_CASE(SixButtonLayoutM30Mode)
{
	InputBindResetDefaults();
	const char* none[16] = {};

	// 8BitDo M30 XInput table: Face C = RT, Face Z = RB, Shoulder L = LB, Shoulder R = LT
	const SixPadMap m30 = InputSixPadBuiltin(SIXPAD_M30_XINPUT);
	ASSERT_TRUE(InputBindApplySixButtonLayout("Dreamcast", none, &m30));
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A);   // DC A
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B);   // DC B
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);   // DC X
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);   // DC Y
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_RT);  // DC Arcade C on Face C
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_RB);  // DC Arcade Z on Face Z
	ASSERT_EQ(InputBindGetPad(BIND_L2), PAD_BTN_LB); // DC Left Trigger on Shoulder L (Flashlight)
	ASSERT_EQ(InputBindGetPad(BIND_R2), PAD_BTN_LT); // DC Right Trigger on Shoulder R (Aim)
	InputBindRestoreGlobal();

	// Saturn in M30 mode:
	ASSERT_TRUE(InputBindApplySixButtonLayout("Saturn", none, &m30));
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A);   // Saturn A
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B);   // Saturn B
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_RT);  // Saturn C on Face C
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);   // Saturn X
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);   // Saturn Y
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_RB);  // Saturn Z on Face Z
	ASSERT_EQ(InputBindGetPad(BIND_L2), PAD_BTN_LB); // Saturn L on Shoulder L
	ASSERT_EQ(InputBindGetPad(BIND_R2), PAD_BTN_LT); // Saturn R on Shoulder R
	InputBindRestoreGlobal();
	InputBindResetDefaults();
}

// Auto mode goes by the controller's name, never by a whole vendor: an 8BitDo
// SN30 Pro or a plain Xbox pad must not get the 6-button layout.
TEST_CASE(SixButtonDetectionByNameOnly)
{
	ASSERT_TRUE(InputNameLooksSixButton("8BitDo M30 gamepad"));
	ASSERT_TRUE(InputNameLooksSixButton("Retro-Bit Sega Saturn Control Pad"));
	ASSERT_TRUE(InputNameLooksSixButton("HORI Fighting Commander"));
	ASSERT_TRUE(InputNameLooksSixButton("Qanba Arcade Stick"));
	ASSERT_TRUE(!InputNameLooksSixButton("8BitDo SN30 Pro"));
	ASSERT_TRUE(!InputNameLooksSixButton("8BitDo Ultimate Wireless"));
	ASSERT_TRUE(!InputNameLooksSixButton("Xbox 360 Controller"));
	ASSERT_TRUE(!InputNameLooksSixButton("DualSense Wireless Controller"));
	ASSERT_TRUE(!InputNameLooksSixButton(""));
	ASSERT_TRUE(!InputNameLooksSixButton(nullptr));
}

// A calibration is kept per controller and drives the layout exactly as
// pressed - whatever the firmware sends for C, Z and the shoulders.
TEST_CASE(SixButtonCalibrationRoundTrip)
{
	InputBindResetDefaults();
	const char* key = "045e_028e_Xbox 360 Controller (test)";
	InputSixPadDelete(key);
	ASSERT_TRUE(!InputSixPadHas(key));

	SixPadMap cal;
	cal.a = PAD_BTN_A; cal.b = PAD_BTN_B; cal.c = PAD_BTN_LT;
	cal.x = PAD_BTN_X; cal.y = PAD_BTN_Y; cal.z = PAD_BTN_LB;
	cal.l = PAD_BTN_NONE; cal.r = PAD_BTN_RT;   // L skipped with START
	ASSERT_TRUE(InputSixPadSave(key, cal));
	ASSERT_TRUE(InputSixPadHas(key));

	SixPadMap back;
	ASSERT_TRUE(InputSixPadLoad(key, &back));
	ASSERT_EQ(back.c, PAD_BTN_LT);
	ASSERT_EQ(back.z, PAD_BTN_LB);
	ASSERT_EQ(back.l, PAD_BTN_NONE);
	ASSERT_EQ(back.r, PAD_BTN_RT);

	const char* none[16] = {};
	ASSERT_TRUE(InputBindApplySixButtonLayout("Genesis", none, &back));
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_LT);  // Genesis C where this pad sends C
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_LB);  // Genesis Z where this pad sends Z
	ASSERT_EQ(InputBindGetPad(BIND_L2), PAD_BTN_NONE);
	InputBindRestoreGlobal();

	ASSERT_TRUE(InputSixPadDelete(key));
	ASSERT_TRUE(!InputSixPadHas(key));
	InputBindResetDefaults();
}

// A fighting game that names its heavy kick on L2 keeps it there: the
// shoulders only fill L2/R2 where no punch or kick went.
TEST_CASE(SixButtonArcadeKeepsKickOnL2)
{
	InputBindResetDefaults();
	const char* labels[16] = {};
	labels[1] = "Weak Punch"; labels[9] = "Medium Punch"; labels[10] = "Heavy Punch";
	labels[0] = "Weak Kick";  labels[8] = "Medium Kick";  labels[12] = "Heavy Kick";   // L2
	ASSERT_TRUE(InputBindApplySixButtonLayout("Arcade", labels));
	ASSERT_EQ(InputBindGetPad(BIND_L2), PAD_BTN_RB);  // heavy kick: bottom right (Sega C)
	ASSERT_EQ(InputBindGetPad(BIND_R2), PAD_BTN_RT);  // right shoulder
	InputBindRestoreGlobal();
	InputBindResetDefaults();
}

// GameCube (Dolphin): the pad's shoulders go to the GameCube's L / R triggers
// (RetroPad L2 / R2) and its Z to the GameCube's Z (RetroPad R) - with the
// global set an M30 zoomed with C and Z. A B X Y stay on the global bindings.
TEST_CASE(SixButtonGameCubeShoulders)
{
	InputBindResetDefaults();
	const char* none[16] = {};
	const int global_a = InputBindGetPad(BIND_A);
	const SixPadMap m30 = InputSixPadBuiltin(SIXPAD_M30_XINPUT);
	ASSERT_TRUE(InputBindApplySixButtonLayout("GameCube", none, &m30));
	ASSERT_EQ(InputBindGetPad(BIND_L2), PAD_BTN_LB);  // shoulder L -> GC L
	ASSERT_EQ(InputBindGetPad(BIND_R2), PAD_BTN_LT);  // shoulder R -> GC R
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_RB);   // Z -> GC Z
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_NONE); // Triforce test: off
	ASSERT_EQ(InputBindGetPad(BIND_A), global_a);     // face buttons untouched
	InputBindRestoreGlobal();
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

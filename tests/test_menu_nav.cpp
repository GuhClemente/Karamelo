#include "test_framework.h"
#include "menu_nav.h"

using menu_nav::LetterKey;
using menu_nav::NextLetter;
using menu_nav::Page;
using menu_nav::RepeatInterval;

// Holding a direction: the first press fires on its own, then a pause, then
// repeats that get faster the longer it is held.
TEST_CASE(MenuNavRepeatSpeedsUp)
{
	ASSERT_EQ(RepeatInterval(0), 0u);
	ASSERT_EQ(RepeatInterval(299), 0u);
	ASSERT_EQ(RepeatInterval(300), 110u);
	ASSERT_EQ(RepeatInterval(1500), 60u);
	ASSERT_EQ(RepeatInterval(5000), 30u);
}

// The letter a row is filed under skips the favorite mark and brackets.
TEST_CASE(MenuNavLetterKey)
{
	ASSERT_EQ(LetterKey("Sonic"), 'S');
	ASSERT_EQ(LetterKey("* sonic"), 'S');
	ASSERT_EQ(LetterKey("[SNES] Mario"), 'S');
	ASSERT_EQ(LetterKey("(USA) Zelda"), 'U');
	ASSERT_EQ(LetterKey("3 Ninjas"), '#');
	ASSERT_EQ(LetterKey("007 GoldenEye"), '#');
	ASSERT_EQ(LetterKey(" "), 0);
	ASSERT_EQ(LetterKey("<..>"), 0);
	ASSERT_EQ(LetterKey(""), 0);
}

// Next letter goes to the first game of the next letter; previous goes to the
// start of the current letter, then to the start of the one before. Wraps.
TEST_CASE(MenuNavNextLetterGroups)
{
	const std::vector<std::string> l = {
		"<..>", "Alien", "Altered Beast", "Batman", "Bonanza", "Bubsy", "Castlevania", " ", "Zombies"};
	ASSERT_EQ(NextLetter(l, 1, +1), 3);   // Alien -> Batman
	ASSERT_EQ(NextLetter(l, 3, +1), 6);   // Batman -> Castlevania
	ASSERT_EQ(NextLetter(l, 6, +1), 8);   // Castlevania -> Zombies (over the spacer)
	ASSERT_EQ(NextLetter(l, 8, +1), 1);   // Zombies -> wraps to Alien
	ASSERT_EQ(NextLetter(l, 5, -1), 3);   // Bubsy -> start of B
	ASSERT_EQ(NextLetter(l, 3, -1), 1);   // Batman (start of B) -> start of A
	ASSERT_EQ(NextLetter(l, 1, -1), 8);   // Alien -> wraps to Z
	ASSERT_EQ(NextLetter(l, 0, +1), 1);   // from "<..>" to the first letter

	const std::vector<std::string> one = {"Abc", "Abd"};
	ASSERT_EQ(NextLetter(one, 0, +1), 0); // one letter only: stays
	ASSERT_EQ(NextLetter({}, 0, +1), 0);
}

// A page is the screen; at the end it goes round, like a single step does.
TEST_CASE(MenuNavPage)
{
	ASSERT_EQ(Page(100, 0, 14, +1), 14);
	ASSERT_EQ(Page(100, 90, 14, +1), 99);  // stops at the last row
	ASSERT_EQ(Page(100, 99, 14, +1), 0);   // then wraps to the first
	ASSERT_EQ(Page(100, 20, 14, -1), 6);
	ASSERT_EQ(Page(100, 5, 14, -1), 0);    // stops at the first row
	ASSERT_EQ(Page(100, 0, 14, -1), 99);   // then wraps to the last
	ASSERT_EQ(Page(0, 0, 14, +1), 0);
	ASSERT_EQ(Page(10, 3, 0, +1), 4);      // a page of at least one row
}

// A pad's own turbo: recognised by the pulses, switched off by a steady hold.
TEST_CASE(MenuNavTurboWatch)
{
	menu_nav::TurboWatch w;
	unsigned on = 0, off = 0, t = 1000;
	const unsigned A = 0x1000;
	w.Update(0, t, &on, &off);

	// A finger: 3 presses in a second - not a turbo.
	for (int i = 0; i < 3; i++)
	{
		w.Update(A, t += 170, &on, &off); ASSERT_EQ(on, 0u);
		w.Update(0, t += 170, &on, &off); ASSERT_EQ(on, 0u);
	}
	t += 1000;
	w.Update(0, t, &on, &off);

	// The M30 turbo: ~13 presses a second (about 38 ms per change).
	unsigned seen_on = 0;
	for (int i = 0; i < 10; i++)
	{
		w.Update(A, t += 38, &on, &off); seen_on |= on;
		w.Update(0, t += 38, &on, &off); seen_on |= on;
	}
	ASSERT_EQ(seen_on, A);
	ASSERT_EQ(w.turbo_bits, A);

	// Turbo switched off: the button is now held steady.
	w.Update(A, t += 40, &on, &off);
	ASSERT_EQ(off, 0u);
	w.Update(A, t += 400, &on, &off);
	ASSERT_EQ(off, 0u);
	w.Update(A, t += 400, &on, &off);
	ASSERT_EQ(off, A);
	ASSERT_EQ(w.turbo_bits, 0u);
}

#ifndef MENU_NAV_H_INCLUDED
#define MENU_NAV_H_INCLUDED

// Pure list-navigation rules for the OSD menu: how fast a held direction
// repeats, what a "page" is, and where "next letter" lands. Header-only and
// free of menu state, so tests/test_menu_nav.cpp checks the very code the menu
// and the pad loops in main_win32.cpp / main_linux.cpp run.

#include <cctype>
#include <string>
#include <vector>

namespace menu_nav {

// Milliseconds between repeats of a held direction or shoulder, by how long it
// has been held. 0 = no repeat yet (the first press already fired). It speeds
// up the longer it is held: a fixed 110 ms took ~50 s to cross a list of 500
// ROMs, and short lists still feel the same because the first steps are slow.
inline unsigned RepeatInterval(unsigned held_ms)
{
	if (held_ms < 300) return 0;
	if (held_ms < 1000) return 110;
	if (held_ms < 2000) return 60;
	return 30;
}

// Rows that are not entries: the blank spacer and the "go up" row.
inline bool IsSpacer(const std::string& label)
{
	return label.empty() || label == " " || label == "<..>";
}

// The letter a row is filed under: the first letter or digit after a favorite
// mark ("* ") and any leading brackets or punctuation ("[SNES] Mario",
// "(USA) Zelda"). Digits are one group, '#'. 0 for a spacer or no letter.
inline char LetterKey(const std::string& label)
{
	if (IsSpacer(label)) return 0;
	size_t i = label.rfind("* ", 0) == 0 ? 2 : 0;
	while (i < label.size() && !isalnum((unsigned char)label[i])) i++;
	if (i >= label.size()) return 0;
	const unsigned char c = (unsigned char)label[i];
	if (isdigit(c)) return '#';
	return (char)toupper(c);
}

// Next (dir > 0) or previous (dir < 0) letter group from `sel`. Forward goes to
// the first row of the next group; back goes to the first row of the current
// group, or of the previous one when already there - like a dictionary's
// thumb index. Wraps around. Works on any order, sorted or not, since it only
// looks for where the letter changes. Returns `sel` when there is nowhere to go.
inline int NextLetter(const std::vector<std::string>& labels, int sel, int dir)
{
	const int n = (int)labels.size();
	if (n <= 0 || sel < 0 || sel >= n) return sel;
	const char cur = LetterKey(labels[sel]);

	if (dir > 0)
	{
		for (int step = 1; step < n; step++)
		{
			const int i = (sel + step) % n;
			const char k = LetterKey(labels[i]);
			if (k && k != cur) return i;
		}
		return sel;
	}

	// Start of a group: walk back while the previous row (skipping spacers)
	// still has the same letter.
	auto group_start = [&](int i) {
		const char k = LetterKey(labels[i]);
		for (int j = i - 1; j >= 0; j--)
		{
			const char kj = LetterKey(labels[j]);
			if (!kj) continue;
			if (kj != k) break;
			i = j;
		}
		return i;
	};

	if (cur)
	{
		const int start = group_start(sel);
		if (start != sel) return start;
	}
	for (int step = 1; step < n; step++)
	{
		const int i = ((sel - step) % n + n) % n;
		const char k = LetterKey(labels[i]);
		if (k && k != cur) return group_start(i);
	}
	return sel;
}

// One page down (dir > 0) or up from `sel` in a list of `total` rows, `page`
// rows per screen. Stops at the ends; a page from the last row goes to the
// first (and from the first to the last), like the single-step wrap.
inline int Page(int total, int sel, int page, int dir)
{
	if (total <= 0) return 0;
	if (page < 1) page = 1;
	if (dir > 0)
	{
		if (sel >= total - 1) return 0;
		return sel + page < total ? sel + page : total - 1;
	}
	if (sel <= 0) return total - 1;
	return sel - page > 0 ? sel - page : 0;
}

// A controller's own turbo (8BitDo: hold a button + press the turbo key) sends
// nothing to the PC when it is switched - the pad just starts or stops
// pulsing that button by itself. So it is recognised by the pulses: 10
// changes (5 presses) inside half a second is a turbo, no finger keeps that
// up; a turbo button then held steady for 0.7 s has been switched off.
struct TurboWatch
{
	static const int kEdges = 10;
	static const unsigned kWindowMs = 500;
	static const unsigned kSteadyMs = 700;

	unsigned turbo_bits = 0;   // buttons currently pulsing on their own
	unsigned last_bits = 0;
	bool started = false;
	unsigned edges[16][kEdges] = {};
	int count[16] = {};
	unsigned down_since[16] = {};

	// `bits`: the buttons down now (one bit per button, the ones that can
	// carry a turbo). Sets `on` / `off` to the buttons whose turbo was just
	// seen to start / stop.
	void Update(unsigned bits, unsigned now_ms, unsigned* on, unsigned* off)
	{
		*on = *off = 0;
		if (!started)
		{
			started = true;
			last_bits = bits;
			for (int b = 0; b < 16; b++) down_since[b] = now_ms;
			return;
		}
		const unsigned changed = bits ^ last_bits;
		last_bits = bits;
		for (int b = 0; b < 16; b++)
		{
			const unsigned m = 1u << b;
			if (changed & m)
			{
				edges[b][count[b] % kEdges] = now_ms;
				count[b]++;
				if (bits & m) down_since[b] = now_ms;
				// The edge kEdges-1 changes ago is the oldest one kept.
				if (!(turbo_bits & m) && count[b] >= kEdges &&
				    now_ms - edges[b][count[b] % kEdges] <= kWindowMs)
				{
					turbo_bits |= m;
					*on |= m;
				}
			}
			else if ((turbo_bits & m) && (bits & m) && now_ms - down_since[b] >= kSteadyMs)
			{
				turbo_bits &= ~m;
				*off |= m;
				count[b] = 0;
			}
		}
	}
};

} // namespace menu_nav

#endif

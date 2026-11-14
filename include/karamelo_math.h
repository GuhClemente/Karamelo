#ifndef KARAMELO_MATH_H_INCLUDED
#define KARAMELO_MATH_H_INCLUDED

// Pure helpers, deliberately free of Windows and of any global state.
//
// These lived as `static` copies inside main_win32.cpp, menu.cpp and
// core_runner.cpp, which made them impossible to link into a test. The test
// suite worked around that by keeping its own copies, and the copies had
// already drifted out of sync with the originals. Extracting them here gives
// both the app and the tests one definition to share.

// Clamp v into [lo, hi].
int ClampInt(int v, int lo, int hi);

// Snap a measured or reported refresh rate onto the nearest standard rate.
// Returns the input unchanged when nothing is close enough, so an implausible
// reading stays visibly implausible instead of being rounded into a lie.
double SnapToStandardRate(double rate);

// Where the emulated picture goes inside a destination surface.
//   aspect_mode 0 or 1 -> 4:3, pillarboxed (or letterboxed on a narrow window)
//   aspect_mode 2      -> fill the surface
void ComputeViewport(int aspect_mode, int dest_w, int dest_h,
                     int* out_x, int* out_y, int* out_w, int* out_h);

// How many frame periods one retro_run() call covered, judged by the sound it
// produced. Most cores emulate exactly one video frame per call. Flycast does
// not: it returns when the game presents a picture, so a game drawing at 30 or
// 20 fps hands back two or three frames of emulated time, and of sound, in one
// call. A frontend that paces by calls alone then runs that game at double
// speed, an octave too high.
//   samples           audio frames delivered during the call
//   samples_per_frame the core's own sample rate divided by its own frame rate
//   carry             remainder kept between calls (start it at 0)
// Returns 1 to 4. The remainder absorbs uneven delivery, so two calls carrying
// 1.4 and 0.6 frames still add up to two periods, and it decays, so a core whose
// real rate sits a fraction of a percent off the declared one never gets an
// extra period out of the drift.
int FramePeriodsForAudio(double samples, double samples_per_frame, double* carry);

#endif

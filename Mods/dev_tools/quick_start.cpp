/*
 * FEngMovieStarter::Callback 0x559680 checks the byte at 0x926144 before starting a movie: if set, it posts the same
 * "movie finished" message (0xc3960eb9) the real movie posts when it ends, instead of opening it - so whatever movie
 * it was (the EA logo, the PSA, the attract movie, or any later in-game one) is torn down the normal way, never shown.
 * NFSMWExtraOptions's SkipMovies and the widescreen fix's SkipIntro set the same byte for the same reason.
 *
 * An earlier version of this tool instead hooked MovieScreen's constructor to skip the screen directly; that let the
 * real movie actually start (the constructor runs before Callback does), so the later skip tore down a playing movie
 * instead of a finished one, and crashed.
 */
#include "quick_start.h"

#include "dev_tools.h"

namespace dev_tools::quick_start {

void install() {
    constexpr uintptr_t kSkipMovies = 0x926144;
    *reinterpret_cast<unsigned char *>(kSkipMovies) = 1;
    g_log.line("quick start: movies will be skipped (byte at %#x set)", kSkipMovies);
}

}  // namespace dev_tools::quick_start

// A dump of the track streamer's state and the players' places, for bug reports about missing geometry (see streaming_log.cpp).
#pragma once

namespace split_screen_support::streaming_log {

// The game's memory pools (bMalloc's pool number is the low 4 bits of its flags; pool 0 is the main heap, 7 the car loader's): what is free and the largest block, in bytes.
int pool_free(int pool);
int pool_largest(int pool);
// One line of text with the pools that have any memory: "pool 0 123 MB free (largest 45 MB), pool 7 ...".
const char *pools_text();

void install();

}  // namespace split_screen_support::streaming_log

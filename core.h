#pragma once
#include <opxy/audio.hpp>

namespace breadboard {
// Per-voice state only. Controls are smoothed here rather than in the shared
// stock layer so the direct octave sub and independent ring modulator retain
// stable relationships per note.
struct Voice {
    opxy::u32 main_phase;
    opxy::u32 sub_phase;
    opxy::u32 ring_phase;
    opxy::u32 random;
    opxy::u32 noise_seed;
    opxy::u32 noise_count;
    opxy::i32 held_noise;
    opxy::i32 controls[4];
    opxy::u32 controls_ready;
    // Memoized exact ratio calculation. Pitch is normally constant across a
    // block, so this avoids repeating an integer divide for all 64 samples.
    opxy::u32 ratio_cache_increment;
    opxy::u32 ratio_cache_key;
    opxy::u32 ratio_cache_result;
    opxy::u32 ratio_cache_valid;
};

struct Shared {};

void initialize(Voice&, Shared&, opxy::u32 owner);
void start(Voice&, Shared&, opxy::u32 owner);
void destroy(Voice&, Shared&, opxy::u32 owner);
void render(Voice&, Shared&, opxy::u32 owner, opxy::Output, opxy::Parameters, opxy::Pitch);
}  // namespace breadboard

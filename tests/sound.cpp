// The machine's own sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Extend them as the sound grows.
#include "../engine.cpp"
#include <cstdio>

int main() {
    static breadboard::Voice voice;
    static breadboard::Shared shared;
    const opxy::i32 parameters[8] = {0x40000000, 0x40000000, 0x7fffffff, 0x40000000};
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = 42852281u;  // 440 Hz at 44.1 kHz
    opxy::i32 output[opxy::block_frames];
    breadboard::initialize(voice, shared, 1);
    breadboard::start(voice, shared, 1);
    bool sounding = false, bounded = true, exact_ratio_cache = true;
    for (unsigned block = 0; block < 690; ++block) {  // one second
        breadboard::render(voice, shared, 1, output, parameters, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            sounding = sounding || output[i] != 0;
            // definition.json declares outputPeakQ31: 671088640.
            bounded = bounded && output[i] >= -671088640 && output[i] <= 671088640;
        }
    }
    // The optimization must remain a cache of the existing exact arithmetic.
    // Exercise every ratio family and input changes which invalidate the cache.
    const breadboard::Ratio ratios[] = {{1, 1}, {31, 32}, {4, 5}, {2, 3},
                                        {3, 2}, {3, 1}, {12, 5}, {23, 5}};
    const opxy::u32 increments[] = {1u, 42852281u, 314159265u, 1073741823u};
    breadboard::Voice cached{};
    for (opxy::u32 increment : increments) {
        for (breadboard::Ratio ratio : ratios) {
            const opxy::u32 expected = breadboard::ratio_increment(increment, ratio);
            exact_ratio_cache = exact_ratio_cache &&
                breadboard::cached_ratio_increment(cached, increment, ratio) == expected;
            // Repeat to cover the cache-hit path as well.
            exact_ratio_cache = exact_ratio_cache &&
                breadboard::cached_ratio_increment(cached, increment, ratio) == expected;
        }
    }
    // Exercise all control endpoints: tests should reject a silent or
    // out-of-contract candidate without making an optimization bit-identical.
    bool endpoints_sound = true, endpoints_bounded = true;
    const opxy::i32 endpoint_values[] = {0, opxy::q31::maximum};
    for (unsigned control = 0; control < 4; ++control) {
        for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
            opxy::i32 endpoint_parameters[8] = {
                0x40000000, 0x40000000, breadboard::kCenter, 0x40000000,
            };
            endpoint_parameters[control] = endpoint_values[endpoint];
            breadboard::Voice endpoint_voice{};
            breadboard::initialize(endpoint_voice, shared, control * 2 + endpoint + 2);
            breadboard::start(endpoint_voice, shared, control * 2 + endpoint + 2);
            bool endpoint_sounding = false;
            for (unsigned block = 0; block < 8; ++block) {
                breadboard::render(endpoint_voice, shared, control * 2 + endpoint + 2,
                                   output, endpoint_parameters, pitch);
                for (unsigned i = 0; i < opxy::block_frames; ++i) {
                    endpoint_sounding = endpoint_sounding || output[i] != 0;
                    endpoints_bounded = endpoints_bounded &&
                        output[i] >= -671088640 && output[i] <= 671088640;
                }
            }
            endpoints_sound = endpoints_sound && endpoint_sounding;
        }
    }
    // The physical -2%..+2% DetuneRM dead zone must remain exactly neutral.
    opxy::i32 centre_parameters[8] = {0x40000000, 0x40000000,
                                       breadboard::kCenter, 0x60000000};
    opxy::i32 neutral_parameters[8] = {0x40000000, 0x40000000,
                                        // center_distance doubles the raw control distance.
                                        breadboard::kCenter + breadboard::kNeutralDetuneRange / 2,
                                        0x60000000};
    breadboard::Voice centre_voice{}, neutral_voice{};
    breadboard::initialize(centre_voice, shared, 20);
    breadboard::initialize(neutral_voice, shared, 20);
    breadboard::start(centre_voice, shared, 20);
    breadboard::start(neutral_voice, shared, 20);
    bool neutral_detune = true;
    opxy::i32 centre_output[opxy::block_frames], neutral_output[opxy::block_frames];
    for (unsigned block = 0; block < 8; ++block) {
        breadboard::render(centre_voice, shared, 20, centre_output, centre_parameters, pitch);
        breadboard::render(neutral_voice, shared, 20, neutral_output, neutral_parameters, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i)
            neutral_detune = neutral_detune && centre_output[i] == neutral_output[i];
    }
    // Digital's pitched layer is an interpolated waveform whose phase, plus
    // its voice-specific offset, is the only lookup input. One full phase
    // turn must therefore reproduce the same 64 table cells exactly.
    breadboard::Voice texture_voice{};
    breadboard::start(texture_voice, shared, 42);
    bool pitch_locked_digital = true;
    for (opxy::u32 cell = 0; cell < 64; ++cell) {
        const opxy::u32 phase = cell << 26;
        const opxy::u32 next_turn = (cell + 64u) << 26;
        pitch_locked_digital = pitch_locked_digital &&
            breadboard::tuned_noise(phase + texture_voice.noise_phase_offset) ==
            breadboard::tuned_noise(next_turn + texture_voice.noise_phase_offset);
    }
    if (!sounding) std::puts("FAIL silent");
    if (!bounded) std::puts("FAIL above the declared peak");
    if (!exact_ratio_cache) std::puts("FAIL ratio cache changed oscillator tuning");
    if (!endpoints_sound) std::puts("FAIL control endpoint is silent");
    if (!endpoints_bounded) std::puts("FAIL control endpoint above declared peak");
    if (!neutral_detune) std::puts("FAIL neutral DetuneRM changed the signal");
    if (!pitch_locked_digital) std::puts("FAIL Digital noise is not pitch-locked");
    return sounding && bounded && exact_ratio_cache && endpoints_sound && endpoints_bounded &&
           neutral_detune && pitch_locked_digital ? 0 : 1;
}

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
    if (!sounding) std::puts("FAIL silent");
    if (!bounded) std::puts("FAIL above the declared peak");
    if (!exact_ratio_cache) std::puts("FAIL ratio cache changed oscillator tuning");
    return sounding && bounded && exact_ratio_cache ? 0 : 1;
}

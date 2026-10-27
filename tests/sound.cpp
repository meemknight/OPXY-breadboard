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
    bool sounding = false, bounded = true;
    for (unsigned block = 0; block < 690; ++block) {  // one second
        breadboard::render(voice, shared, 1, output, parameters, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            sounding = sounding || output[i] != 0;
            // definition.json declares outputPeakQ31: 671088640.
            bounded = bounded && output[i] >= -671088640 && output[i] <= 671088640;
        }
    }
    if (!sounding) std::puts("FAIL silent");
    if (!bounded) std::puts("FAIL above the declared peak");
    return sounding && bounded ? 0 : 1;
}

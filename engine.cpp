#include "core.h"
#include <opxy/q31.hpp>

// Set to 1 before compiling to preserve the first Breadboard
// detune/ring implementation.  The default is the musical-ratio redesign.
#define BREADBOARD_CLASSIC_DETUNE_RM 0
static_assert(BREADBOARD_CLASSIC_DETUNE_RM == 0 || BREADBOARD_CLASSIC_DETUNE_RM == 1,
              "BREADBOARD_CLASSIC_DETUNE_RM must be 0 or 1");

// Set to 1 to restore the original sample-held white-noise Digital control.
// The default uses a per-note, phase-locked noise waveform instead.
#define BREADBOARD_CLASSIC_WHITE_NOISE 0
static_assert(BREADBOARD_CLASSIC_WHITE_NOISE == 0 || BREADBOARD_CLASSIC_WHITE_NOISE == 1,
              "BREADBOARD_CLASSIC_WHITE_NOISE must be 0 or 1");

namespace breadboard {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::q31::half;
using opxy::q31::maximum;
using opxy::q31::multiply;

const u32 kNyquistIncrement = 0x7fffffffu;
const i32 kCenter = 1073741823;
const i32 kOutputLevel = 603979776;  // 0.28125, leaving stock mix headroom.
const i32 kPitchNoiseMix = 644245094; // 30% pitched grain, 70% moving noise.
const i32 kDigitalMaxLevel = 1825361100; // 85% at maximum hybrid Digital.
const i32 kHighOctaveSubMixStep = 107374182; // Reach 65% sub at +3 octaves.

inline i32 normalized(i32 value) {
    return value < 0 ? 0 : value > maximum ? maximum : value;
}

inline i32 double_half(i32 value) {
    if (value >= 1073741824) return maximum;
    if (value <= -1073741823) return -maximum;
    return value * 2;
}

// A full-width fixed-point crossfade without overflowing when a and b differ
// in sign. It is also used to introduce ring modulation gradually.
inline i32 blend(i32 a, i32 b, i32 amount) {
    const i32 t = normalized(amount);
    const i32 left = multiply(half(a), maximum - t);
    const i32 right = multiply(half(b), t);
    return double_half(left + right);
}

inline i32 saw(u32 phase) {
    return phase < 0x80000000u ? -maximum + static_cast<i32>(phase)
                                : static_cast<i32>(phase - 0x80000000u);
}

inline i32 triangle(u32 phase) {
    const i32 rise = static_cast<i32>((phase & 0x3fffffffu) << 1);
    switch (phase >> 30) {
    case 0: return rise;
    case 1: return maximum - rise;
    case 2: return -rise;
    default: return -maximum + rise;
    }
}

// A softened triangle starts close to a sine-like fundamental and moves into
// a saw as Wave increases. The original Digital engine's exact oscillator is
// proprietary; this is an independent behavior-led implementation.
inline i32 shaped_wave(u32 phase, i32 shape) {
    const i32 tri = triangle(phase);
    const i32 magnitude = tri < 0 ? -tri : tri;
    const i32 rounded = tri + multiply(half(tri), maximum - magnitude);
    return blend(rounded, saw(phase), shape);
}

inline unsigned octave_index(i32 control) {
    const u32 position = (static_cast<u32>(normalized(control)) >> 16) * 6u + 16384u;
    const unsigned index = position >> 15;
    return index > 6 ? 6 : index;
}

inline u32 octave_increment(u32 increment, unsigned index) {
    if (index <= 3) return increment >> (3 - index);
    const unsigned shift = index - 3;
    return increment > (kNyquistIncrement >> shift) ? kNyquistIncrement : increment << shift;
}

inline i32 sub_mix_for_octave(unsigned index) {
    // Keep negative octaves and unison balanced. Higher octave sub voices
    // need a little more presence to remain audible over the main oscillator.
    if (index <= 3) return half(maximum);
    return half(maximum) + static_cast<i32>(index - 3) * kHighOctaveSubMixStep;
}

inline i32 center_distance(i32 control) {
    const i32 value = normalized(control);
    const i32 distance = value > kCenter ? value - kCenter : kCenter - value;
    const u32 doubled = static_cast<u32>(distance) << 1;
    return doubled > static_cast<u32>(maximum) ? maximum : static_cast<i32>(doubled);
}

inline u32 detuned_increment(u32 increment, i32 amount, bool upward) {
    const u32 offset = static_cast<u32>(multiply(static_cast<i32>(increment >> 4), amount));
    if (!upward) return increment > offset ? increment - offset : 0;
    return increment > kNyquistIncrement - offset ? kNyquistIncrement : increment + offset;
}

struct Ratio {
    unsigned numerator;
    unsigned denominator;
};

// The lower half moves the sub through useful consonances instead of a small
// arbitrary pitch offset.  Near-unison retains a chorus-like first step;
// farther positions land on a minor second, major third, fifth and octave.
const Ratio kDetuneRatios[] = {
    {1, 1}, {31, 32}, {15, 16}, {4, 5}, {2, 3}, {1, 2},
};

// The musically stable part of the right half deliberately snaps between a
// fifth, octave and twelfth. Ring modulation at those ratios produces a
// pitched harmonic structure rather than the impression of a slightly
// mistuned second oscillator. Only the final part of the control reaches the
// inharmonic bell family.
const Ratio kMusicalRingRatios[] = {
    {3, 2}, {2, 1}, {3, 1},
};
const Ratio kInharmonicBellRatios[] = {
    {7, 2}, {9, 2}, {12, 5}, {17, 5}, {23, 5},
};
const i32 kMusicalPhaseDepth = 901943132;  // 0.42 cycle PM at the musical stages.
const i32 kMusicalRingAmount = 858993459;  // 40% product: metallic but pitched.
const i32 kMusicalVoiceAmount = 1288490188; // 60% metallic voice at the fifth.
// The physical DetuneRM caption is bipolar, so this is an exact -2%..+2%
// neutral window around its centre position.
const i32 kNeutralDetuneRange = 42949673;
// After the neutral window, 2 + 1/12 reaches full ring depth at 50% right.
const i32 kRingRiseTail = 178956971;
const i32 kBellExtraPhaseDepth = 1181116006; // +0.55 cycle through the detuned tail.
const i32 kBellExtremeRingAmount = 1825361100;  // 85% ring product at 100%.
const i32 kBellExtremeVoiceAmount = 1975684956; // 92% metallic voice at 100%.
const i32 kOvertoneStart = 536870911;       // Bring extra metal in from +25%.
const i32 kOctaveStart = 1073741823;       // 50% of the right half.
const i32 kTwelfthStart = 1503238553;      // 70% of the right half.
const i32 kInharmonicStart = 1825361100;   // The final 15% becomes detuned.

inline i32 saturating_positive_add(i32 a, i32 b) {
    return a >= maximum - b ? maximum : a + b;
}

// The displayed right-hand amount is 0..100%. It stays fully dry through
// +2%, then ramps in gently but reaches the former 100% bell sound at 50%.
// From +25% onward, a second overtone stage grows into the full bell tail.
inline i32 ring_depth(i32 distance) {
    if (distance <= kNeutralDetuneRange) return 0;
    const i32 active = distance - kNeutralDetuneRange;
    return saturating_positive_add(double_half(active), multiply(active, kRingRiseTail));
}

inline i32 ring_extension(i32 distance) {
    if (distance <= kOvertoneStart) return 0;
    // The remaining 75% maps to 0..100% with 1 + 1/3 gain. This keeps the
    // intense +100% bell sound unchanged while making +50% substantially
    // more metallic than the original delayed extension.
    const i32 active = distance - kOvertoneStart;
    return saturating_positive_add(active, multiply(active, 715827883));
}

inline unsigned ratio_position(i32 amount, unsigned count) {
    const u32 position = static_cast<u32>(normalized(amount)) >> 16;
    return (position * (count - 1) + 16383u) / 32767u;
}

inline Ratio right_ring_ratio(i32 distance) {
    if (distance <= kOctaveStart) return kMusicalRingRatios[0];  // 3:2 fifth.
    if (distance <= kTwelfthStart) return kMusicalRingRatios[1]; // 2:1 octave.
    if (distance <= kInharmonicStart) return kMusicalRingRatios[2]; // 3:1 twelfth.
    // The tail is 15% wide. Scale it to the full selector range with
    // additions only: 4x + 2x + 2/3x = 6 2/3x, so 15% reaches 100%.
    const i32 tail_delta = distance - kInharmonicStart;
    const i32 tail_four = double_half(double_half(tail_delta));
    const i32 tail_six = saturating_positive_add(tail_four, double_half(tail_delta));
    const i32 tail = saturating_positive_add(tail_six, multiply(tail_delta, 1431655765));
    const unsigned count = sizeof(kInharmonicBellRatios) / sizeof(kInharmonicBellRatios[0]);
    return kInharmonicBellRatios[ratio_position(tail, count)];
}

// This performs a rational rescale without a wide multiply.  That keeps the
// Blackfin path bounded even for ratios above one and saturates at Nyquist.
inline u32 ratio_increment(u32 increment, Ratio ratio) {
    const u32 whole = increment / ratio.denominator;
    const u32 remainder = increment % ratio.denominator;
    if (whole > kNyquistIncrement / ratio.numerator) return kNyquistIncrement;
    const u32 scaled_whole = whole * ratio.numerator;
    const u32 scaled_remainder = (remainder * ratio.numerator) / ratio.denominator;
    return scaled_whole > kNyquistIncrement - scaled_remainder
               ? kNyquistIncrement
               : scaled_whole + scaled_remainder;
}

template <bool Classic>
struct DetuneRing;

template <>
struct DetuneRing<true> {
    static inline u32 increment(u32 base_sub, i32 distance, i32, bool ring_side) {
        return detuned_increment(base_sub, distance, ring_side);
    }

    static inline i32 mix(i32 main, i32 sub, u32, u32, i32, i32 sub_mix,
                          i32 distance, bool ring_side, i32, i32) {
        const i32 sub_signal = ring_side ? blend(sub, multiply(main, sub), distance) : sub;
        return blend(main, sub_signal, sub_mix);
    }
};

template <>
struct DetuneRing<false> {
    static inline u32 increment(u32 base_sub, i32 distance, i32 ring_depth, bool ring_side) {
        // The right half leaves the played carrier pitch untouched. Its
        // internal modulator is consonant through 85% of the range, then
        // steps through inharmonic bell ratios only near the extreme.
        if (ring_side) {
            // Do not retune the hidden modulator until it can be heard. This
            // makes 1% (and the complete dead zone) genuinely indistinguishable
            // from centre rather than a quiet off-key sub oscillator.
            if (!ring_depth) return base_sub;
            return ratio_increment(base_sub, right_ring_ratio(distance));
        }
        const unsigned ratio_count = sizeof(kDetuneRatios) / sizeof(kDetuneRatios[0]);
        return ratio_increment(base_sub, kDetuneRatios[ratio_position(distance, ratio_count)]);
    }

    static inline i32 mix(i32 main, i32 sub, u32 main_phase, u32 sub_phase,
                          i32 shape, i32 sub_mix, i32, bool ring_side,
                          i32 ring_depth, i32 extension) {
        const i32 dry_pair = blend(main, sub, sub_mix);
        // The modulator feeds both a phase-modulated carrier and a true ring
        // product.  Phase modulation supplies the bell-like sideband stack;
        // the product supplies the sharp metallic edge.  Every blend is
        // bounded, keeping the output peak contract intact.
        const i32 ring_source = shaped_wave(sub_phase + 0x40000000u, shape);
        const i32 phase_depth = saturating_positive_add(
            multiply(ring_depth, kMusicalPhaseDepth), multiply(extension, kBellExtraPhaseDepth));
        const i32 phase_q31 = multiply(ring_source, phase_depth);
        const u32 phase_offset = static_cast<u32>(phase_q31) << 1;
        const i32 bell_carrier = shaped_wave(main_phase + phase_offset, half(shape));
        const i32 ring_product = multiply(bell_carrier, ring_source);
        const i32 ring_amount = blend(kMusicalRingAmount, kBellExtremeRingAmount, extension);
        const i32 metallic = blend(bell_carrier, ring_product, ring_amount);
        const i32 voice_amount = blend(kMusicalVoiceAmount, kBellExtremeVoiceAmount, extension);
        const i32 ring_voice = blend(main, metallic, voice_amount);
        // Keep the octave-shifted sub outside the ring crossfade.  Its level
        // is therefore exactly the same at every positive DetuneRM position;
        // only the carrier portion becomes increasingly metallic.
        const i32 ring_carrier = blend(main, ring_voice, ring_depth);
        return ring_side ? blend(ring_carrier, sub, sub_mix) : dry_pair;
    }
};

inline void smooth(i32& current, i32 target) {
    const i32 difference = target - current;
    const i32 step = difference / 64;
    current = step ? current + step : target;
}

inline u32 noise_hash(u32 value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

inline i32 noise_word(u32 value) {
    // Hashing yields a deterministic bipolar value without an audio-rate
    // random state update. double_half restores full Q31 range safely.
    return double_half(static_cast<i32>(noise_hash(value) >> 1) - 1073741823);
}

// A 64-cell pseudo-random wavetable, interpolated so its grain is continuous.
// Because phase wraps, the waveform (and therefore every overtone it adds)
// repeats at the played fundamental instead of behaving as white noise.
inline i32 tuned_noise(u32 phase, u32 seed) {
    const u32 cell = phase >> 26;
    const u32 next = (cell + 1u) & 63u;
    const i32 fraction = static_cast<i32>((phase & 0x03ffffffu) << 5);
    const i32 first = noise_word(seed ^ (cell * 0x9e3779b9u));
    const i32 second = noise_word(seed ^ (next * 0x9e3779b9u));
    return blend(first, second, fraction);
}

template <bool Classic>
struct DigitalTexture;

template <>
struct DigitalTexture<true> {
    static inline i32 grain(Voice& voice, i32 amount, u32) {
        // The original sample-and-hold white-noise Digital implementation.
        if (!voice.noise_count) {
            voice.random = voice.random * 1664525u + 1013904223u;
            voice.held_noise = static_cast<i32>(voice.random);
            voice.noise_count = 1u + (static_cast<u32>(maximum - amount) >> 28);
        }
        --voice.noise_count;
        return multiply(half(voice.held_noise), amount);
    }

    static inline i32 output_gain(i32) { return maximum; }
};

template <>
struct DigitalTexture<false> {
    static inline i32 grain(Voice& voice, i32 amount, u32 phase) {
        // Keep a majority of the original moving sample-and-hold noise, then
        // blend in a stable note-locked layer. The result stays recognizably
        // noisy while its spectral centre follows the played note.
        if (!voice.noise_count) {
            voice.random = voice.random * 1664525u + 1013904223u;
            voice.held_noise = static_cast<i32>(voice.random);
            voice.noise_count = 1u + (static_cast<u32>(maximum - amount) >> 28);
        }
        --voice.noise_count;
        const i32 white = multiply(half(voice.held_noise), amount);
        const i32 pitched = multiply(half(tuned_noise(phase, voice.noise_seed)), amount);
        return blend(white, pitched, kPitchNoiseMix);
    }

    static inline i32 output_gain(i32 amount) {
        return blend(maximum, kDigitalMaxLevel, amount);
    }
};

inline i32 digitalize(Voice& voice, i32 signal, i32 amount, u32 phase) {
    typedef DigitalTexture<(BREADBOARD_CLASSIC_WHITE_NOISE != 0)> DigitalTextureMode;
    const i32 grain = DigitalTextureMode::grain(voice, amount, phase);
    const i32 combined = half(signal) + half(grain);
    // Symmetric cubic folding curve: it adds dense harmonics without an
    // unbounded hard clip and stays within Q31 for every input.
    const i32 cubic = multiply(multiply(combined, combined), combined);
    const i32 folded = combined - half(cubic);
    const unsigned discarded = static_cast<u32>(amount) >> 26;
    return static_cast<i32>(static_cast<u32>(folded) & (0xffffffffu << discarded));
}
}  // namespace

void start(Voice& voice, Shared&, u32 owner) {
    voice.main_phase = voice.sub_phase = voice.ring_phase = 0;
    voice.random = 0x6d2b79f5u ^ (owner * 0x9e3779b9u);
    voice.noise_seed = noise_hash(voice.random ^ 0xa511e9b3u);
    voice.noise_count = 0;
    voice.held_noise = 0;
    voice.controls_ready = 0;
}

void initialize(Voice& voice, Shared& shared, u32 owner) {
    start(voice, shared, owner);
}

void destroy(Voice&, Shared&, u32) {}

void render(Voice& voice, Shared&, u32, opxy::Output output,
            opxy::Parameters parameters, opxy::Pitch pitch) {
    i32 targets[4];
    for (unsigned control = 0; control < 4; ++control) targets[control] = normalized(parameters[control]);
    if (!voice.controls_ready) {
        for (unsigned control = 0; control < 4; ++control) voice.controls[control] = targets[control];
        voice.controls_ready = 1;
    }

    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        for (unsigned control = 0; control < 4; ++control) smooth(voice.controls[control], targets[control]);
        const u32 main_increment = pitch[i] > kNyquistIncrement ? kNyquistIncrement : pitch[i];
        const i32 detune = voice.controls[2];
        const i32 distance = center_distance(detune);
        const bool neutral_detune = distance <= kNeutralDetuneRange;
        const bool ring_side = detune > kCenter && !neutral_detune;
        const i32 active_distance = neutral_detune ? 0 : distance;
        const i32 bell_depth = ring_side ? ring_depth(distance) : 0;
        const i32 bell_extension = ring_side ? ring_extension(distance) : 0;
        const unsigned octave = octave_index(voice.controls[1]);
        const u32 base_sub = octave_increment(main_increment, octave);
        const i32 sub_mix = sub_mix_for_octave(octave);
        const i32 main = shaped_wave(voice.main_phase, voice.controls[0]);
        typedef DetuneRing<(BREADBOARD_CLASSIC_DETUNE_RM != 0)> DetuneRingMode;
        const u32 modulator_increment = DetuneRingMode::increment(base_sub, active_distance, bell_depth, ring_side);
        // On the left, this remains the audible detuned sub. Once the right
        // side engages, Octave advances its own audible sub oscillator while
        // ring modulation uses an independent phase, so no positive setting
        // can make Octave appear to stop.
        const u32 sub_increment = (BREADBOARD_CLASSIC_DETUNE_RM || !ring_side)
                                      ? modulator_increment
                                      : base_sub;
        const u32 modulator_phase = BREADBOARD_CLASSIC_DETUNE_RM ? voice.sub_phase : voice.ring_phase;
        const i32 sub = shaped_wave(voice.sub_phase, voice.controls[0]);
        const i32 pair = DetuneRingMode::mix(main, sub, voice.main_phase, modulator_phase,
                                              voice.controls[0], sub_mix, distance, ring_side,
                                              bell_depth, bell_extension);
        typedef DigitalTexture<(BREADBOARD_CLASSIC_WHITE_NOISE != 0)> DigitalTextureMode;
        const i32 digital = digitalize(voice, pair, voice.controls[3], voice.main_phase);
        output[i] = multiply(multiply(digital, DigitalTextureMode::output_gain(voice.controls[3])), kOutputLevel);
        voice.main_phase += main_increment;
        voice.sub_phase += sub_increment;
        voice.ring_phase += modulator_increment;
    }
}
}  // namespace breadboard

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

// Set to 1 to restore interpolation between Digital noise-table cells. The
// default keeps the deliberately noisy table stepped, saving two audio-rate
// Q31 multiplies per voice sample.
#define BREADBOARD_INTERPOLATED_DIGITAL_TABLE 0
static_assert(BREADBOARD_INTERPOLATED_DIGITAL_TABLE == 0 || BREADBOARD_INTERPOLATED_DIGITAL_TABLE == 1,
              "BREADBOARD_INTERPOLATED_DIGITAL_TABLE must be 0 or 1");

// Set to 0 to restore the Wave Shaper on the hidden ring-modulator source.
// The default triangle keeps the audible main and sub oscillators unchanged
// while making the metallic sideband source much cheaper to render.
#define BREADBOARD_FAST_RING_SOURCE 1
static_assert(BREADBOARD_FAST_RING_SOURCE == 0 || BREADBOARD_FAST_RING_SOURCE == 1,
              "BREADBOARD_FAST_RING_SOURCE must be 0 or 1");

// Set to 0 to restore the Wave Shaper on the hidden phase-modulated carrier.
// The default triangle retains the PM/ring bell structure with much less work.
#define BREADBOARD_FAST_RING_CARRIER 1
static_assert(BREADBOARD_FAST_RING_CARRIER == 0 || BREADBOARD_FAST_RING_CARRIER == 1,
              "BREADBOARD_FAST_RING_CARRIER must be 0 or 1");

// The phase-modulated carrier is the bell-like metallic layer. Set this to 1
// to add the former true ring-product edge on top; the default keeps the
// metallic harmonic motion at substantially lower CPU cost.
#define BREADBOARD_RING_PRODUCT 0
static_assert(BREADBOARD_RING_PRODUCT == 0 || BREADBOARD_RING_PRODUCT == 1,
              "BREADBOARD_RING_PRODUCT must be 0 or 1");

namespace breadboard {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::q31::half;
using opxy::q31::maximum;
using opxy::q31::multiply;

const u32 kNyquistIncrement = 0x7fffffffu;
const i32 kCenter = 1073741823;
const i32 kOutputLevel = 1181116006;  // 0.55 base level; boosted modes stay ceiling-limited.
const i32 kLowShapeOutputBoost = 1610612735; // Up to 75% extra gain at W Shaper 0%.
const i32 kOutputGainCeiling = 2093796556;  // 97.5%, avoid full-scale clipping.
const i32 kLowOctaveOutputBoosts[] = {
    644245094, // -3: 30%
    429496729, // -2: 20%
    214748365, // -1: 10%
};
const i32 kPitchNoiseMix = 644245094; // 30% pitched grain, 70% moving noise.
const i32 kDigitalMaxLevel = 1717986918; // 80% at maximum hybrid Digital.
const i32 kPitchedDigitalThreshold = 268435456; // 12.5%: full hybrid above this point.
const i32 kHighOctaveSubMixStep = 107374182; // Reach 65% sub at +3 octaves.
const i32 kLowOctaveSubMixStep = 178956970; // Reach 75% sub at -3 octaves.

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
    // A low-octave sub has less perceived loudness than the carrier, so give
    // it a gradual presence lift below unison. Higher octave settings retain
    // their existing smaller lift to stay audible over the main oscillator.
    if (index < 3) return half(maximum) + static_cast<i32>(3 - index) * kLowOctaveSubMixStep;
    if (index == 3) return half(maximum);
    return half(maximum) + static_cast<i32>(index - 3) * kHighOctaveSubMixStep;
}

inline i32 output_boost_for_octave(unsigned index) {
    return index < 3 ? kLowOctaveOutputBoosts[index] : 0;
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

inline u32 ratio_key(Ratio ratio) {
    return (ratio.numerator << 16) | ratio.denominator;
}

// This is deliberately a cache around the exact calculation above, not an
// approximation. A changed pitch increment or ratio always takes the original
// path, so the audible result is bit-identical to the uncached implementation.
inline u32 cached_ratio_increment(Voice& voice, u32 increment, Ratio ratio) {
    const u32 key = ratio_key(ratio);
    if (voice.ratio_cache_valid && voice.ratio_cache_increment == increment &&
        voice.ratio_cache_key == key) return voice.ratio_cache_result;
    const u32 result = ratio_increment(increment, ratio);
    voice.ratio_cache_increment = increment;
    voice.ratio_cache_key = key;
    voice.ratio_cache_result = result;
    voice.ratio_cache_valid = 1;
    return result;
}

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

// A fixed 64-cell pseudo-random wavetable. It is the former hash sequence
// evaluated once at compile time rather than twice per audio sample. The
// table stays shared and a per-voice phase offset below supplies variation.
const i32 kDigitalNoiseTable[] = {
    -2147483647, -2114132652, -2064067936, -1886642754,
    -341466792, -2055227398, -559613112, 292791780,
    -520890164, -1559266550, -669696742, -316948786,
    -1276414438, 1570947414, 1439743220, 1417859292,
    -1398364738, -1011888488, -234295532, 1691492540,
    1260504264, -512623470, 1898904130, -837984232,
    2110782926, 734940004, -2007354952, 459918200,
    2084235684, -2133489276, -91826754, 2108317726,
    689255546, -1834552556, -431041502, -941684192,
    395450224, -906336860, 1202122230, 1965555650,
    -242614832, -1552549490, -2049515594, -1425301268,
    1678423788, -1996307396, 110174734, 744383666,
    -1227818240, -1017496186, -369794408, 222648748,
    -1867226258, -398399084, 1162039178, 411721560,
    2020922182, 396122700, -233120524, -86194272,
    -447124148, -1371891514, 1687727172, -290595820,
};
static_assert(sizeof(kDigitalNoiseTable) / sizeof(kDigitalNoiseTable[0]) == 64,
              "Digital noise table must have 64 cells");

// Because phase wraps, this waveform (and therefore every overtone it adds)
// repeats at the played fundamental instead of behaving as white noise. The
// stepped default is appropriate for Digital's deliberately grainy texture;
// interpolation remains available above for softer variants.
inline i32 tuned_noise(u32 phase) {
    const u32 cell = phase >> 26;
    if (!BREADBOARD_INTERPOLATED_DIGITAL_TABLE)
        return kDigitalNoiseTable[cell];
    const u32 next = (cell + 1u) & 63u;
    const i32 fraction = static_cast<i32>((phase & 0x03ffffffu) << 5);
    return blend(kDigitalNoiseTable[cell], kDigitalNoiseTable[next], fraction);
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
        // At very small amounts the moving grain is already the audible
        // feature. Skip the two extra operations for the pitched layer until
        // Digital is high enough for that harmonic centre to be meaningful.
        if (amount < kPitchedDigitalThreshold)
            return multiply(half(voice.held_noise), amount);
        // Blend sources before applying amount. This is equivalent to
        // scaling each source then blending, apart from inaudible Q31
        // rounding, and removes one audio-rate multiply in full Digital.
        const i32 mixed_noise = blend(voice.held_noise,
                                      tuned_noise(phase + voice.noise_phase_offset),
                                      kPitchNoiseMix);
        return multiply(half(mixed_noise), amount);
    }

    static inline i32 output_gain(i32 amount) {
        return blend(maximum, kDigitalMaxLevel, amount);
    }
};

inline i32 digitalize(Voice& voice, i32 signal, i32 amount, u32 phase, u32 bit_mask) {
    typedef DigitalTexture<(BREADBOARD_CLASSIC_WHITE_NOISE != 0)> DigitalTextureMode;
    const i32 grain = DigitalTextureMode::grain(voice, amount, phase);
    const i32 combined = half(signal) + half(grain);
    // A symmetric one-multiply soft fold keeps Digital's dense, compressed
    // texture while replacing the former two-multiply cubic curve. combined
    // is already bounded above INT32_MIN, so its magnitude is safe to take.
    const i32 magnitude = combined < 0 ? -combined : combined;
    const i32 folded = combined - half(multiply(combined, magnitude));
    return static_cast<i32>(static_cast<u32>(folded) & bit_mask);
}

// Parameters are normally stationary for many audio blocks. Keep the original
// sample-smoothed path below for an actively moving control, but prepare the
// steady-state decisions once here instead of repeating control decoding,
// octave selection and output-gain work 64 times per block.
inline void render_steady(Voice& voice, opxy::Output output, opxy::Pitch pitch,
                          unsigned first_frame, unsigned frame_count) {
    const i32 shape = voice.controls[0];
    const i32 detune = voice.controls[2];
    const i32 digital_amount = voice.controls[3];
    const u32 digital_bit_mask = 0xffffffffu << (static_cast<u32>(digital_amount) >> 26);
    const i32 distance = center_distance(detune);
    const bool neutral_detune = distance <= kNeutralDetuneRange;
    const bool ring_side = detune > kCenter && !neutral_detune;
    const i32 active_distance = neutral_detune ? 0 : distance;
    const i32 bell_depth = ring_side ? ring_depth(distance) : 0;
    const i32 bell_extension = ring_side ? ring_extension(distance) : 0;
    const unsigned octave = octave_index(voice.controls[1]);
    const i32 sub_mix = sub_mix_for_octave(octave);
    typedef DigitalTexture<(BREADBOARD_CLASSIC_WHITE_NOISE != 0)> DigitalTextureMode;
    // Combining these two fixed gains removes one Q31 multiply from every
    // steady sample. The tiny rounding difference is below audible level.
    const i32 base_output_gain = multiply(DigitalTextureMode::output_gain(digital_amount), kOutputLevel);
    // The rounded low-W-Shaper waveform has less perceived body than its
    // sharper settings. Apply a gentle, control-rate quadratic lift that
    // reaches 75% at 0% W Shaper and vanishes at 100%.
    const i32 low_shape = maximum - normalized(shape);
    const i32 low_shape_squared = multiply(low_shape, low_shape);
    const i32 shape_boost = multiply(base_output_gain,
                                     multiply(low_shape_squared, kLowShapeOutputBoost));
    const i32 shaped_output_gain = base_output_gain + shape_boost;
    // Subs need extra perceived loudness. This is also control-rate work and
    // reaches +30% only at octave -3.
    const i32 octave_boost = output_boost_for_octave(octave);
    const i32 octave_lift = multiply(shaped_output_gain, octave_boost);
    const i32 output_gain = shaped_output_gain >= kOutputGainCeiling - octave_lift
                                ? kOutputGainCeiling
                                : shaped_output_gain + octave_lift;

    // At neutral DetuneRM and unison octave the two oscillators have identical
    // phase, increment and shape. Rendering the second copy then crossfading
    // it cannot add sound, so render the carrier once while preserving all
    // phase state for a later parameter change.
    if (neutral_detune && octave == 3) {
        for (unsigned i = first_frame; i < first_frame + frame_count; ++i) {
            const u32 increment = pitch[i] > kNyquistIncrement ? kNyquistIncrement : pitch[i];
            const i32 main = shaped_wave(voice.main_phase, shape);
            const i32 digital = digitalize(voice, main, digital_amount, voice.main_phase, digital_bit_mask);
            output[i] = multiply(digital, output_gain);
            voice.main_phase += increment;
            voice.sub_phase += increment;
            voice.ring_phase += increment;
        }
        return;
    }

    Ratio selected_ratio = {1, 1};
    if (!neutral_detune && !BREADBOARD_CLASSIC_DETUNE_RM) {
        const unsigned ratio_count = sizeof(kDetuneRatios) / sizeof(kDetuneRatios[0]);
        selected_ratio = ring_side ? right_ring_ratio(distance)
                                   : kDetuneRatios[ratio_position(distance, ratio_count)];
    }
    // These three values depend only on a steady control position. The old
    // ring path rebuilt them in every sample even though no input to them can
    // change until the next block.
    i32 ring_phase_depth = 0, ring_amount = 0, ring_voice_amount = 0, ring_wet_amount = 0;
    i32 unison_ring_wet = 0;
    if (ring_side && !BREADBOARD_CLASSIC_DETUNE_RM) {
        ring_phase_depth = saturating_positive_add(
            multiply(bell_depth, kMusicalPhaseDepth),
            multiply(bell_extension, kBellExtraPhaseDepth));
        ring_amount = blend(kMusicalRingAmount, kBellExtremeRingAmount, bell_extension);
        ring_voice_amount = blend(kMusicalVoiceAmount, kBellExtremeVoiceAmount, bell_extension);
        // blend(main, blend(main, metallic, voice), depth) is the same
        // crossfade shape as blend(main, metallic, depth * voice), apart
        // from one less rounding step. Calculate its fixed amount once.
        ring_wet_amount = multiply(bell_depth, ring_voice_amount);
        // The unison mix is also a fixed control-derived coefficient; keeping
        // it out of the 64-sample loop avoids a redundant Q31 multiply.
        if (octave == 3) unison_ring_wet = multiply(ring_wet_amount, maximum - sub_mix);
    }

    for (unsigned i = first_frame; i < first_frame + frame_count; ++i) {
        const u32 main_increment = pitch[i] > kNyquistIncrement ? kNyquistIncrement : pitch[i];
        const u32 base_sub = octave_increment(main_increment, octave);
        u32 modulator_increment = base_sub;
        if (BREADBOARD_CLASSIC_DETUNE_RM) {
            modulator_increment = detuned_increment(base_sub, active_distance, ring_side);
        } else if (!neutral_detune) {
            modulator_increment = cached_ratio_increment(voice, base_sub, selected_ratio);
        }
        const u32 sub_increment = (BREADBOARD_CLASSIC_DETUNE_RM || !ring_side)
                                      ? modulator_increment
                                      : base_sub;
        const i32 main = shaped_wave(voice.main_phase, shape);
        i32 pair = main;
        if (ring_side) {
            if (BREADBOARD_CLASSIC_DETUNE_RM) {
                const i32 sub = shaped_wave(voice.sub_phase, shape);
                const i32 sub_signal = blend(sub, multiply(main, sub), distance);
                pair = blend(main, sub_signal, sub_mix);
            } else {
                const u32 ring_phase = voice.ring_phase + 0x40000000u;
                const i32 ring_source = BREADBOARD_FAST_RING_SOURCE ? triangle(ring_phase)
                                                                      : shaped_wave(ring_phase, shape);
                const i32 phase_q31 = multiply(ring_source, ring_phase_depth);
                const u32 phase_offset = static_cast<u32>(phase_q31) << 1;
                const u32 bell_phase = voice.main_phase + phase_offset;
                const i32 bell_carrier = BREADBOARD_FAST_RING_CARRIER ? triangle(bell_phase)
                                                                        : shaped_wave(bell_phase, half(shape));
                // Phase modulation alone supplies a stable, bell-like metal
                // spectrum. The optional product adds a rougher edge but
                // needs another multiply and crossfade at every sample.
                i32 metallic = bell_carrier;
                if (BREADBOARD_RING_PRODUCT) {
                    const i32 ring_product = multiply(bell_carrier, ring_source);
                    metallic = blend(bell_carrier, ring_product, ring_amount);
                }
                if (octave == 3) {
                    // In unison the audible sub is exactly main, so fold the
                    // outer sub mix into the metallic crossfade and skip it.
                    pair = blend(main, metallic, unison_ring_wet);
                } else {
                    const i32 sub = shaped_wave(voice.sub_phase, shape);
                    const i32 ring_carrier = blend(main, metallic, ring_wet_amount);
                    pair = blend(ring_carrier, sub, sub_mix);
                }
            }
        } else {
            const i32 sub = shaped_wave(voice.sub_phase, shape);
            pair = blend(main, sub, sub_mix);
        }
        const i32 digital = digitalize(voice, pair, digital_amount, voice.main_phase, digital_bit_mask);
        output[i] = multiply(digital, output_gain);
        voice.main_phase += main_increment;
        voice.sub_phase += sub_increment;
        voice.ring_phase += modulator_increment;
    }
}
}  // namespace

void start(Voice& voice, Shared&, u32 owner) {
    voice.main_phase = voice.sub_phase = voice.ring_phase = 0;
    voice.random = 0x6d2b79f5u ^ (owner * 0x9e3779b9u);
    voice.noise_phase_offset = noise_hash(voice.random ^ 0xa511e9b3u);
    voice.noise_count = 0;
    voice.held_noise = 0;
    voice.controls_ready = 0;
    voice.ratio_cache_valid = 0;
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

    bool controls_steady = true;
    for (unsigned control = 0; control < 4; ++control)
        controls_steady = controls_steady && voice.controls[control] == targets[control];
    if (controls_steady) {
        render_steady(voice, output, pitch, 0, opxy::block_frames);
        return;
    }

    // The former control-motion renderer rebuilt the most expensive ring
    // algorithm for every sample, unlike the normal steady path. Move the
    // same one-pole controls in short slices instead: each slice uses the
    // optimized renderer and keeps oscillator phases continuous.
    const unsigned control_frames = 8;
    for (unsigned first_frame = 0; first_frame < opxy::block_frames;
         first_frame += control_frames) {
        for (unsigned step = 0; step < control_frames; ++step)
            for (unsigned control = 0; control < 4; ++control)
                smooth(voice.controls[control], targets[control]);
        render_steady(voice, output, pitch, first_frame, control_frames);
    }
}
}  // namespace breadboard

# breadboard

<img width="1140" height="538" alt="image" src="https://github.com/user-attachments/assets/26d44fde-1281-4eca-bde5-a66e3c3854b8" />

An original OP-XY synth inspired by the publicly documented control behavior
of the OP-1 and OP-1 field **Digital** engine. It is not a binary-accurate
port and contains no Teenage Engineering source, assets or firmware logic.

| Control | Behavior |
| --- | --- |
| W Shaper | OP-XY's eight-character label for the OP-1's **Wave Shaper**: morphs a rounded, fundamental-heavy waveform toward a raw saw. |
| Octave | Transposes the sub oscillator from -3 to +3 octaves; 0 is unison. Positive octave settings receive a small presence boost. |
| DetuneRM | The abbreviated display label for **Detune and Ring Modulation**: `-2%` through `+2%` is neutral; below that the sub lands on musical ratios, while above it an independent modulator snaps through a fifth, octave and twelfth before the final detuned FM-bell range. |
| Digital | The OP-1's **Digitalness** control: adds a per-note, pitch-locked noise texture, a bounded folding curve and increasing bit reduction. |

## Hardware CPU profiling

Measured on an OP-XY running OS 1.2.1 with the SDK's
`opxy-synth-c8-v1` workload: one whole voice at note 60, velocity 127,
controls 0–3 at midpoint, 1,000 rendered blocks followed by release.

| Run | Mean block budget |
| --- | ---: |
| 1 | 2.171% |
| 2 | 2.170% |
| 3 | 2.181% |
| **Median** | **2.171%** |

Measured package: commit `d9b4b41`, firmware SHA-256
`e279bff69e22d10570c972e60125127f0ec3a2b476632ed3c971e32e46c538f4`.
This is a synthetic whole-voice measurement, not a maximum-polyphony or
live-project utilization claim.


# breadboard

An original OP-XY synth inspired by the publicly documented control behavior
of the OP-1 and OP-1 field **Digital** engine. It is not a binary-accurate
port and contains no Teenage Engineering source, assets or firmware logic.

| Control | Behavior |
| --- | --- |
| W Shaper | OP-XY's eight-character label for the OP-1's **Wave Shaper**: morphs a rounded, fundamental-heavy waveform toward a raw saw. |
| Octave | Transposes the sub oscillator from -3 to +3 octaves; 0 is unison. Positive octave settings receive a small presence boost. |
| DetuneRM | The abbreviated display label for **Detune and Ring Modulation**: `-2%` through `+2%` is neutral; below that the sub lands on musical ratios, while above it an independent modulator snaps through a fifth, octave and twelfth before the final detuned FM-bell range. |
| Digital | The OP-1's **Digitalness** control: adds a per-note, pitch-locked noise texture, a bounded folding curve and increasing bit reduction. |

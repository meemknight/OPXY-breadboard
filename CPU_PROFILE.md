# breadboard: hardware CPU measurement

Machine commit: `d9b4b41`. SDK: Open-XY `v0.0.2` (machine SDK 1.3). Device OS: OP-XY 1.2.1. Benchmark: `opxy-synth-c8-v1`. Scope: one whole voice, including oscillator and filter, at note 60 / velocity 127 with controls 0–3 at their midpoint; it renders 1,000 blocks before release.

Median of 3 run means: **2.171%** of the stock 580,352-cycle block budget. Run means: 2.171%, 2.170%, 2.181%.

This is a synthetic measurement on the USB-MIDI command endpoint, not total device CPU, maximum polyphony or a worst-case settings sweep. Graphics are excluded. Callback tests exclude sample preparation, allocation, stock mixing and voice processing; they include stopwatch-call overhead and may include interrupt time. See docs/profiling.md for the versioned workloads.

Firmware SHA-256: `e279bff69e22d10570c972e60125127f0ec3a2b476632ed3c971e32e46c538f4`.

Attribution uses the user-confirmed installed package, expected case order and exact report count; the device does not attest its firmware hash. Raw captures remain private because they contain device identity.

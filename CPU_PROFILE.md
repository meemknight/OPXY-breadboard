# breadboard: hardware CPU measurement

Machine commit: `fa4d2b1e9eb07b3256620e2ae0e9eafb9d20bebe`. Benchmark: `opxy-synth-c8-v1`. Scope: whole voice, including oscillator/filter.

Median of 3 run means: **9.899%** of the stock 580,352-cycle block budget. Run means: 9.899%, 9.899%, 9.899%.

This is a synthetic measurement on the USB-MIDI command endpoint, not total device CPU, maximum polyphony or a worst-case settings sweep. Graphics are excluded. Callback tests exclude sample preparation, allocation, stock mixing and voice processing; they include stopwatch-call overhead and may include interrupt time. See docs/profiling.md for the versioned workloads.

Firmware SHA-256: `540382be005f66e0705d7e895b5918f9b146225bab0d3276be297d35551f0b39`.

Compile report SHA-256: `ffa6fb704b268c35b271cf0827d6cefd4135840d0f34fe38b63847b1f9d9dc80`.

Attribution uses the user-confirmed installed package, expected case order and exact report count; the device does not attest its firmware hash. Raw captures remain private because they contain device identity.

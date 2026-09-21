# Extend CST Audio 12dyn — builder's guide

This repository is intended to be **forked and extended once the owner has supplied reuse permission**. Public viewing is not a license: read [README reuse rights](../README.md#evidence--limitations) and obtain explicit permission or wait for an explicit license before redistributing third-party copies. Everything below describes possible upgrades, **not** functionality already implemented.

Read [the friendly math guide](MATH_FOR_HUMANS.md) and [the exact equations](AUDIO_MATH.md) first. Current code is in [src/audio.cpp](../src/audio.cpp), public API in [audio.hpp](../include/cst_audio/audio.hpp), CLI in [main.cpp](../src/main.cpp), and baseline tests in [test_audio.cpp](../tests/test_audio.cpp).

## 1. What exists, and what you'd be adding

| Current system | Upgrade opportunity | What would establish success |
| --- | --- | --- |
| CSV-timed footstep events | Timeline, MIDI, game-state or video event adapter | Exact timestamps and repeatable imported events |
| Six stylized surface presets | Licensed measured/recorded material library; new synthesized materials | Blind listening plus metadata and provenance |
| 12D state with **two** audible taps | Learnable or user-authored routing from all or some dimensions | Same-compute ablation against the two-tap version |
| CPU single-thread offline WAV | Real-time host/plugin, SIMD, multithreading or GPU | Glitch/latency tests on target hardware |
| Fixed math, no training | Separate trainable parameter estimator or audio model | Reproducible train/validation/test split and cost logs |
| Mono/stereo pan | Occlusion, room reverb, binaural/HRTF or multichannel | Spatial audio and latency evaluation |

**Design invariant:** \( \mathrm{MODEL}\ne\mathrm{STATE}\ne\mathrm{AUTHORITY} \). If you connect a trained model, let it recommend bounded event/synthesis parameters; do **not** let model output execute shell commands, select arbitrary filesystem paths, leak recordings or inherit credentials.

## 2. Build the unmodified baseline before editing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/cst-foley --probe
./build/cst-foley --render --events examples/footsteps.csv \
    --seconds 4.5 --seed 127 --no-ambience --out baseline.wav
./build/cst-foley --render --events examples/footsteps.csv \
    --seconds 4.5 --seed 127 --no-ambience --static --out static.wav
```

If a platform uses multi-config CMake, add \`--config Release\` and use the platform-specific executable path. Keep both WAV files, CLI reports, compiler version, OS, CPU, RAM, actual command line, and commit SHA with each experiment. They are your baseline evidence, not a guarantee about any other hardware.

## 3. Add a **new surface** safely

A surface change requires a **coordinated** update to four locations:

1. Add the value in \`enum class Surface\` in \`include/cst_audio/audio.hpp\`; extend \`parse_surface\` and \`surface_name\` in \`src/audio.cpp\`.
2. Add its body and texture formulas in the \`switch (v.surface)\` of \`render\`. Use a stable local elapsed time \`t\` and deterministic \`noise(v.seed,n)\` where possible.
3. Decide its duration (\`decay\` choice) and hardness-drive coefficient (\`h_j\`). These are **independent choices**; the current engine groups metal/water for durations and metal/concrete for hardness.
4. Add a CSV fixture and tests for deterministic output, nonzero signal, bounds, CLI acceptance and audible comparison. Update [AUDIO_MATH.md](AUDIO_MATH.md).

For example, **proposed, not implemented**: snow could use a quiet, low-frequency crunch burst combined with short granular clicks. Start from an equation for the sound, not a label alone. Tune on legally obtained recordings if you use recordings for comparison. Do not treat synthetic "snow" as physical truth.

## 4. Make more CST state dimensions audible

The renderer currently drives all 12 components with a four-value repeated input, but reads only \`state[1]\` (timbre) and \`state[4]\` (texture). An optional general controller could use a parameter vector \(q\) and a *calibrated, bounded* routing matrix:

\[
q_k[b]=\operatorname{clip}\left(q_k^{(0)}+
       \sum_{i=0}^{11} W_{ki}\,x_i[b+1],\ q_k^{\min},q_k^{\max}\right).
\]

Say this as "each output setting is a baseline plus a weighted sum of states, then bounded." Here \(W\) are **new proposed routing parameters**: they do not exist in the current code, and calling them learned weights would require an actual fitting procedure.

Potential outputs: oscillator frequency, transient decay, surface-noise gain, saturation, reverb send. Proposed safeguards:

- Keep \(0 < f_\mathrm{osc} < F_s/2\) and add anti-aliasing for harmonics / nonlinearities. Existing formulas are already stylized and **not bandlimited**.
- Bound gains, decays and cumulative voice levels; add a limiter if many events overlap.
- Smooth parameter changes **across blocks**: in today's code \(T_b,Q_b\) are held constant for a block and can jump at a boundary, possibly creating clicks.
- Avoid assigning a new audio interpretation to all dimensions based solely on the number 12. Measure whether the additional taps matter.

Control conditions: static \(x=0\), existing two-tap dyn12, same-cost randomly shuffled taps, proposed multi-tap dyn12, and, if training, equal-size trainable non-CST controller. Run across several seeds and overlapping-event counts. Report nulls.

## 5. Make the renderer *actually* real-time

The current renderer renders entire WAV buffers offline and scans **all voices every block** to compute the state drives. It also erases expired voices during per-sample processing, and allocates a full interleaved output vector. Merely reducing \`block_frames\` does **not** make this a real-time engine.

Suggested incremental separation:

```text
CSV/game events -> validated scheduler -> active-voice index
                                  |-> bounded feature extractor -> dyn12 state
                                  |-> sample generation -> audio callback -> device
                                                      \-> optional offline WAV
```

A real-time implementation should preallocate voices, buffers and DSP scratch, avoid disk/network/locks/allocations in the callback, preserve state and deterministic event ordering, and handle event onsets **causally** within a block. Add a fixed maximum polyphony and a documented voice-stealing rule. Benchmark worst-case callback time under overlap and device buffer deadlines: e.g. \(N/F_s\) seconds for a buffer of \(N\) frames at rate \(F_s\). Note that this buffer duration is **only a deadline budget**, not a proven end-to-end or hardware latency.

## 6. Make it truly hardware-specific

Do not use random people's GPU/CPU numbers as an optimization target. Get the **actual end user's** OS, CPU family, available RAM, playback interface, preferred sample rate, expected overlap and quality/latency constraints. \`--probe\` presently reports OS, *logical* core count, physical RAM if reported, and a conservative default profile. It does **not** detect GPU, vector instruction sets or sound device capability.

Build measured profiles by varying a single factor at a time (sample rate, block frames, active voices, SIMD implementation). Record runtime, peak memory, audio quality and **normalized compute cost** for identical clips on one target. Test no-event, single-event and dense-event cases. CPU clock scaling, thermal throttling and background applications introduce variance: repeat and report median and range. Avoid selecting profiles based on a single fast run.

A GPU kernel is optional and may lose on short clips due to transfer/setup cost; benchmark before implementing one. Never assert an environment-specific cost saving until the measurements exist.

See [PROFILE.md](PROFILE.md).

## 7. Add trainable audio *only when warranted*

The existing sound engine doesn't require training. If you want a model to infer scene events, estimate synthesis parameters, produce residual audio, or generate arbitrary sound, that's a **separate experiment**. A reasonable bounded adapter is:

```text
owned/licensed events + reference audio
 -> feature extraction and dataset versioning
 -> trainable parameter predictor
 -> validation and safety bounds
 -> existing C++ synth (no filesystem/tool authority)
 -> measured WAV and listening evaluation
```

Define task and objective first: e.g. attack time/decay from aligned dry footsteps, or correct-material selection from annotated video. Use distinct recording sessions/speakers/shoes/surfaces across splits as applicable to prevent leakage. Establish rights to both input and target audio. A spectral/reconstruction loss can be useful, but needs listening evaluations; lower numerical loss does not guarantee better perceived Foley.

Preserve data provenance, model/provider identity, training hours and actual billable costs, random seeds, checkpoints, failed conditions and privacy rules. **This repo currently contains no training loop, model checkpoint or such evidence.** Do not imply that it already solves broad SFX generation.

## 8. Suggested experiment record template

```text
Experiment ID:
Owner, date and source commit:
Hardware: OS, CPU, RAM, audio interface, sample rate:
Data: event CSV version/hash, rights, duration, concurrent voices:
Conditions: static / current dyn12 / new variant:
Shared seed, render settings and repetitions:
Output artifacts: WAV hashes, peak_preclip, RMS:
Performance: runtime, peak RSS, callback deadline (if applicable):
Listening: anonymized blind ratings, protocol and sample size:
Nulls/failures:
Conclusion limited to the measured task:
```

**Engineering note:** Before merging a behavior change, run all existing tests, add a dedicated test for the change, review the WAV and measurements, and update the equation sheet. Documentation or code may evolve; previously measured outcomes must remain historical facts. Do not auto-enable experimental changes or break the static control.

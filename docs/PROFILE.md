# Hardware-specific optimization and measurement

No one else's machine is silently used as a target. The hardware probe runs on the user's own computer, and explicit command-line settings always override the conservative profile. The renderer is offline, on one CPU thread; core count informs only the default chunk length and sample rate.

1. Run `cst-foley --probe` on the target machine. Note CPU model separately if willing, RAM, OS, driver, sample rate, performance goal and expected concurrent events. Do **not** post serial numbers, audio recordings, private paths or API keys.
2. Produce the same CSV output with `--sample-rate 32000 --block 512`, then `--sample-rate 48000 --block 256`, and `--static` controls. Set identical `--seed` and `--no-ambience` to isolate the effect of dyn12.
3. Measure wall time and peak RSS with native OS tools. Compare WAV RMS/peak and blind-listen across materials, noting any unacceptable artifacts. Clip the input events/seconds identically across conditions.
4. Tune explicit profile from **target-machine measurements**, not guessed hardware. If a trained model is desired, first establish a licensed dataset, inference engine and separately measured CPU/GPU budget; this repo provides neither a training pipeline nor model weights.

Example: `cst-foley --render --events examples/footsteps.csv --seconds 4.5 --sample-rate 48000 --block 256 --seed 127 --no-ambience --out out.wav`.

Security: events only supply timestamp and bounded numeric/material fields, with a 100,000-event read limit. Rendering is capped at 600 seconds and WAV output at the RIFF 4 GiB boundary; do not write untrusted output paths. This is not a sandbox for arbitrary executable plugins.

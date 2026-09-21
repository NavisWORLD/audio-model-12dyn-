# Audio math for humans — learn the CST/dyn12 Foley method

You do **not** need to know calculus to begin. This is a guided translation of the **specific public-reference C++ addon** into sounds and ordinary language. The [technical equation sheet](AUDIO_MATH.md) gives the full details.

## First, the entire idea in one sentence

**Write down when a foot hits a surface → turn that into impact and texture waves → use a 12-number state to change a couple of sound settings as footsteps happen → put the waves into a WAV file.**

This is *procedural synthesis*: calculating wave samples. It is **not** a trained text-to-sound neural network.

## Say the symbols out loud

| Written | Say it | Think of it as |
| --- | --- | --- |
| \(x_i\) | "ex sub eye" | One number from a 12-number memory of recent events |
| \(x_i[b]\) | "ex sub eye at block bee" | That number during a processing block |
| \(u\) | "you" | Current input features, e.g., footsteps and hardness |
| \(\theta\) | "THAY-tuh" | A fixed angle multiplier in the reference update |
| \(\tanh\) | "hyperbolic tangent" ("tan-ch") | A smooth limiter that keeps state between -1 and +1 |
| \(\sin\) | "sine" | A smoothly oscillating wave |
| \(\pi\) | "pie" | The circle constant, about 3.14159 |
| \(e^{-kt}\) | "ee to the minus kay tee" | A sound that fades over time |
| \(\sum\) | "sigma" or "sum" | Add several voices together |
| \(\bmod\) | "mod" | Remainder after division; here repeats four inputs across 12 states |
| \(\sqrt{\phantom{x}}\) | "square root" | Used to calculate average signal level (RMS) |
| \(\operatorname{clip}(v,-1,1)\) | "clip vee between minus one and one" | Stop samples from exceeding the PCM range |

**Don't confuse:** Sigma can mean "sum" when written as \(\sum\), but \(\sigma\) (a smaller Greek sigma) often means a statistical scale elsewhere. This renderer does not use \(\sigma\) in its waveform equations.

## Lesson 1: How a computer makes an audible sound

A microphone normally measures air pressure and stores a rapidly changing number. A synthesizer instead **calculates** those numbers. For a sine wave:

\[
y[n]=\sin(2\pi f n/F_s).
\]

- \(n\): "en", current sample number.
- \(f\): "eff", frequency in hertz; higher tends to sound higher-pitched.
- \(F_s\): "eff sub ess", number of samples made every second.

At 48,000 samples/second, the program calculates 48,000 numbers for one second of mono sound, or two numbers per frame for stereo.

**Try:** replace \(f=95\) Hz with \(f=420\) Hz in a *new experiment branch* and listen. The engine already uses roughly these frequencies for different material bodies.

## Lesson 2: Why a footstep isn't a pure tone

Real footsteps are short impacts with noise, resonances, and a changing envelope. The current renderer *approximates* a footstep by combining:

1. **Body:** a low or metallic sine-like vibration.
2. **Texture:** deterministic pseudo-noise that resembles contact with the surface.
3. **Envelope:** a fast fade-in and a longer exponential fade-out.

In everyday words:

\[
\text{one step} = \text{loudness}\times\text{envelope}
                  \times(\text{tone}+\text{texture}).
\]

The exact coefficients for all six surfaces are in [AUDIO_MATH.md](AUDIO_MATH.md#32-the-mathematical-material-palette). These are artistic starting points, not recordings or validated physics.

**Try:** change a step's \`intensity\` from \`0.3\` to \`0.9\` in a copy of [footsteps.csv](../examples/footsteps.csv). Keep the other parameters fixed. You should get different waveform values.

## Lesson 3: What the twelve CST numbers actually do

Think of twelve little gauges that react to the footsteps. Every *block* of audio, the engine reads four quantities: overall activity, hardness, panning, and intensity of brand-new onsets.

The public reference is:

\[
\text{new gauge}=\tanh(0.86\times\text{old gauge}
 +0.14\times\text{its input}
 +\text{tiny sine forcing}).
\]

The old state persists a little, new information pushes it, and \`tanh\` keeps the value bounded. The full forcing is \(0.015\sin((b+1)(i+1)\theta)\) with \(\theta=0.17320508075688773\).

**Crucial implementation detail:** there are 12 evolving gauges, but **only two** are wired into sound today:

- Gauge \(x_1\) changes the oscillator frequency for **wood and metal**.
- Gauge \(x_4\) changes the noise/texture gain for **all six materials**.

The rest are **available for future experiments**, not twelve separate audible features today. It would be misleading to call this a 12-channel audio model.

### A gentle worked example

Imagine zero initial state, the first 256-frame block, and exactly one active wood step of intensity 0.8 with pan -0.4. Its four feature inputs are:

\[
u=[0.8,\ 0.24,\ -0.32,\ 0.8].
\]

Why 0.24? Wood hardness factor \(0.3\) times intensity \(0.8\). Why -0.32? Pan \(-0.4\) times intensity \(0.8\). The onset value 0.8 assumes the footstep begins **inside that block**.

For gauge \(i=1\) (the hardness-connected one):

\[
x_1[1]=\tanh(0.86\times0+0.14\times0.24
 +0.015\sin(1\times2\times\theta)).
\]

This is a well-defined number between -1 and +1. It influences wood's frequency as \(95+22x_1[1]\) Hz during that block. This example is a **calculation**, not proof that a human will prefer it. Note that the bundled demo events start at other times, so this is an illustrative input, not a claim about the demo's first block.

## Lesson 4: Left foot, right foot

A pan value \(p=-1\) means left; \(p=0\) means center; \(p=+1\) means right.

\[
\phi=(p+1)\pi/4,\quad L=v\cos\phi,\quad R=v\sin\phi.
\]

The program adds overlapping steps into each channel, optionally adds a quiet noise bed, clips to the legal PCM range, and writes WAV. This is equal-power *panning of one voice*, not spatial acoustics or head-tracked 3D audio.

## Lesson 5: How to tell whether your upgrade helps

Use the same event file, length, sample rate, channels, and seed. Render two versions:

```bash
cst-foley --render --events examples/footsteps.csv --seconds 4.5 \
  --seed 127 --no-ambience --out dynamic.wav

cst-foley --render --events examples/footsteps.csv --seconds 4.5 \
  --seed 127 --no-ambience --static --out static.wav
```

Windows users can run these as separate one-line commands in PowerShell using \`build\\Release\\cst-foley.exe\`.

Listen blind; compare the printed \`peak_preclip\` and \`rms\`. A difference in samples says only that the algorithm changed, **not** that it sounds more realistic or is cheaper on other computers. If both sound equally good, record that null result.

## Choose your next lesson

- **Exact equations and line-to-code mapping:** [AUDIO_MATH.md](AUDIO_MATH.md).
- **Add a new material, effect or renderer:** [EXTENDING_THE_ENGINE.md](EXTENDING_THE_ENGINE.md).
- **Measure it on *your* computer:** [PROFILE.md](PROFILE.md).

Cory Davis / NavisWORLD. Public reference lineage: [Beast Box dyn12.py](https://github.com/NavisWORLD/The-beast-box-/blob/main/beastbox/dyn12.py). This is instructional documentation for the present renderer, not a general statement about all CST research.

# CST/dyn12 audio mathematics — exact implementation guide

**Scope:** This document explains the *current C++17 procedural Foley renderer*, line for line. The CST link is an independent application of the **public** [Beast Box dyn12 reference](https://github.com/NavisWORLD/The-beast-box-/blob/main/beastbox/dyn12.py). No historical private CST equation, trained sound model, biological hearing claim, quantum effect, or measured superiority is implied. Code is the authority where prose and implementation differ.

**Start here if you're new to math:** [MATH_FOR_HUMANS.md](MATH_FOR_HUMANS.md). **To change or extend this engine:** [EXTENDING_THE_ENGINE.md](EXTENDING_THE_ENGINE.md). Code: [src/audio.cpp](../src/audio.cpp), [audio.hpp](../include/cst_audio/audio.hpp), [tests](../tests/test_audio.cpp).

## 1. Four levels of time and their units

| Symbol | Pronunciation | Meaning | Code |
| --- | --- | --- | --- |
| \(F_s\) | "eff sub ess" | Sample rate in samples per second (Hz) | \`cfg.sample_rate\` |
| \(N\) | "en" | Frames per processing block | \`cfg.block_frames\` |
| \(n\) | "en" | Absolute audio frame, integer | \`frame\` |
| \(b\) | "bee" | Block index, integer starting at 0 | \`block_index\` |
| \(t\) | "tee" | Time elapsed since one event's onset, seconds | \`(frame - v.start) / cfg.sample_rate\` |
| \(j\) | "jay" | Event / voice index | \`voices[j]\` |

One audio frame contains one mono sample or one sample per stereo channel. An event at \(\tau_j\) seconds is converted to a sample onset:

\[
n_j = \operatorname{round}(\tau_j F_s),\qquad t_j(n)=(n-n_j)/F_s.
\]

The current code uses \`std::llround\` for event onsets. If an event is so close to the end that rounding gives \(n_j\) equal to the frame count, it produces no audio. The project is an **offline renderer**; block size is a control-update choice, **not** a verified real-time audio latency.

At \(F_s=48{,}000\) Hz and \(N=256\), nominal control-update rate is \(F_s/N=187.5\) blocks/second; a final partial block may be shorter. Larger \(N\) means fewer state updates but coarser state changes. Audio samples remain at \(F_s\).

## 2. The public dyn12 reference equation

There are exactly 12 scalar states \(x_i[b]\), where \(i\in\{0,\dots,11\}\). Their initial values are all zero, and the current renderer evolves them once per block:

\[
x_i[b+1] = \tanh\left(
 0.86\,x_i[b] +
 0.14\,u_{i\bmod 4}[b] +
 0.015\,\sin\big((b+1)(i+1)\theta\big)
\right),
\qquad \theta=0.17320508075688773.
\]

The \`update_dyn12\` **library function** also accepts an arbitrary finite drive vector: its actual index is \(i\bmod|\mathrm{drive}|\); if the vector is empty, \(u=0\). Only the **renderer** supplies exactly four drive values.

Read this as:
- \(0.86 x\) retains a portion of the previous state (a *leaky recurrence*).
- \(0.14 u\) injects current sound-event features.
- The small deterministic sine term gives each state dimension a different forcing phase.
- \(\tanh\) ("hyperbolic tangent") bounds each new state strictly between \(-1\) and \(1\) for finite inputs.

The coefficients are **reference implementation choices**, not learned weights or proven physically optimal constants. No claim is made that 12 is the uniquely correct number of perceptual audio dimensions. Changing the equation yields a new experimental variant and requires its own controls.

### The four input drives

For a voice \(j\), let its intensity be \(a_j\in[0,1]\), stereo pan be \(p_j\in[-1,1]\), and hardness coefficient be

\[
h_j=\begin{cases}1.0,&\text{metal or concrete},\\0.3,&\text{wood, gravel, fabric, or water}.\end{cases}
\]

Define \(A_b\) as all events whose sampled onset \(n_j\) is *strictly before* this block's end \(e_b\), and whose end \(n_j+D_j\) is *strictly after* its start \(s_b\). This is a block-level overlap test. \(\operatorname{clip}(v,l,r)=\min(r,\max(l,v))\).

\[
\begin{aligned}
u_0[b]&=\operatorname{clip}\big(\sum_{j\in A_b}a_j,\ 0,\ 1\big)&\text{activity},\\
u_1[b]&=\operatorname{clip}\big(\sum_{j\in A_b}h_ja_j,\ 0,\ 1\big)&\text{hardness},\\
u_2[b]&=\operatorname{clip}\big(\sum_{j\in A_b}p_ja_j,\ -1,\ 1\big)&\text{pan},\\
u_3[b]&=\operatorname{clip}\big(\sum_{\substack{j\in A_b\\s_b\le n_j<e_b}}a_j,\ 0,\ 1\big)&\text{new-onset intensity}.
\end{aligned}
\]

These are simple heuristics, not calibrated physical quantities. Several overlapping footsteps saturate the 0..1 drives. An onset later in a block can affect that block's state from its first sample, because state is computed from whole-block features in advance; this is an **offline look-ahead artifact**, not strictly sample-causal processing. A real-time version should split blocks at event onsets or use a causal per-segment update.

Because drive indices repeat every four dimensions, \(x_0,x_4,x_8\) see activity; \(x_1,x_5,x_9\) see hardness; \(x_2,x_6,x_{10}\) see pan; and \(x_3,x_7,x_{11}\) see new onsets. **Currently only \(x_1\) and \(x_4\) are connected to the audio waveform:**

\[
T_b=x_1[b+1]\quad\text{("timbre", from hardness lane)},\qquad
Q_b=x_4[b+1]\quad\text{("texture", from activity lane)}.
\]

The other ten states evolve, but are **not** directly heard. They must not be described as 12 independently functional audio controls.

The \`--static\` control bypasses all updates and fixes \(T_b=Q_b=0\), while holding the rest of the event/rendering pipeline constant.

## 3. From event to sound: synthesis

Each event selects a surface. The current implementation uses **deterministic synthesized signals**, not an external recording or an inference model.

### 3.1 Finite voice lifetime and impact envelope

Event duration in frames is \(D_j=\lceil d_jF_s\rceil\), with \(d_j=0.42\) s for metal, \(0.34\) s for water, and \(0.27\) s for all other surfaces.

While the event is active, its envelope at elapsed time \(t\) is

\[
E_j(t)=e^{-\lambda_j t}\,\min(1,450t),\qquad
\lambda_j=\begin{cases}9,&\text{metal},\\15,&\text{otherwise}.\end{cases}
\]

An additional decay appears inside material-specific *body oscillators* below; don't mistake that separate decay for \(E_j\). At \(t=0\), the fast fade-in term is zero; by \(t=1/450\) seconds (~2.22 ms), it reaches one. At 48 kHz, this is about 107 samples. The exponential decay describes a design choice for transient shape, **not** a material-acoustic measurement.

### 3.2 The mathematical material palette

Let \(r_j[k]\) denote the event-seeded deterministic noise evaluated at the integer local sample \(k=n-n_j\). The implementation uses a 32-bit integer hash mapped to \([-1,1]\): it is repeatable pseudo-noise, not cryptographic randomness and not a recorded floor texture. Set \(f(t,\nu)=\sin(2\pi\nu t)\).

Each surface has a **body** \(B_j(t)\) and a **texture** \(S_j(t)\):

| Surface | \(B_j(t)\) (low-frequency / tonal body) | \(S_j(t)\) (surface noise) |
| --- | --- | --- |
| Wood | \(f(t,95+22T_b)e^{-17t}\) | \(0.45r_j[k]\) |
| Gravel | \(0.4 f(t,74)e^{-18t}\) | \(r_j[k]\,(0.8+0.2f(t,61))\) |
| Metal | \(0.55\,[f(t,420+20T_b)+0.45f(t,873)]e^{-10t}\) | \(0.38r_j[k]\) |
| Concrete | \(0.65 f(t,67)e^{-25t}\) | \(0.46r_j[k]\) |
| Fabric | \(0.24 f(t,51)e^{-21t}\) | \(0.14\,[r_j[k]+r_j[\lfloor k/5\rfloor]]\) |
| Water | \(0.2 f(t,131)e^{-19t}\) | \(r_j[k]\,(0.43+0.15f(t,23))\) |

These labels describe **stylized** sound palettes, not validated acoustic models for specific shoes, terrain, microphones, or listening environments. For fabric, the \(\lfloor k/5\rfloor\) noise lookup is a slower repeated-noise component; it is **not** an actual low-pass filter.

The mono event's unclipped signal is then

\[
v_j[n] = a_j E_j(t)\left[0.55\,B_j(t) + (0.38+0.08Q_b)\,S_j(t)\right].
\]

The activity-linked \(Q_b\) modulates texture by a small amount. Wood and metal also respond to \(T_b\) through frequency. No other surface changes oscillator frequency from dyn12 in this version. A different material palette or routing matrix would be an extension, not the present algorithm.

### 3.3 Stereo, summation, and optional bed

For each event, \(\phi_j=(p_j+1)\pi/4\). Before clipping:

\[
L[n] = \sum_jv_j[n]\cos\phi_j,\qquad
R[n] = \sum_jv_j[n]\sin\phi_j.
\]

These are equal-power *pan gains*, although total perceived power depends on source correlations and summation. If enabled, a low-level pseudo-noise bed is added **equally** to left/right:

\[
A[n]=0.004\left[r_{s_1}(\lfloor n/16\rfloor)+r_{s_2}(\lfloor n/49\rfloor)\right].
\]

Seeds \(s_1,s_2\) are deterministic functions of the configured seed; the exact implementation is in \`audio.cpp\`. Use \`--no-ambience\` when isolating footstep timbre. Mono output uses \(M[n]=(L[n]+R[n])/\sqrt{2}\), *not* an arithmetic mean.

### 3.4 Clipping, RMS, WAV conversion

Each channel is hard-clipped to \([-1,1]\). The reported \`peak_preclip\` is \(\max|y_{\mathrm{unclipped}}|\) across all channels and frames, so values above 1 reveal clipping. The reported RMS is computed **after** clipping:

\[
\mathrm{RMS}=\sqrt{\frac{1}{FC}\sum_{n=0}^{F-1}\sum_{c=0}^{C-1}\widehat y[n,c]^2}.
\]

Here \(F\) is the rendered frame count, \(C\) is 1 or 2 channels, and \(\widehat y=\operatorname{clip}(y,-1,1)\). Writing WAV converts nonnegative values using \(\operatorname{round}(32767y)\) and negative values using \(\operatorname{round}(32768y)\) to signed little-endian PCM16. The exporter uses a 44-byte RIFF header. The renderer's frame-count guard is conservative, but the writer performs a separate final RIFF-size check.

A higher RMS is **not** automatically better; clipping can make sound harsh. WAV output preserves no semantic event metadata.

## 4. Why dyn12 isn't automatically "better audio"

The state is a bounded event-conditioned controller and its output is audible *in principle* because two state channels modify actual synthesis coefficients. But this does **not** show enhanced realism, lower training costs relative to models, perceived expressiveness, or generalization. Both dyn12 and static modes generate sound without training. Comparing them changes the *control rule*, not the presence of training.

Proposed test: fixed CSV, seed, format, ambience and render length; render on vs \`--static\`; inspect WAV hashes, peak/RMS, spectrograms and blind-listening judgments. Include a zero-drive control and material-specific trials. Use the **same hardware** and fixed output quality targets for runtime/cost measurements. Report negative results. See [EXTENDING_THE_ENGINE.md](EXTENDING_THE_ENGINE.md).

## 5. Exact naming and provenance

- **CST:** Cosmic Synapse Theory, the author's research lineage; here it names the design's reference-state integration.
- **dyn12:** twelve bounded computational state scalars; neither twelve acoustic frequencies nor twelve physical dimensions.
- **Foley:** synchronized effects for a film, game or scene; this implementation currently synthesizes footsteps.
- **SFX / diegetic:** broader categories and scene-context terms; this renderer has no scene perception.
- **Source:** [public Python update](https://github.com/NavisWORLD/The-beast-box-/blob/main/beastbox/dyn12.py) and [current C++ mapping](../src/audio.cpp). This document describes these versions only.

To change an equation, update code, matched controls, tests and this document together; preserve historical results rather than rewriting them.

# AGENTS.md — scrim

Read this before touching the screen pass (`source/Shaders.cpp`: `lightAt`, `spillAt`,
`openOver`, `main`), the frame's numbers (`source/Model.cpp`: `Plan`), the output alpha or
clamp, index 0, the two lights' defaults, or any tolerance in `tools/smtest/main.cpp`. Read
genlock's AGENTS.md (mixer section) before touching `ProcessOpenGL`'s inputs. The design is
`~/Projects/resolume/specs/SPEC-scrim.md`; its "As built" section says where the code departs
from it, and is restated at the end of this file. Built 2026-11-17, local only; public at
`stoatworks-labs/scrim` since 2026-11-18.

## The model, as built

Per channel, in linear light (Linear Light on) or on the codes (off):

    L = ρ (1 − c_ap) · Op · FL · Src  +  ρ (1 − c_box) · Wash
      + c_box · Dest · ( Back + φ · Op · FL · Src_spill / M² )

    out.rgb = encode( clamp( L, 0, 1 ) )          (Linear Light off: clamp( L, 0, 1 ))
    out.a   = 1

- Src and Dest are straight alpha: a texel's light is `decode(rgb) · a`. An input not at the
  output's size is resampled by a bilinear of that light done by hand from four `texelFetch`es
  (never the texture's filter: the colour under a transparent texel would bleed, and Apple's
  GPU filters with 8-bit weights).
- `c_box` is the holes' share of the whole output pixel, `c_ap` their share of the projector
  mirror, a centred square of side √Fill. With Draw Mesh off both are φ. The weave is
  separable bars, x pitch Mesh, y pitch Mesh/2; thread fraction a = (3 − √(1 + 8φ))/4, so
  φ_x = 1 − a, φ_y = 1 − 2a and φ_x φ_y = φ. A box average is a product of two 1D averages
  from `openTo(t) = floor(t)·φ_a + min(frac t, φ_a)`.
- The weave's phase at pixel i is frac(i·f), taken as `fract(((i·FreqHi) & 255)/256 + i·FreqLo)`
  with FreqHi = floor(256·frac f) (exact in integers up to kMaxSize) and FreqLo the rest,
  split in double on the CPU. A float i·f at 4K and three cycles a pixel has no fraction left.
- **The spill**: M = 1 + k (k = Spill Distance, s/T), M_app = (1 + k)/(1 + k/v) (v = Audience,
  V/T). Output pixel p looks through to projection point c = centre + (p + 0.5 − centre)/M_app.
  Src_spill is Src integrated over the defocus disc of radius A·k/(2(1 + k)) gauze metres
  (A = 25.6/1.7 mm, the PT-RZ120's pupil), in output pixels radius/W_g·outW, capped at 16 px
  and a point below 1/64 px. Rows of texels at the midpoint of their overlap with the disc,
  each row's chord integrated over its texels exactly, normalised by the total weight. Off the
  projection's frame there is no light.
- The Wash reflects off the threads over the whole pixel (1 − c_box): it is a light on the
  gauze, not the projector's, so the mirror's aperture does not apply. It never reaches the
  set.

## Tolerances, and where they come from

Every level tolerance is three times a stated bound; bit claims are 0 bits. The bounds:

| quantity | bound | from |
| --- | --- | --- |
| a float rounding (`kFloatUlp`) | 2⁻²⁴ | IEEE 754 single, values ≤ 1 |
| the sRGB pair on this chain (`kPowBound`) | 6e-6 absolute, encoded | below |
| the same in linear light (`kLinearBound`) | 2.275 × 6e-6 | decode's slope on 0..1 is at most 2.4/1.055 |
| the spill's gather | 2·n·2⁻²⁴ relative, n texels summed | two n-term float sums (the light and the weight), n = (2⌈R⌉ + 3)² |
| a half-light crossing | Δ/2, Δ the sample spacing | below |
| the blur's disc from the edge's slope | [R, R·A_max/fall] | below |
| the moiré's ratio and mean | Σ over aliased harmonics of 1/(πm)·\|sinc(m f w)\| | below |

**The sRGB pair.** penumbra's grid evaluation of GLSL 4.10 §4.5.1 (pow as exp2(y·log2 x),
exp2 good to 3 + 2|x| ULP, log2 to 3 ULP outside [0.5, 2] and 2⁻²¹ absolute inside; its
AGENTS.md, 2026-11-16): decode is good to **5.1e-6** relative, encode to **6.8e-7**
absolute. Scrim's output is a sum of terms with at most two decoded factors each (Src times
Dest in the spill), so its relative error is at most 2 × 5.1e-6 plus sixteen float roundings,
1.12e-5; encode turns a relative error e in y into y·encode′(y)·e = 0.44·y^(1/2.4)·e ≤ 0.44 e
absolute, 4.9e-6, plus its own 6.8e-7: 5.6e-6. **6e-6** is used.

**A half-light crossing.** A step edge blurred by a disc is antisymmetric about the edge:
P(x₀ + d) − ½ = g(d) = −(P(x₀ − d) − ½), and g is concave on d ≥ 0 (its slope is the disc's
chord, which shrinks away from the centre). Interpolating linearly between the samples at
x₀ − u and x₀ + w (u + w = Δ) puts the crossing at x₀ + (w g(u) − u g(w))/(g(u) + g(w)); for
w ≥ u, concavity gives g(w)/w ≤ g(u)/u, so the error is between 0 and w g(u)/(2 g(u)) ≤ Δ/2.
A point spill (a step) is the limit. Plus the samples' noise over their slope.

**The blur.** For a disc of radius R, the edge profile's slope at the half-light point is
the disc's central chord over its area, 2/(πR), so the slope read between the two samples
that straddle it gives R_est = 2/(π·slope). The row-midpoint rule over-estimates the area of
a disc (midpoints of a concave chord), A ≥ πR², while the central chord is still 2R, so the
centre slope is at most 2/(πR): R_est ≥ R. The samples are 1/M_app projection pixels apart,
so the secant slope is at least the slope one gap from the centre, where the rows that no
longer reach lose at most H ≤ 4(R − √(R² − gap²)) of height: fall = 1 − H/(2R). With A_max
the row rule's area over πR² at its worst on a grid of 1024 row phases,
**R ≤ R_est ≤ R·A_max/fall** (times the noise). At 320×180 this window is wide (to 1.47 R)
because the gap is half the radius; at 1280×720 it is 1.026–1.032 R.

**The moiré.** The weave's bars have Fourier coefficients |c_m| ≤ 1/(πm). Seen through a box
w pixels wide and sampled at integer pixels, harmonic m lands at frac(m f) with weight
|sinc(m f w)|. The bound on what else lands in the beat's bin (or at DC, for the means) is
the sum of 1/(πm)·|sinc(m f w)| over the harmonics m = 2 … 2¹⁸ that alias within one bin, plus
the tail past 2¹⁸ (1/(π² f w 2¹⁸)). The test's f (1.975 and 2.95 cycles a pixel, Gauze
Width scaled with the raster) puts a whole number of beats across and down the frame, so the
beat sits exactly on a DFT bin and the frequency's tolerance is that bin.

### Every harness claim, its number on this Mac's GPU, its tolerance

Both rasters, 1280×720 and 320×180 (where they differ, the 1280 figure first).

| check | claim | measured | tolerance (source) |
| --- | --- | --- | --- |
| `--names` | names ≤ 16 bytes, unique; SW Scrim / SM01; index 0 Linear Light (boolean, on); Opacity standard, 1; Thread a RED/GREEN/BLUE triplet; About last; every mapping inverts; Open Area 0.535 = mid 52–55%; Thread 0.8822 decodes to 0.35/(1 − 0.535) | 0 over; 0.75265 vs 0.75269 | exact; the inversions float's 1e-6; Thread 1e-4 (the picker's four decimals × decode's slope 1.93) |
| `--mixer` | 7 guards return FF_FAIL; GL state back; Dest 200×120 of 256², Src 96×70 of 128², resized: 0 sentinel pixels, each quadrant flat; a float ramp through the hand bilinear comes back | 0 sentinels; worst 0 codes; ramp worst 9.3e-8 / 1.1e-7 | 1 code (float weights); ramp 1.79e-6 (ten counted roundings × 3) |
| `--closed` | flat float inputs, mesh off: L above, 8 settings × 3 channels × every pixel, Linear Light on and off, nothing clipping (highest 0.79 / 0.95) | worst 7.6e-8 (on), 3.6e-8 (off) | on: 3 × (kPowBound + 0.44·2n·2⁻²⁴), largest 2.19e-5; off: 3 × (18 + 2n)·2⁻²⁴, largest 1.22e-5 |
| `--unlit` | nothing behind lit (Dest black, and transparent over white; Back Light 9, Spill Distance 3), unit gain, Linear Light off: **the output is Src** | 0 of 5,529,600 floats differ | 0 bits: products with exact 1s and 0s, sums with exact 0 |
| | the same, Linear Light on | worst 2.4e-7 | kPowTol 1.8e-5 |
| | **a dark backstage stays invisible**: Dest black, Back Light 0 → 9 and Spill 0.5 → 3, φ 0, 0.25, 0.535, 0.9, 1, mesh off and on | 0 floats change | 0 bits |
| | Dest a picture with no light on it (Back Light 0, Opacity 0) under a wash = Dest black | 0 floats differ | 0 bits |
| | converse: the spill and Back Light 1 each make Dest show | every float differs | > W·H |
| `--superpose` | Front 0.8 + Back 0.5 + Wash 0.2 together = each alone, summed in linear light, mesh drawn, spill on | worst 2.9e-7 / 2.8e-7 | 3 × 4 × kLinearBound = 1.64e-4 |
| `--spill` | (a) the block's half-light crossings at M_app × its edges, k 0.5, 1.5, 3 × v 1, 4, 16 | worst 0.21 / 0.67 of tolerance | 0.5 px + noise (the crossing bound) |
| | (b) inside, φ²·Op·FL/(1 + k)² | worst 0.00 of tolerance | 3 × (kLinearBound + plateau·(2n + 10)·2⁻²⁴), largest 4.7e-5 |
| | (c) the disc: 2 m gauze, k 3, v 4 and 16 | 3.656, 3.655 px vs R 3.614 (1280); 0.977, 0.989 vs 0.904 (320) | [1, 1.032] R and [1, 1.026] R; [1, 1.47] R and [1, 1.24] R (the blur bound) |
| `--moire` | the beat's frequency across and down is the strongest DFT bin, at f 1.975 and 2.95 | bins 32, 36, 64, 72 (1280); 8, 9, 16, 18 (320): all exact | the bin |
| | Pixel Fill 1 over 0.92 shrinks the beat by \|sinc f\|/\|sinc(√Fill f)\| | 0.2316 vs 0.2310; 0.2951 vs 0.2940 | [0.2276, 0.2344]; [0.2824, 0.3062] (aliased harmonics) |
| | the threads' share of the mirrors averages 1 − φ; the light through the holes averages φ | 0.534996, 0.534986; 0.535000 | 1.2e-4, 3.7e-4; 4e-6 (aliased harmonics to DC, + 64 ULP) |
| `--alpha` | white under every transparent texel changes nothing, Src and Dest, at the output's size and resized | 0 floats | 0 bits |
| | out.a is 1; white on ~160 opaque Src texels shows | every pixel | exact |
| | half alpha is half the light, Src and Dest | worst 4.6e-8 | 3 × 1.5 × kLinearBound = 6.1e-5 |
| `--state` | two frames; SetTime(0), (1), (499000); a resize mid-run (to 320×180 with Dest 400×240, then 1280×720 with Src 960×540); each against a fresh instance | GPU: 0 floats differ. Software renderer: 0 at sizes it knows; at sizes it has not met (`SMTEST_STATE_WIDEN` 90, 94, 98, 102), up to 307 floats, worst 2.38e-7 | GPU: 0 bits, one context. Software renderer: 5.39e-5, three times two evaluations' bound (below, "The software renderer moves") |
| `--mutation` | `cBox * stage` → `cBox + stage` in the shipped text, through the shader hook, fails `--closed`; the shipped text through the hook renders the default path's floats | 2 assertions fail; 0 differ | exact |
| clips alpha | BattleWeapon_Tank_09 and Ethnik2, each as Src (over grey) and as Dest (under grey), at the defaults: white under every transparent pixel | 0 output bytes change; alpha 255 | 0 |
| clips defaults | Front Light and Back Light = 1 / the median clip's p99 brightest channel at light 1 | 12.2668 vs 12.2665; 8.0270 vs 8.0279 (`--list`'s four decimals) | 1.78e-3, 1.32e-3 (`--list`'s and the source's four decimals, the histogram bin) |

The level bounds are bounds, not fits, and they are loose: `--closed` measures 0.003 of its
tolerance. An arithmetic slip smaller than about 1e-5 in L would pass `--closed`; the
negative controls and the mutants are each far larger than that.

## Would this hold on another rasteriser, at another raster?

- Positions come from `floor(gl_FragCoord.xy)`, inputs from `texelFetch` at integer texels.
  No varying is interpolated and no texture filter is used (except under
  `kFaultHardwareBilinear`, the negative control).
- The bit claims (`--unlit`, the paint in `--alpha`, `--state` on the GPU) rest on IEEE 754
  products with an exact 0 or 1 and sums with an exact 0, and on a single context; never on
  two roundings agreeing across contexts, and no check compares runs across processes. On
  the software renderer they also rest on its drawing both renders with the same code, which
  it does not always do: see "The software renderer moves", next.
- `--unlit`'s unit gain is exact by construction: Open Area 0.75 (1 − φ = 0.25), Front Light 4
  (host 0.5, squared and × 16 exactly), Thread 1, Opacity 1. Front Light 2 would not be: its
  host value √(1/8) is not a float.
- The weave's phase is exact in integers to kMaxSize (16384 × 255 < 2³¹); its remainder's
  product is under 64 cycles, good to 2⁻¹⁸ there.
- No `sin`, `atan` or `asin` in any shader (Apple's software renderer gets two of them
  wrong: nipkow).
- Every check passes at 1280×720 and 320×180 on this Mac's GPU (GL "4.1 Metal - 90.5", as oxbow reports it)
  and on Apple's software renderer (`SMTEST_RENDERER=software`). On the software renderer the ramp check in `--mixer` cannot see
  `kFaultHardwareBilinear`, because that renderer filters with float weights; the control is
  still caught there by the padding it reads. llvmpipe, other GPUs, Windows: not run.

## The software renderer moves

Found when `--state` failed once on the software renderer in an independent full verify on
2026-11-17 (237 floats differed between the running instance after its resize to 320×180 and
a fresh one) while bubble's verify, carousel's build and snowglobe's start were running; it
passed five times alone afterwards. Measured since, on this Mac (M4 Max, macOS 26.4.1):

| what was rendered | software renderer | GPU |
| --- | --- | --- |
| one fresh instance, sizes it has not met, nothing changed between frames: frame 1 against frame 3 | moved in 11 of 12, 12 of 12 and 8 of 8 instances; up to 655 floats, worst 7 ULP (2.38e-7) | 0 of 12, 0 of 12, 0 of 8 |
| the same, frame 1 against frame 2, back to back | moved | — |
| the same, frame 3 against frame 4, each after 300 ms | 0 of 64 moved | 0 of 32 |
| the same sizes again, in a new process | 0 of 12, 0 of 12, 0 of 8 moved | 0 of 12 |
| a known size, Back Light and Front Light set to values never used, frames 1–3 | 0 of 10 moved | — |
| two fresh instances back to back at `--state`'s 320×180 step | 47 floats differ, 2 ULP (once in many: when the sizes were new to it) | — |
| `--state` as it was, every width widened by 6, 10, 14, 18 (`SMTEST_STATE_WIDEN`) | failed 4 of 4 (28–43 floats at the 1280×720 step) | — |
| sizes it knows, under load: 20 busy loops at load average 76, 30 runs of the 320×180 step; then 8 such probes at once, 200 runs, load 24–36 | 0 moved | — |

So it is not load: the renderer gives different floats for the same draw for a few hundred
milliseconds after it first meets the draw's sizes and formats, and what it learns outlives the
process. Load and other processes only decide when a size it once knew is new to it again
(another session's software-renderer runs evicting it is the likely reading of the original
failure; not proven). The plugin keeps no texture, framebuffer or size between frames
(`source/Scrim.cpp`; `firstWidth` is read only under `kFaultStaleSize`), every fetch is clamped
into the used area, and two fresh instances differ by the same few ULP. It is not plugin state.
The differences line up in columns (x = 27, 141, 159 …), as an x-only term of the weave or the
bilinear would if two code versions round it differently. `smtest --settle-probe` reproduces the
first table row (`SMTEST_SETTLE_BASE` picks the widths).

**The check, as fixed.** On the GPU every `--state` comparison stays 0 bits. On the software
renderer each is two evaluations of the same shader on the same inputs by two code versions,
each within one evaluation's bound B of the exact value, so the two are within 2B:

- B = kPowBound + 0.44 · (2n + 64) · 2⁻²⁴: `--closed`'s bound (the sRGB pair, which already
  counts the light sum's sixteen roundings, and the gather's two n-term sums, n = 25 here) plus
  an allowance of 64 roundings for what `--closed` leaves out. Counted: an `openOver` ratio is
  about 14.5 roundings (t₀ and t₁ three each, the two `openTo`s two each, the difference and the
  span one each, the division 2.5), amplified by its cancellation about (1 + f)/f ≈ 1.4 at
  `--state`'s sizes (f ≈ 2.7–12 weave cycles a pixel across): about 22; cBox and cAp are each a product
  of two, about 45; the bilinear's three `mix`es, 9. The encode turns a relative error e into at
  most 0.44 e absolute, as in kPowBound's derivation.
- **Assumed, with evidence:** both versions put every texel and the spill's centre in the same
  place. GLSL lets a division be 2.5 ULP off, and a texel position one ULP off would show at the
  cards' sharp edges as a difference of order 1e-4; the worst measured is 2.4e-7. Not proven
  covered: a `mix` computed as x + a(y − x) is good only to roundings of the brighter texel, so a
  dark output beside a bright texel could exceed its share of the allowance.
- The tolerance is 3 × 2B = **5.39e-5**. At new sizes the check measures up to 307 floats, worst
  2.38e-7 (four runs), 1/226 of it. The stale-width fault lands at 0.0213 and 0.0443, the clock
  fault at 0.0375 and 0.764: 395× the tolerance at the least. A state bug smaller than 5.4e-5
  would pass on the software renderer; the GPU run, at 0 bits, would catch it.

**Tried first, and wrong: a witness.** One more instance per step rendered before and after
each attempt, and an attempt discarded if any witness moved. Twelve runs at new sizes passed,
each discarding one or two attempts; the thirteenth had still witnesses and failed (306 and 17
floats).
The likely reading (not proven): a new instance's program goes through its own unsettled
frames whatever an older instance at the same sizes does, so a still witness says nothing
about the instances under test.

**Exposed in principle, not changed:** the other 0-bit comparisons of two renders on the
software renderer: `--unlit`'s dark backstage and wash, `--alpha`'s white under transparent
texels, `--mutation`'s shipped text through the hook. `--unlit` and `--alpha` passed at two
rasters the renderer had not met (338×190 and 346×194); `--mutation` (fixed at 320×180) was not
run at a new size. If one of them fails on the software renderer alone by a few ULP, it is this,
and `compareRenders` in `tools/smtest/main.cpp` is the fix.

## The negative controls and the mutants

`SetFaultForTest` builds the wrong answer into the real plugin. 17 faults, each caught at both
rasters (assertions failed at 1280×720):

| fault | check | the wrong answer | failed |
| --- | --- | --- | --- |
| `kFaultOpacity` | closed | the gauze as an opacity: ρ ignored | 2 |
| `kFaultSwapArea` | closed | the holes reflect, the threads pass | 2 |
| `kFaultGammaSum` | closed, superpose | reflected and passed light summed encoded | 1, 2 |
| `kFaultNoSpread` | closed, spill | the spill not spread over M² | 2, 4 |
| `kFaultBackAlways` | closed, unlit | a floor of back light (1/64): a dark backstage glows | 2, 1 |
| `kFaultWashSpills` | unlit | the wash passes the holes and lights the set | 1 |
| `kFaultViewerScale` | spill | the spill at the set's magnification M, not M_app | 3 |
| `kFaultBlurOnSet` | spill | the disc at its size on the set, A s/T | 2 |
| `kFaultBoxAperture` | moire | the reflected light over the whole pixel | 2 |
| `kFaultSquareCell` | moire | the cell as tall as it is wide | 2 |
| `kFaultStraight` | alpha | colour not multiplied by alpha | 3 |
| `kFaultHardwareBilinear` | alpha, mixer | a resized input through the texture's filter | 1, 5 |
| `kFaultAlphaFromInput` | alpha | out.a from Dest | 2 |
| `kFaultLeaveBound` | mixer | a texture left bound on unit 1 | 1 |
| `kFaultPadded` | mixer | an input read over its hardware size | 3 |
| `kFaultStaleSize` | state | the first frame's width kept across a resize | 2 |
| `kFaultClock` | state | the weave breathing with the host's clock | 2 |

`tools/mutate.sh`: **39** one-character mutants (25 of the GLSL, 7 of Model.cpp, 3 of
Controls.cpp, 4 of Scrim.cpp, one of them a misspelt uniform name), each built arm64 in a
temporary tree with the touched objects and the binary deleted first, the GLSL ones (and the
uniform name) found in the built binary before running, each under a 300 s perl alarm with a
timeout counted as caught. All 39 caught, in about a minute. No mutant contains `|` (the
field separator) and none raises a loop bound.

## Defaults

Front Light **12.2665** and Back Light **8.0279** (`Controls.h`), set by `tools/clips.py
defaults` on 2026-11-17 from Arena's 33 demo clips (`media/AV`, `media/Shop74`) at 640×360,
30 frames from 1 s: each clip's p99 brightest output channel, pooled over every pixel of the
30 frames, at light 1, with Dest black (Front Light) or Src black (Back Light). The median clip
is Shop74/Cyberspace_09 both times: 0.08152311 and 0.12456560 linear, so the defaults are
their reciprocals. At light 1 nothing clips (the largest clip reads 0.354 and 0.538), so the
p99 scales exactly with the light. The script fails if the binary's default is not the
measurement, if the median clip does not reach 1.0 at the default, or if it already does at
0.999 of it.

The ratio of the two medians, 1.528, is close to the ratio of the mean factors,
φ/(ρ(1 − φ)) = 0.535/0.350 = 1.529 (the weave makes the p99s not exactly proportional). All
12 Bass and Synth clips in `media/AV` read under 0.011 at Front Light 1 (mostly black frames
one second in; Bass 001 reads 0), which pulls the median down: see the open questions.

## The sweep

`tools/sweep.py`: every parameter at both ends at 320×180, with Front Light and Back Light
brought to 1 (BASE), so the picture does not clip: 15 swept, 0 dead, 0 barely alive.
Contexts: Pixel Fill and Mesh with Draw Mesh on; Audience with Spill Distance 1. `--bare`
(no base, no contexts: the plugin's defaults) also finds nothing dead, because each context
restates a default. Checked by hand (identical PNGs): with Draw Mesh off, Mesh and Pixel Fill
are dead; with Spill Distance 0, Audience is dead; with Front Light 0, Spill Distance, Pixel
Fill and Audience are dead. Those are the dependencies.

## Render cost

`smtest --bench`, this Mac (Apple M4 Max, GL "4.1 Metal - 90.5"), Dest and Src at the output
size, each frame timed alone after a 60-frame warm-up; medians of 60 frames, ms/frame. `gpu`
is GL_TIME_ELAPSED around the frame, `wall` is Render to glFinish. From verify's run
(2026-11-17, load average 21.8):

| resolution | defaults gpu | defaults wall | biggest disc gpu | biggest disc wall |
| --- | --- | --- | --- | --- |
| 1280×720 | 0.496 | 0.703 | 0.564 | 0.783 |
| 1920×1080 | 0.197 | 0.384 | 2.308 | 2.507 |

The light frames are noisy on a shared GPU: over eight more runs, 720p at the defaults read
0.071–0.089 ms on the GPU's clock in six and 0.50 in two (each the first figure of its run),
and 1080p at the defaults 0.196–0.362. A half-second warm-up did not remove the 0.50s, so it is
not the idle clock; other sessions' GL harnesses were running at the time, which is the likely
cause (not proven). The biggest disc (a 2 m gauze, Spill 3, Audience 16: a 5.4 px radius at 1920
wide, well under the 16 px cap) is steady at 0.55 ms (720p) and 2.29–2.54 ms (1080p). The defaults' disc is under a
pixel. Never measured inside Resolume; 4K not run.

## Traps hit building this

- **`fmt()` takes doubles only** (penumbra's trap): a `%s` slipped into the `--state` message
  and was caught reading it back, before it ran.
- **The "measured" counter** was read before the `Check` that increments it, so it printed one
  fewer than it counted.
- **`--closed`'s grid clipped with Linear Light off** (1.05 at φ 0 with a wash; 1.06 at φ 1):
  the codes are not decoded, so the same settings are brighter. The grid now stays under 0.95
  in both modes, and the check asserts it.
- **`grep -c FAIL` counts "FF_FAIL"**: the guards' own lines name it. verify matches `^  FAIL`.
- **The output clamp hides levels.** At the measured defaults (FL 12.3, BL 8.0) the cards clip
  over much of the frame, so every check that claims a level sets its own lights; only the
  alpha checks run at the defaults, and their negative controls show they still see colour.
- **SetTime**: the SDK defaults to asking for the host's clock. The plugin now declares
  `SetTimeSupported(false)`; verify checks oxbow reads `set-time: no`.
- **The hand bilinear's interior was unmeasured** until the ramp check: the quadrant check
  skips two texels around each edge, where all of the bilinear's arithmetic lives, so a
  mutant swapping its row fraction for its column fraction passed. The ramp catches it.
- **Apple's GPU filters with 8-bit weights**; its software renderer with float weights.
- `--list` prints four decimals of the host's 0..1, which through 16 p² moves Front Light's
  default by up to 1.4e-3: clips.py's tolerance carries it.
- **The software renderer moves under a new draw** (above): `--state` passed on every run here,
  then failed once on a busy machine. The witness fix passed twelve runs at new sizes and failed
  the thirteenth.
- **Frames queued back to back time nothing.** The bench first ran 60 frames with glFinish
  only at the ends: 0.04 ms/frame at 720p, 0.09 at 1080p, and 1080p queued came out faster
  than 720p. Timed alone, the biggest disc at 1080p takes 2.3 ms on GL_TIME_ELAPSED and the
  queued run 0.03 ms/frame. Probably the tile renderer's hidden-surface removal shading only
  the last of a run of opaque full-screen draws into one framebuffer (not proven). Each
  frame is now timed alone, and the bench fails if a frame did not render or the output is
  blank.
- **A boolean that changes no byte at 64×36.** The pipe's cue test stepped Draw Mesh; at
  64×36 a 9.5 m gauze puts about 59 weave cells in a pixel, so the weave per pixel and its
  average are the same bytes and the step never showed. It steps Linear Light.

## Open questions for Allan

1. **The defaults are bright.** By the lithophane rule the median clip fills the range at each
   light alone; but the median clip is dark (Cyberspace_09), and the 12 Bass and Synth clips
   are nearly black at 1 s, so a typical bright clip clips hard at Front Light 12.3, and with both lights at their
   defaults the picture clips over most of the frame. A rule over clips with content, or the
   default as the light where the brightest clip fills the range (about 2.8 and 1.9), would be
   gentler. Should Back Light default lower still, so a gauze reads opaque on first load?
2. **The output is clamped to [0, 1]** before encoding. Arena composites in 8 bits, so nothing
   above 1 would survive anyway; a soft knee would keep highlights' colour.
3. **Straight or premultiplied inputs?** Both are read as straight (the fleet's reading of
   Arena's DXV clips through ffmpeg). Whether Arena hands a mixer straight or premultiplied
   textures is unmeasured; the fleet's mixers are split.
4. **Does Arena bypass a mixer at layer Opacity 0?** If it does, the reveal's last step (the
   projector off, the scene through) never reaches the plugin.
5. **ShowTex's luminance factor's geometry.** ρ = 0.35/(1 − φ) assumes the chart was measured
   over a dark ground at the sheet's angles; the sheet does not say.

## Not verified

- Never loaded into Resolume (either platform); index 0 hidden and Opacity on the fader are
  measured on other mixers, not this one. Never built on Windows.
- No llvmpipe, no other GPU.
- The physics is checked against the model, not against a gauze: no photograph or
  measurement of a real scrim was compared.
- The spill's blur at the default 9.5 m gauze is under a pixel (0.51 px radius at 1920 wide,
  k 0.5); the blur check runs at 2 m to have something to measure.

## As built (departures from SPEC-scrim.md)

- `--superpose` is superposition in the **lights** (Front, Back, Wash each alone, summed),
  not in the inputs: the spill multiplies Src by Dest, so out(Src, Dest) is not a sum of
  out(Src, black) and out(black, Dest).
- `--unlit`'s exact case is φ 0.75 and Front Light 4, not φ 0.5 and Front Light 2 (not exact
  from 16 p²).
- `--closed` includes the spill term, with a flat Src and Audience ≥ 2 throws so the spill
  reads well inside the frame.
- The output is clamped to [0, 1]. The Wash reflects with c_box, not c_ap.
- The blur is measured from the edge's slope at its half-light point, not its second moment.
- `--mixer` also measures the hand bilinear on a float ramp; `--state` also has the
  stale-size and clock faults; the plugin declares no clock.
- `--state` on the software renderer compares within 5.39e-5 (two evaluations' bound, ×3), not
  0 bits: the renderer's floats for a draw it has not met move by up to 7 ULP.
- `--bench` runs 720p and 1080p only (no 4K loops on a shared Mac).
- The sRGB budget is 6e-6, not penumbra's 4e-6: two decoded factors per term here, one there.
- Defaults as measured: Front Light 12.2665, Back Light 8.0279.
- Scrim has no option parameter, so `--set`'s option-name path is unreachable from verify;
  booleans take on/off/true/false.

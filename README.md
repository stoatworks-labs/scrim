# scrim

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been loaded into Resolume
> Arena on a Mac and on Windows and drew what the harness draws, on test stills (see
> [Status](#status)). Everything else below is measured by an offline harness, `smtest`, that drives the real plugin class in a headless
> GL context with two inputs, at 1280×720 and 320×180, on this Mac's GPU and again on Apple's
> software renderer, and reads the light off the plugin's float output. With nothing behind
> the gauze lit, the projection comes back with **0** of 5,529,600 floats different; a dark
> backstage changes **0** floats at any open area, whatever Back Light says. The spill's
> edges sit where similar triangles put them to within 0.11 px at 1280×720 (0.34 px at
> 320×180, against a half-pixel bound); its defocus disc reads 3.656 px where the lens's
> circle of confusion gives 3.614. **17** deliberately wrong versions
> of the plugin and **39** one-character mutants of its GLSL and C++ are all caught. That the
> threads pass no light, that the set is one flat plane, and that ShowTex's luminance factor
> was measured over a dark ground are **assumptions**: see [What it leaves out](#what-it-leaves-out).

> **Photosensitivity note.** Scrim makes no flash of its own, but at its defaults its lights
> brighten both pictures about four times, so a clip's own pulses can come out as large
> changes of brightness. Over 33 pairs of Resolume's demo clips (8 s each, xenon's rule: more
> than six transitions in any second, over the whole frame or any third of it), two pairs went
> over three flashes a second through it at the defaults (8 in a third of the frame; 6 and 4
> on their own). Stepping Front Light and Back Light about four times a second put 6 of the 33
> pairs over, and about eight times a second 27. With Draw Mesh on, the weave's moiré is a
> regular pattern of fine stripes, past the broadcast guidance for such patterns when it fills
> much of the screen on a plain bright projection; Draw Mesh off removes it. Flashing light and
> striped patterns can trigger seizures in people with photosensitive epilepsy: the
> [user guide](https://stoatworks-labs.com/software/scrim/guide/#flashing) has the table.

A theatre gauze, for [Resolume](https://resolume.com) Arena and Avenue, as an FFGL
**mixer**, **SW Scrim**. Set it as the blend mode of the layer that holds what the projector
sends, over the layer that holds the scene behind the gauze. With the front lit and the back
dark the gauze is a screen; light the back and darken the front and it all but disappears; in
between you see both. The projection's own light carries on through the holes and lands on
the set behind, bigger, dimmer and out of focus, and lights it.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/scrim/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`scrim-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/scrim/releases/download/v0.1.0/scrim-0.1.0-macos-universal.dmg) | 208 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`scrim-macos-universal.zip`](https://github.com/stoatworks-labs/scrim/releases/latest/download/scrim-macos-universal.zip) | 171 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`scrim-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/scrim/releases/download/v0.1.0/scrim-0.1.0-windows-x86_64-setup.exe) | 242 KB |
| x64 · .zip archive | [`scrim-windows-x86_64.zip`](https://github.com/stoatworks-labs/scrim/releases/latest/download/scrim-windows-x86_64.zip) | 115 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/scrim/releases](https://github.com/stoatworks-labs/scrim/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## The one idea

**What the audience sees at a point of the gauze is the light its threads reflect plus the
light that comes through its holes.**

A woven gauze is a fraction φ of holes (its open area) and 1 − φ of threads of reflectance ρ.
Too far away to resolve the weave, the audience sees

    L = ρ (1 − φ) · light on the gauze  +  φ · light from behind

and nothing in that is an opacity. The light on the gauze is the projection (Front Light ×
the layer's opacity × Src) plus a Wash; the light from behind is the scene (Dest) as lit by
Back Light plus the projection's light that came through the holes. With no light behind,
the scene contributes nothing, whatever the layer shows: the dark backstage.

- **The spill.** The light through the holes lands on a set k throws behind the gauze, spread
  over (1 + k)² the area, and the audience, v throws out, sees it magnified by
  (1 + k)/(1 + k/v). It is out of focus by the lens's circle of confusion: a Panasonic
  PT-RZ120's 15 mm pupil, which on a 9.5 m gauze is half a pixel of blur at 1920 wide. The
  spill is mostly dimmer, not softer.
- **The moiré.** The output is taken as the projector's pixel grid, each pixel's light falling
  on a mirror 92% of its area (TI's DLP7000). The weave (ShowTex GobelinTulle's 2.5 × 1.25 mm
  cell) beats against those mirrors. `Draw Mesh` off averages it away.

## Controls

| control | range | default | what it is |
| --- | --- | --- | --- |
| Linear Light | on/off | on | light summed in linear light. Arena hides a mixer's first parameter, so it stays on |
| Opacity | 0–1 | 1 | the projector's level: Arena's layer fader |
| Front Light | 0–16 | 12.27 | the projector at full, in units of what a white diffuser shows |
| Pixel Fill | 0.5–1 | 0.92 | the projector's mirror fill factor (the moiré) |
| Back Light | 0–16 | 8.03 | the light on the scene behind |
| Wash | 0–16 | 0 | a steep light on the gauze: lights the threads, never the set |
| Spill Distance | 0–3 throws | 0.5 | how far behind the gauze the set stands |
| Audience | 0.25–16 throws | 4 | how far in front the audience sits |
| Open Area | 0–1 | 0.535 | the holes' share (ShowTex: 52–55%) |
| Thread | colour | white, ρ 0.753 | the threads' reflectance (ShowTex white's 0.35 / 0.465) |
| Mesh | 0.5–10 mm | 2.5 mm | the cell's width; its height is half |
| Draw Mesh | on/off | on | the weave per pixel, or its average |
| Gauze Width | 2–20 m | 9.5 m | the gauze image's width: the mesh's and the lens's size in pixels |

Front Light and Back Light default to the light at which the median of Arena's 33 demo clips
fills the range, each alone (`tools/clips.py defaults`). Both at once clip.

## Build

From a checkout with its submodule (`git clone --recursive
https://github.com/stoatworks-labs/scrim`, or `git submodule update --init --recursive` in an
existing clone):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/smtest --out /tmp/scrim.png         # the defaults, offline
```

`cmake --install build` copies `Scrim.bundle` into Arena's Extra Effects. `./build/smtest --bench` times it: on an M4 Max, about 0.2 ms a frame at 1080p at
the defaults on the GPU's clock, 2.3 ms with the biggest defocus disc the controls allow
(noisy on a shared GPU; see AGENTS.md). Full verification: `TMPDIR=<scratch> OXBOW=<a built oxbow> tools/verify.sh`. See
[AGENTS.md](AGENTS.md) for every check, its tolerance and where the tolerance comes from.

## What it leaves out

The threads transmit no light (ShowTex's white passes some diffusely); the luminance factor is
taken at 0°; the set behind is one flat plane at one distance; no light comes back from the set
onto the gauze and the threads cast no shadow on it; the audience sits on the axis; the lens's
blur of the pixel grid, two-scrim moiré and hexagonal bobbinet are not modelled. The output is
clamped to [0, 1] and is always opaque.

## Browser demo

**<https://scrim-demo.stoatworks-labs.com/>** is SW Scrim in a web page: every control with
its own name, group and default, and two of the kit's generated clips (or a black layer) as
the projection and the scene behind, or a picture of yours (decoded in the page, never
uploaded) as the scene. The shader is the plugin's own GLSL, spliced unedited
(`demo/tools/check_shaders.py`, run by `tools/verify.sh`). What feeds it (the conversions,
`Plan`, every uniform) is a JavaScript port, and `demo/tools/crosscheck/` pipes the page's
own two inputs through `smtest --pipe` frame for frame: in 19 cases, 149 frames, every pixel
is the plugin's. It is a demo, not the plugin, and says on the page what it leaves out.

## Status

**v0.1.0, 18 November 2026, and early days.** It has been loaded into Resolume Arena
on a Mac (7.29.0) and on Windows (7.27.1) and drew what the harness draws, but only over test stills: never with real
clips through a show, never timed inside Resolume. The macOS build is Developer ID-signed and
notarised. `oxbow probe` reads the bundle as a host does (`SW Scrim` / `SM01` / **mixer**,
two inputs, parameter 0 Linear Light, a standard parameter named Opacity), and `oxbow
selftest`, an effect host that hands the plugin one input, is refused on its first frame, as a
mixer must refuse it. There's a [user guide](docs/USER-GUIDE.md) and a
[browser demo](#browser-demo), but no OpenFX port and no presets.

### In Resolume

The fleet's Arena gate cannot gate a mixer, so SW Scrim was mounted by hand over Arena's REST
API and read through its own log and Arena's composition snapshot, on 18 November 2026.

![A rainbow of rings, very bright, washed to white over most of the lower half, with a faint
grid showing through, as Resolume Arena's own composition snapshot shows it](docs/arena.jpg)

<sub>SW Scrim on its defaults inside Resolume Arena 7.29.0 on a Mac, from Arena's composition
snapshot over REST: the harness's grid card as a still on one layer (the scene), its ring card
on the layer above, whose Blend Mode is SW Scrim. Both lights at their defaults clip most of
the frame. It matches `smtest --pipe` on the same two stills to a mean of 1.07 levels.</sub>

- **macOS: Arena 7.29.0, Apple M4 Max**, a local universal build of this source. Offered in
  a layer's Blend Mode list; 18 of its 19 parameters shown, Linear Light the hidden one; the
  layer's opacity fader logged as the plugin's Opacity (0.2, 0.85, 0.5, 1.0), and a REST write
  to the mixer's own Opacity overridden; inputs padded to 1920x1088. Two snapshots a moment
  apart are identical.
- **Windows: Arena 7.27.1 on a test PC with no GPU** (Mesa's llvmpipe), the DLL that
  release.yml built from this source. Offered in the Blend Mode list; 19 of its 20 parameters
  shown, Linear Light the hidden one (the About block's User guide button makes 20); the
  fader logged as Opacity, a REST write to the mixer's Opacity overridden; inputs padded to
  1280x768. At the defaults on the same two stills it matches `smtest --pipe` to a mean of 1.4
  levels in Arena's JPEG snapshot. The two findings below hold there too, level for level:
  231 and 83 for the half-alpha still, and the bare scene (124) at a fader of 0, where Arena
  7.27.1 never handed Scrim an Opacity of 0 (its log's last value was 0.01).
- **Arena hands a mixer premultiplied pictures; Scrim reads straight.** A still of white at
  alpha 1 and 0.5, over black, with the lights set so nothing clips: the harness predicts 231
  and 170 for straight input, 83 for premultiplied, and Arena showed **231 and 83**. So a
  part-transparent pixel, on either layer, gives its light times its alpha twice: white at
  half alpha gives about a fifth of the light it should. Opaque clips are exact and fully
  transparent pixels give nothing either way. Use opaque clips, or flatten one with alpha onto
  black first.
- **At a layer opacity of exactly 0, Arena shows the layers below as they are.** The plugin's
  log shows Arena still calling it every frame at Opacity 0, but the composition showed the
  bare scene (124 on a grey still where the plugin's picture was 170, its Back Light on the
  scene). At 0.01, 0.1, 0.5 and 1 the snapshot was the plugin's own picture, level for level.
  So the reveal's last step, the projector off and the scene lit through the gauze, is at 1%,
  not 0: at 0 the gauze, the Back Light and the Wash vanish at once.

The defaults are bright: each light alone fills the range on the middle one of Resolume's
demo clips, which is a dark clip, so a bright clip clips and both together clip most of the
frame (the snapshot above). The output is clamped to white before encoding. These, the
assumptions in [What it leaves out](#what-it-leaves-out) and the two Arena findings above are
in the user guide's known limits. MIT; see [LICENSE](LICENSE) and
[ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->

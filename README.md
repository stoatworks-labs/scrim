# scrim

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has **never been loaded into
> Resolume**, on macOS or Windows, and has never been built on Windows. Everything below is
> measured by an offline harness, `smtest`, that drives the real plugin class in a headless
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

A theatre gauze, for [Resolume](https://resolume.com) Arena and Avenue, as an FFGL
**mixer**, **SW Scrim**. Set it as the blend mode of the layer that holds what the projector
sends, over the layer that holds the scene behind the gauze. With the front lit and the back
dark the gauze is a screen; light the back and darken the front and it all but disappears; in
between you see both. The projection's own light carries on through the holes and lands on
the set behind, bigger, dimmer and out of focus, and lights it.

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

`cmake --install build` copies `Scrim.bundle` into Arena's Extra Effects. Not done on this
machine. `./build/smtest --bench` times it: on an M4 Max, about 0.2 ms a frame at 1080p at
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

Public at `stoatworks-labs/scrim` since 2026-11-18, released at v0.1.0; never loaded into
Resolume yet. MIT; see
[LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

# Attributions

Scrim is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

It is a provisional hand copy: the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py` once a project is registered there.
Scrim is not registered yet, so this file was written by hand on 2026-11-17, adapted from
penumbra's, and will be replaced by the generated one.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The mixer's shape, the harness, verify, the mutants and the sweep — Stoatworks penumbra

Not yet published.  
Licence: MIT  
Copyright: Stoatworks Labs

The skeleton is penumbra's: CMakeLists.txt, the guards on the input count, pointers and sizes, index 0 as a sacrificial parameter, `Opacity` named so Arena binds the layer fader, the per-instance Opacity log, the two-input harness rig with its magenta padding, `--pipe`/`--pipe-src` (SIGPIPE ignored, a closed stdout exits 1), the cue sheets, and tools/verify.sh, tools/mutate.sh, tools/sweep.py and tools/glslc.sh, each adapted. Penumbra's own sources for these are pepper (the mixer, the pipe, Diag), genlock (what a mixer is in Arena), haze (the tooling and the About block) and vectrix (GLState.h, from resolume-scopes), and those debts carry over.

### Defaults from Resolume's demo clips, and the percentile — Stoatworks lithophane

Not yet published.  
Licence: MIT  
Copyright: Stoatworks Labs

tools/clips.py is lithophane's, adapted for two inputs: the alpha check on the two DXV clips with real transparency (white painted under transparent pixels must change nothing), and the rule that a light's default makes the median demo clip's 99th-percentile brightest channel reach the top code. `smtest --stats` and its 2^20-bin histogram are lithophane's `lptest --stats`.

### What a mixer is in Arena — Stoatworks genlock

<https://github.com/stoatworks-labs/genlock>  
Licence: MIT  
Copyright: Stoatworks Labs

genlock's AGENTS.md is the measured account of how Resolume Arena drives an FFGL mixer (which input is Dest, the padding, the hidden first parameter), and the magenta-padding sentinel in `smtest --mixer` is genlock's test, by way of penumbra.

### The About block — Stoatworks haze

Not yet published.  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout.h, StoatworksAboutLinks.h and StoatworksAboutParams.h are provisional hand copies, with `guide` empty: no user guide exists.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is penumbra's, which is haze's, radar's and vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned to b1afaf9.

The plugin ABI itself. An FFGL mixer is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately. Resolves OpenGL entry points on Windows.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample. Part of the upstream SDK tree rather than something this plugin calls.

## Work we checked ourselves against

No code was taken from these. They are where the numbers and the behaviour come from, each read on 2026-11-17; SPEC-scrim.md quotes them.

### ShowTex GobelinTulle, technical data sheet 3420 (10/2026)

<https://files.showtex.com/Soft_goods/3420-GobelinTulle-tds-ShowTex-en.pdf>

The default gauze: the open area (52–55%, so Open Area 0.535), the mesh (2.5 mm wide, half as tall: Mesh 2.5 mm and the cell's shape), the width sold (950 cm: Gauze Width 9.5 m), and the reflection luminance factor of white at 0°, 0.35, read off the sheet's chart, from which the Thread default's reflectance 0.753 is derived (assuming the chart was measured over a dark ground, which the sheet does not say). ShowTex's article "Making magic with scrim projections" (2025-07-11) and the product page give the reveal's rule and the projection's light passing through.

### Peter Monahan, "Softly Speaking", Live Design (2004)

Rose Brand's reprint, <https://www.rosebrand.com/downloads/Lighting%20a%20scrim.pdf>, and part 2 (filled scrim)

The lighting practice the controls follow: a dark space behind for the gauze to read as solid, a steep front light whose spill falls short of the set (the Wash control lights the threads and never the scene), and projection light carrying on through the gauze onto whatever is behind (the spill).

### Rose Brand sharkstooth spec sheets

<https://www.rosebrand.com/downloads/>

Thread counts and weights for 8/2 and 6/3 sharkstooth, and the light grey projection sheet's front and rear gains, as context for the data table in the spec. No number from them is a default.

### Panasonic PT-RZ120 specifications (UK)

The lens behind the spill's blur: the standard zoom's wide end, f 25.6 mm at F1.7, a 15.06 mm entrance pupil. Hard-coded as `kPupilMm`; not a control.

### Texas Instruments DLP7000 data sheet (DLPS026G, revised 2023-04)

The mirror array's fill factor, 92%: Pixel Fill's default, and the dark gaps between projector pixels that make the weave's moiré visible.

### Wikipedia: "Circle of confusion", "Moiré pattern", "Scrim (material)"

The circle of confusion referred to the focused plane, C = A |S2 − S1| / S2, by similar triangles (the spill's blur); the beat between two gratings (the moiré's period, cross-checked against aliasing, which the plugin's model is); and the reveal's rule as theatre practice states it (citing Theatrecrafts.com, also read).

## Standards and published specifications

What the implementation is measured against.

- **OpenGL Shading Language 4.10** (revision 6, 24 July 2010) — §4.5.1: the precision of `+ − ×` (correctly rounded), division (2.5 ULP), `exp2`, `log2` and so `pow`, from which the sRGB pair's budget, and every tolerance built on it, is derived; §8.3: `mix` with a boolean selector picks without arithmetic.
- **OpenGL 4.1 core profile** (25 July 2010) — `texelFetch` at integer texels, no host filtering.
- **IEC 61966-2-1:1999 (sRGB)**, amended 2003 — the decode and encode in source/Shaders.cpp and source/Controls.cpp (constants checked against Wikipedia's sRGB article, not against the paid standard itself), and the harness's own separate copy.
- **IEEE 754** — a product with an exact zero and a sum with an exact zero, on which the bit-exact claims rest.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.

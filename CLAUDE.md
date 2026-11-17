# scrim

A theatre gauze, for Resolume Arena/Avenue, as one FFGL **mixer**: a projection on the gauze
(Src), a scene behind it (Dest), and the light that decides which one the audience sees. What
the audience sees is the light the threads reflect plus the light through the holes; the
projection's light through the holes lands on the set, magnified, dimmer and out of focus;
the weave beats against the projector's pixels. C++/GLSL, CMake MODULE → universal `.bundle`
(macOS). MIT.

| id | display name | bundle | type | inputs |
| --- | --- | --- | --- | --- |
| `SM01` | `SW Scrim` | `Scrim.bundle`, `com.stoatworks.ffgl.scrim` | mixer | Dest = the layer below (the scene behind), Src = this layer (the projection) |

Read `AGENTS.md` before touching the screen pass (`source/Shaders.cpp`), the frame's numbers
(`source/Model.cpp`), the output alpha or clamp, index 0, the lights' defaults, or any
tolerance in `tools/smtest/main.cpp`. Read genlock's AGENTS.md (mixer section) before
touching `ProcessOpenGL`'s inputs. The design is `~/Projects/resolume/specs/SPEC-scrim.md`;
its "As built" section says where the code departs from it.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64` (iteration builds here are arm64)
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects). Not run on this
  machine: the plugin has never been loaded into Resolume.
- Render offline: `./build/smtest --out /tmp/scrim.png` (the `stage` card as Dest, the
  `projection` card as Src, on the defaults, which clip on these cards)
- Inputs: `--input-a NAME` (Dest) and `--input-b NAME` (Src), each one of `stage stage-alpha
  projection projection-alpha quads black white clear grey`; `--src-size WxH`; `--size WxH`
- List parameters: `./build/smtest --list`
- Set anything by name: `./build/smtest --set "Front Light=0.25" --set "Draw Mesh=off"` (host
  0..1 values; a boolean also takes on/off/true/false; anything unparsable, or a name that
  doesn't exist, exits 2)
- A clip through it: `ffmpeg -nostdin -i scene.mov -f rawvideo -pix_fmt rgba - | ./build/smtest
  --pipe --size WxH --pipe-src projection.rgba | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -i -
  out.mp4`. stdin is Dest, `--pipe-src` (a file or FIFO) is Src; without it, `--input-b` is
  held. The run ends with the shorter input; a partial last frame is dropped; a reader that
  hangs up ends it with exit 1 (SIGPIPE ignored). `--stats` prints each frame's p99 brightest
  channel on stderr, and pooled at the end.
- Cue sheets for `--pipe`: `--script cues`, `frame Name value` per line; standard and colour
  parameters ramp between keys, booleans step; a bad line exits 2.
- Render cost: `./build/smtest --bench` (720p and 1080p, at the defaults and at the biggest
  disc the controls allow; each frame timed alone, GL_TIME_ELAPSED and wall clock, medians.
  Never time frames queued back to back: on this GPU they measure nothing).

## Verify
- Everything: `TMPDIR=<a writable dir> OXBOW=/path/to/oxbow tools/verify.sh [BUILD_DIR]`
  (source greps, both shaders through `glslc`, a fresh universal build in `build-verify` with
  no warnings outside the SDK, every check on the GPU and again on Apple's software renderer
  at both rasters, the pipe, the bundle, `oxbow probe` and `selftest`, 39 mutants, the sweep,
  Arena's clips, the bench): 569 s in the last full run on this Mac (M4 Max, load average
  15–22). It ends with one line, `verify: all N checks passed (M negative controls; logs in
  …)`, or the failures.
- **Set `TMPDIR`** to a writable scratch directory in a sandboxed Claude session, or the
  heredocs hang.
- **Set `OXBOW`** to a built `oxbow`. There is no default, and verify **fails** without one.
  Build it out of tree: `cmake -S ~/Projects/resolume/oxbow -B <scratch>/oxbow-build &&
  cmake --build <scratch>/oxbow-build` (never inside ~/Projects). oxbow is an effect host and
  hands a mixer one input, so its `selftest` must be refused on frame 0.
- **The checks** (GL, each at 1280×720 and 320×180, each with negative controls): `--mixer`,
  `--closed`, `--unlit`, `--superpose`, `--spill`, `--moire`, `--alpha`, `--state`,
  `--mutation`; `--names` needs no GL. `SMTEST_RENDERER=software` runs any of them on Apple's
  software renderer, which gives different floats (a few ULP) for a draw whose sizes it has not
  met, for a few hundred ms: `--state` compares within a derived 5.39e-5 there, 0 bits on the
  GPU (AGENTS.md, "The software renderer moves"). `SMTEST_STATE_WIDEN=N` runs `--state` at new
  sizes; `--settle-probe` shows the renderer moving.
- The checks can fail: `tools/mutate.sh` (39 one-character mutants, arm64, a 300 s perl alarm
  each; about a minute).
- No dead controls: `python3 tools/sweep.py` (`--bare` drops the contexts).
- Arena's clips: `python3 tools/clips.py alpha` and `python3 tools/clips.py defaults` (needs
  `/Applications/Resolume Arena/media`; about 30 s).

## Notes
- **Index 0 is sacrificial**: Arena hides a mixer's first parameter, so it is Linear Light
  (on). Arena binds a parameter named `Opacity` to the layer fader: here, the projector's level.
- All host parameters are 0..1 and mapped in `source/Controls.cpp`.
- **Straight alpha in, opaque out.** A texel's light is colour × alpha; a resized input is a
  bilinear of that light by hand, never the texture's filter.
- **No state, no clock**: one pass into the host's framebuffer; `SetTimeSupported(false)`.
- GLSL reserved words must not be identifiers; `verify.sh` greps for them. No `sin`/`atan`.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **macOS make compares whole-second mtimes**: mutate.sh deletes the touched objects and the
  binary before every build; never hand-mutate a source file in a shared build directory.
- `smtest`'s `fmt()` takes doubles only: never pass it a `%s` or `%d` format.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide yet)
  and `ATTRIBUTIONS.md`.
- **No workflows, and none to add**: the repo has no remote and may stay private.
- Local only: `~/dev/scrim`, no remote, nothing released, not on the website.

## Not done yet
- Never loaded in Resolume (either platform); never built on Windows.
- No user guide, no OpenFX port, no browser demo, no presets.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/scrim/scrim.YYYY-MM-DD.log
    %LOCALAPPDATA%\scrim\logs\scrim.YYYY-MM-DD.log

`SCRIM_LOG_DIR` overrides the folder. It logs the first frame's sizes, each change of Opacity
(up to a cap per instance), the spill's disc reaching its cap (once), and every guard that
refused a frame.

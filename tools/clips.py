"""Resolume's own demo clips through `smtest --pipe`: the alpha check, and
the measurement Front Light's and Back Light's defaults were set from.

    tools/clips.py alpha [--build DIR]      both alpha clips as Src and as Dest: alpha honoured,
                                            the output opaque
    tools/clips.py defaults [--build DIR]   each clip's 99th-percentile brightest output channel
                                            projected on the gauze at Front Light 1 and seen
                                            through it at Back Light 1: the measurement the two
                                            defaults were set by, checked against the binary

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV,
decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal otherwise, and a
script running it in a loop loses its input). Nothing here is a second copy
of the plugin's formulas: every number is read off the pipe's frames or its
`--stats` lines, which read the brightest channel from the float readback of
the plugin's output. (lithophane's script, for a mixer: stdin is Dest, the
scene behind; `--pipe-src` is Src, the projection.)

**alpha.** The plugin's decision (SPEC-scrim.md): both inputs are straight
alpha, a texel's light is its colour times its alpha, and the output is
opaque. On BattleWeapon_Tank_09 and Ethnik2, the two clips with real
transparency, each as Src (over a flat grey Dest) and as Dest (under a flat
grey Src), at the plugin's defaults (the weave drawn, the spill on, both
lights): every output alpha is 255, and white painted under every fully
transparent pixel changes no output byte. The clips are straight alpha over
black, so without the paint a plugin that ignored alpha would pass. The
negative control paints ~200 opaque pixels instead, which must show.

**defaults.** Front Light and Back Light are in units where a white
diffuser shows the light. SPEC-scrim.md's rule: at the default gauze, the
median over the demo clips (`media/AV`, `media/Shop74`) of each clip's
99th-percentile brightest output channel is 1.0, (a) for the clip projected
on the gauze with nothing behind it (Dest black), at Front Light; (b) for
the clip seen through the gauze with nothing projected (Src black), at Back
Light. Measured at 640x360, 30 frames from 1 s in, the p99 pooled over every
pixel of the 30 frames, each at light 1. At light 1 the output is under 1
everywhere (the threads' share of the mirrors times their reflectance, or
the holes' share, is under 1), so nothing clips there and the linear p99
scales exactly with the light: the default is 1 / the median. This prints
the measurement, re-measures at the binary's defaults (where the median
clip's p99 must be 1.0, and 0.999 of the default must stay under it), and
fails if a binary default is not 1 / its median.
"""

import argparse
import pathlib
import statistics
import subprocess
import sys
import tempfile

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]
LIGHT_MAX = 16.0  # Front Light and Back Light are 16 p^2 (Controls.h)


def light_param(value):
    return (value / LIGHT_MAX) ** 0.5


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def flat(width, height, frames, rgba):
    return bytes(rgba) * (width * height * frames)


def pipe(smtest, scratch, dest, src, width, height, settings, stats=False):
    """Dest on stdin; Src through --pipe-src from a file, or None to hold black."""
    args = [str(smtest), "--pipe", "--size", f"{width}x{height}"]
    if src is None:
        args += ["--input-b", "black"]
    else:
        path = scratch / "src.rgba"
        path.write_bytes(src)
        args += ["--pipe-src", str(path)]
    for s in settings:
        args += ["--set", s]
    if stats:
        args += ["--stats"]
    result = subprocess.run(args, input=dest, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"smtest --pipe failed ({result.returncode}): {result.stderr.decode()[-400:]}")
    frames = np.frombuffer(result.stdout, dtype=np.uint8).reshape(-1, height, width, 4)
    pooled = None
    for line in result.stderr.decode().splitlines():
        if line.startswith("smtest --pipe:") and "over every pixel of every frame" in line:
            words = line.split()
            pooled = (float(words[words.index("channel") + 1]), float(words[words.index("linear") + 1]))
    if stats and pooled is None:
        raise RuntimeError("smtest --stats printed no pooled p99")
    return frames, pooled


def alpha_check(smtest, scratch):
    width, height, count = 320, 180, 20
    failures, negative_caught, negatives = 0, 0, 0
    for name in ALPHA_CLIPS:
        path = MEDIA / name
        if not path.exists():
            print(f"  FAIL  {path} is missing: the alpha check needs Resolume Arena's demo clips")
            return 1
        raw, frames = decode(path, width, height, count)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        grey = flat(width, height, frames, (128, 128, 128, 255))
        hidden = clip[..., 3] == 0
        white = clip.copy()
        white[..., :3][hidden] = 255
        shown = clip.copy()
        opaque = np.argwhere(shown[..., 3] == 255)
        pick = opaque[:: max(1, len(opaque) // 200)]
        shown[pick[:, 0], pick[:, 1], pick[:, 2], :3] = 255
        for role in ("Src", "Dest"):
            def run(frames_bytes):
                if role == "Src":
                    out, _ = pipe(smtest, scratch, grey, frames_bytes, width, height, [])
                else:
                    out, _ = pipe(smtest, scratch, frames_bytes, grey, width, height, [])
                return out
            reference = run(raw)
            painted = run(white.tobytes())
            control = run(shown.tobytes())
            opaque_alpha = int(reference[..., 3].min())
            changed = int(np.count_nonzero(painted != reference))
            ok = opaque_alpha == 255 and changed == 0 and len(reference) == frames
            failures += not ok
            negatives += 1
            negative_caught += int(np.count_nonzero(control != reference)) > 0
            print(f"  {'ok  ' if ok else 'FAIL'}  {path.name} as {role}: {frames} frames at {width}x{height}, "
                  f"{hidden.mean():.0%} fully transparent; output alpha >= {opaque_alpha} (want 255); white painted "
                  f"under every transparent pixel changes {changed} output bytes (want 0)")
    print(f"  {'ok  ' if negative_caught == negatives else 'FAIL'}  negative control: white painted on ~200 "
          f"opaque pixels shows in the output in {negative_caught} of {negatives} runs")
    failures += negative_caught != negatives
    print(f"\n  {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def binary_default(smtest, name):
    """A light's default, read from the binary's own --list (the host's 0..1, four decimals)."""
    listing = subprocess.run([str(smtest), "--list"], capture_output=True, text=True, check=True).stdout
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) >= 5 and " ".join(parts[1:3]) == name:
            p = float(parts[4])
            return p, LIGHT_MAX * p * p
    raise RuntimeError(f"no {name} in smtest --list")


def defaults(smtest, scratch):
    clips = sorted(p for p in (MEDIA / "AV").glob("*.mov")) + sorted(p for p in (MEDIA / "Shop74").glob("*.mov"))
    if not clips:
        print(f"  FAIL  no demo clips under {MEDIA}")
        return 1
    width, height, count = 640, 360, 30
    black = None
    lights = {
        # name: (how a clip is fed, the other light held off)
        "Front Light": "projected on the gauze, Dest black",
        "Back Light": "seen through the gauze, Src black",
    }
    measured = {name: [] for name in lights}
    at_default = {name: [] for name in lights}
    decoded = {}
    for path in clips:
        raw, frames = decode(path, width, height, count)
        decoded[path] = (raw, frames)
        black = flat(width, height, frames, (0, 0, 0, 0))
        row = []
        for name in lights:
            def feed(settings):
                if name == "Front Light":
                    return pipe(smtest, scratch, black, raw, width, height, settings, stats=True)[1]
                return pipe(smtest, scratch, raw, None, width, height, settings, stats=True)[1]
            _, one = feed([f"{name}={light_param(1.0):.7f}"])
            enc, _ = feed([])
            measured[name].append(one)
            at_default[name].append(enc)
            row.append(f"{name} 1: {one:.6f}, default: {enc:.6f}")
        print(f"  {path.parent.name + '/' + path.name:34s} p99 brightest channel, linear at light 1 / encoded at the "
              f"default -- {'; '.join(row)}")
    ok_all = True
    for name, how in lights.items():
        values = measured[name]
        median = statistics.median(values)
        want = 1.0 / median
        p, default = binary_default(smtest, name)
        reached = statistics.median(at_default[name])
        print(f"\n{name} ({how}), {len(clips)} clips at the default gauze, light 1: p99 brightest channel (linear) "
              f"{min(values):.4f} .. {max(values):.4f}, median {median:.8f}, so the rule's {name} is 1 / {median:.8f} = "
              f"{want:.6f}")
        # --list prints the host's 0..1 to four decimals: 16 p^2 moves by 32 p x 5e-5.
        # The source writes the default to four decimals: 5e-5. The p99 histogram's
        # bin is 2^-20 encoded, at most 2.4 / 1.055 x 2^-20 linear, which moves
        # 1 / median by want^2 times that.
        tolerance = 32.0 * p * 5e-5 + 5e-5 + want * want * 2.275 * 2.0 ** -20
        # At the default: the source's four decimals (relative 5e-5 / default), the
        # histogram's bin, and the GPU's encode and the harness's decode (2^-17 each).
        reach_tolerance = 5e-5 / want + 2.0 ** -20 + 2.0 * 2.0 ** -17
        ok_default = abs(default - want) <= tolerance
        ok_reach = abs(reached - 1.0) <= reach_tolerance
        ok_linear = max(values) < 1.0
        print(f"  {'ok  ' if ok_default else 'FAIL'}  the binary's default {default:.4f} against 1 / median {want:.4f}: "
              f"off by {abs(default - want):.5f} (tolerance {tolerance:.5f}: --list's four decimals of the host's 0..1 "
              f"through 16 p^2; the source's four decimals; the p99 histogram's bin)")
        print(f"  {'ok  ' if ok_reach else 'FAIL'}  at the default, the median clip's p99 brightest channel {reached:.6f} "
              f"against 1: off by {abs(reached - 1.0):.2e} (tolerance {reach_tolerance:.2e}: the source's four decimals, "
              f"the bin, the encode and decode)")
        print(f"  {'ok  ' if ok_linear else 'FAIL'}  every clip under 1 at light 1 (nothing clips, so the p99 scales with "
              f"the light): {'yes' if ok_linear else 'NO'}")
        # Clipping makes the check above one-sided: a default too high also reads
        # 1. So the median clip again a thousandth under the default must not.
        middle = clips[sorted(range(len(clips)), key=lambda i: values[i])[len(clips) // 2]]
        raw, frames = decoded[middle]
        black = flat(width, height, frames, (0, 0, 0, 0))
        setting = [f"{name}={light_param(default * 0.999):.7f}"]
        if name == "Front Light":
            under = pipe(smtest, scratch, black, raw, width, height, setting, stats=True)[1][0]
        else:
            under = pipe(smtest, scratch, raw, None, width, height, setting, stats=True)[1][0]
        ok_under = under < 1.0
        print(f"  {'ok  ' if ok_under else 'FAIL'}  the median clip ({middle.name}) at 0.999 of the default: p99 "
              f"brightest channel {under:.6f} (under 1: the default is the threshold, not past it)")
        ok_all = ok_all and ok_default and ok_reach and ok_linear and ok_under
    print(f"\n  {'PASS' if ok_all else 'FAIL'}")
    return 0 if ok_all else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    build = args.build if args.build.is_absolute() else REPO / args.build
    smtest = build / "smtest"
    if not smtest.exists():
        print(f"{smtest} not found", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        if args.what == "alpha":
            print("\n=== alpha on real clips: Resolume's DXV alpha clips through --pipe, as Src and as Dest")
            return alpha_check(smtest, scratch)
        return defaults(smtest, scratch)


if __name__ == "__main__":
    sys.exit(main())

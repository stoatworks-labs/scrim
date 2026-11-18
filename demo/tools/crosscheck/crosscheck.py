"""The browser demo against the plugin: the same frames, two renderers, one picture.

    python3 demo/tools/crosscheck/crosscheck.py [--smtest build-verify/smtest] [--keep DIR]

Each case below is a few frames of two of the page's own inputs -- the kit's
generated clips, or the page's black layer -- at the frames' times, with the
case's settings (and, in one case, cues that ramp and step controls frame by
frame). Each frame is rendered twice:

  - by the PAGE: demo/ served from here and loaded in headless Chrome,
    through window.scrimDemo.begin/frame, which makes a fresh instance,
    renders the two clips, runs the page's ProcessOpenGL into the canvas and
    reads back the picture and both inputs exactly as the shader got them
    (RGBA8, top row first).
  - by the PLUGIN: those same two inputs, frame for frame, piped through
    `smtest --pipe --pipe-src` (the real Scrim class in a GL 4.1 context):
    Dest on stdin, Src from a file, the settings as `--set`, the cues as a
    `--script`. Build smtest first; tools/verify.sh leaves one in
    build-verify/.

The GPU half is the plugin's GLSL (check_shaders.py), so what this mostly
tests is the PORT, demo/port.js: Controls.cpp's conversions, asked(), Plan
and every uniform of ProcessOpenGL. A difference is a port bug, or a
difference between GL 4.1 on this machine and WebGL2 through ANGLE on the
same GPU. Before any picture, the page's table of controls (names, kinds,
defaults) is compared with `smtest --list`. port_numbers.py, beside this,
compares the port's numbers with the C++'s directly.

A case passes when no pixel of any frame is more than --max levels of 255
apart and --exact % of all its pixels are identical. Negative controls --
the page with one control one slider step (0.001) off, or its projection one
frame late -- are compared with the plugin's frames as they should be, and
each must fail that test, so the comparison is shown to see a slip of that
size.

Measured on an M4 Max (GL 4.1 against Chrome's ANGLE on Metal), 18 November
2026: see AGENTS.md, "Browser demo", for the figures.

Not run by tools/verify.sh: it needs a GPU and Chrome. The plugin's log goes
to $SCRIM_LOG_DIR if set, else to the run's temporary folder, never to
~/Library/Logs. Exit 1 if the table differs, a uniform is missing or unset,
a case fails or a negative control passes.
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
from port_numbers import defaults  # noqa: E402  (Scrim::Scrim()'s host defaults)

NAMES = ["Linear Light", "Opacity", "Front Light", "Pixel Fill", "Back Light", "Wash", "Spill Distance",
         "Audience", "Open Area", "Thread", "Thread_Green", "Thread_Blue", "Mesh", "Draw Mesh", "Gauze Width"]
DEFAULT = dict(zip(NAMES, defaults()))
STEP = 0.2  # seconds between frames: far enough apart that every clip moves
FRAMES = 8

# name, settings (host values, as `smtest --set` takes them), Dest (the scene
# behind), Src (the projection), size, Src's size as a share of the output's.
CASES = [
    ("defaults", {}, "scene", "grid", "640x360", 1),
    ("defaults-960", {}, "scene", "grid", "960x540", 1),
    ("detail-over-bars", {}, "bars", "detail", "640x360", 1),
    ("screen", {"Back Light": 0}, "grid", "scene", "640x360", 1),
    ("reveal", {"Front Light": 0}, "scene", "grid", "640x360", 1),
    ("linear-off", {"Linear Light": 0}, "ramp", "spot", "640x360", 1),
    ("wash-alone", {"Wash": 0.35, "Front Light": 0}, "scene", "black", "640x360", 1),
    ("spill-far", {"Back Light": 0, "Spill Distance": 0.5}, "ramp", "spot", "640x360", 1),
    ("audience-at-projector", {"Back Light": 0, "Spill Distance": 0.5, "Audience": 1 / 3}, "bars100", "grid", "640x360", 1),
    ("widest-disc", {"Gauze Width": 0, "Spill Distance": 1, "Back Light": 0}, "scene", "detail", "640x360", 1),
    ("coarse-mesh", {"Gauze Width": 0, "Mesh": 1}, "detail", "scene", "640x360", 1),
    ("draw-mesh-off", {"Draw Mesh": 0}, "scene", "grid", "640x360", 1),
    ("coloured-threads", {"Thread": 1, "Thread_Green": 0.5, "Thread_Blue": 0.1, "Opacity": 0.6,
                          "Pixel Fill": 0, "Open Area": 0.8}, "spot", "bars", "640x360", 1),
    ("closed-gauze", {"Open Area": 0}, "scene", "grid", "640x360", 1),
    ("open-gauze", {"Open Area": 1}, "scene", "grid", "640x360", 1),
    ("alpha-both", {}, "alpha", "alpha", "640x360", 1),
    ("dark-set", {"Spill Distance": 0.5}, "black", "scene", "640x360", 1),
    ("src-three-quarters", {"Spill Distance": 0.4}, "scene", "grid", "640x360", 0.75),
    ("cues", {}, "scene", "grid", "640x360", 1),
]
# The cue sheet for "cues", in smtest --script's form. Every ramp is exact in
# float at every frame (spans of 8 frames, steps of 1/8 of a short binary
# number), so whether the harness's valueAt fuses its multiply-add or not
# cannot move a value.
CUES = [
    (0, "Front Light", 0.25), (8, "Front Light", 0.875),
    (0, "Back Light", 0.75), (8, "Back Light", 0.25),
    (0, "Draw Mesh", 1), (4, "Draw Mesh", 0),
    (0, "Thread_Blue", 0.5), (8, "Thread_Blue", 1),
    (0, "Spill Distance", 0), (8, "Spill Distance", 0.5),
]
CUE_FRAMES = 9
BOOLEANS = {"Linear Light", "Draw Mesh"}

# The negative controls: a case rendered by the page a little off, compared
# with the plugin's frames of the case as it should be.
NEGATIVES = [
    ("defaults", "back-light-1-step", {"Back Light": DEFAULT["Back Light"] + 0.001}, None, "Back Light one slider step (0.001) off"),
    ("defaults", "front-light-1-step", {"Front Light": DEFAULT["Front Light"] + 0.001}, None, "Front Light one step off"),
    ("spill-far", "spill-1-step", {"Spill Distance": 0.501}, None, "Spill Distance one step off"),
    ("defaults", "mesh-1-step", {"Mesh": DEFAULT["Mesh"] + 0.001}, None, "Mesh one step off"),
    ("defaults", "open-area-1-step", {"Open Area": DEFAULT["Open Area"] + 0.001}, None, "Open Area one step off"),
    ("defaults", "thread-1-step", {"Thread": DEFAULT["Thread"] + 0.001}, None, "Thread (red) one step off"),
    ("defaults", "src-1-frame-late", {}, "late", "the projection one frame late"),
]


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def value_at(track, frame, ramps):
    """smtest's valueAt, in float."""
    f = np.float32
    if frame <= track[0][0]:
        return f(track[0][1])
    if frame >= track[-1][0]:
        return f(track[-1][1])
    for i in range(1, len(track)):
        if frame < track[i][0]:
            a, b = track[i - 1], track[i]
            if not ramps:
                return f(a[1])
            span = f(b[0] - a[0])
            t = f(frame - a[0]) / span if span > 0 else f(1)
            return f(a[1]) + (f(b[1]) - f(a[1])) * t
        if frame == track[i][0]:
            return f(track[i][1])
    return f(track[-1][1])


def cue_frames():
    tracks = {}
    for frame, name, value in CUES:
        tracks.setdefault(name, []).append((frame, value))
    for track in tracks.values():
        track.sort()
    return [{name: float(value_at(track, n, name not in BOOLEANS)) for name, track in tracks.items()}
            for n in range(CUE_FRAMES)]


# ---------------------------------------------------------------- the server
class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        parts = self.path.split("/")
        if self.path.startswith(("/xc/out/", "/xc/dest/", "/xc/src/")):
            self.state[parts[2]][(parts[3], int(parts[4]))] = body
        elif self.path == "/xc/table":
            self.state["table"] = json.loads(body)
        elif self.path == "/xc/missing":
            self.state["missing"] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------- the plugin
def run_smtest(smtest, args, env, stdin=None):
    done = subprocess.run([smtest, *args], stdin=stdin, capture_output=True, check=False, env=env)
    if done.returncode != 0:
        sys.exit(f"smtest {' '.join(args)} failed: {done.stderr.decode(errors='replace').strip()}")
    return done.stdout


def plugin_frames(smtest, work, env, case, dest, src):
    """The page's own inputs, frame for frame, through smtest --pipe."""
    name = case["name"]
    dest_path = os.path.join(work, f"{name}.dest.rgba")
    src_path = os.path.join(work, f"{name}.src.rgba")
    with open(dest_path, "wb") as handle:
        handle.write(b"".join(dest))
    with open(src_path, "wb") as handle:
        handle.write(b"".join(src))
    args = ["--pipe", "--size", f"{case['width']}x{case['height']}",
            "--src-size", f"{case['srcWidth']}x{case['srcHeight']}", "--pipe-src", src_path]
    for key, value in case["settings"].items():
        args += ["--set", f"{key}={value!r}"]
    if case["cues"]:
        script = os.path.join(work, f"{name}.cues")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write("".join(f"{frame} {key} {value!r}\n" for frame, key, value in CUES))
        args += ["--script", script]
    with open(dest_path, "rb") as handle:
        out = run_smtest(smtest, args, env, stdin=handle)
    size = case["width"] * case["height"] * 4
    if len(out) != size * len(dest):
        sys.exit(f"smtest --pipe for {name} wrote {len(out)} bytes, not {len(dest)} frames of {size}")
    os.remove(dest_path)
    os.remove(src_path)
    return [out[i * size:(i + 1) * size] for i in range(len(dest))]


# ---------------------------------------------------------------- the judging
def compare(frames_a, frames_b, width, height):
    x = np.stack([np.frombuffer(f, np.uint8).reshape(height, width, 4) for f in frames_a]).astype(np.int16)
    y = np.stack([np.frombuffer(f, np.uint8).reshape(height, width, 4) for f in frames_b]).astype(np.int16)
    worst = np.abs(x - y).max(axis=3)
    return {
        "exact": float((worst == 0).mean() * 100.0),
        "within1": float((worst <= 1).mean() * 100.0),
        "max": int(worst.max()),
        "mean": float(np.abs(x - y)[..., :3].mean()),
        "frames_exact": int(sum(1 for i in range(len(frames_a)) if not worst[i].any())),
    }


def passes(s, args):
    return s["max"] <= args.max and s["exact"] >= args.exact


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(boolean|standard|option|red|green|blue|about)\s+(\S+)\s+(\[[^\]]*\]|-)\s*(.*)$")


def compare_table(smtest, page_table, env):
    """`smtest --list` against the page's table: name, kind, default (4 dp), in order."""
    rows = []
    for line in run_smtest(smtest, ["--list"], env).decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None or m.group(3) == "about":
            continue
        rows.append((int(m.group(1)), m.group(2), m.group(3), float(m.group(4))))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"smtest lists {len(rows)} controls before the About block, the page {len(page_table)}")
    for (index, name, kind, value), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005:
            problems.append(f"#{index}: smtest says {name} {kind} {value:.4f}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f}")
    return len(rows), problems


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--smtest", default=os.path.join(REPO, "build-verify", "smtest"))
    parser.add_argument("--keep", help="write the first and last frame of each case, both renderers, as PNGs")
    parser.add_argument("--max", type=int, default=1,
                        help="the largest difference, in levels of 255, allowed anywhere (default 1)")
    parser.add_argument("--exact", type=float, default=99.0,
                        help="share of pixels, %%, that must be identical (default 99)")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--port", type=int, default=0, help="the local server's port (default: any free one)")
    args = parser.parse_args(argv)
    if not os.access(args.smtest, os.X_OK):
        sys.exit(f"no smtest at {args.smtest}: build it (tools/verify.sh, or cmake --build build --target smtest)")

    work = tempfile.mkdtemp(prefix="scrim-xc-")
    # The harness instantiates the real plugin, which writes a log: keep it
    # out of ~/Library/Logs, where an Arena session's log is read from.
    env = {**os.environ, "SCRIM_LOG_DIR": os.environ.get("SCRIM_LOG_DIR") or os.path.join(work, "log")}

    cases = []
    for name, settings, dest, src, size, share in CASES:
        width, height = parse_size(size)
        count = CUE_FRAMES if name == "cues" else (4 if width > 640 else FRAMES)
        times = [round(n * STEP, 6) for n in range(count)]
        cases.append({"name": name, "settings": settings, "dest": dest, "src": src,
                      "width": width, "height": height,
                      "srcWidth": round(width * share), "srcHeight": round(height * share),
                      "times": times, "srcTimes": times, "inputs": True,
                      "cues": cue_frames() if name == "cues" else None})
    by_name = {c["name"]: c for c in cases}
    for base, name, change, late, _ in NEGATIVES:
        b = by_name[base]
        cases.append({**b, "name": name, "settings": {**b["settings"], **change}, "inputs": False,
                      "srcTimes": [round(t - STEP, 6) for t in b["times"]] if late else b["times"]})

    state = {"job": {"cases": cases}, "out": {}, "dest": {}, "src": {}, "table": None, "missing": None,
             "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, state=state))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="scrim-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(900):
            sys.exit("the page never finished (900 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    failures = 0
    count, table_problems = compare_table(args.smtest, state["table"] or [], env)
    if table_problems:
        failures += len(table_problems)
        print("the page's controls differ from smtest --list:")
        for line in table_problems:
            print(f"  {line}")
    else:
        print(f"controls: all {count} of smtest --list's before the About block, same names, kinds and defaults, in order")
    gaps = state["missing"] or {}
    for label, key in (("uniforms the page set that the program lacks", "missing"),
                       ("uniforms the program has that the page never set", "unset"), ("GL errors", "errors"),
                       ("frames the instance refused", "refused")):
        if gaps.get(key):
            failures += 1
            print(f"{label}: {', '.join(sorted(set(gaps[key])))}")
    if not any(gaps.get(k) for k in ("missing", "unset", "errors", "refused")):
        print("every uniform set, none missing, no GL error, no frame refused")

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)

    plugin = {}
    total_frames = 0
    print(f"\n{'case':<24} {'size':>9} {'src':>9} {'frames':>6} {'==':>9} {'<=1':>9} {'max':>4} {'mean':>7}")
    for c in cases:
        if not c["inputs"]:
            continue
        name, n = c["name"], len(c["times"])
        try:
            dest = [state["dest"][(name, i)] for i in range(n)]
            src = [state["src"][(name, i)] for i in range(n)]
            page = [state["out"][(name, i)] for i in range(n)]
        except KeyError:
            print(f"{name:<24} frames missing from the page")
            failures += 1
            continue
        plugin[name] = plugin_frames(args.smtest, work, env, c, dest, src)
        total_frames += n
        s = compare(plugin[name], page, c["width"], c["height"])
        ok = passes(s, args)
        failures += 0 if ok else 1
        print(f"{name:<24} {c['width']}x{c['height']:<4} {c['srcWidth']}x{c['srcHeight']:<4} {n:>6} {s['exact']:8.3f}% "
              f"{s['within1']:8.3f}% {s['max']:>4} {s['mean']:7.4f}  {'ok' if ok else 'FAIL'}"
              f"{'' if s['frames_exact'] < n else '  (every frame identical)'}")
        if args.keep:
            for i in (0, n - 1):
                for label, data in (("plugin", plugin[name][i]), ("page", page[i]), ("dest", dest[i])):
                    Image.frombytes("RGBA", (c["width"], c["height"]), data).save(
                        os.path.join(args.keep, f"{name}-{i}-{label}.png"))
                Image.frombytes("RGBA", (c["srcWidth"], c["srcHeight"]), src[i]).save(
                    os.path.join(args.keep, f"{name}-{i}-src.png"))
    print(f"{total_frames} frames in all")

    print("\nnegative controls: the page a little off, against the plugin as it should be")
    for base, name, _, _, label in NEGATIVES:
        c = by_name[base]
        n = len(c["times"])
        got = [state["out"].get((name, i)) for i in range(n)]
        s = compare(plugin[base], got, c["width"], c["height"]) if base in plugin and all(got) else None
        caught = s is not None and not passes(s, args)
        failures += 0 if caught else 1
        print(f"  {label:<40} " + (f"{s['exact']:8.3f}% {s['within1']:8.3f}% {s['max']:>4} {s['mean']:7.4f}  "
                                     f"{'caught' if caught else 'NOT CAUGHT'}" if s else "no picture"))
    print(f"\n{'every case passes, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: no pixel more than {args.max} level(s) apart and {args.exact}% identical)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

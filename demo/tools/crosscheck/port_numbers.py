"""The port's numbers against the C++'s: demo/port.js's Plan against Model.cpp's.

    python3 demo/tools/crosscheck/port_numbers.py [--cases N] [--seed S]

plan_print.cpp is compiled here with /usr/bin/c++ on the plugin's own
source/Controls.cpp and source/Model.cpp (the flags the Release build uses:
-O3, C++17), and plan_print.mjs imports demo/port.js under node, the page's
own ScrimInstance.asked() and Plan. Both are fed the same host values -- the
defaults, every control at 0 and at 1 on its own, everything at 0 and at 1,
and N random sets (floats, as a host sends them) at widths from 1 to 16384
pixels -- and print every field of Setup and of its Asked.

The rule: every int and boolean equal; in a case where the three pows
(AudienceFromParam, MeshFromParam and WidthFromParam: 0.25 x 64^p,
0.5 x 20^p and 2 x 10^p) come out the same, every double equal to the bit;
where one of them does not (libm's pow against V8's, a place apart), every
double within 1e-12 of the C++'s, relative, and the same float -- what the
uniform carries to the shader. freqLo is f less its integer and 8-bit
parts, so its error is f's: it is judged against f, and it alone may round
to a neighbouring float (a phase of a few 1e-12 cycles a pixel).

plan_print.cpp carries a copy of Scrim::asked()'s body (the class needs the
SDK otherwise); this fails if those lines are no longer in source/Scrim.cpp.

Not run by tools/verify.sh (it needs node). Exit 1 on any difference.
"""
import argparse
import math
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))

# Setup's fields as the printers write them: (name, kind), kind i (an int or
# a boolean) or d (a double).
FIELDS = ([("linear", "i")]
          + [(n, "d") for n in ("opacity", "front", "fill", "back", "wash", "spill", "audience", "open",
                                "thread[0]", "thread[1]", "thread[2]", "meshMm")]
          + [("drawMesh", "i"), ("widthM", "d")]
          + [(n, "d") for n in ("reflect[0]", "reflect[1]", "reflect[2]", "frontGain", "back(setup)", "wash(setup)",
                                "spillGain", "magnification", "apparent", "invApparent", "radiusPx")]
          + [("radiusCapped", "i")]
          + [(n, "d") for n in ("pitchPx", "freq[0]", "freq[1]")]
          + [("freqHi[0]", "i"), ("freqHi[1]", "i")]
          + [(n, "d") for n in ("freqLo[0]", "freqLo[1]", "openAxis[0]", "openAxis[1]", "aperture")])
COUNT = 15
POW_FIELDS = ("audience", "meshMm", "widthM")  # 0.25 x 64^p, 0.5 x 20^p, 2 x 10^p
WIDTHS = [1, 2, 63, 320, 640, 960, 1280, 1920, 2048, 3840, 7680, 16384]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def defaults():
    """Scrim::Scrim()'s defaults, as Controls.cpp's ParamFor* would give them."""
    return [1.0, 1.0, f32(math.sqrt(12.2665 / 16.0)), f32((0.92 - 0.5) / 0.5), f32(math.sqrt(8.0279 / 16.0)), 0.0,
            f32(0.5 / 3.0), f32(math.log(4.0 / 0.25) / math.log(64.0)), f32(0.535),
            f32(0.8822), f32(0.8822), f32(0.8822),
            f32(math.log(2.5 / 0.5) / math.log(20.0)), 1.0, f32(math.log(9.5 / 2.0) / math.log(10.0))]


def cases(count, seed):
    rng = random.Random(seed)
    base = defaults()
    out = [(base, 960), (base, 1920), (base, 640)]
    for i in range(COUNT):
        for v in (0.0, 1.0):
            row = list(base)
            row[i] = v
            out.append((row, 960))
    out += [([0.0] * COUNT, 960), ([1.0] * COUNT, 960), ([0.0] * COUNT, 1), ([1.0] * COUNT, 16384)]
    for _ in range(count):
        row = [f32(rng.random()) for _ in range(COUNT)]
        out.append((row, rng.choice(WIDTHS) if rng.random() < 0.5 else rng.randint(1, 16384)))
    return out


def check_asked_copy():
    """The asked() body in plan_print.cpp, line for line in Scrim.cpp."""
    mine = open(os.path.join(HERE, "plan_print.cpp"), encoding="utf-8").read()
    body = mine.split("// ---- Scrim::asked(), verbatim ----", 1)[1].split("// ---- end ----", 1)[0]
    want = [line.strip() for line in body.splitlines() if line.strip()]
    source = open(os.path.join(REPO, "source", "Scrim.cpp"), encoding="utf-8").read()
    start = source.index("Asked Scrim::asked() const")
    theirs = [line.strip() for line in source[start:].splitlines()[2:2 + len(want)]]
    return want == theirs, want, theirs


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", type=int, default=4000)
    parser.add_argument("--seed", type=int, default=20261118)
    parser.add_argument("--node", default="node")
    args = parser.parse_args(argv)

    same, want, theirs = check_asked_copy()
    if not same:
        print("plan_print.cpp's asked() is no longer Scrim::asked():")
        for a, b in zip(want, theirs):
            if a != b:
                print(f"  copy:   {a}\n  source: {b}")
        return 1

    work = tempfile.mkdtemp(prefix="scrim-plan-")
    exe = os.path.join(work, "plan_print")
    build = subprocess.run(["/usr/bin/c++", "-std=c++17", "-O3", "-DNDEBUG", "-I", os.path.join(REPO, "source"),
                            os.path.join(HERE, "plan_print.cpp"), os.path.join(REPO, "source", "Controls.cpp"),
                            os.path.join(REPO, "source", "Model.cpp"), "-o", exe], capture_output=True, text=True)
    if build.returncode != 0:
        sys.exit(f"plan_print.cpp did not build:\n{build.stderr}")

    rows = cases(args.cases, args.seed)
    stdin = "".join(" ".join(repr(v) for v in values) + f" {width}\n" for values, width in rows)
    cpp = subprocess.run([exe], input=stdin, capture_output=True, text=True, check=True).stdout.split("\n")
    js = subprocess.run([args.node, os.path.join(HERE, "plan_print.mjs")], input=stdin, capture_output=True,
                        text=True, check=True).stdout.split("\n")
    os.remove(exe)
    os.rmdir(work)

    problems = []
    exact = total = traced = 0
    by_field = {}
    floats_apart = 0
    worst = (0.0, "")
    names = [name for name, _ in FIELDS]
    pow_fields = [names.index(n) for n in POW_FIELDS]
    for n, (values, width) in enumerate(rows):
        a, b = cpp[n].split(), js[n].split()
        if len(a) != len(FIELDS) or len(b) != len(FIELDS):
            problems.append(f"case {n}: {len(a)} fields from the C++, {len(b)} from the port, {len(FIELDS)} expected")
            continue
        # Does the case start from a pow that came out differently?
        from_pow = any(float(a[i]) != float(b[i]) for i in pow_fields)
        freq = {0: abs(float(a[names.index("freq[0]")])), 1: abs(float(a[names.index("freq[1]")]))}
        differs = False
        for (name, kind), x, y in zip(FIELDS, a, b):
            if kind == "i":
                if int(x) != int(y):
                    problems.append(f"case {n} (width {width}): {name} {x} in the C++, {y} in the port")
                continue
            total += 1
            p, q = float(x), float(y)
            if p == q:
                exact += 1
                continue
            differs = True
            by_field[name] = by_field.get(name, 0) + 1
            if not from_pow:
                problems.append(f"case {n} (width {width}): {name} {x} in the C++, {y} in the port, and no pow differs")
                continue
            # freqLo is f less its integer and 8-bit parts: its error is f's, so it is judged against f.
            scale = max(1.0, freq[int(name[-2])]) if name.startswith("freqLo") else max(abs(p), abs(q))
            rel = abs(p - q) / scale
            if rel > worst[0]:
                worst = (rel, f"case {n} (width {width}) {name}: {x} against {y}")
            if rel > 1e-12:
                problems.append(f"case {n} (width {width}): {name} {x} in the C++, {y} in the port ({rel:.3g} apart)")
            elif f32(p) != f32(q):
                if name.startswith("freqLo"):
                    floats_apart += 1
                else:
                    problems.append(f"case {n} (width {width}): {name} {x} and {y} are different floats")
        traced += 1 if differs else 0
    print(f"asked(): plan_print.cpp's copy is Scrim.cpp's, line for line ({len(want)} lines)")
    print(f"{len(rows)} cases, {len(FIELDS)} fields each: {total} doubles, {exact} equal to the bit "
          f"({exact / total * 100:.4f}%), and every int and boolean compared")
    if by_field:
        print(f"{traced} case(s) differ, each where a pow came out a place apart (libm against V8): "
              + ", ".join(f"{k} {v}" for k, v in by_field.items()))
        print(f"the largest difference: {worst[0]:.3g} (relative; freqLo against its frequency), {worst[1]}")
        print(f"freqLo rounds to a neighbouring float in {floats_apart} of those; every other field is the same float")
    for line in problems[:40]:
        print(f"  {line}")
    print("the port's numbers are the C++'s" if not problems else f"{len(problems)} difference(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

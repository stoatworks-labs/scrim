"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py            compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write    regenerate demo/shaders.js
    python3 demo/tools/check_shaders.py --hashes   print the ported C++'s hashes (for a reader)

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL pieces of `source/Shaders.cpp`: kVersion,
kVertex (which passes nothing but the position), kCommon (the sRGB pair) and
kScreenFragment, the one pass: the weave's open share of each pixel and of the
projector's mirror, the projection, the scene behind, the spill's disc and the
sum. That is two copies of the same text, and two copies drift -- quietly,
because a gauze drawn by a *plausible* shader looks exactly like one drawn by
the right one. The whole claim of the page is that it runs the plugin's own
shaders, so the claim needs something enforcing it. `smtest` drives the real
plugin class and has never heard of this page, and `tools/glslc.sh` compiles
the C++ copies and never looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each piece out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. A piece may be several adjacent raw strings
(MSVC refuses one literal over about 16 KB); this joins them as the compiler
does, with nothing between.

It fails when Shaders.cpp grows a piece this script does not list (a pass the
page would silently lack), and it checks the JOIN as well as the pieces:
AssembleFragment() is kVersion + kCommon + the pass and AssembleVertex() is
kVersion + kVertex, Scrim.cpp's InitGL compiles those, and demo/port.js must
assemble them the same way.

The page is more than the shaders. The CPU half -- Controls.cpp's conversions,
Model.cpp's Plan (the frame's numbers, in double: the lights' gains, the
spill's magnifications and disc, the weave's frequencies and the split of its
phase into an exact 8-bit part and a remainder), and Scrim.cpp's constructor,
asked() and ProcessOpenGL (which texture is Dest and which is Src, the guards,
every uniform) -- is translated by hand into demo/port.js. This cannot check a
translation, but it can trip when its source moves: PORTED_FROM holds the
SHA-256 of every C++ file and function the port was made from, and a change to
any of them fails here until a reader carries it across and updates the hash
(`--hashes` prints the new ones).

And it fails when a piece declares a uniform that demo/port.js never names (a
uniform the page does not set sits at 0, a control quietly dead on the page and
live in the plugin), or when port.js hands an integer uniform -- int, ivec,
uint, bool or a sampler -- to the kit's `.set()`, which always uploads a float
and so is refused by GL without a word. Every piece is checked, not only the
fragment.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write would escape one as \\`.
Shaders.cpp has none today. This undoes that escape and REJECTS any other
backslash or any `${` on the JS side; there are none in the C++ bodies, so
either could only be somebody hiding a difference.

`--write` produces shaders.js from the C++ by exactly that escape, so the copy
is spliced by this script and never typed.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half itself. demo/tools/crosscheck/ renders
smtest --pipe and the page from the same frames and compares the pictures,
frame by frame, and port_numbers.py compares the port's numbers with the C++'s;
both need more than verify.sh has (Chrome and a GPU; node), so they are run by
hand. When you change the C++ the port came from, change demo/port.js too.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there.
SHADERS = [
    ("VERSION", "kVersion"),
    ("VERTEX", "kVertex"),
    ("COMMON", "kCommon"),
    ("SCREEN_FRAGMENT", "kScreenFragment"),
]

# The pieces whose uniforms port.js must set (by name, quoted).
UNIFORM_PIECES = [symbol for _, symbol in SHADERS]

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugin's assembly; the JS lines
# are port.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/Shaders.cpp",
     "std::string AssembleFragment( const char* pass )\n{\n"
     "\treturn std::string( kVersion ) + kCommon + pass;\n}"),
    ("source/Shaders.cpp",
     "std::string AssembleVertex()\n{\n"
     "\treturn std::string( kVersion ) + kVertex;\n}"),
    ("source/Scrim.cpp",
     "const std::string screen = fragmentOverride ? std::string( fragmentOverride ) : AssembleFragment( kScreenFragment );"),
    ("source/Scrim.cpp", "if( !screenShader.Compile( AssembleVertex(), screen ) )"),
    ("demo/port.js", "export const AssembleVertex = () => S.VERSION + S.VERTEX;"),
    ("demo/port.js", "export const AssembleFragment = (pass) => S.VERSION + S.COMMON + pass;"),
    ("demo/port.js", "return new Program(gl, AssembleVertex(), AssembleFragment(S.SCREEN_FRAGMENT), 'screen');"),
]

# What demo/port.js was translated from, by SHA-256: whole files, or one C++
# function from its signature to its closing brace. Not a proof of the port: a
# tripwire for its source. Run --hashes after carrying a change across.
PORTED_FROM = [
    ("source/Names.h", None, "0f441d94273b7060443747edd34d20457aed26262b6766c63b56aa3bd569cfb7"),
    ("source/Controls.h", None, "74fc070ab35510075057208fdab01aef107c2d3f26b95b7aeedd464325a9544d"),
    ("source/Controls.cpp", None, "ba133e4219001e4bb2137640be2e6819685dc93132cd58432a62e0f8c9628cba"),
    ("source/Model.h", None, "a8c4e9335ebd98f32e35695febe349ec9844bc6a639a13f63b94dc554023c9ff"),
    ("source/Model.cpp", None, "1d685324ec3fcb93af3b526f90e72c102459eee8bf9df87ed52bd5605fe1a234"),
    ("source/Scrim.h", "enum ParamID : FFUInt32", "b8f4e38e00034b17fd67927b4f4a5c6875f78152c9bbda767241f7b87792c378"),
    ("source/Scrim.cpp", "Scrim::Scrim()", "46858a7f978dd014be81b683d59e742fa0bc049c2be18c52cba35a42b375d622"),
    ("source/Scrim.cpp", "Asked Scrim::asked() const", "766d6fab06fea07d9fd9569f46bb89292b6308fc25f5ca4a71cbd962e1db1926"),
    ("source/Scrim.cpp", "FFResult Scrim::ProcessOpenGL( ProcessOpenGLStruct* pGL )",
     "972f0bc6567db9c484c46de9961a733f672f96571aff91fe19533531d24647a6"),
    ("source/Scrim.cpp", "FFResult Scrim::SetFloatParameter( unsigned int index, float value )",
     "b181d9180b5579310d28e4bca8f3e86589051f767ddabcf1ebc90bc1d16feb2f"),
    ("source/GLState.h", "inline void unbindTextureUnits( int count )",
     "8fffd06413f51dc000b0bfdfca429c5a78b0af7724717012371b102cdf948997"),
    ("source/GLState.h", "inline void bindUnit( int unit, GLuint texture )",
     "6dfa576379a2dfc1b3e68e5f860bcf227fc2e7a4b1bbddb6837450d34d415d74"),
]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts), "r", encoding="utf-8") as handle:
        return handle.read()


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them.

    Returns (text, count of raw strings), or (None, 0) if the symbol is not
    there or what follows its `=` is not a run of adjacent raw strings ending
    in `;`.
    """
    match = re.search(r'const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    """Every raw-string piece Shaders.cpp defines, by symbol."""
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def uniforms(glsl):
    """Every uniform a piece declares, as (type, name), comments stripped
    first. An array (`uniform vec2 Offset[ 3 ];`) counts by its name."""
    code = re.sub(r"//[^\n]*", "", glsl)
    return re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(?:\[\s*\d+\s*\])?\s*;", code, re.M)


INTEGER_TYPES = re.compile(r"^(int|uint|ivec[234]|uvec[234]|bool|bvec[234]|[iu]?sampler\w+)$")


# ---------------------------------------------------------------- the port's source
def function_body(source, signature):
    """From `signature` to the brace that closes the body after it."""
    start = source.find(signature)
    if start < 0:
        return None
    brace = source.find("{", start + len(signature))
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    return None


def ported_hashes():
    out = []
    for path, signature, want in PORTED_FROM:
        try:
            text = read(*path.split("/"))
        except FileNotFoundError:
            out.append((path, signature, want, None))
            continue
        piece = text if signature is None else function_body(text, signature)
        got = None if piece is None else hashlib.sha256(piece.encode("utf-8")).hexdigest()
        out.append((path, signature, want, got))
    return out


def check_ported():
    problems = 0
    for path, signature, want, got in ported_hashes():
        what = path if signature is None else f"{path}: {signature.split('(')[0]}"
        if got is None:
            print(f"FAIL  ported from {what}: not found")
            problems += 1
        elif got != want:
            print(f"FAIL  ported from {what}: changed since demo/port.js was made from it "
                  f"(sha256 {got[:12]}, the port's {want[:12]}) -- carry the change across, then --hashes")
            problems += 1
        else:
            print(f"ok    ported from {what} ({got[:12]})")
    return problems


# ---------------------------------------------------------------- shaders.js
def write(cpp):
    out = [HEADER]
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w", encoding="utf-8") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def verbatim(entries, what):
    problems = 0
    for path, text in entries:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    {what}: {path} still says {text.splitlines()[0][:64]}")
        else:
            print(f"FAIL  {what}: {path} no longer says: {text.splitlines()[0]}")
            problems += 1
    return problems


def check(cpp, js):
    problems = 0
    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    problems += verbatim(ASSEMBLY, "assembly")
    problems += check_ported()

    try:
        port = read("demo", "port.js")
    except FileNotFoundError:
        port = ""

    for symbol in UNIFORM_PIECES:
        text, _ = from_cpp(cpp, symbol)
        if text is None:
            continue
        declared = uniforms(text)
        # Named as 'Name' or, for an array, element by element as 'Name[0]'.
        unset = [n for _, n in declared if f"'{n}'" not in port and f"'{n}[" not in port]
        floated = [n for t, n in declared
                   if INTEGER_TYPES.match(t) and re.search(r"\.set\(\s*'" + n + r"[\[']", port)]
        if unset:
            print(f"FAIL  {symbol} declares {', '.join(unset)}, which demo/port.js never names -- "
                  "the page would leave it at 0")
            problems += 1
        elif floated:
            print(f"FAIL  {symbol}'s integer uniform(s) {', '.join(floated)} go through .set(), which uploads a "
                  "float that GL refuses -- use setInt, setSampler or gl.uniform2i")
            problems += 1
        else:
            print(f"ok    uniforms: all {len(declared)} of {symbol}'s are named in demo/port.js, integers as integers")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp)
    if "--hashes" in argv:
        for path, signature, _, got in ported_hashes():
            print(f"{got}  {path}{'' if signature is None else ': ' + signature}")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js by hand); an assembly, ported-source, uniform or unlisted-piece "
              "failure needs a reader")
        return 1
    print(f"all {len(SHADERS)} shader pieces, their assembly, the ported C++ and the uniforms are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

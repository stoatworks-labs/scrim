#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# Each step answers a question none of the others can:
#
#   source        GLSL reserved words declared as identifiers, M_PI, the
#                 built-ins GLSL ES 3.00 lacks, no workflow files, and the
#                 FFGL SDK still at the fleet's pin
#   shaders       do the two shaders (the vertex shader and the screen pass)
#                 compile through a real GLSL compiler (tools/glslc.sh),
#                 before a host has to find out. glslc is REQUIRED here: a
#                 skipped compile is not a pass.
#   build         a fresh universal Release build, which is what would ship;
#                 no warning in this repo's own code
#   checks        the plugin's claims, measured at TWO rasters (1280x720 and
#                 320x180), each with negative controls built into the real
#                 plugin, and one character of the shipped GLSL mutated
#   software      the same checks, both rasters, on Apple's software
#                 renderer, which is what a GPU-less machine gets: a check
#                 calibrated on this Mac's GPU fails here before it fails
#                 anywhere else.
#   pipe          the filming mode: N frames in, N out, two inputs; a
#                 standard parameter ramps and a boolean steps; bad cues
#                 and bad --set values refused; a closed stdout is exit 1
#   bundle        plugMain exported (the OBJECT-library trap), lipo
#                 (universal, or did CMake latch the architecture list
#                 early and report success anyway), the plist, an ad-hoc
#                 signature on a copy, the version in two places
#   oxbow         the name, id, TYPE and input count a host reads, parameter
#                 0 (which Arena hides on a mixer), the Opacity name, no
#                 clock asked for, and the refusal of the single input an
#                 effect host hands it. REQUIRED: set OXBOW= to a built
#                 oxbow; there is no default, and without one this FAILS.
#   mutants       tools/mutate.sh: one-character mutants, each caught
#   sweep         does every control change the picture (tools/sweep.py);
#                 then once more with every context emptied (--bare), for
#                 the record
#   clips         Resolume's demo clips: alpha honoured on both inputs, and
#                 Front Light's and Back Light's defaults re-measured
#                 (tools/clips.py). Needs /Applications/Resolume Arena/media.
#   bench         the render cost at 720p and 1080p, each frame timed alone
#                 on the GPU's clock, for the record: the times are not
#                 pass/fail; a frame that fails to render is.
#
#     TMPDIR=<scratch> OXBOW=/path/to/oxbow tools/verify.sh [BUILD_DIR]
#
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

BUILD="${1:-$REPO/build-verify}"
OXBOW="${OXBOW:-}"
passes=0
failures=0
LOGS="$( mktemp -d "${TMPDIR:-/tmp}/smverify.XXXXXX" )"

# Every harness run instantiates the real plugin, and the plugin writes a
# log. Keep those lines out of the log an Arena session would be read from.
export SCRIM_LOG_DIR="${SCRIM_LOG_DIR:-$LOGS/scrim-logs}"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
pass() { printf '   \033[32mok\033[0m   %s\n' "$1"; passes=$(( passes + 1 )); }
fail() { printf '   \033[31mFAIL\033[0m %s\n' "$1"; failures=$(( failures + 1 )); }
limit() { perl -e 'alarm shift; exec @ARGV' "$@"; }

#---------------------------------------------------------------------------
step "source"
#---------------------------------------------------------------------------
# GLSL 4.10's reserved words, declared as identifiers: a declaration of one
# compiles on some drivers and not on others (`packed` passed Apple's compiler
# and glslc and failed only Mesa: atrac; noise1-4 are built-ins a user
# function is silently shadowed by: dice). Only declarations -- a type, then
# the word -- since calling a built-in such as round() is what it is for.
if python3 -I - <<'PY'
import re, sys, pathlib
words = ( "patch sample input output filter common active half layout flat packed smooth round "
          "noise1 noise2 noise3 noise4 superp partition resource namespace using cast external "
          "interface long short fixed unsigned inline goto asm class union enum typedef template "
          "this volatile public static extern sizeof near far precise noperspective" ).split()
types = r"(?:float|int|uint|bool|[iuhdf]?vec[234]|mat[234]|u?sampler2D|void|const)"
pattern = re.compile( r"\b" + types + r"\s+(" + "|".join( words ) + r")\b" )
text = pathlib.Path( "source/Shaders.cpp" ).read_text()
glsl = "".join( re.findall( r'R"\((.*?)\)"', text, re.S ) )
glsl = re.sub( r"//[^\n]*", "", glsl )# comments are free to use any word
if not glsl.strip():
	sys.exit( "   no GLSL found in source/Shaders.cpp" )
hits = sorted( set( m.group( 1 ) for m in pattern.finditer( glsl ) ) )
if hits:
	print( "   declared in GLSL: " + ", ".join( hits ) )
	sys.exit( 1 )
if "M_PI" in glsl:
	print( "   M_PI in GLSL: no GLSL defines it" )
	sys.exit( 1 )
# The check must be able to see one.
if not pattern.search( "float sample = 1.0;" ):
	sys.exit( "   the reserved-word pattern matches nothing" )
PY
then
	pass "no GLSL reserved word declared as an identifier; no M_PI"
else
	fail "a GLSL reserved word is declared as an identifier, or M_PI is used"
fi
# Built-ins that arrived in GLSL 4.00 / ES 3.10: they compile everywhere here
# but not in WebGL2, where the fleet's browser demos run the shaders unedited.
# Scrim has no demo; the rule is kept so one can be made.
if command grep -nE '(^|[^A-Za-z_])(findMSB|findLSB|bitCount|bitfieldExtract|bitfieldInsert|bitfieldReverse|uaddCarry|usubBorrow|umulExtended|imulExtended|frexp|ldexp|fma|textureGather|textureQueryLevels|packUnorm4x8|unpackUnorm4x8)[[:space:]]*\(' source/Shaders.cpp | command grep -vE '^[0-9]+:[[:space:]]*//' >"$LOGS/webgl.txt"; then
	sed 's/^/   /' "$LOGS/webgl.txt"
	fail "a built-in GLSL ES 3.00 lacks is in the GLSL"
else
	pass "no built-in GLSL ES 3.00 lacks"
fi
# No GitHub workflows: the repo is local and may become a private one, and
# the fleet's private repos share one small Actions allowance.
if [ -d .github/workflows ] && [ -n "$( ls -A .github/workflows 2>/dev/null )" ]; then
	fail "there are workflow files in .github/workflows -- this repo builds and tests locally"
else
	pass "no GitHub workflows"
fi
pin=$( git -C external/ffgl rev-parse --short=7 HEAD 2>/dev/null )
if [ "$pin" = "b1afaf9" ]; then
	pass "external/ffgl at the fleet's pin, b1afaf9"
else
	fail "external/ffgl is at '$pin', not the fleet's b1afaf9"
fi

#---------------------------------------------------------------------------
step "shaders"
#---------------------------------------------------------------------------
if GLSLC_REQUIRED=1 tools/glslc.sh; then
	pass "every shader compiles through glslc"
else
	fail "a shader does not compile, or glslc is missing (brew install shaderc)"
fi

#---------------------------------------------------------------------------
step "build"
#---------------------------------------------------------------------------
# A fresh universal Release build -- the one that would ship. The dev build
# in build/ is arm64 only and is not what any of the binary checks below
# should be looking at. Fresh only when it is verify's own directory: a
# BUILD_DIR passed in is someone's, and lipo below catches one that is not
# universal.
[ "$BUILD" = "$REPO/build-verify" ] && rm -rf "$BUILD"
if cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$LOGS/configure.txt" 2>&1 \
	&& cmake --build "$BUILD" --parallel >"$LOGS/build.txt" 2>&1; then
	pass "universal Release build in $( basename "$BUILD" )"
else
	tail -20 "$LOGS/build.txt" "$LOGS/configure.txt" 2>/dev/null | sed 's/^/   /'
	fail "build failed"
	printf '\n\033[31mverify: stopped at the build\033[0m\n'
	exit 1
fi
# Our own warnings only: the SDK's FFGLUtilities.cpp warns about RAND_MAX.
if command grep -E "warning:" "$LOGS/build.txt" | command grep -v "external/ffgl" >"$LOGS/warnings.txt"; then
	sed 's/^/   /' "$LOGS/warnings.txt" | /usr/bin/head -10
	fail "the build warns in this repo's own code"
else
	pass "no warnings in this repo's own code"
fi
SMTEST="$BUILD/smtest"

#---------------------------------------------------------------------------
step "checks (this Mac's GPU, 1280x720 and 320x180)"
#---------------------------------------------------------------------------
# Every claim the README makes. Each GL check runs at both rasters itself and
# carries its own negative controls; --names needs no GL. A check that
# printed no assertion at all is a failure, whatever its exit code.
CHECKS="names mixer closed unlit superpose spill moire alpha state mutation"
negatives=0
for check in $CHECKS; do
	status=0
	out=$( limit 900 "$SMTEST" --$check 2>&1 ) || status=$?
	n=$( printf '%s\n' "$out" | command grep -c '^  ok' )
	neg=$( printf '%s\n' "$out" | command grep -c '^  ok    negative control' )
	if [ "$status" -eq 0 ] && [ "$n" -gt 0 ]; then
		negatives=$(( negatives + neg ))
		pass "smtest --$check: $n assertions, $neg of them negative controls"
	else
		printf '%s\n' "$out" | command grep -E '^  FAIL' | sed 's/^/      /'
		fail "smtest --$check (exit $status, $n assertions)"
	fi
done

#---------------------------------------------------------------------------
step "software renderer (both rasters)"
#---------------------------------------------------------------------------
# All of them, on Apple's software renderer. It is not bit-identical under
# load, so no check here compares a run against another run's bits except
# within one process.
for check in $CHECKS; do
	[ "$check" = names ] && continue
	status=0
	out=$( SMTEST_RENDERER=software limit 1800 "$SMTEST" --$check 2>&1 ) || status=$?
	n=$( printf '%s\n' "$out" | command grep -c '^  ok' )
	case "$out" in *"Apple's software renderer"*) renderer=yes ;; *) renderer=no ;; esac
	if [ "$status" -eq 0 ] && [ "$n" -gt 0 ] && [ "$renderer" = yes ]; then
		pass "smtest --$check (software): $n assertions"
	else
		printf '%s\n' "$out" | command grep -E '^  FAIL' | sed 's/^/      /'
		fail "smtest --$check on the software renderer (exit $status, $n assertions, renderer confirmed: $renderer) -- run: SMTEST_RENDERER=software $SMTEST --$check"
	fi
done

#---------------------------------------------------------------------------
step "pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, with the mixer's second input: stdin is
# Dest (the scene behind), --pipe-src is Src (the projection), raw RGBA, top
# row first.
W=64; H=36; FRAME=$(( W * H * 4 ))
tmp="$LOGS/pipe"; mkdir -p "$tmp"
# frames N R G B A > file: N flat frames.
frames() {
	python3 -I -c "import sys; sys.stdout.buffer.write(bytes([$2, $3, $4, $5]) * ($W * $H * $1))"
}
# N frames of a coloured card, every pixel different, opaque.
cardframes() {
	python3 -I -c "
import sys
w, h, n = $W, $H, $1
out = bytearray()
for f in range(n):
    for y in range(h):
        for x in range(w):
            out += bytes([(x * 4 + f * 7) % 256, (y * 7 + 31) % 256, (x * y + 90) % 256, 255])
sys.stdout.buffer.write(out)"
}
frames 3 0 0 0 255 > "$tmp/black3.rgba"
frames 2 0 0 0 255 > "$tmp/black2.rgba"
cardframes 3 > "$tmp/card3.rgba"
cardframes 2 > "$tmp/card2.rgba"
/usr/bin/head -c $(( FRAME * 5 / 2 )) "$tmp/black3.rgba" > "$tmp/black2half.rgba"
# Unit gain: white threads, a quarter of each mirror thread (Open Area 0.75,
# the weave averaged), Front Light 4 (host 0.5), Linear Light off: what the
# audience sees with nothing behind lit is the projection, bit for bit.
UNIT=( --set "Linear Light=off" --set "Draw Mesh=off" --set "Open Area=0.75" --set "Front Light=0.5"
       --set "Thread=1" --set "Thread_Green=1" --set "Thread_Blue=1" )

status=0
"$SMTEST" --pipe --size ${W}x${H} --pipe-src "$tmp/card3.rgba" "${UNIT[@]}" < "$tmp/black3.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
if [ "$status" -eq 0 ] && cmp -s "$tmp/out.rgba" "$tmp/card3.rgba"; then
	pass "3 frames in, 3 out; nothing behind lit gives the projection back, byte for byte, through both inputs"
else
	fail "3 frames, nothing behind: exit $status, $( wc -c < "$tmp/out.rgba" | tr -d ' ' ) bytes, or not the projection"
fi
status=0
"$SMTEST" --pipe --size ${W}x${H} --pipe-src "$tmp/black3.rgba" --set "Back Light=0.5" < "$tmp/card3.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
if [ "$status" -eq 0 ] && [ "$( wc -c < "$tmp/out.rgba" | tr -d ' ' )" -eq $(( FRAME * 3 )) ] && ! cmp -s "$tmp/out.rgba" "$tmp/black3.rgba"; then
	pass "a lit scene on stdin shows through the gauze"
else
	fail "a lit scene on stdin: exit $status, or the output was black"
fi
status=0
"$SMTEST" --pipe --size ${W}x${H} --pipe-src "$tmp/card2.rgba" < "$tmp/black3.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
got=$( wc -c < "$tmp/out.rgba" | tr -d ' ' )
if [ "$status" -eq 0 ] && [ "$got" -eq $(( FRAME * 2 )) ]; then
	pass "3 Dest frames under 2 Src frames: 2 out -- the stream ends with either input"
else
	fail "3 Dest frames under 2 Src frames gave $got bytes, exit $status"
fi
status=0
"$SMTEST" --pipe --size ${W}x${H} --input-b black < "$tmp/black2half.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
got=$( wc -c < "$tmp/out.rgba" | tr -d ' ' )
if [ "$status" -eq 0 ] && [ "$got" -eq $(( FRAME * 2 )) ]; then
	pass "2.5 frames in, exactly 2 out: a partial frame ends the stream"
else
	fail "2.5 frames in gave $got bytes (want $(( FRAME * 2 ))), exit $status"
fi
# Cues: Front Light ramps 0 -> host 0.5 over frames 0..2; Linear Light steps
# off -> on at frame 4. A grey projection, nothing behind:
#   frames 0 < 1 < 2   the ramp (a STEPPED Front Light would be 0, 0, 0.5)
#   frames 2 = 3       Linear Light still off (a RAMPED one is 0.75 at 3: on)
#   frames 3 != 4 = 5  the step
# (Not Draw Mesh: at 64x36 a 9.5 m gauze puts ~59 weave cells in a pixel, so
# the weave per pixel and its average are the same bytes.)
frames 6 128 128 128 255 > "$tmp/grey6.rgba"
frames 6 0 0 0 255 > "$tmp/black6.rgba"
printf '0 Front Light 0\n2 Front Light 0.5\n0 Linear Light 0\n4 Linear Light 1\n' > "$tmp/cues.txt"
status=0
"$SMTEST" --pipe --size ${W}x${H} --pipe-src "$tmp/grey6.rgba" --script "$tmp/cues.txt" < "$tmp/black6.rgba" > "$tmp/cued.rgba" 2>/dev/null || status=$?
verdict=$( python3 -I - "$tmp/cued.rgba" $FRAME <<'PY'
import sys
data, size = open( sys.argv[ 1 ], "rb" ).read(), int( sys.argv[ 2 ] )
f = [ data[ i * size:( i + 1 ) * size ] for i in range( len( data ) // size ) ]
if len( f ) != 6:
	print( f"bad {len( f )} frames" ); sys.exit()
m = [ sum( x[ 0::4 ] ) / ( size / 4 ) for x in f ]
ok = m[ 0 ] < m[ 1 ] < m[ 2 ] and f[ 2 ] == f[ 3 ] and f[ 3 ] != f[ 4 ] and f[ 4 ] == f[ 5 ]
print( ( "ok " if ok else "bad " ) + " ".join( f"{v:.1f}" for v in m ) )
PY
)
case "$status:$verdict" in
	0:ok*) pass "cues: Front Light ramps, Linear Light steps (mean red ${verdict#ok })" ;;
	*) fail "cues gave exit $status, '${verdict#bad }': want a rising ramp over 0..2, equal 2 and 3, a step at 4" ;;
esac
printf '0 No Such Control 0.5\n' > "$tmp/bad.txt"
status=0
"$SMTEST" --pipe --size ${W}x${H} --script "$tmp/bad.txt" < "$tmp/black3.rgba" >/dev/null 2>&1 || status=$?
[ "$status" -eq 2 ] && pass "a cue naming no parameter is refused (exit 2)" || fail "a cue naming no parameter gave exit $status, not 2"
# --set: a number, all of it; a boolean also takes on/off/true/false; anything
# else, and a name that is not a parameter, refused with exit 2 -- never read
# as 0. (Scrim has no option parameter, so the option-name path is not
# reachable from here.)
s1=0; "$SMTEST" --out "$tmp/x.png" --size ${W}x${H} --set "Draw Mesh=off" >/dev/null 2>&1 || s1=$?
s2=0; "$SMTEST" --out "$tmp/x.png" --size ${W}x${H} --set "Draw Mesh=sideways" >/dev/null 2>&1 || s2=$?
s3=0; "$SMTEST" --out "$tmp/x.png" --size ${W}x${H} --set "Wash=0.5x" >/dev/null 2>&1 || s3=$?
s4=0; "$SMTEST" --out "$tmp/x.png" --size ${W}x${H} --set "Curtain=0.5" >/dev/null 2>&1 || s4=$?
s5=0; "$SMTEST" --out "$tmp/x.png" --size ${W}x${H} --set "Wash=nan" >/dev/null 2>&1 || s5=$?
if [ "$s1" -eq 0 ] && [ "$s2" -eq 2 ] && [ "$s3" -eq 2 ] && [ "$s4" -eq 2 ] && [ "$s5" -eq 2 ]; then
	pass "--set takes 'Draw Mesh=off', refuses 'Draw Mesh=sideways', 'Wash=0.5x', 'Wash=nan' and 'Curtain=0.5' (exit 2)"
else
	fail "--set: Draw Mesh=off gave $s1, sideways $s2, 0.5x $s3, no such name $s4, nan $s5"
fi
# The two-input pipe into a reader that hangs up. Read from files, not a
# pipe: a writer killed by SIGPIPE would fail the pipeline whatever smtest
# did. Under bash, for PIPESTATUS.
frames 20 128 128 128 255 > "$tmp/grey20.rgba"
cardframes 3 > /dev/null
python3 -I -c "import sys; sys.stdout.buffer.write(bytes([200, 100, 50, 255]) * ($W * $H * 20))" > "$tmp/src20.rgba"
"$SMTEST" --pipe --size ${W}x${H} --pipe-src "$tmp/src20.rgba" < "$tmp/grey20.rgba" 2>/dev/null | /usr/bin/head -c 1 >/dev/null
status=${PIPESTATUS[0]}
if [ "$status" -eq 1 ]; then
	pass "smtest --pipe --pipe-src ... | head -c 1 exits 1 (SIGPIPE ignored), not 141"
else
	fail "a closed stdout gave exit $status, expected 1"
fi

#---------------------------------------------------------------------------
step "Scrim.bundle"
#---------------------------------------------------------------------------
bundle="$BUILD/Scrim.bundle"
bin="$bundle/Contents/MacOS/Scrim"
declared=$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | /usr/bin/head -1 )
if [ -n "$declared" ] && command grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h; then
	pass "CMakeLists and StoatworksAbout.h agree: v$declared"
else
	fail "StoatworksAbout.h's versionFallback is not v$declared"
fi
# `nm ... | grep -q X` FAILS when grep FINDS its match under pipefail: grep
# exits at once, nm takes SIGPIPE, and the pipeline reports that failure.
syms=$( nm -gU "$bin" 2>/dev/null )
case "$syms" in
	*_plugMain*) pass "exports plugMain" ;;
	*) fail "no plugMain -- the bundle contains no plugin" ;;
esac
archs=$( lipo -archs "$bin" 2>/dev/null )
case "$archs" in *arm64*x86_64*|*x86_64*arm64*) pass "universal: $archs" ;; *) fail "not universal (got: $archs)" ;; esac
plist="$bundle/Contents/Info.plist"
read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null; }
exe=$( read_plist CFBundleExecutable )
if [ -n "$exe" ] && [ -f "$bundle/Contents/MacOS/$exe" ] && [ "$( read_plist CFBundleIdentifier )" = "com.stoatworks.ffgl.scrim" ] \
	&& [ "$( read_plist CFBundlePackageType )" = "BNDL" ] && [ "$( read_plist CFBundleVersion )" = "$declared" ]; then
	pass "plist: CFBundleExecutable $exe on disk, com.stoatworks.ffgl.scrim, BNDL, v$declared"
else
	fail "plist: executable '$exe', id '$( read_plist CFBundleIdentifier )', type '$( read_plist CFBundlePackageType )', version '$( read_plist CFBundleVersion )'"
fi
copy="$LOGS/sign"; mkdir -p "$copy"; cp -R "$bundle" "$copy/"
if codesign --force --sign - --timestamp=none "$copy/Scrim.bundle" >/dev/null 2>&1 \
	&& codesign --verify --deep --strict "$copy/Scrim.bundle" >/dev/null 2>&1; then
	pass "ad-hoc signs and verifies (on a copy)"
else
	fail "ad-hoc signing failed"
fi

#---------------------------------------------------------------------------
step "oxbow"
#---------------------------------------------------------------------------
if [ -z "$OXBOW" ] || [ ! -x "$OXBOW" ]; then
	fail "no oxbow (OXBOW='$OXBOW') -- set OXBOW= to a built oxbow; this step does not skip"
else
	probe=$( limit 120 "$OXBOW" probe "$bundle" 2>&1 )
	printf '%s\n' "$probe" > "$LOGS/probe.txt"
	case "$probe" in *"name:        SW Scrim"$'\n'*) pass "a host reads the name SW Scrim" ;; *) fail "wrong or missing name -- see: $OXBOW probe $bundle" ;; esac
	case "$probe" in *"id:          SM01"*) pass "a host reads the id SM01" ;; *) fail "wrong or missing id (want SM01)" ;; esac
	case "$probe" in *"type:        mixer"*) pass "a host reads the type mixer" ;; *) fail "wrong plugin type (want mixer) -- the 8th argument of CFFGLPluginInfo" ;; esac
	case "$probe" in *"inputs:      2..2"*) pass "a host is told 2..2 inputs" ;; *) fail "wrong input count (want 2..2) -- SetMinInputs/SetMaxInputs are separate from the type" ;; esac
	case "$probe" in *"set-time:    no"*) pass "a host is told the plugin takes no clock" ;; *) fail "the plugin asks for the host's clock (want set-time: no)" ;; esac
	# Arena does not expose a mixer's parameter 0 (genlock, wipe, relay,
	# lenticular, pepper), so index 0 is a control whose default is right for
	# ever: Linear Light, on.
	case "$probe" in
		*"[ 0] Linear Light     type=0   default=1 "*) pass "parameter 0, which Arena hides, is Linear Light, a boolean, on" ;;
		*) fail "parameter 0 is not Linear Light (boolean, on) -- Arena hides a mixer's first parameter" ;;
	esac
	# Arena binds a mixer parameter named Opacity to the layer's fader.
	case "$probe" in
		*"] Opacity          type=10"*) pass "the projector's level is a standard parameter named Opacity" ;;
		*) fail "no standard parameter named Opacity -- Arena binds the layer fader by that name" ;;
	esac
	# oxbow hosts effects: it hands a plugin one input. A mixer must refuse
	# that (its guard, FF_FAIL), not draw from an unbound texture unit.
	selftest=$( limit 120 "$OXBOW" selftest "$bundle" 2>&1 )
	case "$selftest" in
		*"FF_PROCESS_OPENGL failed on frame 0"*) pass "one input, as an effect host gives, is refused (FF_FAIL)" ;;
		*) fail "the mixer did not refuse a single input" ;;
	esac
fi

#---------------------------------------------------------------------------
step "mutants"
#---------------------------------------------------------------------------
if tools/mutate.sh >"$LOGS/mutate.txt" 2>&1; then
	pass "$( tail -1 "$LOGS/mutate.txt" )"
else
	command grep -E '^== mutant|FAIL' "$LOGS/mutate.txt" | command grep -B1 'FAIL' | sed 's/^/   /'
	fail "a mutant survived: $( tail -1 "$LOGS/mutate.txt" )"
fi

#---------------------------------------------------------------------------
step "sweep"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
if out=$( python3 -I tools/sweep.py --build "$BUILD" 2>&1 ); then
	pass "$( printf '%s\n' "$out" | tail -1 )"
else
	printf '%s\n' "$out" | sed 's/^/   /'
	fail "a dead control"
fi
# For the record, not pass/fail: every context emptied, the plugin's defaults.
out=$( python3 -I tools/sweep.py --build "$BUILD" --bare 2>&1 )
printf '%s\n' "$out" | sed 's/^/   (bare) /'

#---------------------------------------------------------------------------
step "clips (Resolume's demo clips)"
#---------------------------------------------------------------------------
if out=$( python3 -I tools/clips.py alpha --build "$BUILD" 2>&1 ); then
	pass "alpha on BattleWeapon_Tank_09 and Ethnik2, each as Src and as Dest: $( printf '%s\n' "$out" | command grep -c '^  ok' ) checks"
else
	printf '%s\n' "$out" | command grep -E 'FAIL' | sed 's/^/   /'
	fail "tools/clips.py alpha"
fi
if out=$( python3 -I tools/clips.py defaults --build "$BUILD" 2>&1 ); then
	printf '%s\n' "$out" > "$LOGS/defaults.txt"
	pass "Front Light's and Back Light's defaults are 1 / the median clip's p99 ($( printf '%s\n' "$out" | command grep -c '^  ok' ) checks; $LOGS/defaults.txt)"
else
	printf '%s\n' "$out" > "$LOGS/defaults.txt"
	printf '%s\n' "$out" | command grep -E 'FAIL' | sed 's/^/   /'
	fail "tools/clips.py defaults ($LOGS/defaults.txt)"
fi

#---------------------------------------------------------------------------
step "bench"
#---------------------------------------------------------------------------
# The times are for the record; a frame that failed to render, or a blank
# output, is a failure.
status=0
"$SMTEST" --bench --frames 60 > "$LOGS/bench.txt" 2>&1 || status=$?
sed 's/^/   /' "$LOGS/bench.txt"
[ "$status" -eq 0 ] && pass "bench: every frame rendered and drew" || fail "bench exited $status ($LOGS/bench.txt)"

printf '\n'
if [ "$failures" -eq 0 ]; then
	printf '\033[32mverify: all %d checks passed\033[0m (%d negative controls; logs in %s)\n' "$passes" "$negatives" "$LOGS"
else
	printf '\033[31mverify: %d of %d checks failed\033[0m (logs in %s)\n' "$failures" $(( passes + failures )) "$LOGS"
fi
exit $(( failures > 0 ? 1 : 0 ))

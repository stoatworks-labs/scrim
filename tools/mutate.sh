#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), configured once, arm64-only. For each mutant the pristine files
# are put back, their objects and the binary deleted, the one character
# changed, smtest rebuilt, and only the named check run (it runs at 1280x720
# and 320x180), under a time limit (a mutant can hang: honeydew's lesson; a
# timeout counts as caught). No mutant touches a loop bound upwards, so none
# can make the GPU spin.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The scratch tree goes under TMPDIR when it is set (a heredoc into python
# hung once with the system's /var/folders TMPDIR: the fleet's trap).
WORK="$(mktemp -d "${TMPDIR:-/tmp}/smmutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export SCRIM_LOG_DIR="${SCRIM_LOG_DIR:-$WORK/logs}"

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap) -- which is why no mutant here touches a GLSL '||'.
MUTANTS=(
	"source/Shaders.cpp|return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), greaterThan( c, vec3( 0.04045 ) ) );|return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), greaterThan( c, vec3( 0.14045 ) ) );|--unlit|GLSL decode: the knee at 0.14, 0 -> 1"
	"source/Shaders.cpp|return mix( x * 12.92, 1.055 * pow( x, vec3( 1.0 / 2.4 ) ) - 0.055, greaterThan( x, vec3( 0.0031308 ) ) );|return mix( x * 12.92, 1.055 * pow( x, vec3( 1.0 / 2.2 ) ) - 0.055, greaterThan( x, vec3( 0.0031308 ) ) );|--closed|GLSL encode: gamma 1 / 2.2, 4 -> 2"
	"source/Shaders.cpp|return faulty( kFaultStraight ) ? c : c * clamp( t.a, 0.0, 1.0 );|return faulty( kFaultStraight ) ? c : c + clamp( t.a, 0.0, 1.0 );|--alpha|GLSL lightOf: alpha added to the colour, not multiplied, * -> +"
	"source/Shaders.cpp|ivec2 top = ( faulty( kFaultPadded ) ? hardware : size ) - ivec2( 1 );|ivec2 top = ( faulty( kFaultPadded ) ? hardware : size ) - ivec2( 0 );|--mixer|GLSL texelLight: the clamp one texel into the padding, 1 -> 0"
	"source/Shaders.cpp|if( size == OutSize )|if( size != OutSize )|--mixer|GLSL lightAt: fetched where resized, resampled where not, = -> !"
	"source/Shaders.cpp|return mix( mix( l00, l10, f.x ), mix( l01, l11, f.x ), f.y );|return mix( mix( l00, l10, f.x ), mix( l01, l11, f.x ), f.x );|--mixer|GLSL lightAt: the bilinear's rows mixed by the column fraction, y -> x"
	"source/Shaders.cpp|u        = u - 0.5;|u        = u - 0.4;|--mixer|GLSL lightAt: the texel centres a tenth off, 5 -> 4"
	"source/Shaders.cpp|if( any( lessThan( q, ivec2( 0 ) ) )|if( any( lessThan( q, ivec2( 1 ) ) )|--unlit|GLSL projectionAt: the frame's first row and column taken as off it, 0 -> 1"
	"source/Shaders.cpp|float chord  = sqrt( max( Radius * Radius - ym * ym, 0.0 ) );|float chord  = sqrt( max( Radius * Radius + ym * ym, 0.0 ) );|--spill|GLSL spillAt: the disc's chord R^2 + y^2, - -> +"
	"source/Shaders.cpp|float w = ( x1 - x0 ) * height;|float w = ( x1 - x0 ) + height;|--spill|GLSL spillAt: a texel's overlap as width plus height, * -> +"
	"source/Shaders.cpp|return total > 0.0 ? sum / total : vec3( 0.0 );|return total > 0.0 ? sum * total : vec3( 0.0 );|--closed|GLSL spillAt: the gather multiplied by its area, / -> *"
	"source/Shaders.cpp|if( Radius < kPointRadius )|if( Radius > kPointRadius )|--spill|GLSL spillAt: a real disc taken as a point, < -> >"
	"source/Shaders.cpp|int r       = int( ceil( Radius ) ) + 1;|int r       = int( ceil( Radius ) ) - 1;|--spill|GLSL spillAt: the gather two texels short of the disc, + -> -"
	"source/Shaders.cpp|return n * phiA + min( t - n, phiA );|return n * phiA - min( t - n, phiA );|--moire|GLSL openTo: the part cycle's opening subtracted, + -> -"
	"source/Shaders.cpp|return fract( float( ( i * hi ) & 255 ) / 256.0 + float( i ) * lo );|return fract( float( ( i * hi ) & 255 ) / 255.0 + float( i ) * lo );|--moire|GLSL phaseAt: the exact part over 255, 6 -> 5"
	"source/Shaders.cpp|cAp      = openOver( px, Freq.x, Aperture, OpenAxis.x ) * openOver( py, Freq.y, Aperture, OpenAxis.y );|cAp      = openOver( px, Freq.y, Aperture, OpenAxis.x ) * openOver( py, Freq.y, Aperture, OpenAxis.y );|--moire|GLSL main: the mirror's weave across at the vertical frequency, x -> y"
	"source/Shaders.cpp|cBox     = openOver( px, Freq.x, 1.0, OpenAxis.x ) * openOver( py, Freq.y, 1.0, OpenAxis.y );|cBox     = openOver( px, Freq.x, 1.0, OpenAxis.x ) * openOver( py, Freq.y, 1.0, OpenAxis.x );|--moire|GLSL main: the holes' share down at the across opening, y -> x"
	"source/Shaders.cpp|vec3 reflected = rho * ( 1.0 - cAp ) * ( Front * projected ) + rho * ( 1.0 - cBox ) * Wash;|vec3 reflected = rho * ( 1.0 + cAp ) * ( Front * projected ) + rho * ( 1.0 - cBox ) * Wash;|--closed|GLSL main: the holes reflecting the projection, - -> +"
	"source/Shaders.cpp|vec3 reflected = rho * ( 1.0 - cAp ) * ( Front * projected ) + rho * ( 1.0 - cBox ) * Wash;|vec3 reflected = rho * ( 1.0 - cAp ) * ( Front * projected ) + rho * ( 1.0 + cBox ) * Wash;|--closed|GLSL main: the holes reflecting the wash, - -> +"
	"source/Shaders.cpp|vec3 behind    = Back + SpillGain * spill;|vec3 behind    = Back - SpillGain * spill;|--closed|GLSL main: the spill darkening the set, + -> -"
	"source/Shaders.cpp|vec3 passed = cBox * stage * behind;|vec3 passed = cBox + stage * behind;|--unlit|GLSL main: the holes' share added to the light through them, * -> +"
	"source/Shaders.cpp|vec2 centre = 0.5 * vec2( OutSize );|vec2 centre = 0.6 * vec2( OutSize );|--spill|GLSL main: the lens's axis a tenth of the frame off centre, 5 -> 6"
	"source/Shaders.cpp|if( SpillGain > 0.0 )|if( SpillGain < 0.0 )|--closed|GLSL main: the spill never gathered, > -> <"
	"source/Shaders.cpp|shown = encode( clamp( reflected + passed, 0.0, 1.0 ) );|shown = encode( clamp( reflected - passed, 0.0, 1.0 ) );|--closed|GLSL main: the light through the holes subtracted, + -> -"
	"source/Shaders.cpp|float alpha = 1.0;|float alpha = 0.0;|--alpha|GLSL main: the output transparent, 1 -> 0"
	"source/Model.cpp|s.apparent       = ( 1.0 + k ) / ( 1.0 + k / v );|s.apparent       = ( 1.0 + k ) / ( 1.0 - k / v );|--spill|C++: M_app = (1 + k)/(1 - k/v), + -> -"
	"source/Model.cpp|s.spillGain      = phi * s.frontGain / spread;|s.spillGain      = phi + s.frontGain / spread;|--closed|C++: the spill's gain phi + Front / M^2, * -> +"
	"source/Model.cpp|const double onGauze   = pupilM * k / ( 2.0 * ( 1.0 + k ) );|const double onGauze   = pupilM * k / ( 2.0 * ( 1.0 - k ) );|--spill|C++: the circle of confusion over (1 - k), + -> -"
	"source/Model.cpp|s.freq[ 1 ]     = ( fault & kFaultSquareCell ) ? s.freq[ 0 ] : 2.0 * s.freq[ 0 ];|s.freq[ 1 ]     = ( fault & kFaultSquareCell ) ? s.freq[ 0 ] : 3.0 * s.freq[ 0 ];|--moire|C++: the cell a third as tall as it is wide, 2 -> 3"
	"source/Model.cpp|s.openAxis[ 1 ] = 1.0 - 2.0 * a;|s.openAxis[ 1 ] = 1.0 - 3.0 * a;|--moire|C++: the vertical opening 1 - 3t, 2 -> 3"
	"source/Model.cpp|return ( 3.0 - std::sqrt( 1.0 + 8.0 * std::clamp( phi, 0.0, 1.0 ) ) ) / 4.0;|return ( 3.0 - std::sqrt( 1.0 + 9.0 * std::clamp( phi, 0.0, 1.0 ) ) ) / 4.0;|--moire|C++: the thread fraction's root with 9 phi, 8 -> 9"
	"source/Model.cpp|s.freqLo[ axis ] = g - s.freqHi[ axis ] / 256.0;|s.freqLo[ axis ] = g - s.freqHi[ axis ] / 255.0;|--moire|C++: the phase's remainder over 255, 6 -> 5"
	"source/Controls.cpp|return kLightMax * p * p;|return kLightMax * p + p;|--names|C++: the lights' mapping no longer squared, * -> +"
	"source/Controls.cpp|return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );|return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.5 );|--closed|C++: the Thread picker decoded at gamma 2.5, 4 -> 5"
	"source/Controls.cpp|return kAudienceMin * std::pow( kAudienceRange, unit( value ) );|return kAudienceMin / std::pow( kAudienceRange, unit( value ) );|--names|C++: Audience's mapping divided, * -> /"
	"source/Scrim.cpp|bindUnit( 1, src.Handle );|bindUnit( 0, src.Handle );|--mixer|C++: Src bound over Dest on unit 0, 1 -> 0"
	"source/Scrim.cpp|if( pGL->numInputTextures < 2 )|if( pGL->numInputTextures < 1 )|--mixer|C++: one input accepted, 2 -> 1"
	"source/Scrim.cpp|a.drawMesh    = params[ PT_DRAW_MESH ] > 0.5f;|a.drawMesh    = params[ PT_DRAW_MESH ] < 0.5f;|--moire|C++: Draw Mesh inverted, > -> <"
	"source/Scrim.cpp|screenShader.Set( \"Aperture\", static_cast< float >( s.aperture ) );|screenShader.Set( \"Aperturf\", static_cast< float >( s.aperture ) );|--moire|C++: the Aperture uniform's name misspelt, e -> f (glUniform on -1 is a silent no-op)"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$REPO/vcpkg.json" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$tree/build" --target smtest -j6 >/dev/null 2>&1

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	# Every mutated file back to the shipped text first. Each file loses its
	# object too: make on macOS compares whole-second mtimes, and a file put
	# back in the same second the last mutant's object was written looks up
	# to date -- the previous mutant then runs again under this one's name
	# (penumbra's lesson).
	for f in source/Shaders.cpp source/Model.cpp source/Controls.cpp source/Scrim.cpp "$file"; do
		cmp -s "$REPO/$f" "$tree/$f" || cp "$REPO/$f" "$tree/$f"
		find "$tree/build" -name "$(basename "$f").o" -delete
	done
	# And the binary, so it is relinked whatever the clock says.
	rm -f "$tree/build/smtest"

	python3 -I - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	if ! cmake --build "$tree/build" --target smtest -j6 >"$WORK/build.log" 2>&1; then
		printf '   FAIL  the mutant does not build -- a mutant must compile to mean anything\n'
		continue
	fi
	# A mutant is in the binary or whatever runs next is not this mutant: the
	# GLSL ones are string literals in it, and so is a uniform's name.
	if [[ "$file" == source/Shaders.cpp || "$mutant" == *'"'* ]]; then
		needle="$mutant"
		[[ "$mutant" == *'"'* ]] && needle="$( printf '%s' "$mutant" | sed -n 's/.*"\([^"]*\)".*/\1/p' )"
		if ! command grep -qF -- "$needle" "$tree/build/smtest"; then
			printf '   FAIL  the built smtest does not carry the mutant -- a stale build\n'
			continue
		fi
	fi
	status=0
	limit 300 "$tree/build/smtest" "$check" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ command grep -E '^  FAIL' "$WORK/log" || true; } | /usr/bin/head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]

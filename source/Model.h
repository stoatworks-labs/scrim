#pragma once

/**
    The gauze, the projector and the stage, as numbers: everything the screen
    pass needs that does not change from pixel to pixel, worked out once a
    frame in double.

    What the audience sees at a point of the gauze is the light its threads
    reflect plus the light that comes through its holes
    (SPEC-scrim.md, "The one idea"):

        L = rho ( 1 - c_ap ) Front Src + rho ( 1 - c_box ) Wash
          + c_box Dest ( Back + phi Front Src_spill / M^2 )

    c_ap is the holes' share of the projector pixel's mirror (a centred square
    of side sqrt( Fill )), c_box their share of the whole pixel, both phi with
    Draw Mesh off. Src_spill is the projection integrated over the defocus disc
    around the point the audience sees through, x / M_app.

    No state: nothing here survives a frame.
*/
namespace scrim
{

/// The negative controls' bitmask. The shipped plugin always carries 0; only
/// the harness sets any of these. The bits the shader reads are repeated
/// there as GLSL constants (Shaders.cpp).
enum Fault : int
{
	kFaultNone             = 0,
	kFaultOpacity          = 1 << 0, ///< shader: the threads' reflectance ignored, the gauze an opacity -- --closed
	kFaultSwapArea         = 1 << 1, ///< shader: the holes reflect and the threads pass -- --closed
	kFaultBackAlways       = 1 << 2, ///< a floor of back light: a dark backstage glows -- --unlit
	kFaultWashSpills       = 1 << 3, ///< shader: the wash passes the holes and lights the set -- --unlit
	kFaultGammaSum         = 1 << 4, ///< shader: the reflected and passed light summed encoded -- --superpose, --closed
	kFaultViewerScale      = 1 << 5, ///< the spill placed at the set's magnification M, not M_app -- --spill
	kFaultNoSpread         = 1 << 6, ///< the spill not spread over M^2 -- --spill
	kFaultBlurOnSet        = 1 << 7, ///< the defocus disc at its size on the set, A s / T -- --spill
	kFaultBoxAperture      = 1 << 8, ///< the reflected light over the whole pixel: the fill factor ignored -- --moire
	kFaultSquareCell       = 1 << 9, ///< the cell as tall as it is wide -- --moire
	kFaultStraight         = 1 << 10,///< shader: the colour not multiplied by its alpha -- --alpha
	kFaultHardwareBilinear = 1 << 11,///< shader: a resized input through the host's filter before premultiplying -- --alpha
	kFaultAlphaFromInput   = 1 << 12,///< shader: out.a from Dest -- --alpha
	kFaultLeaveBound       = 1 << 13,///< a texture left bound on unit 1 -- --mixer
	kFaultPadded           = 1 << 14,///< shader: an input read over its hardware size -- --mixer
	kFaultStaleSize        = 1 << 15,///< the first frame's output width kept: state across frames -- --state
	kFaultClock            = 1 << 16,///< the weave breathes with the host's clock -- --state
};

/// The lens's entrance pupil, mm: a Panasonic PT-RZ120's standard zoom at its
/// wide end, f 25.6 mm at F1.7 (Panasonic UK specification page, read
/// 2026-11-17): 25.6 / 1.7 = 15.06 mm. Not a control.
inline constexpr double kPupilMm = 25.6 / 1.7;

/// The spill's defocus disc, capped: a guard, not physics. At the controls'
/// extreme (Gauze Width 2 m, Spill Distance 3) the disc is 5.4 texels at
/// 1920 wide and 10.8 at 3840; the cap is reached only past 5600 wide.
inline constexpr double kMaxRadiusPx = 16.0;

/// Below this the disc is a point: the texel under its centre.
inline constexpr double kPointRadiusPx = 1.0 / 64.0;

/// The largest output or input the plugin will draw: GL_MAX_TEXTURE_SIZE on
/// Apple's GL, and the bound under which the weave's integer phase products
/// cannot overflow (Shaders.cpp).
inline constexpr int kMaxSize = 16384;

/// What the parameters ask for, in physical units, through Controls.
struct Asked
{
	bool linear       = true;
	double opacity    = 1.0;
	double front      = 0.0;///< Front Light
	double fill       = 0.92;
	double back       = 0.0;///< Back Light
	double wash       = 0.0;
	double spill      = 0.5;///< k = s / T
	double audience   = 4.0;///< v = V / T
	double open       = 0.535;///< phi
	double thread[ 3 ] = { 0.8822, 0.8822, 0.8822 };///< the picker, sRGB-encoded
	double meshMm     = 2.5;
	bool drawMesh     = true;
	double widthM     = 9.5;
};

/// The frame's numbers, for the shader and for the harness's diagnostics.
struct Setup
{
	Asked asked;
	double reflect[ 3 ] = {};///< rho: the picker decoded (Linear Light) or as picked
	double frontGain    = 0;///< Opacity x Front Light
	double back         = 0;
	double wash         = 0;
	double spillGain    = 0;///< phi x Opacity x Front Light / M^2
	double magnification = 1;///< M = 1 + k, the set's
	double apparent      = 1;///< M_app = ( 1 + k ) / ( 1 + k / v ), as the audience sees it
	double invApparent   = 1;///< what the shader maps by: 1 / M_app
	double radiusPx      = 0;///< the defocus disc's radius, output pixels of the projection's frame
	bool radiusCapped    = false;
	double pitchPx       = 0;///< the cell's width, output pixels
	double freq[ 2 ]     = {};///< the weave, cycles per pixel, x and y
	int freqHi[ 2 ]      = {};///< floor( 256 frac( f ) ): the phase's exact part
	double freqLo[ 2 ]   = {};///< frac( f ) - freqHi / 256
	double openAxis[ 2 ] = {};///< phi_x, phi_y: the open fraction along each axis
	double aperture      = 1;///< sqrt( Fill ): the mirror's side, pixels
};

/// The frame's numbers from what was asked, at an output `width` pixels wide.
/// `fault` builds a negative control in; `seconds` is the host's clock, read
/// only by kFaultClock (nothing in the shipped plugin moves with time).
Setup Plan( const Asked& asked, int width, int fault, double seconds );

/// The thread fraction of the cell's width, t / W, for open area phi on
/// ShowTex's cell (W x W/2, threads of one width t both ways):
/// phi = ( 1 - t/W )( 1 - 2 t/W ).
double ThreadFraction( double phi );

} // namespace scrim

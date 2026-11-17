#pragma once

/**
    Host parameters are 0..1; these are what they mean.

    Every ranged parameter is a plain FF_TYPE_STANDARD float in 0..1.
    `SetParamInfo` clamps a standard default into 0..1 *before* returning, and
    `SetParamRange` can only be called afterwards, so a parameter declared in
    metres cannot declare a default in metres. The conversions live here, in
    one file the plugin and the harness both use, so there is only ever one
    answer to what a slider position means. The harness reads back through
    these ONLY what a slider's float stands for; every physical constant it
    predicts with is its own.

    Lengths: the gauze image is Gauze Width metres wide and fills the output
    frame (the projector is fed by the composition). Distances along the axis
    are in throws, T, the projector's distance from the gauze.
*/
namespace scrim
{

/// MSVC has no M_PI.
inline constexpr double kPi = 3.14159265358979323846;

/// Front Light, Back Light and Wash: 0 to 16, as 16 p^2. In units of what a
/// white (reflectance 1) diffuser lit by that light shows: Front Light 1 on a
/// white diffuser with Src white is white. The squared slider gives the low
/// levels, where a gauze's reveal happens, most of the travel.
inline constexpr double kLightMax = 16.0;

/// Front Light's and Back Light's defaults: set from Resolume's demo clips
/// through `smtest --pipe` (tools/clips.py defaults; AGENTS.md, "Defaults"):
/// at the default, the median demo clip's 99th-percentile brightest output
/// channel reaches the top code, projected on the gauze with nothing behind
/// (Front Light), and seen through the gauze with nothing projected (Back
/// Light), at the default gauze, 640x360.
inline constexpr double kFrontDefault = 2.9171;
inline constexpr double kBackDefault  = 1.8929;

/// Pixel Fill: the projector's mirror fill factor, 0.5 to 1, linear. TI
/// DLP7000 data sheet (DLPS026G, revised 2023-04): "Array fill factor 92%".
inline constexpr double kFillMin     = 0.5;
inline constexpr double kFillDefault = 0.92;

/// Spill Distance: k = s / T, how far behind the gauze the set stands, in
/// throws. 0 to 3, linear.
inline constexpr double kSpillMax     = 3.0;
inline constexpr double kSpillDefault = 0.5;

/// Audience: v = V / T, how far in front of the gauze the audience sits, in
/// throws, 0.25 to 16 as 0.25 x 64^p. At v = 1 the audience sees from the
/// projector's distance and the spill hides behind its own image.
inline constexpr double kAudienceMin     = 0.25;
inline constexpr double kAudienceRange   = 64.0;
inline constexpr double kAudienceDefault = 4.0;

/// Open Area: phi, the holes' share of the gauze, 0 to 1, linear. ShowTex
/// GobelinTulle TDS 3420 (10/2026): "Mesh openness factor of 52%-55%".
inline constexpr double kOpenDefault = 0.535;

/// Thread: the threads' reflectance as an sRGB-encoded picker. ShowTex's
/// reflection luminance factor for white GobelinTulle, 0.35 at 0 degrees, is
/// rho (1 - phi) over a dark ground (an assumption: the sheet does not say),
/// so rho = 0.35 / 0.465 = 0.7527, encoded 0.8822.
inline constexpr double kThreadDefault = 0.8822;

/// Mesh: the cell's width, 0.5 to 10 mm as 0.5 x 20^p; its height is half.
/// ShowTex GobelinTulle: "Mesh size: H 1.25 mm x W 2.5 mm".
inline constexpr double kMeshMinMm     = 0.5;
inline constexpr double kMeshRange     = 20.0;
inline constexpr double kMeshDefaultMm = 2.5;

/// Gauze Width: the gauze image's width, 2 to 20 m as 2 x 10^p. ShowTex sells
/// GobelinTulle 950 cm wide.
inline constexpr double kWidthMinM     = 2.0;
inline constexpr double kWidthRange    = 10.0;
inline constexpr double kWidthDefaultM = 9.5;

double LightFromParam( float value );
float ParamForLight( double light );

double FillFromParam( float value );
float ParamForFill( double fill );

double SpillFromParam( float value );///< k = s / T
float ParamForSpill( double k );

double AudienceFromParam( float value );///< v = V / T
float ParamForAudience( double v );

double MeshFromParam( float value );///< mm
float ParamForMesh( double mm );

double WidthFromParam( float value );///< m
float ParamForWidth( double metres );

/// IEC 61966-2-1:1999 (amended 2003), in double, for the CPU's half: the
/// Thread picker's decode.
double DecodeSrgb( double c );
double EncodeSrgb( double x );

} // namespace scrim

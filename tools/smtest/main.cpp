/**
    smtest -- render SW Scrim offline, and measure the gauze.

    The mixer gets TWO inputs: `inputTextures[0]` is Dest, the layer below --
    the scene behind the gauze (`--input-a`); `[1]` is Src, this layer -- what
    the projector sends (`--input-b`). They may be different sizes, with
    different hardware padding, rendered to a third.

        smtest --out /tmp/f.png      a picture, through the real plugin
        smtest --list                every parameter, for the sweep
        smtest --names               names, index 0, Opacity, the defaults against their sources
        smtest --mixer               the guards; two inputs, two sizes; the padding never shows; GL state back
        smtest --closed              flat inputs: out = rho(1-phi)(Op FL Src + Wash) + phi Dest (Back + phi Op FL Src / M^2)
        smtest --unlit               nothing behind lit gives the gauze image unchanged; a dark backstage stays invisible
        smtest --superpose           light adds: all three lights = the sum of each alone, in linear light
        smtest --spill               the spill's size (M_app), brightness (phi^2 / M^2) and blur (the circle of confusion)
        smtest --moire               the beat between the weave and the projector's pixels: frequency, fill factor, mean
        smtest --alpha               straight alpha honoured on both inputs; the output opaque
        smtest --state               no state and no clock: frames, clocks, a resize mid-run, a fresh instance
        smtest --mutation            one character of the shipped GLSL fails --closed
        smtest --bench               ms/frame at 720p and 1080p
        smtest --pipe                raw frames in, raw frames out (two inputs)

    SMTEST_RENDERER=software renders on Apple's software renderer instead of
    the GPU.

    Every check renders through the REAL plugin class in a headless CGL
    context and measures the property out of the picture, at 1280x720 and at
    320x180, and carries negative controls that build the wrong answer into
    the real plugin (`SetFaultForTest`) and show the check rejects it. The
    predictions come from the harness's own constants (ShowTex's, Panasonic's,
    TI's, below) and its own sRGB pair; from the plugin it reads back only what
    a slider's float stands for (Controls). Every tolerance is derived from
    something stated, and AGENTS.md lists them.
*/

#include "Harness.h"

namespace
{

//---------------------------------------------------------------------------
// The sources' numbers, written here and NOT read from the plugin.
//---------------------------------------------------------------------------
namespace source
{
/// ShowTex GobelinTulle, TDS 3420 (10/2026): "Mesh openness factor of
/// 52%-55%"; reflection luminance factor, white, 0 degrees: 0.35 (chart);
/// "Mesh size: H 1.25 mm x W 2.5 mm"; 950 cm wide.
constexpr double kOpenLow    = 0.52;
constexpr double kOpenHigh   = 0.55;
constexpr double kWhiteBeta  = 0.35;
constexpr double kMeshWideMm = 2.5;
constexpr double kMeshTallMm = 1.25;
constexpr double kWidthM     = 9.5;
/// Panasonic PT-RZ120, standard zoom, wide end: f 25.6 mm, F1.7.
constexpr double kFocalMm = 25.6;
constexpr double kFNumber = 1.7;
/// TI DLP7000 (DLPS026G): "Array fill factor 92%".
constexpr double kFill = 0.92;
} // namespace source

double pupilM()
{
	return source::kFocalMm / source::kFNumber / 1000.0;
}

struct Raster
{
	int w, h;
};
constexpr Raster kRasters[ 2 ] = { { 1280, 720 }, { 320, 180 } };

/// Run `check( W, H, fault )` at both rasters, clean, then with each fault
/// that must fail it.
template< typename F >
void atBothRasters( F&& check, std::initializer_list< std::pair< int, const char* > > faults )
{
	for( const Raster& r : kRasters )
	{
		std::printf( "\n %dx%d\n", r.w, r.h );
		const int before = measured;
		check( r.w, r.h, kFaultNone );
		//A check that asserted nothing measured nothing: that is a failure.
		const int asserted = measured - before;
		Check( asserted > 0, fmt( "%.0f assertions measured at this raster", asserted ) );
		for( const auto& f : faults )
		{
			const int caught = failuresOf( [ & ] { check( r.w, r.h, f.first ); } );
			Negative( caught > 0, std::string( f.second ) + fmt( " fails %.0f assertion(s)", caught ) );
		}
	}
}

double sinc( double x )
{
	return x == 0.0 ? 1.0 : std::sin( kPi * x ) / ( kPi * x );
}

/// The DFT's magnitude at bin k, in double, the angle reduced exactly.
double dftMagnitude( const std::vector< double >& x, int k )
{
	const long n = static_cast< long >( x.size() );
	double re = 0.0, im = 0.0;
	for( long i = 0; i < n; ++i )
	{
		const double angle = 2.0 * kPi * static_cast< double >( ( static_cast< long >( k ) * i ) % n ) / static_cast< double >( n );
		re += x[ static_cast< size_t >( i ) ] * std::cos( angle );
		im -= x[ static_cast< size_t >( i ) ] * std::sin( angle );
	}
	return std::hypot( re, im );
}

/// The bin with the largest magnitude, DC excluded.
int dftPeak( const std::vector< double >& x )
{
	int best        = 1;
	double strongest = -1.0;
	for( int k = 1; k <= static_cast< int >( x.size() ) / 2; ++k )
	{
		const double m = dftMagnitude( x, k );
		if( m > strongest )
		{
			strongest = m;
			best      = k;
		}
	}
	return best;
}

//---------------------------------------------------------------------------
// Settings: what the harness asks for, and what the floats stand for.
//---------------------------------------------------------------------------
struct Gauze
{
	bool linear        = true;
	double opacity     = 1.0;
	double front       = 1.0;
	double back        = 0.0;
	double wash        = 0.0;
	double fill        = source::kFill;
	double spill       = 0.5;
	double audience    = 4.0;
	double open        = 0.535;
	double thread[ 3 ] = { 1.0, 1.0, 1.0 };
	double meshMm      = source::kMeshWideMm;
	bool drawMesh      = false;
	double widthM      = source::kWidthM;
};

/// Set every parameter of the rig's plugin, and return what the floats it
/// was given stand for: the values the predictions are made with.
Gauze apply( Scrim& p, const Gauze& g )
{
	Gauze a = g;
	auto set = [ & ]( unsigned id, float value ) {
		p.SetFloatParameter( id, value );
		return value;
	};
	a.linear   = set( Scrim::PT_LINEAR_LIGHT, g.linear ? 1.0f : 0.0f ) > 0.5f;
	a.opacity  = std::clamp( static_cast< double >( set( Scrim::PT_OPACITY, static_cast< float >( g.opacity ) ) ), 0.0, 1.0 );
	a.front    = LightFromParam( set( Scrim::PT_FRONT_LIGHT, ParamForLight( g.front ) ) );
	a.fill     = FillFromParam( set( Scrim::PT_PIXEL_FILL, ParamForFill( g.fill ) ) );
	a.back     = LightFromParam( set( Scrim::PT_BACK_LIGHT, ParamForLight( g.back ) ) );
	a.wash     = LightFromParam( set( Scrim::PT_WASH, ParamForLight( g.wash ) ) );
	a.spill    = SpillFromParam( set( Scrim::PT_SPILL_DISTANCE, ParamForSpill( g.spill ) ) );
	a.audience = AudienceFromParam( set( Scrim::PT_AUDIENCE, ParamForAudience( g.audience ) ) );
	a.open     = std::clamp( static_cast< double >( set( Scrim::PT_OPEN_AREA, static_cast< float >( g.open ) ) ), 0.0, 1.0 );
	for( int c = 0; c < 3; ++c )
		a.thread[ c ] = set( Scrim::PT_THREAD_RED + static_cast< unsigned >( c ), static_cast< float >( g.thread[ c ] ) );
	a.meshMm   = MeshFromParam( set( Scrim::PT_MESH, ParamForMesh( g.meshMm ) ) );
	a.drawMesh = set( Scrim::PT_DRAW_MESH, g.drawMesh ? 1.0f : 0.0f ) > 0.5f;
	a.widthM   = WidthFromParam( set( Scrim::PT_GAUZE_WIDTH, ParamForWidth( g.widthM ) ) );
	return a;
}

/// The spill's apparent magnification, from similar triangles (SPEC-scrim.md).
double apparentOf( const Gauze& a )
{
	return ( 1.0 + a.spill ) / ( 1.0 + a.spill / a.audience );
}

/// The defocus disc's radius in output pixels of the projection's frame:
/// the circle of confusion C = A s / ( T + s ), halved.
double radiusOf( const Gauze& a, int width )
{
	return pupilM() * a.spill / ( 2.0 * ( 1.0 + a.spill ) ) / a.widthM * width;
}

/// Texels a disc gather of radius R sums at most (for the float budgets).
double gathered( double R )
{
	if( R < 1.0 / 64.0 )
		return 1.0;
	const double side = 2.0 * std::ceil( R ) + 3.0;
	return side * side;
}

//---------------------------------------------------------------------------
// --list, --names
//---------------------------------------------------------------------------
int runList()
{
	Scrim plugin;
	std::printf( "%-4s %-22s %-9s %10s   %-16s %s\n", "id", "name", "kind", "value", "range", "elements" );
	for( unsigned int id = 0; id < Scrim::PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( id >= Scrim::PT_ABOUT_FIRST )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s %s\n", id, name ? name : "", "about", "-", "-", "the Stoatworks About block; not swept" );
			continue;
		}
		const char* kind = "standard";
		switch( plugin.GetParamType( id ) )
		{
		case FF_TYPE_BOOLEAN: kind = "boolean"; break;
		case FF_TYPE_EVENT: kind = "event"; break;
		case FF_TYPE_INTEGER: kind = "integer"; break;
		case FF_TYPE_OPTION: kind = "option"; break;
		case FF_TYPE_RED: kind = "red"; break;
		case FF_TYPE_GREEN: kind = "green"; break;
		case FF_TYPE_BLUE: kind = "blue"; break;
		case FF_TYPE_TEXT: kind = "text"; break;
		default: break;
		}
		RangeStruct range = plugin.GetParamRange( id );
		std::string elements;
		if( plugin.GetParamType( id ) == FF_TYPE_OPTION )
		{
			const unsigned n = std::max( 1u, plugin.GetNumParamElements( id ) );
			range.min        = 0.0f;
			range.max        = static_cast< float >( n ) - 1.0f;
			for( unsigned e = 0; e < n; ++e )
				elements += std::string( e ? " | " : "" ) + plugin.GetParamElementName( id, e );
		}
		char rangeText[ 32 ] = {};
		std::snprintf( rangeText, sizeof( rangeText ), "[%g .. %g]", range.min, range.max );
		std::printf( "%-4u %-22s %-9s %10.4f   %-16s %s\n", id, name ? name : "", kind, plugin.GetFloatParameter( id ), rangeText, elements.c_str() );
	}
	return 0;
}

int runNames()
{
	std::printf( "names: sixteen bytes at most, none twice, the mixer's two load-bearing ones, the defaults' sources\n\n" );
	Scrim mixer;
	int over = 0;
	std::vector< std::string > seen;
	for( unsigned int id = 0; id < Scrim::PT_COUNT; ++id )
	{
		const char* name = mixer.GetParamName( id );
		if( name == nullptr )
			continue;
		if( std::strlen( name ) > 16 || std::find( seen.begin(), seen.end(), std::string( name ) ) != seen.end() )
		{
			std::printf( "  %-3u  %-28s %zu bytes%s\n", id, name, std::strlen( name ), std::strlen( name ) > 16 ? "" : ", duplicate" );
			++over;
		}
		seen.push_back( name );
	}
	Check( over == 0 && seen.size() == Scrim::PT_COUNT, fmt( "%.0f parameter names over 16 bytes or duplicated, of %.0f", over, Scrim::PT_COUNT ) );

	const std::string id = names::kMixerId, shown = names::kMixerName;
	Check( id == "SM01" && shown == "SW Scrim" && shown.size() <= 16, "the plugin is " + shown + fmt( " (%.0f bytes), id ", shown.size() ) + id );

	//Index 0 is sacrificial: Arena does not expose a mixer's first parameter
	//(genlock, wipe, relay, lenticular, pepper), so its default must be right for ever.
	const char* first = mixer.GetParamName( 0 );
	Check( first != nullptr && std::strcmp( first, "Linear Light" ) == 0 && mixer.GetParamType( 0 ) == FF_TYPE_BOOLEAN && mixer.GetFloatParameter( 0 ) == 1.0f,
	       std::string( "parameter 0, which Arena hides, is " ) + ( first ? first : "(none)" ) + " (boolean, default on)" );
	const char* opacity = mixer.GetParamName( Scrim::PT_OPACITY );
	Check( opacity != nullptr && std::strcmp( opacity, "Opacity" ) == 0 && mixer.GetParamType( Scrim::PT_OPACITY ) == FF_TYPE_STANDARD
	           && mixer.GetFloatParameter( Scrim::PT_OPACITY ) == 1.0f,
	       "the projector's level is a standard parameter named Opacity (Arena binds it to the layer fader), default 1" );
	Check( mixer.GetParamType( Scrim::PT_THREAD_RED ) == FF_TYPE_RED && mixer.GetParamType( Scrim::PT_THREAD_GREEN ) == FF_TYPE_GREEN
	           && mixer.GetParamType( Scrim::PT_THREAD_BLUE ) == FF_TYPE_BLUE,
	       "Thread is a RED, GREEN, BLUE triplet (one colour picker)" );
	Check( mixer.GetParamType( Scrim::PT_DRAW_MESH ) == FF_TYPE_BOOLEAN && mixer.GetFloatParameter( Scrim::PT_DRAW_MESH ) == 1.0f,
	       "Draw Mesh is a boolean, on" );
	const char* about = mixer.GetParamName( Scrim::PT_ABOUT_FIRST );
	Check( about != nullptr && std::strcmp( about, "About" ) == 0 && Scrim::PT_COUNT - Scrim::PT_ABOUT_FIRST == stoatworks::about::kParamCount,
	       "the About block is last" );

	//The mappings invert.
	bool inverts = true;
	for( double x : { 0.0, 0.5, 2.9, 16.0 } )
		inverts = inverts && std::fabs( LightFromParam( ParamForLight( x ) ) - x ) < 1e-5 * std::max( 1.0, x );
	for( double x : { 0.5, 0.92, 1.0 } )
		inverts = inverts && std::fabs( FillFromParam( ParamForFill( x ) ) - x ) < 1e-6;
	for( double x : { 0.0, 0.5, 3.0 } )
		inverts = inverts && std::fabs( SpillFromParam( ParamForSpill( x ) ) - x ) < 1e-6;
	for( double x : { 0.25, 1.0, 4.0, 16.0 } )
		inverts = inverts && std::fabs( AudienceFromParam( ParamForAudience( x ) ) / x - 1.0 ) < 1e-6;
	for( double x : { 0.5, 2.5, 10.0 } )
		inverts = inverts && std::fabs( MeshFromParam( ParamForMesh( x ) ) / x - 1.0 ) < 1e-6;
	for( double x : { 2.0, 9.5, 20.0 } )
		inverts = inverts && std::fabs( WidthFromParam( ParamForWidth( x ) ) / x - 1.0 ) < 1e-6;
	Check( inverts, "Front/Back Light, Wash, Pixel Fill, Spill Distance, Audience, Mesh and Gauze Width invert to their values (float's 1e-6)" );

	//The defaults against the sources, read off the declared floats.
	const double open   = mixer.GetFloatParameter( Scrim::PT_OPEN_AREA );
	const double thread = mixer.GetFloatParameter( Scrim::PT_THREAD_RED );
	const double fill   = FillFromParam( mixer.GetFloatParameter( Scrim::PT_PIXEL_FILL ) );
	const double mesh   = MeshFromParam( mixer.GetFloatParameter( Scrim::PT_MESH ) );
	const double width  = WidthFromParam( mixer.GetFloatParameter( Scrim::PT_GAUZE_WIDTH ) );
	const double rho    = source::kWhiteBeta / ( 1.0 - open );
	Check( std::fabs( open - 0.5 * ( source::kOpenLow + source::kOpenHigh ) ) < 1e-6,
	       fmt( "Open Area defaults to %.4f, the middle of ShowTex's 52-55%%", open ) );
	//The picker is written to four decimals: 5e-5 encoded, times decode's
	//slope there (2.4 / 1.055 x 0.89^1.4 = 1.93) is 1e-4 of reflectance.
	Check( std::fabs( srgbToLinear( thread ) - rho ) < 1e-4,
	       fmt( "Thread defaults to %.4f, which decodes to %.5f = ShowTex white's 0.35 / ( 1 - %.3f ) = %.5f (tolerance 1e-4: the picker's four decimals)",
	            thread, srgbToLinear( thread ), open, rho ) );
	Check( std::fabs( fill - source::kFill ) < 1e-6, fmt( "Pixel Fill defaults to %.4f, TI DLP7000's 92%%", fill ) );
	Check( std::fabs( mesh - source::kMeshWideMm ) < 1e-5 && std::fabs( width - source::kWidthM ) < 1e-5,
	       fmt( "Mesh defaults to %.4f mm (ShowTex W 2.5 mm), Gauze Width to %.4f m (ShowTex's 950 cm)", mesh, width ) );
	//The cell's shape: ShowTex's H over W.
	Check( std::fabs( source::kMeshTallMm / source::kMeshWideMm - 0.5 ) < 1e-12, "ShowTex's cell is half as tall as it is wide, as the plugin draws it" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --mixer: the guards; two inputs of two sizes with two paddings; neither
// padding shows; the host's GL state comes back.
//---------------------------------------------------------------------------
/// Every pixel against its quadrant's colour, two input texels clear of the
/// quadrant edges (the bilinear's reach is one).
void quadrantsMatch( const Image& seen, int outW, int outH, const InputSpec& spec, const QuadCard& card, const char* what )
{
	int sentinel = 0, checked = 0, worst = 0;
	for( int y = 0; y < outH; ++y )
		for( int x = 0; x < outW; ++x )
		{
			const Rgb got = pixelAt( seen, outW, x, y );
			if( l1( got, kSentinel ) < kSentinelNear )
				++sentinel;
			const double ux = ( x + 0.5 ) / outW * spec.usedW, vy = ( y + 0.5 ) / outH * spec.usedH;
			if( std::fabs( ux - spec.usedW / 2 ) < 2.0 || std::fabs( vy - spec.usedH / 2 ) < 2.0 )
				continue;
			const Rgb want = card.quadrant[ ( vy >= spec.usedH / 2 ? 2 : 0 ) + ( ux >= spec.usedW / 2 ? 1 : 0 ) ];
			worst          = std::max( { worst, std::abs( got.r - want.r ), std::abs( got.g - want.g ), std::abs( got.b - want.b ) } );
			++checked;
		}
	Check( sentinel == 0, fmt( "no padding reached the picture (%.0f sentinel pixels)", sentinel ) + " -- " + what );
	Check( checked > outW * outH / 2 && worst <= 1,
	       fmt( "each quadrant is its own flat colour, frame edges included (%.0f pixels, worst %.0f codes, tolerance 1: the bilinear's weights summed in float)", checked,
	            worst )
	           + " -- " + what );
}

void mixerCheck( int outW, int outH, int fault )
{
	const InputSpec dest = InputSpec::Padded( 200, 120, 256, 256 );
	const InputSpec src  = InputSpec::Padded( 96, 70, 128, 128 );
	const QuadCard stage = quadCard( dest.usedW, dest.usedH );
	const QuadCard shown = quadCard( src.usedW, src.usedH, true );

	MixRig rig;
	if( !rig.Init( outW, outH, dest, src, false, fault ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	if( !quiet )
		std::printf( "  out %dx%d; Dest %dx%d of %dx%d; Src %dx%d of %dx%d (both padded with opaque magenta)\n", outW, outH, dest.usedW, dest.usedH,
		             dest.hwW, dest.hwH, src.usedW, src.usedH, src.hwW, src.hwH );
	rig.UploadDest( stage.image );
	rig.UploadSrc( shown.image );

	//The scene alone, at unit gain: nothing projected, phi Back = 0.25 x 4 = 1
	//exactly, Linear Light off -- the output is Dest, resampled.
	Gauze g;
	g.linear   = false;
	g.drawMesh = false;
	g.opacity  = 0.0;
	g.open     = 0.25;
	g.back     = 4.0;
	apply( rig.plugin, g );
	rig.Render();
	quadrantsMatch( rig.Pixels(), outW, outH, dest, stage, "Dest alone" );

	//The projection alone, at unit gain: rho ( 1 - phi ) Front = 1 x 0.25 x 4.
	g.opacity = 1.0;
	g.open    = 0.75;
	g.front   = 4.0;
	g.back    = 0.0;
	apply( rig.plugin, g );
	rig.UploadDest( flatField( dest.usedW, dest.usedH, kBlack ) );
	rig.Render();
	quadrantsMatch( rig.Pixels(), outW, outH, src, shown, "Src alone" );

	//The resampling itself: a bilinear of a linear ramp is the ramp, so a
	//float ramp across (red) and down (green) on each input, resized, comes
	//back as ( x + 0.5 ) / outW and ( y + 0.5 ) / outH exactly, inside the
	//half texel at each end where the edge clamps.
	const InputSpec rampSpecs[ 2 ] = { InputSpec::Padded( dest.usedW, dest.usedH, dest.hwW, dest.hwH, true ),
		                               InputSpec::Padded( src.usedW, src.usedH, src.hwW, src.hwH, true ) };
	MixRig ramp;
	if( !ramp.Init( outW, outH, rampSpecs[ 0 ], rampSpecs[ 1 ], true, fault ) )
	{
		Check( false, "the ramp rig initialises" );
		return;
	}
	auto rampCard = []( const InputSpec& sp ) {
		ImageF img( static_cast< size_t >( sp.usedW ) * sp.usedH * 4 );
		for( int y = 0; y < sp.usedH; ++y )
			for( int x = 0; x < sp.usedW; ++x )
			{
				const size_t i = at4( sp.usedW, x, y );
				img[ i + 0 ]   = static_cast< float >( ( x + 0.5 ) / sp.usedW );
				img[ i + 1 ]   = static_cast< float >( ( y + 0.5 ) / sp.usedH );
				img[ i + 2 ]   = 0.5f;
				img[ i + 3 ]   = 1.0f;
			}
		return img;
	};
	const ImageF black = flatFieldF( outW, outH, 0, 0, 0, 1 );
	for( int which = 0; which < 2; ++which )
	{
		const InputSpec& sp = rampSpecs[ which ];
		Gauze r;
		r.linear   = false;
		r.drawMesh = false;
		if( which == 0 )
		{
			r.opacity = 0.0;
			r.open    = 0.25;
			r.back    = 4.0;
			ramp.UploadDest( rampCard( sp ) );
			ramp.UploadSrc( flatFieldF( sp.usedW, sp.usedH, 0, 0, 0, 1 ) );
		}
		else
		{
			r.open  = 0.75;
			r.front = 4.0;
			r.back  = 0.0;
			ramp.UploadSrc( rampCard( rampSpecs[ 1 ] ) );
			ramp.UploadDest( flatFieldF( rampSpecs[ 0 ].usedW, rampSpecs[ 0 ].usedH, 0, 0, 0, 1 ) );
		}
		apply( ramp.plugin, r );
		ramp.Render();
		const ImageF out = ramp.PixelsF();
		double worst     = 0.0;
		long inside      = 0;
		for( int y = 0; y < outH; ++y )
			for( int x = 0; x < outW; ++x )
			{
				const double u = ( x + 0.5 ) * sp.usedW / outW, v = ( y + 0.5 ) * sp.usedH / outH;
				if( u < 0.5 || u > sp.usedW - 0.5 || v < 0.5 || v > sp.usedH - 0.5 )
					continue;
				worst = worse( worst, std::fabs( channel( out, outW, x, y, 0 ) - ( x + 0.5 ) / outW ) );
				worst = worse( worst, std::fabs( channel( out, outW, x, y, 1 ) - ( y + 0.5 ) / outH ) );
				++inside;
			}
		//Counted roundings: the texel's value (1), u and its fraction (4),
		//two mixes of two (4), the unit gain's products (exact) -- ten, x3.
		const double tol = 3.0 * 10.0 * kFloatUlp;
		Check( inside > outW * outH / 2 && within( worst, tol ),
		       std::string( which == 0 ? "Dest" : "Src" )
		           + fmt( ", a float ramp at %.0fx%.0f: the bilinear by hand gives the ramp back at %.0f pixels (worst %.3g, ", sp.usedW, sp.usedH, inside, worst )
		           + fmt( "tolerance %.3g: ten counted float roundings, x3)", tol ) );
	}
}

/// The host's GL state as the plugin found it: no texture on any unit it
/// used, unit 0 active, no program, the host's framebuffer, viewport, blend
/// and clear colour.
void stateCheck( int fault )
{
	MixRig rig;
	if( !rig.Init( 320, 180, false, fault ) )
		return;
	rig.UploadDest( stageCard( 320, 180 ) );
	rig.UploadSrc( projectionCard( 320, 180 ) );
	rig.out.Begin();
	glEnable( GL_BLEND );
	glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
	glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
	glViewport( 0, 0, 320, 180 );
	const FFResult result = rig.plugin.ProcessOpenGL( &rig.process );
	GLint active = 0, bound[ 3 ] = { -1, -1, -1 }, program = -1, fbo = -1, viewport[ 4 ] = {}, src = 0, dst = 0;
	GLfloat clearColour[ 4 ] = {};
	glGetIntegerv( GL_ACTIVE_TEXTURE, &active );
	glGetIntegerv( GL_CURRENT_PROGRAM, &program );
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
	glGetIntegerv( GL_VIEWPORT, viewport );
	glGetIntegerv( GL_BLEND_SRC_RGB, &src );
	glGetIntegerv( GL_BLEND_DST_RGB, &dst );
	glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColour );
	const bool blend = glIsEnabled( GL_BLEND );
	for( int u = 0; u < 3; ++u )
	{
		glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound[ u ] );
	}
	glActiveTexture( GL_TEXTURE0 );
	glDisable( GL_BLEND );
	Check( result == FF_SUCCESS, "a frame renders" );
	Check( active == GL_TEXTURE0 && bound[ 0 ] == 0 && bound[ 1 ] == 0 && bound[ 2 ] == 0,
	       fmt( "after a frame, texture units 0, 1 and 2 are empty (%.0f, %.0f, %.0f)", bound[ 0 ], bound[ 1 ], bound[ 2 ] )
	           + fmt( " and unit 0 is active (unit %.0f)", active - GL_TEXTURE0 ) );
	Check( program == 0 && fbo == static_cast< GLint >( rig.out.fbo ) && viewport[ 2 ] == 320 && viewport[ 3 ] == 180,
	       "no program is left in use; the host's framebuffer and viewport are back" );
	Check( blend && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA && clearColour[ 2 ] == 0.3f, "the host's blend and clear colour are back" );
}

int runMixer()
{
	std::printf( "a mixer takes two inputs, of two sizes, with two paddings\n\n" );
	{
		MixRig rig;
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.UploadDest( stageCard( 320, 180 ) );
		rig.UploadSrc( projectionCard( 320, 180 ) );
		Check( rig.RenderBroken( 2, -2 ) == FF_FAIL, "a null input ARRAY returns FF_FAIL" );
		Check( rig.RenderBroken( 0, -1 ) == FF_FAIL, "zero input textures returns FF_FAIL" );
		Check( rig.RenderBroken( 1, -1 ) == FF_FAIL, "one input texture (an effect host's call) returns FF_FAIL" );
		Check( rig.RenderBroken( 2, 0 ) == FF_FAIL, "a null Dest returns FF_FAIL" );
		Check( rig.RenderBroken( 2, 1 ) == FF_FAIL, "a null Src returns FF_FAIL" );
		Check( rig.RenderBroken( 2, -1, true ) == FF_FAIL, "a zero-sized Dest returns FF_FAIL" );
		Check( rig.RenderBroken( 2, -1, false, true ) == FF_FAIL, "a zero viewport returns FF_FAIL" );
		Check( rig.Render(), "and two real inputs still render afterwards" );
	}
	std::printf( "\n" );
	stateCheck( kFaultNone );
	const int leaked = failuresOf( [ & ] { stateCheck( kFaultLeaveBound ); } );
	Negative( leaked > 0, fmt( "a texture left bound on unit 1 fails %.0f assertion(s)", leaked ) );
	atBothRasters( mixerCheck, { { kFaultPadded, "an input read over its hardware size (the padding shows)" },
	                             { kFaultHardwareBilinear, "a resized input through the texture's own filter" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --closed: flat inputs, the mesh off, and the closed form per channel.
//---------------------------------------------------------------------------
struct ClosedCase
{
	double open, thread[ 3 ], front, opacity, back, wash, spill, audience;
};

void closedCheck( int W, int H, int fault, const char* fragment = nullptr )
{
	//Flat inputs as floats, every channel distinct.
	const double s[ 3 ] = { 0.85, 0.45, 0.2 };
	const double d[ 3 ] = { 0.3, 0.6, 0.9 };
	//Audience >= 2 throws: M_app > 1, so the spill reads well inside the frame
	//and a flat projection spills flat everywhere.
	const ClosedCase cases[] = {
		{ 0.535, { 0.8822, 0.8822, 0.8822 }, 1.0, 1.0, 0.0, 0.0, 0.5, 4.0 },
		{ 0.535, { 0.8822, 0.8822, 0.8822 }, 0.0, 1.0, 1.0, 0.0, 0.5, 4.0 },
		{ 0.25, { 0.9, 0.5, 0.2 }, 1.0, 0.6, 0.5, 0.3, 1.5, 2.0 },
		{ 0.9, { 0.3, 0.7, 1.0 }, 2.0, 1.0, 0.3, 0.0, 3.0, 16.0 },
		{ 0.0, { 1.0, 1.0, 1.0 }, 1.0, 1.0, 1.0, 0.1, 0.5, 4.0 },
		{ 1.0, { 1.0, 1.0, 1.0 }, 1.0, 1.0, 0.6, 0.5, 0.5, 4.0 },
		{ 0.535, { 0.6, 0.6, 0.6 }, 0.5, 0.3, 0.2, 0.1, 0.0, 4.0 },
		{ 0.4, { 0.8, 0.2, 0.5 }, 0.25, 1.0, 0.25, 0.25, 2.0, 2.0 },
	};
	MixRig rig;
	if( !rig.Init( W, H, true, fault, fragment ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	rig.UploadSrc( flatFieldF( W, H, static_cast< float >( s[ 0 ] ), static_cast< float >( s[ 1 ] ), static_cast< float >( s[ 2 ] ) ) );
	rig.UploadDest( flatFieldF( W, H, static_cast< float >( d[ 0 ] ), static_cast< float >( d[ 1 ] ), static_cast< float >( d[ 2 ] ) ) );
	for( int pass = 0; pass < 2; ++pass )
	{
		const bool linear = pass == 0;
		double worst = 0.0, worstTol = 0.0, ratio = 0.0, highest = 0.0;
		for( const ClosedCase& c : cases )
		{
			Gauze g;
			g.linear   = linear;
			g.drawMesh = false;
			g.open     = c.open;
			g.front    = c.front;
			g.opacity  = c.opacity;
			g.back     = c.back;
			g.wash     = c.wash;
			g.spill    = c.spill;
			g.audience = c.audience;
			for( int ch = 0; ch < 3; ++ch )
				g.thread[ ch ] = c.thread[ ch ];
			const Gauze a = apply( rig.plugin, g );
			rig.Render();
			const ImageF out = rig.PixelsF();
			const double M   = 1.0 + a.spill;
			const double n   = gathered( radiusOf( a, W ) );
			//Counted roundings: six uniforms rounded to float, about twelve
			//operations, and the gather's two n-term sums (AGENTS.md).
			const double tol = linear ? 3.0 * ( kPowBound + 0.44 * 2.0 * n * kFloatUlp ) : 3.0 * ( 18.0 + 2.0 * n ) * kFloatUlp;
			for( int ch = 0; ch < 3; ++ch )
			{
				const double S   = linear ? srgbToLinear( static_cast< float >( s[ ch ] ) ) : static_cast< float >( s[ ch ] );
				const double D   = linear ? srgbToLinear( static_cast< float >( d[ ch ] ) ) : static_cast< float >( d[ ch ] );
				const double rho = linear ? srgbToLinear( a.thread[ ch ] ) : a.thread[ ch ];
				const double phi = a.open;
				const double L   = rho * ( 1.0 - phi ) * ( a.opacity * a.front * S + a.wash ) + phi * D * ( a.back + phi * a.opacity * a.front * S / ( M * M ) );
				highest          = std::max( highest, L );
				const double want = linear ? linearToSrgb( std::min( L, 1.0 ) ) : std::min( L, 1.0 );
				for( int y = 0; y < H; ++y )
					for( int x = 0; x < W; ++x )
					{
						const double e = std::fabs( channel( out, W, x, y, ch ) - want );
						worst          = worse( worst, e );
						ratio          = worse( ratio, e / tol );
					}
			}
			worstTol = std::max( worstTol, tol );
		}
		Check( highest < 1.0, fmt( "the grid stays under the top code (highest %.4f): nothing clips, so the form is tested whole", highest ) );
		Check( within( ratio, 1.0 ), std::string( linear ? "Linear Light on: " : "Linear Light off: " )
		                                 + fmt( "8 settings x 3 channels x every pixel within the closed form (worst %.3g, %.2f of its tolerance; largest tolerance %.3g: ",
		                                        worst, ratio, worstTol )
		                                 + ( linear ? "the sRGB pair's budget plus the gather's sums, x3)" : "counted float roundings, x3)" ) );
	}
}

int runClosed()
{
	std::printf( "flat inputs, the mesh off: out = rho (1 - phi)(Op FL Src + Wash) + phi Dest (Back + phi Op FL Src / M^2)\n" );
	atBothRasters( []( int W, int H, int f ) { closedCheck( W, H, f ); },
	               { { kFaultOpacity, "the threads' reflectance ignored (the gauze an opacity)" },
	                 { kFaultSwapArea, "the holes reflecting and the threads passing" },
	                 { kFaultGammaSum, "the reflected and passed light summed encoded" },
	                 { kFaultNoSpread, "the spill not spread over M^2" },
	                 { kFaultBackAlways, "a floor of back light" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --unlit: nothing behind lit gives the gauze image unchanged; a dark
// backstage stays invisible, whatever the open area.
//---------------------------------------------------------------------------
void unlitCheck( int W, int H, int fault )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	const ImageF src         = randomStraight( W, H, 21, true );
	const ImageF black       = flatFieldF( W, H, 0, 0, 0, 1 );
	const ImageF clearWhite  = flatFieldF( W, H, 1, 1, 1, 0 );
	const ImageF stage       = toFloat( stageCard( W, H ) );
	const ImageF projection  = toFloat( projectionCard( W, H ) );

	//1. Unit gain: rho ( 1 - phi ) Op Front = 1 x 0.25 x 1 x 4, all exact.
	//Nothing behind (Dest black, or transparent over white): the output is
	//Src, bit for bit with Linear Light off -- whatever the Back Light and
	//the spill, which have nothing to light.
	Gauze g;
	g.linear   = false;
	g.drawMesh = false;
	g.open     = 0.75;
	g.front    = 4.0;
	g.back     = 9.0;
	g.spill    = 3.0;
	g.audience = 16.0;
	apply( rig.plugin, g );
	rig.UploadSrc( src );
	size_t differ = 0, alphaOff = 0;
	for( const ImageF* behind : { &black, &clearWhite } )
	{
		rig.UploadDest( *behind );
		rig.Render();
		const ImageF out = rig.PixelsF();
		for( size_t i = 0; i < out.size(); i += 4 )
		{
			for( int c = 0; c < 3; ++c )
				differ += std::memcmp( &out[ i + static_cast< size_t >( c ) ], &src[ i + static_cast< size_t >( c ) ], sizeof( float ) ) != 0;
			alphaOff += out[ i + 3 ] != 1.0f;
		}
	}
	Check( differ == 0 && alphaOff == 0,
	       fmt( "nothing behind lit (Dest black, and transparent over white; Back Light 9, Spill Distance 3): the output IS Src, %.0f of %.0f floats differ, alpha 1 (tolerance 0 bits)",
	            differ, 2.0 * W * H * 3 ) );
	//With Linear Light on the gauze decodes and re-encodes: Src within the
	//sRGB pair's budget.
	g.linear = true;
	apply( rig.plugin, g );
	rig.UploadDest( black );
	rig.Render();
	{
		const ImageF out = rig.PixelsF();
		double worst     = 0.0;
		for( size_t i = 0; i < out.size(); i += 4 )
			for( size_t c = 0; c < 3; ++c )
				worst = worse( worst, std::fabs( out[ i + c ] - src[ i + c ] ) );
		Check( within( worst, kPowTol ), fmt( "and with Linear Light on, Src within %.3g (worst %.3g: the sRGB pair's budget, x3)", kPowTol, worst ) );
	}

	//2. A dark backstage stays invisible, whatever the open area, the mesh
	//drawn or not. (a) Dest black: Back Light and the spill change no bit.
	//(b) Dest a lit-looking picture, but no light on it (Back Light 0, the
	//projector off) and a wash on the gauze: the same bits as Dest black.
	rig.UploadSrc( projection );
	int cases = 0;
	size_t blackDiffer = 0, unlitDiffer = 0;
	for( const double phi : { 0.0, 0.25, 0.535, 0.9, 1.0 } )
		for( const bool mesh : { false, true } )
		{
			Gauze d;
			d.drawMesh = mesh;
			d.open     = phi;
			d.wash     = 0.5;
			d.front    = 2.0;
			d.back     = 0.0;
			apply( rig.plugin, d );
			rig.UploadDest( black );
			rig.Render();
			const ImageF dark = rig.PixelsF();
			d.back  = 9.0;
			d.spill = 3.0;
			apply( rig.plugin, d );
			rig.Render();
			blackDiffer += differingFloats( dark, rig.PixelsF() );

			d.back    = 0.0;
			d.spill   = 0.5;
			d.opacity = 0.0;
			apply( rig.plugin, d );
			rig.Render();
			const ImageF washOnly = rig.PixelsF();
			rig.UploadDest( stage );
			rig.Render();
			unlitDiffer += differingFloats( washOnly, rig.PixelsF() );
			++cases;
		}
	Check( blackDiffer == 0, fmt( "Dest black: Back Light 0 -> 9 and Spill Distance 0.5 -> 3 change %.0f floats, at %.0f settings (phi 0, 0.25, 0.535, 0.9, 1; mesh off and on; tolerance 0 bits)",
	                              blackDiffer, cases ) );
	Check( unlitDiffer == 0, fmt( "Dest a picture with no light on it (Back Light 0, Opacity 0) under a wash: %.0f floats differ from Dest black, at %.0f settings (tolerance 0 bits)",
	                              unlitDiffer, cases ) );

	//3. And the check can see Dest: light it either way and it shows.
	Gauze lit;
	lit.drawMesh = true;
	lit.front    = 2.0;
	lit.back     = 0.0;
	apply( rig.plugin, lit );
	rig.UploadDest( black );
	rig.Render();
	const ImageF unlitStage = rig.PixelsF();
	rig.UploadDest( stage );
	rig.Render();
	const size_t spillShows = differingFloats( unlitStage, rig.PixelsF() );
	lit.opacity = 0.0;
	lit.back    = 1.0;
	apply( rig.plugin, lit );
	rig.Render();
	const ImageF backLit = rig.PixelsF();
	rig.UploadDest( black );
	rig.Render();
	const size_t backShows = differingFloats( backLit, rig.PixelsF() );
	Check( spillShows > static_cast< size_t >( W * H ) && backShows > static_cast< size_t >( W * H ),
	       fmt( "the converse: the projection's spill lights the set (%.0f floats differ from Dest black), and so does Back Light 1 (%.0f)", spillShows, backShows ) );
}

int runUnlit()
{
	std::printf( "nothing behind lit gives the gauze image unchanged; a dark backstage stays invisible\n" );
	atBothRasters( unlitCheck, { { kFaultBackAlways, "a floor of back light (a dark backstage glows)" },
	                             { kFaultWashSpills, "the wash passing the holes and lighting the set" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --superpose: light adds. The three lights at once are, in linear light,
// the sum of each alone -- with the mesh drawn and the spill on.
//---------------------------------------------------------------------------
void superposeCheck( int W, int H, int fault )
{
	MixRig rig;
	if( !rig.Init( W, H, InputSpec::Exact( W, H ), InputSpec::Exact( W, H ), true, fault ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	rig.UploadDest( stageCard( W, H ) );
	rig.UploadSrc( projectionCard( W, H ) );
	Gauze g;
	g.drawMesh = true;
	g.thread[ 0 ] = 0.95;
	g.thread[ 1 ] = 0.8;
	g.thread[ 2 ] = 0.6;
	auto render = [ & ]( double front, double back, double wash ) {
		Gauze l = g;
		l.front = front;
		l.back  = back;
		l.wash  = wash;
		apply( rig.plugin, l );
		rig.Render();
		return rig.PixelsF();
	};
	const ImageF all = render( 0.8, 0.5, 0.2 );
	const ImageF f   = render( 0.8, 0.0, 0.0 );
	const ImageF b   = render( 0.0, 0.5, 0.0 );
	const ImageF w   = render( 0.0, 0.0, 0.2 );
	double worst = 0.0, highest = 0.0, smallest = 1.0;
	for( size_t i = 0; i < all.size(); i += 4 )
		for( size_t c = 0; c < 3; ++c )
		{
			const double sum = srgbToLinear( f[ i + c ] ) + srgbToLinear( b[ i + c ] ) + srgbToLinear( w[ i + c ] );
			worst            = worse( worst, std::fabs( srgbToLinear( all[ i + c ] ) - sum ) );
			highest          = std::max( highest, static_cast< double >( all[ i + c ] ) );
			smallest         = std::min( { smallest, static_cast< double >( f[ i + c ] ), static_cast< double >( b[ i + c ] ) } );
		}
	//Four renders, each within the budget in linear light.
	const double tol = 3.0 * 4.0 * kLinearBound;
	Check( highest < 1.0, fmt( "nothing clips (brightest output %.4f): the sum is tested whole", highest ) );
	Check( smallest < 0.999, "the lights alone are not flat white (the test sees the picture)" );
	Check( within( worst, tol ),
	       fmt( "Front 0.8 + Back 0.5 + Wash 0.2 = each alone, summed in linear light, every pixel and channel (worst %.3g, tolerance %.3g: four renders' sRGB budgets in linear light, x3)",
	            worst, tol ) );
}

int runSuperpose()
{
	std::printf( "light adds: the lights together are the sum of each alone, in linear light\n" );
	atBothRasters( superposeCheck, { { kFaultGammaSum, "the reflected and passed light summed encoded" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --spill: the projection's light through the holes, on the set.
//---------------------------------------------------------------------------
/// The row rule's area over the disc's (the midpoint of each texel row's
/// overlap), at its worst over 1024 row phases: a tolerance, never a
/// prediction. At least 1 (midpoints over a concave chord); this is how
/// much over.
double rowRuleAreaMax( double R )
{
	double worst = 1.0;
	const int r  = static_cast< int >( std::ceil( R ) ) + 1;
	for( int k = 0; k < 1024; ++k )
	{
		const double o = k / 1024.0;
		double area    = 0.0;
		for( int j = -r; j <= r; ++j )
		{
			const double y0 = std::max( j - o, -R ), y1 = std::min( j + 1 - o, R );
			if( y1 <= y0 )
				continue;
			const double ym = 0.5 * ( y0 + y1 );
			area += ( y1 - y0 ) * 2.0 * std::sqrt( std::max( R * R - ym * ym, 0.0 ) );
		}
		worst = std::max( worst, area / ( kPi * R * R ) );
	}
	return worst;
}

struct Profile
{
	const ImageF* out;
	int W, row;
	double at( int x ) const
	{
		return srgbToLinear( channel( *out, W, x, row, 0 ) );
	}
};

/// The half-light crossing on the rising (left) or falling (right) edge
/// nearest `from`, linearly between the two samples that straddle it;
/// `slope` gets their difference.
bool crossing( const Profile& p, int from, bool left, double half, double& x, double& slope )
{
	if( left )
	{
		int i = from;
		while( i > 0 && p.at( i ) >= half )
			--i;
		if( i <= 0 )
			return false;
		slope = p.at( i + 1 ) - p.at( i );
		x     = i + 0.5 + ( half - p.at( i ) ) / slope;
		return true;
	}
	int i = from;
	while( i < p.W - 1 && p.at( i ) >= half )
		++i;
	if( i >= p.W - 1 )
		return false;
	slope = p.at( i - 1 ) - p.at( i );
	x     = i - 0.5 + ( p.at( i - 1 ) - half ) / slope;
	return true;
}

void spillCheck( int W, int H, int fault )
{
	const int cx = W / 2, cy = H / 2, wb = W / 16, hb = H / 8;
	ImageF block = flatFieldF( W, H, 0, 0, 0, 1 );
	for( int y = cy - hb; y < cy + hb; ++y )
		for( int x = cx - wb; x < cx + wb; ++x )
			for( int c = 0; c < 3; ++c )
				block[ at4( W, x, y ) + static_cast< size_t >( c ) ] = 1.0f;
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	rig.UploadSrc( block );
	rig.UploadDest( flatFieldF( W, H, 1, 1, 1, 1 ) );

	//Black threads: only the light through the holes, on a white set, seen
	//through the holes. Linear Light on, the mesh off.
	Gauze g;
	g.drawMesh = false;
	g.thread[ 0 ] = g.thread[ 1 ] = g.thread[ 2 ] = 0.0;
	g.back = g.wash = 0.0;
	if( !quiet )
		std::printf( "  a white block %d x %d texels about the axis; the threads black; Dest white; Back Light 0\n", 2 * wb, 2 * hb );

	double worstPos = 0.0, worstPosTol = 0.0, worstLevel = 0.0, worstLevelTol = 0.0;
	int cases = 0;
	bool found = true;
	for( const double k : { 0.5, 1.5, 3.0 } )
		for( const double v : { 1.0, 4.0, 16.0 } )
		{
			g.spill    = k;
			g.audience = v;
			g.front    = std::min( 16.0, 0.6 * ( 1 + k ) * ( 1 + k ) / ( 0.535 * 0.535 ) );
			const Gauze a = apply( rig.plugin, g );
			rig.Render();
			const ImageF out = rig.PixelsF();
			const Profile p{ &out, W, cy };
			const double M = 1.0 + a.spill, Mapp = apparentOf( a );
			//(b) brightness: phi through, spread over M^2, phi back.
			const double plateau = a.open * a.open * a.opacity * a.front / ( M * M );
			const double P0      = p.at( cx );
			const double n       = gathered( radiusOf( a, W ) );
			const double tolB    = 3.0 * ( kLinearBound + plateau * ( 2.0 * n + 10.0 ) * kFloatUlp );
			worstLevel           = worse( worstLevel, std::fabs( P0 - plateau ) / tolB );
			worstLevelTol        = std::max( worstLevelTol, tolB );
			//(a) size: the half-light crossings at M_app times the edges.
			double xl = 0, xr = 0, sl = 0, sr = 0;
			found = crossing( p, cx, true, 0.5 * P0, xl, sl ) && crossing( p, cx, false, 0.5 * P0, xr, sr ) && found;
			const double wantL = cx - wb * Mapp, wantR = cx + wb * Mapp;
			//Half a pixel: the crossing found between two samples of a
			//symmetric edge profile (AGENTS.md), plus the samples' noise over
			//their slope.
			const double tolA = 0.5 + 3.0 * 2.0 * kLinearBound / std::max( 1e-9, std::min( std::fabs( sl ), std::fabs( sr ) ) );
			worstPos          = worse( worstPos, std::max( std::fabs( xl - wantL ), std::fabs( xr - wantR ) ) / tolA );
			worstPosTol       = std::max( worstPosTol, tolA );
			if( !quiet && W == 1280 )
				std::printf( "     k %.1f v %4.1f: M %.3f M_app %.4f; crossings %.3f, %.3f (want %.3f, %.3f); plateau %.6f (want %.6f)\n", a.spill, a.audience, M,
				             Mapp, xl, xr, wantL, wantR, P0, plateau );
			++cases;
		}
	Check( found, "every case's two half-light crossings were found" );
	Check( within( worstPos, 1.0 ),
	       fmt( "size: the spill's edges sit at M_app = (1 + k)/(1 + k/v) times the block's, at %.0f settings (k 0.5, 1.5, 3; v 1, 4, 16), worst %.2f of tolerance (%.3f px: half a pixel, the crossing between two samples, plus noise)",
	            cases, worstPos, worstPosTol ) );
	Check( within( worstLevel, 1.0 ),
	       fmt( "brightness: inside, phi^2 Op FL / (1 + k)^2, worst %.2f of tolerance (largest %.3g: the sRGB budget in linear light, the gather's sums)", worstLevel,
	            worstLevelTol ) );

	//(c) blur: a 2 m gauze and the set 3 throws back, the biggest disc the
	//controls allow; the edge's slope at its half-light point is the disc's
	//chord density there, 2 / ( pi R ).
	double worstBlur = 0.0;
	int blurCases    = 0;
	for( const double v : { 4.0, 16.0 } )
	{
		g.widthM   = 2.0;
		g.spill    = 3.0;
		g.audience = v;
		g.front    = 16.0;
		const Gauze a = apply( rig.plugin, g );
		rig.Render();
		const ImageF out = rig.PixelsF();
		const Profile p{ &out, W, cy };
		const double Mapp = apparentOf( a );
		const double R    = radiusOf( a, W );
		const double P0   = p.at( cx );
		double xl = 0, sl = 0;
		if( !crossing( p, cx, true, 0.5 * P0, xl, sl ) )
		{
			Check( false, "the blurred edge's crossing was found" );
			continue;
		}
		const double slope = sl / P0 * Mapp;//per projection pixel
		const double est   = 2.0 / ( kPi * slope );
		const double gap   = 1.0 / Mapp;//the samples' spacing, projection pixels
		//R <= est <= R x the row rule's area excess / the slope's fall
		//within one sample of the centre (AGENTS.md, "the blur's bound").
		const double fall  = 1.0 - 2.0 * ( R - std::sqrt( std::max( R * R - gap * gap, 0.0 ) ) ) / R;
		const double hi    = rowRuleAreaMax( R ) / fall;
		const double noise = 3.0 * 2.0 * kLinearBound / std::fabs( sl ) + 64.0 * kFloatUlp;
		const bool ok      = gap < R && est >= R * ( 1.0 - noise ) && est <= R * hi * ( 1.0 + noise );
		worstBlur          = std::max( worstBlur, std::fabs( est / R - 1.0 ) );
		Check( ok, fmt( "blur at v %.0f: the edge's slope gives a disc of %.4f px; the circle of confusion A s / (2 (T + s)) is %.4f px", a.audience, est, R )
		               + fmt( " (tolerance [1, %.4f] x R: the row rule's area at its worst over 1024 phases, the slope's fall within a sample of %.3f px)", hi * ( 1.0 + noise ), gap ) );
		++blurCases;
	}
	Check( blurCases == 2, fmt( "%.0f blur settings measured", blurCases ) );
}

int runSpill()
{
	std::printf( "the spill: magnified as the audience sees it, spread over M^2, blurred by the lens's circle of confusion\n" );
	atBothRasters( spillCheck, { { kFaultViewerScale, "the spill at the set's magnification M, not M_app" },
	                             { kFaultNoSpread, "the spill not spread over M^2" },
	                             { kFaultBlurOnSet, "the disc at its size on the set, A s / T" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --moire: the weave against the projector's pixels.
//---------------------------------------------------------------------------
/// Harmonics m >= 2 of a bar pattern of frequency f, seen through a box w
/// pixels wide and sampled at integer pixels, that land within a bin
/// ( 1 / n cycles ) of `target`, each at most 1 / ( pi m ) |sinc( m f w )|,
/// plus the tail past 2^18: a bound on what else lands in the bin.
double aliasBound( double f, double w, double target, int n )
{
	double sum            = 0.0;
	constexpr int kLimit  = 1 << 18;
	const double reach    = 1.0 / n;
	for( int m = 2; m <= kLimit; ++m )
	{
		const double a  = m * f - std::floor( m * f );
		const double up = std::fabs( a - target ), down = std::fabs( 1.0 - a - target );
		if( std::min( { up, down, 1.0 - up } ) <= reach )
			sum += std::fabs( sinc( m * f * w ) ) / ( kPi * m );
	}
	return sum + 1.0 / ( kPi * kPi * f * w * kLimit );
}

void moireCheck( int W, int H, int fault )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
	{
		Check( false, "the rig initialises" );
		return;
	}
	rig.UploadSrc( flatFieldF( W, H, 1, 1, 1, 1 ) );
	const ImageF black = flatFieldF( W, H, 0, 0, 0, 1 );
	const ImageF white = flatFieldF( W, H, 1, 1, 1, 1 );
	//The thread fraction of ShowTex's cell, for |c_1|: the harness's own
	//solution of phi = ( 1 - t )( 1 - 2t ).
	const double phi = static_cast< float >( 0.535 );
	const double t   = ( 3.0 - std::sqrt( 1.0 + 8.0 * phi ) ) / 4.0;
	const double phiX = 1.0 - t, phiY = 1.0 - 2.0 * t;

	//White threads, a white projection at Front Light 1, Linear Light off:
	//the output is 1 - c_ap, the threads' share of each mirror.
	for( const double target : { 1.975, 2.95 } )
	{
		Gauze g;
		g.linear   = false;
		g.drawMesh = true;
		g.front    = 1.0;
		g.widthM   = source::kWidthM * W / 1280.0;
		g.meshMm   = 1000.0 * g.widthM / ( target * W );
		auto render = [ & ]( double fill, const ImageF& behind, double front, double back ) {
			Gauze l = g;
			l.fill  = fill;
			l.front = front;
			l.back  = back;
			l.opacity = front > 0.0 ? 1.0 : 0.0;
			const Gauze a = apply( rig.plugin, l );
			rig.UploadDest( behind );
			rig.Render();
			return std::make_pair( a, rig.PixelsF() );
		};
		const auto [ a, out ] = render( source::kFill, black, 1.0, 0.0 );
		const double f        = a.widthM / ( a.meshMm / 1000.0 * W );
		const double fbx      = std::fabs( f - std::round( f ) );
		const double fby      = std::fabs( 2.0 * f - std::round( 2.0 * f ) );
		const int kx = static_cast< int >( std::lround( fbx * W ) ), ky = static_cast< int >( std::lround( fby * H ) );
		Check( std::fabs( fbx * W - kx ) < 0.01 && std::fabs( fby * H - ky ) < 0.01,
		       fmt( "the test's design: f = %.5f cycles/px fits whole beats in the frame (%.3f across, %.3f down)", f, fbx * W, fby * H ) );
		std::vector< double > across( static_cast< size_t >( W ), 0.0 ), down( static_cast< size_t >( H ), 0.0 );
		double mean = 0.0;
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
			{
				const double v = 1.0 - channel( out, W, x, y, 0 );
				across[ static_cast< size_t >( x ) ] += v / H;
				down[ static_cast< size_t >( y ) ] += v / W;
				mean += v / ( static_cast< double >( W ) * H );
			}
		const int peakX = dftPeak( across ), peakY = dftPeak( down );
		Check( peakX == kx, fmt( "across: the moire's frequency is |f - round f| = %.5f cycles/px (bin %.0f of %.0f); the strongest bin is %.0f (tolerance: the bin)", fbx, kx, W, peakX ) );
		Check( peakY == ky,
		       fmt( "down: the cell half as tall, |2f - round 2f| = %.5f cycles/px (bin %.0f of %.0f); the strongest bin is %.0f", fby, ky, H, peakY ) );

		//The fill factor: through whole-pixel mirrors the beat is weaker by
		//|sinc( f )| / |sinc( sqrt( Fill ) f )|.
		const auto [ a1, out1 ] = render( 1.0, black, 1.0, 0.0 );
		std::vector< double > across1( static_cast< size_t >( W ), 0.0 );
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
				across1[ static_cast< size_t >( x ) ] += ( 1.0 - channel( out1, W, x, y, 0 ) ) / H;
		const double w      = std::sqrt( a.fill );
		const double c1     = std::sin( kPi * phiX ) / kPi;
		const double s1     = std::fabs( sinc( f ) ), sw = std::fabs( sinc( f * w ) );
		const double e1     = aliasBound( f, 1.0, fbx, W ) / c1, ew = aliasBound( f, w, fbx, W ) / c1;
		const double dy1    = aliasBound( 2.0 * f, 1.0, 0.0, H ), dyw = aliasBound( 2.0 * f, w, 0.0, H );
		const double noise  = 2.0 * 64.0 * kFloatUlp * ( f + 2.0 ) / ( f * w ) / ( c1 * sw * phiY );
		const double want   = s1 / sw;
		const double lo     = ( s1 - e1 ) / ( sw + ew ) * ( phiY - dy1 ) / ( phiY + dyw ) * ( 1.0 - noise );
		const double hi     = ( s1 + e1 ) / ( sw - ew ) * ( phiY + dy1 ) / ( phiY - dyw ) * ( 1.0 + noise );
		const double ratio  = dftMagnitude( across1, kx ) / dftMagnitude( across, kx );
		Check( ratio >= lo && ratio <= hi,
		       fmt( "Pixel Fill 1 over %.2f: the beat's amplitude %.4f of itself (want |sinc f| / |sinc(sqrt(Fill) f)| = %.4f; tolerance [%.4f, ", a.fill, ratio, want, lo )
		           + fmt( "%.4f]: harmonics aliasing into the bin, at most 1/(pi m) each)", hi ) );

		//The mean: the holes' share of the frame is phi, reflected and passed.
		const double dx   = aliasBound( f, w, 0.0, W ), dy = aliasBound( 2.0 * f, w, 0.0, H );
		const double tolM = dx + dy + dx * dy + 64.0 * kFloatUlp;
		Check( std::fabs( mean - a.open ) <= tolM,
		       fmt( "the threads' share of the mirrors averages to 1 - phi: holes %.6f of the frame (want %.6f; tolerance %.2g: harmonics aliasing to DC)", mean, a.open, tolM ) );
		const auto [ ab, passed ] = render( source::kFill, white, 0.0, 1.0 );
		double through = 0.0;
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
				through += channel( passed, W, x, y, 0 ) / ( static_cast< double >( W ) * H );
		const double dx1 = aliasBound( f, 1.0, 0.0, W ), dy1b = aliasBound( 2.0 * f, 1.0, 0.0, H );
		const double tolT = dx1 + dy1b + dx1 * dy1b + 64.0 * kFloatUlp;
		Check( std::fabs( through - ab.open ) <= tolT,
		       fmt( "and the light through the holes, seen whole pixels at a time, averages to phi: %.6f (want %.6f; tolerance %.2g)", through, ab.open, tolT ) );
	}
}

int runMoire()
{
	std::printf( "moire: the weave beating against the projector's pixels\n" );
	atBothRasters( moireCheck, { { kFaultBoxAperture, "the reflected light over the whole pixel (no gaps between mirrors)" },
	                             { kFaultSquareCell, "the cell as tall as it is wide" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --alpha: straight alpha in, on both inputs; the output opaque.
//---------------------------------------------------------------------------
void alphaCheck( int W, int H, int fault )
{
	struct Sizes
	{
		const char* name;
		InputSpec dest, src;
	};
	const Sizes sizes[] = {
		{ "at the output's size", InputSpec::Exact( W, H ), InputSpec::Exact( W, H ) },
		{ "resized (the bilinear by hand)", InputSpec::Padded( W * 3 / 4, H * 3 / 4, W, H ), InputSpec::Padded( W * 5 / 8, H * 5 / 8, W * 3 / 4, H * 3 / 4 ) },
	};
	for( const Sizes& sz : sizes )
	{
		MixRig rig;
		if( !rig.Init( W, H, sz.dest, sz.src, true, fault ) )
		{
			Check( false, "the rig initialises" );
			return;
		}
		const InputSpec& d = sz.dest;
		const InputSpec& s = sz.src;
		//The plugin's defaults: Linear Light, the mesh, the spill, both lights.
		rig.UploadDest( stageCard( d.usedW, d.usedH, true, kBlack ) );
		rig.UploadSrc( projectionCard( s.usedW, s.usedH, true, kBlack ) );
		rig.Render();
		const ImageF base = rig.PixelsF();
		rig.UploadSrc( projectionCard( s.usedW, s.usedH, true, kWhite ) );
		rig.Render();
		const size_t srcPaint = differingFloats( base, rig.PixelsF() );
		rig.UploadSrc( projectionCard( s.usedW, s.usedH, true, kBlack ) );
		rig.UploadDest( stageCard( d.usedW, d.usedH, true, kWhite ) );
		rig.Render();
		const ImageF destPainted = rig.PixelsF();
		const size_t destPaint   = differingFloats( base, destPainted );
		size_t opaque            = 0;
		for( const ImageF* img : { &base, &destPainted } )
			for( size_t i = 3; i < img->size(); i += 4 )
				opaque += ( *img )[ i ] == 1.0f;
		Check( srcPaint == 0 && destPaint == 0,
		       std::string( sz.name ) + fmt( ": white under every transparent texel changes %.0f floats (Src) and %.0f (Dest) (tolerance 0 bits)", srcPaint, destPaint ) );
		Check( opaque == 2 * static_cast< size_t >( W ) * H, std::string( sz.name ) + fmt( ": out.a is 1 at %.0f of %.0f pixels", opaque, 2.0 * W * H ) );
		//The check can see colour: the same white on ~200 opaque texels shows.
		Image shown = projectionCard( s.usedW, s.usedH, true, kBlack );
		int painted = 0;
		for( int y = 0; y < s.usedH; y += std::max( 1, s.usedH / 14 ) )
			for( int x = 0; x < s.usedW; x += std::max( 1, s.usedW / 14 ) )
				if( shown[ at4( s.usedW, x, y ) + 3 ] == 255 )
				{
					put( shown, s.usedW, x, y, kWhite );
					++painted;
				}
		rig.UploadDest( stageCard( d.usedW, d.usedH, true, kBlack ) );
		rig.UploadSrc( shown );
		rig.Render();
		Check( painted > 50 && differingFloats( base, rig.PixelsF() ) > 0, fmt( "and the same white on %.0f opaque Src texels shows", painted ) );
	}

	//Half alpha is half the light: Src, then Dest, at alpha 1 and 0.5, in
	//linear light.
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	Gauze g;
	g.drawMesh = false;
	g.front    = 1.0;
	apply( rig.plugin, g );
	rig.UploadDest( flatFieldF( W, H, 0, 0, 0, 1 ) );
	double worst = 0.0;
	ImageF full, half;
	for( const float alpha : { 1.0f, 0.5f } )
	{
		rig.UploadSrc( flatFieldF( W, H, 0.8f, 0.5f, 0.3f, alpha ) );
		rig.Render();
		( alpha == 1.0f ? full : half ) = rig.PixelsF();
	}
	for( size_t i = 0; i < full.size(); i += 4 )
		for( size_t c = 0; c < 3; ++c )
			worst = worse( worst, std::fabs( srgbToLinear( half[ i + c ] ) - 0.5 * srgbToLinear( full[ i + c ] ) ) );
	g.opacity = 0.0;
	g.back    = 1.0;
	apply( rig.plugin, g );
	rig.UploadSrc( flatFieldF( W, H, 0, 0, 0, 1 ) );
	for( const float alpha : { 1.0f, 0.5f } )
	{
		rig.UploadDest( flatFieldF( W, H, 0.7f, 0.4f, 0.9f, alpha ) );
		rig.Render();
		( alpha == 1.0f ? full : half ) = rig.PixelsF();
	}
	for( size_t i = 0; i < full.size(); i += 4 )
		for( size_t c = 0; c < 3; ++c )
			worst = worse( worst, std::fabs( srgbToLinear( half[ i + c ] ) - 0.5 * srgbToLinear( full[ i + c ] ) ) );
	const double tol = 3.0 * 1.5 * kLinearBound;
	Check( within( worst, tol ), fmt( "half alpha is half the light, Src and Dest, in linear light (worst %.3g, tolerance %.3g: one budget and half another, x3)", worst, tol ) );
}

int runAlpha()
{
	std::printf( "straight alpha on both inputs: colour under a transparent texel is no light; the output is opaque\n" );
	atBothRasters( alphaCheck, { { kFaultStraight, "the colour not multiplied by its alpha" },
	                             { kFaultHardwareBilinear, "a resized input through the texture's filter before premultiplying" },
	                             { kFaultAlphaFromInput, "out.a from Dest" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --state: no state across frames and no clock.
//---------------------------------------------------------------------------
void statelessCheck( int fault )
{
	struct Step
	{
		int outW, outH, destW, destH, srcW, srcH;
	};
	const Step steps[] = { { 640, 360, 640, 360, 640, 360 }, { 320, 180, 400, 240, 320, 180 }, { 1280, 720, 1280, 720, 960, 540 } };
	MixRig rig;
	const Step& s0 = steps[ 0 ];
	if( !rig.Init( s0.outW, s0.outH, InputSpec::Exact( s0.destW, s0.destH ), InputSpec::Exact( s0.srcW, s0.srcH ), true, fault ) )
		return;
	rig.UploadDest( stageCard( s0.destW, s0.destH ) );
	rig.UploadSrc( projectionCard( s0.srcW, s0.srcH ) );
	rig.Render();
	const ImageF first = rig.PixelsF();
	rig.Render();
	const size_t again = differingFloats( first, rig.PixelsF() );
	size_t clocks      = 0;
	for( const double t : { 0.0, 1.0, 499000.0 } )
	{
		rig.plugin.SetTime( t );
		rig.Render();
		clocks += differingFloats( first, rig.PixelsF() );
	}
	rig.plugin.SetTime( 0.0 );
	Check( again == 0, fmt( "a second frame is the first's floats (%.0f differ)", again ) );
	Check( clocks == 0, fmt( "the host's clock, SetTime( 0 ), ( 1 ) and ( 499000 ), changes %.0f floats: nothing moves with time", clocks ) );
	bool firstStep = true;
	for( const Step& s : steps )
	{
		if( !firstStep )
		{
			rig.ResizeOutput( s.outW, s.outH, true );
			rig.MakeInputs( InputSpec::Exact( s.destW, s.destH ), InputSpec::Exact( s.srcW, s.srcH ) );
			rig.UploadDest( stageCard( s.destW, s.destH ) );
			rig.UploadSrc( projectionCard( s.srcW, s.srcH ) );
			rig.Render();
		}
		const ImageF running = rig.PixelsF();
		MixRig fresh;
		if( !fresh.Init( s.outW, s.outH, InputSpec::Exact( s.destW, s.destH ), InputSpec::Exact( s.srcW, s.srcH ), true, kFaultNone ) )
			return;
		fresh.UploadDest( stageCard( s.destW, s.destH ) );
		fresh.UploadSrc( projectionCard( s.srcW, s.srcH ) );
		fresh.Render();
		const size_t differ = differingFloats( running, fresh.PixelsF() );
		Check( differ == 0, std::string( firstStep ? "" : "after a resize mid-run, " ) + fmt( "out %.0fx%.0f, Dest %.0fx%.0f", s.outW, s.outH, s.destW, s.destH )
		                        + fmt( ", Src %.0fx%.0f: a fresh instance's floats (%.0f differ)", s.srcW, s.srcH, differ ) );
		firstStep = false;
	}
}

int runState()
{
	std::printf( "no state across frames, no clock: frames, clocks, a resize mid-run, a fresh instance\n\n" );
	statelessCheck( kFaultNone );
	const int stale = failuresOf( [] { statelessCheck( kFaultStaleSize ); } );
	Negative( stale > 0, fmt( "the first frame's width kept across a resize fails %.0f assertion(s)", stale ) );
	const int clock = failuresOf( [] { statelessCheck( kFaultClock ); } );
	Negative( clock > 0, fmt( "a weave that breathes with the host's clock fails %.0f assertion(s)", clock ) );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --mutation
//---------------------------------------------------------------------------
int runMutation()
{
	std::printf( "one character of the shipped screen shader fails a check\n\n" );
	const std::string shipped = AssembleFragment( kScreenFragment );
	const std::string from    = "vec3 passed = cBox * stage * behind;";
	const std::string to      = "vec3 passed = cBox + stage * behind;";
	const size_t at           = shipped.find( from );
	Check( at != std::string::npos && shipped.find( from, at + 1 ) == std::string::npos, "the line to mutate occurs exactly once in the shipped shader" );
	if( at == std::string::npos )
		return 1;
	std::string mutated = shipped;
	mutated.replace( at, from.size(), to );
	int differ = 0;
	for( size_t i = 0; i < shipped.size(); ++i )
		differ += shipped[ i ] != mutated[ i ];
	Check( differ == 1, fmt( "the mutation is one character (%.0f differ): the light through the holes added to the holes' share, not multiplied", differ ) );

	ImageF defaultFloats, hookFloats;
	for( int pass = 0; pass < 2; ++pass )
	{
		MixRig rig;
		if( !rig.Init( 320, 180, true, kFaultNone, pass ? shipped.c_str() : nullptr ) )
			return 1;
		rig.UploadDest( stageCard( 320, 180 ) );
		rig.UploadSrc( projectionCard( 320, 180 ) );
		rig.Render();
		( pass ? hookFloats : defaultFloats ) = rig.PixelsF();
	}
	Check( differingFloats( defaultFloats, hookFloats ) == 0, "the shipped text through the hook renders the default path's floats" );
	const int shippedFails = failuresOf( [ & ] { closedCheck( 320, 180, kFaultNone, shipped.c_str() ); } );
	Check( shippedFails == 0, "--closed passes with the shipped text through the hook" );
	const int mutatedFails = failuresOf( [ & ] { closedCheck( 320, 180, kFaultNone, mutated.c_str() ); } );
	Negative( mutatedFails > 0, fmt( "--closed fails the mutated shader (%.0f assertion(s))", mutatedFails ) );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --bench
//---------------------------------------------------------------------------
//Enough frames for the GPU to leave its idle clock (penumbra's lesson).
constexpr int kBenchWarmUp = 60;

struct BenchResult
{
	double gpu  = -1.0;///< ms/frame, the median of GL_TIME_ELAPSED around each frame
	double wall = -1.0;///< ms/frame, the median wall clock from Render to glFinish
};

//Frames are timed one at a time. Queued back to back into the same framebuffer
//with glFinish only at the ends, 120 frames of the biggest disc at 1080p timed
//0.03 ms/frame where each alone takes 2.3 ms on the GPU's own clock: the queued
//figure measures nothing. (Probably the tile renderer's hidden-surface removal
//shading only the last of a run of opaque full-screen draws; not proven.)
BenchResult benchAt( int width, int height, int frames, bool worst )
{
	BenchResult result;
	MixRig rig;
	if( !rig.Init( width, height ) )
		return result;
	rig.UploadDest( stageCard( width, height ) );
	rig.UploadSrc( projectionCard( width, height ) );
	if( worst )
	{
		//The biggest disc the controls allow: a 2 m gauze, the set 3 throws
		//back, the audience 16 throws out.
		rig.SetIndex( Scrim::PT_GAUZE_WIDTH, 0.0f );
		rig.SetIndex( Scrim::PT_SPILL_DISTANCE, 1.0f );
		rig.SetIndex( Scrim::PT_AUDIENCE, 1.0f );
	}
	//Every frame must have rendered, or the time is of nothing.
	bool rendered = true;
	for( int frame = 0; frame < kBenchWarmUp; ++frame )
	{
		rendered = rig.Render() && rendered;
		glFinish();
	}
	std::vector< double > gpu, wall;
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( int frame = 0; frame < frames; ++frame )
	{
		const auto a = std::chrono::steady_clock::now();
		glBeginQuery( GL_TIME_ELAPSED, query );
		rendered = rig.Render() && rendered;
		glEndQuery( GL_TIME_ELAPSED );
		glFinish();
		wall.push_back( std::chrono::duration< double >( std::chrono::steady_clock::now() - a ).count() * 1000.0 );
		GLuint64 ns = 0;
		glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
		gpu.push_back( static_cast< double >( ns ) / 1.0e6 );
	}
	glDeleteQueries( 1, &query );
	//And the picture is not the clear colour: the pass drew.
	const Image last = rig.Pixels();
	size_t lit       = 0;
	for( size_t i = 0; i < last.size(); i += 4 )
		lit += last[ i ] != 0 || last[ i + 1 ] != 0 || last[ i + 2 ] != 0;
	if( !rendered || lit < last.size() / 32 )
	{
		std::fprintf( stderr, "bench: a frame failed to render, or the output is blank (%zu lit pixels)\n", lit );
		return result;
	}
	std::sort( gpu.begin(), gpu.end() );
	std::sort( wall.begin(), wall.end() );
	result.gpu  = gpu[ gpu.size() / 2 ];
	result.wall = wall[ wall.size() / 2 ];
	return result;
}

int runBench( int frames )
{
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = { { "1280x720 ", 1280, 720 }, { "1920x1080", 1920, 1080 } };
	double load[ 3 ]   = { 0.0, 0.0, 0.0 };
	getloadavg( load, 3 );
	std::printf( "%d frames, each timed alone after a %d-frame warm-up; Dest and Src at the output size. Load average %.1f.\n", frames, kBenchWarmUp, load[ 0 ] );
	std::printf( "gpu = GL_TIME_ELAPSED around the frame; wall = Render to glFinish. Medians, ms/frame.\n" );
	std::printf( "Defaults, then the biggest defocus disc the controls allow (2 m gauze, Spill 3, Audience 16).\n\n" );
	std::printf( "resolution   defaults:   gpu     wall | biggest disc:   gpu     wall\n" );
	for( const Size& size : sizes )
	{
		const BenchResult plain = benchAt( size.width, size.height, frames, false );
		const BenchResult big   = benchAt( size.width, size.height, frames, true );
		if( plain.gpu < 0.0 || big.gpu < 0.0 )
			return 1;
		std::printf( "%s             %7.3f  %7.3f |               %7.3f  %7.3f\n", size.name, plain.gpu, plain.wall, big.gpu, big.wall );
	}
	return 0;
}

//---------------------------------------------------------------------------
// --set, strictly: a value is a number, all of it; a boolean also takes
// on/off/true/false; an option takes its element's name. Anything else is
// refused, never read as 0.
//---------------------------------------------------------------------------
bool applyOne( Scrim& plugin, const std::string& setting )
{
	const size_t equals = setting.find( '=' );
	if( equals == std::string::npos )
	{
		std::fprintf( stderr, "--set %s: expected Name=Value\n", setting.c_str() );
		return false;
	}
	const std::string name = setting.substr( 0, equals ), text = setting.substr( equals + 1 );
	for( unsigned id = 0; id < Scrim::PT_ABOUT_FIRST; ++id )
	{
		const char* declared = plugin.GetParamName( id );
		if( declared == nullptr || name != declared )
			continue;
		char* end         = nullptr;
		const float value = std::strtof( text.c_str(), &end );
		if( !text.empty() && end != nullptr && *end == '\0' && std::isfinite( value ) )
		{
			plugin.SetFloatParameter( id, value );
			return true;
		}
		const unsigned type = plugin.GetParamType( id );
		if( type == FF_TYPE_BOOLEAN )
		{
			if( text == "on" || text == "true" || text == "off" || text == "false" )
			{
				plugin.SetFloatParameter( id, text == "on" || text == "true" ? 1.0f : 0.0f );
				return true;
			}
		}
		if( type == FF_TYPE_OPTION )
			for( unsigned e = 0; e < plugin.GetNumParamElements( id ); ++e )
				if( text == plugin.GetParamElementName( id, e ) )
				{
					plugin.SetFloatParameter( id, static_cast< float >( e ) );
					return true;
				}
		std::fprintf( stderr, "--set %s: '%s' is not a number%s\n", setting.c_str(), text.c_str(),
		              type == FF_TYPE_BOOLEAN ? " or on/off/true/false" : type == FF_TYPE_OPTION ? " or one of the option's names (try --list)" : "" );
		return false;
	}
	std::fprintf( stderr, "--set %s: no parameter called '%s' (try --list)\n", setting.c_str(), name.c_str() );
	return false;
}

bool applySettings( Scrim& plugin, const std::vector< std::string >& settings )
{
	for( const std::string& s : settings )
		if( !applyOne( plugin, s ) )
			return false;
	return true;
}

//---------------------------------------------------------------------------
// --pipe: the fleet's frame format, with a second input, in pepper's and
// penumbra's shape.
//
//   * stdin is DEST, inputTextures[0], the layer below: the scene behind.
//     Output-sized, raw RGBA, top row first.
//   * `--pipe-src PATH` is SRC, inputTextures[1], this layer: the
//     projection. Raw RGBA, top row first, at `--src-size` (default: the
//     output size). A FIFO will do. Without it the `--input-b` card is held.
//
// `--script` is the cue sheet: `frame Name value`, value in host units, '#'
// a comment; a STANDARD (or colour) parameter ramps between keys, a boolean
// steps. Unknown names are refused before a frame is read. SIGPIPE is
// ignored: a closed stdout is exit 1. `--stats` prints each frame's
// 99th-percentile brightest output channel, encoded and in linear light, from
// the float readback, and the same pooled over every frame at the end
// (lithophane's, for tools/clips.py defaults).
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( words.back().c_str(), &end );
		if( end == nullptr || *end != '\0' || !std::isfinite( value ) )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + words.back() + "' is not a number";
			return {};
		}
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame, bool ramps )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
	{
		if( frame < track[ i ].first )
		{
			const auto& a = track[ i - 1 ];
			const auto& b = track[ i ];
			if( !ramps )
				return a.second;
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? ( static_cast< float >( frame - a.first ) / span ) : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
		if( frame == track[ i ].first )
			return track[ i ].second;
	}
	return track.back().second;
}

bool readFrame( int fd, Image& frame )
{
	size_t filled = 0;
	while( filled < frame.size() )
	{
		const ssize_t got = read( fd, frame.data() + filled, frame.size() - filled );
		if( got <= 0 )
			return false;
		filled += static_cast< size_t >( got );
	}
	return true;
}

bool writeAll( int fd, const Image& frame )
{
	size_t written = 0;
	while( written < frame.size() )
	{
		const ssize_t n = write( fd, frame.data() + written, frame.size() - written );
		if( n <= 0 )
			return false;
		written += static_cast< size_t >( n );
	}
	return true;
}

void flipRows( const Image& in, Image& out, int width, int height )
{
	const size_t row = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::memcpy( out.data() + static_cast< size_t >( height - 1 - y ) * row, in.data() + static_cast< size_t >( y ) * row, row );
}

/// A histogram over [0, 1] in 2^20 bins, for the pooled percentile: exact to
/// a millionth without keeping every pixel (lithophane's).
struct Percentile
{
	static constexpr int kBins = 1 << 20;
	std::vector< uint64_t > bins = std::vector< uint64_t >( kBins + 1, 0 );
	uint64_t count               = 0;
	void Add( double y )
	{
		const double v = std::clamp( y == y ? y : 0.0, 0.0, 1.0 );
		++bins[ static_cast< size_t >( std::lround( v * kBins ) ) ];
		++count;
	}
	double At( double q ) const
	{
		const uint64_t want = static_cast< uint64_t >( std::ceil( q * static_cast< double >( count ) ) );
		uint64_t seen       = 0;
		for( size_t i = 0; i < bins.size(); ++i )
		{
			seen += bins[ i ];
			if( seen >= want && want > 0 )
				return static_cast< double >( i ) / kBins;
		}
		return 1.0;
	}
};

int runPipe( int width, int height, int srcWidth, int srcHeight, const std::string& scriptPath, const std::string& srcPath, const std::string& inputB,
             const std::vector< std::string >& settings, bool stats )
{
	signal( SIGPIPE, SIG_IGN );
	MixRig rig;
	if( !rig.Init( width, height, InputSpec::Exact( width, height ), InputSpec::Exact( srcWidth, srcHeight ), stats ) )
		return 1;
	if( !applySettings( rig.plugin, settings ) )
		return 2;

	struct Automation
	{
		unsigned int id;
		bool ramps;
		Track track;
	};
	std::vector< Automation > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
		for( const auto& entry : tracks )
		{
			bool found = false;
			for( unsigned int id = 0; id < Scrim::PT_ABOUT_FIRST; ++id )
			{
				const char* name = rig.plugin.GetParamName( id );
				if( name != nullptr && entry.first == name )
				{
					const unsigned int type = rig.plugin.GetParamType( id );
					const bool ramps        = type == FF_TYPE_STANDARD || type == FF_TYPE_RED || type == FF_TYPE_GREEN || type == FF_TYPE_BLUE;
					automation.push_back( { id, ramps, entry.second } );
					found = true;
					break;
				}
			}
			if( !found )
			{
				std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
				return 2;
			}
		}
	}

	int srcFd = -1;
	if( !srcPath.empty() )
	{
		srcFd = open( srcPath.c_str(), O_RDONLY );
		if( srcFd < 0 )
		{
			std::fprintf( stderr, "cannot open --pipe-src %s\n", srcPath.c_str() );
			return 2;
		}
	}
	else
		rig.UploadSrc( generate( inputB, srcWidth, srcHeight ) );

	Image destIn( static_cast< size_t >( width ) * height * 4 ), destUp( destIn.size() );
	Image srcIn( static_cast< size_t >( srcWidth ) * srcHeight * 4 ), srcUp( srcIn.size() );
	Image out( destIn.size() ), bytes( destIn.size() );
	Percentile pooled;
	int index  = 0;
	int result = 0;
	for( ;; ++index )
	{
		if( !readFrame( STDIN_FILENO, destIn ) )
			break;
		if( srcFd >= 0 )
		{
			if( !readFrame( srcFd, srcIn ) )
				break;
			flipRows( srcIn, srcUp, srcWidth, srcHeight );
			rig.UploadSrc( srcUp );
		}
		flipRows( destIn, destUp, width, height );
		rig.UploadDest( destUp );
		for( const Automation& track : automation )
			rig.plugin.SetFloatParameter( track.id, valueAt( track.track, index, track.ramps ) );
		if( !rig.Render() )
		{
			std::fprintf( stderr, "ProcessOpenGL failed at frame %d\n", index );
			result = 1;
			break;
		}
		if( stats )
		{
			//The float readback, before any 8-bit rounding.
			const ImageF f = rig.PixelsF();
			Percentile frame;
			for( size_t i = 0; i < f.size(); i += 4 )
			{
				const double top = std::max( { f[ i ], f[ i + 1 ], f[ i + 2 ] } );
				frame.Add( top );
				pooled.Add( top );
				bytes[ i + 0 ] = toByte( f[ i + 0 ] );
				bytes[ i + 1 ] = toByte( f[ i + 1 ] );
				bytes[ i + 2 ] = toByte( f[ i + 2 ] );
				bytes[ i + 3 ] = toByte( f[ i + 3 ] );
			}
			const double p99 = frame.At( 0.99 );
			std::fprintf( stderr, "frame %d p99 brightest channel %.8f linear %.8f\n", index, p99, srgbToLinear( p99 ) );
			flipRows( bytes, out, width, height );
		}
		else
			flipRows( rig.Pixels(), out, width, height );
		if( !writeAll( STDOUT_FILENO, out ) )
		{
			std::fprintf( stderr, "smtest --pipe: stdout closed at frame %d\n", index );
			result = 1;
			break;
		}
	}
	if( srcFd >= 0 )
		close( srcFd );
	if( stats )
	{
		const double p99 = pooled.At( 0.99 );
		std::fprintf( stderr, "smtest --pipe: %d frames; over every pixel of every frame, p99 brightest channel %.8f linear %.8f\n", index, p99, srgbToLinear( p99 ) );
	}
	else
		std::fprintf( stderr, "smtest --pipe: %d frames\n", index );
	return result;
}

//---------------------------------------------------------------------------
void usage()
{
	std::printf(
		"smtest -- render and measure SW Scrim (FFGL mixer)\n"
		"\n"
		"  --out PATH        render through the plugin (default scrim.png)\n"
		"  --input-a NAME    DEST, the scene behind the gauze (default stage)\n"
		"  --input-b NAME    SRC, what the projector sends (default projection)\n"
		"                    %s\n"
		"  --src-size WxH    Src's size (default: the output size)\n"
		"  --size WxH        picture size (default 1280x720)\n"
		"  --frames N        frames to render for --out (stateless: the last is written), or --bench's count\n"
		"  --set \"Name=V\"    a parameter by display name, in host units; a boolean also takes\n"
		"                    on/off/true/false. Repeatable. Unparsable values are refused (exit 2).\n"
		"  --list            every parameter, its kind, default and range\n"
		"  --names --mixer --closed --unlit --superpose --spill --moire --alpha --state --mutation\n"
		"                    the checks (see the top of tools/smtest/main.cpp)\n"
		"  --bench           ms/frame at 720p and 1080p (--frames N, default 30)\n"
		"  --pipe            raw RGBA Dest frames on stdin, raw RGBA frames on stdout\n"
		"  --pipe-src PATH   raw RGBA Src frames for --pipe (a file or FIFO); default: --input-b, held\n"
		"  --script PATH     cues for --pipe: 'frame Name value'; standard and colour parameters ramp, the rest step\n"
		"  --stats           with --pipe: each frame's p99 brightest output channel on stderr, and pooled at the end\n"
		"  --help\n",
		kCardNames );
}

bool parseSize( const std::string& text, int& w, int& h )
{
	const size_t x = text.find( 'x' );
	if( x == std::string::npos )
		return false;
	char* end = nullptr;
	w         = static_cast< int >( std::strtol( text.substr( 0, x ).c_str(), &end, 10 ) );
	if( end == nullptr || *end != '\0' )
		return false;
	h = static_cast< int >( std::strtol( text.substr( x + 1 ).c_str(), &end, 10 ) );
	return end != nullptr && *end == '\0' && w > 0 && h > 0 && w <= kMaxSize && h <= kMaxSize;
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath = "scrim.png";
	std::string inputA = "stage", inputB = "projection";
	int width = 1280, height = 720, srcWidth = 0, srcHeight = 0, frames = 0;
	bool wantList = false, wantBench = false, wantPipe = false, wantStats = false;
	std::string scriptPath, srcPath, check;
	std::vector< std::string > settings;
	static const char* checks[] = { "--names", "--mixer", "--closed", "--unlit", "--superpose", "--spill", "--moire", "--alpha", "--state", "--mutation" };

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		bool isCheck               = false;
		for( const char* c : checks )
			isCheck = isCheck || argument == c;
		if( argument == "--help" )
		{
			usage();
			return 0;
		}
		else if( isCheck )
			check = argument;
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( ( argument == "--input-a" || argument == "--input-b" ) && hasNext )
		{
			const std::string name = argv[ ++i ];
			if( !isCard( name ) )
			{
				std::fprintf( stderr, "%s: no card called '%s' (%s)\n", argument.c_str(), name.c_str(), kCardNames );
				return 2;
			}
			( argument == "--input-a" ? inputA : inputB ) = name;
		}
		else if( ( argument == "--size" || argument == "--src-size" ) && hasNext )
		{
			if( !parseSize( argv[ ++i ], argument == "--size" ? width : srcWidth, argument == "--size" ? height : srcHeight ) )
			{
				std::fprintf( stderr, "%s wants a positive WxH, at most %d each way\n", argument.c_str(), kMaxSize );
				return 2;
			}
		}
		else if( argument == "--frames" && hasNext )
		{
			char* end        = nullptr;
			const long value = std::strtol( argv[ ++i ], &end, 10 );
			if( end == nullptr || *end != '\0' || value <= 0 )
			{
				std::fprintf( stderr, "--frames wants a positive whole number\n" );
				return 2;
			}
			frames = static_cast< int >( value );
		}
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--bench" )
			wantBench = true;
		else if( argument == "--pipe" )
			wantPipe = true;
		else if( argument == "--stats" )
			wantStats = true;
		else if( argument == "--pipe-src" && hasNext )
			srcPath = argv[ ++i ];
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else
		{
			std::fprintf( stderr, "unknown argument: %s\n", argument.c_str() );
			usage();
			return 2;
		}
	}
	if( srcWidth == 0 )
	{
		srcWidth  = width;
		srcHeight = height;
	}

	if( wantList )
		return runList();
	if( check == "--names" )
		return runNames();

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}

	int result = 0;
	if( check == "--mixer" )
		result = runMixer();
	else if( check == "--closed" )
		result = runClosed();
	else if( check == "--unlit" )
		result = runUnlit();
	else if( check == "--superpose" )
		result = runSuperpose();
	else if( check == "--spill" )
		result = runSpill();
	else if( check == "--moire" )
		result = runMoire();
	else if( check == "--alpha" )
		result = runAlpha();
	else if( check == "--state" )
		result = runState();
	else if( check == "--mutation" )
		result = runMutation();
	else if( wantBench )
		result = runBench( frames > 0 ? frames : 30 );
	else if( wantPipe )
		result = runPipe( width, height, srcWidth, srcHeight, scriptPath, srcPath, inputB, settings, wantStats );
	else
	{
		MixRig rig;
		if( !rig.Init( width, height, InputSpec::Exact( width, height ), InputSpec::Exact( srcWidth, srcHeight ) ) )
			return 1;
		rig.UploadDest( generate( inputA, width, height ) );
		rig.UploadSrc( generate( inputB, srcWidth, srcHeight ) );
		if( !applySettings( rig.plugin, settings ) )
			return 2;
		bool ok = true;
		for( int f = 0; f < std::max( 1, frames ); ++f )
			ok = rig.Render() && ok;
		if( !ok || !writePng( outPath, width, height, rig.Pixels() ) )
		{
			std::fprintf( stderr, "could not render or write %s\n", outPath.c_str() );
			return 1;
		}
		std::printf( "wrote %s\n", outPath.c_str() );
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	if( result != 0 )
		std::printf( "\n%d check(s) FAILED\n", failures > 0 ? failures : result );
	return result;
}

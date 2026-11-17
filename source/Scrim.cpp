#include "Scrim.h"

#include "Controls.h"
#include "Diag.h"
#include "GLState.h"
#include "Names.h"
#include "Shaders.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;
using namespace scrim;

//---------------------------------------------------------------------------
// The eighth argument is the plugin TYPE, and it is the only thing in the
// whole repo that makes this a mixer rather than an effect. The input count
// is a SEPARATE declaration, in the constructor. See genlock's AGENTS.md.
//---------------------------------------------------------------------------
static CFFGLPluginInfo PluginInfo(
	PluginFactory< Scrim >,// Create method
	names::kMixerId,       // Plugin unique ID of maximum length 4.
	names::kMixerName,     // Plugin name
	2,                     // API major version number
	1,                     // API minor version number
	0,                     // Plugin major version number
	1,                     // Plugin minor version number
	FF_MIXER,              // Plugin type
	"A theatre gauze. This layer is what the projector sends onto the gauze; the layer below is the scene behind it. What the audience sees is the light the threads reflect plus the light that comes through the holes: with the front lit and the back dark the gauze is an opaque screen, with the back lit and the front dark it all but disappears, and in between the two are seen together. The projection's light that passes the holes lands on the set behind, magnified, dimmer and softer, and lights it. The layer's opacity is the projector's level.\n\nThis is a MIXER: set it as the projection layer's blend mode, over the scene.",
	"Scrim FFGL mixer" );

static_assert( Scrim::PT_COUNT - Scrim::PT_ABOUT_FIRST == stoatworks::about::kParamCount,
               "the About block's size changed with the generated header" );

namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

std::string sizeText( const FFGLTextureStruct& t )
{
	return std::to_string( t.Width ) + "x" + std::to_string( t.Height ) + " of " + std::to_string( t.HardwareWidth ) + "x"
	       + std::to_string( t.HardwareHeight );
}

/// How many Opacity changes the log records, per instance. Enough to follow a
/// whole layer transition frame by frame (a 2 s fade at 60 fps is 120); not a
/// line a frame for the length of a show.
constexpr int kOpacityLines = 400;

/// Instances are numbered in creation order, so a host session's log can tell
/// the layer's mixer from a transition's (Arena makes separate instances).
int gInstances = 0;

bool tooBig( const FFGLTextureStruct& t )
{
	return t.Width > static_cast< FFUInt32 >( kMaxSize ) || t.Height > static_cast< FFUInt32 >( kMaxSize );
}
} // namespace

Scrim::Scrim()
{
	//A mixer takes exactly two. This is a separate declaration from the type
	//above, read by the host through different function codes entirely.
	SetMinInputs( 2 );
	SetMaxInputs( 2 );
	//The SDK leaves its clock uninitialised until a host calls SetTime.
	hostTime = 0.0;

	//---------------------------------------------------------------------
	// Defaults. SetParamInfof reads each one back out of GetFloatParameter,
	// so these assignments are what the host is told the defaults are.
	//---------------------------------------------------------------------
	params[ PT_LINEAR_LIGHT ]   = 1.0f;
	params[ PT_OPACITY ]        = 1.0f;
	params[ PT_FRONT_LIGHT ]    = ParamForLight( kFrontDefault );
	params[ PT_PIXEL_FILL ]     = ParamForFill( kFillDefault );
	params[ PT_BACK_LIGHT ]     = ParamForLight( kBackDefault );
	params[ PT_WASH ]           = ParamForLight( 0.0 );
	params[ PT_SPILL_DISTANCE ] = ParamForSpill( kSpillDefault );
	params[ PT_AUDIENCE ]       = ParamForAudience( kAudienceDefault );
	params[ PT_OPEN_AREA ]      = static_cast< float >( kOpenDefault );
	params[ PT_THREAD_RED ]     = static_cast< float >( kThreadDefault );
	params[ PT_THREAD_GREEN ]   = static_cast< float >( kThreadDefault );
	params[ PT_THREAD_BLUE ]    = static_cast< float >( kThreadDefault );
	params[ PT_MESH ]           = ParamForMesh( kMeshDefaultMm );
	params[ PT_DRAW_MESH ]      = 1.0f;
	params[ PT_GAUZE_WIDTH ]    = ParamForWidth( kWidthDefaultM );

	//---------------------------------------------------------------------
	// Declaration. Every ranged parameter is a plain 0..1 float, with the
	// conversions in Controls.cpp.
	//---------------------------------------------------------------------
	//FIRST, deliberately: Resolume Arena does not expose a mixer's parameter
	//0 (measured on genlock, wipe, relay, lenticular and pepper), so it holds
	//a control whose default is right if it can never be changed.
	SetParamInfo( PT_LINEAR_LIGHT, "Linear Light", FF_TYPE_BOOLEAN, true );

	//Named Opacity, and the name is load-bearing: Resolume binds a mixer
	//parameter called Opacity to the LAYER's opacity fader, and a layer
	//transition ramps it (measured on genlock, wipe and relay in Arena
	//7.27.1). So the projection layer's fader is the projector's level.
	SetParamInfof( PT_OPACITY, "Opacity", FF_TYPE_STANDARD );
	SetParamInfof( PT_FRONT_LIGHT, "Front Light", FF_TYPE_STANDARD );
	SetParamInfof( PT_PIXEL_FILL, "Pixel Fill", FF_TYPE_STANDARD );

	SetParamInfof( PT_BACK_LIGHT, "Back Light", FF_TYPE_STANDARD );
	SetParamInfof( PT_WASH, "Wash", FF_TYPE_STANDARD );
	SetParamInfof( PT_SPILL_DISTANCE, "Spill Distance", FF_TYPE_STANDARD );
	SetParamInfof( PT_AUDIENCE, "Audience", FF_TYPE_STANDARD );

	SetParamInfof( PT_OPEN_AREA, "Open Area", FF_TYPE_STANDARD );
	//The threads' colour is a triplet the host draws as one colour picker:
	//RED named for the control, then _Green and _Blue (the fleet's spelling).
	SetParamInfof( PT_THREAD_RED, "Thread", FF_TYPE_RED );
	SetParamInfof( PT_THREAD_GREEN, "Thread_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_THREAD_BLUE, "Thread_Blue", FF_TYPE_BLUE );
	SetParamInfof( PT_MESH, "Mesh", FF_TYPE_STANDARD );
	SetParamInfo( PT_DRAW_MESH, "Draw Mesh", FF_TYPE_BOOLEAN, true );
	SetParamInfof( PT_GAUZE_WIDTH, "Gauze Width", FF_TYPE_STANDARD );

	// Groups, the way Resolume shows them: each group one contiguous run.
	SetParamGroup( PT_LINEAR_LIGHT, "Light" );
	for( FFUInt32 i = PT_OPACITY; i <= PT_PIXEL_FILL; ++i )
		SetParamGroup( i, "Projector" );
	for( FFUInt32 i = PT_BACK_LIGHT; i <= PT_AUDIENCE; ++i )
		SetParamGroup( i, "Stage" );
	for( FFUInt32 i = PT_OPEN_AREA; i <= PT_GAUZE_WIDTH; ++i )
		SetParamGroup( i, "Gauze" );

	// The About block. Declared inline rather than through a helper, because
	// SetParamInfo is protected on CFFGLPlugin.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Scrim mixer" );

	diag::init();
	instance = ++gInstances;
}

//---------------------------------------------------------------------------
FFResult Scrim::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string screen = fragmentOverride ? std::string( fragmentOverride ) : AssembleFragment( kScreenFragment );
	if( !screenShader.Compile( AssembleVertex(), screen ) )
	{
		//Returning FF_FAIL from InitGL is invisible to the operator: the
		//mixer simply does nothing in Resolume. These lines are the only record.
		diag::error( "the screen shader failed to compile - the mixer will do nothing" );
		FFGLLog::LogToHost( "Scrim: the screen shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	diag::info( "initialised, viewport " + std::to_string( vp ? vp->width : 0 ) + "x" + std::to_string( vp ? vp->height : 0 )
	            + " (instance #" + std::to_string( instance ) + ")" );

	//Use base-class init as the success result so it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}

void Scrim::LogGuard( unsigned bit, const char* what )
{
	if( guardsLogged & bit )
		return;
	guardsLogged |= bit;
	diag::warn( std::string( "guard: called with " ) + what + " -- returned FF_FAIL (logged once)" );
}

Asked Scrim::asked() const
{
	Asked a;
	a.linear      = params[ PT_LINEAR_LIGHT ] > 0.5f;
	a.opacity     = std::clamp( static_cast< double >( params[ PT_OPACITY ] ), 0.0, 1.0 );
	a.front       = LightFromParam( params[ PT_FRONT_LIGHT ] );
	a.fill        = FillFromParam( params[ PT_PIXEL_FILL ] );
	a.back        = LightFromParam( params[ PT_BACK_LIGHT ] );
	a.wash        = LightFromParam( params[ PT_WASH ] );
	a.spill       = SpillFromParam( params[ PT_SPILL_DISTANCE ] );
	a.audience    = AudienceFromParam( params[ PT_AUDIENCE ] );
	a.open        = std::clamp( static_cast< double >( params[ PT_OPEN_AREA ] ), 0.0, 1.0 );
	a.thread[ 0 ] = params[ PT_THREAD_RED ];
	a.thread[ 1 ] = params[ PT_THREAD_GREEN ];
	a.thread[ 2 ] = params[ PT_THREAD_BLUE ];
	a.meshMm      = MeshFromParam( params[ PT_MESH ] );
	a.drawMesh    = params[ PT_DRAW_MESH ] > 0.5f;
	a.widthM      = WidthFromParam( params[ PT_GAUZE_WIDTH ] );
	return a;
}

//---------------------------------------------------------------------------
FFResult Scrim::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	//The SDK's Add example guards on the input count and on each pointer,
	//with a comment saying a host calls a mixer with one input while the
	//operator is still patching. Not observed in Arena (genlock, one
	//sequence) -- but a mixer that dereferenced a null would take Resolume
	//down with it. oxbow, an effect host, hands it one input: refused here.
	if( pGL == nullptr || pGL->inputTextures == nullptr )
	{
		LogGuard( 1u, "no input array" );
		return FF_FAIL;
	}
	if( pGL->numInputTextures < 2 )
	{
		LogGuard( 2u, ( std::to_string( pGL->numInputTextures ) + " input(s)" ).c_str() );
		return FF_FAIL;
	}
	if( pGL->inputTextures[ 0 ] == nullptr || pGL->inputTextures[ 1 ] == nullptr )
	{
		LogGuard( 4u, "a null input" );
		return FF_FAIL;
	}

	const FFGLTextureStruct& dest = *pGL->inputTextures[ 0 ];//the layer below: the scene behind
	const FFGLTextureStruct& src  = *pGL->inputTextures[ 1 ];//this layer: the projection
	if( dest.Width == 0 || dest.Height == 0 || dest.HardwareWidth < dest.Width || dest.HardwareHeight < dest.Height || src.Width == 0
	    || src.Height == 0 || src.HardwareWidth < src.Width || src.HardwareHeight < src.Height )
	{
		LogGuard( 8u, "a zero-sized input, or one larger than its own texture" );
		return FF_FAIL;
	}

	const int outW = static_cast< int >( currentViewport.width );
	const int outH = static_cast< int >( currentViewport.height );
	if( outW <= 0 || outH <= 0 )
	{
		LogGuard( 16u, "a zero viewport" );
		return FF_FAIL;
	}
	//The weave's integer phase products and the GL's own texture limit.
	if( outW > kMaxSize || outH > kMaxSize || tooBig( dest ) || tooBig( src ) )
	{
		LogGuard( 32u, ( "a frame or an input over " + std::to_string( kMaxSize ) + " pixels" ).c_str() );
		return FF_FAIL;
	}
	if( !screenShader.IsReady() )
		return FF_FAIL;

	++frames;
	if( frames == 1 )
	{
		diag::info( "first frame: Dest " + sizeText( dest ) + ", Src " + sizeText( src ) + ", out " + std::to_string( outW ) + "x"
		            + std::to_string( outH ) );
		firstWidth = outW;
	}
	if( params[ PT_OPACITY ] != loggedOpacity && opacityLines < kOpacityLines )
	{
		//The only evidence a host gives of what drives a mixer's Opacity:
		//the layer fader, a transition, or the mixer's own slider.
		diag::info( "Opacity " + std::to_string( params[ PT_OPACITY ] ) + " at frame " + std::to_string( frames ) + " (instance #"
		            + std::to_string( instance ) + ")" );
		loggedOpacity = params[ PT_OPACITY ];
		++opacityLines;
	}

	//The frame's numbers, in double. kFaultStaleSize keeps the first frame's
	//width: the one piece of state the shipped plugin does not have.
	const int planWidth = ( fault & kFaultStaleSize ) ? firstWidth : outW;
	const Setup s       = Plan( asked(), planWidth, fault, hostTime );
	lastSetup           = s;
	if( s.radiusCapped && !loggedCap )
	{
		diag::warn( "the spill's defocus disc is over " + std::to_string( static_cast< int >( kMaxRadiusPx ) ) + " pixels at "
		            + std::to_string( outW ) + " wide: capped (logged once)" );
		loggedCap = true;
	}

	//Everything below changes GL state; all of it goes back on the way out.
	ScopedGLState state;
	glDisable( GL_BLEND );

	glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
	glUseProgram( screenShader.GetGLID() );
	bindUnit( 0, dest.Handle );
	bindUnit( 1, src.Handle );
	screenShader.Set( "Stage", 0 );
	screenShader.Set( "Projection", 1 );

	glUniform2i( screenShader.FindUniform( "OutSize" ), outW, outH );
	glUniform2i( screenShader.FindUniform( "StageSize" ), static_cast< GLint >( dest.Width ), static_cast< GLint >( dest.Height ) );
	glUniform2i( screenShader.FindUniform( "ProjectionSize" ), static_cast< GLint >( src.Width ), static_cast< GLint >( src.Height ) );
	glUniform2i( screenShader.FindUniform( "StageHardware" ), static_cast< GLint >( dest.HardwareWidth ),
	             static_cast< GLint >( dest.HardwareHeight ) );
	glUniform2i( screenShader.FindUniform( "ProjectionHardware" ), static_cast< GLint >( src.HardwareWidth ),
	             static_cast< GLint >( src.HardwareHeight ) );
	screenShader.Set( "Linear", s.asked.linear ? 1 : 0 );
	screenShader.Set( "Reflect", static_cast< float >( s.reflect[ 0 ] ), static_cast< float >( s.reflect[ 1 ] ),
	                  static_cast< float >( s.reflect[ 2 ] ) );
	screenShader.Set( "Open", static_cast< float >( s.asked.open ) );
	screenShader.Set( "Front", static_cast< float >( s.frontGain ) );
	screenShader.Set( "Wash", static_cast< float >( s.wash ) );
	screenShader.Set( "Back", static_cast< float >( s.back ) );
	screenShader.Set( "SpillGain", static_cast< float >( s.spillGain ) );
	screenShader.Set( "InvApparent", static_cast< float >( s.invApparent ) );
	screenShader.Set( "Radius", static_cast< float >( s.radiusPx ) );
	screenShader.Set( "DrawMesh", s.asked.drawMesh ? 1 : 0 );
	glUniform2i( screenShader.FindUniform( "FreqHi" ), s.freqHi[ 0 ], s.freqHi[ 1 ] );
	screenShader.Set( "FreqLo", static_cast< float >( s.freqLo[ 0 ] ), static_cast< float >( s.freqLo[ 1 ] ) );
	screenShader.Set( "Freq", static_cast< float >( s.freq[ 0 ] ), static_cast< float >( s.freq[ 1 ] ) );
	screenShader.Set( "OpenAxis", static_cast< float >( s.openAxis[ 0 ] ), static_cast< float >( s.openAxis[ 1 ] ) );
	screenShader.Set( "Aperture", static_cast< float >( s.aperture ) );
	screenShader.Set( "Fault", fault );

	quad.Draw();

	//Leave nothing bound: a texture left on unit 1 stays bound into the
	//host's context.
	if( fault & kFaultLeaveBound )
		glActiveTexture( GL_TEXTURE0 );
	else
		unbindTextureUnits( 2 );
	glUseProgram( 0 );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Scrim::DeInitGL()
{
	if( frames > 0 )
		diag::info( "DeInitGL after " + std::to_string( frames ) + " frames, last Opacity " + std::to_string( params[ PT_OPACITY ] )
		            + " (instance #" + std::to_string( instance ) + ")" );
	screenShader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Scrim::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	params[ index ] = value;
	return FF_SUCCESS;
}

float Scrim::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;

	return params[ index ];
}

//---------------------------------------------------------------------------
char* Scrim::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Scrim::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only, so there is genuinely
	// nothing to store -- but it has to say so successfully.
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

//---------------------------------------------------------------------------
void Scrim::SetHostInfo( const char* hostname, const char* version )
{
	CFFGLPlugin::SetHostInfo( hostname, version );
	diag::info( std::string( "host=" ) + ( hostname ? hostname : "?" ) + " version=" + ( version ? version : "?" ) + " loaded from "
	            + diag::modulePath() );
}

#pragma once

/**
    smtest's plumbing: checks, pictures, a headless GL context and the
    two-input rig. penumbra's putest's, adapted: the inputs here are STRAIGHT
    alpha (the decision in SPEC-scrim.md), either can be a float texture, and
    there is no model of the plugin in here at all -- every prediction is made
    in main.cpp from the harness's own constants (ShowTex's, Panasonic's, TI's)
    and its own sRGB pair.
*/

#include "Controls.h"
#include "Model.h"
#include "Names.h"
#include "Scrim.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace scrim;

namespace
{
int failures = 0;
int measured = 0;///< assertions that measured something, across the run
bool quiet   = false;

void Check( bool ok, const std::string& what )
{
	if( !quiet )
		std::printf( "  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str() );
	if( !ok )
		++failures;
	++measured;
}

/// A negative control: something the check must be able to REJECT. Passes
/// when `rejected` is true.
void Negative( bool rejected, const std::string& what )
{
	if( !quiet )
		std::printf( "  %s  negative control: %s\n", rejected ? "ok  " : "FAIL", what.c_str() );
	if( !rejected )
		++failures;
}

/// EVERY conversion must be a floating-point one.
__attribute__( ( format( printf, 1, 0 ) ) ) std::string fmt( const char* format, double a, double b = 0.0, double c = 0.0, double d = 0.0 )
{
	char buffer[ 320 ];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	std::snprintf( buffer, sizeof( buffer ), format, a, b, c, d );
#pragma clang diagnostic pop
	return buffer;
}

/// Run a check silently and return how many of its assertions failed,
/// leaving the global count as it was: how a negative control asks "did the
/// check reject this?".
template< typename F >
int failuresOf( F&& run )
{
	const int before = failures;
	const bool was   = quiet;
	quiet            = true;
	run();
	quiet            = was;
	const int caught = failures - before;
	failures         = before;
	return caught;
}

/// The worse of two errors, where a NaN is the worst of all and stays so: a
/// NaN pixel must fail a check, and std::max( worst, NaN ) quietly keeps worst.
double worse( double worst, double error )
{
	if( std::isnan( worst ) || std::isnan( error ) )
		return std::numeric_limits< double >::quiet_NaN();
	return std::max( worst, error );
}

/// Passes only if `worst` is a number and within `tolerance`.
bool within( double worst, double tolerance )
{
	return !std::isnan( worst ) && worst <= tolerance;
}

constexpr double kFloatUlp = 1.0 / 16777216.0;///< 2^-24, float32's unit roundoff at 1

/// The sRGB pair's budget on this plugin's chain, absolute, on an encoded
/// output in 0..1 -- a bound, derived in AGENTS.md ("Tolerances") from
/// penumbra's grid evaluation of GLSL 4.10 4.5.1 (pow is exp2( y log2 x )):
/// decode is good to 5.1e-6 relative, encode to 6.8e-7 absolute. Scrim's
/// output is a sum of terms each with at most two decoded factors (the set's
/// colour times the projection's, in the spill), so its relative error is at
/// most 2 x 5.1e-6 plus sixteen float roundings, 1.12e-5; encode turns a
/// relative error e in y into y encode'( y ) e = 0.44 y^(1/2.4) e <= 0.44 e
/// absolute, 4.9e-6, plus its own 6.8e-7: 5.6e-6. 6e-6 is used; tolerances
/// are three times it.
constexpr double kPowBound = 6e-6;
constexpr double kPowTol   = 3.0 * kPowBound;

/// The same, in linear light after the harness decodes the output: decode's
/// slope is at most 2.4 / 1.055 x ( 1.055 / 1.055 )^1.4 = 2.275 on 0..1.
constexpr double kLinearBound = 2.275 * kPowBound;

/// The sRGB pair, IEC 61966-2-1:1999 (amended 2003): the harness's own copy.
/// The plugin's (Controls.cpp) decodes the Thread picker, so it must not also
/// be the yardstick -- a mutant there would move both sides at once.
double srgbToLinear( double v )
{
	if( v <= 0.04045 )
		return v / 12.92;
	return std::pow( ( v + 0.055 ) / 1.055, 2.4 );
}

double linearToSrgb( double v )
{
	if( v <= 0.0031308 )
		return v * 12.92;
	return 1.055 * std::pow( v, 1.0 / 2.4 ) - 0.055;
}

//---------------------------------------------------------------------------
// PNG writer. zlib ships with the OS. Rows BOTTOM-UP in, top-down out.
//---------------------------------------------------------------------------
using Image  = std::vector< unsigned char >;
using ImageF = std::vector< float >;

void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const Image& bottomUp )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		const unsigned char* row = bottomUp.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// Pictures. Bottom-up, GL's orientation: row 0 is the BOTTOM. RGBA,
// STRAIGHT alpha, as Arena's clips arrive (SPEC-scrim.md).
//---------------------------------------------------------------------------
struct Rgb
{
	unsigned char r, g, b;
};

unsigned char toByte( double v )
{
	return static_cast< unsigned char >( std::lround( std::clamp( v, 0.0, 1.0 ) * 255.0 ) );
}

constexpr Rgb kBlack        = { 0, 0, 0 };
constexpr Rgb kWhite        = { 255, 255, 255 };
constexpr Rgb kSentinel     = { 0xFF, 0x00, 0xFF };///< padding: a colour that appears nowhere else
constexpr int kSentinelNear = 135;

size_t at4( int width, int x, int y )
{
	return ( static_cast< size_t >( y ) * width + x ) * 4;
}

void put( Image& image, int width, int x, int y, Rgb c, unsigned char a = 255 )
{
	const size_t at = at4( width, x, y );
	image[ at + 0 ] = c.r;
	image[ at + 1 ] = c.g;
	image[ at + 2 ] = c.b;
	image[ at + 3 ] = a;
}

Rgb pixelAt( const Image& image, int width, int x, int y )
{
	const size_t at = at4( width, x, y );
	return { image[ at + 0 ], image[ at + 1 ], image[ at + 2 ] };
}

int l1( Rgb a, Rgb b )
{
	return std::abs( a.r - b.r ) + std::abs( a.g - b.g ) + std::abs( a.b - b.b );
}

Image flatField( int width, int height, Rgb c, unsigned char alpha = 255 )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( image, width, x, y, c, alpha );
	return image;
}

ImageF flatFieldF( int width, int height, float r, float g, float b, float a = 1.0f )
{
	ImageF image( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < image.size(); i += 4 )
	{
		image[ i + 0 ] = r;
		image[ i + 1 ] = g;
		image[ i + 2 ] = b;
		image[ i + 3 ] = a;
	}
	return image;
}

void hsv( double h, double s, double v, double& r, double& g, double& b )
{
	h              = h - std::floor( h );
	const int i    = static_cast< int >( h * 6.0 ) % 6;
	const double f = h * 6.0 - std::floor( h * 6.0 );
	const double p = v * ( 1 - s ), q = v * ( 1 - f * s ), t = v * ( 1 - ( 1 - f ) * s );
	switch( i )
	{
	case 0: r = v, g = t, b = p; break;
	case 1: r = q, g = v, b = p; break;
	case 2: r = p, g = v, b = t; break;
	case 3: r = p, g = q, b = v; break;
	case 4: r = t, g = p, b = v; break;
	default: r = v, g = p, b = q; break;
	}
}

/// Where the cards' alpha is half, and where it is zero: a band and a hole,
/// as the demo clips have.
double cardAlpha( double u, double v )
{
	if( u > 0.66 && u < 0.9 && v > 0.12 && v < 0.42 )
		return 0.0;
	if( v > 0.6 && v < 0.8 )
		return 0.5;
	return 1.0;
}

/// The scene behind the gauze: a set, lit. Hue across, a brightness wave
/// down, a grid of thin light lines; every pixel coloured.
Image stageCard( int width, int height, bool withAlpha = false, Rgb under = kBlack )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			double r, g, b;
			hsv( 0.05 + u * 0.8, 0.55, 0.55 + 0.4 * std::cos( v * 5.0 ), r, g, b );
			if( ( x % std::max( 8, width / 12 ) ) == 0 || ( y % std::max( 8, height / 7 ) ) == 0 )
				r = g = b = 0.9;
			const double a = withAlpha ? cardAlpha( u, v ) : 1.0;
			if( a == 0.0 )
				put( image, width, x, y, under, 0 );
			else
				put( image, width, x, y, { toByte( r ), toByte( g ), toByte( b ) }, toByte( a ) );
		}
	return image;
}

/// What the projector sends: rings of hue about the frame's centre over a
/// diagonal ramp -- distinct from the stage card everywhere, every pixel
/// coloured.
Image projectionCard( int width, int height, bool withAlpha = false, Rgb under = kBlack )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			const double d = std::hypot( ( u - 0.5 ) * width / height, v - 0.5 );
			double r, g, b;
			hsv( 0.6 + 2.2 * d, 0.7, 0.45 + 0.5 * ( 0.5 + 0.5 * ( u - v ) ), r, g, b );
			const double a = withAlpha ? cardAlpha( 1.0 - u, 1.0 - v ) : 1.0;
			if( a == 0.0 )
				put( image, width, x, y, under, 0 );
			else
				put( image, width, x, y, { toByte( r ), toByte( g ), toByte( b ) }, toByte( a ) );
		}
	return image;
}

/// Four flat quadrants, for --mixer.
struct QuadCard
{
	Image image;
	Rgb quadrant[ 4 ];
};

QuadCard quadCard( int width, int height, bool second = false )
{
	QuadCard c;
	if( second )
	{
		c.quadrant[ 0 ] = { 0x40, 0xA0, 0xD0 };
		c.quadrant[ 1 ] = { 0xD0, 0x60, 0x20 };
		c.quadrant[ 2 ] = { 0x90, 0xD0, 0x50 };
		c.quadrant[ 3 ] = { 0x60, 0x30, 0xB0 };
	}
	else
	{
		c.quadrant[ 0 ] = { 0xC0, 0x30, 0x30 };
		c.quadrant[ 1 ] = { 0x30, 0xC0, 0x30 };
		c.quadrant[ 2 ] = { 0x30, 0x30, 0xC0 };
		c.quadrant[ 3 ] = { 0xC0, 0xC0, 0x30 };
	}
	c.image = Image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( c.image, width, x, y, c.quadrant[ ( y >= height / 2 ? 2 : 0 ) + ( x >= width / 2 ? 1 : 0 ) ] );
	return c;
}

Image generate( const std::string& name, int width, int height )
{
	if( name == "stage" )
		return stageCard( width, height );
	if( name == "stage-alpha" )
		return stageCard( width, height, true );
	if( name == "projection" )
		return projectionCard( width, height );
	if( name == "projection-alpha" )
		return projectionCard( width, height, true );
	if( name == "quads" )
		return quadCard( width, height ).image;
	if( name == "black" )
		return flatField( width, height, kBlack );
	if( name == "white" )
		return flatField( width, height, kWhite );
	if( name == "clear" )
		return flatField( width, height, kBlack, 0 );
	return flatField( width, height, { 0x80, 0x80, 0x80 } );//"grey"
}

const char* const kCardNames = "stage | stage-alpha | projection | projection-alpha | quads | black | white | clear | grey";

bool isCard( const std::string& name )
{
	static const char* names[] = { "stage", "stage-alpha", "projection", "projection-alpha", "quads", "black", "white", "clear", "grey" };
	for( const char* n : names )
		if( name == n )
			return true;
	return false;
}

/// A reproducible pseudo-random float in [0, 1): PCG's output function over
/// a counter, so a card is the same on every run and every machine.
float hashUnit( uint32_t n )
{
	uint32_t state = n * 747796405u + 2891336453u;
	uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	word           = ( word >> 22u ) ^ word;
	return static_cast< float >( word >> 8 ) / 16777216.0f;
}

/// Random straight-alpha RGBA floats, colour anywhere in 0..1; alpha 1, or
/// anywhere in 0..1.
ImageF randomStraight( int width, int height, uint32_t seed, bool opaque )
{
	ImageF image( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < image.size(); i += 4 )
	{
		const uint32_t n = seed * 7919u + static_cast< uint32_t >( i );
		image[ i + 0 ]   = hashUnit( n + 0 );
		image[ i + 1 ]   = hashUnit( n + 1 );
		image[ i + 2 ]   = hashUnit( n + 2 );
		image[ i + 3 ]   = opaque ? 1.0f : hashUnit( n + 3 );
	}
	return image;
}

ImageF toFloat( const Image& bytes )
{
	ImageF image( bytes.size() );
	for( size_t i = 0; i < bytes.size(); ++i )
		image[ i ] = bytes[ i ] / 255.0f;
	return image;
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
bool softwareRenderer()
{
	const char* renderer = std::getenv( "SMTEST_RENDERER" );
	return renderer != nullptr && std::strcmp( renderer, "software" ) == 0;
}

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	//SMTEST_RENDERER=software asks for Apple's software renderer by id, on a
	//Mac that has a GPU: what a GPU-less runner falls back to.
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( softwareRenderer() )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		std::fprintf( stderr, "smtest: SMTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, bool floatFormat, const void* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	if( floatFormat )
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	else
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
	//LINEAR, as a host's textures are: the hardware filter is what
	//kFaultHardwareBilinear reaches for.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

struct InputSpec
{
	int usedW = 0, usedH = 0;
	int hwW = 0, hwH = 0;
	bool floating = false;
	static InputSpec Exact( int w, int h, bool f = false )
	{
		return { w, h, w, h, f };
	}
	static InputSpec Padded( int w, int h, int hw, int hh, bool f = false )
	{
		return { w, h, hw, hh, f };
	}
};

/// Set a parameter by display name, in host units.
bool setByName( Scrim& plugin, const std::string& name, float value )
{
	for( unsigned int i = 0; i < Scrim::PT_ABOUT_FIRST; ++i )
	{
		const char* declared = plugin.GetParamName( i );
		if( declared != nullptr && name == declared )
		{
			plugin.SetFloatParameter( i, value );
			return true;
		}
	}
	std::fprintf( stderr, "no parameter called '%s'\n", name.c_str() );
	return false;
}

/// The output framebuffer, its texture, and reading it back.
struct Target
{
	int width = 0, height = 0;
	GLuint texture = 0, fbo = 0;

	void Init( int w, int h, bool floatOutput )
	{
		width   = w;
		height  = h;
		texture = makeTexture( w, h, floatOutput, nullptr );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	}

	void Begin()
	{
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	Image Pixels()
	{
		Image pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		return pixels;
	}

	ImageF PixelsF()
	{
		ImageF pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	void Release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( texture )
			glDeleteTextures( 1, &texture );
		fbo = texture = 0;
	}
};

//---------------------------------------------------------------------------
// The mixer's rig: SW Scrim, TWO input textures and an output, each at its
// own size. inputTextures[0] is Dest (the scene behind, `--input-a`), [1] is
// Src (the projection, `--input-b`). Padding is the sentinel, opaque.
//---------------------------------------------------------------------------
struct MixRig
{
	Scrim plugin;
	Target out;
	InputSpec destSpec, srcSpec;
	GLuint destTexture = 0, srcTexture = 0;
	FFGLTextureStruct destStruct = {}, srcStruct = {};
	FFGLTextureStruct* inputs[ 2 ] = { nullptr, nullptr };
	ProcessOpenGLStruct process    = {};
	bool ready                     = false;

	bool Init( int outW, int outH, InputSpec dest, InputSpec src, bool floatOutput = false, int fault = 0, const char* fragment = nullptr )
	{
		plugin.SetFaultForTest( fault );
		plugin.SetFragmentShaderForTest( fragment );
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( outW );
		viewport.height             = static_cast< FFUInt32 >( outH );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for the shader\n" );
			return false;
		}
		MakeInputs( dest, src );
		out.Init( outW, outH, floatOutput );
		process.numInputTextures = 2;
		process.inputTextures    = inputs;
		process.HostFBO          = out.fbo;
		ready                    = true;
		return true;
	}

	/// Both inputs at the output's size; `floating` makes inputs and output
	/// float textures, for the checks that claim bits.
	bool Init( int outW, int outH, bool floating = false, int fault = 0, const char* fragment = nullptr )
	{
		return Init( outW, outH, InputSpec::Exact( outW, outH, floating ), InputSpec::Exact( outW, outH, floating ), floating, fault, fragment );
	}

	static GLuint padded( const InputSpec& s )
	{
		if( s.floating )
		{
			ImageF pad( static_cast< size_t >( s.hwW ) * s.hwH * 4 );
			for( size_t i = 0; i < pad.size(); i += 4 )
				pad[ i ] = pad[ i + 2 ] = pad[ i + 3 ] = 1.0f;
			return makeTexture( s.hwW, s.hwH, true, pad.data() );
		}
		return makeTexture( s.hwW, s.hwH, false, flatField( s.hwW, s.hwH, kSentinel ).data() );
	}

	void MakeInputs( InputSpec dest, InputSpec src )
	{
		if( destTexture )
			glDeleteTextures( 1, &destTexture );
		if( srcTexture )
			glDeleteTextures( 1, &srcTexture );
		destSpec    = dest;
		srcSpec     = src;
		destTexture = padded( dest );
		srcTexture  = padded( src );
		auto fill   = []( FFGLTextureStruct& t, const InputSpec& s, GLuint handle ) {
            t.Width          = static_cast< FFUInt32 >( s.usedW );
            t.Height         = static_cast< FFUInt32 >( s.usedH );
            t.HardwareWidth  = static_cast< FFUInt32 >( s.hwW );
            t.HardwareHeight = static_cast< FFUInt32 >( s.hwH );
            t.Handle         = handle;
		};
		fill( destStruct, dest, destTexture );
		fill( srcStruct, src, srcTexture );
		inputs[ 0 ] = &destStruct;
		inputs[ 1 ] = &srcStruct;
	}

	/// A new output size, as a host's FF_RESIZE.
	void ResizeOutput( int outW, int outH, bool floatOutput = false )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( outW );
		viewport.height             = static_cast< FFUInt32 >( outH );
		plugin.Resize( &viewport );
		out.Release();
		out.Init( outW, outH, floatOutput );
		process.HostFBO = out.fbo;
	}

	static void upload( GLuint texture, const InputSpec& s, const void* pixels, bool floatPixels )
	{
		glBindTexture( GL_TEXTURE_2D, texture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, s.usedW, s.usedH, GL_RGBA, floatPixels ? GL_FLOAT : GL_UNSIGNED_BYTE, pixels );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void UploadDest( const Image& bottomUp )
	{
		upload( destTexture, destSpec, bottomUp.data(), false );
	}
	void UploadDest( const ImageF& bottomUp )
	{
		upload( destTexture, destSpec, bottomUp.data(), true );
	}
	void UploadSrc( const Image& bottomUp )
	{
		upload( srcTexture, srcSpec, bottomUp.data(), false );
	}
	void UploadSrc( const ImageF& bottomUp )
	{
		upload( srcTexture, srcSpec, bottomUp.data(), true );
	}

	bool Set( const std::string& name, float value )
	{
		return setByName( plugin, name, value );
	}

	void SetIndex( unsigned id, float value )
	{
		plugin.SetFloatParameter( id, value );
	}

	bool Render()
	{
		out.Begin();
		return plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
	}

	FFResult RenderBroken( int numInputs, int nullIndex, bool zeroSize = false, bool zeroViewport = false )
	{
		FFGLTextureStruct* saved[ 2 ]     = { inputs[ 0 ], inputs[ 1 ] };
		const FFGLTextureStruct savedDest = destStruct;
		if( nullIndex >= 0 && nullIndex < 2 )
			inputs[ nullIndex ] = nullptr;
		if( nullIndex == -2 )
			process.inputTextures = nullptr;
		if( zeroSize )
			destStruct.HardwareWidth = 0;
		FFGLViewportStruct viewport = {};
		if( zeroViewport )
			plugin.Resize( &viewport );
		process.numInputTextures = static_cast< FFUInt32 >( numInputs );
		out.Begin();
		const FFResult result = plugin.ProcessOpenGL( &process );
		if( zeroViewport )
		{
			viewport.width  = static_cast< FFUInt32 >( out.width );
			viewport.height = static_cast< FFUInt32 >( out.height );
			plugin.Resize( &viewport );
		}
		inputs[ 0 ]              = saved[ 0 ];
		inputs[ 1 ]              = saved[ 1 ];
		destStruct               = savedDest;
		process.inputTextures    = inputs;
		process.numInputTextures = 2;
		return result;
	}

	Image Pixels()
	{
		return out.Pixels();
	}

	ImageF PixelsF()
	{
		return out.PixelsF();
	}

	~MixRig()
	{
		if( !ready )
			return;
		plugin.DeInitGL();
		out.Release();
		glDeleteTextures( 1, &srcTexture );
		glDeleteTextures( 1, &destTexture );
	}
};

double channel( const ImageF& img, int width, int x, int y, int c )
{
	return img[ at4( width, x, y ) + static_cast< size_t >( c ) ];
}

size_t differingBytes( const Image& a, const Image& b )
{
	if( a.size() != b.size() )
		return std::max( a.size(), b.size() );
	size_t n = 0;
	for( size_t i = 0; i < a.size(); ++i )
		n += a[ i ] != b[ i ];
	return n;
}

/// Floats that differ in any bit (a NaN differs from everything, itself too).
size_t differingFloats( const ImageF& a, const ImageF& b )
{
	if( a.size() != b.size() )
		return std::max( a.size(), b.size() );
	size_t n = 0;
	for( size_t i = 0; i < a.size(); ++i )
		n += std::memcmp( &a[ i ], &b[ i ], sizeof( float ) ) != 0 || std::isnan( a[ i ] );
	return n;
}

} // namespace

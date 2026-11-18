// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.

// kVersion, source/Shaders.cpp
export const VERSION = `#version 410 core
`;

// kVertex, source/Shaders.cpp
export const VERTEX = `
layout( location = 0 ) in vec4 vPosition;

void main()
{
	gl_Position = vPosition;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
vec3 decode( vec3 c )
{
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), greaterThan( c, vec3( 0.04045 ) ) );
}

vec3 encode( vec3 x )
{
	return mix( x * 12.92, 1.055 * pow( x, vec3( 1.0 / 2.4 ) ) - 0.055, greaterThan( x, vec3( 0.0031308 ) ) );
}
`;

// kScreenFragment, source/Shaders.cpp
export const SCREEN_FRAGMENT = `
//inputTextures[0] -- Dest, the layer BELOW: the scene behind the gauze, as lit
//at Back Light 1.
uniform sampler2D Stage;
//inputTextures[1] -- Src, THIS layer: what the projector sends.
uniform sampler2D Projection;

uniform ivec2 OutSize;
uniform ivec2 StageSize;          //Dest's used size
uniform ivec2 ProjectionSize;     //Src's used size
uniform ivec2 StageHardware;      //Dest's allocated size: read only by two faults
uniform ivec2 ProjectionHardware; //Src's allocated size: read only by two faults
uniform int Linear;               //1: light summed in linear light
uniform vec3 Reflect;             //rho, the threads' reflectance
uniform float Open;               //phi, the holes' share
uniform float Front;              //Opacity x Front Light
uniform float Wash;               //a steep white wash on the threads
uniform float Back;               //the light on the scene behind
uniform float SpillGain;          //phi x Opacity x Front Light / M^2
uniform float InvApparent;        //1 / M_app: where on the projection the audience looks through
uniform float Radius;             //the defocus disc's radius, projection pixels
uniform int DrawMesh;             //1: the weave per pixel; 0: its average, phi
uniform ivec2 FreqHi;             //floor( 256 frac( f ) ), x and y
uniform vec2 FreqLo;              //frac( f ) - FreqHi / 256
uniform vec2 Freq;                //the weave, cycles per pixel, x and y
uniform vec2 OpenAxis;            //phi_x, phi_y
uniform float Aperture;           //the mirror's side, pixels: sqrt( Fill )

//The harness's negative controls. 0 in the shipped plugin.
uniform int Fault;

out vec4 fragColor;

const int kFaultOpacity          = 1;
const int kFaultSwapArea         = 2;
const int kFaultWashSpills       = 8;
const int kFaultGammaSum         = 16;
const int kFaultStraight         = 1024;
const int kFaultHardwareBilinear = 2048;
const int kFaultAlphaFromInput   = 4096;
const int kFaultPadded           = 16384;

//Below this the defocus disc is a point (Model.h's kPointRadiusPx).
const float kPointRadius = 1.0 / 64.0;

bool faulty( int bit )
{
	return ( Fault & bit ) != 0;
}

//A texel's light: straight alpha in, so its colour (decoded, with Linear
//Light) times its coverage. A transparent texel is no light, whatever colour
//it carries.
vec3 lightOf( vec4 t )
{
	vec3 c = clamp( t.rgb, 0.0, 1.0 );
	c      = Linear != 0 ? decode( c ) : c;
	return faulty( kFaultStraight ) ? c : c * clamp( t.a, 0.0, 1.0 );
}

//One input's light at integer texel q, clamped into its used area.
vec3 texelLight( sampler2D s, ivec2 q, ivec2 size, ivec2 hardware )
{
	ivec2 top = ( faulty( kFaultPadded ) ? hardware : size ) - ivec2( 1 );
	return lightOf( texelFetch( s, clamp( q, ivec2( 0 ), top ), 0 ) );
}

//An input's light at output pixel p, the input stretched over the frame:
//fetched where its size is the output's, otherwise a bilinear by hand of
//premultiplied light, so colour under a transparent texel never bleeds.
vec3 lightAt( sampler2D s, ivec2 p, ivec2 size, ivec2 hardware )
{
	if( size == OutSize )
		return texelLight( s, p, size, hardware );
	vec2 u = ( vec2( p ) + 0.5 ) * vec2( size ) / vec2( OutSize );
	if( faulty( kFaultHardwareBilinear ) )
		return lightOf( texture( s, u / vec2( hardware ) ) );
	u        = u - 0.5;
	vec2 b   = floor( u );
	vec2 f   = u - b;
	ivec2 q  = ivec2( b );
	vec3 l00 = texelLight( s, q, size, hardware );
	vec3 l10 = texelLight( s, q + ivec2( 1, 0 ), size, hardware );
	vec3 l01 = texelLight( s, q + ivec2( 0, 1 ), size, hardware );
	vec3 l11 = texelLight( s, q + ivec2( 1, 1 ), size, hardware );
	return mix( mix( l00, l10, f.x ), mix( l01, l11, f.x ), f.y );
}

//The projection's light at pixel q of its own frame. Off the frame the
//projector sends nothing.
vec3 projectionAt( ivec2 q )
{
	if( any( lessThan( q, ivec2( 0 ) ) ) || any( greaterThanEqual( q, OutSize ) ) )
		return vec3( 0.0 );
	return lightAt( Projection, q, ProjectionSize, ProjectionHardware );
}

//The spill: the projection's light integrated over the defocus disc of
//radius Radius around c, in its own frame. Rows of texels at the midpoint of
//their overlap with the disc; along each row the chord integrated over the
//texels exactly, each texel a box. Coordinates relative to the centre's
//texel, so they stay small at 4K.
vec3 spillAt( vec2 c )
{
	ivec2 base = ivec2( floor( c ) );
	if( Radius < kPointRadius )
		return projectionAt( base );
	vec2 o      = c - floor( c );
	int r       = int( ceil( Radius ) ) + 1;
	vec3 sum    = vec3( 0.0 );
	float total = 0.0;
	for( int j = -r; j <= r; ++j )
	{
		float y0 = max( float( j ) - o.y, -Radius );
		float y1 = min( float( j + 1 ) - o.y, Radius );
		if( y1 <= y0 )
			continue;
		float ym     = 0.5 * ( y0 + y1 );
		float chord  = sqrt( max( Radius * Radius - ym * ym, 0.0 ) );
		float height = y1 - y0;
		for( int i = -r; i <= r; ++i )
		{
			float x0 = max( float( i ) - o.x, -chord );
			float x1 = min( float( i + 1 ) - o.x, chord );
			if( x1 <= x0 )
				continue;
			float w = ( x1 - x0 ) * height;
			sum += projectionAt( base + ivec2( i, j ) ) * w;
			total += w;
		}
	}
	return total > 0.0 ? sum / total : vec3( 0.0 );
}

//The weave along one axis: each cycle is open for its first phiA. The open
//length from 0 to t cycles.
float openTo( float t, float phiA )
{
	float n = floor( t );
	return n * phiA + min( t - n, phiA );
}

//The weave's phase at the start of pixel i, cycles, in [0, 1): frac( i f ),
//its 8-bit part taken exactly in integers (Model.cpp).
float phaseAt( int i, int hi, float lo )
{
	return fract( float( ( i * hi ) & 255 ) / 256.0 + float( i ) * lo );
}

//The open fraction of one axis over the centred span w pixels wide.
float openOver( float phase, float f, float w, float phiA )
{
	float t0 = phase + ( 0.5 - 0.5 * w ) * f;
	float t1 = phase + ( 0.5 + 0.5 * w ) * f;
	return ( openTo( t1, phiA ) - openTo( t0, phiA ) ) / ( t1 - t0 );
}

void main()
{
	ivec2 p = ivec2( floor( gl_FragCoord.xy ) );

	//The holes' share: of the whole pixel, which is what the audience sees
	//through, and of the projector's mirror, which is where its light falls.
	float cBox = Open;
	float cAp  = Open;
	if( DrawMesh != 0 )
	{
		float px = phaseAt( p.x, FreqHi.x, FreqLo.x );
		float py = phaseAt( p.y, FreqHi.y, FreqLo.y );
		cBox     = openOver( px, Freq.x, 1.0, OpenAxis.x ) * openOver( py, Freq.y, 1.0, OpenAxis.y );
		cAp      = openOver( px, Freq.x, Aperture, OpenAxis.x ) * openOver( py, Freq.y, Aperture, OpenAxis.y );
	}
	if( faulty( kFaultSwapArea ) )
	{
		cBox = 1.0 - cBox;
		cAp  = 1.0 - cAp;
	}

	vec3 rho       = faulty( kFaultOpacity ) ? vec3( 1.0 ) : Reflect;
	vec3 projected = projectionAt( p );
	vec3 stage     = lightAt( Stage, p, StageSize, StageHardware );
	vec3 spill     = vec3( 0.0 );
	if( SpillGain > 0.0 )
	{
		vec2 centre = 0.5 * vec2( OutSize );
		spill       = spillAt( centre + ( vec2( p ) + 0.5 - centre ) * InvApparent );
	}

	//What the threads reflect, and what comes through the holes.
	vec3 reflected = rho * ( 1.0 - cAp ) * ( Front * projected ) + rho * ( 1.0 - cBox ) * Wash;
	vec3 behind    = Back + SpillGain * spill;
	if( faulty( kFaultWashSpills ) )
		behind += Open * Wash;
	vec3 passed = cBox * stage * behind;

	vec3 shown;
	if( Linear == 0 )
		shown = reflected + passed;
	else if( faulty( kFaultGammaSum ) )
		shown = encode( clamp( reflected, 0.0, 1.0 ) ) + encode( clamp( passed, 0.0, 1.0 ) );
	else
		shown = encode( clamp( reflected + passed, 0.0, 1.0 ) );

	//Opaque: the gauze fills the frame, black where nothing is lit.
	float alpha = 1.0;
	if( faulty( kFaultAlphaFromInput ) )
		alpha = texelFetch( Stage, clamp( p * StageSize / OutSize, ivec2( 0 ), StageSize - ivec2( 1 ) ), 0 ).a;
	fragColor = vec4( clamp( shown, 0.0, 1.0 ), alpha );
}
`;

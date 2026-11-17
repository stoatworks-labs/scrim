#include "Model.h"

#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace scrim
{

double ThreadFraction( double phi )
{
	//2 a^2 - 3 a + ( 1 - phi ) = 0, the root in [0, 1/2].
	return ( 3.0 - std::sqrt( 1.0 + 8.0 * std::clamp( phi, 0.0, 1.0 ) ) ) / 4.0;
}

Setup Plan( const Asked& asked, int width, int fault, double seconds )
{
	Setup s;
	s.asked = asked;
	const double phi = std::clamp( asked.open, 0.0, 1.0 );

	for( int c = 0; c < 3; ++c )
	{
		const double picked = std::clamp( asked.thread[ c ], 0.0, 1.0 );
		s.reflect[ c ]      = asked.linear ? DecodeSrgb( picked ) : picked;
	}

	s.frontGain = asked.opacity * asked.front;
	s.back      = asked.back + ( ( fault & kFaultBackAlways ) ? 1.0 / 64.0 : 0.0 );
	s.wash      = asked.wash;

	//The spill (SPEC-scrim.md, "The spill"). Similar triangles from the lens
	//through the gauze to the set: a ray through gauze point x lands at
	//x ( 1 + k ); the audience, v throws out, sees that set point at
	//x ( 1 + k ) v / ( v + k ) on the gauze.
	const double k   = std::max( asked.spill, 0.0 );
	const double v   = std::max( asked.audience, 1e-6 );
	s.magnification  = 1.0 + k;
	s.apparent       = ( 1.0 + k ) / ( 1.0 + k / v );
	s.invApparent    = 1.0 / ( ( fault & kFaultViewerScale ) ? s.magnification : s.apparent );
	const double spread = ( fault & kFaultNoSpread ) ? 1.0 : s.magnification * s.magnification;
	s.spillGain      = phi * s.frontGain / spread;

	//The defocus disc, referred to the gauze (the circle of confusion,
	//C = A |S2 - S1| / S2 with S1 = T, S2 = T + s): radius A k / ( 2 ( 1 + k ) )
	//in gauze metres, in frame widths over the gauze's width.
	const double pupilM    = kPupilMm / 1000.0;
	const double widthM    = std::max( asked.widthM, 1e-6 );
	const double onGauze   = pupilM * k / ( 2.0 * ( 1.0 + k ) );
	const double onSet     = pupilM * k / 2.0;
	const double radiusM   = ( fault & kFaultBlurOnSet ) ? onSet : onGauze;
	const double radius    = radiusM / widthM * width;
	s.radiusCapped         = radius > kMaxRadiusPx;
	s.radiusPx             = std::min( radius, kMaxRadiusPx );

	//The weave: a cell Mesh wide and Mesh / 2 tall, threads t wide both ways.
	double mesh = std::max( asked.meshMm, 1e-6 );
	if( fault & kFaultClock )
		mesh *= 1.0 + 1e-4 * seconds;
	s.pitchPx       = mesh / 1000.0 / widthM * width;
	s.freq[ 0 ]     = 1.0 / s.pitchPx;
	s.freq[ 1 ]     = ( fault & kFaultSquareCell ) ? s.freq[ 0 ] : 2.0 * s.freq[ 0 ];
	const double a  = ThreadFraction( phi );
	s.openAxis[ 0 ] = 1.0 - a;
	s.openAxis[ 1 ] = 1.0 - 2.0 * a;
	for( int axis = 0; axis < 2; ++axis )
	{
		//The phase at integer pixel i is frac( i f ) = frac( i frac( f ) ):
		//split frac( f ) into an 8-bit part, whose product with i the shader
		//takes exactly in integers, and the rest (under 1/256), whose product
		//is under 64 cycles and good to a float's 2^-18 there.
		const double g   = s.freq[ axis ] - std::floor( s.freq[ axis ] );
		s.freqHi[ axis ] = std::clamp( static_cast< int >( std::floor( g * 256.0 ) ), 0, 255 );
		s.freqLo[ axis ] = g - s.freqHi[ axis ] / 256.0;
	}
	s.aperture = ( fault & kFaultBoxAperture ) ? 1.0 : std::sqrt( std::clamp( asked.fill, 0.0, 1.0 ) );
	return s;
}

} // namespace scrim

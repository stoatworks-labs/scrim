#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace scrim
{
namespace
{
double unit( float value )
{
	return std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}

float toParam( double p )
{
	return static_cast< float >( std::clamp( p, 0.0, 1.0 ) );
}
} // namespace

double LightFromParam( float value )
{
	const double p = unit( value );
	return kLightMax * p * p;
}

float ParamForLight( double light )
{
	return toParam( std::sqrt( std::max( light, 0.0 ) / kLightMax ) );
}

double FillFromParam( float value )
{
	return kFillMin + ( 1.0 - kFillMin ) * unit( value );
}

float ParamForFill( double fill )
{
	return toParam( ( fill - kFillMin ) / ( 1.0 - kFillMin ) );
}

double SpillFromParam( float value )
{
	return kSpillMax * unit( value );
}

float ParamForSpill( double k )
{
	return toParam( k / kSpillMax );
}

double AudienceFromParam( float value )
{
	return kAudienceMin * std::pow( kAudienceRange, unit( value ) );
}

float ParamForAudience( double v )
{
	return toParam( std::log( std::max( v, 1e-9 ) / kAudienceMin ) / std::log( kAudienceRange ) );
}

double MeshFromParam( float value )
{
	return kMeshMinMm * std::pow( kMeshRange, unit( value ) );
}

float ParamForMesh( double mm )
{
	return toParam( std::log( std::max( mm, 1e-9 ) / kMeshMinMm ) / std::log( kMeshRange ) );
}

double WidthFromParam( float value )
{
	return kWidthMinM * std::pow( kWidthRange, unit( value ) );
}

float ParamForWidth( double metres )
{
	return toParam( std::log( std::max( metres, 1e-9 ) / kWidthMinM ) / std::log( kWidthRange ) );
}

double DecodeSrgb( double c )
{
	return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
}

double EncodeSrgb( double x )
{
	return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow( x, 1.0 / 2.4 ) - 0.055;
}

} // namespace scrim

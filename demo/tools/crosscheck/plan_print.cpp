// The C++ half of port_numbers.py: the plugin's own Controls.cpp and
// Model.cpp, compiled as they are, fed host values and printing every number
// Plan works out. plan_print.mjs prints the same from demo/port.js.
//
// stdin: one case a line, the 15 host values (index order, 0..1 floats) then
// the output width. stdout: one line a case, every field of Setup (and of
// its Asked) as %.17g, in the order plan_print.mjs prints them.
//
// asked() is Scrim::asked()'s body, copied from source/Scrim.cpp line for line
// (it needs the plugin class otherwise); port_numbers.py fails if those lines
// are not still in Scrim.cpp as they are here.
#include "Controls.h"
#include "Model.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using namespace scrim;

enum : int
{
	PT_LINEAR_LIGHT,
	PT_OPACITY,
	PT_FRONT_LIGHT,
	PT_PIXEL_FILL,
	PT_BACK_LIGHT,
	PT_WASH,
	PT_SPILL_DISTANCE,
	PT_AUDIENCE,
	PT_OPEN_AREA,
	PT_THREAD_RED,
	PT_THREAD_GREEN,
	PT_THREAD_BLUE,
	PT_MESH,
	PT_DRAW_MESH,
	PT_GAUZE_WIDTH,
	PT_ABOUT_FIRST,
};

static float params[ PT_ABOUT_FIRST ];

static Asked asked()
{
	// ---- Scrim::asked(), verbatim ----
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
	// ---- end ----
}

static void put( double v ) { std::printf( " %.17g", v ); }

int main()
{
	std::string line;
	while( std::getline( std::cin, line ) )
	{
		std::istringstream in( line );
		for( float& p : params )
			in >> p;
		int width = 0;
		in >> width;
		if( !in )
			continue;
		const Setup s = Plan( asked(), width, kFaultNone, 0.0 );
		const Asked& a = s.asked;
		std::printf( "%d", a.linear ? 1 : 0 );
		for( double v : { a.opacity, a.front, a.fill, a.back, a.wash, a.spill, a.audience, a.open, a.thread[ 0 ], a.thread[ 1 ], a.thread[ 2 ], a.meshMm } )
			put( v );
		std::printf( " %d", a.drawMesh ? 1 : 0 );
		put( a.widthM );
		for( double v : { s.reflect[ 0 ], s.reflect[ 1 ], s.reflect[ 2 ], s.frontGain, s.back, s.wash, s.spillGain, s.magnification, s.apparent, s.invApparent, s.radiusPx } )
			put( v );
		std::printf( " %d", s.radiusCapped ? 1 : 0 );
		for( double v : { s.pitchPx, s.freq[ 0 ], s.freq[ 1 ] } )
			put( v );
		std::printf( " %d %d", s.freqHi[ 0 ], s.freqHi[ 1 ] );
		for( double v : { s.freqLo[ 0 ], s.freqLo[ 1 ], s.openAxis[ 0 ], s.openAxis[ 1 ], s.aperture } )
			put( v );
		std::printf( "\n" );
	}
	return 0;
}

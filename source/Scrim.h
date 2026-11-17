#pragma once

#include <FFGLSDK.h>

//AFTER the SDK: this header names FFUInt32 and does not pull the SDK in
//itself, so an include placed above it fails with "unknown type name" errors
//that point at the About block rather than at the include order.
#include "StoatworksAboutParams.h"
#include "Model.h"

#include <string>

/**
    Scrim -- a theatre gauze, as an FFGL **mixer** for Resolume: a projection
    on the gauze, a scene behind it, and the light that decides which one the
    audience sees.

    Src, the layer whose blend mode this is, is what the projector sends.
    Dest, the layer below, is the scene behind the gauze as lit at Back Light
    1. What the audience sees at a point is the light the threads reflect plus
    the light that comes through the holes, so the gauze looks opaque when the
    front is lit and the back dark, and vanishes the other way round -- and the
    projection's light that passes the holes lands on the set, magnified,
    dimmer and out of focus. See Model.h and AGENTS.md.

    **This is the fleet's seventh FF_MIXER.** Read the mixer section of
    genlock's AGENTS.md before changing `ProcessOpenGL`: what is written
    there was measured, and several things the SDK's headers imply are not
    true.

    No state from one frame to the next, and no buffers: one pass into the
    host's framebuffer. Nothing moves with time.
*/
class Scrim : public CFFGLPlugin
{
public:
	Scrim();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	/// Changes no pixel. It exists so that ONE session in front of Resolume
	/// leaves a log that says which host and which bundle.
	void SetHostInfo( const char* hostname, const char* version ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default.
	/// instantiateGL pushes every declared default back through the setters
	/// and deletes the whole instance if one fails, and CFFGLPlugin's
	/// SetTextParameter is a stub that returns exactly that failure.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	/// Optional, not required -- see genlock's AGENTS.md. A four-character
	/// label for a host with no room for "SW Scrim".
	const char* GetShortName() override
	{
		return "Scrm";
	}

	/// What the last rendered frame used, for the harness's diagnostics.
	const scrim::Setup& SetupForTest() const
	{
		return lastSetup;
	}

	/// What the parameters ask for, in physical units, through Controls.
	scrim::Asked AskedForTest() const
	{
		return asked();
	}

	/// The negative controls: a bitmask of scrim::Fault, 0 in the shipped
	/// plugin, reachable only from here. Each one builds the wrong answer to
	/// a check into the real ProcessOpenGL or the real shader, so the check
	/// can be shown to reject the plugin's own wrong version.
	void SetFaultForTest( int mask )
	{
		fault = mask;
	}

	/// The mutation test: compile this fragment shader instead of the
	/// shipped one. Call before InitGL. Null restores the shipped text.
	void SetFragmentShaderForTest( const char* text )
	{
		fragmentOverride = text;
	}

	/// In the order the host shows them.
	///
	/// **Index 0 is sacrificial.** Resolume Arena 7.27.1 does not expose a
	/// mixer's FIRST parameter at all -- measured on genlock, wipe, relay,
	/// lenticular and pepper: it is absent from Arena's mixer panel and REST
	/// JSON, so it is stuck at its default. Whatever sits here must be a
	/// control whose default is right if nobody can ever reach it: Linear
	/// Light, on, which is how light adds. Arena, not the harness, must
	/// confirm which parameter it hides.
	enum ParamID : FFUInt32
	{
		//Light
		PT_LINEAR_LIGHT,

		//Projector
		PT_OPACITY,///< the projector's level: Arena binds it to the LAYER's fader
		PT_FRONT_LIGHT,
		PT_PIXEL_FILL,

		//Stage
		PT_BACK_LIGHT,
		PT_WASH,
		PT_SPILL_DISTANCE,
		PT_AUDIENCE,

		//Gauze
		PT_OPEN_AREA,
		PT_THREAD_RED,
		PT_THREAD_GREEN,
		PT_THREAD_BLUE,
		PT_MESH,
		PT_DRAW_MESH,
		PT_GAUZE_WIDTH,

		//About. Last in the enum so nothing before it ever moves.
		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

private:
	scrim::Asked asked() const;

	ffglex::FFGLShader screenShader;
	ffglex::FFGLScreenQuad quad;

	scrim::Setup lastSetup;

	float params[ PT_COUNT ] = {};

	int fault                    = 0;
	const char* fragmentOverride = nullptr;
	/// Read only under kFaultStaleSize: the first frame's width, kept.
	int firstWidth = 0;

	//The host-session log: what reached the mixer. Nothing renders from it.
	unsigned long frames  = 0;
	int instance          = 0;
	int opacityLines      = 0;
	float loggedOpacity   = -1.0f;
	unsigned guardsLogged = 0;
	bool loggedCap        = false;
	void LogGuard( unsigned bit, const char* what );

	/// GetTextParameter hands the host a bare pointer, so the string has to
	/// outlive the call.
	std::string aboutText;
};

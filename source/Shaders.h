#pragma once

#include <string>

/**
    The one pass, as GLSL.

    Per output pixel: the weave's open share of the pixel and of the
    projector's mirror (closed-form box integrals of a bar pattern, separable
    in x and y), the projection's light here, the set's light here, the
    projection integrated over the defocus disc around the point the audience
    sees through (the spill), and the sum -- reflected plus passed -- in linear
    light. Model.h has the formula.

    Every position comes from `floor( gl_FragCoord.xy )`, an integer, never
    from an interpolated uv; every input is read with texelFetch at integer
    texels inside its USED size (so its padding is never read and no MaxUV is
    involved), and an input whose size is not the output's is resampled by a
    bilinear done by hand on premultiplied linear light. The vertex shader
    passes nothing but the position.
*/

namespace scrim
{

extern const char* const kVersion;
extern const char* const kVertex;
extern const char* const kCommon;
extern const char* const kScreenFragment;

/// kVersion + kCommon + the pass, the order the fragment shader is built in.
std::string AssembleFragment( const char* pass );
std::string AssembleVertex();

} // namespace scrim

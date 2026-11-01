#ifndef OPENEDEN_PIPELINE_PARTICLEDESCRIPTORS_H
#define OPENEDEN_PIPELINE_PARTICLEDESCRIPTORS_H

// The shader headers are C++ here, and the second needs the first's macros: keep this order, so the whole file is
// left to hand formatting.
// clang-format off
namespace Pipeline
{
    namespace Descriptors
    {
#include <files/shaders/lib/material/bindings.glsl>
#include <files/shaders/comp/particle/bindings.glsl>
    }
}
// clang-format on

#endif

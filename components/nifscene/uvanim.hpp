#ifndef OPENEDEN_COMPONENTS_NIFSCENE_UVANIM_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_UVANIM_HPP

// Task 0359: playing the UV controllers of a Skyrim effect shader (the scrolling flame cards of fires, the drifting
// smoke and glow planes) as pure maths. Formats layer: no renderer type. The renderer asks for the texture matrix at a
// clock time and binds it as the shape's texture matrix.

#include <array>

#include "scene.hpp"

namespace NifScene::UvAnim
{
    /// The value of a track at `clock` seconds of the session's clock: the controller's own time is
    /// `clock * frequency + phase`, mapped into [start, stop] by the extrapolation mode, and the keys are read with the
    /// track's interpolation. An empty track, or a clock or key that is not finite, gives 0.
    [[nodiscard]] float sample(const FloatTrack& track, double clock);

    /// The texture matrix at `clock`, column-major as vsg::mat4 stores it: uv' = uv * scale + offset in the xy plane
    /// (the translation is the last column). Each component comes from its track, else from the animation's base value.
    [[nodiscard]] std::array<float, 16> matrixAt(const UvAnimation& animation, double clock);
}

#endif

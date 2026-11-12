#ifndef GAME_RENDER_ANIMCLIP_H
#define GAME_RENDER_ANIMCLIP_H

#include <opas/anim/Clip.hpp>

#include <memory>

namespace Anim
{
    class AnimSet;
    class ControllerMap;
}

namespace MWRender
{
    /// Rate at which channels with no exact clip equivalent are resampled (task 0007, open question 5).
    constexpr float sClipResampleRate = 60.f;

    /// What an animation source is as CreativeMotion's runtime plays it (task 0124, stage C2): an
    /// opas::anim::Clip on the source's own timeline, in metres, with one track per bone the source animates. A track
    /// is named by the bone's lower-case name (the key of Anim::Bones), so one clip serves every actor whatever its
    /// skeleton; the pose driver binds the tracks to its own bones. Immutable once made.
    struct AnimClip
    {
        opas::anim::Clip mClip;
    };

    /// Converts a source (the conversion rules of task 0007, R1): Linear keys and quaternion CubicHermiteSpline keys
    /// become linear keys at the same times (exact); Flip (constant) keys become step keys switching just after each
    /// midpoint; any other channel is resampled at sClipResampleRate over [0, duration] plus the key times of keyed
    /// channels. The duration covers the source's text keys and every key of its keyed channels. Controllers that
    /// animate nothing (no translate and no rotate) are skipped. Null when no controller animates anything. Tracks are
    /// in target-name order, so the result does not depend on the source's hash map.
    std::shared_ptr<const AnimClip> buildAnimClip(const Anim::ControllerMap& source);

    /// The clip of a source's AnimSet, made on the first call and then shared by every actor playing that set (thread
    /// safe). Null when the source animates nothing.
    std::shared_ptr<const AnimClip> animClipOf(const Anim::AnimSet& set);
}

#endif

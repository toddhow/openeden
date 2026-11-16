#ifndef OPENMW_MWRENDER_RIDERBOB_H
#define OPENMW_MWRENDER_RIDERBOB_H

// Task 0255 (riding, second slice): the rider's body follows the saddle bone's animated position. The horse's gait
// moves the `SaddleBone` node (a walk bobs it by a few units, a run by more and pitches it); the rider's own place
// (the stub player the pack carries) is the saddle at REST, so the body is drawn displaced by the bone's change.
// Pure maths on plain vectors: no scene, no allocation. Main thread.

#include <cmath>

#include <vsg/maths/mat3.h>
#include <vsg/maths/vec3.h>

namespace MWRender
{
    /// The most the body is displaced from the rest saddle, game units (a bone that jumps further is a fault: the rest
    /// position or the node is wrong, and the body stays near the saddle). OURS.
    inline constexpr float kMaxRiderBob = 60.f;

    /// `bone` and `rest` (the bone's place when the ride began) are in the horse's own frame (x right, y forward, z
    /// up); `heading` is the horse's rotation (the
    /// columns are the images of its axes, as Anim::Transform stores them). Returns the displacement in world axes, or
    /// zero when any input is not finite; its length is at most kMaxRiderBob.
    [[nodiscard]] inline vsg::vec3 riderBobOffset(
        const vsg::vec3& bone, const vsg::vec3& rest, const vsg::mat3& heading)
    {
        const vsg::vec3 delta(bone.x - rest.x, bone.y - rest.y, bone.z - rest.z);
        if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
            return vsg::vec3(0.f, 0.f, 0.f);
        // The horse's axes in world axes: column c of the rotation is the image of axis c.
        vsg::vec3 world(0.f, 0.f, 0.f);
        for (int c = 0; c < 3; ++c)
        {
            const float amount = c == 0 ? delta.x : (c == 1 ? delta.y : delta.z);
            world.x += heading[c][0] * amount;
            world.y += heading[c][1] * amount;
            world.z += heading[c][2] * amount;
        }
        if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
            return vsg::vec3(0.f, 0.f, 0.f);
        const float length = std::sqrt(world.x * world.x + world.y * world.y + world.z * world.z);
        if (length > kMaxRiderBob)
        {
            const float scale = kMaxRiderBob / length;
            world.x *= scale;
            world.y *= scale;
            world.z *= scale;
        }
        return world;
    }
}

#endif

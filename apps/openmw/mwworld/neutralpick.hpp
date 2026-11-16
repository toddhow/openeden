#ifndef OPENMW_MWWORLD_NEUTRALPICK_H
#define OPENMW_MWWORLD_NEUTRALPICK_H

// Task 0233: picking a neutral furniture by its bounds. The activate key picks what the camera ray hits in the RENDERED
// geometry, and a cooking pot or a fire spit is mostly thin: an iron rail, a wire, an open bowl, so a ray that aims
// at the pot goes through it and ends on the floor behind. The ray is therefore also tested against the object's
// bounding box (the base record's, in the object's own frame); the furniture is picked when the ray passes through the
// whole box without touching anything that is inside it. Pure: no engine object, any thread.

#include <optional>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

namespace MWWorld
{
    /// Where a ray meets a box: distances along the ray (the ray's direction is a unit vector).
    struct NeutralBoxHit
    {
        double mEntry = 0.0;
        double mExit = 0.0;
    };

    /// The ray `origin + t * direction` (t from 0 to `maxDistance`) against the box [`boxMin`, `boxMax`] given in the
    /// object's own frame: `position` is the frame's origin in the world, `rotation` turns the frame into the world's
    /// (the rotation the object was inserted with) and `scale` its uniform scale. Nothing when the ray misses, when it
    /// starts beyond the box or the box starts beyond `maxDistance`, or when any number is not finite or the box is
    /// empty. A ray that starts inside the box has an entry of 0.
    [[nodiscard]] std::optional<NeutralBoxHit> rayOrientedBox(const vsg::dvec3& origin, const vsg::dvec3& direction,
        double maxDistance, const vsg::dvec3& boxMin, const vsg::dvec3& boxMax, const vsg::dvec3& position,
        const vsg::dquat& rotation, double scale);

    /// Does the furniture's box win over what the rendered ray hit? `renderDistance` is how far the rendered hit is
    /// (negative or not a number: it hit nothing) and `renderHitCanBeActivated` says whether that hit is something the
    /// activate key could use (an item, an actor, a door: it has a tooltip). A hit that cannot be activated (the floor
    /// under the furniture, a wall, a static) loses as soon as the ray entered the box before it; one that can be
    /// activated (an item on the furniture, an actor in front of it) loses only when it lies beyond the whole box.
    [[nodiscard]] bool boxBeatsRenderHit(const NeutralBoxHit& box, double renderDistance, bool renderHitCanBeActivated);
}

#endif

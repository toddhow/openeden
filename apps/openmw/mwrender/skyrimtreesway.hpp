#ifndef OPENMW_MWRENDER_SKYRIMTREESWAY_H
#define OPENMW_MWRENDER_SKYRIMTREESWAY_H

// Task 0136 (Skyrim tree sway, `[Game] neutral tree sway`): Skyrim SE shapes with the Tree_Anim shader flag bend in the
// weather's wind through the vertex shader path the world LOD's far trees already use (`LOD_SWAY` in
// default/main.vert and its Slang twin; task 0096 stage c). No shader or pipeline set is new.
//
// How the pieces fit:
//  - flatten (components/nifscene) marks the shape (MaterialKey::mSway) and keeps the file's per-vertex alpha, which
//    is the vertex's flexibility (0: rigid trunk ... 1: free tip; docs/tasks/0136-skyrim-tree-sway.md survey).
//  - the model builder (skyrimmodel.cpp) turns that alpha into the sway weight byte of `weightByte` and binds ONE
//    process-wide uniform (this Driver's) as the shape's TEXMAT_BINDING buffer, with the LOD_SWAY pipeline mode.
//  - the Driver writes column 0 of that uniform every frame: the wind push in world units at full weight, the clock's
//    angle and no distance boost, exactly the layout LodClusters writes for its groups.
//
// Threads: `weightByte` is pure. The Driver's uniform is created on the main thread, read by the builder on any loader
// thread (the pointer only) and written by `update` on the main thread, as the cluster groups' matrices are.

#include <cstdint>

#include <vsg/core/Value.h>
#include <vsg/maths/mat4.h>

namespace MWRender::SkyrimTreeSway
{
    /// The top of a free-swinging tree leans by this share of its height at full wind (first guess for a look; the same
    /// number as `[WorldLod] object cluster sway lean`'s default).
    inline constexpr float lean = 0.04f;

    /// The wind the sway sees: a calm weather still moves a little. As LodClusters: 0.3 + 0.7 * the weather's base wind
    /// speed (clamped to 0..1; a speed that is not finite counts as 0).
    float windScale(float baseWindSpeed);

    /// The colour-alpha byte the sway shader reads as weight, for a vertex whose file alpha (flexibility) is `alpha` on
    /// a model `objectHeight` units tall: the square root of objectHeight * alpha^2 / Lod::swayWeightScale, in 0..255.
    /// The shader squares it again, so the tip of the tree moves by lean * wind * objectHeight * alpha^2 (to within the
    /// byte's step). A height or alpha that is not finite, a height of 0 or less, or alpha of 0 or less gives 0; alpha
    /// above 1 counts as 1.
    std::uint8_t weightByte(float alpha, float objectHeight);

    /// Owns the uniform and its clock. Created by RenderManager when the setting is on.
    class Driver
    {
    public:
        Driver();

        /// The uniform every swaying shape binds; column 0 is (push x, push y, clock angle, 0). Zero push until the
        /// first update.
        const vsg::ref_ptr<vsg::mat4Value>& uniform() const { return mUniform; }

        /// Advances the clock by `seconds` (ignored when negative or not finite) and writes the uniform from the
        /// weather's base wind speed; marks it modified only when a value changed.
        void update(double seconds, float baseWindSpeed);

        double clockSeconds() const { return mSeconds; }

    private:
        vsg::ref_ptr<vsg::mat4Value> mUniform;
        double mSeconds = 0.0;
    };
}

#endif

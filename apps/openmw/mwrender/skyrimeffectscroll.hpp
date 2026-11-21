#ifndef OPENMW_MWRENDER_SKYRIMEFFECTSCROLL_H
#define OPENMW_MWRENDER_SKYRIMEFFECTSCROLL_H

// Task 0359 (Skyrim fire and effect models, `[Game] neutral fire effects`): the flame cards, smoke and glow planes of
// Skyrim's fires scroll their texture (BSEffectShaderPropertyFloatController on the shader's U and V offset and scale).
// flatten (components/nifscene) keeps those tracks on the shape (Shape::mScroll) and leaves its coordinates raw; the
// model builder (skyrimmodel.cpp) gives each such shape its own texture matrix uniform, bound at TEXMAT_BINDING with
// Pipeline::Mode::TEXMAT (the path Morrowind's NiUVController already takes), and registers it here. This driver writes
// every live uniform once a frame from one session clock, so all instances of a model scroll in step.
//
// Threads: `add` is called from the loader threads that build models; `update` from the main thread once a frame. A
// uniform no shape holds any more is dropped at the next update (the registry holds it weakly).

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include <vsg/core/Value.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/maths/mat4.h>

#include <components/nifscene/scene.hpp>

namespace MWRender::SkyrimEffectScroll
{
    class Driver
    {
    public:
        /// Writes the animation's matrix at the current clock into the uniform now and keeps it up to date from the
        /// next `update` on. The uniform must be DYNAMIC_DATA (made so here).
        void add(const vsg::ref_ptr<vsg::mat4Value>& uniform, std::shared_ptr<const NifScene::UvAnimation> animation);

        /// Advances the clock by `seconds` (ignored when negative or not finite), writes every live uniform whose
        /// matrix changed and drops the dead ones.
        void update(double seconds);

        double clockSeconds() const;
        /// Registered uniforms still alive (a count for tests and the log).
        std::size_t liveCount() const;

    private:
        struct Entry
        {
            vsg::observer_ptr<vsg::mat4Value> mUniform;
            std::shared_ptr<const NifScene::UvAnimation> mAnimation;
        };

        mutable std::mutex mMutex;
        std::vector<Entry> mEntries;
        double mSeconds = 0.0;
    };

    /// The matrix of an animation at a clock time as a vsg matrix (UvAnim::matrixAt, column-major).
    vsg::mat4 matrixAt(const NifScene::UvAnimation& animation, double clock);
}

#endif

#ifndef OPENEDEN_MWRENDER_WATERRIPPLES_H
#define OPENEDEN_MWRENDER_WATERRIPPLES_H

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

#include <vsg/core/Array.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include "../mwworld/ptr.hpp"

namespace Pipeline
{
    class Builder;
}
namespace vsgUtil
{
    class CompileContext;
}
namespace vsg
{
    class Options;
    class Switch;
    class VertexDraw;
}
namespace Anim
{
    class Transform;
}
namespace MWWorld
{
    class CellStore;
}

namespace MWRender
{
    /// The ripple fallback values (Morrowind.ini [Water]), as OpenMW's RippleSimulation read them.
    struct RippleSettings
    {
        int maxNumberRipples = 0; // Water_MaxNumberRipples
        float lifetime = 0.f; // Water_RippleLifetime, seconds
        float rotationSpeed = 0.f; // Water_RippleRotSpeed, radians per second
    };

    /**
     * @brief The ripples on the water surface, with no renderer (task 0012, V4): OpenMW's RippleSimulation without
     * the shader water's ripple map, at cf3dbd3aa7.
     *
     * - emit() places a ripple at the given point if it lies within 20 units of the water height, at a seeded random
     *   rotation in [-pi, pi). Nothing is emitted when maxNumberRipples is 0 or less. When more than
     *   maxNumberRipples ripples are alive, the oldest is replaced first, so at most maxNumberRipples + 1 live at once,
     *   as with osgParticle's reuseParticle.
     * - step() ages each ripple by dt, turns it by rotationSpeed * dt, and removes it once its age reaches the
     *   lifetime. A lifetime of 0 or less never ends, as in osgParticle; such ripples stay at their first size and
     *   alpha.
     * - A ripple's size (the half-width of its quad) grows linearly from 15 to 180 over its life, and its alpha falls
     *   linearly from 0.7 to 0, as osgParticle interpolated the template's ranges.
     *
     * Positions are in the water plane; the drawing places them at the water height. step() and emit() allocate
     * nothing; the constructor allocates the capacity.
     */
    class RippleParticles
    {
    public:
        struct Ripple
        {
            vsg::vec2 position;
            float age = 0.f;
            float angle = 0.f; // about Z, radians
        };

        explicit RippleParticles(const RippleSettings& settings, unsigned seed = 1);

        void setWaterHeight(float height) { mWaterHeight = height; }
        float getWaterHeight() const { return mWaterHeight; }

        void emit(const vsg::vec3& position);
        void step(float dt);
        void clear() { mCount = 0; }

        std::size_t size() const { return mCount; }
        std::size_t capacity() const { return mRipples.size(); }
        const Ripple& operator[](std::size_t index) const { return mRipples[index]; }

        float sizeOf(const Ripple& ripple) const;
        float alphaOf(const Ripple& ripple) const;

    private:
        float lifeFraction(const Ripple& ripple) const;

        RippleSettings mSettings;
        std::vector<Ripple> mRipples;
        std::size_t mCount = 0;
        float mWaterHeight = 0.f;
        std::minstd_rand mRandom;
    };

    /// When an actor in the water makes a ripple, without the ripple map (OpenMW's RippleSimulation::update).
    struct RippleEmitterTimer
    {
        vsg::vec3 lastEmitPosition;
        float timer = 0.f;
    };

    /// Advances the timer by dt; returns whether the emitter makes a ripple now. An emitter that is not in the water
    /// resets its timer. One in the water makes a ripple when its timer has run out or when it has moved more than 10
    /// units since its last ripple, and then waits 1.5 s.
    bool updateRippleEmitter(RippleEmitterTimer& timer, float dt, const vsg::vec3& position, bool inWater);

    /**
     * @brief Draws the ripples of actors and projectiles on the water (task 0012, V4): the textured, lit, fading quads
     * of OpenMW's RippleSimulation, flipping through the Water_RippleTexture frames every 0.3 s.
     *
     * Owned by RenderManager; every function runs on the main thread. Storage is created on first use, so updating
     * allocates nothing once the emitters are registered.
     */
    class WaterRipples
    {
    public:
        WaterRipples(const Pipeline::Builder& builder, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            vsg::ref_ptr<const vsg::Options> textureOptions);
        ~WaterRipples();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        void addEmitter(const MWWorld::ConstPtr& ptr);
        void removeEmitter(const MWWorld::ConstPtr& ptr);
        /// Follows an actor whose Ptr changed when it moved between two loaded cells.
        void updateEmitterPtr(const MWWorld::ConstPtr& old, const MWWorld::ConstPtr& ptr);
        /// Removes the emitters of a cell, except the player's.
        void removeCell(const MWWorld::CellStore* store);
        void emitRipple(const vsg::vec3& position);
        void clear();
        void setWaterHeight(float height);

        /// Paused keeps the ripples still, as OpenMW skipped the simulation while paused.
        void update(float dt, bool paused);

    private:
        struct Emitter
        {
            MWWorld::ConstPtr ptr;
            RippleEmitterTimer timer;
        };

        void create();
        void draw();

        const Pipeline::Builder& mBuilder;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<const vsg::Options> mTextureOptions;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<Anim::Transform> mTransform;
        vsg::ref_ptr<vsg::Switch> mFrames;
        vsg::ref_ptr<vsg::VertexDraw> mDraw;
        vsg::ref_ptr<vsg::vec3Array> mVertices;
        vsg::ref_ptr<vsg::vec4Array> mColors;
        vsg::ref_ptr<vsg::vec2Array> mUvs;

        RippleParticles mParticles;
        std::vector<Emitter> mEmitters;
        float mTime = 0.f;
        bool mCreated = false;
        std::size_t mDrawnCount = 0;
    };
}

#endif

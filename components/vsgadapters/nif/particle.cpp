#include "particle.hpp"

#include <cmath>
#include <iostream>

#include <vsg/nodes/Switch.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/CullGroup.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/commands/Dispatch.h>
#include <vsg/maths/transform.h>

#include <components/animation/bones.hpp>
#include <components/animation/context.hpp>
#include <components/animation/range.hpp>
#include <components/animation/transform.hpp>
#include <components/vsgutil/computetransform.hpp>
#include <components/vsgutil/id.hpp>
#include <components/vsgutil/nodepath.hpp>
#include <components/vsgutil/traverse.hpp>
#include <components/nif/particle.hpp>

#include "anim.hpp"

namespace vsgAdapters
{
    float getParticlesPerSecond(const Nif::NiParticleSystemController& partctrl)
    {
        if (partctrl.noAutoAdjust())
            return partctrl.mBirthRate;
        else if (partctrl.mLifetime == 0 && partctrl.mLifetimeVariation == 0)
            return 0.f;
        else
            return (partctrl.mNumParticles / (partctrl.mLifetime + partctrl.mLifetimeVariation / 2));
    }

    Pipeline::Data::EmitArgs handleEmitterData(const Nif::NiParticleSystemController& partctrl)
    {
        Pipeline::Data::EmitArgs data{
            .offsetRandom = { toVsg(partctrl.mEmitterDimensions), 0 },
            .direction = { partctrl.mPlanarAngle, partctrl.mPlanarAngleVariation, partctrl.mDeclination, partctrl.mDeclinationVariation },
            .velocityLifetime = { partctrl.mSpeed, partctrl.mSpeedVariation, partctrl.mLifetime, partctrl.mLifetimeVariation }
        };
        return data;
    }

    void handleInitialParticles(vsg::Array<Pipeline::Data::Particle>& array, const Nif::NiParticlesData& data,
        const Nif::NiParticleSystemController& partctrl)
    {
        for (int i = 0; i < data.mNumParticles; ++i)
        {
            auto& in = partctrl.mParticles[i];
            float age = std::max(0.f, in.mAge);
            if (i >= data.mActiveCount || in.mCode >= data.mVertices.size())
                age = in.mLifespan;
            auto& out = array.at(i);
            out.velocityAge = vsg::vec4(toVsg(in.mVelocity), age);
            out.maxAge.x = in.mLifespan;
            // openeden-vsg: the system's particle size for soft particles (task 0012, V4k), OpenMW's default particle
            // template size; the simulation never changes it.
            out.maxAge.y = partctrl.mInitialSize;
            out.color = toVsg(partctrl.mInitialColor);
            out.color.a = 1;
            if (in.mCode < data.mVertices.size())
                out.positionSize = vsg::vec4(toVsg(data.mVertices[in.mCode]), 1.f);
            float size = partctrl.mInitialSize;
            if (in.mCode < data.mSizes.size())
                size *= data.mSizes[in.mCode];
            out.positionSize.w = size;
        }
    }

    vsg::ref_ptr<vsg::Data> createColorCurve(const Nif::NiColorData& clrdata, float maxDuration, float timeStep)
    {
        auto channel = handleInterpolatedKeyframes<vsg::vec4>(clrdata.mKeyMap);
        int numSteps = maxDuration / timeStep;
        auto data = vsg::vec4Array::create(numSteps, vsg::Data::Properties(VK_FORMAT_R32G32B32A32_SFLOAT));
        for (int i = 0; i < numSteps; ++i)
            data->at(i) = channel->value(timeStep * i);
        return data;
    }

    class GetNodeBounds : public vsgUtil::TTraverse<vsg::Node>
    {
    public:
        std::vector<vsg::dsphere*> nodeBounds;
        using vsg::Visitor::apply;
        void apply(vsg::DepthSorted& node) override
        {
            nodeBounds.push_back(&node.bound);
            node.traverse(*this);
        }
         void apply(vsg::CullNode& node) override
        {
            nodeBounds.push_back(&node.bound);
            node.traverse(*this);
        }
        void apply(vsg::CullGroup& node) override
        {
            nodeBounds.push_back(&node.bound);
            node.traverse(*this);
        }
    };

    class LinkParticleSystem : public vsg::Visitor
    {
        ParticleSystem& mParticleSystem;
        vsgUtil::AccumulatePath<const Anim::Transform*> mTransformPath;
        vsgUtil::AccumulatePath<const vsg::Switch*> mSwitchPath;
        bool mCollectEmitters = false;
    public:
        bool foundEmitterNode = false;
        bool foundParticleNode = false;

        LinkParticleSystem(vsgAdapters::ParticleSystem& p)
            : mParticleSystem(p)
        {
            overrideMask = vsg::MASK_ALL;
        }
        using vsg::Visitor::apply;
        void apply(vsg::Switch& sw) override
        {
            auto ppn = mSwitchPath.pushPop(&sw);
            checkAndTraverse(sw);
        }
        void apply(vsg::Transform& node) override
        {
            if (auto trans = dynamic_cast<Anim::Transform*>(&node))
            {
                auto ppn = mTransformPath.pushPop(trans);
                checkAndTraverse(node);
            }
            else
            {
                checkAndTraverse(node);
            }
        }
        void apply(vsg::Node& node) override
        {
            checkAndTraverse(node);
        }

        void checkAndTraverse(vsg::Node& node)
        {
            if (auto id = vsgUtil::ID::get(node))
            {
                int index = id->id;
                if (index == mParticleSystem.mEmitterNodeIndex)
                {
                    foundEmitterNode = true;
                    mParticleSystem.mSwitches = mSwitchPath.path;
                    if (mParticleSystem.mArrayEmitter)
                    {
                        mCollectEmitters = true;
                        node.traverse(*this);
                        mCollectEmitters = false;
                    }
                    else
                    {
                        mParticleSystem.mPathsToEmitters = { mTransformPath.path };
                    }
                }
                else if (index == mParticleSystem.mParticleNodeIndex)
                {
                    foundParticleNode = true;
                    mParticleSystem.mPathToParticle = mTransformPath.path;

                    GetNodeBounds getBounds;
                    node.accept(getBounds);

                    if (getBounds.nodeBounds.empty())
                    {
                        std::cerr << "!LinkParticleSystem::foundNodeBounds" << std::endl;
                    }
                    else
                    {
                        mParticleSystem.mInitialBound = *getBounds.nodeBounds.front();
                        mParticleSystem.mDynamicBounds = getBounds.nodeBounds;
                    }
                }
                else if (mCollectEmitters)
                {
                    mParticleSystem.mPathsToEmitters.push_back(mTransformPath.path);
                    node.traverse(*this);
                }
                else
                    node.traverse(*this);
            }
            else
                node.traverse(*this);
        }
    };

    float ParticleSystem::calculateRadius(const Nif::NiParticleSystemController& partctrl)
    {
        // OpenEden: a quad reaches the particle size from its centre (main.vert), not half of it.
        return calculateMaxLifetime(partctrl) * (partctrl.mSpeed + partctrl.mSpeedVariation)
            + std::max(partctrl.mEmitterDimensions.x(),
                std::max(partctrl.mEmitterDimensions.y(), partctrl.mEmitterDimensions.z()))
            + partctrl.mInitialSize;
    }

    float ParticleSystem::calculateMaxLifetime(const Nif::NiParticleSystemController& partctrl)
    {
        return partctrl.mLifetime + partctrl.mLifetimeVariation;
    }

    ParticleSystem::ParticleSystem(const Nif::NiParticleSystemController& partctrl, int particleNodeIndex, bool worldSpace)
        : mWorldSpace(worldSpace)
        , mParticleNodeIndex(particleNodeIndex)
    {
        mMaxLifetime = calculateMaxLifetime(partctrl);
        mParticlesPerSecond = getParticlesPerSecond(partctrl);

        if (partctrl.mTimeStop <= partctrl.mEmitStopTime && partctrl.mTimeStart >= partctrl.mEmitStartTime)
            active = Anim::make_constant(true);
        else
        {
            active = Anim::make_channel<Anim::Range>(partctrl.mEmitStartTime, partctrl.mEmitStopTime);
            addExtrapolatorIfRequired(partctrl, active, partctrl.mEmitStartTime, partctrl.mEmitStopTime);
        }

        if (partctrl.mRecordType == Nif::RC_NiBSPArrayController)
            mArrayEmitter = true;
        if (!partctrl.mEmitter.empty())
            mEmitterNodeIndex = partctrl.mEmitter->mRecordIndex;
    }

    void ParticleSystem::apply(vsg::Value<Pipeline::Data::FrameArgs>& val, float time)
    {
        // float dt = scene.dt;
        // openmw-7238-particle-system-time
        // OpenEden: start the clock at the first update. Auto-played controllers start at a random phase, so the first
        // delta from 0 could be thousands of seconds, emitting a burst of particles at once.
        if (!mStarted)
        {
            mDt.last = time;
            mStarted = true;
        }
        float dt = mDt.get(time);
        auto& data = val.value();
        data.time.x = dt;
        data.time.y = time;

        if (mWorldSpace || wrapAround)
        {
            auto worldMatrix = vsgUtil::computeTransform(mLocalToWorld);
            if (wrapAround)
            {
                const vsg::mat4 toCentre = vsg::translate(-wrapAround->centre) * worldMatrix;
                data.wrapMatrix = toCentre;
                data.wrapInverse = vsg::inverse(toCentre);
                data.wrapRange = vsg::vec4(wrapAround->range, 1.f);
            }
            else
                data.wrapRange = vsg::vec4();
            if (!mLastWorldMatrix)
                data.worldOffset = vsg::mat4();
            else if (!mWorldSpace)
                data.worldOffset = vsg::inverse_4x3(worldMatrix) * (*mLastWorldMatrix);
            else
            {
                // calculate the worldOffset matrix, used by compute shaders to transform the previous frame's particle positions/velocities, leaving a trail of particles behind in local space
                data.worldOffset = vsg::inverse_4x3(worldMatrix) * (*mLastWorldMatrix);

                // update Cull/DepthSorted bounds to include trailing particles
                mWorldBound.timer += dt;
                auto movement = data.worldOffset * vsg::vec4(0,0,0,1);
                vsg::vec3 movementVec(movement.x, movement.y, movement.z);
                float moved = vsg::length(movementVec);
                const float epsilon = 0.003f;
                if (moved > epsilon)
                {
                    mWorldBound.totalMoved += moved;
                    mWorldBound.moved.emplace_back(mWorldBound.timer + mMaxLifetime, moved);
                }
                while (!mWorldBound.moved.empty() && mWorldBound.timer >= mWorldBound.moved.front().first)
                {
                    mWorldBound.totalMoved -= mWorldBound.moved.front().second;
                    mWorldBound.moved.pop_front();
                }
                updateBounds();
            }
            // OpenEden: with no time passing the particles are not simulated this frame (CullParticles skips the dispatch),
            // so the movement is kept for the next frame's offset rather than lost (task 0012, V4c).
            if (dt > 0.f || !mLastWorldMatrix)
                mLastWorldMatrix = worldMatrix;
        }

        if (active->value(time) && emitterVisible())
        {
            data.emitMatrix = calculateEmitMatrix();
            data.emitCount = vsg::ivec4(calculateEmitCount(dt), 0, 0, 0 );
        }
        else
            data.emitCount = vsg::ivec4();
    }

    void ParticleSystem::link(Anim::Context& ctx, vsg::Object&)
    {
        LinkParticleSystem visitor(*this);
        //ctx.attachmentPath.back()->accept(visitor);
        ctx.attachmentPath.front()->accept(visitor);

        if (mEmitterNodeIndex != -1 && !visitor.foundEmitterNode)
            std::cerr << "!LinkParticleSystem::foundEmitterNode(" << mEmitterNodeIndex << ")" << std::endl;
        if (mParticleNodeIndex != -1 && !visitor.foundParticleNode)
            std::cerr << "!LinkParticleSystem::foundParticleNode(" << mParticleNodeIndex << ")" << std::endl;
        if (mPathsToEmitters.empty())
            mPathsToEmitters.resize(1);

        mLocalToWorld = vsgUtil::path<const Anim::Transform*>(ctx.worldAttachmentPath);
        vsgUtil::addPath(mLocalToWorld, mPathToParticle);

        if (mPathsToEmitters.size() == 1)
            vsgUtil::trim(mPathsToEmitters[0], mPathToParticle);
        else
        {
            size_t trimCount = 0;
            for (auto& node : mPathToParticle)
            {
                auto itr = mPathsToEmitters.begin();
                for (; itr != mPathsToEmitters.end(); ++itr)
                {
                    if (itr->size() <= trimCount || (*itr)[trimCount] != node)
                        break;
                }
                if (itr != mPathsToEmitters.end())
                    break;
                ++trimCount;
            }
            if (trimCount > 0)
            {
                vsgUtil::trim(mPathToParticle, trimCount);
                for (auto& path : mPathsToEmitters)
                    vsgUtil::trim(path, trimCount);
            }
        }
    }

    int particlesToEmit(float& carry, float dt, float perSecond)
    {
        if (!(dt > 0.f) || !(perSecond > 0.f))
            return 0;
        const float total = carry + dt * perSecond;
        if (total < 0.f)
        {
            carry = total;
            return 0;
        }
        const float whole = std::floor(total);
        carry = total - whole;
        return static_cast<int>(whole);
    }

    int ParticleSystem::calculateEmitCount(float dt)
    {
        // OpenEden: OpenMW's rate (components/nifosg/nifloader.cpp, cf3dbd3aa7); vsgopenmw scaled it by 0.01.
        return particlesToEmit(mCarryOver, dt, mParticlesPerSecond);
    }

    vsg::mat4 ParticleSystem::calculateEmitMatrix()
    {
        int emitIndex = std::rand() / static_cast<double>(RAND_MAX) * mPathsToEmitters.size();
        const auto& pathToEmitter = mPathsToEmitters[emitIndex];
        auto emitterToWorld = vsgUtil::computeTransform(pathToEmitter);
        auto worldToPs = vsg::inverse_4x3(vsgUtil::computeTransform(mPathToParticle));
        auto emitMatrix = /*orthoNormalize(*/ worldToPs * emitterToWorld;
        auto pos = vsgUtil::translation(emitMatrix);
        float length = vsg::length(pos);
        if (length > mEmitRadius)
        {
            mEmitRadius = length;
            updateBounds();
        }
        return emitMatrix;
    }

    void ParticleSystem::updateBounds()
    {
        auto bound = mInitialBound;
        bound.radius += mWorldBound.totalMoved + mEmitRadius;
        for (auto& b : mDynamicBounds)
            *b = bound;
    }

    bool ParticleSystem::emitterVisible() const
    {
        for (auto sw : mSwitches)
            for (auto& switchChild : sw->children)
                if (switchChild.mask == vsg::MASK_OFF)
                    return false;
        return true;
    }

    vsg::ref_ptr<vsg::Command> createParticleDispatch(uint32_t numParticles)
    {
        #include <files/shaders/comp/particle/workgroupsize.glsl>
        // Vulkan guarantees a minimum of 128 maxComputeWorkGroupInvocations.
        static_assert(workGroupSizeX <= 128);
        return vsg::Dispatch::create(std::ceil(numParticles/static_cast<float>(workGroupSizeX)), 1, 1);
    }
}

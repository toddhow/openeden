#ifndef VSGOPENMW_VSGADAPTERS_NIF_PARTICLE_H
#define VSGOPENMW_VSGADAPTERS_NIF_PARTICLE_H

#include <memory>
#include <optional>
#include <deque>

#include <vsg/core/Array.h>
#include <vsg/core/Value.h>
#include <vsg/commands/Command.h>

#include <components/animation/deltatime.hpp>
#include <components/animation/channel.hpp>
#include <components/animation/updatedata.hpp>
#include <components/pipeline/particledata.hpp>

namespace Nif
{
    struct NiParticleSystemController;
    struct NiColorData;
    struct NiParticlesData;
}
namespace Anim
{
    class Transform;
}
namespace Pipeline::Data
{
    #include <files/shaders/comp/particle/data.glsl>
}
namespace vsgAdapters
{
    /*
     * ParticleSystem is a controller class for updating the parameters used by particle compute shaders.
     */
    class ParticleSystem : public Anim::MUpdateData<ParticleSystem, vsg::Value<Pipeline::Data::FrameArgs>>
    {
        Anim::DeltaTime mDt;
        bool mStarted = false;
        friend class LinkParticleSystem;
        float mCarryOver{}; // OpenEden: was int, which dropped every fraction
        using TransformPath = std::vector<const Anim::Transform*>;
        TransformPath mLocalToWorld;
        std::optional<vsg::mat4> mLastWorldMatrix;
        TransformPath mPathToParticle;
        std::vector<TransformPath> mPathsToEmitters;
        std::vector<const vsg::Switch*> mSwitches;
        bool mWorldSpace = false;

        /*
         * Links to parent Cull/DepthSorted nodes.
         */
        struct WorldBounds
        {
            std::deque<std::pair<float /*time*/, float /*moved*/>> moved;
            float totalMoved = 0;
            float timer = 0;
        } mWorldBound;
        float mMaxLifetime = 0;
        float mEmitRadius = 0;
        std::vector<vsg::dsphere*> mDynamicBounds;
        vsg::dsphere mInitialBound;
        void updateBounds();

        bool mArrayEmitter = false;
        int mEmitterNodeIndex = -1;
        int mParticleNodeIndex = -1;

        float mParticlesPerSecond{};
        int calculateEmitCount(float dt);
        vsg::mat4 calculateEmitMatrix();
        /*
         * Stops emitting particles when the emitter node is switched off.
         */
        bool emitterVisible() const;
    public:
        ParticleSystem(const Nif::NiParticleSystemController& partctrl, int particleNodeIndex, bool worldSpace);
        Anim::channel_ptr<bool> active;

        // OpenEden: OpenMW's WrapAroundOperator for weather particles (task 0012). When set, live particles stay
        // fixed in the world as the node moves and are kept within range / 2 of centre on each world axis. The owner
        // sets it on a cloned controller and moves centre (usually the camera) before each update.
        struct WrapAround
        {
            vsg::vec3 range;
            vsg::vec3 centre;
        };
        std::shared_ptr<const WrapAround> wrapAround;

        static float calculateRadius(const Nif::NiParticleSystemController& partctrl);
        static float calculateMaxLifetime(const Nif::NiParticleSystemController& partctrl);

        void apply(vsg::Value<Pipeline::Data::FrameArgs>& val, float time);
        void link(Anim::Context& ctx, vsg::Object&) override;
    };

    /// OpenEden: OpenMW's emission count, osgParticle's ConstantRateCounter: adds dt * perSecond to carry, returns its
    /// whole part (never negative) and keeps the fraction in carry. A dt or rate of 0 or less emits nothing and keeps
    /// carry.
    int particlesToEmit(float& carry, float dt, float perSecond);

    /// OpenEden: picks one of count emitters from a std::rand() result in [0, RAND_MAX]. The earlier rand() / RAND_MAX * count
    /// reached count itself for RAND_MAX, one past the end of the emitter paths. count must be above 0.
    size_t chooseEmitter(int randValue, size_t count);

    Pipeline::Data::EmitArgs handleEmitterData(const Nif::NiParticleSystemController& partctrl);

    void handleInitialParticles(vsg::Array<Pipeline::Data::Particle>& array, const Nif::NiParticlesData& data, const Nif::NiParticleSystemController& partctrl);

    vsg::ref_ptr<vsg::Data> createColorCurve(const Nif::NiColorData& clrdata, int numSteps);
    vsg::ref_ptr<vsg::Command> createParticleDispatch(uint32_t numParticles);
}

#endif

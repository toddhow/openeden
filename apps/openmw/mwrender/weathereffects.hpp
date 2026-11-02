#ifndef OPENEDEN_MWRENDER_WEATHEREFFECTS_H
#define OPENEDEN_MWRENDER_WEATHEREFFECTS_H

#include <memory>
#include <string>
#include <vector>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>
#include <components/pipeline/material.hpp>
#include <components/vsgadapters/nif/particle.hpp>

namespace Anim
{
    class Transform;
}
namespace MWAnim
{
    struct Effect;
}

namespace MWRender
{
    /**
     * @brief The weather's particle effect (ash, blight, snow, blizzard), drawn in the main scene so walls and roofs
     * hide it (task 0012, V4a; OpenMW drew it from Sky's camera-relative root, `mParticleNode`).
     *
     * The effect is the weather's NIF (`[Models] weather...`), loaded as a looping vsgopenmw effect under a transform
     * that follows the camera; during storms it is turned to face the storm direction, as Morrowind does. Snow
     * particles stay fixed in the world and wrap around a 1024 x 1024 x 800 box centred on the camera, as OpenMW's
     * WrapAroundOperator kept them. Owned by RenderManager; runs on the main thread from RenderManager::update.
     */
    class WeatherEffects
    {
    public:
        explicit WeatherEffects(const MWAnim::Context& context);
        ~WeatherEffects();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        /// Loads model ("" for none) when it differs from the current one.
        void setEffect(const std::string& model);
        const std::string& getEffect() const { return mModel; }

        /// Places the effect at the camera, turned to orientation, fades it to alpha (the precipitation alpha), and
        /// shows it only when visible (false underwater).
        void update(float dt, const vsg::vec3& cameraPosition, const vsg::quat& orientation, float alpha, bool visible);

        /// Task 0040: lights the effect by the sun's part of its light (the display-encoded colour the material's
        /// emissive takes), replacing the NIF's constant emissive glow; null only stops writing it (the effect's
        /// materials are cloned anew whenever the particle file changes, so a stale colour never outlives the snow).
        /// Main thread.
        void setSunLight(const vsg::vec3* encodedColour)
        {
            mHasSunLight = encodedColour != nullptr;
            if (encodedColour)
                mSunLight = *encodedColour;
        }

    private:
        MWAnim::Context mContext;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<Anim::Transform> mTransform;
        std::unique_ptr<MWAnim::Effect> mEffect;
        std::string mModel;
        // Shared with the snow effect's particle controllers; its centre follows the camera.
        std::shared_ptr<vsgAdapters::ParticleSystem::WrapAround> mWrapAround;
        // The effect's own material uniforms (MWAnim::cloneWithOwnMaterials), faded by update().
        std::vector<vsg::ref_ptr<vsg::Value<Pipeline::Data::Material>>> mMaterials;
        bool mHasSunLight = false;
        vsg::vec3 mSunLight;
    };
}

#endif

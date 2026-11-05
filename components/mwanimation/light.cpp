#include "light.hpp"

#include <cstdint>
#include <optional>

#include <vsg/lighting/Light.h>
#include <vsg/lighting/PointLight.h>

#include <components/animation/deltatime.hpp>
#include <components/animation/tmutable.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/rng.hpp>

#include "color.hpp"

namespace
{
    class LightController : public Anim::TMutable<LightController, vsg::Light>
    {
        Anim::DeltaTime mDt;
        bool mStarted = false;
        float mBrightness = 0.675f;
        float mTicksToAdvance = 0.f;
        float mPhase;
        float mSpeed;
        int mType;
        vsg::vec3 mBaseColor;

    public:
        LightController(int type, const vsg::vec3& baseColor)
            : mType(type)
            , mBaseColor(baseColor)
        {
            hints.autoPlay = true;
            mPhase = 0.25f + Misc::Rng::rollClosedProbability() * 0.75f;
            mSpeed = (type & ESM::Light::Flicker || type & ESM::Light::Pulse) ? 0.1f : 0.05f;
        }
        void apply(vsg::Light& light, float time)
        {
            // OpenEden (task 0074c): the first run's step is 0, as in OpenMW 0.51's SceneUtil::LightController, whose
            // clock starts at its first run (mStartTime, 442eec6e2f). Anim::DeltaTime starts at 0, so the first step
            // was the whole timer of the auto-play group the light joined: for an inventory light added to an actor
            // whose group 0 had run for minutes the colour swung by hundreds of times its value, negative on some
            // frames. DeltaTime itself is unchanged: Anim::Roll's random phase relies on it.
            if (!mStarted)
            {
                mStarted = true;
                mDt.last = time;
            }
            // Updating flickering at 15 FPS like vanilla.
            constexpr float updateRate = 15.f;
            mTicksToAdvance = mDt.get(time) * updateRate * 0.25f + mTicksToAdvance * 0.75f;
            if (mBrightness >= mPhase)
                mBrightness -= mTicksToAdvance * mSpeed;
            else
                mBrightness += mTicksToAdvance * mSpeed;

            if (std::abs(mBrightness - mPhase) < mSpeed)
            {
                if (mType & ESM::Light::Flicker || mType & ESM::Light::FlickerSlow)
                    mPhase = 0.25f + Misc::Rng::rollClosedProbability() * 0.75f;
                else // if (mType == LT_Pulse || mType == LT_PulseSlow)
                    mPhase = mPhase <= 0.5f ? 1.f : 0.25f;
            }
            // OpenEden: the colour flickers, not the radius, as OpenMW's LightController at 442eec6e2f.
            light.color = mBaseColor * mBrightness /* * actorFade*/;
        }
    };

    vsg::ref_ptr<vsg::PointLight> createLight(const ESM::Light& light)
    {
        auto node = vsg::PointLight::create();
        node->intensity = light.mData.mRadius;
        node->color = MWAnim::rgbColor(light.mData.mColor);
        if (light.mData.mFlags & ESM::Light::Negative)
            node->color *= -1;
        return node;
    }

    vsg::ref_ptr<LightController> createLightControllerIfRequired(const ESM::Light& light, const vsg::vec3& color)
    {
        if (!(light.mData.mFlags
                & (ESM::Light::Flicker | ESM::Light::FlickerSlow | ESM::Light::Pulse | ESM::Light::PulseSlow)))
            return {};
        return LightController::create(light.mData.mFlags, color);
    }
}

namespace MWAnim
{
    vsg::Node* addLight(
        vsg::Group* attachLight, vsg::Group* fallback, const ESM::Light& light, Anim::Controllers& controllers)
    {
        if (!attachLight)
            attachLight = fallback;
        auto l = createLight(light);
        attachLight->addChild(l);
        if (auto ctrl = createLightControllerIfRequired(light, l->color))
        {
            ctrl->attachTo(*l);
            controllers.emplace_back(ctrl, l);
        }
        return l;
    }
}

namespace MWAnim
{
    void markPointShadowCarrier(Anim::Transform* carrier, vsg::Node* light)
    {
        if (!carrier || !light)
            return;
        // The id is the PointLight's address, as View::CollectLights records it; it is only compared.
        if (auto* pointLight = dynamic_cast<vsg::PointLight*>(light))
            carrier->setPointShadowCarrier(reinterpret_cast<std::uintptr_t>(pointLight));
    }
}

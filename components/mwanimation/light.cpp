#include "light.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <optional>

#include <array>

#include <vsg/lighting/Light.h>
#include <vsg/lighting/PointLight.h>

#include <components/animation/deltatime.hpp>
#include <components/animation/pointshadowcarrier.hpp>
#include <components/animation/tmutable.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/rng.hpp>
#include <components/view/typedlight.hpp>

#include "color.hpp"
#include "lightflicker.hpp"

namespace
{
    // OPENEDEN_LIGHT_FLICKER_TRACE=1 (developer aid, task 0355 stage h): LFLICK lines with the brightness of the first
    // twelve flickering lights of the process, one per run of the controller (600 per light).
    struct FlickerTrace
    {
        int mId = -1;
        int mSamples = 0;
        static bool enabled()
        {
            static const bool on = std::getenv("OPENEDEN_LIGHT_FLICKER_TRACE") != nullptr;
            return on;
        }
        void begin(const char* kind, int flags, int radius)
        {
            static std::atomic<int> next{ 0 };
            if (!enabled())
                return;
            mId = next++;
            if (mId < 12)
                Log(Debug::Info) << "LFLICKNEW id " << mId << " " << kind << " " << flags << " " << radius;
        }
        void sample(float time, float brightness, const std::array<float, 3>* offset = nullptr)
        {
            if (mId < 0 || mId >= 12 || mSamples >= 600)
                return;
            ++mSamples;
            if (offset != nullptr)
                Log(Debug::Info) << "LFLICK " << mId << " t " << time << " b " << brightness << " off "
                                 << (*offset)[0] << "," << (*offset)[1] << "," << (*offset)[2];
            else
                Log(Debug::Info) << "LFLICK " << mId << " t " << time << " b " << brightness;
        }
    };
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
        FlickerTrace mTrace;

    public:
        LightController(int type, const vsg::vec3& baseColor)
            : mType(type)
            , mBaseColor(baseColor)
        {
            mTrace.begin("morrowind-rule", type, 0);
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
            mTrace.sample(time, mBrightness);
        }
    };

    // OpenEden (task 0355 stage h): a neutral (Skyrim) light with its own period and amplitude. The brightness comes
    // from MWAnim::NeutralFlicker::Walk (seeded by the placement, no global generator); the colour is still the base
    // colour times it, as in LightController.
    class NeutralLightController : public Anim::TMutable<NeutralLightController, vsg::Light>
    {
        Anim::DeltaTime mDt;
        bool mStarted = false;
        MWAnim::NeutralFlicker::Walk mWalk;
        vsg::vec3 mBaseColor;
        bool mMoves;
        bool mMovementBaseTaken = false;
        vsg::dvec3 mBasePosition{ 0.0, 0.0, 0.0 };
        FlickerTrace mTrace;

    public:
        NeutralLightController(const MWAnim::NeutralFlicker::Params& params, bool pulse, const vsg::vec3& baseColor)
            : mWalk(params, pulse)
            , mBaseColor(baseColor)
            , mMoves(params.mMovement > 0.f)
        {
            hints.autoPlay = true;
            mTrace.begin("own period_ms,amp_permille", static_cast<int>(params.mPeriod * 1000.f), static_cast<int>(params.mAmplitude * 1000.f));
        }
        void apply(vsg::Light& light, float time)
        {
            // The first run's step is 0, as LightController's (task 0074c).
            if (!mStarted)
            {
                mStarted = true;
                mDt.last = time;
            }
            const float b = mWalk.step(mDt.get(time));
            light.color = mBaseColor * b;
            // Task 0362 item 2: a light with a movement amplitude wanders round its place (the offset is seeded, no clock).
            if (mMoves)
            {
                if (auto* point = dynamic_cast<vsg::PointLight*>(&light))
                {
                    if (!mMovementBaseTaken)
                    {
                        mMovementBaseTaken = true;
                        mBasePosition = point->position;
                    }
                    // Task 0362 item 2 (toddhow's decision): a light that holds a point shadow slot does not move, so the
                    // shadow cache (which redraws a slot whose light moved over a unit) is not redrawn every frame. It keeps
                    // the place it had and moves on when it loses the slot.
                    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(point));
                    bool holdsSlot = false;
                    for (int k = 0; k < Anim::pointShadowSlotCount; ++k)
                        holdsSlot = holdsSlot || Anim::pointShadowOwner(k) == id;
                    if (!holdsSlot)
                    {
                        const auto& o = mWalk.offset();
                        point->position = mBasePosition + vsg::dvec3(o[0], o[1], o[2]);
                    }
                }
                mTrace.sample(time, b, &mWalk.offset());
                return;
            }
            mTrace.sample(time, b);
        }
    };

    vsg::ref_ptr<vsg::PointLight> createLight(const ESM::Light& light)
    {
        // Task 0355 stage g: a game pack's light that has a spot cone or a shadow rule is a View::TypedPointLight;
        // every other light (all of Morrowind's) is the plain node it always was.
        constexpr int typedBits
            = ESM::Light::NeutralSpot | ESM::Light::NeutralShadowCasts | ESM::Light::NeutralShadowNever;
        vsg::ref_ptr<vsg::PointLight> node;
        if (light.mData.mFlags & typedBits)
        {
            auto typed = vsg::ref_ptr<View::TypedPointLight>(new View::TypedPointLight);
            if (light.mData.mFlags & ESM::Light::NeutralSpot)
                typed->spotCosHalf = View::spotConeCos(light.mNeutralSpotFov);
            if (light.mData.mFlags & ESM::Light::NeutralShadowCasts)
                typed->shadowRule = Render::PointShadow::Rule::Casts;
            else if (light.mData.mFlags & ESM::Light::NeutralShadowNever)
                typed->shadowRule = Render::PointShadow::Rule::Never;
            node = typed;
        }
        else
            node = vsg::PointLight::create();
        node->intensity = light.mData.mRadius;
        node->color = MWAnim::rgbColor(light.mData.mColor);
        // OpenEden (task 0355 stage b): the brightness a neutral light has beyond what its colour byte holds.
        if ((light.mData.mFlags & ESM::Light::NeutralGain) && std::isfinite(light.mData.mWeight)
            && light.mData.mWeight > 0.f)
            node->color *= light.mData.mWeight;
        if (light.mData.mFlags & ESM::Light::Negative)
            node->color *= -1;
        return node;
    }

    vsg::ref_ptr<Anim::Controller> createLightControllerIfRequired(const ESM::Light& light, const vsg::vec3& color)
    {
        if (!(light.mData.mFlags
                & (ESM::Light::Flicker | ESM::Light::FlickerSlow | ESM::Light::Pulse | ESM::Light::PulseSlow)))
            return {};
        if (light.mData.mFlags & ESM::Light::NeutralFlicker)
        {
            if (auto params = MWAnim::NeutralFlicker::unpack(light.mData.mTime, light.mData.mValue))
            {
                params->mMovement = light.mNeutralMovement;
                // The same precedence as the Morrowind rule: a flicker bit wins over a pulse bit.
                const bool pulse = !(light.mData.mFlags & (ESM::Light::Flicker | ESM::Light::FlickerSlow));
                return vsg::ref_ptr<Anim::Controller>(NeutralLightController::create(*params, pulse, color));
            }
        }
        return vsg::ref_ptr<Anim::Controller>(LightController::create(light.mData.mFlags, color));
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

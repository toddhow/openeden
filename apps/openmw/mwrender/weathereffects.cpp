#include "weathereffects.hpp"

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/mwanimation/effect.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "mask.hpp"

namespace MWRender
{
    namespace
    {
        // OpenMW's defaultWrapRange for weather particles (SkyManager::setWeather, cf3dbd3aa7).
        const vsg::vec3 sSnowWrapRange(1024.f, 1024.f, 800.f);
    }

    WeatherEffects::WeatherEffects(const MWAnim::Context& context)
        : mContext(context)
        , mRoot(vsg::Group::create())
        , mTransform(new Anim::Transform)
        , mWrapAround(std::make_shared<vsgAdapters::ParticleSystem::WrapAround>())
    {
        mWrapAround->range = sSnowWrapRange;
        mContext.compileContext = mContext.compileContext->clone(Mask_Effect);
        mRoot->addChild(mTransform);
    }

    WeatherEffects::~WeatherEffects()
    {
        setEffect({});
    }

    void WeatherEffects::setEffect(const std::string& model)
    {
        if (model == mModel)
            return;
        mModel = model;

        if (mEffect)
        {
            mEffect->detach();
            mEffect.reset();
            mMaterials.clear();
        }
        if (model.empty())
            return;

        try
        {
            auto effect = std::make_unique<MWAnim::Effect>();
            effect->mwctx = mContext;
            effect->loop = true;
            // The effect gets its own materials, which update() fades, as OpenMW's AlphaFader did.
            auto node = mContext.readNode(model);
            mMaterials.clear();
            Anim::Animation animation = MWAnim::cloneWithOwnMaterials(node, mMaterials);
            // OpenMW wrapped a NIF effect's particles around the camera for snow, or when the weather also had rain,
            // which no Morrowind weather combines with a NIF effect (SkyManager::setWeather, cf3dbd3aa7).
            if (model == Settings::models().mWeathersnow.get().value())
            {
                for (auto& [controller, object] : animation.clonedControllers)
                    if (auto* particles = dynamic_cast<vsgAdapters::ParticleSystem*>(controller))
                        particles->wrapAround = mWrapAround;
            }
            effect->compile(animation, node, { mTransform.get() }, { node.get() });
            effect->attachTo(mTransform.get());
            mEffect = std::move(effect);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load weather effect '" << model << "': " << e.what();
        }
    }

    void WeatherEffects::update(
        float dt, const vsg::vec3& cameraPosition, const vsg::quat& orientation, float alpha, bool visible)
    {
        if (visible != !mRoot->children.empty())
        {
            if (visible)
                mRoot->children = { mTransform };
            else
                mRoot->children.clear();
        }
        if (!mEffect)
            return;
        mTransform->translation = cameraPosition;
        mWrapAround->centre = cameraPosition;
        mTransform->setAttitude(orientation);
        mEffect->run(dt);
        // OpenMW's AlphaFader: diffuse (0, 0, 0, precipitation alpha) on every material of the effect, every frame and
        // after the effect's own material controllers, so a controller animating diffuse does not undo it.
        for (auto& material : mMaterials)
        {
            material->value().diffuse = vsg::vec4(0.f, 0.f, 0.f, alpha);
            material->dirty();
        }
    }
}

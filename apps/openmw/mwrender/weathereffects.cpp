#include "weathereffects.hpp"

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/mwanimation/effect.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "mask.hpp"

namespace MWRender
{
    WeatherEffects::WeatherEffects(const MWAnim::Context& context)
        : mContext(context)
        , mRoot(vsg::Group::create())
        , mTransform(new Anim::Transform)
    {
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
        }
        if (model.empty())
            return;

        try
        {
            auto effect = std::make_unique<MWAnim::Effect>();
            effect->mwctx = mContext;
            effect->loop = true;
            auto [animation, node] = MWAnim::Effect::load(mContext, mContext.readNode(model), {}, false);
            effect->compile(animation, node, { mTransform.get() }, { node.get() });
            effect->attachTo(mTransform.get());
            mEffect = std::move(effect);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load weather effect '" << model << "': " << e.what();
        }
    }

    void WeatherEffects::update(float dt, const vsg::vec3& cameraPosition, const vsg::quat& orientation, bool visible)
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
        mTransform->setAttitude(orientation);
        mEffect->run(dt);
    }
}

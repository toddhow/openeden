#ifndef VSGOPENMW_MWRENDER_EFFECTS_H
#define VSGOPENMW_MWRENDER_EFFECTS_H

#include <components/mwanimation/context.hpp>
#include <components/vsgutil/composite.hpp>

namespace MWAnim
{
    class Effects;
}
namespace MWRender
{
    // Note: effects attached to another object should be managed by MWRender::addEffect.
    // This class manages "free" effects, i.e. attached to a dedicated scene node in the world.
    class Effects : public vsgUtil::Composite<vsg::Group>
    {
    public:
        Effects(const MWAnim::Context& ctx);
        ~Effects();

        /// Add an effect. When it's finished playing, it will be removed automatically unless it loops.
        /// @param effectId identifies the effect for remove (OpenMW 0.51 Lua VFX); may be empty
        void add(const std::string& model, const std::string& textureOverride, const vsg::vec3& worldPosition,
            float scale, bool isMagicVFX = true, std::string_view effectId = {}, bool loop = false);

        /// Remove the effects with this ID
        void remove(std::string_view effectId);

        void update(float dt);

        /// Remove all effects
        void clear();

    private:
        vsg::ref_ptr<MWAnim::Effects> mEffects;
        MWAnim::Context mContext;
    };
}

#endif

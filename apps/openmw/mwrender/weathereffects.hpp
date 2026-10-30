#ifndef OPENEDEN_MWRENDER_WEATHEREFFECTS_H
#define OPENEDEN_MWRENDER_WEATHEREFFECTS_H

#include <memory>
#include <string>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>

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
     * that follows the camera; during storms it is turned to face the storm direction, as Morrowind does. Owned by
     * RenderManager; runs on the main thread from RenderManager::update.
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

        /// Places the effect at the camera, turned to orientation, and shows it only when visible (false underwater).
        void update(float dt, const vsg::vec3& cameraPosition, const vsg::quat& orientation, bool visible);

    private:
        MWAnim::Context mContext;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<Anim::Transform> mTransform;
        std::unique_ptr<MWAnim::Effect> mEffect;
        std::string mModel;
    };
}

#endif

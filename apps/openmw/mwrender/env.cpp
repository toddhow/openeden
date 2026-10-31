#include "env.hpp"

#include <vsg/state/BindDescriptorSet.h>

#include <components/pipeline/layout.hpp>
#include <components/pipeline/object.hpp>
#include <components/pipeline/sets.hpp>
#include <components/pipeline/viewbindings.hpp>
#include <components/vsgutil/addchildren.hpp>

#include "../mwworld/class.hpp"

namespace MWRender
{
    vsg::ref_ptr<vsg::StateGroup> createEnv(const vsg::vec4& color, float alpha)
    {
        auto sg = vsg::StateGroup::create();
        Pipeline::Object object;
        object.value().envColor = color;
        object.value().alpha = alpha;
        auto layout = Pipeline::getCompatiblePipelineLayout();
        sg->stateCommands = { vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::OBJECT_SET, vsg::Descriptors{ object.descriptor() }) };
        return sg;
    }

    std::optional<vsg::vec4> getGlowColor(const MWWorld::ConstPtr& item)
    {
        if (!item.getClass().getEnchantment(item).empty())
            return item.getClass().getEnchantmentColor(item);
        return {};
    }

    void addEnv(vsg::ref_ptr<vsg::Node>& node, std::optional<vsg::vec4> color, float alpha)
    {
        if (color || alpha != 1.f)
        {
            auto sg = createEnv(*color, alpha);
            vsgUtil::addChildren(*sg, *node);
            node = sg;
        }
    }

    void addEnv(vsg::ref_ptr<vsg::Node>& node, const MWWorld::ConstPtr& item, float alpha)
    {
        addEnv(node, getGlowColor(item), alpha);
    }
}

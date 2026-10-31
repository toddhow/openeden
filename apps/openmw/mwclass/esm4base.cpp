#include "esm4base.hpp"
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include "../mwgui/tooltips.hpp"

#include "../mwrender/renderinginterface.hpp"

#include "../mwphysics/physicssystem.hpp"
#include "../mwworld/ptr.hpp"

namespace MWClass
{
    void ESM4Impl::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface)
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    void ESM4Impl::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics)
    {
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, MWPhysics::Layers::WORLD);
    }

    MWGui::ToolTipInfo ESM4Impl::getToolTipInfo(std::string_view name, int count)
    {
        MWGui::ToolTipInfo info;
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + MWGui::ToolTips::getCountString(count);
        return info;
    }
}

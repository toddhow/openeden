#include "static.hpp"
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>

#include <components/esm3/loadstat.hpp>

#include "../mwphysics/physicssystem.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/vismask.hpp"

#include "classmodel.hpp"

namespace MWClass
{
    Static::Static()
        : MWWorld::RegisteredClass<Static>(ESM::Static::sRecordId)
    {
    }

    void Static::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    void Static::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Static::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, MWPhysics::Layers::WORLD);
    }

    std::string_view Static::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Static>(ptr);
    }

    std::string_view Static::getName(const MWWorld::ConstPtr& ptr) const
    {
        return {};
    }

    bool Static::hasToolTip(const MWWorld::ConstPtr& ptr) const
    {
        return false;
    }

    MWWorld::Ptr Static::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Static>* ref = ptr.get<ESM::Static>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }
}

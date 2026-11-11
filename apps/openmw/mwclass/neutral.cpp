#include "neutral.hpp"

#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>

#include "../mwphysics/physicssystem.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"

namespace MWClass
{
    namespace
    {
        using openeden::sdk::game::CollisionSource;
        using openeden::sdk::game::ObjectKind;

        // The same rule the ESM4 classes use to keep editor markers out of the world (esm4base.hpp, isMarkerModel).
        bool isMarkerModel(std::string_view model)
        {
            return Misc::StringUtils::ciStartsWith(model, "marker");
        }

        // Kinds that are live things in later stages (N5): inert references here.
        bool isLiveThing(ObjectKind kind)
        {
            return kind == ObjectKind::Actor || kind == ObjectKind::Item || kind == ObjectKind::LeveledList;
        }

        const MWWorld::NeutralBase& baseOf(const MWWorld::ConstPtr& ptr)
        {
            return *ptr.get<MWWorld::NeutralBase>()->mBase;
        }
    }

    Neutral::Neutral()
        : MWWorld::RegisteredClass<Neutral>(MWWorld::NeutralBase::sRecordId)
    {
    }

    MWWorld::Ptr Neutral::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<MWWorld::NeutralBase>* ref = ptr.get<MWWorld::NeutralBase>();
        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    std::string_view Neutral::getName(const MWWorld::ConstPtr& ptr) const
    {
        return baseOf(ptr).mEditorId;
    }

    std::string_view Neutral::getModel(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::NeutralBase& base = baseOf(ptr);
        if (isLiveThing(base.mKind) || isMarkerModel(base.mModel))
            return {};
        return base.mModel;
    }

    void Neutral::insertObjectRendering(
        const MWWorld::Ptr&, const std::string&, MWRender::RenderingInterface&) const
    {
        // Stage N3a-4 (the Skyrim model reader) draws the model. Nothing is drawn from the bounds.
    }

    void Neutral::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Neutral::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string&, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        const MWWorld::NeutralBase& base = baseOf(ptr);
        if (base.mCollision.source == CollisionSource::None || !base.mCollision.hasBounds)
            return;
        const auto& lo = base.mCollision.boundsMin;
        const auto& hi = base.mCollision.boundsMax;
        const vsg::vec3 centre(static_cast<float>((lo.x + hi.x) * 0.5), static_cast<float>((lo.y + hi.y) * 0.5),
            static_cast<float>((lo.z + hi.z) * 0.5));
        const vsg::vec3 half(static_cast<float>((hi.x - lo.x) * 0.5), static_cast<float>((hi.y - lo.y) * 0.5),
            static_cast<float>((hi.z - lo.z) * 0.5));
        physics.addBoxObject(ptr, half, centre, rotation, MWPhysics::Layers::WORLD);
    }
}

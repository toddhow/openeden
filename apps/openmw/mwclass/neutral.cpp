#include "neutral.hpp"

#include <cstdlib>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>

#include "../mwphysics/physicssystem.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/skyrimmodel.hpp"
#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

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

        // Kinds that are live things in later stages (N5): inert references here, with no model. Items (clutter, books,
        // food) are drawn from stage N3a-4 on: they are static meshes in the world until N5 makes them pick-up-able.
        bool isLiveThing(ObjectKind kind)
        {
            return kind == ObjectKind::Actor || kind == ObjectKind::LeveledList;
        }

        // Developer trace (task 0110 stage N3a-4b): OPENEDEN_NEUTRAL_COLLISION_TRACE=1 logs, for every object of a
        // neutral cell that gets collision, its base, editor id, model, position, scale and whether it collides as
        // its mesh or as its box. Read once.
        bool collisionTraceWanted()
        {
            static const bool wanted = [] {
                const char* value = std::getenv("OPENEDEN_NEUTRAL_COLLISION_TRACE");
                return value != nullptr && *value != '\0' && *value != '0';
            }();
            return wanted;
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
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        // Stage N3a-4: the model is read through the same cache as Morrowind's (the NIF reader sees a Skyrim file and
        // draws it as static shapes). An empty model (markers, actors, leveled lists) draws nothing; nothing is drawn
        // from the bounds.
        if (!model.empty())
        {
            MWRender::SkyrimModel::counters().mRequests.fetch_add(1, std::memory_order_relaxed);
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    void Neutral::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Neutral::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        const MWWorld::NeutralBase& base = baseOf(ptr);
        if (base.mCollision.source == CollisionSource::None)
            return;
        // Stage N3a-4b: an object that has a model collides with the model's own triangles (a Skyrim SE file, read
        // through the physics shape manager's scene hook); an object whose model gives no collision mesh, and one
        // with no model, keeps the box of its bounds (stage N3a-3).
        const bool asMesh = !model.empty()
            && physics.addModelObject(ptr, VFS::Path::toNormalized(model), rotation, MWPhysics::Layers::WORLD);
        if (collisionTraceWanted())
        {
            const auto& position = ptr.getRefData().getPosition().asVec3();
            Log(Debug::Info) << "NCT " << ptr.getCellRef().getRefId() << " " << base.mEditorId << " model '" << model
                             << "' at " << position.x << "," << position.y << "," << position.z << " scale "
                             << ptr.getCellRef().getScale()
                             << (asMesh                         ? " mesh"
                                    : base.mCollision.hasBounds ? " box"
                                                                : " none");
        }
        if (asMesh || !base.mCollision.hasBounds)
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

#include "neutral.hpp"

#include <cstdlib>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>

#include <components/l10n/manager.hpp>

#include <components/worldmodel/store.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/skyrimmodel.hpp"
#include "../mwworld/action.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/failedaction.hpp"
#include "../mwworld/neutraldoor.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

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

    namespace
    {
        // Developer aids (task 0115), read once: OPENEDEN_NEUTRAL_DOOR_TRACE=1 logs a line per door activation,
        // OPENEDEN_NEUTRAL_UNLOCK_DOORS=1 lets every locked door open (for testing; not a setting).
        bool envFlag(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr && *value != '\0' && *value != '0';
        }

        bool doorTraceWanted()
        {
            static const bool wanted = envFlag("OPENEDEN_NEUTRAL_DOOR_TRACE");
            return wanted;
        }

        bool unlockDoorsWanted()
        {
            static const bool wanted = envFlag("OPENEDEN_NEUTRAL_UNLOCK_DOORS");
            return wanted;
        }

        // A refusal: shows its text (a key of the OMWEngine l10n context, looked up when the action runs) to the
        // player. Owns what it needs, so nothing dangles.
        class RefusalAction final : public MWWorld::Action
        {
            std::string mKey;

            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor != MWMechanics::getPlayer())
                    return;
                const std::string text
                    = MWBase::Environment::get().getL10nManager()->getMessage("OMWEngine", mKey);
                MWBase::Environment::get().getWindowManager()->messageBox(text);
            }

        public:
            explicit RefusalAction(std::string key)
                : MWWorld::Action(false)
                , mKey(std::move(key))
            {
            }
        };

        // One trace line per activation (OPENEDEN_NEUTRAL_DOOR_TRACE).
        void logDoor(const MWWorld::Ptr& ptr, const MWWorld::NeutralDoorDecision& decision,
            std::string_view destination, bool reused)
        {
            if (!doorTraceWanted())
                return;
            // What a hop leaves behind (task 0115 memory check): the base records and the extension host's resident
            // neutral cells must not grow when a cell is entered again.
            const std::size_t bases = MWBase::Environment::get().getESMStore()->get<MWWorld::NeutralBase>().getSize();
            const NeutralWorld::NeutralWorldStore* neutral
                = MWBase::Environment::get().getExtensionManager()->neutralWorldStore();
            Log(Debug::Info) << "NDOOR " << ptr.getCellRef().getRefNum().mIndex << " in "
                             << ptr.getCell()->getCell()->getDescription() << ": "
                             << MWWorld::neutralDoorOutcomeName(decision.mOutcome) << " destination '" << destination
                             << "' arrival " << decision.mArrival.pos[0] << "," << decision.mArrival.pos[1] << ","
                             << decision.mArrival.pos[2] << (destination.empty() ? "" : reused ? " reused" : " built")
                             << ", base records " << bases
                             << ", resident neutral cells "
                             << (neutral != nullptr ? neutral->stats().mResidentCells : 0);
        }
    }

    std::unique_ptr<MWWorld::Action> Neutral::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        const MWWorld::NeutralBase& base = baseOf(ptr);
        if (base.mKind != ObjectKind::Door)
            return MWWorld::Class::activate(ptr, actor);

        const MWWorld::CellStore* cell = ptr.getCell();
        const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
        const openeden::sdk::game::WorldObject* object
            = (cell != nullptr && placement) ? cell->neutralObject(*placement) : nullptr;
        if (object == nullptr)
            return MWWorld::Class::activate(ptr, actor);

        MWWorld::WorldModel* worldModel = MWBase::Environment::get().getWorldModel();
        MWWorld::CellStore* destination = nullptr;
        bool reused = false;
        const auto decision = MWWorld::decideNeutralDoor(*object, actor == MWMechanics::getPlayer(),
            unlockDoorsWanted(), [&](openeden::sdk::game::WorldId destinationCell) {
                const MWWorld::NeutralCellLookup found
                    = worldModel->findNeutralCell(cell->neutralGameId(), destinationCell);
                if (found.mStatus == MWWorld::NeutralCellStatus::Found)
                {
                    destination = found.mStore;
                    reused = found.mReused;
                    return MWWorld::NeutralDestination::Interior;
                }
                // Logged on every activation; asking again is cheap because the store caches the cell it built.
                Log(Debug::Info) << "Neutral door " << *placement << ": " << found.mError;
                return found.mStatus == MWWorld::NeutralCellStatus::Exterior ? MWWorld::NeutralDestination::Exterior
                                                                             : MWWorld::NeutralDestination::Unavailable;
            });
        logDoor(ptr, decision,
            destination != nullptr ? std::string_view(destination->getCell()->getNameId()) : std::string_view(),
            reused);

        switch (decision.mOutcome)
        {
            case MWWorld::NeutralDoorOutcome::Teleport:
                // No followers: a neutral session has none (task 0115, open question 9). No door sound: Skyrim's are
                // sound descriptor records the pack does not decode.
                return std::make_unique<MWWorld::ActionTeleport>(
                    destination->getCell()->getId(), decision.mArrival, false);
            case MWWorld::NeutralDoorOutcome::Locked:
            case MWWorld::NeutralDoorOutcome::Exterior:
            case MWWorld::NeutralDoorOutcome::Nowhere:
                return std::make_unique<RefusalAction>(std::string(MWWorld::neutralDoorMessageKey(decision.mOutcome)));
            case MWWorld::NeutralDoorOutcome::NotAPlayer:
                return std::make_unique<MWWorld::FailedAction>(std::string_view{}, ptr);
            case MWWorld::NeutralDoorOutcome::Inert:
            case MWWorld::NeutralDoorOutcome::NotADoor:
                break;
        }
        return MWWorld::Class::activate(ptr, actor);
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
        //
        // Stage N3a-5: a light (the base carries a LightComponent) gives its light through the engine's light path
        // (MWRender::Scene::insertModel reads the component of a neutral base); a light with no model (Skyrim has
        // many) still gets a light-only object, and a light that is off by default is not created at all.
        const MWWorld::NeutralBase& base = baseOf(ptr);
        const bool offByDefault = base.mLight && base.mLight->offByDefault;
        const bool wantsLight = base.mLight && !offByDefault;
        if (!model.empty())
        {
            MWRender::SkyrimModel::counters().mRequests.fetch_add(1, std::memory_order_relaxed);
            // allowLight false only for a light that is off by default (the same call ESM4 lights and Morrowind's
            // Off By Default lights make); every other object keeps the call it had.
            renderingInterface.getObjects().insertModel(ptr, model, !offByDefault);
        }
        else if (wantsLight)
            renderingInterface.getObjects().insertModel(ptr, model, true);
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

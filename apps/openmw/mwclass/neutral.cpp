#include "neutral.hpp"

#include <cmath>
#include <cstdlib>

#include <components/debug/debuglog.hpp>
#include <components/esmbridge/util.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/game/actor_rules.hpp>
#include <openeden/game/dialogue_source.hpp>

#include <vsg/maths/quat.h>

#include <components/l10n/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/skyrimshape.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/skyrimmodel.hpp"
#include "../mwworld/action.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/failedaction.hpp"
#include "../mwworld/neutraldialogue.hpp"
#include "../mwworld/neutraldoor.hpp"
#include "../mwworld/neutrallock.hpp"
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

        // Task 0141: talking to a neutral actor. The pack that owns the actor's base runs the conversation through the
        // optional DialogueSource of its registered world source; the engine only presents it (neutraldialogue.hpp).
        class TalkAction final : public MWWorld::Action
        {
            MWWorld::Ptr mTarget;

            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor != MWMechanics::getPlayer())
                    return;
                const MWWorld::NeutralBase& base = baseOf(mTarget);
                MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
                NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
                openeden::sdk::game::WorldCellSource* source = store != nullptr ? store->source(base.mGameId) : nullptr;
                auto* dialogue = dynamic_cast<openeden::sdk::game::DialogueSource*>(source);
                const MWWorld::CellStore* cell = mTarget.getCell();
                if (dialogue == nullptr || cell == nullptr || cell->neutralCell() == nullptr)
                    return;
                const auto placement = MWWorld::neutralPlacementOf(mTarget.getCellRef().getRefNum());
                openeden::sdk::game::DialogueTurn turn;
                std::string error;
                if (!dialogue->beginConversation(base.mBase, placement ? *placement : openeden::sdk::game::kNoWorldId,
                        cell->neutralCell()->id, turn, error))
                {
                    Log(Debug::Info) << "Neutral dialogue with " << base.mEditorId << ": " << error;
                    MWBase::Environment::get().getWindowManager()->messageBox(
                        MWBase::Environment::get().getL10nManager()->getMessage("OMWEngine", "NeutralDialogueNothing"));
                    return;
                }
                Log(Debug::Info) << "NTALK " << base.mEditorId << ": greeting of " << turn.text.size() << " bytes, "
                                 << turn.topics.size() << " topics";
                MWWorld::neutralDialogueSession().start(
                    base.mGameId, turn, turn.speakerName.empty() ? base.mEditorId : turn.speakerName, mTarget);
            }

        public:
            explicit TalkAction(const MWWorld::Ptr& target)
                : MWWorld::Action(false, target)
                , mTarget(target)
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
        // Task 0141: an actor of a pack that can run conversations is talked to (only with the actors switched on, the
        // same setting that draws them, and only by the player).
        if (base.mKind == ObjectKind::Actor && Settings::game().mNeutralActors && actor == MWMechanics::getPlayer())
            return std::make_unique<TalkAction>(ptr);
        if (base.mKind != ObjectKind::Door && base.mKind != ObjectKind::Container)
            return MWWorld::Class::activate(ptr, actor);

        const MWWorld::CellStore* cell = ptr.getCell();
        const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
        const openeden::sdk::game::WorldObject* object
            = (cell != nullptr && placement) ? cell->neutralObject(*placement) : nullptr;
        if (object == nullptr)
            return MWWorld::Class::activate(ptr, actor);

        // Task 0128: a lock is opened by its key (the session's key ring) or refused; an opened lock stays open for the
        // session. Lock picking is not applied (Morrowind's rules read Morrowind GMSTs and skills). Only the player
        // is judged here: a door still refuses anybody else below.
        bool lockOpen = false;
        if (actor == MWMechanics::getPlayer())
        {
            MWWorld::NeutralLockState& locks = MWWorld::neutralLockState();
            const std::string& gameId = cell->neutralGameId();
            const MWWorld::NeutralLockOutcome lockOutcome = MWWorld::decideNeutralLock(*object, unlockDoorsWanted(),
                locks.isUnlocked(gameId, *placement),
                [&](openeden::sdk::game::WorldId key) { return locks.hasKey(gameId, key); });
            if (doorTraceWanted() && object->lock)
                Log(Debug::Info) << "NLOCK " << *placement << " in " << cell->getCell()->getDescription()
                                 << ": locked=" << object->lock->locked << " key " << object->lock->key << " level "
                                 << object->lock->level << " -> " << static_cast<int>(lockOutcome)
                                 << " (0 none, 1 open, 2 unlocked by key, 3 refused)";
            if (lockOutcome == MWWorld::NeutralLockOutcome::Locked)
                return std::make_unique<RefusalAction>(base.mKind == ObjectKind::Container
                        ? std::string("NeutralContainerLocked")
                        : std::string(MWWorld::neutralLockMessageKey(lockOutcome)));
            if (lockOutcome == MWWorld::NeutralLockOutcome::UnlockedByKey)
            {
                locks.unlock(gameId, *placement);
                MWBase::Environment::get().getWindowManager()->messageBox(
                    MWBase::Environment::get().getL10nManager()->getMessage(
                        "OMWEngine", std::string(MWWorld::neutralLockMessageKey(lockOutcome))));
            }
            lockOpen = lockOutcome != MWWorld::NeutralLockOutcome::NotLocked;
        }
        // A container has nothing more to do before the neutral inventory (stage N5).
        if (base.mKind == ObjectKind::Container)
            return MWWorld::Class::activate(ptr, actor);

        MWWorld::WorldModel* worldModel = MWBase::Environment::get().getWorldModel();
        MWWorld::CellStore* destination = nullptr;
        bool reused = false;
        const auto decision = MWWorld::decideNeutralDoor(*object, actor == MWMechanics::getPlayer(),
            unlockDoorsWanted() || lockOpen, [&](openeden::sdk::game::WorldId destinationCell) {
                Misc::LoadTrace::Scope traceFind("neutral.door.find");
                const MWWorld::NeutralCellLookup found
                    = worldModel->findNeutralCell(cell->neutralGameId(), destinationCell);
                traceFind.end();
                if (found.mStatus == MWWorld::NeutralCellStatus::Found)
                {
                    destination = found.mStore;
                    reused = found.mReused;
                    return MWWorld::NeutralDestination::Interior;
                }
                if (found.mStatus == MWWorld::NeutralCellStatus::Exterior && found.mWorldSupported)
                {
                    // Task 0125 (stage E2): an exterior of a world the pack can describe. The square is the one the
                    // arrival stands in, in the worldspace's own cell size; the cell is built (or reused) and entered
                    // like any other store.
                    const auto& arrival = object->teleport->destination.position;
                    const auto square = MWWorld::neutralExteriorSquare(
                        arrival.x, arrival.y, ESM::getCellSize(found.mWorldspace));
                    if (!square)
                    {
                        Log(Debug::Warning) << "Neutral door " << *placement
                                            << ": the arrival is not a place the exterior grid can hold";
                        return MWWorld::NeutralDestination::Unavailable;
                    }
                    const ESM::ExteriorCellLocation location(square->first, square->second, found.mWorldspace);
                    Misc::LoadTrace::Scope traceExterior("neutral.door.exterior");
                    destination = &worldModel->getExterior(location);
                    reused = false;
                    return MWWorld::NeutralDestination::ExteriorWorld;
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

    const std::string& Neutral::getInventoryIcon(const MWWorld::ConstPtr&) const
    {
        static const std::string empty;
        return empty;
    }

    const ESM::RefId& Neutral::getUpSoundId(const MWWorld::ConstPtr&) const
    {
        static const ESM::RefId empty;
        return empty;
    }

    const ESM::RefId& Neutral::getDownSoundId(const MWWorld::ConstPtr&) const
    {
        static const ESM::RefId empty;
        return empty;
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
        if (base.mKind == ObjectKind::Actor)
        {
            insertActorRendering(ptr, base, renderingInterface);
            return;
        }
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

    void Neutral::insertActorRendering(const MWWorld::Ptr& ptr, const MWWorld::NeutralBase& base,
        MWRender::RenderingInterface& renderingInterface) const
    {
        // Task 0121 (decision 0017, stage N5-1): the game pack that owns the base describes how its actor looks through
        // the optional ActorAppearanceSource of its registered world source. With the setting off, or a pack that has no
        // such interface, an actor stays an inert reference as before.
        if (!Settings::game().mNeutralActors)
            return;
        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
        openeden::sdk::game::WorldCellSource* source = store != nullptr ? store->source(base.mGameId) : nullptr;
        auto* appearanceSource = dynamic_cast<openeden::sdk::game::ActorAppearanceSource*>(source);
        if (appearanceSource == nullptr)
            return;
        openeden::sdk::game::ActorAppearance appearance;
        std::string error;
        // The variation is the placement's own number: a leveled template picks the same entry every time it is shown.
        const std::uint32_t variation = static_cast<std::uint32_t>(ptr.getCellRef().getRefNum().mIndex);
        if (!appearanceSource->describeActor(base.mBase, variation, appearance, error))
        {
            Log(Debug::Warning) << "Skyrim actor " << base.mEditorId << " has no appearance: " << error;
            return;
        }
        renderingInterface.getObjects().insertNeutralActor(ptr, appearance);

        // Task 0135 (decision 0017, stage N5-3): the actor's body in the physics world, a capsule sized from the race
        // and the individual. Scene::addObject calls insertObject only for an object with a model, and an actor has
        // none, so this is the one place that sees both the appearance and the physics system.
        if (MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem())
        {
            static const float raise = [] {
                const char* text = std::getenv("OPENEDEN_NEUTRAL_ACTOR_RAISE"); // developer aid: shows the fall
                return text != nullptr ? static_cast<float>(std::atof(text)) : 0.f;
            }();
            physics->addNeutralActor(ptr, appearance.heightScale, raise);

            // Stage S3 (actor hooks): a game whose source implements ActorRules is told about the actor and, once a
            // frame, asked what it wants. The resolver looks the source up each time it is used (the pack may have
            // been stopped by then); it holds the game id, never a pointer to the pack.
            if (dynamic_cast<openeden::sdk::game::ActorRules*>(source) != nullptr)
            {
                const std::string gameId = base.mGameId;
                physics->setNeutralRulesResolver([gameId]() -> openeden::sdk::game::ActorRules* {
                    MWBase::ExtensionManager* manager = MWBase::Environment::get().getExtensionManager();
                    NeutralWorld::NeutralWorldStore* worldStore = manager != nullptr ? manager->neutralWorldStore() : nullptr;
                    return dynamic_cast<openeden::sdk::game::ActorRules*>(
                        worldStore != nullptr ? worldStore->source(gameId) : nullptr);
                });
                if (const auto reference = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum()))
                    physics->announceNeutralActor(ptr, *reference, base.mBase, base.mEditorId);
            }
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
        if (doorTraceWanted() && (base.mKind == ObjectKind::Door || base.mKind == ObjectKind::Container)
            && ptr.getCell() != nullptr)
        {
            // Task 0128 (developer trace): every locked door and container of a cell as it is inserted.
            const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            const openeden::sdk::game::WorldObject* object
                = placement ? ptr.getCell()->neutralObject(*placement) : nullptr;
            if (object != nullptr && object->lock && object->lock->locked)
            {
                const auto& position = ptr.getRefData().getPosition().asVec3();
                Log(Debug::Info) << "NLOCKED " << (base.mKind == ObjectKind::Door ? "door" : "container") << " "
                                 << *placement << " " << ptr.getCellRef().getRefId() << " " << base.mEditorId
                                 << " at " << position.x << "," << position.y << "," << position.z << " key "
                                 << object->lock->key << " level " << object->lock->level;
            }
        }
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
        // Task 0140: a model that was read and has nothing to collide with (effects, water volumes, markers) gets no
        // collision at all, not the box of its bounds: those bounds can be 3,000 units across.
        if (!MWPhysics::SkyrimShape::standInBoxWanted(asMesh, base.mCollision.hasBounds,
                !model.empty() && MWPhysics::SkyrimShape::modelReadWithNoCollision(VFS::Path::toNormalized(model).value())))
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

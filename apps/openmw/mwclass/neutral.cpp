#include "neutral.hpp"

#include <cmath>
#include <cstdlib>

#include <components/debug/debuglog.hpp>
#include <components/esmbridge/util.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/game/actor_mechanics.hpp>
#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>
#include <openeden/game/dialogue_source.hpp>
#include <openeden/game/player_item_source.hpp>

#include <vsg/maths/quat.h>

#include <components/l10n/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwgui/tooltips.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/skyrimshape.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/skyrimmodel.hpp"
#include "../mwrender/skyrimsequence.hpp"
#include "../mwworld/action.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/cellref.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/failedaction.hpp"
#include "../mwworld/neutraldialogue.hpp"
#include "../mwworld/neutraldisplayname.hpp"
#include "../mwworld/neutraldoor.hpp"
#include "../mwworld/neutralfollowers.hpp"
#include "../mwworld/neutralinventory.hpp"
#include "../mwworld/neutrallock.hpp"
#include "../mwworld/neutralstore.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

namespace MWClass
{
    namespace
    {
        // Task 0232 (`[Game] neutral object animations`): the model has an Open sequence the renderer plays. A pure
        // function of the model path (cached), so it does not depend on whether the object is drawn yet.
        bool objectAnimationsWanted(std::string_view model)
        {
            if (!Settings::game().mNeutralObjectAnimations || model.empty())
                return false;
            Resource::ResourceSystem* resources = MWBase::Environment::get().getResourceSystem();
            return resources != nullptr && resources->getVFS() != nullptr
                && MWRender::SkyrimSequence::loadModel(
                       *resources->getVFS(), std::string(model), Settings::game().mNeutralWorldMaterials)
                != nullptr;
        }

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

        // A template over Ptr and ConstPtr: a ConstPtr parameter made GCC see a temporary behind the returned
        // reference.
        template <class PtrType>
        const MWWorld::NeutralBase& baseOf(const PtrType& ptr)
        {
            return *ptr.template get<MWWorld::NeutralBase>()->mBase;
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
                const std::string text = MWBase::Environment::get().getL10nManager()->getMessage("OMWEngine", mKey);
                MWBase::Environment::get().getWindowManager()->messageBox(text);
            }

        public:
            explicit RefusalAction(std::string key)
                : MWWorld::Action(false)
                , mKey(std::move(key))
            {
            }
        };

        // Task 0196 (followers): a teleport that takes the player's followers with him. The followers are collected
        // and moved when the action runs (they may have moved since it was made), just before the player goes.
        class TeleportWithFollowersAction final : public MWWorld::Action
        {
            std::string mGameId;
            ESM::RefId mCellId;
            MWWorld::CellStore* mDestination;
            ESM::Position mArrival;

            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor == MWMechanics::getPlayer())
                {
                    // Task 0255: a rider gets off before the door takes him (the horse stays; the pack releases him).
                    if (MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem())
                        physics->dismountNeutralRider();
                    const std::vector<MWWorld::Ptr> followers = MWWorld::collectNeutralFollowers(mGameId, actor);
                    (void)MWWorld::carryNeutralFollowers(mGameId, followers, *mDestination, mArrival);
                }
                MWWorld::ActionTeleport(mCellId, mArrival, false).execute(actor);
            }

        public:
            TeleportWithFollowersAction(
                std::string gameId, const ESM::RefId& cellId, MWWorld::CellStore& destination, const ESM::Position& arrival)
                : MWWorld::Action(true)
                , mGameId(std::move(gameId))
                , mCellId(cellId)
                , mDestination(&destination)
                , mArrival(arrival)
            {
            }
        };

        // Task 0141: talking to a neutral actor. The pack that owns the actor's base runs the conversation through the
        // optional DialogueSource of its registered world source; the engine only presents it (neutraldialogue.hpp).
        class TalkAction final : public MWWorld::Action
        {
            MWWorld::Ptr mTarget;

            // Opens the conversation (the pack's greeting, then the box). The player's own state does not matter here.
            static void begin(const MWWorld::Ptr& target)
            {
                const MWWorld::NeutralBase& base = baseOf(target);
                openeden::sdk::game::DialogueSource* dialogue = dialogueOf(base);
                const MWWorld::CellStore* cell = target.getCell();
                if (dialogue == nullptr || cell == nullptr || cell->neutralCell() == nullptr)
                    return;
                const auto placement = MWWorld::neutralPlacementOf(target.getCellRef().getRefNum());
                openeden::sdk::game::DialogueTurn turn;
                std::string error;
                if (!dialogue->beginConversation(base.mBase, placement ? *placement : openeden::sdk::game::kNoWorldId,
                        cell->neutralCell()->id, turn, error))
                {
                    Log(Debug::Info) << "Neutral dialogue with " << base.mEditorId << ": " << error;
                    if (placement)
                        dialogue->cancelPreparedConversation(*placement); // a sleeper made ready goes back to bed
                    MWBase::Environment::get().getWindowManager()->messageBox(
                        MWBase::Environment::get().getL10nManager()->getMessage("OMWEngine", "NeutralDialogueNothing"));
                    return;
                }
                Log(Debug::Info) << "NTALK " << base.mEditorId << ": greeting of " << turn.text.size() << " bytes, "
                                 << turn.topics.size() << " topics";
                MWWorld::neutralDialogueSession().start(
                    base.mGameId, turn, turn.speakerName.empty() ? base.mEditorId : turn.speakerName, target);
            }

            static openeden::sdk::game::DialogueSource* dialogueOf(const MWWorld::NeutralBase& base)
            {
                MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
                NeutralWorld::NeutralWorldStore* store
                    = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
                return dynamic_cast<openeden::sdk::game::DialogueSource*>(
                    store != nullptr ? store->source(base.mGameId) : nullptr);
            }

            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor != MWMechanics::getPlayer())
                    return;
                MWWorld::NeutralDialogueSession& session = MWWorld::neutralDialogueSession();
                if (session.active() || session.waitingToTalk())
                    return; // a conversation is open, or the last activation is still waiting for its actor
                const MWWorld::NeutralBase& base = baseOf(mTarget);
                openeden::sdk::game::DialogueSource* dialogue = dialogueOf(base);
                const MWWorld::CellStore* cell = mTarget.getCell();
                if (dialogue == nullptr || cell == nullptr || cell->neutralCell() == nullptr)
                    return;
                // Stage S6g: the conversation box pauses the world, so an actor that has to get ready (a sleeper gets
                // up) is asked first, while the world runs; the box opens when the pack says it is ready (or after
                // NeutralTalkWait::kMaxSeconds).
                const auto placement = MWWorld::neutralPlacementOf(mTarget.getCellRef().getRefNum());
                if (placement && !dialogue->prepareConversation(*placement))
                {
                    Log(Debug::Info) << "NTALK " << base.mEditorId << ": waiting for it to get ready";
                    const MWWorld::Ptr target = mTarget;
                    if (session.startWhenReady(base.mGameId, *placement, target, [target] { begin(target); }))
                        return;
                    dialogue->cancelPreparedConversation(*placement);
                }
                begin(mTarget);
            }

        public:
            explicit TalkAction(const MWWorld::Ptr& target)
                : MWWorld::Action(false, target)
                , mTarget(target)
            {
            }
        };

        // Task 0182 (milestone M-S6): activating a container tells the pack's Lua scripts (a global script, then the
        // player's windows); the engine opens no window of its own. See mwworld/neutralinventory.hpp.
        class OpenInventoryAction final : public MWWorld::Action
        {
            std::uint64_t mPlacement;
            std::uint64_t mBase;
            std::string mName;
            std::string mKind;

            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor != MWMechanics::getPlayer())
                    return;
                std::vector<MWBase::LuaManager::ExtensionEventField> fields;
                for (const MWWorld::NeutralInventoryEventField& f :
                    MWWorld::neutralInventoryEventFields(mPlacement, mBase, mKind, mName))
                    fields.push_back({ f.mName, f.mText });
                Log(Debug::Info) << "NINV " << mKind << " " << MWWorld::neutralHexId(mPlacement) << " base "
                                 << MWWorld::neutralHexId(mBase) << " '" << mName << "' opened";
                // Task 0208: "item" is a placed item picked up, "pickpocket" a living actor's pockets.
                // Task 0221: "flora" is a plant the player harvests.
                const std::string_view eventName = mKind == "bench" ? MWWorld::kNeutralBenchEvent
                    : mKind == "item"                               ? MWWorld::kNeutralPickupEvent
                    : mKind == "pickpocket"                         ? MWWorld::kNeutralPickpocketEvent
                    : mKind == "flora"                              ? MWWorld::kNeutralHarvestEvent
                    : mKind == "resource"                           ? MWWorld::kNeutralResourceEvent
                                                                    : MWWorld::kNeutralInventoryEvent;
                MWBase::Environment::get().getLuaManager()->sendExtensionEvent(std::string(eventName), fields);
                // Task 0232: a chest's lid lifts while its window is open.
                if (mKind == "container" && objectAnimationsWanted(getTarget().getClass().getModel(getTarget())))
                    if (MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem())
                        physics->openNeutralLid(getTarget());
            }

        public:
            OpenInventoryAction(const MWWorld::Ptr& target, std::uint64_t placement, std::uint64_t base,
                std::string name, std::string kind = "container")
                : MWWorld::Action(false, target)
                , mPlacement(placement)
                , mBase(base)
                , mName(std::move(name))
                , mKind(std::move(kind))
            {
            }
        };

        // Task 0251 (riding): activating a horse the pack lets the player mount, or the one he sits on, tells the pack.
        class MountAction final : public MWWorld::Action
        {
            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor != MWMechanics::getPlayer())
                    return;
                if (MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem())
                    physics->requestNeutralMount(getTarget());
            }

        public:
            explicit MountAction(const MWWorld::Ptr& target)
                : MWWorld::Action(false, target)
            {
            }
        };

        // Task 0203: a locked door or container the player has no key for offers the pack's lock picking minigame (an
        // event to the scripts) instead of the refusal; see mwworld/neutralinventory.hpp.
        class LockpickAction final : public MWWorld::Action
        {
            std::uint64_t mPlacement;
            std::uint64_t mBase;
            std::string mKind;
            std::int32_t mLevel;
            std::string mName;

            void executeImp(const MWWorld::Ptr& actor) override
            {
                if (actor != MWMechanics::getPlayer())
                    return;
                std::vector<MWBase::LuaManager::ExtensionEventField> fields;
                for (const MWWorld::NeutralInventoryEventField& f :
                    MWWorld::neutralLockpickEventFields(mPlacement, mBase, mKind, mLevel, mName))
                    fields.push_back({ f.mName, f.mText });
                Log(Debug::Info) << "NLOCKPICK " << mKind << " " << MWWorld::neutralHexId(mPlacement) << " level "
                                 << mLevel << " '" << mName << "' offered";
                MWBase::Environment::get().getLuaManager()->sendExtensionEvent(
                    std::string(MWWorld::kNeutralLockpickEvent), fields);
            }

        public:
            LockpickAction(const MWWorld::Ptr& target, std::uint64_t placement, std::uint64_t base, std::string kind,
                std::int32_t level, std::string name)
                : MWWorld::Action(false, target)
                , mPlacement(placement)
                , mBase(base)
                , mKind(std::move(kind))
                , mLevel(level)
                , mName(std::move(name))
            {
            }
        };

        // The registered cell source of a game as the interface the pack's player items answer (null when the game has
        // none or the extension manager is not there).
        openeden::sdk::game::PlayerItemSource* playerItemSourceOf(const std::string& gameId)
        {
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            return dynamic_cast<openeden::sdk::game::PlayerItemSource*>(
                store != nullptr ? store->source(gameId) : nullptr);
        }

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
                             << decision.mArrival.pos[2]
                             << (destination.empty() ? ""
                                        : reused     ? " reused"
                                                     : " built")
                             << ", base records " << bases << ", resident neutral cells "
                             << (neutral != nullptr ? neutral->stats().mResidentCells : 0);
        }
    }

    std::unique_ptr<MWWorld::Action> Neutral::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        const MWWorld::NeutralBase& base = baseOf(ptr);
        // Task 0184: a dead actor is searched, not talked to (only with [Game] neutral inventory; the pack's combat
        // rules told the engine it died).
        if (base.mKind == ObjectKind::Actor && actor == MWMechanics::getPlayer()
            && Settings::game().mNeutralInventory.get())
        {
            const MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem();
            const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            const bool dead = physics != nullptr && physics->isNeutralActorDead(ptr);
            if (placement != std::nullopt
                && MWWorld::decideNeutralLoot(base.mKind, true, true, dead) == MWWorld::NeutralInventoryDecision::Open)
                return std::make_unique<OpenInventoryAction>(
                    ptr, *placement, base.mBase, std::string(getName(ptr)), "actor");
        }
        // Task 0208: a sneaking player picks a living actor's pockets (only with [Game] neutral inventory); a placed item
        // is picked up by the pack's scripts.
        if ((base.mKind == ObjectKind::Actor || base.mKind == ObjectKind::Item) && actor == MWMechanics::getPlayer())
        {
            const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            const bool inventoryOn = Settings::game().mNeutralInventory.get();
            if (placement != std::nullopt)
            {
                const MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem();
                const bool dead = physics != nullptr && physics->isNeutralActorDead(ptr);
                const bool sneaking = actor.getClass().getCreatureStats(actor).getStance(
                    MWMechanics::CreatureStats::Stance_Sneak);
                if (MWWorld::decideNeutralPickpocket(base.mKind, true, inventoryOn, dead, sneaking)
                    == MWWorld::NeutralInventoryDecision::Open)
                    return std::make_unique<OpenInventoryAction>(
                        ptr, *placement, base.mBase, std::string(getName(ptr)), "pickpocket");
                if (MWWorld::decideNeutralPickup(base.mKind, true, inventoryOn)
                    == MWWorld::NeutralInventoryDecision::Open)
                    return std::make_unique<OpenInventoryAction>(
                        ptr, *placement, base.mBase, std::string(getName(ptr)), "item");
            }
        }
        // Task 0221: a placed flora or tree the player activates is harvested (the pack's scripts decide whether the base
        // names an ingredient; nothing happens when no script listens).
        if ((base.mKind == ObjectKind::Flora || base.mKind == ObjectKind::Tree) && actor == MWMechanics::getPlayer())
        {
            const auto plantPlacement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            if (plantPlacement != std::nullopt
                && MWWorld::decideNeutralHarvest(base.mKind, true, Settings::game().mNeutralInventory.get())
                    == MWWorld::NeutralInventoryDecision::Open)
                return std::make_unique<OpenInventoryAction>(
                    ptr, *plantPlacement, base.mBase, std::string(getName(ptr)), "flora");
        }
        // Task 0224: a placed Activator the player activates (an ore vein) tells the pack's scripts; nothing happens when
        // none listens.
        if (base.mKind == ObjectKind::Activator && actor == MWMechanics::getPlayer())
        {
            const auto resourcePlacement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            if (resourcePlacement != std::nullopt
                && MWWorld::decideNeutralResource(base.mKind, true, Settings::game().mNeutralInventory.get())
                    == MWWorld::NeutralInventoryDecision::Open)
                return std::make_unique<OpenInventoryAction>(
                    ptr, *resourcePlacement, base.mBase, std::string(getName(ptr)), "resource");
        }
        // Task 0251: a horse the player may mount, or the one he sits on, is mounted or left (before talking).
        if (base.mKind == ObjectKind::Actor && Settings::game().mNeutralActors && actor == MWMechanics::getPlayer())
        {
            const MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem();
            if (physics != nullptr && physics->neutralCanMount(ptr))
                return std::make_unique<MountAction>(ptr);
        }
        // Task 0141: an actor of a pack that can run conversations is talked to (only with the actors switched on, the
        // same setting that draws them, and only by the player).
        if (base.mKind == ObjectKind::Actor && Settings::game().mNeutralActors && actor == MWMechanics::getPlayer())
            return std::make_unique<TalkAction>(ptr);
        // Task 0199: a furniture is a crafting bench when the pack says so; the pack answers from the base record.
        if (base.mKind == ObjectKind::Furniture && actor == MWMechanics::getPlayer())
        {
            const auto benchPlacement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            if (benchPlacement != std::nullopt
                && MWWorld::decideNeutralBench(base.mKind, true, Settings::game().mNeutralInventory.get())
                    == MWWorld::NeutralInventoryDecision::Open)
                return std::make_unique<OpenInventoryAction>(
                    ptr, *benchPlacement, base.mBase, std::string(getName(ptr)), "bench");
        }
        if (base.mKind != ObjectKind::Door && base.mKind != ObjectKind::Container)
            return MWWorld::Class::activate(ptr, actor);

        const MWWorld::CellStore* cell = ptr.getCell();
        const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
        const openeden::sdk::game::WorldObject* object
            = (cell != nullptr && placement) ? cell->neutralObject(*placement) : nullptr;
        if (object == nullptr)
            return MWWorld::Class::activate(ptr, actor);

        // Task 0184: a key in the pack's inventory (with [Game] neutral inventory) opens a lock as one from the ring
        // does.
        const auto keyHeld = [&](const std::string& gameIdOf, openeden::sdk::game::WorldId key) {
            std::function<bool(openeden::sdk::game::WorldId)> inventory;
            if (Settings::game().mNeutralInventory.get())
            {
                MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
                NeutralWorld::NeutralWorldStore* store
                    = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
                if (auto* items = dynamic_cast<openeden::sdk::game::PlayerItemSource*>(
                        store != nullptr ? store->source(gameIdOf) : nullptr))
                    inventory = [items](openeden::sdk::game::WorldId item) { return items->playerHasItem(item); };
            }
            return MWWorld::neutralKeyHeld(key,
                [&](openeden::sdk::game::WorldId k) { return MWWorld::neutralLockState().hasKey(gameIdOf, k); },
                inventory);
        };
        // Task 0128: a lock is opened by its key (the session's key ring) or refused; an opened lock stays open for the
        // session. Lock picking is not applied (Morrowind's rules read Morrowind GMSTs and skills). Only the player
        // is judged here: a door still refuses anybody else below.
        bool lockOpen = false;
        if (actor == MWMechanics::getPlayer())
        {
            MWWorld::NeutralLockState& locks = MWWorld::neutralLockState();
            const std::string& gameId = cell->neutralGameId();
            // Task 0203: a lock the player picked (the pack remembers it and saves it) is open.
            openeden::sdk::game::PlayerItemSource* packItems
                = Settings::game().mNeutralInventory.get() ? playerItemSourceOf(gameId) : nullptr;
            std::function<bool(openeden::sdk::game::WorldId)> packPicked;
            if (packItems != nullptr)
                packPicked = [packItems](openeden::sdk::game::WorldId id) { return packItems->lockPicked(id); };
            const bool alreadyOpen
                = MWWorld::neutralLockAlreadyOpen(locks.isUnlocked(gameId, *placement), packPicked, *placement);
            if (alreadyOpen && !locks.isUnlocked(gameId, *placement))
                locks.unlock(gameId, *placement);
            const MWWorld::NeutralLockOutcome lockOutcome
                = MWWorld::decideNeutralLock(*object, unlockDoorsWanted(), alreadyOpen,
                    [&](openeden::sdk::game::WorldId key) { return keyHeld(gameId, key); });
            if (doorTraceWanted() && object->lock)
                Log(Debug::Info) << "NLOCK " << *placement << " in " << cell->getCell()->getDescription()
                                 << ": locked=" << object->lock->locked << " key " << object->lock->key << " level "
                                 << object->lock->level << " -> " << static_cast<int>(lockOutcome)
                                 << " (0 none, 1 open, 2 unlocked by key, 3 refused)";
            if (lockOutcome == MWWorld::NeutralLockOutcome::Locked
                && MWWorld::decideNeutralLockpick(base.mKind, true, Settings::game().mNeutralInventory.get(),
                       Settings::gui().mRmlui.get(), packItems != nullptr && packItems->lockpickingOn(), true)
                    == MWWorld::NeutralInventoryDecision::Open)
                return std::make_unique<LockpickAction>(ptr, *placement, base.mBase,
                    base.mKind == ObjectKind::Container ? "container" : "door", object->lock ? object->lock->level : 0,
                    std::string(getName(ptr)));
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
        // Task 0182: with [Game] neutral inventory a container opens the inventory windows (through the pack's
        // scripts); otherwise a container has nothing more to do (as before).
        if (base.mKind == ObjectKind::Container)
        {
            if (MWWorld::decideNeutralInventory(base.mKind, actor == MWMechanics::getPlayer(),
                    Settings::game().mNeutralInventory.get())
                == MWWorld::NeutralInventoryDecision::Open)
                return std::make_unique<OpenInventoryAction>(ptr, *placement, base.mBase, std::string(getName(ptr)));
            return MWWorld::Class::activate(ptr, actor);
        }

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
                    const auto square
                        = MWWorld::neutralExteriorSquare(arrival.x, arrival.y, ESM::getCellSize(found.mWorldspace));
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
                if (Settings::game().mNeutralActors)
                    return std::make_unique<TeleportWithFollowersAction>(
                        cell->neutralGameId(), destination->getCell()->getId(), *destination, decision.mArrival);
                return std::make_unique<MWWorld::ActionTeleport>(
                    destination->getCell()->getId(), decision.mArrival, false);
            case MWWorld::NeutralDoorOutcome::Locked:
            case MWWorld::NeutralDoorOutcome::Exterior:
            case MWWorld::NeutralDoorOutcome::Nowhere:
                return std::make_unique<RefusalAction>(std::string(MWWorld::neutralDoorMessageKey(decision.mOutcome)));
            case MWWorld::NeutralDoorOutcome::NotAPlayer:
                return std::make_unique<MWWorld::FailedAction>(std::string_view{}, ptr);
            case MWWorld::NeutralDoorOutcome::Inert:
                // Task 0232: the player opens a door that goes nowhere (a key, or a plain press): it swings open and
                // closes by itself a few seconds later.
                if (base.mKind == ObjectKind::Door && actor == MWMechanics::getPlayer()
                    && objectAnimationsWanted(ptr.getClass().getModel(ptr)))
                    if (MWPhysics::PhysicsSystem* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem())
                        physics->pokeNeutralDoor(ptr, true);
                break;
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

    namespace
    {
        MWWorld::NeutralDisplayNames& displayNames()
        {
            static MWWorld::NeutralDisplayNames names;
            return names;
        }

        openeden::sdk::game::DisplayNameSource* displayNameSourceOf(const MWWorld::NeutralBase& base)
        {
            if (!MWBase::Environment::exists())
                return nullptr;
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            return dynamic_cast<openeden::sdk::game::DisplayNameSource*>(
                store != nullptr ? store->source(base.mGameId) : nullptr);
        }

        // Task 0224: the pack's name for this ONE placement (a depleted ore vein), else empty. Only asked for the kinds
        // that can be resources, and only with `[Game] neutral inventory`.
        std::string_view placementNameOf(const MWWorld::ConstPtr& ptr, const MWWorld::NeutralBase& base)
        {
            if ((base.mKind != ObjectKind::Activator && base.mKind != ObjectKind::Furniture)
                || !Settings::game().mNeutralInventory.get())
                return {};
            openeden::sdk::game::DisplayNameSource* source = displayNameSourceOf(base);
            const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            if (source == nullptr || placement == std::nullopt)
                return {};
            const std::string name = source->placementNameOf(*placement, base.mBase);
            return name.empty() ? std::string_view() : displayNames().intern(name);
        }
    }

    std::string_view Neutral::getName(const MWWorld::ConstPtr& ptr) const
    {
        // Task 0150: the display name of the base (the pack's optional DisplayNameSource), else the editor id.
        const MWWorld::NeutralBase& base = baseOf(ptr);
        if (const std::string_view own = placementNameOf(ptr, base); !own.empty())
            return own;
        return displayNames().nameOf(base, displayNameSourceOf(base));
    }

    bool Neutral::hasToolTip(const MWWorld::ConstPtr& ptr) const
    {
        // task 0199: a furniture shows its name when the neutral inventory is on (a crafting bench may be among them);
        // task 0221: so does a flora or a tree (the pack's name is the harvest prompt); task 0224: an activator shows
        // one only when the pack has a name for that placement (an ore vein), so levers and markers stay quiet
        const MWWorld::NeutralBase& base = baseOf(ptr);
        if (base.mKind == ObjectKind::Activator)
            return !placementNameOf(ptr, base).empty();
        return MWWorld::neutralKindHasToolTip(base.mKind, Settings::game().mNeutralInventory.get());
    }

    MWGui::ToolTipInfo Neutral::getToolTipInfo(const MWWorld::ConstPtr& ptr, int /*count*/) const
    {
        MWGui::ToolTipInfo info;
        info.caption = std::string(getName(ptr));
        return info;
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
        // OPENEDEN_NEUTRAL_PLACE_TRACE=1 (developer aid, task 0152) logs where the engine puts every model.
        static const bool placeTrace = std::getenv("OPENEDEN_NEUTRAL_PLACE_TRACE") != nullptr;
        if (placeTrace && !model.empty())
        {
            const ESM::Position& p = ptr.getRefData().getPosition();
            Log(Debug::Info) << "NPLACE " << model << " pos " << p.pos[0] << "," << p.pos[1] << "," << p.pos[2]
                             << " rot " << p.rot[0] << "," << p.rot[1] << "," << p.rot[2] << " scale "
                             << ptr.getCellRef().getScale() << " id " << ptr.getCellRef().getRefId().toDebugString();
        }
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
        // the optional ActorAppearanceSource of its registered world source. With the setting off, or a pack that has
        // no such interface, an actor stays an inert reference as before.
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
            physics->addNeutralActor(ptr, appearance.heightScale, raise, appearance.walkSpeed, appearance.runSpeed,
                appearance.capsuleRadius);

            // Task 0154 (stage S7): the pack's numbers and relations for this actor, as a log line (a debug readout:
            // nothing in the engine consumes them yet).
            static const bool mechanicsTrace = std::getenv("OPENEDEN_NEUTRAL_ACTOR_TRACE") != nullptr;
            if (mechanicsTrace)
            {
                if (auto* mechanics = dynamic_cast<openeden::sdk::game::ActorMechanicsSource*>(source))
                {
                    openeden::sdk::game::ActorStats stats;
                    openeden::sdk::game::ActorDisposition disposition;
                    std::string mechanicsError;
                    if (mechanics->actorStats(base.mBase, variation, 0, stats, mechanicsError)
                        && mechanics->actorDisposition(base.mBase, variation, disposition, mechanicsError))
                    {
                        std::string skills;
                        for (const std::uint8_t skill : stats.skills)
                            skills += (skills.empty() ? "" : ",") + std::to_string(skill);
                        Log(Debug::Info) << "NACTVAL " << base.mEditorId << " level " << stats.level << " health "
                                         << stats.health << " magicka " << stats.magicka << " stamina " << stats.stamina
                                         << " skills " << skills << " scales " << stats.scalesWithPlayer
                                         << " essential " << stats.essential << " unique " << stats.unique
                                         << " relation " << static_cast<int>(disposition.relationToPlayer)
                                         << " aggression " << static_cast<int>(disposition.aggression) << " hostile "
                                         << disposition.hostileOnSight;
                    }
                    else
                        Log(Debug::Info) << "NACTVAL " << base.mEditorId << " none: " << mechanicsError;
                }
            }

            // Stage S3 (actor hooks): a game whose source implements ActorRules is told about the actor and, once a
            // frame, asked what it wants. The resolver looks the source up each time it is used (the pack may have
            // been stopped by then); it holds the game id, never a pointer to the pack.
            if (dynamic_cast<openeden::sdk::game::ActorRules*>(source) != nullptr)
            {
                const std::string gameId = base.mGameId;
                physics->setNeutralRulesResolver([gameId]() -> openeden::sdk::game::ActorRules* {
                    MWBase::ExtensionManager* manager = MWBase::Environment::get().getExtensionManager();
                    NeutralWorld::NeutralWorldStore* worldStore
                        = manager != nullptr ? manager->neutralWorldStore() : nullptr;
                    return dynamic_cast<openeden::sdk::game::ActorRules*>(
                        worldStore != nullptr ? worldStore->source(gameId) : nullptr);
                });
                // Task 0170 (combat stage S5-2): the same source may also implement the combat hooks.
                if (dynamic_cast<openeden::sdk::game::CombatRules*>(source) != nullptr)
                    physics->setNeutralCombatResolver([gameId]() -> openeden::sdk::game::CombatRules* {
                        MWBase::ExtensionManager* manager = MWBase::Environment::get().getExtensionManager();
                        NeutralWorld::NeutralWorldStore* worldStore
                            = manager != nullptr ? manager->neutralWorldStore() : nullptr;
                        return dynamic_cast<openeden::sdk::game::CombatRules*>(
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
                                 << *placement << " " << ptr.getCellRef().getRefId() << " " << base.mEditorId << " at "
                                 << position.x << "," << position.y << "," << position.z << " key " << object->lock->key
                                 << " level " << object->lock->level;
            }
        }
        if (doorTraceWanted() && base.mKind == ObjectKind::Container && ptr.getCell() != nullptr)
        {
            // Task 0182 (developer trace): every container of a cell as it is inserted, with its lock state, to find
            // one to open in a check run.
            const auto placement = MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum());
            const openeden::sdk::game::WorldObject* object
                = placement ? ptr.getCell()->neutralObject(*placement) : nullptr;
            const auto& position = ptr.getRefData().getPosition().asVec3();
            Log(Debug::Info) << "NCONT " << (placement ? *placement : 0) << " " << ptr.getCellRef().getRefId() << " "
                             << base.mEditorId << " at " << position.x << "," << position.y << "," << position.z
                             << " locked " << (object != nullptr && object->lock && object->lock->locked ? 1 : 0);
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
                             << (asMesh                             ? " mesh"
                                        : base.mCollision.hasBounds ? " box"
                                                                    : " none");
        }
        // Task 0157: a door neutral actors walk through swings open for them (the scene gave the navigator the matching
        // off-mesh connection).
        if (asMesh && MWWorld::neutralDoorPtrOpensForActors(ptr))
            physics.addNeutralDoor(ptr, objectAnimationsWanted(model));
        // Task 0140: a model that was read and has nothing to collide with (effects, water volumes, markers) gets no
        // collision at all, not the box of its bounds: those bounds can be 3,000 units across.
        if (!MWPhysics::SkyrimShape::standInBoxWanted(asMesh, base.mCollision.hasBounds,
                !model.empty()
                    && MWPhysics::SkyrimShape::modelReadWithNoCollision(VFS::Path::toNormalized(model).value())))
            return;
        const auto& lo = base.mCollision.boundsMin;
        const auto& hi = base.mCollision.boundsMax;
        const vsg::vec3 centre(static_cast<float>((lo.x + hi.x) * 0.5), static_cast<float>((lo.y + hi.y) * 0.5),
            static_cast<float>((lo.z + hi.z) * 0.5));
        const vsg::vec3 half(static_cast<float>((hi.x - lo.x) * 0.5), static_cast<float>((hi.y - lo.y) * 0.5),
            static_cast<float>((hi.z - lo.z) * 0.5));
        physics.addBoxObject(ptr, half, centre, rotation, MWPhysics::Layers::WORLD);
        if (MWWorld::neutralDoorPtrOpensForActors(ptr))
            physics.addNeutralDoor(ptr, objectAnimationsWanted(model));
    }
}

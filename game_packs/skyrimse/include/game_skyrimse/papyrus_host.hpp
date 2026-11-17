#ifndef OPENEDEN_GAME_SKYRIMSE_PAPYRUS_HOST_H
#define OPENEDEN_GAME_SKYRIMSE_PAPYRUS_HOST_H

// The Skyrim SE game pack's Papyrus host (task 0176, milestone M-S6). It owns a `ScriptPapyrus::PapyrusRuntime`
// (components/script_papyrus), loads ".pex" files through a reader the pack gives it, attaches the scripts that VMAD
// subrecords name (quests, quest fragments, alias scripts, scripts on placed references), fills their properties,
// raises the events the neutral world can raise, runs them on the main thread in the pack's frame phase and answers
// the natives a script calls from what the pack knows. Everything not answered is tolerant: the runtime logs it once
// and returns the zero of its type.
//
// What it does NOT do (see the task): no save chunk (the host leaves `serialiseState()` and `restoreState()` as the
// hook for the saves work), no OnHit or OnDeath (the combat work), no array properties, no scene, package or perk
// fragments, no INFO fragments, no scripts on base forms.
//
// Threading: every call, and every native, runs on the main thread. Determinism: no clock and no randomness of its
// own; time is the sum of the `dt` given to `tick`, the random numbers come from a fixed seed, every container the
// host iterates is ordered.
//
// Attach keys: the runtime allows one instance per `AttachKey`. A form with several scripts uses
// `AttachKind::Form`, the form id and the script's ordinal in the VMAD (0 for the first) in `effectKey`; an alias
// script uses `AttachKind::Alias`, the quest, the alias id in `aliasIndex` and the ordinal in `effectKey`. The saves
// work must write the same keys.

#include <game_skyrimse/follower_state.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_alias.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/story.hpp>
#include <game_skyrimse/triggers.hpp>
#include <game_skyrimse/vmad.hpp>

#include <components/script_papyrus/papyrusruntime.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// The pack setting (`[extension.skyrimse] papyrus`, bool, off by default) that runs the Papyrus host.
    inline constexpr std::string_view kPapyrusKey = "papyrus";

    /// Task 0237: what a MESG says (Message.Show). A box (`mBox`) with two or more buttons asks the player; anything
    /// else is shown as a corner text and answers button 0.
    struct PapyrusMessageInfo
    {
        std::string mText;
        std::vector<std::string> mButtons;
        bool mBox = false;
    };

    /// What the host reads from the pack. Every function runs on the main thread.
    class PapyrusGameData
    {
    public:
        virtual ~PapyrusGameData() = default;

        /// Task 0237: the text, buttons and kind of a MESG. False when the form is not a message.
        [[nodiscard]] virtual bool messageInfo(FormId message, PapyrusMessageInfo& out)
        {
            (void)message, (void)out;
            return false;
        }

        /// Task 0223: the default value of a GLOB (GlobalVariable.GetValue). False when the form is not a global.
        [[nodiscard]] virtual bool globalValue(FormId global, float& value) const
        {
            (void)global, (void)value;
            return false;
        }

        /// Task 0239: the factions a quest alias adds its fill to (ALFC): the game's DialogueFollower alias 0 puts a
        /// follower in CurrentFollowerFaction and PlayerFollowerFaction. False when the alias names none.
        [[nodiscard]] virtual bool aliasFactions(FormId quest, std::uint32_t alias, std::vector<FormId>& out) const
        {
            (void)quest, (void)alias;
            out.clear();
            return false;
        }

        /// The bytes of `scripts/<lowerName>.pex` ("" is never asked). False when there is no such file.
        virtual bool readPex(const std::string& lowerName, std::vector<std::uint8_t>& bytes) = 0;

        /// "Quest", "Actor", "ObjectReference", "Cell", "Keyword", ... for a form id; empty when unknown.
        [[nodiscard]] virtual std::string formClass(FormId form) const = 0;

        /// The position of a placed reference as the data gives it (game units). False for any other form.
        [[nodiscard]] virtual bool referencePosition(FormId reference, double& x, double& y, double& z) const
        {
            (void)reference;
            (void)x;
            (void)y;
            (void)z;
            return false;
        }

        /// The base form of a placed reference, 0 for any other form.
        [[nodiscard]] virtual FormId baseOf(FormId reference) const
        {
            (void)reference;
            return 0;
        }

        /// Task 0181: does the form (a placed reference, or a base form) have the keyword? For a reference the answer
        /// is its base form's. False when the data cannot say.
        [[nodiscard]] virtual bool hasKeyword(FormId form, FormId keyword) const
        {
            (void)form;
            (void)keyword;
            return false;
        }

        /// Task 0181: whether the data places the reference enabled (false for one flagged initially disabled). A
        /// script's Enable and Disable start from this.
        [[nodiscard]] virtual bool initiallyEnabled(FormId reference) const
        {
            (void)reference;
            return true;
        }

        /// Task 0187: is the form a dialogue INFO (a form a TIF fragment script is attached to)? Its class is "TopicInfo".
        [[nodiscard]] virtual bool isTopicInfo(FormId form) const
        {
            (void)form;
            return false;
        }

        /// Task 0187: the quest a dialogue INFO belongs to (TopicInfo.GetOwningQuest): its QNAM, else its topic's.
        [[nodiscard]] virtual FormId questOfInfo(FormId info) const
        {
            (void)info;
            return 0;
        }

        /// Task 0264: the quest a scene belongs to (Scene.GetOwningQuest): the scene's PNAM.
        [[nodiscard]] virtual FormId questOfScene(FormId scene) const
        {
            (void)scene;
            return 0;
        }

        /// Task 0194: is the form a scene (SCEN)? Its class is "Scene".
        [[nodiscard]] virtual bool isScene(FormId form) const
        {
            (void)form;
            return false;
        }

        /// Task 0198: the reference a placement links to by `keyword` (0: the unkeyworded link); 0 when it has none or the
        /// cell was never built (ObjectReference.GetLinkedRef).
        [[nodiscard]] virtual FormId linkedRef(FormId reference, FormId keyword) const
        {
            (void)reference, (void)keyword;
            return 0;
        }

        /// Task 0198: does the data place the actor dead (ACHR flagged dead)? Actor.IsDead starts from it.
        [[nodiscard]] virtual bool initiallyDead(FormId actor) const
        {
            (void)actor;
            return false;
        }

        /// Task 0198: Game.GetFormFromFile: the load-order form id of the local id `local` in the plugin named `lowerFile`
        /// (a load order id is 0 when the plugin is not loaded or the id is out of range).
        [[nodiscard]] virtual FormId formFromFile(std::string_view lowerFile, std::uint32_t local) const
        {
            (void)lowerFile, (void)local;
            return 0;
        }

        /// Task 0217: the location (LCTN) a placed reference stands in, or the player's for the player reference 0x14
        /// (ObjectReference.GetCurrentLocation); 0 when it has none or the data cannot say.
        [[nodiscard]] virtual FormId locationOf(FormId reference) const
        {
            (void)reference;
            return 0;
        }
        /// Task 0202: the pack's id of the cell a placed reference stands in (0: not known), and whether a cell is an
        /// interior. A MoveTo between two different cells of which one is an interior is refused (the engine keeps an
        /// object in its own cell).
        [[nodiscard]] virtual FormId cellOf(FormId reference) const
        {
            (void)reference;
            return 0;
        }
        [[nodiscard]] virtual bool cellIsInterior(FormId cell) const
        {
            (void)cell;
            return false;
        }

        /// The editor id or the name of a form for GetName; empty when none.
        [[nodiscard]] virtual std::string nameOf(FormId form) const
        {
            (void)form;
            return {};
        }
    };

    /// Task 0202: a script moved or turned a reference. Positions are game units, angles radians in the data's own
    /// convention (the pack turns them into a rotation with its eulerToQuat); a part that is not set stays as it is.
    /// `toOriginal`: put the reference back where its record (or its creation) placed it (MoveToMyEditorLocation).
    /// Task 0202: the seconds an Enable(abFadeIn) or Disable(abFadeOut) takes. The game's own fade time is not known to
    /// this code (RECALLED-VERIFY: about a second); a script cannot ask for another.
    inline constexpr float kPapyrusFadeSeconds = 1.0f;

    struct ObjectMoveRequest
    {
        bool hasPosition = false;
        double position[3] = {};
        bool hasAngles = false;
        double angles[3] = {};
        bool toOriginal = false;
    };

    /// Task 0202: a script asked for a new object (PlaceAtMe). `nearCell` is the pack's cell id when the near reference
    /// is itself a created one (0: the hook finds the cell of a placed reference by its record).
    struct ObjectPlaceRequest
    {
        FormId base = 0;
        FormId nearReference = 0;
        FormId nearCell = 0;
        double position[3] = {};
        double angles[3] = {};
        float scale = 1.f;
    };

    struct ObjectPlaceResult
    {
        FormId reference = 0; ///< the new reference's id; 0: nothing was created
        FormId cell = 0; ///< the pack's id of the cell it went to
    };

    /// Task 0198: what the world natives reach beyond the quest state (inventory, actor values, deaths, plugin lookups).
    /// A reference is its form id as the host sees it: a placement id, or 0x14 for the player. Every function runs on the
    /// main thread and every default means "cannot": the native then answers as it did before (the tolerant zero).
    class PapyrusWorldHooks
    {
    public:
        virtual ~PapyrusWorldHooks() = default;
        /// Task 0202. Enable or disable a placed or created reference, fading over `fadeSeconds` (0: at once). True
        /// when
        /// the world was told; false: the host falls back to its EnableChanged callback.
        virtual bool setObjectEnabled(FormId reference, bool enabled, float fadeSeconds)
        {
            (void)reference, (void)enabled, (void)fadeSeconds;
            return false;
        }
        /// Task 0202. Move or turn a reference in the world. False when it cannot (the host still records the change).
        virtual bool moveObject(FormId reference, const ObjectMoveRequest& request)
        {
            (void)reference, (void)request;
            return false;
        }
        /// Task 0202. Create an object near a reference. A result with reference 0 means it could not.
        virtual ObjectPlaceResult placeObject(const ObjectPlaceRequest& request)
        {
            (void)request;
            return {};
        }
        /// Task 0268 (the main quest's opening): a script moved `reference` (of base `base`) to a place that the world
        /// cannot reach by moving the object itself: an actor (the actor system places actors) or an object of another,
        /// maybe unloaded, cell. The hook makes a NEW reference of the same base at the place (`destinationCell` is the
        /// pack's id of the cell the target stands in), hides the old one, and returns the new reference. Reference 0:
        /// not done, the host goes on as before. Main thread.
        virtual ObjectPlaceResult relocateObject(
            FormId reference, FormId base, const ObjectMoveRequest& request, FormId destinationCell)
        {
            (void)reference, (void)base, (void)request, (void)destinationCell;
            return {};
        }
        /// Task 0268: ObjectReference.Is3DLoaded: the world has the reference. The default is yes (an object).
        virtual bool referenceLoaded(FormId reference)
        {
            (void)reference;
            return true;
        }
        /// Task 0261: Cell.SetFactionOwner / SetActorOwner: the cell's owner (a faction or an NPC_ base) is now `owner`.
        virtual bool setCellOwner(FormId cell, FormId owner)
        {
            (void)cell, (void)owner;
            return false;
        }
        /// Adds `count` of base item `item` to the owner's inventory. False when the inventory cannot take it.
        virtual bool addItem(FormId owner, FormId item, std::int32_t count)
        {
            (void)owner, (void)item, (void)count;
            return false;
        }
        /// Removes `count` (fewer when the owner holds fewer); returns how many were removed, -1 when it cannot say.
        virtual std::int32_t removeItem(FormId owner, FormId item, std::int32_t count)
        {
            (void)owner, (void)item, (void)count;
            return -1;
        }
        /// How many of the item the owner holds; -1 when the inventory does not know the owner.
        [[nodiscard]] virtual std::int32_t itemCount(FormId owner, FormId item)
        {
            (void)owner, (void)item;
            return -1;
        }
        /// Moves `count` from one owner to another (RemoveItem with a container). False when it cannot.
        virtual bool transferItem(FormId from, FormId to, FormId item, std::int32_t count)
        {
            (void)from, (void)to, (void)item, (void)count;
            return false;
        }
        /// Moves everything the owner holds to `target` (0: it is removed); returns the number of items, -1 when it cannot.
        virtual std::int64_t removeAllItems(FormId owner, FormId target)
        {
            (void)owner, (void)target;
            return -1;
        }
        /// An actor value by lower-case name ("health", "magicka", "stamina", skills): its current and its maximum
        /// value. False when the pack has no number for it.
        [[nodiscard]] virtual bool actorValue(FormId actor, std::string_view lowerName, double& current, double& maximum)
        {
            (void)actor, (void)lowerName, (void)current, (void)maximum;
            return false;
        }
        /// Sets the current health of an actor (clamped to [0, maximum] by the pack); false when it cannot.
        virtual bool setHealth(FormId actor, double health)
        {
            (void)actor, (void)health;
            return false;
        }
        /// Is the actor dead (combat's own state)? False when unknown.
        [[nodiscard]] virtual bool isDead(FormId actor)
        {
            (void)actor;
            return false;
        }
        /// Starts the death of an actor now (the combat rules' death path). False when it cannot (stale, the player).
        virtual bool killActor(FormId actor)
        {
            (void)actor;
            return false;
        }
        /// Task 0264: Actor.StartCombat (the player is the target), Actor.StopCombat, Actor.ResetHealthAndLimbs and
        /// Actor.GetCombatState. False / 0 when the actor is unknown or the combat AI is off.
        virtual bool startCombat(FormId actor)
        {
            (void)actor;
            return false;
        }
        virtual bool stopCombat(FormId actor)
        {
            (void)actor;
            return false;
        }
        virtual bool resetHealth(FormId actor)
        {
            (void)actor;
            return false;
        }
        virtual bool inCombat(FormId actor)
        {
            (void)actor;
            return false;
        }
        /// Is the form a concrete item the inventory knows? An inventory event filter that names anything else (a form
        /// list the host cannot expand) is ignored.
        [[nodiscard]] virtual bool isItem(FormId form)
        {
            (void)form;
            return false;
        }
        /// Shows a short message to the player (Debug.Notification).
        virtual void notification(const std::string& text) { (void)text; }
        /// Task 0205 (a word wall's script): the player learns a word of power, unlocks one with no dragon soul, asks
        /// whether one is unlocked. False when the pack cannot (the shouts are off, or the form is no word).
        virtual bool teachWord(FormId word)
        {
            (void)word;
            return false;
        }
        virtual bool unlockWord(FormId word)
        {
            (void)word;
            return false;
        }
        [[nodiscard]] virtual bool isWordUnlocked(FormId word)
        {
            (void)word;
            return false;
        }
        // ---- task 0258: what the guards' arrest dialogue fragments call (TIF__000267E0 and its kin). Every default
        // means "cannot": the native then answers as a tolerant one.
        /// The crime faction of an actor (Actor.GetCrimeFaction): its NPC_'s CRIF after templates; 0 when it has none.
        [[nodiscard]] virtual FormId crimeFactionOf(FormId actor)
        {
            (void)actor;
            return 0;
        }
        /// Faction.PlayerPayCrimeGold(abRemoveStolenItems, abGoToJail): the player pays his bounty in the faction. True
        /// when it was done (false: not enough gold, or no offer stands).
        virtual bool payCrimeGold(FormId faction, bool removeStolenItems, bool goToJail)
        {
            (void)faction, (void)removeStolenItems, (void)goToJail;
            return false;
        }
        /// Faction.SendPlayerToJail(abRemoveInventory, abRealJail): the player serves his sentence. True when done.
        virtual bool sendToJail(FormId faction, bool removeInventory, bool realJail)
        {
            (void)faction, (void)removeInventory, (void)realJail;
            return false;
        }
        /// Actor.SetPlayerResistingArrest(): the player will not come quietly; the guards fight. True when done.
        virtual bool resistArrest(FormId actor)
        {
            (void)actor;
            return false;
        }
        /// Faction.GetCrimeGold(): the player's bounty in the faction; -1 when the pack cannot say.
        [[nodiscard]] virtual std::int32_t crimeGold(FormId faction)
        {
            (void)faction;
            return -1;
        }
    };

    /// Task 0194: what the Scene natives and the quest hooks reach (the scene runner). Every function runs on the main
    /// thread; the host calls them outside natives (deferred like the quest attach).
    class SceneControl
    {
    public:
        virtual ~SceneControl() = default;
        virtual bool startScene(FormId scene, bool force) = 0;
        virtual bool stopScene(FormId scene) = 0;
        [[nodiscard]] virtual bool scenePlaying(FormId scene) const = 0;
        virtual void questStarted(FormId quest) = 0;
        virtual void questStopped(FormId quest) = 0;
    };

    struct PapyrusHostOptions
    {
        /// Seed of Utility.RandomInt and RandomFloat.
        std::uint64_t mRandomSeed = 0x9E3779B97F4A7C15ull;
        /// Game seconds per real second for Utility.GetCurrentGameTime (Skyrim's default timescale is 20;
        /// RECALLED-VERIFY).
        double mTimeScale = 20.0;
        /// Scripts one load may pull in through the names in their string tables (a bound on the walk).
        std::uint32_t mMaxLoadedScripts = 4096;
        /// Instances the host attaches in total.
        std::uint32_t mMaxInstances = 65536;
    };

    /// Counts of what happened since the host was made. Deterministic.
    struct PapyrusHostCounters
    {
        std::uint64_t mScriptsLoaded = 0;
        std::uint64_t mScriptsMissing = 0; ///< a script a record names that has no .pex
        std::uint64_t mScriptsRefused = 0; ///< a .pex the reader or loader refused
        std::uint64_t mInstancesAttached = 0;
        std::uint64_t mAttachRefused = 0;
        std::uint64_t mPropertiesSet = 0;
        std::uint64_t mPropertiesRejected = 0; ///< the runtime refused the value (not an auto property, wrong type)
        std::uint64_t mPropertiesSkipped = 0; ///< an array property, or one the script does not have
        std::uint64_t mFragmentsRun = 0; ///< a quest stage fragment function was queued
        std::uint64_t mStageSets = 0; ///< Quest.SetStage calls that changed a stage
        std::uint64_t mUpdatesFired = 0; ///< OnUpdate events sent for RegisterForSingleUpdate or RegisterForUpdate
        std::uint64_t mInfoFragmentsRun = 0; ///< INFO fragment functions queued (task 0187)
        std::uint64_t mStoryEvents = 0; ///< story events raised (SendStoryEvent and the pack's own)
        std::uint64_t mStoryQuestsStarted = 0; ///< quests the story manager started
        std::uint64_t mAliasesFilled = 0; ///< aliases that gained a reference when a quest started or on demand
        std::uint64_t mItemEvents = 0; ///< task 0198: OnItemAdded, OnItemRemoved and OnContainerChanged sent
        std::uint64_t mCombatEvents = 0; ///< task 0198: OnHit, OnDying and OnDeath sent
        std::uint64_t mTicks = 0;
        std::uint64_t mStacksRun = 0;
        std::uint64_t mScriptErrors = 0; ///< a stack that ended with an error
        std::map<std::string, std::uint64_t> mEvents; ///< events sent, by name (lower case)
        std::map<std::string, std::uint64_t> mNatives; ///< natives the host answered, by "class.function" (lower case)
    };

    /// The answer of PapyrusHost::restoreSavedState: ok, or why the state was refused (nothing changed then).
    struct PapyrusRestoreOutcome
    {
        bool mOk = false;
        std::string mError;
    };

    /// Task 0256: where the player is and where he looks, once a frame (from the combat update's player state). The
    /// place decides which trigger volumes count; the view serves ObjectReference.HasLOS and GetDistance of the player.
    struct PapyrusPlayerView
    {
        bool mKnown = false;
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        float mHeading = 0.f; ///< radians about Z as the engine stores it (0 faces +Y)
        float mPitch = 0.f; ///< radians, positive up
        TriggerPlace mPlace;
    };

    class PapyrusHost
    {
    public:
        using LogFunction = std::function<void(bool error, const std::string& line)>;
        /// Called when a script changes a quest's stage, after the state changed: quest, new stage.
        using QuestChanged = std::function<void(FormId quest, std::uint32_t stage)>;
        /// Called when a script enables or disables a placed reference (the engine cannot be told yet: no SDK path).
        using EnableChanged = std::function<void(FormId reference, bool enabled)>;
        using QuestVmadLookup = std::function<const vmad::Vmad*(FormId quest)>;
        /// Task 0187: raises a story event: returns the quests to start (the pack's story manager walks its tree).
        using StoryEventSink = std::function<std::vector<FormId>(const StoryEvent& event)>;

        explicit PapyrusHost(PapyrusGameData& data, PapyrusHostOptions options = {});
        ~PapyrusHost();
        PapyrusHost(const PapyrusHost&) = delete;
        PapyrusHost& operator=(const PapyrusHost&) = delete;

        void setLog(LogFunction log);
        void setQuestChanged(QuestChanged changed);
        /// Task 0217: told after the story manager's event started a quest and its aliases were filled (a developer's
        // log).
        using StoryStarted = std::function<void(FormId quest, const StoryEvent& event)>;
        void setStoryStarted(StoryStarted started);
        void setEnableChanged(EnableChanged changed);
        /// The quest records and their run-time state, and where a quest's VMAD is. The state is changed by
        /// Quest.SetStage, Start and Stop; the host does not own any of the three.
        void setQuests(const QuestIndex* index, QuestState* state, QuestVmadLookup vmads);

        /// Task 0187: fills quest aliases when a quest starts and on demand (`ReferenceAlias.GetReference` of an unfilled
        /// alias). Not owned; null: aliases are only what the quest state already holds.
        void setAliasFiller(QuestAliasFiller* filler);
        /// Task 0187: the story manager. `Keyword.SendStoryEvent` and `raiseStoryEvent` call it and start the quests it
        /// returns (state, scripts, start up stage fragment, aliases).
        void setStoryEventSink(StoryEventSink sink);
        /// Task 0261: does the log entry `entry` of `stage` of `quest` apply now? A stage's fragment (VMAD, "StageIndex")
        /// is that log entry's: it runs only when the entry's conditions hold. Without it every fragment of the stage
        /// runs, as before.
        using StageEntryCheck = std::function<bool(FormId quest, std::uint32_t stage, std::int32_t entry)>;
        void setStageEntryCheck(StageEntryCheck check);
        /// Task 0268: a MoveTo near the player that the world cannot do in place (actors, objects of other cells) makes
        /// a new reference there (PapyrusWorldHooks::relocateObject); the quest's aliases follow it. Off by default.
        void setRelocation(bool on, double maxPlayerDistance);
        /// Task 0261: the live value of a number or bool variable of a script attached to `form` (by the name the
        /// condition's CIS2 string gives); nullopt when no instance of it has one.
        [[nodiscard]] std::optional<double> anyNumberVariable(FormId form, std::string_view name) const;
        /// Task 0194: the scene runner. Not owned; null: the Scene natives answer false and quests start no scenes.
        void setSceneControl(SceneControl* control);
        /// Task 0198: the world natives' reach (see PapyrusWorldHooks). Not owned; null: those natives are tolerant.
        void setWorldHooks(PapyrusWorldHooks* hooks);
        /// Task 0217: where Location.SetKeywordData stores (and GetKeywordData reads). Not owned; null: those natives
        // are
        /// tolerant (nothing stored, 0 read).
        void setLocationKeywordData(LocationKeywordData* data);
        /// Task 0239: where the follower scripts' effects are kept (factions, relationship ranks, the teammate flag,
        /// globals) and the WaitingForPlayer actor value is read from. Not owned; null: those natives are tolerant.
        void setFollowerState(FollowerState* state);
        /// Task 0223: Game.FastTravel(destination reference). The sink queues the travel (the pack's travel service; a
        /// global script does the teleport) and says whether it took it. Not set: the native does nothing but count.
        using FastTravelSink = std::function<bool(FormId destination)>;
        void setFastTravelSink(FastTravelSink sink);

        /// Task 0198: the events the inventory and combat can raise. `from` and `to` are the owners' reference ids (0:
        /// none, as when a script added the item). Queued like any event; run by the next tick.
        /// OnItemAdded goes to `to`'s scripts, OnItemRemoved to `from`'s, OnContainerChanged to scripts on the item form.
        void onItemMoved(FormId from, FormId to, FormId item, std::int32_t count);
        /// OnHit goes to the victim's scripts (and alias scripts that hold it). `attacker` 0: unknown.
        void onActorHit(FormId victim, FormId attacker, bool blocked, bool bash);
        /// OnDying and OnDeath go to the victim's scripts; the actor counts as dead for IsDead.
        void onActorDied(FormId victim, FormId killer);
        /// Task 0264: an essential actor collapsed (OnEnterBleedout on the scripts that hold it).
        void onActorBleedout(FormId actor);
        /// Notifications (Debug.Notification) shown since the host was made: a ring of the last kMaxNotifications.
        static constexpr std::size_t kMaxNotifications = 16;
        [[nodiscard]] std::uint64_t notificationCount() const;
        [[nodiscard]] std::string notificationAt(std::uint64_t number) const; ///< 1-based; empty when out of the ring
        /// Task 0194: a scene fragment function. The scripts of the SCEN VMAD are attached to the scene form (once) and
        /// `function` of the instance named `script` is queued. Returns how many functions were queued.
        std::size_t runSceneFragment(
            FormId scene, const vmad::Vmad& vmad, const std::string& script, const std::string& function);

        /// Raises an event as a script's SendStoryEvent would; returns how many quests it started.
        std::size_t raiseStoryEvent(const StoryEvent& event);
        /// Task 0211 (a developer aid, `story test quest`): starts a quest as the story manager would after choosing it
        /// (aliases filled, scripts attached, start-up stage run). False when the quest is unknown, running or completed.
        bool startQuestForTest(FormId quest, std::int32_t stage = -1); ///< `stage` >= 0: also sets that stage
        /// Task 0269 (a developer aid, `houses test permission`): marks a stage done and current WITHOUT running its
        /// fragments or events, the way a quest the player finished earlier would stand. False for an unknown quest.
        bool markStageDoneForTest(FormId quest, std::uint32_t stage);

        /// Task 0187: the fragments of a shown dialogue line. The scripts of the INFO's VMAD are attached to the INFO
        /// (once), their properties set, and the begin fragment function and then the end fragment function are queued
        /// with the speaker as `akSpeakerRef` (run by the next tick). Returns how many functions were queued.
        std::size_t runInfoFragments(FormId info, const vmad::Vmad& vmad, FormId speakerReference, FormId speakerBase);

        /// Attaches the scripts of every start-game-enabled quest of the index (sorted by form id) and queues the
        /// fragment of its start up stage. Once; later calls do nothing. Needs setQuests.
        void startGame();

        /// Attaches the scripts and the fragment script of one quest, then its alias scripts. False when the quest
        /// is unknown or has nothing to attach. A quest already attached is not attached twice.
        bool attachQuest(FormId quest);

        /// Attaches scripts a VMAD put on a placed reference (already resolved to load order ids). `actor` makes the
        /// reference an Actor, otherwise an ObjectReference.
        std::size_t attachReference(
            FormId reference, FormId base, bool actor, const std::vector<vmad::Script>& scripts);

        /// A cell's references finished loading: sends OnCellLoad and then OnLoad to the instances attached to them.
        void onCellLoaded(FormId cell, const std::vector<FormId>& references);
        /// Task 0268: sends OnCellLoad to the alias scripts of a quest whose alias holds a reference. A quest that starts
        /// after its cells loaded (the main quest's opening: the game starts in the cell, the quest a moment later) has
        /// missed the event the vanilla game gives it; the caller says the cells are loaded. Returns how many were sent.
        std::size_t replayAliasCellLoad(FormId quest);

        /// Task 0256: a placed reference that is a trigger volume (a sphere primitive). False (counted) when it is not a
        /// supported volume. The scripts on the reference get OnTriggerEnter and OnTriggerLeave with the player.
        bool addTrigger(FormId reference, const TriggerVolume& volume);
        /// Task 0256: the player's place and look, once a frame before `updateTriggers`.
        void setPlayerView(const PapyrusPlayerView& view);
        /// Task 0256: sends OnTriggerEnter or OnTriggerLeave for every volume the player went into or out of since the
        /// last call; returns how many events were queued. Run by the next tick.
        std::size_t updateTriggers();
        [[nodiscard]] std::size_t triggerCount() const;
        /// A load: the player is taken to be outside every volume again, so the ones he stands in report entered.
        void resetTriggers();

        /// The player (or an actor) used a placed reference: sends OnActivate(akActionRef) to its instances.
        void onActivate(FormId target, FormId actionRef);

        /// Task 0234: the player read a placed book: sends OnRead to its instances.
        void onRead(FormId target);

        /// Advances script time by \a dt seconds: fires the updates that are due, runs the queued events and wakes
        /// the waiting scripts.
        void tick(float dt);

        /// The runtime's own state as bytes (empty inside a tick). Part of saveState().
        [[nodiscard]] openeden::script::SaveBlob serialiseState() const;
        bool restoreState(const openeden::script::SaveBlob& blob);

        /// Task 0181: everything a save needs to resume the scripts (the pack's `papyrus` chunk): the runtime's state,
        /// the host's ledger of instances (so that its own tables are rebuilt), script time, timers, enable and position
        /// overrides, forced alias fills, objective flags, recorded spells, the quest state it changed, the sets of
        /// attached quests and references, and the counters. Canonical: restoring and saving again gives the same
        /// bytes. Empty before the game has started, inside a tick, and when the runtime cannot write its state.
        [[nodiscard]] std::vector<std::uint8_t> saveState() const;
        /// Replaces the host's state with a saved one. The scripts the state names are loaded first; the state is checked
        /// whole (including every attach key, against the game data) before anything changes: a refusal changes nothing.
        /// Needs setQuests. A host that restored counts as started (startGame does nothing afterwards). For every quest
        /// with a stage above 0 the QuestChanged sink is told, so the package brain's own copy follows. Not inside a
        /// tick.
        PapyrusRestoreOutcome restoreSavedState(std::span<const std::uint8_t> bytes);
        /// startGame() ran, or a state was restored.
        [[nodiscard]] bool started() const;

        /// The enabled state a script gave a reference (nullopt when no script ever enabled or disabled it), and all of
        /// them in form id order: what the pack pushes to the world and replays after a restore.
        [[nodiscard]] std::optional<bool> enableOverride(FormId reference) const;
        [[nodiscard]] std::vector<std::pair<FormId, bool>> enableOverrides() const;
        /// Where a script put a reference (MoveTo, SetPosition; nullopt when it was never moved), and every such
        /// position
        /// in form id order. Task 0202: the world follows (PapyrusWorldHooks::moveObject); the pack also replays them
        /// after a restore, for the cells that were not loaded when the script ran.
        [[nodiscard]] std::optional<std::array<double, 3>> positionOverride(FormId reference) const;
        [[nodiscard]] std::vector<std::pair<FormId, std::array<double, 3>>> positionOverrides() const;
        /// Task 0202: the angles (radians, the data's convention) SetAngle or a MoveTo with matched rotation gave a
        /// reference, and all of them in form id order.
        [[nodiscard]] std::optional<std::array<double, 3>> angleOverride(FormId reference) const;
        [[nodiscard]] std::vector<std::pair<FormId, std::array<double, 3>>> angleOverrides() const;
        /// Task 0202: the references scripts created with PlaceAtMe, in id order. Saved with the host's state.
        struct CreatedObject
        {
            FormId mReference = 0;
            FormId mBase = 0;
            FormId mCell = 0;
            std::array<double, 3> mPosition{};
            std::array<double, 3> mAngles{};
        };
        [[nodiscard]] std::vector<CreatedObject> createdObjects() const;
        /// Task 0185 (the quest journal): the flags Quest.SetObjectiveDisplayed and SetObjectiveCompleted left for an
        /// objective, and a counter that every such call increments.
        struct ObjectiveState
        {
            bool mDisplayed = false;
            bool mCompleted = false;
        };
        [[nodiscard]] ObjectiveState objectiveState(FormId quest, std::uint32_t objective) const;
        [[nodiscard]] std::uint64_t objectiveRevision() const;
        /// Task 0196 (followers): the reference a script forced into an alias (ReferenceAlias.ForceRefTo), 0 when none
        /// or after Clear. Read only; the pack polls the DialogueFollower alias with it.
        [[nodiscard]] FormId forcedAlias(FormId quest, std::uint32_t alias) const;
        /// Task 0238: the value a script gave a GLOB with GlobalVariable.SetValue; false when none did.
        [[nodiscard]] bool globalSetByScript(FormId global, float& value) const;

        /// Whether Actor.AddSpell recorded the spell for the actor (the pack reads it: `spellsOf`).
        [[nodiscard]] bool hasSpell(FormId actor, FormId spell) const;

        // ---- task 0237: standing stones and shrines. Additive.
        /// A Spell.Cast a script made, for the pack to apply (a shrine's blessing). Queued; `takeCasts` empties it.
        struct SpellCast
        {
            FormId mSpell = 0;
            FormId mSource = 0;
            FormId mTarget = 0;
        };
        static constexpr std::size_t kMaxQueuedCasts = 64;
        [[nodiscard]] std::vector<SpellCast> takeCasts();
        /// The spells Actor.AddSpell gave the actor (the player is 0x14), sorted; `spellRevision` moves on every change
        /// (AddSpell, RemoveSpell).
        [[nodiscard]] std::vector<FormId> spellsOf(FormId actor) const;
        [[nodiscard]] std::uint64_t spellRevision() const;
        /// The perks Actor.AddPerk gave the actor (task 0241: saved in the host's state, version 3).
        [[nodiscard]] std::vector<FormId> perksOf(FormId actor) const;
        /// Moves whenever the perk list changes (a new perk, a state restored): the pack reads it again then.
        [[nodiscard]] std::uint64_t perkRevision() const;
        /// The game seconds a script sees (script time at the host's time scale).
        [[nodiscard]] double gameSeconds() const;
        /// Message.Show of a message box asks the player through this sink and answers the last button (the usual
        /// "No"); `answerMessage` then makes the activation of the same object run again with that answer.
        using MessageSink = std::function<void(FormId message, const PapyrusMessageInfo& info)>;
        void setMessageSink(MessageSink sink);
        /// The player's answer (a button index): a button other than the last runs the script again (once every wait it
        /// started is over) with Message.Show answering it. False when no such message was asked.
        bool answerMessage(FormId message, std::int32_t button);
        [[nodiscard]] std::size_t messagesAsked() const;

        [[nodiscard]] const PapyrusHostCounters& counters() const;
        [[nodiscard]] const ScriptPapyrus::Stats& runtimeStats() const;
        /// Calls of natives nobody answered (tolerant: the zero came back), across a save and load: the runtime's own
        /// count of this run plus what the state that was restored had counted.
        [[nodiscard]] std::uint64_t tolerantCalls() const;
        /// Natives a script called that nobody answered ("class.function", lower case, sorted).
        [[nodiscard]] std::vector<std::string> tolerantNatives() const;
        /// Task 0198: tolerant calls of this run by "class.function" (lower case), most calls first, ties by name.
        [[nodiscard]] std::vector<std::pair<std::string, std::uint64_t>> tolerantRanking() const;
        /// The enabled state of a reference as scripts left it (true when no script changed it).
        [[nodiscard]] bool referenceEnabled(FormId reference) const;
        [[nodiscard]] std::size_t instanceCount() const;
        [[nodiscard]] double scriptTime() const;

        // ---- reading and calling, for the tests and the pack's own checks (never needed by a game)
        /// A numeric variable or property (int, float, bool) of the instance of  script attached to  form (the
        /// form id of a quest or a placed reference); nullopt when there is no such instance or name.
        [[nodiscard]] std::optional<double> numberVariable(
            FormId form, std::string_view script, std::string_view name) const;
        [[nodiscard]] std::optional<std::string> stringVariable(
            FormId form, std::string_view script, std::string_view name) const;
        /// An object variable that names a form (an alias handle is not a form: nullopt).
        [[nodiscard]] std::optional<FormId> formVariable(
            FormId form, std::string_view script, std::string_view name) const;
        [[nodiscard]] std::optional<double> aliasScriptNumber(
            FormId quest, std::uint32_t alias, std::string_view script, std::string_view name) const;
        /// Runs a function of the instance now and returns whether it finished without an error (the function runs
        /// to its end or its first wait; natives it calls are answered as usual). Not for use inside a tick.
        bool callFunction(FormId form, std::string_view script, std::string_view function);

        // ---- task 0260 (vampirism). Additive.
        /// One argument of callFunctionWith: a form (an Actor, a Quest ...), an int or a float.
        struct CallArgument
        {
            enum class Kind : std::uint8_t
            {
                Form,
                Int,
                Float
            };
            Kind mKind = Kind::Int;
            FormId mForm = 0;
            std::int32_t mInt = 0;
            float mFloat = 0.f;
            static CallArgument form(FormId f)
            {
                CallArgument a;
                a.mKind = Kind::Form;
                a.mForm = f;
                return a;
            }
            static CallArgument integer(std::int32_t v)
            {
                CallArgument a;
                a.mKind = Kind::Int;
                a.mInt = v;
                return a;
            }
            static CallArgument number(float v)
            {
                CallArgument a;
                a.mKind = Kind::Float;
                a.mFloat = v;
                return a;
            }
        };
        /// callFunction with arguments (a function that takes the player, a stage ...). Same rules as callFunction.
        bool callFunctionWith(FormId form, std::string_view script, std::string_view function,
            std::span<const CallArgument> arguments);
        /// Queues an event (for example "OnUpdateGameTime", which nothing here raises by itself: the pack owns the game
        /// clock) for the instance of  script attached to  form. False when there is no such instance.
        bool sendEventTo(FormId form, std::string_view script, std::string_view event);
        /// Gives a GLOB the value the game's clock (or any pack rule) says, as if a script had called SetValue.
        void setGlobalValue(FormId global, float value);
        /// Sets a numeric variable of an instance (an Int, Float or Bool; a test aid and the pack's own state sync).
        bool setNumberVariable(FormId form, std::string_view script, std::string_view name, double value);

        struct Impl;

    private:
        std::unique_ptr<Impl> mImpl;
    };
}

#endif

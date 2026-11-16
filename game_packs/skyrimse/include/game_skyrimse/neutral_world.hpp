#ifndef OPENEDEN_GAME_SKYRIMSE_NEUTRAL_WORLD_H
#define OPENEDEN_GAME_SKYRIMSE_NEUTRAL_WORLD_H

// The Skyrim SE adapter into the neutral world model (decision 0016, task 0099 stage N0). It reads a cell through the
// SDK's WorldView contract (SkyrimWorldView implements it) and hands back a WorldCell: the same objects, with a
// canonical double precision transform, a collision hint and the pack's payload. This file is also THE named place
// where Skyrim's stored Euler angles become a quaternion (the guide's rule: convert at one named boundary, never ad
// hoc).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only (it calls WorldView::loadCell). Deterministic: the same
// view and cell give equal results, on one platform; floating point results are not claimed to be bit-identical across
// compilers. Nothing throws: a failure is `false` plus a located message.

#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/deaths.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/hud.hpp>
#include <game_skyrimse/hud_style.hpp>
#include <game_skyrimse/sneak.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/riding.hpp>
#include <game_skyrimse/terrain_source.hpp>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/game/player_body.hpp>
#include <openeden/game/player_identity.hpp>
#include <openeden/game/player_image_space.hpp>
#include <openeden/game/actor_mechanics.hpp>
#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>
#include <openeden/game/dialogue_source.hpp>
#include <openeden/game/follower_rules.hpp>
#include <openeden/game/display_name_source.hpp>
#include <openeden/game/grass_source.hpp>
#include <openeden/game/music_source.hpp>
#include <openeden/game/player_item_source.hpp>
#include <openeden/game/player_place.hpp>
#include <openeden/game/scene_speech.hpp>
#include <openeden/game/weather_source.hpp>
#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/game/world_water.hpp>

#include <cstdint>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace game_skyrimse
{
    /// Values past these are refused as hostile (a Skyrim world is about 1e5 units across).
    struct NeutralLimits
    {
        double mMaxCoordinate = 1.0e7;
        double mMaxScale = 1.0e3;
        /// A light's radius past this is refused as hostile (Skyrim's largest in the masters is 2,000 units).
        double mMaxLightRadius = 1.0e6;
        /// A light's fade value past this is refused (the masters use 0.1 to 10).
        double mMaxLightFade = 1.0e3;
    };

    /// The neutral components of one placement (task 0099 stage N4, the record-decoding half): who owns it, how it is
    /// locked and where it leads. Built from the decoded references by makeComponents; the SDK's WorldView 1.0 has
    /// nowhere to carry them, so they travel beside the placed objects through a pack-internal ComponentSource.
    struct PlacementComponents
    {
        std::optional<openeden::sdk::game::OwnerComponent> owner;
        std::optional<openeden::sdk::game::LockComponent> lock;
        std::optional<openeden::sdk::game::TeleportComponent> teleport;
        /// The light of a LIGH base (task 0113, stage N3a-5), filled by the view from the placement's base.
        std::optional<openeden::sdk::game::LightComponent> light;

        /// The XCNT of a placed item stack (task 0208), 0 when none.
        std::int32_t itemCount = 0;

        [[nodiscard]] bool empty() const { return !owner && !lock && !teleport && !light && itemCount == 0; }
        friend bool operator==(const PlacementComponents&, const PlacementComponents&) = default;
    };

    /// What makeComponents could not use, counted (never listed).
    struct ComponentStats
    {
        std::uint64_t mOwnersDropped = 0; ///< XOWN whose owner is not in the load order: no owner component
        std::uint64_t mKeysUnresolved = 0; ///< XLOC whose key is not in the load order: the lock has no key
        std::uint64_t mTeleportsDropped = 0; ///< XTEL with no usable door, or a position or rotation that is hostile
        std::uint64_t mTeleportCellsUnknown = 0; ///< XTEL kept, but its door is in no cell the store knows
        std::uint64_t mLightsDropped = 0; ///< LIGH whose radius or fade is not usable: no light component
        std::uint64_t mLightFadesReset = 0; ///< LIGH whose FNAM is not finite, not positive or past the limit: fade 1
    };

    /// Maps a door reference's id to the cell it stands in (the store's index of teleport doors).
    using RefCellIndex = std::unordered_map<FormId, FormId>;

    /// Owner, lock and teleport of one decoded reference as neutral components. An owner or teleport that cannot be
    /// used is left out and counted; the object itself is never dropped for it. `refCells` names the destination
    /// door's cell (none: the cell stays kNoWorldId and is counted). Hostile numbers: a non-finite or past-the-limit
    /// arrival position or rotation drops the teleport.
    [[nodiscard]] PlacementComponents makeComponents(
        const PlacedRef& ref, const RefCellIndex& refCells, ComponentStats& stats, const NeutralLimits& limits);

    /// The light of a LIGH base as a neutral component: radius, colour, the fade value (FNAM; 1 when absent or
    /// unusable, counted), the animation (Flicker, else FlickerSlow, else Pulse, else PulseSlow: the order the engine's
    /// own light controller tests them in) and the Negative and Off By Default bits; the raw flag word is kept. nullopt
    /// (counted) when the radius is past the limit.
    [[nodiscard]] std::optional<openeden::sdk::game::LightComponent> makeLightComponent(
        const LightData& light, ComponentStats& stats, const NeutralLimits& limits);

    /// The interior lighting of an XCLL: ambient, directional and fog colour and the fog distances. nullopt when a
    /// distance is not finite.
    [[nodiscard]] std::optional<openeden::sdk::game::CellLighting> makeCellLighting(const CellLightingData& lighting);

    /// Gives the components of a placement of a cell the view has just loaded. The pointer is valid until the next
    /// loadCell on the same view.
    /// Gives the cell source the terrain index of a view (task 0120, stage E1). Pack-internal, like ComponentSource.
    class TerrainProvider
    {
    public:
        virtual ~TerrainProvider() = default;
        /// Null when the view cannot say (for example its index timed out).
        [[nodiscard]] virtual const SkyrimTerrainIndex* terrainIndex() const = 0;
    };

    class ComponentSource
    {
    public:
        virtual ~ComponentSource() = default;
        [[nodiscard]] virtual const PlacementComponents* find(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldId placement) const = 0;
        /// The interior lighting of the cell the view has just loaded (task 0113), none when the cell has none or is
        /// not the last one loaded.
        [[nodiscard]] virtual std::optional<openeden::sdk::game::CellLighting> findCellLighting(
            openeden::sdk::game::WorldId /*cell*/) const
        {
            return std::nullopt;
        }
    };

    /// Skyrim's stored angles (radians about X, Y and Z) as a unit quaternion: R = Rx * Ry * Rz with every angle
    /// negated (clockwise for a positive angle): a vector is turned about Z first. The signs were settled by frame (Z)
    /// and by terrain normals (X, Y) in task 0086; the ORDER by task 0151: with Rz * Ry * Rx the object's up axis
    /// depended on its yaw, and 26,397 tilted Tamriel placements near the ground then fit the terrain normal at 0.51
    /// rad on average against 0.26 rad with this order; the floating MountainTrim01 slab (b197124) lies on its slope
    /// with it (docs/tasks/0151-floating-mountain-slabs.md). The engine drew exactly what the pack said (ray hits on
    /// the slab lay on the old order's mesh to 0.2 units): the fault was the order, not the engine. A non-finite angle
    /// gives the identity (toNeutral refuses such an object before it gets here).
    [[nodiscard]] openeden::sdk::game::Quatd eulerToQuat(const openeden::sdk::game::WorldVec3& storedAngles);

    /// One placed object as a neutral object. False with `problem` when a value is not usable (not finite, a coordinate
    /// or bound past the limits, a scale that is not positive or too large); `out` is then untouched.
    /// `components`, if given, supplies the owner, lock and teleport.
    [[nodiscard]] bool toNeutral(const openeden::sdk::game::PlacedObject& placed, openeden::sdk::game::WorldObject& out,
        std::string& problem, const NeutralLimits& limits = {}, const PlacementComponents* components = nullptr);

    /// Loads a cell through `view` and fills `out`; objects that toNeutral refuses are counted in `droppedObjects`.
    /// False with `error` when the view cannot load the cell (`out` is then empty). `components`, if given, is asked
    /// for each placement after the view has loaded the cell (a SkyrimWorldView is its own source); without it the
    /// owner, lock and teleport stay empty.
    [[nodiscard]] bool loadNeutralCell(openeden::sdk::game::WorldView& view, openeden::sdk::game::WorldId cell,
        openeden::sdk::game::WorldCell& out, std::string& error, const NeutralLimits& limits = {},
        const ComponentSource* components = nullptr);

    /// The Skyrim SE side of the core's neutral world store (task 0100 stage N1): a WorldCellSource over any WorldView.
    /// Cell ids are the view's own ids (the load-order form ids), which are already stable for the session. It keeps a
    /// reference to the view, which must outlive it. Main thread only, like the view.
    class SkyrimCellSource final : public openeden::sdk::game::WorldCellSource,
                                   public openeden::sdk::game::ActorAppearanceSource,
                                   public openeden::sdk::game::PlayerBodySource,
                                   public openeden::sdk::game::PlayerIdentitySource,
                                   public openeden::sdk::game::PlayerImageSpaceSource,
                                   public openeden::sdk::game::ActorMechanicsSource,
                                   public openeden::sdk::game::DialogueSource,
                                   public openeden::sdk::game::DisplayNameSource,
                                   public openeden::sdk::game::ActorRules,
                                   public openeden::sdk::game::FollowerRules,
                                   public openeden::sdk::game::CombatRules,
                                   public openeden::sdk::game::CellWaterSource,
                                   public openeden::sdk::game::WeatherSource,
                                   public openeden::sdk::game::MusicSource,
                                   public openeden::sdk::game::GrassSource,
                                   public openeden::sdk::game::PlayerItemSource,
                                   public openeden::sdk::game::SceneSpeechSource,
                                   public openeden::sdk::game::PlayerPlaceSink,
                                   public openeden::sdk::game::RidingRules
    {
    public:
        explicit SkyrimCellSource(openeden::sdk::game::WorldView& view, NeutralLimits limits = {})
            : mView(view)
            , mLimits(limits)
        {
            // Task 0171: a view that knows the combat records (SkyrimWorldView does) gives the melee rules their
            // numbers.
            mCombat.setData(dynamic_cast<CombatData*>(&view));
            // Task 0197: followers fight on the player's side.
            mCombat.setFollowerQuery(
                [this](openeden::sdk::game::WorldId reference) { return mActors.isFollower(reference); });
            // Task 0238: where the player stands, for IsInInterior and GetInWorldspace in an effect's conditions.
            mCombat.setPlaceSource([this](effectconditions::Place& out) {
                if (!mHavePlayerPlace)
                    return false;
                out.mInterior = mPlayerPlace.interior;
                out.mWorld = mPlayerPlace.interior ? kNullFormId : static_cast<FormId>(mPlayerPlace.world);
                return true;
            });
            // Task 0251: a horse is an actor whose appearance names the horse project; the brain stands aside while
            // the player rides it.
            mRiding.setHorseLookup([this](openeden::sdk::game::WorldId base, std::uint32_t variation, HorseInfo& out) {
                openeden::sdk::game::ActorAppearance appearance;
                std::string error;
                if (!describeActor(base, variation, appearance, error) || appearance.animationProject != "horseproject")
                    return false;
                out.mWalkSpeed = appearance.walkSpeed;
                out.mRunSpeed = appearance.runSpeed;
                return true;
            });
            mRiding.setBrainControl(
                [this](openeden::sdk::game::WorldId reference, bool stands) { mActors.onConversation(reference, stands); });
            // Task 0180: the combat HUD reads the pack's fighters through the combat rules.
            mHud.setFighterLookup([this](openeden::sdk::game::CombatSubject subject, SkyrimCombat::FighterView& out) {
                return mCombat.fighterView(subject, out);
            });
        }

        std::string_view gameId() const override { return "skyrimse"; }
        /// Task 0184: whether the player holds an item, from the inventory (the game pack gives the lookup when the
        /// inventory is on); false without one.
        void setPlayerItemLookup(std::function<bool(FormId)> lookup) { mPlayerItems = std::move(lookup); }
        bool playerHasItem(openeden::sdk::game::WorldId item) const override
        {
            return mPlayerItems && mPlayerItems(static_cast<FormId>(item));
        }
        /// Task 0203: the lock picking service's answers (the game pack gives them when `lockpicking and books` is on).
        void setLockpickLookups(std::function<bool(std::uint64_t)> picked)
        {
            mLockPicked = std::move(picked);
            mLockpickingOn = static_cast<bool>(mLockPicked);
        }
        bool lockpickingOn() const override { return mLockpickingOn; }
        bool lockPicked(openeden::sdk::game::WorldId placement) const override
        {
            return mLockPicked && mLockPicked(placement);
        }
        /// Task 0150: forwards to the view's DisplayNameSource.
        std::string displayNameOf(openeden::sdk::game::WorldId base) override;
        /// Task 0224: the pack's name for one placement (a depleted ore vein); empty without a lookup.
        std::string placementNameOf(
            openeden::sdk::game::WorldId placement, openeden::sdk::game::WorldId base) override
        {
            return mPlacementName ? mPlacementName(placement, base) : std::string();
        }
        void setPlacementNameLookup(
            std::function<std::string(openeden::sdk::game::WorldId, openeden::sdk::game::WorldId)> lookup)
        {
            mPlacementName = std::move(lookup);
        }
        std::size_t cellCount() const override { return mView.cellCount(); }
        std::size_t listCells(
            std::size_t first, std::size_t maxCount, std::vector<openeden::sdk::game::WorldId>& out) const override;
        bool buildCell(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCell& out, std::string& error) override;

        // The lookups of world_source.hpp 1.0, through the view (all case-insensitive, as the view's are).
        openeden::sdk::game::WorldId findCellByEditorId(std::string_view editorId) const override;
        openeden::sdk::game::WorldId findCellByGrid(
            openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y) const override;
        openeden::sdk::game::WorldId findWorldByEditorId(std::string_view editorId) const override;
        /// Both games use a unit of about 1.43 cm; the value is compat_skyrimse's
        /// SkyrimGameContract::getUnitsPerMeter() (Morrowind's Constants::UnitsPerMeter), NOT measured for Skyrim:
        /// RECALLED-VERIFY.
        double unitsPerMetre() const override { return 69.99125109; }

        // Terrain (world_source.hpp 1.1), through the view's SkyrimTerrainIndex when the view has one.
        bool worldInfo(openeden::sdk::game::WorldId world, openeden::sdk::game::WorldSpaceInfo& out) const override;
        openeden::sdk::game::TerrainResult buildTerrain(openeden::sdk::game::WorldId world, std::int32_t x,
            std::int32_t y, openeden::sdk::game::TerrainCell& out, std::string& error) override;
        std::size_t listGroundTextures(std::vector<openeden::sdk::game::GroundTexture>& out) const override;
        /// Task 0129: the water of a grid square, through the terrain index (world_water.hpp).
        bool cellWater(openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y,
            openeden::sdk::game::CellWater& out) const override;
        /// Task 0153: the grass of a grid square, through the terrain index (grass_source.hpp).
        bool grassForCell(openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y,
            openeden::sdk::game::GrassCell& out) override;
        /// Forwards to the view when it can describe actors (SkyrimWorldView does), else refuses.
        bool describeActor(openeden::sdk::game::WorldId base, std::uint32_t variation,
            openeden::sdk::game::ActorAppearance& out, std::string& error) override;
        /// Task 0222: the player's body. The picture comes from the view; the swing state is the combat rules'.
        bool playerBody(openeden::sdk::game::PlayerBodyState& out) override;
        bool describePlayerBody(openeden::sdk::game::ActorAppearance& out, std::string& error) override;
        bool describeFirstPersonBody(openeden::sdk::game::ActorAppearance& out, std::string& error) override; // 0245
        bool playerIdentity(openeden::sdk::game::PlayerIdentityState& out) override; // task 0228
        bool playerImageSpace(openeden::sdk::game::PlayerImageSpaceState& out) override; // task 0257
        /// Actor numbers and relations (task 0154): forwarded to the view when it has them (SkyrimWorldView does).
        bool actorStats(openeden::sdk::game::WorldId base, std::uint32_t variation, std::uint16_t playerLevel,
            openeden::sdk::game::ActorStats& out, std::string& error) override;
        bool actorDisposition(openeden::sdk::game::WorldId base, std::uint32_t variation,
            openeden::sdk::game::ActorDisposition& out, std::string& error) override;
        openeden::sdk::game::ActorRelation relationBetween(openeden::sdk::game::WorldId baseA, std::uint32_t variationA,
            openeden::sdk::game::WorldId baseB, std::uint32_t variationB) override;
        /// Conversations (task 0141): forwarded to the view when it can run them (SkyrimWorldView does), else refused.
        bool beginConversation(openeden::sdk::game::WorldId base, openeden::sdk::game::WorldId reference,
            openeden::sdk::game::WorldId cell, openeden::sdk::game::DialogueTurn& out, std::string& error) override;
        bool chooseTopic(std::uint32_t topicId, openeden::sdk::game::DialogueTurn& out, std::string& error) override;
        void endConversation() override;
        /// Stage S6g: a sleeper is asked to stand up before the conversation box (which pauses the world) opens.
        bool prepareConversation(openeden::sdk::game::WorldId reference) override;
        bool readyForConversation(openeden::sdk::game::WorldId reference) override;
        void cancelPreparedConversation(openeden::sdk::game::WorldId reference) override;
        /// Actor hooks (task 0135, stage S3): handled by the pack's own table.
        void onActorAdded(const openeden::sdk::game::ActorInfo& info) override
        {
            mActors.onActorAdded(info);
            mRiding.onActorAdded(info); // task 0251
            mCombat.onActorAdded(info);
            // Task 0180: the name the enemy bar shows (FULL name when the record has one, else the editor id).
            std::string name = displayNameOf(info.base);
            mHud.actorAdded(info.handle, name.empty() ? info.editorId : std::string_view(name), info.editorId);
        }
        void onActorRemoved(openeden::sdk::game::ActorHandle handle) override
        {
            mActors.onActorRemoved(handle);
            mRiding.onActorRemoved(handle); // task 0251
            mCombat.onActorRemoved(handle);
            if (mDeaths != nullptr)
                mDeaths->actorGone(handle.reference); // task 0254
            mHud.actorRemoved(handle);
        }
        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer) override
        {
            // Task 0190: the combat AI's movement. The package brain is told to wait for an engaged actor (a fight is a
            // conversation as far as it is concerned: it stands aside and takes its package up afterwards) BEFORE it
            // runs; the goals are written after it, so nothing of the brain's overrides them. Stops go first for the
            // same reason.
            mAiMoves.clear();
            mCombat.drainAiMoves(mAiMoves);
            using Kind = SkyrimCombat::AiMoveIntent::Kind;
            for (const SkyrimCombat::AiMoveIntent& move : mAiMoves)
            {
                if (move.mKind == Kind::Suspend)
                    mActors.onConversation(move.mActor.reference, true);
                else if (move.mKind == Kind::Resume)
                    mActors.onConversation(move.mActor.reference, false);
                else if (move.mKind == Kind::Stop)
                {
                    (void)writer.cancelGoal(move.mActor);
                    if (mDirectWalkers.erase(move.mActor.reference) != 0) // task 0253
                        (void)writer.setMovement(move.mActor, 0.f, 0.f);
                }
            }
            if (mDeaths != nullptr)
            {
                // Task 0254: the clock, then the dead of a loaded save are put back before the brain moves anyone
                const openeden::sdk::game::ActorClock clock = writer.clock();
                mDeaths->setClock(static_cast<double>(clock.day) * 24.0 + static_cast<double>(clock.hour), clock.valid);
                mDeaths->placeCorpses(writer, mCombat);
            }
            mActors.update(seconds, writer);
            mRiding.update(seconds, writer); // task 0251: after the brain, so a ridden horse's requests win
            if (mBarks != nullptr)
                updateBarks(seconds, writer); // task 0231
            for (const SkyrimCombat::AiMoveIntent& move : mAiMoves)
            {
                if (move.mKind == Kind::GoTo && move.mDirect)
                {
                    // Task 0253: a dragon walks straight at its goal (its size finds no path on the navmesh).
                    double x = 0.0, y = 0.0, z = 0.0;
                    if (writer.actorPosition(move.mActor, x, y, z))
                    {
                        const double dx = move.mX - x, dy = move.mY - y;
                        const double d = std::hypot(dx, dy);
                        if (d > 1.0)
                        {
                            const double speed = static_cast<double>(dragons::kGroundSpeed);
                            (void)writer.setMovement(
                                move.mActor, static_cast<float>(dx / d * speed), static_cast<float>(dy / d * speed));
                            mDirectWalkers.insert(move.mActor.reference);
                        }
                    }
                }
                else if (move.mKind == Kind::GoTo)
                    (void)writer.goTo(move.mActor, { move.mX, move.mY, move.mZ, move.mSpeed });
                else if (move.mKind == Kind::Fly)
                {
                    // Task 0253: a dragon in the air is on a pose that the pack moves (the furniture machinery: no fall,
                    // no walk, a looped clip). A new loop is a new useFurniture; every other step only moves it.
                    openeden::sdk::game::FurnitureUse use;
                    use.x = move.mX;
                    use.y = move.mY;
                    use.z = move.mZ;
                    use.heading = move.mHeading;
                    use.pose = openeden::sdk::game::FurniturePose::Sit;
                    use.clip = move.mClip;
                    if (move.mStart)
                    {
                        mDirectWalkers.erase(move.mActor.reference);
                        (void)writer.useFurniture(move.mActor, use);
                    }
                    else if (!writer.placeFurniture(move.mActor, move.mX, move.mY, move.mZ, move.mHeading))
                        (void)writer.useFurniture(move.mActor, use);
                }
                else if (move.mKind == Kind::Land)
                    (void)writer.leaveFurniture(move.mActor, move.mX, move.mY, move.mZ);
            }
        }
        void onGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result) override
        {
            mActors.onGoalFinished(handle, result);
            mCombat.aiGoalFinished(handle, result); // task 0190
        }
        /// Task 0251 (riding): the rules; off (the default) until the game pack turns them on.
        [[nodiscard]] Riding& riding() { return mRiding; }
        [[nodiscard]] bool canMount(openeden::sdk::game::ActorHandle handle) const override
        {
            return mRiding.canMount(handle);
        }
        void onMountRequested(openeden::sdk::game::ActorHandle handle) override { mRiding.onMountRequested(handle); }
        [[nodiscard]] SkyrimActorTable& actorTable() { return mActors; }
        /// Task 0231 (barks): null (the default) is off. The system must outlive the cell source's use of it; the cell
        /// source feeds it the fight's moments (through the combat's bark sink) and, once an update, the roster.
        void setBarks(barks::BarkSystem* system);
        /// Followers (task 0196): answered by the actor table (the package brain keeps who follows).
        [[nodiscard]] std::size_t followers(openeden::sdk::game::WorldId* out, std::size_t capacity) const override
        {
            return mActors.followers(out, capacity);
        }
        [[nodiscard]] bool isFollower(openeden::sdk::game::WorldId reference) const override
        {
            return mActors.isFollower(reference);
        }
        void onFollowerCarried(openeden::sdk::game::WorldId reference) override { mActors.onFollowerCarried(reference); }
        [[nodiscard]] openeden::sdk::game::WorldId homeCell(openeden::sdk::game::WorldId reference) const override
        {
            return mActors.homeCell(reference);
        }
        /// Task 0194 (scenes): the pack's scene runner reaches the actors through these. `sceneSpeak` queues a line for
        /// the core (SceneSpeechSource::takeSpeech) and is false for an actor the core has not told the pack about or
        /// when 64 lines wait; `sceneHold` makes an actor stand still for its lines (the brain's conversation state).
        [[nodiscard]] std::vector<openeden::sdk::game::WorldId> sceneActors() const { return mActors.actorReferences(); }
        [[nodiscard]] bool sceneActorLoaded(openeden::sdk::game::WorldId reference) const { return mActors.has(reference); }
        bool sceneSpeak(openeden::sdk::game::WorldId reference, std::string text, std::vector<std::string> voice)
        {
            const openeden::sdk::game::ActorHandle handle = mActors.handleOf(reference);
            if (!handle.valid() || mSpeech.size() >= kMaxQueuedSpeech)
                return false;
            mSpeech.push_back({ handle, std::move(text), std::move(voice) });
            return true;
        }
        void sceneHold(openeden::sdk::game::WorldId reference, bool on) { mActors.onConversation(reference, on); }
        bool sceneForcePackage(openeden::sdk::game::WorldId reference, const PackageRecord* package)
        {
            return mActors.forcePackage(reference, package);
        }
        [[nodiscard]] bool scenePackageDone(openeden::sdk::game::WorldId reference) const
        {
            return mActors.packageDone(reference);
        }
        bool takeSpeech(openeden::sdk::game::SceneSpeech& out) override
        {
            if (mSpeech.empty())
                return false;
            out = std::move(mSpeech.front());
            mSpeech.erase(mSpeech.begin());
            return true;
        }
        /// Combat hooks (task 0168, stage S5-0): the pack's skeleton, which refuses and counts; nothing calls it yet.
        void onClipEvent(const openeden::sdk::game::ClipEvent& event) override { mCombat.onClipEvent(event); }
        void onSweepResult(openeden::sdk::game::CombatSubject subject, std::uint32_t token,
            const openeden::sdk::game::SweepHit* hits, std::size_t count) override
        {
            mCombat.onSweepResult(subject, token, hits, count);
        }
        void combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer) override
        {
            // Task 0180: the HUD sees the damage and death reports that go through (combat.cpp is not changed) and
            // reads the player's numbers, which are only readable inside this call.
            if (mDeaths != nullptr)
                mDeaths->restoreDeaths(writer, mCombat); // task 0254: the core's own writer: no OnDeath, no crime
            HudWriter hudWriter(writer, mHud);
            AiDamageWriter aiWriter(hudWriter, mCombat); // task 0190: damage of every kind provokes the victim
            ScriptEventWriter scriptWriter(aiWriter, mScriptBridge); // task 0198: OnHit, OnDeath, Actor.Kill
            mScriptBridge.drain(scriptWriter);
            if (mStanceSink)
            {
                // Task 0235: the player's stance (sneaking, drawn weapon), which is only readable inside this call
                openeden::sdk::game::SubjectState playerState;
                if (scriptWriter.subjectState(openeden::sdk::game::CombatSubject::ofPlayer(), playerState))
                    mStanceSink(playerState);
            }
            if (mSneak != nullptr)
                mSneak->update(seconds, scriptWriter); // task 0227: judged before the blows of this update
            if (mCrime != nullptr)
            {
                // Task 0204: the crime rules see every blow and death first (before the victim is provoked), and run
                // once the combat has moved on (their rays and positions only work inside this call).
                crime::CrimeWriter crimeWriter(scriptWriter, *mCrime);
                mCombat.combatUpdate(seconds, crimeWriter);
                mCrime->update(seconds, scriptWriter);
            }
            else
                mCombat.combatUpdate(seconds, scriptWriter);
            if (mDeaths != nullptr)
                mDeaths->noteDeaths(seconds, writer, mCombat); // task 0254
            mHud.update(seconds, writer);
            if (mStyleHud != nullptr)
                mStyleHud->update(seconds, writer); // task 0244: after the HUD's own update
        }
        /// Task 0171: forwarded so that the pack's melee rules see a clip's end and the player's attack key.
        void onClipFinished(
            openeden::sdk::game::CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how) override
        {
            mCombat.onClipFinished(subject, token, how);
        }
        void onPlayerAttack(bool power) override { mCombat.onPlayerAttack(power); }
        /// Task 0178 (stage S5-6): the arrow results and the attack control's presses and releases.
        void onProjectileResult(const openeden::sdk::game::ProjectileResult& result) override
        {
            mCombat.onProjectileResult(result);
        }
        void onPlayerAttackEdge(bool pressed) override { mCombat.onPlayerAttackEdge(pressed); }
        void onPlayerShoutEdge(bool pressed) override { mCombat.onPlayerShoutEdge(pressed); }
        [[nodiscard]] SkyrimCombat& combat() { return mCombat; }
        /// Task 0204: the crime rules (null: off). The system must outlive the cell source's use of it.
        void setCrime(crime::CrimeSystem* system) { mCrime = system; }
        /// Task 0254: who died stays dead across a load (null: off). Must outlive the cell source's use of it.
        void setDeaths(DeathKeeper* keeper) { mDeaths = keeper; }
        /// Task 0227: sneaking and detection (null: off). The system must outlive the cell source's use of it.
        void setSneak(sneak::SneakSystem* system) { mSneak = system; }
        void setStyleHud(hudstyle::StyleHud* hud) { mStyleHud = hud; } // task 0244; not owned
        /// Task 0235: told the player's state once a combat update (empty: off).
        using StanceSink = std::function<void(const openeden::sdk::game::SubjectState&)>;
        void setStanceSink(StanceSink sink) { mStanceSink = std::move(sink); }
        /// Task 0198: where the Papyrus host hears of damage and deaths and queues Actor.Kill (inactive until sinks are set).
        [[nodiscard]] ScriptCombatBridge& scriptBridge() { return mScriptBridge; }
        /// Task 0180: what the combat HUD shows (the script package `core.skyrimse.combat` reads it).
        [[nodiscard]] CombatHud& hud() { return mHud; }
        /// Task 0176: the Papyrus host hears of a built cell and of a conversation that began (the player used an
        /// actor). Main thread, called after the engine's request succeeded; empty functions are not called.
        void setScriptSinks(std::function<void(openeden::sdk::game::WorldId cell)> cellBuilt,
            std::function<void(openeden::sdk::game::WorldId reference)> activated)
        {
            mCellBuilt = std::move(cellBuilt);
            mActivated = std::move(activated);
        }
        /// Task 0253 (a developer aid): in every cell built, each placed actor whose base editor id is `replace` (any case)
        /// gets the base of the NPC_ `place` instead (a dragon where a wolf stood). Both empty: nothing changes.
        void setTestDragon(std::string place, std::string replace)
        {
            mTestDragonPlace = std::move(place);
            mTestDragonReplace = std::move(replace);
        }
        /// Task 0211: the core tells where the player stands when it changes (the story manager's "change location").
        /// Main thread. Empty: nothing is told on.
        void setPlayerPlaceSink(std::function<void(const openeden::sdk::game::PlayerPlace&)> sink)
        {
            mPlaceSink = std::move(sink);
        }
        /// Task 0250: the cell a summon of the player is placed in (an interior's id; none outside, the core finds the
        /// grid square by position).
        [[nodiscard]] openeden::sdk::game::WorldId playerInteriorCell() const
        {
            return mHavePlayerPlace && mPlayerPlace.interior ? mPlayerPlace.cell : openeden::sdk::game::kNoWorldId;
        }
        void onPlayerPlace(const openeden::sdk::game::PlayerPlace& place) override
        {
            mPlayerPlace = place; // task 0238: the effect conditions' IsInInterior and GetInWorldspace
            mHavePlayerPlace = true;
            if (mPlaceSink)
                mPlaceSink(place);
        }
        /// Task 0181: what a script made of a placement's enabled state (nullopt when it never touched it). A cell built
        /// afterwards comes out with it (WorldObject::initiallyDisabled), so Enable and Disable reach cells that were not
        /// loaded when the script ran. Main thread.
        void setEnableOverrideLookup(std::function<std::optional<bool>(openeden::sdk::game::WorldId)> lookup)
        {
            mEnableOverride = std::move(lookup);
        }
        /// Task 0208: the placements the player picked up; a cell built afterwards (a load, a return) comes out with
        /// them disabled, whether or not the Papyrus host is on. Main thread.
        void setTakenLookup(std::function<bool(openeden::sdk::game::WorldId)> lookup) { mTaken = std::move(lookup); }
        /// Task 0221: the harvested plants that are hidden until they grow back; a cell built afterwards (a load, a
        /// return) comes out with them disabled. Main thread.
        void setHarvestedLookup(std::function<bool(openeden::sdk::game::WorldId)> lookup)
        {
            mHarvested = std::move(lookup);
        }
        /// Task 0208: tells the script sink (when the Papyrus host is on) that the player activated a placement.
        void notifyActivated(openeden::sdk::game::WorldId reference)
        {
            if (mActivated)
                mActivated(reference);
        }
        /// Task 0202: where a script moved a placement to (position and/or rotation), asked for each object when a cell
        /// is
        /// built: a cell that was not loaded when MoveTo ran comes out with the object already there. nullopt: not
        /// moved.
        void setMoveOverrideLookup(
            std::function<std::optional<openeden::sdk::game::ObjectMove>(openeden::sdk::game::WorldId)> lookup)
        {
            mMoveOverride = std::move(lookup);
        }
        /// Weather (task 0127, W-S1): forwarded to the view when it can describe weather (SkyrimWorldView does).
        bool describeWeathers(std::vector<openeden::sdk::game::WeatherDescription>& out, std::string& error) override;
        bool describeTimeOfDay(openeden::sdk::game::TimeOfDayDescription& out) override;
        bool weatherListForCell(openeden::sdk::game::WorldId cell, openeden::sdk::game::CellWeatherList& out) override;

        /// Task 0209 (music, M-S7): off unless the pack setting `music` is true (setMusic); the data comes from the
        /// view's MusicRuntime (a worker reads the records) and "the player is in a fight" from the combat rules.
        void setMusic(bool enabled, std::uint64_t seed)
        {
            mMusicOn = enabled;
            mMusicSeed = seed;
        }
        bool musicEnabled() override { return mMusicOn; }
        bool musicReady() override;
        bool musicType(const openeden::sdk::game::MusicContext& context,
            openeden::sdk::game::MusicTypeDescription& out) override;
        bool musicPlan(openeden::sdk::game::WorldId typeId, const openeden::sdk::game::MusicContext& context,
            openeden::sdk::game::MusicPlan& out) override;

    private:
        [[nodiscard]] const SkyrimTerrainIndex* terrain() const;
        openeden::sdk::game::WorldView& mView;
        NeutralLimits mLimits;
        SkyrimActorTable mActors;
        Riding mRiding; ///< task 0251
        static constexpr std::size_t kMaxQueuedSpeech = 64;
        std::vector<openeden::sdk::game::SceneSpeech> mSpeech; ///< task 0194: lines waiting for the core
        void updateBarks(float seconds, openeden::sdk::game::ActorRequestWriter& writer); ///< task 0231
        barks::BarkSystem* mBarks = nullptr;
        std::vector<barks::Actor> mBarkRoster; ///< reused
        SkyrimCombat mCombat;
        bool mMusicOn = false; ///< task 0209
        std::uint64_t mMusicSeed = 1;
        std::vector<SkyrimCombat::AiMoveIntent> mAiMoves; ///< task 0190: drained each update
        std::set<openeden::sdk::game::WorldId> mDirectWalkers; ///< task 0253: dragons walked by setMovement
        CombatHud mHud;
        ScriptCombatBridge mScriptBridge; ///< task 0198
        crime::CrimeSystem* mCrime = nullptr; ///< task 0204
        DeathKeeper* mDeaths = nullptr; ///< task 0254
        sneak::SneakSystem* mSneak = nullptr; ///< task 0227
        hudstyle::StyleHud* mStyleHud = nullptr; ///< task 0244
        StanceSink mStanceSink; ///< task 0235
        std::function<bool(FormId)> mPlayerItems; // task 0184
        std::function<bool(std::uint64_t)> mLockPicked; // task 0203
        bool mLockpickingOn = false;
        std::function<void(openeden::sdk::game::WorldId)> mCellBuilt;
        std::function<void(openeden::sdk::game::WorldId)> mActivated;
        std::function<void(const openeden::sdk::game::PlayerPlace&)> mPlaceSink; ///< task 0211
        openeden::sdk::game::PlayerPlace mPlayerPlace; ///< task 0238: the last place the core told
        bool mHavePlayerPlace = false;
        std::function<std::optional<bool>(openeden::sdk::game::WorldId)> mEnableOverride;
        std::string mTestDragonPlace; ///< task 0253
        std::string mTestDragonReplace;
        std::function<bool(openeden::sdk::game::WorldId)> mTaken; ///< task 0208
        std::function<bool(openeden::sdk::game::WorldId)> mHarvested; ///< task 0221
        /// task 0224
        std::function<std::string(openeden::sdk::game::WorldId, openeden::sdk::game::WorldId)> mPlacementName;
        std::function<std::optional<openeden::sdk::game::ObjectMove>(openeden::sdk::game::WorldId)> mMoveOverride;
        /// Stage S6f: the actor the open conversation is with (0 when none), told to the package brain.
        openeden::sdk::game::WorldId mTalkingTo = openeden::sdk::game::kNoWorldId;
    };
}

#endif

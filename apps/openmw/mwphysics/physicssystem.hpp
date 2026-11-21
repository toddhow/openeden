#ifndef OPENMW_MWPHYSICS_PHYSICSSYSTEM_H
#define OPENMW_MWPHYSICS_PHYSICSSYSTEM_H

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/box.h>
#include <vsg/maths/quat.h>

#include <components/vfs/pathutil.hpp>

#include "../mwworld/ptr.hpp"

#include "joltlisteners.hpp"
#include <openeden/game/actor_rules.hpp>
#include <openeden/game/riding_rules.hpp>
#include <openeden/game/world_view.hpp>

#include "neutralactor.hpp"
#include "neutralactorbridge.hpp"
#include "neutralcombatbridge.hpp"
#include "neutraldoorswing.hpp"
#include "neutralmover.hpp"
#include "neutralpathfollower.hpp"
#include "neutralplanelinks.hpp"
#include "neutralplayerattack.hpp"
#include "neutralplayerplace.hpp"
#include "neutralprocess.hpp"
#include "neutralremovedrefs.hpp"
#include "ragdolltypes.hpp"
#include "raycasting.hpp"

namespace JPH
{
    class JobSystem;
    class TempAllocatorImpl;
    class PhysicsSystem;
    class BodyInterface;
    class BodyLockInterfaceLocking;
}

namespace vsg
{
    class Object;
}

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace Render
{
    class DebugGeometry;
}

namespace MWRender
{
    class JoltDebugDrawer;
}

namespace Resource
{
    class NifFileManager;
    class PhysicsShapeManager;
    class ResourceSystem;
}

namespace MWPhysics
{
    class MWWater;
    class HeightField;
    class Object;
    class Actor;
    class PhysicsTaskScheduler;
    class Projectile;
    class RagdollHost;
    enum ScriptedCollisionType : char;

    using ActorMap = std::unordered_map<const MWWorld::LiveCellRefBase*, std::shared_ptr<Actor>>;

    struct ContactPoint
    {
        MWWorld::Ptr mObject;
        vsg::vec3 mPoint;
        vsg::vec3 mNormal;
    };

    struct LOSRequest
    {
        LOSRequest(const std::weak_ptr<Actor>& a1, const std::weak_ptr<Actor>& a2);
        std::array<std::weak_ptr<Actor>, 2> mActors;
        std::array<const Actor*, 2> mRawActors;
        bool mResult;
        bool mStale;
        int mAge;
    };
    bool operator==(const LOSRequest& lhs, const LOSRequest& rhs) noexcept;

    struct ActorFrameData
    {
        ActorFrameData(Actor& actor, bool inert, bool waterCollision, float slowFall, float waterlevel, bool isPlayer);
        Actor* mActor = nullptr;
        vsg::vec3 mPosition;
        vsg::vec3 mInertia;
        bool mIsOnGround;
        bool mIsOnSlope;
        bool mWalkingOnWater;
        const bool mInert;
        JPH::BodyID mStandingOn;
        JPH::BodyID mPhysicsBody;
        const float mSwimLevel;
        const float mSlowFall;
        vsg::vec2 mRotation;
        vsg::vec3 mMovement;
        vsg::vec3 mLastStuckPosition;
        const float mWaterlevel;
        const float mHalfExtentsZ;
        float mOldHeight;
        unsigned int mStuckFrames;
        const bool mFlying;
        const bool mWasOnGround;
        const bool mIsAquatic;
        const bool mWaterCollision;
        const bool mSkipCollisionDetection;
        const bool mIsPlayer;
        JPH::ObjectLayer mCollisionMask;
        float mStrideShape = 1.f; // Actor::getStrideShape when the frame data was made
    };

    struct WorldFrameData
    {
        WorldFrameData();
        bool mIsInStorm;
        vsg::vec3 mStormDirection;
    };

    template <class Ptr, class FrameData>
    class SimulationImpl
    {
    public:
        explicit SimulationImpl(const std::weak_ptr<Ptr>& ptr, FrameData&& data)
            : mPtr(ptr)
            , mData(data)
        {
        }

        std::optional<std::pair<std::shared_ptr<Ptr>, std::reference_wrapper<FrameData>>> lock()
        {
            if (auto locked = mPtr.lock())
                return { { std::move(locked), std::ref(mData) } };
            return std::nullopt;
        }

    private:
        std::weak_ptr<Ptr> mPtr;
        FrameData mData;
    };

    using ActorSimulation = SimulationImpl<Actor, ActorFrameData>;
    using Simulation = std::variant<ActorSimulation>;

    class PhysicsSystem : public RayCastingInterface
    {
    public:
        PhysicsSystem(Resource::ResourceSystem* resourceSystem, const ToUTF8::StatelessUtf8Encoder* encoder = nullptr);
        virtual ~PhysicsSystem();

        Resource::PhysicsShapeManager* getShapeManager();

        /// Drops collision shapes no longer referenced outside the shape caches.
        void pruneCache();

        void enableWater(float height);
        void setWaterHeight(float height);
        void disableWater();

        void addObject(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, vsg::dquat rotation,
            int collisionType = Layers::WORLD);

        /// Task 0110 (neutral world, stage N3a-4b): adds an object whose collision is the model's own mesh, read
        /// through the shape manager's scene hook (a Skyrim SE model). Returns false, adding nothing, when the model
        /// has no collision shape (no triangles, over the cap, unreadable): the caller then falls back to a box.
        /// Returns true without adding anything while the object's physics is postponed, as addObject does.
        bool addModelObject(
            const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, vsg::dquat rotation, int collisionType);

        /// Task 0109 (neutral world, stage N3a-3): adds an object whose collision is a box (half extents and centre in
        /// the object's own frame), for an object that has bounds and no collision mesh yet. Nothing is added for a
        /// box that cannot be made.
        void addBoxObject(const MWWorld::Ptr& ptr, const vsg::vec3& halfExtents, const vsg::vec3& centre,
            vsg::dquat rotation, int collisionType);
        void addActor(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh);

        /// Task 0135 (decision 0017, stage N5-3): adds the physics body of an actor of a neutral session, an upright
        /// capsule (NeutralActor::capsuleFor(heightScale)) on the WORLD layer, so the player and every ray meet it. It
        /// is an Object, not an Actor: nothing about it needs CreatureStats. `raise` lifts the feet that many units
        /// above the placement first (a developer aid that shows the fall). Main thread only. Does nothing when the
        /// object already has a body or the shape cannot be made.
        void addNeutralActor(const MWWorld::Ptr& ptr, float heightScale, float raise = 0.f, float walkSpeed = 0.f,
            float runSpeed = 0.f, float radius = 0.f);

        /// Task 0135: one frame of the neutral actors: those not yet on the floor are stood on it
        /// (NeutralActor::stepFall, a ray cast down per actor), those that are grounded and have a movement request are
        /// walked (NeutralMover::step: swept capsule, step up, slide, floor snap). Writes each new position and heading
        /// into the object's position and fills getNeutralActorsMoved() / getNeutralActorsTurned(); the caller moves
        /// the drawn object and calls updatePosition for each. Call before stepSimulation, on the main thread. A
        /// grounded actor with no request costs one state test and one request test; neither list allocates once
        /// reserved.
        void updateNeutralActors(float dt);

        /// Task 0157: a door of a neutral interior that neutral actors walk through (the scene decides which:
        /// MWWorld::neutralDoorPtrOpensForActors). From now on it swings open when a walking neutral actor comes
        /// near and closes behind it (NeutralDoor::step). The door must already have a body (addModelObject).
        /// Task 0232: `animated` says the door's model swings by its own Open sequence (the drawn object then stays
        /// where it is and only the collision sinks, once the swing is half done).
        void addNeutralDoor(const MWWorld::Ptr& ptr, bool animated = false);
        /// Task 0232: the player opened this door (a key, or just a press): it counts as approached for
        /// NeutralDoor::kPokeSeconds. Adds the door when it was not one yet.
        void pokeNeutralDoor(const MWWorld::Ptr& ptr, bool animated);
        /// Task 0232: a container's inventory window opens: its lid should lift until the window is gone. One lid at a
        /// time (a newer one replaces the older).
        void openNeutralLid(const MWWorld::Ptr& ptr);
        struct NeutralLid
        {
            MWWorld::Ptr mPtr;
            bool mPendingOpen = false; ///< the world has not told the renderer yet
            bool mSawWindow = false;
            float mAge = 0.f;
        };
        NeutralLid& neutralLid() { return mNeutralLid; }

        /// The doors whose opening changed in the last updateNeutralActors, with the height of the reference the caller
        /// must give them (closed height minus NeutralDoor::sinkOffset): it moves the drawn object, the body and the
        /// navigator's shape.
        struct NeutralDoorMove
        {
            MWWorld::Ptr mPtr;
            float mZ = 0.f;
            /// Task 0232: an animated door. The renderer is given mProgress and mClosing (the drawn object is NOT
            /// moved); the body and the navigator's shape move only when mMoveBody is set.
            bool mAnimated = false;
            bool mMoveBody = true;
            bool mClosing = false;
            float mProgress = 0.f;
        };
        /// Task 0159: the crossings of one-sided walls of a neutral object that the mover can make (see
        /// neutralplanelinks.hpp). Main thread, after the cell's objects have bodies.
        std::vector<NeutralPlaneLinks::Link> findNeutralPlaneLinks(const Object& object) const;

        const std::vector<NeutralDoorMove>& getNeutralDoorsMoved() const { return mNeutralDoorsMoved; }

        /// Task 0135 stage S5: the game clock the pack reads (ActorRequestWriter::clock()) in the next update. Main
        /// thread; call before updateNeutralActors.
        void setNeutralClock(float hour, int day);

        /// Task 0154 (stage S7): where the player stands, for the process levels of `[Game] neutral actor process
        /// levels` (NeutralProcess: actors far from here step their bodies less often). Main thread; call before
        /// updateNeutralActors. Without a call (or with the setting off) every actor is High: stepped every frame.
        void setNeutralFocus(const vsg::vec3& position);

        /// Task 0211: where the player stands (an interior cell or a grid square of a worldspace) for the game pack's
        /// story manager. The pack hears of it through openeden::sdk::game::PlayerPlaceSink on its rules, only when the
        /// place changed (PlayerPlaceFilter). Main thread; call once a frame, before updateNeutralActors.
        void notifyNeutralPlayerPlace(const openeden::sdk::game::PlayerPlace& place);

        /// Task 0154: counters of the last frames for the trace and the cost measurement.
        struct NeutralProcessStats
        {
            std::uint32_t mActors[3] = { 0, 0, 0 }; ///< actors per level in the last frame (grounded ones)
            std::uint64_t mStepped[3] = { 0, 0, 0 }; ///< body steps run per level, since the start
            std::uint64_t mSkipped[3] = { 0, 0, 0 }; ///< frames a grounded actor was not stepped, per level
        };
        [[nodiscard]] const NeutralProcessStats& getNeutralProcessStats() const { return mNeutralProcessStats; }

        /// The actors the last updateNeutralActors changed the position of, and (a subset) those it turned.
        const std::vector<MWWorld::Ptr>& getNeutralActorsMoved() const { return mNeutralMoved; }
        const std::vector<MWWorld::Ptr>& getNeutralActorsTurned() const { return mNeutralTurned; }

        /// Task 0135 stage S6b: furniture use. A change of an actor's furniture pose in the last updateNeutralActors:
        /// an empty clip means "off the furniture" (the renderer goes back to the idle). `mClipPerSex` says the clip is
        /// in the male/female sub-folder. Cleared at the start of every updateNeutralActors.
        struct NeutralPoseChange
        {
            MWWorld::Ptr mPtr;
            std::string mClip;
            bool mClipPerSex = false;
        };
        const std::vector<NeutralPoseChange>& getNeutralPoseChanges() const { return mNeutralPoseChanges; }

        /// Task 0194: lines the game pack's scenes want said, taken from the pack in the last updateNeutralActors
        /// (SceneSpeechSource, found by dynamic_cast on the actor rules). Cleared at the start of every update.
        struct NeutralSpeechLine
        {
            MWWorld::Ptr mPtr;
            std::string mText;
            std::vector<std::string> mVoice;
        };
        const std::vector<NeutralSpeechLine>& getNeutralSpeech() const { return mNeutralSpeech; }
        /// Task 0273b: the keys (reference pointers) of the neutral actors removed since the last call, which the world
        /// uses to drop their scene speech, speaker `Ptr`, mouth driver and corpse rest record. Main thread.
        std::vector<const void*> takeNeutralRemoved() { return mNeutralRemoved.take(); }

        /// Task 0148: the horizontal ground speed (units a second, measured over the last frame) of each neutral actor
        /// whose measured speed changed in the last updateNeutralActors, or that walked in it; a stopped actor is
        /// reported once with 0. The caller hands each to the renderer (locomotion blend). Reserved with the other
        /// lists when an actor is added, so a frame does not allocate.
        struct NeutralSpeedReport
        {
            MWWorld::Ptr mPtr;
            float mSpeed;
        };
        const std::vector<NeutralSpeedReport>& getNeutralActorSpeeds() const { return mNeutralSpeeds; }

        /// Task 0135 stage S2: the movement the pack (stage S3) or a test seam wants of a neutral actor from now on:
        /// a velocity on the ground in units a second. A zero velocity stops it. The mover turns the actor to face the
        /// velocity. Ignored for an actor that has no neutral body. Main thread.
        void setNeutralActorRequest(const MWWorld::ConstPtr& ptr, const NeutralMover::Request& request);

        /// Task 0135 stage S3 (actor hooks, sdk/include/openeden/game/actor_rules.hpp). The resolver finds the game's
        /// ActorRules (null when there is none, or the pack is gone); it is asked each time the bridge calls the pack,
        /// never cached, so a pack that stops cannot be called. Set it before announcing actors.
        using NeutralRulesResolver = std::function<openeden::sdk::game::ActorRules*()>;
        void setNeutralRulesResolver(NeutralRulesResolver resolver) { mNeutralRulesResolver = std::move(resolver); }

        /// Task 0170 (combat stage S5-2): the game's CombatRules (null when it has none or the pack is gone), resolved
        /// on every use like the actor rules. Only used with `[Game] neutral combat`.
        using NeutralCombatResolver = std::function<openeden::sdk::game::CombatRules*()>;
        void setNeutralCombatResolver(NeutralCombatResolver resolver) { mNeutralCombatResolver = std::move(resolver); }
        /// Where the clip meta of an actor comes from (the renderer); null while it is still being read.
        using NeutralClipMetaProvider = std::function<HavokPack::ClipProgress(const MWWorld::Ptr&)>;
        void setNeutralClipMetaProvider(NeutralClipMetaProvider provider);
        /// Task 0183: for repeatable test runs (fixed frame time) only: the same lookup, but it blocks for up to
        /// `waitMilliseconds` for the meta of a clip that has just started, and the combat bridge asks it once per
        /// clip.
        using NeutralClipMetaWaitProvider = std::function<HavokPack::ClipProgress(const MWWorld::Ptr&, int)>;
        void setNeutralClipMetaWaitProvider(NeutralClipMetaWaitProvider provider, int waitMilliseconds);
        /// Task 0171 stage S5-3b: the sidecar meta of a clip for the player's hidden swing clock (null until read).
        using NeutralPlayerClipMetaFn
            = std::function<std::shared_ptr<const HavokPack::ClipMeta>(std::string_view clip, bool perSex)>;
        void setNeutralPlayerClipMeta(NeutralPlayerClipMetaFn provider)
        {
            mNeutralPlayerClipMeta = std::move(provider);
        }
        /// The clips the combat hooks started (a clip name) or stopped (empty) in the last updateNeutralActors; the
        /// caller hands them to the renderer. Reserved with the other lists, so a frame does not allocate.
        struct NeutralClipOp
        {
            MWWorld::Ptr mPtr;
            std::string mClip;
            bool mPerSex = false;
        };
        const std::vector<NeutralClipOp>& getNeutralClipOps() const { return mNeutralClipOps; }
        /// Task 0267: queues a paired clip (or, with an empty name, its end) for the neutral actor behind `key`; the
        /// caller hands it to the renderer with the other clip ops. False when no such actor is drawn.
        bool queueNeutralPairedClip(const void* key, std::string_view clip);
        /// Task 0178 (combat stage S5-6): the clips whose speed the pack set in the last updateNeutralActors (applied
        /// by the caller after the clip ops: a new clip resets its speed).
        struct NeutralClipSpeedOp
        {
            MWWorld::Ptr mPtr;
            float mMultiplier = 1.f;
        };
        const std::vector<NeutralClipSpeedOp>& getNeutralClipSpeedOps() const { return mNeutralClipSpeedOps; }
        /// Task 0193 (combat stage S5-10): the sounds the combat hooks asked for in the last updateNeutralActors, for
        /// the
        /// caller to play through the sound manager. `mPtr` is empty for the player.
        struct NeutralSoundOp
        {
            MWWorld::Ptr mPtr;
            std::string mFile; ///< a VFS path
            std::string mName; ///< the sound descriptor's editor id (for the log)
            float mVolume = 1.f;
        };
        const std::vector<NeutralSoundOp>& getNeutralSoundOps() const { return mNeutralSoundOps; }
        /// Task 0178: the projectiles the combat bridge started, moved or ended in the last updateNeutralActors, for
        /// the
        /// caller to draw (`mId` is the bridge's number).
        struct NeutralProjectileOp
        {
            enum class Kind : std::uint8_t
            {
                Spawn,
                Move,
                Remove,
            };
            Kind mKind = Kind::Move;
            std::uint32_t mId = 0;
            std::string mModel; ///< Spawn only
            NeutralProjectilePose mPose; ///< Spawn and Move
        };
        const std::vector<NeutralProjectileOp>& getNeutralProjectileOps() const { return mNeutralProjectileOps; }
        /// Task 0171 (combat stage S5-3): the player as the combat hooks see it. The world supplies where the player is
        /// and the engine's own health; `damage` writes the health the pack computed back (main thread, called from
        /// inside the pack's combatUpdate). Without these hooks the player is not a target or an attacker.
        struct NeutralPlayerState
        {
            double mX = 0.0;
            double mY = 0.0;
            double mZ = 0.0; ///< the feet
            float mHeading = 0.f;
            float mHealth = 0.f;
            float mHealthMax = 0.f; ///< 0 when the engine has no health for the player
            /// Task 0172 (stage S5-4): the attack action is down, and the controls take it (no menu, not disabled).
            bool mAttackHeld = false;
            bool mAttackEnabled = false;
            /// Task 0205: the shout action is down (the same controls-enabled test as the attack).
            bool mShoutHeld = false;
            /// Task 0178: the look direction's elevation, radians, positive up (the bow's launch pitch).
            float mPitch = 0.f;
            /// Task 0227: the player's Stance_Sneak.
            bool mSneaking = false;
            /// Task 0235: the player's draw state (0 nothing, 1 weapon, 2 spell).
            std::uint8_t mDrawState = 0;
        };
        using NeutralPlayerStateFn = std::function<bool(NeutralPlayerState&)>;
        /// Task 0206: also the report's maximum (0 when it names none), so that a raised maximum reaches the
        /// engine's stat.
        using NeutralPlayerDamageFn = std::function<void(float healthAfter, float healthMax)>;
        void setNeutralPlayerHooks(NeutralPlayerStateFn state, NeutralPlayerDamageFn damage)
        {
            mNeutralPlayerState = std::move(state);
            mNeutralPlayerDamage = std::move(damage);
        }
        [[nodiscard]] const NeutralCombatBridge::Stats& neutralCombatStats() const { return mNeutralCombat.stats(); }
        /// Task 0251 (riding): the world gives the bridge's RidingWriter its hooks (the player's controls, carrying and
        /// releasing the player). The hooks are kept here, so the bridge's pointer stays valid.
        void setNeutralRidingHooks(NeutralRidingHooks hooks)
        {
            mNeutralRidingHooks = std::move(hooks);
            mNeutralBridge.setRidingHooks(&mNeutralRidingHooks);
        }
        /// Task 0251: whether the pack offers the player to mount this actor (or to get off the horse he sits on).
        [[nodiscard]] bool neutralCanMount(const MWWorld::Ptr& ptr) const;
        /// Task 0251: the player activated the mount: tells the pack, which acts in its next update.
        void requestNeutralMount(const MWWorld::Ptr& ptr);
        /// Task 0255: a load door is about to take the player: a rider gets off now (the pack releases him). A no-op
        /// for a player on foot or a session with no riding rules.
        void dismountNeutralRider();
        /// Task 0275: is static geometry (walls, floors, terrain, closed doors) between two points? The neutral actors'
        /// bodies never block. Used by the activate key's actor pick: an actor behind a low counter is picked when its
        /// head is in clear view.
        [[nodiscard]] bool neutralLineBlocked(const vsg::vec3& from, const vsg::vec3& to);
        /// Task 0184: the pack reported this neutral actor dead (a corpse the player may search).
        [[nodiscard]] bool isNeutralActorDead(const MWWorld::Ptr& ptr) const
        {
            return mNeutralCombat.isDeadByKey(ptr.mRef);
        }

        /// Tells the pack about a neutral actor that already has a body (addNeutralActor): `reference` is the
        /// placement id and `base` the base id in the pack's terms. Does nothing without a body, a resolver or rules.
        void announceNeutralActor(const MWWorld::Ptr& ptr, openeden::sdk::game::WorldId reference,
            openeden::sdk::game::WorldId base, std::string_view editorId);

        /// Task 0135 stage S4: how a go-to request gets its path. Called on the main thread when an actor asks for a
        /// path (at a new goal, and again after a stall), with the capsule of the actor, and appends waypoints to the
        /// vector. The World sets it to the navigator; without one a go-to ends as NoPath.
        using NeutralPathFinderFn = std::function<NeutralPath::FindStatus(const vsg::vec3& from, const vsg::vec3& to,
            const NeutralActor::Capsule& capsule, std::vector<vsg::vec3>& out)>;
        void setNeutralPathFinder(NeutralPathFinderFn finder) { mNeutralPathFinder = std::move(finder); }

        /// Task 0291 (swimming): the water surface under a neutral actor's position (x, y), from the cell the Ptr
        /// belongs to and the grid square the position is in (a neutral exterior has one height per square). The World
        /// sets it; without one an actor never swims. Main thread.
        using NeutralWaterLookupFn = std::function<std::optional<float>(const MWWorld::CellStore&, float x, float y)>;
        void setNeutralWaterLookup(NeutralWaterLookupFn lookup) { mNeutralWaterLookup = std::move(lookup); }

        [[nodiscard]] const NeutralActorBridge::Stats& getNeutralBridgeStats() const { return mNeutralBridge.stats(); }

        /// The fall state of a neutral actor, null if `ptr` has none. For traces and tests.
        const NeutralActor::Fall* getNeutralActorFall(const MWWorld::ConstPtr& ptr) const;

        int addProjectile(
            const MWWorld::Ptr& caster, const vsg::vec3& position, VFS::Path::NormalizedView mesh, bool computeRadius);
        void setCaster(int projectileId, const MWWorld::Ptr& caster);
        void removeProjectile(const int projectileId);

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated);

        /// The one Jolt world. The extension host gives extensions read-only queries on it; nothing else should need
        /// it.
        JPH::PhysicsSystem& getJoltWorld() { return mPhysicsSystem; }

        /// Task 0035 stage b, the cmRagdoll debug console command. Main thread. Returns false and a message when the
        /// setting `[Physics] creativemotion ragdolls` is off or the ragdoll cannot be built.
        bool spawnDebugRagdoll(const MWWorld::ConstPtr& ptr, std::string& message);
        void removeDebugRagdoll();

        /// Task 0106. A dead NPC becomes a CreativeMotion ragdoll in the one Jolt world. All main thread.
        /// Builds one at the actor's position and yaw, moving as the actor was. False and a message when the setting
        /// is off, the actor has no physics body, it already has one, or the host refuses (the table is full).
        bool startActorRagdoll(const MWWorld::Ptr& ptr, ActorRagdollHandle& handle, std::string& message,
            const ActorStartPose* pose = nullptr, const double* velocityUnits = nullptr);
        /// The ragdoll's bones in game units, in actorRagdollBones() order; false for a stale handle.
        bool readActorRagdoll(ActorRagdollHandle handle, std::span<RagdollBoneState> out) const;
        bool hasActorRagdoll(ActorRagdollHandle handle) const;
        /// Task 0106, second slice (3). True when every body of the actor's ragdoll is asleep; then pelvis holds the
        /// pelvis bone's position (game units). False for a stale handle or while anything still moves.
        bool actorRagdollAtRest(ActorRagdollHandle handle, double pelvis[3]) const;
        /// The first static surface (world, ground, door) straight below `from`, at most maxDrop units down; false if
        /// there is none. Main thread.
        bool groundBelow(const vsg::vec3& from, float maxDrop, float& groundZ) const;
        /// Removes the actor's ragdoll if it has one. Called when the actor leaves the physics system.
        void stopActorRagdoll(const MWWorld::Ptr& ptr);
        /// Task 0263. True once for a neutral actor the pack put back as a saved corpse
        /// (ActorRequestWriter::placeCorpse), then false until it is put back again. Main thread.
        bool takeRestoredCorpse(const MWWorld::Ptr& ptr);
        /// The rig bone each ragdoll body follows; empty until the first ragdoll was built.
        std::span<const std::string> actorRagdollBones() const;
        std::size_t actorRagdollCount() const;

        /// Task 0174 (S5-5). The neutral actors the pack reported dead since the last updateNeutralActors, when
        /// `[Physics] creativemotion ragdolls` is on (valid until the next call of it). Main thread.
        const std::vector<MWWorld::Ptr>& getNeutralDeaths() const { return mNeutralDeaths; }
        /// Turns a dead neutral actor into a ragdoll: its capsule leaves every layer the ragdoll meets, the ragdoll is
        /// built in `pose` (null: the rig's standing rest pose) at the actor's place and yaw, at rest. False with a
        /// message when the setting is off, the actor is not a neutral actor, it has one already or the host refuses.
        bool startNeutralActorRagdoll(
            const MWWorld::Ptr& ptr, ActorRagdollHandle& handle, std::string& message, const ActorStartPose* pose);
        /// Calls fn(ptr, handle, alive) for every neutral actor that has a ragdoll; one whose ragdoll the host has
        /// removed (it fell out of the world) is told once with alive false and forgotten.
        void forEachNeutralRagdoll(const std::function<void(const MWWorld::Ptr&, ActorRagdollHandle, bool)>& fn);

        /// The box around every body of the Jolt world (task 0028: the light probe baker sizes its volume with it).
        void getWorldBounds(vsg::vec3& low, vsg::vec3& high) const;

        /// Null stops it. It must stay alive until then.
        void setContactObserver(ContactObserver* observer) { mContactListener.setObserver(observer); }

        Actor* getActor(const MWWorld::Ptr& ptr);
        const Actor* getActor(const MWWorld::ConstPtr& ptr) const;

        const Object* getObject(const MWWorld::ConstPtr& ptr) const;

        Projectile* getProjectile(int projectileId) const;

        // Object or Actor
        void remove(const MWWorld::Ptr& ptr);

        void updateScale(const MWWorld::Ptr& ptr);
        void updateRotation(const MWWorld::Ptr& ptr, vsg::dquat rotate);
        void updatePosition(const MWWorld::Ptr& ptr);
        /// Task 0274: a game pack moves this object every frame (a cart trailing its horse): a mount's sweeps leave
        /// its body out, or the cart blocks the horse that pulls it in a bend.
        /// Task 0336: a neutral object a walking neutral actor's capsule passes through (the objects the navigator
        /// leaves out; see `MWWorld::neutralActorWalksThrough`). Main thread, with the object's insertion.
        void noteNeutralWalkThrough(
            const MWWorld::Ptr& ptr, openeden::sdk::game::BaseKind kind, bool standInBox, bool interior) const;
        [[nodiscard]] std::size_t neutralWalkThroughCount() const { return mNeutralWalkThrough.size(); }
        void noteMovedByPack(const MWWorld::Ptr& ptr) { mNeutralMovable.insert(ptr.mRef); }

        void addHeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            const vsg::Object* holdObject);

        void removeHeightField(int x, int y);

        const HeightField* getHeightField(int x, int y) const;

        bool toggleCollisionMode();
        /// Task 0251 (riding): sets the player's collision mode (off: no collision, no gravity, as after `tcl`) and
        /// reports it.
        void setPlayerCollisionMode(bool on);
        [[nodiscard]] bool playerCollisionMode() const;

        /// Determine new position based on all queued movements, then clear the list.
        void stepSimulation(float dt, bool skipSimulation);

        /// Apply new positions to actors
        void moveActors();
        void debugDraw();

        std::vector<MWWorld::Ptr> getCollisions(const MWWorld::ConstPtr& ptr, int collisionGroup,
            int collisionMask) const; ///< get handles this object collides with
        std::vector<ContactPoint> getCollisionsPoints(
            const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const;
        vsg::vec3 traceDown(const MWWorld::Ptr& ptr, const vsg::vec3& position, float maxHeight);
        void optimize();

        /// @param ignore Optional, a list of Ptr to ignore in the list of results. targets are actors to filter for,
        /// ignoring all other actors.
        RayCastingResult castRay(const vsg::vec3& from, const vsg::vec3& to,
            const std::vector<MWWorld::ConstPtr>& ignore = {}, const std::vector<MWWorld::Ptr>& targets = {},
            int mask = CollisionMask_Default, int group = 0xff) const override;
        using RayCastingInterface::castRay;

        RayCastingResult castSphere(const vsg::vec3& from, const vsg::vec3& to, float radius,
            int mask = CollisionMask_Default, int group = 0xff) const override;

        /// Return true if actor1 can see actor2.
        bool getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const override;

        bool isOnGround(const MWWorld::Ptr& actor);

        bool canMoveToWaterSurface(const MWWorld::ConstPtr& actor, const float waterlevel);

        /// Get physical half extents (scaled) of the given actor.
        vsg::vec3 getHalfExtents(const MWWorld::ConstPtr& actor) const;

        /// Get physical half extents (not scaled) of the given actor.
        vsg::vec3 getOriginalHalfExtents(const MWWorld::ConstPtr& actor) const;

        /// @see MWPhysics::Actor::getRenderingHalfExtents
        vsg::vec3 getRenderingHalfExtents(const MWWorld::ConstPtr& actor) const;

        /// Get the position of the collision shape for the actor. Use together with getHalfExtents() to get the
        /// collision bounds in world space.
        /// @note The collision shape's origin is in its center, so the position returned can be described as center of
        /// the actor collision box in world space.
        vsg::vec3 getCollisionObjectPosition(const MWWorld::ConstPtr& actor) const;

        /// Get bounding box in world space of the given object.
        vsg::dbox getBoundingBox(const MWWorld::ConstPtr& object) const;

        /// Queues velocity movement for a Ptr. If a Ptr is already queued, its velocity will
        /// be overwritten. Valid until the next call to stepSimulation
        void queueObjectMovement(const MWWorld::Ptr& ptr, const vsg::vec3& velocity);

        /// Force a collision-shape animation update for a single object.
        void updateAnimatedCollisionShape(const MWWorld::Ptr& ptr);

        /// Clear the queued movements list without applying.
        void clearQueuedMovement();

        /// Return true if \a actor has been standing on \a object in this frame
        /// This will trigger whenever the object is directly below the actor.
        /// It doesn't matter if the actor is stationary or moving.
        bool isActorStandingOn(const MWWorld::Ptr& actor, const MWWorld::ConstPtr& object) const;

        /// Get the handle of all actors standing on \a object in this frame.
        void getActorsStandingOn(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const;

        /// Return true if an object of the given type has collided with this object
        bool isObjectCollidingWith(const MWWorld::ConstPtr& object, ScriptedCollisionType type) const;

        /// Get the handle of all actors colliding with \a object in this frame.
        void getActorsCollidingWith(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const;

        bool toggleDebugRendering();

        /// Connects the renderer's debug geometry (owned by MWRender::RenderManager, which outlives this) for `tcb`.
        void setDebugGeometry(Render::DebugGeometry& geometry);

        void reportCollision(const vsg::vec3& position, const vsg::vec3& normal);

        /// Mark the given object as a 'non-solid' object. A non-solid object means that
        /// \a isOnSolidGround will return false for actors standing on that object.
        void markAsNonSolid(const MWWorld::ConstPtr& ptr);

        bool isOnSolidGround(const MWWorld::Ptr& actor) const;

        template <class Function>
        void forEachAnimatedObject(Function&& function) const
        {
            std::for_each(mAnimatedObjects.begin(), mAnimatedObjects.end(), function);
        }

        bool isAreaOccupiedByOtherActor(const vsg::vec3& position, const float radius,
            std::span<const MWWorld::ConstPtr> ignore, std::vector<MWWorld::Ptr>* occupyingActors) const;

        float getPhysicsDt() const { return mPhysicsDt; }

        inline const JPH::BodyLockInterfaceLocking& getBodyLockInterface() const;

        const JPH::BodyInterface& getBodyInterface() const;

    private:
        void updateWater();
        void updatePtrHolders();

        void prepareSimulation(bool willSimulate, std::vector<Simulation>& simulations);

        JoltContactListener mContactListener;
        JoltBPLayerInterface mBPLayerInterface;
        JoltObjectVsBroadPhaseLayerFilter mObjectVsBPLayerFilter;
        JoltObjectLayerPairFilter mObjectVsObjectLayerFilter;

        JPH::PhysicsSystem mPhysicsSystem;
        std::unique_ptr<PhysicsTaskScheduler> mTaskScheduler;
        std::unique_ptr<JPH::TempAllocatorImpl> mMemoryAllocator;
        std::unique_ptr<JPH::JobSystem> mPhysicsJobSystem;
        // Created by the first successful spawnDebugRagdoll, never when the setting is off. Declared after the system
        // and the job system so that it goes first; ~PhysicsSystem also resets it before anything else.
        std::unique_ptr<RagdollHost> mRagdollHost;
        // Task 0106: the ragdoll each dead actor has, keyed like mActors.
        std::unordered_map<const MWWorld::LiveCellRefBase*, ActorRagdollHandle> mActorRagdolls;
        // openeden-vsg: physics owns its NIF cache, since vsgopenmw's ResourceSystem has no NIF manager.
        std::unique_ptr<Resource::NifFileManager> mNifFileManager;
        std::unique_ptr<Resource::PhysicsShapeManager> mShapeManager;
        Resource::ResourceSystem* mResourceSystem;

        using ObjectMap = std::unordered_map<const MWWorld::LiveCellRefBase*, std::shared_ptr<Object>>;
        ObjectMap mObjects;

        std::map<Object*, bool> mAnimatedObjects; // stores pointers to elements in mObjects

        ActorMap mActors;

        // Task 0135: the neutral actors' fall states, in the order they were added (the order never matters to the
        // result: every actor's ray ignores all of them). Main thread only.
        struct NeutralActorEntry
        {
            const MWWorld::LiveCellRefBase* mRef;
            NeutralActor::Fall mFall;
            float mStartZ;
            NeutralMover::Request mRequest;
            std::uint32_t mWalkFrames = 0; ///< frames this actor was walked, for the trace
            NeutralActor::Capsule mCapsule; ///< for the navigator's agent size (stage S4)
            NeutralPath::Follower mFollow; ///< the go-to request, if any (stage S4)
            float mMeasuredSpeed = 0.f; ///< ground speed over the last walk step (task 0148), 0 when it did not move
            float mReportedSpeed = 0.f; ///< what getNeutralActorSpeeds last told
            float mFace = 0.f; ///< a heading request, applied once when the actor stands
            bool mFaceValid = false;
            bool mSeated = false; ///< on furniture (stage S6b): no fall, no walk, no goal until it leaves
            NeutralProcess::Level mLevel = NeutralProcess::Level::High; ///< task 0154
            NeutralProcess::Schedule mSchedule;
            bool mScheduleSeeded = false;
            // Task 0174 (S5-5): the pack reported it dead and the ragdoll host holds its ragdoll. A handle that is
            // valid keeps the fall and walk code away from the entry; the corpse is moved to where it lies once.
            ActorRagdollHandle mRagdoll;
            bool mCorpseDone = false;
            std::uint32_t mCorpseFrames = 0;
            bool mRestoredCorpse = false; ///< task 0263: the pack put a saved corpse back (placeCorpse), until taken
            float mWalkSpeed = 0.f; ///< task 0212: a creature's own walk and run speeds, 0 for the core's
            float mRunSpeed = 0.f;
            bool mSwimming = false; ///< task 0291: it landed in deep water and floats at the surface
        };
        NeutralProcess::Config mNeutralProcessConfig;
        NeutralProcessStats mNeutralProcessStats;
        vsg::vec3 mNeutralFocus;
        bool mNeutralFocusValid = false;
        PlayerPlaceFilter mNeutralPlaceFilter; ///< task 0211
        std::uint64_t mNeutralCostFrames = 0; ///< OPENEDEN_NEUTRAL_ACTOR_COST: frames, nanoseconds in the whole update
        std::uint64_t mNeutralCostNs = 0;
        std::uint64_t mNeutralCostPackNs = 0;
        std::uint64_t mNeutralCostFollowNs = 0; ///< inside followNeutralGoal (the path finder is called from there)
        std::uint64_t mNeutralCostWalkNs = 0; ///< inside walkNeutralActor (swept capsule)
        std::uint64_t mNeutralCostActorFrames = 0;
        // Task 0192: the combat bridge's whole update, the pack's combatUpdate inside it, and the longest frame of
        // each in
        // the window (all only with OPENEDEN_NEUTRAL_ACTOR_COST).
        std::uint64_t mNeutralCostCombatNs = 0;
        std::uint64_t mNeutralCostCombatMaxNs = 0;
        std::uint64_t mNeutralCostRulesNs = 0;
        std::uint64_t mNeutralCostRulesMaxNs = 0;
        std::uint64_t mNeutralCostRulesPrevNs = 0; ///< the bridge's running total at the last frame
        std::vector<NeutralActorEntry> mNeutralActors;
        std::set<const MWWorld::LiveCellRefBase*> mNeutralMovable; ///< task 0274: objects a game pack moves (carts)
        mutable std::unordered_set<std::uint32_t>
            mNeutralWalkThrough; ///< task 0336: bodies (index and sequence) a walker passes
        mutable std::unordered_map<const MWWorld::LiveCellRefBase*, std::uint32_t>
            mNeutralWalkThroughOf; ///< and whose they are
        std::vector<JPH::BodyID> mNeutralSweepIgnore; ///< task 0274: scratch, the bodies a mount's sweeps ignore
        // Task 0157: doors that open for walking neutral actors.
        struct NeutralDoorEntry
        {
            const MWWorld::LiveCellRefBase* mRef;
            float mClosedZ = 0.f; ///< the reference's height while closed
            float mCentreX = 0.f, mCentreY = 0.f, mCentreZ = 0.f; ///< world centre of the door's bounds while closed
            NeutralDoor::State mState;
            float mPublishedProgress = 0.f;
            bool mAnimated = false; ///< task 0232
            bool mCollisionOpen = false;
            bool mClosing = false;
            float mPokeLeft = 0.f;
        };
        NeutralLid mNeutralLid;
        std::vector<NeutralDoorEntry> mNeutralDoors;
        std::vector<NeutralDoorMove> mNeutralDoorsMoved;
        void updateNeutralDoors(float dt);
        std::vector<NeutralPoseChange> mNeutralPoseChanges;
        std::vector<NeutralSpeechLine> mNeutralSpeech;
        NeutralRemovedRefs mNeutralRemoved; // task 0273b
        // Stage S3: the pack's hooks. The sink turns a validated request into the entry's request.
        class NeutralRequestApplier final : public NeutralRequestSink
        {
        public:
            explicit NeutralRequestApplier(PhysicsSystem& physics)
                : mPhysics(physics)
            {
            }
            void setRequest(const void* key, float velX, float velY) override;
            void setGoal(const void* key, const openeden::sdk::game::ActorGoal& goal) override;
            void cancelGoal(const void* key) override;
            void setHeading(const void* key, float heading) override;
            void useFurniture(const void* key, const openeden::sdk::game::FurnitureUse& use) override;
            void leaveFurniture(const void* key, double x, double y, double z) override;
            bool placeSeated(const void* key, double x, double y, double z, float heading) override;
            bool placeCorpse(const void* key, double x, double y, double z, float heading) override;
            bool placeLiving(const void* key, double x, double y, double z, float heading) override;
            bool positionOf(const void* key, double& x, double& y, double& z) const override;

        private:
            PhysicsSystem& mPhysics;
        };
        NeutralRulesResolver mNeutralRulesResolver;
        NeutralActorBridge mNeutralBridge;
        NeutralRidingHooks mNeutralRidingHooks; // task 0251
        openeden::sdk::game::ActorHandle mNeutralRidden; // task 0255: the horse the player sits on, set once a frame
        const openeden::sdk::game::RidingRules* mNeutralRidingRules = nullptr; // task 0274: set once a frame
        int mNeutralRiddenFrames = 0; // task 0255: frames of the ride, for the developer aid above
        // Task 0170: the combat hooks' clips. The sink turns a clip request into an op for the renderer.
        class NeutralClipApplier final : public NeutralClipSink
        {
        public:
            explicit NeutralClipApplier(PhysicsSystem& physics)
                : mPhysics(physics)
            {
            }
            void playClip(const void* key, std::string_view clip, bool perSex) override;
            void stopClip(const void* key) override;
            void setHeading(const void* key, float heading) override;
            void setClipSpeed(const void* key, float multiplier) override;
            void playSound(const void* key, std::string_view file, std::string_view name, float volume) override;
            void spawnProjectile(std::uint32_t id, std::string_view model, const NeutralProjectilePose& pose) override;
            void moveProjectile(std::uint32_t id, const NeutralProjectilePose& pose) override;
            void removeProjectile(std::uint32_t id) override;

        private:
            PhysicsSystem& mPhysics;
        };
        // Task 0171: what the combat bridge needs from this world (sweep bodies, positions, the player's health).
        class NeutralCombatWorldImpl final : public NeutralCombatWorld
        {
        public:
            explicit NeutralCombatWorldImpl(PhysicsSystem& physics)
                : mPhysics(physics)
            {
            }
            bool subjectState(
                openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out) override;
            void collectCandidates(std::vector<NeutralSweep::Candidate>& out) override;
            bool lineBlocked(double x0, double y0, double z0, double x1, double y1, double z1) override;
            bool rayWorld(
                const NeutralProjectile::Vec& p0, const NeutralProjectile::Vec& p1, double& fraction) override;
            std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view clip, bool perSex) override;
            void applyPlayerDamage(const openeden::sdk::game::DamageReport& report) override;
            void actorDied(const void* key, openeden::sdk::game::ActorHandle actor) override;

        private:
            PhysicsSystem& mPhysics;
        };
        NeutralPlayerStateFn mNeutralPlayerState;
        NeutralPlayerAttackInput mNeutralPlayerAttack; ///< task 0172: the player's attack key as the pack wants it
        NeutralPlayerClipMetaFn mNeutralPlayerClipMeta;
        std::vector<JPH::BodyID> mNeutralSightIgnore; ///< the neutral actors' bodies, refilled once a frame
        std::uint64_t mNeutralFrame = 0; ///< counts updateNeutralActors
        std::uint64_t mNeutralSightIgnoreFrame = ~std::uint64_t{ 0 }; ///< the frame mNeutralSightIgnore was filled for
        NeutralPlayerDamageFn mNeutralPlayerDamage;
        NeutralCombatResolver mNeutralCombatResolver;
        NeutralClipMetaFn mNeutralClipMeta;
        NeutralCombatBridge mNeutralCombat{ mNeutralBridge };
        NeutralClipApplier mNeutralClipApplier{ *this };
        NeutralCombatWorldImpl mNeutralCombatWorld{ *this };
        std::vector<NeutralClipOp> mNeutralClipOps;
        std::vector<NeutralClipSpeedOp> mNeutralClipSpeedOps; // task 0178
        std::vector<NeutralSoundOp> mNeutralSoundOps; // task 0193
        std::vector<NeutralProjectileOp> mNeutralProjectileOps; // task 0178
        bool mNeutralShoutHeldPrev = false; // task 0205: the shout control as the pack last heard it
        bool mNeutralPlayerHeldPrev = false; // task 0178: the attack control as the pack last heard it
        const std::vector<JPH::BodyID>& neutralSightIgnore(); // the neutral actors' bodies, refilled once a frame
        NeutralRequestApplier mNeutralApplier{ *this };
        std::string describeNeutralRefusal(const NeutralMover::Result& result) const; ///< task 0336, trace only
        void walkNeutralActor(NeutralActorEntry& entry, Object& object, float dt, bool trace);
        void followNeutralGoal(NeutralActorEntry& entry, const MWWorld::Ptr& ptr, float dt, bool trace);
        NeutralActorEntry* findNeutralActor(const void* key);
        void restNeutralCorpse(NeutralActorEntry& entry, const MWWorld::Ptr& ptr);
        NeutralPathFinderFn mNeutralPathFinder;
        NeutralWaterLookupFn mNeutralWaterLookup;
        std::vector<MWWorld::Ptr> mNeutralMoved; // reserved when an actor is added, so a frame does not allocate
        std::vector<MWWorld::Ptr> mNeutralTurned;
        std::vector<MWWorld::Ptr> mNeutralDeaths; // task 0174: reported dead since the last updateNeutralActors
        std::vector<NeutralSpeedReport> mNeutralSpeeds;

        using ProjectileMap = std::map<int, std::shared_ptr<Projectile>>;
        ProjectileMap mProjectiles;

        using HeightFieldMap = std::map<std::pair<int, int>, std::unique_ptr<HeightField>>;
        HeightFieldMap mHeightFields;

        bool mDebugDrawEnabled;

        float mTimeAccum;

        int mProjectileId;

        float mTimeAccumJolt = 0.0f;
        float mWaterHeight;
        bool mWaterEnabled;

        std::unique_ptr<MWWater> mWaterInstance;

        // Null until setDebugGeometry() connects the renderer's debug geometry.
        std::unique_ptr<MWRender::JoltDebugDrawer> mJoltDebugDrawer;
        MWRender::JoltDebugDrawer* getJoltDebugDrawer() const { return mJoltDebugDrawer.get(); }

        float mPhysicsDt;

        std::size_t mSimulationsCounter = 0;
        std::array<std::vector<Simulation>, 2> mSimulations;
        std::vector<std::pair<MWWorld::Ptr, vsg::vec3>> mActorsPositions;

        PhysicsSystem(const PhysicsSystem&);
        PhysicsSystem& operator=(const PhysicsSystem&);
    };
}

#endif

#ifndef OPENMW_MWPHYSICS_PHYSICSSYSTEM_H
#define OPENMW_MWPHYSICS_PHYSICSSYSTEM_H

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
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

#include "neutralactor.hpp"
#include "neutralactorbridge.hpp"
#include "neutralmover.hpp"
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
        void addNeutralActor(const MWWorld::Ptr& ptr, float heightScale, float raise = 0.f);

        /// Task 0135: one frame of the neutral actors: those not yet on the floor are stood on it (NeutralActor::stepFall,
        /// a ray cast down per actor), those that are grounded and have a movement request are walked
        /// (NeutralMover::step: swept capsule, step up, slide, floor snap). Writes each new position and heading into
        /// the object's position and fills getNeutralActorsMoved() / getNeutralActorsTurned(); the caller moves the
        /// drawn object and calls updatePosition for each. Call before stepSimulation, on the main thread. A grounded
        /// actor with no request costs one state test and one request test; neither list allocates once reserved.
        void updateNeutralActors(float dt);

        /// The actors the last updateNeutralActors changed the position of, and (a subset) those it turned.
        const std::vector<MWWorld::Ptr>& getNeutralActorsMoved() const { return mNeutralMoved; }
        const std::vector<MWWorld::Ptr>& getNeutralActorsTurned() const { return mNeutralTurned; }

        /// Task 0135 stage S2: the movement the pack (stage S3) or a test seam wants of a neutral actor from now on:
        /// a velocity on the ground in units a second. A zero velocity stops it. The mover turns the actor to face the
        /// velocity. Ignored for an actor that has no neutral body. Main thread.
        void setNeutralActorRequest(const MWWorld::ConstPtr& ptr, const NeutralMover::Request& request);

        /// Task 0135 stage S3 (actor hooks, sdk/include/openeden/game/actor_rules.hpp). The resolver finds the game's
        /// ActorRules (null when there is none, or the pack is gone); it is asked each time the bridge calls the pack,
        /// never cached, so a pack that stops cannot be called. Set it before announcing actors.
        using NeutralRulesResolver = std::function<openeden::sdk::game::ActorRules*()>;
        void setNeutralRulesResolver(NeutralRulesResolver resolver) { mNeutralRulesResolver = std::move(resolver); }

        /// Tells the pack about a neutral actor that already has a body (addNeutralActor): `reference` is the
        /// placement id and `base` the base id in the pack's terms. Does nothing without a body, a resolver or rules.
        void announceNeutralActor(const MWWorld::Ptr& ptr, openeden::sdk::game::WorldId reference,
            openeden::sdk::game::WorldId base, std::string_view editorId);

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
            const ActorStartPose* pose = nullptr,
            const double* velocityUnits = nullptr);
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
        /// The rig bone each ragdoll body follows; empty until the first ragdoll was built.
        std::span<const std::string> actorRagdollBones() const;
        std::size_t actorRagdollCount() const;

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

        void addHeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            const vsg::Object* holdObject);

        void removeHeightField(int x, int y);

        const HeightField* getHeightField(int x, int y) const;

        bool toggleCollisionMode();

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
        };
        std::vector<NeutralActorEntry> mNeutralActors;
        // Stage S3: the pack's hooks. The sink turns a validated request into the entry's request.
        class NeutralRequestApplier final : public NeutralRequestSink
        {
        public:
            explicit NeutralRequestApplier(PhysicsSystem& physics)
                : mPhysics(physics)
            {
            }
            void setRequest(const void* key, float velX, float velY) override;

        private:
            PhysicsSystem& mPhysics;
        };
        NeutralRulesResolver mNeutralRulesResolver;
        NeutralActorBridge mNeutralBridge;
        NeutralRequestApplier mNeutralApplier{ *this };
        void walkNeutralActor(NeutralActorEntry& entry, Object& object, float dt, bool trace);
        std::vector<MWWorld::Ptr> mNeutralMoved; // reserved when an actor is added, so a frame does not allocate
        std::vector<MWWorld::Ptr> mNeutralTurned;

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

#ifndef OPENEDEN_APPS_OPENMW_PLUGINWORLD_H
#define OPENEDEN_APPS_OPENMW_PLUGINWORLD_H

#include <components/plugin/entity_manager.hpp>
#include <components/plugin/host_impl.hpp>
#include <components/plugin/physics_service.hpp>

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mwphysics/joltlisteners.hpp"
#include "mwworld/ptr.hpp"

namespace MWPhysics
{
    class PhysicsSystem;
}

namespace OMW
{
    /**
     * @brief Tells plugins what happens in the game's world (task 0017). Owned by PluginHost, only with plugins loaded.
     *
     * Engine code calls the notification functions from whichever thread it runs on: cells load on a worker, contacts
     * arrive on physics threads. They only queue, under a lock. deliver(), on the main thread at the start of
     * Phase::Input, gives plugins everything queued, in order. So plugin code never runs on another thread, and
     * entity IDs and the tables below are touched only on the main thread.
     */
    class PluginWorld final : public MWPhysics::ContactObserver,
                              public openeden::plugin::EntityBodies,
                              public openeden::plugin::EntityWorld
    {
    public:
        /// physics may be null, in tests: objects then have no body.
        PluginWorld(openeden::plugin::HostImpl& host, MWPhysics::PhysicsSystem* physics);

        void objectAdded(const MWWorld::Ptr& ptr);
        void objectRemoved(const MWWorld::Ptr& ptr);
        void cellLoaded(std::string_view name);
        void cellUnloaded(std::string_view name);
        void cellChanged(std::string_view name);
        void actorDied(const MWWorld::Ptr& actor);
        void actorRevived(const MWWorld::Ptr& actor);
        void actorHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful);
        void gameLoaded(const MWWorld::Ptr& player);
        void playerRebuilt(const MWWorld::Ptr& player);
        void gameSaved();

        void deliver();

        // MWPhysics::ContactObserver: physics threads
        void contactAdded(const JPH::BodyID& body1, const JPH::BodyID& body2, JPH::RVec3Arg point, JPH::Vec3Arg normal,
            bool projectileHit) override;
        void contactRemoved(const JPH::BodyID& body1, const JPH::BodyID& body2) override;

        // openeden::plugin::EntityBodies
        [[nodiscard]] openeden::sdk::BodyId bodyOfEntity(openeden::sdk::EntityId entity) const override;
        [[nodiscard]] openeden::sdk::EntityId entityOfBody(openeden::sdk::BodyId body) const override;

        // openeden::plugin::EntityWorld
        [[nodiscard]] openeden::sdk::EntityId playerEntity() const override { return mPlayer; }
        [[nodiscard]] std::string recordId(openeden::sdk::EntityId id) const override;

    private:
        enum class Kind
        {
            ObjectAdded,
            ObjectRemoved,
            CellLoaded,
            CellUnloaded,
            CellChanged,
            ActorDied,
            ActorRevived,
            ActorHit,
            GameLoaded,
            PlayerRebuilt,
            GameSaved,
            ContactBegin,
            ContactEnd,
            ProjectileHit,
        };

        /// The object behind an entity, by the address of its reference: Ptr can be dangling by delivery time.
        using ObjectKey = const void*;

        struct Queued
        {
            Kind mKind = Kind::GameSaved;
            ObjectKey mObject = nullptr;
            ObjectKey mOtherObject = nullptr;
            openeden::sdk::BodyId mBody{};
            openeden::sdk::BodyId mOtherBody{};
            std::string mText;
            float mDamage = 0.0f;
            bool mSuccessful = false;
            openeden::sdk::PhysicsVec3 mPoint{};
            openeden::sdk::PhysicsVec3 mNormal{};
        };

        struct Entity
        {
            openeden::sdk::EntityId mId{};
            openeden::sdk::BodyId mBody{};
            std::string mRecordId;
        };

        void queue(Queued item);
        void deliverOne(Queued& item);
        void updatePlayer(Queued& item);
        openeden::sdk::EntityId entityOf(ObjectKey object) const;
        void addEntity(ObjectKey object, const openeden::sdk::BodyId& body, std::string recordId);
        void removeEntity(ObjectKey object);
        void notify(openeden::sdk::EntityLifecycleEvent kind, openeden::sdk::EntityId entity);
        openeden::sdk::BodyId bodyOf(const MWWorld::Ptr& ptr) const;

        openeden::plugin::HostImpl& mHost;
        MWPhysics::PhysicsSystem* mPhysics;

        std::mutex mQueueMutex;
        std::vector<Queued> mQueue;
        // Jolt reports a contact per pair of sub-shapes, plugins want it per pair of bodies. Guarded by mQueueMutex.
        std::map<std::pair<std::uint32_t, std::uint32_t>, int> mContacts;

        std::vector<Queued> mDelivering; // Kept between frames, so that its storage is reused
        std::map<ObjectKey, Entity> mEntities;
        std::map<std::uint64_t, ObjectKey> mObjectsById;
        std::map<std::uint64_t, openeden::sdk::EntityId> mEntitiesByBody;
        ObjectKey mPlayerObject = nullptr;
        openeden::sdk::EntityId mPlayer{};
    };
}

#endif

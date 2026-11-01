#include "extensionworld.hpp"

#include <algorithm>
#include <cstring>

#include <components/extension/physics_service.hpp>
#include <components/misc/constants.hpp>

#include <openeden/game_events.hpp>
#include <openeden/physics_events.hpp>

#include "mwphysics/actor.hpp"
#include "mwphysics/object.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwworld/cellref.hpp"
#include "mwworld/livecellref.hpp"

namespace OMW
{
    namespace
    {
        using openeden::extension::PhysicsServiceImpl;
        using namespace openeden::sdk;

        std::uint64_t keyOf(EntityId id)
        {
            return (std::uint64_t{ id.index } << 32) | id.generation;
        }

        std::uint64_t keyOf(BodyId id)
        {
            return (std::uint64_t{ id.index } << 32) | id.generation;
        }

        template <std::size_t size>
        void copyName(char (&destination)[size], const std::string& text)
        {
            const std::size_t length = std::min(text.size(), size - 1);
            std::memcpy(destination, text.data(), length);
            destination[length] = '\0';
        }

        // This is the world-units boundary for events, as PhysicsServiceImpl is for queries.
        PhysicsVec3 toMeters(JPH::RVec3Arg point)
        {
            return { static_cast<float>(point.GetX() / Constants::UnitsPerMeter),
                static_cast<float>(point.GetY() / Constants::UnitsPerMeter),
                static_cast<float>(point.GetZ() / Constants::UnitsPerMeter) };
        }
    }

    ExtensionWorld::ExtensionWorld(openeden::extension::HostImpl& host, MWPhysics::PhysicsSystem* physics)
        : mHost(host)
        , mPhysics(physics)
    {
    }

    BodyId ExtensionWorld::bodyOf(const MWWorld::Ptr& ptr) const
    {
        if (mPhysics == nullptr)
            return {};
        if (const MWPhysics::Actor* actor = mPhysics->getActor(MWWorld::ConstPtr(ptr)))
            return PhysicsServiceImpl::toBodyId(actor->getPhysicsBody());
        if (const MWPhysics::Object* object = mPhysics->getObject(MWWorld::ConstPtr(ptr)))
            return PhysicsServiceImpl::toBodyId(object->getPhysicsBody());
        return {};
    }

    void ExtensionWorld::queue(Queued item)
    {
        const std::lock_guard lock(mQueueMutex);
        mQueue.push_back(std::move(item));
    }

    void ExtensionWorld::objectAdded(const MWWorld::Ptr& ptr)
    {
        Queued item;
        item.mKind = Kind::ObjectAdded;
        item.mObject = ptr.mRef;
        item.mBody = bodyOf(ptr); // Here, on the thread that changes the physics tables
        item.mText = ptr.getCellRef().getRefId().serializeText();
        queue(std::move(item));
    }

    void ExtensionWorld::objectRemoved(const MWWorld::Ptr& ptr)
    {
        Queued item;
        item.mKind = Kind::ObjectRemoved;
        item.mObject = ptr.mRef;
        queue(std::move(item));
    }

    void ExtensionWorld::cellLoaded(std::string_view name)
    {
        Queued item;
        item.mKind = Kind::CellLoaded;
        item.mText = name;
        queue(std::move(item));
    }

    void ExtensionWorld::cellUnloaded(std::string_view name)
    {
        Queued item;
        item.mKind = Kind::CellUnloaded;
        item.mText = name;
        queue(std::move(item));
    }

    void ExtensionWorld::cellChanged(std::string_view name)
    {
        Queued item;
        item.mKind = Kind::CellChanged;
        item.mText = name;
        queue(std::move(item));
    }

    void ExtensionWorld::actorDied(const MWWorld::Ptr& actor)
    {
        Queued item;
        item.mKind = Kind::ActorDied;
        item.mObject = actor.mRef;
        queue(std::move(item));
    }

    void ExtensionWorld::actorRevived(const MWWorld::Ptr& actor)
    {
        Queued item;
        item.mKind = Kind::ActorRevived;
        item.mObject = actor.mRef;
        queue(std::move(item));
    }

    void ExtensionWorld::actorHit(
        const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful)
    {
        Queued item;
        item.mKind = Kind::ActorHit;
        item.mObject = target.mRef;
        item.mOtherObject = attacker.isEmpty() ? nullptr : attacker.mRef;
        item.mDamage = damage;
        item.mSuccessful = successful;
        queue(std::move(item));
    }

    void ExtensionWorld::gameLoaded(const MWWorld::Ptr& player)
    {
        Queued item;
        item.mKind = Kind::GameLoaded;
        item.mObject = player.mRef;
        item.mBody = bodyOf(player);
        item.mText = player.getCellRef().getRefId().serializeText();
        queue(std::move(item));
    }

    void ExtensionWorld::playerRebuilt(const MWWorld::Ptr& player)
    {
        Queued item;
        item.mKind = Kind::PlayerRebuilt;
        item.mObject = player.mRef;
        item.mBody = bodyOf(player);
        item.mText = player.getCellRef().getRefId().serializeText();
        queue(std::move(item));
    }

    void ExtensionWorld::gameSaved()
    {
        Queued item;
        item.mKind = Kind::GameSaved;
        queue(std::move(item));
    }

    void ExtensionWorld::contactAdded(const JPH::BodyID& body1, const JPH::BodyID& body2, JPH::RVec3Arg point,
        JPH::Vec3Arg normal, bool projectileHit)
    {
        const BodyId a = PhysicsServiceImpl::toBodyId(body1);
        const BodyId b = PhysicsServiceImpl::toBodyId(body2);
        const PhysicsVec3 pointM = toMeters(point);
        const PhysicsVec3 worldNormal{ normal.GetX(), normal.GetY(), normal.GetZ() };

        const std::lock_guard lock(mQueueMutex);
        if (projectileHit)
        {
            Queued item;
            item.mKind = Kind::ProjectileHit;
            item.mBody = a;
            item.mOtherBody = b;
            item.mPoint = pointM;
            item.mNormal = worldNormal;
            mQueue.push_back(std::move(item));
        }
        if (++mContacts[std::minmax(body1.GetIndexAndSequenceNumber(), body2.GetIndexAndSequenceNumber())] == 1)
        {
            Queued item;
            item.mKind = Kind::ContactBegin;
            item.mBody = a;
            item.mOtherBody = b;
            item.mPoint = pointM;
            item.mNormal = worldNormal;
            mQueue.push_back(std::move(item));
        }
    }

    void ExtensionWorld::contactRemoved(const JPH::BodyID& body1, const JPH::BodyID& body2)
    {
        const std::lock_guard lock(mQueueMutex);
        // Only pairs contactAdded took: Jolt reports the end of every contact, of every kind of body
        const auto found
            = mContacts.find(std::minmax(body1.GetIndexAndSequenceNumber(), body2.GetIndexAndSequenceNumber()));
        if (found == mContacts.end() || --found->second > 0)
            return;
        mContacts.erase(found);

        Queued item;
        item.mKind = Kind::ContactEnd;
        item.mBody = PhysicsServiceImpl::toBodyId(body1);
        item.mOtherBody = PhysicsServiceImpl::toBodyId(body2);
        mQueue.push_back(std::move(item));
    }

    EntityId ExtensionWorld::entityOf(ObjectKey object) const
    {
        const auto found = mEntities.find(object);
        return found != mEntities.end() ? found->second.mId : EntityId{};
    }

    void ExtensionWorld::notify(EntityLifecycleEvent kind, EntityId entity)
    {
        mHost.entityManager().dispatchLifecycleEvent(kind, entity);
        mHost.eventBus().publishImmediate(EntityLifecycleNotice{ kind, entity });
    }

    void ExtensionWorld::addEntity(ObjectKey object, const BodyId& body, std::string recordId)
    {
        if (mEntities.contains(object))
            return;
        const EntityId id = mHost.entityManager().createEntity();
        mEntities.emplace(object, Entity{ id, body, std::move(recordId) });
        mObjectsById.emplace(keyOf(id), object);
        if (body.isValid())
            mEntitiesByBody.emplace(keyOf(body), id);
        notify(EntityLifecycleEvent::Spawned, id);
    }

    void ExtensionWorld::removeEntity(ObjectKey object)
    {
        const auto found = mEntities.find(object);
        if (found == mEntities.end())
            return;
        const Entity entity = std::move(found->second);
        mEntities.erase(found);
        mObjectsById.erase(keyOf(entity.mId));
        if (entity.mBody.isValid())
            mEntitiesByBody.erase(keyOf(entity.mBody));

        // destroyEntity tells listeners, then frees the entity's data
        mHost.eventBus().publishImmediate(EntityLifecycleNotice{ EntityLifecycleEvent::Despawned, entity.mId });
        mHost.entityManager().destroyEntity(entity.mId);
    }

    void ExtensionWorld::updatePlayer(Queued& item)
    {
        // The player is not added to the scene like other objects. Its reference stays at one address for good
        // (Player::mPlayer), but World::renderPlayer gives it a new body on every load and in character generation.
        if (mPlayerObject != item.mObject)
        {
            removeEntity(mPlayerObject);
            addEntity(item.mObject, item.mBody, std::move(item.mText));
            mPlayerObject = item.mObject;
            mPlayer = entityOf(item.mObject);
            return;
        }
        const auto found = mEntities.find(mPlayerObject);
        if (found == mEntities.end() || found->second.mBody == item.mBody)
            return;
        if (found->second.mBody.isValid())
            mEntitiesByBody.erase(keyOf(found->second.mBody));
        found->second.mBody = item.mBody;
        if (item.mBody.isValid())
            mEntitiesByBody.emplace(keyOf(item.mBody), mPlayer);
    }

    void ExtensionWorld::deliverOne(Queued& item)
    {
        auto& bus = mHost.eventBus();
        switch (item.mKind)
        {
            case Kind::ObjectAdded:
                addEntity(item.mObject, item.mBody, std::move(item.mText));
                break;
            case Kind::ObjectRemoved:
                removeEntity(item.mObject);
                break;
            case Kind::CellLoaded:
            {
                CellLoadedEvent event;
                copyName(event.cellName, item.mText);
                notify(EntityLifecycleEvent::CellLoaded, {});
                bus.publishImmediate(event);
                break;
            }
            case Kind::CellUnloaded:
            {
                CellUnloadedEvent event;
                copyName(event.cellName, item.mText);
                notify(EntityLifecycleEvent::CellUnloaded, {});
                bus.publishImmediate(event);
                break;
            }
            case Kind::CellChanged:
            {
                CellChangedEvent event;
                copyName(event.cellName, item.mText);
                bus.publishImmediate(event);
                break;
            }
            case Kind::ActorDied:
                notify(EntityLifecycleEvent::Died, entityOf(item.mObject));
                break;
            case Kind::ActorRevived:
                notify(EntityLifecycleEvent::Revived, entityOf(item.mObject));
                break;
            case Kind::ActorHit:
                bus.publishImmediate(ActorHitEvent{
                    entityOf(item.mOtherObject), entityOf(item.mObject), item.mDamage, item.mSuccessful });
                break;
            case Kind::GameLoaded:
                updatePlayer(item);
                bus.publishImmediate(GameLoadedEvent{});
                break;
            case Kind::PlayerRebuilt:
                updatePlayer(item);
                break;
            case Kind::GameSaved:
                bus.publishImmediate(GameSavedEvent{});
                break;
            case Kind::ContactBegin:
                bus.publishImmediate(
                    PhysicsContactBeginEvent{ item.mBody, item.mOtherBody, item.mPoint, item.mNormal });
                break;
            case Kind::ContactEnd:
                bus.publishImmediate(PhysicsContactEndEvent{ item.mBody, item.mOtherBody });
                break;
            case Kind::ProjectileHit:
                bus.publishImmediate(PhysicsHitEvent{ item.mBody, item.mOtherBody, item.mPoint, item.mNormal });
                break;
        }
    }

    void ExtensionWorld::deliver()
    {
        {
            const std::lock_guard lock(mQueueMutex);
            if (mQueue.empty())
                return;
            mQueue.swap(mDelivering);
        }
        for (Queued& item : mDelivering)
            deliverOne(item);
        mDelivering.clear();
    }

    BodyId ExtensionWorld::bodyOfEntity(EntityId entity) const
    {
        const auto object = mObjectsById.find(keyOf(entity));
        if (object == mObjectsById.end())
            return {};
        const auto found = mEntities.find(object->second);
        return found != mEntities.end() ? found->second.mBody : BodyId{};
    }

    EntityId ExtensionWorld::entityOfBody(BodyId body) const
    {
        const auto found = mEntitiesByBody.find(keyOf(body));
        return found != mEntitiesByBody.end() ? found->second : EntityId{};
    }

    std::string ExtensionWorld::recordId(EntityId id) const
    {
        const auto object = mObjectsById.find(keyOf(id));
        if (object == mObjectsById.end())
            return {};
        const auto found = mEntities.find(object->second);
        return found != mEntities.end() ? found->second.mRecordId : std::string{};
    }
}

#ifndef OPENEDEN_APPS_OPENMW_MWBASE_EXTENSIONMANAGER_H
#define OPENEDEN_APPS_OPENMW_MWBASE_EXTENSIONMANAGER_H

#include <string>
#include <string_view>

namespace MWWorld
{
    class Ptr;
}

namespace MWBase
{
    /// How game code tells extensions what happens (task 0017). Extensions never take part in a save (decision 0005).
    class ExtensionManager
    {
    public:
        virtual ~ExtensionManager() = default;

        /// What happens in the world, for extensions (task 0017). Any thread; these only queue, and do nothing without
        /// extensions.
        virtual void objectAddedToScene(const MWWorld::Ptr& ptr) = 0;
        virtual void objectRemovedFromScene(const MWWorld::Ptr& ptr) = 0;
        virtual void cellLoaded(std::string_view name) = 0;
        virtual void cellUnloaded(std::string_view name) = 0;
        virtual void cellChanged(std::string_view name) = 0;
        virtual void actorDied(const MWWorld::Ptr& actor) = 0;
        virtual void actorRevived(const MWWorld::Ptr& actor) = 0;
        virtual void actorHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful)
            = 0;
        virtual void gameLoaded() = 0;
        virtual void playerRebuilt(const MWWorld::Ptr& player) = 0;
        virtual void gameSaved() = 0;

        /// The game pack of this session and its version, recorded in every save (task 0084). Empty with none.
        /// Fixed once the host has started; safe to read from the save and load workers.
        virtual const std::string& gamePackId() const
        {
            static const std::string none;
            return none;
        }
        virtual const std::string& gamePackVersion() const
        {
            static const std::string none;
            return none;
        }

        /// Non-empty once the game pack has failed: saving is refused. Main thread only.
        virtual const std::string& sessionError() const
        {
            static const std::string none;
            return none;
        }
    };
}

#endif

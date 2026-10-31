#ifndef OPENEDEN_APPS_OPENMW_MWBASE_PLUGINMANAGER_H
#define OPENEDEN_APPS_OPENMW_MWBASE_PLUGINMANAGER_H

#include <string_view>

namespace MWWorld
{
    class Ptr;
}

namespace MWBase
{
    /// How game code tells plugins what happens (task 0017). Plugins never take part in a save (decision 0005).
    class PluginManager
    {
    public:
        virtual ~PluginManager() = default;

        /// What happens in the world, for plugins (task 0017). Any thread; these only queue, and do nothing without
        /// plugins.
        virtual void objectAddedToScene(const MWWorld::Ptr& ptr) = 0;
        virtual void objectRemovedFromScene(const MWWorld::Ptr& ptr) = 0;
        virtual void cellLoaded(std::string_view name) = 0;
        virtual void cellUnloaded(std::string_view name) = 0;
        virtual void cellChanged(std::string_view name) = 0;
        virtual void actorDied(const MWWorld::Ptr& actor) = 0;
        virtual void actorRevived(const MWWorld::Ptr& actor) = 0;
        virtual void actorHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful) = 0;
        virtual void gameLoaded() = 0;
        virtual void playerRebuilt(const MWWorld::Ptr& player) = 0;
        virtual void gameSaved() = 0;
    };
}

#endif

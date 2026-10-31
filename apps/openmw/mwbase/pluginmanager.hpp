#ifndef OPENEDEN_APPS_OPENMW_MWBASE_PLUGINMANAGER_H
#define OPENEDEN_APPS_OPENMW_MWBASE_PLUGINMANAGER_H

#include <cstdint>
#include <string_view>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWBase
{
    /// How the game's save code reaches plugins' saved data (task 0016).
    ///
    /// Saving and loading run on a worker thread while frames continue, and plugins must never be called there: their
    /// phase callbacks are running. So prepareSave and applyLoadedRecords, which call plugins, are for the main thread,
    /// and the record functions between them only pass bytes along.
    class PluginManager
    {
    public:
        virtual ~PluginManager() = default;

        /// Main thread, before the save's worker starts: takes each plugin's chunk.
        virtual void prepareSave() = 0;

        virtual int countSavedGameRecords() const = 0;
        virtual void write(ESM::ESMWriter& writer) = 0;
        virtual void readRecord(ESM::ESMReader& reader, std::uint32_t type) = 0;

        /// Main thread, when loading has finished: gives plugins the chunks that were read.
        virtual void applyLoadedRecords() = 0;

        /// Forgets the data of the game that was running: a new game or another save is starting.
        virtual void clearSaveData() = 0;

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

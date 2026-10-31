#ifndef OPENEDEN_APPS_OPENMW_MWBASE_PLUGINMANAGER_H
#define OPENEDEN_APPS_OPENMW_MWBASE_PLUGINMANAGER_H

#include <cstdint>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
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
    };
}

#endif

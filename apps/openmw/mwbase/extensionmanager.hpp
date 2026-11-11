#ifndef OPENEDEN_APPS_OPENMW_MWBASE_EXTENSIONMANAGER_H
#define OPENEDEN_APPS_OPENMW_MWBASE_EXTENSIONMANAGER_H

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MWWorld
{
    class Ptr;
}

namespace NeutralWorld
{
    class NeutralWorldStore;
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

        /// The extension profiler overlay (task 0017 stage 5): whether it is switched on (`[Game] extension profiler`),
        /// and its text, the scopes of the last frame as a report for the debug window's Extensions tab (empty when
        /// off). The text escapes MyGUI markup. Main thread only.
        virtual bool profilerOverlayEnabled() const { return false; }
        virtual std::string profilerReport() { return {}; }

        /// The neutral world store when `[Game] neutral world` is on (task 0109, stage N3a-3: the world model asks it
        /// for cells by editor id), else null. Main thread only.
        virtual NeutralWorld::NeutralWorldStore* neutralWorldStore() { return nullptr; }

        /// Whether a game pack has registered a neutral world source (a session of that pack's game, task 0109): the
        /// session then has its own start path. False with `[Game] neutral world` off and with Morrowind. Main thread.
        virtual bool hasNeutralWorldSource() { return false; }

        /// Non-empty once the game pack has failed: saving is refused. Main thread only.
        virtual const std::string& sessionError() const
        {
            static const std::string none;
            return none;
        }

        /// Why saving is refused for this session, or empty when it is allowed: the game pack's failure
        /// (sessionError) or, for a game pack that registered a neutral world source, the missing save support until
        /// stage N6 (task 0103). Main thread only.
        virtual std::string savingRefusal() const { return {}; }

        /// The game pack's save chunks as one blob for the save (task 0084 leftovers, stage I4). Empty with no game
        /// pack or when the pack writes no chunk: the save then has no record for them and is byte for byte what it
        /// always was. False, with the reason, when the chunks cannot be written (the pack has failed, a handler
        /// refused). Main thread only: the engine calls it just before the save's worker starts, so that no pack code
        /// runs on the worker.
        virtual bool collectGamePackSave(std::vector<std::byte>& bytes, std::string& error)
        {
            bytes.clear();
            error.clear();
            return true;
        }

        /// Hands a loaded save's blob to the game pack. Main thread only: the engine calls it when a load has finished
        /// reading. False, with the reason, when the blob is malformed or a handler refuses it.
        virtual bool restoreGamePackSave(std::span<const std::byte> bytes, std::string& error)
        {
            error.clear();
            return true;
        }
    };
}

#endif

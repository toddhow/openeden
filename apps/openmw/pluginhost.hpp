#ifndef OPENEDEN_APPS_OPENMW_PLUGINHOST_H
#define OPENEDEN_APPS_OPENMW_PLUGINHOST_H

#include <components/plugin/host_impl.hpp>
#include <components/esm3/pluginchunk.hpp>
#include <components/plugin/lifecycle_manager.hpp>
#include <components/plugin/save_service.hpp>
#include <components/plugin/vfs_service.hpp>

#include "mwbase/luamanager.hpp"
#include "mwbase/pluginmanager.hpp"

#include <openeden/phases.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace OMW
{
    /**
     * @brief Runs plugins inside the game (task 0015). Owned by OMW::Engine, used on the main thread only.
     *
     * The constructor finds and loads the enabled plugins' libraries, without running any of their code beyond the
     * handshake. start() registers, resolves and starts them; stop() stops, destroys and unloads them. A plugin that
     * fails at any point is logged with its stage and reason and disabled with its dependants, and the game goes on.
     * With no plugin loaded, every call here does nothing.
     */
    class PluginHost final : public MWBase::PluginManager
    {
    public:
        struct Config
        {
            /// Searched in this order; a plugin found in an earlier directory wins over a later one with the same ID.
            std::vector<std::filesystem::path> searchDirs;
            /// Plugin IDs to load. Empty means none: unlike filterEnabledPlugins, nothing loads by default.
            std::vector<std::string> enabled;
        };

        explicit PluginHost(const Config& config);
        ~PluginHost() override;

        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        void start();

        /// Calling it again, or before start(), is safe.
        void stop();

        /// No allocation: this is called several times per frame.
        void runPhase(openeden::sdk::Phase phase, float dt)
        {
            if (mRunning)
                mLifecycle.runPhase(phase, dt);
        }

        [[nodiscard]] bool hasPlugins() const noexcept { return mLoaded; }

        /// The loaded plugins' data folders that exist, in load order, for the game's VFS.
        [[nodiscard]] const std::vector<openeden::plugin::PluginDataDir>& dataDirs() const noexcept { return mDataDirs; }

        /// Logs each plugin file that hides another file, once. gameData holds the game's data without any plugin's.
        void reportDataOverrides(const VFS::Manager& gameData) const;

        /// Plugins read files through this from start() on. It must stay alive until stop().
        void setEngineVfs(const VFS::Manager* vfs) { mHost.vfsService().setEngineVfs(vfs); }

        /// Marked events are queued, and sent here. The engine calls this only where Lua may be touched: a save's or
        /// a load's worker uses Lua while frames, and so phases, continue.
        void sendPendingLuaEvents()
        {
            if (!mPendingLuaEvents.empty())
                sendPendingLuaEventsNow();
        }

        /// Global scripts exist only in a running game, so marked events wait until there is one.
        void setLuaManager(MWBase::LuaManager* lua);

        // MWBase::PluginManager
        void prepareSave() override;
        int countSavedGameRecords() const override;
        void write(ESM::ESMWriter& writer) override;
        void readRecord(ESM::ESMReader& reader, std::uint32_t type) override;
        void applyLoadedRecords() override;
        void clearSaveData() override;

        [[nodiscard]] openeden::plugin::HostImpl& host() noexcept { return mHost; }
        [[nodiscard]] openeden::plugin::LifecycleManager& lifecycle() noexcept { return mLifecycle; }

    private:
        struct LogBridge;

        void sendPendingLuaEventsNow();

        openeden::plugin::HostImpl mHost;
        openeden::plugin::LifecycleManager mLifecycle;
        std::shared_ptr<LogBridge> mLogBridge;
        std::vector<openeden::plugin::PluginDataDir> mDataDirs;
        std::vector<openeden::plugin::SaveServiceImpl::Chunk> mChunksToSave;
        std::vector<ESM::PluginChunk> mLoadedChunks;
        MWBase::LuaManager* mLuaManager = nullptr;
        struct PendingLuaEvent
        {
            std::string mName;
            std::vector<MWBase::LuaManager::PluginEventField> mFields;
        };
        std::vector<PendingLuaEvent> mPendingLuaEvents;
        bool mPendingLuaEventsOverflowed = false;
        bool mLoaded = false;
        bool mRunning = false;
    };
}

#endif

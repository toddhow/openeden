#ifndef OPENEDEN_APPS_OPENMW_EXTENSIONHOST_H
#define OPENEDEN_APPS_OPENMW_EXTENSIONHOST_H

#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/vfs_service.hpp>

#include "mwbase/extensionmanager.hpp"
#include "mwbase/luamanager.hpp"

#include <openeden/phases.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace MWPhysics
{
    class PhysicsSystem;
}

namespace OMW
{
    class ExtensionWorld;

    /**
     * @brief Runs extensions inside the game (task 0015). Owned by OMW::Engine, used on the main thread only.
     *
     * The constructor finds and loads the enabled extensions' libraries, without running any of their code beyond the
     * handshake. start() registers, resolves and starts them; stop() stops, destroys and unloads them. An extension
     * that fails at any point is logged with its stage and reason and disabled with its dependants, and the game goes
     * on. With no extension loaded, every call here does nothing.
     */
    class ExtensionHost final : public MWBase::ExtensionManager
    {
    public:
        struct Config
        {
            /// Searched in this order; an extension found in an earlier directory wins over a later one with the same
            /// ID.
            std::vector<std::filesystem::path> searchDirs;
            /// Extension IDs to load. Empty means none: unlike filterEnabledExtensions, nothing loads by default.
            std::vector<std::string> enabled;
            /// The game pack of this session (game=), empty for none. It is loaded without being in `enabled`; any
            /// other pack of kind "game" is refused, and with this empty so is every one (task 0084).
            std::string gamePackId;
        };

        explicit ExtensionHost(const Config& config);
        ~ExtensionHost() override;

        ExtensionHost(const ExtensionHost&) = delete;
        ExtensionHost& operator=(const ExtensionHost&) = delete;

        void start();

        /// Calling it again, or before start(), is safe.
        void stop();

        /// No allocation: this is called several times per frame.
        void runPhase(openeden::sdk::Phase phase, float dt)
        {
            if (mRunning)
                mLifecycle.runPhase(phase, dt);
        }

        [[nodiscard]] bool hasExtensions() const noexcept { return mLoaded; }

        /// The ID of the session's game pack once start() has run; empty if none is loaded.
        [[nodiscard]] const std::string& gamePackId() const noexcept override { return mLifecycle.getGamePackId(); }

        [[nodiscard]] const std::string& gamePackVersion() const noexcept override
        {
            return mLifecycle.getGamePackVersion();
        }

        /// Non-empty once the game pack has failed: the session cannot go on and saving is refused.
        [[nodiscard]] const std::string& sessionError() const noexcept override { return mLifecycle.getSessionError(); }

        /// The loaded extensions' data folders that exist, in load order, for the game's VFS.
        [[nodiscard]] const std::vector<openeden::extension::ExtensionDataDir>& dataDirs() const noexcept
        {
            return mDataDirs;
        }

        /// Logs each extension file that hides another file, once. gameData holds the game's data without any
        /// extension's.
        void reportDataOverrides(const VFS::Manager& gameData) const;

        /// Extensions read files through this from start() on. It must stay alive until stop().
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

        // MWBase::ExtensionManager
        void objectAddedToScene(const MWWorld::Ptr& ptr) override;
        void objectRemovedFromScene(const MWWorld::Ptr& ptr) override;
        void cellLoaded(std::string_view name) override;
        void cellUnloaded(std::string_view name) override;
        void cellChanged(std::string_view name) override;
        void actorDied(const MWWorld::Ptr& actor) override;
        void actorRevived(const MWWorld::Ptr& actor) override;
        void actorHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful) override;
        void gameLoaded() override;
        void playerRebuilt(const MWWorld::Ptr& player) override;
        void gameSaved() override;

        /// Gives extensions the game's world. Call it before start(): extensions keep the services they get there, and
        /// the physics system must stay alive until stop().
        void attachWorld(MWPhysics::PhysicsSystem& physics);

        /// One line for each extension that has failed since the last call, for the message the game shows at the main
        /// menu. Empty if none has. Each failure is reported once.
        [[nodiscard]] std::string takeFailureReport();

        /// Gives extensions what the world queued. The engine calls it just before Phase::Input.
        void deliverWorldEvents();

        [[nodiscard]] openeden::extension::HostImpl& host() noexcept { return mHost; }
        [[nodiscard]] openeden::extension::LifecycleManager& lifecycle() noexcept { return mLifecycle; }

    private:
        struct LogBridge;

        std::unique_ptr<ExtensionWorld> mWorld;
        MWPhysics::PhysicsSystem* mPhysics = nullptr;

        void sendPendingLuaEventsNow();

        openeden::extension::HostImpl mHost;
        openeden::extension::LifecycleManager mLifecycle;
        std::shared_ptr<LogBridge> mLogBridge;
        std::vector<openeden::extension::ExtensionDataDir> mDataDirs;
        std::vector<std::string> mLoadFailures;
        std::size_t mReportedFailures = 0;
        MWBase::LuaManager* mLuaManager = nullptr;
        struct PendingLuaEvent
        {
            std::string mName;
            std::vector<MWBase::LuaManager::ExtensionEventField> mFields;
        };
        std::vector<PendingLuaEvent> mPendingLuaEvents;
        bool mPendingLuaEventsOverflowed = false;
        bool mLoaded = false;
        bool mRunning = false;
    };
}

#endif

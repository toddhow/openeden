#ifndef OPENEDEN_APPS_OPENMW_EXTENSIONHOST_H
#define OPENEDEN_APPS_OPENMW_EXTENSIONHOST_H

#include <components/extension/debug_draw_frame.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/profiler_snapshot.hpp>
#include <components/extension/vfs_service.hpp>
#include <components/worldmodel/store.hpp>

#include "builtinpack.hpp"
#include "mwbase/extensionmanager.hpp"
#include "mwbase/luamanager.hpp"

#include <openeden/phases.hpp>

#include <openeden/extension.hpp>

#include <filesystem>
#include <functional>
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
        using StaticPack = BuiltInGamePack;

        struct Config
        {
            /// Searched in this order; an extension found in an earlier directory wins over a later one with the same
            /// ID.
            std::vector<std::filesystem::path> searchDirs;
            /// Extension IDs to load. Empty means none: unlike filterEnabledExtensions, nothing loads by default.
            std::vector<std::string> enabled;
            /// The game pack of this session (game=), empty for none. It is loaded without being in `enabled`; any
            /// other pack of kind "game" is refused, and with this empty so is every one (task 0084).
            std::string gamePackId = {};
            /// Game packs linked into the executable (task 0084 leftovers, stage I1b). One is used only when it is the
            /// game= pack and no pack of that id is installed on disk. It goes through the same lifecycle as a loaded
            /// pack, with no library.
            std::vector<StaticPack> staticPacks = {};
            /// [Game] neutral world (task 0103): build the neutral world store and let the game pack register its world
            /// source in it. False builds nothing and the pack is told that nothing reads its source.
            bool neutralWorld = false;
        };

        explicit ExtensionHost(const Config& config);
        ~ExtensionHost() override;

        ExtensionHost(const ExtensionHost&) = delete;
        ExtensionHost& operator=(const ExtensionHost&) = delete;

        /// The first start phase (task 0084 leftovers, stage I1b): starts the game pack chosen by game= and the
        /// extensions it needs, and installs the pack's GameRules into the core. The Game state calls it before it
        /// builds the managers and windows that depend on the rules; start() later starts everything else and skips
        /// these. Does nothing, and returns false, when no game pack is loaded: then start() is the old single start.
        /// `physics`, if given, is attached first so that the pack gets the world's services at its start. Main thread.
        bool startGamePack(MWPhysics::PhysicsSystem* physics = nullptr);

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

        /// The extensions' debug draw of this frame for the renderer (task 0017 stage 5), copied at the end of the Ui
        /// phase; null when `[Game] extension debug draw` is off. Valid until the next Ui phase. Main thread.
        [[nodiscard]] const openeden::extension::DebugDrawFrame* debugDrawFrame() const noexcept
        {
            return mDebugDrawShown ? mDebugFrame.get() : nullptr;
        }

        // MWBase::ExtensionManager: the profiler overlay (task 0017 stage 5)
        bool profilerOverlayEnabled() const override { return mProfilerShown; }
        std::string profilerReport() override;

        // MWBase::ExtensionManager: the game pack's save chunks (stage I4)
        bool collectGamePackSave(std::vector<std::byte>& bytes, std::string& error) override;
        bool restoreGamePackSave(std::span<const std::byte> bytes, std::string& error) override;

        /// The game pack's failure for the engine to act on, once: the first call after the pack has failed returns the
        /// located message (the one sessionError() holds, which also keeps saving refused); every other call returns an
        /// empty string. The engine asks once per frame (apps/openmw/game.hpp) and, on a message, ends the session
        /// through MWState::StateManager::error (task 0084 leftovers, stage I2). Main thread only.
        [[nodiscard]] std::string takeSessionFault();

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

        /// The neutral world store (decision 0016; N3a-1: nothing reads it yet). Null unless Config::neutralWorld. It
        /// outlives the game pack: the pack's source leaves it when the pack stops. Main thread only.
        [[nodiscard]] NeutralWorld::NeutralWorldStore* neutralWorldStore() noexcept { return mNeutralWorldStore.get(); }

    private:
        struct LogBridge;
        struct StoreListener;

        std::unique_ptr<ExtensionWorld> mWorld;
        MWPhysics::PhysicsSystem* mPhysics = nullptr;

        void sendPendingLuaEventsNow();
        // Runs at the end of Phase::Ui, before the end-of-frame reset (LifecycleManager::setFrameEndHook)
        static void frameEndHook(void* host, float dt);
        void collectFrameEnd();
        void beginRunning();
        void finishStart();
        void installPackRules();
        void attachWorldNow(MWPhysics::PhysicsSystem& physics);
        bool mRulesFromPack = false;
        bool mStarted = false;
        bool mGamePackBuiltIn = false;

        // Task 0103 (N3a-1): both are made in the constructor when Config::neutralWorld is set, and are declared before
        // mHost and mLifecycle so that they are destroyed after them: a pack withdraws its source (through the
        // listener, into the store) while the lifecycle destroys it.
        std::unique_ptr<NeutralWorld::NeutralWorldStore> mNeutralWorldStore;
        std::unique_ptr<StoreListener> mStoreListener;

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
        bool mSessionFaultTaken = false;

        // Task 0017 stage 5. Both are made once, in beginRunning(), and only when their setting is on; the hook then
        // fills them on the main thread without allocating.
        std::unique_ptr<openeden::extension::DebugDrawFrame> mDebugFrame;
        std::unique_ptr<openeden::extension::ProfilerSnapshot> mProfilerSnapshot;
        bool mDebugDrawShown = false;
        bool mProfilerShown = false;
        bool mReportedDebugOverflow = false;
    };
}

#endif

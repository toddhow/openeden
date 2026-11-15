#ifndef CSM_EXTENSION_EDITORHOST_H
#define CSM_EXTENSION_EDITORHOST_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/session_content.hpp>

#include "../module/modulehost.hpp"
#include <openeden/physics.hpp>

namespace JPH
{
    class PhysicsSystem;
}

namespace NeutralWorld
{
    class NeutralWorldStore;
}

namespace VFS
{
    class Manager;
}

namespace GameSession
{
    class StoreListener;
}

namespace CSMExtension
{
    /// What the Extension Manager shows about one extension.
    struct ExtensionInfo
    {
        enum class State
        {
            Installed, ///< Found on disk, not in the enable list: not loaded.
            Active, ///< Loaded and started.
            Failed, ///< Enabled, but it did not load, did not start, or was disabled with something it needs.
        };

        std::string mId; ///< From the manifest; for a folder with a broken manifest, what the loader could name.
        std::string mName;
        std::string mVersion;
        std::filesystem::path mDirectory;
        State mState = State::Installed;
        bool mEnabled = false; ///< In the enable list (extension= lines in openmw.cfg, or --extension).
        std::string mMessage; ///< Why, for Failed; empty otherwise.
        /// The manifest's "editor" entry (task 0031), if it has one. Not used by the game.
        std::optional<openeden::extension::EditorEntry> mEditor;
        /// The session's game pack (task 0173): the pack Config::mGamePackId chose, listed first.
        bool mGamePack = false;
    };

    /// Eden Studio's extension host (task 0024, stage D; decision 0007, Phase 1): it loads the enabled extensions the
    /// way the game does (OMW::ExtensionHost, apps/openmw/extensionhost.hpp), beside the editor's viewport, read-only.
    ///
    /// Read-only here means: the host has no physics world, no entities, no data folders in a VFS and no Lua, and no
    /// phase is called (there is no frame loop of the simulation yet; that is stage E). Extensions are loaded,
    /// registered, resolved and started, so their handshake, contracts and dependencies are checked and their
    /// failures are reported, and they are stopped and unloaded again when the host goes. The host API has no
    /// service that reaches a document, so an extension cannot change one through it.
    ///
    /// Ownership: owned by CS::Editor, which lives as long as the program; views hold plain pointers to it.
    /// Threads: main thread only.
    class EditorExtensionHost
    {
    public:
        struct Config
        {
            /// Searched in this order; an extension found in an earlier directory wins over a later one.
            std::vector<std::filesystem::path> mSearchDirs;
            /// Extension IDs to load. Empty means none.
            std::vector<std::string> mEnabled;
            /// The session's game pack (game= or --game, task 0173). Loaded without an extension= line; any other
            /// pack of kind "game" is refused (GameSession::classify, as in the game). Empty, or an id in
            /// mBuiltInGamePacks, loads and starts no game pack: the editor works as before.
            std::string mGamePackId = {};
            /// Game pack ids the editor has built in and does not start (it works on Morrowind's structs directly).
            std::vector<std::string> mBuiltInGamePacks = { "morrowind" };
        };

        enum class GamePackState
        {
            None, ///< No game pack chosen.
            BuiltIn, ///< A built-in one (Morrowind): nothing loaded or started.
            Loaded, ///< Loaded, start() not run yet.
            Active, ///< Started; its world source, if it has one, is in neutralWorldStore().
            Failed, ///< Not installed, refused, or failed to load or start: gamePackFailure() says why.
        };

        /// Finds extensions and loads the enabled ones' libraries (the handshake only). Never throws for a bad
        /// extension: that one is recorded as Failed and the others go on.
        explicit EditorExtensionHost(const Config& config);
        ~EditorExtensionHost();

        EditorExtensionHost(const EditorExtensionHost&) = delete;
        EditorExtensionHost& operator=(const EditorExtensionHost&) = delete;

        /// Gives extensions read-only queries on the editor's Jolt world (task 0030), and publishes the query masks
        /// "world" and "dynamic_world" under the values CSMPhysics::StudioWorld uses. Call it before start(), with a
        /// world that outlives the host (there is no detach). 	hrows std::logic_error if extensions are running. \param
        /// unitsPerMeter the world's length unit.
        void attachPhysicsWorld(JPH::PhysicsSystem& world, float unitsPerMeter);

        /// Null until attachPhysicsWorld has been called.
        openeden::sdk::PhysicsQueryService* getPhysicsQueryService();

        /// Around a physics step (CSMPhysics::StudioWorld::StepObserver): beforeStep runs Phase::PreSimulation;
        /// afterStep runs Phase::PostSimulation and then Phase::Ui, which resets the per-frame scratch memory and
        /// profiler. They do nothing unless the host is running. No other phase runs in the editor (no game frame).
        void beforeStep(float dt);
        void afterStep(float dt);

        /// The editor modules of the extensions that are active and have an "editor" manifest entry, in load order,
        /// each with its library next to the manifest (Origin::Extension). Eden Studio loads them after the core list.
        std::vector<CSMModule::ModuleSource> editorModuleSources() const;

        /// Registers, resolves and starts the loaded extensions. Calling it again does nothing.
        void start();

        /// Stops, destroys and unloads them. Safe to call again, or before start().
        void stop();

        bool isRunning() const { return mRunning; }

        /// Every extension found, plus the enabled ones that were not found, in the order the loader saw them.
        /// State reflects start() if it has run.
        std::vector<ExtensionInfo> extensions() const;

        /// "2 of 3 extensions active" and the like, for the menu and the status line.
        std::string summary() const;

        /// The chosen game pack id (Config::mGamePackId), empty for none.
        const std::string& gamePackId() const { return mGamePackId; }
        /// The manifest name of the chosen game pack once it is found ("Morrowind" for the built-in one), else its id.
        std::string gamePackName() const;
        GamePackState gamePackState() const;
        /// Why the game pack failed, "<id>: <reason>" as the game reports it; empty unless gamePackState() is Failed.
        std::string gamePackFailure() const;

        /// Hands a loaded game pack the session's content files (GameSession::collectContent of the content= list)
        /// and the VFS its reads go through (string tables). Only the files the pack's content loaders accept reach
        /// it. Call it before start(), with a VFS that outlives the host. Does nothing unless a game pack was loaded.
        void setSessionContent(std::vector<openeden::extension::SessionContentEntry> entries, const VFS::Manager* vfs);

        /// The neutral world store the game pack registers its world source into (decision 0016). Exists only when a
        /// game pack that is not built in was loaded; null otherwise.
        NeutralWorld::NeutralWorldStore* neutralWorldStore();

    private:
        // Declared before the host so that they outlive it: the host's world source service points at the listener,
        // and the pack withdraws its source from the store while the lifecycle shuts down.
        std::unique_ptr<NeutralWorld::NeutralWorldStore> mNeutralWorldStore;
        std::unique_ptr<GameSession::StoreListener> mStoreListener;
        openeden::extension::HostImpl mHost;
        openeden::extension::LifecycleManager mLifecycle;
        std::vector<ExtensionInfo> mInfos;
        bool mAnyLoaded = false;
        bool mRunning = false;
        std::string mGamePackId;
        bool mGamePackBuiltIn = false;
        bool mGamePackLoaded = false;
    };
}

#endif

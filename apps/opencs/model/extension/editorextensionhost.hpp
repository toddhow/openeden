#ifndef CSM_EXTENSION_EDITORHOST_H
#define CSM_EXTENSION_EDITORHOST_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <openeden/physics.hpp>

namespace JPH
{
    class PhysicsSystem;
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
        };

        /// Finds extensions and loads the enabled ones' libraries (the handshake only). Never throws for a bad
        /// extension: that one is recorded as Failed and the others go on.
        explicit EditorExtensionHost(const Config& config);
        ~EditorExtensionHost();

        EditorExtensionHost(const EditorExtensionHost&) = delete;
        EditorExtensionHost& operator=(const EditorExtensionHost&) = delete;

        /// Gives extensions read-only queries on the editor's Jolt world (task 0030), and publishes the query masks
        /// "world" and "dynamic_world" under the values CSMPhysics::StudioWorld uses. Call it before start(), with a
        /// world that outlives the host's running extensions. \param unitsPerMeter the world's length unit.
        void attachPhysicsWorld(JPH::PhysicsSystem& world, float unitsPerMeter);

        /// Null until attachPhysicsWorld has been called.
        openeden::sdk::PhysicsQueryService* getPhysicsQueryService();

        /// Around a physics step (CSMPhysics::StudioWorld::StepObserver): beforeStep runs Phase::PreSimulation;
        /// afterStep runs Phase::PostSimulation and then Phase::Ui, which resets the per-frame scratch memory and
        /// profiler. They do nothing unless the host is running. No other phase runs in the editor (no game frame).
        void beforeStep(float dt);
        void afterStep(float dt);

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

    private:
        openeden::extension::HostImpl mHost;
        openeden::extension::LifecycleManager mLifecycle;
        std::vector<ExtensionInfo> mInfos;
        bool mAnyLoaded = false;
        bool mRunning = false;
    };
}

#endif

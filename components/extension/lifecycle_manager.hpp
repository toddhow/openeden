#pragma once

#include <components/extension/dependency_resolver.hpp>
#include <components/extension/dynamic_library.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/pack_host.hpp>
#include <components/extension/service_registry.hpp>
#include <openeden/extension.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief Coordinates extension lifecycle: registration, resolution, startup,
     * phase execution, and reverse-order shutdown.
     */
    class LifecycleManager
    {
    public:
        explicit LifecycleManager(openeden::sdk::Host& host);
        ~LifecycleManager();

        // Non-copyable
        LifecycleManager(const LifecycleManager&) = delete;
        LifecycleManager& operator=(const LifecycleManager&) = delete;

        /**
         * @brief Adds an in-process or dynamically loaded extension with its manifest.
         */
        void addExtension(ExtensionManifest manifest, std::unique_ptr<openeden::sdk::Extension> extension,
            std::unique_ptr<DynamicLibrary> library = nullptr);

        /**
         * @brief Resolves dependencies, registers services, and starts active extensions in load order.
         * @return True if initialization completed cleanly.
         */
        bool initialize();

        /**
         * @brief The first of two start phases, for a session with a game pack (task 0084 leftovers, stage I1b):
         * registers and resolves every extension, then starts only the game pack and the extensions that provide a
         * contract it requires, in load order. A later initialize() starts the rest and does not start these again.
         * Returns false and does nothing when no game pack was added, or when initialize() or this call has already
         * run; then initialize() behaves exactly as it always did.
         */
        bool initializeGamePack();

        /// True if a game pack (manifest kind "game") was added. Valid before and after initialize().
        bool hasGamePack() const;

        /**
         * @brief Runs a frame phase, calling registered callbacks in load order.
         * Exceptions thrown by extensions are caught at the boundary and isolate the failing extension.
         */
        void runPhase(openeden::sdk::Phase phase, float dt);

        /**
         * @brief Stops all active extensions in reverse load order. Idempotent.
         */
        void shutdown();

        /**
         * @brief Sets a function shutdown() calls when extensions are stopped and destroyed, just before unloading
         * them.
         */
        void setBeforeUnload(std::function<void()> callback) { mBeforeUnload = std::move(callback); }

        /// A plain function the manager calls at the end of Phase::Ui, after the phase's callbacks and BEFORE the
        /// end-of-frame reset of debug draw and the profiler, so the engine can copy what extensions submitted this
        /// frame (task 0017 stage 5). A function pointer and a context, not a std::function: it is called every frame
        /// and must not allocate. Pass nullptr to remove it. Main thread.
        using FrameEndHook = void (*)(void* context, float dt);
        void setFrameEndHook(FrameEndHook hook, void* context) noexcept
        {
            mFrameEndHook = hook;
            mFrameEndContext = context;
        }

        /// Off by default. When on, runPhase times every extension callback and records it in the profiler service as
        /// the scope phaseScopeName(phase) under the extension's ID (two clock reads and one profiler sample per
        /// callback). The profiler overlay turns it on.
        void setPhaseTiming(bool enabled) noexcept { mPhaseTiming = enabled; }
        [[nodiscard]] bool phaseTiming() const noexcept { return mPhaseTiming; }

        /// The profiler scope name of a phase's callbacks: "phase.Input", "phase.PreSimulation", ...
        [[nodiscard]] static const char* phaseScopeName(openeden::sdk::Phase phase) noexcept;

        /**
         * @brief Checks if an extension is currently running and active.
         */
        bool isExtensionActive(std::string_view id) const;

        /**
         * @brief Checks if an extension faulted during execution (e.g. threw an exception).
         */
        bool isExtensionFaulted(std::string_view id) const;

        /**
         * @brief Returns the count of currently active extensions.
         */
        size_t getActiveExtensionCount() const;

        /**
         * @brief Returns the determined load order of extensions.
         */
        const std::vector<std::string>& getLoadOrder() const;

        /**
         * @brief Why each failed extension failed, in the order they failed. An extension disabled because an extension
         * it needs failed has an entry of its own.
         */
        struct Failure
        {
            std::string extensionId;
            std::string stage;
            std::string reason;
        };
        const std::vector<Failure>& getFailures() const { return mFailures; }

        /**
         * @brief Returns the list of disabled extensions.
         */
        const std::vector<std::string>& getDisabledExtensions() const;

        /**
         * @brief Requests an extension made for something its kind may not have: a game service from the Host it was
         * given, or a game phase callback. The request got nullptr (or was ignored) and is listed here, located.
         */
        const std::vector<CapabilityDenial>& getCapabilityDenials() const { return mCapabilityLog.denials(); }

        /**
         * @brief The ID of the session's game pack (manifest kind "game"), or empty if none was added. There is one:
         * a second game pack is refused in initialize() with a failure naming both.
         */
        const std::string& getGamePackId() const { return mGamePackId; }

        /// Where contracts are looked up (read only): the host asks for `openeden.game` after initialize().
        const openeden::sdk::ServiceLocator& serviceLocator() const { return mRegistry; }

        /// The game pack's manifest "extensionVersion" ("major.minor.patch"), recorded in the save header. Empty with
        /// no game pack.
        const std::string& getGamePackVersion() const { return mGamePackVersion; }

        /**
         * @brief True once the game pack has failed at any stage (design doc 7: it is the game, so the session cannot
         * go on). The message names the pack and the stage. Saving is refused from then on (isSavingAllowed()).
         */
        bool hasSessionError() const { return !mSessionError.empty(); }
        const std::string& getSessionError() const { return mSessionError; }
        bool isSavingAllowed() const { return mSessionError.empty(); }

    private:
        void prepare();
        void startPending(const std::set<std::string>* only);
        void markExtensionFaulted(const std::string& extensionId, const char* stage, const char* reason = nullptr);
        void processDeferredFaults();
        void noteGamePackFailure(const std::string& extensionId, std::string_view stage, std::string_view reason);

        openeden::sdk::Host& mHost;
        ServiceRegistry mRegistry;
        std::function<void()> mBeforeUnload;
        FrameEndHook mFrameEndHook = nullptr;
        void* mFrameEndContext = nullptr;
        bool mPhaseTiming = false;
        std::vector<Failure> mFailures;

        struct ExtensionRecord
        {
            ExtensionManifest manifest;
            std::unique_ptr<DynamicLibrary> library;
            std::unique_ptr<PackScopedHost> scopedHost; // what start() is given; declared before instance: outlives it
            std::unique_ptr<openeden::sdk::Extension> instance;
            openeden::sdk::PackKind kind = openeden::sdk::PackKind::Feature;
            bool started = false;
        };

        std::map<std::string, ExtensionRecord> mExtensions;
        std::vector<std::string> mLoadOrder;
        std::vector<std::string> mStartedOrder;
        std::set<std::string> mActiveExtensions;
        std::set<std::string> mDisabledExtensions;
        std::set<std::string> mFaultedExtensions;
        std::vector<std::string> mDisabledExtensionsList;
        std::vector<std::string> mPendingFaultedExtensions;
        CapabilityLog mCapabilityLog;
        std::string mGamePackId;
        std::string mGamePackVersion;
        std::string mSessionError;
        bool mInitialized = false;
        bool mPrepared = false;
    };
} // namespace openeden::extension

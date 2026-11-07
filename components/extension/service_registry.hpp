#pragma once

#include <components/extension/pack_host.hpp>
#include <openeden/pack_kind.hpp>
#include <openeden/phases.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>
#include <openeden/version.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    struct FrameCallbackEntry
    {
        std::string extensionId;
        openeden::sdk::PhaseCallback callback;
        bool parallel = false;
        bool game = false; ///< Registered by a game pack: runs before the other kinds' callbacks of the same phase
    };

    struct RegisteredService
    {
        std::string extensionId;
        openeden::sdk::Version version;
        void* implPointer = nullptr;
    };

    /**
     * @brief Concrete implementation of Registrar and ServiceLocator.
     */
    class ServiceRegistry : public openeden::sdk::Registrar, public openeden::sdk::ServiceLocator
    {
    public:
        ServiceRegistry() = default;
        ~ServiceRegistry() override = default;

        // Registrar implementation
        void registerPhaseCallback(
            openeden::sdk::Phase phase, openeden::sdk::PhaseCallback callback, bool parallel = false) override;

        // ServiceLocator implementation
        void* findService(std::string_view contractId, openeden::sdk::Version minVersion) const override;

        // Lifecycle management helpers
        void setCurrentExtension(
            std::string extensionId, openeden::sdk::PackKind kind = openeden::sdk::PackKind::Feature);

        /// Where refused registrations (a game phase from a non-game kind) are recorded. Not owned; may be null.
        void setCapabilityLog(CapabilityLog* log) noexcept { mCapabilityLog = log; }
        void withdrawExtension(const std::string& extensionId);
        void clear();

        const std::vector<FrameCallbackEntry>& getFrameCallbacks(openeden::sdk::Phase phase) const;

    protected:
        void provideService(std::string_view contractId, openeden::sdk::Version version, void* implPointer) override;

    private:
        std::string mCurrentExtensionId;
        openeden::sdk::PackKind mCurrentKind = openeden::sdk::PackKind::Feature;
        CapabilityLog* mCapabilityLog = nullptr;

        // Key: (contractId, majorVersion) -> RegisteredService
        std::map<std::pair<std::string, std::uint16_t>, RegisteredService> mServices;

        // Phase callbacks
        std::map<openeden::sdk::Phase, std::vector<FrameCallbackEntry>> mFrameCallbacks;
    };
} // namespace openeden::extension

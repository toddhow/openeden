#pragma once

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
        void setCurrentExtension(std::string extensionId);
        void withdrawExtension(const std::string& extensionId);
        void clear();

        const std::vector<FrameCallbackEntry>& getFrameCallbacks(openeden::sdk::Phase phase) const;

    protected:
        void provideService(std::string_view contractId, openeden::sdk::Version version, void* implPointer) override;

    private:
        std::string mCurrentExtensionId;

        // Key: (contractId, majorVersion) -> RegisteredService
        std::map<std::pair<std::string, std::uint16_t>, RegisteredService> mServices;

        // Phase callbacks
        std::map<openeden::sdk::Phase, std::vector<FrameCallbackEntry>> mFrameCallbacks;
    };
} // namespace openeden::extension

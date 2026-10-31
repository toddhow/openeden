#pragma once

#include <openeden/phases.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>
#include <openeden/version.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    struct FrameCallbackEntry
    {
        std::string pluginId;
        openeden::sdk::PhaseCallback callback;
        bool parallel = false;
    };

    struct RegisteredService
    {
        std::string pluginId;
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
        void setCurrentPlugin(std::string pluginId);
        void withdrawPlugin(const std::string& pluginId);
        void clear();

        const std::vector<FrameCallbackEntry>& getFrameCallbacks(openeden::sdk::Phase phase) const;

    protected:
        void provideService(std::string_view contractId, openeden::sdk::Version version, void* implPointer) override;

    private:
        std::string mCurrentPluginId;

        // Key: (contractId, majorVersion) -> RegisteredService
        std::map<std::pair<std::string, std::uint16_t>, RegisteredService> mServices;

        // Phase callbacks
        std::map<openeden::sdk::Phase, std::vector<FrameCallbackEntry>> mFrameCallbacks;
    };
} // namespace openeden::plugin

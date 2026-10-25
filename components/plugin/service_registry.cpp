#include <components/plugin/service_registry.hpp>

#include <algorithm>

namespace openeden::plugin
{
    void ServiceRegistry::registerPhaseCallback(
        openeden::sdk::Phase phase, openeden::sdk::PhaseCallback callback, bool parallel)
    {
        mFrameCallbacks[phase].push_back({ mCurrentPluginId, std::move(callback), parallel });
    }

    void ServiceRegistry::registerPhaseCallback(
        openeden::sdk::Phase phase, openeden::sdk::FixedStepPhaseCallback callback, bool parallel)
    {
        mFixedStepCallbacks[phase].push_back({ mCurrentPluginId, std::move(callback), parallel });
    }

    void ServiceRegistry::provideService(std::string_view contractId, openeden::sdk::Version version, void* implPointer)
    {
        mServices[{ std::string(contractId), version.major }] = { mCurrentPluginId, version, implPointer };
    }

    void* ServiceRegistry::findService(std::string_view contractId, openeden::sdk::Version minVersion) const
    {
        auto it = mServices.find({ std::string(contractId), minVersion.major });
        if (it == mServices.end())
            return nullptr;

        const auto& service = it->second;
        if (openeden::sdk::isCompatible(minVersion, service.version))
        {
            return service.implPointer;
        }

        return nullptr;
    }

    void ServiceRegistry::setCurrentPlugin(std::string pluginId)
    {
        mCurrentPluginId = std::move(pluginId);
    }

    void ServiceRegistry::withdrawPlugin(const std::string& pluginId)
    {
        // Remove services provided by this plugin
        for (auto it = mServices.begin(); it != mServices.end();)
        {
            if (it->second.pluginId == pluginId)
            {
                it = mServices.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Remove callbacks registered by this plugin
        for (auto& [phase, callbacks] : mFrameCallbacks)
        {
            callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                [&](const FrameCallbackEntry& e) { return e.pluginId == pluginId; }),
                callbacks.end());
        }

        for (auto& [phase, callbacks] : mFixedStepCallbacks)
        {
            callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                [&](const FixedStepCallbackEntry& e) { return e.pluginId == pluginId; }),
                callbacks.end());
        }
    }

    void ServiceRegistry::clear()
    {
        mCurrentPluginId.clear();
        mServices.clear();
        mFrameCallbacks.clear();
        mFixedStepCallbacks.clear();
    }

    const std::vector<FrameCallbackEntry>& ServiceRegistry::getFrameCallbacks(openeden::sdk::Phase phase) const
    {
        static const std::vector<FrameCallbackEntry> empty;
        auto it = mFrameCallbacks.find(phase);
        if (it != mFrameCallbacks.end())
            return it->second;
        return empty;
    }

    const std::vector<FixedStepCallbackEntry>& ServiceRegistry::getFixedStepCallbacks(openeden::sdk::Phase phase) const
    {
        static const std::vector<FixedStepCallbackEntry> empty;
        auto it = mFixedStepCallbacks.find(phase);
        if (it != mFixedStepCallbacks.end())
            return it->second;
        return empty;
    }
} // namespace openeden::plugin

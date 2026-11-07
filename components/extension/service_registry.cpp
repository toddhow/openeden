#include <components/extension/service_registry.hpp>

#include <algorithm>

namespace openeden::extension
{
    void ServiceRegistry::registerPhaseCallback(
        openeden::sdk::Phase phase, openeden::sdk::PhaseCallback callback, bool parallel)
    {
        const bool gamePhase
            = phase == openeden::sdk::Phase::GameSimulation || phase == openeden::sdk::Phase::GameUpdateUi;
        const bool gamePack = openeden::sdk::hasGameCapabilities(mCurrentKind);
        if (gamePhase && !gamePack)
        {
            // Refused: the callback is dropped and the request is on record. The extension keeps running.
            if (mCapabilityLog != nullptr)
            {
                mCapabilityLog->add(mCurrentExtensionId, mCurrentKind,
                    phase == openeden::sdk::Phase::GameSimulation ? "game phase GameSimulation"
                                                                  : "game phase GameUpdateUi");
            }
            return;
        }
        mFrameCallbacks[phase].push_back({ mCurrentExtensionId, std::move(callback), parallel, gamePack });
    }

    void ServiceRegistry::provideService(std::string_view contractId, openeden::sdk::Version version, void* implPointer)
    {
        mServices[{ std::string(contractId), version.major }] = { mCurrentExtensionId, version, implPointer };
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

    void ServiceRegistry::setCurrentExtension(std::string extensionId, openeden::sdk::PackKind kind)
    {
        mCurrentExtensionId = std::move(extensionId);
        mCurrentKind = kind;
    }

    void ServiceRegistry::withdrawExtension(const std::string& extensionId)
    {
        // Remove services provided by this extension
        for (auto it = mServices.begin(); it != mServices.end();)
        {
            if (it->second.extensionId == extensionId)
            {
                it = mServices.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Remove callbacks registered by this extension
        for (auto& [phase, callbacks] : mFrameCallbacks)
        {
            callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                [&](const FrameCallbackEntry& e) { return e.extensionId == extensionId; }),
                callbacks.end());
        }
    }

    void ServiceRegistry::clear()
    {
        mCurrentExtensionId.clear();
        mServices.clear();
        mFrameCallbacks.clear();
    }

    const std::vector<FrameCallbackEntry>& ServiceRegistry::getFrameCallbacks(openeden::sdk::Phase phase) const
    {
        static const std::vector<FrameCallbackEntry> empty;
        auto it = mFrameCallbacks.find(phase);
        if (it != mFrameCallbacks.end())
            return it->second;
        return empty;
    }

} // namespace openeden::extension

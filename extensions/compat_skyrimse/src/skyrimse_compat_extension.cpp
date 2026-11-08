#include "compat_skyrimse/skyrimse_compat_extension.hpp"

#include <memory>

namespace compat_skyrimse
{
    void SkyrimCompatExtension::registerServices(openeden::sdk::Registrar& registrar)
    {
        registrar.provide(mGameContract);
    }

    bool SkyrimCompatExtension::resolve(const openeden::sdk::ServiceLocator& /*locator*/)
    {
        return true;
    }

    void SkyrimCompatExtension::start(openeden::sdk::Host& /*host*/) {}

    void SkyrimCompatExtension::stop() {}
}

#ifndef OPENEDEN_SKYRIMSE_NO_ENTRY
static const openeden::sdk::ExtensionDescriptor sCompatDescriptor{ openeden::sdk::kSdkVersion, "compat_skyrimse",
    []() -> std::unique_ptr<openeden::sdk::Extension> {
        return std::make_unique<compat_skyrimse::SkyrimCompatExtension>();
    } };

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sCompatDescriptor;
}
#endif

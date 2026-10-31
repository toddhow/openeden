#include "compat_morrowind/morrowind_compat_extension.hpp"

namespace compat_morrowind
{
    MorrowindCompatExtension::MorrowindCompatExtension() = default;
    MorrowindCompatExtension::~MorrowindCompatExtension() = default;

    void MorrowindCompatExtension::registerServices(openeden::sdk::Registrar& registrar)
    {
        registrar.provide(mGameContract);
        registrar.provide(mKfConverter);
    }

    bool MorrowindCompatExtension::resolve(const openeden::sdk::ServiceLocator& /*locator*/)
    {
        return true;
    }

    void MorrowindCompatExtension::start(openeden::sdk::Host& /*host*/) {}

    void MorrowindCompatExtension::stop() {}
} // namespace compat_morrowind

static const openeden::sdk::ExtensionDescriptor sDescriptor{ openeden::sdk::kVersion, "compat_morrowind",
    []() -> std::unique_ptr<openeden::sdk::Extension> {
        return std::make_unique<compat_morrowind::MorrowindCompatExtension>();
    } };

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}

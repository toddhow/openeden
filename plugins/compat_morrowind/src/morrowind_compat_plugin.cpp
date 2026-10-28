#include "compat_morrowind/morrowind_compat_plugin.hpp"

namespace compat_morrowind
{
    MorrowindCompatPlugin::MorrowindCompatPlugin() = default;
    MorrowindCompatPlugin::~MorrowindCompatPlugin() = default;

    void MorrowindCompatPlugin::registerServices(openeden::sdk::Registrar& registrar)
    {
        registrar.provide(mGameContract);
        registrar.provide(mKfConverter);
    }

    bool MorrowindCompatPlugin::resolve(const openeden::sdk::ServiceLocator& /*locator*/)
    {
        return true;
    }

    void MorrowindCompatPlugin::start(openeden::sdk::Host& /*host*/)
    {
    }

    void MorrowindCompatPlugin::stop()
    {
    }
} // namespace compat_morrowind

static const openeden::sdk::PluginDescriptor sDescriptor{
    openeden::sdk::kVersion,
    "compat_morrowind",
    []() -> std::unique_ptr<openeden::sdk::Plugin> {
        return std::make_unique<compat_morrowind::MorrowindCompatPlugin>();
    }
};

extern "C" OPENEDEN_PLUGIN_EXPORT const openeden::sdk::PluginDescriptor* openeden_plugin_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}

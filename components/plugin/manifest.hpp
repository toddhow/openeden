#pragma once

#include <openeden/version.hpp>

#include <string>
#include <vector>

#ifdef linux
#undef linux
#endif

namespace openeden::plugin
{
    /**
     * @brief Declaration of a contract dependency or provision.
     */
    struct ContractRequirement
    {
        std::string contract;
        openeden::sdk::Version version;
    };

    /**
     * @brief Platform library filenames.
     */
    struct PluginLibrary
    {
        std::string windows;
        std::string linux;
    };

    /**
     * @brief Typed plugin setting with defaults.
     */
    struct PluginSetting
    {
        std::string key;
        std::string type; // "bool", "int", "float", "string"
        bool defaultBool = false;
        int defaultInt = 0;
        double defaultDouble = 0.0;
        std::string defaultString;
    };

    using SettingDesc = PluginSetting;

    /**
     * @brief In-memory representation of plugin.json manifest.
     */
    struct PluginManifest
    {
        std::string format = "openeden.plugin";
        int version = 1;
        std::string id;
        std::string name;
        std::string pluginVersion;
        std::string kind = "feature";
        openeden::sdk::Version sdkVersion = openeden::sdk::kSdkVersion;
        std::string toolchain;
        PluginLibrary library;
        std::vector<ContractRequirement> providedContracts;
        std::vector<ContractRequirement> requiredContracts;
        std::vector<ContractRequirement> optionalContracts;
        std::vector<std::string> loadAfter;
        std::string dataPath = "data";
        std::vector<PluginSetting> settings;
        std::string licence;
    };
} // namespace openeden::plugin

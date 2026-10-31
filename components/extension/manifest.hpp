#pragma once

#include <openeden/version.hpp>

#include <string>
#include <vector>

#ifdef linux
#undef linux
#endif

namespace openeden::extension
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
    struct ExtensionLibrary
    {
        std::string windows;
        std::string linux;
    };

    /**
     * @brief Typed extension setting with defaults.
     */
    struct ExtensionSetting
    {
        std::string key;
        std::string type; // "bool", "int", "float", "string"
        bool defaultBool = false;
        int defaultInt = 0;
        double defaultDouble = 0.0;
        std::string defaultString;
    };

    using SettingDesc = ExtensionSetting;

    /**
     * @brief In-memory representation of extension.json manifest.
     */
    struct ExtensionManifest
    {
        std::string format = "openeden.extension";
        int version = 1;
        std::string id;
        std::string name;
        std::string extensionVersion;
        std::string kind = "feature";
        openeden::sdk::Version sdkVersion = openeden::sdk::kSdkVersion;
        std::string toolchain;
        ExtensionLibrary library;
        std::vector<ContractRequirement> providedContracts;
        std::vector<ContractRequirement> requiredContracts;
        std::vector<ContractRequirement> optionalContracts;
        std::vector<std::string> loadAfter;
        std::string dataPath = "data";
        std::vector<ExtensionSetting> settings;
        std::string licence;
    };
} // namespace openeden::extension

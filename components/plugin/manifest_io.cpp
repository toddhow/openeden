#include "manifest_io.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace openeden::plugin
{
    namespace
    {
        std::size_t computeLineNumber(std::string_view text, std::size_t byteOffset)
        {
            const std::size_t limit = std::min(byteOffset, text.size());
            std::size_t line = 1;
            for (std::size_t i = 0; i < limit; ++i)
            {
                if (text[i] == '\n')
                    ++line;
            }
            return line;
        }

        std::size_t findKeyLine(std::string_view text, std::string_view key)
        {
            const std::string needle = "\"" + std::string(key) + "\"";
            const auto pos = text.find(needle);
            if (pos == std::string_view::npos)
                return 1;
            return computeLineNumber(text, pos);
        }
    } // namespace

    std::optional<PluginManifest> parseManifest(std::string_view jsonText, std::string_view filename, LoadError& error)
    {
        nlohmann::ordered_json root;
        try
        {
            root = nlohmann::ordered_json::parse(jsonText);
        }
        catch (const nlohmann::json::parse_error& e)
        {
            error.file = std::string(filename);
            error.line = computeLineNumber(jsonText, e.byte);
            error.field = "";
            error.message = e.what();
            return std::nullopt;
        }

        if (!root.is_object())
        {
            error.file = std::string(filename);
            error.line = 1;
            error.field = "";
            error.message = "Manifest root must be a JSON object";
            return std::nullopt;
        }

        // Validate allowed keys at root to catch typos
        static const std::vector<std::string_view> kAllowedRootKeys
            = { "format", "version", "id", "name", "pluginVersion", "kind", "sdk", "toolchain", "library", "provides",
                  "requires", "optional", "loadAfter", "data", "settings", "licence", "license" };

        for (auto it = root.begin(); it != root.end(); ++it)
        {
            const std::string& key = it.key();
            const bool allowed = std::any_of(
                kAllowedRootKeys.begin(), kAllowedRootKeys.end(), [&key](std::string_view k) { return k == key; });
            if (!allowed)
            {
                error.file = std::string(filename);
                error.line = findKeyLine(jsonText, key);
                error.field = "/" + key;
                error.message = "unknown key \"" + key + "\"";
                return std::nullopt;
            }
        }

        // Validate required fields
        auto requireField = [&](std::string_view fieldName) -> bool {
            if (!root.contains(fieldName))
            {
                error.file = std::string(filename);
                error.line = 1;
                error.field = "/" + std::string(fieldName);
                error.message = "missing required field '" + std::string(fieldName) + "'";
                return false;
            }
            return true;
        };

        if (!requireField("format") || !requireField("version") || !requireField("id") || !requireField("name")
            || !requireField("pluginVersion") || !requireField("kind") || !requireField("sdk")
            || !requireField("toolchain"))
        {
            return std::nullopt;
        }

        PluginManifest manifest;

        // format
        if (!root["format"].is_string())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "format");
            error.field = "/format";
            error.message = "field 'format' must be a string";
            return std::nullopt;
        }
        manifest.format = root["format"].get<std::string>();
        if (manifest.format != "openeden.plugin")
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "format");
            error.field = "/format";
            error.message = "expected format 'openeden.plugin', got '" + manifest.format + "'";
            return std::nullopt;
        }

        // version
        if (!root["version"].is_number_integer())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "version");
            error.field = "/version";
            error.message = "field 'version' must be an integer";
            return std::nullopt;
        }
        manifest.version = root["version"].get<int>();
        if (manifest.version != 1)
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "version");
            error.field = "/version";
            error.message = "unsupported manifest format version " + std::to_string(manifest.version);
            return std::nullopt;
        }

        // id
        if (!root["id"].is_string())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "id");
            error.field = "/id";
            error.message = "field 'id' must be a string";
            return std::nullopt;
        }
        manifest.id = root["id"].get<std::string>();

        // name
        if (!root["name"].is_string())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "name");
            error.field = "/name";
            error.message = "field 'name' must be a string";
            return std::nullopt;
        }
        manifest.name = root["name"].get<std::string>();

        // pluginVersion
        if (!root["pluginVersion"].is_string())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "pluginVersion");
            error.field = "/pluginVersion";
            error.message = "field 'pluginVersion' must be a string";
            return std::nullopt;
        }
        manifest.pluginVersion = root["pluginVersion"].get<std::string>();

        // kind
        if (!root["kind"].is_string())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "kind");
            error.field = "/kind";
            error.message = "field 'kind' must be a string";
            return std::nullopt;
        }
        manifest.kind = root["kind"].get<std::string>();

        // sdk
        const auto& sdkVal = root["sdk"];
        if (!sdkVal.is_object() || !sdkVal.contains("major") || !sdkVal.contains("minor")
            || !sdkVal["major"].is_number_unsigned() || !sdkVal["minor"].is_number_unsigned())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "sdk");
            error.field = "/sdk";
            error.message = "field 'sdk' must be an object with unsigned 'major' and 'minor'";
            return std::nullopt;
        }
        manifest.sdkVersion.major = static_cast<std::uint16_t>(sdkVal["major"].get<unsigned>());
        manifest.sdkVersion.minor = static_cast<std::uint16_t>(sdkVal["minor"].get<unsigned>());

        // toolchain
        if (!root["toolchain"].is_string())
        {
            error.file = std::string(filename);
            error.line = findKeyLine(jsonText, "toolchain");
            error.field = "/toolchain";
            error.message = "field 'toolchain' must be a string";
            return std::nullopt;
        }
        manifest.toolchain = root["toolchain"].get<std::string>();

        // library (optional)
        if (root.contains("library"))
        {
            const auto& libVal = root["library"];
            if (libVal.is_object())
            {
                if (libVal.contains("windows") && libVal["windows"].is_string())
                    manifest.library.windows = libVal["windows"].get<std::string>();
                if (libVal.contains("linux") && libVal["linux"].is_string())
                    manifest.library.linux = libVal["linux"].get<std::string>();
            }
        }

        // Helper to parse contract arrays
        auto parseContracts = [&](std::string_view key, std::vector<ContractRequirement>& out) -> bool {
            if (!root.contains(key))
                return true;

            const auto& arr = root[key];
            if (!arr.is_array())
            {
                error.file = std::string(filename);
                error.line = findKeyLine(jsonText, key);
                error.field = "/" + std::string(key);
                error.message = "field '" + std::string(key) + "' must be an array";
                return false;
            }

            for (std::size_t i = 0; i < arr.size(); ++i)
            {
                const auto& item = arr[i];
                const std::string itemPointer = "/" + std::string(key) + "/" + std::to_string(i);

                if (!item.is_object() || !item.contains("contract") || !item["contract"].is_string())
                {
                    error.file = std::string(filename);
                    error.line = findKeyLine(jsonText, key);
                    error.field = itemPointer + "/contract";
                    error.message = "contract requirement must contain a string 'contract'";
                    return false;
                }

                if (!item.contains("version") || !item["version"].is_array())
                {
                    error.file = std::string(filename);
                    error.line = findKeyLine(jsonText, key);
                    error.field = itemPointer + "/version";
                    error.message = "contract version must be an array of [major, minor]";
                    return false;
                }

                const auto& verArr = item["version"];
                if (verArr.size() != 2 || !verArr[0].is_number_unsigned() || !verArr[1].is_number_unsigned())
                {
                    error.file = std::string(filename);
                    error.line = findKeyLine(jsonText, key);
                    error.field = itemPointer + "/version";
                    error.message = "contract version array must contain exactly 2 unsigned integers [major, minor]";
                    return false;
                }

                ContractRequirement req;
                req.contract = item["contract"].get<std::string>();
                req.version.major = static_cast<std::uint16_t>(verArr[0].get<unsigned>());
                req.version.minor = static_cast<std::uint16_t>(verArr[1].get<unsigned>());
                out.push_back(std::move(req));
            }
            return true;
        };

        if (!parseContracts("provides", manifest.providedContracts))
            return std::nullopt;
        if (!parseContracts("requires", manifest.requiredContracts))
            return std::nullopt;
        if (!parseContracts("optional", manifest.optionalContracts))
            return std::nullopt;

        // loadAfter (optional)
        if (root.contains("loadAfter"))
        {
            const auto& laVal = root["loadAfter"];
            if (!laVal.is_array())
            {
                error.file = std::string(filename);
                error.line = findKeyLine(jsonText, "loadAfter");
                error.field = "/loadAfter";
                error.message = "field 'loadAfter' must be an array of strings";
                return std::nullopt;
            }
            for (const auto& item : laVal)
            {
                if (item.is_string())
                    manifest.loadAfter.push_back(item.get<std::string>());
            }
        }

        // data (optional)
        if (root.contains("data") && root["data"].is_string())
            manifest.dataPath = root["data"].get<std::string>();

        // settings (optional)
        if (root.contains("settings"))
        {
            const auto& setArr = root["settings"];
            if (setArr.is_array())
            {
                for (const auto& item : setArr)
                {
                    if (item.is_object() && item.contains("key") && item.contains("type"))
                    {
                        PluginSetting s;
                        s.key = item["key"].get<std::string>();
                        s.type = item["type"].get<std::string>();
                        if (item.contains("default"))
                        {
                            const auto& def = item["default"];
                            if (def.is_boolean())
                                s.defaultBool = def.get<bool>();
                            else if (def.is_number_integer())
                                s.defaultInt = def.get<int>();
                            else if (def.is_number_float())
                                s.defaultDouble = def.get<double>();
                            else if (def.is_string())
                                s.defaultString = def.get<std::string>();
                        }
                        manifest.settings.push_back(std::move(s));
                    }
                }
            }
        }

        // licence / license (optional)
        if (root.contains("licence") && root["licence"].is_string())
            manifest.licence = root["licence"].get<std::string>();
        else if (root.contains("license") && root["license"].is_string())
            manifest.licence = root["license"].get<std::string>();

        return manifest;
    }

    std::string writeCanonicalManifest(const PluginManifest& manifest)
    {
        nlohmann::ordered_json j;
        j["format"] = manifest.format;
        j["version"] = manifest.version;
        j["id"] = manifest.id;
        j["name"] = manifest.name;
        j["pluginVersion"] = manifest.pluginVersion;
        j["kind"] = manifest.kind;
        j["sdk"]
            = nlohmann::ordered_json{ { "major", manifest.sdkVersion.major }, { "minor", manifest.sdkVersion.minor } };
        j["toolchain"] = manifest.toolchain;
        j["library"]
            = nlohmann::ordered_json{ { "windows", manifest.library.windows }, { "linux", manifest.library.linux } };

        auto serializeContracts = [](const std::vector<ContractRequirement>& list) {
            nlohmann::ordered_json arr = nlohmann::ordered_json::array();
            for (const auto& c : list)
            {
                nlohmann::ordered_json item;
                item["contract"] = c.contract;
                item["version"] = nlohmann::ordered_json::array({ c.version.major, c.version.minor });
                arr.push_back(item);
            }
            return arr;
        };

        j["provides"] = serializeContracts(manifest.providedContracts);
        j["requires"] = serializeContracts(manifest.requiredContracts);
        j["optional"] = serializeContracts(manifest.optionalContracts);

        if (manifest.loadAfter.empty())
            j["loadAfter"] = nlohmann::ordered_json::array();
        else
            j["loadAfter"] = manifest.loadAfter;

        j["data"] = manifest.dataPath.empty() ? "data" : manifest.dataPath;

        nlohmann::ordered_json settingsArr = nlohmann::ordered_json::array();
        for (const auto& s : manifest.settings)
        {
            nlohmann::ordered_json item;
            item["key"] = s.key;
            item["type"] = s.type;
            if (s.type == "bool")
                item["default"] = s.defaultBool;
            else if (s.type == "int")
                item["default"] = s.defaultInt;
            else if (s.type == "float" || s.type == "double")
                item["default"] = s.defaultDouble;
            else
                item["default"] = s.defaultString;
            settingsArr.push_back(item);
        }
        j["settings"] = settingsArr;

        if (!manifest.licence.empty())
            j["licence"] = manifest.licence;

        return j.dump(2);
    }

    std::optional<PluginManifest> loadManifestFile(const std::filesystem::path& path, LoadError& error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
        {
            error.file = path.string();
            error.line = 1;
            error.field = "";
            error.message = "cannot open file: " + path.string();
            return std::nullopt;
        }

        std::ostringstream ss;
        ss << in.rdbuf();
        return parseManifest(ss.str(), path.string(), error);
    }
} // namespace openeden::plugin

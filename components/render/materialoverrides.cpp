#include "materialoverrides.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>

#include <nlohmann/json.hpp>

namespace Render::Pbr
{
    namespace
    {
        constexpr const char* formatName = "openeden.material";
        constexpr int formatVersion = 1;

        int lineAt(std::string_view text, std::size_t offset)
        {
            offset = std::min(offset, text.size());
            return 1 + static_cast<int>(std::count(text.begin(), text.begin() + offset, '\n'));
        }

        // The offset of the start of the `index`-th entry: each entry has exactly one "match" key.
        std::size_t entryOffset(std::string_view text, std::size_t index)
        {
            std::size_t at = text.find("\"materials\"");
            if (at == std::string_view::npos)
                at = 0;
            std::size_t found = at;
            for (std::size_t i = 0; i <= index; ++i)
            {
                found = text.find("\"match\"", found == at && i == 0 ? at : found + 1);
                if (found == std::string_view::npos)
                    return at;
            }
            return found;
        }

        int keyLine(std::string_view text, const std::string& key, std::size_t from)
        {
            const std::size_t at = text.find("\"" + key + "\"", from);
            return lineAt(text, at == std::string_view::npos ? from : at);
        }

        struct Failure
        {
            int line;
            std::string field;
            std::string message;
        };

        std::optional<double> readNumber(const nlohmann::ordered_json& entry, const char* key, double low,
            std::string_view text, std::size_t from, std::size_t index, std::optional<Failure>& failure)
        {
            const auto it = entry.find(key);
            if (it == entry.end())
                return std::nullopt;
            const std::string field = "materials[" + std::to_string(index) + "]." + key;
            if (!it->is_number())
            {
                failure = Failure{ keyLine(text, key, from), field, "must be a number" };
                return std::nullopt;
            }
            const double value = it->get<double>();
            if (!(value >= low && value <= 1.0))
            {
                failure = Failure{ keyLine(text, key, from), field,
                    "must be between " + nlohmann::ordered_json(low).dump() + " and 1" };
                return std::nullopt;
            }
            return value;
        }

        nlohmann::ordered_json number(double value)
        {
            // 1 and 0 are written without a decimal point, as a person would; the rest as the shortest round trip.
            if (value == std::floor(value) && std::abs(value) < 1e15)
                return static_cast<long long>(value);
            return value;
        }
    }

    std::string OverrideError::describe() const
    {
        return file + ":" + std::to_string(line) + ": " + field + ": " + message;
    }

    std::optional<OverrideSet> parseOverrides(std::string_view json, std::string_view fileName, OverrideError& error)
    {
        error = {};
        error.file = std::string(fileName);
        const auto fail = [&](int line, std::string field, std::string message) -> std::optional<OverrideSet> {
            error.line = line;
            error.field = std::move(field);
            error.message = std::move(message);
            return std::nullopt;
        };

        nlohmann::ordered_json root;
        try
        {
            root = nlohmann::ordered_json::parse(json);
        }
        catch (const nlohmann::json::parse_error& e)
        {
            return fail(lineAt(json, e.byte), "(file)", e.what());
        }
        if (!root.is_object())
            return fail(1, "(file)", "the top level must be an object");

        for (const auto& [key, value] : root.items())
            if (key != "format" && key != "version" && key != "materials")
                return fail(keyLine(json, key, 0), key, "unknown key");

        if (!root.contains("format") || !root["format"].is_string() || root["format"] != formatName)
            return fail(keyLine(json, "format", 0), "format", std::string("must be \"") + formatName + "\"");
        if (!root.contains("version") || !root["version"].is_number_integer()
            || root["version"].get<int>() != formatVersion)
            return fail(keyLine(json, "version", 0), "version", "must be 1");
        if (!root.contains("materials") || !root["materials"].is_array())
            return fail(keyLine(json, "materials", 0), "materials", "must be an array");

        OverrideSet set;
        std::size_t index = 0;
        for (const auto& item : root["materials"])
        {
            const std::size_t from = entryOffset(json, index);
            const std::string prefix = "materials[" + std::to_string(index) + "]";
            if (!item.is_object())
                return fail(lineAt(json, from), prefix, "must be an object");
            for (const auto& [key, value] : item.items())
                if (key != "match" && key != "roughness" && key != "metalness" && key != "reflectance"
                    && key != "occlusion" && key != "subsurface")
                    return fail(keyLine(json, key, from), prefix + "." + key, "unknown key");

            OverrideEntry entry;
            const auto match = item.find("match");
            if (match == item.end() || !match->is_object())
                return fail(lineAt(json, from), prefix + ".match", "must be an object with one of texture or model");
            const bool hasTexture = match->contains("texture");
            const bool hasModel = match->contains("model");
            if (hasTexture == hasModel || match->size() != 1)
                return fail(keyLine(json, "match", from), prefix + ".match",
                    "must have exactly one of texture or model");
            const char* matchKey = hasTexture ? "texture" : "model";
            if (!(*match)[matchKey].is_string() || (*match)[matchKey].get<std::string>().empty())
                return fail(keyLine(json, matchKey, from), prefix + ".match." + matchKey,
                    "must be a non-empty path");
            entry.kind = hasTexture ? OverrideEntry::Kind::Texture : OverrideEntry::Kind::Model;
            entry.path = (*match)[matchKey].get<std::string>();

            std::optional<Failure> failure;
            entry.values.roughness = readNumber(item, "roughness", minRoughness, json, from, index, failure);
            if (!failure)
                entry.values.metalness = readNumber(item, "metalness", 0.0, json, from, index, failure);
            if (!failure)
                entry.values.reflectance = readNumber(item, "reflectance", 0.0, json, from, index, failure);
            if (!failure)
                entry.values.occlusion = readNumber(item, "occlusion", 0.0, json, from, index, failure);
            if (failure)
                return fail(failure->line, failure->field, failure->message);
            if (const auto it = item.find("subsurface"); it != item.end())
            {
                if (!it->is_boolean())
                    return fail(keyLine(json, "subsurface", from), prefix + ".subsurface", "must be true or false");
                entry.values.subsurface = it->get<bool>();
            }
            if (!entry.values.roughness && !entry.values.metalness && !entry.values.reflectance
                && !entry.values.occlusion && !entry.values.subsurface)
                return fail(lineAt(json, from), prefix, "needs at least one value besides match");
            set.entries.push_back(std::move(entry));
            ++index;
        }
        return set;
    }

    std::string writeCanonical(const OverrideSet& set)
    {
        nlohmann::ordered_json root = nlohmann::ordered_json::object();
        root["format"] = formatName;
        root["version"] = formatVersion;
        auto materials = nlohmann::ordered_json::array();
        for (const OverrideEntry& entry : set.entries)
        {
            nlohmann::ordered_json item = nlohmann::ordered_json::object();
            item["match"] = nlohmann::ordered_json::object();
            item["match"][entry.kind == OverrideEntry::Kind::Texture ? "texture" : "model"] = entry.path;
            if (entry.values.roughness)
                item["roughness"] = number(*entry.values.roughness);
            if (entry.values.metalness)
                item["metalness"] = number(*entry.values.metalness);
            if (entry.values.reflectance)
                item["reflectance"] = number(*entry.values.reflectance);
            if (entry.values.occlusion)
                item["occlusion"] = number(*entry.values.occlusion);
            if (entry.values.subsurface)
                item["subsurface"] = *entry.values.subsurface;
            materials.push_back(std::move(item));
        }
        root["materials"] = std::move(materials);
        return root.dump(2) + "\n";
    }

    std::string normalizePath(std::string_view path)
    {
        std::string result(path);
        for (char& c : result)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }

    namespace
    {
        // The table's key: normalised, without the folder a loader may or may not have added ("meshes/" for models,
        // "textures/" for textures), so the path in a file, a body part record and a loader's filename all meet.
        std::string tableKey(std::string_view path)
        {
            std::string key = normalizePath(path);
            for (const std::string_view prefix : { std::string_view("meshes/"), std::string_view("textures/") })
                if (key.compare(0, prefix.size(), prefix) == 0)
                {
                    key.erase(0, prefix.size());
                    break;
                }
            return key;
        }
    }

    void OverrideTable::add(const OverrideSet& set)
    {
        std::unique_lock lock(mMutex);
        for (const OverrideEntry& entry : set.entries)
        {
            auto& target = entry.kind == OverrideEntry::Kind::Texture ? mTextures : mModels;
            target[tableKey(entry.path)].push_back(entry.values);
        }
    }

    void OverrideTable::markSubsurfaceModel(std::string_view modelPath)
    {
        std::unique_lock lock(mMutex);
        mSkinModels.insert(tableKey(modelPath));
    }

    bool OverrideTable::empty() const
    {
        std::shared_lock lock(mMutex);
        return mSkinModels.empty() && mModels.empty() && mTextures.empty();
    }

    namespace
    {
        void merge(const OverrideValues& values, OverrideValues& into)
        {
            if (values.roughness)
                into.roughness = values.roughness;
            if (values.metalness)
                into.metalness = values.metalness;
            if (values.reflectance)
                into.reflectance = values.reflectance;
            if (values.occlusion)
                into.occlusion = values.occlusion;
            if (values.subsurface)
                into.subsurface = values.subsurface;
        }
    }

    OverrideValues OverrideTable::resolveValues(std::string_view modelPath, std::string_view texturePath) const
    {
        OverrideValues result;
        std::shared_lock lock(mMutex);
        const std::string model = tableKey(modelPath);
        if (mSkinModels.count(model))
            result.subsurface = true;
        if (const auto it = mModels.find(model); it != mModels.end())
            for (const OverrideValues& values : it->second)
                merge(values, result);
        if (const auto it = mTextures.find(tableKey(texturePath)); it != mTextures.end())
            for (const OverrideValues& values : it->second)
                merge(values, result);
        return result;
    }

    Surface OverrideTable::resolve(std::string_view modelPath, std::string_view texturePath, const Surface& base) const
    {
        const OverrideValues values = resolveValues(modelPath, texturePath);
        Surface result = base;
        if (values.roughness)
            result.roughness = static_cast<float>(*values.roughness);
        if (values.metalness)
            result.metalness = static_cast<float>(*values.metalness);
        if (values.reflectance)
            result.reflectance = static_cast<float>(*values.reflectance);
        if (values.occlusion)
            result.occlusion = static_cast<float>(*values.occlusion);
        if (values.subsurface)
            result.subsurface = *values.subsurface;
        return clampSurface(result);
    }
}

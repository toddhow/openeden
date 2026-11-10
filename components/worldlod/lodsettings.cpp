#include "lodsettings.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <system_error>

namespace WorldLod
{
    namespace
    {
        // The ranges are the sanitizers of components/settings/categories/worldlod.hpp. The key order is the order
        // the page shows them in.
        std::vector<SettingSpec> makeSpecs()
        {
            using K = SettingKind;
            std::vector<SettingSpec> specs;
            auto add = [&specs](std::string_view key, K kind, std::string_view def, double min, double max,
                           std::string_view group, std::string_view needs, std::vector<std::string_view> choices = {}) {
                specs.push_back({ key, kind, def, min, max, std::move(choices), group, needs });
            };

            add("enabled", K::Bool, "false", 0, 0, "General", "");
            add("terrain tile threshold", K::Float, "8", 0.01, 1000000, "General", "enabled");
            add("terrain fade time", K::Float, "0.4", 0, 60, "General", "enabled");
            add("view distance", K::DistanceOrAuto, "40000", 1, 10000000, "General", "enabled");

            add("map view", K::Bool, "false", 0, 0, "Whole map view", "enabled");
            add("max view distance", K::Float, "600000", 1, 10000000, "Whole map view", "map view");
            add("haze", K::Bool, "true", 0, 0, "Whole map view", "map view");
            add("haze visibility", K::Float, "100000", 1, 10000000, "Whole map view", "map view");
            add("haze height", K::Float, "4000", 1, 1000000, "Whole map view", "map view");
            add("haze weather gain", K::Float, "3", 0, 100, "Whole map view", "map view");
            add("tier", K::Choice, "auto", 0, 0, "Whole map view", "map view",
                { "auto", "minimal", "low", "medium", "high" });
            add("far levels", K::Int, "0", 0, 6, "Whole map view", "map view");
            add("far texture max size", K::Int, "0", 0, 8192, "Whole map view", "map view");
            add("far memory budget", K::Int, "0", 0, 65536, "Whole map view", "map view");

            add("object clusters", K::Bool, "false", 0, 0, "Far objects", "enabled");
            add("object cluster threshold", K::Float, "8", 0.01, 1000000, "Far objects", "object clusters");
            add("object cluster fade time", K::Float, "0.4", 0, 60, "Far objects", "object clusters");
            add("object cluster effects", K::Bool, "false", 0, 0, "Far objects", "object clusters");
            add("object cluster scenery", K::Bool, "false", 0, 0, "Far objects", "object clusters");
            add("object cluster sway", K::Bool, "false", 0, 0, "Far objects", "object clusters");
            add("object cluster sway models", K::Text, "tree, bush, reed, weed, fern, palm, kelp, grass", 0, 0,
                "Far objects", "object cluster sway");
            add("object cluster sway lean", K::Float, "0.04", 0, 0.1, "Far objects", "object cluster sway");
            add("object cluster sway wind", K::Float, "1", 0, 4, "Far objects", "object cluster sway");
            return specs;
        }

        std::string trimmed(std::string_view text)
        {
            const std::size_t b = text.find_first_not_of(" \t");
            if (b == std::string_view::npos)
                return {};
            const std::size_t e = text.find_last_not_of(" \t");
            return std::string(text.substr(b, e - b + 1));
        }

        std::string lower(std::string text)
        {
            for (char& c : text)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return text;
        }

        std::string formatNumber(double value)
        {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
            return std::string(buffer, result.ptr);
        }

        bool parseDouble(const std::string& text, double& value)
        {
            if (text.empty())
                return false;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(value);
        }

        Normalized problem(std::string text)
        {
            Normalized result;
            result.mProblem = std::move(text);
            return result;
        }

        Normalized done(std::string text, const std::string& input)
        {
            Normalized result;
            result.mOk = true;
            result.mChanged = text != input;
            result.mText = std::move(text);
            return result;
        }
    }

    const std::vector<SettingSpec>& settingSpecs()
    {
        static const std::vector<SettingSpec> specs = makeSpecs();
        return specs;
    }

    const SettingSpec* findSpec(std::string_view key)
    {
        for (const SettingSpec& spec : settingSpecs())
            if (spec.mKey == key)
                return &spec;
        return nullptr;
    }

    Normalized normalizeSetting(const SettingSpec& spec, std::string_view rawText)
    {
        const std::string input = trimmed(rawText);
        switch (spec.mKind)
        {
            case SettingKind::Bool:
            {
                const std::string word = lower(input);
                if (word == "true" || word == "false")
                    return done(word, input);
                return problem("Use true or false.");
            }
            case SettingKind::Float:
            {
                double value = 0;
                if (!parseDouble(input, value))
                    return problem("Not a number.");
                value = std::clamp(value, spec.mMin, spec.mMax);
                return done(formatNumber(value), input);
            }
            case SettingKind::Int:
            {
                double value = 0;
                if (!parseDouble(input, value) || value != std::floor(value))
                    return problem("Not a whole number.");
                value = std::clamp(value, spec.mMin, spec.mMax);
                return done(formatNumber(value), input);
            }
            case SettingKind::Choice:
            {
                const std::string word = lower(input);
                for (const std::string_view choice : spec.mChoices)
                    if (choice == word)
                        return done(word, input);
                return problem("Not one of the choices.");
            }
            case SettingKind::DistanceOrAuto:
            {
                const std::string word = lower(input);
                if (word == "auto")
                    return done(word, input);
                double value = 0;
                if (!parseDouble(input, value) || value <= 0)
                    return problem("Use a number of units above 0, or auto.");
                value = std::min(value, spec.mMax);
                return done(formatNumber(value), input);
            }
            case SettingKind::Text:
            {
                if (input.size() > 1024)
                    return problem("Too long.");
                if (input.find_first_of("\r\n") != std::string::npos)
                    return problem("One line only.");
                std::string joined;
                std::size_t start = 0;
                bool any = false;
                while (start <= input.size())
                {
                    std::size_t comma = input.find(',', start);
                    if (comma == std::string::npos)
                        comma = input.size();
                    const std::string word = trimmed(std::string_view(input).substr(start, comma - start));
                    if (!word.empty())
                    {
                        if (any)
                            joined += ", ";
                        joined += word;
                        any = true;
                    }
                    start = comma + 1;
                }
                if (!any)
                    return problem("Give at least one word.");
                return done(std::move(joined), input);
            }
        }
        return problem("Unknown kind.");
    }

    bool isDefault(const SettingSpec& spec, std::string_view text)
    {
        const Normalized value = normalizeSetting(spec, text);
        const Normalized def = normalizeSetting(spec, spec.mDefault);
        return value.mOk && def.mOk && value.mText == def.mText;
    }

    std::filesystem::path lodFolder(const std::filesystem::path& userData)
    {
        return userData / "openeden" / "lod";
    }

    std::vector<PackInfo> describePacks(const std::filesystem::path& userData)
    {
        std::vector<PackInfo> packs;
        const std::filesystem::path folder = lodFolder(userData);
        const std::pair<PackKind, const char*> known[] = {
            { PackKind::TerrainTiles, "terrain.edenpak" },
            { PackKind::ObjectClusters, "objects.edenpak" },
        };
        for (const auto& [kind, name] : known)
        {
            PackInfo info;
            info.mKind = kind;
            info.mFileName = name;
            const std::filesystem::path file = folder / name;
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec))
            {
                info.mExists = true;
                info.mBytes = std::filesystem::file_size(file, ec);
                const auto time = std::filesystem::last_write_time(file, ec);
                if (!ec)
                {
                    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
                    info.mModifiedSeconds
                        = std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
                }
            }
            packs.push_back(std::move(info));
        }
        return packs;
    }

    Summary summarize(
        bool featureEnabled, bool childRunning, bool statusFilePresent, const std::vector<PackInfo>& packs)
    {
        if (!featureEnabled)
            return Summary::Off;
        if (childRunning)
            return Summary::RunningHere;
        if (statusFilePresent)
            return Summary::FromStatusFile;
        const auto present = std::count_if(packs.begin(), packs.end(), [](const PackInfo& p) { return p.mExists; });
        if (present == 0)
            return Summary::NotBaked;
        return static_cast<std::size_t>(present) == packs.size() ? Summary::Baked : Summary::PartlyBaked;
    }

    std::vector<std::string> bakeArguments(PackKind kind)
    {
        return { kind == PackKind::TerrainTiles ? "--bake-lod-terrain" : "--bake-lod-objects" };
    }
}

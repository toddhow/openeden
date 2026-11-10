#ifndef OPENEDEN_COMPONENTS_WORLDLOD_LODSETTINGS_H
#define OPENEDEN_COMPONENTS_WORLDLOD_LODSETTINGS_H

// The [WorldLod] keys the launcher's World LOD page edits (task 0058), as data: kind, default, range, and what each
// needs. Defaults and ranges are the ones of files/settings-default.cfg and components/settings/categories/
// worldlod.hpp (a test reads the default file and compares); the page shows them and does not define anything.
// Also what the page may say about the baked packs and how it starts the bake commands that exist today
// (`openmw --bake-lod-terrain`, `openmw --bake-lod-objects`). No Qt. Threading: pure, except describePacks, which
// does file system reads (not on the GUI thread).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WorldLod
{
    inline constexpr std::string_view sCategory = "WorldLod";

    enum class SettingKind
    {
        Bool,
        Float,
        Int,
        Choice, // one of mChoices
        Text, // free text (a list of words, or a number or "auto")
        DistanceOrAuto, // a number of units or the word "auto"
    };

    struct SettingSpec
    {
        std::string_view mKey;
        SettingKind mKind = SettingKind::Bool;
        std::string_view mDefault;
        double mMin = 0;
        double mMax = 0;
        std::vector<std::string_view> mChoices;
        std::string_view mGroup; // "General", "Whole map view", "Far objects"
        /// Key of the bool this one needs to have any effect ("" for none, e.g. "enabled"). The page greys the row.
        std::string_view mNeeds;
    };

    const std::vector<SettingSpec>& settingSpecs();
    const SettingSpec* findSpec(std::string_view key);

    struct Normalized
    {
        bool mOk = false;
        std::string mText; // the text to store
        bool mChanged = false; // the input was cut to a range or tidied
        std::string mProblem; // when !mOk
    };

    /// The engine's own rules, so that the launcher stores what the game would read: floats and integers are cut to
    /// the range (the engine does it silently), a bool is "true" or "false", a choice must be one of the choices,
    /// "auto" or a positive number for the distance, text keeps its words. Numbers are written the short way
    /// ("8", "0.4"). Not a number where one is needed is a problem, never a guess.
    Normalized normalizeSetting(const SettingSpec& spec, std::string_view text);

    bool isDefault(const SettingSpec& spec, std::string_view text);

    /// Reasons a row is greyed: its bool is off ("" when the row works). `valueOf` gives the current text of a key.
    template <class ValueOf>
    std::string unmetNeed(const SettingSpec& spec, ValueOf&& valueOf)
    {
        if (spec.mNeeds.empty())
            return {};
        return valueOf(spec.mNeeds) == "true" ? std::string() : std::string(spec.mNeeds);
    }

    // ---- the packs and the bake commands that exist ----------------------------------------------------------------

    enum class PackKind
    {
        TerrainTiles,
        ObjectClusters,
    };

    struct PackInfo
    {
        PackKind mKind = PackKind::TerrainTiles;
        std::string mFileName; // terrain.edenpak or objects.edenpak
        bool mExists = false;
        std::uint64_t mBytes = 0;
        std::int64_t mModifiedSeconds = 0; // since the Unix epoch, 0 when unknown
    };

    /// <user data>/openeden/lod, where the bake commands write and the game reads.
    std::filesystem::path lodFolder(const std::filesystem::path& userData);
    std::vector<PackInfo> describePacks(const std::filesystem::path& userData);

    /// What the page says about the bake as a whole. The bake commands that exist write no status file and take no
    /// pause or stop request, so most of the time the answer comes from the packs themselves.
    enum class Summary
    {
        Off, // the world LOD is switched off
        RunningHere, // the launcher started a bake and it has not ended; there is no progress to show
        FromStatusFile, // a baker wrote status.json: the page shows what judge() says
        Baked, // both packs exist
        PartlyBaked, // one of the two exists
        NotBaked,
    };
    Summary summarize(
        bool featureEnabled, bool childRunning, bool statusFilePresent, const std::vector<PackInfo>& packs);

    /// The command line of a bake: the arguments of `openmw` that exist (no other flag is ever produced).
    std::vector<std::string> bakeArguments(PackKind kind);
}

#endif

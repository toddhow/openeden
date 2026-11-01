#ifndef OPENEDEN_RENDER_MATERIALOVERRIDES_H
#define OPENEDEN_RENDER_MATERIALOVERRIDES_H

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "materialmath.hpp"

namespace Render::Pbr
{
    /*
     * Data-driven material overrides (task 0027, tier 2): JSON files in `openeden/materials/` (names ending in .json),
     * format `openeden.material` version 1. Values are doubles so that the canonical writer reproduces a file byte for
     * byte.
     */

    struct OverrideValues
    {
        std::optional<double> roughness;
        std::optional<double> metalness;
        std::optional<double> reflectance;
        std::optional<double> occlusion;
        std::optional<bool> subsurface;
        // Task 0044: the True PBR sets' scales. A multiplier of the map's roughness (0 to maxRoughnessScale) and the
        // dielectric F0 before the map's alpha multiplies it (0 to 1).
        std::optional<double> roughnessScale;
        std::optional<double> specularLevel;
    };

    struct OverrideEntry
    {
        enum class Kind
        {
            Texture,
            Model
        };
        Kind kind = Kind::Texture;
        std::string path; // as written in the file
        OverrideValues values;
    };

    struct OverrideSet
    {
        std::vector<OverrideEntry> entries;
    };

    struct OverrideError
    {
        std::string file;
        int line = 0;
        std::string field;
        std::string message;

        std::string describe() const;
    };

    /// Parses a file's text; on failure returns nothing and fills `error` (file, line, field, message).
    std::optional<OverrideSet> parseOverrides(std::string_view json, std::string_view fileName, OverrideError& error);

    /// Two-space indent, keys in the documented order, shortest round-trip numbers, LF, a final newline.
    std::string writeCanonical(const OverrideSet& set);

    /// Lowercase, backslashes to forward slashes: the form paths are compared in.
    std::string normalizePath(std::string_view path);

    /*
     * The loaded overrides. Precedence per value: the base, then models marked as skin, then `model` entries in the
     * order they were added, then `texture` entries in the order they were added. `add` and `markSubsurfaceModel`
     * (main thread) and `resolve` (loader threads) may run at the same time.
     */
    class OverrideTable
    {
    public:
        void add(const OverrideSet& set);
        void markSubsurfaceModel(std::string_view modelPath);
        Surface resolve(std::string_view modelPath, std::string_view texturePath, const Surface& base) const;
        /// The same layers merged but not applied to a base: a value stays unset where no layer sets it (the loaders
        /// store "derive" for those).
        OverrideValues resolveValues(std::string_view modelPath, std::string_view texturePath) const;
        bool empty() const;

    private:
        mutable std::shared_mutex mMutex;
        std::unordered_set<std::string> mSkinModels;
        std::unordered_map<std::string, std::vector<OverrideValues>> mModels;
        std::unordered_map<std::string, std::vector<OverrideValues>> mTextures;
    };
}

#endif

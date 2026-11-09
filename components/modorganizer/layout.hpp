#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_LAYOUT_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_LAYOUT_H

// A profile rendered as the lines the engine reads (task 0057, stage b): the "virtual folder" of the organizer is
// nothing more than the right data=, content=, fallback-archive=, groundcover= and extension= lines. Pure.

#include "model.hpp"

#include <map>
#include <string>
#include <vector>

namespace ModOrganizer
{
    /// What a repacked mod contributes in place of its folder (task 0064: both stand at the old position).
    struct RepackedMod
    {
        std::string mShimFolder; // plugins and other files read from disk, may be empty
        std::string mPackFile; // the .edenpak
    };

    struct LayoutOptions
    {
        /// The fixed first layer (the game data folder), shown locked, never a mod.
        std::vector<std::string> mBaseData;
        /// Mod id to repack; only used for mods whose ProfileMod::mUseRepack is set.
        std::map<std::string, RepackedMod> mRepacks;
    };

    struct DataEntry
    {
        std::string mPath;
        std::string mModId; // empty for the base layer
        bool mIsPack = false;
        bool mIsShim = false;

        bool operator==(const DataEntry&) const = default;
    };

    struct Layout
    {
        std::vector<DataEntry> mData; // ascending priority, later wins
        std::vector<std::string> mArchives; // fallback-archive names
        std::vector<std::string> mContent; // active plugins, in load order
        std::vector<std::string> mGroundcover;
        std::vector<std::string> mExtensions;
        std::vector<std::string> mProblems; // unknown mod ids, duplicates, a repack asked for but not available
    };

    Layout buildLayout(const ModLibrary& library, const Profile& profile, const LayoutOptions& options);

    /// "..." with the openmw.cfg quoting: '"' becomes '&"' and '&' becomes '&&'.
    std::string quoteConfigPath(std::string_view path);

    /// The owned lines in a fixed order: fallback-archive, data, groundcover, content, extension.
    std::vector<std::string> renderConfigLines(const Layout& layout);

    /// The path of a mod's data folder: root joined with dataPath ('/' separators), or the pack file itself.
    std::string modDataPath(const Mod& mod);
}

#endif

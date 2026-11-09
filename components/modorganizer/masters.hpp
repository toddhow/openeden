#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_MASTERS_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_MASTERS_H

// Master-dependency hints for the plugin list (task 0057, stage b, decision D16). Hints never reorder anything;
// suggestSortedOrder only returns an order that the player may accept.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ModOrganizer
{
    struct PluginInfo
    {
        std::string mName;
        bool mActive = true;
        std::vector<std::string> mMasters; // in header order
    };

    enum class HintKind
    {
        MissingMaster, // a master that is not in the list at all
        InactiveMaster, // listed but switched off
        MasterAfter, // listed after the plugin that needs it
    };

    struct Hint
    {
        HintKind mKind;
        std::string mPlugin;
        std::string mMaster;

        bool operator==(const Hint&) const = default;
    };

    /// Names compare ASCII case-insensitively. Only active plugins are checked. `available` lists names the game
    /// itself provides; they are never "missing".
    std::vector<Hint> masterHints(const std::vector<PluginInfo>& inOrder, const std::vector<std::string>& available);

    /// A stable topological order: masters first, otherwise the current order. nullopt when masters form a cycle.
    std::optional<std::vector<std::string>> suggestSortedOrder(const std::vector<PluginInfo>& inOrder);

    /// Reads the master names from a plugin header (Content::Plugin). Empty on failure, `error` says why.
    std::vector<std::string> readMasters(const std::filesystem::path& plugin, std::string& error);
}

#endif

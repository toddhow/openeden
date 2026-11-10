#ifndef CSM_MESH_SKYRIMCELLSOURCE_H
#define CSM_MESH_SKYRIMCELLSOURCE_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <components/worldmodel/store.hpp>

#include <openeden/game/world_view.hpp>

namespace CSMMesh
{
    /// The Skyrim SE world the cell viewer reads: the game pack's SkyrimWorldView over a list of plugins, behind the
    /// SDK's read-only WorldView contract, adapted by game_skyrimse::SkyrimCellSource into the core's NEUTRAL world
    /// store (task 0102, stage N2). The viewer reads cells from `store()` (kGameId), not from the view. One per
    /// process; no Qt, no GPU.
    ///
    /// Where the plugin list comes from: `configureFromEnvironment` reads OPENEDEN_SKYRIMSE_PLUGINS (a text file with
    /// one plugin path per line, in load order; blank lines and lines starting with '#' are skipped) and
    /// OPENEDEN_SKYRIMSE_DATA (the game's Data folder, for the string tables), because the editor has no load order of
    /// its own for Skyrim yet.
    class SkyrimCellSource
    {
    public:
        static SkyrimCellSource& instance();

        SkyrimCellSource();
        ~SkyrimCellSource();

        /// Replaces the world. False with `error` when no plugin could be taken.
        bool configure(const std::vector<std::filesystem::path>& plugins, const std::filesystem::path& dataFolder,
            std::string& error);
        bool configureFromEnvironment(std::string& error);

        [[nodiscard]] bool ready() const { return mView != nullptr; }
        [[nodiscard]] openeden::sdk::game::WorldView* view() { return mView; }
        /// The neutral store with the Skyrim source registered; null until configured.
        [[nodiscard]] NeutralWorld::NeutralWorldStore* store() { return mView != nullptr ? mStore : nullptr; }
        /// The game id the source is registered under.
        static constexpr const char* kGameId = "skyrimse";
        /// One line of what was loaded, for logs.
        [[nodiscard]] const std::string& summary() const { return mSummary; }

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        openeden::sdk::game::WorldView* mView = nullptr; // owned by mImpl
        NeutralWorld::NeutralWorldStore* mStore = nullptr; // owned by mImpl
        std::string mSummary;
    };
}

#endif

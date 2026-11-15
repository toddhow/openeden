#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_GAMES_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_GAMES_H

// The launcher's game choice (task 0173, revision of section 8: "the launcher will be used to select the game which
// will then set the game pack used"). Which games can be chosen, the profiles of each game, a game's first profile
// from its Data folder, and the options the launcher hands the programs it starts. No Qt.
//
// Threading: listGames, profilesOfGame, selectGame, buildFirstGameProfile and forwardedConfigArguments are pure;
// scanGamePlugins and listArchives read the file system (a worker thread).

#include "profilestore.hpp"

#include <components/contentsource/plugin.hpp>
#include <components/extension/manifest.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ModOrganizer
{
    struct GameEntry
    {
        std::string mId; ///< The game pack id; "morrowind" for the built-in entry.
        std::string mName; ///< The manifest's name; "Morrowind" for the built-in entry.
        std::string mVersion; ///< The manifest's extensionVersion; empty for the built-in entry.
        bool mBuiltIn = false;

        bool operator==(const GameEntry&) const = default;
    };

    /// The games the launcher offers: the built-in Morrowind entry first, then every manifest of kind "game" sorted
    /// by id. Manifests of other kinds are left out; a second manifest with an id already listed (and one with the id
    /// "morrowind") is ignored.
    std::vector<GameEntry> listGames(const std::vector<openeden::extension::ExtensionManifest>& manifests);

    /// Empty and "morrowind" both mean Morrowind, which writes no game= line.
    bool isMorrowind(std::string_view gameId);

    /// The profile game id for a game entry id: "" for Morrowind, the id otherwise.
    std::string profileGameId(std::string_view gameId);

    /// The game a plugin header must be guessed as (Content::Header::mGame) for a pack's first profile:
    /// "skyrimse" is SkyrimSE, "fallout4" Fallout4, "falloutnv" FalloutNV, "morrowind" Morrowind; Unknown otherwise.
    Content::GameGuess expectedHeaderGame(std::string_view gameId);

    /// The names of the profiles for a game (Profile::mGame equal to profileGameId(gameId)), in the store's order.
    std::vector<std::string> profilesOfGame(const Store& store, std::string_view gameId);

    /// Makes a game's profile active: the one Store::mLastProfileOfGame names for it when that still exists, else
    /// the game's first profile. Records the profile that was active as the last profile of its own game. False, and
    /// nothing changed, when the game has no profile.
    bool selectGame(Store& store, std::string_view gameId);

    /// One plugin file of a Data folder, with the header facts the first profile needs.
    struct GamePlugin
    {
        std::string mName;
        std::vector<std::string> mMasters;
        bool mMaster = false; ///< The header's master flag, or an .esm name.
        bool mLight = false; ///< The header's light flag, or an .esl name.

        bool operator==(const GamePlugin&) const = default;
    };

    /// A game's first profile, "Default" by default (8.5 question 1): the Data folder is its locked base layer, every
    /// plugin is active, in this order: masters, then light plugins, then the rest, each group by name (ASCII case
    /// ignored), then a stable masters-first sort (suggestSortedOrder; kept as it is when masters form a cycle). The
    /// archives (fallback-archive) are, for each plugin in that order, the archive names that begin with the plugin's
    /// name without its extension (ASCII case ignored), sorted by name, each archive once. Pure.
    ImportResult buildFirstGameProfile(std::string_view gameId, const std::string& dataFolder,
        const std::vector<GamePlugin>& plugins, const std::vector<std::string>& archiveNames,
        const std::string& profileName = "Default");

    /// The plugin files (.esm, .esp, .esl, ASCII case ignored) directly in `dataFolder` whose header
    /// (Content::Plugin) is guessed as `expected`; files with another guess are skipped, unreadable ones are named in
    /// `problems`. In name order.
    std::vector<GamePlugin> scanGamePlugins(
        const std::filesystem::path& dataFolder, Content::GameGuess expected, std::vector<std::string>& problems);

    /// The .bsa and .ba2 file names directly in `dataFolder` (ASCII case ignored), in name order.
    std::vector<std::string> listArchives(const std::filesystem::path& dataFolder);

    /// The options of the launcher's own command line that it hands to the programs it starts, in their order:
    /// --config, --replace, --user-data and --resources, in either form ("--config X" or "--config=X"), with their
    /// values. Everything else is dropped. Pure.
    std::vector<std::string> forwardedConfigArguments(const std::vector<std::string>& launcherArguments);
}

#endif

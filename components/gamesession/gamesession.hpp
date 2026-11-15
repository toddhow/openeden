#ifndef OPENEDEN_COMPONENTS_GAMESESSION_GAMESESSION_H
#define OPENEDEN_COMPONENTS_GAMESESSION_GAMESESSION_H

// The game pack rules a host of the core follows when it starts a session (task 0173, stage b): which folders are
// searched, which discovered extensions load, which game pack is refused, how the session's content list is read and
// how a registered world source reaches the neutral world store. The game (apps/openmw/extensionhost.cpp) and Eden
// Studio (apps/opencs/model/extension/editorextensionhost.cpp) both use these, so a session picks its extensions the
// same way in both (decision 0013: one game pack per session, chosen by game=; task 0084).
//
// Layer host (CI/layer_rules.txt): it reads plugin headers (components/contentsource) through the file collections
// (components/files) for the extension layer (components/extension).
//
// Threading: pure functions, and a listener called on the thread that registers or withdraws sources (the main
// thread in both hosts).

#include <components/extension/extension_loader.hpp>
#include <components/extension/game_services.hpp>
#include <components/extension/session_content.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Files
{
    class Collections;
}

namespace NeutralWorld
{
    class NeutralWorldStore;
}

namespace GameSession
{
    /// The id of the Morrowind game pack, which both programs have built in.
    inline constexpr std::string_view sMorrowindId = "morrowind";

    /// The extension search folders of a session, in search order (an earlier folder wins over a later one with the
    /// same id): <local>/extensions, <user data>/extensions, then, when `withGamePacks`, <local>/game_packs and
    /// <user data>/game_packs.
    std::vector<std::filesystem::path> searchDirectories(
        const std::filesystem::path& localFolder, const std::filesystem::path& userDataFolder, bool withGamePacks);

    struct Rules
    {
        /// Extension ids to load (extension= lines and --extension).
        std::vector<std::string> mEnabled;
        /// The session's game pack (game= or --game); empty for none.
        std::string mGamePackId;
    };

    /// The game pack is enabled without an extension= line; any other id must be in mEnabled.
    bool isEnabled(const Rules& rules, std::string_view id);

    enum class Verdict
    {
        Load, ///< Enabled and allowed: load its library.
        Duplicate, ///< The same id was found earlier in another folder: ignored.
        NotEnabled, ///< Installed, not enabled.
        RefusedGamePack, ///< Enabled, but a pack of kind "game" that is not this session's game pack.
    };

    struct ItemVerdict
    {
        Verdict mVerdict = Verdict::NotEnabled;
        /// For RefusedGamePack, the reason (refusedGamePackReason); empty otherwise.
        std::string mReason;
    };

    /// One verdict per discovered extension, in the same order. Duplicates are judged first (an id seen earlier in
    /// the list), then the enable rule, then the game pack rule. Pure.
    std::vector<ItemVerdict> classify(
        const std::vector<openeden::extension::DiscoveredExtension>& discovered, const Rules& rules);

    /// "no game= line chooses a game pack" for an empty id, else "this session's game pack is '<id>'".
    std::string refusedGamePackReason(std::string_view gamePackId);

    /// The session's content list read through the data folders: each plugin's header (Content::readSessionTag),
    /// the names not found, the files that are not plugins. See openeden::extension::collectSessionContent.
    openeden::extension::SessionContentCollection collectContent(
        const std::vector<std::string>& contentFiles, const Files::Collections& collections);

    /// A host's side of the world source service: a source the game pack registers goes into `store`, and leaves it
    /// when the pack withdraws it (by game id; the source object may be gone). The store must outlive the listener.
    class StoreListener final : public openeden::extension::WorldSourceServiceImpl::Listener
    {
    public:
        explicit StoreListener(NeutralWorld::NeutralWorldStore& store);

        bool sourceRegistered(openeden::sdk::game::WorldCellSource& source, std::string& error) override;
        void sourceWithdrawn(std::string_view gameId) override;

    private:
        NeutralWorld::NeutralWorldStore& mStore;
    };
}

#endif

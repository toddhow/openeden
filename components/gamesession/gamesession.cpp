#include "gamesession.hpp"

#include <algorithm>
#include <optional>
#include <set>

#include <components/contentsource/sessiontag.hpp>
#include <components/files/collections.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/pack_kind.hpp>

namespace GameSession
{
    std::vector<std::filesystem::path> searchDirectories(
        const std::filesystem::path& localFolder, const std::filesystem::path& userDataFolder, bool withGamePacks)
    {
        // The game's order (apps/openmw/vsgengine.cpp, Engine::prepareEngine before task 0173)
        std::vector<std::filesystem::path> result{ localFolder / "extensions", userDataFolder / "extensions" };
        if (withGamePacks)
        {
            result.push_back(localFolder / "game_packs");
            result.push_back(userDataFolder / "game_packs");
        }
        return result;
    }

    bool isEnabled(const Rules& rules, std::string_view id)
    {
        return id == rules.mGamePackId
            || std::find(rules.mEnabled.begin(), rules.mEnabled.end(), id) != rules.mEnabled.end();
    }

    std::vector<ItemVerdict> classify(
        const std::vector<openeden::extension::DiscoveredExtension>& discovered, const Rules& rules)
    {
        std::vector<ItemVerdict> result;
        result.reserve(discovered.size());
        std::set<std::string> seen;
        for (const openeden::extension::DiscoveredExtension& item : discovered)
        {
            const std::string& id = item.manifest.id;
            ItemVerdict verdict;
            if (!seen.insert(id).second)
                verdict.mVerdict = Verdict::Duplicate;
            else if (!isEnabled(rules, id))
                verdict.mVerdict = Verdict::NotEnabled;
            // One game pack per session, chosen by game= (task 0084)
            else if (item.manifest.kind == openeden::sdk::kKindGame && id != rules.mGamePackId)
            {
                verdict.mVerdict = Verdict::RefusedGamePack;
                verdict.mReason = refusedGamePackReason(rules.mGamePackId);
            }
            else
                verdict.mVerdict = Verdict::Load;
            result.push_back(std::move(verdict));
        }
        return result;
    }

    std::string refusedGamePackReason(std::string_view gamePackId)
    {
        if (gamePackId.empty())
            return "no game= line chooses a game pack";
        return "this session's game pack is '" + std::string(gamePackId) + "'";
    }

    openeden::extension::SessionContentCollection collectContent(
        const std::vector<std::string>& contentFiles, const Files::Collections& collections)
    {
        return openeden::extension::collectSessionContent(
            contentFiles,
            [&collections](const std::string& name) -> std::optional<std::filesystem::path> {
                if (!collections.doesExist(name))
                    return std::nullopt;
                return collections.getPath(name);
            },
            [](const std::filesystem::path& path,
                std::string& error) -> std::optional<openeden::extension::SessionContentHeader> {
                const std::optional<Content::SessionTag> tag = Content::readSessionTag(path, error);
                if (!tag.has_value())
                    return std::nullopt;
                return openeden::extension::SessionContentHeader{ tag->mTag, tag->mFormatVersion };
            });
    }

    StoreListener::StoreListener(NeutralWorld::NeutralWorldStore& store)
        : mStore(store)
    {
    }

    bool StoreListener::sourceRegistered(openeden::sdk::game::WorldCellSource& source, std::string& error)
    {
        return mStore.addSource(source, error);
    }

    void StoreListener::sourceWithdrawn(std::string_view gameId)
    {
        mStore.removeSource(gameId);
    }
}

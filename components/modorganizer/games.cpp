#include "games.hpp"

#include "masters.hpp"

#include <components/files/conversion.hpp>
#include <optional>

#include <openeden/pack_kind.hpp>

#include <algorithm>
#include <set>
#include <system_error>

namespace ModOrganizer
{
    namespace
    {
        constexpr std::string_view sMorrowind = "morrowind";

        std::string lower(std::string_view text)
        {
            std::string result(text);
            for (char& c : result)
            {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
            return result;
        }

        bool lessIgnoringCase(const std::string& a, const std::string& b)
        {
            const std::string la = lower(a);
            const std::string lb = lower(b);
            return la != lb ? la < lb : a < b;
        }

        std::string extensionOf(std::string_view name)
        {
            const std::size_t dot = name.rfind('.');
            return dot == std::string_view::npos ? std::string() : lower(name.substr(dot));
        }

        std::string stemOf(std::string_view name)
        {
            const std::size_t dot = name.rfind('.');
            return lower(dot == std::string_view::npos ? name : name.substr(0, dot));
        }
    }

    std::vector<GameEntry> listGames(const std::vector<openeden::extension::ExtensionManifest>& manifests)
    {
        std::vector<GameEntry> packs;
        std::set<std::string> seen{ std::string(sMorrowind) };
        for (const openeden::extension::ExtensionManifest& manifest : manifests)
        {
            if (manifest.kind != openeden::sdk::kKindGame || !seen.insert(manifest.id).second)
                continue;
            packs.push_back({ manifest.id, manifest.name, manifest.extensionVersion, false });
        }
        std::stable_sort(
            packs.begin(), packs.end(), [](const GameEntry& a, const GameEntry& b) { return a.mId < b.mId; });
        std::vector<GameEntry> result{ { std::string(sMorrowind), "Morrowind", "", true } };
        result.insert(result.end(), packs.begin(), packs.end());
        return result;
    }

    bool isMorrowind(std::string_view gameId)
    {
        return gameId.empty() || gameId == sMorrowind;
    }

    std::string profileGameId(std::string_view gameId)
    {
        return isMorrowind(gameId) ? std::string() : std::string(gameId);
    }

    Content::GameGuess expectedHeaderGame(std::string_view gameId)
    {
        if (gameId == "skyrimse")
            return Content::GameGuess::SkyrimSE;
        if (gameId == "fallout4")
            return Content::GameGuess::Fallout4;
        if (gameId == "falloutnv")
            return Content::GameGuess::FalloutNV;
        if (gameId == sMorrowind)
            return Content::GameGuess::Morrowind;
        return Content::GameGuess::Unknown;
    }

    std::vector<std::string> profilesOfGame(const Store& store, std::string_view gameId)
    {
        const std::string game = profileGameId(gameId);
        std::vector<std::string> names;
        for (const Profile& profile : store.mProfiles)
        {
            if (profile.mGame == game)
                names.push_back(profile.mName);
        }
        return names;
    }

    bool selectGame(Store& store, std::string_view gameId)
    {
        const std::string game = profileGameId(gameId);
        const std::vector<std::string> names = profilesOfGame(store, game);
        if (names.empty())
            return false;
        std::string chosen = names.front();
        const auto last = store.mLastProfileOfGame.find(game);
        if (last != store.mLastProfileOfGame.end()
            && std::find(names.begin(), names.end(), last->second) != names.end())
            chosen = last->second;
        if (const Profile* current = store.active())
            store.mLastProfileOfGame[current->mGame] = current->mName;
        store.mActive = chosen;
        store.mLastProfileOfGame[game] = chosen;
        return true;
    }

    ImportResult buildFirstGameProfile(std::string_view gameId, const std::string& dataFolder,
        const std::vector<GamePlugin>& plugins, const std::vector<std::string>& archiveNames,
        const std::string& profileName)
    {
        ImportResult result;
        result.mProfile.mName = profileName;
        result.mProfile.mGame = profileGameId(gameId);
        result.mBaseData = { dataFolder };

        // Masters, then light plugins, then the rest; each group by name
        std::vector<GamePlugin> ordered = plugins;
        const auto group = [](const GamePlugin& plugin) { return plugin.mMaster ? 0 : plugin.mLight ? 1 : 2; };
        std::stable_sort(ordered.begin(), ordered.end(), [&](const GamePlugin& a, const GamePlugin& b) {
            if (group(a) != group(b))
                return group(a) < group(b);
            return lessIgnoringCase(a.mName, b.mName);
        });
        std::vector<PluginInfo> infos;
        infos.reserve(ordered.size());
        for (const GamePlugin& plugin : ordered)
            infos.push_back({ plugin.mName, true, plugin.mMasters });
        std::vector<std::string> names;
        if (const std::optional<std::vector<std::string>> sorted = suggestSortedOrder(infos))
            names = *sorted;
        else
        {
            for (const GamePlugin& plugin : ordered)
                names.push_back(plugin.mName);
        }
        for (const std::string& name : names)
            result.mProfile.mPlugins.push_back({ name, true });

        std::vector<std::string> archives = archiveNames;
        std::sort(archives.begin(), archives.end(), lessIgnoringCase);
        std::set<std::string> taken;
        for (const std::string& plugin : names)
        {
            const std::string stem = stemOf(plugin);
            for (const std::string& archive : archives)
            {
                if (lower(archive).compare(0, stem.size(), stem) != 0 || !taken.insert(lower(archive)).second)
                    continue;
                result.mProfile.mArchives.push_back(archive);
            }
        }
        return result;
    }

    std::vector<GamePlugin> scanGamePlugins(
        const std::filesystem::path& dataFolder, Content::GameGuess expected, std::vector<std::string>& problems)
    {
        std::vector<GamePlugin> result;
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(dataFolder, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const std::string ext = extensionOf(Files::pathToUnicodeString(it->path().filename()));
            if (ext == ".esm" || ext == ".esp" || ext == ".esl")
                files.push_back(it->path());
        }
        if (ec)
            problems.push_back(Files::pathToUnicodeString(dataFolder) + ": " + ec.message());
        std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return lessIgnoringCase(Files::pathToUnicodeString(a.filename()), Files::pathToUnicodeString(b.filename()));
        });
        for (const std::filesystem::path& file : files)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(file, error);
            const std::string name = Files::pathToUnicodeString(file.filename());
            if (plugin == nullptr)
            {
                problems.push_back(error.describe());
                continue;
            }
            const Content::Header& header = plugin->header();
            if (header.mGame != expected)
                continue;
            GamePlugin entry;
            entry.mName = name;
            for (const Content::MasterFile& master : header.mMasters)
                entry.mMasters.push_back(master.mName);
            const std::string ext = extensionOf(name);
            entry.mMaster = header.isMaster() || ext == ".esm";
            entry.mLight = header.isLight() || ext == ".esl";
            result.push_back(std::move(entry));
        }
        return result;
    }

    std::vector<std::string> listArchives(const std::filesystem::path& dataFolder)
    {
        std::vector<std::string> result;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dataFolder, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const std::string name = Files::pathToUnicodeString(it->path().filename());
            const std::string ext = extensionOf(name);
            if (ext == ".bsa" || ext == ".ba2")
                result.push_back(name);
        }
        std::sort(result.begin(), result.end(), lessIgnoringCase);
        return result;
    }

    std::vector<std::string> forwardedConfigArguments(const std::vector<std::string>& launcherArguments)
    {
        static constexpr std::string_view sForwarded[] = { "--config", "--replace", "--user-data", "--resources" };
        std::vector<std::string> result;
        for (std::size_t i = 0; i < launcherArguments.size(); ++i)
        {
            const std::string& argument = launcherArguments[i];
            for (const std::string_view option : sForwarded)
            {
                if (argument == option)
                {
                    result.push_back(argument);
                    if (i + 1 < launcherArguments.size())
                        result.push_back(launcherArguments[++i]);
                    break;
                }
                if (argument.size() > option.size() && argument.compare(0, option.size(), option) == 0
                    && argument[option.size()] == '=')
                {
                    result.push_back(argument);
                    break;
                }
            }
        }
        return result;
    }
}

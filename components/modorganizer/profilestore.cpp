#include "profilestore.hpp"

#include "scanner.hpp"

#include <components/files/conversion.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace ModOrganizer
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string lowerAscii(std::string_view text)
        {
            std::string result(text);
            for (char& c : result)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return result;
        }

        bool equalNoCase(std::string_view a, std::string_view b)
        {
            return lowerAscii(a) == lowerAscii(b);
        }

        /// '/' separators, as the library stores paths.
        std::string generic(const fs::path& path)
        {
            std::string text = Files::pathToUnicodeString(path);
            std::replace(text.begin(), text.end(), '\\', '/');
            return text;
        }

        bool samePath(std::string_view a, std::string_view b)
        {
            auto strip = [](std::string_view text) {
                while (text.size() > 1 && text.back() == '/')
                    text.remove_suffix(1);
                return std::string(text);
            };
#ifdef _WIN32
            return lowerAscii(strip(a)) == lowerAscii(strip(b));
#else
            return strip(a) == strip(b);
#endif
        }

        /// The last component of a '/' or '\' separated path, without a trailing separator.
        std::string lastComponent(std::string_view path)
        {
            while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
                path.remove_suffix(1);
            const std::size_t cut = path.find_last_of("/\\");
            return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
        }

        bool endsWithNoCase(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && equalNoCase(text.substr(text.size() - suffix.size()), suffix);
        }

        std::string withoutPackSuffix(const std::string& name)
        {
            return endsWithNoCase(name, ".edenpak") ? name.substr(0, name.size() - 8) : name;
        }

        std::string tidyName(std::string name)
        {
            // A display name is plain text of a bounded size
            name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c < 0x20; }), name.end());
            if (name.size() > Limits::sMaxNameBytes)
                name.resize(Limits::sMaxNameBytes);
            return name;
        }

        std::vector<std::string> modIds(const ModLibrary& library)
        {
            std::vector<std::string> ids;
            for (const Mod& mod : library.mMods)
                ids.push_back(mod.mId);
            return ids;
        }

        bool readTextFile(const fs::path& path, std::string& text, bool& exists, std::string& error)
        {
            std::error_code ec;
            exists = fs::is_regular_file(path, ec);
            if (!exists)
                return true;
            const auto size = fs::file_size(path, ec);
            if (ec || size > Limits::sMaxFileBytes)
            {
                error = Files::pathToUnicodeString(path) + ": the file is too large or cannot be read";
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            std::ostringstream out;
            out << in.rdbuf();
            text = out.str();
            return true;
        }

        std::string writeIfChanged(const fs::path& path, const std::string& text)
        {
            std::error_code ec;
            if (fs::is_regular_file(path, ec))
            {
                bool exists = false;
                std::string old;
                std::string ignored;
                if (readTextFile(path, old, exists, ignored) && old == text)
                    return {};
            }
            if (path.has_parent_path())
                fs::create_directories(path.parent_path(), ec);
            const fs::path temp = fs::path(path).concat(".tmp");
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.flush();
                if (!out)
                {
                    out.close();
                    fs::remove(temp, ec);
                    return Files::pathToUnicodeString(path) + ": could not write the temporary file";
                }
            }
            fs::rename(temp, path, ec);
            if (ec)
            {
                fs::remove(temp, ec);
                return Files::pathToUnicodeString(path) + ": could not replace the file: " + ec.message();
            }
            return {};
        }

        std::vector<std::string> splitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::istringstream in(text);
            std::string line;
            while (std::getline(in, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                    line.pop_back();
                const std::size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos)
                    lines.push_back(line.substr(first));
            }
            return lines;
        }

        void sortProfiles(Store& store)
        {
            std::sort(store.mProfiles.begin(), store.mProfiles.end(),
                [](const Profile& a, const Profile& b) { return a.mName < b.mName; });
        }

        bool nameUsed(const Store& store, const std::string& name)
        {
            for (const Profile& profile : store.mProfiles)
                if (equalNoCase(profile.mName, name))
                    return true;
            for (const std::string& readOnly : store.mReadOnlyProfiles)
                if (equalNoCase(readOnly, name))
                    return true;
            return false;
        }

        bool hasPlugin(const Profile& profile, std::string_view name)
        {
            for (const PluginEntry& plugin : profile.mPlugins)
                if (equalNoCase(plugin.mName, name))
                    return true;
            return false;
        }

        Profile* ensureActive(Store& store)
        {
            if (Profile* profile = store.active())
                return profile;
            if (!createProfile(store, "Default"))
                return nullptr;
            store.mActive = "Default";
            return store.active();
        }
    }

    // ---- paths ---------------------------------------------------------------------------------------------------

    fs::path StorePaths::modsFile() const
    {
        return mFolder / "mods.json";
    }
    fs::path StorePaths::profilesFolder() const
    {
        return mFolder / "profiles";
    }
    fs::path StorePaths::profileFile(const std::string& name) const
    {
        return profilesFolder() / Files::pathFromUnicodeString(name + ".json");
    }
    fs::path StorePaths::activeFile() const
    {
        return mFolder / "active-profile.txt";
    }
    fs::path StorePaths::baseDataFile() const
    {
        return mFolder / "base-data.txt";
    }
    fs::path StorePaths::appliedFile(const std::string& name) const
    {
        return mFolder / "applied" / Files::pathFromUnicodeString(name + ".txt");
    }

    StorePaths storePaths(const fs::path& userConfigFolder)
    {
        return StorePaths{ userConfigFolder / "openeden" };
    }

    // ---- the store ---------------------------------------------------------------------------------------------

    Profile* Store::findProfile(const std::string& name)
    {
        for (Profile& profile : mProfiles)
            if (profile.mName == name)
                return &profile;
        return nullptr;
    }
    const Profile* Store::findProfile(const std::string& name) const
    {
        for (const Profile& profile : mProfiles)
            if (profile.mName == name)
                return &profile;
        return nullptr;
    }
    Profile* Store::active()
    {
        return findProfile(mActive);
    }
    const Profile* Store::active() const
    {
        return findProfile(mActive);
    }
    bool Store::isReadOnly(const std::string& profileName) const
    {
        return std::find(mReadOnlyProfiles.begin(), mReadOnlyProfiles.end(), profileName) != mReadOnlyProfiles.end();
    }

    Store loadStore(const StorePaths& paths)
    {
        Store store;
        std::string text;
        bool exists = false;
        std::string ioError;

        if (!readTextFile(paths.modsFile(), text, exists, ioError))
            store.mProblems.push_back(ioError);
        else if (exists)
        {
            LoadError error;
            if (auto library = readModLibrary(text, generic(paths.modsFile()), error))
                store.mLibrary = std::move(*library);
            else
            {
                store.mProblems.push_back(error.format());
                store.mLibraryReadOnly = error.mNewerVersion;
            }
        }

        std::error_code ec;
        std::vector<fs::path> files;
        if (fs::is_directory(paths.profilesFolder(), ec))
        {
            for (const auto& entry : fs::directory_iterator(paths.profilesFolder(), ec))
            {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
                    files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files)
        {
            const std::string stem = Files::pathToUnicodeString(file.stem());
            text.clear();
            if (!readTextFile(file, text, exists, ioError))
            {
                store.mProblems.push_back(ioError);
                continue;
            }
            LoadError error;
            if (auto profile = readProfile(text, generic(file), error))
            {
                profile->mName = stem; // the file name is the key; a renamed file is a renamed profile
                store.mProfiles.push_back(std::move(*profile));
            }
            else
            {
                store.mProblems.push_back(error.format());
                if (error.mNewerVersion)
                    store.mReadOnlyProfiles.push_back(stem);
            }
        }
        sortProfiles(store);

        text.clear();
        if (readTextFile(paths.activeFile(), text, exists, ioError) && exists)
        {
            const auto lines = splitLines(text);
            if (!lines.empty() && store.findProfile(lines[0]) != nullptr)
                store.mActive = lines[0];
        }
        if (store.mActive.empty() && !store.mProfiles.empty())
            store.mActive = store.mProfiles.front().mName;

        text.clear();
        if (readTextFile(paths.baseDataFile(), text, exists, ioError) && exists)
            store.mBaseData = splitLines(text);
        return store;
    }

    std::vector<std::string> saveStore(const StorePaths& paths, const Store& store)
    {
        std::vector<std::string> errors;
        auto note = [&errors](const std::string& error) {
            if (!error.empty())
                errors.push_back(error);
        };

        if (!store.mLibraryReadOnly)
            note(writeIfChanged(paths.modsFile(), writeModLibrary(store.mLibrary)));

        for (const Profile& profile : store.mProfiles)
        {
            if (store.isReadOnly(profile.mName))
                continue;
            note(writeIfChanged(paths.profileFile(profile.mName), writeProfile(profile)));
        }

        // A profile that was deleted or renamed: its file goes, but only when it is a valid profile file of ours
        std::error_code ec;
        if (fs::is_directory(paths.profilesFolder(), ec))
        {
            std::vector<fs::path> stale;
            for (const auto& entry : fs::directory_iterator(paths.profilesFolder(), ec))
            {
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".json")
                    continue;
                const std::string stem = Files::pathToUnicodeString(entry.path().stem());
                if (store.findProfile(stem) != nullptr || store.isReadOnly(stem))
                    continue;
                std::string text;
                bool exists = false;
                std::string ignored;
                LoadError error;
                if (readTextFile(entry.path(), text, exists, ignored) && readProfile(text, stem, error))
                    stale.push_back(entry.path());
            }
            for (const fs::path& file : stale)
                fs::remove(file, ec);
        }

        if (!store.mActive.empty())
            note(writeIfChanged(paths.activeFile(), store.mActive + "\n"));

        std::string base;
        for (const std::string& line : store.mBaseData)
            base += line + "\n";
        if (!base.empty() || fs::exists(paths.baseDataFile(), ec))
            note(writeIfChanged(paths.baseDataFile(), base));
        return errors;
    }

    // ---- import ------------------------------------------------------------------------------------------------

    std::string parseConfigValue(std::string_view value)
    {
        const std::size_t begin = value.find_first_not_of(" \t");
        if (begin == std::string_view::npos)
            return {};
        value.remove_prefix(begin);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
            value.remove_suffix(1);
        if (value.empty() || value.front() != '"')
            return std::string(value);
        std::string result;
        for (std::size_t i = 1; i < value.size(); ++i)
        {
            const char c = value[i];
            if (c == '&' && i + 1 < value.size())
                result.push_back(value[++i]);
            else if (c == '"')
                break;
            else
                result.push_back(c);
        }
        return result;
    }

    ConfigSnapshot snapshotFromConfigText(std::string_view text)
    {
        ConfigSnapshot snapshot;
        for (const std::string& line : ownedLines(text))
        {
            const std::size_t eq = line.find('=');
            const std::string key = line.substr(0, line.find_first_of(" \t="));
            const std::string value = parseConfigValue(std::string_view(line).substr(eq + 1));
            if (value.empty())
                continue;
            if (key == "data")
                snapshot.mData.push_back(value);
            else if (key == "content")
                snapshot.mContent.push_back(value);
            else if (key == "fallback-archive")
                snapshot.mArchives.push_back(value);
            else if (key == "groundcover")
                snapshot.mGroundcover.push_back(value);
            else if (key == "extension")
                snapshot.mExtensions.push_back(value);
        }
        return snapshot;
    }

    ImportResult importFromConfig(const ConfigSnapshot& snapshot, const std::string& profileName)
    {
        ImportResult result;
        result.mProfile.mName = profileName;
        std::vector<std::string> seenPaths;
        auto seen = [&seenPaths](const std::string& path) {
            for (const std::string& other : seenPaths)
                if (samePath(other, path))
                    return true;
            return false;
        };

        bool first = snapshot.mFirstDataIsBase;
        for (const std::string& raw : snapshot.mData)
        {
            std::string path = raw;
            std::replace(path.begin(), path.end(), '\\', '/');
            if (path.empty() || seen(path))
                continue;
            seenPaths.push_back(path);
            if (first)
            {
                result.mBaseData.push_back(path);
                first = false;
                continue;
            }
            Mod mod;
            const bool pack = endsWithNoCase(path, ".edenpak");
            mod.mKind = pack ? ModKind::Pack : ModKind::Folder;
            mod.mName = tidyName(pack ? withoutPackSuffix(lastComponent(path)) : lastComponent(path));
            if (mod.mName.empty())
                mod.mName = "mod";
            mod.mId = makeModId(mod.mName, modIds(result.mLibrary));
            mod.mRoot = path;
            result.mProfile.mMods.push_back({ mod.mId, true, false });
            result.mLibrary.mMods.push_back(std::move(mod));
        }
        for (const std::string& name : snapshot.mContent)
            if (!name.empty() && !hasPlugin(result.mProfile, name))
                result.mProfile.mPlugins.push_back({ name, true });
        result.mProfile.mArchives = snapshot.mArchives;
        result.mProfile.mGroundcover = snapshot.mGroundcover;
        result.mProfile.mExtensions = snapshot.mExtensions;
        return result;
    }

    bool importIfEmpty(Store& store, const ConfigSnapshot& snapshot)
    {
        if (!store.mProfiles.empty())
            return false;
        ImportResult imported = importFromConfig(snapshot, "Default");
        // A mod the library already knows (a profile file was lost, the library was not) keeps its id
        for (Mod& mod : imported.mLibrary.mMods)
        {
            const std::string importedId = mod.mId;
            const Mod* known = nullptr;
            for (const Mod& other : store.mLibrary.mMods)
                if (samePath(other.mRoot, mod.mRoot))
                    known = &other;
            std::string finalId;
            if (known != nullptr)
                finalId = known->mId;
            else
            {
                finalId = makeModId(mod.mName, modIds(store.mLibrary));
                Mod added = mod;
                added.mId = finalId;
                store.mLibrary.mMods.push_back(std::move(added));
            }
            for (ProfileMod& entry : imported.mProfile.mMods)
                if (entry.mId == importedId)
                    entry.mId = finalId;
        }
        store.mProfiles.push_back(std::move(imported.mProfile));
        store.mActive = store.mProfiles.back().mName;
        if (store.mBaseData.empty())
            store.mBaseData = std::move(imported.mBaseData);
        return true;
    }

    // ---- edits -------------------------------------------------------------------------------------------------

    namespace
    {
        EditResult fail(std::string error)
        {
            EditResult result;
            result.mError = std::move(error);
            return result;
        }

        const Mod* findByRoot(const ModLibrary& library, const std::string& root)
        {
            for (const Mod& mod : library.mMods)
                if (samePath(mod.mRoot, root))
                    return &mod;
            return nullptr;
        }

        EditResult finishAdd(Store& store, Mod mod, const std::vector<std::string>& plugins, EditResult result)
        {
            Profile* profile = ensureActive(store);
            if (profile == nullptr)
                return fail("There is no profile to add the mod to.");
            mod.mId = makeModId(mod.mName, modIds(store.mLibrary));
            result.mId = mod.mId;
            profile->mMods.push_back({ mod.mId, true, false });
            for (const std::string& name : plugins)
                if (!hasPlugin(*profile, name))
                    profile->mPlugins.push_back({ name, false });
            store.mLibrary.mMods.push_back(std::move(mod));
            result.mOk = true;
            return result;
        }
    }

    EditResult addFolderMod(Store& store, const fs::path& folder, const std::string& displayName)
    {
        if (store.mLibraryReadOnly)
            return fail("The mod library was written by a newer version and cannot be changed.");
        std::error_code ec;
        if (!fs::is_directory(folder, ec))
            return fail("This is not a folder: " + generic(folder));
        const std::string root = generic(folder);
        if (const Mod* known = findByRoot(store.mLibrary, root))
            return fail("This folder is already in the library as \"" + known->mName + "\".");

        Mod mod;
        mod.mKind = ModKind::Folder;
        mod.mRoot = root;
        mod.mDataPath = detectDataPath(folder);
        const std::string name = displayName.empty() ? lastComponent(root) : displayName;
        mod.mName = tidyName(name);
        if (mod.mName.empty())
            mod.mName = "mod";

        EditResult result;
        const fs::path dataRoot = mod.mDataPath.empty() ? folder : folder / Files::pathFromUnicodeString(mod.mDataPath);
        const ScanResult scan = scanDataRoot(dataRoot);
        if (scan.mRootMissing)
            return fail("The folder cannot be read: " + root);
        for (const ScanProblem& problem : scan.mProblems)
            result.mWarnings.push_back(problem.mPath + ": " + problem.mMessage);
        if (scan.mTruncated)
            result.mWarnings.push_back("The folder is very large; only part of it was looked at.");
        return finishAdd(store, std::move(mod), scan.mPlugins, std::move(result));
    }

    EditResult addPackMod(Store& store, const fs::path& file, const std::string& displayName)
    {
        if (store.mLibraryReadOnly)
            return fail("The mod library was written by a newer version and cannot be changed.");
        if (!looksLikeEdenPack(file))
            return fail("This is not an Eden Archive pack: " + generic(file));
        const std::string root = generic(file);
        if (const Mod* known = findByRoot(store.mLibrary, root))
            return fail("This pack is already in the library as \"" + known->mName + "\".");
        Mod mod;
        mod.mKind = ModKind::Pack;
        mod.mRoot = root;
        const std::string name = displayName.empty() ? withoutPackSuffix(lastComponent(root)) : displayName;
        mod.mName = tidyName(name);
        if (mod.mName.empty())
            mod.mName = "pack";
        return finishAdd(store, std::move(mod), {}, EditResult());
    }

    bool removeMod(Store& store, const std::string& id)
    {
        if (store.mLibraryReadOnly)
            return false;
        auto& mods = store.mLibrary.mMods;
        const auto it = std::find_if(mods.begin(), mods.end(), [&](const Mod& mod) { return mod.mId == id; });
        if (it == mods.end())
            return false;
        mods.erase(it);
        store.mRepacks.erase(id);
        for (Profile& profile : store.mProfiles)
        {
            auto& entries = profile.mMods;
            entries.erase(
                std::remove_if(entries.begin(), entries.end(), [&](const ProfileMod& m) { return m.mId == id; }),
                entries.end());
        }
        return true;
    }

    namespace
    {
        template <class T>
        bool moveEntry(std::vector<T>& list, std::size_t from, std::size_t to)
        {
            if (from >= list.size() || to >= list.size())
                return false;
            if (from == to)
                return true;
            T item = std::move(list[from]);
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(from));
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
            return true;
        }
    }

    bool moveMod(Profile& profile, std::size_t from, std::size_t to)
    {
        return moveEntry(profile.mMods, from, to);
    }
    bool movePlugin(Profile& profile, std::size_t from, std::size_t to)
    {
        return moveEntry(profile.mPlugins, from, to);
    }

    bool setModEnabled(Profile& profile, const std::string& id, bool enabled)
    {
        for (ProfileMod& mod : profile.mMods)
            if (mod.mId == id)
            {
                mod.mEnabled = enabled;
                return true;
            }
        return false;
    }
    bool setModUseRepack(Profile& profile, const std::string& id, bool useRepack)
    {
        for (ProfileMod& mod : profile.mMods)
            if (mod.mId == id)
            {
                mod.mUseRepack = useRepack;
                return true;
            }
        return false;
    }
    bool setPluginActive(Profile& profile, const std::string& name, bool active)
    {
        for (PluginEntry& plugin : profile.mPlugins)
            if (equalNoCase(plugin.mName, name))
            {
                plugin.mActive = active;
                return true;
            }
        return false;
    }

    PluginInventory inventoryPlugins(const ModLibrary& library, const Profile& profile)
    {
        PluginInventory inventory;
        for (const ProfileMod& entry : profile.mMods)
        {
            if (!entry.mEnabled)
                continue;
            const Mod* mod = library.find(entry.mId);
            if (mod == nullptr || mod->mKind != ModKind::Folder)
                continue;
            const fs::path root = Files::pathFromUnicodeString(modDataPath(*mod));
            const ScanResult scan = scanDataRoot(root);
            inventory.mTruncated = inventory.mTruncated || scan.mTruncated;
            for (const std::string& name : scan.mPlugins)
            {
                const std::string key = lowerAscii(name);
                inventory.mProvider[key] = mod->mId;
                inventory.mSpelling[key] = name;
            }
        }
        return inventory;
    }

    std::vector<std::string> dropMissingPlugins(
        Profile& profile, const PluginInventory& inventory, const std::vector<std::string>& keep)
    {
        std::vector<std::string> dropped;
        std::vector<PluginEntry> kept;
        for (PluginEntry& plugin : profile.mPlugins)
        {
            const std::string key = lowerAscii(plugin.mName);
            bool wanted = inventory.mProvider.count(key) != 0;
            for (const std::string& name : keep)
                wanted = wanted || equalNoCase(name, plugin.mName);
            if (wanted)
                kept.push_back(std::move(plugin));
            else
                dropped.push_back(plugin.mName);
        }
        profile.mPlugins = std::move(kept);
        return dropped;
    }

    std::size_t appendNewPlugins(Profile& profile, const PluginInventory& inventory)
    {
        std::size_t added = 0;
        for (const auto& entry : inventory.mProvider)
        {
            const std::string& key = entry.first;
            if (hasPlugin(profile, key))
                continue;
            const auto spelling = inventory.mSpelling.find(key);
            profile.mPlugins.push_back({ spelling != inventory.mSpelling.end() ? spelling->second : key, false });
            ++added;
        }
        return added;
    }

    PluginReport buildPluginReport(const Store& store, const MasterReader& readMasters)
    {
        PluginReport report;
        const Profile* profile = store.active();
        if (profile == nullptr)
            return report;

        const PluginInventory inventory = inventoryPlugins(store.mLibrary, *profile);
        report.mTruncated = inventory.mTruncated;

        std::map<std::string, fs::path> baseFiles; // lower-case name to path, later folders win
        for (const std::string& base : store.mBaseData)
        {
            const fs::path folder = Files::pathFromUnicodeString(base);
            const ScanResult scan = scanDataRoot(folder);
            report.mTruncated = report.mTruncated || scan.mTruncated;
            for (const std::string& name : scan.mPlugins)
            {
                baseFiles[lowerAscii(name)] = folder / Files::pathFromUnicodeString(name);
                if (std::find(report.mBasePlugins.begin(), report.mBasePlugins.end(), name)
                    == report.mBasePlugins.end())
                    report.mBasePlugins.push_back(name);
            }
        }

        for (const PluginEntry& plugin : profile->mPlugins)
        {
            PluginRow row;
            row.mName = plugin.mName;
            row.mActive = plugin.mActive;
            const std::string key = lowerAscii(plugin.mName);
            const auto provider = inventory.mProvider.find(key);
            if (provider != inventory.mProvider.end())
            {
                row.mProviderModId = provider->second;
                const Mod* mod = store.mLibrary.find(provider->second);
                const auto spelling = inventory.mSpelling.find(key);
                if (mod != nullptr && spelling != inventory.mSpelling.end())
                    row.mFile = Files::pathFromUnicodeString(modDataPath(*mod))
                        / Files::pathFromUnicodeString(spelling->second);
            }
            else if (const auto base = baseFiles.find(key); base != baseFiles.end())
            {
                row.mFromBase = true;
                row.mFile = base->second;
            }
            else
                row.mError = "The file was not found in the game data or in any enabled mod.";

            if (!row.mFile.empty() && readMasters)
                row.mMasters = readMasters(row.mFile, row.mError);
            report.mRows.push_back(std::move(row));
        }
        return report;
    }

    // ---- profiles ----------------------------------------------------------------------------------------------

    bool createProfile(Store& store, const std::string& name, const std::string& copyFrom)
    {
        if (!isValidProfileName(name) || nameUsed(store, name))
            return false;
        Profile profile;
        if (!copyFrom.empty())
            if (const Profile* source = store.findProfile(copyFrom))
                profile = *source;
        profile.mName = name;
        store.mProfiles.push_back(std::move(profile));
        sortProfiles(store);
        if (store.mActive.empty())
            store.mActive = name;
        return true;
    }

    bool deleteProfile(Store& store, const std::string& name)
    {
        if (store.mProfiles.size() <= 1 || store.isReadOnly(name))
            return false;
        const auto it = std::find_if(
            store.mProfiles.begin(), store.mProfiles.end(), [&](const Profile& p) { return p.mName == name; });
        if (it == store.mProfiles.end())
            return false;
        store.mProfiles.erase(it);
        if (store.mActive == name)
            store.mActive = store.mProfiles.front().mName;
        return true;
    }

    bool renameProfile(Store& store, const std::string& from, const std::string& to)
    {
        if (store.isReadOnly(from) || !isValidProfileName(to) || nameUsed(store, to))
            return false;
        Profile* profile = store.findProfile(from);
        if (profile == nullptr)
            return false;
        profile->mName = to;
        if (store.mActive == from)
            store.mActive = to;
        sortProfiles(store);
        return true;
    }

    // ---- openmw.cfg --------------------------------------------------------------------------------------------

    LayoutOptions layoutOptions(const Store& store)
    {
        LayoutOptions options;
        options.mBaseData = store.mBaseData;
        options.mRepacks = store.mRepacks;
        return options;
    }

    Layout activeLayout(const Store& store)
    {
        const Profile* profile = store.active();
        if (profile == nullptr)
            return Layout();
        return buildLayout(store.mLibrary, *profile, layoutOptions(store));
    }

    ApplyOutcome applyActiveProfile(const StorePaths& paths, const Store& store, const fs::path& configFile,
        const ApplyOptions& options, bool acceptHandEdits)
    {
        ApplyOutcome outcome;
        const Profile* profile = store.active();
        if (profile == nullptr)
        {
            outcome.mError = "There is no active profile.";
            return outcome;
        }
        const Layout layout = activeLayout(store);
        outcome.mProblems = layout.mProblems;
        outcome.mLines = renderConfigLines(layout);

        std::string existing;
        bool exists = false;
        std::string ioError;
        if (!readTextFile(configFile, existing, exists, ioError))
        {
            outcome.mError = ioError;
            return outcome;
        }

        // A hand edit only matters when this apply would change the file
        if (exists && !acceptHandEdits && rewriteOwnedLines(existing, outcome.mLines).mChanged)
        {
            std::string appliedText;
            bool appliedExists = false;
            std::string ignored;
            if (readTextFile(paths.appliedFile(profile->mName), appliedText, appliedExists, ignored) && appliedExists
                && ownedLines(existing) != splitLines(appliedText))
            {
                outcome.mHandEdited = true;
                outcome.mError
                    = "The configuration file was edited since this profile was last applied. "
                      "Applying replaces those lines (a backup copy is made first).";
                return outcome;
            }
        }

        const ApplyResult applied = applyToConfigFile(configFile, outcome.mLines, options);
        outcome.mOk = applied.mOk;
        outcome.mWritten = applied.mWritten;
        outcome.mBackup = applied.mBackup;
        outcome.mError = applied.mError;
        if (!applied.mOk)
            return outcome;

        std::string text;
        for (const std::string& line : outcome.mLines)
            text += line + "\n";
        const std::string recorded = writeIfChanged(paths.appliedFile(profile->mName), text);
        if (!recorded.empty())
            outcome.mProblems.push_back(recorded);
        return outcome;
    }
}

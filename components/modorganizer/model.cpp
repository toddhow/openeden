#include "model.hpp"

#include <components/misc/strictjson.hpp>
#include <components/vfs/entryname.hpp>

#include <algorithm>
#include <set>

namespace ModOrganizer
{
    namespace
    {
        namespace Json = Misc::StrictJson;
        using nlohmann::json;

        constexpr std::string_view sModsFormat = "openeden.mods";
        constexpr std::string_view sProfileFormat = "openeden.profile";
        constexpr int sVersion = 1;

        std::string lowerAscii(std::string_view text)
        {
            std::string result(text);
            for (char& c : result)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return result;
        }

        // A relative path made of safe components, '/' separated; empty is allowed.
        bool isSafeRelativePath(std::string_view path)
        {
            if (path.empty())
                return true;
            if (path.front() == '/' || path.back() == '/')
                return false;
            std::size_t start = 0;
            while (start <= path.size())
            {
                const std::size_t end = std::min(path.find('/', start), path.size());
                const std::string_view part = path.substr(start, end - start);
                if (part.empty() || part == "." || part == "..")
                    return false;
                if (VFS::Path::checkExtractionSafe(part).has_value())
                    return false;
                start = end + 1;
            }
            return true;
        }

        // The structure of the file is read in one place for both formats: format string, version, then the body.
        template <class Body>
        auto readFile(std::string_view text, std::string_view fileName, std::string_view format,
            std::initializer_list<std::string_view> keys, LoadError& error, Body body)
            -> decltype(body(std::declval<const Json::ObjectView&>()))
        {
            error = LoadError();
            error.mFile = std::string(fileName);
            Json::Limits limits;
            limits.mMaxBytes = Limits::sMaxFileBytes;
            Json::ParseError parseError;
            const auto parsed = Json::parse(text, limits, parseError);
            if (!parsed)
            {
                error.mLine = parseError.mLine;
                error.mMessage = parseError.mMessage;
                return std::nullopt;
            }
            try
            {
                if (!parsed->is_object())
                    throw Json::FormatError{ "", "expected an object" };
                // Look at format and version first: a newer file may have keys this reader does not know.
                const auto formatIt = parsed->find("format");
                if (formatIt == parsed->end() || !formatIt->is_string() || formatIt->get<std::string>() != format)
                    throw Json::FormatError{ "/format", "expected \"" + std::string(format) + "\"" };
                const auto versionIt = parsed->find("version");
                if (versionIt == parsed->end() || !versionIt->is_number_integer())
                    throw Json::FormatError{ "/version", "expected an integer" };
                if (versionIt->get<std::int64_t>() > sVersion)
                {
                    error.mNewerVersion = true;
                    throw Json::FormatError{ "/version",
                        "written by a newer version of OpenEden; it is shown read-only and never overwritten" };
                }
                if (versionIt->get<std::int64_t>() < 1)
                    throw Json::FormatError{ "/version", "expected 1" };
                const Json::ObjectView view(*parsed, "", keys);
                return body(view);
            }
            catch (const Json::FormatError& e)
            {
                error.mPointer = e.mPointer;
                error.mMessage = e.mMessage;
            }
            return std::nullopt;
        }

        const char* kindName(ModKind kind)
        {
            return kind == ModKind::Pack ? "pack" : "folder";
        }

        json stringArray(const std::vector<std::string>& values)
        {
            json result = json::array();
            for (const std::string& v : values)
                result.push_back(v);
            return result;
        }

        std::vector<std::string> readStringArray(const Json::ObjectView& view, std::string_view key)
        {
            std::vector<std::string> result;
            const json& array = view.array(key, Limits::sMaxListEntries);
            for (std::size_t i = 0; i < array.size(); ++i)
            {
                const std::string pointer = view.pointer(key) + "/" + std::to_string(i);
                if (!array[i].is_string())
                    throw Json::FormatError{ pointer, "expected a string" };
                std::string value = array[i].get<std::string>();
                if (value.empty() || value.size() > Limits::sMaxNameBytes || !Json::isPlainText(value))
                    throw Json::FormatError{ pointer, "expected plain text of 1 to 256 bytes" };
                result.push_back(std::move(value));
            }
            return result;
        }

        void checkUniqueNames(const std::vector<std::string>& names, const std::string& pointer)
        {
            std::set<std::string> seen;
            for (std::size_t i = 0; i < names.size(); ++i)
                if (!seen.insert(lowerAscii(names[i])).second)
                    throw Json::FormatError{ pointer + "/" + std::to_string(i), "listed twice" };
        }
    }

    std::string LoadError::format() const
    {
        std::string result = mFile;
        if (mLine != 0)
            result += ":" + std::to_string(mLine);
        if (!mPointer.empty())
            result += ": " + mPointer;
        return result + ": " + mMessage;
    }

    const Mod* ModLibrary::find(std::string_view id) const
    {
        for (const Mod& mod : mMods)
            if (mod.mId == id)
                return &mod;
        return nullptr;
    }

    bool isValidModId(std::string_view id)
    {
        if (id.empty() || id.size() > 64)
            return false;
        const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
        if (!alnum(id.front()))
            return false;
        return std::all_of(id.begin(), id.end(), [&](char c) { return alnum(c) || c == '.' || c == '_' || c == '-'; });
    }

    std::string makeModId(std::string_view name, const std::vector<std::string>& taken)
    {
        std::string slug;
        for (const char c : name)
        {
            const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            const bool keep
                = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '.' || lower == '_';
            if (keep)
                slug.push_back(lower);
            else if (!slug.empty() && slug.back() != '-' && (lower == ' ' || lower == '-'))
                slug.push_back('-');
        }
        while (!slug.empty() && (slug.back() == '-' || slug.back() == '.'))
            slug.pop_back();
        while (!slug.empty() && !isValidModId(slug.substr(0, 1)))
            slug.erase(0, 1);
        if (slug.size() > 56)
            slug.resize(56);
        while (!slug.empty() && slug.back() == '-')
            slug.pop_back();
        if (slug.empty())
            slug = "mod";
        const auto isTaken
            = [&](const std::string& id) { return std::find(taken.begin(), taken.end(), id) != taken.end(); };
        std::string candidate = slug;
        for (int n = 2; isTaken(candidate); ++n)
            candidate = slug + "-" + std::to_string(n);
        return candidate;
    }

    bool isValidProfileName(std::string_view name)
    {
        if (name.empty() || name.size() > 64 || !Json::isPlainText(name))
            return false;
        if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos)
            return false;
        return !VFS::Path::checkExtractionSafe(name).has_value();
    }

    std::optional<ModLibrary> readModLibrary(std::string_view text, std::string_view fileName, LoadError& error)
    {
        return readFile(text, fileName, sModsFormat, { "format", "version", "mods" }, error,
            [](const Json::ObjectView& view) -> std::optional<ModLibrary> {
                ModLibrary library;
                const json& mods = view.array("mods", Limits::sMaxMods);
                std::set<std::string> ids;
                for (std::size_t i = 0; i < mods.size(); ++i)
                {
                    const std::string at = view.pointer("mods") + "/" + std::to_string(i);
                    const Json::ObjectView m(mods[i], at, { "id", "name", "kind", "root", "dataPath", "notes" });
                    Mod mod;
                    mod.mId = m.str("id", 64);
                    if (!isValidModId(mod.mId))
                        throw Json::FormatError{ m.pointer("id"),
                            "not a valid id (lower case letters, digits, . _ -)" };
                    if (!ids.insert(mod.mId).second)
                        throw Json::FormatError{ m.pointer("id"), "used by an earlier mod" };
                    mod.mName = m.str("name", Limits::sMaxNameBytes);
                    const std::string kind = m.str("kind", 16);
                    if (kind == "folder")
                        mod.mKind = ModKind::Folder;
                    else if (kind == "pack")
                        mod.mKind = ModKind::Pack;
                    else
                        throw Json::FormatError{ m.pointer("kind"), "expected \"folder\" or \"pack\"" };
                    mod.mRoot = m.str("root", Limits::sMaxPathBytes);
                    if (mod.mRoot.empty())
                        throw Json::FormatError{ m.pointer("root"), "empty" };
                    mod.mDataPath = m.str("dataPath", Limits::sMaxPathBytes);
                    if (!isSafeRelativePath(mod.mDataPath))
                        throw Json::FormatError{ m.pointer("dataPath"), "not a safe relative path" };
                    mod.mNotes = m.str("notes", Limits::sMaxTextBytes);
                    library.mMods.push_back(std::move(mod));
                }
                return library;
            });
    }

    std::string writeModLibrary(const ModLibrary& library)
    {
        json mods = json::array();
        for (const Mod& mod : library.mMods)
        {
            mods.push_back({ { "id", mod.mId }, { "name", mod.mName }, { "kind", kindName(mod.mKind) },
                { "root", mod.mRoot }, { "dataPath", mod.mDataPath }, { "notes", mod.mNotes } });
        }
        json root = { { "format", std::string(sModsFormat) }, { "version", sVersion }, { "mods", std::move(mods) } };
        return Json::canonicalText(root);
    }

    std::optional<Profile> readProfile(std::string_view text, std::string_view fileName, LoadError& error)
    {
        return readFile(text, fileName, sProfileFormat,
            { "format", "version", "name", "mods", "plugins", "archives", "groundcover", "extensions" }, error,
            [](const Json::ObjectView& view) -> std::optional<Profile> {
                Profile profile;
                profile.mName = view.str("name", 64);
                if (!isValidProfileName(profile.mName))
                    throw Json::FormatError{ view.pointer("name"), "not a valid profile name" };
                const json& mods = view.array("mods", Limits::sMaxMods);
                std::set<std::string> ids;
                for (std::size_t i = 0; i < mods.size(); ++i)
                {
                    const std::string at = view.pointer("mods") + "/" + std::to_string(i);
                    const Json::ObjectView m(mods[i], at, { "id", "enabled", "useRepack" });
                    ProfileMod mod;
                    mod.mId = m.str("id", 64);
                    if (!isValidModId(mod.mId))
                        throw Json::FormatError{ m.pointer("id"), "not a valid id" };
                    if (!ids.insert(mod.mId).second)
                        throw Json::FormatError{ m.pointer("id"), "listed twice" };
                    mod.mEnabled = m.boolean("enabled", true);
                    mod.mUseRepack = m.boolean("useRepack", false);
                    profile.mMods.push_back(std::move(mod));
                }
                const json& plugins = view.array("plugins", Limits::sMaxPlugins);
                std::vector<std::string> pluginNames;
                for (std::size_t i = 0; i < plugins.size(); ++i)
                {
                    const std::string at = view.pointer("plugins") + "/" + std::to_string(i);
                    const Json::ObjectView p(plugins[i], at, { "name", "active" });
                    PluginEntry plugin;
                    plugin.mName = p.str("name", Limits::sMaxNameBytes);
                    if (plugin.mName.empty())
                        throw Json::FormatError{ p.pointer("name"), "empty" };
                    plugin.mActive = p.boolean("active", true);
                    pluginNames.push_back(plugin.mName);
                    profile.mPlugins.push_back(std::move(plugin));
                }
                checkUniqueNames(pluginNames, view.pointer("plugins"));
                profile.mArchives = readStringArray(view, "archives");
                profile.mGroundcover = readStringArray(view, "groundcover");
                profile.mExtensions = readStringArray(view, "extensions");
                return profile;
            });
    }

    std::string writeProfile(const Profile& profile)
    {
        json mods = json::array();
        for (const ProfileMod& m : profile.mMods)
            mods.push_back({ { "id", m.mId }, { "enabled", m.mEnabled }, { "useRepack", m.mUseRepack } });
        json plugins = json::array();
        for (const PluginEntry& p : profile.mPlugins)
            plugins.push_back({ { "name", p.mName }, { "active", p.mActive } });
        json root = { { "format", std::string(sProfileFormat) }, { "version", sVersion }, { "name", profile.mName },
            { "mods", std::move(mods) }, { "plugins", std::move(plugins) },
            { "archives", stringArray(profile.mArchives) }, { "groundcover", stringArray(profile.mGroundcover) },
            { "extensions", stringArray(profile.mExtensions) } };
        return Json::canonicalText(root);
    }
}

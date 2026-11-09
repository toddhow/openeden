#include "layout.hpp"

namespace ModOrganizer
{
    namespace
    {
        bool hasLineBreak(std::string_view text)
        {
            return text.find_first_of("\r\n") != std::string_view::npos;
        }

        void copyNames(const std::vector<std::string>& from, std::vector<std::string>& to, const char* what,
            std::vector<std::string>& problems)
        {
            for (const std::string& name : from)
            {
                if (name.empty() || hasLineBreak(name))
                    problems.push_back(std::string("a ") + what + " name with a line break or no text was left out");
                else
                    to.push_back(name);
            }
        }
    }

    std::string modDataPath(const Mod& mod)
    {
        if (mod.mKind == ModKind::Pack || mod.mDataPath.empty())
            return mod.mRoot;
        std::string root = mod.mRoot;
        while (!root.empty() && (root.back() == '/' || root.back() == '\\'))
            root.pop_back();
        return root + "/" + mod.mDataPath;
    }

    Layout buildLayout(const ModLibrary& library, const Profile& profile, const LayoutOptions& options)
    {
        Layout layout;
        for (const std::string& base : options.mBaseData)
            layout.mData.push_back({ base, "", false, false });

        for (const ProfileMod& entry : profile.mMods)
        {
            const Mod* mod = library.find(entry.mId);
            if (mod == nullptr)
            {
                layout.mProblems.push_back("The profile names the mod \"" + entry.mId + "\", which is not in the library");
                continue;
            }
            if (!entry.mEnabled)
                continue;
            if (hasLineBreak(mod->mRoot) || hasLineBreak(mod->mDataPath))
            {
                layout.mProblems.push_back("The mod \"" + mod->mId + "\" has a path with a line break and was left out");
                continue;
            }
            if (entry.mUseRepack)
            {
                const auto repack = options.mRepacks.find(mod->mId);
                if (repack != options.mRepacks.end() && !repack->second.mPackFile.empty()
                    && !hasLineBreak(repack->second.mPackFile) && !hasLineBreak(repack->second.mShimFolder))
                {
                    if (!repack->second.mShimFolder.empty())
                        layout.mData.push_back({ repack->second.mShimFolder, mod->mId, false, true });
                    layout.mData.push_back({ repack->second.mPackFile, mod->mId, true, false });
                    continue;
                }
                layout.mProblems.push_back(
                    "The mod \"" + mod->mId + "\" should use its repack, but none is available; the original is used");
            }
            layout.mData.push_back({ modDataPath(*mod), mod->mId, mod->mKind == ModKind::Pack, false });
        }

        copyNames(profile.mArchives, layout.mArchives, "archive", layout.mProblems);
        copyNames(profile.mGroundcover, layout.mGroundcover, "groundcover", layout.mProblems);
        copyNames(profile.mExtensions, layout.mExtensions, "extension", layout.mProblems);
        for (const PluginEntry& plugin : profile.mPlugins)
        {
            if (plugin.mName.empty() || hasLineBreak(plugin.mName))
                layout.mProblems.push_back("A plugin name with a line break or no text was left out");
            else if (plugin.mActive)
                layout.mContent.push_back(plugin.mName);
        }
        return layout;
    }

    std::string quoteConfigPath(std::string_view path)
    {
        std::string result = "\"";
        for (const char c : path)
        {
            if (c == '"' || c == '&')
                result.push_back('&');
            result.push_back(c);
        }
        result.push_back('"');
        return result;
    }

    std::vector<std::string> renderConfigLines(const Layout& layout)
    {
        std::vector<std::string> lines;
        for (const std::string& name : layout.mArchives)
            lines.push_back("fallback-archive=" + name);
        for (const DataEntry& entry : layout.mData)
            lines.push_back("data=" + quoteConfigPath(entry.mPath));
        for (const std::string& name : layout.mGroundcover)
            lines.push_back("groundcover=" + name);
        for (const std::string& name : layout.mContent)
            lines.push_back("content=" + name);
        for (const std::string& name : layout.mExtensions)
            lines.push_back("extension=" + name);
        return lines;
    }
}

#include "masters.hpp"

#include <components/contentsource/plugin.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace ModOrganizer
{
    namespace
    {
        std::string key(const std::string& name)
        {
            std::string result = name;
            for (char& c : result)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return result;
        }
    }

    std::vector<Hint> masterHints(const std::vector<PluginInfo>& inOrder, const std::vector<std::string>& available)
    {
        std::map<std::string, std::size_t> position;
        for (std::size_t i = 0; i < inOrder.size(); ++i)
            position.emplace(key(inOrder[i].mName), i);
        std::set<std::string> provided;
        for (const std::string& name : available)
            provided.insert(key(name));

        std::vector<Hint> hints;
        for (std::size_t i = 0; i < inOrder.size(); ++i)
        {
            const PluginInfo& plugin = inOrder[i];
            if (!plugin.mActive)
                continue;
            for (const std::string& master : plugin.mMasters)
            {
                const std::string k = key(master);
                const auto it = position.find(k);
                if (it == position.end())
                {
                    if (!provided.count(k))
                        hints.push_back({ HintKind::MissingMaster, plugin.mName, master });
                    continue;
                }
                if (it->second > i)
                    hints.push_back({ HintKind::MasterAfter, plugin.mName, master });
                if (!inOrder[it->second].mActive && !provided.count(k))
                    hints.push_back({ HintKind::InactiveMaster, plugin.mName, master });
            }
        }
        // Order of the hints: by plugin position, then by the kinds in the declared order of the master list.
        return hints;
    }

    std::optional<std::vector<std::string>> suggestSortedOrder(const std::vector<PluginInfo>& inOrder)
    {
        const std::size_t n = inOrder.size();
        std::map<std::string, std::size_t> position;
        for (std::size_t i = 0; i < n; ++i)
            position.emplace(key(inOrder[i].mName), i);

        std::vector<std::vector<std::size_t>> dependents(n);
        std::vector<std::size_t> waiting(n, 0);
        for (std::size_t i = 0; i < n; ++i)
        {
            std::set<std::size_t> masters;
            for (const std::string& master : inOrder[i].mMasters)
            {
                const auto it = position.find(key(master));
                if (it != position.end() && it->second != i)
                    masters.insert(it->second);
                else if (it != position.end())
                    return std::nullopt; // its own master
            }
            for (const std::size_t m : masters)
            {
                dependents[m].push_back(i);
                ++waiting[i];
            }
        }
        // Stable Kahn: always take the lowest current position that has no waiting master.
        std::vector<std::string> order;
        std::vector<bool> done(n, false);
        for (std::size_t step = 0; step < n; ++step)
        {
            std::size_t pick = n;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!done[i] && waiting[i] == 0)
                {
                    pick = i;
                    break;
                }
            }
            if (pick == n)
                return std::nullopt;
            done[pick] = true;
            order.push_back(inOrder[pick].mName);
            for (const std::size_t d : dependents[pick])
                --waiting[d];
        }
        return order;
    }

    std::vector<std::string> readMasters(const std::filesystem::path& plugin, std::string& error)
    {
        error.clear();
        Content::Error contentError;
        const auto opened = Content::Plugin::openFile(plugin, contentError);
        if (!opened)
        {
            error = contentError.mMessage.empty() ? "cannot read the plugin header" : contentError.mMessage;
            return {};
        }
        std::vector<std::string> masters;
        for (const Content::MasterFile& master : opened->header().mMasters)
            masters.push_back(master.mName);
        return masters;
    }
}

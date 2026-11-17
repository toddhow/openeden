#include <game_skyrimse/marriage.hpp>

#include <cctype>
#include <cstdint>

namespace game_skyrimse::marriage
{
    bool parseBaseIdList(std::string_view text, std::vector<FormId>& out)
    {
        out.clear();
        std::vector<FormId> ids;
        std::size_t at = 0;
        while (at < text.size())
        {
            while (at < text.size() && (text[at] == ',' || text[at] == ' ' || text[at] == '\t'))
                ++at;
            if (at >= text.size())
                break;
            std::size_t end = at;
            while (end < text.size() && text[end] != ',' && text[end] != ' ' && text[end] != '\t')
                ++end;
            std::string_view word = text.substr(at, end - at);
            at = end;
            if (word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X'))
                word.remove_prefix(2);
            if (word.empty() || word.size() > 8)
                return false;
            std::uint32_t value = 0;
            for (const char c : word)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (!std::isxdigit(u))
                    return false;
                value = value * 16 + static_cast<std::uint32_t>(std::isdigit(u) ? u - '0' : std::tolower(u) - 'a' + 10);
            }
            if (value == 0 || ids.size() >= kMaxFriends)
                return false;
            ids.push_back(static_cast<FormId>(value));
        }
        out = std::move(ids);
        return true;
    }
}

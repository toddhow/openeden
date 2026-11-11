#include "header.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace NifScene
{
    bool isSkyrimSeHeader(std::string_view fileStart)
    {
        constexpr std::string_view key = "Version ";
        const std::size_t end = std::min(fileStart.find('\n'), std::min(fileStart.size(), sMaxHeaderLine));
        const std::string_view line = fileStart.substr(0, end);
        if (std::any_of(line.begin(), line.end(), [](char c) { return c < 0x20 || c > 0x7e; }))
            return false;
        const std::size_t at = line.find(key);
        if (at == std::string_view::npos)
            return false;
        std::string_view rest = line.substr(at + key.size());
        std::array<unsigned, 4> parts{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            const auto result = std::from_chars(rest.data(), rest.data() + rest.size(), parts[i]);
            if (result.ec != std::errc())
                return false;
            rest.remove_prefix(static_cast<std::size_t>(result.ptr - rest.data()));
            if (i < 3)
            {
                if (rest.empty() || rest.front() != '.')
                    return false;
                rest.remove_prefix(1);
            }
        }
        return rest.empty() && parts == std::array<unsigned, 4>{ 20, 2, 0, 7 };
    }
}

#include "objectproject.hpp"

#include <cctype>
#include <charconv>
#include <cstddef>

namespace HavokPack
{
    namespace
    {
        std::string lowerSlashes(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
            {
                if (c == '\\')
                    c = '/';
                else
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        // The next line of `text` from `pos`, without its line end; false at the end of the text.
        bool nextLine(std::string_view text, std::size_t& pos, std::string& line)
        {
            if (pos >= text.size())
                return false;
            const std::size_t end = text.find('\n', pos);
            std::string_view piece = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
            pos = end == std::string_view::npos ? text.size() : end + 1;
            while (!piece.empty() && (piece.back() == '\r' || piece.back() == ' ' || piece.back() == '\t'))
                piece.remove_suffix(1);
            line.assign(piece);
            return true;
        }

        bool goesUp(const std::string& path)
        {
            return path.find("..") != std::string::npos || (!path.empty() && path.front() == '/')
                || path.find(':') != std::string::npos;
        }
    }

    bool parseObjectProject(std::string_view text, ObjectProject& out)
    {
        out = {};
        std::size_t pos = 0;
        std::string line;
        if (!nextLine(text, pos, line) || !nextLine(text, pos, line))
            return false;
        std::size_t count = 0;
        const auto parsed = std::from_chars(line.data(), line.data() + line.size(), count);
        if (parsed.ec != std::errc() || parsed.ptr != line.data() + line.size() || count < 3
            || count > kMaxProjectEntries)
            return false;
        std::vector<std::string> names;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!nextLine(text, pos, line) || line.empty())
                return false;
            std::string name = lowerSlashes(line);
            if (goesUp(name))
                return false;
            names.push_back(std::move(name));
        }
        out.mBehavior = names[0];
        out.mCharacter = names[1];
        out.mSkeleton = names[2];
        out.mClips.assign(names.begin() + 3, names.end());
        return true;
    }

    std::string objectProjectPath(std::string_view modelPath)
    {
        const std::string path = lowerSlashes(modelPath);
        if (path.size() < 5 || path.compare(path.size() - 4, 4, ".nif") != 0)
            return {};
        const std::size_t slash = path.rfind('/');
        const std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1,
            path.size() - 4 - (slash == std::string::npos ? 0 : slash + 1));
        if (stem.empty())
            return {};
        return "meshes/animationdata/" + stem + ".txt";
    }

    std::string objectFolder(std::string_view modelPath)
    {
        const std::string path = lowerSlashes(modelPath);
        const std::size_t slash = path.rfind('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }

    std::string clipStem(std::string_view path)
    {
        const std::string lower = lowerSlashes(path);
        const std::size_t slash = lower.rfind('/');
        std::string name = slash == std::string::npos ? lower : lower.substr(slash + 1);
        const std::size_t dot = name.rfind('.');
        if (dot != std::string::npos)
            name.resize(dot);
        return name;
    }

    std::vector<std::string> eventNamesOfClip(std::string_view stem)
    {
        std::vector<std::string> names;
        const std::string lower = lowerSlashes(stem);
        if (lower.empty())
            return names;
        names.push_back(lower);
        std::size_t digitsFrom = lower.size();
        while (digitsFrom > 0 && std::isdigit(static_cast<unsigned char>(lower[digitsFrom - 1])))
            --digitsFrom;
        const std::size_t digits = lower.size() - digitsFrom;
        if (digits >= 1 && digits <= 2 && digitsFrom > 0)
        {
            std::string number = lower.substr(digitsFrom);
            if (number.size() == 1)
                number.insert(0, "0");
            names.push_back("trigger" + number);
        }
        constexpr std::string_view down = "down";
        if (lower.size() > down.size() && lower.compare(lower.size() - down.size(), down.size(), down) == 0)
            names.push_back("open");
        return names;
    }

    std::string startStateClipEvent(std::string_view event)
    {
        const std::string lower = lowerSlashes(event);
        constexpr std::string_view prefix = "startstate";
        if (lower.size() <= prefix.size() || lower.compare(0, prefix.size(), prefix) != 0)
            return {};
        const std::string number = lower.substr(prefix.size());
        if (number.size() > 2)
            return {};
        for (char c : number)
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return {};
        return "trigger" + (number.size() == 1 ? "0" + number : number);
    }
}

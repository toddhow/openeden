#include "configfile.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>

namespace ModOrganizer
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string_view trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        struct SplitText
        {
            std::vector<std::string> mLines; // without line ends
            bool mCrlf = false;
            bool mFinalNewline = true;
        };

        SplitText split(std::string_view text)
        {
            SplitText result;
            const std::size_t firstNewline = text.find('\n');
            result.mCrlf = firstNewline != std::string_view::npos && firstNewline > 0 && text[firstNewline - 1] == '\r';
            result.mFinalNewline = text.empty() || text.back() == '\n';
            std::size_t start = 0;
            while (start < text.size())
            {
                std::size_t end = text.find('\n', start);
                const bool last = end == std::string_view::npos;
                if (last)
                    end = text.size();
                std::string_view line = text.substr(start, end - start);
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                result.mLines.emplace_back(line);
                start = end + 1;
            }
            return result;
        }

        std::string readAll(const fs::path& path, bool& exists)
        {
            std::ifstream in(path, std::ios::binary);
            exists = in.is_open();
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        std::string backupPrefix(const fs::path& path)
        {
            const auto name = path.filename().u8string();
            return std::string(name.begin(), name.end()) + ".bak-";
        }

        std::vector<std::pair<long, fs::path>> numberedBackups(const fs::path& path)
        {
            std::vector<std::pair<long, fs::path>> result;
            const std::string prefix = backupPrefix(path);
            std::error_code ec;
            const fs::path folder = path.has_parent_path() ? path.parent_path() : fs::path(".");
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            {
                const auto u8 = it->path().filename().u8string();
                const std::string name(u8.begin(), u8.end());
                if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
                    continue;
                long n = 0;
                const char* first = name.data() + prefix.size();
                const char* last = name.data() + name.size();
                const auto parsed = std::from_chars(first, last, n);
                if (parsed.ec == std::errc() && parsed.ptr == last)
                    result.emplace_back(n, it->path());
            }
            std::sort(result.begin(), result.end());
            return result;
        }
    }

    bool isOwnedLine(std::string_view line)
    {
        static const std::array<std::string_view, 5> keys
            = { "data", "content", "groundcover", "fallback-archive", "extension" };
        line = trim(line);
        for (const std::string_view key : keys)
        {
            if (line.size() < key.size() || line.compare(0, key.size(), key) != 0)
                continue;
            std::string_view rest = line.substr(key.size());
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
                rest.remove_prefix(1);
            if (!rest.empty() && rest.front() == '=')
                return true;
        }
        return false;
    }

    std::vector<std::string> ownedLines(std::string_view text)
    {
        std::vector<std::string> result;
        for (const std::string& line : split(text).mLines)
            if (isOwnedLine(line))
                result.emplace_back(trim(line));
        return result;
    }

    RewriteResult rewriteOwnedLines(std::string_view existing, const std::vector<std::string>& wanted)
    {
        const SplitText parts = split(existing);
        std::vector<std::string> out;
        bool inserted = false;
        for (const std::string& line : parts.mLines)
        {
            if (isOwnedLine(line))
            {
                if (!inserted)
                {
                    out.insert(out.end(), wanted.begin(), wanted.end());
                    inserted = true;
                }
                continue;
            }
            out.push_back(line);
        }
        if (!inserted)
            out.insert(out.end(), wanted.begin(), wanted.end());

        const std::string eol = parts.mCrlf ? "\r\n" : "\n";
        RewriteResult result;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            result.mText += out[i];
            if (i + 1 < out.size() || parts.mFinalNewline)
                result.mText += eol;
        }
        result.mChanged = result.mText != existing;
        return result;
    }

    bool differsFromProfile(std::string_view existing, const std::vector<std::string>& expected)
    {
        std::vector<std::string> trimmed;
        for (const std::string& line : expected)
            trimmed.emplace_back(trim(line));
        return ownedLines(existing) != trimmed;
    }

    std::vector<fs::path> listBackups(const fs::path& path)
    {
        std::vector<fs::path> result;
        for (const auto& entry : numberedBackups(path))
            result.push_back(entry.second);
        return result;
    }

    ApplyResult applyToConfigFile(
        const fs::path& path, const std::vector<std::string>& wanted, const ApplyOptions& options)
    {
        ApplyResult result;
        if (options.mGameRunning && options.mGameRunning())
        {
            result.mError = "The game is running; close it first. The file was not changed.";
            return result;
        }
        std::error_code ec;
        if (!options.mGlobalConfig.empty())
        {
            const fs::path a = fs::weakly_canonical(path, ec);
            const fs::path b = fs::weakly_canonical(options.mGlobalConfig, ec);
            if (a == b)
            {
                result.mError = "This is the global openmw.cfg of the installation; it is never written.";
                return result;
            }
        }
        for (const std::string& line : wanted)
        {
            if (!isOwnedLine(line) || line.find_first_of("\r\n") != std::string::npos)
            {
                result.mError = "Refused a line that is not one of the profile's own keys or has a line break.";
                return result;
            }
        }

        bool exists = false;
        const std::string existing = readAll(path, exists);
        const RewriteResult rewritten = rewriteOwnedLines(existing, wanted);
        if (!rewritten.mChanged && (exists || wanted.empty()))
        {
            result.mOk = true;
            return result;
        }

        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        if (exists)
        {
            const auto backups = numberedBackups(path);
            const long next = backups.empty() ? 1 : backups.back().first + 1;
            const fs::path backup = path.parent_path() / fs::u8path(backupPrefix(path) + std::to_string(next));
            fs::copy_file(path, backup, fs::copy_options::none, ec);
            if (ec)
            {
                result.mError = "Could not write the backup copy: " + ec.message() + ". The file was not changed.";
                return result;
            }
            result.mBackup = backup;
        }

        const fs::path temp = fs::path(path).concat(".tmp");
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(rewritten.mText.data(), static_cast<std::streamsize>(rewritten.mText.size()));
            out.flush();
            if (!out)
            {
                out.close();
                fs::remove(temp, ec);
                result.mError = "Could not write the temporary file next to the config file.";
                return result;
            }
        }
        fs::rename(temp, path, ec);
        if (ec)
        {
            fs::remove(temp, ec);
            result.mError = "Could not replace the config file: " + ec.message();
            return result;
        }

        const int keep = std::max(1, options.mKeepBackups);
        auto backups = numberedBackups(path);
        while (static_cast<int>(backups.size()) > keep)
        {
            fs::remove(backups.front().second, ec);
            backups.erase(backups.begin());
        }
        result.mOk = true;
        result.mWritten = true;
        return result;
    }
}

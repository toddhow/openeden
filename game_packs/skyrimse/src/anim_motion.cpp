#include "game_skyrimse/anim_motion.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>

namespace game_skyrimse
{
    namespace
    {
        constexpr double kTwoPi = 6.283185307179586;

        std::vector<std::string_view> splitLines(std::string_view text)
        {
            std::vector<std::string_view> lines;
            std::size_t start = 0;
            while (start <= text.size())
            {
                std::size_t end = text.find('\n', start);
                if (end == std::string_view::npos)
                    end = text.size();
                std::string_view line = text.substr(start, end - start);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                    line.remove_suffix(1);
                while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                    line.remove_prefix(1);
                lines.push_back(line);
                start = end + 1;
            }
            return lines;
        }

        bool toDouble(std::string_view token, double& out)
        {
            if (token.empty())
                return false;
            double value = 0.0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (result.ec != std::errc() || result.ptr != token.data() + token.size() || !std::isfinite(value))
                return false;
            out = value;
            return true;
        }

        bool toInt(std::string_view token, long& out)
        {
            if (token.empty() || token.size() > 12)
                return false;
            long value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (result.ec != std::errc() || result.ptr != token.data() + token.size())
                return false;
            out = value;
            return true;
        }

        /// Splits a line into exactly `count` finite numbers.
        template <std::size_t N>
        bool numbers(std::string_view line, double (&out)[N])
        {
            std::size_t found = 0;
            std::size_t i = 0;
            while (i < line.size())
            {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                    ++i;
                if (i >= line.size())
                    break;
                std::size_t j = i;
                while (j < line.size() && line[j] != ' ' && line[j] != '\t')
                    ++j;
                if (found >= N || !toDouble(line.substr(i, j - i), out[found]))
                    return false;
                ++found;
                i = j;
            }
            return found == N;
        }

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }
    }

    void ClipMotion::sample(float time, double& x, double& y, double& yaw) const
    {
        x = y = yaw = 0.0;
        if (!std::isfinite(time) || time <= 0.f)
            return;
        // Translation: from the origin at time 0 through the rows, linear, held after the last.
        {
            float t0 = 0.f;
            double x0 = 0.0, y0 = 0.0;
            for (const Row& row : mTranslation)
            {
                if (time <= row.mT)
                {
                    const double span = static_cast<double>(row.mT) - t0;
                    const double f = span > 0.0 ? (static_cast<double>(time) - t0) / span : 1.0;
                    x = x0 + (row.mX - x0) * f;
                    y = y0 + (row.mY - y0) * f;
                    break;
                }
                t0 = row.mT;
                x0 = row.mX;
                y0 = row.mY;
                x = x0;
                y = y0;
            }
        }
        {
            float t0 = 0.f;
            double a0 = 0.0;
            for (const Turn& row : mRotation)
            {
                if (time <= row.mT)
                {
                    const double span = static_cast<double>(row.mT) - t0;
                    const double f = span > 0.0 ? (static_cast<double>(time) - t0) / span : 1.0;
                    yaw = a0 + (row.mYaw - a0) * f;
                    break;
                }
                t0 = row.mT;
                a0 = row.mYaw;
                yaw = a0;
            }
        }
    }

    std::map<std::string, long> parseClipNumbers(std::string_view projectText)
    {
        std::map<std::string, long> out;
        const std::vector<std::string_view> lines = splitLines(projectText);
        for (std::size_t i = 0; i + 4 < lines.size(); ++i)
        {
            const std::string_view name = lines[i];
            double asNumber = 0;
            if (name.empty() || toDouble(name, asNumber))
                continue;
            long id = 0;
            if (!toInt(lines[i + 1], id) || lines[i + 2] != "1" || lines[i + 3] != "0" || lines[i + 4] != "0")
                continue;
            if (out.size() < kMaxMotionClips)
                out.emplace(lowerAscii(name), id);
        }
        return out;
    }

    std::map<long, ClipMotion> parseMotionBlocks(std::string_view boundText)
    {
        std::map<long, ClipMotion> out;
        const std::vector<std::string_view> lines = splitLines(boundText);
        std::size_t i = 0;
        while (i + 3 < lines.size())
        {
            long id = 0, rows = 0, turns = 0;
            double duration = 0;
            if (!toInt(lines[i], id) || !toDouble(lines[i + 1], duration) || !toInt(lines[i + 2], rows) || rows < 0
                || rows > static_cast<long>(kMaxMotionRows) || duration < 0)
            {
                ++i;
                continue;
            }
            std::size_t j = i + 3;
            ClipMotion clip;
            clip.mDuration = static_cast<float>(duration);
            bool ok = j + static_cast<std::size_t>(rows) < lines.size();
            for (long r = 0; ok && r < rows; ++r, ++j)
            {
                double v[4];
                if (!numbers(lines[j], v))
                {
                    ok = false;
                    break;
                }
                clip.mTranslation.push_back(
                    { static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]) });
            }
            if (ok
                && (j >= lines.size() || !toInt(lines[j], turns) || turns < 0
                    || turns > static_cast<long>(kMaxMotionRows)))
                ok = false;
            if (ok)
            {
                ++j;
                if (j + static_cast<std::size_t>(turns) > lines.size())
                    ok = false;
            }
            double previous = 0.0;
            for (long r = 0; ok && r < turns; ++r, ++j)
            {
                double q[5];
                if (!numbers(lines[j], q))
                {
                    ok = false;
                    break;
                }
                // A rotation about Z: yaw = 2 atan2(z, w), with the quaternion on the w >= 0 side.
                double z = q[3], w = q[4];
                if (w < 0.0)
                {
                    z = -z;
                    w = -w;
                }
                double yaw = 2.0 * std::atan2(z, w);
                // Unwrap against the row before, so a turn past a half circle keeps counting.
                while (yaw - previous > 3.141592653589793)
                    yaw -= kTwoPi;
                while (yaw - previous < -3.141592653589793)
                    yaw += kTwoPi;
                previous = yaw;
                clip.mRotation.push_back({ static_cast<float>(q[0]), static_cast<float>(yaw) });
            }
            if (!ok)
            {
                ++i;
                continue;
            }
            out.emplace(id, std::move(clip));
            i = j;
        }
        return out;
    }

    std::size_t MotionTable::add(std::string_view projectText, std::string_view boundText)
    {
        const std::map<std::string, long> names = parseClipNumbers(projectText);
        if (names.empty())
            return 0;
        std::map<long, ClipMotion> blocks = parseMotionBlocks(boundText);
        std::size_t added = 0;
        for (const auto& [name, id] : names)
        {
            if (mClips.count(name) != 0 || mClips.size() >= kMaxMotionClips)
                continue;
            const auto block = blocks.find(id);
            if (block == blocks.end())
                continue;
            mClips.emplace(name, std::make_unique<const ClipMotion>(block->second));
            ++added;
        }
        return added;
    }

    const ClipMotion* MotionTable::find(std::string_view clipName) const
    {
        const auto it = mClips.find(lowerAscii(clipName));
        return it == mClips.end() ? nullptr : it->second.get();
    }
}

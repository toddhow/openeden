#include "faceimport.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace Face
{
    namespace
    {
        constexpr std::size_t sMaxImportLines = 1u << 20; // lines read from a text file
        constexpr double sFrameEdgeSlack = 1e-9; // seconds: rounding slack when a line time sits on a frame edge

        LoadError located(std::string_view file, std::size_t line, std::string message, std::string pointer = {})
        {
            return LoadError{ std::string(file), line, std::move(pointer), std::move(message) };
        }

        std::string_view trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
                s.remove_suffix(1);
            return s;
        }

        struct Line
        {
            std::size_t mNumber;
            std::string_view mText;
        };

        // Splits on '\n' (a trailing '\r' is trimmed later). Returns false when there are too many lines.
        bool splitLines(std::string_view text, std::vector<Line>& lines)
        {
            std::size_t number = 1;
            std::size_t begin = 0;
            while (begin <= text.size())
            {
                std::size_t end = text.find('\n', begin);
                if (end == std::string_view::npos)
                    end = text.size();
                if (lines.size() >= sMaxImportLines)
                    return false;
                lines.push_back({ number, text.substr(begin, end - begin) });
                ++number;
                begin = end + 1;
            }
            return true;
        }

        bool parseDouble(std::string_view s, double& value)
        {
            if (s.empty())
                return false;
            const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
            return r.ec == std::errc() && r.ptr == s.data() + s.size() && std::isfinite(value);
        }

        void fillMeta(Curves& c, const ImportMeta& meta, std::uint32_t frameCount)
        {
            c.mAudioName = meta.mAudioName;
            c.mAudioDurationSeconds = meta.mAudioDurationSeconds;
            c.mAudioSha256 = meta.mAudioSha256;
            c.mGeneratorName = meta.mGeneratorName;
            c.mGeneratorVersion = meta.mGeneratorVersion;
            c.mSeed = meta.mSeed;
            c.mEmotion = meta.mEmotion;
            c.mFrameRate = meta.mFrameRate;
            c.mFrameCount = frameCount;
        }

        bool finish(Curves& c, std::string_view file, LoadError& error)
        {
            const auto issues = validate(c);
            if (issues.empty())
                return true;
            error = located(file, 1, issues.front().mMessage, issues.front().mPointer);
            return false;
        }
    }

    std::optional<Curves> importCsv(std::string_view text, std::string_view fileName, const ImportMeta& meta,
        const std::vector<std::string>& signedChannels, LoadError& error)
    {
        std::vector<Line> lines;
        if (!splitLines(text, lines))
        {
            error = located(fileName, 1, "the file has too many lines");
            return std::nullopt;
        }
        Curves c;
        bool haveHeader = false;
        std::size_t frames = 0;
        for (const Line& line : lines)
        {
            const std::string_view t = trim(line.mText);
            if (t.empty() || t.front() == '#')
                continue;
            std::vector<std::string_view> cells;
            for (std::size_t begin = 0;;)
            {
                const std::size_t comma = t.find(',', begin);
                cells.push_back(
                    trim(t.substr(begin, comma == std::string_view::npos ? std::string_view::npos : comma - begin)));
                if (comma == std::string_view::npos)
                    break;
                begin = comma + 1;
            }
            if (!haveHeader)
            {
                if (cells.size() > Limits::sMaxChannels)
                {
                    error = located(
                        fileName, line.mNumber, "more than " + std::to_string(Limits::sMaxChannels) + " channels");
                    return std::nullopt;
                }
                for (const std::string_view name : cells)
                {
                    c.mChannels.push_back({ std::string(name), ChannelKind::Weight, {} });
                    for (std::size_t i = 0; i + 1 < c.mChannels.size(); ++i)
                        if (c.mChannels[i].mName == c.mChannels.back().mName)
                        {
                            error = located(
                                fileName, line.mNumber, "the channel \"" + std::string(name) + "\" appears twice");
                            return std::nullopt;
                        }
                    if (!isValidChannelName(name))
                    {
                        error = located(fileName, line.mNumber,
                            "\"" + std::string(name)
                                + "\" is not a valid channel name (a-z, 0-9, '_' and '.', 1 to 64)");
                        return std::nullopt;
                    }
                }
                for (const std::string& s : signedChannels)
                {
                    bool found = false;
                    for (Channel& ch : c.mChannels)
                        if (ch.mName == s)
                        {
                            ch.mKind = ChannelKind::Signed;
                            found = true;
                        }
                    if (!found)
                    {
                        error
                            = located(fileName, line.mNumber, "the signed channel \"" + s + "\" is not in the header");
                        return std::nullopt;
                    }
                }
                haveHeader = true;
                continue;
            }
            if (cells.size() != c.mChannels.size())
            {
                error = located(fileName, line.mNumber,
                    "the row has " + std::to_string(cells.size()) + " values, the header has "
                        + std::to_string(c.mChannels.size()));
                return std::nullopt;
            }
            if (++frames > Limits::sMaxFrameCount)
            {
                error = located(
                    fileName, line.mNumber, "more than " + std::to_string(Limits::sMaxFrameCount) + " frames");
                return std::nullopt;
            }
            for (std::size_t i = 0; i < cells.size(); ++i)
            {
                double v = 0.0;
                if (!parseDouble(cells[i], v))
                {
                    error = located(fileName, line.mNumber, "\"" + std::string(cells[i]) + "\" is not a finite number");
                    return std::nullopt;
                }
                const double low = c.mChannels[i].mKind == ChannelKind::Signed ? -1.0 : 0.0;
                if (v < low || v > 1.0)
                {
                    error = located(fileName, line.mNumber,
                        "the value " + std::string(cells[i]) + " is outside " + (low < 0.0 ? "-1 to 1" : "0 to 1"));
                    return std::nullopt;
                }
                c.mChannels[i].mValues.push_back(static_cast<std::int16_t>(std::lround(v * Limits::sMaxWeight)));
            }
        }
        if (!haveHeader || frames == 0)
        {
            error
                = located(fileName, 1, haveHeader ? "the file has a header but no rows" : "the file has no header row");
            return std::nullopt;
        }
        fillMeta(c, meta, static_cast<std::uint32_t>(frames));
        if (!finish(c, fileName, error))
            return std::nullopt;
        return c;
    }

    std::optional<Curves> importRhubarbTsv(
        std::string_view text, std::string_view fileName, const ImportMeta& meta, LoadError& error)
    {
        static constexpr char sShapes[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'x' };
        constexpr std::size_t sShapeCount = sizeof(sShapes);
        const double rate = static_cast<double>(meta.mFrameRate);
        const double frameCountReal = std::round(meta.mAudioDurationSeconds * rate);
        if (!(meta.mFrameRate >= 1 && meta.mFrameRate <= Limits::sMaxFrameRate) || !std::isfinite(frameCountReal)
            || frameCountReal < 1.0 || frameCountReal > static_cast<double>(Limits::sMaxFrameCount))
        {
            error = located(fileName, 1,
                "the audio duration and frame rate give a frame count outside 1 to "
                    + std::to_string(Limits::sMaxFrameCount));
            return std::nullopt;
        }
        const std::uint32_t frames = static_cast<std::uint32_t>(frameCountReal);

        std::vector<Line> lines;
        if (!splitLines(text, lines))
        {
            error = located(fileName, 1, "the file has too many lines");
            return std::nullopt;
        }
        struct Cue
        {
            std::uint32_t mStart;
            std::size_t mShape;
        };
        std::vector<Cue> cues;
        double previous = 0.0;
        for (const Line& line : lines)
        {
            const std::string_view t = trim(line.mText);
            if (t.empty())
                continue;
            const std::size_t tab = t.find('\t');
            if (tab == std::string_view::npos)
            {
                error = located(fileName, line.mNumber, "expected \"seconds<TAB>shape\"");
                return std::nullopt;
            }
            double seconds = 0.0;
            if (!parseDouble(trim(t.substr(0, tab)), seconds) || seconds < 0.0)
            {
                error = located(fileName, line.mNumber, "the time is not a finite number of seconds, zero or more");
                return std::nullopt;
            }
            if (seconds < previous)
            {
                error = located(fileName, line.mNumber, "the times must not go backwards");
                return std::nullopt;
            }
            previous = seconds;
            const std::string_view shape = trim(t.substr(tab + 1));
            std::size_t shapeIndex = sShapeCount;
            if (shape.size() == 1)
                for (std::size_t i = 0; i < sShapeCount; ++i)
                    if (shape[0] == sShapes[i] || shape[0] == sShapes[i] - ('a' - 'A'))
                        shapeIndex = i;
            if (shapeIndex == sShapeCount)
            {
                error = located(
                    fileName, line.mNumber, "\"" + std::string(shape) + "\" is not a Rhubarb shape (A to H or X)");
                return std::nullopt;
            }
            const double startReal = std::ceil(seconds * rate - sFrameEdgeSlack);
            const std::uint32_t start = startReal >= static_cast<double>(frames)
                ? frames
                : static_cast<std::uint32_t>(std::max(0.0, startReal));
            cues.push_back({ start, shapeIndex });
        }
        if (cues.empty())
        {
            error = located(fileName, 1, "the file has no cues");
            return std::nullopt;
        }

        // The rest shape X holds until the first cue.
        std::vector<std::uint8_t> shapeOfFrame(frames, static_cast<std::uint8_t>(sShapeCount - 1));
        for (std::size_t i = 0; i < cues.size(); ++i)
        {
            const std::uint32_t end = i + 1 < cues.size() ? cues[i + 1].mStart : frames;
            for (std::uint32_t f = cues[i].mStart; f < end; ++f)
                shapeOfFrame[f] = static_cast<std::uint8_t>(cues[i].mShape);
        }
        Curves c;
        fillMeta(c, meta, frames);
        for (std::size_t s = 0; s < sShapeCount; ++s)
        {
            Channel ch;
            ch.mName = std::string("viseme_") + sShapes[s];
            ch.mValues.resize(frames);
            for (std::uint32_t f = 0; f < frames; ++f)
                ch.mValues[f] = shapeOfFrame[f] == s ? Limits::sMaxWeight : 0;
            c.mChannels.push_back(std::move(ch));
        }
        if (!finish(c, fileName, error))
            return std::nullopt;
        return c;
    }
}

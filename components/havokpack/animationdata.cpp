#include "animationdata.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace HavokPack
{
    namespace
    {
        // Splits the text into lines on LF; one CR before the LF is dropped. Keeps the number of the line it last
        // returned so that every error can say where it is.
        class Lines
        {
        public:
            Lines(std::string_view text, const TextLimits& limits)
                : mText(text)
                , mLimits(limits)
            {
                if (text.size() > limits.mMaxFileBytes)
                    throw Error("the file is too large (" + std::to_string(text.size()) + " bytes)");
            }
            bool atEnd() const { return mPos >= mText.size(); }
            // The line number the next call will return, for end-of-file messages and for "data after the end" errors.
            std::uint32_t nextNumber() const { return mLine + 1; }
            [[noreturn]] void fail(const std::string& what) const
            {
                throw Error(what + " (line " + std::to_string(mLine) + ")");
            }
            std::string_view next(const char* what)
            {
                if (atEnd())
                    throw Error(std::string("unexpected end of file, expected ") + what + " (line "
                        + std::to_string(nextNumber()) + ")");
                ++mLine;
                const std::size_t end = mText.find('\n', mPos);
                std::string_view line
                    = end == std::string_view::npos ? mText.substr(mPos) : mText.substr(mPos, end - mPos);
                mPos = end == std::string_view::npos ? mText.size() : end + 1;
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                if (line.size() > mLimits.mMaxLineLength)
                    fail("line is too long");
                return line;
            }
            float number(std::string_view token, const char* what) const
            {
                float v = 0;
                const auto r = std::from_chars(token.data(), token.data() + token.size(), v);
                if (token.empty() || r.ec != std::errc{} || r.ptr != token.data() + token.size() || !std::isfinite(v))
                    fail(std::string("bad number for ") + what);
                return v;
            }
            std::uint32_t count(std::string_view token, const char* what) const
            {
                std::uint32_t v = 0;
                const auto r = std::from_chars(token.data(), token.data() + token.size(), v);
                if (token.empty() || r.ec != std::errc{} || r.ptr != token.data() + token.size())
                    fail(std::string("bad count for ") + what);
                return v;
            }

        private:
            std::string_view mText;
            const TextLimits& mLimits;
            std::size_t mPos = 0;
            std::uint32_t mLine = 0;
        };

        // A row of exactly N numbers separated by single spaces.
        template <std::size_t N>
        std::array<float, N> readRow(Lines& lines, const char* what)
        {
            const std::string_view row = lines.next(what);
            std::array<float, N> out{};
            std::size_t start = 0;
            for (std::size_t i = 0; i < N; ++i)
            {
                const std::size_t space = i + 1 == N ? std::string_view::npos : row.find(' ', start);
                if (i + 1 < N && space == std::string_view::npos)
                    lines.fail(std::string("too few numbers in ") + what);
                out[i] = lines.number(
                    row.substr(start, space == std::string_view::npos ? std::string_view::npos : space - start), what);
                start = space + 1;
            }
            return out;
        }

        char lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        // Lower case, without a trailing ".hkx".
        std::string stemKey(std::string_view name)
        {
            std::string s(name);
            std::transform(s.begin(), s.end(), s.begin(), lower);
            if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".hkx") == 0)
                s.resize(s.size() - 4);
            return s;
        }
    }

    AnimationProject readAnimationProject(std::string_view text, const TextLimits& limits)
    {
        Lines lines(text, limits);
        AnimationProject project;
        if (lines.next("the header") != "1")
            lines.fail("the first line must be 1");
        const std::uint32_t files = lines.count(lines.next("the file count"), "the file count");
        if (files > limits.mMaxFiles)
            lines.fail("too many files (" + std::to_string(files) + ")");
        project.mFiles.reserve(files);
        for (std::uint32_t i = 0; i < files; ++i)
        {
            const std::string_view f = lines.next("a file name");
            if (f.empty())
                lines.fail("empty file name");
            project.mFiles.emplace_back(f);
        }
        const std::string_view flag = lines.next("the clip flag");
        if (flag != "0" && flag != "1")
            lines.fail("the clip flag must be 0 or 1");
        if (flag == "0")
        {
            if (!lines.atEnd())
                throw Error(
                    "unexpected data after a project without clips (line " + std::to_string(lines.nextNumber()) + ")");
            return project;
        }
        while (!lines.atEnd())
        {
            if (project.mClips.size() >= limits.mMaxClips)
                throw Error("too many clips (line " + std::to_string(lines.nextNumber()) + ")");
            SidecarClip clip;
            const std::string_view name = lines.next("a clip name");
            if (name.empty())
                lines.fail("empty clip name");
            clip.mName = std::string(name);
            clip.mCacheIndex = lines.count(lines.next("the cache index"), "the cache index");
            clip.mPlaybackSpeed = lines.number(lines.next("the playback speed"), "the playback speed");
            clip.mUnknownA = lines.number(lines.next("a clip number"), "a clip number");
            clip.mUnknownB = lines.number(lines.next("a clip number"), "a clip number");
            const std::uint32_t events = lines.count(lines.next("the event count"), "the event count");
            if (events > limits.mMaxEventsPerClip)
                lines.fail("too many events (" + std::to_string(events) + ")");
            clip.mEvents.reserve(events);
            for (std::uint32_t i = 0; i < events; ++i)
            {
                const std::string_view e = lines.next("an event");
                const std::size_t colon = e.rfind(':');
                if (colon == std::string_view::npos || colon == 0)
                    lines.fail("an event is name:time");
                clip.mEvents.push_back(
                    { std::string(e.substr(0, colon)), lines.number(e.substr(colon + 1), "an event time") });
            }
            if (!lines.next("the blank line after a clip").empty())
                lines.fail("expected a blank line after the clip");
            project.mClips.push_back(std::move(clip));
        }
        return project;
    }

    const MotionBlock* MotionTable::find(std::uint32_t cacheIndex) const
    {
        for (const MotionBlock& b : mBlocks)
            if (b.mCacheIndex == cacheIndex)
                return &b;
        return nullptr;
    }

    MotionTable readMotionTable(std::string_view text, const TextLimits& limits)
    {
        Lines lines(text, limits);
        MotionTable table;
        while (!lines.atEnd())
        {
            if (table.mBlocks.size() >= limits.mMaxBlocks)
                throw Error("too many blocks (line " + std::to_string(lines.nextNumber()) + ")");
            MotionBlock block;
            block.mCacheIndex = lines.count(lines.next("the cache index"), "the cache index");
            if (table.find(block.mCacheIndex) != nullptr)
                lines.fail("cache index " + std::to_string(block.mCacheIndex) + " appears twice");
            block.mDuration = lines.number(lines.next("the duration"), "the duration");
            if (!(block.mDuration > 0.0f))
                lines.fail("the duration must be positive");
            const std::uint32_t translations
                = lines.count(lines.next("the translation count"), "the translation count");
            if (translations > limits.mMaxRowsPerBlock)
                lines.fail("too many translation rows (" + std::to_string(translations) + ")");
            block.mTranslation.reserve(translations);
            for (std::uint32_t i = 0; i < translations; ++i)
            {
                const auto row = readRow<4>(lines, "a translation row");
                if (row[0] < 0.0f || (i > 0 && row[0] <= block.mTranslation.back()[0]))
                    lines.fail("row times must increase from zero");
                block.mTranslation.push_back(row);
            }
            const std::uint32_t rotations = lines.count(lines.next("the rotation count"), "the rotation count");
            if (rotations > limits.mMaxRowsPerBlock)
                lines.fail("too many rotation rows (" + std::to_string(rotations) + ")");
            block.mRotation.reserve(rotations);
            for (std::uint32_t i = 0; i < rotations; ++i)
            {
                const auto row = readRow<5>(lines, "a rotation row");
                if (row[0] < 0.0f || (i > 0 && row[0] <= block.mRotation.back()[0]))
                    lines.fail("row times must increase from zero");
                block.mRotation.push_back(row);
            }
            if (!lines.next("the blank line after a block").empty())
                lines.fail("expected a blank line after the block");
            table.mBlocks.push_back(std::move(block));
        }
        return table;
    }

    namespace
    {
        // Position in rows (time first, N-1 values after) of `time`: the rows on either side and the blend factor, from
        // the origin (time 0, all values zero) before the first row. Returns false when `time` is beyond the last row.
        template <std::size_t N>
        bool locate(const std::vector<std::array<float, N>>& rows, float time, const std::array<float, N>*& lo,
            const std::array<float, N>*& hi, float& alpha)
        {
            std::size_t i = 0;
            while (i < rows.size() && rows[i][0] < time)
                ++i;
            if (i == rows.size())
                return false;
            hi = &rows[i];
            lo = i == 0 ? nullptr : &rows[i - 1];
            const float t0 = lo ? (*lo)[0] : 0.0f;
            alpha = ((*hi)[0] > t0) ? (time - t0) / ((*hi)[0] - t0) : 1.0f;
            return true;
        }
    }

    std::array<float, 3> rootTranslationAt(const MotionBlock& block, float time)
    {
        std::array<float, 3> out{ 0, 0, 0 };
        if (block.mTranslation.empty() || !(time > 0.0f))
            return out;
        const std::array<float, 4>* lo = nullptr;
        const std::array<float, 4>* hi = nullptr;
        float alpha = 0;
        if (!locate(block.mTranslation, time, lo, hi, alpha))
        {
            const auto& last = block.mTranslation.back();
            return { last[1], last[2], last[3] };
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            const float a = lo ? (*lo)[k + 1] : 0.0f;
            out[k] = a + ((*hi)[k + 1] - a) * alpha;
        }
        return out;
    }

    std::array<float, 4> rootRotationAt(const MotionBlock& block, float time)
    {
        const std::array<float, 4> identity{ 0, 0, 0, 1 };
        if (block.mRotation.empty() || !(time > 0.0f))
            return identity;
        const std::array<float, 5>* lo = nullptr;
        const std::array<float, 5>* hi = nullptr;
        float alpha = 0;
        if (!locate(block.mRotation, time, lo, hi, alpha))
        {
            const auto& last = block.mRotation.back();
            return { last[1], last[2], last[3], last[4] };
        }
        std::array<float, 4> a = identity;
        if (lo)
            a = { (*lo)[1], (*lo)[2], (*lo)[3], (*lo)[4] };
        std::array<float, 4> b = { (*hi)[1], (*hi)[2], (*hi)[3], (*hi)[4] };
        if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f)
            for (float& v : b)
                v = -v; // the shorter way round
        std::array<float, 4> q{};
        float norm = 0;
        for (std::size_t k = 0; k < 4; ++k)
        {
            q[k] = a[k] + (b[k] - a[k]) * alpha;
            norm += q[k] * q[k];
        }
        if (!(norm > 0.0f))
            return identity;
        const float inv = 1.0f / std::sqrt(norm);
        for (float& v : q)
            v *= inv;
        return q;
    }

    float eventAnimationTime(const SidecarClip& clip, const ClipEvent& event)
    {
        return event.mTime * clip.mPlaybackSpeed;
    }

    ClipLink linkClip(
        const AnimationProject& project, const MotionTable& motion, std::string_view fileStem, float duration)
    {
        ClipLink link;
        const std::string key = stemKey(fileStem);
        for (const SidecarClip& clip : project.mClips)
        {
            if (stemKey(clip.mName) != key)
                continue;
            const MotionBlock* block = motion.find(clip.mCacheIndex);
            if (block != nullptr && std::fabs(block->mDuration - duration) > 1e-3f * std::max(1.0f, block->mDuration))
                continue; // same name, other animation
            link.mClips.push_back(&clip);
            if (block != nullptr && link.mMotion == nullptr)
                link.mMotion = block;
        }
        return link;
    }
}

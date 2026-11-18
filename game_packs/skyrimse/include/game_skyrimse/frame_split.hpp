#pragma once

// Task 0323: where the pack's GameSimulation callback spends its time. The engine traces the callback as one scope
// ("phase.GameSimulation", `[Cells] load trace`), but a game pack is a separate library with its own copy of the
// engine tracer's flag, so it cannot write into that trace. This splits the callback into two parts, the Papyrus
// host's frame (script attaches and events on cell loads) and everything else, from timestamps the caller reads off
// the steady clock, the clock the engine's trace uses, so the SKYFRAME lines (absolute microseconds) join the trace
// by time. Developer aid: the environment variable OPENEDEN_PACK_FRAME_TRACE=1; off, it records nothing. Pure: it takes
// the times, it never reads a clock.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    /// The steady clock in microseconds: the clock of the engine's load trace, so the times line up.
    inline std::int64_t steadyNowMicroseconds()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    class FrameSplit
    {
    public:
        explicit FrameSplit(bool on, std::int64_t slowMicroseconds = 5000) noexcept
            : mOn(on)
            , mSlow(slowMicroseconds)
        {
        }

        [[nodiscard]] static bool wanted(const char* text) noexcept
        {
            return text != nullptr && text[0] != '\0' && text[0] != '0';
        }

        [[nodiscard]] bool on() const noexcept { return mOn; }

        /// One frame: the Papyrus part is [begin, middle), the rest [middle, end). Returns the log line when the frame
        /// took at least the slow limit, else an empty string.
        std::string add(std::int64_t begin, std::int64_t middle, std::int64_t end)
        {
            if (!mOn)
                return {};
            const std::int64_t papyrus = std::max<std::int64_t>(0, middle - begin);
            const std::int64_t rest = std::max<std::int64_t>(0, end - middle);
            ++mFrames;
            mPapyrus += papyrus;
            mRest += rest;
            const std::int64_t total = papyrus + rest;
            mWorst = std::max(mWorst, total);
            if (total < mSlow)
                return {};
            ++mSlowFrames;
            return "SKYFRAME b=" + std::to_string(begin) + " e=" + std::to_string(end)
                + " papyrus=" + std::to_string(papyrus) + " rest=" + std::to_string(rest);
        }

        [[nodiscard]] std::string summary() const
        {
            if (!mOn)
                return {};
            return "SKYFRAME summary: " + std::to_string(mFrames) + " frames, papyrus " + std::to_string(mPapyrus)
                + " us, rest " + std::to_string(mRest) + " us, worst " + std::to_string(mWorst) + " us, "
                + std::to_string(mSlowFrames) + " over " + std::to_string(mSlow) + " us";
        }

        [[nodiscard]] std::uint64_t frames() const noexcept { return mFrames; }
        [[nodiscard]] std::uint64_t slowFrames() const noexcept { return mSlowFrames; }
        [[nodiscard]] std::int64_t papyrusMicroseconds() const noexcept { return mPapyrus; }
        [[nodiscard]] std::int64_t restMicroseconds() const noexcept { return mRest; }
        [[nodiscard]] std::int64_t worstMicroseconds() const noexcept { return mWorst; }

    private:
        bool mOn;
        std::int64_t mSlow;
        std::uint64_t mFrames = 0;
        std::uint64_t mSlowFrames = 0;
        std::int64_t mPapyrus = 0;
        std::int64_t mRest = 0;
        std::int64_t mWorst = 0;
    };

    /// Named stretches of one frame of the Papyrus driver, from marks the caller takes between its steps: mark("a", t)
    /// ends the stretch "a" that began at the last mark (or start). Same-named stretches in one frame add up. The names
    /// must outlive the frame (string literals).
    class SectionMarks
    {
    public:
        void start(std::int64_t now)
        {
            mLast = now;
            mFirst = now;
            mParts.clear();
        }

        void mark(std::string_view name, std::int64_t now)
        {
            const std::int64_t length = std::max<std::int64_t>(0, now - mLast);
            mLast = now;
            for (auto& part : mParts)
                if (part.first == name)
                {
                    part.second += length;
                    return;
                }
            if (mParts.size() < kMaxParts)
                mParts.emplace_back(name, length);
        }

        [[nodiscard]] std::int64_t total() const noexcept { return std::max<std::int64_t>(0, mLast - mFirst); }

        /// "SKYPAPYRUS b=<start> total=<us> flush=<us> place=<us> ..." (parts in the order first marked).
        [[nodiscard]] std::string line() const
        {
            std::string text = "SKYPAPYRUS b=" + std::to_string(mFirst) + " total=" + std::to_string(total());
            for (const auto& part : mParts)
                text += " " + std::string(part.first) + "=" + std::to_string(part.second);
            return text;
        }

        static constexpr std::size_t kMaxParts = 16;

    private:
        std::int64_t mFirst = 0;
        std::int64_t mLast = 0;
        std::vector<std::pair<std::string_view, std::int64_t>> mParts;
    };
}

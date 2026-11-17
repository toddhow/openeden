#include "game_skyrimse/werewolf.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game_skyrimse::werewolf
{
    namespace
    {
        constexpr float kMaxStep = 1.f; ///< a frame longer than this is clamped

        // The row (docs/tasks/0259-skyrim-werewolf.md section 1): walk and run strides are the root motion of
        // ww_walkforward and ww_runforward, the height is the skin's z extent; the radius is OURS.
        const CreatureProfile kBare = { "werewolf", "actors/werewolfbeast/character assets/skeleton.nif",
            "werewolfbeastproject", "ww_idlebase", "ww_walkforward", "ww_runforward", 93.4308f / 1.33333f,
            222.219f / 0.733333f, 93.4308f, 222.219f, 139.6f, "ww_attackrightfast", "ww_attackrightpowercombo",
            "ww_staggersmalll", "ww_staggermedium", "ww_deathtemp", 30.f };
        const CreatureProfile kPlayer = { "werewolf", "actors/werewolfbeast/character assets/skeleton.nif",
            "werewolfbeastproject", "ww_idlebase", "ww_walkforward", "ww_runforward", 93.4308f / 1.33333f,
            222.219f / 0.733333f, 93.4308f, 222.219f, 139.6f, "werewolfbeast::ww_attackrightfast",
            "werewolfbeast::ww_attackrightpowercombo", "ww_staggersmalll", "ww_staggermedium", "ww_deathtemp", 30.f };
    }

    FormId levelAbility(std::uint32_t level)
    {
        struct Step
        {
            std::uint32_t mUpTo;
            FormId mAbility;
        };
        static constexpr Step kSteps[] = { { 10, 0x10C6E8 }, { 15, 0x10F186 }, { 20, 0x10C6E9 }, { 25, 0x10F187 },
            { 30, 0x10C6EA }, { 35, 0x10F188 }, { 40, 0x10F189 }, { 45, 0x10F18A } };
        const std::uint32_t l = std::max<std::uint32_t>(level, 1);
        for (const Step& step : kSteps)
            if (l <= step.mUpTo)
                return step.mAbility;
        return 0x10C6EB;
    }

    void BeastClock::start(float seconds)
    {
        mActive = true;
        mRemaining = std::isfinite(seconds) && seconds > 0.f ? std::min(seconds, kMaxSeconds) : kStandardSeconds;
        mWarned = mRemaining <= kWarnSeconds;
    }

    void BeastClock::stop()
    {
        mActive = false;
        mRemaining = 0.f;
        mWarned = false;
    }

    BeastClock::Event BeastClock::tick(float dt)
    {
        if (!mActive)
            return Event::None;
        const float step = std::isfinite(dt) && dt > 0.f ? std::min(dt, kMaxStep) : 0.f;
        mRemaining -= step;
        if (mRemaining <= 0.f)
        {
            stop();
            return Event::Ended;
        }
        if (!mWarned && mRemaining <= kWarnSeconds)
        {
            mWarned = true;
            return Event::Warned;
        }
        return Event::None;
    }

    bool BeastClock::feed(bool victimIsPerson)
    {
        if (!mActive)
            return false;
        mRemaining
            = std::min(kMaxSeconds, mRemaining + kFeedSeconds / 2.f + (victimIsPerson ? kFeedSeconds / 2.f : 0.f));
        if (mRemaining > kWarnSeconds)
            mWarned = false; // the warning comes again when it runs low again
        return true;
    }

    std::string_view bareClip(std::string_view clip)
    {
        const std::size_t at = clip.find(kClipSeparator);
        return at == std::string_view::npos ? clip : clip.substr(at + kClipSeparator.size());
    }

    std::string_view clipFolder(std::string_view clip)
    {
        const std::size_t at = clip.find(kClipSeparator);
        return at == std::string_view::npos ? std::string_view() : clip.substr(0, at);
    }

    const CreatureProfile& playerProfile()
    {
        return kPlayer;
    }

    const CreatureProfile& bareProfile()
    {
        return kBare;
    }

    // ---- saving (task 0265) -----------------------------------------------------------------------------------------

    namespace
    {
        constexpr std::size_t kHeaderBytes = 1 + 1 + sizeof(float) + sizeof(std::uint32_t);

        class BeastChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            BeastChunk(std::function<BeastSaved()> get, std::function<void(const BeastSaved&)> set)
                : mGet(std::move(get))
                , mSet(std::move(set))
            {
            }
            std::string_view chunkName() const override { return "werewolf"; }
            std::uint32_t chunkVersion() const override { return 1; }
            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const std::vector<std::uint8_t> bytes = encodeBeastSaved(mGet ? mGet() : BeastSaved());
                writer.write(bytes.data(), bytes.size());
            }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                std::vector<std::uint8_t> bytes(reader.remaining());
                BeastSaved saved;
                if (version != 1 || bytes.empty() || !reader.read(bytes.data(), bytes.size())
                    || !decodeBeastSaved(bytes.data(), bytes.size(), saved))
                    return false;
                if (mSet)
                    mSet(saved);
                return true;
            }

        private:
            std::function<BeastSaved()> mGet;
            std::function<void(const BeastSaved&)> mSet;
        };
    }

    std::vector<std::uint8_t> encodeBeastSaved(const BeastSaved& saved)
    {
        const bool active = saved.mActive && std::isfinite(saved.mRemaining) && saved.mRemaining > 0.f;
        std::vector<std::uint64_t> fed;
        if (active)
        {
            fed = saved.mFed;
            std::sort(fed.begin(), fed.end());
            fed.erase(std::unique(fed.begin(), fed.end()), fed.end());
            if (fed.size() > kMaxSavedFed)
                fed.resize(kMaxSavedFed);
        }
        const float seconds = active ? std::min(saved.mRemaining, kMaxSeconds) : 0.f;
        const std::uint32_t count = static_cast<std::uint32_t>(fed.size());
        std::vector<std::uint8_t> out(kHeaderBytes + fed.size() * sizeof(std::uint64_t));
        out[0] = saved.mGranted ? 1 : 0;
        out[1] = active ? 1 : 0;
        std::memcpy(out.data() + 2, &seconds, sizeof(float));
        std::memcpy(out.data() + 6, &count, sizeof(std::uint32_t));
        if (!fed.empty())
            std::memcpy(out.data() + kHeaderBytes, fed.data(), fed.size() * sizeof(std::uint64_t));
        return out;
    }

    bool decodeBeastSaved(const std::uint8_t* data, std::size_t size, BeastSaved& out)
    {
        if (data == nullptr || size < kHeaderBytes || data[0] > 1 || data[1] > 1)
            return false;
        float seconds = 0.f;
        std::uint32_t count = 0;
        std::memcpy(&seconds, data + 2, sizeof(float));
        std::memcpy(&count, data + 6, sizeof(std::uint32_t));
        if (count > kMaxSavedFed || size != kHeaderBytes + static_cast<std::size_t>(count) * sizeof(std::uint64_t))
            return false;
        const bool active = data[1] == 1;
        if (!std::isfinite(seconds))
            return false;
        if (active ? !(seconds > 0.f && seconds <= kMaxSeconds) : (seconds != 0.f || count != 0))
            return false;
        BeastSaved saved;
        saved.mGranted = data[0] == 1;
        saved.mActive = active;
        saved.mRemaining = seconds;
        saved.mFed.resize(count);
        if (count != 0)
            std::memcpy(saved.mFed.data(), data + kHeaderBytes, static_cast<std::size_t>(count) * sizeof(std::uint64_t));
        for (std::size_t i = 1; i < saved.mFed.size(); ++i)
        {
            if (saved.mFed[i - 1] >= saved.mFed[i])
                return false;
        }
        out = std::move(saved);
        return true;
    }

    std::unique_ptr<openeden::sdk::SaveChunkHandler> makeBeastChunk(
        std::function<BeastSaved()> get, std::function<void(const BeastSaved&)> set)
    {
        return std::make_unique<BeastChunk>(std::move(get), std::move(set));
    }
}

#include "game_skyrimse/story_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game_skyrimse
{
    namespace
    {
        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }

        void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        void put64(std::vector<std::uint8_t>& out, std::uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        class Reader
        {
        public:
            explicit Reader(std::span<const std::uint8_t> bytes)
                : mBytes(bytes)
            {
            }
            bool u32(std::uint32_t& v)
            {
                if (mBytes.size() - mAt < 4)
                    return false;
                v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<std::uint32_t>(mBytes[mAt + static_cast<std::size_t>(i)]) << (8 * i);
                mAt += 4;
                return true;
            }
            bool u64(std::uint64_t& v)
            {
                std::uint32_t lo = 0, hi = 0;
                if (!u32(lo) || !u32(hi))
                    return false;
                v = lo | (static_cast<std::uint64_t>(hi) << 32);
                return true;
            }
            [[nodiscard]] std::size_t at() const { return mAt; }
            [[nodiscard]] std::size_t left() const { return mBytes.size() - mAt; }

        private:
            std::span<const std::uint8_t> mBytes;
            std::size_t mAt = 0;
        };
    }

    // ---- LocationKeywordData
    // -------------------------------------------------------------------------------------------

    bool LocationKeywordData::set(FormId location, FormId keyword, float value)
    {
        if (!usable(location) || !usable(keyword) || !std::isfinite(value))
            return false;
        const std::pair<FormId, FormId> key{ location, keyword };
        const auto it = mData.find(key);
        if (it != mData.end())
        {
            it->second = value;
            return true;
        }
        if (mData.size() >= kMaxEntries)
            return false;
        mData.emplace(key, value);
        return true;
    }

    float LocationKeywordData::get(FormId location, FormId keyword) const
    {
        const auto it = mData.find({ location, keyword });
        return it == mData.end() ? 0.f : it->second;
    }

    std::vector<std::tuple<FormId, FormId, float>> LocationKeywordData::entries() const
    {
        std::vector<std::tuple<FormId, FormId, float>> out;
        out.reserve(mData.size());
        for (const auto& [key, value] : mData)
            out.emplace_back(key.first, key.second, value);
        return out;
    }

    // ---- the save body
    // ---------------------------------------------------------------------------------------------------
    //
    // version 1 body: u64 event counter; u32 cycle node count, per node (ascending) u32 node, u32 quest count, u32
    // quests (ascending, no repeat); u32 ran count, u32 quests (ascending, no repeat); u32 keyword data count, per
    // entry u32 location, u32 keyword, u32 float bits (ascending by location then keyword, finite).

    std::vector<std::uint8_t> encodeStoryState(StoryState state)
    {
        std::sort(state.mManager.mCycle.begin(), state.mManager.mCycle.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [node, quests] : state.mManager.mCycle)
        {
            std::sort(quests.begin(), quests.end());
            quests.erase(std::unique(quests.begin(), quests.end()), quests.end());
        }
        std::sort(state.mRan.begin(), state.mRan.end());
        state.mRan.erase(std::unique(state.mRan.begin(), state.mRan.end()), state.mRan.end());
        std::sort(state.mKeywordData.begin(), state.mKeywordData.end(), [](const auto& a, const auto& b) {
            return std::make_pair(std::get<0>(a), std::get<1>(a)) < std::make_pair(std::get<0>(b), std::get<1>(b));
        });
        std::vector<std::uint8_t> out;
        put64(out, state.mManager.mEventCounter);
        put32(out, static_cast<std::uint32_t>(state.mManager.mCycle.size()));
        for (const auto& [node, quests] : state.mManager.mCycle)
        {
            put32(out, node);
            put32(out, static_cast<std::uint32_t>(quests.size()));
            for (const FormId q : quests)
                put32(out, q);
        }
        put32(out, static_cast<std::uint32_t>(state.mRan.size()));
        for (const FormId q : state.mRan)
            put32(out, q);
        put32(out, static_cast<std::uint32_t>(state.mKeywordData.size()));
        for (const auto& [location, keyword, value] : state.mKeywordData)
        {
            put32(out, location);
            put32(out, keyword);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            put32(out, bits);
        }
        return out;
    }

    bool decodeStoryState(std::span<const std::uint8_t> bytes, StoryState& out, std::string& error)
    {
        out = StoryState();
        Reader in(bytes);
        const auto fail = [&](const std::string& why) {
            out = StoryState();
            error = why + " (byte " + std::to_string(in.at()) + ")";
            return false;
        };
        StoryState result;
        std::uint32_t count = 0;
        if (!in.u64(result.mManager.mEventCounter))
            return fail("the story state is cut short");
        if (!in.u32(count))
            return fail("the story state is cut short");
        if (count > kMaxStoryCycleNodes)
            return fail("more than " + std::to_string(kMaxStoryCycleNodes) + " cycle nodes");
        FormId lastNode = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t node = 0, quests = 0;
            if (!in.u32(node) || !in.u32(quests))
                return fail("the story state is cut short");
            if (i != 0 && node <= lastNode)
                return fail("the cycle nodes are not in ascending order");
            lastNode = node;
            if (quests > kMaxStoryCycleQuests || static_cast<std::uint64_t>(quests) * 4 > in.left())
                return fail("a cycle names more quests than the state holds");
            std::vector<FormId> list;
            list.reserve(quests);
            for (std::uint32_t k = 0; k < quests; ++k)
            {
                std::uint32_t q = 0;
                if (!in.u32(q))
                    return fail("the story state is cut short");
                if (k != 0 && q <= list.back())
                    return fail("the cycle quests are not in ascending order");
                list.push_back(q);
            }
            result.mManager.mCycle.emplace_back(node, std::move(list));
        }
        if (!in.u32(count))
            return fail("the story state is cut short");
        if (count > kMaxStoryRan || static_cast<std::uint64_t>(count) * 4 > in.left())
            return fail("more than " + std::to_string(kMaxStoryRan) + " ambient quests");
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t q = 0;
            if (!in.u32(q))
                return fail("the story state is cut short");
            if (i != 0 && q <= result.mRan.back())
                return fail("the ambient quests are not in ascending order");
            result.mRan.push_back(q);
        }
        if (!in.u32(count))
            return fail("the story state is cut short");
        if (count > LocationKeywordData::kMaxEntries || static_cast<std::uint64_t>(count) * 12 > in.left())
            return fail("more keyword data than the state holds");
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t location = 0, keyword = 0, bits = 0;
            if (!in.u32(location) || !in.u32(keyword) || !in.u32(bits))
                return fail("the story state is cut short");
            float value = 0.f;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value) || !usable(location) || !usable(keyword))
                return fail("a keyword data entry is not valid");
            if (i != 0
                && std::make_pair(location, keyword)
                    <= std::make_pair(std::get<0>(result.mKeywordData.back()), std::get<1>(result.mKeywordData.back())))
                return fail("the keyword data is not in ascending order");
            result.mKeywordData.emplace_back(location, keyword, value);
        }
        if (in.left() != 0)
            return fail("the story state has bytes after its end");
        out = std::move(result);
        return true;
    }
}

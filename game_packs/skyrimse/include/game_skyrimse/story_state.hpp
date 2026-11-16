#ifndef OPENEDEN_GAME_SKYRIMSE_STORY_STATE_H
#define OPENEDEN_GAME_SKYRIMSE_STORY_STATE_H

// What a save keeps of the story manager (task 0217): the "do all quests before repeating" rotation of each quest node,
// the event counter (the seed of every random order), the ambient quests that already ran this session, and the data
// scripts store on locations (Location.SetKeywordData, read by GetKeywordDataForLocation, fn_606). One save chunk of
// the pack, `story`, version 1; canonical (the same state is the same bytes), capped, every refusal located by byte
// offset. Pure: no engine, no Papyrus.

#include <game_skyrimse/records.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    /// The data scripts store on a location for a keyword. A float, 0 when nothing was stored (the game's own default,
    /// RECALLED-VERIFY: the Creation Kit wiki is unreachable; every use in the data compares it with 0 or a small
    /// number). Capped: a script cannot grow it without bound.
    class LocationKeywordData
    {
    public:
        static constexpr std::size_t kMaxEntries = 65536;

        /// False (nothing stored) for a null or unresolved location or keyword, a value that is not finite, or a full
        /// table (an existing entry may still change).
        bool set(FormId location, FormId keyword, float value);
        [[nodiscard]] float get(FormId location, FormId keyword) const;
        [[nodiscard]] std::size_t size() const { return mData.size(); }
        void clear() { mData.clear(); }
        /// location, keyword, value; ascending by location then keyword.
        [[nodiscard]] std::vector<std::tuple<FormId, FormId, float>> entries() const;

    private:
        std::map<std::pair<FormId, FormId>, float> mData;
    };

    struct StoryManagerState
    {
        std::uint64_t mEventCounter = 0;
        /// quest node -> the quests it started in the current "do all before repeating" cycle (ascending)
        std::vector<std::pair<FormId, std::vector<FormId>>> mCycle;
        bool operator==(const StoryManagerState&) const = default;
    };

    struct StoryState
    {
        StoryManagerState mManager;
        std::vector<FormId> mRan; ///< ambient (ADIA) quests that ran this session, ascending
        std::vector<std::tuple<FormId, FormId, float>> mKeywordData;
        bool operator==(const StoryState&) const = default;
    };

    inline constexpr std::uint32_t kMaxStoryCycleNodes = 8192;
    inline constexpr std::uint32_t kMaxStoryCycleQuests = 65536;
    inline constexpr std::uint32_t kMaxStoryRan = 65536;

    /// Sorts every list so the bytes depend only on the content, then writes the version 1 body (no version field: the
    /// chunk carries it).
    [[nodiscard]] std::vector<std::uint8_t> encodeStoryState(StoryState state);
    /// False with `error` (and `out` cleared) for a body that is cut short, over a cap, not in canonical order, has a
    /// non-finite value or trailing bytes.
    [[nodiscard]] bool decodeStoryState(std::span<const std::uint8_t> bytes, StoryState& out, std::string& error);
}

#endif

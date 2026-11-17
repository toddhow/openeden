#ifndef OPENEDEN_GAME_SKYRIMSE_HARVEST_H
#define OPENEDEN_GAME_SKYRIMSE_HARVEST_H

// Harvesting flora (task 0221): the pure rules. Standard library and the SDK save chunk types only; no clock, no
// records, no random state.
//
// What is the data's and what is ours. The data's: which bases are harvestable (a FLOR or TREE record with a PFIG
// ingredient, which may be a leveled list), the four seasonal chances (PFPC: spring, summer, fall, winter, a percent
// each) and the activate text (RNAM, FLOR only). Ours (named, RECALLED-VERIFY against Skyrim's own behaviour):
//   - the calendar to season mapping (kSeasonOfMonth),
//   - four zero chances mean "no seasonal data" and give kUnseasonalChance (the data's ChickenNest is such a record),
//   - the regrowth time (kRegrowSeconds; the game's own is its cell reset, GMST iHoursToRespawnCell = 240 hours,
//     RECALLED-VERIFY),
//   - the seeded roll.
// The look of a harvested plant is a hidden object (see the stage log of docs/tasks/0221-skyrim-harvesting.md): the
// engine has no per-placement model variant, and the NiSwitchNode of the model (child 1 = harvested) cannot be chosen.
#include <game_skyrimse/records.hpp>

#include <openeden/game/save_chunks.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::harvest
{
    /// A harvested plant grows back after this many game seconds (10 days, 240 hours). Ours.
    inline constexpr double kRegrowSeconds = 240.0 * 3600.0;
    /// The most plants a session remembers as harvested (a hostile chunk and a very long game).
    inline constexpr std::size_t kMaxHarvested = 65536;
    /// The chance in percent of a record with four zero chances (no seasonal data). Ours.
    inline constexpr float kUnseasonalChance = 100.f;
    /// The word of the activate prompt when the record names none.
    inline constexpr std::string_view kDefaultVerb = "Harvest";
    /// The level a leveled ingredient list is resolved at (a stand-in; the data's lists are not level dependent).
    inline constexpr std::uint16_t kListLevel = 1;

    enum class Season : std::uint8_t
    {
        Spring,
        Summer,
        Fall,
        Winter,
        Unknown, ///< the clock's month is not known yet (or out of range)
    };

    /// The month 0 to 11 (0 = Morning Star, the first of the year) to a season: months 2 to 4 spring, 5 to 7 summer, 8
    /// to 10 fall, 11, 0 and 1 winter. Anything else is Unknown. OURS.
    [[nodiscard]] Season seasonOfMonth(int month);

    /// The chance in percent that a harvest gives its ingredient: the PFPC byte of the season; all four zero is
    /// kUnseasonalChance; an Unknown season takes the best of the four. 0 means nothing grows now.
    [[nodiscard]] float chanceIn(const std::array<std::uint8_t, 4>& chances, Season season);

    /// The seeded roll: a hash of (placement, attempt) mapped to [0, 100); true when it is below `chancePercent`.
    /// 0 or below never succeeds, 100 or above always does.
    [[nodiscard]] bool harvestRoll(FormId placement, std::uint32_t attempt, float chancePercent);

    /// The seed of the ingredient pick of one harvest (a leveled list picks with it).
    [[nodiscard]] std::uint64_t pickSeed(FormId placement, std::uint32_t attempt);

    /// "Harvest Canis Root": the activate text (empty: "Harvest") and the display name (empty: just the verb).
    [[nodiscard]] std::string promptFor(std::string_view activateText, std::string_view name);

    /// The plants harvested and the game second each grows back at. Main thread.
    class HarvestBook
    {
    public:
        /// Records a harvest; false for the null form, a plant already in the book, or a full book.
        bool mark(FormId placement, double due);
        /// True while the plant is harvested and has not grown back by `now` (a negative `now`, the clock not known
        /// yet, counts every entry as harvested).
        [[nodiscard]] bool hidden(FormId placement, double now) const;
        [[nodiscard]] bool has(FormId placement) const { return mEntries.count(placement) != 0; }
        [[nodiscard]] const std::map<FormId, double>& entries() const { return mEntries; }
        /// Removes every entry due at `now` (>= 0) and appends its placement to `out`, ascending.
        void collectGrown(double now, std::vector<FormId>& out);
        /// The tries made; takeAttempt() returns the number to roll with and counts it.
        [[nodiscard]] std::uint32_t attempts() const { return mAttempts; }
        std::uint32_t takeAttempt() { return mAttempts++; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }
        void clear();

        /// The save form: u32 count, then per plant u32 placement (ascending, not 0) and f64 due time (finite), then
        /// u32 attempts. A read that is damaged, over the cap, not ascending, not finite or followed by bytes changes
        /// nothing.
        void write(openeden::sdk::ChunkWriter& writer) const;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

    private:
        std::map<FormId, double> mEntries;
        std::uint32_t mAttempts = 0;
        std::uint32_t mRevision = 0;
    };

    class HarvestChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "harvest";
        explicit HarvestChunk(HarvestBook& book)
            : mBook(book)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        /// Called with the number of plants after a chunk was read and applied.
        void setRestoredSink(std::function<void(std::size_t)> sink) { mRestored = std::move(sink); }
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mBook.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        HarvestBook& mBook;
        std::function<void(const std::string&)> mSink;
        std::function<void(std::size_t)> mRestored;
        std::string mRefusal;
    };
}

#endif

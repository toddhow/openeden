#ifndef OPENEDEN_GAME_SKYRIMSE_PICKUP_H
#define OPENEDEN_GAME_SKYRIMSE_PICKUP_H

// Picking up placed items and pickpocketing (task 0208): the pure rules. Standard library and the SDK save chunk types
// only; no clock, no records.
//
// What is the data's and what is ours: the item's value and weight and the owner of a placement are the data's; the
// pickpocket chance is OURS (every constant below is a named estimate, RECALLED-VERIFY against Skyrim's own formula,
// which is not reproduced here); the roll is a seeded hash, so a scenario run twice gives the same answers.
#include <game_skyrimse/records.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace game_skyrimse::pickup
{
    /// The most placements a session remembers as taken (a hostile chunk and a very long game).
    inline constexpr std::size_t kMaxTaken = 65536;
    /// A placed stack larger than this is cut (a hostile XCNT).
    inline constexpr std::int32_t kMaxPickupCount = 1000000;
    /// The Pickpocket skill when the progression is off: a new character's 15 (RECALLED-VERIFY).
    inline constexpr float kStandInPickpocket = 15.f;

    /// The set of placements the player took, and the number of pickpocket tries so far (the seed of the next roll).
    /// Main thread.
    class PickupBook
    {
    public:
        /// False for the null form, a placement already in the set, or a full set.
        bool markTaken(FormId placement);
        [[nodiscard]] bool taken(FormId placement) const { return mTaken.count(placement) != 0; }
        [[nodiscard]] const std::set<FormId>& placements() const { return mTaken; }
        /// The tries made; takeAttempt() returns the number to roll with and counts it.
        [[nodiscard]] std::uint32_t attempts() const { return mAttempts; }
        std::uint32_t takeAttempt() { return mAttempts++; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }
        void clear();

        /// The save form: u32 count, u32 placements ascending (not 0), u32 attempts. A read that is damaged, over
        /// the cap, not ascending or followed by bytes changes nothing.
        void write(openeden::sdk::ChunkWriter& writer) const;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

    private:
        std::set<FormId> mTaken;
        std::uint32_t mAttempts = 0;
        std::uint32_t mRevision = 0;
    };

    class PickupChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "pickups";
        explicit PickupChunk(PickupBook& book)
            : mBook(book)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mBook.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        PickupBook& mBook;
        std::function<void(const std::string&)> mSink;
        std::string mRefusal;
    };

    /// The count of a placed stack from its XCNT: 0 or below (none, or hostile) is one unit, above the cap is cut.
    [[nodiscard]] std::int32_t placedCount(std::int32_t xcnt);

    struct PickpocketSettings
    {
        /// chance (percent) = base + skillGain * skill - valueTerm * sqrt(value * count) - weightTerm * weight * count,
        /// then clamped to [min, max]. All OURS.
        float mBase = 25.f;
        float mSkillGain = 0.6f;
        float mValueTerm = 3.f;
        float mWeightTerm = 1.5f;
        float mMin = 5.f;
        float mMax = 90.f;
    };

    /// The chance in percent. A non-finite skill, value or weight (or a negative value, weight or count) reads as 0 for
    /// that term; the result is always in [min, max].
    [[nodiscard]] float pickpocketChance(
        const PickpocketSettings& settings, float skill, std::int32_t value, float weight, std::int32_t count);

    /// Task 0275: the chance after a perk's bonus (Mod Pickpocket Chance, entry point 56: percentage points, may be
    /// negative), clamped to [min, max] again. A bonus that is not a number counts as 0.
    [[nodiscard]] float chanceWithBonus(const PickpocketSettings& settings, float percent, float bonus);

    /// Task 0275: the roll of a reverse pickpocket (planting) is seeded from the owner xor this, so that planting an
    /// item and stealing it from the same NPC are not the same draw.
    inline constexpr std::uint64_t kPlantSalt = 0x504C414E54000000ull;

    /// The seeded roll: a hash of (owner, item, attempt) mapped to [0, 100); true when it is below `chancePercent`.
    /// 0 or below never succeeds, 100 or above always does.
    [[nodiscard]] bool pickpocketRoll(std::uint64_t owner, FormId item, std::uint32_t attempt, float chancePercent);

    /// The skill training of a try: the value of what was tried for (at least 1), a failure counts the same.
    [[nodiscard]] float pickpocketTraining(std::int32_t value, std::int32_t count);
}

#endif

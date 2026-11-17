#ifndef OPENEDEN_GAME_SKYRIMSE_SERVICES_H
#define OPENEDEN_GAME_SKYRIMSE_SERVICES_H

// The dialogue services of a Skyrim SE game that are not barter: training, renting a room and gifts (task 0219,
// milestone M-S6). Off unless [extension.skyrimse] services (and the inventory, which holds the player's gold).
//
// Task 0155 recognises a response whose INFO fragment script calls a menu (`classifyService`); barter is task 0189.
// When the pack setting is on, the dialogue runtime marks the three services this file builds as "open:<name>" and the
// engine sends the scripts an event instead of logging "service not implemented". The Lua windows
// (files/data/openeden/skyrimse/services_windows.lua) draw `core.skyrimse.services`, whose state lives here.
//
// TRAINING. Settled by the data (five masters): a trainer is an NPC whose CLAS has a trained skill and a maximum
// training level: DATA byte 4 is the skill slot 0 to 17 (One-Handed first, the order of the 18 skills) and byte 5 the
// level the trainer can teach up to. The classes are named for it: `TrainerSmithingJourneyman` is skill 4 (Smithing)
// at 50, `...Expert` is 75, `...Master` is 90 (VERIFIED by task 0219's survey over the trainer classes: every class
// named Trainer<Skill><Rank> has that skill and that cap). The game settings fTrainingMultCost (10.0),
// iTrainingExpertCost (3) and iTrainingMasterCost (5) exist in Skyrim.esm; there is NO iTrainingNumAllowedPerLevel.
//   RECALLED-VERIFY, tunable (the numbers below are named, nothing is hidden):
//   - five trainings per player level (Skyrim's rule: "up to five times per level");
//   - the price of one training is skill level * fTrainingMultCost (Skyrim's formula is not in the data; the
//     Journeyman/Expert/Master multipliers are not applied);
//   - the trainer teaches while the skill is below its maximum training level.
// Paying removes gold from the player (the inventory model) and the skill rises by one level through the progression.
//
// RENTING A ROOM. `RentRoomScript` (Skyrim - Misc.bsa, decompiled by task 0219) is attached to the innkeeper NPC_
// (VMAD of the base record); its property `Bed` is the room's bed REFR. RentRoom(DialogueGenericScript): the bed's
// owner
// becomes the player, the player pays `RoomRentalCost` gold (the global `RoomCost` = 10, VERIFIED) and a game time
// timer of `RentHours` (24.0, the script property's default, VERIFIED) clears the room again (the owner goes back to
// the innkeeper); the innkeeper's Variable09 is set to 1 meanwhile (what that gates is not reproduced). Reproduced
// here: the cost, the 24 hours and the bed becoming the player's for that time, so the bed ownership check of the rest
// service (task 0210) and the bed trespass (task 0213) both allow it. Not reproduced: Variable09, `WI.ShowPlayerRoom`
// (an immersion message), the innkeeper's death clearing the room.
//
// GIFTS and CARRY. `ShowGiftMenu` opens a transfer of items to the NPC. The pack registers the NPC's inventory as an
// owner of the inventory model (task 0182) and the Lua window moves items to it; what Skyrim does with a gift
// (disposition, the follower's use of it) is not built. A follower's `OpenInventory` lines ("carry") reuse the
// container window of task 0182 on the NPC's owner (services_global.lua), with no state here.
//
// Namespace skyrimse.services 1.0 (Read unless marked), all text ids are hex:
//   state() -> string                 "none", "pending", "training", "rent", "gift" or "refused"
//   reason() -> string                why a service was refused ("" otherwise)
//   name() -> string                  the NPC's name
//   revision() -> int                 moves on every change a window should redraw for
//   gold() -> int                     the player's gold
//   training: skill_name(), skill_level(), max_level(), cost(), trains_left(), can_train() -> string ("" when the
//             player may train, else a short reason: "gold", "skill", "limit", "not_ready")
//   rent: rent_cost(), rent_hours(), rent_state() -> "available", "active", "no_gold" or "none"; rent_hours_left()
//   gift: owner() -> string           the NPC's owner id in the inventory model (hex) once registered
//   last_result() -> string, results() -> int (counts every attempt)
//   rented(bed) -> bool               the bed placement is rented now (hex)
//   open(service, placement, npc_base, name)  [Mutate]   service "training", "rent" or "gift"
//   train(), rent(), close()          [Mutate]
//   set_clock(seconds)                [Mutate]   the game time (rentals end on it)
//
// Threads: main thread only. No engine, renderer or script-language type.

#include <game_skyrimse/records.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse::services
{
    /// OURS, RECALLED-VERIFY (see the file comment). Everything tunable is in `ServicesSettings`.
    inline constexpr int kTrainsPerLevel = 5;
    /// fTrainingMultCost in Skyrim.esm (VERIFIED value 10.0).
    inline constexpr float kTrainingCostMult = 10.f;
    /// RentRoomScript's use: DialogueGenericScript.RentHours (default 24.0) and the global RoomCost (10.0), VERIFIED.
    inline constexpr double kRentHours = 24.0;
    inline constexpr std::int32_t kRoomCost = 10;
    /// A save keeps at most this many rentals (a hostile or damaged chunk is refused beyond it).
    inline constexpr std::size_t kMaxRentals = 64;
    /// The skill slots (One-Handed to Enchanting).
    inline constexpr std::size_t kSkillSlots = 18;

    struct ServicesSettings
    {
        int mTrainsPerLevel = kTrainsPerLevel;
        float mTrainingCostMult = kTrainingCostMult;
        double mRentHours = kRentHours;
        std::int32_t mRoomCost = kRoomCost;
    };

    enum class Lookup : std::uint8_t
    {
        Ready,
        Pending, ///< the records are not read yet; ask again
        None, ///< this NPC does not offer the service
    };

    // ---- training --------------------------------------------------------------------------------------------------

    struct TrainerInfo
    {
        std::size_t mSlot = 0; ///< the skill slot, 0 to 17
        int mMaxLevel = 0; ///< the trainer teaches up to this skill level
    };

    /// A trainer from a class's DATA: the class has data, a skill slot below `kSkillSlots` and a maximum above 0.
    [[nodiscard]] bool makeTrainer(bool hasData, std::uint8_t trainedSkill, std::uint8_t maxLevel, TrainerInfo& out);

    /// The price of one training at `skillLevel`: round(skillLevel * mult); a level below 0 or a mult that is not
    /// finite or not above 0 costs 0.
    [[nodiscard]] std::int32_t trainingCost(int skillLevel, float mult);

    enum class TrainStatus : std::uint8_t
    {
        Ok,
        NotReady, ///< the skill or the player's level is not known yet
        SkillTooHigh, ///< the skill is at or above the trainer's maximum
        LimitReached, ///< `perLevel` trainings were done at this level
        NotEnoughGold,
    };
    /// "" for Ok, else "not_ready", "skill", "limit" or "gold".
    [[nodiscard]] std::string_view trainStatusName(TrainStatus status);
    /// Pure. The first reason that holds, in the order of the enum.
    [[nodiscard]] TrainStatus decideTraining(
        int skillLevel, int maxLevel, int trainedThisLevel, int perLevel, std::int64_t gold, std::int32_t cost);

    /// How many trainings the player did at which player level (the limit is per level, so a level up resets it).
    class TrainingLedger
    {
    public:
        [[nodiscard]] int countAt(std::uint16_t playerLevel) const { return playerLevel == mLevel ? mCount : 0; }
        void record(std::uint16_t playerLevel);
        void restore(std::uint16_t level, int count);
        [[nodiscard]] std::uint16_t level() const { return mLevel; }
        [[nodiscard]] int count() const { return mCount; }

    private:
        std::uint16_t mLevel = 0;
        int mCount = 0;
    };

    // ---- rooms -----------------------------------------------------------------------------------------------------

    struct RentOffer
    {
        FormId mBed = kNullFormId; ///< the placement (REFR) of the room's bed
    };

    struct Rental
    {
        FormId mBed = kNullFormId;
        double mExpires = 0.0; ///< game seconds
    };

    enum class RentStatus : std::uint8_t
    {
        Ok,
        AlreadyRented,
        NotEnoughGold,
    };
    [[nodiscard]] std::string_view rentStatusName(RentStatus status); ///< "", "rented", "gold"
    [[nodiscard]] RentStatus decideRent(bool alreadyRented, std::int64_t gold, std::int32_t cost);

    /// The rented beds and when each rental ends.
    class RentalBook
    {
    public:
        /// Starts or renews a rental from `now` for `hours` game hours. False for a bed of 0, a time that is not
        /// finite, hours that are not above 0 or a full book (kMaxRentals); expired rentals are dropped first.
        bool rent(FormId bed, double now, double hours);
        [[nodiscard]] bool active(FormId bed, double now) const;
        [[nodiscard]] double hoursLeft(FormId bed, double now) const; ///< 0 when not rented
        [[nodiscard]] const std::vector<Rental>& entries() const { return mRentals; }
        /// Replaces the book (a load): refuses more than kMaxRentals, a bed of 0, a duplicate or a time that is not
        /// finite; the book is unchanged then.
        bool restore(std::vector<Rental> rentals);
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }

    private:
        std::vector<Rental> mRentals; ///< sorted by bed
        std::uint32_t mRevision = 0;
    };

    // ---- the save chunk --------------------------------------------------------------------------------------------

    /// The pack's chunk "services" (version 1): the training ledger (u32 level, u32 count) and the rentals (u32 count,
    /// then u32 bed and f64 expiry each). Strict: the version and the size must fit; a refused chunk changes nothing.
    class ServicesChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "services";
        ServicesChunk(TrainingLedger& ledger, RentalBook& book)
            : mLedger(ledger)
            , mBook(book)
        {
        }
        void setRestoredSink(std::function<void(const std::string&)> sink) { mRestored = std::move(sink); }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override;
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        TrainingLedger& mLedger;
        RentalBook& mBook;
        std::function<void(const std::string&)> mRestored;
        std::function<void(const std::string&)> mSink;
    };

    // ---- the service -----------------------------------------------------------------------------------------------

    struct Hooks
    {
        std::function<Lookup(FormId npcBase, TrainerInfo& out)> mTrainerOf;
        std::function<Lookup(FormId npcBase, RentOffer& out)> mRentOf;
        /// The player's skill level in a slot (-1 when unknown), the player's level (0 when unknown), and raising the
        /// skill by one level (the progression's raiseSkill).
        std::function<int(std::size_t slot)> mSkillLevel;
        std::function<std::uint16_t()> mPlayerLevel;
        std::function<bool(std::size_t slot)> mRaise;
        std::function<std::string(std::size_t slot)> mSkillName;
        /// The player's gold, and taking `amount` of it (false when it cannot be taken).
        std::function<std::int64_t()> mGold;
        std::function<bool(std::int32_t amount)> mPay;
        /// Registers an NPC's inventory as an owner of the inventory model (a gift); false when it cannot.
        std::function<bool(std::uint64_t placement, FormId npcBase)> mOpenOwner;
        std::function<void(const std::string&)> mLog;
    };

    class ServicesService
    {
    public:
        enum class State : std::uint8_t
        {
            None,
            Pending,
            Training,
            Rent,
            Gift,
            Refused,
        };
        /// A session still pending after this many polls is refused ("the records were not ready").
        static constexpr std::uint32_t kMaxPendingPolls = 3000;

        explicit ServicesService(Hooks hooks, ServicesSettings settings = ServicesSettings());
        ~ServicesService();
        ServicesService(const ServicesService&) = delete;
        ServicesService& operator=(const ServicesService&) = delete;

        /// Starts a service ("training", "rent" or "gift") for the NPC `placement`/`npcBase` (replacing any); tries at
        /// once, otherwise `poll()` goes on. Main thread.
        void open(std::string_view service, std::uint64_t placement, FormId npcBase, std::string name);
        void poll(); ///< retries a pending session; never waits
        void close();
        void setClock(double gameSeconds);

        bool train(); ///< one training; false when refused (see lastResult)
        bool rent(); ///< rents the room; false when refused

        [[nodiscard]] State state() const { return mState; }
        [[nodiscard]] const std::string& reason() const { return mReason; }
        [[nodiscard]] const std::string& name() const { return mName; }
        [[nodiscard]] const std::string& lastResult() const { return mLastResult; }
        [[nodiscard]] std::uint32_t results() const { return mResults; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision + mRentals.revision(); }
        [[nodiscard]] std::uint64_t owner() const { return mState == State::Gift ? mPlacement : 0; }

        [[nodiscard]] const TrainerInfo& trainer() const { return mTrainer; }
        [[nodiscard]] int skillLevel() const;
        [[nodiscard]] std::int32_t trainingCostNow() const;
        [[nodiscard]] int trainsLeft() const;
        [[nodiscard]] TrainStatus trainStatus() const;

        [[nodiscard]] std::int32_t rentCost() const { return mSettings.mRoomCost; }
        [[nodiscard]] double rentHours() const { return mSettings.mRentHours; }
        /// "none" outside the rent state, else "active" (this room is rented), "no_gold" or "available".
        [[nodiscard]] std::string_view rentState() const;
        [[nodiscard]] double rentHoursLeft() const;

        [[nodiscard]] TrainingLedger& ledger() { return mLedger; }
        [[nodiscard]] RentalBook& rentals() { return mRentals; }
        [[nodiscard]] const RentalBook& rentals() const { return mRentals; }
        [[nodiscard]] double now() const { return mNow; }
        [[nodiscard]] bool isRented(FormId bed) const { return mRentals.active(bed, mNow); }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        bool tryStart();
        void refuse(std::string reason);
        void log(const std::string& line) const;
        void result(std::string text);

        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        ServicesSettings mSettings;
        State mState = State::None;
        std::string mWanted; ///< "training", "rent" or "gift" while pending or running
        std::string mReason;
        std::string mName;
        std::string mLastResult;
        std::uint64_t mPlacement = 0;
        FormId mNpcBase = kNullFormId;
        TrainerInfo mTrainer;
        RentOffer mOffer;
        TrainingLedger mLedger;
        RentalBook mRentals;
        double mNow = 0.0;
        std::uint32_t mPolls = 0;
        std::uint32_t mResults = 0;
        std::uint32_t mRevision = 0;
    };

    /// The dialogue service an INFO's fragment names ("training", "rent a room", "gift") as the name the scripts and
    /// the engine use after "open:" ("training", "rent", "gift"); "" for anything else (barter among them). Pure.
    [[nodiscard]] std::string_view openedServiceName(std::string_view classified);
    /// "open:training" and so on, or "" when `classified` is not one of the services above. Pure.
    [[nodiscard]] std::string openedServiceTag(std::string_view classified);
    /// A follower's inventory line (`Actor.OpenInventory`, the "I need to trade some things with you" family) is not a
    /// service the base classifier names (task 0155 left it as plain text). With the services on it is "carry": the
    /// container window on the NPC's inventory. `referencedNames` are the .pex names, lower case. "" otherwise. Pure.
    [[nodiscard]] std::string classifyCarry(const std::vector<std::string>& referencedNames);
}

#endif

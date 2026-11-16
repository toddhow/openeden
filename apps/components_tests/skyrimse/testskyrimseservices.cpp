// Task 0219: the Skyrim SE dialogue services that are not barter (game_packs/skyrimse services.*): training, renting a
// room and gifts. Written before the code and run red against a stub first.
#include <game_skyrimse/services.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse::services;
    using game_skyrimse::FormId;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

    // ---- training rules ---------------------------------------------------------------------------------------------

    TEST(SkyrimServicesTrainer, aClassWithASkillSlotAndAMaximumIsATrainer)
    {
        TrainerInfo t;
        ASSERT_TRUE(makeTrainer(true, 4, 50, t)); // TrainerSmithingJourneyman
        EXPECT_EQ(t.mSlot, 4u);
        EXPECT_EQ(t.mMaxLevel, 50);
        ASSERT_TRUE(makeTrainer(true, 17, 90, t)); // Enchanting, the last slot
        EXPECT_EQ(t.mSlot, 17u);
    }

    TEST(SkyrimServicesTrainer, noDataNoSkillOrNoMaximumIsNoTrainer)
    {
        TrainerInfo t;
        EXPECT_FALSE(makeTrainer(false, 4, 50, t));
        EXPECT_FALSE(makeTrainer(true, 18, 50, t)); // a slot past Enchanting
        EXPECT_FALSE(makeTrainer(true, 255, 50, t)); // "no skill"
        EXPECT_FALSE(makeTrainer(true, 4, 0, t)); // a class that teaches nothing
    }

    TEST(SkyrimServicesTrainer, theCostIsTheSkillLevelTimesTheMultiplierRounded)
    {
        EXPECT_EQ(trainingCost(15, 10.f), 150);
        EXPECT_EQ(trainingCost(0, 10.f), 0);
        EXPECT_EQ(trainingCost(7, 2.5f), 18); // 17.5 rounds up
        EXPECT_EQ(trainingCost(-3, 10.f), 0);
        EXPECT_EQ(trainingCost(15, 0.f), 0);
        EXPECT_EQ(trainingCost(15, -4.f), 0);
        EXPECT_EQ(trainingCost(15, static_cast<float>(kNan)), 0);
    }

    TEST(SkyrimServicesTraining, theFirstReasonThatHoldsWinsInTheDocumentedOrder)
    {
        EXPECT_EQ(decideTraining(15, 50, 0, 5, 1000, 150), TrainStatus::Ok);
        EXPECT_EQ(decideTraining(-1, 50, 0, 5, 1000, 150), TrainStatus::NotReady);
        EXPECT_EQ(decideTraining(50, 50, 0, 5, 1000, 150), TrainStatus::SkillTooHigh);
        EXPECT_EQ(decideTraining(60, 50, 5, 5, 0, 600), TrainStatus::SkillTooHigh); // skill before limit and gold
        EXPECT_EQ(decideTraining(15, 50, 5, 5, 0, 150), TrainStatus::LimitReached); // limit before gold
        EXPECT_EQ(decideTraining(15, 50, 4, 5, 149, 150), TrainStatus::NotEnoughGold);
        EXPECT_EQ(decideTraining(15, 50, 4, 5, 150, 150), TrainStatus::Ok); // exactly enough
    }

    TEST(SkyrimServicesTraining, statusesHaveStableNames)
    {
        EXPECT_EQ(trainStatusName(TrainStatus::Ok), "");
        EXPECT_EQ(trainStatusName(TrainStatus::NotReady), "not_ready");
        EXPECT_EQ(trainStatusName(TrainStatus::SkillTooHigh), "skill");
        EXPECT_EQ(trainStatusName(TrainStatus::LimitReached), "limit");
        EXPECT_EQ(trainStatusName(TrainStatus::NotEnoughGold), "gold");
    }

    TEST(SkyrimServicesLedger, theLimitIsPerPlayerLevel)
    {
        TrainingLedger l;
        EXPECT_EQ(l.countAt(1), 0);
        l.record(1);
        l.record(1);
        EXPECT_EQ(l.countAt(1), 2);
        EXPECT_EQ(l.countAt(2), 0); // another level has its own count
        l.record(2); // a new level starts a new count
        EXPECT_EQ(l.countAt(2), 1);
        EXPECT_EQ(l.countAt(1), 0);
        l.restore(7, 3);
        EXPECT_EQ(l.countAt(7), 3);
        EXPECT_EQ(l.level(), 7);
        EXPECT_EQ(l.count(), 3);
    }

    // ---- rooms ------------------------------------------------------------------------------------------------------

    TEST(SkyrimServicesRent, aRentalIsRefusedWhenOneIsActiveOrGoldIsShort)
    {
        EXPECT_EQ(decideRent(false, 10, 10), RentStatus::Ok);
        EXPECT_EQ(decideRent(true, 1000, 10), RentStatus::AlreadyRented);
        EXPECT_EQ(decideRent(false, 9, 10), RentStatus::NotEnoughGold);
        EXPECT_EQ(rentStatusName(RentStatus::Ok), "");
        EXPECT_EQ(rentStatusName(RentStatus::AlreadyRented), "rented");
        EXPECT_EQ(rentStatusName(RentStatus::NotEnoughGold), "gold");
    }

    TEST(SkyrimServicesBook, aRentalLastsTheHoursAndThenEnds)
    {
        RentalBook b;
        ASSERT_TRUE(b.rent(0x616ca, 1000.0, 24.0));
        EXPECT_TRUE(b.active(0x616ca, 1000.0));
        EXPECT_TRUE(b.active(0x616ca, 1000.0 + 24 * 3600.0 - 1.0));
        EXPECT_FALSE(b.active(0x616ca, 1000.0 + 24 * 3600.0));
        EXPECT_FALSE(b.active(0x616cb, 1000.0));
        EXPECT_NEAR(b.hoursLeft(0x616ca, 1000.0 + 3600.0 * 6), 18.0, 1e-9);
        EXPECT_DOUBLE_EQ(b.hoursLeft(0x616ca, 1000.0 + 25 * 3600.0), 0.0);
        EXPECT_DOUBLE_EQ(b.hoursLeft(0x1, 1000.0), 0.0);
    }

    TEST(SkyrimServicesBook, renewingStartsTheTimeAgainAndBadInputIsRefused)
    {
        RentalBook b;
        ASSERT_TRUE(b.rent(5, 0.0, 24.0));
        ASSERT_TRUE(b.rent(5, 3600.0 * 20, 24.0));
        EXPECT_EQ(b.entries().size(), 1u);
        EXPECT_TRUE(b.active(5, 3600.0 * 40));
        EXPECT_FALSE(b.rent(0, 0.0, 24.0)); // no bed
        EXPECT_FALSE(b.rent(6, kNan, 24.0));
        EXPECT_FALSE(b.rent(6, 0.0, 0.0));
        EXPECT_FALSE(b.rent(6, 0.0, -1.0));
        EXPECT_FALSE(b.rent(6, 0.0, kNan));
        EXPECT_EQ(b.entries().size(), 1u);
    }

    TEST(SkyrimServicesBook, theBookIsBoundedAndDropsEndedRentalsFirst)
    {
        RentalBook b;
        for (FormId i = 1; i <= kMaxRentals; ++i)
            ASSERT_TRUE(b.rent(i, 0.0, 1.0));
        EXPECT_FALSE(b.rent(1000, 10.0, 1.0)); // all still active at game time 10
        EXPECT_TRUE(b.rent(1000, 3601.0, 1.0)); // the others ended
        EXPECT_EQ(b.entries().size(), 1u);
    }

    TEST(SkyrimServicesBook, theRevisionMovesOnEachChange)
    {
        RentalBook b;
        const std::uint32_t r0 = b.revision();
        ASSERT_TRUE(b.rent(5, 0.0, 24.0));
        EXPECT_NE(b.revision(), r0);
    }

    TEST(SkyrimServicesBook, aLoadedBookIsCheckedAndAbadOneChangesNothing)
    {
        RentalBook b;
        ASSERT_TRUE(b.rent(5, 0.0, 24.0));
        EXPECT_FALSE(b.restore({ { 0, 10.0 } })); // a bed of 0
        EXPECT_FALSE(b.restore({ { 7, kNan } }));
        EXPECT_FALSE(b.restore({ { 7, 10.0 }, { 7, 20.0 } })); // a duplicate
        std::vector<Rental> many;
        for (FormId i = 1; i <= kMaxRentals + 1; ++i)
            many.push_back({ i, 10.0 });
        EXPECT_FALSE(b.restore(many));
        EXPECT_EQ(b.entries().size(), 1u);
        EXPECT_EQ(b.entries()[0].mBed, 5u);
        ASSERT_TRUE(b.restore({ { 9, 100.0 }, { 3, 50.0 } }));
        ASSERT_EQ(b.entries().size(), 2u);
        EXPECT_EQ(b.entries()[0].mBed, 3u); // sorted by bed
        EXPECT_TRUE(b.active(9, 99.0));
    }

    // ---- the chunk --------------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    TEST(SkyrimServicesChunk, theLedgerAndTheRentalsRoundTrip)
    {
        TrainingLedger ledger;
        RentalBook book;
        ledger.restore(4, 3);
        ASSERT_TRUE(book.rent(0x616ca, 500.0, 24.0));
        ServicesChunk chunk(ledger, book);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes.size(), 4u + 4u + 4u + 12u);

        TrainingLedger ledger2;
        RentalBook book2;
        ServicesChunk back(ledger2, book2);
        std::string restored;
        back.setRestoredSink([&](const std::string& s) { restored = s; });
        MemoryReader r(w.mBytes);
        ASSERT_TRUE(back.read(r, ServicesChunk::kVersion));
        EXPECT_EQ(ledger2.countAt(4), 3);
        ASSERT_EQ(book2.entries().size(), 1u);
        EXPECT_EQ(book2.entries()[0].mBed, 0x616cau);
        EXPECT_DOUBLE_EQ(book2.entries()[0].mExpires, 500.0 + 24 * 3600.0);
        EXPECT_FALSE(restored.empty());
    }

    TEST(SkyrimServicesChunk, anEmptyStateIsAValidChunk)
    {
        TrainingLedger ledger;
        RentalBook book;
        ServicesChunk chunk(ledger, book);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes.size(), 12u);
        MemoryReader r(w.mBytes);
        EXPECT_TRUE(chunk.read(r, 1));
    }

    TEST(SkyrimServicesChunk, aWrongVersionSizeOrCountIsRefusedAndChangesNothing)
    {
        TrainingLedger ledger;
        RentalBook book;
        ledger.restore(9, 2);
        ASSERT_TRUE(book.rent(5, 0.0, 24.0));
        ServicesChunk chunk(ledger, book);
        std::string reason;
        chunk.setRefusalSink([&](const std::string& s) { reason = s; });
        const auto refused = [&](const std::vector<unsigned char>& bytes, std::uint32_t version) {
            MemoryReader r(bytes);
            EXPECT_FALSE(chunk.read(r, version));
            EXPECT_FALSE(reason.empty());
            reason.clear();
            EXPECT_EQ(ledger.countAt(9), 2);
            EXPECT_EQ(book.entries().size(), 1u);
        };
        const std::vector<unsigned char> twelve(12, 0);
        refused(twelve, 2); // wrong version
        refused({}, 1);
        refused(std::vector<unsigned char>(11, 0), 1);
        refused(std::vector<unsigned char>(13, 0), 1);
        // a count that promises more than the bytes hold
        std::vector<unsigned char> lying(12, 0);
        lying[8] = 3;
        refused(lying, 1);
        // a count above the cap
        std::vector<unsigned char> huge(12, 0);
        huge[8] = static_cast<unsigned char>(kMaxRentals + 1);
        refused(huge, 1);
        // a negative or non-finite expiry
        std::vector<unsigned char> bad(24, 0);
        bad[8] = 1;
        bad[12] = 5; // bed 5
        const double nan = kNan;
        std::memcpy(bad.data() + 16, &nan, 8);
        refused(bad, 1);
    }

    // ---- the service ------------------------------------------------------------------------------------------------

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            mArgs = std::move(args);
            return it->second.handler->call(mArgs);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mArgs;
    };

    BindingValue str(const std::string& v)
    {
        BindingValue b;
        b.type = BindingType::String;
        b.text = v;
        return b;
    }
    BindingValue dbl(double v)
    {
        BindingValue b;
        b.type = BindingType::Double;
        b.number = v;
        return b;
    }

    constexpr FormId kTrainerNpc = 0x1a6d9;
    constexpr FormId kInnNpc = 0x13ba3;
    constexpr FormId kPlainNpc = 0x500;
    constexpr FormId kLateNpc = 0x501;
    constexpr FormId kBed = 0x616ca;

    struct ServicesFixture : testing::Test
    {
        std::vector<std::string> mLog;
        std::int64_t mGoldValue = 1000;
        int mSkill[kSkillSlots] = {};
        std::uint16_t mLevelValue = 3;
        std::vector<std::size_t> mRaised;
        bool mRaiseWorks = true;
        std::vector<std::int32_t> mPaid;
        int mLateAsks = 0;
        std::vector<std::uint64_t> mOpened;
        bool mOpenWorks = true;
        std::unique_ptr<ServicesService> mService;
        FakeBindings mBindings;

        void SetUp() override
        {
            mSkill[3] = 15; // Block, the trainer's skill
            Hooks h;
            h.mTrainerOf = [this](FormId npc, TrainerInfo& out) {
                if (npc == kTrainerNpc)
                {
                    out.mSlot = 3;
                    out.mMaxLevel = 75;
                    return Lookup::Ready;
                }
                if (npc == kLateNpc)
                {
                    if (++mLateAsks < 4)
                        return Lookup::Pending;
                    out.mSlot = 3;
                    out.mMaxLevel = 50;
                    return Lookup::Ready;
                }
                return npc == kPlainNpc ? Lookup::None : Lookup::Pending;
            };
            h.mRentOf = [](FormId npc, RentOffer& out) {
                if (npc == kInnNpc)
                {
                    out.mBed = kBed;
                    return Lookup::Ready;
                }
                return npc == kPlainNpc ? Lookup::None : Lookup::Pending;
            };
            h.mSkillLevel = [this](std::size_t slot) { return slot < kSkillSlots ? mSkill[slot] : -1; };
            h.mPlayerLevel = [this] { return mLevelValue; };
            h.mRaise = [this](std::size_t slot) {
                if (!mRaiseWorks)
                    return false;
                ++mSkill[slot];
                mRaised.push_back(slot);
                return true;
            };
            h.mSkillName = [](std::size_t slot) { return slot == 3 ? std::string("Block") : std::string("Skill"); };
            h.mGold = [this] { return mGoldValue; };
            h.mPay = [this](std::int32_t amount) {
                if (amount > mGoldValue)
                    return false;
                mGoldValue -= amount;
                mPaid.push_back(amount);
                return true;
            };
            h.mOpenOwner = [this](std::uint64_t placement, FormId) {
                mOpened.push_back(placement);
                return mOpenWorks;
            };
            h.mLog = [this](const std::string& s) { mLog.push_back(s); };
            mService = std::make_unique<ServicesService>(std::move(h));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }

        std::string text(const std::string& fn) { return std::string(mBindings.call(fn).value.text); }
        std::int32_t integer(const std::string& fn) { return mBindings.call(fn).value.integer; }
    };

    TEST_F(ServicesFixture, theNamespaceIsDeclaredWithReadAndMutateFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.services");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        for (const char* name : { "state", "reason", "name", "revision", "gold", "skill_name", "skill_level",
                 "max_level", "cost", "trains_left", "can_train", "rent_cost", "rent_hours", "rent_state",
                 "rent_hours_left", "owner", "last_result", "results", "rented", "open", "train", "rent", "close",
                 "set_clock" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        for (const char* name : { "open", "train", "rent", "close", "set_clock" })
            EXPECT_EQ(mBindings.mFunctions[name].access, openeden::sdk::BindingAccess::Mutate) << name;
        EXPECT_EQ(mBindings.mFunctions["state"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(ServicesFixture, aTrainerOpensTheTrainingWindowWithItsSkillAndPrice)
    {
        mBindings.call("open", { str("training"), str("1a6d9"), str("0001a6d9"), str("Njada") });
        EXPECT_EQ(text("state"), "training");
        EXPECT_EQ(text("name"), "Njada");
        EXPECT_EQ(text("skill_name"), "Block");
        EXPECT_EQ(integer("skill_level"), 15);
        EXPECT_EQ(integer("max_level"), 75);
        EXPECT_EQ(integer("cost"), 150);
        EXPECT_EQ(integer("trains_left"), 5);
        EXPECT_EQ(integer("gold"), 1000);
        EXPECT_EQ(text("can_train"), "");
    }

    TEST_F(ServicesFixture, trainingPaysRaisesTheSkillAndCountsTowardsTheLimit)
    {
        mService->open("training", 0x1a6d9, kTrainerNpc, "Njada");
        ASSERT_TRUE(mService->train());
        ASSERT_EQ(mRaised.size(), 1u);
        EXPECT_EQ(mRaised[0], 3u);
        ASSERT_EQ(mPaid.size(), 1u);
        EXPECT_EQ(mPaid[0], 150); // the price at the level before the training
        EXPECT_EQ(mGoldValue, 850);
        EXPECT_EQ(mService->trainsLeft(), 4);
        EXPECT_EQ(mService->skillLevel(), 16);
        EXPECT_EQ(mService->trainingCostNow(), 160);
        EXPECT_FALSE(mService->lastResult().empty());
    }

    TEST_F(ServicesFixture, theFifthTrainingIsTheLastOfALevelAndALevelUpResetsIt)
    {
        mGoldValue = 5000; // five trainings at 150 to 190 gold and one more after the level up
        mService->open("training", 0x1a6d9, kTrainerNpc, "Njada");
        for (int i = 0; i < 5; ++i)
            ASSERT_TRUE(mService->train()) << i;
        EXPECT_EQ(mService->trainsLeft(), 0);
        EXPECT_FALSE(mService->train());
        EXPECT_EQ(mRaised.size(), 5u); // the sixth did nothing
        EXPECT_EQ(text("can_train"), "limit");
        mLevelValue = 4;
        EXPECT_EQ(mService->trainsLeft(), 5);
        EXPECT_TRUE(mService->train());
    }

    TEST_F(ServicesFixture, aRefusedTrainingChangesNeitherGoldNorSkill)
    {
        mService->open("training", 0x1a6d9, kTrainerNpc, "Njada");
        mGoldValue = 149;
        EXPECT_EQ(text("can_train"), "gold");
        EXPECT_FALSE(mService->train());
        mGoldValue = 1000;
        mSkill[3] = 75; // at the trainer's maximum
        EXPECT_EQ(text("can_train"), "skill");
        EXPECT_FALSE(mService->train());
        EXPECT_TRUE(mRaised.empty());
        EXPECT_TRUE(mPaid.empty());
        EXPECT_EQ(mGoldValue, 1000);
        EXPECT_EQ(mService->trainsLeft(), 5);
    }

    TEST_F(ServicesFixture, aSkillThatCannotBeRaisedIsNotPaidFor)
    {
        mService->open("training", 0x1a6d9, kTrainerNpc, "Njada");
        mRaiseWorks = false;
        EXPECT_FALSE(mService->train());
        EXPECT_TRUE(mPaid.empty());
        EXPECT_EQ(mService->trainsLeft(), 5);
    }

    TEST_F(ServicesFixture, trainingOutsideATrainingSessionDoesNothing)
    {
        EXPECT_FALSE(mService->train());
        mService->open("rent", 0x1, kInnNpc, "Hulda");
        EXPECT_FALSE(mService->train());
        EXPECT_TRUE(mRaised.empty());
    }

    TEST_F(ServicesFixture, aNonTrainerIsRefusedWithAReason)
    {
        mService->open("training", 0x2, kPlainNpc, "Bob");
        EXPECT_EQ(text("state"), "refused");
        EXPECT_FALSE(text("reason").empty());
    }

    TEST_F(ServicesFixture, aPendingLookupIsRetriedByPollAndNeverBlocks)
    {
        mService->open("training", 0x3, kLateNpc, "Late");
        EXPECT_EQ(text("state"), "pending");
        for (int i = 0; i < 10 && mService->state() == ServicesService::State::Pending; ++i)
            mService->poll();
        EXPECT_EQ(text("state"), "training");
        EXPECT_EQ(integer("max_level"), 50);
    }

    TEST_F(ServicesFixture, aSessionThatNeverGetsItsDataIsRefusedAfterTheLimit)
    {
        mService->open("training", 0x4, 0x777, "Never");
        for (std::uint32_t i = 0; i < ServicesService::kMaxPendingPolls + 2; ++i)
            mService->poll();
        EXPECT_EQ(text("state"), "refused");
    }

    TEST_F(ServicesFixture, anUnknownServiceIsRefused)
    {
        mService->open("barter", 0x4, kTrainerNpc, "X");
        EXPECT_EQ(text("state"), "refused");
        mService->open("", 0x4, kTrainerNpc, "X");
        EXPECT_EQ(text("state"), "refused");
    }

    TEST_F(ServicesFixture, rentingTakesTheFeeAndRentsTheBedForTwentyFourHours)
    {
        mBindings.call("set_clock", { dbl(1000.0) });
        mService->open("rent", 0x1a66e, kInnNpc, "Hulda");
        EXPECT_EQ(text("state"), "rent");
        EXPECT_EQ(integer("rent_cost"), 10);
        EXPECT_EQ(mBindings.call("rent_hours").value.number, 24.0);
        EXPECT_EQ(text("rent_state"), "available");
        EXPECT_FALSE(mService->isRented(kBed));
        ASSERT_TRUE(mService->rent());
        ASSERT_EQ(mPaid.size(), 1u);
        EXPECT_EQ(mPaid[0], 10);
        EXPECT_TRUE(mService->isRented(kBed));
        EXPECT_EQ(text("rent_state"), "active");
        EXPECT_NEAR(mBindings.call("rent_hours_left").value.number, 24.0, 1e-9);
        const std::string bed = "000616ca";
        EXPECT_TRUE(mBindings.call("rented", { str(bed) }).value.boolean);
        // the game clock moves: after the rental the room is the innkeeper's again
        mBindings.call("set_clock", { dbl(1000.0 + 25 * 3600.0) });
        EXPECT_FALSE(mService->isRented(kBed));
        EXPECT_FALSE(mBindings.call("rented", { str(bed) }).value.boolean);
        EXPECT_EQ(text("rent_state"), "available");
    }

    TEST_F(ServicesFixture, aSecondRentalWhileOneIsActiveAndAShortPurseAreRefused)
    {
        mService->open("rent", 0x1a66e, kInnNpc, "Hulda");
        ASSERT_TRUE(mService->rent());
        EXPECT_FALSE(mService->rent());
        EXPECT_EQ(mPaid.size(), 1u);
        mService->close();
        mGoldValue = 9;
        mService->setClock(1e9);
        mService->open("rent", 0x1a66e, kInnNpc, "Hulda");
        EXPECT_EQ(text("rent_state"), "no_gold");
        EXPECT_FALSE(mService->rent());
        EXPECT_EQ(mPaid.size(), 1u);
    }

    TEST_F(ServicesFixture, anInnkeeperWithNoRoomIsRefused)
    {
        mService->open("rent", 0x2, kPlainNpc, "Bob");
        EXPECT_EQ(text("state"), "refused");
        EXPECT_EQ(text("rent_state"), "none");
    }

    TEST_F(ServicesFixture, aGiftRegistersTheNpcInventoryAndNamesItsOwner)
    {
        mService->open("gift", 0x1a66e, kInnNpc, "Hulda");
        EXPECT_EQ(text("state"), "gift");
        ASSERT_EQ(mOpened.size(), 1u);
        EXPECT_EQ(mOpened[0], 0x1a66eu);
        EXPECT_EQ(text("owner"), "0001a66e");
        mOpenWorks = false;
        mService->open("gift", 0x1a66e, kInnNpc, "Hulda");
        EXPECT_EQ(text("state"), "refused");
        EXPECT_EQ(text("owner"), "");
    }

    TEST_F(ServicesFixture, theRevisionMovesWhenSomethingChanges)
    {
        const std::int32_t r0 = integer("revision");
        mService->open("training", 0x1a6d9, kTrainerNpc, "Njada");
        const std::int32_t r1 = integer("revision");
        EXPECT_NE(r0, r1);
        ASSERT_TRUE(mService->train());
        EXPECT_NE(integer("revision"), r1);
    }

    TEST_F(ServicesFixture, closeEndsTheSessionButKeepsTheLedgerAndTheRentals)
    {
        mService->open("rent", 0x1a66e, kInnNpc, "Hulda");
        ASSERT_TRUE(mService->rent());
        mService->open("training", 0x1a6d9, kTrainerNpc, "Njada");
        ASSERT_TRUE(mService->train());
        mService->close();
        EXPECT_EQ(text("state"), "none");
        EXPECT_TRUE(mService->isRented(kBed));
        EXPECT_EQ(mService->ledger().countAt(3), 1);
    }

    // ---- the dialogue tags ------------------------------------------------------------------------------------------

    TEST(SkyrimServicesTags, theThreeServicesGetAnOpenTagAndBarterDoesNot)
    {
        EXPECT_EQ(openedServiceTag("training"), "open:training");
        EXPECT_EQ(openedServiceTag("rent a room"), "open:rent");
        EXPECT_EQ(openedServiceTag("gift"), "open:gift");
        EXPECT_EQ(openedServiceTag("barter"), "");
        EXPECT_EQ(openedServiceTag(""), "");
        EXPECT_EQ(openedServiceTag("open:training"), "");
        EXPECT_EQ(openedServiceName("training"), "training");
        EXPECT_EQ(openedServiceName("rent a room"), "rent");
        EXPECT_EQ(openedServiceName("gift"), "gift");
        EXPECT_EQ(openedServiceName("barter"), "");
    }

    TEST(SkyrimServicesTags, aFollowersInventoryLineIsTheCarryService)
    {
        // Actor.OpenInventory is what the follower's "I need to trade some things with you" lines call
        EXPECT_EQ(classifyCarry({ "actor", "openinventory", "getplayer" }), "carry");
        EXPECT_EQ(classifyCarry({ "OpenInventory" }), ""); // names are lower case, as the pex reader gives them
        EXPECT_EQ(classifyCarry({ "actor", "getplayer" }), "");
        EXPECT_EQ(classifyCarry({}), "");
        EXPECT_EQ(openedServiceTag("carry"), "open:carry");
        EXPECT_EQ(openedServiceName("carry"), "carry");
    }
}

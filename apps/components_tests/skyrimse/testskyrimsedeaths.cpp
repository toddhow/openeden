// Task 0254: dead stays dead across a load, the pack side. Every number is derived by hand:
//  - the respawn rule: a unique or essential actor never respawns; the others do once iHoursToRespawnCell (240) game
//    hours have gone by, so 239.9 hours is not enough and 240.0 is;
//  - Brenuin (placement 20, base 200) is ordinary, Cid (placement 30, base 201) is unique;
//  - the chunk is u32 count, then per entry u32 placement, u64 hours, u8 flags (1 position, 2 raised) and, with the
//    position flag, 3 x u64 and a u32: an entry without a position is 4 + 8 + 1 = 13 bytes, with one 13 + 28 = 41.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/deaths.hpp>

#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kBrenuin = 20, kCid = 30, kBrenuinBase = 200, kCidBase = 201;

    class DeathData final : public CombatData
    {
    public:
        DeathData()
        {
            FighterProfile p;
            p.mHealthMax = 50.f;
            p.mStaminaMax = 100.f;
            p.mMagickaMax = 0.f;
            p.mLevel = 5;
            p.mSkills.fill(15);
            mProfiles[kBrenuinBase] = p;
            p.mUnique = true;
            mProfiles[kCidBase] = p;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mLevel = 1;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        bool combatReady() override { return mReady; }
        bool mReady = true;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class DeathCombatWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject.actor.reference, request.slot });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool setClipSpeed(CombatSubject subject, float multiplier) override
        {
            mSpeeds.push_back({ subject.actor.reference, multiplier });
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport&) override { return true; }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor.reference);
            return true;
        }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
                return true;
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::vector<std::pair<WorldId, ClipSlot>> mClips;
        std::vector<std::pair<WorldId, float>> mSpeeds;
        std::vector<WorldId> mDeaths;
        std::map<WorldId, SubjectState> mStates;
    };

    struct PlacedCorpse
    {
        WorldId mReference;
        double mX, mY, mZ;
        float mHeading;
    };

    class DeathActorWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool placeCorpse(ActorHandle handle, double x, double y, double z, float heading) override
        {
            mPlaced.push_back({ handle.reference, x, y, z, heading });
            return true;
        }
        std::vector<PlacedCorpse> mPlaced;
    };

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
        explicit MemoryReader(std::vector<unsigned char> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mAt, size);
            mAt += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mAt; }

    private:
        std::vector<unsigned char> mBytes;
        std::size_t mAt = 0;
    };

    ActorInfo actorInfo(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    DeathEntry entryAt(double hours, bool position = true)
    {
        DeathEntry e;
        e.mHours = hours;
        if (position)
        {
            e.mX = 100.0;
            e.mY = 200.0;
            e.mZ = 30.0;
            e.mHeading = 1.5f;
            e.mHasPosition = true;
        }
        return e;
    }

    std::vector<unsigned char> chunkBytes(const std::map<FormId, DeathEntry>& data)
    {
        DeathsChunk chunk([&data] { return data; }, {});
        MemoryWriter writer;
        chunk.write(writer);
        return writer.mBytes;
    }

    bool readChunk(
        const std::vector<unsigned char>& bytes, std::map<FormId, DeathEntry>& out, std::uint32_t version = 1)
    {
        DeathsChunk chunk({}, [&out](const std::map<FormId, DeathEntry>& data) {
            out = data;
            return true;
        });
        MemoryReader reader(bytes);
        return chunk.read(reader, version);
    }

    struct DeathsKeeperTest : ::testing::Test
    {
        DeathData mData;
        SkyrimCombat mCombat;
        DeathCombatWriter mWriter;
        DeathActorWriter mActorWriter;
        DeathLedger mLedger;
        std::vector<std::pair<WorldId, bool>> mHolds;
        std::vector<WorldId> mEnabled;
        std::vector<WorldId> mPresent;
        double mRespawnHours = 240.0;
        std::unique_ptr<DeathKeeper> mKeeper;

        void SetUp() override
        {
            DeathHooks hooks;
            hooks.handleOf = [this](WorldId reference) {
                for (WorldId present : mPresent)
                    if (present == reference)
                        return ActorHandle{ reference, 1 };
                return ActorHandle{};
            };
            hooks.hold = [this](WorldId reference, bool on) { mHolds.push_back({ reference, on }); };
            hooks.enable = [this](WorldId reference) { mEnabled.push_back(reference); };
            hooks.respawnHours = [this] { return mRespawnHours; };
            mKeeper = std::make_unique<DeathKeeper>(mLedger, std::move(hooks));
            mCombat.setData(&mData);
            mKeeper->setClock(1000.0, true);
        }
        void arrive(WorldId reference, WorldId base, std::string_view name)
        {
            mPresent.push_back(reference);
            mCombat.onActorAdded(actorInfo(reference, base, name));
        }
        void update() { mCombat.combatUpdate(0.25f, mWriter); }
        void kill(WorldId reference)
        {
            ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of({ reference, 1 }), 0.f));
        }
    };

    // ---- the rules and the ledger -------------------------------------------------------------------------------

    TEST(DeathsRules, anOrdinaryActorRespawnsAfterTheCellResetTime)
    {
        EXPECT_FALSE(deathRespawns(1239.9, 1000.0, 240.0, false, false));
        EXPECT_TRUE(deathRespawns(1240.0, 1000.0, 240.0, false, false));
        EXPECT_TRUE(deathRespawns(5000.0, 1000.0, 240.0, false, false));
    }

    TEST(DeathsRules, aUniqueOrEssentialActorNeverRespawns)
    {
        EXPECT_FALSE(deathRespawns(100000.0, 1000.0, 240.0, true, false));
        EXPECT_FALSE(deathRespawns(100000.0, 1000.0, 240.0, false, true));
    }

    TEST(DeathsRules, aClockThatWentBackOrANumberThatIsNotFiniteNeverRespawns)
    {
        EXPECT_FALSE(deathRespawns(900.0, 1000.0, 240.0, false, false));
        EXPECT_FALSE(deathRespawns(std::nan(""), 1000.0, 240.0, false, false));
        EXPECT_FALSE(deathRespawns(2000.0, std::numeric_limits<double>::infinity(), 240.0, false, false));
        EXPECT_FALSE(deathRespawns(2000.0, 1000.0, std::nan(""), false, false));
    }

    TEST(DeathsLedger, anEntryIsAddedOnceAndFound)
    {
        DeathLedger ledger;
        EXPECT_TRUE(ledger.add(20, 5.0));
        EXPECT_FALSE(ledger.add(20, 6.0));
        ASSERT_NE(ledger.find(20), nullptr);
        EXPECT_EQ(ledger.find(20)->mHours, 5.0);
        EXPECT_EQ(ledger.find(21), nullptr);
        EXPECT_EQ(ledger.size(), 1u);
    }

    TEST(DeathsLedger, aNullIdACreatedActorOrAHourThatIsNotFiniteIsRefused)
    {
        DeathLedger ledger;
        EXPECT_FALSE(ledger.add(0, 5.0));
        EXPECT_FALSE(ledger.add(static_cast<FormId>(kCreatedObjectBase) + 1u, 5.0));
        EXPECT_FALSE(ledger.add(20, std::nan("")));
        EXPECT_EQ(ledger.size(), 0u);
    }

    TEST(DeathsLedger, theLedgerHoldsNoMoreThanItsCap)
    {
        DeathLedger ledger;
        for (FormId id = 1; id <= kMaxDeathEntries; ++id)
            ASSERT_TRUE(ledger.add(id, 1.0));
        EXPECT_FALSE(ledger.add(kMaxDeathEntries + 1u, 1.0));
        EXPECT_EQ(ledger.size(), kMaxDeathEntries);
    }

    TEST(DeathsLedger, aPositionNeedsAnEntryAndFiniteNumbers)
    {
        DeathLedger ledger;
        EXPECT_FALSE(ledger.setPosition(20, 1.0, 2.0, 3.0, 0.5f));
        ASSERT_TRUE(ledger.add(20, 5.0));
        EXPECT_FALSE(ledger.setPosition(20, std::nan(""), 2.0, 3.0, 0.5f));
        EXPECT_FALSE(ledger.find(20)->mHasPosition);
        EXPECT_TRUE(ledger.setPosition(20, 1.0, 2.0, 3.0, 0.5f));
        EXPECT_TRUE(ledger.find(20)->mHasPosition);
        EXPECT_EQ(ledger.find(20)->mZ, 3.0);
    }

    TEST(DeathsLedger, theSavedDataLeavesOutWhatOnlyThisSessionKnows)
    {
        DeathLedger ledger;
        ASSERT_TRUE(ledger.add(20, 5.0));
        ledger.find(20)->mPlaced = true;
        ledger.find(20)->mRestored = true;
        ledger.find(20)->mRefreshLeft = 9.f;
        const std::map<FormId, DeathEntry> data = ledger.data();
        ASSERT_EQ(data.size(), 1u);
        EXPECT_FALSE(data.at(20).mPlaced);
        EXPECT_FALSE(data.at(20).mRestored);
        EXPECT_EQ(data.at(20).mRefreshLeft, 0.f);
        EXPECT_EQ(data.at(20).mHours, 5.0);
    }

    TEST(DeathsLedger, aRestoreWithABadEntryChangesNothing)
    {
        DeathLedger ledger;
        ASSERT_TRUE(ledger.add(20, 5.0));
        std::map<FormId, DeathEntry> bad;
        bad[0] = entryAt(1.0);
        EXPECT_FALSE(ledger.restore(bad));
        bad.clear();
        bad[static_cast<FormId>(kCreatedObjectBase) + 2u] = entryAt(1.0);
        EXPECT_FALSE(ledger.restore(bad));
        bad.clear();
        bad[7] = entryAt(std::nan(""));
        EXPECT_FALSE(ledger.restore(bad));
        EXPECT_EQ(ledger.size(), 1u);
        EXPECT_NE(ledger.find(20), nullptr);
    }

    TEST(DeathsLedger, aRestoredEntryWaitsToBePutBack)
    {
        DeathLedger ledger;
        std::map<FormId, DeathEntry> saved;
        saved[20] = entryAt(5.0);
        saved[20].mPlaced = true; // a stale flag in the argument is ignored
        ASSERT_TRUE(ledger.restore(saved));
        EXPECT_FALSE(ledger.find(20)->mPlaced);
        EXPECT_FALSE(ledger.find(20)->mRestored);
        ledger.find(20)->mPlaced = true;
        ledger.find(20)->mRestored = true;
        ledger.actorGone(20);
        EXPECT_FALSE(ledger.find(20)->mPlaced);
        EXPECT_FALSE(ledger.find(20)->mRestored);
    }

    // ---- the chunk ----------------------------------------------------------------------------------------------

    TEST(DeathsChunk, anEntryTakesThirteenBytesAndFortyOneWithAPosition)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0, false);
        EXPECT_EQ(chunkBytes(data).size(), 4u + 13u);
        data[30] = entryAt(6.0, true);
        EXPECT_EQ(chunkBytes(data).size(), 4u + 13u + 41u);
    }

    TEST(DeathsChunk, whatIsWrittenIsReadBackBitForBit)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(1234.5, true);
        data[30] = entryAt(99.25, false);
        data[30].mRaised = true;
        std::map<FormId, DeathEntry> back;
        ASSERT_TRUE(readChunk(chunkBytes(data), back));
        ASSERT_EQ(back.size(), 2u);
        EXPECT_EQ(back.at(20).mHours, 1234.5);
        EXPECT_EQ(back.at(20).mX, 100.0);
        EXPECT_EQ(back.at(20).mZ, 30.0);
        EXPECT_EQ(back.at(20).mHeading, 1.5f);
        EXPECT_TRUE(back.at(20).mHasPosition);
        EXPECT_FALSE(back.at(20).mRaised);
        EXPECT_FALSE(back.at(30).mHasPosition);
        EXPECT_TRUE(back.at(30).mRaised);
    }

    TEST(DeathsChunk, anotherVersionIsRefused)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0);
        std::map<FormId, DeathEntry> back;
        EXPECT_FALSE(readChunk(chunkBytes(data), back, 2));
        EXPECT_TRUE(back.empty());
    }

    TEST(DeathsChunk, everyTruncationIsRefused)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0);
        data[30] = entryAt(6.0, false);
        const std::vector<unsigned char> bytes = chunkBytes(data);
        for (std::size_t length = 0; length < bytes.size(); ++length)
        {
            std::map<FormId, DeathEntry> back;
            EXPECT_FALSE(readChunk(std::vector<unsigned char>(bytes.begin(), bytes.begin() + length), back)) << length;
        }
    }

    TEST(DeathsChunk, trailingBytesAreRefused)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0);
        std::vector<unsigned char> bytes = chunkBytes(data);
        bytes.push_back(0);
        std::map<FormId, DeathEntry> back;
        EXPECT_FALSE(readChunk(bytes, back));
    }

    TEST(DeathsChunk, placementsMustAscendAndStayBelowTheCreatedRange)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0, false);
        data[30] = entryAt(6.0, false);
        std::vector<unsigned char> bytes = chunkBytes(data);
        // swap the two placements (the first entry's id is at offset 4, the second's at 4 + 13)
        std::swap(bytes[4], bytes[17]);
        std::map<FormId, DeathEntry> back;
        EXPECT_FALSE(readChunk(bytes, back));
        std::map<FormId, DeathEntry> created;
        created[static_cast<FormId>(kCreatedObjectBase) + 1u] = entryAt(5.0, false);
        EXPECT_FALSE(readChunk(chunkBytes(created), back));
    }

    TEST(DeathsChunk, anUnknownFlagOrAHourThatIsNotFiniteIsRefused)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0, false);
        std::vector<unsigned char> bytes = chunkBytes(data);
        std::vector<unsigned char> flagged = bytes;
        flagged[4 + 4 + 8] = 4; // the flags byte
        std::map<FormId, DeathEntry> back;
        EXPECT_FALSE(readChunk(flagged, back));
        data[20].mHours = std::nan("");
        EXPECT_FALSE(readChunk(chunkBytes(data), back));
    }

    TEST(DeathsChunk, aCountBeyondTheBytesLeftIsRefusedBeforeAnythingIsReserved)
    {
        std::vector<unsigned char> bytes(4, 0xFF); // a count of 4 billion and nothing else
        std::map<FormId, DeathEntry> back;
        EXPECT_FALSE(readChunk(bytes, back));
    }

    TEST(DeathsChunk, aRefusingSetterMakesTheReadFail)
    {
        std::map<FormId, DeathEntry> data;
        data[20] = entryAt(5.0);
        DeathsChunk chunk({}, [](const std::map<FormId, DeathEntry>&) { return false; });
        MemoryReader reader(chunkBytes(data));
        EXPECT_FALSE(chunk.read(reader, 1));
        EXPECT_FALSE(chunk.refusal().empty());
    }

    // ---- noting a death -----------------------------------------------------------------------------------------

    TEST_F(DeathsKeeperTest, aFighterThatDiesGetsAnEntryWithTheHourAndThePlaceItLay)
    {
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        SubjectState s;
        s.x = 10.0;
        s.y = 20.0;
        s.z = 30.0;
        s.heading = 2.f;
        mWriter.mStates[kBrenuin] = s;
        update();
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        EXPECT_EQ(mLedger.size(), 0u); // alive: nothing noted
        kill(kBrenuin);
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        ASSERT_NE(mLedger.find(kBrenuin), nullptr);
        const DeathEntry& e = *mLedger.find(kBrenuin);
        EXPECT_EQ(e.mHours, 1000.0);
        EXPECT_TRUE(e.mHasPosition);
        EXPECT_EQ(e.mX, 10.0);
        EXPECT_EQ(e.mY, 20.0);
        EXPECT_EQ(e.mZ, 30.0);
        EXPECT_EQ(e.mHeading, 2.f);
        EXPECT_TRUE(e.mPlaced);
        EXPECT_TRUE(e.mRestored);
    }

    TEST_F(DeathsKeeperTest, aBodyThatStillMovesIsReadAgainEveryHalfSecondForTwentySecondsThenEveryFive)
    {
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        kill(kBrenuin);
        SubjectState s;
        s.x = 1.0;
        mWriter.mStates[kBrenuin] = s;
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        EXPECT_EQ(mLedger.find(kBrenuin)->mX, 1.0);
        s.x = 2.0;
        mWriter.mStates[kBrenuin] = s;
        mKeeper->noteDeaths(0.25f, mWriter, mCombat); // 0.25 s since the read: not yet
        EXPECT_EQ(mLedger.find(kBrenuin)->mX, 1.0);
        mKeeper->noteDeaths(0.25f, mWriter, mCombat); // 0.5 s
        EXPECT_EQ(mLedger.find(kBrenuin)->mX, 2.0);
        for (int i = 0; i < 100; ++i) // past the 20 seconds
            mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        s.x = 3.0;
        mWriter.mStates[kBrenuin] = s;
        for (int i = 0; i < 20; ++i) // five seconds: the late rate reads it again at least once
            mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        EXPECT_EQ(mLedger.find(kBrenuin)->mX, 3.0);
    }

    TEST_F(DeathsKeeperTest, aFighterThatDiesIsHeldStillSoThatItsPackageDoesNotWalkTheCorpse)
    {
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        kill(kBrenuin);
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        ASSERT_EQ(mHolds.size(), 1u);
        EXPECT_EQ(mHolds[0].first, kBrenuin);
        EXPECT_TRUE(mHolds[0].second);
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        EXPECT_EQ(mHolds.size(), 1u); // once
    }

    TEST_F(DeathsKeeperTest, aCreatedActorThatDiesIsNotNoted)
    {
        const WorldId created = static_cast<WorldId>(kCreatedObjectBase) + 5u;
        arrive(created, kBrenuinBase, "Summon");
        update();
        kill(created);
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        EXPECT_EQ(mLedger.size(), 0u);
    }

    // ---- putting the dead back ----------------------------------------------------------------------------------

    TEST_F(DeathsKeeperTest, aSavedDeadActorIsPutWhereItLayHeldStillAndMarkedDead)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(900.0);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        mData.mReady = false; // the records are still being read
        mKeeper->placeCorpses(mActorWriter, mCombat);
        EXPECT_TRUE(mActorWriter.mPlaced.empty());
        mData.mReady = true;
        mKeeper->placeCorpses(mActorWriter, mCombat); // an idle fighter is read on demand: no update needed
        ASSERT_EQ(mActorWriter.mPlaced.size(), 1u);
        EXPECT_EQ(mActorWriter.mPlaced[0].mReference, kBrenuin);
        EXPECT_EQ(mActorWriter.mPlaced[0].mX, 100.0);
        EXPECT_EQ(mActorWriter.mPlaced[0].mY, 200.0);
        EXPECT_EQ(mActorWriter.mPlaced[0].mZ, 30.0);
        EXPECT_EQ(mActorWriter.mPlaced[0].mHeading, 1.5f);
        ASSERT_EQ(mHolds.size(), 1u);
        EXPECT_EQ(mHolds[0].first, kBrenuin);
        EXPECT_TRUE(mHolds[0].second);
        EXPECT_TRUE(mWriter.mDeaths.empty()); // placing is not the death report
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_EQ(mWriter.mDeaths, std::vector<WorldId>{ kBrenuin });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].second, ClipSlot::Death);
        ASSERT_EQ(mWriter.mSpeeds.size(), 1u);
        EXPECT_EQ(mWriter.mSpeeds[0].second, 10.f);
    }

    TEST_F(DeathsKeeperTest, theRestoredActorIsDeadForTheCombatAndTheReportComesOnce)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(900.0);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of({ kBrenuin, 1 }), view));
        EXPECT_TRUE(view.mDead);
        EXPECT_EQ(view.mHealth, 0.f);
        EXPECT_EQ(mKeeper->restored(), 1u);
    }

    TEST_F(DeathsKeeperTest, aSavedDeathWithNoPositionStillMarksTheActorDead)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(900.0, false);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        EXPECT_TRUE(mActorWriter.mPlaced.empty());
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
    }

    TEST_F(DeathsKeeperTest, nothingHappensWhileTheActorsCellIsNotLoaded)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(900.0);
        ASSERT_TRUE(mLedger.restore(saved));
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_TRUE(mActorWriter.mPlaced.empty());
        EXPECT_TRUE(mWriter.mDeaths.empty());
    }

    TEST_F(DeathsKeeperTest, aCellThatLoadsAgainPutsTheCorpseBackAgain)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(900.0);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        mCombat.onActorRemoved({ kBrenuin, 1 });
        mKeeper->actorGone(kBrenuin);
        mCombat.onActorAdded(actorInfo(kBrenuin, kBrenuinBase, "Brenuin"));
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_EQ(mActorWriter.mPlaced.size(), 2u);
        EXPECT_EQ(mWriter.mDeaths.size(), 2u);
    }

    // ---- respawn ------------------------------------------------------------------------------------------------

    TEST_F(DeathsKeeperTest, anOrdinaryActorThatDiedTwoHundredFortyHoursAgoStandsAgain)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(760.0); // now is 1000: exactly 240 hours
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_EQ(mLedger.find(kBrenuin), nullptr);
        EXPECT_EQ(mKeeper->respawns(), 1u);
        EXPECT_TRUE(mActorWriter.mPlaced.empty());
        EXPECT_TRUE(mWriter.mDeaths.empty());
        EXPECT_TRUE(mHolds.empty());
    }

    TEST_F(DeathsKeeperTest, anActorThatDiedJustUnderTheTimeStaysDead)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(760.5);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        EXPECT_NE(mLedger.find(kBrenuin), nullptr);
        EXPECT_EQ(mActorWriter.mPlaced.size(), 1u);
    }

    TEST_F(DeathsKeeperTest, aUniqueActorStaysDeadForGood)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kCid] = entryAt(0.0);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kCid, kCidBase, "Cid");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        mKeeper->restoreDeaths(mWriter, mCombat);
        EXPECT_NE(mLedger.find(kCid), nullptr);
        EXPECT_EQ(mWriter.mDeaths, std::vector<WorldId>{ kCid });
        EXPECT_EQ(mKeeper->respawns(), 0u);
    }

    TEST_F(DeathsKeeperTest, theRespawnTimeIsTheDatasNotAConstant)
    {
        mRespawnHours = 10.0;
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(985.0);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        EXPECT_EQ(mLedger.find(kBrenuin), nullptr);
    }

    TEST_F(DeathsKeeperTest, noClockMeansNoRespawn)
    {
        mKeeper->setClock(0.0, false);
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(-100000.0);
        ASSERT_TRUE(mLedger.restore(saved));
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        mKeeper->placeCorpses(mActorWriter, mCombat);
        EXPECT_NE(mLedger.find(kBrenuin), nullptr);
    }

    // ---- the raised corpse --------------------------------------------------------------------------------------

    TEST_F(DeathsKeeperTest, aCorpseAReanimateSpellHidIsEnabledAgainWhenTheLedgerIsRestored)
    {
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        kill(kBrenuin);
        mKeeper->noteDeaths(0.25f, mWriter, mCombat);
        mKeeper->corpseRaised(kBrenuin);
        ASSERT_TRUE(mLedger.find(kBrenuin)->mRaised);
        const std::map<FormId, DeathEntry> saved = mLedger.data();
        EXPECT_TRUE(saved.at(kBrenuin).mRaised);
        ASSERT_TRUE(mLedger.restore(saved));
        mKeeper->ledgerRestored();
        EXPECT_EQ(mEnabled, std::vector<WorldId>{ kBrenuin });
        EXPECT_FALSE(mLedger.find(kBrenuin)->mRaised);
        mKeeper->ledgerRestored();
        EXPECT_EQ(mEnabled.size(), 1u); // once
    }

    TEST_F(DeathsKeeperTest, aCorpseThatWasNeverRaisedIsNotEnabled)
    {
        std::map<FormId, DeathEntry> saved;
        saved[kBrenuin] = entryAt(900.0);
        ASSERT_TRUE(mLedger.restore(saved));
        mKeeper->ledgerRestored();
        EXPECT_TRUE(mEnabled.empty());
    }

    TEST_F(DeathsKeeperTest, theCombatToldWhenACorpseWasRaisedMarksItsEntry)
    {
        std::vector<WorldId> told;
        mCombat.setCorpseRaisedSink([&told](WorldId corpse) { told.push_back(corpse); });
        // the sink is the hook of combat_summons.cpp; without summons nothing calls it
        EXPECT_TRUE(told.empty());
    }

    // ---- restoreDead itself -------------------------------------------------------------------------------------

    TEST_F(DeathsKeeperTest, restoreDeadRefusesAnUnknownActorAndThePlayer)
    {
        update();
        EXPECT_FALSE(mCombat.restoreDead(mWriter, { 999, 1 }));
        EXPECT_FALSE(mCombat.restoreDead(mWriter, ActorHandle{}));
        EXPECT_TRUE(mWriter.mDeaths.empty());
    }

    TEST_F(DeathsKeeperTest, restoreDeadWaitsForTheNumbersOfTheActor)
    {
        mData.mProfiles.erase(kBrenuinBase); // the data knows no such base
        arrive(kBrenuin, kBrenuinBase, "Brenuin");
        update();
        EXPECT_FALSE(mCombat.restoreDead(mWriter, { kBrenuin, 1 }));
        EXPECT_TRUE(mWriter.mDeaths.empty());
    }
}

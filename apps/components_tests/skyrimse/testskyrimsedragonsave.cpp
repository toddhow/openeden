// Task 0280 (dragons, second slice): the save of a dragon in flight (the chunk `dragons`, `Flight::resume`,
// `SkyrimCombat::savedDragons` / `restoreDragons`) and several dragons at once. The fixture is the one of
// testskyrimsecombatdragon.cpp in a shorter form (both live in anonymous namespaces).
// Numbers by hand: the chunk is u32 count then 4 + 3 * 8 + 4 = 32 bytes an entry; a dragon saved in the air comes back
// in the cruise at the saved place; not engaged it goes into the approach at once (the cruise ends when the fight
// does) and lands kLandDistance (900) from the player at the player's height.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/dragons.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<std::uint8_t> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(std::vector<std::uint8_t> bytes)
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
        std::vector<std::uint8_t> mBytes;
        std::size_t mAt = 0;
    };

    dragons::DragonSave saveOf(std::uint32_t placement, double x = 1.0, double y = 2.0, double z = 3.0, float h = 0.5f)
    {
        dragons::DragonSave s;
        s.mPlacement = placement;
        s.mX = x;
        s.mY = y;
        s.mZ = z;
        s.mHeading = h;
        return s;
    }

    // ---- the codec --------------------------------------------------------------------------------------------------

    TEST(DragonSaveCodec, anEmptyListIsJustTheCount)
    {
        const std::vector<std::uint8_t> bytes = dragons::encodeDragonSaves({});
        ASSERT_EQ(bytes.size(), 4u);
        EXPECT_EQ(bytes[0], 0);
        std::vector<dragons::DragonSave> out{ saveOf(9) };
        ASSERT_TRUE(dragons::decodeDragonSaves(bytes, out));
        EXPECT_TRUE(out.empty());
    }

    TEST(DragonSaveCodec, oneEntryIsThirtyTwoBytesWithTheCountFirstAndThePlacementNext)
    {
        const std::vector<std::uint8_t> bytes = dragons::encodeDragonSaves({ saveOf(0x01020304) });
        ASSERT_EQ(bytes.size(), 4u + dragons::kDragonSaveBytes);
        EXPECT_EQ(dragons::kDragonSaveBytes, 32u);
        std::uint32_t count = 0, placement = 0;
        std::memcpy(&count, bytes.data(), 4);
        std::memcpy(&placement, bytes.data() + 4, 4);
        EXPECT_EQ(count, 1u);
        EXPECT_EQ(placement, 0x01020304u);
        double x = 0;
        std::memcpy(&x, bytes.data() + 8, 8);
        EXPECT_EQ(x, 1.0);
        float heading = 0.f;
        std::memcpy(&heading, bytes.data() + 4 + 28, 4);
        EXPECT_EQ(heading, 0.5f);
    }

    TEST(DragonSaveCodec, entriesAreWrittenInAscendingPlacementOrderWhateverTheInputOrder)
    {
        const std::vector<std::uint8_t> a = dragons::encodeDragonSaves({ saveOf(30), saveOf(10), saveOf(20) });
        const std::vector<std::uint8_t> b = dragons::encodeDragonSaves({ saveOf(10), saveOf(20), saveOf(30) });
        EXPECT_EQ(a, b);
        std::vector<dragons::DragonSave> out;
        ASSERT_TRUE(dragons::decodeDragonSaves(a, out));
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0].mPlacement, 10u);
        EXPECT_EQ(out[2].mPlacement, 30u);
    }

    TEST(DragonSaveCodec, nullPlacementsDuplicatesAndNonFiniteEntriesAreNotWritten)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const std::vector<std::uint8_t> bytes
            = dragons::encodeDragonSaves({ saveOf(0), saveOf(5), saveOf(5, 9.0), saveOf(6, nan), saveOf(7) });
        std::vector<dragons::DragonSave> out;
        ASSERT_TRUE(dragons::decodeDragonSaves(bytes, out));
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].mPlacement, 5u);
        EXPECT_EQ(out[0].mX, 1.0) << "the first of two equal placements wins";
        EXPECT_EQ(out[1].mPlacement, 7u);
    }

    TEST(DragonSaveCodec, aListOverTheCapIsCutAtTheCap)
    {
        std::vector<dragons::DragonSave> many;
        for (std::uint32_t i = 1; i <= dragons::kMaxDragonSaves + 10; ++i)
            many.push_back(saveOf(i));
        std::vector<dragons::DragonSave> out;
        ASSERT_TRUE(dragons::decodeDragonSaves(dragons::encodeDragonSaves(many), out));
        EXPECT_EQ(out.size(), dragons::kMaxDragonSaves);
    }

    TEST(DragonSaveCodec, roundTripIsExactAndCanonical)
    {
        const std::vector<dragons::DragonSave> in
            = { saveOf(3, -12345.678901234, 99999.5, 4096.25, -1.25f), saveOf(8, 0.0, 0.0, 0.0, 0.f) };
        const std::vector<std::uint8_t> bytes = dragons::encodeDragonSaves(in);
        std::vector<dragons::DragonSave> out;
        ASSERT_TRUE(dragons::decodeDragonSaves(bytes, out));
        EXPECT_EQ(out, in);
        EXPECT_EQ(dragons::encodeDragonSaves(out), bytes);
    }

    TEST(DragonSaveCodec, aStrictDecoderRefusesEverythingElseAndLeavesTheOutputAlone)
    {
        const std::vector<std::uint8_t> good = dragons::encodeDragonSaves({ saveOf(1), saveOf(2) });
        const std::vector<dragons::DragonSave> marker{ saveOf(77) };
        const auto refuses = [&](std::vector<std::uint8_t> bytes, const char* why) {
            std::vector<dragons::DragonSave> out = marker;
            EXPECT_FALSE(dragons::decodeDragonSaves(bytes, out)) << why;
            EXPECT_EQ(out, marker) << why;
        };
        refuses({}, "no count");
        refuses({ 0, 0, 0 }, "a short count");
        std::vector<std::uint8_t> shorter = good;
        shorter.pop_back();
        refuses(shorter, "a truncated entry");
        std::vector<std::uint8_t> longer = good;
        longer.push_back(0);
        refuses(longer, "a trailing byte");
        std::vector<std::uint8_t> bigCount = good;
        const std::uint32_t over = static_cast<std::uint32_t>(dragons::kMaxDragonSaves) + 1;
        std::memcpy(bigCount.data(), &over, 4);
        refuses(bigCount, "a count over the cap");
        std::vector<std::uint8_t> descending = dragons::encodeDragonSaves({ saveOf(1), saveOf(2) });
        const std::uint32_t two = 2, one = 1;
        std::memcpy(descending.data() + 4, &two, 4);
        std::memcpy(descending.data() + 4 + dragons::kDragonSaveBytes, &one, 4);
        refuses(descending, "descending placements");
        std::vector<std::uint8_t> twins = good;
        std::memcpy(twins.data() + 4 + dragons::kDragonSaveBytes, &one, 4);
        refuses(twins, "equal placements");
        std::vector<std::uint8_t> zero = good;
        const std::uint32_t none = 0;
        std::memcpy(zero.data() + 4, &none, 4);
        refuses(zero, "a null placement");
        std::vector<std::uint8_t> nan = good;
        const double notANumber = std::numeric_limits<double>::quiet_NaN();
        std::memcpy(nan.data() + 8, &notANumber, 8);
        refuses(nan, "a NaN position");
    }

    TEST(DragonsChunkTest, writesTheListAndReadsItBackThroughTheSetter)
    {
        std::vector<dragons::DragonSave> got;
        dragons::DragonsChunk chunk([] { return std::vector<dragons::DragonSave>{ saveOf(4, 5.0) }; },
            [&](std::vector<dragons::DragonSave> saves) { got = std::move(saves); });
        EXPECT_EQ(chunk.chunkName(), "dragons");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        MemoryWriter writer;
        chunk.write(writer);
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(chunk.read(reader, 1));
        ASSERT_EQ(got.size(), 1u);
        EXPECT_EQ(got[0].mPlacement, 4u);
        EXPECT_EQ(got[0].mX, 5.0);
    }

    TEST(DragonsChunkTest, refusesAnotherVersionAndBadBytesAndSetsNothing)
    {
        bool set = false;
        dragons::DragonsChunk chunk(
            [] { return std::vector<dragons::DragonSave>(); }, [&](std::vector<dragons::DragonSave>) { set = true; });
        MemoryWriter writer;
        chunk.write(writer);
        MemoryReader other(writer.mBytes);
        EXPECT_FALSE(chunk.read(other, 2));
        MemoryReader bad({ 1, 0, 0, 0 });
        EXPECT_FALSE(chunk.read(bad, 1));
        EXPECT_FALSE(set);
    }

    // ---- Flight::resume ---------------------------------------------------------------------------------------------

    dragons::FlightInput inputAt(double x, double y, double z, bool engaged)
    {
        dragons::FlightInput in;
        in.mDt = 0.1f;
        in.mX = x;
        in.mY = y;
        in.mZ = z;
        in.mEngaged = engaged;
        return in;
    }

    TEST(FlightResume, aResumedDragonIsInTheCruiseAndTheFirstStepPlacesItWhereItWasWithTheCruiseClip)
    {
        dragons::Flight flight;
        flight.resume(1200.0, 0.0, 900.0, 1.5f, 0.0, 0.0);
        EXPECT_EQ(flight.phase(), dragons::Phase::Cruise);
        EXPECT_TRUE(flight.airborne());
        const dragons::FlightOutput out = flight.step(inputAt(1200.0, 0.0, 900.0, true));
        EXPECT_TRUE(out.mPlace);
        EXPECT_TRUE(out.mStartPose) << "the core has no pose for it yet: a new loop of the clip";
        EXPECT_EQ(out.mClip, dragons::kClipCruise);
        EXPECT_NEAR(out.mZ, 900.0, 1e-6);
        EXPECT_NEAR(std::hypot(out.mX, out.mY), 1200.0, 60.0) << "on its own bearing and distance at first";
        const dragons::FlightOutput next = flight.step(inputAt(out.mX, out.mY, out.mZ, true));
        EXPECT_FALSE(next.mStartPose) << "only the first step starts the pose";
        EXPECT_TRUE(next.mPlace);
    }

    TEST(FlightResume, anEngagedResumedDragonCirclesForTheWholeAirTimeThenApproaches)
    {
        dragons::FlightSettings settings;
        settings.mAirSeconds = 5.f;
        dragons::Flight flight(settings);
        flight.resume(1500.0, 0.0, 900.0, 0.f, 0.0, 0.0);
        dragons::FlightOutput out;
        double x = 1500.0, y = 0.0;
        for (int i = 0; i < 45; ++i)
        {
            out = flight.step(inputAt(x, y, 900.0, true));
            x = out.mX;
            y = out.mY;
        }
        EXPECT_EQ(flight.phase(), dragons::Phase::Cruise) << "4.5 s of 5";
        for (int i = 0; i < 10; ++i)
            out = flight.step(inputAt(x, y, 900.0, true));
        EXPECT_EQ(flight.phase(), dragons::Phase::Approach);
    }

    TEST(FlightResume, aResumedDragonThatIsNotEngagedApproachesAtOnceAndLandsNearThePlayer)
    {
        dragons::Flight flight;
        flight.resume(1200.0, 0.0, 900.0, 0.f, 100.0, 50.0);
        double x = 1200.0, y = 0.0, z = 900.0;
        dragons::FlightOutput out;
        bool landed = false;
        for (int i = 0; i < 200 && !landed; ++i)
        {
            dragons::FlightInput in = inputAt(x, y, z, false);
            in.mPlayerX = 100.0;
            in.mPlayerY = 50.0;
            in.mPlayerZ = 20.0;
            out = flight.step(in);
            if (i == 0)
            {
                EXPECT_EQ(flight.phase(), dragons::Phase::Approach) << "not engaged: the approach begins at once";
            }
            if (out.mPlace)
            {
                x = out.mX;
                y = out.mY;
                z = out.mZ;
            }
            landed = out.mLand;
        }
        ASSERT_TRUE(landed);
        EXPECT_EQ(flight.phase(), dragons::Phase::Ground);
        EXPECT_NEAR(std::hypot(out.mX - 100.0, out.mY - 50.0), 900.0, 1.0);
        EXPECT_NEAR(out.mZ, 20.0, 1e-6);
    }

    TEST(FlightResume, numbersThatAreNotFiniteChangeNothing)
    {
        dragons::Flight flight;
        flight.resume(std::numeric_limits<double>::quiet_NaN(), 0.0, 900.0, 0.f, 0.0, 0.0);
        EXPECT_EQ(flight.phase(), dragons::Phase::Ground);
        flight.resume(0.0, 0.0, 900.0, std::numeric_limits<float>::infinity(), 0.0, 0.0);
        EXPECT_EQ(flight.phase(), dragons::Phase::Ground);
        flight.resume(0.0, 0.0, 900.0, 0.f, 0.0, std::numeric_limits<double>::infinity());
        EXPECT_EQ(flight.phase(), dragons::Phase::Ground);
    }

    TEST(FlightResume, aDragonAlreadyInTheAirIsNotResumedAgain)
    {
        dragons::Flight flight;
        flight.resume(1200.0, 0.0, 900.0, 0.f, 0.0, 0.0);
        (void)flight.step(inputAt(1200.0, 0.0, 900.0, true));
        flight.resume(-5000.0, 0.0, 100.0, 0.f, 0.0, 0.0);
        const dragons::FlightOutput out = flight.step(inputAt(1200.0, 0.0, 900.0, true));
        EXPECT_NEAR(out.mZ, 900.0, 1e-6) << "the live flight wins over a late restore";
    }

    // ---- in the combat ----------------------------------------------------------------------------------------------

    constexpr WorldId kDragonBase = 100;
    constexpr FormId kShout = 0x252C0, kSpell = 0x252C2, kEffect = 0x252C1, kProj = 0x252C6, kWord = 0x252C3;

    class DragonData final : public CombatData
    {
    public:
        DragonData()
        {
            mProj.mCommon.mId = kProj;
            mProj.mHasData = true;
            mProj.mType = 8;
            mProj.mSpeed = 1000.f;
            mProj.mRange = 2000.f;
            MagicEffectRecord e;
            e.mCommon.mId = kEffect;
            e.mHasData = true;
            e.mPrimaryValue = 24;
            e.mFlags = MagicFlag::Hostile | MagicFlag::Detrimental;
            e.mProjectile = kProj;
            mEffects[kEffect] = e;
            mSpell.mCommon.mId = kSpell;
            mSpell.mHasData = true;
            mSpell.mType = 11;
            mSpell.mCost = 5;
            mSpell.mCastType = MagicCast::Concentration;
            mSpell.mDelivery = MagicDelivery::Aimed;
            mSpell.mCastDuration = 2.4f;
            SpellEffect se;
            se.mEffect = kEffect;
            se.mMagnitude = 20.f;
            mSpell.mEffects.push_back(se);
            mShout.mCommon.mId = kShout;
            mShout.mHasData = true;
            mShout.mWordCount = 3;
            for (std::size_t i = 0; i < 3; ++i)
                mShout.mWords[i] = { kWord, kSpell, 5.f };
            dragon.mHealthMax = 1000.f;
            dragon.mStaminaMax = 100.f;
            dragon.mMagickaMax = 0.f;
            dragon.mUnarmedDamage = 30.f;
            dragon.mUnarmedReach = 130.f;
            dragon.mAi.mKnown = true;
            dragon.mAi.mConfidence = 4;
            dragon.mCreature = &dragons::dragonProfile();
            dragon.mSpells.push_back(kShout);
            mPlayer.mHealthMax = 100000.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            if (base != kDragonBase)
                return false;
            out = dragon;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellById(FormId id) override { return id == kSpell ? &mSpell : nullptr; }
        const SpellRecord* spellByEditorId(std::string_view) override { return nullptr; }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProj ? &mProj : nullptr; }
        const ShoutRecord* shoutById(FormId id) override { return id == kShout ? &mShout : nullptr; }

        SpellRecord mSpell;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        ShoutRecord mShout;
        FighterProfile dragon;
        FighterProfile mPlayer;
    };

    class DragonWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject.actor.reference, std::string(request.clip) });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.5f, 3.f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::size_t clipsOf(WorldId reference, const std::string& name) const
        {
            return static_cast<std::size_t>(std::count_if(mClips.begin(), mClips.end(),
                [&](const std::pair<WorldId, std::string>& c) { return c.first == reference && c.second == name; }));
        }
        std::vector<std::pair<WorldId, std::string>> mClips;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    using Kind = SkyrimCombat::AiMoveIntent::Kind;

    ActorInfo infoOf(WorldId reference, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = kDragonBase;
        i.editorId = editorId;
        return i;
    }

    struct CombatDragonSave : ::testing::Test
    {
        DragonData mData;
        SkyrimCombat mCombat;
        DragonWriter mWriter;
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;
        float mClock = 0.f;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setCombatAi(true);
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 0.f, 100000.f, 100.f };
        }
        void setDragons(bool on = true, float ground = 2.f, float air = 5.f)
        {
            dragons::Settings s;
            s.mOn = on;
            s.mFlight.mEnabled = true;
            s.mFlight.mGroundSeconds = ground;
            s.mFlight.mAirSeconds = air;
            mCombat.setDragons(s);
            ASSERT_TRUE(mCombat.setTestHostile("dragonx"));
        }
        void add(WorldId ref, double x, double y, double z = 0.0)
        {
            mCombat.onActorAdded(infoOf(ref, "DragonX"));
            mWriter.mStates[ref] = { x, y, z, 0.f, 0.f, 0.f };
        }
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mClock += dt;
                mCombat.combatUpdate(dt, mWriter);
                std::vector<SkyrimCombat::AiMoveIntent> fresh;
                mCombat.drainAiMoves(fresh);
                for (const auto& m : fresh)
                {
                    if (m.mKind == Kind::Fly || m.mKind == Kind::Land)
                    {
                        SubjectState& s = mWriter.mStates[m.mActor.reference];
                        s.x = m.mX;
                        s.y = m.mY;
                        s.z = m.mZ;
                        if (m.mKind == Kind::Fly)
                            s.heading = m.mHeading;
                    }
                    mMoves.push_back(m);
                }
            }
        }
        std::size_t movesOf(Kind kind, WorldId ref = 0) const
        {
            return static_cast<std::size_t>(
                std::count_if(mMoves.begin(), mMoves.end(), [&](const SkyrimCombat::AiMoveIntent& m) {
                    return m.mKind == kind && (ref == 0 || m.mActor.reference == ref);
                }));
        }
    };

    TEST_F(CombatDragonSave, aDragonOnTheGroundIsNotInTheSave)
    {
        setDragons();
        add(10, 0.0, -300.0);
        run(5);
        EXPECT_TRUE(mCombat.savedDragons().empty());
    }

    TEST_F(CombatDragonSave, aDragonInTheAirIsSavedWhereItWasLastPlacedAndTheNewestPlaceWins)
    {
        setDragons();
        add(10, 0.0, -300.0);
        run(60);
        ASSERT_NE(mCombat.dragonPhase({ 10, 1 }), dragons::Phase::Ground);
        const std::vector<dragons::DragonSave> saved = mCombat.savedDragons();
        ASSERT_EQ(saved.size(), 1u);
        EXPECT_EQ(saved[0].mPlacement, 10u);
        EXPECT_NEAR(saved[0].mX, mWriter.mStates[10].x, 1e-6);
        EXPECT_NEAR(saved[0].mY, mWriter.mStates[10].y, 1e-6);
        EXPECT_NEAR(saved[0].mZ, mWriter.mStates[10].z, 1e-6);
        EXPECT_GT(saved[0].mZ, 100.0);
    }

    TEST_F(CombatDragonSave, aLandedDragonLeavesTheSave)
    {
        setDragons();
        add(10, 0.0, -300.0);
        run(60);
        ASSERT_EQ(mCombat.savedDragons().size(), 1u);
        for (int i = 0; i < 600 && movesOf(Kind::Land) == 0; ++i)
            run(1);
        ASSERT_EQ(movesOf(Kind::Land), 1u);
        EXPECT_TRUE(mCombat.savedDragons().empty());
    }

    TEST_F(CombatDragonSave, aRestoredDragonResumesTheCruiseAtTheSavedPlaceWhenItsActorAppears)
    {
        setDragons();
        mCombat.restoreDragons({ saveOf(10, 1200.0, 0.0, 900.0, 1.5f) });
        EXPECT_EQ(mCombat.dragonRestoresPending(), 1u);
        add(10, 5.0, 5.0); // the engine built it at its placement, on the ground
        run(3);
        EXPECT_EQ(mCombat.dragonRestoresPending(), 0u);
        EXPECT_EQ(mCombat.dragonPhase({ 10, 1 }), dragons::Phase::Cruise);
        const SkyrimCombat::AiMoveIntent* first = nullptr;
        for (const auto& m : mMoves)
            if (m.mKind == Kind::Fly)
            {
                first = &m;
                break;
            }
        ASSERT_NE(first, nullptr);
        EXPECT_TRUE(first->mStart);
        EXPECT_EQ(first->mClip, dragons::kClipCruise);
        EXPECT_NEAR(first->mZ, 900.0, 1e-6);
        EXPECT_NEAR(std::hypot(first->mX, first->mY), 1200.0, 60.0);
    }

    TEST_F(CombatDragonSave, aRestoredDragonInACellThatIsNotLoadedWaitsAndAnotherActorTakesNothing)
    {
        setDragons();
        mCombat.restoreDragons({ saveOf(10, 1200.0, 0.0, 900.0), saveOf(11, 0.0, 1300.0, 900.0) });
        add(10, 5.0, 5.0);
        run(3);
        EXPECT_EQ(mCombat.dragonRestoresPending(), 1u) << "placement 11 has not been loaded";
        EXPECT_EQ(mCombat.dragonPhase({ 10, 1 }), dragons::Phase::Cruise);
        add(11, 7.0, 7.0);
        run(3);
        EXPECT_EQ(mCombat.dragonRestoresPending(), 0u);
        EXPECT_EQ(mCombat.dragonPhase({ 11, 1 }), dragons::Phase::Cruise);
    }

    TEST_F(CombatDragonSave, aRestoredDragonThatIsNotFightingLandsNearThePlayerAndThenFightsOnTheGround)
    {
        setDragons();
        mCombat.restoreDragons({ saveOf(10, 1200.0, 0.0, 900.0) });
        add(10, 5.0, 5.0);
        // The AI engages a `combat test hostile` actor at once, so the fight resumes: it circles for the air time.
        for (int i = 0; i < 400 && movesOf(Kind::Land) == 0; ++i)
            run(1);
        ASSERT_EQ(movesOf(Kind::Land), 1u);
        EXPECT_EQ(mCombat.dragonPhase({ 10, 1 }), dragons::Phase::Ground);
        EXPECT_TRUE(mCombat.savedDragons().empty());
    }

    TEST_F(CombatDragonSave, withTheDragonsSettingOffARestoreChangesNothing)
    {
        setDragons(false);
        mCombat.restoreDragons({ saveOf(10, 1200.0, 0.0, 900.0) });
        add(10, 5.0, 5.0);
        run(10);
        EXPECT_EQ(mCombat.dragonPhase({ 10, 1 }), dragons::Phase::Ground);
        EXPECT_EQ(movesOf(Kind::Fly), 0u);
        EXPECT_EQ(mCombat.dragonRestoresPending(), 0u);
    }

    TEST_F(CombatDragonSave, aRestoreListIsCappedAndAReplacementReplaces)
    {
        setDragons();
        std::vector<dragons::DragonSave> many;
        for (std::uint32_t i = 1; i <= dragons::kMaxDragonSaves + 20; ++i)
            many.push_back(saveOf(i));
        mCombat.restoreDragons(many);
        EXPECT_EQ(mCombat.dragonRestoresPending(), dragons::kMaxDragonSaves);
        mCombat.restoreDragons({ saveOf(5) });
        EXPECT_EQ(mCombat.dragonRestoresPending(), 1u);
    }

    // ---- several dragons --------------------------------------------------------------------------------------------

    TEST_F(CombatDragonSave, twoDragonsBreatheAndFlyEachOnItsOwnAndAreSavedInOrder)
    {
        setDragons(true, 2.f, 8.f);
        add(11, 0.0, -900.0);
        add(10, -1200.0, 0.0);
        EXPECT_EQ(mCombat.dragonCount(), 0u) << "learned at the first update";
        run(8);
        EXPECT_EQ(mCombat.dragonCount(), 2u);
        EXPECT_GE(mWriter.clipsOf(10, "ground_exhalefirebreath"), 1u);
        EXPECT_GE(mWriter.clipsOf(11, "ground_exhalefirebreath"), 1u);
        for (int i = 0; i < 400 && mCombat.savedDragons().size() < 2; ++i)
            run(1);
        EXPECT_GE(movesOf(Kind::Fly, 10), 1u);
        EXPECT_GE(movesOf(Kind::Fly, 11), 1u);
        const std::vector<dragons::DragonSave> saved = mCombat.savedDragons();
        ASSERT_EQ(saved.size(), 2u);
        EXPECT_EQ(saved[0].mPlacement, 10u);
        EXPECT_EQ(saved[1].mPlacement, 11u);
        EXPECT_GE(mCombat.dragonBreaths(), 2u);
    }

    TEST_F(CombatDragonSave, oneDragonDyingDoesNotStopTheOther)
    {
        setDragons(true, 2.f, 30.f);
        add(10, 0.0, -900.0);
        add(11, -1200.0, 0.0);
        for (int i = 0; i < 400 && mCombat.savedDragons().size() < 2; ++i)
            run(1);
        ASSERT_EQ(mCombat.savedDragons().size(), 2u);
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of({ 10, 1 }), 0.f));
        const std::size_t before = movesOf(Kind::Fly, 11);
        run(20);
        EXPECT_GT(movesOf(Kind::Fly, 11), before);
        EXPECT_NE(mCombat.dragonPhase({ 11, 1 }), dragons::Phase::Ground);
        EXPECT_EQ(movesOf(Kind::Land, 10), 1u) << "the dead one falls from its pose";
        const std::vector<dragons::DragonSave> saved = mCombat.savedDragons();
        ASSERT_EQ(saved.size(), 1u);
        EXPECT_EQ(saved[0].mPlacement, 11u);
    }

    // ---- the walk (the navmesh first, straight at the goal after "no path") -----------------------------------------

    struct CombatDragonWalk : CombatDragonSave
    {
        void navmesh(bool on)
        {
            dragons::Settings s;
            s.mOn = true;
            s.mNavmeshWalk = on;
            s.mFlight.mEnabled = false;
            mCombat.setDragons(s);
            ASSERT_TRUE(mCombat.setTestHostile("dragonx"));
        }
        std::vector<const SkyrimCombat::AiMoveIntent*> goTos() const
        {
            std::vector<const SkyrimCombat::AiMoveIntent*> out;
            for (const auto& m : mMoves)
                if (m.mKind == Kind::GoTo)
                    out.push_back(&m);
            return out;
        }
    };

    TEST_F(CombatDragonWalk, withTheNavmeshSettingOffEveryGoalIsADirectWalkAsInTheFirstSlice)
    {
        navmesh(false);
        add(10, 0.0, -2600.0); // beyond the breath's 2000: it walks
        run(40);
        const auto goals = goTos();
        ASSERT_FALSE(goals.empty());
        for (const auto* g : goals)
            EXPECT_TRUE(g->mDirect);
    }

    TEST_F(CombatDragonWalk, withTheNavmeshSettingOnTheFirstGoalAsksTheNavigator)
    {
        navmesh(true);
        add(10, 0.0, -2600.0);
        run(40);
        const auto goals = goTos();
        ASSERT_FALSE(goals.empty());
        for (const auto* g : goals)
            EXPECT_FALSE(g->mDirect);
    }

    TEST_F(CombatDragonWalk, afterNoPathItWalksStraightForTheFallbackSecondsAndThenAsksTheNavigatorAgain)
    {
        navmesh(true);
        add(10, 0.0, -2600.0);
        run(20);
        ASSERT_FALSE(goTos().empty());
        const std::size_t before = mMoves.size();
        mCombat.aiGoalFinished({ 10, 1 }, GoalResult::NoPath);
        run(30); // 3 s
        bool direct = false;
        for (std::size_t i = before; i < mMoves.size(); ++i)
            if (mMoves[i].mKind == Kind::GoTo)
            {
                direct = true;
                EXPECT_TRUE(mMoves[i].mDirect) << "inside the fallback seconds";
            }
        EXPECT_TRUE(direct) << "a goal was sent";
        run(static_cast<int>(dragons::kDirectFallbackSeconds * 10.f) + 20);
        mCombat.aiGoalFinished({ 10, 1 }, GoalResult::Arrived); // the walk ended: the AI sends the next goal
        const std::size_t after = mMoves.size();
        run(30);
        bool again = false;
        for (std::size_t i = after; i < mMoves.size(); ++i)
            if (mMoves[i].mKind == Kind::GoTo)
            {
                again = true;
                EXPECT_FALSE(mMoves[i].mDirect) << "the fallback is over";
            }
        EXPECT_TRUE(again);
    }

    TEST_F(CombatDragonWalk, aGoalThatArrivedOrWasBlockedDoesNotSwitchToTheDirectWalk)
    {
        navmesh(true);
        add(10, 0.0, -2600.0);
        run(20);
        mCombat.aiGoalFinished({ 10, 1 }, GoalResult::Arrived);
        mCombat.aiGoalFinished({ 10, 1 }, GoalResult::Blocked);
        const std::size_t before = mMoves.size();
        run(30);
        for (std::size_t i = before; i < mMoves.size(); ++i)
            if (mMoves[i].mKind == Kind::GoTo)
            {
                EXPECT_FALSE(mMoves[i].mDirect);
            }
    }
}

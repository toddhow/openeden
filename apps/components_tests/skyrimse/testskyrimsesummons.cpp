// Task 0250: summons and raise dead, the pack side, through the combat contract with a fake data source, a recording
// writer and recording world hooks. Every number is derived by hand:
//  - ConjureFamiliar: self delivery, fire and forget, cost 20, one Summon Creature effect (archetype 18) naming the NPC
//    base 300 ("Familiar"), duration 60 s;
//  - RaiseZombie: target actor delivery, cost 30, one Reanimate effect (archetype 22), magnitude 25 (the highest level
//    it may raise), duration 90 s;
//  - the player (the caster) stands at the origin facing +X (heading pi / 2: the engine's heading 0 faces +Y);
//  - Brenuin (20, base 200, level 10) stands at x 800; Cid (30, base 201, level 30) at x 900.
// HOW the rules combine is OURS or RECALLED-VERIFY: these tests pin THIS implementation.
#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/summons.hpp>

#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kFamiliarBase = 300, kBrenuinBase = 200, kCidBase = 201;
    constexpr FormId kConjureAt = 0x1002, kRaiseAimed = 0x1003;
    constexpr FormId kConjure = 0x1000, kRaise = 0x1001, kSummonEffect = 0x2000, kRaiseEffect = 0x2001;
    constexpr FormId kFamiliarForm = 300;
    constexpr FormId kFamiliarList = 0x5000; // a leveled list that resolves to the familiar
    constexpr float kPi = 3.14159265358979f;

    class SummonData final : public CombatData
    {
    public:
        SummonData()
        {
            addEffect(kSummonEffect, SummonArchetype::SummonCreature, kFamiliarForm);
            addEffect(kRaiseEffect, SummonArchetype::Reanimate, 0);
            addSpell(kConjure, "ConjureFamiliar", 20, MagicDelivery::Self, kSummonEffect, 0.f, 60);
            // The data's own conjure spells (Conjure Familiar and the others) have "target location" delivery.
            addSpell(kConjureAt, "ConjureAtLocation", 20, MagicDelivery::TargetLocation, kSummonEffect, 0.f, 60);
            addSpell(kRaise, "RaiseZombie", 30, MagicDelivery::TargetActor, kRaiseEffect, 25.f, 90);
            // Raise Zombie itself is an aimed spell in the data (its hand variants are target actor).
            addSpell(kRaiseAimed, "RaiseZombieAimed", 30, MagicDelivery::Aimed, kRaiseEffect, 25.f, 90);
            FighterProfile caster;
            caster.mHealthMax = 100.f;
            caster.mStaminaMax = 100.f;
            caster.mMagickaMax = 100.f;
            caster.mLevel = 12;
            mProfiles[kFamiliarBase] = caster;
            mProfiles[kFamiliarBase].mHealthMax = 40.f;
            caster.mHealthMax = 50.f;
            caster.mLevel = 10;
            mProfiles[kBrenuinBase] = caster;
            caster.mLevel = 30;
            mProfiles[kCidBase] = caster;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mLevel = 7;
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
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mSpells)
            {
                std::string lower = entry.second.mCommon.mEditorId;
                for (char& c : lower)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                if (lower == editorId)
                    return &entry.second;
            }
            return nullptr;
        }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        MagicSettings magicSettings() override { return {}; }

        void addEffect(FormId id, std::uint32_t archetype, FormId associated)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = -1;
            m.mAssociatedItem = associated;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t delivery, FormId effect,
            float magnitude, std::uint32_t duration)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = delivery;
            SpellEffect se;
            se.mEffect = effect;
            se.mMagnitude = magnitude;
            se.mDuration = duration;
            s.mEffects.push_back(se);
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class SummonWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind != ClipEventKind::SpellFire)
                return false;
            if (clip == "MRh_Release")
                out = 0.167f;
            else if (clip == "MRh_SelfRelease")
                out = 0.367f;
            else
                return false;
            return true;
        }
        bool launchProjectile(CombatSubject, const ProjectileRequest&) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor);
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
        bool castRay(CombatSubject, const RayRequest&, RayHit& out) override
        {
            out = mRayHit;
            return true;
        }
        std::vector<std::pair<CombatSubject, std::uint32_t>> mClips;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        RayHit mRayHit;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct PlacedCall
    {
        SummonFormId mBase;
        double mX, mY, mZ;
        float mHeading;
        WorldId mId;
    };

    struct SummonsTest : ::testing::Test
    {
        SummonData mData;
        SkyrimCombat mCombat;
        SummonWriter mWriter;
        std::vector<std::string> mTrace;
        std::vector<PlacedCall> mPlaced;
        std::vector<WorldId> mRemoved;
        std::vector<std::pair<WorldId, bool>> mFollows;
        SummonFormId mAskedAssociated = 0;
        std::uint16_t mAskedLevel = 0;
        WorldId mNextId = 0xFF800001;
        bool mRefusePlace = false;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        SummonHooks hooks()
        {
            SummonHooks h;
            h.resolveBase = [this](SummonFormId associated, std::uint16_t level, std::uint32_t) -> SummonFormId {
                mAskedAssociated = associated;
                mAskedLevel = level;
                if (associated == kFamiliarList)
                    return kFamiliarForm;
                return associated;
            };
            h.place = [this](SummonFormId base, double x, double y, double z, float heading) -> WorldId {
                if (mRefusePlace)
                    return kNoWorldId;
                const WorldId id = mNextId++;
                mPlaced.push_back({ base, x, y, z, heading, id });
                return id;
            };
            h.remove = [this](WorldId reference) { mRemoved.push_back(reference); };
            h.follow = [this](WorldId reference, bool follow) { mFollows.push_back({ reference, follow }); };
            return h;
        }
        void start(bool summons = true, bool withHooks = true)
        {
            mCombat.setSpellDelivery(true);
            mCombat.setSummons(summons);
            if (withHooks)
                mCombat.setSummonHooks(hooks());
            mCombat.setData(&mData);
            mCombat.onActorAdded(info(20, kBrenuinBase, "Brenuin"));
            mCombat.onActorAdded(info(30, kCidBase, "Cid"));
            place(20, 800.0, 0.0);
            place(30, 900.0, 50.0);
            mWriter.mPlayerState.heading = kPi / 2.f;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void place(WorldId reference, double x, double y)
        {
            SubjectState s;
            s.x = x;
            s.y = y;
            s.heading = kPi / 2.f;
            mWriter.mStates[reference] = s;
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        void playerCasts(const char* spell)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            mCombat.onPlayerAttackEdge(true);
            updates(4);
            mCombat.onPlayerAttackEdge(false);
            update();
        }
        // The core tells the pack about the actor it made for the last placement.
        void arrives(const PlacedCall& placed, std::string_view name = "Familiar")
        {
            mCombat.onActorAdded(info(placed.mId, placed.mBase, name));
            place(placed.mId, placed.mX, placed.mY);
            update();
        }
        void kill(WorldId reference)
        {
            ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of({ reference, 1 }), 0.f));
        }
    };

    // ---- saves ------------------------------------------------------------------------------------------------

    class NoPackages final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>&) const override { return false; }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId, double&, double&, double&) const override { return false; }
    };

    TEST(SummonSaves, aSummonThatFollowsThePlayerIsNotInTheSavedFollowers)
    {
        NoPackages source;
        SkyrimActorTable table;
        table.enablePackages(source, 1);
        ASSERT_TRUE(table.recruit(0x20));
        ASSERT_TRUE(table.recruit(kCreatedObjectBase + 1));
        EXPECT_TRUE(table.isFollower(kCreatedObjectBase + 1)) << "it follows while it lives";
        const std::vector<FollowerEntry> saved = table.followerEntries();
        ASSERT_EQ(saved.size(), 1u) << "but a save does not keep it: summons are not saved";
        EXPECT_EQ(saved[0].mId, 0x20u);
    }

    // ---- the pure rules ---------------------------------------------------------------------------------------

    TEST(SummonRules, theSpawnSpotIsAheadOfTheCasterAlongItsHeading)
    {
        const SummonSpot north = summonSpawnSpot(10.0, 20.0, 30.0, 0.0, 150.0, 8.0);
        EXPECT_NEAR(north.x, 10.0, 1e-9);
        EXPECT_NEAR(north.y, 170.0, 1e-9);
        EXPECT_NEAR(north.z, 38.0, 1e-9);
        const SummonSpot east = summonSpawnSpot(10.0, 20.0, 30.0, 1.5707963267948966, 150.0, 8.0);
        EXPECT_NEAR(east.x, 160.0, 1e-9);
        EXPECT_NEAR(east.y, 20.0, 1e-9);
    }

    TEST(SummonRules, aSummonLivesForTheEffectsDurationOrTheDefault)
    {
        EXPECT_FLOAT_EQ(summonSeconds(75), 75.f);
        EXPECT_FLOAT_EQ(summonSeconds(0), kDefaultSummonSeconds);
    }

    TEST(SummonRules, theMagnitudeIsTheHighestLevelThatMayBeRaised)
    {
        EXPECT_TRUE(reanimateLevelAllowed(10, 25.f));
        EXPECT_TRUE(reanimateLevelAllowed(25, 25.f));
        EXPECT_FALSE(reanimateLevelAllowed(26, 25.f));
        EXPECT_TRUE(reanimateLevelAllowed(80, 0.f)) << "no magnitude: no limit";
        EXPECT_TRUE(reanimateLevelAllowed(80, -3.f));
        EXPECT_TRUE(reanimateLevelAllowed(80, std::numeric_limits<float>::quiet_NaN()));
    }

    TEST(SummonRules, aCorpseMustBeAheadAndWithinReach)
    {
        // The caster faces +X (heading pi / 2).
        EXPECT_TRUE(corpseInSights(0.0, 0.0, 1.5707963267948966, 800.0, 0.0, 1500.0, 0.3));
        EXPECT_FALSE(corpseInSights(0.0, 0.0, 1.5707963267948966, -800.0, 0.0, 1500.0, 0.3)) << "behind";
        EXPECT_FALSE(corpseInSights(0.0, 0.0, 1.5707963267948966, 2000.0, 0.0, 1500.0, 0.3)) << "too far";
        EXPECT_FALSE(corpseInSights(0.0, 0.0, 1.5707963267948966, 0.0, 800.0, 1500.0, 0.3)) << "off to the side";
        EXPECT_TRUE(corpseInSights(5.0, 5.0, 1.5707963267948966, 5.0, 5.0, 1500.0, 0.3)) << "on the same spot";
    }

    TEST(SummonRules, theLedgerKeepsEntriesInOrderAndRefusesBadOnes)
    {
        SummonLedger ledger;
        SummonEntry a;
        a.mReference = 0xFF800001;
        a.mMaster = CombatSubject::ofPlayer();
        a.mExpiresAt = 50.f;
        SummonEntry b = a;
        b.mReference = 0xFF800002;
        b.mExpiresAt = 10.f;
        SummonEntry none = a;
        none.mReference = kNoWorldId;
        EXPECT_TRUE(ledger.add(a));
        EXPECT_TRUE(ledger.add(b));
        EXPECT_FALSE(ledger.add(a)) << "the same reference twice";
        EXPECT_FALSE(ledger.add(none));
        EXPECT_EQ(ledger.size(), 2u);
        EXPECT_EQ(ledger.countOf(CombatSubject::ofPlayer()), 2u);
        EXPECT_EQ(ledger.countOf(CombatSubject::of({ 5, 1 })), 0u);
        ASSERT_NE(ledger.oldestOf(CombatSubject::ofPlayer()), nullptr);
        EXPECT_EQ(ledger.oldestOf(CombatSubject::ofPlayer())->mReference, 0xFF800001u);
        const std::vector<WorldId> due = ledger.expired(10.f);
        ASSERT_EQ(due.size(), 1u);
        EXPECT_EQ(due[0], 0xFF800002u);
        EXPECT_TRUE(ledger.expired(9.99f).empty());
        EXPECT_TRUE(ledger.erase(0xFF800002));
        EXPECT_FALSE(ledger.erase(0xFF800002));
        EXPECT_EQ(ledger.find(0xFF800001)->mExpiresAt, 50.f);
        EXPECT_EQ(ledger.find(0xFF800002), nullptr);
    }

    TEST(SummonRules, theLedgerHoldsNoMoreThanItsCap)
    {
        SummonLedger ledger;
        for (std::size_t i = 0; i < kMaxSummonEntries; ++i)
        {
            SummonEntry e;
            e.mReference = 0xFF800000u + static_cast<WorldId>(i) + 1;
            e.mMaster = CombatSubject::ofPlayer();
            EXPECT_TRUE(ledger.add(e)) << i;
        }
        SummonEntry extra;
        extra.mReference = 0xFF8FFFFF;
        EXPECT_FALSE(ledger.add(extra));
    }

    // ---- Summon Creature --------------------------------------------------------------------------------------

    TEST_F(SummonsTest, aConjureSpellPlacesTheCreatureAheadOfTheCaster)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        EXPECT_EQ(mPlaced[0].mBase, kFamiliarForm);
        // The player stands at the origin facing +X: the spot is 150 ahead and 8 above.
        EXPECT_NEAR(mPlaced[0].mX, kSummonSpawnDistance, 1e-3);
        EXPECT_NEAR(mPlaced[0].mY, 0.0, 1e-3);
        EXPECT_NEAR(mPlaced[0].mZ, kSummonSpawnRise, 1e-3);
        EXPECT_NEAR(mPlaced[0].mHeading, kPi / 2.f, 1e-3);
        EXPECT_EQ(mAskedAssociated, kFamiliarForm);
        EXPECT_EQ(mAskedLevel, 7) << "the caster's level picks a leveled list";
        ASSERT_EQ(mCombat.summons().size(), 1u);
        const SummonEntry& entry = mCombat.summons().entries()[0];
        EXPECT_EQ(entry.mReference, mPlaced[0].mId);
        EXPECT_TRUE(entry.mMaster.player);
        EXPECT_EQ(entry.mKind, SummonKind::Summoned);
        EXPECT_TRUE(traceHas("COMBAT summon"));
    }

    TEST_F(SummonsTest, aConjureSpellWithTargetLocationDeliveryPlacesTheCreatureAheadToo)
    {
        start();
        playerCasts("conjureatlocation");
        ASSERT_EQ(mPlaced.size(), 1u);
        EXPECT_NEAR(mPlaced[0].mX, kSummonSpawnDistance, 1e-3);
        EXPECT_EQ(mCombat.summons().size(), 1u);
    }

    TEST_F(SummonsTest, aConjureSpellNeedsTheDeliverySetting)
    {
        start();
        mCombat.setSpellDelivery(false);
        playerCasts("conjureatlocation");
        EXPECT_TRUE(mPlaced.empty());
    }

    TEST_F(SummonsTest, nothingIsPlacedWithTheSettingOffOrWithoutHooks)
    {
        start(false);
        playerCasts("conjurefamiliar");
        EXPECT_TRUE(mPlaced.empty());
        EXPECT_EQ(mCombat.summons().size(), 0u);
    }

    TEST_F(SummonsTest, aWorldThatRefusesTheActorLeavesNoEntry)
    {
        mRefusePlace = true;
        start();
        playerCasts("conjurefamiliar");
        EXPECT_TRUE(mPlaced.empty());
        EXPECT_EQ(mCombat.summons().size(), 0u);
        EXPECT_TRUE(traceHas("COMBAT summon failed"));
    }

    TEST_F(SummonsTest, aSummonOfThePlayerFollowsHimOnceItArrives)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        EXPECT_TRUE(mFollows.empty()) << "it is not an actor yet";
        arrives(mPlaced[0]);
        ASSERT_EQ(mFollows.size(), 1u);
        EXPECT_EQ(mFollows[0].first, mPlaced[0].mId);
        EXPECT_TRUE(mFollows[0].second);
        EXPECT_TRUE(mCombat.summons().entries()[0].mArrived);
    }

    TEST_F(SummonsTest, aSummonGoesWhenItsDurationEnds)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        arrives(mPlaced[0]);
        updates(55, 1.f);
        EXPECT_TRUE(mRemoved.empty()) << "60 seconds have not passed";
        updates(10, 1.f);
        ASSERT_EQ(mRemoved.size(), 1u);
        EXPECT_EQ(mRemoved[0], mPlaced[0].mId);
        EXPECT_EQ(mCombat.summons().size(), 0u);
        EXPECT_TRUE(traceHas("COMBAT summon ended"));
    }

    TEST_F(SummonsTest, aNewSummonDismissesTheOldOneOfTheSameCaster)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        arrives(mPlaced[0]);
        const WorldId first = mPlaced[0].mId;
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 2u);
        ASSERT_EQ(mRemoved.size(), 1u);
        EXPECT_EQ(mRemoved[0], first);
        ASSERT_EQ(mCombat.summons().size(), 1u);
        EXPECT_EQ(mCombat.summons().entries()[0].mReference, mPlaced[1].mId);
    }

    TEST_F(SummonsTest, aKilledSummonVanishesAfterAShortWhile)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        arrives(mPlaced[0]);
        kill(mPlaced[0].mId);
        update(1.f);
        EXPECT_TRUE(mRemoved.empty()) << "it lies there for a moment";
        update(1.f);
        update(1.f);
        ASSERT_EQ(mRemoved.size(), 1u);
        EXPECT_EQ(mRemoved[0], mPlaced[0].mId);
        EXPECT_EQ(mCombat.summons().size(), 0u);
    }

    TEST_F(SummonsTest, anActorTheCoreRemovedLeavesNoEntryAndNoRemoveCall)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        arrives(mPlaced[0]);
        mCombat.onActorRemoved({ mPlaced[0].mId, 1 });
        EXPECT_EQ(mCombat.summons().size(), 0u);
        EXPECT_TRUE(mRemoved.empty());
    }

    TEST_F(SummonsTest, aSummonedActorIsLeftAloneByTheLedgerOfAnotherCaster)
    {
        start();
        playerCasts("conjurefamiliar");
        ASSERT_EQ(mPlaced.size(), 1u);
        // Cid (not a summon) is removed: the player's entry stays.
        mCombat.onActorRemoved({ 30, 1 });
        EXPECT_EQ(mCombat.summons().size(), 1u);
    }

    // ---- Reanimate --------------------------------------------------------------------------------------------

    TEST_F(SummonsTest, aCorpseInFrontRisesAsALivingActorAtItsPlace)
    {
        start();
        kill(20);
        playerCasts("raisezombie");
        ASSERT_EQ(mPlaced.size(), 1u);
        EXPECT_EQ(mPlaced[0].mBase, static_cast<SummonFormId>(kBrenuinBase)) << "a new actor of the corpse's base";
        EXPECT_NEAR(mPlaced[0].mX, 800.0, 1e-3);
        EXPECT_NEAR(mPlaced[0].mY, 0.0, 1e-3);
        ASSERT_EQ(mRemoved.size(), 1u) << "the corpse is hidden";
        EXPECT_EQ(mRemoved[0], 20u);
        ASSERT_EQ(mCombat.summons().size(), 1u);
        const SummonEntry& entry = mCombat.summons().entries()[0];
        EXPECT_EQ(entry.mKind, SummonKind::Reanimated);
        EXPECT_EQ(entry.mCorpse, 20u);
        EXPECT_TRUE(traceHas("COMBAT reanimate"));
    }

    TEST_F(SummonsTest, anAimedReanimateSpellRaisesTheCorpseToo)
    {
        start();
        kill(20);
        playerCasts("raisezombieaimed");
        ASSERT_EQ(mPlaced.size(), 1u);
        EXPECT_EQ(mPlaced[0].mBase, static_cast<SummonFormId>(kBrenuinBase));
        EXPECT_EQ(mCombat.summons().size(), 1u);
    }

    TEST_F(SummonsTest, aCorpseAboveTheLevelLimitStaysDead)
    {
        start();
        kill(30); // Cid, level 30, magnitude 25
        playerCasts("raisezombie");
        EXPECT_TRUE(mPlaced.empty());
        EXPECT_TRUE(mRemoved.empty());
        EXPECT_EQ(mCombat.summons().size(), 0u);
        EXPECT_TRUE(traceHas("level"));
    }

    TEST_F(SummonsTest, aLivingActorIsNeverRaised)
    {
        start();
        playerCasts("raisezombie");
        EXPECT_TRUE(mPlaced.empty());
        EXPECT_EQ(mCombat.summons().size(), 0u);
    }

    TEST_F(SummonsTest, reanimateNeedsTheDeliverySetting)
    {
        start();
        mCombat.setSpellDelivery(false);
        kill(20);
        playerCasts("raisezombie");
        EXPECT_TRUE(mPlaced.empty());
    }

    TEST_F(SummonsTest, aRaisedCorpseFallsDeadAgainWhenTheDurationEnds)
    {
        start();
        kill(20);
        playerCasts("raisezombie");
        ASSERT_EQ(mPlaced.size(), 1u);
        arrives(mPlaced[0], "Brenuin");
        ASSERT_EQ(mFollows.size(), 1u);
        EXPECT_TRUE(mWriter.mDeaths.empty());
        updates(95, 1.f);
        ASSERT_FALSE(mWriter.mDeaths.empty());
        EXPECT_EQ(mWriter.mDeaths.back().reference, mPlaced[0].mId);
        EXPECT_EQ(mCombat.summons().size(), 0u);
        EXPECT_EQ(std::count(mRemoved.begin(), mRemoved.end(), mPlaced[0].mId), 0)
            << "the risen actor stays as a corpse, it is not taken out";
    }

    TEST_F(SummonsTest, theTestKillKillsTheNamedActorOnceAfterItsDelay)
    {
        start();
        mCombat.setTestKill("Brenuin");
        updates(8, 0.25f);
        EXPECT_TRUE(mWriter.mDeaths.empty()) << "two seconds in";
        updates(8, 0.25f);
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_EQ(mWriter.mDeaths[0].reference, 20u);
        updates(8, 0.25f);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
    }

    TEST_F(SummonsTest, aRaisedCorpseThatIsKilledKeepsItsCorpse)
    {
        start();
        kill(20);
        playerCasts("raisezombie");
        ASSERT_EQ(mPlaced.size(), 1u);
        arrives(mPlaced[0], "Brenuin");
        kill(mPlaced[0].mId);
        updates(5, 1.f);
        EXPECT_EQ(mCombat.summons().size(), 0u);
        EXPECT_EQ(std::count(mRemoved.begin(), mRemoved.end(), mPlaced[0].mId), 0);
    }
}

// Task 0233: the effects that change what an actor can do (paralysis, frenzy, fear) and weakness to an element, in the
// Skyrim pack's magic runtime. The pure rules (magic_status.hpp) are tested by hand-derived numbers; the combat side is
// tested through the combat contract with a fake data source and a recording writer. Every number is derived by hand:
//  - the effects are the real ones in shape (VERIFIED on the masters, see magic_status.hpp): Paralysis archetype 21,
//    Frenzy 8, Fear (Demoralize) 7, weakness Peak 34 on the actor value 41 (fire) or 44 (magic), flags 0x7;
//  - "Burnbolt" does 20 fire damage (resist value 41) to the target's 50 health, no armour, no resistance;
//  - the actors: Archer (10) at the origin, Brenuin (20) at (800, 0), Cedric (30) at (800, 150), the player (0, 600).
// How the numbers combine is RECALLED-VERIFY: these tests pin THIS implementation.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/magic_status.hpp>

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

    // ---- the pure rules --------------------------------------------------------------------------------------------

    TEST(MagicStatusPure, theResistanceValuesAreFortyToFortyFive)
    {
        EXPECT_FALSE(isResistValue(39));
        for (std::int32_t v = 40; v <= 45; ++v)
            EXPECT_TRUE(isResistValue(v)) << v;
        EXPECT_FALSE(isResistValue(46));
        EXPECT_FALSE(isResistValue(-1));
        EXPECT_FALSE(isResistValue(24)); // a vital is not a resistance
    }

    TEST(MagicStatusPure, theLevelGateLetsInATargetAtOrBelowTheMagnitude)
    {
        EXPECT_TRUE(levelGateAllows(4, 8.f));
        EXPECT_TRUE(levelGateAllows(8, 8.f));
        EXPECT_FALSE(levelGateAllows(9, 8.f));
        EXPECT_FALSE(levelGateAllows(1, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FALSE(levelGateAllows(1, std::numeric_limits<float>::infinity() * 0.f));
        EXPECT_TRUE(levelGateAllows(50, std::numeric_limits<float>::infinity()) == false);
    }

    TEST(MagicStatusPure, aWeaknessIsTheNegatedSumCappedAtOneHundred)
    {
        EXPECT_FLOAT_EQ(weaknessPercent(-50.f), 50.f);
        EXPECT_FLOAT_EQ(weaknessPercent(-125.f), 100.f);
        EXPECT_FLOAT_EQ(weaknessPercent(0.f), 0.f);
        EXPECT_FLOAT_EQ(weaknessPercent(30.f), 0.f); // a resistance buff is no weakness
        EXPECT_FLOAT_EQ(weaknessPercent(std::numeric_limits<float>::quiet_NaN()), 0.f);
    }

    TEST(MagicStatusPure, weakenedAddsThePercentToAMagnitude)
    {
        EXPECT_FLOAT_EQ(weakened(20.f, 50.f), 30.f);
        EXPECT_FLOAT_EQ(weakened(20.f, 0.f), 20.f);
        EXPECT_FLOAT_EQ(weakened(20.f, 500.f), 40.f); // clamped at 100
        EXPECT_FLOAT_EQ(weakened(20.f, -10.f), 20.f); // never lowers
        EXPECT_FLOAT_EQ(weakened(std::numeric_limits<float>::quiet_NaN(), 50.f), 0.f);
        EXPECT_FLOAT_EQ(weakened(20.f, std::numeric_limits<float>::quiet_NaN()), 20.f);
    }

    TEST(MagicStatusPure, extendingAStatusKeepsTheLaterEnd)
    {
        EXPECT_FLOAT_EQ(extendStatus(-1.f, 10.f, 6.f), 16.f);
        EXPECT_FLOAT_EQ(extendStatus(20.f, 10.f, 6.f), 20.f); // the longer one stays
        EXPECT_FLOAT_EQ(extendStatus(12.f, 10.f, 6.f), 16.f);
        EXPECT_FLOAT_EQ(extendStatus(-1.f, 10.f, 0.f), -1.f);
        EXPECT_FLOAT_EQ(extendStatus(-1.f, 10.f, -3.f), -1.f);
        EXPECT_FLOAT_EQ(extendStatus(-1.f, 10.f, std::numeric_limits<float>::quiet_NaN()), -1.f);
        EXPECT_FLOAT_EQ(extendStatus(-1.f, 10.f, 1.0e9f), 10.f + kMaxStatusSeconds); // cut
    }

    TEST(MagicStatusPure, aStatusIsOnUntilItsEnd)
    {
        EXPECT_TRUE(statusActive(16.f, 15.99f));
        EXPECT_FALSE(statusActive(16.f, 16.f));
        EXPECT_FALSE(statusActive(-1.f, 0.f)); // -1: never started
    }

    TEST(MagicStatusPure, theArchetypesAreTheOnesTheDataShows)
    {
        EXPECT_EQ(StatusArchetype::Paralysis, 21u);
        EXPECT_EQ(StatusArchetype::Frenzy, 8u);
        EXPECT_EQ(StatusArchetype::Demoralize, 7u);
        EXPECT_EQ(StatusArchetype::Calm, 6u);
        EXPECT_EQ(ResistValue::Fire, 41);
        EXPECT_EQ(ResistValue::Magic, 44);
    }

    // ---- the combat side -------------------------------------------------------------------------------------------

    constexpr WorldId kArcherBase = 100, kBrenuinBase = 200, kCedricBase = 300;
    constexpr FormId kParalyze = 0x1000, kFury = 0x1001, kFear = 0x1002, kWeakFire = 0x1003, kWeakMagic = 0x1004,
                     kBurnbolt = 0x1005, kWeakFrost = 0x1006, kSelfBurn = 0x1007;
    constexpr FormId kParaEffect = 0x2000, kFuryEffect = 0x2001, kFearEffect = 0x2002, kWeakFireEffect = 0x2003,
                     kWeakMagicEffect = 0x2004, kBurnEffect = 0x2005, kWeakFrostEffect = 0x2006,
                     kSelfBurnEffect = 0x2007;
    constexpr FormId kProjectile = 0x3000;

    class StatusData final : public CombatData
    {
    public:
        StatusData()
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "TestBoltProjectile";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mGravity = 0.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\TestBolt.nif";

            constexpr std::uint32_t hostile = MagicFlag::Hostile | MagicFlag::Detrimental;
            constexpr std::uint32_t weak = MagicFlag::Hostile | MagicFlag::Recover | MagicFlag::Detrimental;
            addEffect(kParaEffect, StatusArchetype::Paralysis, 53, hostile);
            addEffect(kFuryEffect, StatusArchetype::Frenzy, 0, MagicFlag::Hostile);
            addEffect(kFearEffect, StatusArchetype::Demoralize, 1, MagicFlag::Hostile);
            addEffect(kWeakFireEffect, MagicArchetype::PeakValueModifier, 41, weak);
            addEffect(kWeakMagicEffect, MagicArchetype::PeakValueModifier, 44, weak);
            addEffect(kWeakFrostEffect, MagicArchetype::PeakValueModifier, 43, weak);
            addEffect(kBurnEffect, MagicArchetype::ValueModifier, 24, hostile, 41);
            addEffect(kSelfBurnEffect, MagicArchetype::ValueModifier, 25, MagicFlag::Recover); // magicka, no heal

            addSpell(kParalyze, "Paralyze", 5, MagicDelivery::Aimed, kParaEffect, 0.f, 6);
            addSpell(kFury, "Fury", 5, MagicDelivery::Aimed, kFuryEffect, 8.f, 10);
            addSpell(kFear, "Fear", 5, MagicDelivery::Aimed, kFearEffect, 8.f, 12);
            addSpell(kWeakFire, "WeakFire", 5, MagicDelivery::Aimed, kWeakFireEffect, 50.f, 30);
            addSpell(kWeakMagic, "WeakMagic", 5, MagicDelivery::Aimed, kWeakMagicEffect, 25.f, 30);
            addSpell(kWeakFrost, "WeakFrost", 5, MagicDelivery::Aimed, kWeakFrostEffect, 50.f, 30);
            addSpell(kBurnbolt, "Burnbolt", 5, MagicDelivery::Aimed, kBurnEffect, 20.f, 0);
            addSpell(kSelfBurn, "SelfBurn", 5, MagicDelivery::Self, kSelfBurnEffect, 1.f, 1);
            mEffects[kParaEffect].mProjectile = kProjectile;
            mEffects[kFuryEffect].mProjectile = kProjectile;
            mEffects[kFearEffect].mProjectile = kProjectile;
            mEffects[kWeakFireEffect].mProjectile = kProjectile;
            mEffects[kWeakMagicEffect].mProjectile = kProjectile;
            mEffects[kWeakFrostEffect].mProjectile = kProjectile;
            mEffects[kBurnEffect].mProjectile = kProjectile;

            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;

            FighterProfile npc;
            npc.mHealthMax = 50.f;
            npc.mStaminaMax = 80.f;
            npc.mMagickaMax = 100.f;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mSkills[0] = 50;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 4; // foolhardy: no flight by health in these tests
            npc.mLevel = 3;
            mProfiles[kArcherBase] = npc;
            mProfiles[kBrenuinBase] = npc;
            mProfiles[kCedricBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
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
        const WeaponRecord* weaponByEditorId(std::string_view id) override
        {
            return id == "ironsword" ? &mSword : nullptr;
        }
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
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }

        void addEffect(
            FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags, std::int32_t resist = -1)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mResistValue = resist;
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
        ProjectileRecord mProj;
        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class StatusWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            std::uint32_t mToken;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
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
        bool launchProjectile(CombatSubject, const ProjectileRequest& request) override
        {
            mLaunches.push_back(request.token);
            return true;
        }
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
        [[nodiscard]] std::size_t clipsOf(CombatSubject who, ClipSlot slot) const
        {
            std::size_t n = 0;
            for (const Clip& c : mClips)
                if (c.mSubject == who && c.mSlot == slot)
                    ++n;
            return n;
        }

        std::vector<Clip> mClips;
        std::vector<std::uint32_t> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    using Kind = SkyrimCombat::AiMoveIntent::Kind;

    struct CombatStatus : ::testing::Test
    {
        StatusData mData;
        SkyrimCombat mCombat;
        StatusWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        ActorHandle mC{ 30, 1 };
        std::vector<std::string> mTrace;
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start(bool ai = true)
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            if (ai)
                mCombat.setCombatAi(true);
            mCombat.onActorAdded(infoOf(10, kArcherBase, "Archer"));
            mCombat.onActorAdded(infoOf(20, kBrenuinBase, "Brenuin"));
            mCombat.onActorAdded(infoOf(30, kCedricBase, "Cedric"));
            SubjectState a;
            mWriter.mStates[10] = a;
            SubjectState b;
            b.x = 800.0;
            mWriter.mStates[20] = b;
            SubjectState c;
            c.x = 800.0;
            c.y = 150.0;
            mWriter.mStates[30] = c;
            mWriter.mPlayerState.x = 0.0;
            mWriter.mPlayerState.y = 600.0;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void update(float seconds = 0.25f)
        {
            mCombat.combatUpdate(seconds, mWriter);
            mCombat.drainAiMoves(mMoves);
        }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        [[nodiscard]] bool traced(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        [[nodiscard]] std::size_t movesOf(ActorHandle who, Kind kind) const
        {
            std::size_t n = 0;
            for (const auto& m : mMoves)
                if (m.mActor == who && m.mKind == kind)
                    ++n;
            return n;
        }
        SkyrimCombat::FighterView view(ActorHandle who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::of(who), v));
            return v;
        }
        // The player casts `spell` and the bolt hits `victim`.
        void playerCast(const char* spell, ActorHandle victim)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            const std::size_t before = mWriter.mLaunches.size();
            mCombat.onPlayerAttackEdge(true);
            updates(3);
            mCombat.onPlayerAttackEdge(false);
            ASSERT_EQ(mWriter.mLaunches.size(), before + 1) << spell;
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = mWriter.mLaunches.back();
            r.fate = ProjectileFate::HitActor;
            r.actor = victim;
            mCombat.onProjectileResult(r);
            update();
        }
    };

    // ---- paralysis -------------------------------------------------------------------------------------------------

    TEST_F(CombatStatus, aParalysisHoldsTheTargetForTheDurationAndLetsItGo)
    {
        start();
        playerCast("Paralyze", mB);
        EXPECT_TRUE(mCombat.paralyzed(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.statusesApplied(), 1u);
        // it falls: a clip of the Death slot (the core holds its last pose), and the package brain is held
        EXPECT_EQ(mWriter.clipsOf(CombatSubject::of(mB), ClipSlot::Death), 1u);
        EXPECT_EQ(movesOf(mB, Kind::Suspend), 1u);
        EXPECT_GE(movesOf(mB, Kind::Stop), 1u);
        EXPECT_TRUE(traced("paralysis"));
        updates(30); // 7.5 s: past the 6 s
        EXPECT_FALSE(mCombat.paralyzed(CombatSubject::of(mB)));
        // it gets up: a recovery clip of the Stagger slot, and the brain is given back
        EXPECT_EQ(mWriter.clipsOf(CombatSubject::of(mB), ClipSlot::Stagger), 1u);
        EXPECT_EQ(movesOf(mB, Kind::Resume), 1u);
        EXPECT_EQ(mWriter.clipsOf(CombatSubject::of(mB), ClipSlot::Death), 1u); // no second fall
    }

    TEST_F(CombatStatus, aParalysisOnThePlayerIsNotApplied)
    {
        // Archer casts Paralyze at the player (test caster) and the bolt hits him: the pack cannot hold the player.
        start(false);
        ASSERT_TRUE(mCombat.setTestCasts("Archer:Paralyze"));
        mCombat.setTestCastEvery(1.f);
        updates(4);
        const std::size_t casts = mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::Cast);
        ASSERT_EQ(casts, 1u);
        ClipEvent fire;
        fire.subject = CombatSubject::of(mA);
        fire.slot = ClipSlot::Cast;
        fire.token = mWriter.mClips.back().mToken;
        fire.kind = ClipEventKind::SpellFire;
        mCombat.onClipEvent(fire);
        update();
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        ProjectileResult r;
        r.owner = CombatSubject::of(mA);
        r.token = mWriter.mLaunches.back();
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_FALSE(mCombat.paralyzed(CombatSubject::ofPlayer()));
        EXPECT_EQ(mCombat.statusesApplied(), 0u);
        EXPECT_TRUE(traced("player"));
    }

    TEST_F(CombatStatus, aParalysedActorDoesNotCastAndAFreeOneDoes)
    {
        start(false);
        ASSERT_TRUE(mCombat.setTestCasts("Brenuin:SelfBurn"));
        mCombat.setTestCastEvery(2.f);
        updates(12); // 3 s: the first cast begins at 2 s
        EXPECT_GE(mWriter.clipsOf(CombatSubject::of(mB), ClipSlot::Cast), 1u);
    }

    TEST_F(CombatStatus, aHeldCasterDoesNotBeginACast)
    {
        start(false);
        ASSERT_TRUE(mCombat.setTestCasts("Brenuin:SelfBurn"));
        mCombat.setTestCastEvery(2.f);
        playerCast("Paralyze", mB); // at about 1 s: held until about 7 s
        ASSERT_TRUE(mCombat.paralyzed(CombatSubject::of(mB)));
        updates(16); // to about 5 s: the 2 s and 4 s casts are due and refused
        EXPECT_TRUE(mCombat.paralyzed(CombatSubject::of(mB)));
        EXPECT_EQ(mWriter.clipsOf(CombatSubject::of(mB), ClipSlot::Cast), 0u);
    }

    TEST_F(CombatStatus, aParalysedHostileDoesNotAttackAndAFreeOneDoes)
    {
        start();
        ASSERT_TRUE(mCombat.setTestHostile("archer"));
        mCombat.onActorAdded(infoOf(10, kArcherBase, "Archer"));
        mData.mProfiles[kArcherBase].mAi.mStyle.mOffensive = 1.f;
        mData.mProfiles[kArcherBase].mAi.mStyle.mDefensive = 0.f;
        mWriter.mPlayerState.x = 0.0;
        mWriter.mPlayerState.y = 100.0; // in reach of Archer
        updates(8);
        const std::size_t freeSwings = mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::Attack)
            + mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::PowerAttack)
            + mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::Bash);
        EXPECT_GE(freeSwings, 1u);
        playerCast("Paralyze", mA);
        EXPECT_TRUE(mCombat.paralyzed(CombatSubject::of(mA)));
        const auto swings = [&] {
            return mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::Attack)
                + mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::PowerAttack)
                + mWriter.clipsOf(CombatSubject::of(mA), ClipSlot::Bash);
        };
        const std::size_t held = swings();
        updates(16); // 4 s of the 6 s
        EXPECT_TRUE(mCombat.paralyzed(CombatSubject::of(mA)));
        EXPECT_EQ(swings(), held);
    }

    // ---- frenzy and fear -------------------------------------------------------------------------------------------

    TEST_F(CombatStatus, aFrenziedActorAttacksTheNearestActorAndStopsWhenItEnds)
    {
        start();
        playerCast("Fury", mB);
        EXPECT_TRUE(mCombat.frenzied(CombatSubject::of(mB)));
        update();
        // Cedric is 150 away, Archer 800 and the player 1,000: the nearest is Cedric
        EXPECT_EQ(mCombat.aiTargetOf(mB), CombatSubject::of(mC));
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_EQ(movesOf(mB, Kind::Suspend), 1u);
        EXPECT_TRUE(traced("frenzy"));
        updates(48); // 12 s: past the 10 s
        EXPECT_FALSE(mCombat.frenzied(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_EQ(movesOf(mB, Kind::Resume), 1u);
    }

    TEST_F(CombatStatus, aFrenziedActorChoosesAgainWhenItsTargetDies)
    {
        start();
        playerCast("Fury", mB);
        update();
        ASSERT_EQ(mCombat.aiTargetOf(mB), CombatSubject::of(mC));
        EXPECT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mC), 0.f));
        updates(4);
        // with Cedric gone the nearest is Archer (800) and not the player (1,000)
        EXPECT_EQ(mCombat.aiTargetOf(mB), CombatSubject::of(mA));
    }

    TEST_F(CombatStatus, aFrenzyOnATargetAboveTheMagnitudeLevelDoesNothing)
    {
        start();
        mData.mProfiles[kBrenuinBase].mLevel = 20; // the Fury is 8
        playerCast("Fury", mB);
        EXPECT_FALSE(mCombat.frenzied(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_TRUE(traced("level"));
    }

    TEST_F(CombatStatus, aFearMakesTheTargetFleeTheCasterAndEndsWithTheDuration)
    {
        start();
        playerCast("Fear", mB);
        EXPECT_TRUE(mCombat.feared(CombatSubject::of(mB)));
        updates(2);
        EXPECT_EQ(mCombat.aiModeOf(mB), AiMode::Retreat);
        // away from the player at (0, 600): Brenuin at (800, 0) runs to +x and -y
        const SkyrimCombat::AiMoveIntent* go = nullptr;
        for (const auto& m : mMoves)
            if (m.mActor == mB && m.mKind == Kind::GoTo)
                go = &m;
        ASSERT_NE(go, nullptr);
        EXPECT_GT(go->mX, 800.0);
        EXPECT_LT(go->mY, 0.0);
        EXPECT_EQ(go->mSpeed, ActorSpeed::Run);
        updates(60); // 15 s: past the 12 s
        EXPECT_FALSE(mCombat.feared(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.aiModeOf(mB), AiMode::Calm);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_GE(movesOf(mB, Kind::Resume), 1u);
    }

    TEST_F(CombatStatus, aFearOnATargetAboveTheMagnitudeLevelDoesNotMakeItFlee)
    {
        start();
        mData.mProfiles[kBrenuinBase].mLevel = 20;
        playerCast("Fear", mB);
        updates(2);
        EXPECT_FALSE(mCombat.feared(CombatSubject::of(mB)));
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_TRUE(view(mB).mDemoralized); // the old bookkeeping of the effect is unchanged
    }

    TEST_F(CombatStatus, aFearWithTheAiOffOnlyKeepsTheOldDemoralizedFlag)
    {
        start(false);
        playerCast("Fear", mB);
        updates(2);
        EXPECT_TRUE(view(mB).mDemoralized);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_TRUE(movesOf(mB, Kind::GoTo) == 0u);
    }

    // ---- weakness --------------------------------------------------------------------------------------------------

    TEST_F(CombatStatus, withNoWeaknessABurnboltDoesItsTwentyFireDamage)
    {
        start();
        playerCast("Burnbolt", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 30.f);
    }

    TEST_F(CombatStatus, aWeaknessToFireAddsItsPercentToFireDamage)
    {
        start();
        playerCast("WeakFire", mB);
        playerCast("Burnbolt", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 20.f); // 20 x 1.5 = 30 off 50
    }

    TEST_F(CombatStatus, aWeaknessToMagicAddsToItToo)
    {
        start();
        playerCast("WeakFire", mB);
        playerCast("WeakMagic", mB);
        playerCast("Burnbolt", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 15.f); // 20 x (1 + 0.5 + 0.25) = 35 off 50
    }

    TEST_F(CombatStatus, aWeaknessToFrostLeavesFireDamageAlone)
    {
        start();
        playerCast("WeakFrost", mB);
        playerCast("Burnbolt", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 30.f);
    }

    TEST_F(CombatStatus, aWeaknessOnlyWorksOnItsOwnTarget)
    {
        start();
        playerCast("WeakFire", mB);
        playerCast("Burnbolt", mC);
        EXPECT_FLOAT_EQ(view(mC).mHealth, 30.f);
    }

    TEST_F(CombatStatus, aWeaknessEndsWithItsDuration)
    {
        start();
        playerCast("WeakFire", mB);
        updates(130); // 32 s: past the 30 s
        playerCast("Burnbolt", mB);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 30.f);
    }
}

// Task 0348 (College of Winterhold, fourth slice): the ward lesson of MG01. Tolfdir's package "MG01Stage50TolfdirCastAtPlayer"
// (the UseMagic template, a begin fragment that sets the quest's TimerInit) makes him cast MG01TeachFirebolt at the player;
// the player's ward absorbs it and the game's own MG01PlayerScript hears OnWardHit; the scene's phases read the player's
// WardPower (actor value 63). Written first and run red against code that did none of it.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <tuple>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    // ---- the package: UseMagic and its begin fragment ----------------------------------------------------------------
    constexpr FormId kTolfdirBase = 0x0001C1A1;
    constexpr WorldId kTolfdirRef = 0x1C1A2;
    constexpr FormId kTeachFirebolt = 0x0009152C;

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out.clear();
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Unsupported; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        PackageActivity activityOf(const PackageRecord& package) const override
        {
            return classifyActivity(package.mProcedures);
        }
        bool targetOf(const PackageRecord& package, PackageTarget& out) const override
        {
            if (package.mTargets.empty())
                return false;
            out = package.mTargets.front();
            return true;
        }
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal&) override { return true; }
        bool cancelGoal(ActorHandle) override { return true; }
        ActorClock clock() const override { return { 12.f, 0, true }; }
    };

    struct BrainRig
    {
        Source src;
        PackageRecord pkg;
        Writer w;
        std::unique_ptr<PackageBrain> brain;
        std::vector<std::pair<WorldId, FormId>> mCasts;
        std::vector<std::pair<WorldId, std::string>> mBegins;

        explicit BrainRig(bool useMagic = true, bool withBegin = true, bool installHooks = true)
        {
            pkg.mCommon.mId = 0x9152D;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            if (useMagic)
                pkg.mProcedures.push_back("UseMagic");
            pkg.mTargets.push_back({ PackageTargetType::ObjectId, kTeachFirebolt, 0 });
            pkg.mTargets.push_back({ PackageTargetType::SpecificReference, 0x14, 0 });
            if (withBegin)
            {
                pkg.mBeginScript = "PF_MG01Stage50TolfdirCastAtPl_0009152D";
                pkg.mBeginFunction = "Fragment_0";
            }
            brain = std::make_unique<PackageBrain>(src);
            if (installHooks)
            {
                brain->setUseMagicHook([this](ActorHandle actor, FormId spell) { mCasts.emplace_back(actor.reference, spell); });
                brain->setPackageBeginHook([this](WorldId actor, const PackageRecord& package) {
                    mBegins.emplace_back(actor, package.mBeginFunction);
                });
            }
            ActorInfo i;
            i.handle = { kTolfdirRef, 1 };
            i.base = kTolfdirBase;
            i.editorId = "MG01Tolfdir";
            brain->onActorAdded(i);
            brain->forcePackage(kTolfdirRef, &pkg);
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
                brain->update(0.5f, w);
        }
    };

    TEST(SkyrimCollege4, theUseMagicProcedureIsAnActivityOfItsOwn)
    {
        const std::vector<std::string> names = { "UseMagic" };
        EXPECT_EQ(classifyActivity(names), PackageActivity::UseMagic);
        const std::vector<std::string> sandbox = { "Sandbox", "UseMagic" };
        EXPECT_EQ(classifyActivity(sandbox), PackageActivity::None);
        const std::vector<std::string> travel = { "Travel" };
        EXPECT_EQ(classifyActivity(travel), PackageActivity::None);
    }

    TEST(SkyrimCollege4, aUseMagicPackageAsksForOneCastOfItsSpellWhenItBegins)
    {
        BrainRig r;
        r.step(6);
        ASSERT_EQ(r.mCasts.size(), 1u);
        EXPECT_EQ(r.mCasts.front().first, kTolfdirRef);
        EXPECT_EQ(r.mCasts.front().second, kTeachFirebolt);
        r.step(20); // it stays the package in force and does not cast again
        EXPECT_EQ(r.mCasts.size(), 1u);
    }

    TEST(SkyrimCollege4, aPackageWithNoUseMagicProcedureAsksForNoCast)
    {
        BrainRig r(false);
        r.step(6);
        EXPECT_TRUE(r.mCasts.empty());
    }

    TEST(SkyrimCollege4, theBeginFragmentIsToldOnceWhenThePackageBegins)
    {
        BrainRig r;
        r.step(6);
        ASSERT_EQ(r.mBegins.size(), 1u);
        EXPECT_EQ(r.mBegins.front().first, kTolfdirRef);
        EXPECT_EQ(r.mBegins.front().second, "Fragment_0");
        r.step(20);
        EXPECT_EQ(r.mBegins.size(), 1u);
    }

    TEST(SkyrimCollege4, aPackageWithNoBeginFragmentTellsNobody)
    {
        BrainRig r(true, false);
        r.step(6);
        EXPECT_TRUE(r.mBegins.empty());
        EXPECT_EQ(r.mCasts.size(), 1u);
    }

    TEST(SkyrimCollege4, withNoHooksTheUseMagicPackageIsHarmless)
    {
        BrainRig r(true, true, false);
        r.step(6);
        EXPECT_TRUE(r.mCasts.empty());
        EXPECT_TRUE(r.mBegins.empty());
    }

    // ---- the player's ward power as a condition answer ---------------------------------------------------------------
    constexpr std::uint32_t kWardPower = 63;

    TEST(SkyrimCollege4, theWardPowerOfThePlayerIsWhatTheWardSourceSays)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        world.setPlayerWardSource([] { return 52.f; });
        EXPECT_FLOAT_EQ(world.actorValue(SkyrimConditionWorld::kPlayer, kWardPower), 52.f);
    }

    TEST(SkyrimCollege4, withNoWardSourceTheWardPowerIsZero)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        EXPECT_FLOAT_EQ(world.actorValue(SkyrimConditionWorld::kPlayer, kWardPower), 0.f);
    }

    TEST(SkyrimCollege4, theWardSourceAnswersOnlyForThePlayerAndOnlyForTheWardPower)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        world.setPlayerWardSource([] { return 52.f; });
        EXPECT_FLOAT_EQ(world.actorValue(SkyrimConditionWorld::kPlayer, kWardPower - 1), 0.f);
        EXPECT_FLOAT_EQ(world.actorValue(static_cast<conditions::Subject>(0x01000200), kWardPower), 0.f);
    }

    TEST(SkyrimCollege4, aWardSourceThatSaysNotANumberIsZero)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        world.setPlayerWardSource([] { return std::nanf(""); });
        EXPECT_FLOAT_EQ(world.actorValue(SkyrimConditionWorld::kPlayer, kWardPower), 0.f);
    }

    // ---- an actor's cast at the player, and the ward event ------------------------------------------------------------
    constexpr WorldId kCasterBase = 100;
    constexpr FormId kFirebolt = 0x1000, kWard = 0x1003, kTeach = 0x1010;
    constexpr FormId kTeachEffect = 0x2010;
    constexpr FormId kFireEffect = 0x2000, kAccumulateEffect = 0x2005, kPeakEffect = 0x2006;
    constexpr FormId kProjectile = 0x3000;
    constexpr float kPi = 3.14159265358979f;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "FireboltProjectile01";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\FireBoltProjectile.nif";
            addEffect(kFireEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile, 41);
            addEffect(kAccumulateEffect, MagicArchetype::AccumulateMagnitude, 63, 0x40001002);
            addEffect(kPeakEffect, MagicArchetype::PeakValueModifier, 39, 0x40001002);
            // MG01FireDamageFFAimed: flags 0x10221804, Detrimental (bit 2) and NOT Hostile (bit 0)
            addEffect(kTeachEffect, 0, 24, MagicFlag::Detrimental, kProjectile, 41);
            addSpell(kTeach, "MG01TeachFirebolt", 23, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kTeachEffect, 15.f } });
            addSpell(kFirebolt, "Firebolt", 41, MagicCast::FireAndForget, MagicDelivery::Aimed, { { kFireEffect, 25.f } });
            addSpell(kWard, "WardLesser", 34, MagicCast::Concentration, MagicDelivery::Self,
                { { kAccumulateEffect, 40.f }, { kPeakEffect, 40.f } });
            FighterProfile caster;
            caster.mHealthMax = 100.f;
            caster.mStaminaMax = 100.f;
            caster.mMagickaMax = 100.f;
            mProfiles[kCasterBase] = caster;
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
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }
        MagicSettings magicSettings() override { return MagicSettings(); }

        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags,
            FormId projectile = kNullFormId, std::int32_t resist = -1)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mProjectile = projectile;
            m.mResistValue = resist;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t cast, std::uint32_t delivery,
            const std::vector<std::pair<FormId, float>>& effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = cast;
            s.mDelivery = delivery;
            for (const auto& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.first;
                se.mMagnitude = e.second;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            if (request.slot == ClipSlot::Cast)
                mCastClips.emplace_back(subject, request.token);
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
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            mLaunches.emplace_back(owner, request.token);
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
        bool reportDeath(ActorHandle) override { return true; }
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
        std::vector<std::pair<CombatSubject, std::uint32_t>> mCastClips;
        std::vector<std::pair<CombatSubject, std::uint32_t>> mLaunches;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    using Event = SkyrimCombat::SpellEvent;

    struct ScriptedCast : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        std::vector<Event> mEvents;
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setSpellEventSink([this](const Event& e) { mEvents.push_back(e); });
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void TearDown() override
        {
            if (HasFailure())
                for (const std::string& line : mTrace)
                    std::cerr << "  trace: " << line << std::endl;
        }
        void start()
        {
            mData.mPlayer.mMagickaMax = 1000.f; // a ward costs 34 a second: the stand-in's 100 would end it in 3 s
            mCombat.setData(&mData);
            mCombat.onActorAdded(info(10, kCasterBase, "MG01Tolfdir"));
            SubjectState a;
            a.heading = kPi / 2.f;
            mWriter.mStates[10] = a;
            SubjectState player = a;
            player.x = 400.0;
            player.healthMax = 100.f;
            player.health = 100.f;
            mWriter.mPlayerState = player;
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n)
        {
            for (int i = 0; i < n; ++i)
                update();
        }
        std::size_t count(Event::Kind kind) const
        {
            return static_cast<std::size_t>(
                std::count_if(mEvents.begin(), mEvents.end(), [&](const Event& e) { return e.mKind == kind; }));
        }
        // The cast clip of the actor reaches its spell-fire event (the engine's job), then the bolt is launched.
        void fireActorCast()
        {
            ASSERT_FALSE(mWriter.mCastClips.empty());
            ClipEvent e;
            e.subject = mWriter.mCastClips.back().first;
            e.slot = ClipSlot::Cast;
            e.token = mWriter.mCastClips.back().second;
            e.kind = ClipEventKind::SpellFire;
            mCombat.onClipEvent(e);
            update();
        }
        void wardUp()
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell("WardLesser"));
            mCombat.onPlayerAttackEdge(true);
            updates(8);
            SkyrimCombat::FighterView v;
            ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
            ASSERT_GT(v.mWardPower, 0.f);
        }
    };

    TEST_F(ScriptedCast, anActorAskedToCastASpellLaunchesItAtThePlayer)
    {
        start();
        ASSERT_TRUE(mCombat.castSpellAt(mA, kFirebolt));
        updates(4);
        fireActorCast();
        EXPECT_EQ(count(Event::Kind::Cast), 1u);
        const auto it = std::find_if(
            mEvents.begin(), mEvents.end(), [](const Event& e) { return e.mKind == Event::Kind::Cast; });
        ASSERT_NE(it, mEvents.end());
        EXPECT_FALSE(it->mCaster.player);
        EXPECT_EQ(it->mCaster.actor.reference, 10u);
        EXPECT_EQ(it->mSpell, kFirebolt);
        ASSERT_FALSE(mWriter.mLaunches.empty());
        EXPECT_EQ(mWriter.mLaunches.back().first.actor.reference, 10u);
    }

    TEST_F(ScriptedCast, aSpellNobodyKnowsIsRefusedAndNothingIsCast)
    {
        start();
        EXPECT_FALSE(mCombat.castSpellAt(mA, 0x7777));
        updates(4);
        EXPECT_EQ(count(Event::Kind::Cast), 0u);
    }

    TEST_F(ScriptedCast, anActorTheCombatDoesNotKnowIsRefused)
    {
        start();
        EXPECT_FALSE(mCombat.castSpellAt(ActorHandle{ 99, 1 }, kFirebolt));
    }

    TEST_F(ScriptedCast, aBoltThatTheWardAbsorbsGivesAWardHitNamingCasterTargetAndSpell)
    {
        start();
        wardUp();
        mEvents.clear();
        ASSERT_TRUE(mCombat.castSpellAt(mA, kFirebolt));
        updates(4);
        fireActorCast();
        ASSERT_FALSE(mWriter.mLaunches.empty());
        ProjectileResult r;
        r.owner = CombatSubject::of(mA);
        r.token = mWriter.mLaunches.back().second;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        ASSERT_EQ(count(Event::Kind::WardHit), 1u);
        const auto it = std::find_if(
            mEvents.begin(), mEvents.end(), [](const Event& e) { return e.mKind == Event::Kind::WardHit; });
        EXPECT_TRUE(it->mTarget.player);
        EXPECT_FALSE(it->mCaster.player);
        EXPECT_EQ(it->mCaster.actor.reference, 10u);
        EXPECT_EQ(it->mSpell, kFirebolt);
        EXPECT_TRUE(mWriter.mDamage.empty()); // the ward held all of it
    }

    TEST_F(ScriptedCast, aDetrimentalSpellThatIsNotFlaggedHostileIsStillHeldByTheWard)
    {
        start();
        wardUp();
        mEvents.clear();
        ASSERT_TRUE(mCombat.castSpellAt(mA, kTeach));
        updates(4);
        fireActorCast();
        ASSERT_FALSE(mWriter.mLaunches.empty());
        ProjectileResult r;
        r.owner = CombatSubject::of(mA);
        r.token = mWriter.mLaunches.back().second;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_EQ(count(Event::Kind::WardHit), 1u);
        EXPECT_TRUE(mWriter.mDamage.empty()); // the ward held all 15
    }

    TEST_F(ScriptedCast, aBoltWithNoWardGivesNoWardHitAndHurtsThePlayer)
    {
        start();
        ASSERT_TRUE(mCombat.castSpellAt(mA, kFirebolt));
        updates(4);
        fireActorCast();
        ASSERT_FALSE(mWriter.mLaunches.empty());
        ProjectileResult r;
        r.owner = CombatSubject::of(mA);
        r.token = mWriter.mLaunches.back().second;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_EQ(count(Event::Kind::WardHit), 0u);
        EXPECT_FALSE(mWriter.mDamage.empty());
    }

    // ---- OnWardHit reaches the script on the player's alias ----------------------------------------------------------
    using namespace ContentTests;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kPlayerRef = 0x14;
    constexpr FormId kHostTolfdir = 0x01000A02;
    constexpr FormId kHostSpell = 0x01000C01;
    constexpr FormId kBase = 0x01000B00;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class HostData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
    };

    struct HostRig
    {
        HostRig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            mData.add(actor);
            ScriptBuilder spellClass("Spell"); // the real script declares `spell AkSpell`
            spellClass.parent("Form");
            mData.add(spellClass);
            ScriptBuilder probe("WardProbe");
            probe.parent("ObjectReference");
            probe.var("wards", "Int");
            probe.var("status", "Int");
            probe.var("lastCaster", "ObjectReference");
            probe.var("lastSpell", "Form");
            probe.function("OnWardHit")
                .param("akCaster", "ObjectReference")
                .param("akSpell", "Spell")
                .param("aiStatus", "Int")
                .iadd(Id("wards"), Id("wards"), Int(1))
                .assign(Id("status"), Id("aiStatus"))
                .assign(Id("lastCaster"), Id("akCaster"))
                .assign(Id("lastSpell"), Id("akSpell"));
            mData.add(probe);
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kHostTolfdir] = "Actor";
            mData.mClasses[kPlayerRef] = "Actor";
            mData.mClasses[kHostSpell] = "Spell";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            // the Player alias of MG01: a unique actor alias whose actor is the Player NPC_ (0x00000007), and one of
            // another actor
            for (const auto& [id, name, base] : { std::tuple<std::uint32_t, const char*, FormId>{ 13, "Player", 7 },
                     std::tuple<std::uint32_t, const char*, FormId>{ 14, "Other", 0x01000F00 } })
            {
                quest.append(sub4("ALST", Bytes().u32(id)));
                quest.append(sub4("ALID", Bytes().zstring(name)));
                quest.append(sub4("FNAM", Bytes().u32(0)));
                quest.append(sub4("ALUA", Bytes().u32(base)));
                quest.append(sub4("ALED", Bytes()));
            }
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
        }
        ~HostRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void attach(FormId ref, bool actor)
        {
            vmad::Script s;
            s.mName = "WardProbe";
            ASSERT_EQ(mHost->attachReference(ref, kBase, actor, { s }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        std::optional<double> number(FormId ref, const char* name)
        {
            return mHost->numberVariable(ref, "WardProbe", name);
        }
        HostData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimCollege4, aWardHitReachesOnWardHitOfTheScriptsOnThePlayerWithCasterAndSpell)
    {
        HostRig rig;
        rig.attach(kPlayerRef, true);
        rig.mHost->onWardHit(kPlayerRef, kHostTolfdir, kHostSpell, 0);
        rig.tick();
        EXPECT_EQ(rig.number(kPlayerRef, "wards"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kPlayerRef, "WardProbe", "lastCaster"), kHostTolfdir);
        EXPECT_EQ(rig.mHost->formVariable(kPlayerRef, "WardProbe", "lastSpell"), kHostSpell);
        EXPECT_EQ(rig.number(kPlayerRef, "status"), 0.0);
    }

    TEST(SkyrimCollege4, aUniqueActorAliasOfThePlayerNpcHoldsThePlayerWhenTheHostIsToldSo)
    {
        HostRig rig;
        rig.mHost->setPlayerAliasFill(true);
        EXPECT_EQ(rig.mHost->aliasReference(kQuestA, 13), kPlayerRef);
        EXPECT_EQ(rig.mHost->aliasReference(kQuestA, 14), 0u); // another actor with no placed reference: nothing
    }

    TEST(SkyrimCollege4, withoutTheSwitchThePlayerAliasHoldsNothingAsBefore)
    {
        HostRig rig;
        EXPECT_EQ(rig.mHost->aliasReference(kQuestA, 13), 0u);
    }

    TEST(SkyrimCollege4, aWardHitDoesNotReachScriptsOfOtherReferences)
    {
        HostRig rig;
        rig.attach(kPlayerRef, true);
        rig.attach(kHostTolfdir, true);
        rig.mHost->onWardHit(kPlayerRef, kHostTolfdir, kHostSpell, 0);
        rig.tick();
        EXPECT_EQ(rig.number(kHostTolfdir, "wards"), 0.0);
    }

    TEST(SkyrimCollege4, aWardHitWithNoScriptIsHarmless)
    {
        HostRig rig;
        rig.mHost->onWardHit(kPlayerRef, kHostTolfdir, kHostSpell, 0);
        rig.tick();
        EXPECT_TRUE(rig.mErrors.empty());
    }
}

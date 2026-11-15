// Task 0180 (stage S5-11, the combat HUD): the pack's side. The numbers the HUD shows (CombatHud), the writer that
// watches damage reports (HudWriter) and the script package that hands them over (CombatHudBindings). Written from
// the header (game_skyrimse/hud.hpp) and the task spec first; no game data.
#include <game_skyrimse/hud.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk;
    using namespace openeden::sdk::game;

    ActorHandle handle(std::uint32_t reference)
    {
        return ActorHandle{ reference, 1 };
    }

    // The writer the HUD wraps: the player's state, and a record of what passed through.
    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override
        {
            ++mPlayClips;
            return true;
        }
        bool stopClip(CombatSubject) override
        {
            ++mStopClips;
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override
        {
            ++mSweeps;
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mReports.push_back(report);
            return mAcceptReports;
        }
        bool faceHeading(CombatSubject, float) override
        {
            ++mFaces;
            return true;
        }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out.hitTime = 0.25f;
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player || !mHasPlayer)
                return false;
            out.health = mHealth;
            out.healthMax = mHealthMax;
            return true;
        }
        bool launchProjectile(CombatSubject, const ProjectileRequest&) override
        {
            ++mLaunches;
            return true;
        }
        bool setClipSpeed(CombatSubject, float) override
        {
            ++mSpeeds;
            return true;
        }
        bool clipEventTime(std::string_view, bool, ClipEventKind, float& out) override
        {
            out = 1.5f;
            return true;
        }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor);
            return mAcceptReports;
        }

        bool mHasPlayer = true;
        float mHealth = 80.f;
        float mHealthMax = 100.f;
        bool mAcceptReports = true;
        int mPlayClips = 0, mStopClips = 0, mSweeps = 0, mFaces = 0, mLaunches = 0, mSpeeds = 0;
        std::vector<DamageReport> mReports;
        std::vector<ActorHandle> mDeaths;
    };

    // What the pack knows about each fighter, by the lookup the HUD is given.
    struct FakeFighters
    {
        std::map<std::uint32_t, SkyrimCombat::FighterView> mActors;
        SkyrimCombat::FighterView mPlayer;
        bool mHasPlayer = true;
        CombatHud::FighterLookup lookup()
        {
            return [this](CombatSubject subject, SkyrimCombat::FighterView& out) {
                if (subject.player)
                {
                    out = mPlayer;
                    return mHasPlayer;
                }
                auto it = mActors.find(subject.actor.reference);
                if (it == mActors.end())
                    return false;
                out = it->second;
                return true;
            };
        }
    };

    DamageReport hit(bool attackerIsPlayer, ActorHandle attacker, bool victimIsPlayer, ActorHandle victim,
        float amount, float healthAfter, float healthMax)
    {
        DamageReport report;
        report.attackerIsPlayer = attackerIsPlayer;
        report.attacker = attacker;
        report.victimIsPlayer = victimIsPlayer;
        report.victim = victim;
        report.amount = amount;
        report.healthAfter = healthAfter;
        report.healthMax = healthMax;
        return report;
    }

    struct CombatHudTest : testing::Test
    {
        CombatHud hud;
        FakeFighters fighters;
        FakeWriter writer;
        CombatHudTest()
        {
            fighters.mPlayer.mStamina = 60.f;
            fighters.mPlayer.mStaminaMax = 100.f;
            hud.setFighterLookup(fighters.lookup());
            hud.actorAdded(handle(10), "Hulda");
            hud.actorAdded(handle(11), "Mikael");
            fighters.mActors[10] = { 75.f, 75.f, 29.f, 30.f };
        }
        std::array<double, kHudValueCount> values() const
        {
            std::array<double, kHudValueCount> v{};
            hud.values(v);
            return v;
        }
    };

    TEST_F(CombatHudTest, isInactiveUntilTheFirstUpdate)
    {
        EXPECT_FALSE(hud.active());
        hud.update(0.016f, writer);
        EXPECT_TRUE(hud.active());
    }

    TEST_F(CombatHudTest, theValueOrderIsPinned)
    {
        EXPECT_EQ(kHudValueCount, 10u);
        EXPECT_EQ(kHudNamespace, "skyrimse.combat");
    }

    TEST_F(CombatHudTest, theBarsOfThePlayerComeFromTheEngineHealthAndThePackStamina)
    {
        hud.update(0.016f, writer);
        const auto v = values();
        EXPECT_DOUBLE_EQ(v[0], 80.0);
        EXPECT_DOUBLE_EQ(v[1], 100.0);
        EXPECT_DOUBLE_EQ(v[4], 60.0);
        EXPECT_DOUBLE_EQ(v[5], 100.0);
        // Magicka is the stand-in: full (decision for toddhow, no magicka system before spells).
        EXPECT_DOUBLE_EQ(v[2], kHudMagickaStandIn);
        EXPECT_DOUBLE_EQ(v[3], kHudMagickaStandIn);
        EXPECT_DOUBLE_EQ(v[6], 0.0) << "no enemy yet";
    }

    TEST_F(CombatHudTest, thePlayerBarsFollowTheEngineEveryUpdate)
    {
        hud.update(0.016f, writer);
        writer.mHealth = 55.5f;
        fighters.mPlayer.mStamina = 12.f;
        hud.update(0.016f, writer);
        const auto v = values();
        EXPECT_DOUBLE_EQ(v[0], 55.5);
        EXPECT_DOUBLE_EQ(v[4], 12.0);
    }

    TEST_F(CombatHudTest, aPlayerWithNoEngineHealthKeepsTheLastBarsAndZeroAtFirst)
    {
        writer.mHasPlayer = false;
        hud.update(0.016f, writer);
        EXPECT_DOUBLE_EQ(values()[1], 0.0) << "healthMax 0 tells the script to draw no health bar";
        writer.mHasPlayer = true;
        hud.update(0.016f, writer);
        writer.mHasPlayer = false;
        hud.update(0.016f, writer);
        EXPECT_DOUBLE_EQ(values()[0], 80.0);
    }

    TEST_F(CombatHudTest, aHitTheEnemyTakesShowsItsBarAndName)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.5f, 67.5f, 75.f));
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        EXPECT_EQ(hud.enemyName(), "Hulda");
        const auto v = values();
        EXPECT_DOUBLE_EQ(v[6], 1.0);
        EXPECT_DOUBLE_EQ(v[7], 67.5);
        EXPECT_DOUBLE_EQ(v[8], 75.0);
        EXPECT_DOUBLE_EQ(v[9], 0.0);
    }

    TEST_F(CombatHudTest, aHitOnThePlayerShowsTheAttackerAsTheEnemy)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(false, handle(11), true, {}, 7.77f, 92.23f, 100.f));
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        EXPECT_EQ(hud.enemyName(), "Mikael");
    }

    TEST_F(CombatHudTest, aHitBetweenTwoActorsChangesNothing)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(false, handle(11), false, handle(10), 7.f, 60.f, 75.f));
        EXPECT_FALSE(hud.snapshot().mEnemyShown);
        EXPECT_TRUE(hud.enemyName().empty());
    }

    TEST_F(CombatHudTest, aHitBetweenThePlayerAndThePlayerChangesNothing)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, true, {}, 7.f, 60.f, 75.f));
        EXPECT_FALSE(hud.snapshot().mEnemyShown);
    }

    TEST_F(CombatHudTest, theBarStaysForTheDocumentedSecondsAndThenGoes)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f));
        hud.update(kHudEnemyBarSeconds - 0.5f, writer);
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        hud.update(1.0f, writer);
        EXPECT_FALSE(hud.snapshot().mEnemyShown);
        EXPECT_DOUBLE_EQ(values()[6], 0.0);
    }

    TEST_F(CombatHudTest, aLaterHitStartsTheTimeAgain)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f));
        hud.update(kHudEnemyBarSeconds - 1.f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 61.f, 75.f));
        hud.update(kHudEnemyBarSeconds - 1.f, writer);
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
    }

    TEST_F(CombatHudTest, aHitOnAnotherActorMovesTheBarToIt)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f));
        hud.noteDamage(hit(true, {}, false, handle(11), 5.f, 30.f, 40.f));
        EXPECT_EQ(hud.enemyName(), "Mikael");
        EXPECT_DOUBLE_EQ(values()[7], 30.0);
    }

    TEST_F(CombatHudTest, theEnemyHealthFollowsThePackWhileTheBarIsShown)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f));
        fighters.mActors[10].mHealth = 70.f;
        hud.update(0.5f, writer);
        EXPECT_DOUBLE_EQ(values()[7], 70.0);
    }

    TEST_F(CombatHudTest, theDeathOfTheTargetMarksItAndHidesTheBarAfterTheDeadSeconds)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 0.f, 75.f));
        hud.noteDeath(handle(10));
        EXPECT_TRUE(hud.snapshot().mEnemyDead);
        EXPECT_DOUBLE_EQ(values()[9], 1.0);
        EXPECT_DOUBLE_EQ(values()[7], 0.0);
        hud.update(kHudDeadBarSeconds - 0.5f, writer);
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        hud.update(1.f, writer);
        EXPECT_FALSE(hud.snapshot().mEnemyShown);
    }

    TEST_F(CombatHudTest, aDeathNobodyIsLookingAtChangesNothing)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f));
        hud.noteDeath(handle(11));
        EXPECT_FALSE(hud.snapshot().mEnemyDead);
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
    }

    TEST_F(CombatHudTest, aRemovedTargetLeavesNoBar)
    {
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f));
        hud.actorRemoved(handle(10));
        hud.update(0.016f, writer);
        EXPECT_FALSE(hud.snapshot().mEnemyShown);
    }

    TEST_F(CombatHudTest, aLongNameIsCutAtACharacterBoundary)
    {
        std::string name(kMaxHudNameBytes - 1, 'a');
        name += "\xC3\xA9\xC3\xA9"; // two 2-byte characters straddle the limit
        hud.actorAdded(handle(12), name);
        hud.noteDamage(hit(true, {}, false, handle(12), 1.f, 5.f, 10.f));
        const std::string_view shown = hud.enemyName();
        EXPECT_LE(shown.size(), kMaxHudNameBytes);
        EXPECT_EQ(shown.size(), kMaxHudNameBytes - 1) << "the half character is dropped, not kept";
        for (char c : shown)
            EXPECT_EQ(c, 'a');
    }

    TEST_F(CombatHudTest, anActorWithoutANameIsShownWithAnEmptyName)
    {
        hud.noteDamage(hit(true, {}, false, handle(99), 1.f, 5.f, 10.f));
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        EXPECT_TRUE(hud.enemyName().empty());
    }

    TEST_F(CombatHudTest, numbersThatAreNotFiniteOrNegativeBecomeZeroAndNeverReachTheScript)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        writer.mHealth = nan;
        writer.mHealthMax = inf;
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 1.f, nan, -5.f));
        for (double v : values())
            EXPECT_TRUE(std::isfinite(v));
        EXPECT_GE(values()[7], 0.0);
        EXPECT_GE(values()[8], 0.0);
        hud.update(nan, writer);
        hud.update(-3.f, writer);
        EXPECT_TRUE(hud.snapshot().mEnemyShown) << "a bad frame time must not expire or crash the bar";
    }

    TEST_F(CombatHudTest, theNameTableIsBounded)
    {
        for (std::uint32_t i = 0; i < kMaxHudActors + 10; ++i)
            hud.actorAdded(handle(1000 + i), "x");
        hud.noteDamage(hit(true, {}, false, handle(1000 + kMaxHudActors + 5), 1.f, 5.f, 10.f));
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        EXPECT_TRUE(hud.enemyName().empty());
    }

    // ---- HudWriter ----

    TEST(CombatHudWriter, passesEveryRequestOnAndAnswersAsTheInnerWriterDoes)
    {
        CombatHud hud;
        FakeWriter inner;
        HudWriter writer(inner, hud);
        EXPECT_TRUE(writer.playClip(CombatSubject::ofPlayer(), ClipRequest{}));
        EXPECT_TRUE(writer.stopClip(CombatSubject::ofPlayer()));
        EXPECT_TRUE(writer.requestSweep(CombatSubject::ofPlayer(), SweepRequest{}));
        EXPECT_TRUE(writer.faceHeading(CombatSubject::ofPlayer(), 1.f));
        ClipTiming timing;
        EXPECT_TRUE(writer.clipTiming("x", false, timing));
        EXPECT_FLOAT_EQ(timing.hitTime, 0.25f);
        SubjectState state;
        EXPECT_TRUE(writer.subjectState(CombatSubject::ofPlayer(), state));
        EXPECT_FLOAT_EQ(state.health, 80.f);
        EXPECT_TRUE(writer.launchProjectile(CombatSubject::ofPlayer(), ProjectileRequest{}));
        EXPECT_TRUE(writer.setClipSpeed(CombatSubject::ofPlayer(), 1.f));
        float t = 0.f;
        EXPECT_TRUE(writer.clipEventTime("x", false, ClipEventKind::Hit, t));
        EXPECT_FLOAT_EQ(t, 1.5f);
        EXPECT_EQ(inner.mPlayClips, 1);
        EXPECT_EQ(inner.mStopClips, 1);
        EXPECT_EQ(inner.mSweeps, 1);
        EXPECT_EQ(inner.mFaces, 1);
        EXPECT_EQ(inner.mLaunches, 1);
        EXPECT_EQ(inner.mSpeeds, 1);
    }

    TEST(CombatHudWriter, aReportTheCoreAcceptedReachesTheHudAndTheInnerWriter)
    {
        CombatHud hud;
        hud.actorAdded(handle(10), "Hulda");
        FakeWriter inner;
        HudWriter writer(inner, hud);
        const DamageReport report = hit(true, {}, false, handle(10), 7.f, 68.f, 75.f);
        EXPECT_TRUE(writer.reportDamage(report));
        EXPECT_EQ(inner.mReports.size(), 1u);
        EXPECT_FLOAT_EQ(inner.mReports[0].amount, 7.f);
        EXPECT_TRUE(hud.snapshot().mEnemyShown);
        EXPECT_TRUE(writer.reportDeath(handle(10)));
        EXPECT_EQ(inner.mDeaths.size(), 1u);
        EXPECT_TRUE(hud.snapshot().mEnemyDead);
    }

    TEST(CombatHudWriter, aReportTheCoreRefusedNeverShowsAnything)
    {
        CombatHud hud;
        hud.actorAdded(handle(10), "Hulda");
        FakeWriter inner;
        inner.mAcceptReports = false;
        HudWriter writer(inner, hud);
        EXPECT_FALSE(writer.reportDamage(hit(true, {}, false, handle(10), 7.f, 68.f, 75.f)));
        EXPECT_FALSE(hud.snapshot().mEnemyShown);
        EXPECT_FALSE(writer.reportDeath(handle(10)));
        EXPECT_FALSE(hud.snapshot().mEnemyDead);
    }

    // ---- the script package ----

    // Keeps the declaration and lets the test call a handler by name.
    class FakeBindings final : public BindingsService
    {
    public:
        DeclareResult declareNamespace(const NamespaceDescriptor& descriptor) override
        {
            mName = std::string(descriptor.name);
            mVersion = descriptor.version;
            mMenuSafe = descriptor.menuSafe;
            mFunctions.assign(descriptor.functions.begin(), descriptor.functions.end());
            return { true, {} };
        }
        const FunctionDescriptor* find(std::string_view name) const
        {
            for (const FunctionDescriptor& f : mFunctions)
                if (f.name == name)
                    return &f;
            return nullptr;
        }
        std::string mName;
        Version mVersion{ 0, 0 };
        bool mMenuSafe = true;
        std::vector<FunctionDescriptor> mFunctions;
    };

    struct CombatHudBindingsTest : CombatHudTest
    {
        CombatHudBindings bindings{ hud };
        FakeBindings service;
        void declare()
        {
            const DeclareResult result = bindings.declareTo(service);
            ASSERT_TRUE(result.ok) << result.message;
        }
    };

    TEST_F(CombatHudBindingsTest, declaresOneReadOnlyVersionOneNamespaceThatTheMenuDoesNotSee)
    {
        declare();
        EXPECT_EQ(service.mName, "skyrimse.combat");
        EXPECT_EQ(static_cast<int>(service.mVersion.major), 1);
        EXPECT_EQ(static_cast<int>(service.mVersion.minor), 0);
        EXPECT_FALSE(service.mMenuSafe);
        EXPECT_EQ(service.mFunctions.size(), 3u);
        for (const FunctionDescriptor& f : service.mFunctions)
        {
            EXPECT_EQ(f.access, BindingAccess::Read);
            EXPECT_TRUE(f.hasResult);
            EXPECT_TRUE(f.params.empty());
            EXPECT_NE(f.handler, nullptr);
        }
    }

    TEST_F(CombatHudBindingsTest, activeSaysWhetherCombatRuns)
    {
        declare();
        const FunctionDescriptor* f = service.find("active");
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->result, BindingType::Bool);
        BindingResult before = f->handler->call({});
        EXPECT_EQ(before.status, BindingStatus::Ok);
        EXPECT_FALSE(before.value.boolean);
        hud.update(0.016f, writer);
        EXPECT_TRUE(f->handler->call({}).value.boolean);
    }

    TEST_F(CombatHudBindingsTest, hudReturnsTheTenNumbersAsAFlatArrayOfDoubles)
    {
        declare();
        const FunctionDescriptor* f = service.find("hud");
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->result, BindingType::Double);
        EXPECT_EQ(f->resultShape, BindingShape::Array);
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.5f, 67.5f, 75.f));
        const BindingResult result = f->handler->call({});
        ASSERT_EQ(result.status, BindingStatus::Ok);
        ASSERT_EQ(result.value.count, kHudValueCount);
        ASSERT_NE(result.value.elements, nullptr);
        EXPECT_EQ(result.value.type, BindingType::Double);
        const auto expected = values();
        for (std::size_t i = 0; i < kHudValueCount; ++i)
        {
            EXPECT_EQ(result.value.elements[i].type, BindingType::Double);
            EXPECT_TRUE(result.value.elements[i].present);
            EXPECT_DOUBLE_EQ(result.value.elements[i].number, expected[i]) << i;
        }
        EXPECT_DOUBLE_EQ(result.value.elements[7].number, 67.5);
    }

    TEST_F(CombatHudBindingsTest, enemyNameIsEmptyUntilAnEnemyIsShown)
    {
        declare();
        const FunctionDescriptor* f = service.find("enemy_name");
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->result, BindingType::String);
        EXPECT_EQ(f->handler->call({}).value.text, "");
        hud.update(0.016f, writer);
        hud.noteDamage(hit(true, {}, false, handle(10), 7.5f, 67.5f, 75.f));
        const BindingResult named = f->handler->call({});
        EXPECT_EQ(named.status, BindingStatus::Ok);
        EXPECT_EQ(named.value.text, "Hulda");
        hud.update(kHudEnemyBarSeconds + 1.f, writer);
        EXPECT_EQ(f->handler->call({}).value.text, "");
    }

    // ---- the registered source ----

    TEST(CombatHudSource, theSourceKeepsTheHudAndFeedsItFromTheCombatUpdate)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        EXPECT_FALSE(source.hud().active());
        ActorInfo info;
        info.handle = handle(20);
        info.base = 0x1234;
        info.editorId = "BanneredMareHulda";
        source.onActorAdded(info);
        FakeWriter inner;
        CombatRules* rules = &source;
        rules->combatUpdate(0.016f, inner);
        EXPECT_TRUE(source.hud().active());
        EXPECT_DOUBLE_EQ(static_cast<double>(source.hud().snapshot().mPlayerHealth), 80.0);
        // A report the combat rules make during the update passes through the HUD's writer: here the rules make none,
        // so the report is made by hand through the same decorator the source builds.
        HudWriter writer(inner, source.hud());
        EXPECT_TRUE(writer.reportDamage(hit(true, {}, false, handle(20), 1.f, 5.f, 10.f)));
        EXPECT_EQ(source.hud().enemyName(), "BanneredMareHulda") << "no FULL name: the editor id is the fallback";
        source.onActorRemoved(handle(20));
    }
}

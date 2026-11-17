// Task 0244: the Skyrim-style combat HUD's numbers (game_skyrimse/hud_style.hpp): the boss rule, which actors get a
// bar, the secondary bars, the top bar and the script package. Written from the header and the task spec first; no game
// data.
#include <game_skyrimse/hud.hpp>
#include <game_skyrimse/hud_style.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::hudstyle;
    using namespace openeden::sdk;
    using namespace openeden::sdk::game;

    ActorHandle handle(std::uint32_t reference)
    {
        return ActorHandle{ reference, 1 };
    }

    ActorSample sample(std::uint32_t reference, double x, double y, bool engaged = true)
    {
        ActorSample s;
        s.mHandle = handle(reference);
        s.mEngaged = engaged;
        s.mHealth = 50.f;
        s.mHealthMax = 100.f;
        s.mX = x;
        s.mY = y;
        return s;
    }

    // ---- the boss rule ----

    TEST(HudStyleBossRule, anEditorIdWithBossOrADragonPrefixIsABoss)
    {
        EXPECT_TRUE(hudBossRule("DraugrBoss"));
        EXPECT_TRUE(hudBossRule("LCharBanditBossNordM2HOnly"));
        EXPECT_TRUE(hudBossRule("bossfight"));
        EXPECT_TRUE(hudBossRule("Dragon_Boss"));
        EXPECT_TRUE(hudBossRule("DragonPriest01"));
        EXPECT_TRUE(hudBossRule("dragonAncient"));
    }

    TEST(HudStyleBossRule, anOrdinaryActorIsNot)
    {
        EXPECT_FALSE(hudBossRule(""));
        EXPECT_FALSE(hudBossRule("Hulda"));
        EXPECT_FALSE(hudBossRule("EncWolf"));
        EXPECT_FALSE(hudBossRule("bos"));
        EXPECT_FALSE(hudBossRule("MyDragon")); // only a prefix counts for dragons
    }

    // ---- selection ----

    TEST(HudStyleSelect, keepsTheEngagedLivingActorsInRangeNearestFirst)
    {
        std::vector<ActorSample> in
            = { sample(1, 300, 0), sample(2, 100, 0), sample(3, 200, 0, false), sample(4, 9000, 0), sample(5, 50, 0) };
        in[4].mDead = true;
        std::vector<ActorSample> out;
        selectBars(0, 0, 0, in, SelectOptions{}, out);
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].mHandle.reference, 2u);
        EXPECT_EQ(out[1].mHandle.reference, 1u);
    }

    TEST(HudStyleSelect, theCapKeepsTheNearest)
    {
        std::vector<ActorSample> in;
        for (std::uint32_t i = 1; i <= 20; ++i)
            in.push_back(sample(i, 100.0 * i, 0));
        SelectOptions options;
        options.mMaxBars = 3;
        options.mRange = 100000.f;
        std::vector<ActorSample> out;
        selectBars(0, 0, 0, in, options, out);
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[2].mHandle.reference, 3u);
    }

    TEST(HudStyleSelect, equalDistancesGoByReference)
    {
        std::vector<ActorSample> in = { sample(9, 100, 0), sample(4, -100, 0), sample(6, 0, 100) };
        std::vector<ActorSample> out;
        selectBars(0, 0, 0, in, SelectOptions{}, out);
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0].mHandle.reference, 4u);
        EXPECT_EQ(out[1].mHandle.reference, 6u);
        EXPECT_EQ(out[2].mHandle.reference, 9u);
    }

    TEST(HudStyleSelect, aPositionThatIsNotFiniteNeverPasses)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<ActorSample> in
            = { sample(1, nan, 0), sample(2, 0, std::numeric_limits<double>::infinity()), sample(3, 10, 0) };
        std::vector<ActorSample> out;
        selectBars(0, 0, 0, in, SelectOptions{}, out);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].mHandle.reference, 3u);
    }

    TEST(HudStyleSelect, theOutputIsClearedFirstAndNoCapGivesNothing)
    {
        std::vector<ActorSample> in = { sample(1, 10, 0) };
        std::vector<ActorSample> out = { sample(7, 0, 0) };
        SelectOptions none;
        none.mMaxBars = 0;
        selectBars(0, 0, 0, in, none, out);
        EXPECT_TRUE(out.empty());
        selectBars(0, 0, 0, in, SelectOptions{}, out);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].mHandle.reference, 1u);
    }

    TEST(HudStyleBoss, theBiggestEngagedLivingBossWins)
    {
        std::vector<ActorSample> in = { sample(1, 0, 0), sample(2, 0, 0), sample(3, 0, 0), sample(4, 0, 0) };
        in[0].mBoss = true;
        in[0].mHealthMax = 300.f;
        in[1].mBoss = true;
        in[1].mHealthMax = 900.f;
        in[2].mBoss = true;
        in[2].mHealthMax = 5000.f;
        in[2].mDead = true;
        in[3].mHealthMax = 7000.f; // not a boss
        EXPECT_EQ(chooseBoss(in), 1u);
    }

    TEST(HudStyleBoss, noBossGivesTheSizeOfTheList)
    {
        std::vector<ActorSample> in = { sample(1, 0, 0) };
        EXPECT_EQ(chooseBoss(in), in.size());
        EXPECT_EQ(chooseBoss({}), 0u);
    }

    // ---- the HUD over fakes ----

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport&) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming&) override { return true; }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out.x = mPlayerX;
                out.y = 0;
                out.z = 0;
                out.health = 80.f;
                out.healthMax = 100.f;
                return true;
            }
            auto it = mPositions.find(subject.actor.reference);
            if (it == mPositions.end())
                return false;
            out.x = it->second[0];
            out.y = it->second[1];
            out.z = it->second[2];
            return true;
        }
        bool launchProjectile(CombatSubject, const ProjectileRequest&) override { return true; }
        bool setClipSpeed(CombatSubject, float) override { return true; }
        bool clipEventTime(std::string_view, bool, ClipEventKind, float& out) override
        {
            out = 1.f;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }

        double mPlayerX = 0.0;
        std::map<std::uint32_t, std::array<double, 3>> mPositions;
    };

    struct HudStyleTest : testing::Test
    {
        CombatHud hud;
        FakeWriter writer;
        std::map<std::uint32_t, SkyrimCombat::FighterView> views;
        SkyrimCombat::FighterView playerView;
        std::vector<std::uint32_t> allies;
        StyleHud::Source source;
        std::unique_ptr<StyleHud> style;

        void SetUp() override
        {
            playerView.mLevel = 12;
            source.mFighters = [this](const SkyrimCombat::FighterVisitor& visit) {
                for (const auto& [reference, view] : views)
                    visit(CombatSubject::of(handle(reference)), view);
            };
            source.mLookup = [this](CombatSubject subject, SkyrimCombat::FighterView& out) {
                if (subject.player)
                {
                    out = playerView;
                    return true;
                }
                auto it = views.find(subject.actor.reference);
                if (it == views.end())
                    return false;
                out = it->second;
                return true;
            };
            source.mIsAlly = [this](WorldId reference) {
                for (const std::uint32_t ally : allies)
                    if (ally == reference)
                        return true;
                return false;
            };
            style = std::make_unique<StyleHud>(hud, source);
        }

        void addActor(std::uint32_t reference, const char* name, const char* editorId, double x, double y,
            bool engaged = true, float health = 60.f)
        {
            hud.actorAdded(handle(reference), name, editorId);
            SkyrimCombat::FighterView v;
            v.mHealth = health;
            v.mHealthMax = 100.f;
            v.mMagicka = 40.f;
            v.mMagickaMax = 40.f;
            v.mStamina = 50.f;
            v.mStaminaMax = 50.f;
            v.mLevel = 7;
            v.mEngaged = engaged;
            views[reference] = v;
            writer.mPositions[reference] = { x, y, 0.0 };
        }

        void update(float seconds = 0.016f) { style->update(seconds, writer); }
    };

    TEST_F(HudStyleTest, isInactiveUntilTheFirstUpdate)
    {
        EXPECT_FALSE(style->active());
        update();
        EXPECT_TRUE(style->active());
    }

    TEST_F(HudStyleTest, anEngagedActorInRangeGetsABarAboveItsHead)
    {
        addActor(10, "Hulda", "Hulda", 200, 0);
        update();
        ASSERT_EQ(style->bars().size(), 1u);
        const ActorSample& bar = style->bars()[0];
        EXPECT_EQ(bar.mHandle.reference, 10u);
        EXPECT_DOUBLE_EQ(bar.mX, 200.0);
        EXPECT_DOUBLE_EQ(bar.mZ, static_cast<double>(kHeadOffset));
        EXPECT_EQ(bar.mLevel, 7);
        EXPECT_EQ(style->barName(0), "Hulda");
        EXPECT_EQ(style->barName(1), "");
    }

    TEST_F(HudStyleTest, anActorThatIsNotEngagedOrIsFarGetsNone)
    {
        addActor(10, "Calm", "Calm", 100, 0, false);
        addActor(11, "Far", "Far", 99999, 0);
        update();
        EXPECT_TRUE(style->bars().empty());
    }

    TEST_F(HudStyleTest, aBarLingersAfterTheFightAndThenGoes)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        update();
        ASSERT_EQ(style->bars().size(), 1u);
        views[10].mEngaged = false;
        update(kBarLingerSeconds - 1.f);
        EXPECT_EQ(style->bars().size(), 1u);
        update(2.f);
        EXPECT_TRUE(style->bars().empty());
    }

    TEST_F(HudStyleTest, aDeadActorLosesItsBar)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        update();
        views[10].mDead = true;
        update();
        EXPECT_TRUE(style->bars().empty());
    }

    TEST_F(HudStyleTest, onlyTheNearestEightGetBars)
    {
        for (std::uint32_t i = 1; i <= 12; ++i)
            addActor(i, "N", "N", 100.0 * i, 0);
        update();
        ASSERT_EQ(style->bars().size(), kMaxActorBars);
        EXPECT_EQ(style->bars().back().mHandle.reference, static_cast<std::uint32_t>(kMaxActorBars));
    }

    TEST_F(HudStyleTest, anAllyIsMarked)
    {
        addActor(10, "Mikael", "Mikael", 100, 0);
        addActor(11, "Foe", "Foe", 200, 0);
        allies.push_back(10);
        update();
        ASSERT_EQ(style->bars().size(), 2u);
        EXPECT_TRUE(style->bars()[0].mAlly);
        EXPECT_FALSE(style->bars()[1].mAlly);
    }

    TEST_F(HudStyleTest, magickaAndStaminaShowOnlyAfterTheyChange)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        update();
        EXPECT_FALSE(style->bars()[0].mShowMagicka);
        EXPECT_FALSE(style->bars()[0].mShowStamina);
        views[10].mStamina = 30.f;
        update();
        EXPECT_TRUE(style->bars()[0].mShowStamina);
        update(kSecondarySeconds + 0.5f);
        EXPECT_FALSE(style->bars()[0].mShowStamina);
        views[10].mMagicka = 10.f;
        update();
        EXPECT_TRUE(style->bars()[0].mShowMagicka);
    }

    TEST_F(HudStyleTest, aChangeBelowTheNoiseFloorShowsNothing)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        update();
        views[10].mStamina += kSecondaryChange / 2.f;
        update();
        EXPECT_FALSE(style->bars()[0].mShowStamina);
    }

    TEST_F(HudStyleTest, aBossFarthestOrNotTakesTheTopBar)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        addActor(11, "Draugr Overlord", "DraugrBoss", 900, 0);
        views[11].mHealthMax = 500.f;
        views[11].mHealth = 400.f;
        update();
        const Boss& boss = style->boss();
        EXPECT_TRUE(boss.mShown);
        EXPECT_TRUE(boss.mIsBoss);
        EXPECT_FLOAT_EQ(boss.mHealthMax, 500.f);
        EXPECT_EQ(style->bossName(), "Draugr Overlord");
        EXPECT_EQ(boss.mLevel, 7);
    }

    TEST_F(HudStyleTest, withoutABossTheEnemyFoughtLastTakesTheTopBar)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        update();
        EXPECT_FALSE(style->boss().mShown);
        DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = handle(10);
        report.healthAfter = 40.f;
        report.healthMax = 100.f;
        hud.noteDamage(report);
        update();
        const Boss& boss = style->boss();
        EXPECT_TRUE(boss.mShown);
        EXPECT_FALSE(boss.mIsBoss);
        EXPECT_FLOAT_EQ(boss.mHealth, 40.f);
        EXPECT_EQ(style->bossName(), "Hulda");
        EXPECT_EQ(boss.mLevel, 7);
    }

    TEST_F(HudStyleTest, theTopBarGoesWhenTheEnemyBarDoes)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = handle(10);
        report.healthAfter = 40.f;
        report.healthMax = 100.f;
        hud.noteDamage(report);
        hud.update(0.016f, writer);
        update();
        EXPECT_TRUE(style->boss().mShown);
        hud.update(kHudEnemyBarSeconds + 1.f, writer);
        update();
        EXPECT_FALSE(style->boss().mShown);
        EXPECT_EQ(style->bossName(), "");
    }

    TEST_F(HudStyleTest, thePlayerNumbersCarryTheLevel)
    {
        hud.update(0.016f, writer);
        update();
        std::array<double, kPlayerValueCount> v;
        style->playerValues(v);
        EXPECT_DOUBLE_EQ(v[0], 80.0);
        EXPECT_DOUBLE_EQ(v[1], 100.0);
        EXPECT_DOUBLE_EQ(v[6], 12.0);
    }

    TEST_F(HudStyleTest, theBarNumbersAreThirteenAPiece)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        allies.push_back(10);
        update();
        std::vector<double> numbers;
        style->barValues(numbers);
        ASSERT_EQ(numbers.size(), kBarStride);
        EXPECT_DOUBLE_EQ(numbers[0], 10.0);
        EXPECT_DOUBLE_EQ(numbers[1], 100.0);
        EXPECT_DOUBLE_EQ(numbers[4], 60.0);
        EXPECT_DOUBLE_EQ(numbers[5], 100.0);
        EXPECT_DOUBLE_EQ(numbers[10], 7.0);
        EXPECT_DOUBLE_EQ(numbers[11], 1.0);
    }

    TEST_F(HudStyleTest, anUpdateAfterTheActorsLeaveForgetsThem)
    {
        addActor(10, "Hulda", "Hulda", 100, 0);
        update();
        views.clear();
        update();
        EXPECT_TRUE(style->bars().empty());
    }

    // ---- the script package ----

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

    struct HudStyleBindingsTest : HudStyleTest
    {
        std::unique_ptr<StyleBindings> bindings;
        FakeBindings service;
        void declare()
        {
            bindings = std::make_unique<StyleBindings>(*style);
            const DeclareResult result = bindings->declareTo(service);
            ASSERT_TRUE(result.ok) << result.message;
        }
    };

    TEST_F(HudStyleBindingsTest, declaresASixFunctionReadOnlyNamespace)
    {
        declare();
        EXPECT_EQ(service.mName, "skyrimse.hudstyle");
        EXPECT_EQ(static_cast<int>(service.mVersion.major), 1);
        EXPECT_FALSE(service.mMenuSafe);
        EXPECT_EQ(service.mFunctions.size(), 6u);
        for (const FunctionDescriptor& f : service.mFunctions)
        {
            EXPECT_EQ(f.access, BindingAccess::Read);
            EXPECT_NE(f.handler, nullptr);
        }
    }

    TEST_F(HudStyleBindingsTest, barsAndNamesFollowTheHud)
    {
        declare();
        addActor(10, "Hulda", "Hulda", 100, 0);
        addActor(11, "Sinmir", "Sinmir", 300, 0);
        update();
        const FunctionDescriptor* bars = service.find("bars");
        const FunctionDescriptor* names = service.find("bar_names");
        ASSERT_NE(bars, nullptr);
        ASSERT_NE(names, nullptr);
        EXPECT_EQ(bars->resultShape, BindingShape::Array);
        EXPECT_EQ(names->result, BindingType::String);
        const BindingResult b = bars->handler->call({});
        ASSERT_EQ(b.status, BindingStatus::Ok);
        EXPECT_EQ(b.value.count, 2 * kBarStride);
        EXPECT_DOUBLE_EQ(b.value.elements[0].number, 10.0);
        EXPECT_DOUBLE_EQ(b.value.elements[kBarStride].number, 11.0);
        const BindingResult n = names->handler->call({});
        ASSERT_EQ(n.value.count, 2u);
        EXPECT_EQ(n.value.elements[0].text, "Hulda");
        EXPECT_EQ(n.value.elements[1].text, "Sinmir");
    }

    TEST_F(HudStyleBindingsTest, noBarsIsAnEmptyArray)
    {
        declare();
        update();
        EXPECT_EQ(service.find("bars")->handler->call({}).value.count, 0u);
        EXPECT_EQ(service.find("bar_names")->handler->call({}).value.count, 0u);
    }

    TEST_F(HudStyleBindingsTest, playerAndBossAreFixedSizeArrays)
    {
        declare();
        update();
        const BindingResult p = service.find("player")->handler->call({});
        EXPECT_EQ(p.value.count, kPlayerValueCount);
        const BindingResult b = service.find("boss")->handler->call({});
        EXPECT_EQ(b.value.count, kBossValueCount);
        EXPECT_DOUBLE_EQ(b.value.elements[0].number, 0.0);
        EXPECT_TRUE(service.find("active")->handler->call({}).value.boolean);
        EXPECT_EQ(service.find("boss_name")->handler->call({}).value.text, "");
    }
}

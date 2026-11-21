// Task 0275 (docs/tasks/0275-skyrim-thieves-guild-2.md): the Thieves Guild's real actions, pure parts, no game data:
//  * SkyrimPlantBonus:  a perk's pickpocket bonus (entry point 56) on a chance;
//  * SkyrimPlant:       reverse pickpocketing through the pickup service (the player slips an item into an NPC's list).
// Written first and run red against stubs (counts in the task's stage log).
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/perks.hpp>
#include <game_skyrimse/pickup.hpp>
#include <game_skyrimse/pickup_service.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::pickup;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kRing = 0x00076f12;
    constexpr FormId kGold = 0x0000000f;
    constexpr std::uint64_t kBrandShei = 0x00019ddc;

    // ---- the bonus
    // ---------------------------------------------------------------------------------------------------

    TEST(SkyrimPlantBonus, aPositiveBonusAddsPercentagePointsAndStaysUnderTheMaximum)
    {
        const PickpocketSettings settings;
        EXPECT_FLOAT_EQ(chanceWithBonus(settings, 20.f, 30.f), 50.f);
        EXPECT_FLOAT_EQ(chanceWithBonus(settings, 80.f, 30.f), settings.mMax);
    }

    TEST(SkyrimPlantBonus, aNegativeBonusLowersTheChanceButNotBelowTheMinimum)
    {
        const PickpocketSettings settings;
        EXPECT_FLOAT_EQ(chanceWithBonus(settings, 40.f, -10.f), 30.f);
        EXPECT_FLOAT_EQ(chanceWithBonus(settings, 8.f, -50.f), settings.mMin);
    }

    TEST(SkyrimPlantBonus, aBonusThatIsNotANumberCountsAsNothing)
    {
        const PickpocketSettings settings;
        EXPECT_FLOAT_EQ(chanceWithBonus(settings, 33.f, std::nanf("")), 33.f);
        EXPECT_FLOAT_EQ(chanceWithBonus(settings, 33.f, 0.f), 33.f);
    }

    // ---- planting
    // ----------------------------------------------------------------------------------------------------

    struct PlantFixture : ::testing::Test
    {
        std::map<FormId, std::pair<std::int32_t, float>> mItems{ { kRing, { 25, 0.f } }, { kGold, { 1, 0.f } } };
        std::map<std::uint64_t, std::map<FormId, std::int32_t>> mHeld{
            { inventory::kPlayerOwner, { { kRing, 1 }, { kGold, 50 } } }, { kBrandShei, {} }
        };
        float mSkill = 15.f;
        float mBonus = 0.f;
        bool mAware = false;
        float mTrained = 0.f;
        int mCaught = 0;
        std::vector<std::string> mLog;
        std::unique_ptr<PickupService> mService;

        Hooks hooks()
        {
            Hooks h;
            h.mItem = [this](FormId item, std::int32_t& value, float& weight) {
                const auto it = mItems.find(item);
                if (it == mItems.end())
                    return false;
                value = it->second.first;
                weight = it->second.second;
                return true;
            };
            h.mHeldBy = [this](std::uint64_t owner, FormId item) {
                const auto o = mHeld.find(owner);
                if (o == mHeld.end())
                    return 0;
                const auto it = o->second.find(item);
                return it == o->second.end() ? 0 : it->second;
            };
            h.mMoveFromPlayer = [this](std::uint64_t owner, FormId item, std::int32_t count) {
                mHeld[inventory::kPlayerOwner][item] -= count;
                mHeld[owner][item] += count;
                return true;
            };
            h.mSkill = [this]() { return mSkill; };
            h.mTrain = [this](float amount) { mTrained += amount; };
            h.mAware = [this](std::uint64_t) { return mAware; };
            h.mCaught = [this](std::uint64_t, FormId, std::int32_t) { ++mCaught; };
            h.mChanceBonus = [this](std::uint64_t) { return mBonus; };
            h.mLog = [this](const std::string& line) { mLog.push_back(line); };
            return h;
        }

        void make() { mService = std::make_unique<PickupService>(hooks()); }
        void SetUp() override { make(); }
    };

    TEST_F(PlantFixture, aSuccessMovesTheItemFromThePlayerToTheNpcAndSaysSo)
    {
        // a chance of 90 percent: look for an owner whose first roll passes (the roll is seeded, so this is repeatable)
        mBonus = 70.f;
        TryStatus status = TryStatus::Caught;
        std::uint64_t owner = 0;
        for (std::uint64_t o = kBrandShei; o < kBrandShei + 64 && status != TryStatus::Success; ++o)
        {
            mHeld[inventory::kPlayerOwner][kRing] = 1;
            mHeld[o] = {};
            make();
            status = mService->plant(o, kRing, 1);
            owner = o;
        }
        ASSERT_EQ(status, TryStatus::Success);
        EXPECT_EQ(mHeld[inventory::kPlayerOwner][kRing], 0);
        EXPECT_EQ(mHeld[owner][kRing], 1);
        EXPECT_EQ(mService->state(), "planted");
        EXPECT_EQ(mCaught, 0);
        EXPECT_GT(mTrained, 0.f);
    }

    TEST_F(PlantFixture, aFailureIsACaughtTryAndTheItemStaysWithThePlayer)
    {
        mSkill = 0.f;
        mBonus = -100.f; // the minimum chance, 5 percent
        TryStatus status = TryStatus::Success;
        std::uint64_t owner = 0;
        for (std::uint64_t o = kBrandShei; o < kBrandShei + 64 && status != TryStatus::Caught; ++o)
        {
            mHeld[o] = {};
            mHeld[inventory::kPlayerOwner][kRing] = 1;
            make();
            status = mService->plant(o, kRing, 1);
            owner = o;
        }
        ASSERT_EQ(status, TryStatus::Caught);
        EXPECT_EQ(mHeld[inventory::kPlayerOwner][kRing], 1);
        EXPECT_EQ(mHeld[owner][kRing], 0);
        EXPECT_EQ(mService->state(), "caught");
        EXPECT_EQ(mCaught, 1);
    }

    TEST_F(PlantFixture, anAwareVictimAlwaysCatchesTheHand)
    {
        mAware = true;
        mBonus = 200.f;
        mSkill = 1000.f;
        make();
        for (int i = 0; i < 20; ++i)
        {
            mHeld[inventory::kPlayerOwner][kRing] = 1;
            EXPECT_EQ(mService->plant(kBrandShei, kRing, 1), TryStatus::Caught);
        }
        EXPECT_EQ(mCaught, 20);
        EXPECT_EQ(mHeld[kBrandShei][kRing], 0);
    }

    TEST_F(PlantFixture, aCountAboveWhatThePlayerHoldsOrBelowOneIsRefusedWithoutARollOrTraining)
    {
        EXPECT_EQ(mService->plant(kBrandShei, kRing, 2), TryStatus::BadCount); // the player holds one
        EXPECT_EQ(mService->plant(kBrandShei, kRing, 0), TryStatus::BadCount);
        EXPECT_EQ(mService->plant(kBrandShei, 0x9999, 1), TryStatus::BadCount); // not held
        EXPECT_EQ(mService->book().attempts(), 0u);
        EXPECT_FLOAT_EQ(mTrained, 0.f);
    }

    TEST_F(PlantFixture, aMissingHookRefusesInsteadOfCrashing)
    {
        Hooks h = hooks();
        h.mMoveFromPlayer = nullptr;
        PickupService service(std::move(h));
        EXPECT_EQ(service.plant(kBrandShei, kRing, 1), TryStatus::Refused);
        EXPECT_EQ(service.book().attempts(), 0u);
    }

    TEST_F(PlantFixture, theOutcomesAreTheSameInEveryRunAndBothKindsOccur)
    {
        std::string a, b;
        for (int run = 0; run < 2; ++run)
        {
            mHeld[kBrandShei] = {};
            mBonus = 20.f;
            make();
            std::string& out = run == 0 ? a : b;
            for (int i = 0; i < 16; ++i)
            {
                mHeld[inventory::kPlayerOwner][kRing] = 1;
                out += mService->plant(kBrandShei, kRing, 1) == TryStatus::Success ? 'S' : 'c';
            }
        }
        EXPECT_EQ(a, b);
        EXPECT_NE(a.find('S'), std::string::npos);
        EXPECT_NE(a.find('c'), std::string::npos);
    }

    TEST_F(PlantFixture, plantingAndStealingTheSameItemAreDifferentDraws)
    {
        int differ = 0;
        for (std::uint32_t attempt = 0; attempt < 64; ++attempt)
            if (pickpocketRoll(kBrandShei, kRing, attempt, 50.f)
                != pickpocketRoll(kBrandShei ^ kPlantSalt, kRing, attempt, 50.f))
                ++differ;
        EXPECT_GT(differ, 0);
    }

    TEST_F(PlantFixture, theBonusAlsoMovesTheChanceReadAndATake)
    {
        mBonus = 0.f;
        make();
        const float plain = mService->chance(kBrandShei, kGold, 1);
        mBonus = 30.f;
        EXPECT_FLOAT_EQ(mService->chance(kBrandShei, kGold, 1), std::min(plain + 30.f, mService->settings().mMax));
    }

    TEST_F(PlantFixture, thePlantBindingIsDeclaredAsAMutateFunctionAndTheVersionStays)
    {
        struct Stub final : openeden::sdk::BindingsService
        {
            openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
            {
                version = d.version;
                for (const auto& f : d.functions)
                    functions[std::string(f.name)] = f;
                return { true, {} };
            }
            openeden::sdk::Version version{ 0, 0 };
            std::map<std::string, openeden::sdk::FunctionDescriptor> functions;
        } stub;
        ASSERT_TRUE(mService->declare(stub).ok);
        ASSERT_EQ(stub.functions.count("plant"), 1u);
        EXPECT_EQ(stub.functions.at("plant").access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(stub.version.minor, 0); // additive: the version the pinned test expects stays
        BindingValue o, i, c;
        o.type = BindingType::String;
        o.text = "00019ddc";
        i.type = BindingType::String;
        i.text = "00076f12";
        c.type = BindingType::Int32;
        c.integer = 1;
        mAware = true; // the try is made (and caught): the attempt count moves
        stub.functions.at("plant").handler->call(std::vector<BindingValue>{ o, i, c });
        EXPECT_EQ(mService->book().attempts(), 1u);
    }

    // ---- the helper perk's conditions
    // --------------------------------------------------------------------------------

    TEST(SkyrimPerkWorldTg, aQuestStageSourceAnswersGetStageAndNoSourceAnswersZero)
    {
        PerkConditionWorld world;
        EXPECT_FLOAT_EQ(world.questStage(0x21556), 0.f);
        world.setQuestStageSource([](std::uint32_t quest) { return quest == 0x21556 ? 20.f : 0.f; });
        EXPECT_FLOAT_EQ(world.questStage(0x21556), 20.f);
        EXPECT_FLOAT_EQ(world.questStage(0x1), 0.f);
    }

    TEST(SkyrimPerkWorldTg, aConditionRunOnThePlayersReferenceFindsThePlayersRoleOnlyWhenOneIsSet)
    {
        PerkConditionWorld world;
        PerkParticipant player;
        player.mKind = PerkParticipant::Kind::Actor;
        PerkQuery query;
        query.set(PerkRole::Owner, &player);
        world.setQuery(&query);
        world.setCurrentRole(PerkRole::Owner);
        EXPECT_EQ(world.resolveRunOn(2, PerkConditionWorld::kPlayerReference), 0u); // no player role: fails closed
        world.setPlayerRole(PerkRole::Owner);
        EXPECT_EQ(world.resolveRunOn(2, PerkConditionWorld::kPlayerReference),
            PerkConditionWorld::subjectOf(PerkRole::Owner));
        EXPECT_EQ(world.resolveRunOn(2, 0x99), 0u); // another reference
        EXPECT_EQ(world.resolveRunOn(0, 0), PerkConditionWorld::subjectOf(PerkRole::Owner)); // "subject" as before
    }

    struct OnePerkLibrary final : PerkLibrary
    {
        PerkRecord mPerk;
        const PerkRecord* perk(FormId id) const override { return id == 0x10F21E ? &mPerk : nullptr; }
    };

    survey::Ctda cond(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1, std::uint32_t runOn = 0,
        std::uint32_t reference = 0)
    {
        survey::Ctda c;
        c.mCompare = compare;
        c.mComparison = value;
        c.mFunction = fn;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mReference = reference;
        return c;
    }

    // The data's own shape of TG00PickpocketHelper (the gated test below prints it): entry point 56, Add 30, tab 0 (the
    // player) GetStage(TG00) >= 10 and < 30 and HasPerk(0xBE124) == 0 run on the reference 0x14, tab 1 (the victim)
    // GetIsID(Brand-Shei) == 1.
    OnePerkLibrary helperLibrary()
    {
        OnePerkLibrary lib;
        PerkEffect e;
        e.mKind = PerkEffectKind::EntryPoint;
        e.mEntryPoint = PerkEntry::ModPickpocketChance;
        e.mFunction = PerkFunction::Add;
        e.mHasParam = true;
        e.mValue = 30.f;
        e.mTabCount = 2;
        PerkTab t0;
        t0.mIndex = 0;
        t0.mConditions
            = { cond(58, 3, 10.f, 0x21556), cond(58, 4, 30.f, 0x21556), cond(448, 0, 0.f, 0xBE124, 2, 0x14) };
        PerkTab t1;
        t1.mIndex = 1;
        t1.mConditions = { cond(72, 0, 1.f, 78671) };
        e.mTabs = { t0, t1 };
        lib.mPerk.mEffects = { e };
        return lib;
    }

    float helperBonus(float stage, FormId victimBase, bool withSource = true, bool withPlayerRole = true)
    {
        const OnePerkLibrary lib = helperLibrary();
        PerkEngine engine(lib);
        if (withSource)
            engine.setQuestStageSource([stage](std::uint32_t q) { return q == 0x21556 ? stage : 0.f; });
        if (withPlayerRole)
            engine.setPlayerRole(PerkRole::Owner);
        const std::vector<FormId> perks{ 0x10F21E };
        PerkParticipant owner;
        owner.mKind = PerkParticipant::Kind::Actor;
        owner.mPerks = &perks;
        PerkParticipant target;
        target.mKind = PerkParticipant::Kind::Actor;
        target.mForm = victimBase;
        PerkQuery query;
        query.set(PerkRole::Owner, &owner);
        query.set(PerkRole::Target, &target);
        return engine.apply(PerkEntry::ModPickpocketChance, perks, query, 0.f).mValue;
    }

    TEST(SkyrimPerkWorldTg, theHelperPerkAddsThirtyBetweenStagesTenAndThirtyForBrandShei)
    {
        EXPECT_FLOAT_EQ(helperBonus(20.f, 78671), 30.f);
        EXPECT_FLOAT_EQ(helperBonus(10.f, 78671), 30.f);
    }

    TEST(SkyrimPerkWorldTg, theHelperPerkDoesNothingBeforeTheJobOrAfterItOrForAnotherVictim)
    {
        EXPECT_FLOAT_EQ(helperBonus(8.f, 78671), 0.f);
        EXPECT_FLOAT_EQ(helperBonus(30.f, 78671), 0.f);
        EXPECT_FLOAT_EQ(helperBonus(20.f, 12345), 0.f);
    }

    TEST(SkyrimPerkWorldTg, withoutAStageSourceTheHelperPerkStaysSilentAsItDidBefore)
    {
        EXPECT_FLOAT_EQ(helperBonus(20.f, 78671, false, true), 0.f);
        // (without a player role the HasPerk == 0 test is answered for nobody, which compares equal to 0: the
        // stage decides, as the evaluator has always treated an unresolved subject)
        EXPECT_FLOAT_EQ(helperBonus(8.f, 78671, true, false), 0.f);
    }

    // ---- the data (gated)
    // --------------------------------------------------------------------------------------------

    TEST(SkyrimThievesGuild2Real, theHelperPerkOfTheQuestIsAnEntryPoint56Effect)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
        {
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
            ASSERT_TRUE(order.add(n, false, err)) << err;
        }
        CombatIndex combat;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_TRUE(file != nullptr) << n;
            CombatRecords records;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records);
            combat.add(records);
        }
        const FormId id = combat.perkByEditorId("TG00PickpocketHelper");
        ASSERT_NE(id, kNullFormId);
        const PerkRecord* perk = combat.perk(id);
        ASSERT_TRUE(perk != nullptr);
        ASSERT_EQ(perk->mEffects.size(), 1u);
        const PerkEffect& e = perk->mEffects[0];
        std::cout << "SKYRIMSE-THIEVES helper perk entry " << static_cast<int>(e.mEntryPoint) << " function "
                  << static_cast<int>(e.mFunction) << " value " << e.mValue << " tabs " << e.mTabs.size() << std::endl;
        for (const PerkTab& tab : e.mTabs)
        {
            bool known = false;
            std::cout << "SKYRIMSE-THIEVES helper tab " << static_cast<int>(tab.mIndex) << " role "
                      << static_cast<int>(entryPointTabRole(e.mEntryPoint, tab.mIndex, known)) << " known " << known
                      << " conditions " << tab.mConditions.size();
            for (const survey::Ctda& c : tab.mConditions)
                std::cout << " [fn " << c.mFunction << " run-on " << c.mRunOn << " cmp " << static_cast<int>(c.mCompare)
                          << " value " << c.mComparison << " p1 " << c.mParam1 << "]";
            std::cout << std::endl;
        }
        EXPECT_EQ(e.mEntryPoint, PerkEntry::ModPickpocketChance);
        EXPECT_FLOAT_EQ(e.mValue, 30.f);
    }
}

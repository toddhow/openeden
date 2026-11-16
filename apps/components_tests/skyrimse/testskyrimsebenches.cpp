// Task 0229 (the rest of the crafting benches): the smelter, the tanning rack and the cooking pot over the crafting
// session of task 0199. A generated plugin, no game data. Every number is worked by hand in a comment.
//
//  - Smelter: COBJ with the keyword CraftingSmelter, no conditions: 1 ore -> 1 ingot (value 5). Trains smithing by the
//    value of what it made (the forge's own rule): +5.
//  - Tanning rack: keyword CraftingTanningRack; the game's recipes carry one condition, GetItemCount(pelt) >= 1 (fn 47,
//    compare type 0x60 = operator 3, ">="), which the material check already says. 1 pelt -> 1 leather (value 3): +3
//    smithing. Without the pelt the recipe is not offered (the condition fails).
//  - Cooking pot: keyword CraftingCookpot: 1 meat + 1 salt -> cooked meat (an ALCH food, heals 5 at once). Trains no
//    skill (RECALLED-VERIFY: Skyrim gives cooking no experience).
// The forge's own recipe still trains smithing (the control).
#include <game_skyrimse/crafting_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::crafting;
    using inventory::kPlayerOwner;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kEffHeal = kSelf | 0x500;
    constexpr FormId kEffDamage = kSelf | 0x501; // hostile: a poison's effect
    constexpr FormId kOre = kSelf | 0x600, kIngot = kSelf | 0x601, kPelt = kSelf | 0x602, kLeather = kSelf | 0x603,
                     kSalt = kSelf | 0x604, kMeat = kSelf | 0x605, kCooked = kSelf | 0x606, kSword = kSelf | 0x607,
                     kPoison = kSelf | 0x608, kPoison2 = kSelf | 0x609, kBow = kSelf | 0x60A;
    constexpr FormId kRecSmelt = kSelf | 0x700, kRecTan = kSelf | 0x701, kRecCook = kSelf | 0x702,
                     kRecForge = kSelf | 0x703;
    constexpr FormId kKwSmelter = kSelf | 0x800, kKwTan = kSelf | 0x801, kKwCook = kSelf | 0x802,
                     kKwForge = kSelf | 0x803;
    constexpr FormId kFurnSmelter = kSelf | 0x900, kFurnTan = kSelf | 0x901, kFurnCook = kSelf | 0x902,
                     kFurnForge = kSelf | 0x903;

    Bytes edidSub(const char* name)
    {
        return sub4("EDID", Bytes().zstring(name));
    }

    Bytes group(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), 0, records);
    }

    // CTDA: type 0x60 (operator 3, ">="), compare 1.0, function 47 (GetItemCount), parameter 1 the item.
    Bytes itemCountCondition(FormId item)
    {
        return sub4("CTDA", Bytes().u8(0x60).u8(0).u16(0).f32(1.f).u16(47).u16(0).u32(item).u32(0).u32(0).u32(0).u32(0));
    }

    Bytes recipe(FormId id, FormId result, FormId bench, std::vector<std::pair<FormId, int>> items, bool counted)
    {
        Bytes d;
        d.append(sub4("COCT", Bytes().u32(static_cast<std::uint32_t>(items.size()))));
        for (const auto& [item, n] : items)
            d.append(sub4("CNTO", Bytes().u32(item).u32(static_cast<std::uint32_t>(n))));
        if (counted)
            d.append(itemCountCondition(items.front().first));
        d.append(sub4("CNAM", Bytes().u32(result)));
        d.append(sub4("BNAM", Bytes().u32(bench)));
        d.append(sub4("NAM1", Bytes().u16(1)));
        return record4("COBJ", 0, id, d);
    }

    Bytes furniture(FormId id, const char* name, FormId keyword)
    {
        Bytes d;
        d.append(edidSub(name));
        d.append(sub4("KSIZ", Bytes().u32(1)));
        d.append(sub4("KWDA", Bytes().u32(keyword)));
        return record4("FURN", 0, id, d);
    }

    Bytes food(FormId id, const char* name, float heal)
    {
        Bytes d;
        d.append(edidSub(name));
        d.append(sub4("DATA", Bytes().f32(0.5f)));
        d.append(sub4("ENIT", Bytes().u32(5).u32(2).u32(0).f32(0.f).u32(0)));
        d.append(sub4("EFID", Bytes().u32(kEffHeal)));
        d.append(sub4("EFIT", Bytes().f32(heal).u32(0).u32(0)));
        return record4("ALCH", 0, id, d);
    }

    // An ALCH whose one effect is hostile: a potion flag 0x20000 marks a poison in the game's data.
    Bytes poisonRecord(FormId id, const char* name, float magnitude, std::uint32_t duration)
    {
        Bytes d;
        d.append(edidSub(name));
        d.append(sub4("DATA", Bytes().f32(0.5f)));
        d.append(sub4("ENIT", Bytes().u32(30).u32(0x20000).u32(0).f32(0.f).u32(0)));
        d.append(sub4("EFID", Bytes().u32(kEffDamage)));
        d.append(sub4("EFIT", Bytes().f32(magnitude).u32(0).u32(duration)));
        return record4("ALCH", 0, id, d);
    }

    Bytes plugin()
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 40, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes mg;
        mg.append(record4("MGEF", 0, kEffHeal, edidSub("FoodRestoreHealth")));
        mg.append(record4("MGEF", 0, kEffDamage, edidSub("AlchDamageHealth")));
        all.append(group("MGEF", mg));
        Bytes misc;
        misc.append(record4("MISC", 0, kOre, sub4("DATA", Bytes().u32(7).f32(4.f))));
        misc.append(record4("MISC", 0, kIngot, sub4("DATA", Bytes().u32(5).f32(1.f))));
        misc.append(record4("MISC", 0, kPelt, sub4("DATA", Bytes().u32(2).f32(2.f))));
        misc.append(record4("MISC", 0, kLeather, sub4("DATA", Bytes().u32(3).f32(0.5f))));
        misc.append(record4("MISC", 0, kSalt, sub4("DATA", Bytes().u32(1).f32(0.1f))));
        all.append(group("MISC", misc));
        Bytes alch;
        alch.append(food(kMeat, "FoodBeef", 2.f));
        alch.append(food(kCooked, "FoodBeefCooked", 5.f));
        alch.append(poisonRecord(kPoison, "TestPoison", 5.f, 4));
        alch.append(poisonRecord(kPoison2, "TestPoisonTwo", 3.f, 6));
        all.append(group("ALCH", alch));
        Bytes weapons;
        weapons.append(record4("WEAP", 0, kSword, sub4("DATA", Bytes().u32(25).f32(9.f).u16(7))));
        weapons.append(record4("WEAP", 0, kBow, sub4("DATA", Bytes().u32(40).f32(5.f).u16(9))));
        all.append(group("WEAP", weapons));
        Bytes kw;
        kw.append(record4("KYWD", 0, kKwSmelter, edidSub("CraftingSmelter")));
        kw.append(record4("KYWD", 0, kKwTan, edidSub("CraftingTanningRack")));
        kw.append(record4("KYWD", 0, kKwCook, edidSub("CraftingCookpot")));
        kw.append(record4("KYWD", 0, kKwForge, edidSub("CraftingSmithingForge")));
        all.append(group("KYWD", kw));
        Bytes fu;
        fu.append(furniture(kFurnSmelter, "TestSmelter", kKwSmelter));
        fu.append(furniture(kFurnTan, "TestTanningRack", kKwTan));
        fu.append(furniture(kFurnCook, "TestCookingPot", kKwCook));
        fu.append(furniture(kFurnForge, "TestForge", kKwForge));
        all.append(group("FURN", fu));
        Bytes co;
        co.append(recipe(kRecSmelt, kIngot, kKwSmelter, { { kOre, 1 } }, false));
        co.append(recipe(kRecTan, kLeather, kKwTan, { { kPelt, 1 } }, true));
        co.append(recipe(kRecCook, kCooked, kKwCook, { { kMeat, 1 }, { kSalt, 1 } }, false));
        co.append(recipe(kRecForge, kSword, kKwForge, { { kIngot, 2 } }, false));
        all.append(group("COBJ", co));
        return all;
    }

    struct BenchWorld
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<CraftingService> mCrafting;
        std::vector<std::pair<std::string, float>> mSkillUsed;
        std::vector<float> mHealed;
        FormId mWeapon = kSword; ///< the equipped weapon the poison hook sees (0: none)
        bool mMelee = true;

        void build(const std::string& name)
        {
            mDir = std::filesystem::temp_directory_path() / (std::string("oe_benches_") + name);
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            const Bytes all = plugin();
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            inventory::Hooks ih;
            ih.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            ih.mName = [](FormId) { return std::string(); };
            ih.mHealthOf = [](FormId effect, float magnitude, std::uint32_t duration) {
                return effect == kEffHeal && duration == 0 ? magnitude : 0.f;
            };
            ih.mApplyHealth = [this](float amount) {
                mHealed.push_back(amount);
                return true;
            };
            ih.mUsePoison = [this](FormId item) {
                const PoisonUse use = mCrafting->usePoison(item, mWeapon, mMelee);
                return use == PoisonUse::NotPoison ? 0 : use == PoisonUse::Applied ? 1 : -1;
            };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(ih), 100.f, 1);
            CraftingHooks ch;
            ch.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            ch.mName = [](FormId) { return std::string(); };
            ch.mEffectInfo = [](FormId effect) {
                EffectInfo info;
                info.mFound = true;
                info.mBaseCost = 1.f;
                info.mHostile = effect == kEffDamage;
                return info;
            };
            ch.mSkill = [](std::string_view) { return 15.f; };
            ch.mSkillUsed
                = [this](std::string_view skill, float amount) { mSkillUsed.emplace_back(std::string(skill), amount); };
            mCrafting = std::make_unique<CraftingService>(*mInventory, std::move(ch), CraftingSettings(), false, false);
        }

        bool inputs(inventory::Inputs& out)
        {
            out.mPaths = { mPath };
            std::string err;
            (void)out.mOrder.add("Skyrim.esm", false, err);
            (void)out.mOrder.add("test.esp", false, err);
            return true;
        }

        void waitReady()
        {
            for (int i = 0; i < 4000 && !(mInventory->poll() && (mCrafting->poll(), mCrafting->ready())); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mInventory->ready());
            ASSERT_TRUE(mCrafting->ready());
        }
        void give(FormId item, int n)
        {
            ASSERT_EQ(mInventory->model().grant(kPlayerOwner, item, n), inventory::TransferStatus::Ok);
        }
        std::int32_t held(FormId item) const { return mInventory->model().count(kPlayerOwner, item); }
        void open(FormId furnitureBase)
        {
            mCrafting->openBench(7, furnitureBase, "Bench");
            mCrafting->poll();
        }
        bool offers(FormId recipeId) const
        {
            for (const Recipe* r : mCrafting->visibleRecipes())
                if (r->mId == recipeId)
                    return true;
            return false;
        }
        ~BenchWorld()
        {
            mCrafting.reset();
            mInventory.reset();
            std::error_code ec;
            if (!mDir.empty())
                std::filesystem::remove_all(mDir, ec);
        }
    };

    struct BenchesFixture : testing::Test, BenchWorld
    {
        void SetUp() override { build(testing::UnitTest::GetInstance()->current_test_info()->name()); }
    };

    // ---- the pure condition rule
    // ------------------------------------------------------------------------------------------

    survey::Ctda itemCount(FormId item, std::uint8_t typeByte, float compare)
    {
        survey::Ctda c;
        c.mTypeByte = typeByte;
        c.mCompare = static_cast<std::uint8_t>(typeByte >> 5);
        c.mComparison = compare;
        c.mFunction = 47;
        c.mParam1 = item;
        return c;
    }

    TEST(BenchConditionRule, anItemCountListIsRecognisedAndNothingElseIs)
    {
        const survey::Ctda a = itemCount(kPelt, 0x60, 1.f);
        const survey::Ctda list1[] = { a };
        EXPECT_TRUE(recipeConditionsItemCountOnly(list1));
        survey::Ctda perk;
        perk.mFunction = 448;
        const survey::Ctda list2[] = { a, perk };
        EXPECT_FALSE(recipeConditionsItemCountOnly(list2));
        EXPECT_FALSE(recipeConditionsItemCountOnly({}));
    }

    TEST(BenchConditionRule, theSixOperatorsCompareTheHeldCount)
    {
        const auto held = [](FormId) { return 3; };
        // operators 0 ==, 1 !=, 2 >, 3 >=, 4 <, 5 <= against 3 held
        const struct
        {
            std::uint8_t mType;
            float mValue;
            bool mHolds;
        } cases[] = { { 0x00, 3.f, true }, { 0x00, 2.f, false }, { 0x20, 3.f, false }, { 0x20, 2.f, true },
            { 0x40, 2.f, true }, { 0x40, 3.f, false }, { 0x60, 3.f, true }, { 0x60, 4.f, false }, { 0x80, 4.f, true },
            { 0x80, 3.f, false }, { 0xA0, 3.f, true }, { 0xA0, 2.f, false } };
        for (const auto& c : cases)
        {
            const survey::Ctda list[] = { itemCount(kPelt, c.mType, c.mValue) };
            EXPECT_EQ(itemCountConditionsHold(list, held), c.mHolds)
                << "type " << int(c.mType) << " value " << c.mValue;
        }
    }

    TEST(BenchConditionRule, everyConditionMustHoldAndAnEmptyListHolds)
    {
        const auto held = [](FormId item) { return item == kPelt ? 1 : 0; };
        const survey::Ctda both[] = { itemCount(kPelt, 0x60, 1.f), itemCount(kOre, 0x60, 1.f) };
        EXPECT_FALSE(itemCountConditionsHold(both, held));
        const survey::Ctda one[] = { itemCount(kPelt, 0x60, 1.f) };
        EXPECT_TRUE(itemCountConditionsHold(one, held));
        EXPECT_TRUE(itemCountConditionsHold({}, held));
    }

    // ---- the smelter
    // -----------------------------------------------------------------------------------------------------

    TEST_F(BenchesFixture, theSmelterTurnsOreIntoAnIngotAndTrainsSmithing)
    {
        waitReady();
        give(kOre, 2);
        open(kFurnSmelter);
        ASSERT_EQ(mCrafting->session(), CraftingService::State::Ready);
        EXPECT_TRUE(offers(kRecSmelt));
        EXPECT_FALSE(offers(kRecForge)); // the forge's recipe is not the smelter's
        EXPECT_EQ(mCrafting->craft(kRecSmelt), Status::Ok);
        EXPECT_EQ(held(kOre), 1);
        EXPECT_EQ(held(kIngot), 1);
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "smithing");
        EXPECT_FLOAT_EQ(mSkillUsed[0].second, 5.f); // the value of the ingot
    }

    TEST_F(BenchesFixture, theSmelterWithNoOreRefusesAndTrainsNothing)
    {
        waitReady();
        open(kFurnSmelter);
        EXPECT_EQ(mCrafting->craft(kRecSmelt), Status::MissingItems);
        EXPECT_EQ(held(kIngot), 0);
        EXPECT_TRUE(mSkillUsed.empty());
    }

    TEST_F(BenchesFixture, aRecipeOfAnotherBenchIsRefusedAtTheSmelter)
    {
        waitReady();
        give(kMeat, 1);
        give(kSalt, 1);
        give(kIngot, 2);
        open(kFurnSmelter);
        EXPECT_EQ(mCrafting->craft(kRecCook), Status::NoBench);
        EXPECT_EQ(mCrafting->craft(kRecForge), Status::NoBench);
        EXPECT_EQ(held(kMeat), 1);
    }

    // ---- the tanning rack
    // --------------------------------------------------------------------------------------------------

    TEST_F(BenchesFixture, theTanningRackTurnsAPeltIntoLeatherAndTrainsSmithing)
    {
        waitReady();
        give(kPelt, 1);
        open(kFurnTan);
        ASSERT_EQ(mCrafting->session(), CraftingService::State::Ready);
        EXPECT_TRUE(offers(kRecTan)); // its GetItemCount condition holds
        EXPECT_EQ(mCrafting->craft(kRecTan), Status::Ok);
        EXPECT_EQ(held(kPelt), 0);
        EXPECT_EQ(held(kLeather), 1);
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "smithing");
        EXPECT_FLOAT_EQ(mSkillUsed[0].second, 3.f);
    }

    TEST_F(BenchesFixture, withoutThePeltTheTanningRecipeIsNotOfferedAndCannotBeUsed)
    {
        waitReady();
        open(kFurnTan);
        EXPECT_FALSE(offers(kRecTan)); // GetItemCount(pelt) >= 1 fails
        EXPECT_EQ(mCrafting->craft(kRecTan), Status::UnknownRecipe);
        EXPECT_TRUE(mSkillUsed.empty());
        give(kPelt, 1);
        EXPECT_TRUE(offers(kRecTan)); // it follows what is held
    }

    // ---- the cooking pot
    // ---------------------------------------------------------------------------------------------------

    TEST_F(BenchesFixture, theCookingPotMakesFoodAndTrainsNoSkill)
    {
        waitReady();
        give(kMeat, 1);
        give(kSalt, 1);
        open(kFurnCook);
        EXPECT_TRUE(offers(kRecCook));
        EXPECT_EQ(mCrafting->craft(kRecCook), Status::Ok);
        EXPECT_EQ(held(kMeat), 0);
        EXPECT_EQ(held(kSalt), 0);
        EXPECT_EQ(held(kCooked), 1);
        EXPECT_TRUE(mSkillUsed.empty());
    }

    TEST_F(BenchesFixture, cookingNeedsEveryIngredient)
    {
        waitReady();
        give(kMeat, 1);
        open(kFurnCook);
        EXPECT_EQ(mCrafting->craft(kRecCook), Status::MissingItems);
        EXPECT_EQ(held(kMeat), 1);
        EXPECT_EQ(held(kCooked), 0);
    }

    TEST_F(BenchesFixture, cookedFoodIsEatenThroughTheUsePath)
    {
        waitReady();
        give(kMeat, 1);
        give(kSalt, 1);
        open(kFurnCook);
        ASSERT_EQ(mCrafting->craft(kRecCook), Status::Ok);
        EXPECT_EQ(mInventory->use(kCooked), inventory::InventoryService::UseStatus::Ok);
        ASSERT_EQ(mHealed.size(), 1u);
        EXPECT_FLOAT_EQ(mHealed[0], 5.f);
        EXPECT_EQ(held(kCooked), 0);
    }

    // ---- the forge, as the control
    // -----------------------------------------------------------------------------------------

    TEST_F(BenchesFixture, theForgeStillTrainsSmithingByTheValueOfTheItem)
    {
        waitReady();
        give(kIngot, 2);
        open(kFurnForge);
        EXPECT_EQ(mCrafting->craft(kRecForge), Status::Ok);
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "smithing");
        EXPECT_FLOAT_EQ(mSkillUsed[0].second, 25.f);
    }

    // ---- poisons applied to the equipped weapon (the service half; the combat half is in testskyrimsepoison.cpp)
    // --------------------------------------------------------------------------------------------------------------

    TEST_F(BenchesFixture, aPoisonCoatsTheEquippedWeaponWithItsEffectsAndOneCharge)
    {
        waitReady();
        give(kPoison, 1);
        EXPECT_EQ(mCrafting->usePoison(kPoison, kSword, true), PoisonUse::Applied);
        const WeaponPoison* coat = mCrafting->coating(kSword);
        ASSERT_NE(coat, nullptr);
        EXPECT_EQ(coat->mPoison, kPoison);
        EXPECT_EQ(coat->mCharges, 1); // CraftingSettings::mPoisonCharges
        ASSERT_EQ(coat->mEffects.size(), 1u);
        EXPECT_EQ(coat->mEffects[0].mEffect, kEffDamage);
        EXPECT_FLOAT_EQ(coat->mEffects[0].mMagnitude, 5.f);
        EXPECT_EQ(coat->mEffects[0].mDuration, 4u);
    }

    TEST_F(BenchesFixture, aPotionAFoodAndAnUnknownItemAreNotPoisons)
    {
        waitReady();
        EXPECT_EQ(mCrafting->usePoison(kCooked, kSword, true), PoisonUse::NotPoison);
        EXPECT_EQ(mCrafting->usePoison(kOre, kSword, true), PoisonUse::NotPoison);
        EXPECT_EQ(mCrafting->usePoison(0x12345678u, kSword, true), PoisonUse::NotPoison);
        EXPECT_EQ(mCrafting->coating(kSword), nullptr);
    }

    TEST_F(BenchesFixture, withoutAWeaponOrWithABowAPoisonIsRefused)
    {
        waitReady();
        EXPECT_EQ(mCrafting->usePoison(kPoison, 0, true), PoisonUse::NoWeapon);
        EXPECT_EQ(mCrafting->usePoison(kPoison, kBow, false), PoisonUse::NotMelee);
        EXPECT_EQ(mCrafting->coating(kBow), nullptr);
        EXPECT_EQ(mCrafting->coating(kSword), nullptr);
    }

    TEST_F(BenchesFixture, aWeaponAlreadyPoisonedRefusesASecondPoison)
    {
        waitReady();
        EXPECT_EQ(mCrafting->usePoison(kPoison, kSword, true), PoisonUse::Applied);
        EXPECT_EQ(mCrafting->usePoison(kPoison2, kSword, true), PoisonUse::AlreadyPoisoned);
        const WeaponPoison* coat = mCrafting->coating(kSword);
        ASSERT_NE(coat, nullptr);
        EXPECT_EQ(coat->mPoison, kPoison); // the first stays
    }

    TEST_F(BenchesFixture, usingAHeldPoisonFromTheInventoryConsumesItAndCoatsTheWeapon)
    {
        waitReady();
        give(kPoison, 2);
        EXPECT_EQ(mInventory->use(kPoison), inventory::InventoryService::UseStatus::Ok);
        EXPECT_EQ(held(kPoison), 1);
        ASSERT_NE(mCrafting->coating(kSword), nullptr);
        // the weapon is poisoned: the second one is refused and NOT consumed
        EXPECT_EQ(mInventory->use(kPoison), inventory::InventoryService::UseStatus::Refused);
        EXPECT_EQ(held(kPoison), 1);
        EXPECT_TRUE(mHealed.empty()); // a poison is never drunk
    }

    TEST_F(BenchesFixture, spendingTheChargeEndsTheCoating)
    {
        waitReady();
        ASSERT_EQ(mCrafting->usePoison(kPoison, kSword, true), PoisonUse::Applied);
        EXPECT_EQ(mCrafting->spendPoison(kSword), 0); // none left
        EXPECT_EQ(mCrafting->coating(kSword), nullptr);
        EXPECT_EQ(mCrafting->spendPoison(kSword), -1); // nothing to spend
        EXPECT_EQ(mCrafting->usePoison(kPoison2, kSword, true), PoisonUse::Applied); // the weapon takes a new one
    }
}

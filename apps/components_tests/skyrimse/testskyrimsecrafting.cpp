// Task 0199 (crafting; milestone M-S7): the records, the pure rules, the synthetic items and the save chunk. Generated
// plugin (testskyrimsecraftingdata.hpp), no game data. Written before the code and run red against stubs first.
#include <game_skyrimse/crafting.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "testskyrimsecraftingdata.hpp"

namespace
{
    using namespace CraftingTestData;
    using namespace game_skyrimse;
    using namespace game_skyrimse::crafting;

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

    CraftingTable makeTable()
    {
        CraftingRecords records;
        const auto plugin = openPlugin(testPlugin());
        EXPECT_NE(plugin, nullptr);
        const bool ok = plugin != nullptr && parseCraftingRecords(*plugin, order(), CraftingLimits(), records);
        EXPECT_TRUE(ok);
        CraftingTable table;
        table.add(std::move(records));
        table.finish();
        return table;
    }

    Ingredient ing(FormId id, std::vector<EffectEntry> effects)
    {
        Ingredient i;
        i.mId = id;
        i.mEffects = std::move(effects);
        return i;
    }

    // ---- bench names
    // -------------------------------------------------------------------------------------------------

    TEST(SkyrimCraftingBench, everyKindHasANameAndComesBack)
    {
        for (const BenchKind kind : { BenchKind::Alchemy, BenchKind::Enchanting, BenchKind::Forge, BenchKind::Skyforge,
                 BenchKind::SharpeningWheel, BenchKind::ArmorTable, BenchKind::Smelter, BenchKind::Cookpot,
                 BenchKind::TanningRack })
        {
            EXPECT_FALSE(benchKindName(kind).empty());
            EXPECT_EQ(benchKindFromName(benchKindName(kind)), kind);
            EXPECT_FALSE(benchKeywordName(kind).empty());
        }
        EXPECT_EQ(benchKindFromName("nonsense"), BenchKind::None);
        EXPECT_TRUE(benchKeywordName(BenchKind::None).empty());
    }

    TEST(SkyrimCraftingBench, onlyTheWheelAndTheArmourTableTemper)
    {
        EXPECT_TRUE(isTemperBench(BenchKind::SharpeningWheel));
        EXPECT_TRUE(isTemperBench(BenchKind::ArmorTable));
        EXPECT_FALSE(isTemperBench(BenchKind::Forge));
        EXPECT_FALSE(isTemperBench(BenchKind::Alchemy));
        EXPECT_FALSE(isTemperBench(BenchKind::None));
    }

    TEST(SkyrimCraftingBench, theKeywordNamesAreTheGamesOwn)
    {
        EXPECT_EQ(benchKeywordName(BenchKind::Forge), "CraftingSmithingForge");
        EXPECT_EQ(benchKeywordName(BenchKind::SharpeningWheel), "CraftingSmithingSharpeningWheel");
        EXPECT_EQ(benchKeywordName(BenchKind::Alchemy), "WICraftingAlchemy");
        EXPECT_EQ(benchKeywordName(BenchKind::Enchanting), "WICraftingEnchanting");
    }

    // ---- rules
    // -------------------------------------------------------------------------------------------------------

    TEST(SkyrimCraftingRules, theAlchemyStrengthGrowsWithTheSkillAndIsClamped)
    {
        const CraftingSettings s;
        EXPECT_FLOAT_EQ(alchemyStrength(0.f, s), 1.f);
        EXPECT_FLOAT_EQ(alchemyStrength(100.f, s), 1.5f);
        EXPECT_NEAR(alchemyStrength(15.f, s), 1.075f, 1e-6f);
        EXPECT_FLOAT_EQ(alchemyStrength(250.f, s), 1.5f);
        EXPECT_FLOAT_EQ(alchemyStrength(-5.f, s), 1.f);
        EXPECT_FLOAT_EQ(alchemyStrength(std::nanf(""), s), 1.f);
    }

    TEST(SkyrimCraftingRules, twoIngredientsShareTheirCommonEffectsWithTheBestNumbers)
    {
        const Ingredient a = ing(1, { { 10, 5.f, 0, 0 }, { 11, 10.f, 0, 60 }, { 12, 2.f, 0, 0 } });
        const Ingredient b = ing(2, { { 10, 4.f, 0, 0 }, { 11, 12.f, 0, 30 }, { 13, 1.f, 0, 5 } });
        const Ingredient* both[] = { &a, &b };
        const std::vector<EffectEntry> shared = sharedEffects(both);
        ASSERT_EQ(shared.size(), 2u);
        EXPECT_EQ(shared[0].mEffect, 10u);
        EXPECT_FLOAT_EQ(shared[0].mMagnitude, 5.f);
        EXPECT_EQ(shared[1].mEffect, 11u);
        EXPECT_FLOAT_EQ(shared[1].mMagnitude, 12.f);
        EXPECT_EQ(shared[1].mDuration, 60u);
    }

    TEST(SkyrimCraftingRules, anEffectOfOneIngredientAloneIsNotShared)
    {
        const Ingredient a = ing(1, { { 10, 5.f, 0, 0 } });
        const Ingredient b = ing(2, { { 11, 5.f, 0, 0 } });
        const Ingredient* both[] = { &a, &b };
        EXPECT_TRUE(sharedEffects(both).empty());
    }

    TEST(SkyrimCraftingRules, threeIngredientsShareWhatAnyTwoHave)
    {
        const Ingredient a = ing(1, { { 10, 5.f, 0, 0 } });
        const Ingredient b = ing(2, { { 11, 6.f, 0, 0 } });
        const Ingredient c = ing(3, { { 11, 7.f, 0, 0 }, { 10, 1.f, 0, 0 } });
        const Ingredient* three[] = { &a, &b, &c };
        const std::vector<EffectEntry> shared = sharedEffects(three);
        ASSERT_EQ(shared.size(), 2u);
        EXPECT_EQ(shared[0].mEffect, 10u); // first appearance order
        EXPECT_FLOAT_EQ(shared[0].mMagnitude, 5.f);
        EXPECT_EQ(shared[1].mEffect, 11u);
        EXPECT_FLOAT_EQ(shared[1].mMagnitude, 7.f);
    }

    TEST(SkyrimCraftingRules, oneIngredientTwiceOrNoneOrFourShareNothing)
    {
        const Ingredient a = ing(1, { { 10, 5.f, 0, 0 } });
        const Ingredient b = ing(2, { { 10, 5.f, 0, 0 } });
        const Ingredient* twice[] = { &a, &a };
        EXPECT_TRUE(sharedEffects(twice).empty());
        const Ingredient* one[] = { &a };
        EXPECT_TRUE(sharedEffects(one).empty());
        EXPECT_TRUE(sharedEffects(std::span<const Ingredient* const>()).empty());
        const Ingredient* four[] = { &a, &b, &a, &b };
        EXPECT_TRUE(sharedEffects(four).empty());
    }

    TEST(SkyrimCraftingRules, thePotionScalesMagnitudesAndLeavesDurationsAlone)
    {
        const std::vector<EffectEntry> effects = { { 10, 5.f, 0, 0 }, { 11, 0.f, 0, 30 } };
        const std::vector<EffectEntry> scaled = scalePotionEffects(effects, 1.5f);
        ASSERT_EQ(scaled.size(), 2u);
        EXPECT_FLOAT_EQ(scaled[0].mMagnitude, 7.5f);
        EXPECT_EQ(scaled[0].mDuration, 0u);
        EXPECT_FLOAT_EQ(scaled[1].mMagnitude, 0.f);
        EXPECT_EQ(scaled[1].mDuration, 30u);
    }

    TEST(SkyrimCraftingRules, aPotionIsWorthItsEffectsBaseCostAndAtLeastOne)
    {
        const CraftingSettings s;
        const auto cost10 = [](FormId) { return 10.f; };
        const std::int32_t v = potionValue({ { 10, 20.f, 0, 0 } }, cost10, s);
        EXPECT_NEAR(v, 270, 2); // 10 x (20 x 10 / 10) ^ 1.1
        EXPECT_EQ(potionValue({}, cost10, s), 0);
        EXPECT_EQ(potionValue(
                      { { 10, 5.f, 0, 0 } }, [](FormId) { return 0.f; }, s),
            1);
        CraftingSettings capped;
        capped.mMaxValue = 50;
        EXPECT_EQ(potionValue({ { 10, 20.f, 0, 0 } }, cost10, capped), 50);
        EXPECT_GT(potionValue({ { 10, 20.f, 0, 0 }, { 11, 20.f, 0, 0 } }, cost10, s), v);
    }

    TEST(SkyrimCraftingRules, theTemperFractionFollowsTheSkillAndTheGradesFollowTheFraction)
    {
        const CraftingSettings s;
        EXPECT_NEAR(temperFraction(15.f, s), 0.06f, 1e-6f);
        EXPECT_NEAR(temperFraction(100.f, s), 0.4f, 1e-6f);
        EXPECT_FLOAT_EQ(temperFraction(-3.f, s), 0.f);
        EXPECT_EQ(temperGrade(0.039f), "");
        EXPECT_EQ(temperGrade(0.04f), "Fine");
        EXPECT_EQ(temperGrade(0.08f), "Superior");
        EXPECT_EQ(temperGrade(0.15f), "Exquisite");
        EXPECT_EQ(temperGrade(0.25f), "Flawless");
        EXPECT_EQ(temperGrade(0.35f), "Epic");
        EXPECT_EQ(temperGrade(0.5f), "Legendary");
        EXPECT_EQ(temperGrade(3.f), "Legendary");
        EXPECT_EQ(toPermille(0.06f), 60);
        EXPECT_EQ(toPermille(0.4f), 400);
        EXPECT_EQ(toPermille(-1.f), 0);
    }

    TEST(SkyrimCraftingRules, theEnchantmentMagnitudeFollowsTheSoulAndTheSkill)
    {
        const CraftingSettings s;
        EXPECT_NEAR(enchantMagnitude(10.f, 5, 15.f, s), 16.6f, 0.01f); // 10 x 1.6 x (1 + 0.25 x 0.15)
        EXPECT_NEAR(enchantMagnitude(10.f, 1, 0.f, s), 5.f, 1e-4f);
        EXPECT_FLOAT_EQ(enchantMagnitude(10.f, 0, 15.f, s), 0.f);
        EXPECT_FLOAT_EQ(enchantMagnitude(10.f, 6, 15.f, s), 0.f);
        EXPECT_GT(enchantMagnitude(10.f, 4, 50.f, s), enchantMagnitude(10.f, 4, 15.f, s));
        EXPECT_GT(enchantMagnitude(10.f, 5, 15.f, s), enchantMagnitude(10.f, 3, 15.f, s));
    }

    TEST(SkyrimCraftingRules, anEditorIdBecomesAReadableName)
    {
        EXPECT_EQ(prettyEditorId("EnchWeaponFireDamageBase"), "Fire Damage");
        EXPECT_EQ(prettyEditorId("EnchWeaponFrostDamage02"), "Frost Damage 02");
        EXPECT_EQ(prettyEditorId("AlchRestoreHealth"), "Restore Health");
        EXPECT_EQ(prettyEditorId("EnchArmorFortifyAlchemy03"), "Fortify Alchemy 03");
        EXPECT_EQ(prettyEditorId(""), "");
        EXPECT_EQ(prettyEditorId("Ench"), "Ench");
    }

    // ---- the records
    // -------------------------------------------------------------------------------------------------

    TEST(SkyrimCraftingRecords, anIngredientKeepsItsFourEffects)
    {
        const CraftingTable t = makeTable();
        const Ingredient* a = t.ingredient(kIngA);
        ASSERT_NE(a, nullptr);
        ASSERT_EQ(a->mEffects.size(), 4u);
        EXPECT_EQ(a->mEffects[1].mEffect, kEffFortify);
        EXPECT_FLOAT_EQ(a->mEffects[1].mMagnitude, 10.f);
        EXPECT_EQ(a->mEffects[1].mDuration, 60u);
        EXPECT_EQ(t.ingredient(kSword), nullptr);
        EXPECT_EQ(t.ingredientCount(), 4u);
    }

    TEST(SkyrimCraftingRecords, aRecipeKeepsItsResultCountBenchAndItems)
    {
        const CraftingTable t = makeTable();
        const Recipe* r = t.recipe(kRecForge);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->mResult, kSword);
        EXPECT_EQ(r->mCount, 1u);
        EXPECT_EQ(r->mBench, kKwForge);
        ASSERT_EQ(r->mItems.size(), 2u);
        EXPECT_EQ(r->mItems[0].mItem, kIngot);
        EXPECT_EQ(r->mItems[0].mCount, 2);
        EXPECT_EQ(r->mConditions, 0u);
        EXPECT_EQ(t.recipe(kRecCond)->mConditions, 2u);
    }

    TEST(SkyrimCraftingRecords, anEnchantmentKeepsItsBaseTypeAndEffects)
    {
        const CraftingTable t = makeTable();
        const Enchantment* e = t.enchantment(kEnchFire03);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->mBase, kEnchFireBase);
        EXPECT_EQ(e->mEnchantType, 6u);
        ASSERT_EQ(e->mEffects.size(), 1u);
        EXPECT_EQ(e->mEffects[0].mEffect, kEffFire);
        EXPECT_FLOAT_EQ(e->mEffects[0].mMagnitude, 15.f);
        EXPECT_EQ(e->mEditorId, "EnchWeaponFireDamage03");
        EXPECT_EQ(t.enchantment(kEnchFireBase)->mBase, kNullFormId);
    }

    TEST(SkyrimCraftingRecords, anItemRemembersTheEnchantmentOfItsRecord)
    {
        const CraftingTable t = makeTable();
        EXPECT_EQ(t.enchantmentOf(kEnchSword), kEnchFire03);
        EXPECT_EQ(t.enchantmentOf(kSword), kNullFormId);
    }

    TEST(SkyrimCraftingRecords, aSoulGemKeepsItsCapacityAndSoul)
    {
        const CraftingTable t = makeTable();
        const SoulGem* g = t.soulGem(kGemFull);
        ASSERT_NE(g, nullptr);
        EXPECT_EQ(g->mCapacity, 5);
        EXPECT_EQ(g->mSoul, 5);
        EXPECT_EQ(t.soulGem(kGemEmpty)->mSoul, 0);
        EXPECT_EQ(t.soulGem(kGemPetty)->mCapacity, 1);
    }

    TEST(SkyrimCraftingRecords, onlyTheCraftingKeywordsAreKept)
    {
        const CraftingTable t = makeTable();
        EXPECT_EQ(t.keyword("CraftingSmithingForge"), kKwForge);
        EXPECT_EQ(t.keyword("WICraftingAlchemy"), kKwAlchemy);
        EXPECT_EQ(t.keyword("VendorItemFood"), kNullFormId);
        EXPECT_EQ(t.benchOfKeyword(kKwForge), BenchKind::Forge);
        EXPECT_EQ(t.benchOfKeyword(kKwWheel), BenchKind::SharpeningWheel);
        EXPECT_EQ(t.benchOfKeyword(kKwOther), BenchKind::None);
    }

    TEST(SkyrimCraftingRecords, aFurnitureOffersTheBenchesOfItsKeywords)
    {
        const CraftingTable t = makeTable();
        EXPECT_EQ(t.benchesOf(kFurnForge), std::vector<BenchKind>({ BenchKind::Forge }));
        const std::vector<BenchKind> sky = t.benchesOf(kFurnSky);
        ASSERT_EQ(sky.size(), 2u);
        EXPECT_NE(std::find(sky.begin(), sky.end(), BenchKind::Skyforge), sky.end());
        EXPECT_NE(std::find(sky.begin(), sky.end(), BenchKind::Forge), sky.end());
        EXPECT_EQ(t.benchesOf(kFurnAlchemy), std::vector<BenchKind>({ BenchKind::Alchemy }));
        EXPECT_EQ(t.benchesOf(kFurnEnchant), std::vector<BenchKind>({ BenchKind::Enchanting }));
        EXPECT_TRUE(t.benchesOf(kFurnChair).empty());
        EXPECT_TRUE(t.benchesOf(kSword).empty());
    }

    TEST(SkyrimCraftingRecords, theForgeShowsItsRecipesAndHidesConditionsByDefault)
    {
        const CraftingTable t = makeTable();
        const std::vector<const Recipe*> forge = t.recipesFor(BenchKind::Forge, false);
        ASSERT_EQ(forge.size(), 2u); // kRecForge and kRecBad; kRecCond has conditions
        EXPECT_EQ(forge[0]->mId, kRecForge);
        EXPECT_EQ(forge[1]->mId, kRecBad);
        EXPECT_EQ(t.recipesFor(BenchKind::Forge, true).size(), 3u);
        const std::vector<const Recipe*> wheel = t.recipesFor(BenchKind::SharpeningWheel, false);
        ASSERT_EQ(wheel.size(), 1u);
        EXPECT_EQ(wheel[0]->mId, kRecTemperSword);
        EXPECT_EQ(t.recipesFor(BenchKind::ArmorTable, false).size(), 1u);
        EXPECT_EQ(t.recipesFor(BenchKind::Skyforge, false).size(), 1u);
        EXPECT_TRUE(t.recipesFor(BenchKind::Alchemy, true).empty());
    }

    TEST(SkyrimCraftingRecords, aTemperingRecipeIsShownWhateverItsConditions)
    {
        const CraftingTable t = makeTable();
        EXPECT_EQ(t.recipe(kRecTemperArmor)->mConditions, 2u);
        const std::vector<const Recipe*> armour = t.recipesFor(BenchKind::ArmorTable, false);
        ASSERT_EQ(armour.size(), 1u);
        EXPECT_EQ(armour[0]->mId, kRecTemperArmor);
    }

    TEST(SkyrimCraftingRecords, anEffectHasItsEditorIdAsAName)
    {
        const CraftingTable t = makeTable();
        EXPECT_EQ(t.effectName(kEffHeal), "AlchRestoreHealth");
        EXPECT_EQ(t.effectName(kSword), "");
    }

    TEST(SkyrimCraftingRecords, aLaterPluginReplacesAnEarlierRecord)
    {
        CraftingTable t;
        CraftingRecords a, b;
        Ingredient first;
        first.mId = 5;
        first.mEffects = { { 1, 1.f, 0, 0 } };
        Ingredient second = first;
        second.mEffects = { { 2, 9.f, 0, 0 } };
        a.mIngredients.push_back(first);
        b.mIngredients.push_back(second);
        t.add(std::move(a));
        t.add(std::move(b));
        t.finish();
        ASSERT_NE(t.ingredient(5), nullptr);
        EXPECT_EQ(t.ingredient(5)->mEffects[0].mEffect, 2u);
    }

    TEST(SkyrimCraftingRecords, aWrongSizedSubrecordIsCountedAndSkipped)
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 2, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes bad = sub4("CNAM", Bytes().u32(kSword));
        bad.append(sub4("BNAM", Bytes().u16(3)));
        bad.append(sub4("NAM1", Bytes().u16(1)));
        all.append(grp("COBJ", record4("COBJ", 0, kRecForge, bad)));
        const auto plugin = openPlugin(all);
        ASSERT_NE(plugin, nullptr);
        CraftingRecords records;
        EXPECT_TRUE(parseCraftingRecords(*plugin, order(), CraftingLimits(), records));
        EXPECT_GE(records.mOddSubrecords, 1u);
    }

    TEST(SkyrimCraftingRecords, aPluginOfAnotherGameIsRefused)
    {
        const auto plugin = openPlugin(tes3Header(1.3f, 0, "A", "B", 0, {}));
        ASSERT_NE(plugin, nullptr);
        CraftingRecords records;
        EXPECT_FALSE(parseCraftingRecords(*plugin, order(), CraftingLimits(), records));
        EXPECT_TRUE(records.mWalkError.has_value());
    }

    // ---- synthetic items and the chunk
    // -------------------------------------------------------------------------------

    TEST(SkyrimCraftingState, aMadeItemHasAnIdWithTheMadeInGamePrefix)
    {
        CraftingState s;
        const FormId id = s.addPotion("Potion of Restore Health", 40, 0.5f, false, { { kEffHeal, 5.f, 0, 0 } });
        EXPECT_EQ(id, kSyntheticPrefix | 1u);
        EXPECT_TRUE(isSyntheticId(id));
        EXPECT_FALSE(isSyntheticId(kSword));
        EXPECT_FALSE(isSyntheticId(kUnresolvedFormId));
        ASSERT_NE(s.find(id), nullptr);
        EXPECT_EQ(s.find(id)->mName, "Potion of Restore Health");
        EXPECT_EQ(s.find(id)->mKind, SyntheticKind::Potion);
        EXPECT_EQ(s.find(id)->mCategory, inventory::Category::Potion);
    }

    TEST(SkyrimCraftingState, anEqualDefinitionIsTheSameItemAndADifferentOneIsNext)
    {
        CraftingState s;
        const FormId a = s.addPotion("P", 40, 0.5f, false, { { kEffHeal, 5.f, 0, 0 } });
        EXPECT_EQ(s.addPotion("P", 40, 0.5f, false, { { kEffHeal, 5.f, 0, 0 } }), a);
        const FormId b = s.addPotion("P", 40, 0.5f, false, { { kEffHeal, 6.f, 0, 0 } });
        EXPECT_EQ(b, a + 1);
        const FormId g = s.addGear(kSword, "Fine Sword", 30, 9.f, inventory::Category::Weapon, 60, 0, 0.f);
        EXPECT_EQ(g, a + 2);
        EXPECT_EQ(s.addGear(kSword, "Fine Sword", 30, 9.f, inventory::Category::Weapon, 60, 0, 0.f), g);
        EXPECT_NE(s.addGear(kSword, "Fine Sword", 30, 9.f, inventory::Category::Weapon, 80, 0, 0.f), g);
        EXPECT_EQ(s.all().size(), 4u);
    }

    TEST(SkyrimCraftingState, aBadDefinitionGivesNoItem)
    {
        CraftingState s;
        EXPECT_EQ(s.addPotion("P", 1, 1.f, false, {}), kNullFormId);
        EXPECT_EQ(s.addPotion("", 1, 1.f, false, { { kEffHeal, 1.f, 0, 0 } }), kNullFormId);
        EXPECT_EQ(
            s.addPotion(std::string(CraftingState::kMaxName + 1, 'x'), 1, 1.f, false, { { kEffHeal, 1.f, 0, 0 } }),
            kNullFormId);
        EXPECT_EQ(s.addPotion("P", 1, 1.f, false,
                      std::vector<EffectEntry>(CraftingState::kMaxEffects + 1, EffectEntry{ 1, 1.f, 0, 0 })),
            kNullFormId);
        EXPECT_EQ(s.addGear(0, "G", 1, 1.f, inventory::Category::Weapon, 10, 0, 0.f), kNullFormId);
        EXPECT_EQ(s.addGear(kSword, "G", 1, 1.f, inventory::Category::Potion, 10, 0, 0.f), kNullFormId);
        EXPECT_TRUE(s.all().empty());
    }

    TEST(SkyrimCraftingState, theTableIsBounded)
    {
        CraftingState s;
        for (std::size_t i = 0; i < CraftingState::kMaxSynthetic; ++i)
            ASSERT_NE(s.addPotion("P", 1, 1.f, false, { { kEffHeal, static_cast<float>(i + 1), 0, 0 } }), kNullFormId);
        EXPECT_EQ(s.addPotion("P", 1, 1.f, false, { { kEffHeal, 99999.f, 0, 0 } }), kNullFormId);
    }

    TEST(SkyrimCraftingState, whatTheSessionKnowsGrowsOnceAndMovesTheRevision)
    {
        CraftingState s;
        const std::uint32_t r0 = s.revision();
        EXPECT_TRUE(s.learnEffect(kEffHeal));
        EXPECT_GT(s.revision(), r0);
        const std::uint32_t r1 = s.revision();
        EXPECT_FALSE(s.learnEffect(kEffHeal));
        EXPECT_EQ(s.revision(), r1);
        EXPECT_TRUE(s.knowsEffect(kEffHeal));
        EXPECT_FALSE(s.knowsEffect(kEffSlow));
        EXPECT_TRUE(s.learnEnchantment(kEnchFireBase));
        EXPECT_FALSE(s.learnEnchantment(kEnchFireBase));
        EXPECT_TRUE(s.knowsEnchantment(kEnchFireBase));
        EXPECT_FALSE(s.learnEffect(kNullFormId));
    }

    CraftingState sample()
    {
        CraftingState s;
        s.addPotion(
            "Potion of Restore Health", 40, 0.5f, false, { { kEffHeal, 5.f, 0, 0 }, { kEffFortify, 12.f, 0, 60 } });
        s.addPotion("Poison of Damage", 12, 0.5f, true, { { kEffDamage, 3.f, 0, 0 } });
        s.addGear(
            kSword, "Fine Iron Sword of Fire Damage", 80, 9.f, inventory::Category::Weapon, 60, kEnchFireBase, 16.6f);
        s.learnEffect(kEffHeal);
        s.learnEffect(kEffFortify);
        s.learnEnchantment(kEnchFireBase);
        return s;
    }

    TEST(SkyrimCraftingChunk, theStateRoundTripsExactly)
    {
        const CraftingState a = sample();
        MemoryWriter w;
        a.write(w);
        CraftingState b;
        MemoryReader r(w.mBytes);
        std::string error;
        ASSERT_TRUE(b.read(r, error)) << error;
        EXPECT_EQ(b.all(), a.all());
        EXPECT_EQ(b.knownEffects(), a.knownEffects());
        EXPECT_EQ(b.knownEnchantments(), a.knownEnchantments());
        // the next id continues where the saved game stopped
        EXPECT_EQ(b.addPotion("New", 1, 1.f, false, { { kEffSlow, 1.f, 0, 0 } }), kSyntheticPrefix | 4u);
        MemoryWriter w2;
        a.write(w2);
        EXPECT_EQ(w2.mBytes, w.mBytes); // canonical
    }

    TEST(SkyrimCraftingChunk, aReadChangesTheRevision)
    {
        MemoryWriter w;
        sample().write(w);
        CraftingState b;
        const std::uint32_t before = b.revision();
        MemoryReader r(w.mBytes);
        std::string error;
        ASSERT_TRUE(b.read(r, error));
        EXPECT_NE(b.revision(), before);
    }

    TEST(SkyrimCraftingChunk, everyTruncationAndATrailingByteIsRefusedAndChangesNothing)
    {
        MemoryWriter w;
        sample().write(w);
        for (std::size_t n = 0; n < w.mBytes.size(); ++n)
        {
            CraftingState b = sample();
            const std::vector<unsigned char> cut(w.mBytes.begin(), w.mBytes.begin() + static_cast<std::ptrdiff_t>(n));
            MemoryReader r(cut);
            std::string error;
            EXPECT_FALSE(b.read(r, error)) << "length " << n;
            EXPECT_EQ(b.all(), sample().all());
        }
        std::vector<unsigned char> more = w.mBytes;
        more.push_back(0);
        CraftingState b;
        MemoryReader r(more);
        std::string error;
        EXPECT_FALSE(b.read(r, error));
        EXPECT_TRUE(b.all().empty());
        EXPECT_FALSE(error.empty());
    }

    TEST(SkyrimCraftingChunk, aDamagedByteIsRefusedOrReadsBackAsItIs)
    {
        MemoryWriter w;
        sample().write(w);
        std::size_t refused = 0;
        for (std::size_t i = 0; i < w.mBytes.size(); ++i)
        {
            std::vector<unsigned char> bytes = w.mBytes;
            bytes[i] ^= 0xFF;
            CraftingState b;
            MemoryReader r(bytes);
            std::string error;
            if (!b.read(r, error))
                ++refused;
            else
            {
                MemoryWriter again;
                b.write(again);
                EXPECT_EQ(again.mBytes, bytes) << "byte " << i;
            }
        }
        EXPECT_GT(refused, 0u);
    }

    TEST(SkyrimCraftingChunk, theHandlerNamesAndVersionsTheChunk)
    {
        CraftingState s = sample();
        CraftingChunk chunk(s);
        EXPECT_EQ(chunk.chunkName(), "crafting");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_FALSE(w.mBytes.empty());
        CraftingState other;
        CraftingChunk reader(other);
        std::string restored;
        reader.setRestoredSink([&](const std::string& what) { restored = what; });
        MemoryReader r(w.mBytes);
        EXPECT_TRUE(reader.read(r, 1));
        EXPECT_FALSE(restored.empty());
        EXPECT_EQ(other.all().size(), 3u);
        MemoryReader again(w.mBytes);
        EXPECT_FALSE(reader.read(again, 2));
        EXPECT_FALSE(reader.refusal().empty());
    }
}

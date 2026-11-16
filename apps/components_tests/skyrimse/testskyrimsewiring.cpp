// Task 0206 (wiring the overnight Skyrim systems): crafting trains the player's skills and reads the real ones, Papyrus
// reads the skills, and a recipe's perk conditions are answered from the perks the player bought. Generated plugin and
// a real ProgressionService; no game data. Written before the code, run red against stubs.
#include <game_skyrimse/crafting_service.hpp>
#include <game_skyrimse/progression_service.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "testskyrimsecraftingdata.hpp"

namespace
{
    using namespace CraftingTestData;
    using namespace game_skyrimse;
    using namespace game_skyrimse::crafting;
    using inventory::kPlayerOwner;
    using progression::kSkillCount;
    namespace Slot = progression::Slot;

    constexpr float kUseMult = 0.01f; // small, so that one craft never rolls over into a skill increase
    constexpr FormId kPerkSteel = kSelf | 0xC00;
    constexpr FormId kPerkArcane = kSelf | 0xC01;
    constexpr FormId kRecPerkForge = kSelf | 0x910; // forge: the sword, 1 ingot, needs HasPerk(kPerkSteel)
    constexpr FormId kRecGlobalForge = kSelf | 0x911; // forge: a global condition (not perk-only)
    constexpr FormId kRecTemperEnch = kSelf | 0x912; // wheel: the enchanted sword; "not enchanted OR Arcane" (0x01 last)
    constexpr FormId kRecTemperPlain = kSelf | 0x913; // wheel: the plain sword with the same pair (0x00 last)

    survey::Ctda makeCtda(std::uint8_t type, float comparison, std::uint16_t function, std::uint32_t param1)
    {
        const Bytes raw = Bytes()
                              .u8(type)
                              .u8(0)
                              .u8(0)
                              .u8(0)
                              .f32(comparison)
                              .u16(function)
                              .u16(0)
                              .u32(param1)
                              .u32(0)
                              .u32(0)
                              .u32(0)
                              .u32(0);
        survey::Ctda c;
        EXPECT_TRUE(survey::decodeCtda({ reinterpret_cast<const std::byte*>(raw.str().data()), raw.str().size() }, c));
        return c;
    }

    Bytes rawCtda(std::uint8_t type, float comparison, std::uint16_t function, std::uint32_t param1)
    {
        return sub4("CTDA",
            Bytes()
                .u8(type)
                .u8(0)
                .u8(0)
                .u8(0)
                .f32(comparison)
                .u16(function)
                .u16(0)
                .u32(param1)
                .u32(0)
                .u32(0)
                .u32(0)
                .u32(0));
    }

    Bytes cobjWith(FormId id, FormId result, FormId bench, std::vector<std::pair<FormId, int>> items,
        std::vector<Bytes> conditions)
    {
        Bytes d;
        d.append(sub4("COCT", Bytes().u32(static_cast<std::uint32_t>(items.size()))));
        for (const auto& [item, n] : items)
            d.append(sub4("CNTO", Bytes().u32(item).u32(static_cast<std::uint32_t>(n))));
        for (const Bytes& c : conditions)
            d.append(c);
        d.append(sub4("CNAM", Bytes().u32(result)));
        d.append(sub4("BNAM", Bytes().u32(bench)));
        d.append(sub4("NAM1", Bytes().u16(1)));
        return record4("COBJ", 0, id, d);
    }

    Bytes wiringPlugin()
    {
        Bytes all = testPlugin();
        Bytes co;
        co.append(cobjWith(kRecPerkForge, kSword, kKwForge, { { kIngot, 1 } }, { rawCtda(0x00, 1.f, 448, kPerkSteel) }));
        co.append(cobjWith(
            kRecGlobalForge, kArmor, kKwForge, { { kIngot, 1 } }, { rawCtda(0x00, 1.f, 74, kSelf | 0xD00) }));
        co.append(cobjWith(kRecTemperEnch, kEnchSword, kKwWheel, { { kIngot, 1 } },
            { rawCtda(0x21, 1.f, 659, 0), rawCtda(0x01, 1.f, 448, kPerkArcane) }));
        co.append(cobjWith(kRecTemperPlain, kSword, kKwWheel, { { kIngot, 1 } },
            { rawCtda(0x21, 1.f, 659, 0), rawCtda(0x00, 1.f, 448, kPerkArcane) }));
        all.append(grp("COBJ", co));
        return all;
    }

    // ---- the progression of the tests: 18 skills with simple numbers, the Smithing tree holds the perk kPerkSteel
    class FakePerks final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId id) const override
        {
            const auto it = mPerks.find(id);
            return it == mPerks.end() ? nullptr : &it->second;
        }
        std::map<FormId, PerkRecord> mPerks;
    };

    struct ProgressionRig
    {
        progression::SkillTable mTable;
        FakePerks mPerks;
        bool mRecordsRead = true;
        std::vector<std::uint16_t> mLevels;
        std::unique_ptr<progression::ProgressionService> mService;

        ProgressionRig()
        {
            for (std::size_t s = 0; s < kSkillCount; ++s)
            {
                progression::SkillRecord r;
                r.mCommon.mId = 0x44C + static_cast<FormId>(s);
                r.mCommon.mEditorId = std::string(progression::skillEditorId(s));
                r.mHasSkillData = true;
                r.mUseMult = kUseMult;
                r.mImproveMult = 1.f;
                if (s == Slot::Smithing)
                {
                    progression::PerkNode root;
                    root.mIndex = 0;
                    root.mChildren = { 1 };
                    progression::PerkNode node;
                    node.mIndex = 1;
                    node.mPerk = kPerkSteel;
                    r.mNodes = { root, node };
                }
                mTable.add(r);
            }
            PerkRecord steel;
            steel.mCommon.mId = kPerkSteel;
            mPerks.mPerks[kPerkSteel] = steel;
            progression::ServiceHooks hooks;
            hooks.mInputs = [this](progression::Inputs& out) {
                if (!mRecordsRead)
                    return false;
                out.mSkills = &mTable;
                out.mPerks = &mPerks;
                out.mSettings.mSkillUseCurve = 2.f;
                return true;
            };
            hooks.mLevelUp = [this](std::uint16_t level) { mLevels.push_back(level); };
            mService = std::make_unique<progression::ProgressionService>(std::move(hooks));
        }
        progression::PlayerProgression& p() { return *mService->progression(); }
        void ready()
        {
            ASSERT_TRUE(mService->poll());
        }
        /// Buys kPerkSteel: a level up needs experience, then a perk point is spent.
        void buySteel()
        {
            p().useSkill(Slot::OneHanded, 1.0e6f);
            ASSERT_TRUE(mService->chooseLevelUp(progression::Vital::Health));
            ASSERT_TRUE(mService->buyPerk(kPerkSteel)) << mService->lastResult();
        }
    };

    struct World
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::unique_ptr<ProgressionRig> mProg;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<CraftingService> mCrafting;
        std::vector<std::pair<std::string, float>> mSkillUsed;
        std::vector<FormId> mPerks; ///< for the stand-in conditions hook (the unlinked service)
        std::vector<bool> mEnchantedAsked;
        bool mLinkProgression = false;
        bool mStandInConditions = false;
        float mBoost = 1.f;
        bool mIgnoreConditions = false;

        void build(const std::string& name)
        {
            mDir = std::filesystem::temp_directory_path() / (std::string("oe_wiring_") + name);
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            const Bytes all = wiringPlugin();
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            mProg = std::make_unique<ProgressionRig>();
            inventory::Hooks ihooks;
            ihooks.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            ihooks.mName = [](FormId id) { return nameOf(id); };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(ihooks), 100.f, 1);
            CraftingHooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            hooks.mName = [](FormId id) { return nameOf(id); };
            hooks.mEffectInfo = [](FormId) {
                EffectInfo info;
                info.mFound = true;
                info.mBaseCost = 10.f;
                return info;
            };
            hooks.mSkill = [](std::string_view) { return 15.f; };
            hooks.mSkillUsed
                = [this](std::string_view skill, float amount) { mSkillUsed.emplace_back(std::string(skill), amount); };
            if (mStandInConditions)
                hooks.mRecipeConditions = [this](std::span<const survey::Ctda> list, bool enchanted) {
                    mEnchantedAsked.push_back(enchanted);
                    return recipeConditionsHold(list, mPerks, 1, {}, enchanted);
                };
            if (mLinkProgression)
                linkProgression(hooks, *mProg->mService, mBoost);
            mCrafting = std::make_unique<CraftingService>(
                *mInventory, std::move(hooks), CraftingSettings(), mIgnoreConditions, false);
        }

        bool inputs(inventory::Inputs& out)
        {
            out.mPaths = { mPath };
            std::string err;
            (void)out.mOrder.add("Skyrim.esm", false, err);
            (void)out.mOrder.add("test.esp", false, err);
            return true;
        }

        static std::string nameOf(FormId id)
        {
            switch (id)
            {
                case kSword:
                    return "Iron Sword";
                case kEnchSword:
                    return "Enchanted Sword";
                case kArmor:
                    return "Iron Armor";
                case kIngot:
                    return "Iron Ingot";
                case kLeather:
                    return "Leather";
                case kIngA:
                    return "Ingredient A";
                case kIngB:
                    return "Ingredient B";
                default:
                    return {};
            }
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
        void open(FormId furniture)
        {
            mCrafting->openBench(7, furniture, "Bench");
            mCrafting->poll();
        }
        std::vector<FormId> visible()
        {
            std::vector<FormId> out;
            for (const Recipe* r : mCrafting->visibleRecipes())
                out.push_back(r->mId);
            return out;
        }
        bool visibleHas(FormId recipe)
        {
            const std::vector<FormId> v = visible();
            return std::find(v.begin(), v.end(), recipe) != v.end();
        }

        ~World()
        {
            mCrafting.reset();
            mInventory.reset();
            mProg.reset();
            std::error_code ec;
            if (!mDir.empty())
                std::filesystem::remove_all(mDir, ec);
        }
    };

    // ---- names ------------------------------------------------------------------------------------------------------

    TEST(SkyrimWiringNames, everySkillHasItsScriptNameAndTheOthersAreNone)
    {
        using progression::slotOfActorValueName;
        EXPECT_EQ(slotOfActorValueName("onehanded"), Slot::OneHanded);
        EXPECT_EQ(slotOfActorValueName("twohanded"), Slot::TwoHanded);
        EXPECT_EQ(slotOfActorValueName("marksman"), Slot::Archery);
        EXPECT_EQ(slotOfActorValueName("archery"), Slot::Archery);
        EXPECT_EQ(slotOfActorValueName("block"), Slot::Block);
        EXPECT_EQ(slotOfActorValueName("smithing"), Slot::Smithing);
        EXPECT_EQ(slotOfActorValueName("heavyarmor"), Slot::HeavyArmor);
        EXPECT_EQ(slotOfActorValueName("lightarmor"), Slot::LightArmor);
        EXPECT_EQ(slotOfActorValueName("pickpocket"), Slot::Pickpocket);
        EXPECT_EQ(slotOfActorValueName("lockpicking"), Slot::Lockpicking);
        EXPECT_EQ(slotOfActorValueName("sneak"), Slot::Sneak);
        EXPECT_EQ(slotOfActorValueName("alchemy"), Slot::Alchemy);
        EXPECT_EQ(slotOfActorValueName("speechcraft"), Slot::Speechcraft);
        EXPECT_EQ(slotOfActorValueName("speech"), Slot::Speechcraft);
        EXPECT_EQ(slotOfActorValueName("alteration"), Slot::Alteration);
        EXPECT_EQ(slotOfActorValueName("conjuration"), Slot::Conjuration);
        EXPECT_EQ(slotOfActorValueName("destruction"), Slot::Destruction);
        EXPECT_EQ(slotOfActorValueName("illusion"), Slot::Illusion); // the AVIF is AVMysticism: the name is not
        EXPECT_EQ(slotOfActorValueName("restoration"), Slot::Restoration);
        EXPECT_EQ(slotOfActorValueName("enchanting"), Slot::Enchanting);
        for (const char* other : { "", "health", "magicka", "stamina", "OneHanded", "level", "mysticism" })
            EXPECT_EQ(slotOfActorValueName(other), kSkillCount) << other;
    }

    TEST(SkyrimWiringNames, theThreeCraftingSkillsTrainTheirSlots)
    {
        EXPECT_EQ(skillSlotOfCraftingName("alchemy"), Slot::Alchemy);
        EXPECT_EQ(skillSlotOfCraftingName("smithing"), Slot::Smithing);
        EXPECT_EQ(skillSlotOfCraftingName("enchanting"), Slot::Enchanting);
        EXPECT_EQ(skillSlotOfCraftingName("sneak"), kSkillCount);
        EXPECT_EQ(skillSlotOfCraftingName(""), kSkillCount);
    }

    // ---- Papyrus ----------------------------------------------------------------------------------------------------

    TEST(SkyrimWiringPapyrus, aSkillReadsTheRealLevelAndNothingElseDoes)
    {
        ProgressionRig rig;
        float v = -1.f;
        rig.mRecordsRead = false;
        EXPECT_FALSE(progression::skillActorValue(*rig.mService, "onehanded", v)); // not ready
        rig.mRecordsRead = true;
        rig.ready();
        ASSERT_TRUE(progression::skillActorValue(*rig.mService, "onehanded", v));
        EXPECT_FLOAT_EQ(v, 15.f);
        rig.mService->skillUsed(Slot::OneHanded, 1.0e6f);
        ASSERT_TRUE(progression::skillActorValue(*rig.mService, "onehanded", v));
        EXPECT_GT(v, 15.f);
        EXPECT_FLOAT_EQ(v, static_cast<float>(rig.p().skill(Slot::OneHanded)));
        ASSERT_TRUE(progression::skillActorValue(*rig.mService, "marksman", v));
        EXPECT_FLOAT_EQ(v, 15.f);
        ASSERT_TRUE(progression::skillActorValue(*rig.mService, "illusion", v));
        EXPECT_FALSE(progression::skillActorValue(*rig.mService, "health", v));
        EXPECT_FALSE(progression::skillActorValue(*rig.mService, "", v));
    }

    // ---- recipe conditions (pure) -----------------------------------------------------------------------------------

    TEST(SkyrimWiringConditions, hasPerkHoldsOnlyWithThePerk)
    {
        const std::vector<survey::Ctda> list = { makeCtda(0x00, 1.f, 448, kPerkSteel) };
        const std::vector<FormId> with = { kPerkSteel, kPerkArcane };
        const std::vector<FormId> without = { kPerkArcane };
        EXPECT_TRUE(recipeConditionsPerkOnly(list));
        EXPECT_TRUE(recipeConditionsHold(list, with, 1, {}, false));
        EXPECT_FALSE(recipeConditionsHold(list, without, 1, {}, false));
        EXPECT_FALSE(recipeConditionsHold(list, {}, 1, {}, false));
    }

    TEST(SkyrimWiringConditions, theTemperingPairIsNotEnchantedOrArcaneBlacksmithWithEitherOrFlagOnTheLast)
    {
        for (const std::uint8_t last : { std::uint8_t(0x00), std::uint8_t(0x01) })
        {
            const std::vector<survey::Ctda> list
                = { makeCtda(0x21, 1.f, 659, 0), makeCtda(last, 1.f, 448, kPerkArcane) };
            const std::vector<FormId> arcane = { kPerkArcane };
            EXPECT_TRUE(recipeConditionsPerkOnly(list));
            EXPECT_TRUE(recipeConditionsHold(list, {}, 1, {}, false)); // a plain item needs no perk
            EXPECT_FALSE(recipeConditionsHold(list, {}, 1, {}, true)); // an enchanted one does
            EXPECT_TRUE(recipeConditionsHold(list, arcane, 1, {}, true));
            EXPECT_TRUE(recipeConditionsHold(list, arcane, 1, {}, false));
        }
    }

    TEST(SkyrimWiringConditions, aSkillThresholdReadsTheValuesAndTheListMustBeKnownFunctionsOnly)
    {
        const std::vector<survey::Ctda> list = { makeCtda(0x60, 30.f, 277, 6) }; // GetBaseActorValue(One-Handed) >= 30
        const std::vector<std::pair<std::uint32_t, float>> low = { { 6, 29.f } }, high = { { 6, 30.f } };
        EXPECT_TRUE(recipeConditionsPerkOnly(list));
        EXPECT_FALSE(recipeConditionsHold(list, {}, 1, low, false));
        EXPECT_TRUE(recipeConditionsHold(list, {}, 1, high, false));
        EXPECT_TRUE(recipeConditionsHold({}, {}, 1, {}, false)); // no conditions hold
        EXPECT_TRUE(recipeConditionsPerkOnly({}));
        const std::vector<survey::Ctda> global = { makeCtda(0x00, 1.f, 74, kSelf | 0xD00) };
        EXPECT_FALSE(recipeConditionsPerkOnly(global));
        const std::vector<survey::Ctda> mixed = { makeCtda(0x00, 1.f, 448, kPerkSteel), makeCtda(0x00, 1.f, 74, 1) };
        EXPECT_FALSE(recipeConditionsPerkOnly(mixed));
    }

    // ---- the recipe list and the refusals with a conditions hook ---------------------------------------------------

    struct WithHook : testing::Test
    {
        World w;
        void SetUp() override
        {
            w.mStandInConditions = true;
            w.build(testing::UnitTest::GetInstance()->current_test_info()->name());
            w.waitReady();
        }
    };

    TEST_F(WithHook, aPerkGatedForgeRecipeShowsOnlyWithThePerkAndIsRefusedWithout)
    {
        w.give(kIngot, 5);
        w.open(kFurnForge);
        EXPECT_TRUE(w.visibleHas(kRecForge)); // no conditions
        EXPECT_FALSE(w.visibleHas(kRecPerkForge));
        EXPECT_EQ(w.mCrafting->craft(kRecPerkForge), Status::UnknownRecipe);
        EXPECT_EQ(w.mInventory->model().count(kPlayerOwner, kIngot), 5);
        w.mPerks = { kPerkSteel };
        EXPECT_TRUE(w.visibleHas(kRecPerkForge));
        EXPECT_EQ(w.mCrafting->craft(kRecPerkForge), Status::Ok);
        EXPECT_EQ(w.mInventory->model().count(kPlayerOwner, kIngot), 4);
    }

    TEST_F(WithHook, aRecipeWithAGlobalConditionKeepsTheOldRuleHidden)
    {
        w.mPerks = { kPerkSteel, kPerkArcane };
        w.give(kIngot, 5);
        w.open(kFurnForge);
        EXPECT_FALSE(w.visibleHas(kRecGlobalForge));
        EXPECT_FALSE(w.visibleHas(kRecCond)); // zero-function conditions: not perk-only either
        EXPECT_EQ(w.mCrafting->craft(kRecGlobalForge), Status::UnknownRecipe);
    }

    TEST_F(WithHook, temperingAPlainItemNeedsNoPerkAnEnchantedOneNeedsArcaneBlacksmith)
    {
        w.give(kIngot, 5);
        w.give(kSword, 1);
        w.give(kEnchSword, 1);
        w.open(kFurnWheel);
        EXPECT_TRUE(w.visibleHas(kRecTemperPlain));
        EXPECT_TRUE(w.visibleHas(kRecTemperEnch) == false); // the held enchanted sword makes fn 659 true: no perk
        EXPECT_EQ(w.mCrafting->temper(kRecTemperEnch), Status::UnknownRecipe);
        EXPECT_EQ(w.mCrafting->temper(kRecTemperPlain), Status::Ok);
        w.mPerks = { kPerkArcane };
        EXPECT_TRUE(w.visibleHas(kRecTemperEnch));
        EXPECT_EQ(w.mCrafting->temper(kRecTemperEnch), Status::Ok);
        ASSERT_FALSE(w.mEnchantedAsked.empty());
        EXPECT_NE(std::find(w.mEnchantedAsked.begin(), w.mEnchantedAsked.end(), true), w.mEnchantedAsked.end());
        EXPECT_NE(std::find(w.mEnchantedAsked.begin(), w.mEnchantedAsked.end(), false), w.mEnchantedAsked.end());
    }

    TEST_F(WithHook, theEnchantedRecipeIsShownWhenNoEnchantedItemIsHeld)
    {
        w.give(kIngot, 5);
        w.open(kFurnWheel);
        EXPECT_TRUE(w.visibleHas(kRecTemperEnch)); // nothing to improve yet: the flag is false, so it shows
    }

    TEST(SkyrimWiringIgnore, theSettingOverridesEveryCondition)
    {
        World w;
        w.mStandInConditions = true;
        w.mIgnoreConditions = true;
        w.build("ignore");
        w.waitReady();
        w.give(kIngot, 5);
        w.give(kEnchSword, 1);
        w.open(kFurnSky);
        EXPECT_TRUE(w.visibleHas(kRecPerkForge));
        EXPECT_TRUE(w.visibleHas(kRecGlobalForge));
        EXPECT_EQ(w.mCrafting->craft(kRecPerkForge), Status::Ok);
        w.mCrafting->close();
        w.open(kFurnWheel);
        EXPECT_TRUE(w.visibleHas(kRecTemperEnch));
        EXPECT_EQ(w.mCrafting->temper(kRecTemperEnch), Status::Ok);
    }

    TEST(SkyrimWiringNoHook, withoutAHookTheOldRuleStays)
    {
        World w;
        w.build("nohook");
        w.waitReady();
        w.give(kIngot, 5);
        w.open(kFurnForge);
        EXPECT_FALSE(w.visibleHas(kRecPerkForge)); // forge recipe with conditions: hidden as before
        w.mCrafting->close();
        w.open(kFurnWheel);
        EXPECT_TRUE(w.visibleHas(kRecTemperEnch)); // tempering is shown, as before
    }

    // ---- crafting linked to a real progression ----------------------------------------------------------------------

    struct Linked : testing::Test
    {
        World w;
        void SetUp() override
        {
            w.mLinkProgression = true;
            w.build(testing::UnitTest::GetInstance()->current_test_info()->name());
            w.waitReady();
            w.mProg->ready();
        }
        float xp(std::size_t slot) { return w.mProg->p().skillXp(slot); }
    };

    TEST_F(Linked, aBrewTrainsAlchemyAndTheEarlierHookStillRuns)
    {
        w.give(kIngA, 1);
        w.give(kIngB, 1);
        w.open(kFurnAlchemy);
        const float before = xp(Slot::Alchemy);
        const FormId a[] = { kIngA, kIngB };
        ASSERT_EQ(w.mCrafting->brew(a), Status::Ok);
        ASSERT_EQ(w.mSkillUsed.size(), 1u);
        EXPECT_EQ(w.mSkillUsed[0].first, "alchemy");
        EXPECT_GT(xp(Slot::Alchemy), before);
        EXPECT_NEAR(xp(Slot::Alchemy) - before, kUseMult * w.mSkillUsed[0].second, 1e-3f);
        EXPECT_FLOAT_EQ(xp(Slot::Smithing), 0.f);
    }

    TEST_F(Linked, forgingTemperingAndEnchantingTrainSmithingAndEnchanting)
    {
        w.give(kIngot, 9);
        w.give(kLeather, 3);
        w.open(kFurnForge);
        ASSERT_EQ(w.mCrafting->craft(kRecForge), Status::Ok);
        const float smithing = xp(Slot::Smithing);
        EXPECT_GT(smithing, 0.f);
        w.mCrafting->close();
        w.open(kFurnWheel);
        ASSERT_EQ(w.mCrafting->temper(kRecTemperSword), Status::Ok);
        EXPECT_GT(xp(Slot::Smithing), smithing);
        w.mCrafting->close();
        w.give(kEnchSword, 1);
        w.open(kFurnEnchant);
        ASSERT_EQ(w.mCrafting->disenchant(kEnchSword), Status::Ok);
        EXPECT_GT(xp(Slot::Enchanting), 0.f);
        EXPECT_FLOAT_EQ(xp(Slot::Alchemy), 0.f);
    }

    TEST(SkyrimWiringBoost, theTestMultiplierScalesWhatACraftTrains)
    {
        World plain, boosted;
        boosted.mBoost = 3.f;
        for (World* w : { &plain, &boosted })
        {
            w->mLinkProgression = true;
            w->build(w == &plain ? "boost1" : "boost3");
            w->waitReady();
            w->mProg->ready();
            w->give(kIngA, 1);
            w->give(kIngB, 1);
            w->open(kFurnAlchemy);
            const FormId a[] = { kIngA, kIngB };
            ASSERT_EQ(w->mCrafting->brew(a), Status::Ok);
        }
        const float one = plain.mProg->p().skillXp(Slot::Alchemy);
        ASSERT_GT(one, 0.f);
        EXPECT_NEAR(boosted.mProg->p().skillXp(Slot::Alchemy), 3.f * one, 1e-3f);
    }

    TEST_F(Linked, theFormulasReadTheRealSkillOnceTheProgressionIsReady)
    {
        EXPECT_FLOAT_EQ(w.mCrafting->skill("alchemy"), 15.f);
        w.mProg->mService->skillUsed(Slot::Alchemy, 1.0e6f);
        const float real = static_cast<float>(w.mProg->p().skill(Slot::Alchemy));
        ASSERT_GT(real, 15.f);
        EXPECT_FLOAT_EQ(w.mCrafting->skill("alchemy"), real);
        EXPECT_FLOAT_EQ(w.mCrafting->skill("smithing"), 15.f);
        EXPECT_FLOAT_EQ(w.mCrafting->skill("enchanting"), 15.f);
    }

    TEST(SkyrimWiringNotReady, aProgressionThatIsNotReadyChangesNothing)
    {
        World w;
        w.mLinkProgression = true;
        w.build("notready");
        w.mProg->mRecordsRead = false;
        w.waitReady();
        EXPECT_FLOAT_EQ(w.mCrafting->skill("alchemy"), 15.f); // the earlier hook's number
        w.give(kIngA, 1);
        w.give(kIngB, 1);
        w.open(kFurnAlchemy);
        const FormId a[] = { kIngA, kIngB };
        EXPECT_EQ(w.mCrafting->brew(a), Status::Ok);
        EXPECT_EQ(w.mSkillUsed.size(), 1u); // the earlier hook ran
        w.mProg->mRecordsRead = true;
        w.mProg->ready();
        EXPECT_FLOAT_EQ(w.mProg->p().skillXp(Slot::Alchemy), 0.f); // the training before ready was lost, not queued
    }

    TEST_F(Linked, aPerkTheProgressionBuysOpensItsRecipesAndOnlyThen)
    {
        w.give(kIngot, 5);
        w.open(kFurnForge);
        EXPECT_FALSE(w.visibleHas(kRecPerkForge));
        EXPECT_EQ(w.mCrafting->craft(kRecPerkForge), Status::UnknownRecipe);
        w.mProg->buySteel();
        EXPECT_TRUE(w.visibleHas(kRecPerkForge));
        EXPECT_EQ(w.mCrafting->craft(kRecPerkForge), Status::Ok);
        // a global condition is still not answered
        EXPECT_FALSE(w.visibleHas(kRecGlobalForge));
    }

    TEST_F(Linked, theArcaneBlacksmithGateFollowsTheBoughtPerkToo)
    {
        w.give(kIngot, 5);
        w.give(kEnchSword, 1);
        w.open(kFurnWheel);
        EXPECT_FALSE(w.visibleHas(kRecTemperEnch)); // enchanted item, no Arcane Blacksmith (not even a perk record)
        EXPECT_TRUE(w.visibleHas(kRecTemperPlain));
    }
}

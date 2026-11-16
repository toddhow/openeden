// Task 0199 (crafting; milestone M-S7): the crafting session over the inventory, the operations (brew, eat, craft,
// temper, disenchant, enchant), the combat read of made gear, the save chunk with the inventory, and the
// "skyrimse.crafting" bindings over a stub bindings service. Generated plugin, no game data. Written before the code,
// run red against stubs.
#include <game_skyrimse/crafting_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
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
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            EXPECT_EQ(args.size(), it->second.params.size()) << name;
            return it->second.handler->call(args);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }

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

    struct World
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<CraftingService> mCrafting;
        StubBindings mBindings;
        std::vector<std::string> mLog;
        std::vector<std::pair<std::string, float>> mSkillUsed;
        std::vector<float> mHealed;
        float mSkill = 15.f;
        bool mIgnoreConditions = false;
        bool mTestBench = false;

        void build(const std::string& name)
        {
            mDir = std::filesystem::temp_directory_path() / (std::string("oe_craft_") + name);
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            const Bytes all = testPlugin();
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            mInventory = makeInventory();
            mCrafting = makeCrafting(*mInventory);
            EXPECT_TRUE(mInventory->declare(mBindings).ok);
            EXPECT_TRUE(mCrafting->declare(mBindings).ok);
        }

        std::unique_ptr<inventory::InventoryService> makeInventory()
        {
            inventory::Hooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            hooks.mName = [](FormId id) { return nameOf(id); };
            hooks.mHealthOf = [](FormId effect, float magnitude, std::uint32_t duration) {
                return effect == kEffHeal && duration == 0 ? magnitude : 0.f;
            };
            hooks.mApplyHealth = [this](float amount) {
                mHealed.push_back(amount);
                return true;
            };
            return std::make_unique<inventory::InventoryService>(std::move(hooks), 100.f, 1);
        }

        std::unique_ptr<CraftingService> makeCrafting(inventory::InventoryService& inv)
        {
            CraftingHooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            hooks.mName = [](FormId id) { return nameOf(id); };
            hooks.mEffectInfo = [](FormId effect) {
                EffectInfo info;
                info.mFound = true;
                info.mBaseCost = effect == kEffHeal ? 10.f : 5.f;
                info.mHostile = effect == kEffDamage || effect == kEffFire;
                info.mDamageHealth = info.mHostile;
                return info;
            };
            hooks.mSkill = [this](std::string_view) { return mSkill; };
            hooks.mSkillUsed
                = [this](std::string_view skill, float amount) { mSkillUsed.emplace_back(std::string(skill), amount); };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            return std::make_unique<CraftingService>(
                inv, std::move(hooks), CraftingSettings(), mIgnoreConditions, mTestBench);
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
                case kIngC:
                    return "Ingredient C";
                case kIngD:
                    return "Ingredient D";
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
        std::int32_t held(FormId item) const { return mInventory->model().count(kPlayerOwner, item); }
        void open(FormId furniture)
        {
            mCrafting->openBench(7, furniture, "Bench");
            mCrafting->poll();
        }

        std::string text(const std::string& fn, std::vector<BindingValue> args = {})
        {
            return std::string(mBindings.call(fn, std::move(args)).value.text);
        }
        std::int32_t integer(const std::string& fn, std::vector<BindingValue> args = {})
        {
            return mBindings.call(fn, std::move(args)).value.integer;
        }
        bool boolean(const std::string& fn, std::vector<BindingValue> args = {})
        {
            return mBindings.call(fn, std::move(args)).value.boolean;
        }
        std::vector<std::string> strings(const std::string& fn, std::vector<BindingValue> args = {})
        {
            const openeden::sdk::BindingResult r = mBindings.call(fn, std::move(args));
            std::vector<std::string> out;
            for (std::size_t i = 0; i < r.value.count; ++i)
                out.emplace_back(r.value.elements[i].text);
            return out;
        }

        ~World()
        {
            mCrafting.reset();
            mInventory.reset();
            std::error_code ec;
            if (!mDir.empty())
                std::filesystem::remove_all(mDir, ec);
        }
    };

    struct CraftingServiceFixture : testing::Test, World
    {
        void SetUp() override { build(testing::UnitTest::GetInstance()->current_test_info()->name()); }
    };

    FormId potionOf(const World& w)
    {
        for (const Synthetic& s : w.mCrafting->state().all())
            if (s.mKind == SyntheticKind::Potion)
                return s.mId;
        return kNullFormId;
    }

    // ---- the session
    // -------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, thereIsNoSessionAtFirstAndNothingWorks)
    {
        EXPECT_EQ(mCrafting->session(), CraftingService::State::None);
        EXPECT_EQ(text("state"), "none");
        const FormId two[] = { kIngA, kIngB };
        EXPECT_EQ(mCrafting->brew(two), Status::NotReady);
        waitReady();
        EXPECT_EQ(mCrafting->brew(two), Status::NoBench);
        EXPECT_EQ(mCrafting->craft(kRecForge), Status::NoBench);
    }

    TEST_F(CraftingServiceFixture, aSessionIsPendingUntilTheRecordsAreReadThenReady)
    {
        mCrafting->openBench(7, kFurnAlchemy, "Alchemy Lab");
        EXPECT_EQ(mCrafting->session(), CraftingService::State::Pending);
        EXPECT_EQ(text("state"), "pending");
        waitReady();
        for (int i = 0; i < 10 && mCrafting->session() == CraftingService::State::Pending; ++i)
            mCrafting->poll();
        EXPECT_EQ(mCrafting->session(), CraftingService::State::Ready);
        EXPECT_EQ(text("state"), "ready");
        EXPECT_EQ(text("bench"), "alchemy");
        EXPECT_EQ(text("bench_name"), "Alchemy Lab");
    }

    TEST_F(CraftingServiceFixture, aFurnitureWithNoCraftingKeywordIsRefusedWithAReason)
    {
        waitReady();
        open(kFurnChair);
        EXPECT_EQ(mCrafting->session(), CraftingService::State::Refused);
        EXPECT_FALSE(mCrafting->reason().empty());
        EXPECT_EQ(text("state"), "refused");
        EXPECT_EQ(text("reason"), mCrafting->reason());
        open(kSword);
        EXPECT_EQ(mCrafting->session(), CraftingService::State::Refused);
    }

    TEST_F(CraftingServiceFixture, aSkyforgeOffersBothTheForgeAndTheSkyforge)
    {
        waitReady();
        open(kFurnSky);
        ASSERT_EQ(mCrafting->session(), CraftingService::State::Ready);
        EXPECT_EQ(text("bench"), "forge,skyforge");
    }

    TEST_F(CraftingServiceFixture, closingEndsTheSession)
    {
        waitReady();
        open(kFurnForge);
        ASSERT_EQ(mCrafting->session(), CraftingService::State::Ready);
        mCrafting->close();
        EXPECT_EQ(mCrafting->session(), CraftingService::State::None);
        EXPECT_EQ(text("bench"), "");
    }

    TEST_F(CraftingServiceFixture, theTestBenchNeedsItsSetting)
    {
        waitReady();
        mBindings.call("open_kind", { str("alchemy") });
        EXPECT_EQ(mCrafting->session(), CraftingService::State::None);
        World other;
        other.mTestBench = true;
        other.build("testbench");
        other.waitReady();
        other.mBindings.call("open_kind", { str("forge") });
        EXPECT_EQ(other.mCrafting->session(), CraftingService::State::Ready);
        EXPECT_EQ(other.text("bench"), "forge");
        other.mBindings.call("open_kind", { str("nonsense") });
        EXPECT_EQ(other.text("bench"), "forge"); // an unknown kind changes nothing
    }

    // ---- alchemy
    // -----------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, brewingTwoIngredientsMakesAPotionOfTheirSharedEffects)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 2);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        EXPECT_EQ(held(kIngA), 1);
        EXPECT_EQ(held(kIngB), 0);
        const FormId potion = potionOf(*this);
        ASSERT_NE(potion, kNullFormId);
        EXPECT_EQ(held(potion), 1);
        const inventory::ItemStat* stat = mInventory->table().item(potion);
        ASSERT_NE(stat, nullptr);
        EXPECT_EQ(stat->mCategory, inventory::Category::Potion);
        EXPECT_EQ(stat->mName, "Potion of Restore Health");
        ASSERT_EQ(stat->mEffects.size(), 2u);
        EXPECT_EQ(stat->mEffects[0].mEffect, kEffHeal);
        EXPECT_NEAR(stat->mEffects[0].mMagnitude, 5.f * 1.075f, 1e-4f);
        EXPECT_EQ(stat->mEffects[0].mDuration, 0u);
        EXPECT_EQ(stat->mEffects[1].mEffect, kEffFortify);
        EXPECT_NEAR(stat->mEffects[1].mMagnitude, 12.f * 1.075f, 1e-4f);
        EXPECT_EQ(stat->mEffects[1].mDuration, 60u);
        EXPECT_GE(stat->mValue, 1);
        EXPECT_EQ(text("last_result"), "ok");
    }

    TEST_F(CraftingServiceFixture, theSkillMakesAStrongerPotion)
    {
        mSkill = 100.f;
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        EXPECT_NEAR(mInventory->table().item(potionOf(*this))->mEffects[0].mMagnitude, 7.5f, 1e-4f);
    }

    TEST_F(CraftingServiceFixture, theNameOfAPotionIsItsFirstEffectAndAHostileOneIsAPoison)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngC, 1);
        const FormId two[] = { kIngA, kIngC };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        const FormId potion = potionOf(*this);
        EXPECT_EQ(mInventory->table().item(potion)->mName, "Poison of Damage Health");
        EXPECT_TRUE(mCrafting->state().find(potion)->mPoison);
    }

    TEST_F(CraftingServiceFixture, threeIngredientsShareWhatAnyTwoHave)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        give(kIngC, 1);
        const FormId three[] = { kIngA, kIngB, kIngC };
        ASSERT_EQ(mCrafting->brew(three), Status::Ok);
        const inventory::ItemStat* stat = mInventory->table().item(potionOf(*this));
        ASSERT_NE(stat, nullptr);
        ASSERT_EQ(stat->mEffects.size(), 4u); // heal, fortify (A, B), damage, slow (A, C)
        EXPECT_EQ(stat->mEffects[0].mEffect, kEffHeal);
        EXPECT_EQ(stat->mEffects[2].mEffect, kEffDamage);
    }

    TEST_F(CraftingServiceFixture, brewingTheSamePotionAgainStacksIt)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 2);
        give(kIngB, 2);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        EXPECT_EQ(mCrafting->state().all().size(), 1u);
        EXPECT_EQ(held(potionOf(*this)), 2);
    }

    TEST_F(CraftingServiceFixture, brewingTeachesTheSharedEffectsOnly)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        EXPECT_TRUE(mCrafting->state().knowsEffect(kEffHeal));
        EXPECT_TRUE(mCrafting->state().knowsEffect(kEffFortify));
        EXPECT_FALSE(mCrafting->state().knowsEffect(kEffDamage));
        EXPECT_FALSE(mCrafting->state().knowsEffect(kEffInvis));
    }

    TEST_F(CraftingServiceFixture, aBrewIsRefusedAndChangesNothingWhenItCannotWork)
    {
        waitReady();
        give(kIngA, 1);
        give(kIngB, 1);
        give(kIngD, 1);
        const FormId ab[] = { kIngA, kIngB };
        EXPECT_EQ(mCrafting->brew(ab), Status::NoBench);
        open(kFurnForge);
        EXPECT_EQ(mCrafting->brew(ab), Status::NoBench); // a forge is no alchemy lab
        open(kFurnAlchemy);
        const FormId one[] = { kIngA };
        EXPECT_EQ(mCrafting->brew(one), Status::TooFewIngredients);
        const FormId twice[] = { kIngA, kIngA };
        EXPECT_EQ(mCrafting->brew(twice), Status::TooFewIngredients);
        const FormId none[] = { kIngA, kIngD };
        EXPECT_EQ(mCrafting->brew(none), Status::NoSharedEffect);
        const FormId missing[] = { kIngA, kIngC };
        EXPECT_EQ(mCrafting->brew(missing), Status::NotHeld);
        const FormId notIngredient[] = { kIngA, kSword };
        EXPECT_EQ(mCrafting->brew(notIngredient), Status::UnknownItem);
        const FormId many[] = { kIngA, kIngB, kIngD, kIngA };
        EXPECT_EQ(mCrafting->brew(many), Status::TooFewIngredients);
        EXPECT_EQ(held(kIngA), 1);
        EXPECT_EQ(held(kIngB), 1);
        EXPECT_EQ(held(kIngD), 1);
        EXPECT_TRUE(mCrafting->state().all().empty());
        EXPECT_TRUE(mCrafting->state().knownEffects().empty());
    }

    TEST_F(CraftingServiceFixture, theSkillHookIsToldAfterABrewAndOnlyThen)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngD };
        EXPECT_NE(mCrafting->brew(two), Status::Ok);
        EXPECT_TRUE(mSkillUsed.empty());
        const FormId ok[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(ok), Status::Ok);
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "alchemy");
        EXPECT_GT(mSkillUsed[0].second, 0.f);
    }

    TEST_F(CraftingServiceFixture, aBrewedPotionIsDrunkThroughTheUsePath)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        const FormId potion = potionOf(*this);
        EXPECT_EQ(mInventory->use(potion), inventory::InventoryService::UseStatus::Ok);
        ASSERT_EQ(mHealed.size(), 1u);
        EXPECT_NEAR(mHealed[0], 5.f * 1.075f, 1e-4f);
        EXPECT_EQ(held(potion), 0);
    }

    TEST_F(CraftingServiceFixture, eatingAnIngredientTeachesItsFirstUnknownEffectAndUsesItUp)
    {
        waitReady();
        give(kIngA, 5);
        EXPECT_EQ(mCrafting->eat(kIngA), Status::Ok);
        EXPECT_EQ(held(kIngA), 4);
        EXPECT_TRUE(mCrafting->state().knowsEffect(kEffHeal));
        EXPECT_FALSE(mCrafting->state().knowsEffect(kEffFortify));
        EXPECT_EQ(mCrafting->eat(kIngA), Status::Ok);
        EXPECT_TRUE(mCrafting->state().knowsEffect(kEffFortify));
        EXPECT_EQ(mCrafting->eat(kIngA), Status::Ok);
        EXPECT_EQ(mCrafting->eat(kIngA), Status::Ok);
        EXPECT_EQ(held(kIngA), 1);
        EXPECT_EQ(mCrafting->eat(kIngA), Status::NoEffect); // all four known: nothing is learned, nothing is eaten
        EXPECT_EQ(held(kIngA), 1);
    }

    TEST_F(CraftingServiceFixture, eatingSomethingElseOrNothingIsRefused)
    {
        waitReady();
        give(kSword, 1);
        EXPECT_EQ(mCrafting->eat(kIngB), Status::NotHeld);
        EXPECT_EQ(mCrafting->eat(kSword), Status::UnknownItem);
        EXPECT_EQ(mCrafting->eat(kNullFormId), Status::UnknownItem);
    }

    TEST_F(CraftingServiceFixture, unknownEffectsShowAsAQuestionMarkUntilLearned)
    {
        waitReady();
        give(kIngA, 2);
        give(kIngB, 1);
        EXPECT_EQ(text("ingredient_effects", { str(inventory::formatId(kIngA)) }), "?, ?, ?, ?");
        EXPECT_EQ(
            text("preview", { str(inventory::formatId(kIngA)), str(inventory::formatId(kIngB)), str("") }), "?, ?");
        ASSERT_EQ(mCrafting->eat(kIngA), Status::Ok);
        EXPECT_EQ(text("ingredient_effects", { str(inventory::formatId(kIngA)) }), "Restore Health, ?, ?, ?");
        EXPECT_EQ(text("preview", { str(inventory::formatId(kIngA)), str(inventory::formatId(kIngB)), str("") }),
            "Restore Health, ?");
        EXPECT_EQ(text("preview", { str(inventory::formatId(kIngA)), str(inventory::formatId(kIngD)), str("") }),
            "no shared effects");
    }

    TEST_F(CraftingServiceFixture, theIngredientListHoldsWhatThePlayerHas)
    {
        waitReady();
        give(kIngB, 1);
        give(kIngA, 1);
        give(kSword, 1);
        EXPECT_EQ(strings("ingredients"),
            (std::vector<std::string>{ inventory::formatId(kIngA), inventory::formatId(kIngB) }));
    }

    // ---- smithing
    // ----------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, theForgeMakesTheItemAndUsesTheMaterials)
    {
        waitReady();
        open(kFurnForge);
        give(kIngot, 5);
        give(kLeather, 1);
        EXPECT_TRUE(boolean("recipe_ok", { str(inventory::formatId(kRecForge)) }));
        ASSERT_EQ(mCrafting->craft(kRecForge), Status::Ok);
        EXPECT_EQ(held(kSword), 1);
        EXPECT_EQ(held(kIngot), 3);
        EXPECT_EQ(held(kLeather), 0);
        EXPECT_FALSE(boolean("recipe_ok", { str(inventory::formatId(kRecForge)) }));
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "smithing");
    }

    TEST_F(CraftingServiceFixture, aRecipeWithMissingMaterialsChangesNothing)
    {
        waitReady();
        open(kFurnForge);
        give(kIngot, 1);
        give(kLeather, 1);
        EXPECT_EQ(mCrafting->craft(kRecForge), Status::MissingItems);
        EXPECT_EQ(held(kIngot), 1);
        EXPECT_EQ(held(kLeather), 1);
        EXPECT_EQ(held(kSword), 0);
        EXPECT_TRUE(mSkillUsed.empty());
    }

    TEST_F(CraftingServiceFixture, aRecipeNeedsItsOwnBenchAndAnExistingItem)
    {
        waitReady();
        give(kIngot, 9);
        give(kLeather, 9);
        EXPECT_EQ(mCrafting->craft(kRecForge), Status::NoBench);
        open(kFurnWheel);
        EXPECT_EQ(mCrafting->craft(kRecForge), Status::NoBench); // a grindstone is no forge
        open(kFurnForge);
        EXPECT_EQ(mCrafting->craft(kRecSky), Status::NoBench); // a forge is no skyforge
        EXPECT_EQ(mCrafting->craft(kSelf | 0xEEE), Status::UnknownRecipe);
        EXPECT_EQ(mCrafting->craft(kRecBad), Status::UnknownItem); // its result is no item
        EXPECT_EQ(mCrafting->craft(kRecTemperSword), Status::NoBench); // a tempering recipe is not a craft
        EXPECT_EQ(held(kIngot), 9);
    }

    TEST_F(CraftingServiceFixture, theSkyforgeCraftsItsRecipes)
    {
        waitReady();
        open(kFurnSky);
        give(kIngot, 3);
        ASSERT_EQ(mCrafting->craft(kRecSky), Status::Ok);
        EXPECT_EQ(held(kArmor), 1);
        EXPECT_EQ(held(kIngot), 0);
    }

    TEST_F(CraftingServiceFixture, theRecipeListHidesConditionsAndItemsThatDoNotExist)
    {
        waitReady();
        open(kFurnForge);
        EXPECT_EQ(strings("recipes"), (std::vector<std::string>{ inventory::formatId(kRecForge) }));
        EXPECT_EQ(text("recipe_name", { str(inventory::formatId(kRecForge)) }), "Iron Sword");
        EXPECT_EQ(text("recipe_needs", { str(inventory::formatId(kRecForge)) }), "Iron Ingot x2 (0), Leather x1 (0)");
        EXPECT_FALSE(boolean("recipe_temper", { str(inventory::formatId(kRecForge)) }));
        World other;
        other.mIgnoreConditions = true;
        other.build("ignorecond");
        other.waitReady();
        other.open(kFurnForge);
        EXPECT_EQ(other.strings("recipes"),
            (std::vector<std::string>{ inventory::formatId(kRecForge), inventory::formatId(kRecCond) }));
    }

    TEST_F(CraftingServiceFixture, aSkyforgeListsTheForgeAndTheSkyforgeRecipes)
    {
        waitReady();
        open(kFurnSky);
        EXPECT_EQ(strings("recipes"),
            (std::vector<std::string>{ inventory::formatId(kRecForge), inventory::formatId(kRecSky) }));
    }

    // ---- tempering
    // ---------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, temperingMakesABetterItemInPlaceOfTheOld)
    {
        waitReady();
        open(kFurnWheel);
        give(kSword, 1);
        give(kIngot, 2);
        EXPECT_TRUE(boolean("recipe_temper", { str(inventory::formatId(kRecTemperSword)) }));
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        EXPECT_EQ(held(kSword), 0);
        EXPECT_EQ(held(kIngot), 1);
        const Synthetic* made = nullptr;
        for (const Synthetic& s : mCrafting->state().all())
            if (s.mKind == SyntheticKind::Gear)
                made = &s;
        ASSERT_NE(made, nullptr);
        EXPECT_EQ(made->mBase, kSword);
        EXPECT_EQ(made->mTemperPermille, 60);
        EXPECT_EQ(made->mName, "Fine Iron Sword");
        EXPECT_EQ(held(made->mId), 1);
        const inventory::ItemStat* stat = mInventory->table().item(made->mId);
        ASSERT_NE(stat, nullptr);
        EXPECT_EQ(stat->mCategory, inventory::Category::Weapon);
        EXPECT_EQ(stat->mName, "Fine Iron Sword");
        EXPECT_GT(stat->mValue, 25);
        EXPECT_EQ(mCrafting->baseOf(made->mId), kSword);
        EXPECT_EQ(mCrafting->baseOf(kSword), kSword);
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "smithing");
    }

    TEST_F(CraftingServiceFixture, anEquippedItemStaysEquippedAsItsBetterSelf)
    {
        waitReady();
        open(kFurnWheel);
        give(kSword, 1);
        give(kIngot, 1);
        ASSERT_EQ(mInventory->equip(kSword), inventory::EquipStatus::Ok);
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        const std::vector<inventory::EquippedItem> worn = mInventory->model().equipped();
        ASSERT_EQ(worn.size(), 1u);
        EXPECT_TRUE(isSyntheticId(worn[0].mItem));
        EXPECT_EQ(worn[0].mKind, inventory::EquipKind::Weapon);
    }

    TEST_F(CraftingServiceFixture, temperingAgainNeedsABetterResult)
    {
        waitReady();
        open(kFurnWheel);
        give(kSword, 1);
        give(kIngot, 5);
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        EXPECT_EQ(mCrafting->temper(kRecTemperSword), Status::AlreadyBetter);
        EXPECT_EQ(held(kIngot), 4); // refused: nothing used
        mSkill = 100.f;
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        EXPECT_EQ(held(kIngot), 3);
        std::int32_t gear = 0;
        std::int32_t best = 0;
        for (const Synthetic& s : mCrafting->state().all())
            if (s.mKind == SyntheticKind::Gear)
            {
                gear += held(s.mId);
                best = std::max(best, s.mTemperPermille);
            }
        EXPECT_EQ(gear, 1); // one sword, the better one
        EXPECT_EQ(best, 400);
    }

    TEST_F(CraftingServiceFixture, temperingRefusesWithoutTheItemOrTheMaterialsOrTheBench)
    {
        waitReady();
        give(kIngot, 1);
        EXPECT_EQ(mCrafting->temper(kRecTemperSword), Status::NoBench);
        open(kFurnWheel);
        EXPECT_EQ(mCrafting->temper(kRecTemperSword), Status::NotHeld);
        give(kSword, 1);
        EXPECT_EQ(mCrafting->temper(kRecTemperArmor), Status::NoBench); // the armour table is another bench
        EXPECT_EQ(mCrafting->temper(kRecForge), Status::NoBench); // not a tempering recipe
        EXPECT_EQ(mCrafting->temper(kSelf | 0xEEE), Status::UnknownRecipe);
        EXPECT_EQ(held(kSword), 1);
        (void)mInventory->model().consume(kPlayerOwner, kIngot, 1);
        EXPECT_EQ(mCrafting->temper(kRecTemperSword), Status::MissingItems);
        EXPECT_EQ(held(kSword), 1);
    }

    TEST_F(CraftingServiceFixture, anArmourIsTemperedAtTheArmourTable)
    {
        waitReady();
        open(kFurnArmor);
        give(kArmor, 1);
        give(kLeather, 1);
        ASSERT_EQ(mCrafting->temper(kRecTemperArmor), Status::Ok);
        EXPECT_EQ(held(kArmor), 0);
        bool found = false;
        for (const Synthetic& s : mCrafting->state().all())
            if (s.mKind == SyntheticKind::Gear && s.mBase == kArmor)
            {
                found = true;
                EXPECT_EQ(s.mCategory, inventory::Category::Armor);
                EXPECT_EQ(s.mName, "Fine Iron Armor");
            }
        EXPECT_TRUE(found);
    }

    TEST_F(CraftingServiceFixture, theCombatReadsTheTemperedWeaponAndArmour)
    {
        mSkill = 100.f;
        waitReady();
        open(kFurnWheel);
        give(kSword, 1);
        give(kIngot, 1);
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        const FormId made = mCrafting->state().all().back().mId;
        WeaponRecord base;
        base.mDamage = 7;
        base.mCommon.mId = kSword;
        WeaponRecord out;
        ASSERT_TRUE(mCrafting->weaponFor(made, base, out));
        EXPECT_EQ(out.mDamage, 10); // 7 x 1.4 = 9.8
        EXPECT_EQ(out.mCommon.mId, kSword);
        EXPECT_FALSE(mCrafting->weaponFor(kSword, base, out)); // a record item is read as it is
        EXPECT_EQ(base.mDamage, 7);
        open(kFurnArmor);
        give(kArmor, 1);
        give(kLeather, 1);
        ASSERT_EQ(mCrafting->temper(kRecTemperArmor), Status::Ok);
        ArmourPiece piece;
        piece.mRating = 25.f;
        piece.mForm = kArmor;
        FormId madeArmour = 0;
        for (const Synthetic& s : mCrafting->state().all())
            if (s.mBase == kArmor)
                madeArmour = s.mId;
        ASSERT_TRUE(mCrafting->armourFor(madeArmour, piece));
        EXPECT_NEAR(piece.mRating, 35.f, 1e-3f); // x 1.4
        ArmourPiece plain;
        plain.mRating = 25.f;
        EXPECT_FALSE(mCrafting->armourFor(kArmor, plain));
        EXPECT_FLOAT_EQ(plain.mRating, 25.f);
    }

    // ---- enchanting
    // --------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, disenchantingTeachesTheBaseEnchantmentAndDestroysTheItem)
    {
        waitReady();
        open(kFurnEnchant);
        give(kEnchSword, 2);
        ASSERT_EQ(mCrafting->disenchant(kEnchSword), Status::Ok);
        EXPECT_EQ(held(kEnchSword), 1);
        EXPECT_TRUE(mCrafting->state().knowsEnchantment(kEnchFireBase));
        EXPECT_FALSE(mCrafting->state().knowsEnchantment(kEnchFire03));
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "enchanting");
        EXPECT_EQ(mCrafting->disenchant(kEnchSword), Status::AlreadyKnown);
        EXPECT_EQ(held(kEnchSword), 1); // refused: not destroyed
        EXPECT_EQ(strings("enchantments"), (std::vector<std::string>{ inventory::formatId(kEnchFireBase) }));
        EXPECT_EQ(text("enchant_name", { str(inventory::formatId(kEnchFireBase)) }), "Fire Damage");
    }

    TEST_F(CraftingServiceFixture, disenchantingRefusesTheWrongItemOrBench)
    {
        waitReady();
        give(kSword, 1);
        give(kEnchSword, 1);
        EXPECT_EQ(mCrafting->disenchant(kEnchSword), Status::NoBench);
        open(kFurnEnchant);
        EXPECT_EQ(mCrafting->disenchant(kSword), Status::NotEnchanted);
        EXPECT_EQ(mCrafting->disenchant(kIngot), Status::NotGear);
        EXPECT_EQ(mCrafting->disenchant(kArmor), Status::NotHeld);
        EXPECT_EQ(held(kSword), 1);
        EXPECT_EQ(held(kEnchSword), 1);
        EXPECT_TRUE(mCrafting->state().knownEnchantments().empty());
    }

    TEST_F(CraftingServiceFixture, enchantingMakesTheItemAndUsesTheSoulGem)
    {
        waitReady();
        open(kFurnEnchant);
        mCrafting->state().learnEnchantment(kEnchFireBase);
        give(kSword, 1);
        give(kGemFull, 1);
        ASSERT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kGemFull), Status::Ok);
        EXPECT_EQ(held(kSword), 0);
        EXPECT_EQ(held(kGemFull), 0);
        const Synthetic* made = nullptr;
        for (const Synthetic& s : mCrafting->state().all())
            if (s.mKind == SyntheticKind::Gear)
                made = &s;
        ASSERT_NE(made, nullptr);
        EXPECT_EQ(made->mBase, kSword);
        EXPECT_EQ(made->mEnchantment, kEnchFireBase);
        EXPECT_NEAR(made->mEnchantMagnitude, 16.6f, 0.01f);
        EXPECT_EQ(made->mName, "Iron Sword of Fire Damage");
        EXPECT_EQ(held(made->mId), 1);
        ASSERT_EQ(mSkillUsed.size(), 1u);
        EXPECT_EQ(mSkillUsed[0].first, "enchanting");
        EXPECT_EQ(text("gear_enchant", { str(inventory::formatId(made->mId)) }), "Fire Damage");
    }

    TEST_F(CraftingServiceFixture, aSmallerSoulMakesAWeakerEnchantment)
    {
        waitReady();
        open(kFurnEnchant);
        mCrafting->state().learnEnchantment(kEnchFireBase);
        give(kSword, 1);
        give(kGemPetty, 1);
        ASSERT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kGemPetty), Status::Ok);
        EXPECT_NEAR(mCrafting->state().all().back().mEnchantMagnitude, 10.f * 0.5f * 1.0375f, 0.01f);
    }

    TEST_F(CraftingServiceFixture, enchantingRefusesWhatCannotWorkAndChangesNothing)
    {
        waitReady();
        give(kSword, 1);
        give(kGemFull, 1);
        give(kGemEmpty, 1);
        give(kEnchSword, 1);
        EXPECT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kGemFull), Status::NoBench);
        open(kFurnEnchant);
        EXPECT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kGemFull), Status::UnknownEnchantment); // not learned yet
        mCrafting->state().learnEnchantment(kEnchFireBase);
        EXPECT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kGemEmpty), Status::NoSoulGem); // an empty gem
        EXPECT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kSword), Status::NoSoulGem); // not a gem at all
        EXPECT_EQ(mCrafting->enchant(kEnchSword, kEnchFireBase, kGemFull), Status::AlreadyEnchanted);
        EXPECT_EQ(mCrafting->enchant(kIngot, kEnchFireBase, kGemFull), Status::NotGear);
        EXPECT_EQ(mCrafting->enchant(kArmor, kEnchFireBase, kGemFull), Status::NotHeld);
        EXPECT_EQ(mCrafting->enchant(kSword, kSelf | 0xEEE, kGemFull), Status::UnknownEnchantment);
        EXPECT_EQ(held(kSword), 1);
        EXPECT_EQ(held(kGemFull), 1);
        EXPECT_EQ(held(kEnchSword), 1);
        EXPECT_TRUE(mCrafting->state().all().empty());
        EXPECT_TRUE(mSkillUsed.empty());
    }

    TEST_F(CraftingServiceFixture, aTemperedItemKeepsItsTemperWhenEnchanted)
    {
        waitReady();
        open(kFurnWheel);
        give(kSword, 1);
        give(kIngot, 1);
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        const FormId tempered = mCrafting->state().all().back().mId;
        open(kFurnEnchant);
        mCrafting->state().learnEnchantment(kEnchFireBase);
        give(kGemFull, 1);
        ASSERT_EQ(mCrafting->enchant(tempered, kEnchFireBase, kGemFull), Status::Ok);
        const Synthetic& made = mCrafting->state().all().back();
        EXPECT_EQ(made.mBase, kSword);
        EXPECT_EQ(made.mTemperPermille, 60);
        EXPECT_EQ(made.mEnchantment, kEnchFireBase);
        EXPECT_EQ(made.mName, "Fine Iron Sword of Fire Damage");
        EXPECT_EQ(held(tempered), 0);
    }

    TEST_F(CraftingServiceFixture, theCombatReadsADamageEnchantmentAsExtraDamage)
    {
        waitReady();
        open(kFurnEnchant);
        mCrafting->state().learnEnchantment(kEnchFireBase);
        give(kSword, 1);
        give(kGemFull, 1);
        ASSERT_EQ(mCrafting->enchant(kSword, kEnchFireBase, kGemFull), Status::Ok);
        WeaponRecord base;
        base.mDamage = 7;
        WeaponRecord out;
        ASSERT_TRUE(mCrafting->weaponFor(mCrafting->state().all().back().mId, base, out));
        EXPECT_EQ(out.mDamage, 24); // 7 + 16.6 rounded
    }

    TEST_F(CraftingServiceFixture, theEnchantListsFollowWhatThePlayerHolds)
    {
        waitReady();
        give(kSword, 1);
        give(kArmor, 1);
        give(kEnchSword, 1);
        give(kIngot, 3);
        give(kGemFull, 1);
        give(kGemEmpty, 1);
        mCrafting->state().learnEnchantment(kEnchFireBase);
        EXPECT_EQ(strings("gear").size(), 3u);
        EXPECT_EQ(strings("gems"), (std::vector<std::string>{ inventory::formatId(kGemFull) }));
        EXPECT_EQ(text("gear_enchant", { str(inventory::formatId(kEnchSword)) }), "Fire Damage");
        EXPECT_EQ(text("gear_enchant", { str(inventory::formatId(kSword)) }), "");
        EXPECT_EQ(integer("gem_size", { str(inventory::formatId(kGemFull)) }), 5);
        open(kFurnEnchant);
        const std::string preview = text("enchant_preview",
            { str(inventory::formatId(kSword)), str(inventory::formatId(kEnchFireBase)),
                str(inventory::formatId(kGemFull)) });
        EXPECT_NE(preview.find("Fire Damage"), std::string::npos);
        EXPECT_NE(preview.find("16"), std::string::npos);
    }

    // ---- saving
    // ------------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, theMadeItemsAndWhatWasLearnedSurviveASaveAndALoad)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        open(kFurnWheel);
        give(kSword, 1);
        give(kIngot, 1);
        ASSERT_EQ(mCrafting->temper(kRecTemperSword), Status::Ok);
        const FormId potion = potionOf(*this);
        const FormId sword = mCrafting->state().all().back().mId;

        MemoryWriter craftingBytes, inventoryBytes;
        mCrafting->chunk().write(craftingBytes);
        mInventory->chunk().write(inventoryBytes);

        World loaded;
        loaded.build("loaded");
        {
            MemoryReader r(craftingBytes.mBytes);
            ASSERT_TRUE(loaded.mCrafting->chunk().read(r, 1));
            MemoryReader r2(inventoryBytes.mBytes);
            ASSERT_TRUE(loaded.mInventory->chunk().read(r2, 1));
        }
        loaded.waitReady();
        EXPECT_EQ(loaded.held(potion), 1);
        EXPECT_EQ(loaded.held(sword), 1);
        const inventory::ItemStat* stat = loaded.mInventory->table().item(potion);
        ASSERT_NE(stat, nullptr);
        EXPECT_EQ(stat->mName, "Potion of Restore Health");
        EXPECT_EQ(stat->mEffects.size(), 2u);
        const inventory::ItemStat* made = loaded.mInventory->table().item(sword);
        ASSERT_NE(made, nullptr);
        EXPECT_EQ(made->mCategory, inventory::Category::Weapon);
        EXPECT_EQ(loaded.mCrafting->baseOf(sword), kSword);
        EXPECT_TRUE(loaded.mCrafting->state().knowsEffect(kEffHeal));
        EXPECT_EQ(loaded.text("ingredient_effects", { str(inventory::formatId(kIngA)) }),
            "Restore Health, Fortify Health, ?, ?");
        // the potion still drinks
        EXPECT_EQ(loaded.mInventory->use(potion), inventory::InventoryService::UseStatus::Ok);
    }

    TEST_F(CraftingServiceFixture, aNewItemAfterALoadContinuesTheNumbering)
    {
        waitReady();
        open(kFurnAlchemy);
        give(kIngA, 1);
        give(kIngB, 1);
        const FormId two[] = { kIngA, kIngB };
        ASSERT_EQ(mCrafting->brew(two), Status::Ok);
        MemoryWriter bytes;
        mCrafting->chunk().write(bytes);
        World loaded;
        loaded.build("loaded2");
        MemoryReader r(bytes.mBytes);
        ASSERT_TRUE(loaded.mCrafting->chunk().read(r, 1));
        loaded.waitReady();
        loaded.open(kFurnAlchemy);
        loaded.give(kIngA, 1);
        loaded.give(kIngC, 1);
        const FormId ac[] = { kIngA, kIngC };
        ASSERT_EQ(loaded.mCrafting->brew(ac), Status::Ok);
        EXPECT_EQ(loaded.mCrafting->state().all().size(), 2u);
        EXPECT_EQ(loaded.mCrafting->state().all()[1].mId, kSyntheticPrefix | 2u);
    }

    // ---- bindings
    // ----------------------------------------------------------------------------------------------------

    TEST_F(CraftingServiceFixture, theNamespaceIsDeclaredWithReadAndMutateFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.crafting");
        EXPECT_EQ(mBindings.mVersion.major, 1u);
        for (const char* name :
            { "state", "reason", "bench", "bench_name", "results", "revision", "last_result", "ingredients",
                "ingredient_effects", "preview", "recipes", "recipe_name", "recipe_needs", "recipe_ok", "recipe_temper",
                "gear", "gear_enchant", "enchantments", "enchant_name", "gems", "gem_size", "enchant_preview" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(name), 1u) << name;
            EXPECT_EQ(mBindings.mFunctions[name].access, openeden::sdk::BindingAccess::Read) << name;
        }
        for (const char* name :
            { "open", "open_kind", "close", "brew", "eat", "craft", "temper", "disenchant", "enchant" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(name), 1u) << name;
            EXPECT_EQ(mBindings.mFunctions[name].access, openeden::sdk::BindingAccess::Mutate) << name;
        }
    }

    TEST_F(CraftingServiceFixture, theMutateCallsDoTheOperationsAndTheResultsCounterMoves)
    {
        waitReady();
        mBindings.call("open", { str("7"), str(inventory::formatId(kFurnAlchemy)), str("Lab") });
        mCrafting->poll();
        ASSERT_EQ(text("state"), "ready");
        give(kIngA, 1);
        give(kIngB, 1);
        const std::int32_t before = integer("results");
        const std::int32_t revision = integer("revision");
        mBindings.call("brew", { str(inventory::formatId(kIngA)), str(inventory::formatId(kIngB)), str("") });
        EXPECT_GT(integer("results"), before);
        EXPECT_NE(integer("revision"), revision);
        EXPECT_EQ(text("last_result"), "ok");
        EXPECT_FALSE(strings("ingredients").size() > 0);
        mBindings.call("brew", { str(inventory::formatId(kIngA)), str(inventory::formatId(kIngB)), str("") });
        EXPECT_EQ(text("last_result"), "not held");
        mBindings.call("brew", { str("zz"), str("1"), str("") });
        EXPECT_NE(text("last_result"), "ok");
    }

    TEST_F(CraftingServiceFixture, everyStatusHasAName)
    {
        for (int i = 0; i <= static_cast<int>(Status::NoEffect); ++i)
            EXPECT_FALSE(statusName(static_cast<Status>(i)).empty()) << i;
    }
}

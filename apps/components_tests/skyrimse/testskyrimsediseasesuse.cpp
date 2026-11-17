// Task 0240 (diseases): drinking a potion with a Cure Disease effect through the inventory service, and a shrine spell
// cast on the player through the stones service. The rig is a copy of testskyrimseinventoryequip.cpp (a generated
// plugin in a temporary folder); no game data.
#include <game_skyrimse/inventory_service.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/stones.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::inventory;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kCurePotion = kSelf | 0x130; // only Cure Disease
    constexpr FormId kCureAndHeal = kSelf | 0x131; // Cure Disease and 25 health
    constexpr FormId kHealOnly = kSelf | 0x132;
    constexpr FormId kEffCure = kSelf | 0x200;
    constexpr FormId kEffHeal = kSelf | 0x201;

    Bytes withEffect(Bytes alch, FormId effect, float magnitude)
    {
        alch.append(sub4("EFID", Bytes().u32(effect)));
        alch.append(sub4("EFIT", Bytes().f32(magnitude).u32(0).u32(0)));
        return alch;
    }

    Bytes curePlugin()
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 3, { { "Skyrim.esm", 0 } }, 24, 44);
        const auto potion = [&](FormId id, std::vector<std::pair<FormId, float>> list) {
            Bytes alch;
            alch.append(sub4("DATA", Bytes().f32(0.5f)));
            alch.append(sub4("ENIT", Bytes().u32(30).u32(0).u32(0).u32(0).u32(0)));
            for (const auto& [effect, magnitude] : list)
                alch = withEffect(std::move(alch), effect, magnitude);
            return record4("ALCH", 0, id, alch);
        };
        all.append(group4(Content::fourCC("ALCH"), 0,
            potion(kCurePotion, { { kEffCure, 0.f } })
                .append(potion(kCureAndHeal, { { kEffCure, 0.f }, { kEffHeal, 25.f } }))
                .append(potion(kHealOnly, { { kEffHeal, 25.f } }))));
        return all;
    }

    struct DiseaseUseService : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<float> mHealed;
        int mCures = 0;
        bool mHealWorks = true;
        std::unique_ptr<InventoryService> mService;

        void SetUp() override
        {
            const Bytes all = curePlugin();
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_disuse_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            Hooks hooks;
            hooks.mInputs = [this](Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mEquipInfo = [](FormId, EquipInfo&) { return false; };
            hooks.mHealthOf = [](FormId effect, float magnitude, std::uint32_t duration) {
                return effect == kEffHeal && duration == 0 ? magnitude : 0.f;
            };
            hooks.mApplyHealth = [this](float amount) {
                if (!mHealWorks)
                    return false;
                mHealed.push_back(amount);
                return true;
            };
            hooks.mIsCureEffect = [](FormId effect) { return effect == kEffCure; };
            hooks.mCureDisease = [this] { ++mCures; };
            mService = std::make_unique<InventoryService>(std::move(hooks), 100.f, 1);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!mService->poll() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mService->ready());
        }
        void TearDown() override
        {
            mService.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        void give(FormId item, std::int32_t count)
        {
            std::vector<Stack> stacks = mService->model().stacks(kPlayerOwner);
            stacks.push_back({ item, count });
            mService->model().setPlayer(std::move(stacks));
        }
    };

    TEST_F(DiseaseUseService, aCurePotionThatRestoresNoHealthIsDrunkAndCures)
    {
        give(kCurePotion, 2);
        EXPECT_EQ(mService->use(kCurePotion), InventoryService::UseStatus::Ok);
        EXPECT_EQ(mCures, 1);
        EXPECT_TRUE(mHealed.empty());
        EXPECT_EQ(mService->model().count(kPlayerOwner, kCurePotion), 1);
    }

    TEST_F(DiseaseUseService, aPotionThatCuresAndHealsDoesBoth)
    {
        give(kCureAndHeal, 1);
        EXPECT_EQ(mService->use(kCureAndHeal), InventoryService::UseStatus::Ok);
        EXPECT_EQ(mCures, 1);
        ASSERT_EQ(mHealed.size(), 1u);
        EXPECT_FLOAT_EQ(mHealed[0], 25.f);
        EXPECT_EQ(mService->model().count(kPlayerOwner, kCureAndHeal), 0);
    }

    TEST_F(DiseaseUseService, aHealingPotionCuresNothing)
    {
        give(kHealOnly, 1);
        EXPECT_EQ(mService->use(kHealOnly), InventoryService::UseStatus::Ok);
        EXPECT_EQ(mCures, 0);
    }

    TEST_F(DiseaseUseService, aRefusedHealKeepsTheCurePotionAndCuresNothing)
    {
        mHealWorks = false;
        give(kCureAndHeal, 1);
        EXPECT_EQ(mService->use(kCureAndHeal), InventoryService::UseStatus::Refused);
        EXPECT_EQ(mCures, 0);
        EXPECT_EQ(mService->model().count(kPlayerOwner, kCureAndHeal), 1);
    }

    // ---- a shrine spell
    // ---------------------------------------------------------------------------------------------------

    class ShrineLibrary final : public PerkLibrary
    {
    public:
        ShrineLibrary()
        {
            MagicEffectRecord cure;
            cure.mCommon.mId = kEffCure;
            cure.mHasData = true;
            cure.mArchetype = 3;
            mEffects[kEffCure] = cure;
            MagicEffectRecord fortify;
            fortify.mCommon.mId = kEffHeal;
            fortify.mHasData = true;
            fortify.mArchetype = MagicArchetype::PeakValueModifier;
            fortify.mPrimaryValue = 26;
            mEffects[kEffHeal] = fortify;
            add(0x1001, "AltarKynarethSpell", true);
            add(0x1002, "AltarOther", false);
        }
        const PerkRecord* perk(FormId) const override { return nullptr; }
        const SpellRecord* spell(FormId id) const override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* magicEffect(FormId id) const override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        void add(FormId id, const std::string& editorId, bool withCure)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = editorId;
            s.mHasData = true;
            SpellEffect fort;
            fort.mEffect = kEffHeal;
            fort.mMagnitude = 25.f;
            fort.mDuration = 28800;
            s.mEffects.push_back(fort);
            if (withCure)
            {
                SpellEffect c;
                c.mEffect = kEffCure;
                s.mEffects.push_back(c);
            }
            mSpells[id] = s;
        }
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
    };

    TEST(SkyrimDiseasesShrine, aSpellWithACureDiseaseEffectCastOnThePlayerCures)
    {
        ShrineLibrary library;
        int cures = 0;
        stones::StonesService::Hooks hooks;
        hooks.mLibrary = [&]() -> const PerkLibrary* { return &library; };
        hooks.mNow = [] { return 0.0; };
        hooks.mCure = [&] { ++cures; };
        stones::StonesService service(std::move(hooks));
        EXPECT_TRUE(service.cast(0x1002, 0x14)); // a blessing with no cure
        EXPECT_EQ(cures, 0);
        EXPECT_TRUE(service.cast(0x1001, 0x14));
        EXPECT_EQ(cures, 1);
        EXPECT_FALSE(service.cast(0x1001, 0x55)); // not cast on the player
        EXPECT_EQ(cures, 1);
    }

    TEST(SkyrimDiseasesShrine, withNoCureHookACastIsAsBefore)
    {
        ShrineLibrary library;
        stones::StonesService::Hooks hooks;
        hooks.mLibrary = [&]() -> const PerkLibrary* { return &library; };
        hooks.mNow = [] { return 0.0; };
        stones::StonesService service(std::move(hooks));
        EXPECT_TRUE(service.cast(0x1001, 0x14));
        EXPECT_EQ(service.timed().size(), 1u);
    }
}

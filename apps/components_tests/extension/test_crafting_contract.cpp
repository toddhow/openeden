// Task 0085 move 2, stage 2c: the crafting contract header (sdk/include/openeden/game/crafting.hpp). Written from the
// spec (docs/tasks/0085-move-morrowind-rules-first-moves.md, the decisions of 2026-11-08) before the header existed:
// plain values only, an opaque item handle, additive interfaces.
#include <openeden/game/crafting.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;

    TEST(CraftingContract, the_item_handle_is_an_opaque_number_where_zero_is_no_item)
    {
        static_assert(std::is_trivially_copyable_v<ItemHandle>);
        static_assert(std::is_standard_layout_v<ItemHandle>);
        static_assert(sizeof(ItemHandle) == sizeof(std::uint64_t));
        EXPECT_TRUE(ItemHandle{}.empty());
        EXPECT_TRUE(kNoItem.empty());
        EXPECT_EQ(kNoItem, ItemHandle{});
        EXPECT_FALSE(ItemHandle{ 7 }.empty());
        EXPECT_EQ(ItemHandle{ 7 }, ItemHandle{ 7 });
        EXPECT_NE(ItemHandle{ 7 }, ItemHandle{ 8 });
    }

    TEST(CraftingContract, the_numbers_of_the_enums_are_the_games)
    {
        EXPECT_EQ(static_cast<int>(AlchemyResult::Success), 0);
        EXPECT_EQ(static_cast<int>(AlchemyResult::NoMortarAndPestle), 1);
        EXPECT_EQ(static_cast<int>(AlchemyResult::LessThanTwoIngredients), 2);
        EXPECT_EQ(static_cast<int>(AlchemyResult::NoName), 3);
        EXPECT_EQ(static_cast<int>(AlchemyResult::NoEffects), 4);
        EXPECT_EQ(static_cast<int>(AlchemyResult::RandomFailure), 5);
        EXPECT_EQ(static_cast<int>(CastStyle::CastOnce), 0);
        EXPECT_EQ(static_cast<int>(CastStyle::WhenStrikes), 1);
        EXPECT_EQ(static_cast<int>(CastStyle::WhenUsed), 2);
        EXPECT_EQ(static_cast<int>(CastStyle::ConstantEffect), 3);
        EXPECT_EQ(static_cast<int>(ApparatusSlot::MortarPestle), 0);
        EXPECT_EQ(static_cast<int>(ApparatusSlot::Alembic), 1);
        EXPECT_EQ(static_cast<int>(ApparatusSlot::Calcinator), 2);
        EXPECT_EQ(static_cast<int>(ApparatusSlot::Retort), 3);
        EXPECT_EQ(kAlchemyIngredientSlots, 4u);
        EXPECT_EQ(kAlchemyApparatusSlots, 4u);
    }

    TEST(CraftingContract, the_value_types_are_plain_and_comparable)
    {
        static_assert(std::is_copy_constructible_v<EffectLine> && std::is_nothrow_move_constructible_v<EffectLine>);
        const EffectLine defaults;
        EXPECT_TRUE(defaults.effectId.empty());
        EXPECT_TRUE(defaults.skill.empty());
        EXPECT_TRUE(defaults.attribute.empty());
        EffectLine a;
        a.effectId = "restorehealth";
        a.duration = 30;
        EffectLine b = a;
        EXPECT_EQ(a, b);
        b.magnitudeMax = 5;
        EXPECT_NE(a, b);
        const AlchemyEffectLine line;
        EXPECT_TRUE(line.skill.empty());
        EXPECT_FALSE(line.known);
    }

    TEST(CraftingContract, the_version_is_two_point_zero)
    {
        EXPECT_EQ(kCraftingRulesVersion.major, 2u);
        EXPECT_EQ(kCraftingRulesVersion.minor, 0u);
    }

    // A pack can implement every interface with nothing but the header: this is the proof that the classes are complete
    // and abstract in the way the spec wants (no engine type is needed to derive from them).
    class FakeAlchemy final : public AlchemySession
    {
    public:
        void setAlchemist(ItemHandle actor) override { mActor = actor; }
        void clear() override { mActor = kNoItem; }
        void setPotionName(std::string_view name) override { mName = name; }
        int countPotionsToBrew() const override { return 3; }
        int addIngredient(ItemHandle) override { return 0; }
        void addApparatus(ItemHandle) override {}
        void removeIngredient(std::size_t) override {}
        void removeApparatus(std::size_t) override {}
        ItemHandle ingredientSlot(std::size_t) override { return kNoItem; }
        ItemHandle apparatusSlot(std::size_t) override { return kNoItem; }
        std::vector<AlchemyEffectLine> effects() const override
        {
            return { AlchemyEffectLine{ "restorehealth", "", "strength", true } };
        }
        std::string suggestPotionName() override { return "x"; }
        AlchemyResult create(std::string_view, int& count) override
        {
            count = 2;
            return AlchemyResult::Success;
        }

        ItemHandle mActor;
        std::string mName;
    };

    class FakeEnchanting final : public EnchantingSession
    {
    public:
        void setEnchanter(ItemHandle) override {}
        void setSelfEnchanting(bool) override {}
        void setOldItem(ItemHandle item) override { mOld = item; }
        void setSoulGem(ItemHandle gem) override { mGem = gem; }
        ItemHandle oldItem() override { return mOld; }
        ItemHandle soulGem() override { return mGem; }
        void setNewItemName(std::string_view) override {}
        void setEffects(std::span<const EffectLine> effects) override { mCount = effects.size(); }
        bool create() override { return true; }
        void nextCastStyle() override { mStyle = CastStyle::WhenUsed; }
        CastStyle castStyle() const override { return mStyle; }
        float enchantPoints(bool) const override { return 1.5f; }
        int effectiveCastCost() const override { return 1; }
        int enchantPrice() const override { return 2; }
        int maxEnchantValue() const override { return 3; }
        int gemCharge() const override { return 4; }
        int enchantChance() const override { return 5; }
        bool soulEmpty() const override { return mGem.empty(); }
        bool itemEmpty() const override { return mOld.empty(); }

        ItemHandle mOld, mGem;
        std::size_t mCount = 0;
        CastStyle mStyle = CastStyle::CastOnce;
    };

    class FakeRepair final : public RepairSession
    {
    public:
        void setTool(ItemHandle tool) override { mTool = tool; }
        ItemHandle tool() override { return mTool; }
        int toolUses() const override { return 10; }
        float toolQuality() const override { return 1.25f; }
        int toolCount() const override { return 1; }
        void repair(ItemHandle) override {}

        ItemHandle mTool;
    };

    class FakeRules final : public CraftingRules
    {
    public:
        std::unique_ptr<AlchemySession> beginAlchemy() override { return std::make_unique<FakeAlchemy>(); }
        std::unique_ptr<EnchantingSession> beginEnchanting() override { return std::make_unique<FakeEnchanting>(); }
        std::unique_ptr<RepairSession> beginRepair() override { return std::make_unique<FakeRepair>(); }
        bool rechargeWithGem(ItemHandle, ItemHandle gem) override { return !gem.empty(); }
        int gemSoul(ItemHandle) override { return 100; }
        float rechargedCharge(float charge, float maxCharge, float duration) override
        {
            return std::min(charge + duration, maxCharge);
        }
        bool knownAlchemyEffect(std::size_t index, ItemHandle) override { return index == 0; }
        std::vector<std::string> ingredientEffectNames(ItemHandle, float) override { return { "a" }; }
        int apparatusSlotOf(ItemHandle) override { return 1; }
    };

    TEST(CraftingContract, a_pack_can_implement_every_interface_from_the_header_alone)
    {
        FakeRules rules;
        CraftingRules& contract = rules;

        auto alchemy = contract.beginAlchemy();
        ASSERT_TRUE(alchemy);
        int count = 0;
        EXPECT_EQ(alchemy->create("p", count), AlchemyResult::Success);
        EXPECT_EQ(count, 2);
        ASSERT_EQ(alchemy->effects().size(), 1u);
        EXPECT_TRUE(alchemy->effects()[0].known);

        auto enchanting = contract.beginEnchanting();
        ASSERT_TRUE(enchanting);
        enchanting->setOldItem(ItemHandle{ 3 });
        EXPECT_EQ(enchanting->oldItem(), ItemHandle{ 3 });
        EXPECT_FALSE(enchanting->itemEmpty());
        EXPECT_TRUE(enchanting->soulEmpty());
        const std::vector<EffectLine> lines(2);
        enchanting->setEffects(lines);

        auto repair = contract.beginRepair();
        ASSERT_TRUE(repair);
        repair->setTool(ItemHandle{ 9 });
        EXPECT_EQ(repair->tool(), ItemHandle{ 9 });

        EXPECT_FLOAT_EQ(contract.rechargedCharge(1.0f, 10.0f, 2.0f), 3.0f);
        EXPECT_TRUE(contract.rechargeWithGem(ItemHandle{ 1 }, ItemHandle{ 2 }));
        EXPECT_FALSE(contract.rechargeWithGem(ItemHandle{ 1 }, kNoItem));
        EXPECT_EQ(contract.apparatusSlotOf(ItemHandle{ 5 }), static_cast<int>(ApparatusSlot::Alembic));
    }
}

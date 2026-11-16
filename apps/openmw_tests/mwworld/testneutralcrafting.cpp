// Task 0199: the engine's part of crafting, the pure decision to open a bench's window. Written before the code.
#include "apps/openmw/mwworld/neutralinventory.hpp"

#include <gtest/gtest.h>

namespace
{
    using openeden::sdk::game::ObjectKind;
    using namespace MWWorld;

    TEST(NeutralCrafting, theEventNameIsThePinnedOne)
    {
        EXPECT_EQ(kNeutralBenchEvent, "openeden_skyrimse_bench_opened");
    }

    TEST(NeutralCrafting, aFurnitureTheSettingAndThePlayerOpenTheBench)
    {
        EXPECT_EQ(decideNeutralBench(ObjectKind::Furniture, true, true), NeutralInventoryDecision::Open);
    }

    TEST(NeutralCrafting, anythingElseIsLeftAlone)
    {
        EXPECT_EQ(decideNeutralBench(ObjectKind::Furniture, false, true), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralBench(ObjectKind::Furniture, true, false), NeutralInventoryDecision::NotApplicable);
        for (const ObjectKind kind : { ObjectKind::Unknown, ObjectKind::Static, ObjectKind::Door, ObjectKind::Activator,
                 ObjectKind::Container, ObjectKind::Item, ObjectKind::Actor, ObjectKind::Other })
            EXPECT_EQ(decideNeutralBench(kind, true, true), NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralCrafting, theEventFieldsAreThoseOfAContainerWithKindBench)
    {
        const auto fields = neutralInventoryEventFields(0x1a66e, 0xb00, "bench", "Alchemy Lab");
        ASSERT_EQ(fields.size(), 4u);
        EXPECT_EQ(fields[2].mText, "bench");
        EXPECT_EQ(fields[3].mText, "Alchemy Lab");
    }
}

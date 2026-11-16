// Task 0224 (mining and woodcutting): what the activation of a placed Activator does, the event it sends, and the
// interned per-placement names. Written with the code (not run red against stubs).
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutraldisplayname.hpp"
#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using namespace MWWorld;

    TEST(NeutralResourceDecision, onlyThePlayerActivatingAnActivatorWithTheSettingOnSendsTheEvent)
    {
        EXPECT_EQ(decideNeutralResource(ObjectKind::Activator, true, true), NeutralInventoryDecision::Open);
        EXPECT_EQ(decideNeutralResource(ObjectKind::Activator, true, false), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralResource(ObjectKind::Activator, false, true), NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralResourceDecision, nothingElseIsAResource)
    {
        for (const ObjectKind kind : { ObjectKind::Unknown, ObjectKind::Static, ObjectKind::MovableStatic,
                 ObjectKind::Furniture, ObjectKind::Door, ObjectKind::Flora, ObjectKind::Tree, ObjectKind::Container,
                 ObjectKind::Item, ObjectKind::Light, ObjectKind::Actor, ObjectKind::LeveledList, ObjectKind::Other })
            EXPECT_EQ(decideNeutralResource(kind, true, true), NeutralInventoryDecision::NotApplicable)
                << static_cast<int>(kind);
    }

    TEST(NeutralResourceDecision, theEventNameIsTheOneTheScriptListensToAndDiffersFromTheOthers)
    {
        EXPECT_EQ(kNeutralResourceEvent, "openeden_skyrimse_resource_activated");
        EXPECT_NE(kNeutralResourceEvent, kNeutralHarvestEvent);
        EXPECT_NE(kNeutralResourceEvent, kNeutralBenchEvent);
        EXPECT_NE(kNeutralResourceEvent, kNeutralInventoryEvent);
    }

    TEST(NeutralResourceDisplayName, anInternedNameKeepsItsViewAndTheSameTextGivesTheSameView)
    {
        NeutralDisplayNames names;
        const std::string_view a = names.intern("Iron Ore Vein (depleted)");
        const std::string_view b = names.intern("Mine Iron Ore Vein");
        for (int i = 0; i < 200; ++i)
            (void)names.intern("filler " + std::to_string(i));
        EXPECT_EQ(a, "Iron Ore Vein (depleted)");
        EXPECT_EQ(b, "Mine Iron Ore Vein");
        EXPECT_EQ(names.intern("Iron Ore Vein (depleted)").data(), a.data());
    }
}

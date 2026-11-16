// Task 0186 (combat stage S5-8, contract 0.4): the cast clips' event name reaches the pack as ClipEventKind::SpellFire.
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

namespace
{
    using openeden::sdk::game::ClipEventKind;

    TEST(NeutralSpellFire, theCastClipsEventsAreClassifiedIgnoringCase)
    {
        using MWPhysics::classifyClipEvent;
        EXPECT_EQ(classifyClipEvent("MRh_SpellFire_Event"), ClipEventKind::SpellFire);
        EXPECT_EQ(classifyClipEvent("MLh_SpellFire_Event"), ClipEventKind::SpellFire);
        EXPECT_EQ(classifyClipEvent("mrh_spellfire_event"), ClipEventKind::SpellFire);
    }

    TEST(NeutralSpellFire, otherEventsAreNotTakenForTheSpellEvent)
    {
        using MWPhysics::classifyClipEvent;
        EXPECT_EQ(classifyClipEvent("Voice_SpellFire_Event"), ClipEventKind::Other); // a shout, not a cast
        EXPECT_EQ(classifyClipEvent("MRh_WinStart"), ClipEventKind::Other);
        EXPECT_EQ(classifyClipEvent("HitFrame"), ClipEventKind::Hit); // the old kinds keep their meaning
        EXPECT_EQ(classifyClipEvent("recoilStop"), ClipEventKind::RecoilStop);
        EXPECT_EQ(static_cast<int>(ClipEventKind::SpellFire), 16);
    }
}

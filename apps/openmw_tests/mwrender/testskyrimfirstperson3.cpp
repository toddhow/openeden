// Task 0249 (the player's first-person hands, third slice): the pure rules of apps/openmw/mwrender/skyrimhands.hpp and
// the loop offset of the Timeline. The hands' loop for a bow and for a held spell, the weapon that appears and leaves at
// its equip and unequip clip's own event, and where the event falls in a clip's meta. Nothing reads a file. The event
// times are those of animationdata/firstperson.txt: 1HM_Equip weaponDraw 0.566667, 1HM_Unequip weaponSheathe 1.0,
// Bow_Equip weaponDraw 0.533333.
#include <apps/openmw/mwrender/skyrimhands.hpp>
#include <apps/openmw/mwrender/skyrimheldgear.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>

namespace
{
    using namespace MWRender::SkyrimFirstPerson;

    TEST(SkyrimFirstPerson3Idle, aBowHasItsOwnIdleHeldAndDrawnBack)
    {
        EXPECT_EQ(idleClipFor(7, Stance::Weapon), "bow_idleheld");
        EXPECT_EQ(handsIdleClip(7, Stance::Weapon, false, {}), "bow_idleheld");
        EXPECT_EQ(handsIdleClip(7, Stance::Weapon, true, {}), "bow_idledrawn");
        EXPECT_EQ(handsIdleClip(7, Stance::Sheathed, true, {}), "mt_idle"); // nothing is up: the string is not held
    }

    TEST(SkyrimFirstPerson3Idle, aDrawnBackStringMeansNothingForAnyOtherWeapon)
    {
        EXPECT_EQ(handsIdleClip(1, Stance::Weapon, true, {}), "1hm_idle");
        EXPECT_EQ(handsIdleClip(0, Stance::Weapon, true, {}), "h2h_idle");
        EXPECT_EQ(handsIdleClip(5, Stance::Weapon, true, {}), "2hm_idle");
    }

    TEST(SkyrimFirstPerson3Idle, theOtherStancesAreTheOnesTheyWere)
    {
        EXPECT_EQ(handsIdleClip(1, Stance::Sheathed, false, {}), "mt_idle");
        EXPECT_EQ(handsIdleClip(1, Stance::Spell, false, {}), "mrh_idle");
        EXPECT_EQ(handsIdleClip(7, Stance::Spell, false, {}), "mrh_idle");
    }

    TEST(SkyrimFirstPerson3Idle, aHeldSpellsLoopReplacesTheIdleInAnyStance)
    {
        EXPECT_EQ(handsIdleClip(1, Stance::Spell, false, "MRh_SelfConcentration"), "MRh_SelfConcentration");
        EXPECT_EQ(handsIdleClip(1, Stance::Weapon, false, "MRh_WardLoop"), "MRh_WardLoop");
        EXPECT_EQ(handsIdleClip(7, Stance::Weapon, true, "MRh_ChargeLoop"), "MRh_ChargeLoop");
        EXPECT_EQ(handsIdleClip(0, Stance::Sheathed, false, "MRh_ChargeLoop"), "MRh_ChargeLoop");
    }

    TEST(SkyrimFirstPerson3Gear, thePhasesNameTheEventTheyWaitFor)
    {
        EXPECT_EQ(gearEventName(GearPhase::Equipping), "weaponDraw");
        EXPECT_EQ(gearEventName(GearPhase::Unequipping), "weaponSheathe");
        EXPECT_TRUE(gearEventName(GearPhase::None).empty());
        EXPECT_EQ(kBowType, 7);
    }

    TEST(SkyrimFirstPerson3Gear, withNoClipTheStanceDecides)
    {
        const EventTime none;
        EXPECT_TRUE(weaponShown(Stance::Weapon, GearPhase::None, false, 0.f, none));
        EXPECT_FALSE(weaponShown(Stance::Spell, GearPhase::None, false, 0.f, none));
        EXPECT_FALSE(weaponShown(Stance::Sheathed, GearPhase::None, false, 0.f, none));
    }

    TEST(SkyrimFirstPerson3Gear, aDrawnWeaponAppearsAtTheEventNotAtTheStartOfTheClip)
    {
        const EventTime at{ EventTime::State::At, 0.566667f };
        EXPECT_FALSE(weaponShown(Stance::Weapon, GearPhase::Equipping, false, 0.f, at)); // not started
        EXPECT_FALSE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 0.f, at));
        EXPECT_FALSE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 0.4f, at));
        EXPECT_TRUE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 0.566667f, at)); // at the event
        EXPECT_TRUE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 1.2f, at));
    }

    TEST(SkyrimFirstPerson3Gear, anEventAtTheStartOfTheClipShowsTheWeaponAtOnce)
    {
        const EventTime at{ EventTime::State::At, 0.f }; // H2H_Equip: weaponDraw 0
        EXPECT_TRUE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 0.f, at));
    }

    TEST(SkyrimFirstPerson3Gear, whileTheSidecarIsStillBeingReadTheWeaponWaitsAndAClipWithNoEventShowsItAtTheStart)
    {
        const EventTime unknown;
        const EventTime missing{ EventTime::State::Missing, 0.f };
        EXPECT_FALSE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 0.3f, unknown));
        EXPECT_TRUE(weaponShown(Stance::Weapon, GearPhase::Equipping, true, 0.f, missing));
        EXPECT_FALSE(weaponShown(Stance::Weapon, GearPhase::Equipping, false, 0.f, missing)); // not started yet
    }

    TEST(SkyrimFirstPerson3Gear, aSheathedWeaponLeavesAtTheEventNotAtTheEndOfTheClip)
    {
        const EventTime at{ EventTime::State::At, 1.f };
        EXPECT_TRUE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, false, 0.f, at)); // the hands still hold it
        EXPECT_TRUE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, true, 0.f, at));
        EXPECT_TRUE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, true, 0.9f, at));
        EXPECT_FALSE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, true, 1.f, at));
        EXPECT_FALSE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, true, 1.8f, at));
    }

    TEST(SkyrimFirstPerson3Gear, aSheathedWeaponStaysToTheEndWhenTheEventIsUnknownOrMissing)
    {
        const EventTime unknown;
        const EventTime missing{ EventTime::State::Missing, 0.f };
        EXPECT_TRUE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, true, 5.f, unknown));
        EXPECT_TRUE(weaponShown(Stance::Sheathed, GearPhase::Unequipping, true, 5.f, missing));
    }

    TEST(SkyrimFirstPerson3Nodes, aBowWaitsOnItsSheathNodeAndIsHeldInTheLeftHand)
    {
        using namespace MWRender::SkyrimHeldGear;
        EXPECT_EQ(heldSheathNode(7), "WeaponBow");
        EXPECT_EQ(heldHandNode(7), "SHIELD");
    }

    TEST(SkyrimFirstPerson3Nodes, everyOtherTypeKeepsItsSheathAndTheRightHand)
    {
        using namespace MWRender::SkyrimHeldGear;
        for (std::uint8_t type : { 1, 2, 3, 4, 5, 6 })
        {
            EXPECT_EQ(heldSheathNode(type), sheathNodeFor(type)) << int(type);
            EXPECT_EQ(heldHandNode(type), "WEAPON") << int(type);
        }
        EXPECT_TRUE(heldSheathNode(0).empty());
        EXPECT_TRUE(heldSheathNode(9).empty());
    }

    TEST(SkyrimFirstPerson3Event, aMetaThatIsNotReadYetGivesUnknownAndAFailedOneMissing)
    {
        const std::shared_ptr<const HavokPack::ClipMeta> none;
        EXPECT_EQ(eventTimeIn(none, false, "weaponDraw").state, EventTime::State::Unknown);
        EXPECT_EQ(eventTimeIn(none, true, "weaponDraw").state, EventTime::State::Missing);
    }

    TEST(SkyrimFirstPerson3Event, anEventIsFoundByNameWithoutRegardToCaseAndScaledByTheClipsSpeed)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = 1.43f;
        meta->mSpeed = 1.f;
        meta->mEvents.push_back({ "PitchOverrideStart", 0.f });
        meta->mEvents.push_back({ "BeginWeaponDraw", 0.f });
        meta->mEvents.push_back({ "weaponDraw", 0.566667f });
        meta->mEvents.push_back({ "weaponDraw", 0.9f }); // the first of the name counts
        meta->sortEvents();
        const std::shared_ptr<const HavokPack::ClipMeta> shared = meta;
        const EventTime found = eventTimeIn(shared, true, "WEAPONDRAW");
        EXPECT_EQ(found.state, EventTime::State::At);
        EXPECT_NEAR(found.seconds, 0.566667f, 1e-6f);
        EXPECT_EQ(eventTimeIn(shared, true, "weaponSheathe").state, EventTime::State::Missing);
        meta->mSpeed = 2.f; // a clip played at twice the speed: the stored time is playback time
        EXPECT_NEAR(eventTimeIn(shared, true, "weaponDraw").seconds, 1.133334f, 1e-5f);
        meta->mSpeed = 0.f; // a speed that is not positive plays as 1
        EXPECT_NEAR(eventTimeIn(shared, true, "weaponDraw").seconds, 0.566667f, 1e-6f);
    }

    TEST(SkyrimFirstPerson3Timeline, aLoopThatStartsLateIsInPhase)
    {
        Timeline timeline;
        const auto duration = [](const std::string&) { return 2.f; };
        timeline.setIdle("mrh_wardloop", 2.f);
        EXPECT_FLOAT_EQ(timeline.pick(10.f, duration).time, 2.f);
        EXPECT_FLOAT_EQ(timeline.pick(10.5f, duration).time, 2.5f);
        timeline.setIdle("mrh_wardloop", 5.f); // the loop does not change: the offset is not applied again
        EXPECT_FLOAT_EQ(timeline.pick(11.f, duration).time, 3.f);
        timeline.setIdle("mrh_idle", 1.f);
        EXPECT_FLOAT_EQ(timeline.pick(12.f, duration).time, 1.f);
    }

    TEST(SkyrimFirstPerson3Timeline, anOffsetThatIsNegativeOrNotANumberIsZero)
    {
        Timeline timeline;
        const auto duration = [](const std::string&) { return 2.f; };
        timeline.setIdle("a", -3.f);
        EXPECT_FLOAT_EQ(timeline.pick(4.f, duration).time, 0.f);
        timeline.setIdle("b", std::numeric_limits<float>::quiet_NaN());
        EXPECT_FLOAT_EQ(timeline.pick(5.f, duration).time, 0.f);
    }

    TEST(SkyrimFirstPerson3Timeline, theDefaultOffsetIsZeroAsBefore)
    {
        Timeline timeline;
        const auto duration = [](const std::string&) { return 2.f; };
        timeline.setIdle("mt_idle");
        EXPECT_FLOAT_EQ(timeline.pick(7.f, duration).time, 0.f);
        EXPECT_FLOAT_EQ(timeline.pick(7.25f, duration).time, 0.25f);
    }
}

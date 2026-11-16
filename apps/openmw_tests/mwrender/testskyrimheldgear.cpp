// Task 0220 (Skyrim actors show their gear): the engine half of the held weapon and shield
// (apps/openmw/mwrender/skyrimheldgear.*): which skeleton node a weapon waits at, the draw clock (a combat clip draws
// the weapon, eight seconds without one sheathes it), and the controller that shows the sheathed or the drawn copy.
// Written after the code and not mutation-checked. Nothing is read from disk.

#include <apps/openmw/mwrender/skyrimheldgear.hpp>

#include <gtest/gtest.h>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

namespace
{
    using namespace MWRender::SkyrimHeldGear;

    bool shown(const vsg::Switch& sw)
    {
        return !sw.children.empty() && sw.children[0].mask != vsg::MASK_OFF;
    }

    vsg::ref_ptr<vsg::Switch> oneChildSwitch(bool on)
    {
        auto sw = vsg::Switch::create();
        sw->addChild(on, vsg::Group::create());
        return sw;
    }
}

TEST(SkyrimHeldGearNodes, each_animation_type_waits_at_the_skeletons_own_node)
{
    EXPECT_EQ(sheathNodeFor(1), "WeaponSword");
    EXPECT_EQ(sheathNodeFor(2), "WeaponDagger");
    EXPECT_EQ(sheathNodeFor(3), "WeaponAxe");
    EXPECT_EQ(sheathNodeFor(4), "WeaponMace");
    EXPECT_EQ(sheathNodeFor(5), "WeaponBack");
    EXPECT_EQ(sheathNodeFor(6), "WeaponBack");
}

TEST(SkyrimHeldGearNodes, bare_hands_bows_and_unknown_types_have_no_sheath_node)
{
    for (std::uint8_t type : { 0, 7, 8, 9, 200 })
        EXPECT_TRUE(sheathNodeFor(type).empty()) << int(type);
}

TEST(SkyrimHeldGearClock, a_new_clock_is_sheathed)
{
    DrawClock clock;
    EXPECT_FALSE(clock.update(0.f));
    EXPECT_FALSE(clock.update(100.f));
}

TEST(SkyrimHeldGearClock, a_combat_clip_draws_the_weapon_for_the_hold_time)
{
    DrawClock clock;
    EXPECT_FALSE(clock.update(10.f));
    clock.note();
    EXPECT_TRUE(clock.update(10.1f));
    EXPECT_TRUE(clock.update(10.1f + kDrawnHoldSeconds - 0.5f));
    EXPECT_FALSE(clock.update(10.1f + kDrawnHoldSeconds + 0.5f));
}

TEST(SkyrimHeldGearClock, every_clip_renews_the_hold)
{
    DrawClock clock;
    clock.note();
    EXPECT_TRUE(clock.update(0.f));
    clock.note();
    EXPECT_TRUE(clock.update(kDrawnHoldSeconds - 1.f));
    clock.note();
    EXPECT_TRUE(clock.update(2.f * kDrawnHoldSeconds - 2.f));
    EXPECT_FALSE(clock.update(4.f * kDrawnHoldSeconds));
}

TEST(SkyrimHeldGearClock, a_clock_that_starts_again_keeps_at_most_the_hold)
{
    DrawClock clock;
    clock.note();
    EXPECT_TRUE(clock.update(1000.f));
    EXPECT_TRUE(clock.update(1.f)); // restarted: still inside the hold
    EXPECT_FALSE(clock.update(1.f + kDrawnHoldSeconds + 1.f));
}

TEST(SkyrimHeldGearController, shows_the_sheathed_copy_until_a_clip_starts_and_the_drawn_one_after)
{
    auto sheathed = oneChildSwitch(true);
    auto drawn = oneChildSwitch(false);
    auto controller = vsg::ref_ptr<Controller>(new Controller(sheathed, drawn));
    EXPECT_TRUE(controller->hints.autoPlay);
    auto target = vsg::Group::create();
    controller->run(*target, 0.f);
    EXPECT_TRUE(shown(*sheathed));
    EXPECT_FALSE(shown(*drawn));
    EXPECT_FALSE(controller->drawn());

    controller->note();
    controller->run(*target, 1.f);
    EXPECT_FALSE(shown(*sheathed));
    EXPECT_TRUE(shown(*drawn));
    EXPECT_TRUE(controller->drawn());

    controller->run(*target, 1.f + kDrawnHoldSeconds + 1.f);
    EXPECT_TRUE(shown(*sheathed));
    EXPECT_FALSE(shown(*drawn));
}

TEST(SkyrimHeldGearController, noteCombat_finds_the_controller_among_the_things_an_actor_keeps_alive)
{
    auto sheathed = oneChildSwitch(true);
    auto drawn = oneChildSwitch(false);
    auto controller = vsg::ref_ptr<Controller>(new Controller(sheathed, drawn));
    std::vector<vsg::ref_ptr<vsg::Object>> keep{ vsg::Group::create(), controller };
    noteCombat(keep);
    auto target = vsg::Group::create();
    controller->run(*target, 0.f);
    EXPECT_TRUE(controller->drawn());
    std::vector<vsg::ref_ptr<vsg::Object>> none;
    noteCombat(none); // no controller: nothing happens
}

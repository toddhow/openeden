// Tests for MWRender::PartControllers (task 0074): what Npc::updateParts links again after it emptied the controller
// lists. Written from the task spec before the implementation.

#include "apps/openmw/mwrender/partcontrollers.hpp"

#include <components/animation/controller.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace
{
    using MWRender::PartControllerList;

    constexpr std::size_t numParts = 8;
    constexpr std::size_t head = 0;
    constexpr std::size_t chest = 3;
    constexpr std::size_t hands = 5;
    using Log = MWRender::PartControllers<numParts>;

    struct DummyController : public Anim::Controller
    {
        void run(vsg::Object& /*target*/, float /*time*/) const override {}
    };

    struct PartControllersTest : public ::testing::Test
    {
        std::vector<vsg::ref_ptr<DummyController>> mControllers;
        std::vector<vsg::ref_ptr<vsg::Object>> mTargets;

        const Anim::Controller* controller()
        {
            mControllers.emplace_back(new DummyController);
            return mControllers.back().get();
        }

        vsg::Object* target()
        {
            mTargets.push_back(vsg::Object::create());
            return mTargets.back().get();
        }
    };

    using Linked = std::vector<std::pair<std::size_t, Log::Entry>>;

    Linked relink(Log& log)
    {
        Linked linked;
        log.relinkSurvivors([&](std::size_t part, const std::vector<Log::Entry>& entries) {
            for (const Log::Entry& entry : entries)
                linked.emplace_back(part, entry);
        });
        return linked;
    }

    void insertPart(Log& log, std::size_t part, const Anim::Controller* c, vsg::Object* t, PartControllerList list)
    {
        log.beginPart(part);
        log.record(part, c, t, list);
    }

    TEST_F(PartControllersTest, a_part_that_stays_is_linked_again_into_the_list_it_had)
    {
        Log log;
        const Anim::Controller* skin = controller();
        vsg::Object* geometry = target();
        insertPart(log, chest, skin, geometry, PartControllerList::Static);

        log.beginUpdate();
        const Linked linked = relink(log);

        ASSERT_EQ(linked.size(), 1u);
        EXPECT_EQ(linked[0].first, chest);
        EXPECT_EQ(linked[0].second.mController, skin);
        EXPECT_EQ(linked[0].second.mTarget, geometry);
        EXPECT_EQ(linked[0].second.mList, PartControllerList::Static);
    }

    TEST_F(PartControllersTest, a_part_inserted_during_the_update_is_not_linked_twice)
    {
        Log log;
        insertPart(log, chest, controller(), target(), PartControllerList::Static);

        log.beginUpdate();
        insertPart(log, chest, controller(), target(), PartControllerList::Static);

        EXPECT_TRUE(relink(log).empty());
    }

    TEST_F(PartControllersTest, a_removed_part_is_never_linked_again)
    {
        Log log;
        insertPart(log, chest, controller(), target(), PartControllerList::Static);

        log.beginUpdate();
        log.forget(chest);

        EXPECT_TRUE(relink(log).empty());
        EXPECT_TRUE(log.entries(chest).empty());
    }

    TEST_F(PartControllersTest, a_part_removed_before_the_update_stays_forgotten)
    {
        Log log;
        insertPart(log, chest, controller(), target(), PartControllerList::Static);
        log.forget(chest);

        log.beginUpdate();
        EXPECT_TRUE(relink(log).empty());
    }

    TEST_F(PartControllersTest, the_list_of_each_entry_is_kept)
    {
        Log log;
        log.beginPart(head);
        log.record(head, controller(), target(), PartControllerList::Face);
        log.beginPart(hands);
        log.record(hands, controller(), target(), PartControllerList::Static);
        log.record(hands, controller(), target(), PartControllerList::AutoPlay);

        log.beginUpdate();
        const Linked linked = relink(log);

        ASSERT_EQ(linked.size(), 3u);
        EXPECT_EQ(linked[0].second.mList, PartControllerList::Face);
        EXPECT_EQ(linked[1].second.mList, PartControllerList::Static);
        EXPECT_EQ(linked[2].second.mList, PartControllerList::AutoPlay);
    }

    TEST_F(PartControllersTest, parts_come_in_part_order_and_entries_in_the_order_they_were_recorded)
    {
        Log log;
        const Anim::Controller* handsFirst = controller();
        const Anim::Controller* handsSecond = controller();
        const Anim::Controller* chestOnly = controller();
        log.beginPart(hands);
        log.record(hands, handsFirst, target(), PartControllerList::Static);
        log.record(hands, handsSecond, target(), PartControllerList::Static);
        log.beginPart(chest);
        log.record(chest, chestOnly, target(), PartControllerList::Static);

        log.beginUpdate();
        const Linked linked = relink(log);

        ASSERT_EQ(linked.size(), 3u);
        EXPECT_EQ(linked[0].first, chest);
        EXPECT_EQ(linked[0].second.mController, chestOnly);
        EXPECT_EQ(linked[1].first, hands);
        EXPECT_EQ(linked[1].second.mController, handsFirst);
        EXPECT_EQ(linked[2].first, hands);
        EXPECT_EQ(linked[2].second.mController, handsSecond);
    }

    TEST_F(PartControllersTest, a_second_relink_in_the_same_update_links_nothing)
    {
        Log log;
        insertPart(log, chest, controller(), target(), PartControllerList::Static);

        log.beginUpdate();
        EXPECT_EQ(relink(log).size(), 1u);
        EXPECT_TRUE(relink(log).empty());
    }

    TEST_F(PartControllersTest, every_update_links_the_part_that_stays_again)
    {
        Log log;
        insertPart(log, chest, controller(), target(), PartControllerList::Static);

        for (int update = 0; update < 3; ++update)
        {
            log.beginUpdate();
            EXPECT_EQ(relink(log).size(), 1u) << "update " << update;
        }
    }

    TEST_F(PartControllersTest, inserting_a_part_again_replaces_its_entries)
    {
        Log log;
        insertPart(log, chest, controller(), target(), PartControllerList::Static);
        const Anim::Controller* replacement = controller();
        insertPart(log, chest, replacement, target(), PartControllerList::Static);

        ASSERT_EQ(log.entries(chest).size(), 1u);
        EXPECT_EQ(log.entries(chest)[0].mController, replacement);
    }

    TEST_F(PartControllersTest, a_part_with_no_controller_links_nothing)
    {
        Log log;
        log.beginPart(chest);

        log.beginUpdate();
        EXPECT_TRUE(relink(log).empty());
    }
}

// Tests for task 0074e, the fresh review of task 0074d (finding 3): the quiver is changed on the main thread, not in
// MWRender::Npc::update. update() runs on an update worker in the game, where compiling is not allowed, so a quiver
// change that show() asks for ([Game] weapon sheathing) stays pending until the main thread runs it
// (Npc::runPendingQuiver, which the game's updatePendingQuiver calls with updateQuiver): the change once, then the
// actor's real compile() once, also when the change throws, and then nothing is pending. Written from the task's test
// specification before the implementation and run red against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"

#include <components/mwanimation/context.hpp>
#include <components/mwanimation/wielding.hpp>
#include <components/settings/values.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    using PartsTestSupport::frameTime;
    using Wield = MWAnim::Wielding::Wield;

    // An Npc built without a record (Npc's test constructor, view mode Normal), with the quiver change in reach.
    struct QuiverNpc : public MWRender::Npc
    {
        explicit QuiverNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::mQuiverDirty;
        using MWRender::Npc::runPendingQuiver;
    };

    struct QuiverUpdateTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<QuiverNpc> mNpc;
        // [Game] weapon sheathing before the test (the test main initialises the settings).
        bool mWeaponSheathing = false;
        // How many times the quiver change ran.
        int mQuiverCalls = 0;

        void SetUp() override
        {
            mWeaponSheathing = Settings::game().mWeaponSheathing.get();
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
        }

        void TearDown() override { Settings::game().mWeaponSheathing.set(mWeaponSheathing); }

        // The actor, made with [Game] weapon sheathing set as given.
        QuiverNpc& makeNpc(bool weaponSheathing)
        {
            Settings::game().mWeaponSheathing.set(weaponSheathing);
            mNpc = std::make_unique<QuiverNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> skeleton = PartsTestSupport::makeSkeleton(
                { "Bip01 Head", "Weapon Bone", "Shield Bone", "Bip01 Weapon", "Bip01 AttachShield" });
            mNpc->animation = PartsTestSupport::npcPlay(*skeleton);
            return *mNpc;
        }
    };

    // Finding 3: a quiver change asked for by show() is not made in update(): several frames compile nothing (update
    // runs on an update worker in the game) and the change stays pending.
    TEST_F(QuiverUpdateTest, update_compiles_nothing_and_leaves_the_quiver_change_pending)
    {
        QuiverNpc& npc = makeNpc(true);
        npc.show(Wield::Weapon, true);
        ASSERT_TRUE(npc.mQuiverDirty);

        for (int i = 0; i < 5; ++i)
            npc.update(frameTime);

        EXPECT_EQ(mCompile.count(), 0);
        EXPECT_TRUE(npc.mQuiverDirty);
    }

    // Finding 3: the main thread runs the pending change: the quiver change once and the actor's real compile() once,
    // and nothing stays pending, so a second call runs neither.
    TEST_F(QuiverUpdateTest, the_main_thread_runs_the_pending_quiver_change_once_with_a_compile)
    {
        QuiverNpc& npc = makeNpc(true);
        npc.show(Wield::Weapon, true);
        auto quiver = [&] { ++mQuiverCalls; };

        npc.runPendingQuiver(quiver);

        EXPECT_EQ(mQuiverCalls, 1);
        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_FALSE(npc.mQuiverDirty);

        npc.runPendingQuiver(quiver);

        EXPECT_EQ(mQuiverCalls, 1);
        EXPECT_EQ(mCompile.count(), 1);
    }

    // Finding 3: showing the ammunition asks for a quiver change too, which the main thread runs.
    TEST_F(QuiverUpdateTest, showing_the_ammunition_asks_for_a_quiver_change_the_main_thread_runs)
    {
        QuiverNpc& npc = makeNpc(true);
        npc.show(Wield::Ammo, true);
        auto quiver = [&] { ++mQuiverCalls; };

        npc.runPendingQuiver(quiver);

        EXPECT_EQ(mQuiverCalls, 1);
        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_FALSE(npc.mQuiverDirty);
    }

    // Finding 3, guard: with nothing pending the main thread runs neither the quiver change nor a compile.
    TEST_F(QuiverUpdateTest, with_nothing_pending_the_main_thread_runs_neither_the_change_nor_a_compile)
    {
        QuiverNpc& npc = makeNpc(true);
        ASSERT_FALSE(npc.mQuiverDirty);
        auto quiver = [&] { ++mQuiverCalls; };

        npc.runPendingQuiver(quiver);

        EXPECT_EQ(mQuiverCalls, 0);
        EXPECT_EQ(mCompile.count(), 0);
    }

    // Finding 3, guard: with [Game] weapon sheathing off, showing the weapon or the ammunition asks for no quiver
    // change, so the main thread runs neither the change nor a compile.
    TEST_F(QuiverUpdateTest, with_weapon_sheathing_off_showing_asks_for_no_quiver_change)
    {
        QuiverNpc& npc = makeNpc(false);
        npc.show(Wield::Weapon, true);
        npc.show(Wield::Ammo, true);
        EXPECT_FALSE(npc.mQuiverDirty);
        auto quiver = [&] { ++mQuiverCalls; };

        npc.runPendingQuiver(quiver);

        EXPECT_EQ(mQuiverCalls, 0);
        EXPECT_EQ(mCompile.count(), 0);
    }

    // Finding 3: a quiver change that throws a std::exception does not reach the caller (the main thread's render
    // update, where it would end the game session); the actor's real compile() still runs once, since the nodes the
    // change attached before the throw are in the scene, and nothing stays pending.
    TEST_F(QuiverUpdateTest, a_quiver_change_that_throws_is_compiled_and_stops_there)
    {
        QuiverNpc& npc = makeNpc(true);
        npc.show(Wield::Weapon, true);
        // Read from data, so the compiler does not see a change that always throws (MSVC's C4702 on the code after
        // it).
        const std::vector<int> failures{ 1 };
        auto quiver = [&] {
            ++mQuiverCalls;
            if (!failures.empty())
                throw PartsTestSupport::UpdateError("the quiver change");
        };

        EXPECT_NO_THROW(npc.runPendingQuiver(quiver));

        EXPECT_EQ(mQuiverCalls, 1);
        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_FALSE(npc.mQuiverDirty);
    }
}

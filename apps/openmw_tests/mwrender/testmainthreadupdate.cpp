// Tests for task 0074f, the fresh review of task 0074e (findings 3 and 4): the main-thread step of the actors.
// MWAnim::Object::updateOnMainThread is called by MWRender::Scene::update for every ACTOR object once per frame, on the
// calling (main) thread, after every object's update() has finished on the update workers; MWRender::Player::update
// calls it on itself after Npc::update; MWRender::Npc overrides it to do the pending quiver change and its compile.
// Nothing may be compiled from a worker: CompileContext::compile updates the viewer without a lock. Written from the
// task's test specification before the implementation and run red against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/player.hpp"
#include "apps/openmw/mwrender/scene.hpp"

#include <components/mwanimation/context.hpp>
#include <components/mwanimation/object.hpp>
#include <components/mwanimation/wielding.hpp>
#include <components/settings/values.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    using Wield = MWAnim::Wielding::Wield;

    constexpr float sceneStep = 0.016f;

    // One call of an object's update or main-thread step: where it ran and when (a number from one counter shared by
    // all objects of a test).
    struct Call
    {
        std::thread::id mThread;
        int mSequence;
    };

    // An object that records its calls. update() runs on an update worker, updateOnMainThread() on the thread that
    // calls Scene::update; the scene waits for the workers between the two, so the vectors need no lock.
    class CountingObject : public MWAnim::Object
    {
    public:
        CountingObject(const MWAnim::Context& context, std::atomic<int>& sequence)
            : MWAnim::Object(context)
            , mSequence(sequence)
        {
        }

        void update(float /*dt*/) override { mUpdates.push_back(Call{ std::this_thread::get_id(), ++mSequence }); }
        void updateOnMainThread() override
        {
            mMainThreadSteps.push_back(Call{ std::this_thread::get_id(), ++mSequence });
        }

        std::vector<Call> mUpdates;
        std::vector<Call> mMainThreadSteps;

    private:
        std::atomic<int>& mSequence;
    };

    // The Ptr keys of a Scene's objects: a reference of its own for each (a light's, whose class the support header
    // registers), so that two objects do not collide.
    class PtrKeys
    {
    public:
        MWWorld::Ptr next()
        {
            mItems.push_back(std::make_unique<PartsTestSupport::LightItem>(0, "meshes/test/mainthread_key.nif"));
            return MWWorld::Ptr(&mItems.back()->mRef, nullptr);
        }

    private:
        std::vector<std::unique_ptr<PartsTestSupport::LightItem>> mItems;
    };

    struct MainThreadUpdateTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        PtrKeys mKeys;
        std::atomic<int> mSequence{ 0 };
        // [Game] weapon sheathing before the test (the test main initialises the settings).
        bool mWeaponSheathing = false;
        // Where each compile ran.
        std::vector<std::thread::id> mCompileThreads;
        // Declared after the context and the keys: it goes first, with the objects in it.
        std::unique_ptr<MWRender::Scene> mScene;

        void SetUp() override
        {
            mWeaponSheathing = Settings::game().mWeaponSheathing.get();
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
            mCompile.onCompile = [this] { mCompileThreads.push_back(std::this_thread::get_id()); };
            mScene = std::make_unique<MWRender::Scene>(1);
        }

        void TearDown() override
        {
            mCompile.onCompile = nullptr;
            Settings::game().mWeaponSheathing.set(mWeaponSheathing);
        }

        CountingObject& addCounting(bool actor)
        {
            auto object = std::make_unique<CountingObject>(mContext, mSequence);
            CountingObject& result = *object;
            mScene->addObject(mKeys.next(), std::move(object), actor);
            return result;
        }
    };

    // Findings 3 and 4: Scene::update calls each actor's update once on a worker thread, then, after the last update
    // has finished, each actor's updateOnMainThread once on the calling thread; objects that are not actors get the
    // update only. A second frame does the same again.
    TEST_F(MainThreadUpdateTest, the_scene_runs_the_main_thread_step_of_every_actor_after_all_updates)
    {
        CountingObject& first = addCounting(true);
        CountingObject& second = addCounting(true);
        CountingObject& notActor = addCounting(false);
        const std::thread::id testThread = std::this_thread::get_id();

        mScene->update(sceneStep);

        for (const CountingObject* actor : { &first, &second })
        {
            ASSERT_EQ(actor->mUpdates.size(), 1u);
            ASSERT_EQ(actor->mMainThreadSteps.size(), 1u);
            EXPECT_NE(actor->mUpdates[0].mThread, testThread) << "update runs on an update worker";
            EXPECT_EQ(actor->mMainThreadSteps[0].mThread, testThread);
        }
        const int lastUpdate = std::max(first.mUpdates[0].mSequence, second.mUpdates[0].mSequence);
        EXPECT_GT(first.mMainThreadSteps[0].mSequence, lastUpdate);
        EXPECT_GT(second.mMainThreadSteps[0].mSequence, lastUpdate);
        EXPECT_EQ(notActor.mUpdates.size(), 1u);
        EXPECT_TRUE(notActor.mMainThreadSteps.empty());

        mScene->update(sceneStep);

        for (const CountingObject* actor : { &first, &second })
        {
            ASSERT_EQ(actor->mUpdates.size(), 2u);
            ASSERT_EQ(actor->mMainThreadSteps.size(), 2u);
            EXPECT_EQ(actor->mMainThreadSteps[1].mThread, testThread);
            EXPECT_GT(actor->mMainThreadSteps[1].mSequence, first.mUpdates[1].mSequence);
            EXPECT_GT(actor->mMainThreadSteps[1].mSequence, second.mUpdates[1].mSequence);
        }
        EXPECT_EQ(notActor.mUpdates.size(), 2u);
        EXPECT_TRUE(notActor.mMainThreadSteps.empty());
    }

    // An Npc built without a record (Npc's test constructor, view mode Normal), with the quiver change in reach.
    struct MainThreadNpc : public MWRender::Npc
    {
        explicit MainThreadNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::mQuiverDirty;
    };

    // The actor, made with [Game] weapon sheathing on and a skeleton with the bones the sheathing needs.
    std::unique_ptr<MainThreadNpc> makeSheathingNpc(const MWAnim::Context& context)
    {
        Settings::game().mWeaponSheathing.set(true);
        auto npc = std::make_unique<MainThreadNpc>(context);
        const vsg::ref_ptr<vsg::Group> skeleton = PartsTestSupport::makeSkeleton(
            { "Bip01 Head", "Weapon Bone", "Shield Bone", "Bip01 Weapon", "Bip01 AttachShield" });
        npc->animation = PartsTestSupport::npcPlay(*skeleton);
        return npc;
    }

    // Finding 3: a quiver change that show() asked for is done in the scene's main-thread step: the compile happens
    // once, on the thread that calls Scene::update (never on a worker), and nothing stays pending. Without the call
    // to show() nothing compiles.
    TEST_F(MainThreadUpdateTest, the_scene_does_an_actors_pending_quiver_change_on_the_calling_thread)
    {
        std::unique_ptr<MainThreadNpc> made = makeSheathingNpc(mContext);
        MainThreadNpc& npc = *made;
        mScene->addObject(mKeys.next(), std::move(made), true);
        npc.show(Wield::Weapon, true);
        ASSERT_TRUE(npc.mQuiverDirty);
        const int compilesBefore = mCompile.count();

        mScene->update(sceneStep);

        EXPECT_EQ(mCompile.count() - compilesBefore, 1);
        EXPECT_FALSE(npc.mQuiverDirty);
        ASSERT_EQ(mCompileThreads.size(), 1u);
        EXPECT_EQ(mCompileThreads[0], std::this_thread::get_id());

        mScene->update(sceneStep);

        EXPECT_EQ(mCompile.count() - compilesBefore, 1);
    }

    // Finding 3, guard: with nothing asked for, the scene compiles nothing.
    TEST_F(MainThreadUpdateTest, the_scene_compiles_nothing_for_an_actor_with_no_pending_change)
    {
        std::unique_ptr<MainThreadNpc> made = makeSheathingNpc(mContext);
        MainThreadNpc& npc = *made;
        mScene->addObject(mKeys.next(), std::move(made), true);
        ASSERT_FALSE(npc.mQuiverDirty);

        mScene->update(sceneStep);
        mScene->update(sceneStep);

        EXPECT_EQ(mCompile.count(), 0);
        EXPECT_TRUE(mCompileThreads.empty());
    }

    // Finding 4, guard: the Npc's main-thread step is the pending quiver change and its compile, once.
    TEST_F(MainThreadUpdateTest, the_npcs_main_thread_step_does_the_pending_quiver_change)
    {
        std::unique_ptr<MainThreadNpc> made = makeSheathingNpc(mContext);
        MainThreadNpc& npc = *made;
        npc.show(Wield::Weapon, true);
        ASSERT_TRUE(npc.mQuiverDirty);

        npc.updateOnMainThread();

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_FALSE(npc.mQuiverDirty);

        npc.updateOnMainThread();

        EXPECT_EQ(mCompile.count(), 1);
    }

    // A Player built without a record or skeleton (Player's test constructor), with the quiver change in reach.
    struct MainThreadPlayer : public MWRender::Player
    {
        explicit MainThreadPlayer(const MWAnim::Context& mwContext)
            : MWRender::Player(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::mQuiverDirty;
    };

    // Finding 4: the player's update is on the calling thread and does the pending quiver change itself, after the
    // Npc update: one compile, on the calling thread; without the call to show() nothing compiles.
    TEST_F(MainThreadUpdateTest, the_players_update_does_the_pending_quiver_change_on_the_calling_thread)
    {
        Settings::game().mWeaponSheathing.set(true);
        MainThreadPlayer player(mContext);
        const vsg::ref_ptr<vsg::Group> skeleton = PartsTestSupport::makeSkeleton(
            { "Bip01 Head", "Weapon Bone", "Shield Bone", "Bip01 Weapon", "Bip01 AttachShield" });
        player.animation = PartsTestSupport::npcPlay(*skeleton);

        player.update(sceneStep);

        EXPECT_EQ(mCompile.count(), 0);

        player.show(Wield::Weapon, true);
        ASSERT_TRUE(player.mQuiverDirty);

        player.update(sceneStep);

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_FALSE(player.mQuiverDirty);
        ASSERT_EQ(mCompileThreads.size(), 1u);
        EXPECT_EQ(mCompileThreads[0], std::this_thread::get_id());
    }
}

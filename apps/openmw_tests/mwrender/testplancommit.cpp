// Tests for task 0074f, the fresh review of task 0074e (finding 2): an actor's type, view mode and part context change
// only after every file of the rebuild's plan has been read. MWRender::Npc::BasePlan carries what the plan was made
// for (mNpcType, mViewMode, mPartContext), applyBasePlan commits it after the reads (only what is set), and the
// callers (Npc::updateParts, Player::setViewMode) no longer set the actor's members first: a rebuild whose read throws
// leaves the old values, so the next call retries the rebuild instead of believing it was done. Written from the
// task's test specification before the implementation and run red against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/player.hpp"

#include <components/animation/bones.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/mwanimation/actor.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/mwanimation/play.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // The files the plan names (lower case, as the reader compares them).
    constexpr const char* skeletonFile = "meshes/test/commit_skeleton.nif";
    constexpr const char* customBonesFile = "meshes/test/commit_bones.nif";
    constexpr const char* animationFile = "meshes/test/xcommit_anim.kf";

    // An Npc built without a record, with the rebuild in reach, the plan hand-made and the three things that need a
    // record or the running game replaced: the type the actor should have, the plan, and the controller refresh.
    struct CommitNpc : public MWRender::Npc
    {
        explicit CommitNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::BasePlan;
        using MWRender::Npc::mNpcType;
        using MWRender::Npc::mPartContext;
        using MWRender::Npc::mViewMode;
        using NpcType = MWRender::Npc::NpcType;
        using MWRender::Npc::runRebuild;

        // The type getNpcType gives (the real one asks the record).
        NpcType mChosenType = NpcType::Normal;
        int mPlansMade = 0;
        int mRefreshes = 0;
        // The plan's files; empty names a plan that reads nothing.
        std::string mPlanSkeleton = skeletonFile;

        NpcType getNpcType() const override { return mChosenType; }

        BasePlan makeBasePlan(ViewMode view, const MWAnim::Context* partContext) override
        {
            ++mPlansMade;
            BasePlan plan;
            plan.mSkeleton = mPlanSkeleton;
            plan.mNpcType = mChosenType;
            plan.mViewMode = view;
            plan.mPartContext = partContext;
            return plan;
        }

        void refreshController() override { ++mRefreshes; }
    };

    struct PlanCommitTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        // The context a plan of this test names (a first-person context, for example).
        MWAnim::Context mOtherContext;
        // Declared after the contexts: it goes first.
        std::unique_ptr<CommitNpc> mNpc;
        // The actor before the rebuild.
        const MWAnim::Play* mOldPlay = nullptr;
        // A file that throws while mBreak is set, read through the reader's callback so a test can stop it again.
        std::string mBroken;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
            PartsTestSupport::setUpContext(mOtherContext, mCompile.context(), mReader);
            mReader->add(skeletonFile, PartsTestSupport::makeSkeleton({ "Bip01 Head", "Chest" }));
            mReader->add(customBonesFile, PartsTestSupport::makeCustomBones({ "Custom Bone" }));
            mReader->onRead = [this](const std::string& name) {
                if (!mBroken.empty() && name.ends_with(mBroken))
                    throw PartsTestSupport::ReadError("cannot read " + name);
            };
            mNpc = std::make_unique<CommitNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> oldSkeleton = PartsTestSupport::makeSkeleton({ "Bip01 Head", "Old Bone" });
            mNpc->animation = PartsTestSupport::npcPlay(*oldSkeleton);
            mNpc->assignBones();
            mOldPlay = mNpc->animation.get();
            ASSERT_EQ(mNpc->mNpcType, CommitNpc::NpcType::Normal);
            ASSERT_EQ(mNpc->mViewMode, MWRender::Npc::ViewMode::Normal);
        }

        // A plan that names all three kinds of file and what it was made for (a werewolf in first person, parts read
        // with the other context).
        CommitNpc::BasePlan plan() const
        {
            CommitNpc::BasePlan result;
            result.mSkeleton = skeletonFile;
            result.mCustomBones = { customBonesFile };
            result.mAnimations = { animationFile };
            result.mNpcType = CommitNpc::NpcType::Werewolf;
            result.mViewMode = MWRender::Npc::ViewMode::FirstPerson;
            result.mPartContext = &mOtherContext;
            return result;
        }

        // Finding 2: a read of `broken` throws; runRebuild passes it on and the actor keeps its type, view mode and
        // part context, and its Play.
        void expectNothingCommittedAfterAThrowAt(const std::string& broken)
        {
            CommitNpc& npc = *mNpc;
            const MWAnim::Context* const oldContext = npc.mPartContext;
            mBroken = broken;
            int partsCalls = 0;
            int refreshCalls = 0;
            const CommitNpc::BasePlan basePlan = plan();

            EXPECT_THROW(npc.runRebuild(
                             basePlan, [&] { ++partsCalls; }, [&] { ++refreshCalls; }),
                PartsTestSupport::ReadError)
                << broken;

            EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Normal) << broken;
            EXPECT_EQ(npc.mViewMode, MWRender::Npc::ViewMode::Normal) << broken;
            EXPECT_EQ(npc.mPartContext, oldContext) << broken;
            EXPECT_EQ(npc.animation.get(), mOldPlay) << broken;
            EXPECT_EQ(partsCalls, 0) << broken;
            EXPECT_EQ(refreshCalls, 0) << broken;
        }
    };

    // Finding 2: a throw reading the skeleton commits nothing.
    TEST_F(PlanCommitTest, a_throw_reading_the_skeleton_commits_no_type_view_or_context)
    {
        expectNothingCommittedAfterAThrowAt(skeletonFile);
    }

    // Finding 2: a throw reading the first custom-bone file commits nothing.
    TEST_F(PlanCommitTest, a_throw_reading_the_custom_bones_commits_no_type_view_or_context)
    {
        expectNothingCommittedAfterAThrowAt(customBonesFile);
    }

    // Finding 2: a throw reading an animation file commits nothing.
    TEST_F(PlanCommitTest, a_throw_reading_an_animation_commits_no_type_view_or_context)
    {
        expectNothingCommittedAfterAThrowAt(animationFile);
    }

    // Finding 2: a plan whose reads succeed commits all three, and the part update already sees them.
    TEST_F(PlanCommitTest, a_plan_whose_reads_succeed_commits_type_view_and_context_before_the_parts_run)
    {
        CommitNpc& npc = *mNpc;
        bool partsSawType = false;
        bool partsSawView = false;
        bool partsSawContext = false;
        int partsCalls = 0;
        auto parts = [&] {
            ++partsCalls;
            partsSawType = npc.mNpcType == CommitNpc::NpcType::Werewolf;
            partsSawView = npc.mViewMode == MWRender::Npc::ViewMode::FirstPerson;
            partsSawContext = npc.mPartContext == &mOtherContext;
        };
        int refreshCalls = 0;

        EXPECT_NO_THROW(npc.runRebuild(plan(), parts, [&] { ++refreshCalls; }));

        EXPECT_EQ(partsCalls, 1);
        EXPECT_TRUE(partsSawType);
        EXPECT_TRUE(partsSawView);
        EXPECT_TRUE(partsSawContext);
        EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Werewolf);
        EXPECT_EQ(npc.mViewMode, MWRender::Npc::ViewMode::FirstPerson);
        EXPECT_EQ(npc.mPartContext, &mOtherContext);
        EXPECT_EQ(refreshCalls, 1);
    }

    // Finding 2, guard: a plan with none of the three set leaves them as they were (the rebuilds of the existing
    // tests rely on it).
    TEST_F(PlanCommitTest, a_plan_with_none_of_the_three_set_leaves_them_as_they_were)
    {
        CommitNpc& npc = *mNpc;
        npc.mNpcType = CommitNpc::NpcType::Vampire;
        npc.mViewMode = MWRender::Npc::ViewMode::HeadOnly;
        npc.mPartContext = &mOtherContext;
        CommitNpc::BasePlan basePlan = plan();
        basePlan.mNpcType.reset();
        basePlan.mViewMode.reset();
        basePlan.mPartContext = nullptr;

        EXPECT_NO_THROW(npc.runRebuild(
            basePlan, [] {}, [] {}));

        EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Vampire);
        EXPECT_EQ(npc.mViewMode, MWRender::Npc::ViewMode::HeadOnly);
        EXPECT_EQ(npc.mPartContext, &mOtherContext);
        EXPECT_NE(npc.animation.get(), mOldPlay);
    }

    // Finding 2: a throw in the part update comes after the reads, so the new values are committed (the new Play is
    // in; the actor is consistent), and the refresh still ran.
    TEST_F(PlanCommitTest, a_throw_in_the_part_update_leaves_the_new_values_committed_and_refreshes)
    {
        CommitNpc& npc = *mNpc;
        int refreshCalls = 0;
        // Read from data, so the compiler does not see a part update that always throws (MSVC's C4702 on the code
        // after it).
        const std::vector<int> failures{ 1 };
        auto parts = [&] {
            if (!failures.empty())
                throw PartsTestSupport::UpdateError("the part update");
        };

        EXPECT_THROW(npc.runRebuild(plan(), parts, [&] { ++refreshCalls; }), PartsTestSupport::UpdateError);

        EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Werewolf);
        EXPECT_EQ(npc.mViewMode, MWRender::Npc::ViewMode::FirstPerson);
        EXPECT_EQ(npc.mPartContext, &mOtherContext);
        EXPECT_NE(npc.animation.get(), mOldPlay);
        EXPECT_EQ(refreshCalls, 1);
    }

    // True when `run` throws something other than a ReadError (the part update after the plan was applied fails on the
    // test actor's empty Ptr), false when it returns; a ReadError is rethrown to the test.
    template <class Run>
    bool throwsApartFromAReadError(Run&& run)
    {
        try
        {
            run();
        }
        catch (const PartsTestSupport::ReadError&)
        {
            throw;
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }

    // Finding 2, through updateParts: a type that differs from the actor's starts a rebuild; while its read throws
    // the actor keeps its old type, so every call retries the rebuild; once the reads work the type is committed and
    // the rebuild is not made again.
    TEST_F(PlanCommitTest, update_parts_retries_a_rebuild_whose_read_threw_until_it_succeeds)
    {
        CommitNpc& npc = *mNpc;
        npc.mChosenType = CommitNpc::NpcType::Werewolf;
        mBroken = skeletonFile;

        EXPECT_THROW(npc.updateParts(), PartsTestSupport::ReadError);
        EXPECT_EQ(npc.mPlansMade, 1);
        EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Normal);

        EXPECT_THROW(npc.updateParts(), PartsTestSupport::ReadError);
        EXPECT_EQ(npc.mPlansMade, 2);
        EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Normal);

        mBroken.clear();
        // The plan is applied; the part update then fails on the test actor's empty Ptr.
        EXPECT_TRUE(throwsApartFromAReadError([&] { npc.updateParts(); }));
        EXPECT_EQ(npc.mPlansMade, 3);
        EXPECT_EQ(npc.mNpcType, CommitNpc::NpcType::Werewolf);

        throwsApartFromAReadError([&] { npc.updateParts(); });
        EXPECT_EQ(npc.mPlansMade, 3);
    }

    // Finding 2, guard: a type equal to the actor's starts no rebuild.
    TEST_F(PlanCommitTest, update_parts_makes_no_plan_when_the_type_is_the_actors)
    {
        CommitNpc& npc = *mNpc;
        ASSERT_EQ(npc.getNpcType(), npc.mNpcType);

        throwsApartFromAReadError([&] { npc.updateParts(); });

        EXPECT_EQ(npc.mPlansMade, 0);
        EXPECT_EQ(npc.mRefreshes, 0);
    }

    // A Player built without a record or skeleton, with setViewMode and the plan replaced as above.
    struct CommitPlayer : public MWRender::Player
    {
        explicit CommitPlayer(const MWAnim::Context& mwContext)
            : MWRender::Player(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::mPartContext;
        using MWRender::Npc::mViewMode;

        int mPlansMade = 0;
        int mRefreshes = 0;
        // The part context of the latest plan (what setViewMode handed makeBasePlan).
        const MWAnim::Context* mContextOfLastPlan = nullptr;

        BasePlan makeBasePlan(ViewMode view, const MWAnim::Context* partContext) override
        {
            ++mPlansMade;
            mContextOfLastPlan = partContext;
            BasePlan plan;
            plan.mSkeleton = skeletonFile;
            plan.mViewMode = view;
            plan.mPartContext = partContext;
            return plan;
        }

        void refreshController() override { ++mRefreshes; }
    };

    struct PlanCommitPlayerTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<CommitPlayer> mPlayer;
        std::string mBroken;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
            mReader->add(skeletonFile, PartsTestSupport::makeSkeleton({ "Bip01 Head", "Chest" }));
            mReader->onRead = [this](const std::string& name) {
                if (!mBroken.empty() && name.ends_with(mBroken))
                    throw PartsTestSupport::ReadError("cannot read " + name);
            };
            mPlayer = std::make_unique<CommitPlayer>(mContext);
            const vsg::ref_ptr<vsg::Group> oldSkeleton = PartsTestSupport::makeSkeleton({ "Bip01 Head", "Old Bone" });
            mPlayer->animation = PartsTestSupport::npcPlay(*oldSkeleton);
            ASSERT_EQ(mPlayer->mViewMode, MWRender::Npc::ViewMode::Normal);
            ASSERT_EQ(mPlayer->mPartContext, &mContext);
        }
    };

    // Finding 2, through Player::setViewMode: a rebuild for the first-person view whose read throws leaves the view
    // mode and the part context as they were, so the next call is not taken for "already in that view" and rebuilds
    // again; once the reads work the view and the first-person context are committed.
    TEST_F(PlanCommitPlayerTest, set_view_mode_commits_the_view_and_context_only_after_the_reads)
    {
        CommitPlayer& player = *mPlayer;
        mBroken = skeletonFile;

        EXPECT_THROW(player.setViewMode(MWRender::Npc::ViewMode::FirstPerson), PartsTestSupport::ReadError);
        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::Normal);
        EXPECT_EQ(player.mPartContext, &mContext);
        EXPECT_EQ(player.mPlansMade, 1);

        EXPECT_THROW(player.setViewMode(MWRender::Npc::ViewMode::FirstPerson), PartsTestSupport::ReadError);
        EXPECT_EQ(player.mPlansMade, 2);
        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::Normal);
        EXPECT_EQ(player.mPartContext, &mContext);

        mBroken.clear();
        // The plan is applied; the part update then fails on the test player's empty Ptr.
        EXPECT_TRUE(throwsApartFromAReadError([&] { player.setViewMode(MWRender::Npc::ViewMode::FirstPerson); }));
        EXPECT_EQ(player.mPlansMade, 3);
        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::FirstPerson);
        EXPECT_NE(player.mContextOfLastPlan, nullptr);
        EXPECT_NE(player.mContextOfLastPlan, &mContext);
        EXPECT_EQ(player.mPartContext, player.mContextOfLastPlan);
    }

    // Finding 2, guard: the view the player is in already asks for nothing.
    TEST_F(PlanCommitPlayerTest, set_view_mode_to_the_current_view_does_nothing)
    {
        CommitPlayer& player = *mPlayer;

        player.setViewMode(MWRender::Npc::ViewMode::Normal);

        EXPECT_EQ(player.mPlansMade, 0);
        EXPECT_EQ(player.mRefreshes, 0);
        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::Normal);
        EXPECT_EQ(player.mPartContext, &mContext);
    }

    // Task 0074g: the player's first-person neck bone (Player::mNeck) points into the actor's Play, so every way that
    // gives the actor a new Play must give it the new neck too. A Player built without a record or skeleton, whose
    // plan is hand-made and whose view is first person (or not) as the test sets it.
    constexpr const char* neckSkeletonFile = "meshes/test/neck_skeleton.nif";

    struct NeckPlayer : public MWRender::Player
    {
        explicit NeckPlayer(const MWAnim::Context& mwContext)
            : MWRender::Player(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::BasePlan;
        using MWRender::Npc::mViewMode;
        using MWRender::Npc::runRebuild;
        using MWRender::Player::assignBones;
        using MWRender::Player::mNeck;

        int mRefreshes = 0;

        BasePlan makeBasePlan(ViewMode view, const MWAnim::Context* partContext) override
        {
            BasePlan plan;
            plan.mSkeleton = neckSkeletonFile;
            plan.mViewMode = view;
            plan.mPartContext = partContext;
            return plan;
        }

        void refreshController() override { ++mRefreshes; }
    };

    struct PlayerNeckTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<NeckPlayer> mPlayer;
        // The player before the rebuild: in first person, with the neck of its first Play.
        const MWAnim::Play* mOldPlay = nullptr;
        const Anim::Bone* mOldNeck = nullptr;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
            mReader->add(neckSkeletonFile, PartsTestSupport::makeSkeleton({ "Bip01 Head", "Bip01 Neck", "New Bone" }));
            mPlayer = std::make_unique<NeckPlayer>(mContext);
            NeckPlayer& player = *mPlayer;
            const vsg::ref_ptr<vsg::Group> oldSkeleton
                = PartsTestSupport::makeSkeleton({ "Bip01 Head", "Bip01 Neck", "Old Bone" });
            player.animation = PartsTestSupport::npcPlay(*oldSkeleton);
            player.mViewMode = MWRender::Npc::ViewMode::FirstPerson;
            // Player::assignBones itself, as Player::setViewMode and the constructor call it.
            player.assignBones();
            mOldPlay = player.animation.get();
            mOldNeck = player.mNeck;
            ASSERT_NE(mOldNeck, nullptr);
            ASSERT_EQ(mOldNeck, player.animation->bones.search("bip01 neck"));
        }

        // The player's neck is the neck bone of the player's own, new animation.
        void expectNeckOfTheNewAnimation(const char* what) const
        {
            const NeckPlayer& player = *mPlayer;
            ASSERT_NE(player.animation.get(), mOldPlay) << what;
            const Anim::Bone* const newNeck = player.animation->bones.search("bip01 neck");
            ASSERT_NE(newNeck, nullptr) << what;
            // The new Play is built before the old one is released, so its bones cannot reuse the old addresses.
            EXPECT_NE(newNeck, mOldNeck) << what;
            EXPECT_EQ(player.mNeck, newNeck) << what;
        }
    };

    // The base class' assignBones reaches Player's: this is the call Npc::applyBasePlan makes (its `this` is an
    // MWAnim::Actor). Without the dispatch it finds the head and spine and leaves the neck alone.
    TEST_F(PlayerNeckTest, assign_bones_through_the_base_class_finds_the_first_person_neck)
    {
        NeckPlayer& player = *mPlayer;
        player.mNeck = nullptr;

        static_cast<MWAnim::Actor&>(player).assignBones();

        EXPECT_EQ(player.mNeck, mOldNeck);
        EXPECT_NE(player.mNeck, nullptr);
    }

    // The same through the base class in the third person view: no neck.
    TEST_F(PlayerNeckTest, assign_bones_through_the_base_class_clears_the_neck_in_third_person)
    {
        NeckPlayer& player = *mPlayer;
        player.mViewMode = MWRender::Npc::ViewMode::Normal;

        static_cast<MWAnim::Actor&>(player).assignBones();

        EXPECT_EQ(player.mNeck, nullptr);
    }

    // The werewolf and vampire change (RenderManager::rebuildPtr -> Npc::rebuild -> runRebuild -> applyBasePlan) in
    // the first person view: the new Play's neck, not the freed old one.
    TEST_F(PlayerNeckTest, a_rebuild_of_a_first_person_player_gives_it_the_neck_of_the_new_animation)
    {
        NeckPlayer& player = *mPlayer;
        const NeckPlayer::BasePlan plan = player.makeBasePlan(MWRender::Npc::ViewMode::FirstPerson, &mContext);
        int partsCalls = 0;

        EXPECT_NO_THROW(player.runRebuild(
            plan, [&] { ++partsCalls; }, [&] { ++player.mRefreshes; }));

        EXPECT_EQ(partsCalls, 1);
        EXPECT_EQ(player.mRefreshes, 1);
        expectNeckOfTheNewAnimation("runRebuild");
        EXPECT_EQ(player.getHead(), player.animation->bones.search("bip01 head"));
    }

    // The same through Npc::rebuild(), which RenderManager::rebuildPtr calls. The test player's part update throws
    // on its empty Ptr, after the new Play is in; the neck is the new one all the same.
    TEST_F(PlayerNeckTest, rebuild_of_a_first_person_player_gives_it_the_neck_of_the_new_animation)
    {
        NeckPlayer& player = *mPlayer;

        throwsApartFromAReadError([&] { player.rebuild(); });

        EXPECT_EQ(player.mRefreshes, 1);
        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::FirstPerson);
        expectNeckOfTheNewAnimation("rebuild");
    }

    // Player::setViewMode(Normal) when the part update throws after the new Play is in: setViewMode's own
    // assignBones() is never reached, and the rebuild must have cleared the neck.
    TEST_F(PlayerNeckTest, set_view_mode_to_third_person_clears_the_neck_when_the_part_update_throws)
    {
        NeckPlayer& player = *mPlayer;

        throwsApartFromAReadError([&] { player.setViewMode(MWRender::Npc::ViewMode::Normal); });

        EXPECT_NE(player.animation.get(), mOldPlay);
        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::Normal);
        EXPECT_EQ(player.mNeck, nullptr);
    }

    // Player::setViewMode(FirstPerson) when the part update throws: the new Play's neck.
    TEST_F(PlayerNeckTest, set_view_mode_to_first_person_finds_the_neck_when_the_part_update_throws)
    {
        NeckPlayer& player = *mPlayer;
        player.mViewMode = MWRender::Npc::ViewMode::Normal;
        player.mNeck = nullptr;

        throwsApartFromAReadError([&] { player.setViewMode(MWRender::Npc::ViewMode::FirstPerson); });

        EXPECT_EQ(player.mViewMode, MWRender::Npc::ViewMode::FirstPerson);
        expectNeckOfTheNewAnimation("setViewMode");
    }

    // Guard: a rebuild in third person leaves no neck (the third person body has none to turn).
    TEST_F(PlayerNeckTest, a_rebuild_of_a_third_person_player_leaves_no_neck)
    {
        NeckPlayer& player = *mPlayer;
        player.mViewMode = MWRender::Npc::ViewMode::Normal;
        player.mNeck = nullptr;
        const NeckPlayer::BasePlan plan = player.makeBasePlan(MWRender::Npc::ViewMode::Normal, &mContext);

        EXPECT_NO_THROW(player.runRebuild(
            plan, [] {}, [] {}));

        EXPECT_NE(player.animation.get(), mOldPlay);
        EXPECT_EQ(player.mNeck, nullptr);
    }
}

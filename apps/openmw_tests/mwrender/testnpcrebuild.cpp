// Tests for task 0074e, the fresh review of task 0074d (finding 1): an NPC's rebuild (MWRender::Npc::runRebuild) reads
// the skeleton, the custom-bone files and the animations before it changes the actor. A read that throws reaches the
// caller and leaves the actor exactly as it was: its Play (the CharacterController holds it by a raw pointer), its
// bones, its head, its parts and their controllers. Without a throw the new Play is the actor's before the part update
// runs, the actor is compiled, and the CharacterController is given the new Play after both, also when the part update
// throws. Written from the task's test specification before the implementation and run red against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"

#include <components/animation/bones.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/mwanimation/play.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using PartsTestSupport::frameTime;

    // The files the rebuild's plan names (lower case, as the reader compares them).
    constexpr const char* newSkeletonFile = "meshes/test/rebuild_skeleton.nif";
    constexpr const char* customBonesFileA = "meshes/test/rebuild_bones_a.nif";
    constexpr const char* customBonesFileB = "meshes/test/rebuild_bones_b.nif";
    constexpr const char* animationFileA = "meshes/test/xrebuild_anim_a.kf";
    constexpr const char* animationFileB = "meshes/test/xrebuild_anim_b.kf";
    // The part inserted before the rebuild.
    constexpr const char* groinFile = "meshes/test/rebuild_groin.nif";
    // The models the actor names before the rebuild and the ones the plan names.
    constexpr const char* oldHeadModel = "meshes/test/old_head.nif";
    constexpr const char* oldHairModel = "meshes/test/old_hair.nif";
    constexpr const char* newHeadModel = "meshes/test/new_head.nif";
    constexpr const char* newHairModel = "meshes/test/new_hair.nif";

    // An Npc built without a record (Npc's test constructor), with the rebuild and the part functions in reach.
    struct RebuildNpc : public MWRender::Npc
    {
        explicit RebuildNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::BasePlan;
        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::mDefaultSkeleton;
        using MWRender::Npc::mHairModel;
        using MWRender::Npc::mHeadModel;
        using MWRender::Npc::mParts;
        using MWRender::Npc::mStaticControllers;
        using MWRender::Npc::runPartUpdate;
        using MWRender::Npc::runRebuild;
    };

    // Finding 1 (and 3, 5): the counting compile context, through which the tests see the actor's real compile(),
    // counts a compile: its viewer has no window, so the compile succeeds without Vulkan and calls onCompiled.
    TEST(PartsTestSupportCompile, a_counting_compile_context_counts_one_compile_of_a_group)
    {
        PartsTestSupport::CountingCompile compile;
        EXPECT_EQ(compile.count(), 0);
        EXPECT_TRUE(compile.context()->compile(vsg::Group::create()));
        EXPECT_EQ(compile.count(), 1);
    }

    struct NpcRebuildTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<RebuildNpc> mNpc;

        // The controller of the part inserted before the rebuild: a groin's, not auto-play (the static list).
        vsg::ref_ptr<PartsTestSupport::Recorder> mOldPartController = PartsTestSupport::listRecorder();
        // The actor before the rebuild.
        const MWAnim::Play* mOldPlay = nullptr;
        const Anim::Bone* mOldHead = nullptr;
        const Anim::Bone* mOldGroin = nullptr;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
            mReader->add(newSkeletonFile, PartsTestSupport::makeSkeleton({ "Bip01 Head", "Groin", "New Bone" }));
            mReader->add(customBonesFileA, PartsTestSupport::makeCustomBones({ "Custom Bone A" }));
            mReader->add(customBonesFileB, PartsTestSupport::makeCustomBones({ "Custom Bone B" }));
            mReader->add(groinFile, PartsTestSupport::makeModel({ mOldPartController }));

            mNpc = std::make_unique<RebuildNpc>(mContext);
            RebuildNpc& npc = *mNpc;
            const vsg::ref_ptr<vsg::Group> oldSkeleton
                = PartsTestSupport::makeSkeleton({ "Bip01 Head", "Groin", "Old Bone" });
            npc.animation = PartsTestSupport::npcPlay(*oldSkeleton);
            npc.assignBones();
            npc.mHeadModel = oldHeadModel;
            npc.mHairModel = oldHairModel;
            npc.mDefaultSkeleton = false;
            npc.runPartUpdate([&] {
                npc.insertBoundedPart(
                    ESM::PRT_Groin, groinFile, MWAnim::sPartBones.at(ESM::PRT_Groin), std::nullopt, nullptr);
            });

            ASSERT_FALSE(npc.mParts[ESM::PRT_Groin].empty());
            ASSERT_EQ(PartsTestSupport::countIn(npc.mStaticControllers, mOldPartController.get()), 1u);
            ASSERT_NE(npc.getHead(), nullptr);
            mOldPlay = npc.animation.get();
            mOldHead = npc.getHead();
            mOldGroin = npc.animation->bones.search("Groin");
            ASSERT_NE(mOldGroin, nullptr);
        }

        static RebuildNpc::BasePlan plan()
        {
            RebuildNpc::BasePlan result;
            result.mSkeleton = newSkeletonFile;
            result.mCustomBones = { customBonesFileA, customBonesFileB };
            result.mAnimations = { animationFileA, animationFileB };
            result.mHeadModel = newHeadModel;
            result.mHairModel = newHairModel;
            result.mDefaultSkeleton = true;
            return result;
        }

        // Finding 1: a read that throws reaches the caller unchanged, and the actor is exactly as it was; neither the
        // part update nor the refresh is called.
        void expectUnchangedAfterAThrowAt(const std::string& broken)
        {
            mReader->breakWithReadError(broken);
            RebuildNpc& npc = *mNpc;
            int partsCalls = 0;
            int refreshCalls = 0;
            const RebuildNpc::BasePlan basePlan = plan();
            auto parts = [&] { ++partsCalls; };
            auto refresh = [&] { ++refreshCalls; };

            EXPECT_THROW(npc.runRebuild(basePlan, parts, refresh), PartsTestSupport::ReadError) << broken;

            EXPECT_EQ(partsCalls, 0) << broken;
            EXPECT_EQ(refreshCalls, 0) << broken;
            EXPECT_EQ(npc.mHeadModel, oldHeadModel) << broken;
            EXPECT_EQ(npc.mHairModel, oldHairModel) << broken;
            EXPECT_FALSE(npc.mDefaultSkeleton) << broken;
            EXPECT_EQ(npc.getHead(), mOldHead) << broken;
            EXPECT_FALSE(npc.mParts[ESM::PRT_Groin].empty()) << broken;
            EXPECT_EQ(PartsTestSupport::countIn(npc.mStaticControllers, mOldPartController.get()), 1u) << broken;
            // The same Play object: the CharacterController's pointer to it stays valid. The bones and an update are
            // only looked at on it (another Play's bones may not be assigned).
            ASSERT_EQ(npc.animation.get(), mOldPlay) << broken;
            EXPECT_EQ(npc.animation->bones.search("Groin"), mOldGroin) << broken;
            EXPECT_EQ(npc.animation->bones.search("New Bone"), nullptr) << broken;
            EXPECT_EQ(npc.animation->bones.search("Custom Bone A"), nullptr) << broken;
            EXPECT_EQ(npc.animation->bones.search("Custom Bone B"), nullptr) << broken;
            const std::size_t runs = mOldPartController->mTimes.size();
            npc.update(frameTime);
            EXPECT_EQ(mOldPartController->mTimes.size(), runs + 1) << broken;
        }
    };

    // Finding 1: a throw reading the skeleton leaves the actor as it was.
    TEST_F(NpcRebuildTest, a_throw_reading_the_skeleton_leaves_the_actor_as_it_was)
    {
        expectUnchangedAfterAThrowAt(newSkeletonFile);
    }

    // Finding 1: a throw reading the first custom-bone file leaves the actor as it was.
    TEST_F(NpcRebuildTest, a_throw_reading_the_first_custom_bone_file_leaves_the_actor_as_it_was)
    {
        expectUnchangedAfterAThrowAt(customBonesFileA);
    }

    // Finding 1: a throw reading the second custom-bone file leaves the actor as it was.
    TEST_F(NpcRebuildTest, a_throw_reading_the_second_custom_bone_file_leaves_the_actor_as_it_was)
    {
        expectUnchangedAfterAThrowAt(customBonesFileB);
    }

    // Finding 1: a throw reading the first animation file leaves the actor as it was.
    TEST_F(NpcRebuildTest, a_throw_reading_the_first_animation_file_leaves_the_actor_as_it_was)
    {
        expectUnchangedAfterAThrowAt(animationFileA);
    }

    // Finding 1: a throw reading the last of two animation files (the first gave no animation) leaves the actor as
    // it was.
    TEST_F(NpcRebuildTest, a_throw_reading_the_last_animation_file_leaves_the_actor_as_it_was)
    {
        expectUnchangedAfterAThrowAt(animationFileB);
    }

    // Finding 1: without a throw every read comes before the actor changes; then the part update runs once with the
    // new Play already the actor's, the actor's real compile() once, and the refresh once after both. The actor ends
    // with a new Play holding the new skeleton's bones and the custom bones, the plan's models, and none of the parts
    // it had.
    TEST_F(NpcRebuildTest, a_rebuild_reads_everything_first_then_runs_the_parts_the_compile_and_the_refresh)
    {
        RebuildNpc& npc = *mNpc;
        // Each read, with whether the actor was still as before the rebuild then.
        struct Read
        {
            std::string mName;
            bool mOldPlay;
            bool mOldPartKept;
            bool mOldModels;
        };
        std::vector<Read> reads;
        mReader->onRead = [&](const std::string& name) {
            reads.push_back(Read{ name, npc.animation.get() == mOldPlay, !npc.mParts[ESM::PRT_Groin].empty(),
                npc.mHeadModel == oldHeadModel && npc.mHairModel == oldHairModel && !npc.mDefaultSkeleton });
        };
        // The calls after the reads, in order.
        std::vector<std::string> calls;
        mCompile.onCompile = [&] { calls.emplace_back("compile"); };
        bool partsSawNewBones = false;
        bool partsSawNewHead = false;
        auto parts = [&] {
            calls.emplace_back("parts");
            partsSawNewBones = npc.animation != nullptr && npc.animation->bones.search("New Bone") != nullptr
                && npc.animation->bones.search("Old Bone") == nullptr;
            partsSawNewHead = npc.animation != nullptr && npc.getHead() != nullptr
                && npc.getHead() == npc.animation->bones.search("Bip01 Head");
        };
        auto refresh = [&] { calls.emplace_back("refresh"); };
        const RebuildNpc::BasePlan basePlan = plan();
        const int compilesBefore = mCompile.count();

        EXPECT_NO_THROW(npc.runRebuild(basePlan, parts, refresh));
        mReader->onRead = nullptr;
        mCompile.onCompile = nullptr;

        std::vector<std::string> names;
        for (const Read& read : reads)
        {
            names.push_back(read.mName);
            EXPECT_TRUE(read.mOldPlay) << read.mName << " was read after the actor's Play changed";
            EXPECT_TRUE(read.mOldPartKept) << read.mName << " was read after the actor's parts were removed";
            EXPECT_TRUE(read.mOldModels) << read.mName << " was read after the actor's models changed";
        }
        for (const char* file : { newSkeletonFile, customBonesFileA, customBonesFileB, animationFileA, animationFileB })
        {
            EXPECT_NE(std::find(names.begin(), names.end(), file), names.end()) << file << " was not read";
        }
        const auto firstA = std::find(names.begin(), names.end(), customBonesFileA);
        const auto firstB = std::find(names.begin(), names.end(), customBonesFileB);
        EXPECT_LT(std::distance(names.begin(), firstA), std::distance(names.begin(), firstB))
            << "the custom-bone files are read in the plan's order";

        EXPECT_EQ(calls, (std::vector<std::string>{ "parts", "compile", "refresh" }));
        EXPECT_EQ(mCompile.count() - compilesBefore, 1);
        EXPECT_TRUE(partsSawNewBones);
        EXPECT_TRUE(partsSawNewHead);

        ASSERT_NE(npc.animation, nullptr);
        // The old Play was alive while the new one was made from the bones read, so a new object has another address.
        EXPECT_NE(npc.animation.get(), mOldPlay);
        EXPECT_NE(npc.animation->bones.search("New Bone"), nullptr);
        EXPECT_EQ(npc.animation->bones.search("Old Bone"), nullptr);
        EXPECT_NE(npc.animation->bones.search("Custom Bone A"), nullptr);
        EXPECT_NE(npc.animation->bones.search("Custom Bone B"), nullptr);
        EXPECT_NE(npc.getHead(), nullptr);
        EXPECT_EQ(npc.getHead(), npc.animation->bones.search("Bip01 Head"));
        EXPECT_EQ(npc.mHeadModel, newHeadModel);
        EXPECT_EQ(npc.mHairModel, newHairModel);
        EXPECT_TRUE(npc.mDefaultSkeleton);
        EXPECT_TRUE(npc.mParts[ESM::PRT_Groin].empty());
        EXPECT_EQ(PartsTestSupport::countIn(npc.mStaticControllers, mOldPartController.get()), 0u);
        const std::size_t runs = mOldPartController->mTimes.size();
        npc.update(frameTime);
        EXPECT_EQ(mOldPartController->mTimes.size(), runs);
    }

    // Finding 1: when the part update throws, the new Play is already the actor's: the exception reaches the caller,
    // the actor's real compile() still runs once, and the refresh is still called once (the CharacterController must
    // be given the new Play, or it keeps the freed one).
    TEST_F(NpcRebuildTest, when_the_part_update_throws_the_actor_is_compiled_and_refreshed_with_its_new_play)
    {
        RebuildNpc& npc = *mNpc;
        int partsCalls = 0;
        int refreshCalls = 0;
        bool refreshSawNewPlay = false;
        // Read from data, so the compiler does not see a part update that always throws (MSVC's C4702 on the code
        // after it).
        const std::vector<int> failures{ 1 };
        auto parts = [&] {
            ++partsCalls;
            if (!failures.empty())
                throw PartsTestSupport::UpdateError("the part update");
        };
        auto refresh = [&] {
            ++refreshCalls;
            refreshSawNewPlay = npc.animation != nullptr && npc.animation->bones.search("New Bone") != nullptr;
        };
        const RebuildNpc::BasePlan basePlan = plan();
        const int compilesBefore = mCompile.count();

        EXPECT_THROW(npc.runRebuild(basePlan, parts, refresh), PartsTestSupport::UpdateError);

        EXPECT_EQ(partsCalls, 1);
        EXPECT_EQ(mCompile.count() - compilesBefore, 1);
        EXPECT_EQ(refreshCalls, 1);
        EXPECT_TRUE(refreshSawNewPlay);
        ASSERT_NE(npc.animation, nullptr);
        EXPECT_NE(npc.animation->bones.search("New Bone"), nullptr);
        EXPECT_NE(npc.animation->bones.search("Custom Bone A"), nullptr);
        EXPECT_NE(npc.animation->bones.search("Custom Bone B"), nullptr);
        EXPECT_NE(npc.getHead(), nullptr);
        EXPECT_EQ(npc.getHead(), npc.animation->bones.search("Bip01 Head"));
    }
}

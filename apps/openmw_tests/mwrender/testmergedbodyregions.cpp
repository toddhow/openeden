// Tests for MWRender::MergedBodies::applyHiddenRegions (task 0069 stage e review, findings 1 to 3). A template and an
// actor's copy are built by hand from plain VSG nodes (no device), the copy as MWAnim::decorate leaves it: its own
// root, cull nodes and state groups (with its own bone matrices), sharing the template's draw. Written before the
// change that applies the regions again to a copy changed before, and run red against the code before it.

#include "apps/openmw/mwrender/mergedbody.hpp"

#include <components/body/mergedruntime.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/sets.hpp>

#include <vsg/core/Array.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorSet.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace
{
    using MWRender::MergedBodies::applyHiddenRegions;
    using MWRender::MergedBodies::RegionChange;
    using Runs = std::vector<std::vector<Body::SlotRun>>;

    // A state group with its own bone matrices at BONE_BINDING, as the merged body's draws have (mergedbody.cpp).
    vsg::ref_ptr<vsg::StateGroup> stateGroupWithBones(vsg::ref_ptr<vsg::mat4Array> bones)
    {
        vsg::Descriptors descriptors{ vsg::DescriptorBuffer::create(
            bones, Pipeline::Descriptors::BONE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) };
        auto set = vsg::DescriptorSet::create(vsg::ref_ptr<vsg::DescriptorSetLayout>(), descriptors);
        auto group = vsg::StateGroup::create();
        group->stateCommands
            = { vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, nullptr, Pipeline::TEXTURE_SET, set) };
        return group;
    }

    struct Template
    {
        vsg::ref_ptr<vsg::Group> mRoot;
        std::vector<vsg::ref_ptr<vsg::VertexIndexDraw>> mDraws;
        Runs mRuns;
    };

    // One draw per entry of `runs`; with `wide`, each draw has 70,000 vertices and 32-bit indices that use them.
    Template makeTemplate(const Runs& runs, bool wide = false)
    {
        Template result;
        result.mRoot = vsg::Group::create();
        result.mRuns = runs;
        for (const auto& draw : runs)
        {
            std::uint32_t count = 0;
            for (const Body::SlotRun& run : draw)
                count += run.mIndexCount;
            const std::uint32_t vertices = wide ? 70000 : 64;
            auto geometry = vsg::VertexIndexDraw::create();
            geometry->assignArrays({ vsg::vec3Array::create(vertices) });
            if (wide)
            {
                auto indices = vsg::uintArray::create(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    (*indices)[i] = (i * 7919u) % vertices;
                geometry->assignIndices(indices);
            }
            else
            {
                auto indices = vsg::ushortArray::create(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    (*indices)[i] = static_cast<std::uint16_t>(i % vertices);
                geometry->assignIndices(indices);
            }
            geometry->indexCount = count;
            geometry->instanceCount = 1;
            auto group = stateGroupWithBones(vsg::mat4Array::create(4));
            group->children = { geometry };
            result.mRoot->addChild(vsg::CullNode::create(vsg::dsphere(0.0, 0.0, 0.0, 100.0), group));
            result.mDraws.push_back(geometry);
        }
        return result;
    }

    // The actor's copy: its own root, cull nodes and state groups (with their own bone matrices), the same draws.
    vsg::ref_ptr<vsg::Group> copyOf(const Template& model)
    {
        auto root = vsg::Group::create();
        for (const auto& child : model.mRoot->children)
        {
            const auto& cull = static_cast<const vsg::CullNode&>(*child);
            const auto& group = static_cast<const vsg::StateGroup&>(*cull.child);
            auto copy = stateGroupWithBones(vsg::mat4Array::create(4));
            copy->children = group.children;
            root->addChild(vsg::CullNode::create(cull.bound, copy));
        }
        return root;
    }

    vsg::StateGroup& groupOf(vsg::Group& root, std::size_t draw)
    {
        auto& cull = static_cast<vsg::CullNode&>(*root.children[draw]);
        return static_cast<vsg::StateGroup&>(*cull.child);
    }

    const vsg::Object* bonesOf(vsg::Group& root, std::size_t draw)
    {
        const auto& bind = static_cast<const vsg::BindDescriptorSet&>(*groupOf(root, draw).stateCommands[0]);
        const auto& buffer = static_cast<const vsg::DescriptorBuffer&>(*bind.descriptorSet->descriptors[0]);
        return buffer.bufferInfoList[0]->data.get();
    }

    // VertexIndexDraw::indexType is protected; a pointer to it taken through a derived class reads it from any draw.
    struct IndexTypeReader : vsg::VertexIndexDraw
    {
        static VkIndexType of(const vsg::VertexIndexDraw& draw) { return draw.*(&IndexTypeReader::indexType); }
    };

    VkIndexType indexTypeOf(const vsg::Node& node)
    {
        return IndexTypeReader::of(static_cast<const vsg::VertexIndexDraw&>(node));
    }

    struct Range
    {
        std::uint32_t mFirst;
        std::uint32_t mCount;
        bool operator==(const Range&) const = default;
    };

    // The index ranges a draw of the copy draws, checking that every part shares the template draw's buffers.
    std::vector<Range> rangesOf(vsg::Group& root, const Template& model, std::size_t draw)
    {
        std::vector<Range> ranges;
        const vsg::VertexIndexDraw& source = *model.mDraws[draw];
        for (const auto& child : groupOf(root, draw).children)
        {
            const auto* part = dynamic_cast<const vsg::VertexIndexDraw*>(child.get());
            EXPECT_NE(part, nullptr);
            if (part == nullptr)
                continue;
            EXPECT_EQ(part->indices, source.indices); // the same buffer info: nothing uploaded again
            EXPECT_EQ(part->arrays, source.arrays);
            EXPECT_EQ(IndexTypeReader::of(*part), IndexTypeReader::of(source));
            EXPECT_EQ(part->instanceCount, 1u);
            ranges.push_back({ part->firstIndex, part->indexCount });
        }
        return ranges;
    }

    // Three draws: slots 2, 3, 4 in the first; slot 3 alone in the second; slots 4 and 5 in the third.
    const Runs sRuns{ { { 2, 0, 30 }, { 3, 30, 60 }, { 4, 90, 30 } }, { { 3, 0, 48 } },
        { { 4, 0, 12 }, { 5, 12, 6 } } };

    TEST(MergedBodyCopyRegions, nothing_hidden_leaves_the_copy_as_it_is)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, {}, change));
        for (std::size_t i = 0; i < sRuns.size(); ++i)
        {
            ASSERT_EQ(groupOf(*copy, i).children.size(), 1u);
            EXPECT_EQ(groupOf(*copy, i).children[0], model.mDraws[i]);
        }
        EXPECT_EQ(change.mDrawn, (std::vector<bool>{ true, true, true }));
        EXPECT_TRUE(change.mRemoved.empty());
    }

    TEST(MergedBodyCopyRegions, a_hidden_slot_leaves_the_ranges_that_remain)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3 }, change));
        EXPECT_EQ(rangesOf(*copy, model, 0), (std::vector<Range>{ { 0, 30 }, { 90, 30 } }));
        EXPECT_TRUE(groupOf(*copy, 1).children.empty());
        ASSERT_EQ(groupOf(*copy, 2).children.size(), 1u);
        EXPECT_EQ(groupOf(*copy, 2).children[0], model.mDraws[2]); // untouched
        EXPECT_EQ(change.mDrawn, (std::vector<bool>{ true, false, true }));
        // The two draws taken out of the copy (the template's own, which it still has), for the caller to detach.
        EXPECT_EQ(change.mRemoved.size(), 2u);
        for (std::size_t i = 0; i < sRuns.size(); ++i)
        {
            ASSERT_EQ(groupOf(*model.mRoot, i).children.size(), 1u);
            EXPECT_EQ(groupOf(*model.mRoot, i).children[0], model.mDraws[i]);
        }
    }

    TEST(MergedBodyCopyRegions, sixteen_bit_indices_stay_sixteen_bit)
    {
        const Template model = makeTemplate(sRuns);
        ASSERT_EQ(IndexTypeReader::of(*model.mDraws[0]), VK_INDEX_TYPE_UINT16);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 2 }, change));
        for (const auto& child : groupOf(*copy, 0).children)
            EXPECT_EQ(indexTypeOf(*child), VK_INDEX_TYPE_UINT16);
    }

    TEST(MergedBodyCopyRegions, thirty_two_bit_indices_keep_their_type)
    {
        // VSG's VertexIndexDraw copy constructor does not copy indexType (it stays VK_INDEX_TYPE_UINT16): a part of a
        // body with more than 65,535 vertices would read its 32-bit indices as pairs of 16-bit ones.
        const Template model = makeTemplate(sRuns, true);
        ASSERT_EQ(IndexTypeReader::of(*model.mDraws[0]), VK_INDEX_TYPE_UINT32);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3, 5 }, change));
        EXPECT_EQ(rangesOf(*copy, model, 0), (std::vector<Range>{ { 0, 30 }, { 90, 30 } }));
        EXPECT_EQ(rangesOf(*copy, model, 2), (std::vector<Range>{ { 0, 12 } }));
        for (const std::size_t draw : { std::size_t{ 0 }, std::size_t{ 2 } })
        {
            for (const auto& child : groupOf(*copy, draw).children)
                EXPECT_EQ(indexTypeOf(*child), VK_INDEX_TYPE_UINT32) << draw;
        }
    }

    TEST(MergedBodyCopyRegions, the_regions_change_again_on_a_copy_changed_before)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3 }, change));
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 4 }, change));
        EXPECT_EQ(rangesOf(*copy, model, 0), (std::vector<Range>{ { 0, 90 } }));
        EXPECT_EQ(rangesOf(*copy, model, 1), (std::vector<Range>{ { 0, 48 } }));
        EXPECT_EQ(rangesOf(*copy, model, 2), (std::vector<Range>{ { 12, 6 } }));
        EXPECT_EQ(change.mDrawn, (std::vector<bool>{ true, true, true }));
        // Replaced: draw 0's two parts and draw 2's whole draw; draw 1 had none.
        EXPECT_EQ(change.mRemoved.size(), 3u);

        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, {}, change));
        EXPECT_EQ(rangesOf(*copy, model, 0), (std::vector<Range>{ { 0, 120 } }));
        EXPECT_EQ(rangesOf(*copy, model, 1), (std::vector<Range>{ { 0, 48 } }));
        EXPECT_EQ(rangesOf(*copy, model, 2), (std::vector<Range>{ { 0, 18 } }));
        EXPECT_EQ(change.mDrawn, (std::vector<bool>{ true, true, true }));
    }

    // Task 0069 stage e, second review (finding 6): a draw of the copy that nothing hides any more draws the template's
    // own draw again, as a copy that was never changed does, and the draws it replaced go to the caller. Written before
    // the change, run red against the code before it (which made a new draw of the whole range).
    TEST(MergedBodyCopyRegions, the_template_draw_comes_back_when_nothing_is_hidden_again)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3 }, change));
        // Draw 1 is slot 3 alone: hidden entirely, then drawn again whole.
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 4 }, change));
        ASSERT_EQ(groupOf(*copy, 1).children.size(), 1u);
        EXPECT_EQ(groupOf(*copy, 1).children[0], model.mDraws[1]);

        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, {}, change));
        for (std::size_t i = 0; i < sRuns.size(); ++i)
        {
            ASSERT_EQ(groupOf(*copy, i).children.size(), 1u) << i;
            EXPECT_EQ(groupOf(*copy, i).children[0], model.mDraws[i]) << i;
        }
        EXPECT_EQ(change.mDrawn, (std::vector<bool>{ true, true, true }));
        // Replaced: draw 0's one part (0 to 90) and draw 2's one part (12 to 18); draw 1 already had the template's.
        ASSERT_EQ(change.mRemoved.size(), 2u);
        for (const auto& removed : change.mRemoved)
        {
            for (const auto& draw : model.mDraws)
                EXPECT_NE(removed, draw);
        }
        // The template itself never changes.
        for (std::size_t i = 0; i < sRuns.size(); ++i)
        {
            ASSERT_EQ(groupOf(*model.mRoot, i).children.size(), 1u);
            EXPECT_EQ(groupOf(*model.mRoot, i).children[0], model.mDraws[i]);
        }
    }

    TEST(MergedBodyCopyRegions, the_same_hidden_set_again_changes_nothing)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3 }, change));
        const auto before = groupOf(*copy, 0).children;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3 }, change));
        EXPECT_EQ(groupOf(*copy, 0).children, before);
        EXPECT_TRUE(change.mRemoved.empty());
        EXPECT_EQ(change.mDrawn, (std::vector<bool>{ true, false, true }));
    }

    TEST(MergedBodyCopyRegions, each_draw_names_the_bone_matrices_of_the_copy)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        RegionChange change;
        ASSERT_TRUE(applyHiddenRegions(*copy, *model.mRoot, model.mRuns, { 3 }, change));
        ASSERT_EQ(change.mBoneMatrices.size(), sRuns.size());
        for (std::size_t i = 0; i < sRuns.size(); ++i)
        {
            EXPECT_EQ(change.mBoneMatrices[i], bonesOf(*copy, i)) << i;
            EXPECT_NE(change.mBoneMatrices[i], bonesOf(*model.mRoot, i)) << i;
        }
    }

    // Every refusal leaves the copy exactly as it was.
    void expectRefused(vsg::Group& copy, const Template& model, const Runs& runs, const std::vector<int>& hidden)
    {
        std::vector<std::decay_t<decltype(vsg::StateGroup::children)>> before;
        for (std::size_t i = 0; i < copy.children.size(); ++i)
            before.push_back(groupOf(copy, i).children);
        RegionChange change;
        EXPECT_FALSE(applyHiddenRegions(copy, *model.mRoot, runs, hidden, change));
        for (std::size_t i = 0; i < copy.children.size(); ++i)
            EXPECT_EQ(groupOf(copy, i).children, before[i]) << i;
        EXPECT_TRUE(change.mRemoved.empty());
    }

    TEST(MergedBodyCopyRegions, a_copy_that_shares_a_state_group_is_refused)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        static_cast<vsg::CullNode&>(*copy->children[1]).child
            = static_cast<vsg::CullNode&>(*model.mRoot->children[1]).child;
        expectRefused(*copy, model, model.mRuns, { 3 });
    }

    TEST(MergedBodyCopyRegions, runs_that_do_not_fit_the_template_are_refused)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        Runs fewer(model.mRuns.begin(), model.mRuns.begin() + 2);
        expectRefused(*copy, model, fewer, { 3 });
        Runs shorter = model.mRuns;
        shorter[0].back().mIndexCount -= 3; // the last run no longer ends at the draw's index count
        expectRefused(*copy, model, shorter, { 3 });
    }

    TEST(MergedBodyCopyRegions, a_copy_with_a_foreign_draw_is_refused)
    {
        const Template model = makeTemplate(sRuns);
        auto copy = copyOf(model);
        auto foreign = vsg::VertexIndexDraw::create();
        foreign->assignArrays({ vsg::vec3Array::create(3) });
        foreign->assignIndices(vsg::ushortArray::create(3));
        foreign->indexCount = 3;
        groupOf(*copy, 2).children = { foreign };
        expectRefused(*copy, model, model.mRuns, { 3 });
    }
}

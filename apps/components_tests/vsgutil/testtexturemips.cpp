// Tests for vsgUtil::TextureMips::Swapper (task 0080, stage d, first slice): the registry of texture users and the
// swap of one texture image. No device is needed: the compile and retire hooks are fakes that record what they
// were given. Written from the d0 findings of the task spec before the code: a swap builds a NEW bind (the old one is
// never edited, it may be in flight), every group that held the old bind points at the new one, groups sharing one
// bind share the new one, the old bind goes to the retire hook, the full image comes back on a restore.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/texturemips.hpp>

#include "texturetestutil.hpp"

#include <vsg/nodes/CullGroup.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/Image.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/Sampler.h>
#include <vsg/state/StateSwitch.h>

#include <vsgXchange/all.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    using namespace TextureTest;
    namespace TM = Render::VideoMemory::TextureMips;
    using vsgUtil::TextureMips::Hooks;
    using vsgUtil::TextureMips::Swapper;

    struct Recorder
    {
        std::vector<vsg::ref_ptr<vsg::Object>> compiled;
        std::vector<vsg::ref_ptr<vsg::Object>> retired;
        bool failCompile = false;

        Hooks hooks()
        {
            Hooks h;
            h.compile = [this](const vsg::ref_ptr<vsg::Object>& o) {
                if (failCompile)
                    return false;
                compiled.push_back(o);
                return true;
            };
            h.retire = [this](vsg::ref_ptr<vsg::Object> o) { retired.push_back(std::move(o)); };
            return h;
        }
    };

    vsg::ref_ptr<vsg::Data> bigTexture(uint32_t size, uint32_t mips)
    {
        auto data = readDds(makeDds(DdsFormat::Bc1Legacy, size, size, mips));
        EXPECT_TRUE(data.valid());
        return data;
    }

    vsg::ref_ptr<vsg::DescriptorImage> imageDescriptor(vsg::ref_ptr<vsg::Data> data, uint32_t binding)
    {
        auto image = vsg::Image::create(data);
        image->usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto view = vsg::ImageView::create(image);
        auto info = vsg::ImageInfo::create(vsg::Sampler::create(), view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return vsg::DescriptorImage::create(info, binding);
    }

    vsg::ref_ptr<vsg::BindDescriptorSet> makeBind(vsg::ref_ptr<vsg::Data> data, uint32_t binding = 0)
    {
        vsg::Descriptors descriptors = { imageDescriptor(data, binding) };
        auto set = vsg::DescriptorSet::create(vsg::ref_ptr<vsg::DescriptorSetLayout>(), descriptors);
        return vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, nullptr, 2, set);
    }

    vsg::ref_ptr<vsg::StateGroup> makeGroup(vsg::ref_ptr<vsg::StateCommand> command)
    {
        auto group = vsg::StateGroup::create();
        group->stateCommands = { command };
        return group;
    }

    const vsg::Data* boundData(const vsg::StateCommand& command, size_t index = 0)
    {
        const auto* bind = command.cast<vsg::BindDescriptorSet>();
        if (!bind || !bind->descriptorSet || bind->descriptorSet->descriptors.size() <= index)
            return nullptr;
        const auto* image = bind->descriptorSet->descriptors[index]->cast<vsg::DescriptorImage>();
        return image->imageInfoList[0]->imageView->image->data.get();
    }

    TM::Params params()
    {
        return TM::Params{};
    }

    TEST(TextureMipsSwapper, a_scan_finds_a_texture_of_2048_and_up)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_EQ(textures[0].width, 2048u);
        EXPECT_EQ(textures[0].height, 2048u);
        EXPECT_EQ(textures[0].mipLevels, 12u);
        EXPECT_EQ(textures[0].blockWidth, 4u);
        EXPECT_EQ(textures[0].blockBytes, 8u);
        EXPECT_EQ(textures[0].drop, 0);
    }

    TEST(TextureMipsSwapper, a_small_texture_is_not_tracked)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(512, 10))));
        EXPECT_TRUE(swapper.scan(*root).empty());
    }

    TEST(TextureMipsSwapper, a_texture_without_a_chain_is_not_tracked)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(2048, 1))));
        EXPECT_TRUE(swapper.scan(*root).empty());
    }

    TEST(TextureMipsSwapper, groups_sharing_one_image_are_one_texture)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto bind = makeBind(data);
        auto root = vsg::Group::create();
        root->addChild(makeGroup(bind));
        root->addChild(makeGroup(bind));
        root->addChild(makeGroup(makeBind(data))); // another bind, the same image
        EXPECT_EQ(swapper.scan(*root).size(), 1u);
    }

    TEST(TextureMipsSwapper, a_group_reached_by_two_parents_is_scanned_once)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto group = makeGroup(makeBind(bigTexture(2048, 12)));
        auto root = vsg::Group::create();
        root->addChild(group);
        root->addChild(group);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        const auto result = swapper.swap(textures[0].id, 1, 10);
        EXPECT_EQ(result.groups, 1);
    }

    TEST(TextureMipsSwapper, a_swap_points_the_group_at_a_new_bind_with_the_reduced_image)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto oldBind = makeBind(data);
        auto group = makeGroup(oldBind);
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto textures = swapper.scan(*root);
        const auto result = swapper.swap(textures[0].id, 1, 10);
        ASSERT_TRUE(result.ok);
        EXPECT_EQ(result.groups, 1);
        EXPECT_EQ(result.binds, 1);
        ASSERT_EQ(group->stateCommands.size(), 1u);
        EXPECT_NE(group->stateCommands[0].get(), oldBind.get());
        const vsg::Data* now = boundData(*group->stateCommands[0]);
        ASSERT_NE(now, nullptr);
        EXPECT_NE(now, data.get());
        EXPECT_EQ(now->properties.mipLevels, 11);
        EXPECT_EQ(now->properties.format, data->properties.format);
        const auto extents = now->pixelExtents();
        EXPECT_EQ(std::get<0>(extents), 1024u);
    }

    TEST(TextureMipsSwapper, the_old_bind_is_never_edited)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto oldBind = makeBind(data);
        auto oldSet = oldBind->descriptorSet;
        auto root = vsg::Group::create();
        root->addChild(makeGroup(oldBind));
        const auto textures = swapper.scan(*root);
        swapper.swap(textures[0].id, 1, 10);
        EXPECT_EQ(oldBind->descriptorSet.get(), oldSet.get());
        EXPECT_EQ(boundData(*oldBind), data.get());
    }

    TEST(TextureMipsSwapper, the_new_bind_keeps_the_slot_and_set_number_of_the_old_one)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto oldBind = makeBind(bigTexture(2048, 12));
        oldBind->slot = 7;
        auto group = makeGroup(oldBind);
        auto root = vsg::Group::create();
        root->addChild(group);
        swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        const auto* bind = group->stateCommands[0]->cast<vsg::BindDescriptorSet>();
        ASSERT_NE(bind, nullptr);
        EXPECT_EQ(bind->slot, 7u);
        EXPECT_EQ(bind->firstSet, 2u);
        EXPECT_EQ(bind->pipelineBindPoint, VK_PIPELINE_BIND_POINT_GRAPHICS);
    }

    TEST(TextureMipsSwapper, the_new_bind_is_compiled_and_the_old_one_retired)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto oldBind = makeBind(bigTexture(2048, 12));
        auto group = makeGroup(oldBind);
        auto root = vsg::Group::create();
        root->addChild(group);
        swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        ASSERT_EQ(recorder.compiled.size(), 1u);
        EXPECT_EQ(recorder.compiled[0].get(), group->stateCommands[0].get());
        ASSERT_EQ(recorder.retired.size(), 1u);
        EXPECT_EQ(recorder.retired[0].get(), oldBind.get());
    }

    TEST(TextureMipsSwapper, groups_sharing_a_bind_share_the_new_one)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto bind = makeBind(bigTexture(2048, 12));
        auto a = makeGroup(bind);
        auto b = makeGroup(bind);
        auto root = vsg::Group::create();
        root->addChild(a);
        root->addChild(b);
        const auto result = swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        EXPECT_EQ(result.groups, 2);
        EXPECT_EQ(result.binds, 1);
        EXPECT_EQ(a->stateCommands[0].get(), b->stateCommands[0].get());
        EXPECT_EQ(recorder.compiled.size(), 1u);
        EXPECT_EQ(recorder.retired.size(), 1u);
    }

    TEST(TextureMipsSwapper, a_state_switch_child_is_replaced)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto oldBind = makeBind(bigTexture(2048, 12));
        auto choice = vsg::StateSwitch::create();
        choice->add(1, oldBind);
        auto group = makeGroup(choice);
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        const auto result = swapper.swap(textures[0].id, 1, 10);
        EXPECT_EQ(result.groups, 1);
        ASSERT_EQ(choice->children.size(), 1u);
        EXPECT_NE(choice->children[0].stateCommand.get(), oldBind.get());
        EXPECT_EQ(choice->children[0].mask, 1u);
    }

    TEST(TextureMipsSwapper, a_swap_back_restores_the_full_image_itself)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto group = makeGroup(makeBind(data));
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto id = swapper.scan(*root)[0].id;
        swapper.swap(id, 1, 10);
        const auto again = swapper.scan(*root);
        ASSERT_EQ(again.size(), 1u);
        EXPECT_EQ(again[0].id, id);
        EXPECT_EQ(again[0].drop, 1);
        const auto result = swapper.swap(id, 0, 400);
        ASSERT_TRUE(result.ok);
        EXPECT_EQ(boundData(*group->stateCommands[0]), data.get());
        EXPECT_EQ(swapper.scan(*root)[0].drop, 0);
    }

    TEST(TextureMipsSwapper, a_swap_to_the_drop_already_held_changes_nothing)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto bind = makeBind(bigTexture(2048, 12));
        auto group = makeGroup(bind);
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto result = swapper.swap(swapper.scan(*root)[0].id, 0, 10);
        EXPECT_EQ(result.groups, 0);
        EXPECT_EQ(group->stateCommands[0].get(), bind.get());
        EXPECT_TRUE(recorder.compiled.empty());
        EXPECT_TRUE(recorder.retired.empty());
    }

    TEST(TextureMipsSwapper, a_drop_below_the_floor_is_refused)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto bind = makeBind(bigTexture(2048, 12));
        auto group = makeGroup(bind);
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto result = swapper.swap(swapper.scan(*root)[0].id, 2, 10); // 2048 -> 512 is under the floor of 1024
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(group->stateCommands[0].get(), bind.get());
    }

    TEST(TextureMipsSwapper, an_unknown_texture_is_refused)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        EXPECT_FALSE(swapper.swap(99, 1, 0).ok);
    }

    TEST(TextureMipsSwapper, a_failed_compile_leaves_the_groups_alone)
    {
        Recorder recorder;
        recorder.failCompile = true;
        Swapper swapper(recorder.hooks(), params());
        auto bind = makeBind(bigTexture(2048, 12));
        auto group = makeGroup(bind);
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto result = swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(group->stateCommands[0].get(), bind.get());
        EXPECT_TRUE(recorder.retired.empty());
    }

    TEST(TextureMipsSwapper, a_group_that_is_gone_is_skipped)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        auto kept = makeGroup(makeBind(bigTexture(2048, 12)));
        root->addChild(kept);
        auto gone = makeGroup(makeBind(bigTexture(2048, 12)));
        root->addChild(gone);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 2u);
        root->children.pop_back();
        gone = nullptr;
        const auto result = swapper.swap(textures[1].id, 1, 10);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.groups, 0);
    }

    TEST(TextureMipsSwapper, a_bind_with_two_big_images_keeps_the_other_one)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto a = bigTexture(2048, 12);
        auto b = bigTexture(2048, 12);
        vsg::Descriptors descriptors = { imageDescriptor(a, 0), imageDescriptor(b, 1) };
        auto set = vsg::DescriptorSet::create(vsg::ref_ptr<vsg::DescriptorSetLayout>(), descriptors);
        auto bind = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, nullptr, 2, set);
        auto group = makeGroup(bind);
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 2u);
        swapper.swap(textures[0].id, 1, 10);
        ASSERT_NE(boundData(*group->stateCommands[0], 0), nullptr);
        EXPECT_NE(boundData(*group->stateCommands[0], 0), a.get());
        EXPECT_EQ(boundData(*group->stateCommands[0], 1), b.get());
        const auto* second = group->stateCommands[0]
                                 ->cast<vsg::BindDescriptorSet>()
                                 ->descriptorSet->descriptors[1]
                                 ->cast<vsg::DescriptorImage>();
        EXPECT_EQ(second->dstBinding, 1u);
        // The second texture swaps in the new bind.
        swapper.swap(textures[1].id, 1, 10);
        EXPECT_NE(boundData(*group->stateCommands[0], 1), b.get());
        EXPECT_NE(boundData(*group->stateCommands[0], 0), a.get());
    }

    TEST(TextureMipsSwapper, the_image_usage_and_sampler_are_kept)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto oldBind = makeBind(data);
        const auto* oldInfo = oldBind->descriptorSet->descriptors[0]->cast<vsg::DescriptorImage>();
        auto sampler = oldInfo->imageInfoList[0]->sampler;
        const auto usage = oldInfo->imageInfoList[0]->imageView->image->usage;
        auto group = makeGroup(oldBind);
        auto root = vsg::Group::create();
        root->addChild(group);
        swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        const auto* now = group->stateCommands[0]
                              ->cast<vsg::BindDescriptorSet>()
                              ->descriptorSet->descriptors[0]
                              ->cast<vsg::DescriptorImage>();
        EXPECT_EQ(now->imageInfoList[0]->sampler.get(), sampler.get());
        EXPECT_EQ(now->imageInfoList[0]->imageView->image->usage, usage);
        EXPECT_EQ(now->imageInfoList[0]->imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TEST(TextureMipsSwapper, a_texture_loaded_later_at_full_size_is_seen_at_drop_zero)
    {
        // The shared-object cache hands a model loaded after a swap the full-size bind.
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto first = makeGroup(makeBind(data));
        auto root = vsg::Group::create();
        root->addChild(first);
        const auto id = swapper.scan(*root)[0].id;
        swapper.swap(id, 1, 10);
        auto later = makeGroup(makeBind(data));
        root->addChild(later);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_EQ(textures[0].drop, 0); // the least reduced user decides, so the planner reduces it again
        const auto result = swapper.swap(id, 1, 500);
        EXPECT_EQ(result.groups, 1); // only the late group changes
    }

    TEST(TextureMipsSwapper, resident_bytes_follow_the_swap)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(data)));
        const auto textures = swapper.scan(*root);
        const std::int64_t full = swapper.residentBytes();
        EXPECT_EQ(full, TM::chainBytes(textures[0], 0));
        const auto result = swapper.swap(textures[0].id, 1, 10);
        EXPECT_EQ(result.bytesBefore - result.bytesAfter, TM::levelBytes(textures[0], 0));
        EXPECT_EQ(swapper.residentBytes(), TM::chainBytes(textures[0], 1));
    }

    TEST(TextureMipsSwapper, a_swap_records_the_frame_for_the_hysteresis)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        const auto id = swapper.scan(*root)[0].id;
        EXPECT_FALSE(swapper.scan(*root)[0].changedBefore);
        swapper.swap(id, 1, 1234);
        const auto after = swapper.scan(*root);
        EXPECT_TRUE(after[0].changedBefore);
        EXPECT_EQ(after[0].lastChangeFrame, 1234u);
    }

    TEST(TextureMipsSwapper, a_texture_that_left_the_scene_is_forgotten)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        EXPECT_EQ(swapper.scan(*root).size(), 1u);
        root->children.clear();
        EXPECT_TRUE(swapper.scan(*root).empty());
        EXPECT_EQ(swapper.textureCount(), 0u);
    }

    TEST(TextureMipsSwapper, ids_do_not_change_between_scans)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        const auto first = swapper.scan(*root);
        const auto second = swapper.scan(*root);
        ASSERT_EQ(first.size(), 2u);
        ASSERT_EQ(second.size(), 2u);
        EXPECT_EQ(first[0].id, second[0].id);
        EXPECT_EQ(first[1].id, second[1].id);
        EXPECT_NE(first[0].id, first[1].id);
    }

    TEST(TextureMipsReduction, a_swapped_texture_is_handed_out_reduced_to_later_readers)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto group = makeGroup(makeBind(data));
        auto root = vsg::Group::create();
        root->addChild(group);
        EXPECT_FALSE(vsgUtil::TextureMips::activeReduction(data.get()).valid());
        const auto id = swapper.scan(*root)[0].id;
        swapper.swap(id, 1, 10);
        const auto reduced = vsgUtil::TextureMips::activeReduction(data.get());
        ASSERT_TRUE(reduced.valid());
        EXPECT_EQ(reduced.get(), boundData(*group->stateCommands[0]));
        EXPECT_EQ(reduced->properties.mipLevels, 11);
    }

    TEST(TextureMipsReduction, a_restore_ends_it)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(data)));
        const auto id = swapper.scan(*root)[0].id;
        swapper.swap(id, 1, 10);
        swapper.swap(id, 0, 400);
        EXPECT_FALSE(vsgUtil::TextureMips::activeReduction(data.get()).valid());
    }

    TEST(TextureMipsReduction, the_reduction_follows_the_latest_drop)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(4096, 13);
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(data)));
        const auto id = swapper.scan(*root)[0].id;
        swapper.swap(id, 1, 10);
        const auto first = vsgUtil::TextureMips::activeReduction(data.get());
        swapper.swap(id, 2, 400);
        const auto second = vsgUtil::TextureMips::activeReduction(data.get());
        ASSERT_TRUE(second.valid());
        EXPECT_NE(first.get(), second.get());
        EXPECT_EQ(second->properties.mipLevels, 11);
    }

    TEST(TextureMipsReduction, a_texture_that_left_the_scene_is_no_longer_reduced_for_readers)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(data)));
        swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        root->children.clear();
        swapper.scan(*root);
        EXPECT_FALSE(vsgUtil::TextureMips::activeReduction(data.get()).valid());
    }

    TEST(TextureMipsReduction, a_failed_swap_sets_no_reduction)
    {
        Recorder recorder;
        recorder.failCompile = true;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(data)));
        swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        EXPECT_FALSE(vsgUtil::TextureMips::activeReduction(data.get()).valid());
    }

    TEST(TextureMipsCachedModels, the_models_of_a_shared_object_cache_are_listed)
    {
        auto shared = vsg::SharedObjects::create();
        auto options = vsg::Options::create();
        options->sharedObjects = shared;
        auto model = vsg::Group::create();
        shared->add(model, "meshes/a.nif", options);
        auto other = vsg::Group::create();
        shared->add(other, "meshes/b.nif", options);
        const auto models = vsgUtil::TextureMips::cachedModels(*shared);
        ASSERT_EQ(models.size(), 2u);
        const bool hasModel = models[0].get() == model.get() || models[1].get() == model.get();
        const bool hasOther = models[0].get() == other.get() || models[1].get() == other.get();
        EXPECT_TRUE(hasModel);
        EXPECT_TRUE(hasOther);
    }

    TEST(TextureMipsCachedModels, an_empty_cache_lists_nothing)
    {
        auto shared = vsg::SharedObjects::create();
        EXPECT_TRUE(vsgUtil::TextureMips::cachedModels(*shared).empty());
    }

    TEST(TextureMipsCachedModels, a_cached_model_that_is_gone_is_left_out)
    {
        auto shared = vsg::SharedObjects::create();
        auto options = vsg::Options::create();
        {
            auto model = vsg::Group::create();
            shared->add(model, "meshes/a.nif", options);
        }
        shared->prune(); // nothing else holds the model: the cache lets it go
        EXPECT_TRUE(vsgUtil::TextureMips::cachedModels(*shared).empty());
    }

    TEST(TextureMipsCachedModels, a_template_holding_a_bind_is_reached_by_the_swapper)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto oldBind = makeBind(data);
        auto templateGroup = makeGroup(oldBind);
        auto templateRoot = vsg::Group::create();
        templateRoot->addChild(templateGroup);
        auto shared = vsg::SharedObjects::create();
        auto options = vsg::Options::create();
        shared->add(templateRoot, "meshes/a.nif", options);
        auto sceneGroup = makeGroup(oldBind); // the scene's own group, the same bind
        auto root = vsg::Group::create();
        root->addChild(sceneGroup);
        for (const auto& model : vsgUtil::TextureMips::cachedModels(*shared))
            root->addChild(model);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        const auto result = swapper.swap(textures[0].id, 1, 10);
        EXPECT_EQ(result.groups, 2);
        EXPECT_EQ(templateGroup->stateCommands[0].get(), sceneGroup->stateCommands[0].get());
    }

    TEST(TextureMipsReduction, a_later_read_of_the_file_gets_the_reduced_image_and_a_restore_the_full_one)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "openeden_mips_read_test.dds";
        {
            const auto bytes = makeDds(DdsFormat::Bc1Legacy, 2048, 2048, 12);
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        auto options = vsg::Options::create();
        options->readerWriters.push_back(vsgXchange::all::create());
        options->sharedObjects = vsg::SharedObjects::create();

        auto first = vsgUtil::readOptionalImage(path.string(), options);
        ASSERT_TRUE(first.valid());
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(first)));
        const auto id = swapper.scan(*root)[0].id;

        auto unchanged = vsgUtil::readOptionalImage(path.string(), options);
        EXPECT_EQ(unchanged.get(), first.get());

        swapper.swap(id, 1, 10);
        auto reduced = vsgUtil::readOptionalImage(path.string(), options);
        ASSERT_TRUE(reduced.valid());
        EXPECT_NE(reduced.get(), first.get());
        EXPECT_EQ(reduced->properties.mipLevels, 11);

        swapper.swap(id, 0, 400);
        auto full = vsgUtil::readOptionalImage(path.string(), options);
        EXPECT_EQ(full.get(), first.get());
        std::filesystem::remove(path);
    }

    // The use signal: a CullNode sphere (moved by the transforms above it) near the camera.
    vsg::ref_ptr<vsg::Node> placed(vsg::dvec3 at, vsg::ref_ptr<vsg::Node> child, double radius = 10.0)
    {
        auto cull = vsg::CullNode::create(vsg::dsphere(vsg::dvec3(0, 0, 0), radius), child);
        auto transform = vsg::MatrixTransform::create(vsg::translate(at));
        transform->addChild(cull);
        return transform;
    }

    TEST(TextureMipsUse, a_group_inside_a_near_sphere_makes_the_texture_visible)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(placed({ 1000, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12)))));
        const auto textures = swapper.scan(*root, vsg::dvec3(0, 0, 0), 77);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_TRUE(textures[0].visible);
        EXPECT_EQ(textures[0].lastUsedFrame, 77u);
    }

    TEST(TextureMipsUse, a_far_sphere_is_not_visible_and_has_no_last_use)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(placed({ 9000, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12)))));
        const auto textures = swapper.scan(*root, vsg::dvec3(0, 0, 0), 77);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_FALSE(textures[0].visible);
        EXPECT_EQ(textures[0].lastUsedFrame, 0u);
    }

    TEST(TextureMipsUse, the_distance_is_to_the_surface_of_the_sphere)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params()); // visibleDistance 3000
        auto root = vsg::Group::create();
        root->addChild(placed({ 3400, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12))), 500.0));
        EXPECT_TRUE(swapper.scan(*root, vsg::dvec3(0, 0, 0), 1)[0].visible); // 3400 - 500 = 2900
        auto other = vsg::Group::create();
        other->addChild(placed({ 3600, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12))), 500.0));
        EXPECT_FALSE(swapper.scan(*other, vsg::dvec3(0, 0, 0), 1)[0].visible); // 3100
    }

    TEST(TextureMipsUse, transforms_above_the_sphere_add_up)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto outer = vsg::MatrixTransform::create(vsg::translate(vsg::dvec3(2000, 0, 0)));
        outer->addChild(placed({ 900, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12)))));
        auto root = vsg::Group::create();
        root->addChild(outer);
        EXPECT_TRUE(swapper.scan(*root, vsg::dvec3(0, 0, 0), 1)[0].visible); // 2900 - 10
        EXPECT_FALSE(swapper.scan(*root, vsg::dvec3(0, 0, 5000), 1)[0].visible);
    }

    TEST(TextureMipsUse, the_camera_decides_not_the_origin)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(placed({ 20000, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12)))));
        EXPECT_FALSE(swapper.scan(*root, vsg::dvec3(0, 0, 0), 1)[0].visible);
        EXPECT_TRUE(swapper.scan(*root, vsg::dvec3(19000, 0, 0), 2)[0].visible);
    }

    TEST(TextureMipsUse, a_group_with_no_sphere_above_it_never_counts)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        EXPECT_FALSE(swapper.scan(*root, vsg::dvec3(0, 0, 0), 5)[0].visible);
    }

    TEST(TextureMipsUse, one_near_user_is_enough)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto data = bigTexture(2048, 12);
        auto root = vsg::Group::create();
        root->addChild(placed({ 90000, 0, 0 }, makeGroup(makeBind(data))));
        root->addChild(placed({ 100, 0, 0 }, makeGroup(makeBind(data))));
        const auto textures = swapper.scan(*root, vsg::dvec3(0, 0, 0), 9);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_TRUE(textures[0].visible);
    }

    TEST(TextureMipsUse, the_last_use_is_kept_when_the_texture_goes_out_of_range)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(placed({ 1000, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12)))));
        swapper.scan(*root, vsg::dvec3(0, 0, 0), 100);
        const auto later = swapper.scan(*root, vsg::dvec3(50000, 0, 0), 400);
        EXPECT_FALSE(later[0].visible);
        EXPECT_EQ(later[0].lastUsedFrame, 100u);
    }

    TEST(TextureMipsUse, a_cull_group_counts_like_a_cull_node)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto cull = vsg::CullGroup::create(vsg::dsphere(vsg::dvec3(0, 0, 0), 10.0));
        cull->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        auto transform = vsg::MatrixTransform::create(vsg::translate(vsg::dvec3(500, 0, 0)));
        transform->addChild(cull);
        auto root = vsg::Group::create();
        root->addChild(transform);
        EXPECT_TRUE(swapper.scan(*root, vsg::dvec3(0, 0, 0), 3)[0].visible);
    }

    TEST(TextureMipsUse, the_plain_scan_gives_no_signal)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(placed({ 100, 0, 0 }, makeGroup(makeBind(bigTexture(2048, 12)))));
        const auto textures = swapper.scan(*root);
        EXPECT_FALSE(textures[0].visible);
        EXPECT_EQ(textures[0].lastUsedFrame, 0u);
    }

    TEST(TextureMipsUse, a_swap_still_reaches_groups_below_a_sphere)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto group = makeGroup(makeBind(bigTexture(2048, 12)));
        auto root = vsg::Group::create();
        root->addChild(placed({ 100, 0, 0 }, group));
        const auto id = swapper.scan(*root, vsg::dvec3(0, 0, 0), 3)[0].id;
        EXPECT_EQ(swapper.swap(id, 1, 4).groups, 1);
    }

    // Terrain layer arrays: one Array3D per kind, bound by many batches that share its descriptor.
    vsg::ref_ptr<vsg::Data> bc1Array2048(uint32_t layers)
    {
        vsg::DataList images;
        for (uint32_t i = 0; i < layers; ++i)
        {
            auto image = bigTexture(2048, 12);
            auto* bytes = static_cast<uint8_t*>(image->dataPointer());
            bytes[0] = static_cast<uint8_t>(i + 1); // a mark of the layer
            images.push_back(image);
        }
        return vsgUtil::convertToArrayTexture(images);
    }

    vsg::ref_ptr<vsg::Data> rgbaArray2048(uint32_t layers, vsg::ubvec4 colour)
    {
        vsg::DataList images;
        for (uint32_t i = 0; i < layers; ++i)
            images.push_back(
                vsg::ubvec4Array2D::create(2048, 2048, colour, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM }));
        return vsgUtil::convertToArrayTexture(images);
    }

    // A bind that holds `descriptor` (shared with other binds) and a small image of its own.
    vsg::ref_ptr<vsg::BindDescriptorSet> bindWith(vsg::ref_ptr<vsg::DescriptorImage> descriptor, uint32_t own)
    {
        auto small = vsg::ubvec4Array2D::create(
            4, 4, vsg::ubvec4(own, 0, 0, 255), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        vsg::Descriptors descriptors = { descriptor, imageDescriptor(small, 3) };
        auto set = vsg::DescriptorSet::create(vsg::ref_ptr<vsg::DescriptorSetLayout>(), descriptors);
        return vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, nullptr, 2, set);
    }

    vsg::ref_ptr<vsg::DescriptorImage> arrayDescriptor(vsg::ref_ptr<vsg::Data> array, uint32_t binding = 2)
    {
        return imageDescriptor(array, binding);
    }

    TEST(TextureMipsArrays, a_compressed_layer_array_is_one_texture_with_its_layers)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto array = bc1Array2048(3);
        auto descriptor = arrayDescriptor(array);
        auto root = vsg::Group::create();
        for (uint32_t i = 0; i < 4; ++i)
            root->addChild(makeGroup(bindWith(descriptor, i)));
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_EQ(textures[0].width, 2048u);
        EXPECT_EQ(textures[0].height, 2048u);
        EXPECT_EQ(textures[0].layers, 3u);
        EXPECT_EQ(textures[0].mipLevels, 12u);
        EXPECT_EQ(textures[0].blockBytes, 8u);
        EXPECT_EQ(TM::chainBytes(textures[0], 0), 3 * TM::chainBytes(TM::Texture{ 0, 2048, 2048, 12, 4, 4, 8 }, 0));
    }

    TEST(TextureMipsArrays, a_small_layer_array_is_not_tracked)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        vsg::DataList images;
        for (int i = 0; i < 3; ++i)
            images.push_back(bigTexture(256, 9));
        auto root = vsg::Group::create();
        root->addChild(makeGroup(bindWith(arrayDescriptor(vsgUtil::convertToArrayTexture(images)), 1)));
        EXPECT_TRUE(swapper.scan(*root).empty());
    }

    TEST(TextureMipsArrays, an_uncompressed_array_counts_the_chain_the_gpu_makes)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto array = rgbaArray2048(2, vsg::ubvec4(1, 2, 3, 4));
        auto root = vsg::Group::create();
        root->addChild(makeGroup(bindWith(arrayDescriptor(array), 1)));
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 1u);
        EXPECT_EQ(textures[0].layers, 2u);
        EXPECT_EQ(textures[0].mipLevels, 12u); // 2048 down to 1
        EXPECT_EQ(textures[0].blockBytes, 4u);
        EXPECT_EQ(TM::levelBytes(textures[0], 0), 2048 * 2048 * 4 * 2);
    }

    TEST(TextureMipsArrays, a_swap_gives_every_bind_the_one_new_descriptor)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto array = bc1Array2048(2);
        auto descriptor = arrayDescriptor(array);
        auto root = vsg::Group::create();
        std::vector<vsg::ref_ptr<vsg::StateGroup>> groups;
        for (uint32_t i = 0; i < 5; ++i)
        {
            groups.push_back(makeGroup(bindWith(descriptor, i)));
            root->addChild(groups.back());
        }
        const auto id = swapper.scan(*root)[0].id;
        const auto result = swapper.swap(id, 1, 10);
        ASSERT_TRUE(result.ok);
        EXPECT_EQ(result.binds, 5);
        EXPECT_EQ(result.groups, 5);
        const vsg::Descriptor* first = nullptr;
        for (const auto& group : groups)
        {
            const auto* bind = group->stateCommands[0]->cast<vsg::BindDescriptorSet>();
            ASSERT_NE(bind, nullptr);
            const auto* now = bind->descriptorSet->descriptors[0].get();
            if (!first)
                first = now;
            EXPECT_EQ(now, first); // one new descriptor, one new image
            EXPECT_NE(now, descriptor.get());
            const vsg::Data* data = boundData(*group->stateCommands[0]);
            EXPECT_NE(data, array.get());
            EXPECT_EQ(data->depth(), 2u);
            EXPECT_EQ(data->width(), 256u); // 1024 texels in blocks
            EXPECT_EQ(data->properties.mipLevels, 11);
            // the bind's own small image is kept
            EXPECT_EQ(bind->descriptorSet->descriptors[1]->cast<vsg::DescriptorImage>()->dstBinding, 3u);
        }
    }

    TEST(TextureMipsArrays, the_replaced_descriptor_is_reported_once_and_binds_in_pairs)
    {
        Recorder recorder;
        int descriptorCalls = 0;
        const vsg::DescriptorImage* oldSeen = nullptr;
        vsg::ref_ptr<vsg::DescriptorImage> newSeen;
        std::vector<std::pair<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>> pairs;
        auto hooks = recorder.hooks();
        hooks.descriptorReplaced
            = [&](const vsg::DescriptorImage* old, vsg::ref_ptr<vsg::DescriptorImage> replacement) {
                  ++descriptorCalls;
                  oldSeen = old;
                  newSeen = replacement;
              };
        hooks.bindsReplaced = [&](const auto& list) { pairs.insert(pairs.end(), list.begin(), list.end()); };
        Swapper swapper(hooks, params());
        auto array = bc1Array2048(2);
        auto descriptor = arrayDescriptor(array);
        auto root = vsg::Group::create();
        std::vector<const vsg::BindDescriptorSet*> olds;
        for (uint32_t i = 0; i < 3; ++i)
        {
            auto bind = bindWith(descriptor, i);
            olds.push_back(bind.get());
            root->addChild(makeGroup(bind));
        }
        swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        EXPECT_EQ(descriptorCalls, 1);
        EXPECT_EQ(oldSeen, descriptor.get());
        ASSERT_TRUE(newSeen.valid());
        ASSERT_EQ(pairs.size(), 3u);
        for (size_t i = 0; i < 3; ++i)
        {
            EXPECT_EQ(pairs[i].first, olds[i]);
            EXPECT_EQ(pairs[i].second->descriptorSet->descriptors[0].get(), newSeen.get());
        }
    }

    TEST(TextureMipsArrays, an_uncompressed_array_is_shrunk_and_a_normal_array_renormalised)
    {
        Recorder recorder;
        auto hooks = recorder.hooks();
        hooks.arrayKind = [](std::uint32_t binding) {
            return binding == 5 ? vsgUtil::TextureResize::Kind::Normal : vsgUtil::TextureResize::Kind::Colour;
        };
        Swapper swapper(hooks, params());
        auto normals = rgbaArray2048(2, vsg::ubvec4(200, 128, 128, 77));
        auto colours = rgbaArray2048(2, vsg::ubvec4(200, 128, 128, 77));
        auto groupN = makeGroup(bindWith(arrayDescriptor(normals, 5), 1));
        auto groupC = makeGroup(bindWith(arrayDescriptor(colours, 2), 2));
        auto root = vsg::Group::create();
        root->addChild(groupN);
        root->addChild(groupC);
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 2u);
        for (const auto& texture : textures)
            ASSERT_TRUE(swapper.swap(texture.id, 1, 10).ok);
        const vsg::Data* n = boundData(*groupN->stateCommands[0]);
        const vsg::Data* c = boundData(*groupC->stateCommands[0]);
        ASSERT_NE(n, nullptr);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(n->width(), 1024u);
        EXPECT_EQ(n->depth(), 2u);
        EXPECT_EQ(static_cast<const uint8_t*>(n->dataPointer())[0], 255); // renormalised
        EXPECT_EQ(static_cast<const uint8_t*>(c->dataPointer())[0], 200); // a colour is left as it was
        EXPECT_EQ(static_cast<const uint8_t*>(n->dataPointer())[3], 77);
    }

    TEST(TextureMipsArrays, a_restore_hands_the_full_array_back)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto array = bc1Array2048(2);
        auto group = makeGroup(bindWith(arrayDescriptor(array), 1));
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto id = swapper.scan(*root)[0].id;
        swapper.swap(id, 1, 10);
        EXPECT_EQ(swapper.scan(*root)[0].drop, 1);
        swapper.swap(id, 0, 400);
        EXPECT_EQ(boundData(*group->stateCommands[0]), array.get());
    }

    TEST(TextureMipsArrays, the_before_hook_runs_once_for_an_array_and_not_for_a_plain_image)
    {
        Recorder recorder;
        int calls = 0;
        auto hooks = recorder.hooks();
        hooks.beforeArraySwap = [&] { ++calls; };
        Swapper swapper(hooks, params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(bindWith(arrayDescriptor(bc1Array2048(2)), 1)));
        root->addChild(makeGroup(makeBind(bigTexture(2048, 12))));
        const auto textures = swapper.scan(*root);
        ASSERT_EQ(textures.size(), 2u);
        for (const auto& texture : textures)
            swapper.swap(texture.id, 1, 10);
        EXPECT_EQ(calls, 1);
    }

    TEST(TextureMipsArrays, a_swap_in_slices_installs_a_few_binds_a_step)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto descriptor = arrayDescriptor(bc1Array2048(2));
        auto root = vsg::Group::create();
        std::vector<vsg::ref_ptr<vsg::StateGroup>> groups;
        for (uint32_t i = 0; i < 5; ++i)
        {
            groups.push_back(makeGroup(bindWith(descriptor, i)));
            root->addChild(groups.back());
        }
        const auto id = swapper.scan(*root)[0].id;
        ASSERT_TRUE(swapper.beginSwap(id, 1, 10));
        EXPECT_TRUE(swapper.swapping());
        EXPECT_FALSE(swapper.beginSwap(id, 1, 10)); // one at a time
        int steps = 0;
        Swapper::StepResult step;
        for (;;)
        {
            step = swapper.stepSwap(2, 1000.0);
            if (step.waiting)
                continue; // the worker is still making the array
            ++steps;
            if (step.done)
                break;
            ASSERT_LT(steps, 10);
        }
        EXPECT_EQ(steps, 3); // 2 + 2 + 1
        EXPECT_TRUE(step.total.ok);
        EXPECT_EQ(step.total.binds, 5);
        EXPECT_FALSE(swapper.swapping());
        for (const auto& group : groups)
            EXPECT_NE(boundData(*group->stateCommands[0]), nullptr);
    }

    TEST(TextureMipsArrays, between_steps_some_groups_hold_the_old_bind_and_some_the_new)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto array = bc1Array2048(2);
        auto descriptor = arrayDescriptor(array);
        auto root = vsg::Group::create();
        std::vector<vsg::ref_ptr<vsg::StateGroup>> groups;
        for (uint32_t i = 0; i < 4; ++i)
        {
            groups.push_back(makeGroup(bindWith(descriptor, i)));
            root->addChild(groups.back());
        }
        const auto id = swapper.scan(*root)[0].id;
        ASSERT_TRUE(swapper.beginSwap(id, 1, 10));
        Swapper::StepResult step;
        do
            step = swapper.stepSwap(1, 1000.0);
        while (step.waiting);
        EXPECT_FALSE(step.done);
        int old = 0;
        int fresh = 0;
        for (const auto& group : groups)
            (boundData(*group->stateCommands[0]) == array.get() ? old : fresh)++;
        EXPECT_EQ(old, 3);
        EXPECT_EQ(fresh, 1);
        while (!step.done)
            step = swapper.stepSwap(1, 1000.0);
    }

    TEST(TextureMipsArrays, a_group_that_changed_since_the_begin_keeps_what_it_holds_now)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto descriptor = arrayDescriptor(bc1Array2048(2));
        auto root = vsg::Group::create();
        auto group = makeGroup(bindWith(descriptor, 1));
        root->addChild(group);
        const auto id = swapper.scan(*root)[0].id;
        ASSERT_TRUE(swapper.beginSwap(id, 1, 10));
        auto other = makeGroup(makeBind(bigTexture(2048, 12)));
        group->stateCommands[0] = other->stateCommands[0]; // someone else replaced it
        Swapper::StepResult step;
        do
            step = swapper.stepSwap(8, 1000.0);
        while (!step.done);
        EXPECT_EQ(group->stateCommands[0].get(), other->stateCommands[0].get());
    }

    TEST(TextureMipsArrays, the_budget_ends_a_step_after_at_least_one_bind)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto descriptor = arrayDescriptor(bc1Array2048(2));
        auto root = vsg::Group::create();
        for (uint32_t i = 0; i < 6; ++i)
            root->addChild(makeGroup(bindWith(descriptor, i)));
        const auto id = swapper.scan(*root)[0].id;
        ASSERT_TRUE(swapper.beginSwap(id, 1, 10));
        Swapper::StepResult step;
        do
            step = swapper.stepSwap(100, 0.0); // no time at all: still one bind
        while (step.waiting);
        EXPECT_EQ(step.total.binds, 1);
        EXPECT_FALSE(step.done);
        while (!step.done)
            step = swapper.stepSwap(100, 0.0);
        EXPECT_EQ(step.total.binds, 6);
    }

    TEST(TextureMipsArrays, an_unknown_texture_or_a_drop_past_the_floor_does_not_begin)
    {
        Recorder recorder;
        Swapper swapper(recorder.hooks(), params());
        auto root = vsg::Group::create();
        root->addChild(makeGroup(bindWith(arrayDescriptor(bc1Array2048(2)), 1)));
        const auto id = swapper.scan(*root)[0].id;
        EXPECT_FALSE(swapper.beginSwap(99, 1, 10));
        EXPECT_FALSE(swapper.beginSwap(id, 2, 10)); // 2048 -> 512 is under the floor
        EXPECT_FALSE(swapper.swapping());
    }

    TEST(TextureMipsArrays, a_failed_compile_ends_the_swap_as_failed)
    {
        Recorder recorder;
        recorder.failCompile = true;
        Swapper swapper(recorder.hooks(), params());
        auto array = bc1Array2048(2);
        auto group = makeGroup(bindWith(arrayDescriptor(array), 1));
        auto root = vsg::Group::create();
        root->addChild(group);
        const auto result = swapper.swap(swapper.scan(*root)[0].id, 1, 10);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(boundData(*group->stateCommands[0]), array.get());
        EXPECT_FALSE(swapper.swapping());
    }
}

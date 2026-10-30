#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/animation/animation.hpp>
#include <components/animation/clone.hpp>
#include <components/animation/controller.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/material.hpp>

#include <vsg/all.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    using MWAnim::MaterialValue;
    using Materials = std::vector<vsg::ref_ptr<MaterialValue>>;

    constexpr std::uint32_t sMaterialBinding = Pipeline::Descriptors::MATERIAL_BINDING;
    // Any binding other than the material's, for a descriptor that must be kept.
    constexpr std::uint32_t sOtherBinding = Pipeline::Descriptors::TEXMAT_BINDING;

    Pipeline::Data::Material makeMaterial(float seed)
    {
        Pipeline::Data::Material mat = Pipeline::Material::createDefault();
        mat.ambient = vsg::vec4(seed, seed + 0.1f, seed + 0.2f, 1.f);
        mat.diffuse = vsg::vec4(0.5f, 0.25f, seed, 0.75f);
        mat.emissive = vsg::vec4(seed, 0.f, 0.f, 1.f);
        mat.specular = vsg::vec4(0.f, seed, 0.f, 0.f);
        mat.shininess = 8.f + seed;
        mat.alphaTestCutoff = 0.5f;
        return mat;
    }

    // A stateless controller to attach to data; run() does nothing and cloneIfRequired() returns none.
    class NullController : public Anim::Controller
    {
    public:
        void run(vsg::Object&, float) const override {}
    };

    bool sameMaterial(const Pipeline::Data::Material& a, const Pipeline::Data::Material& b)
    {
        return std::memcmp(&a, &b, sizeof(Pipeline::Data::Material)) == 0;
    }

    vsg::ref_ptr<vsg::PipelineLayout> makeLayout()
    {
        vsg::DescriptorSetLayoutBindings bindings{
            { sMaterialBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr },
            { sOtherBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr },
        };
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        return vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout }, vsg::PushConstantRanges{});
    }

    // A StateGroup binding one descriptor set (firstSet 0) holding `material` (if any) at MATERIAL_BINDING and
    // `other` (if any) at another binding, with `child` (or a fresh Group) below it.
    vsg::ref_ptr<vsg::StateGroup> makeStateGroup(vsg::ref_ptr<MaterialValue> material,
        vsg::ref_ptr<vsg::Data> other = {}, vsg::ref_ptr<vsg::Node> child = {}, std::uint32_t firstSet = 0)
    {
        auto layout = makeLayout();
        vsg::Descriptors descriptors;
        if (material)
            descriptors.push_back(vsg::DescriptorBuffer::create(material, sMaterialBinding));
        if (other)
            descriptors.push_back(vsg::DescriptorBuffer::create(other, sOtherBinding));
        auto set = vsg::DescriptorSet::create(layout->setLayouts[0], descriptors);
        auto bind = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, firstSet, set);

        auto sg = vsg::StateGroup::create();
        sg->add(bind);
        sg->addChild(child ? child : vsg::ref_ptr<vsg::Node>(vsg::Group::create()));
        return sg;
    }

    // Everything found on BindDescriptorSet state commands of StateGroups in a graph.
    struct Found
    {
        std::vector<vsg::DescriptorBuffer*> materialDescriptors;
        std::vector<vsg::Data*> materialData; // one entry per material BufferInfo
        std::vector<vsg::Data*> otherData; // data of DescriptorBuffers at sOtherBinding
        std::size_t stateGroups = 0;
    };

    class Collector : public vsg::Visitor
    {
    public:
        Found mFound;

        void apply(vsg::Node& node) override { node.traverse(*this); }

        void apply(vsg::StateGroup& sg) override
        {
            ++mFound.stateGroups;
            for (auto& command : sg.stateCommands)
            {
                auto* bind = command->cast<vsg::BindDescriptorSet>();
                if (!bind || !bind->descriptorSet)
                    continue;
                for (auto& descriptor : bind->descriptorSet->descriptors)
                {
                    auto* buffer = descriptor->cast<vsg::DescriptorBuffer>();
                    if (!buffer)
                        continue;
                    if (buffer->dstBinding == sMaterialBinding)
                    {
                        mFound.materialDescriptors.push_back(buffer);
                        for (auto& info : buffer->bufferInfoList)
                            mFound.materialData.push_back(info->data.get());
                    }
                    else if (buffer->dstBinding == sOtherBinding)
                    {
                        for (auto& info : buffer->bufferInfoList)
                            mFound.otherData.push_back(info->data.get());
                    }
                }
            }
            sg.traverse(*this);
        }
    };

    Found collect(vsg::Node& node)
    {
        Collector collector;
        node.accept(collector);
        return collector.mFound;
    }

    bool holdsData(const Materials& materials, const vsg::Data* data)
    {
        return std::any_of(
            materials.begin(), materials.end(), [&](const vsg::ref_ptr<MaterialValue>& m) { return m.get() == data; });
    }

    // Root Group with two StateGroups, each holding its own material value and a non-material descriptor.
    struct TwoMaterialGraph
    {
        vsg::ref_ptr<MaterialValue> matA = MaterialValue::create(makeMaterial(0.1f));
        vsg::ref_ptr<MaterialValue> matB = MaterialValue::create(makeMaterial(0.6f));
        vsg::ref_ptr<vsg::mat4Value> otherA = vsg::mat4Value::create(vsg::translate(1.f, 2.f, 3.f));
        vsg::ref_ptr<vsg::mat4Value> otherB = vsg::mat4Value::create(vsg::scale(2.f, 2.f, 2.f));
        vsg::ref_ptr<vsg::Group> root = vsg::Group::create();

        TwoMaterialGraph()
        {
            root->addChild(makeStateGroup(matA, otherA, {}, 0));
            root->addChild(makeStateGroup(matB, otherB, {}, 1));
        }
    };

    // --- Every material descriptor gets its own value ---

    TEST(MWAnimCloneWithOwnMaterialsTest, AppendsOneValuePerMaterialDescriptor)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        EXPECT_EQ(materials.size(), 2u);
        for (const auto& m : materials)
            EXPECT_TRUE(m);
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, ClonedGraphUsesTheReturnedValues)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        Found found = collect(*node);
        ASSERT_EQ(found.materialData.size(), 2u);
        for (vsg::Data* data : found.materialData)
            EXPECT_TRUE(holdsData(materials, data)) << "a material uniform of the copy is not in materials";
        // Each returned value is used by exactly one material uniform of the copy.
        for (const auto& m : materials)
            EXPECT_EQ(std::count(found.materialData.begin(), found.materialData.end(), m.get()), 1);
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, ReturnedValuesAreNotTheOriginals)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 2u);
        for (const auto& m : materials)
        {
            EXPECT_NE(m.get(), g.matA.get());
            EXPECT_NE(m.get(), g.matB.get());
        }
        EXPECT_NE(materials[0].get(), materials[1].get());
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, ReturnedValuesCopyTheOriginalContents)
    {
        TwoMaterialGraph g;
        const auto expectedA = g.matA->value();
        const auto expectedB = g.matB->value();
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 2u);
        // The order of materials is not specified; each original must be matched by exactly one value.
        int matchesA = 0;
        int matchesB = 0;
        for (const auto& m : materials)
        {
            if (sameMaterial(m->value(), expectedA))
                ++matchesA;
            if (sameMaterial(m->value(), expectedB))
                ++matchesB;
        }
        EXPECT_EQ(matchesA, 1);
        EXPECT_EQ(matchesB, 1);
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, ReturnedValuesAreDynamic)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 2u);
        for (const auto& m : materials)
        {
            EXPECT_TRUE(m->dynamic());
            EXPECT_EQ(m->properties.dataVariance, vsg::DYNAMIC_DATA);
        }
    }

    // --- The original graph is untouched ---

    TEST(MWAnimCloneWithOwnMaterialsTest, ChangingReturnedValueLeavesOriginalUnchanged)
    {
        TwoMaterialGraph g;
        const auto originalA = g.matA->value();
        const auto originalB = g.matB->value();
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 2u);
        for (const auto& m : materials)
        {
            m->value().diffuse.a = 0.f;
            m->value().emissive = vsg::vec4(9.f, 9.f, 9.f, 9.f);
            m->dirty();
        }

        EXPECT_TRUE(sameMaterial(g.matA->value(), originalA));
        EXPECT_TRUE(sameMaterial(g.matB->value(), originalB));
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, OriginalGraphStillUsesOriginalValues)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> original = g.root;
        vsg::ref_ptr<vsg::Node> node = original;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        Found found = collect(*original);
        ASSERT_EQ(found.materialData.size(), 2u);
        EXPECT_NE(
            std::find(found.materialData.begin(), found.materialData.end(), g.matA.get()), found.materialData.end());
        EXPECT_NE(
            std::find(found.materialData.begin(), found.materialData.end(), g.matB.get()), found.materialData.end());
        for (vsg::Data* data : found.materialData)
            EXPECT_FALSE(holdsData(materials, data)) << "the original graph references a returned value";
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, CloneAndOriginalAreDistinctWhereMaterialsAre)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> original = g.root;
        vsg::ref_ptr<vsg::Node> node = original;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        // The root object may be kept when only deeper parts are cloned; distinctness is checked per descriptor.
        Found before = collect(*original);
        Found after = collect(*node);
        ASSERT_EQ(before.materialDescriptors.size(), 2u);
        ASSERT_EQ(after.materialDescriptors.size(), 2u);
        for (vsg::DescriptorBuffer* d : after.materialDescriptors)
            EXPECT_EQ(std::find(before.materialDescriptors.begin(), before.materialDescriptors.end(), d),
                before.materialDescriptors.end())
                << "a material descriptor is shared between the original and the clone";
    }

    // --- Non-material descriptors and structure ---

    TEST(MWAnimCloneWithOwnMaterialsTest, NonMaterialDescriptorsAreKept)
    {
        TwoMaterialGraph g;
        const vsg::mat4 expectedA = g.otherA->value();
        const vsg::mat4 expectedB = g.otherB->value();
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        Found found = collect(*node);
        ASSERT_EQ(found.otherData.size(), 2u);
        int matchesA = 0;
        int matchesB = 0;
        for (vsg::Data* data : found.otherData)
        {
            auto* value = data->cast<vsg::mat4Value>();
            ASSERT_NE(value, nullptr);
            if (value->value() == expectedA)
                ++matchesA;
            if (value->value() == expectedB)
                ++matchesB;
            EXPECT_FALSE(holdsData(materials, data));
        }
        EXPECT_EQ(matchesA, 1);
        EXPECT_EQ(matchesB, 1);
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, StateGroupsAndChildrenAreKept)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        EXPECT_EQ(collect(*node).stateGroups, 2u);
        auto* root = node->cast<vsg::Group>();
        ASSERT_NE(root, nullptr);
        ASSERT_EQ(root->children.size(), 2u);
        for (auto& child : root->children)
        {
            auto* sg = child->cast<vsg::StateGroup>();
            ASSERT_NE(sg, nullptr);
            EXPECT_EQ(sg->children.size(), 1u);
            EXPECT_EQ(sg->stateCommands.size(), 1u);
        }
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, RootStateGroupWithNestedMaterialsCountsEveryOneReached)
    {
        // StateGroup(material) -> Group -> StateGroup(material) -> StateGroup(material, other)
        auto inner = makeStateGroup(MaterialValue::create(makeMaterial(0.3f)), vsg::mat4Value::create());
        auto middle = makeStateGroup(MaterialValue::create(makeMaterial(0.2f)), {}, inner);
        auto group = vsg::Group::create();
        group->addChild(middle);
        auto root = makeStateGroup(MaterialValue::create(makeMaterial(0.1f)), {}, group);

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        EXPECT_EQ(materials.size(), 3u);
        Found found = collect(*node);
        ASSERT_EQ(found.materialData.size(), 3u);
        for (vsg::Data* data : found.materialData)
            EXPECT_TRUE(holdsData(materials, data));
    }

    // --- No material ---

    TEST(MWAnimCloneWithOwnMaterialsTest, GraphWithoutMaterialLeavesMaterialsEmpty)
    {
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup({}, vsg::mat4Value::create(vsg::translate(1.f, 0.f, 0.f))));
        root->addChild(vsg::Group::create());

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        EXPECT_TRUE(materials.empty());
        Found found = collect(*node);
        EXPECT_EQ(found.otherData.size(), 1u);
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, PlainGroupLeavesMaterialsEmpty)
    {
        auto root = vsg::Group::create();
        root->addChild(vsg::Group::create());

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        EXPECT_TRUE(materials.empty());
    }

    // --- Shared material values ---

    TEST(MWAnimCloneWithOwnMaterialsTest, SharedMaterialValueGetsOneCopyUsedEverywhere)
    {
        // Two state groups, each with its own descriptor, both pointing at the same material value.
        auto shared = MaterialValue::create(makeMaterial(0.4f));
        const auto expected = shared->value();
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(shared));
        root->addChild(makeStateGroup(shared));

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        ASSERT_EQ(materials.size(), 1u);
        ASSERT_TRUE(materials[0]);
        EXPECT_NE(materials[0].get(), shared.get());
        EXPECT_TRUE(sameMaterial(materials[0]->value(), expected));

        // Both occurrences in the copy use the one copy.
        Found found = collect(*node);
        ASSERT_EQ(found.materialData.size(), 2u);
        EXPECT_EQ(found.materialData[0], materials[0].get());
        EXPECT_EQ(found.materialData[1], materials[0].get());

        // Changing the copy does not change the shared original.
        materials[0]->value().diffuse.a = 0.f;
        EXPECT_FLOAT_EQ(shared->value().diffuse.a, expected.diffuse.a);
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, SharedMaterialPlusDistinctMaterialGivesTwoCopies)
    {
        auto shared = MaterialValue::create(makeMaterial(0.4f));
        auto single = MaterialValue::create(makeMaterial(0.8f));
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(shared));
        root->addChild(makeStateGroup(single));
        root->addChild(makeStateGroup(shared));

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 2u);
        EXPECT_NE(materials[0].get(), materials[1].get());
        Found found = collect(*node);
        ASSERT_EQ(found.materialData.size(), 3u);
        for (vsg::Data* data : found.materialData)
            EXPECT_TRUE(holdsData(materials, data));
    }

    // --- Controllers and other descriptors ---

    TEST(MWAnimCloneWithOwnMaterialsTest, CopiesKeepTheMaterialController)
    {
        auto material = MaterialValue::create(makeMaterial(0.5f));
        auto controller = vsg::ref_ptr<NullController>(new NullController);
        controller->attachTo(*material);
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(material));

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 1u);
        EXPECT_NE(materials[0].get(), material.get());
        // The copy carries a controller (the original's, or its cloneIfRequired() copy; NullController returns
        // none, so the original itself is expected).
        const Anim::Controller* copied = Anim::Controller::get(*materials[0]);
        ASSERT_NE(copied, nullptr);
        EXPECT_EQ(copied, controller.get());
        // The original keeps its controller too.
        EXPECT_EQ(Anim::Controller::get(*material), controller.get());
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, SharedDynamicNonMaterialDataGetsOneSharedClone)
    {
        // A DYNAMIC_DATA vec4Array at another binding, used by two descriptor sets of the model.
        auto dynamicData = vsg::vec4Array::create(4);
        dynamicData->properties.dataVariance = vsg::DYNAMIC_DATA;
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(MaterialValue::create(makeMaterial(0.1f)), dynamicData));
        root->addChild(makeStateGroup(MaterialValue::create(makeMaterial(0.2f)), dynamicData));

        vsg::ref_ptr<vsg::Node> original = root;
        vsg::ref_ptr<vsg::Node> node = original;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        Found found = collect(*node);
        ASSERT_EQ(found.otherData.size(), 2u);
        EXPECT_NE(found.otherData[0], dynamicData.get());
        EXPECT_EQ(found.otherData[0], found.otherData[1]) << "the dynamic data was cloned more than once";
        EXPECT_FALSE(holdsData(materials, found.otherData[0]));

        // The original graph still uses the original data.
        Found before = collect(*original);
        ASSERT_EQ(before.otherData.size(), 2u);
        EXPECT_EQ(before.otherData[0], dynamicData.get());
        EXPECT_EQ(before.otherData[1], dynamicData.get());
    }

    // --- materials is appended to ---

    TEST(MWAnimCloneWithOwnMaterialsTest, MaterialsIsAppendedNotCleared)
    {
        auto sentinel = MaterialValue::create(makeMaterial(0.9f));
        Materials materials{ sentinel };

        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 3u);
        EXPECT_EQ(materials[0].get(), sentinel.get());
        EXPECT_NE(materials[1].get(), sentinel.get());
        EXPECT_NE(materials[2].get(), sentinel.get());
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, TwoClonesOfOneModelGetIndependentValues)
    {
        TwoMaterialGraph g;
        const auto originalA = g.matA->value();
        vsg::ref_ptr<vsg::Node> first = g.root;
        vsg::ref_ptr<vsg::Node> second = g.root;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(first, materials);
        MWAnim::cloneWithOwnMaterials(second, materials);

        ASSERT_EQ(materials.size(), 4u);
        for (std::size_t i = 0; i < materials.size(); ++i)
            for (std::size_t j = i + 1; j < materials.size(); ++j)
                EXPECT_NE(materials[i].get(), materials[j].get()) << i << " and " << j;

        // The first clone's materials (appended first) are not the second clone's.
        Found firstFound = collect(*first);
        Found secondFound = collect(*second);
        for (vsg::Data* data : firstFound.materialData)
            EXPECT_EQ(std::find(secondFound.materialData.begin(), secondFound.materialData.end(), data),
                secondFound.materialData.end());

        for (const auto& m : materials)
            m->value().diffuse.a = 0.f;
        EXPECT_TRUE(sameMaterial(g.matA->value(), originalA));
    }

    // --- Anim::cloneIfRequired with a data filter ---

    bool isMaterial(const vsg::Data& data)
    {
        return dynamic_cast<const MaterialValue*>(&data) != nullptr;
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, DataFilterTrueClonesAsDynamicOncePerDistinctData)
    {
        auto shared = MaterialValue::create(makeMaterial(0.4f));
        auto single = MaterialValue::create(makeMaterial(0.8f));
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(shared));
        root->addChild(makeStateGroup(single));
        root->addChild(makeStateGroup(shared));

        vsg::ref_ptr<vsg::Node> original = root;
        vsg::ref_ptr<vsg::Node> node = original;
        std::vector<vsg::Data*> cloned;

        Anim::cloneIfRequired(node, {}, isMaterial, [&](vsg::Data& data) { cloned.push_back(&data); });

        ASSERT_TRUE(node);
        ASSERT_EQ(cloned.size(), 2u) << "onDataCloned must be called once per distinct data";
        EXPECT_NE(cloned[0], cloned[1]);
        for (vsg::Data* data : cloned)
        {
            EXPECT_NE(data, shared.get());
            EXPECT_NE(data, single.get());
            EXPECT_EQ(data->properties.dataVariance, vsg::DYNAMIC_DATA);
            auto* value = data->cast<MaterialValue>();
            ASSERT_NE(value, nullptr);
            EXPECT_TRUE(sameMaterial(value->value(), shared->value()) || sameMaterial(value->value(), single->value()));
        }

        // The copy uses only the new data; both uses of the shared value use one new copy.
        Found found = collect(*node);
        ASSERT_EQ(found.materialData.size(), 3u);
        for (vsg::Data* data : found.materialData)
            EXPECT_NE(std::find(cloned.begin(), cloned.end(), data), cloned.end());
        EXPECT_EQ(found.materialData[0], found.materialData[2]);
        EXPECT_NE(found.materialData[0], found.materialData[1]);

        // The original graph is unchanged.
        Found before = collect(*original);
        ASSERT_EQ(before.materialData.size(), 3u);
        EXPECT_EQ(before.materialData[0], shared.get());
        EXPECT_EQ(before.materialData[1], single.get());
        EXPECT_EQ(before.materialData[2], shared.get());
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, DataFilterKeepsTheController)
    {
        auto material = MaterialValue::create(makeMaterial(0.5f));
        auto controller = vsg::ref_ptr<NullController>(new NullController);
        controller->attachTo(*material);
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(material));

        vsg::ref_ptr<vsg::Node> node = root;
        std::vector<vsg::Data*> cloned;

        Anim::cloneIfRequired(node, {}, isMaterial, [&](vsg::Data& data) { cloned.push_back(&data); });

        ASSERT_EQ(cloned.size(), 1u);
        EXPECT_NE(cloned[0], material.get());
        EXPECT_EQ(Anim::Controller::get(*cloned[0]), controller.get());
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, DataFilterFalseKeepsStaticDataButClonesDynamicData)
    {
        auto staticData = vsg::mat4Value::create(vsg::translate(1.f, 2.f, 3.f));
        auto dynamicData = vsg::vec4Array::create(4);
        dynamicData->properties.dataVariance = vsg::DYNAMIC_DATA;
        auto root = vsg::Group::create();
        // Materials (filtered in) make the clone happen along both paths.
        root->addChild(makeStateGroup(MaterialValue::create(makeMaterial(0.1f)), staticData));
        root->addChild(makeStateGroup(MaterialValue::create(makeMaterial(0.2f)), dynamicData));

        vsg::ref_ptr<vsg::Node> node = root;
        std::vector<vsg::Data*> cloned;

        Anim::cloneIfRequired(node, {}, isMaterial, [&](vsg::Data& data) { cloned.push_back(&data); });

        ASSERT_TRUE(node);
        EXPECT_EQ(cloned.size(), 2u) << "only filtered data is passed to onDataCloned";
        Found found = collect(*node);
        ASSERT_EQ(found.otherData.size(), 2u);
        EXPECT_EQ(found.otherData[0], staticData.get()) << "static, controller-less data is not cloned";
        EXPECT_NE(found.otherData[1], dynamicData.get()) << "dynamic data is cloned as cloneIfRequired would";
        EXPECT_EQ(std::find(cloned.begin(), cloned.end(), found.otherData[1]), cloned.end());
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, DataFilterNeverTrueCallsNothing)
    {
        TwoMaterialGraph g;
        vsg::ref_ptr<vsg::Node> node = g.root;
        int calls = 0;

        Anim::cloneIfRequired(node, {}, [](const vsg::Data&) { return false; }, [&](vsg::Data&) { ++calls; });

        ASSERT_TRUE(node);
        EXPECT_EQ(calls, 0);
        Found found = collect(*node);
        ASSERT_EQ(found.materialData.size(), 2u);
        EXPECT_EQ(found.materialData[0], g.matA.get());
        EXPECT_EQ(found.materialData[1], g.matB.get());
    }

    // --- Particle-like node: one storage descriptor shared by a compute and a graphics set ---

    // Collects the data of every DescriptorBuffer at `binding`, per BindDescriptorSet, with the bind point.
    class BindingCollector : public vsg::Visitor
    {
    public:
        explicit BindingCollector(std::uint32_t binding)
            : mBinding(binding)
        {
        }

        std::vector<std::pair<VkPipelineBindPoint, vsg::Data*>> mFound;

        void apply(vsg::Node& node) override { node.traverse(*this); }

        void apply(vsg::StateGroup& sg) override
        {
            for (auto& command : sg.stateCommands)
            {
                auto* bind = command->cast<vsg::BindDescriptorSet>();
                if (!bind || !bind->descriptorSet)
                    continue;
                for (auto& descriptor : bind->descriptorSet->descriptors)
                {
                    auto* buffer = descriptor->cast<vsg::DescriptorBuffer>();
                    if (!buffer || buffer->dstBinding != mBinding)
                        continue;
                    for (auto& info : buffer->bufferInfoList)
                        mFound.emplace_back(bind->pipelineBindPoint, info->data.get());
                }
            }
            sg.traverse(*this);
        }

    private:
        std::uint32_t mBinding;
    };

    TEST(MWAnimCloneWithOwnMaterialsTest, StorageDescriptorSharedByComputeAndGraphicsSetsGetsOneClone)
    {
        constexpr std::uint32_t storageBinding = Pipeline::Descriptors::PARTICLE_BINDING;
        auto particles = vsg::vec4Array::create(16);
        particles->properties.dataVariance = vsg::DYNAMIC_DATA;
        // The same DescriptorBuffer object is used by both sets.
        auto storage = vsg::DescriptorBuffer::create(particles, storageBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        auto computeSetLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
            { storageBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr } });
        auto computeLayout
            = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ computeSetLayout }, vsg::PushConstantRanges{});
        auto computeSet = vsg::DescriptorSet::create(computeSetLayout, vsg::Descriptors{ storage });
        auto computeBind = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout, 0, computeSet);

        auto material = MaterialValue::create(makeMaterial(0.3f));
        auto graphicsSetLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
            { sMaterialBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr },
            { storageBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr } });
        auto graphicsLayout
            = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ graphicsSetLayout }, vsg::PushConstantRanges{});
        auto graphicsSet = vsg::DescriptorSet::create(
            graphicsSetLayout, vsg::Descriptors{ vsg::DescriptorBuffer::create(material, sMaterialBinding), storage });
        auto graphicsBind
            = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsLayout, 0, graphicsSet);

        auto computeGroup = vsg::StateGroup::create();
        computeGroup->add(computeBind);
        computeGroup->addChild(vsg::Group::create());
        auto graphicsGroup = vsg::StateGroup::create();
        graphicsGroup->add(graphicsBind);
        graphicsGroup->addChild(vsg::Group::create());
        auto root = vsg::Group::create();
        root->addChild(computeGroup);
        root->addChild(graphicsGroup);

        vsg::ref_ptr<vsg::Node> original = root;
        vsg::ref_ptr<vsg::Node> node = original;
        Materials materials;

        MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_TRUE(node);
        EXPECT_EQ(materials.size(), 1u);

        BindingCollector after(storageBinding);
        node->accept(after);
        ASSERT_EQ(after.mFound.size(), 2u);
        bool sawCompute = false;
        bool sawGraphics = false;
        for (const auto& [bindPoint, data] : after.mFound)
        {
            sawCompute |= bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE;
            sawGraphics |= bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS;
            EXPECT_NE(data, particles.get()) << "the copy still uses the original dynamic array";
        }
        EXPECT_TRUE(sawCompute);
        EXPECT_TRUE(sawGraphics);
        EXPECT_EQ(after.mFound[0].second, after.mFound[1].second)
            << "the compute and graphics sets of the copy resolve to different arrays";

        // The original graph still uses the original array in both sets.
        BindingCollector before(storageBinding);
        original->accept(before);
        ASSERT_EQ(before.mFound.size(), 2u);
        EXPECT_EQ(before.mFound[0].second, particles.get());
        EXPECT_EQ(before.mFound[1].second, particles.get());
    }

    // --- The material controller is registered on the copy ---

    // A controller whose cloneIfRequired() returns a new copy, as stateful controllers do.
    class CloningController : public Anim::Controller
    {
    public:
        void run(vsg::Object&, float) const override {}
        vsg::ref_ptr<Anim::Controller> cloneIfRequired() const override
        {
            return vsg::ref_ptr<Anim::Controller>(new CloningController);
        }
    };

    // Whether `animation` registers `controller` (in controllers or clonedControllers) with `target`.
    bool registers(const Anim::Animation& animation, const Anim::Controller* controller, const vsg::Object* target)
    {
        for (const auto& entry : animation.controllers)
            if (entry.first == controller && entry.second == target)
                return true;
        for (const auto& entry : animation.clonedControllers)
            if (entry.first == controller && entry.second == target)
                return true;
        return false;
    }

    bool targetsObject(const Anim::Animation& animation, const vsg::Object* target)
    {
        for (const auto& entry : animation.controllers)
            if (entry.second == target)
                return true;
        for (const auto& entry : animation.clonedControllers)
            if (entry.second == target)
                return true;
        return false;
    }

    void expectControllerRegisteredOnCopy(vsg::ref_ptr<Anim::Controller> controller)
    {
        auto material = MaterialValue::create(makeMaterial(0.5f));
        controller->attachTo(*material);
        auto root = vsg::Group::create();
        root->addChild(makeStateGroup(material));

        vsg::ref_ptr<vsg::Node> node = root;
        Materials materials;

        MWAnim::CloneResult result = MWAnim::cloneWithOwnMaterials(node, materials);

        ASSERT_EQ(materials.size(), 1u);
        MaterialValue* copy = materials[0].get();
        ASSERT_NE(copy, material.get());
        const Anim::Controller* onCopy = Anim::Controller::get(static_cast<const vsg::Object&>(*copy));
        ASSERT_NE(onCopy, nullptr);
        EXPECT_TRUE(registers(result, onCopy, copy))
            << "the copy's controller is not registered in the returned Animation with the copy as its target";
        EXPECT_FALSE(targetsObject(result, material.get())) << "the returned Animation targets the original material";
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, StatelessMaterialControllerIsRegisteredOnTheCopy)
    {
        expectControllerRegisteredOnCopy(vsg::ref_ptr<Anim::Controller>(new NullController));
    }

    TEST(MWAnimCloneWithOwnMaterialsTest, ClonedMaterialControllerIsRegisteredOnTheCopy)
    {
        expectControllerRegisteredOnCopy(vsg::ref_ptr<Anim::Controller>(new CloningController));
    }
}

#endif

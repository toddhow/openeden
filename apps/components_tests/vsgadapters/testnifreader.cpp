#include <components/animation/transform.hpp>
#include <components/pipeline/builder.hpp>
#include <components/vsgadapters/nif/nif.hpp>
#include <components/vsgutil/id.hpp>

#include <gtest/gtest.h>

#include <vsg/io/Options.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/Sampler.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Replaces nifosg/testnifloader.cpp (task 0014), which read a default node through NifOsg::Loader and checked the scene
// graph it built, with the record index on the node. Its other cases, the "bs/..." shader prefixes of
// BSShaderPPLightingProperty and BSLightingShaderProperty, have no counterpart: vsgAdapters::nif does not read those
// properties (nifImpl::handleProperty has no case for them), so there is nothing to test.
namespace
{
    template <class T>
    void write(std::ostream& stream, T value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    void writeString(std::ostream& stream, std::string_view value)
    {
        write(stream, static_cast<std::uint32_t>(value.size()));
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    struct NodeRecord
    {
        std::string mName;
        std::array<float, 3> mTranslation{ 0, 0, 0 };
        float mScale = 1;
        std::vector<std::int32_t> mChildren;
    };

    // A NiNode as Nif::NiNode::read reads it from a Morrowind file.
    void writeNode(std::ostream& stream, const NodeRecord& node)
    {
        writeString(stream, "NiNode");
        writeString(stream, node.mName);
        write<std::int32_t>(stream, -1); // extra data
        write<std::int32_t>(stream, -1); // controller
        write<std::uint16_t>(stream, 0); // flags
        for (float value : node.mTranslation)
            write(stream, value);
        for (float value : { 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f })
            write(stream, value);
        write(stream, node.mScale);
        for (int i = 0; i < 3; ++i)
            write(stream, 0.f); // velocity
        write<std::int32_t>(stream, 0); // properties
        write<std::int32_t>(stream, 0); // no bounds
        write(stream, static_cast<std::int32_t>(node.mChildren.size()));
        for (std::int32_t child : node.mChildren)
            write(stream, child);
        write<std::int32_t>(stream, 0); // effects
    }

    std::string makeNif(const std::vector<NodeRecord>& nodes)
    {
        std::ostringstream stream;
        stream << "NetImmerse File Format, Version 4.0.0.2\n";
        write<std::uint32_t>(stream, 0x04000002);
        write(stream, static_cast<std::uint32_t>(nodes.size()));
        for (const NodeRecord& node : nodes)
            writeNode(stream, node);
        write<std::uint32_t>(stream, 1); // one root
        write<std::int32_t>(stream, 0);
        return stream.str();
    }

    struct VsgAdaptersNifReaderTest : ::testing::Test
    {
        vsg::ref_ptr<vsg::Options> mShaderOptions = vsg::Options::create();
        Pipeline::Builder mBuilder{ mShaderOptions, vsg::Sampler::create() };
        vsg::ref_ptr<vsgAdapters::nif> mReader
            = vsg::ref_ptr{ new vsgAdapters::nif(mBuilder, vsg::Options::create(), 0, 0) };

        vsg::ref_ptr<vsg::Object> read(const std::string& content, std::string_view filename = "test.nif") const
        {
            auto options = vsg::Options::create();
            options->extensionHint = ".nif";
            options->setValue("filename", std::string(filename));
            std::istringstream stream(content);
            return mReader->read(stream, options);
        }
    };

    Anim::Transform* findTransform(vsg::Node* node)
    {
        if (auto* transform = dynamic_cast<Anim::Transform*>(node))
            return transform;
        if (auto* group = dynamic_cast<vsg::Group*>(node))
        {
            for (const auto& child : group->children)
                if (auto* found = findTransform(child.get()))
                    return found;
        }
        return nullptr;
    }

    TEST_F(VsgAdaptersNifReaderTest, shouldLoadFileWithDefaultNode)
    {
        const auto result = read(makeNif({ NodeRecord{} }));
        ASSERT_TRUE(result);
        auto* node = result->cast<vsg::Node>();
        ASSERT_NE(node, nullptr);
        // An identity node that nothing refers to is optimized away, so there is no transform to keep.
        EXPECT_EQ(findTransform(node), nullptr);
    }

    TEST_F(VsgAdaptersNifReaderTest, shouldKeepRecordIndexAndTransformOfAChildNode)
    {
        NodeRecord root;
        root.mChildren = { 1 };
        NodeRecord child;
        child.mName = "child";
        child.mTranslation = { 1, 2, 3 };
        child.mScale = 2;
        const auto result = read(makeNif({ root, child }));
        ASSERT_TRUE(result);
        auto* node = result->cast<vsg::Node>();
        ASSERT_NE(node, nullptr);

        const Anim::Transform* transform = findTransform(node);
        ASSERT_NE(transform, nullptr);
        EXPECT_EQ(transform->translation, vsg::vec3(1, 2, 3));
        EXPECT_EQ(transform->scale, vsg::vec3(2, 2, 2));
        const vsgUtil::ID* id = vsgUtil::ID::get(*transform);
        ASSERT_NE(id, nullptr);
        EXPECT_EQ(id->id, 1);
    }

    TEST_F(VsgAdaptersNifReaderTest, rootTransformShouldBeDiscarded)
    {
        // Nif::NiNode::read discards the root's transform, except on a bone named bip01.
        NodeRecord root;
        root.mTranslation = { 5, 6, 7 };
        const auto result = read(makeNif({ root }));
        ASSERT_TRUE(result);
        auto* node = result->cast<vsg::Node>();
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(findTransform(node), nullptr);
    }

    TEST_F(VsgAdaptersNifReaderTest, shouldReturnNullForOtherExtensionHint)
    {
        auto options = vsg::Options::create();
        options->extensionHint = ".kf";
        std::istringstream stream(makeNif({ NodeRecord{} }));
        EXPECT_FALSE(mReader->read(stream, options));
    }

    TEST_F(VsgAdaptersNifReaderTest, shouldReturnNullForATruncatedFile)
    {
        std::string content = makeNif({ NodeRecord{} });
        content.resize(content.size() / 2);
        EXPECT_FALSE(read(content));
    }
}

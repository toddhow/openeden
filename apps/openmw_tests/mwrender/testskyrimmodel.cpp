// Task 0110 (neutral world stage N3a-4): a Skyrim SE file through vsgAdapters::nif::read (the model cache's entry
// point) becomes static draws through the engine's Pipeline::Builder, and only when the host gave the reader a scene
// reader. The files are generated (nifscene/ssebuilder.hpp, shaderbuilder.hpp); no game asset is read.

#include <apps/openmw/mwrender/skyrimmodel.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/pipeline/builder.hpp>
#include <components/resource/fallback.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgadapters/nif/nif.hpp>

#include <gtest/gtest.h>

#include <vsg/core/Visitor.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/Sampler.h>
#include <vsg/utils/SharedObjects.h>

#include <filesystem>
#include <sstream>
#include <string>

#include "apps/components_tests/nifscene/shaderbuilder.hpp"

namespace
{
    using namespace NifScene::Testing;

    class DefaultOptionsGuard
    {
    public:
        explicit DefaultOptionsGuard(const Nif::ReadOptions& options)
            : mSaved(Nif::Reader::getDefaultReadOptions())
        {
            Nif::Reader::setDefaultReadOptions(options);
        }
        DefaultOptionsGuard(const DefaultOptionsGuard&) = delete;
        DefaultOptionsGuard& operator=(const DefaultOptionsGuard&) = delete;
        ~DefaultOptionsGuard() { Nif::Reader::setDefaultReadOptions(mSaved); }

    private:
        Nif::ReadOptions mSaved;
    };

    // Counts what the visitor meets.
    struct Census : vsg::Visitor
    {
        int draws = 0;
        int stateGroups = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t arrays = 0;
        bool uintIndices = false;
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::StateGroup& group) override
        {
            ++stateGroups;
            group.traverse(*this);
        }
        void apply(vsg::VertexIndexDraw& draw) override
        {
            ++draws;
            indexCount += draw.indexCount;
            arrays = static_cast<std::uint32_t>(draw.arrays.size());
            uintIndices = draw.indices && draw.indices->data && draw.indices->data.cast<vsg::uintArray>();
        }
    };

    // Records: 0 root, 1 quad shape, 2 shader property, 3 texture set.
    std::string skyrimQuad(std::uint32_t flags2 = 0x10, const std::string& texture = "textures\\test\\wall_d.dds")
    {
        GamebryoFile file;
        file.mStreamVersion = 100;
        ShapeSpec shapeSpec;
        shapeSpec.mName = "Quad";
        shapeSpec.mFlags = VF_Vertex | VF_UVs | VF_Normals;
        shapeSpec.mShader = 2;
        for (int i = 0; i < 4; ++i)
        {
            Vertex v;
            v.mPosition = { float(i & 1), float(i >> 1), 0.f };
            v.mUV = { float(i & 1), float(i >> 1) };
            shapeSpec.mVertices.push_back(v);
        }
        shapeSpec.mTriangles = { { 0, 1, 3 }, { 0, 3, 2 } };
        LightingSpec spec;
        spec.mTextureSet = 3;
        spec.mFlags2 = flags2;
        spec.mUVOffset = { 0.f, 0.f };
        spec.mUVScale = { 1.f, 1.f };
        spec.mAlpha = 1.f;
        file.mBlocks.push_back(node(file, "Root", { 1 }));
        file.mBlocks.push_back(shape(file, shapeSpec));
        file.mBlocks.push_back(lightingShader(file, spec));
        file.mBlocks.push_back(textureSet({ texture }));
        return file.build();
    }

    struct SkyrimSceneReadTest : ::testing::Test
    {
        // The real shader options (the files of the source tree, compiled by glslang at the first pipeline), so that
        // the pipeline the builder asks for is the one the game would make.
        vsg::ref_ptr<const vsg::Options> mShaderOptions = Resource::createShaderOptions(
            (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "shaders").string(), "");
        Pipeline::Builder mBuilder{ mShaderOptions, vsg::Sampler::create() };
        vsg::ref_ptr<vsgAdapters::nif> mReader
            = vsg::ref_ptr{ new vsgAdapters::nif(mBuilder, vsg::Options::create(), 0, 0) };
        std::shared_ptr<const vsgAdapters::nif::SceneReader> mSceneReader
            = MWRender::SkyrimModel::makeSceneReader(mBuilder, vsg::Options::create(), 0);

        vsg::ref_ptr<vsg::Object> read(const std::string& content, std::string_view filename = "test.nif") const
        {
            auto options = vsg::Options::create();
            options->extensionHint = ".nif";
            options->sharedObjects = vsg::SharedObjects::create();
            options->setValue("filename", std::string(filename));
            std::istringstream stream(content);
            return mReader->read(stream, options);
        }
    };
}

TEST_F(SkyrimSceneReadTest, a_skyrim_file_is_refused_without_a_scene_reader)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} }; // the Morrowind default, whatever other tests left
    mReader->sceneReader = nullptr;
    EXPECT_FALSE(read(skyrimQuad()));
}

TEST_F(SkyrimSceneReadTest, a_skyrim_file_becomes_one_indexed_draw_with_a_scene_reader)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    mReader->sceneReader = mSceneReader;
    const auto before = MWRender::SkyrimModel::counters().mModels.load();
    const auto result = read(skyrimQuad());
    ASSERT_TRUE(result);
    auto* node = result->cast<vsg::Node>();
    ASSERT_NE(node, nullptr);
    Census census;
    node->accept(census);
    EXPECT_EQ(census.draws, 1);
    EXPECT_EQ(census.stateGroups, 1);
    EXPECT_EQ(census.indexCount, 6u);
    EXPECT_TRUE(census.uintIndices);
    // position, normal, texture coordinates
    EXPECT_EQ(census.arrays, 3u);
    EXPECT_EQ(MWRender::SkyrimModel::counters().mModels.load(), before + 1);
}

TEST_F(SkyrimSceneReadTest, the_missing_texture_is_counted_and_the_shape_still_draws)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    mReader->sceneReader = mSceneReader;
    const auto missingBefore = MWRender::SkyrimModel::counters().mTexturesMissing.load();
    const auto result = read(skyrimQuad(0x10, "textures\\does\\not\\exist_d.dds"));
    ASSERT_TRUE(result);
    Census census;
    result->cast<vsg::Node>()->accept(census);
    EXPECT_EQ(census.draws, 1);
    EXPECT_EQ(MWRender::SkyrimModel::counters().mTexturesMissing.load(), missingBefore + 1);
}

TEST_F(SkyrimSceneReadTest, a_double_sided_shape_is_counted)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    mReader->sceneReader = mSceneReader;
    const auto before = MWRender::SkyrimModel::counters().mDoubleSidedShapes.load();
    ASSERT_TRUE(read(skyrimQuad(0x10)));
    EXPECT_EQ(MWRender::SkyrimModel::counters().mDoubleSidedShapes.load(), before + 1);
    ASSERT_TRUE(read(skyrimQuad(0x0)));
    EXPECT_EQ(MWRender::SkyrimModel::counters().mDoubleSidedShapes.load(), before + 1);
}

TEST_F(SkyrimSceneReadTest, a_skyrim_file_that_cannot_be_read_gives_no_node_and_is_counted)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    mReader->sceneReader = mSceneReader;
    const auto before = MWRender::SkyrimModel::counters().mFailed.load();
    std::string broken = skyrimQuad();
    broken.resize(broken.size() / 2);
    EXPECT_FALSE(read(broken));
    EXPECT_EQ(MWRender::SkyrimModel::counters().mFailed.load(), before + 1);
}

TEST_F(SkyrimSceneReadTest, a_file_that_is_not_a_nif_at_all_gives_no_node)
{
    mReader->sceneReader = mSceneReader;
    EXPECT_FALSE(read(std::string("\x89PNG\r\n\x1a\n not a model", 22)));
    EXPECT_FALSE(read(""));
}

TEST_F(SkyrimSceneReadTest, a_model_that_draws_nothing_is_an_empty_group_not_a_missing_model)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    mReader->sceneReader = mSceneReader;
    GamebryoFile file;
    file.mStreamVersion = 100;
    file.mBlocks.push_back(node(file, "Root", {}));
    const auto result = read(file.build());
    ASSERT_TRUE(result);
    Census census;
    result->cast<vsg::Node>()->accept(census);
    EXPECT_EQ(census.draws, 0);
}

TEST_F(SkyrimSceneReadTest, a_texture_the_fallback_reader_answers_for_is_counted_as_missing)
{
    // The game's texture options end with Resource::Fallback, which answers a missing file with a magenta texel: the
    // counter must see through it (a plain "image came back" would count every miss as a loaded texture).
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    auto fallback = vsg::ref_ptr{ new Resource::Fallback };
    fallback->fallbackObject = vsg::ubvec4Array2D::create(
        1, 1, vsg::ubvec4(0xff, 0x00, 0xff, 0xff), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
    auto textureOptions = vsg::Options::create();
    textureOptions->readerWriters.push_back(fallback);
    auto reader = vsg::ref_ptr{ new vsgAdapters::nif(mBuilder, textureOptions, 0, 0) };
    reader->sceneReader = MWRender::SkyrimModel::makeSceneReader(mBuilder, textureOptions, 0);
    auto options = vsg::Options::create();
    options->extensionHint = ".nif";
    options->sharedObjects = vsg::SharedObjects::create();
    options->setValue("filename", std::string("fallback.nif"));
    std::istringstream stream(skyrimQuad(0x10, "textures\\does\\not\\exist_d.dds"));
    const auto missingBefore = MWRender::SkyrimModel::counters().mTexturesMissing.load();
    const auto loadedBefore = MWRender::SkyrimModel::counters().mTexturesLoaded.load();
    ASSERT_TRUE(reader->read(stream, options));
    EXPECT_EQ(MWRender::SkyrimModel::counters().mTexturesMissing.load(), missingBefore + 1);
    EXPECT_EQ(MWRender::SkyrimModel::counters().mTexturesLoaded.load(), loadedBefore);
}

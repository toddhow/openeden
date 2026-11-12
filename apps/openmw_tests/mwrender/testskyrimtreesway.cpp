// Task 0133 (Skyrim tree sway, `[Game] neutral tree sway`): the weight byte, the driver that writes the shared uniform,
// and the model builder giving a Tree_Anim shape the LOD_SWAY draw. Files are generated (nifscene/ssebuilder.hpp,
// shaderbuilder.hpp); no game asset is read.
#include <apps/openmw/mwrender/skyrimmodel.hpp>
#include <apps/openmw/mwrender/skyrimtreesway.hpp>
#include <components/lod/clustersway.hpp>
#include <components/nif/niffile.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/sets.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/shaderpermutation.hpp>
#include <components/vsgadapters/nif/nif.hpp>

#include <gtest/gtest.h>

#include <vsg/core/Visitor.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/Sampler.h>
#include <vsg/utils/SharedObjects.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <sstream>
#include <string>

#include "apps/components_tests/nifscene/shaderbuilder.hpp"

namespace
{
    using namespace NifScene::Testing;
    namespace Sway = MWRender::SkyrimTreeSway;

    constexpr std::uint32_t treeFlags2 = 0x10u | 0x20u | 0x20000000u; // double sided, vertex colours, Tree_Anim
    constexpr std::uint32_t stillFlags2 = 0x10u | 0x20u;

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

    // What the visitor finds in a built model.
    struct Census : vsg::Visitor
    {
        int draws = 0;
        std::vector<vsg::ref_ptr<vsg::vec4Array>> colourArrays;
        std::set<std::string> defines; // the GLSL defines of every stage of every pipeline bound
        std::vector<vsg::ref_ptr<vsg::Data>> uniformData; // the data of every uniform buffer descriptor at TEXMAT_BINDING
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::BindGraphicsPipeline& bind) override
        {
            if (bind.pipeline)
                for (const auto& stage : bind.pipeline->stages)
                    if (const auto* found = vsgUtil::glslDefinesOf(*stage))
                        defines.insert(found->begin(), found->end());
            bind.traverse(*this);
        }
        void apply(vsg::BindDescriptorSet& bind) override
        {
            if (bind.descriptorSet)
                for (const auto& descriptor : bind.descriptorSet->descriptors)
                    if (auto* buffer = descriptor->cast<vsg::DescriptorBuffer>();
                        buffer && buffer->dstBinding == Pipeline::Descriptors::TEXMAT_BINDING)
                        for (const auto& info : buffer->bufferInfoList)
                            uniformData.push_back(info->data);
            bind.traverse(*this);
        }
        void apply(vsg::VertexIndexDraw& draw) override
        {
            ++draws;
            for (const auto& info : draw.arrays)
                if (info && info->data)
                    if (auto colours = info->data.cast<vsg::vec4Array>())
                        colourArrays.push_back(colours);
        }
    };

    // A 100 units tall quad in the XZ plane with the given vertex alpha bytes, one shape.
    std::string treeQuad(std::uint32_t flags2, std::array<std::uint8_t, 4> alphas, bool colours = true)
    {
        GamebryoFile file;
        file.mStreamVersion = 100;
        ShapeSpec shapeSpec;
        shapeSpec.mName = "Leaves";
        shapeSpec.mFlags = VF_Vertex | VF_UVs | VF_Normals | (colours ? VF_Colors : 0);
        shapeSpec.mShader = 2;
        for (int i = 0; i < 4; ++i)
        {
            Vertex v;
            v.mPosition = { float(i & 1), 0.f, float(i >> 1) * 100.f };
            v.mUV = { float(i & 1), float(i >> 1) };
            v.mColor = { 200, 180, 160, alphas[static_cast<std::size_t>(i)] };
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
        file.mBlocks.push_back(textureSet({ "textures\\test\\leaf_d.dds" }));
        return file.build();
    }

    struct TreeSwayBuildTest : ::testing::Test
    {
        vsg::ref_ptr<const vsg::Options> mShaderOptions
            = Resource::createShaderOptions(
                (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "shaders").string(), "");
        Pipeline::Builder mBuilder{ mShaderOptions, vsg::Sampler::create() };
        vsg::ref_ptr<vsgAdapters::nif> mReader
            = vsg::ref_ptr{ new vsgAdapters::nif(mBuilder, vsg::Options::create(), 0, 0) };

        vsg::ref_ptr<vsg::Object> read(const std::string& content, const Sway::Driver* driver) const
        {
            mReader->sceneReader = MWRender::SkyrimModel::makeSceneReader(
                mBuilder, vsg::Options::create(), 0, driver ? driver->uniform() : vsg::ref_ptr<vsg::mat4Value>{});
            auto options = vsg::Options::create();
            options->extensionHint = ".nif";
            options->sharedObjects = vsg::SharedObjects::create();
            options->setValue("filename", std::string("tree.nif"));
            std::istringstream stream(content);
            return mReader->read(stream, options);
        }
    };

    Census census(const vsg::ref_ptr<vsg::Object>& model)
    {
        Census c;
        model->cast<vsg::Node>()->accept(c);
        return c;
    }
}

TEST(SkyrimTreeSwayWeight, a_rigid_vertex_has_no_weight)
{
    EXPECT_EQ(Sway::weightByte(0.f, 800.f), 0);
    EXPECT_EQ(Sway::weightByte(-1.f, 800.f), 0);
}

TEST(SkyrimTreeSwayWeight, a_free_tip_of_a_tall_tree_takes_the_whole_range)
{
    EXPECT_EQ(Sway::weightByte(1.f, Lod::swayWeightScale), 255);
    EXPECT_EQ(Sway::weightByte(1.f, 10.f * Lod::swayWeightScale), 255); // clamped, not wrapped
    EXPECT_EQ(Sway::weightByte(7.f, Lod::swayWeightScale), 255); // alpha above 1 counts as 1
}

TEST(SkyrimTreeSwayWeight, the_value_is_the_square_root_of_height_times_alpha_squared_over_the_scale)
{
    // 512 / 2048 = 0.25, sqrt = 0.5, * 255 = 127.5 -> 128
    EXPECT_EQ(Sway::weightByte(1.f, 512.f), 128);
    // alpha 0.5 on 2048: 0.25, sqrt 0.5
    EXPECT_EQ(Sway::weightByte(0.5f, Lod::swayWeightScale), 128);
    // 800 * 0.36 / 2048 = 0.140625, sqrt = 0.375 -> 95.6 -> 96
    EXPECT_EQ(Sway::weightByte(0.6f, 800.f), 96);
}

TEST(SkyrimTreeSwayWeight, the_shader_squares_it_back_to_height_times_alpha_squared)
{
    for (float height : { 150.f, 400.f, 900.f, 1500.f })
        for (float alpha : { 0.1f, 0.3f, 0.5f, 0.8f, 1.f })
        {
            const float decoded = Lod::swayDecodeWeight(Sway::weightByte(alpha, height));
            const float wanted = height * alpha * alpha;
            // one byte step of the square root: at most 2 * sqrt(wanted * scale) / 255 * ... keep it simple: 3 percent + 8
            EXPECT_NEAR(decoded, wanted, 0.03f * wanted + 8.f) << height << " " << alpha;
        }
}

TEST(SkyrimTreeSwayWeight, a_larger_alpha_never_gives_a_smaller_weight)
{
    int previous = 0;
    for (int i = 0; i <= 100; ++i)
    {
        const int w = Sway::weightByte(static_cast<float>(i) / 100.f, 700.f);
        EXPECT_GE(w, previous);
        previous = w;
    }
}

TEST(SkyrimTreeSwayWeight, values_that_are_not_numbers_give_no_weight)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(Sway::weightByte(nan, 800.f), 0);
    EXPECT_EQ(Sway::weightByte(1.f, nan), 0);
    EXPECT_EQ(Sway::weightByte(1.f, inf), 0);
    EXPECT_EQ(Sway::weightByte(inf, 800.f), 0);
    EXPECT_EQ(Sway::weightByte(1.f, 0.f), 0);
    EXPECT_EQ(Sway::weightByte(1.f, -5.f), 0);
}

TEST(SkyrimTreeSwayWind, a_calm_weather_still_moves_a_little_and_the_scale_is_clamped)
{
    EXPECT_FLOAT_EQ(Sway::windScale(0.f), 0.3f);
    EXPECT_FLOAT_EQ(Sway::windScale(1.f), 1.f);
    EXPECT_FLOAT_EQ(Sway::windScale(0.5f), 0.65f);
    EXPECT_FLOAT_EQ(Sway::windScale(3.f), 1.f);
    EXPECT_FLOAT_EQ(Sway::windScale(-1.f), 0.3f);
    EXPECT_FLOAT_EQ(Sway::windScale(std::numeric_limits<float>::quiet_NaN()), 0.3f);
}

TEST(SkyrimTreeSwayDriver, the_uniform_is_still_before_the_first_update)
{
    Sway::Driver driver;
    ASSERT_TRUE(driver.uniform());
    const auto& m = driver.uniform()->value();
    EXPECT_FLOAT_EQ(m(0, 0), 0.f);
    EXPECT_FLOAT_EQ(m(0, 1), 0.f);
    EXPECT_FLOAT_EQ(m(0, 2), 0.f);
    EXPECT_FLOAT_EQ(m(0, 3), 0.f); // no distance boost
}

TEST(SkyrimTreeSwayDriver, an_update_writes_the_push_along_the_cloud_wind_and_the_clock_angle)
{
    Sway::Driver driver;
    driver.update(1.5, 1.f);
    const auto& m = driver.uniform()->value();
    const float push = Sway::lean * 1.f * Lod::swayWeightScale;
    EXPECT_NEAR(m(0, 0), push * 0.9578263f, 1e-3f);
    EXPECT_NEAR(m(0, 1), push * 0.2873479f, 1e-3f);
    EXPECT_NEAR(m(0, 2), Lod::swayClockAngle(1.5), 1e-6f);
    EXPECT_FLOAT_EQ(m(0, 3), 0.f);
    EXPECT_DOUBLE_EQ(driver.clockSeconds(), 1.5);
}

TEST(SkyrimTreeSwayDriver, the_clock_adds_up_and_bad_steps_are_ignored)
{
    Sway::Driver driver;
    driver.update(0.25, 0.f);
    driver.update(0.25, 0.f);
    driver.update(-3.0, 0.f);
    driver.update(std::numeric_limits<double>::quiet_NaN(), 0.f);
    driver.update(std::numeric_limits<double>::infinity(), 0.f);
    EXPECT_DOUBLE_EQ(driver.clockSeconds(), 0.5);
    EXPECT_NEAR(driver.uniform()->value()(0, 2), Lod::swayClockAngle(0.5), 1e-6f);
}

TEST(SkyrimTreeSwayDriver, the_push_follows_the_weathers_wind)
{
    Sway::Driver calm, gale;
    calm.update(0.1, 0.f);
    gale.update(0.1, 1.f);
    const float calmPush = std::hypot(calm.uniform()->value()(0, 0), calm.uniform()->value()(0, 1));
    const float galePush = std::hypot(gale.uniform()->value()(0, 0), gale.uniform()->value()(0, 1));
    EXPECT_NEAR(calmPush, Sway::lean * 0.3f * Lod::swayWeightScale, 1e-3f);
    EXPECT_NEAR(galePush, Sway::lean * 1.f * Lod::swayWeightScale, 1e-3f);
    EXPECT_GT(galePush, calmPush);
}

TEST(SkyrimTreeSwayDriver, an_update_that_changes_nothing_does_not_mark_the_uniform_modified)
{
    Sway::Driver driver;
    driver.update(0.5, 0.4f);
    vsg::ModifiedCount before;
    driver.uniform()->getModifiedCount(before);
    driver.update(0.0, 0.4f); // the clock does not move, the wind is the same
    EXPECT_FALSE(driver.uniform()->differentModifiedCount(before));
    driver.update(0.1, 0.4f);
    EXPECT_TRUE(driver.uniform()->differentModifiedCount(before));
}

TEST_F(TreeSwayBuildTest, a_tree_anim_shape_gets_the_sway_uniform_and_weights_in_its_colour_alpha)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    Sway::Driver driver;
    const auto model = read(treeQuad(treeFlags2, { 0, 64, 128, 255 }), &driver);
    ASSERT_TRUE(model);
    const Census c = census(model);
    ASSERT_EQ(c.draws, 1);
    ASSERT_EQ(c.colourArrays.size(), 1u);
    ASSERT_EQ(c.uniformData.size(), 1u);
    EXPECT_EQ(c.uniformData[0].get(), static_cast<vsg::Data*>(driver.uniform().get())); // the one shared uniform
    EXPECT_EQ(c.defines.count("LOD_SWAY"), 1u); // the pipeline reads it as the sway path
    const auto& colours = *c.colourArrays[0];
    ASSERT_EQ(colours.size(), 4u);
    // The model is 100 units tall (vertices 0 and 1 at z 0, vertices 2 and 3 at z 100).
    const float alphas[4] = { 0.f, 64.f / 255.f, 128.f / 255.f, 1.f };
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_NEAR(colours[i].a, static_cast<float>(Sway::weightByte(alphas[i], 100.f)) / 255.f, 1e-6f) << i;
    EXPECT_GT(colours[3].a, 0.f);
    EXPECT_FLOAT_EQ(colours[0].a, 0.f);
    // the colour itself is untouched
    EXPECT_NEAR(colours[2].r, 200.f / 255.f, 1e-5f);
}

TEST_F(TreeSwayBuildTest, without_a_driver_a_tree_shape_is_drawn_as_it_was)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    const auto model = read(treeQuad(treeFlags2, { 0, 64, 128, 255 }), nullptr);
    ASSERT_TRUE(model);
    const Census c = census(model);
    ASSERT_EQ(c.draws, 1);
    EXPECT_TRUE(c.uniformData.empty());
    EXPECT_EQ(c.defines.count("LOD_SWAY"), 0u);
}

TEST_F(TreeSwayBuildTest, a_shape_without_the_tree_flag_does_not_sway_even_with_a_driver)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    Sway::Driver driver;
    const auto model = read(treeQuad(stillFlags2, { 0, 64, 128, 255 }), &driver);
    ASSERT_TRUE(model);
    const Census c = census(model);
    ASSERT_EQ(c.draws, 1);
    EXPECT_TRUE(c.uniformData.empty());
}

TEST_F(TreeSwayBuildTest, two_tree_models_share_one_uniform_object)
{
    const DefaultOptionsGuard guard{ Nif::ReadOptions{} };
    Sway::Driver driver;
    const auto a = read(treeQuad(treeFlags2, { 0, 64, 128, 255 }), &driver);
    const auto b = read(treeQuad(treeFlags2, { 255, 255, 0, 0 }), &driver);
    ASSERT_TRUE(a && b);
    const Census ca = census(a), cb = census(b);
    ASSERT_EQ(ca.uniformData.size(), 1u);
    ASSERT_EQ(cb.uniformData.size(), 1u);
    EXPECT_EQ(ca.uniformData[0].get(), cb.uniformData[0].get());
}

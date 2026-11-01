#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <components/fx/technique.hpp>
#include <components/testing/util.hpp>

// What the renderer-free parser records: the built-in shaders parse, and samplers, passes and defaults come out as
// data.
namespace
{
    using namespace testing;
    using namespace Fx;

    std::unique_ptr<TestingOpenMW::VFSTestFile> readShader(const std::string& fileName)
    {
        std::ifstream stream(
            std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / "shaders" / fileName,
            std::ios::binary);
        EXPECT_TRUE(stream.is_open()) << fileName;
        return std::make_unique<TestingOpenMW::VFSTestFile>(
            std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
    }

    struct TechniqueBuiltinShadersTest : Test
    {
        std::unique_ptr<TestingOpenMW::VFSTestFile> mAdjustments = readShader("adjustments.omwfx");
        std::unique_ptr<TestingOpenMW::VFSTestFile> mBloomLinear = readShader("bloomlinear.omwfx");
        std::unique_ptr<TestingOpenMW::VFSTestFile> mDebug = readShader("debug.omwfx");
        std::unique_ptr<TestingOpenMW::VFSTestFile> mDistortion = readShader("internal_distortion.omwfx");
        std::unique_ptr<VFS::Manager> mVFS = TestingOpenMW::createTestVFS({
            { VFS::Path::NormalizedView("shaders/adjustments.omwfx"), mAdjustments.get() },
            { VFS::Path::NormalizedView("shaders/bloomlinear.omwfx"), mBloomLinear.get() },
            { VFS::Path::NormalizedView("shaders/debug.omwfx"), mDebug.get() },
            { VFS::Path::NormalizedView("shaders/internal_distortion.omwfx"), mDistortion.get() },
        });

        std::unique_ptr<Technique> parse(const std::string& name)
        {
            auto technique = std::make_unique<Technique>(*mVFS, Technique::makeFileName(name), name, true, true);
            technique->compile();
            EXPECT_TRUE(technique->isValid()) << technique->getLastError();
            return technique;
        }
    };

    TEST_F(TechniqueBuiltinShadersTest, adjustments)
    {
        const auto technique = parse("adjustments");
        ASSERT_TRUE(technique->isValid());
        ASSERT_EQ(technique->getPasses().size(), 1);
        EXPECT_EQ(technique->getPasses()[0]->getName(), "main");
        EXPECT_EQ(technique->getPasses()[0]->mType, Fx::Types::PassDesc::Type::Pixel);
        ASSERT_EQ(technique->getUniformMap().size(), 2);
        EXPECT_EQ(technique->getUniformMap()[0]->mName, "uGamma");
        EXPECT_FLOAT_EQ(technique->getUniformMap()[0]->getMax<float>(), 5.f);
    }

    TEST_F(TechniqueBuiltinShadersTest, bloomlinear)
    {
        const auto technique = parse("bloomlinear");
        ASSERT_TRUE(technique->isValid());
        ASSERT_EQ(technique->getPasses().size(), 4);
        EXPECT_EQ(technique->getPasses()[0]->getName(), "nomipmap");
        EXPECT_EQ(technique->getPasses()[0]->getTarget(), "RT_NoMipmap");
        EXPECT_EQ(technique->getPasses()[1]->getName(), "horizontal");
        EXPECT_EQ(technique->getPasses()[1]->getTarget(), "RT_Horizontal");
        EXPECT_EQ(technique->getPasses()[1]->mRenderTargets[0], "RT_NoMipmap");
        EXPECT_EQ(technique->getPasses()[3]->getName(), "final");
        EXPECT_EQ(technique->getPasses()[3]->getTarget(), "");
        EXPECT_EQ(technique->getPasses()[3]->mRenderTargets[0], "RT_Vertical");
        EXPECT_EQ(technique->getRenderTargetsMap().size(), 3);
        EXPECT_EQ(technique->getRenderTargetsMap().at("RT_Vertical").mName, "RT_Vertical");
        EXPECT_THAT(technique->getShared(), StartsWith("\n#line "));
        EXPECT_EQ(technique->getUniformMap().size(), 6);
    }

    TEST_F(TechniqueBuiltinShadersTest, debug)
    {
        const auto technique = parse("debug");
        ASSERT_TRUE(technique->isValid());
        ASSERT_EQ(technique->getPasses().size(), 1);
        EXPECT_TRUE(technique->getNormals());
        EXPECT_EQ(technique->getUniformMap().size(), 4);
        EXPECT_TRUE(technique->getUniformMap()[0]->getDefault<bool>());
    }

    TEST_F(TechniqueBuiltinShadersTest, internal_distortion)
    {
        const auto technique = parse("internal_distortion");
        ASSERT_TRUE(technique->isValid());
        ASSERT_EQ(technique->getPasses().size(), 1);
        EXPECT_TRUE(technique->getHidden());
        EXPECT_FALSE(technique->getPasses()[0]->mVertex.has_value());
        ASSERT_TRUE(technique->getPasses()[0]->mFragment.has_value());
        EXPECT_THAT(*technique->getPasses()[0]->mFragment, HasSubstr("omw_GetDistortion"));
    }

    TestingOpenMW::VFSTestFile samplersAndBlend{ R"(
    sampler_1d s1 { source = "textures/One.dds"; wrap_s = mirror; }
    sampler_2d s2 {
        source = "textures/two.dds";
        min_filter = nearest;
        compression = s3tc_dxt5;
        source_format = bgra;
        source_type = half_float;
        internal_format = rgba16f;
    }
    sampler_3d s3 { source = "textures/three.dds"; wrap_r = repeat; }
    render_target rt { width = 64; }
    fragment main(target=rt, rt2=rt, blend=(reverse_subtract, src_alpha, one_minus_src_alpha)) {
        void main() {}
    }
    vertex main {
        void main() {}
    }
    fragment add(blend=(add, one, zero)) {}
    compute cs {
        void main() {}
    }
    technique { passes = main, add, cs; }
)" };

    struct TechniqueDataTest : Test
    {
        std::unique_ptr<VFS::Manager> mVFS = TestingOpenMW::createTestVFS({
            { VFS::Path::NormalizedView("shaders/samplers_and_blend.omwfx"), &samplersAndBlend },
        });
        Technique mTechnique{ *mVFS, Technique::makeFileName("samplers_and_blend"), "samplers_and_blend", true, true };
    };

    TEST_F(TechniqueDataTest, samplers_are_recorded_without_loading_their_image)
    {
        ASSERT_TRUE(mTechnique.compile()) << mTechnique.getLastError();
        const Technique::SamplerList& samplers = mTechnique.getSamplers();
        ASSERT_EQ(samplers.size(), 3);

        EXPECT_EQ(samplers[0].mName, "s1");
        EXPECT_EQ(samplers[0].mType, Fx::Types::Texture_1D);
        EXPECT_EQ(samplers[0].mSource, VFS::Path::NormalizedView("textures/one.dds"));
        EXPECT_EQ(samplers[0].mWrapS, Fx::Types::WrapMode::Mirror);
        EXPECT_EQ(samplers[0].mWrapT, Fx::Types::WrapMode::ClampToEdge);
        EXPECT_EQ(samplers[0].mCompression, Fx::Types::Compression::ImageData);
        EXPECT_FALSE(samplers[0].mInternalFormat.has_value());

        EXPECT_EQ(samplers[1].mType, Fx::Types::Texture_2D);
        EXPECT_EQ(samplers[1].mMinFilter, Fx::Types::FilterMode::Nearest);
        EXPECT_EQ(samplers[1].mMagFilter, Fx::Types::FilterMode::Linear);
        EXPECT_EQ(samplers[1].mCompression, Fx::Types::Compression::S3tcDxt5);
        EXPECT_EQ(samplers[1].mSourceFormat, Fx::Types::SourceFormat::Bgra);
        EXPECT_EQ(samplers[1].mSourceType, Fx::Types::SourceType::HalfFloat);
        EXPECT_EQ(samplers[1].mInternalFormat, Fx::Types::InternalFormat::Rgba16f);

        EXPECT_EQ(samplers[2].mType, Fx::Types::Texture_3D);
        EXPECT_EQ(samplers[2].mWrapR, Fx::Types::WrapMode::Repeat);

        ASSERT_EQ(mTechnique.getUniformMap().size(), 3);
        EXPECT_EQ(mTechnique.getUniformMap()[0]->mSamplerType, Fx::Types::Texture_1D);
        EXPECT_EQ(mTechnique.getUniformMap()[0]->getGLSL(false), "uniform sampler1D s1;");
    }

    TEST_F(TechniqueDataTest, passes_hold_their_blend_state_and_sources)
    {
        ASSERT_TRUE(mTechnique.compile()) << mTechnique.getLastError();
        ASSERT_EQ(mTechnique.getPasses().size(), 3);

        const Fx::Types::PassDesc& main = *mTechnique.getPasses()[0];
        EXPECT_EQ(main.mType, Fx::Types::PassDesc::Type::Pixel);
        EXPECT_EQ(main.mTarget, "rt");
        EXPECT_EQ(main.mRenderTargets[1], "rt");
        EXPECT_EQ(main.mBlendEq, Fx::Types::BlendEquation::ReverseSubtract);
        EXPECT_EQ(main.mBlendSource, Fx::Types::BlendFunc::SrcAlpha);
        EXPECT_EQ(main.mBlendDest, Fx::Types::BlendFunc::OneMinusSrcAlpha);
        ASSERT_TRUE(main.mFragment.has_value());
        EXPECT_THAT(*main.mFragment, StartsWith("\n#line 13\n"));
        ASSERT_TRUE(main.mVertex.has_value());
        EXPECT_THAT(*main.mVertex, StartsWith("\n#line "));
        EXPECT_TRUE(main.mUBO);

        const Fx::Types::PassDesc& add = *mTechnique.getPasses()[1];
        EXPECT_EQ(add.mBlendSource, Fx::Types::BlendFunc::One);
        EXPECT_EQ(add.mBlendDest, Fx::Types::BlendFunc::Zero);
        EXPECT_FALSE(add.mBlendEq.has_value());

        const Fx::Types::PassDesc& compute = *mTechnique.getPasses()[2];
        EXPECT_EQ(compute.mType, Fx::Types::PassDesc::Type::Compute);
        EXPECT_TRUE(compute.mCompute.has_value());
    }

    TEST_F(TechniqueDataTest, render_target_defaults)
    {
        ASSERT_TRUE(mTechnique.compile()) << mTechnique.getLastError();
        const Fx::Types::RenderTarget& rt = mTechnique.getRenderTargetsMap().at("rt");

        EXPECT_EQ(rt.mName, "rt");
        EXPECT_EQ(rt.mSourceFormat, Fx::Types::SourceFormat::Rgb);
        EXPECT_EQ(rt.mInternalFormat, Fx::Types::InternalFormat::Rgb);
        EXPECT_EQ(rt.mSourceType, Fx::Types::SourceType::UnsignedByte);
        EXPECT_EQ(rt.mWrapS, Fx::Types::WrapMode::ClampToEdge);
        EXPECT_EQ(rt.mMinFilter, Fx::Types::FilterMode::LinearMipmapLinear);
        EXPECT_EQ(rt.mMagFilter, Fx::Types::FilterMode::Linear);
        EXPECT_FALSE(rt.mMipMap);
        EXPECT_EQ(rt.mClearColor, vsg::vec4(0, 0, 0, 1));
        EXPECT_EQ(rt.mSize.get(800, 600), std::make_tuple(64, 600));
    }
}

// Task 0153 follow-up: a Skyrim file read with a Pipeline::Override that has a materialData hook (the neutral grass's
// groundcover context) gets the hook's diffuse colour multiplied into the shape's vertex colours, because the lit
// shader takes the vertex colour in place of the material colour (colour mode 2). Without an override the colours are
// the file's. Generated files, no game asset.
#include <apps/openmw/mwrender/skyrimmodel.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgadapters/nif/nif.hpp>

#include <gtest/gtest.h>

#include <vsg/core/Visitor.h>
#include <vsg/io/Options.h>
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

    // The first colour of the first vec4 array a draw binds (position, normal, then the colours).
    struct FirstColour : vsg::Visitor
    {
        bool found = false;
        vsg::vec4 colour{};
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::VertexIndexDraw& draw) override
        {
            for (const auto& info : draw.arrays)
                if (info && info->data)
                {
                    auto colours = info->data.cast<vsg::vec4Array>();
                    if (colours && !found && colours->size() > 0)
                    {
                        found = true;
                        colour = colours->at(0);
                    }
                }
        }
    };

    // A quad whose vertices are mid grey (0.5), with the vertex colour flag of the shader set.
    std::string greyQuad()
    {
        GamebryoFile file;
        file.mStreamVersion = 100;
        ShapeSpec shapeSpec;
        shapeSpec.mName = "Quad";
        shapeSpec.mFlags = VF_Vertex | VF_UVs | VF_Normals | VF_Colors;
        shapeSpec.mShader = 2;
        for (int i = 0; i < 4; ++i)
        {
            Vertex v;
            v.mPosition = { float(i & 1), float(i >> 1), 0.f };
            v.mUV = { float(i & 1), float(i >> 1) };
            v.mColor = { 128, 128, 128, 255 };
            shapeSpec.mVertices.push_back(v);
        }
        shapeSpec.mTriangles = { { 0, 1, 3 }, { 0, 3, 2 } };
        LightingSpec spec;
        spec.mTextureSet = 3;
        spec.mFlags2 = 0x10 | 0x20; // double sided, vertex colours
        spec.mUVOffset = { 0.f, 0.f };
        spec.mUVScale = { 1.f, 1.f };
        spec.mAlpha = 1.f;
        file.mBlocks.push_back(node(file, "Root", { 1 }));
        file.mBlocks.push_back(shape(file, shapeSpec));
        file.mBlocks.push_back(lightingShader(file, spec));
        file.mBlocks.push_back(textureSet({ "textures\\test\\wall_d.dds" }));
        return file.build();
    }

    struct SkyrimOverrideTest : ::testing::Test
    {
        vsg::ref_ptr<const vsg::Options> mShaderOptions
            = Resource::createShaderOptions(
                (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "shaders").string(), "");
        Pipeline::Builder mBuilder{ mShaderOptions, vsg::Sampler::create() };
        vsg::ref_ptr<vsgAdapters::nif> mReader
            = vsg::ref_ptr{ new vsgAdapters::nif(mBuilder, vsg::Options::create(), 0, 0) };

        vsg::vec4 firstColour(float scale, bool withOverride)
        {
            const Nif::ReadOptions saved = Nif::Reader::getDefaultReadOptions();
            Nif::Reader::setDefaultReadOptions(Nif::ReadOptions{});
            mReader->sceneReader = MWRender::SkyrimModel::makeSceneReader(mBuilder, vsg::Options::create(), 0);
            auto options = vsg::Options::create();
            options->extensionHint = ".nif";
            options->sharedObjects = vsg::SharedObjects::create();
            options->setValue("filename", std::string("grass.nif"));
            if (withOverride)
            {
                auto override_ = vsg::ref_ptr{ new Pipeline::Override };
                override_->materialData = [scale](Pipeline::Data::Material& material) {
                    material.diffuse.r *= scale;
                    material.diffuse.g *= scale;
                    material.diffuse.b *= scale;
                };
                override_->composite(*options);
            }
            std::istringstream stream(greyQuad());
            const auto result = mReader->read(stream, options);
            Nif::Reader::setDefaultReadOptions(saved);
            FirstColour finder;
            if (result)
                result->cast<vsg::Node>()->accept(finder);
            EXPECT_TRUE(finder.found);
            return finder.colour;
        }
    };
}

TEST_F(SkyrimOverrideTest, without_an_override_the_vertex_colours_are_the_files)
{
    const vsg::vec4 colour = firstColour(1.f, false);
    EXPECT_NEAR(colour.r, 128.f / 255.f, 1e-3f);
    EXPECT_NEAR(colour.g, 128.f / 255.f, 1e-3f);
    EXPECT_NEAR(colour.b, 128.f / 255.f, 1e-3f);
}

TEST_F(SkyrimOverrideTest, an_override_hooks_diffuse_colour_multiplies_the_vertex_colours)
{
    const vsg::vec4 colour = firstColour(0.5f, true);
    EXPECT_NEAR(colour.r, 0.5f * 128.f / 255.f, 1e-3f);
    EXPECT_NEAR(colour.g, 0.5f * 128.f / 255.f, 1e-3f);
    EXPECT_NEAR(colour.b, 0.5f * 128.f / 255.f, 1e-3f);
}

TEST_F(SkyrimOverrideTest, the_vertex_alpha_is_not_scaled)
{
    const vsg::vec4 colour = firstColour(0.5f, true);
    EXPECT_NEAR(colour.a, 1.f, 1e-3f);
}

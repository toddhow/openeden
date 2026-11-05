// Task 0087 stage b: the terrain tile fade in the shaders and in the batch uniform.
//
// Written before the implementation. A far terrain tile that appears, is replaced or goes away dithers in or out over
// a fraction of a second. The fade is one float in the batch's uniform (binding 2 of the terrain's texture set), read
// by BOTH stages now (the fragment stage discards by an ordered dither, as default/main.frag does for LOD_FADE), so:
//  - the uniform keeps its std140 size with the new member (the shader's block is 32 bytes, the C++ struct must be);
//  - the pipeline layout lets the fragment stage read it;
//  - both shader languages read `batch.fade` and call dither8 (the Slang twin must declare what the GLSL declares;
//    SlangPermutations checks the interfaces, these check that the statements exist);
//  - a draw with nothing to fade holds 1, which discards nothing;
//  - the fade can be set on a built draw's data (Builder::setBatchFade), marking it for upload.

#include <components/pipeline/terrain.hpp>
#include <components/terrain/batchdata.hpp>
#include <components/terrain/builder.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/spirv.h>
#include <vsg/state/DescriptorSetLayout.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/PipelineLayout.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    std::string readSource(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    bool contains(const std::string& text, const std::string& word)
    {
        return text.find(word) != std::string::npos;
    }

    TEST(TerrainFadeUniform, the_batch_uniform_is_32_bytes_with_the_fade_after_the_vertex_scale)
    {
        EXPECT_EQ(sizeof(Pipeline::Data::Batch), 32u);
        EXPECT_EQ(offsetof(Pipeline::Data::Batch, vertexScale), 12u);
        EXPECT_EQ(offsetof(Pipeline::Data::Batch, fade), 16u);
    }

    TEST(TerrainFadeUniform, a_new_batch_is_opaque)
    {
        const Pipeline::Data::Batch batch = Terrain::makeBatchUniform(vsg::vec2(1.f, 2.f), 33.f, 8192.f);
        EXPECT_EQ(batch.fade, 1.f);
        EXPECT_EQ(batch.origin, vsg::vec2(1.f, 2.f));
        EXPECT_EQ(batch.numVerts, 33.f);
        EXPECT_EQ(batch.vertexScale, 8192.f);
    }

    TEST(TerrainFadeUniform, the_fade_is_set_on_the_draws_data_and_marked_for_upload)
    {
        Terrain::Builder::BatchData data;
        EXPECT_FALSE(Terrain::Builder::setBatchFade(data, 0.5f)) << "no uniform, nothing to set";

        auto value = vsg::Value<Pipeline::Data::Batch>::create(Terrain::makeBatchUniform(vsg::vec2(), 33.f, 8192.f));
        data.batchUniform = value;
        vsg::ModifiedCount seen;
        value->getModifiedCount(seen);
        EXPECT_TRUE(Terrain::Builder::setBatchFade(data, 0.25f));
        EXPECT_EQ(value->value().fade, 0.25f);
        EXPECT_TRUE(value->differentModifiedCount(seen));

        // The same value again changes nothing and uploads nothing.
        value->getModifiedCount(seen);
        EXPECT_TRUE(Terrain::Builder::setBatchFade(data, 0.25f));
        EXPECT_FALSE(value->differentModifiedCount(seen));

        // The fade is clamped to 0..1.
        Terrain::Builder::setBatchFade(data, 7.f);
        EXPECT_EQ(value->value().fade, 1.f);
        Terrain::Builder::setBatchFade(data, -1.f);
        EXPECT_EQ(value->value().fade, 0.f);
    }

    TEST(TerrainFadeShader, both_languages_declare_the_member_and_read_it_with_the_ordered_dither)
    {
        for (const char* file : { "terrain/data.glsl", "terrain/data.slang" })
        {
            SCOPED_TRACE(file);
            const std::string text = readSource(file);
            ASSERT_FALSE(text.empty());
            EXPECT_TRUE(contains(text, "float fade;"));
        }
        for (const char* file : { "terrain/main.frag", "terrain/terrain.frag.slang" })
        {
            SCOPED_TRACE(file);
            const std::string text = readSource(file);
            ASSERT_FALSE(text.empty());
            EXPECT_TRUE(contains(text, "dither8("));
            EXPECT_TRUE(contains(text, "batch.fade"));
            EXPECT_TRUE(contains(text, "BATCH_DATA_BINDING"));
        }
    }

    TEST(TerrainFadeShader, the_discard_comes_after_the_derivative_taking_code)
    {
        // The pixels that the dither drops must not change the quad's derivatives for the pixels that stay: the discard
        // is the last statement (the POM march and the texture reads come first).
        const std::string glsl = readSource("terrain/main.frag");
        const auto discard = glsl.rfind("discard");
        ASSERT_NE(discard, std::string::npos);
        const auto lastFetch = glsl.rfind("texture(");
        ASSERT_NE(lastFetch, std::string::npos);
        EXPECT_GT(discard, lastFetch);
        const auto lastDebug = glsl.rfind("DEBUG_CHUNKS");
        EXPECT_GT(discard, lastDebug);
    }

    class StubShaderFolder
    {
    public:
        StubShaderFolder()
            : mDir(std::filesystem::temp_directory_path() / "openeden-terrainfade-stub")
        {
            std::filesystem::create_directories(mDir / "terrain");
            for (const char* name : { "terrain/main.vert", "terrain/main.frag" })
            {
                std::ofstream out(mDir / name, std::ios::binary);
                out << "#version 450\nvoid main() {}\n";
            }
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::glsl::create(), vsg::spirv::create() };
        }
        ~StubShaderFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        vsg::ref_ptr<const vsg::Options> options() const { return mOptions; }

    private:
        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    TEST(TerrainFadeLayout, the_batch_uniform_is_visible_to_the_fragment_stage)
    {
        StubShaderFolder folder;
        for (const bool maps : { false, true })
        {
            auto bind = Pipeline::terrain(folder.options(), maps);
            ASSERT_TRUE(bind);
            const auto& layouts = bind->pipeline->layout->setLayouts;
            ASSERT_FALSE(layouts.empty());
            const auto& textureSet = layouts.back();
            bool found = false;
            for (const auto& binding : textureSet->bindings)
            {
                if (binding.binding == 2)
                {
                    found = true;
                    EXPECT_EQ(binding.descriptorType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                    EXPECT_TRUE((binding.stageFlags & VK_SHADER_STAGE_VERTEX_BIT) != 0);
                    EXPECT_TRUE((binding.stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) != 0);
                }
            }
            EXPECT_TRUE(found);
        }
    }
}

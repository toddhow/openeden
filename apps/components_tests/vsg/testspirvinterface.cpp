// Task 0072, stage a: the SPIR-V interface reader (components/vsgutil/spirvinterface.hpp), on small GLSL stages
// compiled here with VSG's glslang. Written before the implementation.
#include <gtest/gtest.h>

#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::vector<uint32_t> compile(VkShaderStageFlagBits stage, const std::string& source)
    {
        auto shaderStage = vsg::ShaderStage::create(stage, "main", source, vsg::ShaderCompileSettings::create());
        vsg::ShaderStages stages{ shaderStage };
        if (!vsg::ShaderCompiler::create()->compile(stages))
            return {};
        return shaderStage->module->code;
    }

    const char* const sFragment
        = "#version 450\n"
          "layout(set = 2, binding = 0) uniform sampler2D diffuseMap;\n"
          "layout(set = 0, binding = 0) uniform Block { mat4 m; vec4 v; float f; } block;\n"
          "layout(push_constant) uniform PC { mat4 projection; mat4 modelview; } pc;\n"
          "layout(constant_id = 3) const bool useColor = false;\n"
          "layout(location = 1) in vec4 vertColor;\n"
          "layout(location = 2) in vec2 texCoord;\n"
          "layout(location = 0) out vec4 outColor;\n"
          "void main() {\n"
          "    outColor = texture(diffuseMap, texCoord) * (useColor ? vertColor : block.v)\n"
          "        * block.f * pc.projection[0][0];\n"
          "}\n";

    bool has(const vsgUtil::SpirvInterface& i, const std::string& line)
    {
        return i.mLines.count(line) != 0;
    }

    TEST(SpirvInterface, reads_what_a_fragment_stage_declares)
    {
        const auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment);
        ASSERT_FALSE(words.empty());
        const auto i = vsgUtil::readSpirvInterface(words);
        EXPECT_NE(i.mVersion, 0u);
        EXPECT_EQ(i.mEntry, "4 main"); // ExecutionModel Fragment = 4
        EXPECT_TRUE(has(i, "entry 4 main"));
        EXPECT_TRUE(has(i, "descriptor set=2 binding=0 sampled_image(image(f32,dim1,arrayed0,ms0))"));
        EXPECT_TRUE(has(i, "descriptor set=0 binding=0 uniform 0:mat4xvec4f32 stride16;64:vec4f32;80:f32;"));
        EXPECT_TRUE(has(i, "push_constant 0:mat4xvec4f32 stride16;64:mat4xvec4f32 stride16;"));
        EXPECT_TRUE(has(i, "in location=1 vec4f32"));
        EXPECT_TRUE(has(i, "in location=2 vec2f32"));
        EXPECT_TRUE(has(i, "out location=0 vec4f32"));
        EXPECT_TRUE(has(i, "spec_constant id=3 bool"));
    }

    TEST(SpirvInterface, the_same_source_has_the_same_interface)
    {
        const auto a = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment));
        const auto b = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment));
        EXPECT_TRUE(vsgUtil::compareSpirvInterfaces(a, b).same());
    }

    TEST(SpirvInterface, a_moved_binding_or_location_is_a_difference)
    {
        std::string moved = sFragment;
        moved.replace(moved.find("binding = 0) uniform sampler2D"), 11, "binding = 5");
        std::string relocated = sFragment;
        relocated.replace(relocated.find("location = 2) in vec2"), 12, "location = 3");

        const auto base = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment));
        const auto a = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, moved));
        const auto b = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, relocated));

        const auto diffA = vsgUtil::compareSpirvInterfaces(base, a);
        ASSERT_FALSE(diffA.same());
        EXPECT_EQ(diffA.mOnlyInFirst,
            std::vector<std::string>{ "descriptor set=2 binding=0 sampled_image(image(f32,dim1,arrayed0,ms0))" });
        EXPECT_EQ(diffA.mOnlyInSecond,
            std::vector<std::string>{ "descriptor set=2 binding=5 sampled_image(image(f32,dim1,arrayed0,ms0))" });

        const auto diffB = vsgUtil::compareSpirvInterfaces(base, b);
        EXPECT_EQ(diffB.mOnlyInFirst, std::vector<std::string>{ "in location=2 vec2f32" });
        EXPECT_EQ(diffB.mOnlyInSecond, std::vector<std::string>{ "in location=3 vec2f32" });
    }

    TEST(SpirvInterface, a_changed_block_layout_is_a_difference)
    {
        std::string swapped = sFragment;
        swapped.replace(swapped.find("vec4 v; float f;"), 16, "float f; vec4 v;");
        const auto base = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment));
        const auto other = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, swapped));
        EXPECT_FALSE(vsgUtil::compareSpirvInterfaces(base, other).same());
    }

    TEST(SpirvInterface, a_vertex_stage_lists_its_built_ins)
    {
        const auto words = compile(VK_SHADER_STAGE_VERTEX_BIT,
            "#version 450\n"
            "layout(location = 0) in vec3 inVertex;\n"
            "layout(location = 1) out vec4 outColor;\n"
            "out gl_PerVertex { vec4 gl_Position; };\n"
            "void main() { gl_Position = vec4(inVertex, 1.0); outColor = vec4(float(gl_VertexIndex)); }\n");
        ASSERT_FALSE(words.empty());
        const auto i = vsgUtil::readSpirvInterface(words);
        EXPECT_EQ(i.mEntry, "0 main"); // ExecutionModel Vertex = 0
        EXPECT_TRUE(has(i, "in location=0 vec3f32"));
        EXPECT_TRUE(has(i, "out location=1 vec4f32"));
        EXPECT_TRUE(has(i, "out builtin=0")); // Position
        EXPECT_TRUE(has(i, "in builtin=42")); // VertexIndex
    }

    TEST(SpirvInterface, words_that_are_not_spirv_are_refused)
    {
        EXPECT_THROW(vsgUtil::readSpirvInterface({}), std::runtime_error);
        EXPECT_THROW(vsgUtil::readSpirvInterface({ 1, 2, 3, 4, 5 }), std::runtime_error);
        // a valid header followed by an instruction whose word count runs past the end
        EXPECT_THROW(vsgUtil::readSpirvInterface({ 0x07230203, 0x00010000, 0, 10, 0, (20u << 16) | 19u, 1 }),
            std::runtime_error);
        // an instruction with a word count of zero
        EXPECT_THROW(vsgUtil::readSpirvInterface({ 0x07230203, 0x00010000, 0, 10, 0, 0 }), std::runtime_error);
    }

    TEST(SpirvInterface, a_truncated_real_module_is_refused_without_crashing)
    {
        auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment);
        ASSERT_GT(words.size(), 40u);
        for (std::size_t cut = 5; cut < words.size(); cut += 7)
        {
            std::vector<uint32_t> part(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(cut));
            try
            {
                vsgUtil::readSpirvInterface(part);
            }
            catch (const std::runtime_error&)
            {
            }
        }
        SUCCEED();
    }
}

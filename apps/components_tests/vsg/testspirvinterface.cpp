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
        // Task 0072, stage b (review): a cut through an instruction must be refused, not read past the end.
        std::size_t refused = 0;
        for (std::size_t cut = 5; cut < words.size(); cut += 7)
        {
            std::vector<uint32_t> part(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(cut));
            try
            {
                vsgUtil::readSpirvInterface(part);
            }
            catch (const std::runtime_error&)
            {
                ++refused;
            }
        }
        EXPECT_GT(refused, 0u);
    }

    // Task 0072, stage b: what the review of stage a found the reader did not compare. Written before the code.

    TEST(SpirvInterface, a_final_one_word_instruction_is_read_without_going_past_the_end)
    {
        // A valid header and OpNop (word count 1) as the last word: no operands to point at.
        EXPECT_NO_THROW(vsgUtil::readSpirvInterface({ 0x07230203, 0x00010000, 0, 10, 0, (1u << 16) | 0u }));
    }

    std::vector<std::string> differences(VkShaderStageFlagBits stage, const std::string& a, const std::string& b)
    {
        const auto wordsA = compile(stage, a);
        const auto wordsB = compile(stage, b);
        if (wordsA.empty() || wordsB.empty())
            return { "did not compile" };
        const auto diff
            = vsgUtil::compareSpirvInterfaces(vsgUtil::readSpirvInterface(wordsA), vsgUtil::readSpirvInterface(wordsB));
        std::vector<std::string> lines = diff.mOnlyInFirst;
        lines.insert(lines.end(), diff.mOnlyInSecond.begin(), diff.mOnlyInSecond.end());
        return lines;
    }

    std::string fragmentWith(const std::string& declarations, const std::string& body)
    {
        return "#version 450\n" + declarations + "layout(location = 0) out vec4 outColor;\nvoid main() {\n" + body
            + "}\n";
    }

    TEST(SpirvInterface, a_buffer_block_is_storage_and_not_a_uniform_block)
    {
        // glslang writes a std430 buffer at SPIR-V 1.0 as a Uniform variable whose struct is decorated BufferBlock.
        const auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT,
            fragmentWith("layout(std430, set = 0, binding = 1) readonly buffer B { vec4 data[]; } b;\n",
                "outColor = b.data[0];\n"));
        ASSERT_FALSE(words.empty());
        const auto i = vsgUtil::readSpirvInterface(words);
        bool storage = false;
        for (const std::string& line : i.mLines)
        {
            EXPECT_EQ(line.find("descriptor set=0 binding=1 uniform"), std::string::npos) << line;
            if (line.rfind("descriptor set=0 binding=1 storage", 0) == 0)
                storage = true;
        }
        EXPECT_TRUE(storage);
    }

    TEST(SpirvInterface, a_storage_image_a_shadow_sampler_and_a_sampled_image_differ)
    {
        const std::string sampled = fragmentWith(
            "layout(set = 0, binding = 2) uniform sampler2D map;\n", "outColor = texture(map, vec2(0.5));\n");
        const std::string shadow = fragmentWith("layout(set = 0, binding = 2) uniform sampler2DShadow map;\n",
            "outColor = vec4(texture(map, vec3(0.5)));\n");
        const std::string storage = fragmentWith("layout(set = 0, binding = 2, rgba8) uniform readonly image2D map;\n",
            "outColor = imageLoad(map, ivec2(0));\n");
        EXPECT_FALSE(differences(VK_SHADER_STAGE_FRAGMENT_BIT, sampled, shadow).empty());
        EXPECT_FALSE(differences(VK_SHADER_STAGE_FRAGMENT_BIT, sampled, storage).empty());
        EXPECT_FALSE(differences(VK_SHADER_STAGE_FRAGMENT_BIT, shadow, storage).empty());
    }

    TEST(SpirvInterface, a_compute_local_size_is_compared)
    {
        const auto source = [](const std::string& x) {
            return "#version 450\nlayout(local_size_x = " + x
                + ", local_size_y = 1, local_size_z = 1) in;\n"
                  "layout(std430, set = 0, binding = 0) buffer B { float v[]; } b;\n"
                  "void main() { b.v[gl_GlobalInvocationID.x] = 1.0; }\n";
        };
        EXPECT_FALSE(differences(VK_SHADER_STAGE_COMPUTE_BIT, source("8"), source("16")).empty());
        EXPECT_TRUE(differences(VK_SHADER_STAGE_COMPUTE_BIT, source("8"), source("8")).empty());
    }

    TEST(SpirvInterface, interpolation_qualifiers_are_compared)
    {
        const std::string smooth = fragmentWith("layout(location = 1) in vec4 colour;\n", "outColor = colour;\n");
        const std::string flat = fragmentWith("layout(location = 1) flat in vec4 colour;\n", "outColor = colour;\n");
        const std::string noPerspective
            = fragmentWith("layout(location = 1) noperspective in vec4 colour;\n", "outColor = colour;\n");
        EXPECT_FALSE(differences(VK_SHADER_STAGE_FRAGMENT_BIT, smooth, flat).empty());
        EXPECT_FALSE(differences(VK_SHADER_STAGE_FRAGMENT_BIT, smooth, noPerspective).empty());
    }

    TEST(SpirvInterface, a_specialization_constant_default_is_compared)
    {
        const auto source = [](const std::string& value) {
            return fragmentWith(
                "layout(constant_id = 3) const float strength = " + value + ";\n", "outColor = vec4(strength);\n");
        };
        EXPECT_FALSE(differences(VK_SHADER_STAGE_FRAGMENT_BIT, source("1.0"), source("2.0")).empty());
    }

    TEST(SpirvInterface, matrix_majority_and_shape_are_compared)
    {
        const auto source = [](const std::string& layout, const std::string& type) {
            return fragmentWith(
                "layout(std140, " + layout + ", set = 0, binding = 0) uniform Block { " + type + " m; } block;\n",
                "outColor = vec4(block.m[0][0]);\n");
        };
        // The same bytes read as a different matrix are a difference.
        EXPECT_FALSE(
            differences(VK_SHADER_STAGE_FRAGMENT_BIT, source("column_major", "mat4"), source("row_major", "mat4"))
                .empty());
        EXPECT_FALSE(differences(
            VK_SHADER_STAGE_FRAGMENT_BIT, source("column_major", "mat3x4"), source("column_major", "mat4x3"))
                         .empty());
        // A column-major mat3x4 (3 columns of vec4) is described by its mathematical shape.
        const auto i
            = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, source("column_major", "mat3x4")));
        EXPECT_TRUE(has(i, "descriptor set=0 binding=0 uniform 0:mat3xvec4f32 stride16;")) << [&] {
            std::string all;
            for (const auto& line : i.mLines)
                all += line + "\n";
            return all;
        }();
    }

    TEST(SpirvInterface, execution_modes_and_capabilities_are_listed)
    {
        const auto i = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment));
        EXPECT_TRUE(has(i, "execution_mode 7")); // OriginUpperLeft
        EXPECT_TRUE(has(i, "capability 1")); // Shader
    }

    // Task 0072, stage f: the object shader's texture coordinate arrays are sized by the specialization constant
    // numUvSets; a twin is built for one count, so the GLSL is read with that constant pinned to the count.
    TEST(SpirvInterface, a_pinned_specialization_constant_sizes_its_arrays_and_leaves_the_interface)
    {
        const char* const vertex
            = "#version 450\n"
              "layout(constant_id = 0) const int numUvSets = 1;\n"
              "layout(constant_id = 5) const int other = 0;\n"
              "layout(location = 2) in vec2 inTexCoord[numUvSets];\n"
              "layout(location = 0) out Outputs { vec3 viewPos; vec2 texCoord[numUvSets]; } o;\n"
              "void main() {\n"
              "    o.viewPos = vec3(float(other));\n"
              "    for (int i = 0; i < numUvSets; ++i)\n"
              "        o.texCoord[i] = inTexCoord[i];\n"
              "    gl_Position = vec4(0.0);\n"
              "}\n";
        const auto words = compile(VK_SHADER_STAGE_VERTEX_BIT, vertex);
        ASSERT_FALSE(words.empty());
        const auto plain = vsgUtil::readSpirvInterface(words);
        EXPECT_TRUE(has(plain, "in location=2 array1(vec2f32)"));
        EXPECT_TRUE(has(plain, "out location=1 array1(vec2f32)"));
        EXPECT_TRUE(has(plain, "spec_constant id=0 i32"));
        const auto pinned = vsgUtil::readSpirvInterface(words, { { 0u, 3u } });
        EXPECT_TRUE(has(pinned, "in location=2 array3(vec2f32)"));
        EXPECT_TRUE(has(pinned, "out location=1 array3(vec2f32)"));
        for (const std::string& line : pinned.mLines)
        {
            EXPECT_NE(line.rfind("spec_constant id=0 ", 0), 0u) << line;
            EXPECT_NE(line.rfind("spec_constant_default id=0 ", 0), 0u) << line;
        }
        EXPECT_TRUE(has(pinned, "spec_constant id=5 i32"));
        EXPECT_EQ(pinned.mSpecConstantNames.count(0), 0u);
        EXPECT_EQ(pinned.mSpecConstantNames.count(5), 1u);
    }

    // Task 0072, stage f: glslang keeps a specialization constant that no instruction reads (the object shader's UV set
    // constants of the maps a draw does not have), Slang drops it, and a pipeline's value for it changes nothing: it
    // is not part of the interface. Its name stays known.
    TEST(SpirvInterface, a_specialization_constant_nothing_reads_is_not_listed)
    {
        const char* const fragment
            = "#version 450\n"
              "layout(constant_id = 3) const bool used = false;\n"
              "layout(constant_id = 7) const int unused = 0;\n"
              "layout(location = 0) out vec4 outColor;\n"
              "void main() { outColor = used ? vec4(1.0) : vec4(0.0); }\n";
        const auto i = vsgUtil::readSpirvInterface(compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragment));
        EXPECT_TRUE(has(i, "spec_constant id=3 bool"));
        for (const std::string& line : i.mLines)
        {
            EXPECT_NE(line.rfind("spec_constant id=7 ", 0), 0u) << line;
            EXPECT_NE(line.rfind("spec_constant_default id=7 ", 0), 0u) << line;
        }
        EXPECT_EQ(i.mSpecConstantNames.count(7), 1u);
    }
}

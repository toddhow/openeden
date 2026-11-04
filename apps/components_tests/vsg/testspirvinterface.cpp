// Task 0072, stage a: the SPIR-V interface reader (components/vsgutil/spirvinterface.hpp), on small GLSL stages
// compiled here with VSG's glslang. Written before the implementation.
#include <gtest/gtest.h>

#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>

#include <algorithm>
#include <map>
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

    // Task 0072, review of stage f, finding 11: the reader lists a specialization constant only when some instruction
    // reads it, and the twins read some constants only as an OpSwitch selector or only as an OpAccessChain index. The
    // helpers below find, in the compiled words, that the instruction meant is really there, so that each test
    // exercises that use and not another. Written before the implementation.

    // The words of every instruction with `opcode`, its first word (word count and opcode) included.
    std::vector<std::vector<uint32_t>> instructionsWith(const std::vector<uint32_t>& words, uint32_t opcode)
    {
        std::vector<std::vector<uint32_t>> found;
        std::size_t at = 5;
        while (at < words.size())
        {
            const std::size_t count = words[at] >> 16;
            if (count == 0 || at + count > words.size())
                break;
            if ((words[at] & 0xffffu) == opcode)
            {
                found.emplace_back(words.begin() + static_cast<std::ptrdiff_t>(at),
                    words.begin() + static_cast<std::ptrdiff_t>(at + count));
            }
            at += count;
        }
        return found;
    }

    // The result id of the specialization constant with SpecId `specId` (OpDecorate <id> SpecId <n>: opcode 71,
    // decoration 1), or 0.
    uint32_t specConstantResultId(const std::vector<uint32_t>& words, uint32_t specId)
    {
        for (const std::vector<uint32_t>& decorate : instructionsWith(words, 71))
        {
            if (decorate.size() >= 4 && decorate[2] == 1 && decorate[3] == specId)
                return decorate[1];
        }
        return 0;
    }

    std::string linesOf(const vsgUtil::SpirvInterface& i)
    {
        std::string all;
        for (const std::string& line : i.mLines)
            all += line + "\n";
        return all;
    }

    // Task 0072, review of stage f, finding 11 (written before the implementation): a constant read only as the
    // selector of a switch (OpSwitch, opcode 251, selector in word 1) is read, so it is listed.
    TEST(SpirvInterface, a_specialization_constant_read_only_as_a_switch_selector_is_listed)
    {
        const char* const fragment
            = "#version 450\n"
              "layout(constant_id = 4) const int mode = 0;\n"
              "layout(location = 0) out vec4 outColor;\n"
              "void main() {\n"
              "    switch (mode) {\n"
              "    case 1: outColor = vec4(1.0); break;\n"
              "    case 2: outColor = vec4(0.5); break;\n"
              "    case 3: outColor = vec4(0.25); break;\n"
              "    default: outColor = vec4(0.0); break;\n"
              "    }\n"
              "}\n";
        const auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragment);
        ASSERT_FALSE(words.empty());
        const uint32_t id = specConstantResultId(words, 4);
        ASSERT_NE(id, 0u);
        bool selector = false;
        for (const std::vector<uint32_t>& op : instructionsWith(words, 251))
            selector = selector || (op.size() > 1 && op[1] == id);
        ASSERT_TRUE(selector) << "no OpSwitch on the constant";
        const auto i = vsgUtil::readSpirvInterface(words);
        EXPECT_EQ(i.mSpecConstantOps, 0u);
        EXPECT_TRUE(has(i, "spec_constant id=4 i32")) << linesOf(i);
    }

    // Task 0072, review of stage f, finding 11 (written before the implementation): a constant read only as an index
    // into a uniform block's array (OpAccessChain, opcode 65, indices from word 4) is read, so it is listed.
    TEST(SpirvInterface, a_specialization_constant_read_only_as_an_access_chain_index_is_listed)
    {
        const char* const fragment
            = "#version 450\n"
              "layout(constant_id = 6) const int index = 0;\n"
              "layout(set = 0, binding = 0) uniform Block { vec4 v[8]; } b;\n"
              "layout(location = 0) out vec4 outColor;\n"
              "void main() { outColor = b.v[index]; }\n";
        const auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragment);
        ASSERT_FALSE(words.empty());
        const uint32_t id = specConstantResultId(words, 6);
        ASSERT_NE(id, 0u);
        bool indexed = false;
        for (const std::vector<uint32_t>& op : instructionsWith(words, 65))
        {
            for (std::size_t word = 4; word < op.size(); ++word)
                indexed = indexed || op[word] == id;
        }
        ASSERT_TRUE(indexed) << "no OpAccessChain indexed by the constant";
        const auto i = vsgUtil::readSpirvInterface(words);
        EXPECT_EQ(i.mSpecConstantOps, 0u);
        EXPECT_TRUE(has(i, "spec_constant id=6 i32")) << linesOf(i);
    }

    // Task 0072, member names (after the review of stage g, finding 9): the layout lines give each member of a block
    // by its offset and type only, so two members of one type in another order looked the same. The members are also
    // compared by name: one "member <block> <offset>:<path>" line each, the path made of the member names from the
    // block down (a struct's only member adds no name, arrays add "[]"). Written before the implementation.

    vsgUtil::SpirvInterface readFragment(const std::string& source)
    {
        const auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, source);
        EXPECT_FALSE(words.empty()) << "did not compile:\n" << source;
        if (words.empty())
            return {};
        return vsgUtil::readSpirvInterface(words);
    }

    vsgUtil::SpirvInterfaceDiff fragmentDiff(const std::string& first, const std::string& second)
    {
        return vsgUtil::compareSpirvInterfaces(readFragment(first), readFragment(second));
    }

    using Lines = std::vector<std::string>;

    TEST(SpirvInterface, two_members_of_one_type_in_another_order_are_a_difference)
    {
        // The review's red run, in small: the tonemap twin with `ao` and `sss` swapped in its push constants.
        const auto push = [](const std::string& members) {
            return fragmentWith("layout(push_constant) uniform Push { vec4 params; " + members + " } push;\n",
                "outColor = push.params + push.ao + push.sss;\n");
        };
        const auto pushDiff = fragmentDiff(push("vec4 ao; vec4 sss;"), push("vec4 sss; vec4 ao;"));
        EXPECT_EQ(pushDiff.mOnlyInFirst, (Lines{ "member push_constant 16:ao", "member push_constant 32:sss" }));
        EXPECT_EQ(pushDiff.mOnlyInSecond, (Lines{ "member push_constant 16:sss", "member push_constant 32:ao" }));

        // The same in a uniform block and in a storage block.
        const auto uniform = [](const std::string& members) {
            return fragmentWith("layout(std140, set = 0, binding = 3) uniform Block { " + members + " } block;\n",
                "outColor = block.a + block.b;\n");
        };
        const auto uniformDiff = fragmentDiff(uniform("vec4 a; vec4 b;"), uniform("vec4 b; vec4 a;"));
        EXPECT_EQ(uniformDiff.mOnlyInFirst, (Lines{ "member set=0 binding=3 0:a", "member set=0 binding=3 16:b" }));
        EXPECT_EQ(uniformDiff.mOnlyInSecond, (Lines{ "member set=0 binding=3 0:b", "member set=0 binding=3 16:a" }));

        const auto storage = [](const std::string& members) {
            return fragmentWith(
                "layout(std430, set = 1, binding = 2) readonly buffer Block { " + members + " } block;\n",
                "outColor = vec4(block.first + block.second);\n");
        };
        const auto storageDiff
            = fragmentDiff(storage("float first; float second;"), storage("float second; float first;"));
        EXPECT_EQ(
            storageDiff.mOnlyInFirst, (Lines{ "member set=1 binding=2 0:first", "member set=1 binding=2 4:second" }));
        EXPECT_EQ(
            storageDiff.mOnlyInSecond, (Lines{ "member set=1 binding=2 0:second", "member set=1 binding=2 4:first" }));
    }

    TEST(SpirvInterface, a_member_whose_name_alone_differs_is_a_difference)
    {
        const auto push = [](const std::string& last) {
            return fragmentWith(
                "layout(push_constant) uniform Push { vec4 params; vec4 ao; vec4 " + last + "; } push;\n",
                "outColor = push.params + push.ao + push." + last + ";\n");
        };
        const auto diff = fragmentDiff(push("sss"), push("subsurface"));
        EXPECT_EQ(diff.mOnlyInFirst, (Lines{ "member push_constant 32:sss" }));
        EXPECT_EQ(diff.mOnlyInSecond, (Lines{ "member push_constant 32:subsurface" }));
    }

    TEST(SpirvInterface, nested_struct_members_are_compared_by_their_full_path)
    {
        const auto source = [](const std::string& inner, const std::string& outer) {
            return fragmentWith("struct Inner { " + inner
                    + " };\n"
                      "layout(std140, set = 0, binding = 1) uniform Block { vec4 head; "
                    + outer + " } block;\n",
                "outColor = block.head + block.first.x + block.second.y;\n");
        };
        const std::string base = source("vec4 x; vec4 y;", "Inner first; Inner second;");
        const auto i = readFragment(base);
        for (const char* line :
            { "member set=0 binding=1 0:head", "member set=0 binding=1 16:first.x", "member set=0 binding=1 32:first.y",
                "member set=0 binding=1 48:second.x", "member set=0 binding=1 64:second.y" })
        {
            EXPECT_TRUE(has(i, line)) << line << "\n" << linesOf(i);
        }

        // The inner struct's members swapped: every path below it moves.
        const auto inner = fragmentDiff(base, source("vec4 y; vec4 x;", "Inner first; Inner second;"));
        EXPECT_EQ(inner.mOnlyInFirst,
            (Lines{ "member set=0 binding=1 16:first.x", "member set=0 binding=1 32:first.y",
                "member set=0 binding=1 48:second.x", "member set=0 binding=1 64:second.y" }));
        EXPECT_EQ(inner.mOnlyInSecond,
            (Lines{ "member set=0 binding=1 16:first.y", "member set=0 binding=1 32:first.x",
                "member set=0 binding=1 48:second.y", "member set=0 binding=1 64:second.x" }));

        // The two members of the inner struct's type swapped in the block.
        const auto outer = fragmentDiff(base, source("vec4 x; vec4 y;", "Inner second; Inner first;"));
        EXPECT_EQ(outer.mOnlyInFirst,
            (Lines{ "member set=0 binding=1 16:first.x", "member set=0 binding=1 32:first.y",
                "member set=0 binding=1 48:second.x", "member set=0 binding=1 64:second.y" }));
        EXPECT_EQ(outer.mOnlyInSecond,
            (Lines{ "member set=0 binding=1 16:second.x", "member set=0 binding=1 32:second.y",
                "member set=0 binding=1 48:first.x", "member set=0 binding=1 64:first.y" }));
    }

    TEST(SpirvInterface, array_elements_are_named_once_by_their_first_element)
    {
        const auto source = [](const std::string& light) {
            return fragmentWith("struct Light { " + light
                    + " };\n"
                      "layout(std140, set = 0, binding = 4) uniform Block { vec4 head; Light lights[4]; mat4 m[2]; "
                      "vec4 tail; } block;\n",
                "outColor = block.head + block.lights[1].position + block.m[1][0] + block.tail;\n");
        };
        const auto i = readFragment(source("vec4 position; vec4 color;"));
        for (const char* line : { "member set=0 binding=4 0:head", "member set=0 binding=4 16:lights[].position",
                 "member set=0 binding=4 32:lights[].color", "member set=0 binding=4 144:m[]",
                 "member set=0 binding=4 272:tail" })
        {
            EXPECT_TRUE(has(i, line)) << line << "\n" << linesOf(i);
        }
        const auto diff = fragmentDiff(source("vec4 position; vec4 color;"), source("vec4 color; vec4 position;"));
        EXPECT_EQ(diff.mOnlyInFirst,
            (Lines{ "member set=0 binding=4 16:lights[].position", "member set=0 binding=4 32:lights[].color" }));
        EXPECT_EQ(diff.mOnlyInSecond,
            (Lines{ "member set=0 binding=4 16:lights[].color", "member set=0 binding=4 32:lights[].position" }));
    }

    // The shapes slangc gives the GLSL's blocks (task 0072, the dumps of every built twin): `ConstantBuffer<Scene>`
    // puts Scene's members on the block where the GLSL has `uniform SceneData { Scene scene; }`; a
    // `StructuredBuffer<T>` is a block named StructuredBuffer whose only member, `__member0`, is the runtime array; a
    // std140 array of matrices is wrapped in a struct `_Array_std140_matrix<float,4,4>4` whose only member is `data`.
    // None of these names can be swapped with another member, so a struct's only member adds no name to the path.
    TEST(SpirvInterface, a_structs_only_member_adds_no_name)
    {
        // ConstantBuffer<Scene> against the GLSL's block around one struct.
        const std::string wrapped = fragmentWith(
            "struct Scene { vec4 a; vec4 b; };\n"
            "layout(std140, set = 0, binding = 0) uniform SceneData { Scene scene; };\n",
            "outColor = scene.a + scene.b;\n");
        const std::string flat
            = fragmentWith("layout(std140, set = 0, binding = 0) uniform Scene { vec4 a; vec4 b; } scene;\n",
                "outColor = scene.a + scene.b;\n");
        EXPECT_TRUE(fragmentDiff(wrapped, flat).same());
        const auto i = readFragment(wrapped);
        EXPECT_TRUE(has(i, "member set=0 binding=0 0:a")) << linesOf(i);
        EXPECT_TRUE(has(i, "member set=0 binding=0 16:b")) << linesOf(i);

        // A StructuredBuffer<PointLight> against the GLSL's buffer of one runtime array.
        const auto buffer = [](const std::string& declaration, const std::string& use) {
            return fragmentWith(
                "struct PointLight { vec4 colorIntensity; vec4 position; };\n"
                "layout(std430, set = 0, binding = 2) readonly buffer "
                    + declaration + ";\n",
                "outColor = " + use + "[0].colorIntensity + " + use + "[1].position;\n");
        };
        const std::string glslBuffer = buffer("Lights { PointLight pointLights[]; }", "pointLights");
        const std::string slangBuffer = buffer("StructuredBuffer { PointLight member0[]; } lights", "lights.member0");
        EXPECT_TRUE(fragmentDiff(glslBuffer, slangBuffer).same());
        const auto b = readFragment(glslBuffer);
        EXPECT_TRUE(has(b, "member set=0 binding=2 0:[].colorIntensity")) << linesOf(b);
        EXPECT_TRUE(has(b, "member set=0 binding=2 16:[].position")) << linesOf(b);

        // The std140 wrapper of an array of matrices.
        const std::string plainArray
            = fragmentWith("layout(std140, set = 0, binding = 5) uniform Block { vec4 a; mat4 m[2]; } block;\n",
                "outColor = block.a + block.m[1][0];\n");
        const std::string wrappedArray = fragmentWith(
            "struct Wrap { mat4 data[2]; };\n"
            "layout(std140, set = 0, binding = 5) uniform Block { vec4 a; Wrap m; } block;\n",
            "outColor = block.a + block.m.data[1][0];\n");
        EXPECT_TRUE(fragmentDiff(plainArray, wrappedArray).same());
        const auto m = readFragment(wrappedArray);
        EXPECT_TRUE(has(m, "member set=0 binding=5 16:m[]")) << linesOf(m);

        // So the name of a block's only member is never compared (nothing can be swapped with it).
        const auto single = [](const std::string& name) {
            return fragmentWith("layout(std430, set = 3, binding = 1) buffer Count { uint " + name + "; } count;\n",
                "outColor = vec4(float(count." + name + "));\n");
        };
        EXPECT_TRUE(fragmentDiff(single("indexCount"), single("total")).same());
        EXPECT_TRUE(has(readFragment(single("indexCount")), "member set=3 binding=1 0:"));
    }

    TEST(SpirvInterface, block_and_instance_names_are_not_compared)
    {
        // slangc names a block's struct "<type>_std140" or "StructuredBuffer"; the bindings are what a pipeline sees.
        const auto source = [](const std::string& block, const std::string& instance) {
            return fragmentWith(
                "layout(std140, set = 0, binding = 6) uniform " + block + " { vec4 a; vec4 b; } " + instance + ";\n",
                "outColor = " + instance + ".a + " + instance + ".b;\n");
        };
        EXPECT_TRUE(fragmentDiff(source("Data", "data"), source("Data_std140", "other")).same());
        EXPECT_TRUE(has(readFragment(source("Data", "data")), "member set=0 binding=6 16:b"));
    }

    TEST(SpirvInterface, the_members_of_a_block_only_one_side_declares_are_not_listed_again)
    {
        // Its layout line is the difference (and the interface exceptions name that line); its members are not.
        const std::string base = fragmentWith(
            "layout(std140, set = 0, binding = 1) uniform A { vec4 a; vec4 b; } blockA;\n", "outColor = blockA.a;\n");
        const std::string more = fragmentWith(
            "layout(std140, set = 0, binding = 1) uniform A { vec4 a; vec4 b; } blockA;\n"
            "layout(std140, set = 0, binding = 2) uniform B { vec4 c; vec4 d; } blockB;\n"
            "layout(push_constant) uniform Push { vec4 e; vec4 f; } push;\n",
            "outColor = blockA.a + blockB.c + push.e;\n");
        const auto diff = fragmentDiff(base, more);
        EXPECT_TRUE(diff.mOnlyInFirst.empty());
        EXPECT_EQ(diff.mOnlyInSecond,
            (Lines{
                "descriptor set=0 binding=2 uniform 0:vec4f32;16:vec4f32;", "push_constant 0:vec4f32;16:vec4f32;" }));
        const auto i = readFragment(more);
        EXPECT_TRUE(has(i, "member set=0 binding=2 16:d")) << linesOf(i);
        EXPECT_TRUE(has(i, "member push_constant 16:f")) << linesOf(i);
    }

    // Task 0072, review of the member-name round, item 2 (written before the implementation): the game reads only a
    // module's specialization constant names (the ids a twin declares), so it has a reader that does not work out the
    // interface lines, member lines included.
    const char* const sConstantsFragment
        = "#version 450\n"
          "layout(constant_id = 3) const bool useColor = false;\n"
          "layout(constant_id = 7) const int unused = 0;\n"
          "layout(set = 0, binding = 0) uniform Block { vec4 a; vec4 b; } block;\n"
          "layout(location = 0) out vec4 outColor;\n"
          "void main() { outColor = useColor ? block.a : block.b; }\n";

    TEST(SpirvInterface, the_spec_constant_names_alone_are_those_of_the_whole_reader)
    {
        const auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, sConstantsFragment);
        ASSERT_FALSE(words.empty());
        const auto names = vsgUtil::readSpirvSpecConstantNames(words);
        EXPECT_EQ(names, vsgUtil::readSpirvInterface(words).mSpecConstantNames);
        EXPECT_EQ(names, (std::map<uint32_t, std::string>{ { 3u, "useColor" }, { 7u, "unused" } }));
    }

    TEST(SpirvInterface, the_spec_constant_names_are_read_without_the_interface)
    {
        auto words = compile(VK_SHADER_STAGE_FRAGMENT_BIT, sConstantsFragment);
        ASSERT_GT(words.size(), 5u);
        // A uniform variable whose pointer type the module never declares: the interface cannot be described (the
        // whole reader throws), the names of the constants can.
        const uint32_t bound = words[3];
        words.push_back((4u << 16) | 59u); // OpVariable: result type, result id, storage class Uniform (2)
        words.push_back(bound);
        words.push_back(bound + 1);
        words.push_back(2u);
        words[3] = bound + 2;
        EXPECT_THROW(vsgUtil::readSpirvInterface(words), std::runtime_error);
        std::map<uint32_t, std::string> names;
        EXPECT_NO_THROW(names = vsgUtil::readSpirvSpecConstantNames(words));
        EXPECT_EQ(names, (std::map<uint32_t, std::string>{ { 3u, "useColor" }, { 7u, "unused" } }));
        // Words that are not SPIR-V are refused, as by the whole reader.
        EXPECT_THROW(vsgUtil::readSpirvSpecConstantNames({}), std::runtime_error);
    }
}

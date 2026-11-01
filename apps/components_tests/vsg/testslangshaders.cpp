// Task 0023, F1b: the sun shader written in Slang must have the same interface as the GLSL one, because the C++ side
// builds the pipeline layout by hand and the two stages are linked by location. This compares what the two SPIR-V
// modules declare (descriptor sets, bindings, uniform byte layouts, push constants, interface locations, built-ins and
// the entry point), not their text or their instructions.
#include <gtest/gtest.h>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    // SPIR-V opcodes and enumerants used below (SPIR-V specification, unified, sections 3.32 and 3.20 to 3.21).
    enum Op : uint32_t
    {
        OpEntryPoint = 15,
        OpTypeVoid = 19,
        OpTypeBool = 20,
        OpTypeInt = 21,
        OpTypeFloat = 22,
        OpTypeVector = 23,
        OpTypeMatrix = 24,
        OpTypeImage = 25,
        OpTypeSampler = 26,
        OpTypeSampledImage = 27,
        OpTypeArray = 28,
        OpTypeStruct = 30,
        OpTypePointer = 32,
        OpConstant = 43,
        OpVariable = 59,
        OpDecorate = 71,
        OpMemberDecorate = 72,
    };
    enum Decoration : uint32_t
    {
        DecBlock = 2,
        DecRowMajor = 4,
        DecColMajor = 5,
        DecArrayStride = 6,
        DecMatrixStride = 7,
        DecBuiltIn = 11,
        DecLocation = 30,
        DecBinding = 33,
        DecDescriptorSet = 34,
        DecOffset = 35,
    };
    enum StorageClass : uint32_t
    {
        ScUniformConstant = 0,
        ScInput = 1,
        ScUniform = 2,
        ScOutput = 3,
        ScPushConstant = 9,
    };

    struct Type
    {
        uint32_t op = 0;
        std::vector<uint32_t> operands;
    };

    class Module
    {
    public:
        explicit Module(const std::vector<uint32_t>& words)
        {
            if (words.size() < 5 || words[0] != 0x07230203)
                throw std::runtime_error("not SPIR-V");
            mVersion = words[1];
            for (std::size_t i = 5; i < words.size();)
            {
                const uint32_t opcode = words[i] & 0xFFFF;
                const uint32_t count = words[i] >> 16;
                if (count == 0 || i + count > words.size())
                    throw std::runtime_error("bad SPIR-V instruction");
                read(opcode, &words[i + 1], count - 1);
                i += count;
            }
        }

        uint32_t version() const { return mVersion; }

        // The entry point as "<execution model> <name>".
        std::string entry() const { return mEntry; }

        // One line per thing the module declares that a pipeline or the next stage depends on.
        std::set<std::string> interfaceLines() const
        {
            std::set<std::string> lines;
            lines.insert("entry " + mEntry);
            for (const auto& [id, var] : mVariables)
            {
                const uint32_t storage = var.second;
                const Type& pointer = mTypes.at(var.first);
                const uint32_t pointee = pointer.operands.at(1);
                switch (storage)
                {
                    case ScUniform:
                    case ScUniformConstant:
                    {
                        const auto set = decoration(id, DecDescriptorSet);
                        const auto binding = decoration(id, DecBinding);
                        if (!set || !binding)
                            break;
                        lines.insert("descriptor set=" + std::to_string(*set) + " binding=" + std::to_string(*binding)
                            + " " + (storage == ScUniform ? "uniform " + layout(pointee, 0) : describe(pointee)));
                        break;
                    }
                    case ScPushConstant:
                        lines.insert("push_constant " + layout(pointee, 0));
                        break;
                    case ScInput:
                    case ScOutput:
                    {
                        const char* direction = storage == ScInput ? "in" : "out";
                        if (const auto location = decoration(id, DecLocation))
                        {
                            // GLSL declares an interface block (a struct, one location per member here, all vec2), Slang
                            // separate variables; the locations and member types are what the next stage sees.
                            if (const auto it = mTypes.find(pointee); it != mTypes.end() && it->second.op == OpTypeStruct)
                            {
                                for (uint32_t m = 0; m < it->second.operands.size(); ++m)
                                    lines.insert(std::string(direction) + " location=" + std::to_string(*location + m)
                                        + " " + describe(it->second.operands[m]));
                            }
                            else
                                lines.insert(std::string(direction) + " location=" + std::to_string(*location) + " "
                                    + describe(pointee));
                        }
                        if (const auto builtIn = decoration(id, DecBuiltIn))
                            lines.insert(std::string(direction) + " builtin=" + std::to_string(*builtIn));
                        // Built-ins of an output block such as gl_PerVertex are decorated on its members.
                        if (const auto it = mTypes.find(pointee); it != mTypes.end() && it->second.op == OpTypeStruct)
                            for (uint32_t m = 0; m < it->second.operands.size(); ++m)
                                if (const auto builtIn = memberDecoration(pointee, m, DecBuiltIn))
                                    lines.insert(std::string(direction) + " builtin=" + std::to_string(*builtIn));
                        break;
                    }
                    default:
                        break;
                }
            }
            return lines;
        }

    private:
        void read(uint32_t opcode, const uint32_t* w, uint32_t n)
        {
            switch (opcode)
            {
                case OpEntryPoint:
                {
                    // execution model, id, name (nul-terminated, packed), interface ids
                    std::string name(reinterpret_cast<const char*>(w + 2));
                    mEntry = std::to_string(w[0]) + " " + name;
                    break;
                }
                case OpTypeVoid:
                case OpTypeBool:
                case OpTypeInt:
                case OpTypeFloat:
                case OpTypeVector:
                case OpTypeMatrix:
                case OpTypeImage:
                case OpTypeSampler:
                case OpTypeSampledImage:
                case OpTypeArray:
                case OpTypeStruct:
                case OpTypePointer:
                    mTypes[w[0]] = { opcode, std::vector<uint32_t>(w + 1, w + n) };
                    break;
                case OpConstant:
                    mConstants[w[1]] = w[2];
                    break;
                case OpVariable:
                    // result type, result id, storage class
                    mVariables[w[1]] = { w[0], w[2] };
                    break;
                case OpDecorate:
                    mDecorations[{ w[0], w[1] }] = n > 2 ? w[2] : 0;
                    break;
                case OpMemberDecorate:
                    mMemberDecorations[{ w[0], w[1], w[2] }] = n > 3 ? w[3] : 0;
                    break;
                default:
                    break;
            }
        }

        std::optional<uint32_t> decoration(uint32_t id, uint32_t dec) const
        {
            const auto it = mDecorations.find({ id, dec });
            if (it == mDecorations.end())
                return std::nullopt;
            return it->second;
        }
        std::optional<uint32_t> memberDecoration(uint32_t id, uint32_t member, uint32_t dec) const
        {
            const auto it = mMemberDecorations.find({ id, member, dec });
            if (it == mMemberDecorations.end())
                return std::nullopt;
            return it->second;
        }

        // A short name for a type that does not depend on ids or on how the compiler nested its structs.
        std::string describe(uint32_t id) const
        {
            const Type& t = mTypes.at(id);
            switch (t.op)
            {
                case OpTypeFloat:
                    return "f" + std::to_string(t.operands[0]);
                case OpTypeInt:
                    return (t.operands[1] ? "i" : "u") + std::to_string(t.operands[0]);
                case OpTypeVector:
                    return "vec" + std::to_string(t.operands[1]) + describe(t.operands[0]);
                case OpTypeMatrix:
                    return "mat" + std::to_string(t.operands[1]) + "x" + describe(t.operands[0]);
                case OpTypeSampledImage:
                    return "sampled_image(" + describe(t.operands[0]) + ")";
                case OpTypeImage:
                    // sampled type, dim, depth, arrayed, multisampled, sampled, format
                    return "image(" + describe(t.operands[0]) + ",dim" + std::to_string(t.operands[1]) + ",arrayed"
                        + std::to_string(t.operands[3]) + ",ms" + std::to_string(t.operands[4]) + ")";
                case OpTypeArray:
                    return "array" + std::to_string(mConstants.at(t.operands[1])) + "(" + describe(t.operands[0]) + ")";
                case OpTypeStruct:
                    return "struct";
                default:
                    return "type" + std::to_string(t.op);
            }
        }

        // Every scalar, vector or matrix inside a block with its byte offset, so that two compilers that nest the same
        // members differently (a struct inside a block, or the members directly in it) give the same text.
        std::string layout(uint32_t id, uint32_t baseOffset) const
        {
            std::vector<std::string> leaves;
            collect(id, baseOffset, leaves);
            std::string out;
            for (const std::string& leaf : leaves)
                out += leaf + ";";
            return out;
        }
        void collect(uint32_t id, uint32_t base, std::vector<std::string>& leaves) const
        {
            const Type& t = mTypes.at(id);
            if (t.op == OpTypeStruct)
            {
                for (uint32_t m = 0; m < t.operands.size(); ++m)
                {
                    const auto offset = memberDecoration(id, m, DecOffset);
                    collect(t.operands[m], base + offset.value_or(0), leaves);
                    // The matrix majority and stride are on the member that holds the matrix.
                    if (mTypes.at(t.operands[m]).op == OpTypeMatrix || isMatrixArray(t.operands[m]))
                    {
                        const bool colMajor = memberDecoration(id, m, DecColMajor).has_value();
                        const auto stride = memberDecoration(id, m, DecMatrixStride);
                        // Slang writes its float4x4 as the transposed SPIR-V matrix with RowMajor, which has the same bytes
                        // as GLSL's mat4 with ColMajor; the offsets and stride below are the bytes, so the majority
                        // decoration is left out. That the maths agree is checked by the frame comparison.
                        (void)colMajor;
                        leaves.back() += " stride" + std::to_string(stride.value_or(0));
                    }
                }
            }
            else if (t.op == OpTypeArray)
            {
                const auto stride = decoration(id, DecArrayStride).value_or(0);
                for (uint32_t i = 0; i < mConstants.at(t.operands[1]); ++i)
                {
                    collect(t.operands[0], base + i * stride, leaves);
                }
            }
            else
                leaves.push_back(std::to_string(base) + ":" + describe(id));
        }
        bool isMatrixArray(uint32_t id) const
        {
            const Type& t = mTypes.at(id);
            return t.op == OpTypeArray && mTypes.at(t.operands[0]).op == OpTypeMatrix;
        }

        uint32_t mVersion = 0;
        std::string mEntry;
        std::map<uint32_t, Type> mTypes;
        std::map<uint32_t, uint32_t> mConstants;
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> mVariables; // id -> (pointer type, storage class)
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mDecorations;
        std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> mMemberDecorations;
    };

    std::vector<uint32_t> readSpirv(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {};
        const std::streamsize size = in.tellg();
        std::vector<uint32_t> words(static_cast<std::size_t>(size) / sizeof(uint32_t));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(words.data()), size);
        return words;
    }

    std::vector<uint32_t> compileGlsl(const std::string& file)
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_SOURCE_DIR };
        options->readerWriters = { vsg::glsl::create() };
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, options);
        if (!stage)
            return {};
        auto hints = vsg::ShaderCompileSettings::create();
        hints->defines = { "MATERIAL", "DIFFUSE_MAP" }; // the modes weather.cpp gives the sun
        stage->module->hints = hints;
        vsg::ShaderStages stages{ stage };
        if (!vsg::ShaderCompiler::create()->compile(stages))
            return {};
        return stage->module->code;
    }

    class SlangSunTest : public ::testing::TestWithParam<std::string>
    {
    };

    TEST_P(SlangSunTest, has_the_same_interface_as_the_glsl_stage)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc (OPENEDEN_BUILD_SLANG_SHADERS is off or there is no archive)";
#else
        const std::string name = GetParam(); // "vert" or "frag"
        const auto slangWords = readSpirv(std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / "sky/sun" / ("main." + name + ".spv"));
        ASSERT_FALSE(slangWords.empty()) << "main." << name << ".spv was not built";
        const auto glslWords = compileGlsl("sky/sun/main." + name);
        ASSERT_FALSE(glslWords.empty()) << "the GLSL stage did not compile";

        const Module slang(slangWords);
        const Module glsl(glslWords);

        // The SPIR-V version the Vulkan 1.0 instance can take.
        EXPECT_EQ(slang.version(), 0x00010000u);
        EXPECT_EQ(glsl.version(), 0x00010000u);
        // The entry point name VSG gives to every stage.
        EXPECT_EQ(slang.entry(), glsl.entry());

        const auto a = slang.interfaceLines();
        const auto b = glsl.interfaceLines();
        for (const std::string& line : b)
            EXPECT_TRUE(a.count(line)) << "GLSL declares, Slang does not: " << line;
        for (const std::string& line : a)
            EXPECT_TRUE(b.count(line)) << "Slang declares, GLSL does not: " << line;
        EXPECT_GT(a.size(), 3u);
#endif
    }

    INSTANTIATE_TEST_SUITE_P(Stages, SlangSunTest, ::testing::Values("vert", "frag"));
}

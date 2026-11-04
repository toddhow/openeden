#include "spirvinterface.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>

namespace vsgUtil
{
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
            OpTypeRuntimeArray = 29,
            OpTypeStruct = 30,
            OpTypePointer = 32,
            OpConstant = 43,
            OpSpecConstantTrue = 48,
            OpSpecConstantFalse = 49,
            OpSpecConstant = 50,
            OpVariable = 59,
            OpDecorate = 71,
            OpMemberDecorate = 72,
        };
        enum Decoration : uint32_t
        {
            DecSpecId = 1,
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
            ScStorageBuffer = 12,
        };

        constexpr std::size_t maxWords = 64u * 1024u * 1024u;
        // Arrays deeper than this, or larger, are described by their element and count instead of being expanded.
        constexpr uint32_t maxExpandedArray = 4096;
        constexpr int maxTypeDepth = 32;

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
                if (words.size() > maxWords)
                    throw std::runtime_error("SPIR-V module too large");
                mVersion = words[1];
                for (std::size_t i = 5; i < words.size();)
                {
                    const uint32_t opcode = words[i] & 0xFFFF;
                    const uint32_t count = words[i] >> 16;
                    if (count == 0 || count > words.size() - i)
                        throw std::runtime_error("bad SPIR-V instruction at word " + std::to_string(i));
                    read(opcode, &words[i + 1], count - 1);
                    i += count;
                }
            }

            uint32_t version() const { return mVersion; }
            const std::string& entry() const { return mEntry; }

            std::set<std::string> interfaceLines() const
            {
                std::set<std::string> lines;
                lines.insert("entry " + mEntry);
                for (const auto& [id, var] : mVariables)
                {
                    const uint32_t storage = var.second;
                    const Type& pointer = type(var.first);
                    if (pointer.op != OpTypePointer || pointer.operands.size() < 2)
                        throw std::runtime_error("variable " + std::to_string(id) + " is not a pointer");
                    const uint32_t pointee = pointer.operands[1];
                    switch (storage)
                    {
                        case ScUniform:
                        case ScUniformConstant:
                        case ScStorageBuffer:
                        {
                            const auto set = decoration(id, DecDescriptorSet);
                            const auto binding = decoration(id, DecBinding);
                            if (!set || !binding)
                                break;
                            const bool block = storage != ScUniformConstant;
                            lines.insert("descriptor set=" + std::to_string(*set)
                                + " binding=" + std::to_string(*binding) + " "
                                + (block ? std::string(storage == ScUniform ? "uniform " : "storage ")
                                            + layout(pointee, 0)
                                         : describe(pointee, 0)));
                            break;
                        }
                        case ScPushConstant:
                            lines.insert("push_constant " + layout(pointee, 0));
                            break;
                        case ScInput:
                        case ScOutput:
                        {
                            const char* direction = storage == ScInput ? "in" : "out";
                            const Type& t = type(pointee);
                            if (const auto location = decoration(id, DecLocation))
                            {
                                // GLSL may declare an interface block (one location per member), Slang separate
                                // variables; the locations and member types are what the next stage sees.
                                if (t.op == OpTypeStruct)
                                {
                                    for (uint32_t m = 0; m < t.operands.size(); ++m)
                                        lines.insert(std::string(direction) + " location="
                                            + std::to_string(*location + m) + " " + describe(t.operands[m], 0));
                                }
                                else
                                    lines.insert(std::string(direction) + " location=" + std::to_string(*location) + " "
                                        + describe(pointee, 0));
                            }
                            if (const auto builtIn = decoration(id, DecBuiltIn))
                                lines.insert(std::string(direction) + " builtin=" + std::to_string(*builtIn));
                            // Built-ins of a block such as gl_PerVertex are decorated on its members.
                            if (t.op == OpTypeStruct)
                                for (uint32_t m = 0; m < t.operands.size(); ++m)
                                    if (const auto builtIn = memberDecoration(pointee, m, DecBuiltIn))
                                        lines.insert(std::string(direction) + " builtin=" + std::to_string(*builtIn));
                            break;
                        }
                        default:
                            break;
                    }
                }
                for (const auto& [id, typeId] : mSpecConstants)
                    if (const auto specId = decoration(id, DecSpecId))
                        lines.insert("spec_constant id=" + std::to_string(*specId) + " " + describe(typeId, 0));
                return lines;
            }

        private:
            void read(uint32_t opcode, const uint32_t* w, uint32_t n)
            {
                const auto need = [&](uint32_t words) {
                    if (n < words)
                        throw std::runtime_error("SPIR-V instruction " + std::to_string(opcode) + " too short");
                };
                switch (opcode)
                {
                    case OpEntryPoint:
                    {
                        // execution model, id, name (nul-terminated, packed), interface ids
                        need(3);
                        std::string name;
                        const char* bytes = reinterpret_cast<const char*>(w + 2);
                        const std::size_t maxBytes = static_cast<std::size_t>(n - 2) * sizeof(uint32_t);
                        for (std::size_t i = 0; i < maxBytes && bytes[i] != '\0'; ++i)
                            name += bytes[i];
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
                    case OpTypeRuntimeArray:
                    case OpTypeStruct:
                    case OpTypePointer:
                        need(1);
                        mTypes[w[0]] = { opcode, std::vector<uint32_t>(w + 1, w + n) };
                        break;
                    case OpConstant:
                        need(3);
                        mConstants[w[1]] = w[2];
                        break;
                    case OpSpecConstantTrue:
                    case OpSpecConstantFalse:
                    case OpSpecConstant:
                        need(2);
                        mSpecConstants[w[1]] = w[0];
                        if (opcode == OpSpecConstant && n >= 3)
                            mConstants[w[1]] = w[2];
                        break;
                    case OpVariable:
                        // result type, result id, storage class
                        need(3);
                        mVariables[w[1]] = { w[0], w[2] };
                        break;
                    case OpDecorate:
                        need(2);
                        mDecorations[{ w[0], w[1] }] = n > 2 ? w[2] : 0;
                        break;
                    case OpMemberDecorate:
                        need(3);
                        mMemberDecorations[{ w[0], w[1], w[2] }] = n > 3 ? w[3] : 0;
                        break;
                    default:
                        break;
                }
            }

            const Type& type(uint32_t id) const
            {
                const auto it = mTypes.find(id);
                if (it == mTypes.end())
                    throw std::runtime_error("SPIR-V type " + std::to_string(id) + " is not declared");
                return it->second;
            }
            uint32_t operand(const Type& t, std::size_t i) const
            {
                if (i >= t.operands.size())
                    throw std::runtime_error("SPIR-V type with too few operands");
                return t.operands[i];
            }
            uint32_t constant(uint32_t id) const
            {
                const auto it = mConstants.find(id);
                if (it == mConstants.end())
                    throw std::runtime_error("SPIR-V array length " + std::to_string(id) + " is not a constant");
                return it->second;
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
            std::string describe(uint32_t id, int depth) const
            {
                if (depth > maxTypeDepth)
                    throw std::runtime_error("SPIR-V type nesting too deep");
                const Type& t = type(id);
                switch (t.op)
                {
                    case OpTypeBool:
                        return "bool";
                    case OpTypeFloat:
                        return "f" + std::to_string(operand(t, 0));
                    case OpTypeInt:
                        return (operand(t, 1) ? "i" : "u") + std::to_string(operand(t, 0));
                    case OpTypeVector:
                        return "vec" + std::to_string(operand(t, 1)) + describe(operand(t, 0), depth + 1);
                    case OpTypeMatrix:
                        return "mat" + std::to_string(operand(t, 1)) + "x" + describe(operand(t, 0), depth + 1);
                    case OpTypeSampler:
                        return "sampler";
                    case OpTypeSampledImage:
                        return "sampled_image(" + describe(operand(t, 0), depth + 1) + ")";
                    case OpTypeImage:
                        // sampled type, dim, depth, arrayed, multisampled, sampled, format
                        return "image(" + describe(operand(t, 0), depth + 1) + ",dim" + std::to_string(operand(t, 1))
                            + ",arrayed" + std::to_string(operand(t, 3)) + ",ms" + std::to_string(operand(t, 4)) + ")";
                    case OpTypeArray:
                        return "array" + std::to_string(constant(operand(t, 1))) + "("
                            + describe(operand(t, 0), depth + 1) + ")";
                    case OpTypeRuntimeArray:
                        return "runtime_array(" + describe(operand(t, 0), depth + 1) + ")";
                    case OpTypeStruct:
                        return "struct";
                    default:
                        return "type" + std::to_string(t.op);
                }
            }

            // Every scalar, vector or matrix inside a block with its byte offset, so that two compilers that nest the
            // same members differently give the same text.
            std::string layout(uint32_t id, uint32_t baseOffset) const
            {
                std::vector<std::string> leaves;
                collect(id, baseOffset, leaves, 0);
                std::string out;
                for (const std::string& leaf : leaves)
                    out += leaf + ";";
                return out;
            }
            void collect(uint32_t id, uint32_t base, std::vector<std::string>& leaves, int depth) const
            {
                if (depth > maxTypeDepth)
                    throw std::runtime_error("SPIR-V type nesting too deep");
                const Type& t = type(id);
                if (t.op == OpTypeStruct)
                {
                    for (uint32_t m = 0; m < t.operands.size(); ++m)
                    {
                        const auto offset = memberDecoration(id, m, DecOffset);
                        collect(t.operands[m], base + offset.value_or(0), leaves, depth + 1);
                        // The matrix stride is on the member that holds the matrix. Slang writes float4x4 as the
                        // transposed SPIR-V matrix with RowMajor, which has the same bytes as GLSL's mat4 with
                        // ColMajor, so the majority is left out; the offsets and the stride are the bytes.
                        if ((type(t.operands[m]).op == OpTypeMatrix || isMatrixArray(t.operands[m])) && !leaves.empty())
                        {
                            const auto stride = memberDecoration(id, m, DecMatrixStride);
                            leaves.back() += " stride" + std::to_string(stride.value_or(0));
                        }
                    }
                }
                else if (t.op == OpTypeArray)
                {
                    const uint32_t count = constant(operand(t, 1));
                    const auto stride = decoration(id, DecArrayStride).value_or(0);
                    if (count > maxExpandedArray)
                    {
                        leaves.push_back(
                            std::to_string(base) + ":" + describe(id, depth) + " stride" + std::to_string(stride));
                        return;
                    }
                    for (uint32_t i = 0; i < count; ++i)
                        collect(operand(t, 0), base + i * stride, leaves, depth + 1);
                }
                else if (t.op == OpTypeRuntimeArray)
                {
                    const auto stride = decoration(id, DecArrayStride).value_or(0);
                    leaves.push_back(std::to_string(base) + ":runtime_array stride" + std::to_string(stride));
                    collect(operand(t, 0), 0, leaves, depth + 1);
                }
                else
                    leaves.push_back(std::to_string(base) + ":" + describe(id, depth));
            }
            bool isMatrixArray(uint32_t id) const
            {
                const Type& t = type(id);
                return t.op == OpTypeArray && type(operand(t, 0)).op == OpTypeMatrix;
            }

            uint32_t mVersion = 0;
            std::string mEntry;
            std::map<uint32_t, Type> mTypes;
            std::map<uint32_t, uint32_t> mConstants;
            std::map<uint32_t, uint32_t> mSpecConstants; // id -> type
            std::map<uint32_t, std::pair<uint32_t, uint32_t>> mVariables; // id -> (pointer type, storage class)
            std::map<std::pair<uint32_t, uint32_t>, uint32_t> mDecorations;
            std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> mMemberDecorations;
        };
    }

    SpirvInterface readSpirvInterface(const std::vector<uint32_t>& words)
    {
        const Module module(words);
        SpirvInterface out;
        out.mVersion = module.version();
        out.mEntry = module.entry();
        out.mLines = module.interfaceLines();
        return out;
    }

    SpirvInterfaceDiff compareSpirvInterfaces(const SpirvInterface& first, const SpirvInterface& second)
    {
        SpirvInterfaceDiff diff;
        std::set_difference(first.mLines.begin(), first.mLines.end(), second.mLines.begin(), second.mLines.end(),
            std::back_inserter(diff.mOnlyInFirst));
        std::set_difference(second.mLines.begin(), second.mLines.end(), first.mLines.begin(), first.mLines.end(),
            std::back_inserter(diff.mOnlyInSecond));
        return diff;
    }
}

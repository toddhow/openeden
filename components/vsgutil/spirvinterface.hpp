#ifndef OPENEDEN_VSGUTIL_SPIRVINTERFACE_H
#define OPENEDEN_VSGUTIL_SPIRVINTERFACE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vsgUtil
{
    /*
     * OpenEden (task 0072): what a SPIR-V module declares that a pipeline or the next stage depends on, as text that
     * does not depend on ids or on how a compiler nested its structs. A Slang port must declare exactly what the GLSL
     * stage declares, because the C++ side builds the pipeline layouts by hand and stages are linked by location.
     * Taken from the sun test of task 0023 (apps/components_tests/vsg/testslangshaders.cpp) and extended with
     * specialization constants.
     *
     * Lines:
     *     entry <execution model> <name>
     *     descriptor set=<s> binding=<b> uniform <offset>:<type>;...     (uniform and storage blocks: every leaf)
     *     descriptor set=<s> binding=<b> <type>                          (images, samplers)
     *     push_constant <offset>:<type>;...
     *     in|out location=<l> <type>      in|out builtin=<n>
     *     spec_constant id=<n> <type>
     */
    struct SpirvInterface
    {
        uint32_t mVersion = 0;
        std::string mEntry;
        std::set<std::string> mLines;
        // Task 0072 (review of stages b and c): each specialization constant's SpecId with its OpName (empty when the
        // module has none), and how many OpSpecConstantOp instructions the module has: an expression over
        // specialization constants, the pattern apps/openmw/mwrender/CLAUDE.md records as miscompiled by the NVIDIA
        // driver. Neither is part of mLines (the GLSL folds its settings into plain constants).
        std::map<uint32_t, std::string> mSpecConstantNames;
        std::size_t mSpecConstantOps = 0;
    };

    /// Reads the declarations of a SPIR-V module. Throws std::runtime_error for words that are not SPIR-V or an
    /// instruction that runs past the end. At most 64 Mi words.
    SpirvInterface readSpirvInterface(const std::vector<uint32_t>& words);

    struct SpirvInterfaceDiff
    {
        std::vector<std::string> mOnlyInFirst;
        std::vector<std::string> mOnlyInSecond;
        bool same() const { return mOnlyInFirst.empty() && mOnlyInSecond.empty(); }
    };

    /// The lines one interface declares and the other does not, in byte order.
    SpirvInterfaceDiff compareSpirvInterfaces(const SpirvInterface& first, const SpirvInterface& second);
}

#endif

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
     *     member set=<s> binding=<b> <offset>:<path>    member push_constant <offset>:<path>
     *
     * Task 0072 (member names, after the review of stage g, finding 9): a "member" line for every leaf of a uniform,
     * storage or push constant block, so that two members of one type in another order differ (the block's own line
     * gives each leaf by offset and type only). The path is the member names (OpMemberName) from the block down,
     * joined by '.'; an array adds "[]" and is named by its first element; a struct with one member adds no name, so
     * the GLSL's `uniform SceneData { Scene scene; }` and Slang's `ConstantBuffer<Scene>`, `StructuredBuffer<T>` (whose
     * only member is `__member0`) and std140 array wrapper (`data`) read alike. Block and variable names are not read.
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
    ///
    /// Task 0072, stage f: each specialization constant of `pinnedSpecConstants` (SpecId -> value) is read as a plain
    /// constant with that value, as a pipeline that sets it would see the module: arrays it sizes take that length,
    /// and it is no longer listed (no spec_constant lines, not in mSpecConstantNames). The object shader's twins are
    /// built for one count of texture coordinate sets, which its GLSL takes as the specialization constant 0. A
    /// specialization constant that no instruction reads (glslang keeps one, slangc drops it, and a pipeline's value
    /// for it changes nothing) has no spec_constant lines either; it stays in mSpecConstantNames.
    SpirvInterface readSpirvInterface(
        const std::vector<uint32_t>& words, const std::map<uint32_t, uint32_t>& pinnedSpecConstants = {});

    /// Task 0072 (review of the member-name round, item 2): a module's specialization constants by SpecId with their
    /// OpName, as readSpirvInterface gives them in mSpecConstantNames, without working out the interface lines. The
    /// game needs only these (findSlangPermutation and addSettingsConstants, shaderpermutation.cpp). Throws
    /// std::runtime_error for words that are not SPIR-V or an instruction that runs past the end.
    std::map<uint32_t, std::string> readSpirvSpecConstantNames(const std::vector<uint32_t>& words);

    /// Review of stage h (round 4), finding 2: the two facts a cache hit is checked for, read without working out any
    /// interface line: the module's version word and its OpSpecConstantOp count. Throws std::runtime_error as
    /// readSpirvInterface does for words that are not SPIR-V or an instruction that runs past the end.
    struct SpirvStructure
    {
        uint32_t mVersion = 0;
        std::size_t mSpecConstantOps = 0;
    };
    SpirvStructure readSpirvStructure(const std::vector<uint32_t>& words);

    struct SpirvInterfaceDiff
    {
        std::vector<std::string> mOnlyInFirst;
        std::vector<std::string> mOnlyInSecond;
        bool same() const { return mOnlyInFirst.empty() && mOnlyInSecond.empty(); }
    };

    /// The lines one interface declares and the other does not, in byte order. The member lines of a block that only
    /// one of them declares are left out: the block's own line is that difference.
    SpirvInterfaceDiff compareSpirvInterfaces(const SpirvInterface& first, const SpirvInterface& second);
}

#endif

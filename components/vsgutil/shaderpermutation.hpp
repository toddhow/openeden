#ifndef OPENEDEN_VSGUTIL_SHADERPERMUTATION_H
#define OPENEDEN_VSGUTIL_SHADERPERMUTATION_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <vsg/io/Options.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>

namespace vsgUtil
{
    /*
     * OpenEden (task 0072): Slang twins of the GLSL shaders, one precompiled SPIR-V file per set of defines.
     *
     * The GLSL shaders are compiled at run time with a set of defines (Pipeline::Graphics adds one per Mode, the
     * terrain its own). A Slang port is compiled at build time once for each define set listed in the family's
     * permutations file, and the build writes "<stage>.permutations" beside the files, one line per file:
     *
     *     main.frag.p0003.spv<TAB>DIFFUSE_MAP MATERIAL
     *
     * The define list is canonical (see canonicalDefines); an empty list is the stage compiled with no defines. With
     * `[Rendering] slang shaders` on, a stage whose exact define set is listed uses that file; any other set keeps the
     * GLSL, so a missing permutation can never change what is drawn, only which compiler produced it.
     */

    /// The canonical text of a define set: the names in byte order, separated by one space. Throws
    /// std::invalid_argument for a name that is not an identifier ([A-Za-z_][A-Za-z0-9_]*, at most 64 bytes).
    std::string canonicalDefines(const std::set<std::string>& defines);

    /// The files of one stage's permutations: canonical define list -> file name (no directory).
    struct PermutationManifest
    {
        std::map<std::string, std::string> mFiles;
    };

    /// Reads a ".permutations" text. Strict: every line is "<file>\t<canonical defines>" (Unix or Windows line ends),
    /// the file name is "<stem>.<vert|frag|comp>[.p<4 digits>].spv" (stem: lower-case letters and digits), the
    /// define list is already canonical, and no define list or file appears twice. Throws std::runtime_error naming the
    /// line.
    /// At most 4096 lines and 64 KiB of text.
    PermutationManifest parsePermutationManifest(std::string_view text);

    /// The file compiled with exactly `defines`, or nothing.
    std::optional<std::string> selectPermutation(
        const PermutationManifest& manifest, const std::set<std::string>& defines);

    /// The name of the bool value on the shader vsg::Options that holds `[Rendering] slang shaders`.
    inline constexpr const char* preferSlangKey = "openeden.preferSlang";

    /// True when the options (or none) say Slang twins are preferred.
    bool prefersSlang(const vsg::Options* options);

    /// The precompiled Slang module for the stage file `path` (for example "sky/moon/main.frag") compiled with exactly
    /// `defines`, read through the options' paths from "<path>.permutations"; null when there is none or the
    /// manifest cannot be read (logged once per path). Thread safe; manifests are read once per path.
    vsg::ref_ptr<vsg::ShaderModule> findSlangPermutation(
        const std::string& path, const std::set<std::string>& defines, const vsg::ref_ptr<const vsg::Options>& options);

    /// Formats one trace line: "<path>\t<canonical defines>".
    std::string traceLine(std::string_view path, const std::set<std::string>& defines);

    /// When the environment variable OPENEDEN_SHADER_TRACE names a file, appends one trace line the first time a
    /// (path, defines) pair is seen in this process. With OPENEDEN_SHADER_TRACE_TIMING=1 it also compiles a copy of
    /// the GLSL `stage` with VSG's glslang and appends "\t<milliseconds>". Does nothing otherwise. Thread safe.
    void recordShaderUse(std::string_view path, const std::set<std::string>& defines, const vsg::ShaderStage* stage,
        const vsg::ref_ptr<const vsg::Options>& options);
}

#endif

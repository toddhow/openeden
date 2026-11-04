#ifndef OPENEDEN_VSGUTIL_SHADERPERMUTATION_H
#define OPENEDEN_VSGUTIL_SHADERPERMUTATION_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <vsg/core/Inherit.h>
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
     *     main.frag.p3b895119.spv<TAB>DIFFUSE_MAP MATERIAL
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
    /// the file name is "<stem>.<vert|frag|comp>[.p<4 digits or 8 lower-case hex digits>].spv" (stem: lower-case
    /// letters and digits; the build writes the first 8 hex digits of the SHA-1 of the define list), the define list is
    /// already canonical, and no define list or file appears twice. Throws std::runtime_error naming the line. At most
    /// 4096 lines and 64 KiB of text.
    PermutationManifest parsePermutationManifest(std::string_view text);

    /// The file compiled with exactly `defines`, or nothing.
    std::optional<std::string> selectPermutation(
        const PermutationManifest& manifest, const std::set<std::string>& defines);

    /// The names listed by the "#pragma import_defines (...)" lines of a GLSL source (with its includes inserted, as
    /// vsg's glsl reader returns it), read as vsg::ShaderCompiler::combineSourceAndDefines reads them: a line that
    /// starts with the pragma once leading spaces and tabs are skipped; the text between the first '(' and the next
    /// ')'; names split at ',' with spaces and tabs trimmed. Empty names are left out.
    std::set<std::string> importedDefines(std::string_view source);

    /// The defines the GLSL compiler really applies to `source`: the requested names it imports. VSG's ShaderCompiler
    /// writes a "#define" only for an imported name, so two requests that differ only in names the stage does not
    /// import compile to the same code. Slang twins are listed, selected and traced by this set (task 0072, stage b).
    std::set<std::string> effectiveDefines(const std::set<std::string>& requested, std::string_view source);

    /*
     * The values of the GLSL `*.settings` includes (Resource::ShaderSettings) for the Slang twins. The GLSL has them
     * as constants written into the source at run time; a twin compiled at build time cannot, so it declares them as
     * specialization constants with fixed ids (Resource::settingsConstantTable) and the pipeline code gives the values.
     * The object lives on the shader vsg::Options under settingsConstantsKey.
     */
    class SettingsConstants : public vsg::Inherit<vsg::Object, SettingsConstants>
    {
    public:
        vsg::ShaderStage::SpecializationConstants constants;
    };

    inline constexpr const char* settingsConstantsKey = "openeden.settingsConstants";

    /// When `stage`'s module is a precompiled Slang twin (it has SPIR-V and no GLSL source), adds every settings
    /// constant of `options` that the stage does not set already. A GLSL stage, or options without the object, are
    /// left as they are. Ids the module does not declare are ignored by Vulkan.
    void addSettingsConstants(vsg::ShaderStage& stage, const vsg::Options* options);

    /// The name of the bool value on the shader vsg::Options that holds `[Rendering] slang shaders`.
    inline constexpr const char* preferSlangKey = "openeden.preferSlang";

    /// True when the options (or none) say Slang twins are preferred.
    bool prefersSlang(const vsg::Options* options);

    /// The precompiled Slang module for the stage file `path` (for example "sky/moon/main.frag") compiled with exactly
    /// `defines`, read through the options' paths from "<path>.permutations"; null when there is none or the
    /// manifest cannot be read (logged once per path). Thread safe; manifests are read once per path and search paths,
    /// and each (path, set) is looked up and logged once: an unlisted set, or a listed file that cannot be read, is not
    /// tried again.
    vsg::ref_ptr<vsg::ShaderModule> findSlangPermutation(
        const std::string& path, const std::set<std::string>& defines, const vsg::ref_ptr<const vsg::Options>& options);

    /// The Slang twin to draw the GLSL stage `path` with: none when `preferSlang` is false; else the permutation built
    /// for effectiveDefines(requested, glslSource), or none, and then the GLSL is drawn. Never the single "<path>.spv"
    /// file of task 0023, which was built for one define set only (task 0072, stage b).
    vsg::ref_ptr<vsg::ShaderModule> slangTwin(const std::string& path, const std::set<std::string>& requested,
        std::string_view glslSource, bool preferSlang, const vsg::ref_ptr<const vsg::Options>& options);

    /// For a stage read with vsgUtil::readShader and compiled with no defines (the compute passes, task 0072 stage
    /// c): records its use (recordShaderUse) and, when the options prefer Slang and a twin is built for the stage's
    /// effective set, returns a copy of `stage` with the twin's module and the settings constants. Otherwise `stage`
    /// itself. `stage` is never changed: readShader may share it.
    vsg::ref_ptr<vsg::ShaderStage> withSlangTwin(const vsg::ref_ptr<vsg::ShaderStage>& stage, const std::string& path,
        const vsg::ref_ptr<const vsg::Options>& options);

    /// Formats one trace line: "<path>\t<canonical defines>".
    std::string traceLine(std::string_view path, const std::set<std::string>& defines);

    /// When the environment variable OPENEDEN_SHADER_TRACE names a file, appends one trace line the first time a
    /// (path, defines) pair is seen in this process; the defines written are effectiveDefines(defines, the stage's
    /// GLSL source) when the stage has a module. With OPENEDEN_SHADER_TRACE_TIMING=1 it also compiles a copy of
    /// the GLSL `stage` with VSG's glslang and appends "\t<milliseconds>". Does nothing otherwise. Thread safe.
    void recordShaderUse(std::string_view path, const std::set<std::string>& defines, const vsg::ShaderStage* stage,
        const vsg::ref_ptr<const vsg::Options>& options);
}

#endif

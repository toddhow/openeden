#ifndef OPENEDEN_VSGUTIL_SHADERPERMUTATION_H
#define OPENEDEN_VSGUTIL_SHADERPERMUTATION_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <vsg/core/Inherit.h>
#include <vsg/io/Options.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>

#include "slangruntime.hpp"

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

    /// The defines a stage really takes: the requested names it may take (Task 0363, stage g: `imports`, the names its
    /// ".slang-stage" file lists, which its GLSL once imported with "#pragma import_defines"). Two requests that differ
    /// only in names the stage does not take choose the same twin. Slang twins are listed, selected and traced by
    /// this set (task 0072, stage b).
    std::set<std::string> effectiveDefines(
        const std::set<std::string>& requested, const std::set<std::string>& imports);

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
        // Task 0072 (review of stages b and c): the names the settings files write as "#define <name> 1" lines
        // (shadow.settings, terrain.settings) in this run. A stage that includes such a file compiles with them, so
        // they are part of the set its Slang twin is chosen by (stageDefines).
        std::set<std::string> defines;
        // Task 0363 (stage b): `[Rendering] shader constants`: a stage in constantDefinesFor's table takes its settings
        // defines as the constants above, so its Slang twin is the one permutation with SHADOW_CONSTANTS.
        bool constantDefines = false;
    };

    inline constexpr const char* settingsConstantsKey = "openeden.settingsConstants";

    /// When `stage`'s module is a precompiled Slang twin (it has SPIR-V and no GLSL source), adds the settings
    /// constants of `options` that the module declares (by SpecId) and the stage does not set already. A GLSL stage,
    /// a module that declares none of the ids (a Slang-only shader such as the procedural sun), a module that cannot
    /// be read, or options without the object are left exactly as they are (task 0072, review of stages b and c).
    void addSettingsConstants(vsg::ShaderStage& stage, const vsg::Options* options);

    /// The names a settings file defined with a "#define <name>" line in `source` (the GLSL with its includes
    /// inserted, as vsg's glsl reader returns it), out of `settingsDefines` (SettingsConstants::defines).
    std::set<std::string> includedSettingsDefines(
        std::string_view source, const std::set<std::string>& settingsDefines);

    /// Task 0363 (stage b): the set a Slang twin is chosen by when `options` ask for constants instead of defines
    /// (SettingsConstants::constantDefines): for "water/main.frag" the four shadow settings' defines are replaced by
    /// the one name SHADOW_CONSTANTS, whose permutation reads them as specialization constants (lib/view/shadow.slang).
    /// Task 0363 (stage c): for "terrain/main.frag" and "terrain/main.vert" every define except the map class
    /// (TERRAIN_MAPS, TERRAIN_PBR, PARALLAX_POM) and the G-buffer outputs is dropped and MODE_CONSTANTS added; the
    /// permutation reads them as constants (lib/modeconstants.slang, Pipeline::terrainConstants). Any other stage, and
    /// any call without the switch, returns `defines` unchanged.
    std::set<std::string> constantDefinesFor(
        const std::string& path, std::set<std::string> defines, const vsg::Options* options);

    /// The define set a stage's Slang twin is chosen and traced by: the requested names the stage takes
    /// (effectiveDefines) plus the settings files' defines (SettingsConstants::defines) it takes, none without the
    /// options' SettingsConstants.
    std::set<std::string> stageDefines(
        const std::set<std::string>& requested, const std::set<std::string>& imports, const vsg::Options* options);

    /// The Slang module for the stage file `path` (for example "sky/moon/main.frag") compiled with exactly `defines`:
    /// the precompiled one, read through the options' paths from "<path>.permutations"; null when there is none or the
    /// manifest cannot be read (logged once per path). Thread safe; manifests are read once per path and search paths,
    /// and each (path, set) is looked up and logged once. The process-wide cache holds no module (task 0072, review of
    /// stage f, finding 10): once nothing else holds a module it goes, with the Vulkan objects its compile made, and a
    /// later call reads its file again.
    ///
    /// Task 0072, stage h: when the options carry a SlangRuntime (slangRuntimeKey) and `compileAtRun` is set, a set
    /// the manifest does not list is read from the on-disk cache, or compiled at run time; any failure gives null, with
    /// one log line. Task 0363, stage f: that call WAITS for the worker (SlangRuntime::obtainOrWait, SlangWaitSettings:
    /// a loader thread up to 20 s, the main thread up to 2 s with its window messages pumped), so `*pending` is set
    /// only when the wait ran out; the set is then not remembered as failed (a later call finds what the worker made).
    /// Task 0363, stage g: there is no GLSL to draw with meanwhile or to check the result against; a null answer means
    /// the caller has nothing to draw with (Pipeline::Graphics leaves the model out, with one error that names the
    /// stage and the set).
    ///
    /// Task 0363, stage f: true when the developer aid OPENEDEN_SLANG_UNLIST (a comma list of stage files such as
    /// "default/main.frag", read once) names `path`. findSlangPermutation then ignores that stage's prebuilt files for
    /// a caller that compiles at run time.
    bool slangStageUnlisted(const std::string& path);

    vsg::ref_ptr<vsg::ShaderModule> findSlangPermutation(const std::string& path, const std::set<std::string>& defines,
        const vsg::ref_ptr<const vsg::Options>& options, bool compileAtRun = false, bool* pending = nullptr);

    /// Task 0072, stage h: makes the SlangRuntime of `options` (slangRuntimeKey) with the dynamic compiler that
    /// loads the library from `libraryDirectory`, a cache under `cacheDirectory` (empty: none), the build's
    /// permutations of post/omwfx.vert and of the object shader's largest listed fragment set as the self-tests, and
    /// `log` for its one-line messages. Needs `options` to have its search path (the shader folder) and preferSlang
    /// set. Nothing is loaded here: the library starts at the first compile (there is no start-up preload since queue
    /// item 3 of decision 0012: it cost +237 MiB on every run and did not cure the stall).
    /// `eventSink` (optional) gets one record per run-time compile (SlangRuntime::setEventSink).
    /// Nothing is made (and `options` is left alone) when the build has no permutations to test the library with.
    void createSlangRuntime(const vsg::ref_ptr<vsg::Options>& options, const std::filesystem::path& shaderRoot,
        const std::filesystem::path& libraryDirectory, const std::filesystem::path& cacheDirectory,
        std::function<void(bool warning, const std::string&)> log,
        std::function<void(const SlangCompileEvent&)> eventSink = {});

    /// Review of stage h (round 4), finding 2: the cheap check a cache hit passes before it is used (the full check
    /// ran when the entry was made, and its version is in the cache key): the words parse, are SPIR-V 1.0 and have no
    /// OpSpecConstantOp. Empty when fine, else the reason. A cache file is not trusted just because its own hash is
    /// right: the hash is not secret.
    std::string checkSlangTwinStructure(const std::vector<uint32_t>& words);

    /*
     * Task 0072, stage f: the texture coordinate sets of the object shader. Its GLSL declares the vertex input and the
     * varyings of the sets as arrays whose length is the specialization constant numUvSets (id 0), which a twin
     * compiled at build time cannot have. A family whose manifest lists a uvSetsDefine name is therefore built once per
     * count: a stage whose set declares the arrays is chosen by each pipeline, which knows its count, with
     * uvSetsDefine(numUvSets) added to the set. The name is not a GLSL define (no stage imports it), and a family whose
     * manifest names no count (the sky's, which take one set) is chosen by its stage as before.
     */

    /// "UVSETS_<count>".
    std::string uvSetsDefine(uint32_t count);

    /// True when a stage compiled with `defines` declares the texture coordinate arrays: TEXCOORD or PARTICLE.
    bool declaresUvSetArrays(const std::set<std::string>& defines);

    /// True when a listed set names a texture coordinate count ("UVSETS_" and digits).
    bool countsUvSets(const PermutationManifest& manifest);

    /// The set a twin of the stage `path` is chosen and traced by, or none when this call is not the one that chooses.
    /// Without `numUvSets` (the stage, made once for the pipelines of all counts): stageDefines(requested, the stage's
    /// imports, options), or none when that set declares the texture coordinate arrays and the family's manifest counts
    /// them. With `numUvSets` (the pipeline): that set with uvSetsDefine(*numUvSets) added when it declares the arrays
    /// and the family counts them, else none (the stage chose). A family with no manifest counts nothing. Throws
    /// std::runtime_error when the build wrote no ".slang-stage" file for `path` (no slangc: no shaders).
    std::optional<std::set<std::string>> slangSelectionSet(const std::string& path,
        const std::set<std::string>& requested, const vsg::ref_ptr<const vsg::Options>& options,
        std::optional<uint32_t> numUvSets = std::nullopt);

    /// The Slang twin to draw the stage `path` with: the permutation for slangSelectionSet(path, requested, options,
    /// numUvSets), made at run time if it was not prebuilt (findSlangPermutation, compileAtRun); null when there is
    /// none, and then there is nothing to draw the stage with.
    vsg::ref_ptr<vsg::ShaderModule> slangTwin(const std::string& path, const std::set<std::string>& requested,
        const vsg::ref_ptr<const vsg::Options>& options, std::optional<uint32_t> numUvSets = std::nullopt,
        bool* pending = nullptr);

    /// Sets each stage's module to `twins` at the same index when every stage that has no code yet has one (a stage
    /// that is SPIR-V already needs none), and returns true; otherwise, or when the sizes differ, changes nothing and
    /// returns false. Task 0363, stage g: the false answer used to mean "the GLSL draws"; now it means the pipeline
    /// cannot be made (missingTwinText).
    bool takeSlangTwinsTogether(vsg::ShaderStages& stages, const std::vector<vsg::ref_ptr<vsg::ShaderModule>>& twins);

    /*
     * Task 0072 (review of stage f): the choice of a Pipeline::Graphics stage's twin, worked out once for each stage
     * the stage cache makes instead of for every pipeline, and the requested defines kept beside a twin (the merged
     * body compares them through requestedDefinesOf).
     */
    class SlangStageChoice : public vsg::Inherit<vsg::Object, SlangStageChoice>
    {
    public:
        /// The stage file, for example "default/main.frag".
        std::string path;
        /// stageDefines(the requested names, the stage's imports, the options): the set the twin is chosen by.
        std::set<std::string> defines;
        /// True when `defines` declares the texture coordinate arrays and the family's manifest counts them: each
        /// pipeline then adds its count (slangTwinFor).
        bool byPipeline = false;
        /// Without byPipeline: the twin built for `defines`, or null. Guarded by a mutex inside slangTwinFor.
        vsg::ref_ptr<vsg::ShaderModule> twin;
        /// Without byPipeline: the wait for the twin ran out when the choice was made; slangTwinFor asks again.
        bool pending = false;
        /// The compile settings of the stage's module (its hints: the requested names), kept for requestedDefinesOf
        /// once a twin has replaced the module.
        vsg::ref_ptr<vsg::ShaderCompileSettings> hints;
    };

    inline constexpr const char* slangStageChoiceKey = "openeden.slangStageChoice";

    /// Works out the choice for `stage` (a stage with no code yet, made by vsgUtil::readShader; its module's hints are
    /// the requested names, none without hints) of the stage file `path`, and attaches it to `stage` under
    /// slangStageChoiceKey, so that a copy made with vsg::ShaderStage::create(*stage) carries it too. Returns it. A
    /// stage whose module has code gets nothing and null is returned. Throws std::runtime_error when the build wrote
    /// no stage file for `path`.
    vsg::ref_ptr<SlangStageChoice> attachSlangStageChoice(
        vsg::ShaderStage& stage, const std::string& path, const vsg::ref_ptr<const vsg::Options>& options);

    /// The choice attached to `stage` (or to the stage it was copied from), or null.
    const SlangStageChoice* slangStageChoiceOf(const vsg::ShaderStage& stage);

    /// The twin a pipeline with `numUvSets` texture coordinate sets draws the stage of `choice` with: without
    /// byPipeline `choice.twin`, else the permutation built for `choice.defines` plus uvSetsDefine(numUvSets), or null.
    /// The same module slangTwin(path, requested, options, numUvSets) gives, without choosing the set again.
    vsg::ref_ptr<vsg::ShaderModule> slangTwinFor(const SlangStageChoice& choice, uint32_t numUvSets,
        const vsg::ref_ptr<const vsg::Options>& options, bool* pending = nullptr);

    /// The requested defines of `stage`: its module's compile settings when the module has them, else those its
    /// SlangStageChoice kept (a twin has replaced the module); null when neither.
    const std::set<std::string>* requestedDefinesOf(const vsg::ShaderStage& stage);

    /// The error text for a stage with no twin: "No Slang shader for the stage <path> with the defines [<set>]: ...".
    std::string missingTwinText(const std::string& path, const std::set<std::string>& requested,
        const vsg::ref_ptr<const vsg::Options>& options);

    /// For stages that make one pipeline, read with vsgUtil::readShader and made with no defines (the GUI, task 0072
    /// review of stage f, finding 7): records each stage's use (recordShaderUse, `paths[i]` naming stages[i]) and
    /// returns copies of the stages with the twins and the settings constants they declare. The stages are never
    /// changed: readShader may share them. Sizes that differ give `stages`. Task 0363, stage g: throws
    /// std::runtime_error (missingTwinText) when a stage has no twin; there is no GLSL to fall back to.
    vsg::ShaderStages withSlangTwinsTogether(const vsg::ShaderStages& stages, const std::vector<std::string>& paths,
        const vsg::ref_ptr<const vsg::Options>& options);

    /// For a stage read with vsgUtil::readShader and made with no defines (the compute passes, task 0072 stage c):
    /// records its use (recordShaderUse) and returns a copy of `stage` with the twin's module and the settings
    /// constants. `stage` is never changed: readShader may share it. A stage that has code already is returned as it
    /// is. Throws std::runtime_error (missingTwinText) when there is no twin.
    vsg::ref_ptr<vsg::ShaderStage> withSlangTwin(const vsg::ref_ptr<vsg::ShaderStage>& stage, const std::string& path,
        const vsg::ref_ptr<const vsg::Options>& options);

    /// Formats one trace line: "<path>\t<canonical defines>".
    std::string traceLine(std::string_view path, const std::set<std::string>& defines);

    /// When the environment variable OPENEDEN_SHADER_TRACE names a file, appends one trace line the first time a
    /// (path, defines) pair is seen in this process; the defines written are slangSelectionSet(path, defines, options,
    /// numUvSets) (nothing when that is none: the other call writes it). Does nothing otherwise. Thread safe.
    void recordShaderUse(std::string_view path, const std::set<std::string>& defines,
        const vsg::ref_ptr<const vsg::Options>& options, std::optional<uint32_t> numUvSets = std::nullopt);

    /// True when OPENEDEN_SHADER_TRACE names a file (task 0072, review of stage f: the pipelines' trace calls are made
    /// only then or with the switch on).
    bool shaderTraceActive();

    /// Formats one pipeline's trace line: "<family>/pipeline\t<vertex set>\t<fragment set>\t<slang|glsl>", the sets
    /// canonical (with the texture coordinate count where the pipeline chooses). Task 0363, stage g: every pipeline
    /// is Slang now; the field stays so that older trace readers work.
    std::string pipelineTraceLine(std::string_view family, const std::set<std::string>& vertexSet,
        const std::set<std::string>& fragmentSet, bool slang);

    /// When the trace is on, appends pipelineTraceLine(...) the first time that line is seen in this process (task
    /// 0072, review of stage f: a pipeline takes both twins or neither, so a fragment twin shown drawn proves the
    /// vertex twin of every pipeline it pairs with). Does nothing otherwise. Thread safe.
    void recordPipelineUse(std::string_view family, const std::set<std::string>& vertexSet,
        const std::set<std::string>& fragmentSet, bool slang);
}

#endif

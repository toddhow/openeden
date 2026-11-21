#ifndef OPENEDEN_VSGUTIL_PASSSTAGES_H
#define OPENEDEN_VSGUTIL_PASSSTAGES_H

#include <set>
#include <string>
#include <vector>

#include <vsg/io/Options.h>
#include <vsg/state/ShaderStage.h>

namespace vsgUtil
{
    /*
     * OpenEden (task 0072, stage g): the shader stages of a pass whose pipeline C++ builds by hand, such as the
     * tonemap, bloom, velocity and ambient occlusion passes (components/render) and the post-processing passes
     * (apps/openmw/mwrender/postprocess.cpp). Their GLSL was a string in each file, compiled stage by stage with
     * vsg::ShaderCompiler; task 0363 (stage g) left only Slang: a stage file names the define set a Slang permutation
     * was built for.
     */

    /// One stage of such a pass: the GLSL stage file (for example "post/tonemap.frag") and the names it is compiled
    /// with. Only the names the file imports with "#pragma import_defines" are defined, as for every other stage.
    struct PassStage
    {
        std::string mPath;
        std::set<std::string> mDefines;
    };

    /// Makes each stage (vsgUtil::readShader), records its use (vsgUtil::recordShaderUse) and gives it the Slang twin
    /// built, or compiled at run time, for its set (vsgUtil::slangTwin) with the settings constants it declares
    /// (vsgUtil::addSettingsConstants); a pipeline takes the twins of all its stages (vsgUtil::takeSlangTwinsTogether).
    /// Task 0363, stage g: there is no GLSL to compile instead. Empty when a stage cannot be made or has no twin; the
    /// reason, naming the stage and its set (vsgUtil::missingTwinText), is then put in `*error`, or given to vsg::warn
    /// without `error`. After a success `*error` is empty. VSG's default logger writes to stderr, which openmw.log does
    /// not show, so callers log the error themselves (task 0072, review of stage g, finding 5).
    vsg::ShaderStages readPassStages(const std::vector<PassStage>& stages,
        const vsg::ref_ptr<const vsg::Options>& options, std::string* error = nullptr);

    /*
     * Task 0072, stage h (toddhow, 2026-11-04): the one exemption from "a pipeline takes the twins of all its stages
     * or none". The .omwfx post-processing passes (apps/openmw/mwrender/postprocess.cpp) build each pass's pipeline
     * from this vertex stage and a fragment stage translated from a mod's .omwfx file. Both stages are compiled on
     * their own to SPIR-V before the pipeline is made, so vsg::GraphicsPipeline::compile finds no stage that is GLSL
     * source without code (requiresShaderCompiler) and never links them: a Slang twin beside a translated GLSL
     * fragment stage draws. Nothing else may use this: every other pipeline keeps the together rule
     * (vsgUtil::takeSlangTwinsTogether), and the function has no path parameter on purpose.
     */
    inline constexpr const char* omwfxVertexPath = "post/omwfx.vert";

    /// Reads omwfxVertexPath through `options` and returns a stage of its own, made like readPassStages' stages for
    /// the single stage `{ omwfxVertexPath, {} }`, but with SPIR-V code in every case: with the options preferring
    /// Slang and a twin built for the stage, the twin (with the settings constants it declares); otherwise the GLSL
    /// compiled on its own by vsg::ShaderCompiler, as the pass compiled the string it replaced. Empty when the file
    /// cannot be read or compiled; the reason, naming the file, is then in `*error` (or given to vsg::warn without
    /// `error`); after a success `*error` is empty.
    vsg::ref_ptr<vsg::ShaderStage> readOmwfxVertexStage(
        const vsg::ref_ptr<const vsg::Options>& options, std::string* error = nullptr);
}

#endif

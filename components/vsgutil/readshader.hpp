#ifndef VSGOPENMW_VSGUTIL_READSHADER_H
#define VSGOPENMW_VSGUTIL_READSHADER_H

#include <string>

#include <vsg/io/Options.h>
#include <vsg/state/ShaderStage.h>

namespace vsgUtil
{
    /*
     * Task 0363, stage g: a built-in shader stage is no longer read from GLSL. Makes a vsg::ShaderStage for the stage
     * file `path` (for example "default/main.frag", "post/tonemap.frag"): its kind comes from the extension, its entry
     * point is "main" and its module has no code and no source. The caller gives it the Slang twin of the define set it
     * draws with (vsgUtil::slangTwin, vsgUtil::withSlangTwin, Pipeline::Graphics, Pipeline::terrain). Throws when the
     * extension is not .vert, .frag or .comp. Every call returns a new stage (nothing is shared), so a caller may
     * change it.
     */
    vsg::ref_ptr<vsg::ShaderStage> readShader(const std::string& path, vsg::ref_ptr<const vsg::Options> options);

    /*
     * Reads a GLSL vsg::ShaderStage through the options (vsg's glsl reader, includes inserted), potentially shared, or
     * throws. The one GLSL shader the game still ships is post/omwfx.vert (the vertex stage of the .omwfx
     * post-processing passes, decision 0009); the tests read the recorded GLSL of other stages with it as well.
     */
    vsg::ref_ptr<vsg::ShaderStage> readGlslShader(const std::string& path, vsg::ref_ptr<const vsg::Options> options);
}

#endif

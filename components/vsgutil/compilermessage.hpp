#ifndef OPENEDEN_VSGUTIL_COMPILERMESSAGE_H
#define OPENEDEN_VSGUTIL_COMPILERMESSAGE_H

#include <string>

namespace vsgUtil
{
    /*
     * OpenEden: glslang's message for a vsg::ShaderCompiler run that failed. VSG reports it only through vsg::Logger
     * (ShaderCompiler::compile: warn("glslang info log:\n", ...)), and VSG's default logger writes with fprintf to
     * stderr, which openmw.log does not show (Debug::setupLogging redirects the std::cout and std::cerr stream buffers
     * only). Moved here from apps/openmw/mwrender/postprocess.cpp in task 0072 (review of stage g, finding 5), so that
     * the post-processing passes and vsgUtil::readPassStages share one.
     */

    /// While it lives, keeps in `message` the text that follows "glslang info log:" in the last vsg::Logger warning on
    /// this thread; other threads and other messages are not touched, and every message still reaches the logger that
    /// was installed before. Captures nest: the inner one gets the message, the outer one again after it ends. The
    /// first capture installs a logger that passes everything on, once and for good (swapping vsg::Logger::instance()
    /// around each compile would race with other threads logging), with the level the logger had then.
    class CompilerMessageCapture
    {
    public:
        explicit CompilerMessageCapture(std::string& message);
        ~CompilerMessageCapture();
        CompilerMessageCapture(const CompilerMessageCapture&) = delete;
        CompilerMessageCapture& operator=(const CompilerMessageCapture&) = delete;

    private:
        std::string* mPrevious;
    };
}

#endif

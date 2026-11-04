#include "compilermessage.hpp"

#include <string_view>

#include <vsg/core/Inherit.h>
#include <vsg/io/Logger.h>

namespace vsgUtil
{
    namespace
    {
        // The message of the innermost capture on this thread, or null.
        thread_local std::string* tCompilerMessage = nullptr;

        // Passes vsg's messages on, keeping glslang's own for a capture on this thread.
        class CompilerLogCapture : public vsg::Inherit<vsg::Logger, CompilerLogCapture>
        {
        public:
            vsg::ref_ptr<vsg::Logger> previous;

        protected:
            void debug_implementation(const std::string_view& text) override { previous->debug(text); }
            void info_implementation(const std::string_view& text) override { previous->info(text); }
            void warn_implementation(const std::string_view& text) override
            {
                constexpr std::string_view prefix = "glslang info log:";
                if (tCompilerMessage && text.starts_with(prefix))
                    *tCompilerMessage = text.substr(prefix.size());
                previous->warn(text);
            }
            void error_implementation(const std::string_view& text) override { previous->error(text); }
            void fatal_implementation(const std::string_view& text) override { previous->fatal(text); }
        };

        void installCompilerLogCapture()
        {
            static const bool installed = [] {
                auto capture = CompilerLogCapture::create();
                capture->previous = vsg::Logger::instance();
                capture->level = capture->previous->level;
                vsg::Logger::instance() = capture;
                return true;
            }();
            (void)installed;
        }
    }

    CompilerMessageCapture::CompilerMessageCapture(std::string& message)
        : mPrevious(tCompilerMessage)
    {
        installCompilerLogCapture();
        tCompilerMessage = &message;
    }

    CompilerMessageCapture::~CompilerMessageCapture()
    {
        tCompilerMessage = mPrevious;
    }
}

#include "compilecontext.hpp"

#include <iostream>

#include <vsg/app/CompileManager.h>
#include <vsg/app/View.h>
#include <vsg/vk/Device.h>

#include <components/misc/loadtrace.hpp>

namespace vsgUtil
{
    std::shared_mutex& viewerCompileMutex()
    {
        static std::shared_mutex mutex;
        return mutex;
    }

    CompileContext::CompileContext() {}

    CompileContext::~CompileContext() {}

    vsg::ref_ptr<CompileContext> CompileContext::clone(vsg::Mask viewMask) const
    {
        auto ret = vsg::ref_ptr{ new CompileContext(*this) };
        ret->setViewMask(viewMask);
        return ret;
    }

    bool CompileContext::compile(vsg::ref_ptr<vsg::Object> obj) const
    {
        std::shared_lock lock(viewerCompileMutex());
        Misc::LoadTrace::Scope traceCompile("compile.vsg");
        auto res = compileManager->compile(obj, contextSelectionFunction);
        traceCompile.end();
        if (res.result == VK_SUCCESS)
        {
            Misc::LoadTrace::Scope traceDone("compile.oncompiled");
            onCompiled(res);
            return true;
        }
        else
        {
            std::cerr << "!compile(VkResult=" << res.result << ", message=\"" << res.message << "\")" << std::endl;
            return false;
        }
    }

    void CompileContext::setViewMask(vsg::Mask mask)
    {
        contextSelectionFunction = [mask](vsg::Context& ctx) -> bool {
            if (vsg::ref_ptr<vsg::View> view = ctx.view)
                return (view->mask & mask) != 0;
            return false;
        };
    }
}

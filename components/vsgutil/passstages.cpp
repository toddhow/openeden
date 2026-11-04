#include "passstages.hpp"

#include "compilermessage.hpp"
#include "readshader.hpp"
#include "shaderpermutation.hpp"

#include <exception>
#include <string_view>
#include <utility>

#include <vsg/io/Logger.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/utils/ShaderCompiler.h>

namespace vsgUtil
{
    namespace
    {
        // Gives the reason to the caller, or to vsg::warn when the caller takes none, and returns no stages.
        vsg::ShaderStages fail(std::string* error, std::string reason)
        {
            if (error)
                *error = std::move(reason);
            else
                vsg::warn(reason);
            return {};
        }

        std::string trimmed(std::string_view text)
        {
            const std::size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const std::size_t last = text.find_last_not_of(" \t\r\n");
            return std::string(text.substr(first, last - first + 1));
        }
    }

    vsg::ShaderStages readPassStages(
        const std::vector<PassStage>& stages, const vsg::ref_ptr<const vsg::Options>& options, std::string* error)
    {
        if (error)
            error->clear();
        const bool preferSlang = prefersSlang(options.get());
        vsg::ShaderStages result;
        std::vector<vsg::ref_ptr<vsg::ShaderModule>> twins;
        for (const PassStage& pass : stages)
        {
            vsg::ref_ptr<vsg::ShaderStage> shared;
            try
            {
                shared = readShader(pass.mPath, options);
            }
            catch (const std::exception& e)
            {
                return fail(error, "The shader stage \"" + pass.mPath + "\" cannot be read: " + e.what());
            }
            if (!shared->module || shared->module->source.empty())
                return fail(error, "The shader stage \"" + pass.mPath + "\" has no GLSL source");
            recordShaderUse(pass.mPath, pass.mDefines, shared.get(), options);
            // The reader may share `shared` (the shader options keep shared objects). Compiled in place, every variant
            // would share its module: each compile overwrites the module's code (vsg::ShaderCompiler::compile always
            // regenerates it), and vsg::ShaderModule::compile makes the module's Vulkan object once per device, from
            // the code it holds then, so the variants would draw one another's code. So every caller gets a stage and
            // a module of its own, made from the source alone as the passes made theirs (task 0072, review of stage g).
            auto module = vsg::ShaderModule::create(shared->module->source);
            auto stage = vsg::ShaderStage::create(shared->stage, shared->entryPointName, module);
            stage->specializationConstants = shared->specializationConstants;
            result.push_back(stage);
            twins.push_back(slangTwin(pass.mPath, pass.mDefines, shared->module->source, preferSlang, options));
        }
        if (result.empty())
            return {};
        // A pipeline takes the twins of all its stages or none (task 0072, stage f).
        if (takeSlangTwinsTogether(result, twins))
        {
            for (const auto& stage : result)
                addSettingsConstants(*stage, options.get());
            return result;
        }
        // Each stage on its own, as the passes compiled their strings: no program is linked.
        auto compiler = vsg::ShaderCompiler::create();
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const std::vector<std::string> defines(stages[i].mDefines.begin(), stages[i].mDefines.end());
            std::string message;
            bool compiled = false;
            {
                const CompilerMessageCapture capture(message);
                compiled = compiler->compile(result[i], defines, options);
            }
            if (!compiled)
            {
                const std::string glslang = trimmed(message);
                return fail(error,
                    "The shader stage \"" + stages[i].mPath + "\" failed to compile"
                        + (glslang.empty() ? std::string(" (glslang gave no message)") : ": " + glslang));
            }
        }
        return result;
    }
}

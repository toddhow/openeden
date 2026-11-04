#include "passstages.hpp"

#include "readshader.hpp"
#include "shaderpermutation.hpp"

#include <exception>

#include <vsg/io/Logger.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/utils/ShaderCompiler.h>

namespace vsgUtil
{
    vsg::ShaderStages readPassStages(
        const std::vector<PassStage>& stages, const vsg::ref_ptr<const vsg::Options>& options)
    {
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
                vsg::warn("The shader stage \"", pass.mPath, "\" cannot be read: ", e.what());
                return {};
            }
            if (!shared->module || shared->module->source.empty())
            {
                vsg::warn("The shader stage \"", pass.mPath, "\" has no GLSL source");
                return {};
            }
            recordShaderUse(pass.mPath, pass.mDefines, shared.get(), options);
            // The reader may share `shared` (the shader options keep shared objects), and VSG compiles only a module
            // that has no code yet, so every caller gets a stage and a module of its own.
            auto module = vsg::ShaderModule::create(shared->module->source);
            module->hints = shared->module->hints;
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
            if (!compiler->compile(result[i], defines, options))
            {
                vsg::warn("The shader stage \"", stages[i].mPath, "\" failed to compile");
                return {};
            }
        }
        return result;
    }
}

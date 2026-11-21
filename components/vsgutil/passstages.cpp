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
        vsg::ShaderStages result;
        std::vector<vsg::ref_ptr<vsg::ShaderModule>> twins;
        for (const PassStage& pass : stages)
        {
            // A stage of its own for every caller (nothing is shared, see readShader).
            vsg::ref_ptr<vsg::ShaderStage> stage;
            try
            {
                stage = readShader(pass.mPath, options);
                recordShaderUse(pass.mPath, pass.mDefines, options);
                twins.push_back(slangTwin(pass.mPath, pass.mDefines, options));
            }
            catch (const std::exception& e)
            {
                return fail(error, "The shader stage \"" + pass.mPath + "\" cannot be made: " + e.what());
            }
            result.push_back(stage);
        }
        if (result.empty())
            return {};
        // A pipeline takes the twins of all its stages (task 0072, stage f); task 0363, stage g: there is no GLSL to
        // compile instead, so a stage with no twin gives no stages and the reason.
        if (!takeSlangTwinsTogether(result, twins))
        {
            for (std::size_t i = 0; i < twins.size(); ++i)
            {
                if (!twins[i])
                    return fail(error, missingTwinText(stages[i].mPath, stages[i].mDefines, options));
            }
            return fail(error, "The shader stages cannot be made");
        }
        for (const auto& stage : result)
            addSettingsConstants(*stage, options.get());
        return result;
    }

    vsg::ref_ptr<vsg::ShaderStage> readOmwfxVertexStage(
        const vsg::ref_ptr<const vsg::Options>& options, std::string* error)
    {
        if (error)
            error->clear();
        const auto failed = [error](std::string reason) -> vsg::ref_ptr<vsg::ShaderStage> {
            fail(error, std::move(reason));
            return {};
        };
        vsg::ref_ptr<vsg::ShaderStage> shared;
        try
        {
            shared = readGlslShader(omwfxVertexPath, options);
        }
        catch (const std::exception& e)
        {
            return failed(std::string("The shader stage \"") + omwfxVertexPath + "\" cannot be read: " + e.what());
        }
        if (!shared->module || shared->module->source.empty())
            return failed(std::string("The shader stage \"") + omwfxVertexPath + "\" has no GLSL source");
        recordShaderUse(omwfxVertexPath, {}, options);

        // A stage and a module of its own for every caller (see readPassStages): the options share what they read.
        // A folder with no .slang-stage file for it (the editor's tests, a mod's own copy) has no twin: the GLSL stays.
        vsg::ref_ptr<vsg::ShaderModule> twin;
        try
        {
            twin = slangTwin(omwfxVertexPath, {}, options);
        }
        catch (const std::exception&)
        {
        }
        if (twin)
        {
            // The twin is shared too (findSlangPermutation caches it), so the stage gets a copy: SPIR-V and no GLSL
            // source, so that vsg::GraphicsPipeline::compile finds nothing to link (see the header).
            auto stage = vsg::ShaderStage::create(
                shared->stage, shared->entryPointName, vsg::ShaderModule::create(twin->code));
            stage->specializationConstants = shared->specializationConstants;
            addSettingsConstants(*stage, options.get());
            return stage;
        }
        auto stage = vsg::ShaderStage::create(
            shared->stage, shared->entryPointName, vsg::ShaderModule::create(shared->module->source));
        stage->specializationConstants = shared->specializationConstants;
        std::string message;
        bool compiled = false;
        {
            const CompilerMessageCapture capture(message);
            compiled = vsg::ShaderCompiler::create()->compile(stage, std::vector<std::string>{}, options);
        }
        if (!compiled)
        {
            const std::string glslang = trimmed(message);
            return failed(std::string("The shader stage \"") + omwfxVertexPath + "\" failed to compile"
                + (glslang.empty() ? std::string(" (glslang gave no message)") : ": " + glslang));
        }
        return stage;
    }
}

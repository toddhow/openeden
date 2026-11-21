#include "readshader.hpp"

#include <stdexcept>
#include <string_view>

#include <vsg/io/read.h>
#include <vsg/state/ShaderModule.h>

namespace vsgUtil
{
    namespace
    {
        bool endsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }
    }

    vsg::ref_ptr<vsg::ShaderStage> readShader(const std::string& path, vsg::ref_ptr<const vsg::Options> options)
    {
        (void)options;
        VkShaderStageFlagBits kind = VK_SHADER_STAGE_VERTEX_BIT;
        if (endsWith(path, ".frag"))
            kind = VK_SHADER_STAGE_FRAGMENT_BIT;
        else if (endsWith(path, ".comp"))
            kind = VK_SHADER_STAGE_COMPUTE_BIT;
        else if (!endsWith(path, ".vert"))
            throw std::runtime_error("!readShader(\"" + path + "\"): not a .vert, .frag or .comp stage file");
        return vsg::ShaderStage::create(kind, "main", vsg::ShaderModule::create());
    }

    vsg::ref_ptr<vsg::ShaderStage> readGlslShader(const std::string& path, vsg::ref_ptr<const vsg::Options> options)
    {
        if (auto shaderStage = vsg::read_cast<vsg::ShaderStage>(path, options))
            return shaderStage;
        throw std::runtime_error("!readGlslShader(\"" + path + "\")");
    }
}

#include "defaultsampler.hpp"

#include <algorithm>
#include <string>

#include <vsg/state/Sampler.h>
#include <vsg/vk/PhysicalDevice.h>

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

namespace Render
{
    vsg::ref_ptr<vsg::Sampler> createDefaultSampler(vsg::ref_ptr<vsg::PhysicalDevice> physicalDevice, bool supportsAnisotropy)
    {
        auto sampler = vsg::Sampler::create();
        sampler->anisotropyEnable = supportsAnisotropy;
        sampler->maxAnisotropy = std::min(static_cast<int>(Settings::general().mAnisotropy), static_cast<int>(physicalDevice->getProperties().limits.maxSamplerAnisotropy));

        auto getFilterMode = [](const std::string& s) -> auto {
            if (s == "nearest")
                return VK_FILTER_NEAREST;
            else if (s == "linear")
                return VK_FILTER_LINEAR;
            else
                Log(Debug::Warning) << "Warning: Invalid texture filter: " << s;
            return VK_FILTER_LINEAR;
        };
        sampler->minFilter = getFilterMode(Settings::general().mTextureMinFilter);
        sampler->magFilter = getFilterMode(Settings::general().mTextureMagFilter);

        std::string mipstr = Settings::general().mTextureMipmap;
        if (mipstr == "nearest")
        {
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->maxLod = VK_LOD_CLAMP_NONE;
        }
        else if (mipstr == "linear")
        {
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler->maxLod = VK_LOD_CLAMP_NONE;
        }
        else if (mipstr == "none")
        {
            sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler->minLod = 0;
            sampler->maxLod = 0.25;
        }
        else
            Log(Debug::Warning) << "Warning: Invalid texture mipmap: " << mipstr;

        return sampler;
    }
}

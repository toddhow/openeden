#include "readimage.hpp"

#include <stdexcept>

#include <vsg/core/Data.h>
#include <vsg/io/read.h>

namespace vsgUtil
{
    void removeSrgb(vsg::Data& data)
    {
        // openeden-vsg: vsgXchange 1.1's stb_image reader (TGA, PNG, JPG, BMP) tags 8-bit images as sRGB, so the GPU
        // converted them to linear when sampling and textures came out far too dark. vsgopenmw's shaders work on the
        // stored values, as OpenMW's did, so the same bytes are read as UNORM.
        VkFormat& format = data.properties.format;
        switch (format)
        {
            case VK_FORMAT_R8_SRGB:
                format = VK_FORMAT_R8_UNORM;
                break;
            case VK_FORMAT_R8G8_SRGB:
                format = VK_FORMAT_R8G8_UNORM;
                break;
            case VK_FORMAT_R8G8B8_SRGB:
                format = VK_FORMAT_R8G8B8_UNORM;
                break;
            case VK_FORMAT_B8G8R8_SRGB:
                format = VK_FORMAT_B8G8R8_UNORM;
                break;
            case VK_FORMAT_R8G8B8A8_SRGB:
                format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            case VK_FORMAT_B8G8R8A8_SRGB:
                format = VK_FORMAT_B8G8R8A8_UNORM;
                break;
            default:
                break;
        }
    }

    vsg::ref_ptr<vsg::Data> readImage(const std::string& path, vsg::ref_ptr<const vsg::Options> options)
    {
        if (auto data = readOptionalImage(path, options))
            return data;
        throw std::runtime_error("!readImage(\"" + path + "\")");
    }

    vsg::ref_ptr<vsg::Data> readOptionalImage(const std::string& path, vsg::ref_ptr<const vsg::Options> options)
    {
        auto data = vsg::read_cast<vsg::Data>(path, options);
        if (data)
            removeSrgb(*data);
        return data;
    }
}

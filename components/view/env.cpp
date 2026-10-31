#include "env.hpp"

#include <iomanip>
#include <sstream>

#include <vsg/state/DescriptorImage.h>

#include <components/pipeline/viewbindings.hpp>
#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/name.hpp>
#include <components/vsgutil/readimage.hpp>

#include "descriptors.hpp"

namespace View
{
    namespace
    {
        vsg::ref_ptr<vsg::Data> readEnvData(vsg::ref_ptr<const vsg::Options> options)
        {
            vsg::DataList images;
            for (int i = 0; i < Pipeline::Descriptors::VIEW_ENV_COUNT; ++i)
            {
                std::stringstream stream;
                stream << "magicitem/caust";
                stream << std::setw(2);
                stream << std::setfill('0');
                stream << i;
                stream << ".dds";
                auto name = stream.str();
                auto data = vsgUtil::readImage(name, options);
                vsgUtil::setName(*data, name);
                images.push_back(data);
            }
            return vsgUtil::convertToArrayTexture(images);
        }
    }

    vsg::ref_ptr<vsg::Descriptor> readEnv(vsg::ref_ptr<const vsg::Options> options)
    {
        if (auto data = readEnvData(options))
        {
            auto sampler = vsg::Sampler::create();
            return vsg::DescriptorImage::create(
                sampler, data, Pipeline::Descriptors::VIEW_ENV_BINDING, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
        else
            return dummyEnvMap();
    }
}

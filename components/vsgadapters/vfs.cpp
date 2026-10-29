#include "vfs.hpp"

#include <istream>

#include <components/vfs/manager.hpp>

namespace vsgAdapters
{
    vsg::ref_ptr<vsg::Object> vfs::read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const
    {
        auto readImpl = [this] (const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) -> vsg::ref_ptr<vsg::Object>
        {
            std::string filenameStr = filename.string();
            if (!mVfs.exists(filenameStr))
                return {};

            auto stream = mVfs./*search*/ get(filenameStr);
            auto ext = vsg::lowerCaseFileExtension(filename);

            auto extOptions = vsg::Options::create(*options);
            extOptions->extensionHint = ext;
            extOptions->setValue("filename", filenameStr);

            return CompositeReaderWriter::read(*stream, extOptions);
        };

        if (options->findFileCallback)
            return readImpl(options->findFileCallback(filename, options), options);
        return readImpl(filename, options);
    }
}

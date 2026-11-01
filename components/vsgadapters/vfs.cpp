#include "vfs.hpp"

#include <istream>

#include <components/vfs/manager.hpp>

namespace vsgAdapters
{
    vsg::ref_ptr<vsg::Object> vfs::read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const
    {
        auto readImpl
            = [this](const vsg::Path& path, vsg::ref_ptr<const vsg::Options> readOptions) -> vsg::ref_ptr<vsg::Object> {
            std::string filenameStr = path.string();
            if (!mVfs.exists(filenameStr))
                return {};

            auto stream = mVfs./*search*/ get(filenameStr);
            auto ext = vsg::lowerCaseFileExtension(path);

            auto extOptions = vsg::Options::create(*readOptions);
            extOptions->extensionHint = ext;
            extOptions->setValue("filename", filenameStr);

            return CompositeReaderWriter::read(*stream, extOptions);
        };

        if (options->findFileCallback)
            return readImpl(options->findFileCallback(filename, options), options);
        return readImpl(filename, options);
    }
}

#include "bodycommon.hpp"

#include <array>
#include <exception>
#include <utility>

#include <components/files/conversion.hpp>
#include <components/vfs/manager.hpp>

#include <edenarchive/hash.hpp>

namespace OMW::BodyCommon
{
    ParsedFile parseFile(const VFS::Manager& vfs, const std::string& path)
    {
        ParsedFile parsed;
        try
        {
            {
                Files::IStreamPtr stream = vfs.get(VFS::Path::Normalized(path));
                EdenArchive::Hasher hasher;
                std::array<char, 65536> buffer;
                while (true)
                {
                    stream->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const std::streamsize got = stream->gcount();
                    if (got <= 0)
                        break;
                    parsed.mBytes += static_cast<std::uint64_t>(got);
                    if (parsed.mBytes > maxNifBytes)
                    {
                        parsed.mError = "the file is over " + std::to_string(maxNifBytes) + " bytes";
                        return parsed;
                    }
                    hasher.update(buffer.data(), static_cast<std::size_t>(got));
                    if (!*stream)
                        break;
                }
                parsed.mDigest = EdenArchive::toHex(hasher.finalize());
            }
            parsed.mFile = std::make_unique<Nif::NIFFile>(VFS::Path::Normalized(path));
            Nif::Reader reader(*parsed.mFile);
            reader.parse(vfs.get(VFS::Path::Normalized(path)));
        }
        catch (const std::exception& e)
        {
            parsed.mFile.reset();
            parsed.mError = e.what();
        }
        return parsed;
    }

    std::vector<BodyNif::Shape> shapesOf(const ParsedFile& parsed, const std::string& filter,
        const BodyNif::Placement& placement, std::vector<std::string>& errors, std::size_t* fileShapes)
    {
        std::vector<BodyNif::Shape> shapes;
        if (fileShapes != nullptr)
            *fileShapes = 0;
        if (!parsed.mFile)
            return shapes;
        for (const Nif::Record* root : parsed.mFile->mRoots)
        {
            const auto* node = dynamic_cast<const Nif::NiAVObject*>(root);
            if (node == nullptr)
                continue;
            BodyNif::ReadResult result = BodyNif::readShapes(*node, filter, {}, placement);
            for (BodyNif::Shape& shape : result.mShapes)
                shapes.push_back(std::move(shape));
            for (std::string& error : result.mErrors)
                errors.push_back(std::move(error));
            if (fileShapes != nullptr)
                *fileShapes += BodyNif::readShapes(*node, "").mShapes.size();
        }
        return shapes;
    }
}

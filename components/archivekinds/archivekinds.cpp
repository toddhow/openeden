#include "archivekinds.hpp"

#include <components/bsa/bsafile.hpp>

#include "bsaarchive.hpp"

namespace VFS
{
    namespace
    {
        // The keys the three factories are registered under (addArchiveFactoryOnce), so that a second call adds
        // nothing.
        constexpr const char* tes3Key = "archivekinds.tes3";
        constexpr const char* tes4Key = "archivekinds.tes4";
        constexpr const char* ba2Key = "archivekinds.ba2";
    }

    ArchiveFactory tes3ArchiveFactory()
    {
        return [](const std::filesystem::path& path,
                   const ToUTF8::StatelessUtf8Encoder* encoder) -> std::unique_ptr<Archive> {
            if (Bsa::BSAFile::detectVersion(path) != Bsa::BsaVersion::Uncompressed)
                return nullptr;
            return std::make_unique<BsaArchive<Bsa::BSAFile>>(path, encoder);
        };
    }

    ArchiveFactory tes4ArchiveFactory()
    {
        return [](const std::filesystem::path& path,
                   const ToUTF8::StatelessUtf8Encoder* encoder) -> std::unique_ptr<Archive> {
            if (Bsa::BSAFile::detectVersion(path) != Bsa::BsaVersion::Compressed)
                return nullptr;
            return std::make_unique<BsaArchive<Bsa::CompressedBSAFile>>(path, encoder);
        };
    }

    ArchiveFactory ba2ArchiveFactory()
    {
        return [](const std::filesystem::path& path,
                   const ToUTF8::StatelessUtf8Encoder* encoder) -> std::unique_ptr<Archive> {
            switch (Bsa::BSAFile::detectVersion(path))
            {
                case Bsa::BsaVersion::BA2GNRL:
                    return std::make_unique<BsaArchive<Bsa::BA2GNRLFile>>(path, encoder);
                case Bsa::BsaVersion::BA2DX10:
                    return std::make_unique<BsaArchive<Bsa::BA2DX10File>>(path, encoder);
                case Bsa::BsaVersion::Unknown:
                case Bsa::BsaVersion::Uncompressed:
                case Bsa::BsaVersion::Compressed:
                    break;
            }
            return nullptr;
        };
    }

    void registerBethesdaArchiveKinds()
    {
        addArchiveFactoryOnce(tes3Key, tes3ArchiveFactory());
        addArchiveFactoryOnce(tes4Key, tes4ArchiveFactory());
        addArchiveFactoryOnce(ba2Key, ba2ArchiveFactory());
    }
}

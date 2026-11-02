#include "archivefactory.hpp"

#include <stdexcept>
#include <vector>

#include <components/files/conversion.hpp>

#include "bsaarchive.hpp"
#include "edenarchive.hpp"

namespace VFS
{
    namespace
    {
        // Written at start-up only, read while archives are registered, also at start-up (see addArchiveFactory).
        std::vector<ArchiveFactory>& factories()
        {
            static std::vector<ArchiveFactory> list;
            return list;
        }
    }

    void addArchiveFactory(ArchiveFactory factory)
    {
        factories().push_back(std::move(factory));
    }

    void clearArchiveFactories()
    {
        factories().clear();
    }

    std::unique_ptr<Archive> makeArchive(const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        for (const ArchiveFactory& factory : factories())
        {
            if (std::unique_ptr<Archive> archive = factory(path, encoder))
                return archive;
        }

        // The Eden Archive reader (task 0052) is built in like the Bethesda one: a file that begins with the seven
        // bytes EDENPAK is a pack, so `fallback-archive=` can name one without any start-up call.
        if (std::unique_ptr<Archive> archive = makeEdenArchive(path, encoder))
            return archive;

        // makeBsaArchive throws "Unknown archive type '<path>'" for a file it does not recognise.
        return makeBsaArchive(path, encoder);
    }
}

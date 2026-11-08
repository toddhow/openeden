#include "archivefactory.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <components/files/conversion.hpp>

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

        // The keys of the factories added with addArchiveFactoryOnce. Same threading as the list.
        std::set<std::string>& keys()
        {
            static std::set<std::string> list;
            return list;
        }
    }

    void addArchiveFactory(ArchiveFactory factory)
    {
        factories().push_back(std::move(factory));
    }

    bool addArchiveFactoryOnce(const std::string& key, ArchiveFactory factory)
    {
        if (!keys().insert(key).second)
            return false;
        factories().push_back(std::move(factory));
        return true;
    }

    std::size_t archiveFactoryCount()
    {
        return factories().size();
    }

    void clearArchiveFactories()
    {
        factories().clear();
        keys().clear();
    }

    std::unique_ptr<Archive> makeArchive(const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        for (const ArchiveFactory& factory : factories())
        {
            if (std::unique_ptr<Archive> archive = factory(path, encoder))
                return archive;
        }

        // The Eden Archive reader (task 0052) is built in: a file that begins with the seven
        // bytes EDENPAK is a pack, so `fallback-archive=` can name one without any start-up call.
        if (std::unique_ptr<Archive> archive = makeEdenArchive(path, encoder))
            return archive;

        throw std::runtime_error("Unknown archive type '" + Files::pathToUnicodeString(path) + "'");
    }
}

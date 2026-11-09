#include "archivefactory.hpp"

#include <mutex>
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
        // Guards the list and the keys below. A lookup copies the list under the lock and runs the factories outside
        // it, so a factory may itself register or ask for the count, and a slow file read never blocks registration.
        std::mutex& listMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        // Always accessed with listMutex() held.
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
        const std::lock_guard<std::mutex> lock(listMutex());
        factories().push_back(std::move(factory));
    }

    bool addArchiveFactoryOnce(const std::string& key, ArchiveFactory factory)
    {
        const std::lock_guard<std::mutex> lock(listMutex());
        if (!keys().insert(key).second)
            return false;
        factories().push_back(std::move(factory));
        return true;
    }

    std::size_t archiveFactoryCount()
    {
        const std::lock_guard<std::mutex> lock(listMutex());
        return factories().size();
    }

    void clearArchiveFactories()
    {
        const std::lock_guard<std::mutex> lock(listMutex());
        factories().clear();
        keys().clear();
    }

    std::unique_ptr<Archive> makeArchive(const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        std::vector<ArchiveFactory> snapshot;
        {
            const std::lock_guard<std::mutex> lock(listMutex());
            snapshot = factories();
        }
        for (const ArchiveFactory& factory : snapshot)
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

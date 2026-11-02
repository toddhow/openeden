#include "manager.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <stdexcept>

#include <components/files/conversion.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include "archive.hpp"
#include "file.hpp"
#include "pathutil.hpp"
#include "recursivedirectoryiterator.hpp"

namespace VFS
{
    Manager::Manager() = default;

    Manager::~Manager() = default;

    void Manager::reset()
    {
        mIndex.clear();
        mProviders.clear();
        mArchives.clear();
    }

    void Manager::addArchive(std::unique_ptr<Archive>&& archive)
    {
        mArchives.push_back(std::move(archive));
    }

    void Manager::buildIndex()
    {
        mIndex.clear();
        mProviders.clear();

        // Each archive lists its files into a map of its own, which is then merged in registration order: a path
        // that is already in the index is a path the earlier archives provide, so the new file wins and the old one
        // is remembered as overridden.
        for (std::size_t i = 0; i < mArchives.size(); ++i)
        {
            FileMap layer;
            mArchives[i]->listResources(layer);

            if (mIndex.empty())
            {
                mIndex = std::move(layer);
                continue;
            }

            while (!layer.empty())
            {
                FileMap::node_type node = layer.extract(layer.begin());
                File* const file = node.mapped();
                auto inserted = mIndex.insert(std::move(node));
                if (inserted.inserted)
                    continue;

                const auto current = inserted.position;
                std::vector<Provider>& providers = mProviders[current->first];
                if (providers.empty())
                {
                    // The first time this path is overridden: the file in the index belongs to the highest earlier
                    // archive that has the path. Later providers are appended in order, so the list stays sorted.
                    std::size_t owner = 0;
                    [[maybe_unused]] bool found = false;
                    for (std::size_t j = i; j-- > 0;)
                    {
                        if (mArchives[j]->contains(current->first))
                        {
                            owner = j;
                            found = true;
                            break;
                        }
                    }
                    // An archive whose contains() disagrees with its listResources() is a bug in that archive.
                    assert(found);
                    providers.push_back({ owner, current->second });
                }
                providers.push_back({ i, file });
                current->second = file;
            }
        }
    }

    Files::IStreamPtr Manager::find(Path::NormalizedView name) const
    {
        return findNormalized(name.value());
    }

    Files::IStreamPtr Manager::get(const Path::Normalized& name) const
    {
        return getNormalized(name);
    }

    Files::IStreamPtr Manager::get(Path::NormalizedView name) const
    {
        return getNormalized(name.value());
    }

    Files::IStreamPtr Manager::getNormalized(std::string_view normalizedName) const
    {
        assert(Path::isNormalized(normalizedName));
        auto ptr = findNormalized(normalizedName);
        if (ptr == nullptr)
            throw std::runtime_error("Resource '" + std::string(normalizedName) + "' not found");
        return ptr;
    }

    bool Manager::exists(const Path::Normalized& name) const
    {
        return mIndex.find(name) != mIndex.end();
    }

    bool Manager::exists(Path::NormalizedView name) const
    {
        return mIndex.find(name) != mIndex.end();
    }

    std::string Manager::getArchiveDescription(std::size_t index) const
    {
        if (index >= mArchives.size())
            throw std::out_of_range("VFS archive index " + std::to_string(index) + " is not registered");
        return mArchives[index]->getDescription();
    }

    std::vector<Override> Manager::getOverrides() const
    {
        std::vector<Override> result;
        result.reserve(mProviders.size());
        for (const auto& [path, providers] : mProviders)
        {
            Override item{ path, providers.back(), {}, true };
            item.mLosers.assign(providers.rbegin() + 1, providers.rend());

            const std::optional<ContentHash> winnerHash = item.mWinner.mFile->getContentHash();
            if (!winnerHash.has_value())
                item.mIdentical = false;
            for (const Provider& loser : item.mLosers)
            {
                if (!item.mIdentical)
                    break;
                if (loser.mFile->getContentHash() != winnerHash)
                    item.mIdentical = false;
            }
            result.push_back(std::move(item));
        }
        return result;
    }

    std::vector<Override> Manager::getConflicts() const
    {
        std::vector<Override> result = getOverrides();
        result.erase(std::remove_if(result.begin(), result.end(), [](const Override& item) { return item.mIdentical; }),
            result.end());
        return result;
    }

    std::vector<SourceOverrideCount> Manager::getOverrideCounts() const
    {
        std::vector<SourceOverrideCount> counts;
        counts.reserve(mArchives.size());
        for (std::size_t i = 0; i < mArchives.size(); ++i)
            counts.push_back({ i, mArchives[i]->getDescription(), 0, 0, 0 });

        for (const Override& item : getOverrides())
        {
            if (item.mIdentical)
            {
                ++counts[item.mWinner.mArchive].mIdenticalDuplicates;
                for (const Provider& loser : item.mLosers)
                    ++counts[loser.mArchive].mIdenticalDuplicates;
            }
            else
            {
                ++counts[item.mWinner.mArchive].mOverriding;
                for (const Provider& loser : item.mLosers)
                    ++counts[loser.mArchive].mOverridden;
            }
        }
        return counts;
    }

    std::string Manager::getArchive(const Path::Normalized& name) const
    {
        for (auto it = mArchives.rbegin(); it != mArchives.rend(); ++it)
        {
            if ((*it)->contains(name))
                return (*it)->getDescription();
        }
        return {};
    }

    std::filesystem::file_time_type Manager::getLastModified(VFS::Path::NormalizedView name) const
    {
        const auto found = mIndex.find(name);
        if (found == mIndex.end())
            throw std::runtime_error("Resource '" + std::string(name.value()) + "' not found");
        return found->second->getLastModified();
    }

    std::uint64_t Manager::getSize(VFS::Path::NormalizedView name) const
    {
        const auto found = mIndex.find(name);
        if (found == mIndex.end())
            throw std::runtime_error("Resource '" + std::string(name.value()) + "' not found");
        return found->second->size();
    }

    void Manager::readRange(
        VFS::Path::NormalizedView name, std::uint64_t offset, std::size_t size, void* destination) const
    {
        const auto found = mIndex.find(name);
        if (found == mIndex.end())
            throw std::runtime_error("Resource '" + std::string(name.value()) + "' not found");
        found->second->readRange(offset, size, destination);
    }

    std::string Manager::getStem(VFS::Path::NormalizedView name) const
    {
        const auto found = mIndex.find(name);
        if (found == mIndex.end())
            throw std::runtime_error("Resource '" + std::string(name.value()) + "' not found");
        return found->second->getStem();
    }

    RecursiveDirectoryRange Manager::getRecursiveDirectoryIterator(std::string_view path) const
    {
        if (path.empty())
            return { mIndex.begin(), mIndex.end() };
        std::string normalized = Path::normalizeFilename(path);
        const auto it = mIndex.lower_bound(normalized);
        if (it == mIndex.end() || !it->first.view().starts_with(normalized))
            return { it, it };
        ++normalized.back();
        return { it, mIndex.lower_bound(normalized) };
    }

    RecursiveDirectoryRange Manager::getRecursiveDirectoryIterator(VFS::Path::NormalizedView path) const
    {
        if (path.value().empty())
            return { mIndex.begin(), mIndex.end() };
        const auto it = mIndex.lower_bound(path);
        if (it == mIndex.end() || !it->first.view().starts_with(path.value()))
            return { it, it };
        std::string copy(path.value());
        ++copy.back();
        return { it, mIndex.lower_bound(copy) };
    }

    RecursiveDirectoryRange Manager::getRecursiveDirectoryIterator() const
    {
        return { mIndex.begin(), mIndex.end() };
    }

    Files::IStreamPtr Manager::findNormalized(std::string_view normalizedPath) const
    {
        assert(Path::isNormalized(normalizedPath));
        const auto it = mIndex.find(normalizedPath);
        if (it == mIndex.end())
            return nullptr;
        return it->second->open();
    }
}

#include "collections.hpp"
#include "conversion.hpp"

#include <components/misc/strings/algorithm.hpp>

namespace Files
{
    bool isDataPackFile(const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
        {
            return false;
        }
        return Misc::StringUtils::ciEndsWith(pathToUnicodeString(path.filename()), ".edenpak");
    }

    bool isUsableDataEntry(const std::filesystem::path& path)
    {
        // An empty value counts, as it did when the launcher asked QDir("").exists() (the current folder)
        if (path.empty())
            return true;
        std::error_code error;
        return std::filesystem::is_directory(path, error) || isDataPackFile(path);
    }

    Collections::Collections()
        : mDirectories()
        , mCollections()
    {
    }

    Collections::Collections(const Files::PathContainer& directories)
        : mDirectories(directories)
        , mCollections()
    {
    }

    const MultiDirCollection& Collections::getCollection(std::string_view extension) const
    {
        auto iter = mCollections.find(extension);
        if (iter == mCollections.end())
        {
            auto result = mCollections.emplace(extension, MultiDirCollection(mDirectories, extension));

            iter = result.first;
        }

        return iter->second;
    }

    std::filesystem::path Collections::getPath(std::string_view file) const
    {
        for (auto iter = mDirectories.rbegin(); iter != mDirectories.rend(); iter++)
        {
            // A data pack (task 0064) is a file in the list, not a folder to search.
            if (std::filesystem::is_regular_file(*iter))
            {
                continue;
            }
            for (const auto& iter2 : std::filesystem::directory_iterator(*iter))
            {
                const auto& path = iter2.path();
                const auto str = Files::pathToUnicodeString(path.filename());

                if (Misc::StringUtils::ciEqual(file, str))
                    return path;
            }
        }

        throw std::runtime_error("file " + std::string(file) + " not found");
    }

    bool Collections::doesExist(std::string_view file) const
    {
        for (auto iter = mDirectories.rbegin(); iter != mDirectories.rend(); iter++)
        {
            // A data pack (task 0064) is a file in the list, not a folder to search.
            if (std::filesystem::is_regular_file(*iter))
            {
                continue;
            }
            for (const auto& iter2 : std::filesystem::directory_iterator(*iter))
            {
                const auto& path = iter2.path();
                const auto str = Files::pathToUnicodeString(path.filename());

                if (Misc::StringUtils::ciEqual(file, str))
                    return true;
            }
        }

        return false;
    }

    const Files::PathContainer& Collections::getPaths() const
    {
        return mDirectories;
    }
}

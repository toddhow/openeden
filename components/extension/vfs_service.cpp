#include "vfs_service.hpp"

#include <components/files/istreamptr.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>

namespace openeden::extension
{
    namespace
    {
        // This service keeps its own mount table of physical folders and does not use VFS::Manager, so it has its
        // own, smaller path rule: only the separator is unified, the case is kept because the mounts are read from
        // the real file system. Whether a path stays inside its mount is checked in hasParentComponent (task 0050).
        std::string normalizeVirtualPath(std::string_view path)
        {
            std::string normalized(path);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return normalized;
        }

        // True when a '/' separated path has a '..' component, which would leave the mount's folder.
        bool hasParentComponent(std::string_view path)
        {
            std::size_t start = 0;
            while (start <= path.size())
            {
                std::size_t end = path.find('/', start);
                if (end == std::string_view::npos)
                    end = path.size();
                if (path.substr(start, end - start) == "..")
                    return true;
                start = end + 1;
            }
            return false;
        }
    } // namespace

    void VfsServiceImpl::sortMounts()
    {
        std::stable_sort(mMounts.begin(), mMounts.end(),
            [](const MountPoint& a, const MountPoint& b) { return a.priority > b.priority; });
    }

    void VfsServiceImpl::setExtensionLoadOrder(const std::vector<std::string>& loadOrder)
    {
        mLoadOrder = loadOrder;

        // Re-evaluate priorities for extension data mounts based on load order
        for (auto& mount : mMounts)
        {
            if (!mount.extensionId.empty())
            {
                const auto it = std::find(mLoadOrder.begin(), mLoadOrder.end(), mount.extensionId);
                if (it != mLoadOrder.end())
                {
                    mount.priority = 100 + static_cast<int>(std::distance(mLoadOrder.begin(), it)) * 10;
                }
            }
        }
        sortMounts();
    }

    void VfsServiceImpl::mount(std::string_view virtualPrefix, const std::filesystem::path& physicalDir, int priority)
    {
        MountPoint mp;
        mp.virtualPrefix = normalizeVirtualPath(virtualPrefix);
        mp.physicalDir = physicalDir;
        mp.priority = priority;
        mMounts.push_back(std::move(mp));
        sortMounts();
    }

    void VfsServiceImpl::mountExtensionData(std::string_view extensionId, const std::filesystem::path& dataDir)
    {
        int priority = 10;
        const auto it = std::find(mLoadOrder.begin(), mLoadOrder.end(), extensionId);
        if (it != mLoadOrder.end())
        {
            priority = 100 + static_cast<int>(std::distance(mLoadOrder.begin(), it)) * 10;
        }

        MountPoint mp;
        mp.virtualPrefix = "data";
        mp.physicalDir = dataDir;
        mp.priority = priority;
        mp.extensionId = std::string(extensionId);
        mMounts.push_back(std::move(mp));
        sortMounts();
    }

    std::filesystem::path VfsServiceImpl::resolvePhysicalPath(std::string_view virtualPath) const
    {
        const std::string vpath = normalizeVirtualPath(virtualPath);

        // mMounts is kept pre-sorted descending by priority: zero heap allocation on hot path
        for (const auto& mount : mMounts)
        {
            std::string prefix = mount.virtualPrefix;
            if (!prefix.empty() && prefix.back() != '/')
            {
                prefix += '/';
            }

            std::string relativePath;
            if (vpath.rfind(prefix, 0) == 0)
            {
                relativePath = vpath.substr(prefix.size());
            }
            else if (vpath == mount.virtualPrefix)
            {
                relativePath = "";
            }
            else
            {
                continue;
            }

            // A path that climbs out of the mount's folder never resolves, whatever file is there.
            // On Windows 'dir / "/x"' replaces the folder with the drive's root, so a leading '/' is refused as well.
            // A symbolic link inside the mount's folder can still lead out of it; that is the folder owner's choice.
            const std::filesystem::path relative(relativePath);
            if (hasParentComponent(relativePath) || relativePath.starts_with('/') || relative.is_absolute()
                || relative.has_root_name() || relative.has_root_directory())
                continue;

            const auto candidate = mount.physicalDir / relativePath;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                return candidate;
            }
        }

        return {};
    }

    std::vector<DataOverride> findDataOverrides(const VFS::Manager& gameData, const std::vector<ExtensionDataDir>& dirs)
    {
        std::vector<DataOverride> overrides;
        std::map<std::string, std::string, std::less<>> extensionOwner; // Latest extension holding each path

        for (const ExtensionDataDir& dir : dirs)
        {
            std::vector<std::string> paths;
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator it(dir.directory, ec), end; !ec && it != end;
                 it.increment(ec))
            {
                if (it->is_regular_file(ec))
                {
                    std::string path = std::filesystem::relative(it->path(), dir.directory, ec).generic_string();
                    VFS::Path::normalizeFilenameInPlace(path);
                    paths.push_back(std::move(path));
                }
            }
            std::sort(paths.begin(), paths.end());
            paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

            for (std::string& path : paths)
            {
                const auto owner = extensionOwner.find(path);
                if (owner != extensionOwner.end())
                    overrides.push_back({ path, dir.extensionId, owner->second });
                else if (gameData.exists(VFS::Path::Normalized(path)))
                    overrides.push_back({ path, dir.extensionId, {} });
                extensionOwner[std::move(path)] = dir.extensionId;
            }
        }
        return overrides;
    }

    bool VfsServiceImpl::hasFile(std::string_view virtualPath) const
    {
        if (mEngineVfs != nullptr)
            return mEngineVfs->exists(VFS::Path::Normalized(virtualPath));

        return !resolvePhysicalPath(virtualPath).empty();
    }

    std::vector<std::uint8_t> VfsServiceImpl::readFile(std::string_view virtualPath) const
    {
        if (mEngineVfs != nullptr)
        {
            const VFS::Path::Normalized path(virtualPath);
            const Files::IStreamPtr stream = mEngineVfs->find(path);
            if (!stream)
                return {};
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>());
        }

        const auto physicalPath = resolvePhysicalPath(virtualPath);
        if (physicalPath.empty())
            return {};

        std::ifstream in(physicalPath, std::ios::binary | std::ios::ate);
        if (!in.is_open())
            return {};

        const auto size = in.tellg();
        if (size < 0)
            return {};

        if (size == 0)
            return {}; // 0-byte file successfully returns empty vector

        in.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(buffer.data()), size);
        if (in.gcount() != size)
        {
            std::cerr << "OpenEden VfsService: Warning: Incomplete read for " << physicalPath << '\n';
        }
        return buffer;
    }

    void VfsServiceImpl::registerFormatReader(
        std::string_view fileExtension, std::shared_ptr<openeden::sdk::FileFormatReader> reader)
    {
        std::string ext(fileExtension);
        if (!ext.empty() && ext.front() != '.')
        {
            ext = "." + ext;
        }
        if (!reader)
        {
            mFormatReaders.erase(ext);
        }
        else
        {
            mFormatReaders[ext] = std::move(reader);
        }
    }

    bool VfsServiceImpl::readAsset(std::string_view virtualPath, openeden::sdk::FileError* outError)
    {
        if (outError)
        {
            outError->file.clear();
            outError->line = 0;
            outError->field.clear();
            outError->message.clear();
        }

        if (!hasFile(virtualPath))
        {
            if (outError)
            {
                outError->file = std::string(virtualPath);
                outError->line = 1;
                outError->message = "File not found: " + std::string(virtualPath);
            }
            return false;
        }

        const std::string normPath = normalizeVirtualPath(virtualPath);

        // Find matching format reader by suffix (supports multi-dot file extensions like .opas.animgraph)
        std::shared_ptr<openeden::sdk::FileFormatReader> matchedReader;
        std::string matchedExt;
        std::size_t longestMatchLen = 0;

        for (const auto& [ext, reader] : mFormatReaders)
        {
            if (normPath.size() >= ext.size() && normPath.compare(normPath.size() - ext.size(), ext.size(), ext) == 0)
            {
                if (ext.size() > longestMatchLen)
                {
                    longestMatchLen = ext.size();
                    matchedExt = ext;
                    matchedReader = reader;
                }
            }
        }

        if (!matchedReader)
        {
            if (outError)
            {
                outError->file = std::string(virtualPath);
                outError->line = 1;
                outError->message = "No format reader registered for the file extension of '" + normPath + "'";
            }
            return false;
        }

        const auto bytes = readFile(virtualPath);
        try
        {
            return matchedReader->read(virtualPath, bytes, outError);
        }
        catch (const std::exception& e)
        {
            if (outError)
            {
                outError->file = std::string(virtualPath);
                outError->line = 1;
                outError->message = std::string("Exception in format reader for '") + matchedExt + "': " + e.what();
            }
            return false;
        }
        catch (...)
        {
            if (outError)
            {
                outError->file = std::string(virtualPath);
                outError->line = 1;
                outError->message = std::string("Unknown exception in format reader for '") + matchedExt + "'";
            }
            return false;
        }
    }

    void VfsServiceImpl::releaseExtensionObjects()
    {
        mFormatReaders.clear();
    }
} // namespace openeden::extension

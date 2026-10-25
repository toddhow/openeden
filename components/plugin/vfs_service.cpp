#include "vfs_service.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace openeden::plugin
{
    namespace
    {
        std::string normalizeVirtualPath(std::string_view path)
        {
            std::string normalized(path);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return normalized;
        }
    } // namespace

    void VfsServiceImpl::sortMounts()
    {
        std::stable_sort(mMounts.begin(), mMounts.end(),
            [](const MountPoint& a, const MountPoint& b) { return a.priority > b.priority; });
    }

    void VfsServiceImpl::setPluginLoadOrder(const std::vector<std::string>& loadOrder)
    {
        mLoadOrder = loadOrder;

        // Re-evaluate priorities for plugin data mounts based on load order
        for (auto& mount : mMounts)
        {
            if (!mount.pluginId.empty())
            {
                const auto it = std::find(mLoadOrder.begin(), mLoadOrder.end(), mount.pluginId);
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

    void VfsServiceImpl::mountPluginData(std::string_view pluginId, const std::filesystem::path& dataDir)
    {
        int priority = 10;
        const auto it = std::find(mLoadOrder.begin(), mLoadOrder.end(), pluginId);
        if (it != mLoadOrder.end())
        {
            priority = 100 + static_cast<int>(std::distance(mLoadOrder.begin(), it)) * 10;
        }

        MountPoint mp;
        mp.virtualPrefix = "data";
        mp.physicalDir = dataDir;
        mp.priority = priority;
        mp.pluginId = std::string(pluginId);
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

            const auto candidate = mount.physicalDir / relativePath;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                return candidate;
            }
        }

        return {};
    }

    bool VfsServiceImpl::hasFile(std::string_view virtualPath) const
    {
        return !resolvePhysicalPath(virtualPath).empty();
    }

    std::vector<std::uint8_t> VfsServiceImpl::readFile(std::string_view virtualPath) const
    {
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
        std::string_view extension, std::shared_ptr<openeden::sdk::FileFormatReader> reader)
    {
        std::string ext(extension);
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

        const auto physicalPath = resolvePhysicalPath(virtualPath);
        if (physicalPath.empty())
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

        // Find matching format reader by suffix (supports multi-dot extensions like .opas.animgraph)
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
                outError->message = "No format reader registered for extension of file '" + normPath + "'";
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
} // namespace openeden::plugin

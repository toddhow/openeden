#pragma once

#include <openeden/vfs.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VFS
{
    class Manager;
}

namespace openeden::plugin
{
    /**
     * @brief One plugin's folder of data files, as it is added to the game's VFS.
     */
    struct PluginDataDir
    {
        std::string pluginId;
        std::filesystem::path directory;
    };

    /**
     * @brief A plugin's file that hides one with the same VFS path.
     */
    struct DataOverride
    {
        std::string path; ///< Normalized VFS path
        std::string pluginId;
        std::string overriddenPluginId; ///< The earlier plugin it hides, or empty for the game's data
    };

    /**
     * @brief Lists the files in dirs, given in load order, that hide a file in gameData or in an earlier plugin.
     * Each path appears once per plugin that hides it, sorted within a plugin, so the result does not depend on the
     * file system's order. A directory that does not exist has no files.
     */
    [[nodiscard]] std::vector<DataOverride> findDataOverrides(
        const VFS::Manager& gameData, const std::vector<PluginDataDir>& dirs);

    /**
     * @brief Engine implementation of the virtual file system service.
     */
    class VfsServiceImpl : public openeden::sdk::VfsService
    {
    public:
        VfsServiceImpl() = default;
        ~VfsServiceImpl() override = default;

        // Non-copyable
        VfsServiceImpl(const VfsServiceImpl&) = delete;
        VfsServiceImpl& operator=(const VfsServiceImpl&) = delete;

        // Movable
        VfsServiceImpl(VfsServiceImpl&&) noexcept = default;
        VfsServiceImpl& operator=(VfsServiceImpl&&) noexcept = default;

        [[nodiscard]] bool hasFile(std::string_view virtualPath) const override;
        [[nodiscard]] std::vector<std::uint8_t> readFile(std::string_view virtualPath) const override;

        bool readAsset(std::string_view virtualPath, openeden::sdk::FileError* outError = nullptr) override;

        void registerFormatReader(
            std::string_view extension, std::shared_ptr<openeden::sdk::FileFormatReader> reader) override;

        /**
         * @brief Mounts a physical directory onto a virtual prefix with an explicit priority.
         */
        void mount(std::string_view virtualPrefix, const std::filesystem::path& physicalDir, int priority = 0);

        /**
         * @brief Mounts a plugin's data folder under virtual prefix "data", with priority from load order.
         */
        void mountPluginData(std::string_view pluginId, const std::filesystem::path& dataDir);

        /**
         * @brief Sets deterministic plugin load order for overlay priority resolution.
         */
        void setPluginLoadOrder(const std::vector<std::string>& loadOrder);

        /**
         * @brief Reads through the game's VFS instead of this service's own mounts. Null goes back to the mounts.
         *
         * Paths are then the game's, relative to a data folder ("meshes/x.nif"). The manager must stay alive and its
         * index built for as long as it is set.
         */
        void setEngineVfs(const VFS::Manager* vfs) noexcept { mEngineVfs = vfs; }

        /// Drops every object a plugin left here, while its library is still loaded.
        void releasePluginObjects();

    private:
        struct MountPoint
        {
            std::string virtualPrefix;
            std::filesystem::path physicalDir;
            int priority = 0;
            std::string pluginId;
        };

        void sortMounts();

        [[nodiscard]] std::filesystem::path resolvePhysicalPath(std::string_view virtualPath) const;

        const VFS::Manager* mEngineVfs = nullptr;
        std::vector<MountPoint> mMounts;
        std::vector<std::string> mLoadOrder;
        std::map<std::string, std::shared_ptr<openeden::sdk::FileFormatReader>, std::less<>> mFormatReaders;
    };
} // namespace openeden::plugin

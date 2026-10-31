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

namespace openeden::extension
{
    /**
     * @brief One extension's folder of data files, as it is added to the game's VFS.
     */
    struct ExtensionDataDir
    {
        std::string extensionId;
        std::filesystem::path directory;
    };

    /**
     * @brief An extension's file that hides one with the same VFS path.
     */
    struct DataOverride
    {
        std::string path; ///< Normalized VFS path
        std::string extensionId;
        std::string overriddenExtensionId; ///< The earlier extension it hides, or empty for the game's data
    };

    /**
     * @brief Lists the files in dirs, given in load order, that hide a file in gameData or in an earlier extension.
     * Each path appears once per extension that hides it, sorted within an extension, so the result does not depend on
     * the file system's order. A directory that does not exist has no files.
     */
    [[nodiscard]] std::vector<DataOverride> findDataOverrides(
        const VFS::Manager& gameData, const std::vector<ExtensionDataDir>& dirs);

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
            std::string_view fileExtension, std::shared_ptr<openeden::sdk::FileFormatReader> reader) override;

        /**
         * @brief Mounts a physical directory onto a virtual prefix with an explicit priority.
         */
        void mount(std::string_view virtualPrefix, const std::filesystem::path& physicalDir, int priority = 0);

        /**
         * @brief Mounts an extension's data folder under virtual prefix "data", with priority from load order.
         */
        void mountExtensionData(std::string_view extensionId, const std::filesystem::path& dataDir);

        /**
         * @brief Sets deterministic extension load order for overlay priority resolution.
         */
        void setExtensionLoadOrder(const std::vector<std::string>& loadOrder);

        /**
         * @brief Reads through the game's VFS instead of this service's own mounts. Null goes back to the mounts.
         *
         * Paths are then the game's, relative to a data folder ("meshes/x.nif"). The manager must stay alive and its
         * index built for as long as it is set.
         */
        void setEngineVfs(const VFS::Manager* vfs) noexcept { mEngineVfs = vfs; }

        /// Drops every object an extension left here, while its library is still loaded.
        void releaseExtensionObjects();

    private:
        struct MountPoint
        {
            std::string virtualPrefix;
            std::filesystem::path physicalDir;
            int priority = 0;
            std::string extensionId;
        };

        void sortMounts();

        [[nodiscard]] std::filesystem::path resolvePhysicalPath(std::string_view virtualPath) const;

        const VFS::Manager* mEngineVfs = nullptr;
        std::vector<MountPoint> mMounts;
        std::vector<std::string> mLoadOrder;
        std::map<std::string, std::shared_ptr<openeden::sdk::FileFormatReader>, std::less<>> mFormatReaders;
    };
} // namespace openeden::extension

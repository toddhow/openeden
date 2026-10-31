#pragma once

#include <openeden/vfs.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
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

        std::vector<MountPoint> mMounts;
        std::vector<std::string> mLoadOrder;
        std::map<std::string, std::shared_ptr<openeden::sdk::FileFormatReader>, std::less<>> mFormatReaders;
    };
} // namespace openeden::plugin

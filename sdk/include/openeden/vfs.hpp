#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::sdk
{
    /**
     * @brief Located error for asset and file reading failures.
     */
    struct FileError
    {
        std::string file;
        std::size_t line = 0;
        std::string field;
        std::string message;
    };

    /**
     * @brief Reader interface for extension-registered asset file formats.
     */
    class FileFormatReader
    {
    public:
        virtual ~FileFormatReader() = default;

        /**
         * @brief Parses asset bytes from virtual path, returning located errors without throwing.
         */
        virtual bool read(std::string_view path, std::span<const std::uint8_t> data, FileError* outError = nullptr) = 0;
    };

    /**
     * @brief Virtual file system service managing overlaid data mounts and format readers.
     */
    class VfsService
    {
    public:
        virtual ~VfsService() = default;

        [[nodiscard]] virtual bool hasFile(std::string_view virtualPath) const = 0;
        [[nodiscard]] virtual std::vector<std::uint8_t> readFile(std::string_view virtualPath) const = 0;
        virtual bool readAsset(std::string_view virtualPath, FileError* outError = nullptr) = 0;
        virtual void registerFormatReader(std::string_view fileExtension, std::shared_ptr<FileFormatReader> reader) = 0;
    };
} // namespace openeden::sdk

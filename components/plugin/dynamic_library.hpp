#pragma once

#include <filesystem>
#include <string>

namespace openeden::plugin
{
    /**
     * @brief Cross-platform RAII wrapper for dynamically loaded shared libraries (.dll / .so).
     */
    class DynamicLibrary
    {
    public:
        DynamicLibrary();
        DynamicLibrary(const std::filesystem::path& path, std::string& error);
        virtual ~DynamicLibrary();

        // Non-copyable
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;

        // Movable
        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        /**
         * @brief Loads the shared library at the given path.
         * @return True if successfully loaded; false on failure with error populated.
         */
        bool load(const std::filesystem::path& path, std::string& error);

        /**
         * @brief Unloads the library if currently loaded. Idempotent.
         */
        void unload();

        /**
         * @brief Checks if a library is currently loaded.
         */
        [[nodiscard]] bool isLoaded() const noexcept;

        /**
         * @brief Resolves an exported symbol by name.
         * @param name C-linkage symbol name
         * @param error Output error populated if symbol lookup fails
         * @return Function or data pointer, or nullptr on failure
         */
        [[nodiscard]] void* getSymbol(const std::string& name, std::string& error) const;

        /**
         * @brief Typed helper to resolve a function or data symbol.
         */
        template <typename Func>
        [[nodiscard]] Func getSymbol(const std::string& name, std::string& error) const
        {
            return reinterpret_cast<Func>(getSymbol(name, error));
        }

    private:
        void* mHandle = nullptr;
    };
} // namespace openeden::plugin

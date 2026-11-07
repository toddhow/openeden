#pragma once

#include <openeden/game/content.hpp>
#include <openeden/game/records.hpp>
#include <openeden/game/script_host.hpp>
#include <openeden/game/ui.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief The core's record type table, filled by the game pack (docs/design/game-packs.md section 5.1).
     * Main thread only. Owned by HostImpl.
     */
    class RecordServiceImpl final : public openeden::sdk::RecordService
    {
    public:
        [[nodiscard]] bool registerRecordType(const openeden::sdk::RecordTypeInfo& info) override;
        [[nodiscard]] const openeden::sdk::RecordTypeInfo* findRecordType(std::uint32_t typeId) const override;

        [[nodiscard]] std::size_t count() const noexcept { return mTypes.size(); }

        /// Forgets every type; the host calls it when the pack that registered them is gone.
        void clear() noexcept;

    private:
        struct Entry
        {
            std::string name; ///< Owns the text the info's string_view points at
            openeden::sdk::RecordTypeInfo info;
        };
        // std::map keeps node addresses stable, which findRecordType's contract promises
        std::map<std::uint32_t, Entry> mTypes;
    };

    /**
     * @brief Content loaders a game pack registered, asked in registration order. Does not own the loaders: they
     * belong to the pack, and the host calls clear() before the pack is destroyed. Main thread only.
     */
    class ContentServiceImpl final : public openeden::sdk::ContentService
    {
    public:
        void registerLoader(openeden::sdk::ContentLoader& loader) override;
        [[nodiscard]] openeden::sdk::ContentLoader* findLoader(
            const openeden::sdk::ContentHeader& header) const override;

        [[nodiscard]] std::size_t count() const noexcept { return mLoaders.size(); }
        void clear() noexcept { mLoaders.clear(); }

    private:
        std::vector<openeden::sdk::ContentLoader*> mLoaders; // non-owning: the pack owns its loaders
    };

    /**
     * @brief Which script languages the game pack declared. Main thread only.
     */
    class ScriptHostServiceImpl final : public openeden::sdk::ScriptHostService
    {
    public:
        [[nodiscard]] bool declareLanguage(std::string_view name) override;
        [[nodiscard]] bool isLanguageDeclared(std::string_view name) const override;
        void clear() noexcept { mLanguages.clear(); }

    private:
        std::set<std::string, std::less<>> mLanguages;
    };

    /**
     * @brief Which windows the game pack declared. Main thread only.
     */
    class UiServiceImpl final : public openeden::sdk::UiService
    {
    public:
        [[nodiscard]] bool declareWindow(std::string_view id, std::string_view title) override;
        [[nodiscard]] bool isWindowDeclared(std::string_view id) const override;
        void clear() noexcept { mWindows.clear(); }

    private:
        std::map<std::string, std::string, std::less<>> mWindows;
    };
} // namespace openeden::extension

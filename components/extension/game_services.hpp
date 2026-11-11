#pragma once

#include <openeden/game/content.hpp>
#include <openeden/game/records.hpp>
#include <openeden/game/script_host.hpp>
#include <openeden/game/ui.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/game/world_view.hpp>

#include "session_content.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
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

    /// The slot a game pack puts its read-only world view in (openeden/game/world_view.hpp). Holds a pointer; the pack
    /// withdraws it before it is destroyed. Main thread only.
    class WorldViewServiceImpl final : public openeden::sdk::game::WorldViewService
    {
    public:
        void registerWorldView(openeden::sdk::game::WorldView& view) override { mView = &view; }
        void withdrawWorldView(openeden::sdk::game::WorldView& view) override
        {
            if (mView == &view)
                mView = nullptr;
        }
        [[nodiscard]] openeden::sdk::game::WorldView* worldView() override { return mView; }

    private:
        openeden::sdk::game::WorldView* mView = nullptr;
    };

    /// Where game packs put their neutral world sources (openeden/game/world_source_service.hpp, task 0103). Holds
    /// pointers to the sources, which the packs own and withdraw; the game id of each is copied when it is registered,
    /// so that the listener can be told about a source that is gone. The listener is the host's side (the engine's
    /// neutral world store); with none set, sources are only recorded. Main thread only. Owned by HostImpl.
    class WorldSourceServiceImpl final : public openeden::sdk::game::WorldSourceService
    {
    public:
        /// The host's side of the service. The listener must outlive the service's use of it (set it to nullptr, or
        /// let the service go, before it is destroyed).
        class Listener
        {
        public:
            virtual ~Listener() = default;
            /// False with `error` to refuse the source; it is then not registered.
            virtual bool sourceRegistered(openeden::sdk::game::WorldCellSource& source, std::string& error) = 0;
            /// The game id the source was registered under. The source object may already be gone.
            virtual void sourceWithdrawn(std::string_view gameId) = 0;
        };

        [[nodiscard]] bool registerSource(openeden::sdk::game::WorldCellSource& source, std::string& error) override;
        void withdrawSource(openeden::sdk::game::WorldCellSource& source) override;
        [[nodiscard]] bool consumerEnabled() const override { return mConsumerEnabled; }

        /// Task 0107 (stage N3a-2b): the session's content files with their header facts, in load order, and the
        /// predicate that says which of them the session's game pack accepts (HostImpl points it at the registered
        /// content loaders). The service copies the entries.
        [[nodiscard]] std::size_t sessionContent(
            std::vector<openeden::sdk::game::SessionContentFile>& out) const override;
        void setSessionContent(std::vector<SessionContentEntry> entries) { mSessionContent = std::move(entries); }
        void setContentFilter(std::function<bool(const openeden::sdk::ContentHeader&)> accepts)
        {
            mContentFilter = std::move(accepts);
        }

        void setConsumerEnabled(bool enabled) noexcept { mConsumerEnabled = enabled; }
        void setListener(Listener* listener) noexcept { mListener = listener; }
        [[nodiscard]] std::size_t sourceCount() const noexcept { return mSources.size(); }

        /// Withdraws every source still registered (a pack that went without withdrawing), telling the listener by the
        /// game ids it copied. The host calls it before the packs' libraries are unloaded.
        void releaseAll();

    private:
        struct Entry
        {
            openeden::sdk::game::WorldCellSource* mSource = nullptr;
            std::string mGameId;
        };

        std::vector<Entry> mSources;
        std::vector<SessionContentEntry> mSessionContent;
        std::function<bool(const openeden::sdk::ContentHeader&)> mContentFilter;
        Listener* mListener = nullptr;
        bool mConsumerEnabled = false;
    };
} // namespace openeden::extension

#ifndef OPENEDEN_GAME_SKYRIMSE_PACK_CORE_H
#define OPENEDEN_GAME_SKYRIMSE_PACK_CORE_H

// The Skyrim SE game pack's logic without the plug-in glue (task 0086): which plugin headers are Skyrim SE's, which
// record types the pack recognises, a census that walks a plugin and counts records without decoding any, record
// inflation, and a listing of the TES4-family archives of the game. The glue (skyrimse_game_pack.cpp) only registers
// these with the host. Nothing here throws: failures are values that name the file and the offset.

#include <components/contentsource/plugin.hpp>

#include <openeden/game/content.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    inline constexpr std::string_view kPackId = "skyrimse";

    /// What the null rules say when a game is started (design doc game-packs.md 5.5, task 0086).
    inline constexpr std::string_view kRulesNotImplementedMessage = "Skyrim SE rules are not implemented";

    /// The gameTag this pack gives and accepts in an sdk::ContentHeader for the TES4 family. The core has no caller
    /// that fills a ContentHeader yet (task 0085 onwards), so the convention is the pack's own for now.
    inline constexpr std::string_view kContentTag = "TES4";

    /// True for the header of a Skyrim Special Edition plugin (Content::GameGuess::SkyrimSE: 24 byte record headers,
    /// form version 43 or 44, HEDR version about 1.7).
    [[nodiscard]] bool isSkyrimSeHeader(const Content::Header& header);

    [[nodiscard]] openeden::sdk::ContentHeader toContentHeader(const Content::Header& header);

    /// The default string table language (task 0086) and the longest one a setting may name.
    inline constexpr std::string_view kDefaultStringTableLanguage = "english";
    inline constexpr std::size_t kMaxStringTableLanguage = 32;
    /// The pack setting (`[extension.skyrimse]`) that names it.
    inline constexpr std::string_view kStringTableLanguageKey = "string table language";

    /// The string table language a setting asks for (`[extension.skyrimse] string table language`), made lower case.
    /// The value goes into a file path (strings/plugin_language.strings), so only ASCII letters, 2 to 32 of
    /// them, are accepted; anything else gives the default and `usedDefault` is true (the caller warns). An empty
    /// value is "not set" and also gives the default, with `usedDefault` false.
    [[nodiscard]] std::string sanitiseStringTableLanguage(std::string_view value, bool& usedDefault);

    /// The content probe the pack registers with the host's ContentService.
    class SkyrimContentLoader final : public openeden::sdk::ContentLoader
    {
    public:
        [[nodiscard]] bool probe(const openeden::sdk::ContentHeader& header) const override;
    };

    /// Skyrim SE's record types, sorted. From recall of the game's record list, verified against the installed
    /// Skyrim.esm and the DLC masters by the real-data check (an unknown type there is counted and listed).
    [[nodiscard]] std::span<const std::uint32_t> recognisedRecordTypes();
    [[nodiscard]] bool isRecognisedRecordType(std::uint32_t type);

    /// Counts what a plugin holds, record by record, decoding none of it.
    struct RecordCensus
    {
        std::map<std::uint32_t, std::uint64_t> mByType;
        std::map<std::uint32_t, std::uint64_t> mUnknownByType;
        std::uint64_t mRecords = 0;
        std::uint64_t mRecognised = 0;
        std::uint64_t mUnknown = 0;
        std::uint64_t mCompressed = 0;
        std::uint64_t mGroups = 0;
        /// Records the pack recognised and skipped (decoding is not implemented).
        std::uint64_t mSkipped = 0;
    };

    /// Opens the plugin, checks that it is Skyrim SE's and counts its records. False with `error` filled for a file
    /// that cannot be read, that is not Skyrim SE's, or that is damaged. `header` is filled when the header was read.
    [[nodiscard]] bool scanPlugin(
        const std::filesystem::path& path, RecordCensus& census, Content::Header& header, Content::Error& error);

    /// Same on bytes already in memory (the tests use this one); `name` is what errors call the file.
    [[nodiscard]] bool scanPluginBytes(const std::string& bytes, const std::string& name, RecordCensus& census,
        Content::Header& header, Content::Error& error);

    /// Inflates a compressed record (zlib behind a 4 byte size), checking the size against the prefix and the
    /// record's declared size. `record.mData` must hold the stored bytes. False with `error` filled otherwise.
    [[nodiscard]] bool inflateRecord(const Content::RawRecord& record, const std::string& fileName,
        std::vector<std::byte>& out, Content::Error& error);

    /// The id behind a localised string: the subrecord `subrecordType` of a record in a localised plugin holds four
    /// bytes, an index into the plugin's string tables, not text. `recordData` is the record's bytes after inflation.
    /// False if there is no such subrecord, it is not four bytes, or the data is cut short.
    [[nodiscard]] bool readLocalizedStringId(
        std::span<const std::byte> recordData, std::uint32_t subrecordType, std::uint32_t& id);

    /// What a TES4-family archive holds.
    struct ArchiveListing
    {
        std::uint32_t mVersion = 0;
        std::uint32_t mFlags = 0;
        std::vector<std::string> mFiles; ///< As the archive names them ("meshes\actors\...")
    };

    /// The BSA version of Skyrim Special Edition.
    inline constexpr std::uint32_t kArchiveVersion = 105;

    /// Lists a BSA of version 105. False for a file that is not one, with a message that starts with the file name.
    [[nodiscard]] bool readArchiveListing(
        const std::filesystem::path& path, ArchiveListing& listing, std::string& error);

    /// Reads one file out of the archive (LZ4 frames inside a version 105 BSA are decompressed).
    [[nodiscard]] bool readArchiveEntry(
        const std::filesystem::path& path, std::string_view name, std::string& bytes, std::string& error);
}

#endif

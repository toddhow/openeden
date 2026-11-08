#ifndef OPENEDEN_COMPONENTS_CONTENTSOURCE_PLUGIN_H
#define OPENEDEN_COMPONENTS_CONTENTSOURCE_PLUGIN_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Layer 1 of the record source (task 0083, docs/design/game-packs.md 5.1 and 5.2): reads a plugin's header and walks
// its records as (type, id, flags, bytes) without decoding them. It knows the two file families (TES3 and the TES4
// family) and no game rule; typed decoding is registered per game (decoderregistry.hpp). Nothing here throws: every
// failure is an Error value that names the file, the byte offset and the record type, and every size read from the file
// is checked against the bytes that remain before anything is allocated.
namespace Content
{
    constexpr std::uint32_t fourCC(const char (&name)[5])
    {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(name[0]))
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(name[1])) << 8)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(name[2])) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(name[3])) << 24);
    }

    /// The two file layouts. TES3 is Morrowind's; Tes4 is the layout of Oblivion, Fallout 3/NV, Skyrim and Fallout 4
    /// (records in nested GRUPs, 20 or 24 byte record headers).
    enum class Family
    {
        Tes3,
        Tes4,
    };

    /// A guess, made from facts in the header only (see guessGame in plugin.cpp for what was measured and what was
    /// not). Unknown is a normal answer.
    enum class GameGuess
    {
        Unknown,
        Morrowind,
        SkyrimSE,
        Fallout4,
        FalloutNV,
    };

    /// Record flag bits of the TES4 family. sRecMaster, sRecLocalized and sRecCompressed are the values of
    /// ESM4::RecordFlag in components/esm4/common.hpp. sRecLight (0x200) is NOT in that file: it was measured on the
    /// installed Skyrim SE plugins (set on the .esl files, clear on the .esm masters); it is the bit that REFR
    /// records use for "hidden", and it is only meaningful on the header record.
    constexpr std::uint32_t sRecMaster = 0x00000001;
    constexpr std::uint32_t sRecLocalized = 0x00000080;
    constexpr std::uint32_t sRecLight = 0x00000200;
    constexpr std::uint32_t sRecCompressed = 0x00040000;

    struct MasterFile
    {
        std::string mName;
        std::uint64_t mSize = 0;
    };

    struct Header
    {
        Family mFamily = Family::Tes3;
        GameGuess mGame = GameGuess::Unknown;
        /// HEDR's version number (a float in both families), as stored; a TES3 header with no HEDR has 1.3, the typed
        /// reader's default.
        float mVersion = 0.0f;
        /// The header record's own flag word. TES3: the `type` field of HEDR (0 esp, 1 esm, 32 ess), TES4 family: the
        /// record flags.
        std::uint32_t mFlags = 0;
        /// TES4 family, 24 byte headers only: the form version in the TES4 record header (0 otherwise).
        std::uint16_t mFormVersion = 0;
        /// 16 for TES3; 20 or 24 for the TES4 family. Groups have the same size as records.
        std::uint32_t mRecordHeaderSize = 0;
        /// The record count HEDR declares. It is not trusted: iteration counts what is really there.
        std::uint32_t mDeclaredRecords = 0;
        std::string mAuthor;
        std::string mDescription;
        std::vector<MasterFile> mMasters;
        /// Offset of the first record after the header record.
        std::uint64_t mFirstRecordOffset = 0;

        bool isMaster() const;
        bool isLight() const;
        bool isLocalized() const;
    };

    struct Error
    {
        std::string mFile;
        std::uint64_t mOffset = 0;
        /// The FourCC of the record being read, 0 if none.
        std::uint32_t mRecordType = 0;
        std::string mMessage;

        /// "file: offset 0x...: record XXXX: message"
        std::string describe() const;
    };

    struct RawRecord
    {
        std::uint32_t mType = 0;
        /// TES4 family: the form id as stored (the master index is its top byte). TES3: 0.
        std::uint32_t mFormId = 0;
        /// TES3: the record flags word. TES4: the record flags.
        std::uint32_t mFlags = 0;
        /// Offset of the record header in the file.
        std::uint64_t mOffset = 0;
        /// Size of the data as stored in the file (for a compressed record it includes the 4 byte size prefix).
        std::uint64_t mDataSize = 0;
        /// A compressed record (TES4 family, sRecCompressed): the size its data has when inflated, read from the
        /// prefix. 0 otherwise. Layer 1 does not inflate.
        std::uint32_t mUncompressedSize = 0;
        /// TES3 only: the text of the record's first subrecord when that is NAME (the record's id for most types), else
        /// empty. Points into mData.
        std::string_view mId;
        /// The stored bytes after the record header. Valid only inside the visitor call; empty when the walk was
        /// asked not to read data.
        std::span<const std::byte> mData;

        bool isCompressed() const { return (mFlags & sRecCompressed) != 0; }
    };

    /// A group of the TES4 family (a GRUP): a header with a label and a type, then its children.
    struct GroupInfo
    {
        /// The group's label as stored: a record type, a form id, a block number or a grid, by mType.
        std::uint32_t mLabel = 0;
        /// 0 top (label is a record type), 1 world children, 2 interior cell block, 3 interior sub-block, 4 exterior
        /// cell block, 5 exterior sub-block, 6 cell children, 7 topic children, 8 persistent, 9 temporary, 10 visible
        /// distant (ESM4::GroupType, components/esm4/grouptype.hpp).
        std::int32_t mType = 0;
        std::uint64_t mOffset = 0;
        /// The group's size including its header, as stored.
        std::uint64_t mSize = 0;
        /// 0 for a group at the top of the file.
        std::uint32_t mDepth = 0;
    };

    /// Called in file order. Return false to stop the walk early (that is not an error).
    class Visitor
    {
    public:
        virtual ~Visitor() = default;
        virtual bool onGroupBegin(const GroupInfo& group) { return true; }
        virtual bool onGroupEnd(const GroupInfo& group) { return true; }
        virtual bool onRecord(const RawRecord& record) = 0;
    };

    /// Bounds on what a file may ask for. A file that asks for more is refused with an Error.
    struct Limits
    {
        /// The header record, masters and all.
        std::uint64_t mMaxHeaderRecord = 64ull * 1024 * 1024;
        /// One record's stored data.
        std::uint64_t mMaxRecordData = 512ull * 1024 * 1024;
        /// The size a compressed record claims to have when inflated.
        std::uint64_t mMaxUncompressed = 512ull * 1024 * 1024;
        std::uint32_t mMaxMasters = 1024;
        std::uint32_t mMaxGroupDepth = 64;
    };

    struct WalkOptions
    {
        /// false: records are reported with their header facts and no bytes (the stream is not read for them).
        bool mReadData = true;
    };

    /// Which layout a file has, from its first four bytes only (the probe the game's loader always made before it
    /// handed the file to a typed reader). It does not parse the header, so a file whose header record is odd (a short
    /// HEDR, many masters) still answers: the typed reader decides what to do with it. Rewinds the stream first.
    /// Returns nothing for fewer than four bytes or a magic that is neither TES3 nor TES4.
    std::optional<Family> probeFamily(std::istream& stream);

    class Plugin
    {
    public:
        /// Opens the plugin and reads its header. On failure returns nullptr and fills `error`.
        static std::unique_ptr<Plugin> open(
            std::unique_ptr<std::istream> stream, std::string name, Error& error, const Limits& limits = Limits());
        static std::unique_ptr<Plugin> openFile(
            const std::filesystem::path& path, Error& error, const Limits& limits = Limits());

        const Header& header() const { return mHeader; }
        const std::string& name() const { return mName; }
        std::uint64_t fileSize() const { return mFileSize; }

        /// Walks every record after the header in file order. Returns the error that ended the walk, if any.
        std::optional<Error> walk(Visitor& visitor, const WalkOptions& options = WalkOptions()) const;

    private:
        Plugin() = default;

        std::optional<Error> walkTes3(Visitor& visitor, const WalkOptions& options) const;
        std::optional<Error> walkTes4(Visitor& visitor, const WalkOptions& options) const;

        // Owned by the plugin; the reads move the stream's position, so walk() is not safe to call from two threads.
        mutable std::unique_ptr<std::istream> mStream;
        std::string mName;
        Header mHeader;
        Limits mLimits;
        std::uint64_t mFileSize = 0;
    };

    /// What a file holds, by counting: used by the tools and tests. Ordered by type so output is deterministic.
    struct Summary
    {
        std::map<std::uint32_t, std::uint64_t> mRecordsByType;
        std::map<std::int32_t, std::uint64_t> mGroupsByType;
        std::uint64_t mRecords = 0;
        std::uint64_t mGroups = 0;
        std::uint64_t mCompressed = 0;
        std::uint64_t mDeleted = 0;
        std::uint32_t mMaxGroupDepth = 0;
    };

    /// Walks the plugin without reading record data. Returns the error that ended the walk, if any (the counts so far
    /// stay in `summary`).
    std::optional<Error> summarise(const Plugin& plugin, Summary& summary);

    std::string fourCCToString(std::uint32_t type);
}

#endif

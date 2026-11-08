#include "game_skyrimse/pack_core.hpp"

#include <components/bsa/compressedbsafile.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

namespace game_skyrimse
{
    namespace
    {
        constexpr std::uint32_t sFormVersionSkyrimSe = 44;
        constexpr std::uint32_t sFormVersionSkyrimSeOld = 43;

        // Sizes read from a hostile file are checked against these before anything is allocated.
        constexpr std::uint64_t sMaxInflatedRecord = 512ull * 1024 * 1024;

        // Skyrim Special Edition's record types. Written from recall of the game's record list; the real-data check
        // (testskyrimserealdata.cpp) prints every type it finds that is not here.
        constexpr std::array sTypeNames = { "TES4", "GRUP", "AACT", "ACHR", "ACTI", "ADDN", "ALCH", "AMMO", "ANIO",
            "APPA", "ARMA", "ARMO", "ARTO", "ASPC", "ASTP", "AVIF", "BOOK", "BPTD", "CAMS", "CELL", "CLAS", "CLFM",
            "CLMT", "COBJ", "COLL", "CONT", "CPTH", "CSTY", "DEBR", "DIAL", "DLBR", "DLVW", "DOBJ", "DOOR", "DUAL",
            "ECZN", "EFSH", "ENCH", "EQUP", "EXPL", "EYES", "FACT", "FLOR", "FLST", "FSTP", "FSTS", "FURN", "GLOB",
            "GMST", "GRAS", "HAZD", "HDPT", "IDLE", "IDLM", "IMAD", "IMGS", "INFO", "INGR", "IPCT", "IPDS", "KEYM",
            "KYWD", "LAND", "LCRT", "LCTN", "LGTM", "LIGH", "LSCR", "LTEX", "LVLI", "LVLN", "LVSP", "MATO", "MATT",
            "MESG", "MGEF", "MISC", "MOVT", "MSTT", "MUSC", "MUST", "NAVI", "NAVM", "NOTE", "NPC_", "OTFT", "PACK",
            "PERK", "PGRE", "PHZD", "PROJ", "QUST", "RACE", "REFR", "REGN", "RELA", "REVB", "RFCT", "SCEN", "SCRL",
            "SHOU", "SLGM", "SMBN", "SMEN", "SMQN", "SNCT", "SNDR", "SOPM", "SOUN", "SPEL", "SPGD", "STAT", "TACT",
            "TREE", "TXST", "VOLI", "VTYP", "WATR", "WEAP", "WOOP", "WRLD", "WTHR" };

        std::uint32_t idOf(const char* name)
        {
            std::uint32_t id = 0;
            std::memcpy(&id, name, 4);
            return id;
        }

        const std::vector<std::uint32_t>& typeTable()
        {
            static const std::vector<std::uint32_t> table = [] {
                std::vector<std::uint32_t> ids;
                for (const char* name : sTypeNames)
                    ids.push_back(idOf(name));
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
                return ids;
            }();
            return table;
        }

        class CensusVisitor final : public Content::Visitor
        {
        public:
            explicit CensusVisitor(RecordCensus& census)
                : mCensus(census)
            {
            }

            bool onGroupBegin(const Content::GroupInfo&) override
            {
                ++mCensus.mGroups;
                return true;
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                ++mCensus.mRecords;
                ++mCensus.mByType[record.mType];
                if (record.isCompressed())
                    ++mCensus.mCompressed;
                if (isRecognisedRecordType(record.mType))
                {
                    ++mCensus.mRecognised;
                    // Recognised and skipped: no Skyrim record decoder exists in this pack (task 0086 scope).
                    ++mCensus.mSkipped;
                }
                else
                {
                    ++mCensus.mUnknown;
                    ++mCensus.mUnknownByType[record.mType];
                }
                return true;
            }

        private:
            RecordCensus& mCensus;
        };

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        bool scanOpened(
            const Content::Plugin& plugin, RecordCensus& census, Content::Header& header, Content::Error& error)
        {
            header = plugin.header();
            if (!isSkyrimSeHeader(header))
            {
                error = makeError(plugin.name(), 0, 0,
                    header.mFamily == Content::Family::Tes3
                        ? "this is a Morrowind plugin, not a Skyrim Special Edition one"
                        : "this plugin is not a Skyrim Special Edition one (form version "
                            + std::to_string(header.mFormVersion) + ", record header "
                            + std::to_string(header.mRecordHeaderSize) + " bytes)");
                return false;
            }
            census = RecordCensus{};
            CensusVisitor visitor(census);
            Content::WalkOptions options;
            options.mReadData = false;
            if (const std::optional<Content::Error> walkError = plugin.walk(visitor, options))
            {
                error = *walkError;
                return false;
            }
            return true;
        }

        std::string baseName(const std::filesystem::path& path)
        {
            return path.filename().string();
        }

        std::string lowerSlashes(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
            {
                if (c == '/')
                    c = '\\';
                else
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        // The BSA header is 36 bytes; the counts and offsets in it are checked against the file size before the
        // reader (which reserves from the counts) sees them.
        bool checkArchiveHeader(const std::filesystem::path& path, ArchiveListing& listing, std::string& error)
        {
            const std::string name = baseName(path);
            std::error_code ec;
            const std::uint64_t size = std::filesystem::file_size(path, ec);
            if (ec)
            {
                error = name + ": cannot open the file";
                return false;
            }
            std::ifstream file(path, std::ios::binary);
            std::array<std::uint32_t, 9> h{};
            file.read(reinterpret_cast<char*>(h.data()), sizeof(h));
            if (!file || size < sizeof(h))
            {
                error = name + ": too small to be a BSA archive (" + std::to_string(size) + " bytes)";
                return false;
            }
            if (std::memcmp(h.data(), "BSA\0", 4) != 0)
            {
                error = name + ": not a TES4-family BSA archive (the magic is wrong)";
                return false;
            }
            if (h[1] != kArchiveVersion)
            {
                error = name + ": BSA version " + std::to_string(h[1]) + " is not Skyrim Special Edition's ("
                    + std::to_string(kArchiveVersion) + ")";
                return false;
            }
            const std::uint64_t folderRecords = static_cast<std::uint64_t>(h[4]) * 24;
            const std::uint64_t fileRecords = static_cast<std::uint64_t>(h[5]) * 16;
            if (h[2] > size || folderRecords > size || fileRecords > size || h[6] > size || h[7] > size
                || h[2] + folderRecords + fileRecords + h[6] + h[7] > size)
            {
                error = name + ": the header counts more folders, files or names than the file holds";
                return false;
            }
            listing.mVersion = h[1];
            listing.mFlags = h[3];
            return true;
        }
    }

    bool isSkyrimSeHeader(const Content::Header& header)
    {
        return header.mGame == Content::GameGuess::SkyrimSE;
    }

    openeden::sdk::ContentHeader toContentHeader(const Content::Header& header)
    {
        if (header.mFamily == Content::Family::Tes4)
            return { kContentTag, header.mFormVersion };
        return { "TES3", 0 };
    }

    bool SkyrimContentLoader::probe(const openeden::sdk::ContentHeader& header) const
    {
        return header.gameTag == kContentTag
            && (header.formatVersion == sFormVersionSkyrimSe || header.formatVersion == sFormVersionSkyrimSeOld);
    }

    std::span<const std::uint32_t> recognisedRecordTypes()
    {
        return typeTable();
    }

    bool isRecognisedRecordType(std::uint32_t type)
    {
        const std::vector<std::uint32_t>& table = typeTable();
        return std::binary_search(table.begin(), table.end(), type);
    }

    bool scanPlugin(
        const std::filesystem::path& path, RecordCensus& census, Content::Header& header, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, error);
        if (plugin == nullptr)
            return false;
        return scanOpened(*plugin, census, header, error);
    }

    bool scanPluginBytes(const std::string& bytes, const std::string& name, RecordCensus& census,
        Content::Header& header, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        return scanOpened(*plugin, census, header, error);
    }

    bool inflateRecord(const Content::RawRecord& record, const std::string& fileName, std::vector<std::byte>& out,
        Content::Error& error)
    {
        const auto fail = [&](std::string message) {
            error = makeError(fileName, record.mOffset, record.mType, std::move(message));
            return false;
        };
        if (record.mData.size() < 4)
            return fail("a compressed record is too short to hold its size prefix");
        std::uint32_t declared = 0;
        std::memcpy(&declared, record.mData.data(), 4);
        if (record.mUncompressedSize != 0 && declared != record.mUncompressedSize)
            return fail("the size prefix says " + std::to_string(declared) + " bytes but the record says "
                + std::to_string(record.mUncompressedSize));
        if (declared > sMaxInflatedRecord)
            return fail("a compressed record claims " + std::to_string(declared) + " bytes when inflated");
        out.assign(declared, std::byte{ 0 });
        z_stream stream{};
        if (inflateInit(&stream) != Z_OK)
            return fail("zlib could not start");
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(record.mData.data())) + 4;
        stream.avail_in = static_cast<uInt>(record.mData.size() - 4);
        stream.next_out = reinterpret_cast<Bytef*>(out.data());
        stream.avail_out = static_cast<uInt>(declared);
        const int result = inflate(&stream, Z_FINISH);
        const uLong produced = stream.total_out;
        inflateEnd(&stream);
        if (result != Z_STREAM_END)
            return fail("the compressed data is damaged or cut short (zlib code " + std::to_string(result) + ")");
        if (produced != declared)
            return fail("the compressed data inflated to " + std::to_string(produced) + " bytes, not "
                + std::to_string(declared));
        return true;
    }

    bool readLocalizedStringId(std::span<const std::byte> recordData, std::uint32_t subrecordType, std::uint32_t& id)
    {
        constexpr std::uint32_t sXxxx = 0x58585858; // "XXXX": the next subrecord's size is in 32 bits
        std::size_t pos = 0;
        std::uint64_t overrideSize = 0;
        while (recordData.size() - pos >= 6)
        {
            std::uint32_t type = 0;
            std::uint16_t size16 = 0;
            std::memcpy(&type, &recordData[pos], 4);
            std::memcpy(&size16, &recordData[pos + 4], 2);
            pos += 6;
            std::uint64_t size = size16;
            if (overrideSize != 0)
            {
                size = overrideSize;
                overrideSize = 0;
            }
            if (size > recordData.size() - pos)
                return false;
            if (type == sXxxx)
            {
                if (size != 4)
                    return false;
                std::uint32_t big = 0;
                std::memcpy(&big, &recordData[pos], 4);
                overrideSize = big;
            }
            else if (type == subrecordType)
            {
                if (size != 4)
                    return false;
                std::memcpy(&id, &recordData[pos], 4);
                return true;
            }
            pos += static_cast<std::size_t>(size);
        }
        return false;
    }

    bool readArchiveListing(const std::filesystem::path& path, ArchiveListing& listing, std::string& error)
    {
        listing = ArchiveListing{};
        if (!checkArchiveHeader(path, listing, error))
            return false;
        try
        {
            Bsa::CompressedBSAFile archive;
            archive.open(path);
            for (const Bsa::BSAFile::FileStruct& file : archive.getList())
                listing.mFiles.emplace_back(file.name());
            return true;
        }
        catch (const std::exception& e)
        {
            error = baseName(path) + ": " + e.what();
            return false;
        }
    }

    bool readArchiveEntry(
        const std::filesystem::path& path, std::string_view name, std::string& bytes, std::string& error)
    {
        ArchiveListing header;
        if (!checkArchiveHeader(path, header, error))
            return false;
        try
        {
            Bsa::CompressedBSAFile archive;
            archive.open(path);
            const std::string wanted = lowerSlashes(name);
            for (const Bsa::BSAFile::FileStruct& file : archive.getList())
            {
                if (lowerSlashes(file.name()) != wanted)
                    continue;
                const Files::IStreamPtr stream = archive.getFile(&file);
                std::ostringstream all;
                all << stream->rdbuf();
                bytes = all.str();
                return true;
            }
            error = baseName(path) + ": no file '" + std::string(name) + "' in the archive";
            return false;
        }
        catch (const std::exception& e)
        {
            error = baseName(path) + ": " + e.what();
            return false;
        }
    }
}

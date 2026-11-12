#include "game_skyrimse/records.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>
#include <unordered_map>

// Layout status of every subrecord read here (see records.hpp). VERIFIED means the installed Skyrim SE plugins were
// checked by tools/skyrimse_records_check (aggregates, no game text kept); the results are in the task 0086 stage log.
// Everything not marked is RECALLED-VERIFY: UESP / TES5Edit documentation from memory, cross-read with the cc9cii
// readers in components/esm4 where one exists. CONFIRMED means a second reader agrees, OpenMW upstream's ESM4 code at
// 97722d48f9 (itself written from UESP / TES5Edit, so corroboration and not independent proof; research note 0051
// section 4); it does not retire a marker for a value no tool of ours measured, and where upstream skips a field it
// says nothing (NPC_ DNAM, light masters stay RECALLED-VERIFY).

namespace game_skyrimse
{
    // ---- load order and form ids ---------------------------------------------------------------------------------

    namespace
    {
        std::string lowerCase(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        template <class T>
        T readLe(const std::byte* p)
        {
            T value;
            std::memcpy(&value, p, sizeof(T));
            return value;
        }
    }

    bool LoadOrder::add(std::string name, bool light, std::string& error)
    {
        const std::string key = lowerCase(name);
        if (mEntries.count(key) != 0)
        {
            error = name + ": the plugin is in the load order twice";
            return false;
        }
        Entry entry;
        entry.mLight = light;
        if (light)
        {
            if (mLight >= kMaxLight)
            {
                error = name + ": more than " + std::to_string(kMaxLight) + " light plugins";
                return false;
            }
            entry.mPrefix = 0xFE000000u | (static_cast<FormId>(mLight) << 12);
            ++mLight;
        }
        else
        {
            if (mNormal >= kMaxNormal)
            {
                error = name + ": more than " + std::to_string(kMaxNormal) + " full plugins";
                return false;
            }
            entry.mPrefix = static_cast<FormId>(mNormal) << 24;
            ++mNormal;
        }
        mEntries.emplace(key, entry);
        return true;
    }

    bool LoadOrder::contains(std::string_view name) const
    {
        return mEntries.count(lowerCase(name)) != 0;
    }

    std::optional<FormId> LoadOrder::prefixOf(std::string_view name) const
    {
        const auto it = mEntries.find(lowerCase(name));
        if (it == mEntries.end())
            return std::nullopt;
        return it->second.mPrefix;
    }

    bool LoadOrder::isLight(std::string_view name) const
    {
        const auto it = mEntries.find(lowerCase(name));
        return it != mEntries.end() && it->second.mLight;
    }

    std::optional<std::string> LoadOrder::nameOfId(FormId id) const
    {
        const bool light = (id >> 24) == 0xFEu;
        const FormId prefix = light ? (id & 0xFFFFF000u) : (id & 0xFF000000u);
        for (const auto& [name, entry] : mEntries)
            if (entry.mLight == light && entry.mPrefix == prefix)
                return name;
        return std::nullopt;
    }

    FormIdResolver::FormIdResolver(const Content::Header& header, std::string selfName, const LoadOrder& order)
    {
        for (const Content::MasterFile& master : header.mMasters)
        {
            mMasterPrefixes.push_back(order.prefixOf(master.mName));
            mMasterLight.push_back(order.isLight(master.mName));
        }
        mSelfPrefix = order.prefixOf(selfName);
        mSelfLight = order.isLight(selfName);
    }

    std::optional<FormId> FormIdResolver::resolve(std::uint32_t stored) const
    {
        if (stored == 0)
            return kNullFormId;
        const std::uint32_t index = stored >> 24;
        std::optional<FormId> prefix = mSelfPrefix;
        bool light = mSelfLight;
        if (index < mMasterPrefixes.size())
        {
            prefix = mMasterPrefixes[index];
            light = mMasterLight[index];
        }
        if (!prefix)
            return std::nullopt;
        // A light plugin has 12 bits of record id (UESL range 0x800 to 0xFFF); RECALLED-VERIFY, checked by the tool.
        return light ? (*prefix | (stored & 0xFFFu)) : (*prefix | (stored & 0xFFFFFFu));
    }

    std::optional<std::uint32_t> FormIdResolver::ownStoredId(FormId id) const
    {
        if (!mSelfPrefix || mSelfLight || id == kNullFormId)
            return std::nullopt;
        if ((id & 0xFF000000u) != *mSelfPrefix)
            return std::nullopt;
        return (static_cast<std::uint32_t>(mMasterPrefixes.size()) << 24) | (id & 0xFFFFFFu);
    }

    // ---- string tables -------------------------------------------------------------------------------------------

    namespace
    {
        constexpr std::uint64_t sMaxStrings = 4ull * 1024 * 1024;
        constexpr std::uint32_t sMaxTableString = 1u << 20;
    }

    bool StringTable::parse(std::span<const std::byte> bytes, StringTableKind kind, const std::string& name,
        StringTable& out, Content::Error& error)
    {
        out.mStrings.clear();
        const auto fail = [&](std::uint64_t offset, std::string message) {
            error = makeError(name, offset, 0, std::move(message));
            out.mStrings.clear();
            return false;
        };
        if (bytes.size() < 8)
            return fail(0, "a string table needs at least 8 bytes, this one has " + std::to_string(bytes.size()));
        const std::uint64_t count = readLe<std::uint32_t>(bytes.data());
        const std::uint64_t dataSize = readLe<std::uint32_t>(bytes.data() + 4);
        if (count > sMaxStrings)
            return fail(0, "the table claims " + std::to_string(count) + " strings, over the limit");
        const std::uint64_t directoryEnd = 8 + count * 8;
        if (directoryEnd > bytes.size())
            return fail(0, "the directory of " + std::to_string(count) + " strings runs past the end of the file");
        if (dataSize > bytes.size() - directoryEnd)
            return fail(4,
                "the table claims " + std::to_string(dataSize) + " bytes of text but "
                    + std::to_string(bytes.size() - directoryEnd) + " remain");
        const std::byte* data = bytes.data() + directoryEnd;
        for (std::uint64_t i = 0; i < count; ++i)
        {
            const std::uint32_t id = readLe<std::uint32_t>(bytes.data() + 8 + i * 8);
            const std::uint64_t offset = readLe<std::uint32_t>(bytes.data() + 12 + i * 8);
            if (offset >= dataSize)
                return fail(8 + i * 8,
                    "string id " + std::to_string(id) + " points at offset " + std::to_string(offset) + " outside the "
                        + std::to_string(dataSize) + " bytes of text");
            std::string text;
            if (kind == StringTableKind::Strings)
            {
                const char* begin = reinterpret_cast<const char*>(data + offset);
                const void* nul = std::memchr(begin, '\0', static_cast<std::size_t>(dataSize - offset));
                if (nul == nullptr)
                    return fail(directoryEnd + offset, "string id " + std::to_string(id) + " has no terminating zero");
                text.assign(begin, static_cast<const char*>(nul));
            }
            else
            {
                if (dataSize - offset < 4)
                    return fail(directoryEnd + offset, "string id " + std::to_string(id) + " has no length prefix");
                const std::uint64_t length = readLe<std::uint32_t>(data + offset);
                if (length > sMaxTableString || length > dataSize - offset - 4)
                    return fail(directoryEnd + offset,
                        "string id " + std::to_string(id) + " claims " + std::to_string(length)
                            + " bytes past the end of the text");
                const char* begin = reinterpret_cast<const char*>(data + offset + 4);
                std::size_t n = static_cast<std::size_t>(length);
                if (n > 0 && begin[n - 1] == '\0')
                    --n; // the stored length counts the terminating zero
                text.assign(begin, n);
            }
            out.mStrings.emplace(id, std::move(text)); // the first of a repeated id wins
        }
        return true;
    }

    const std::string* StringTable::find(std::uint32_t id) const
    {
        const auto it = mStrings.find(id);
        return it == mStrings.end() ? nullptr : &it->second;
    }

    std::string stringTablePath(std::string_view pluginName, std::string_view language, StringTableKind kind)
    {
        std::string base = lowerCase(pluginName);
        const std::size_t dot = base.rfind('.');
        if (dot != std::string::npos)
            base.resize(dot);
        const char* extension = kind == StringTableKind::Strings ? "strings"
            : kind == StringTableKind::DlStrings                 ? "dlstrings"
                                                                 : "ilstrings";
        return "strings\\" + base + "_" + lowerCase(language) + "." + extension;
    }

    bool lookupLString(LString& value, const StringTable& table)
    {
        if (!value.mPresent || !value.mLocalized)
            return false;
        const std::string* text = table.find(value.mId);
        if (text == nullptr)
            return false;
        value.mText = *text;
        return true;
    }

    const CellRecord* ParsedPlugin::findCell(FormId id) const
    {
        for (const CellRecord& cell : mCells)
            if (cell.mCommon.mId == id)
                return &cell;
        return nullptr;
    }

    // ---- the parse -----------------------------------------------------------------------------------------------

    namespace
    {
        constexpr std::uint32_t sXxxx = Content::fourCC("XXXX");

        std::string tag(std::uint32_t type)
        {
            return Content::fourCCToString(type);
        }

        /// Walks the subrecords of one record's (inflated) bytes. XXXX carries the 32 bit size of the next one.
        class SubReader
        {
        public:
            explicit SubReader(std::span<const std::byte> data)
                : mData(data)
            {
            }

            /// 1: a subrecord, 0: the end, -1: damaged (message()).
            int next(std::uint32_t& type, std::span<const std::byte>& data)
            {
                for (;;)
                {
                    if (mPos == mData.size())
                        return 0;
                    if (mData.size() - mPos < 6)
                        return fail("a subrecord header is cut short at byte " + std::to_string(mPos));
                    type = readLe<std::uint32_t>(mData.data() + mPos);
                    std::uint64_t size = readLe<std::uint16_t>(mData.data() + mPos + 4);
                    mPos += 6;
                    if (mHasOverride)
                    {
                        size = mOverride;
                        mHasOverride = false;
                    }
                    if (size > mData.size() - mPos)
                        return fail("subrecord " + tag(type) + " claims " + std::to_string(size) + " bytes but "
                            + std::to_string(mData.size() - mPos) + " remain in the record");
                    if (type == sXxxx)
                    {
                        if (size != 4)
                            return fail("an XXXX subrecord of " + std::to_string(size) + " bytes, not 4");
                        mOverride = readLe<std::uint32_t>(mData.data() + mPos);
                        mHasOverride = true;
                        mPos += 4;
                        continue;
                    }
                    data = mData.subspan(mPos, static_cast<std::size_t>(size));
                    mPos += static_cast<std::size_t>(size);
                    return 1;
                }
            }

            const std::string& message() const { return mMessage; }

        private:
            int fail(std::string message)
            {
                mMessage = std::move(message);
                return -1;
            }

            std::span<const std::byte> mData;
            std::size_t mPos = 0;
            std::uint64_t mOverride = 0;
            bool mHasOverride = false;
            std::string mMessage;
        };

        class Parser final : public Content::Visitor
        {
        public:
            Parser(const Content::Plugin& plugin, const FormIdResolver& resolver, const ParseLimits& limits,
                ParsedPlugin& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onGroupBegin(const Content::GroupInfo& group) override
            {
                mGroups.push_back(group);
                // The cell-children group follows its cell record: remember where it is (lazy cells).
                if (group.mType == 6)
                {
                    const auto it = mCellByStoredId.find(group.mLabel);
                    if (it != mCellByStoredId.end())
                    {
                        CellRecord& cell = mOut.mCells[it->second];
                        if (!cell.mHasChildren)
                        {
                            cell.mHasChildren = true;
                            cell.mChildrenOffset = group.mOffset;
                            cell.mChildrenSize = group.mSize;
                        }
                    }
                }
                return true;
            }

            bool onGroupEnd(const Content::GroupInfo&) override
            {
                if (!mGroups.empty())
                    mGroups.pop_back();
                return true;
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                ++mOut.mRecordsWalked;
                switch (record.mType)
                {
                    case Content::fourCC("WRLD"):
                    case Content::fourCC("CELL"):
                    case Content::fourCC("REFR"):
                    case Content::fourCC("ACHR"):
                    case Content::fourCC("STAT"):
                    case Content::fourCC("NPC_"):
                    case Content::fourCC("ARMO"):
                    case Content::fourCC("WEAP"):
                    case Content::fourCC("MISC"):
                    case Content::fourCC("LVLI"):
                    case Content::fourCC("LVLN"):
                    case Content::fourCC("ACTI"):
                    case Content::fourCC("CONT"):
                    case Content::fourCC("DOOR"):
                    case Content::fourCC("FLOR"):
                    case Content::fourCC("FURN"):
                    case Content::fourCC("LIGH"):
                    case Content::fourCC("MSTT"):
                    case Content::fourCC("TREE"):
                    case Content::fourCC("ALCH"):
                    case Content::fourCC("AMMO"):
                    case Content::fourCC("APPA"):
                    case Content::fourCC("BOOK"):
                    case Content::fourCC("INGR"):
                    case Content::fourCC("KEYM"):
                    case Content::fourCC("SCRL"):
                    case Content::fourCC("SLGM"):
                    case Content::fourCC("TACT"):
                    case Content::fourCC("IDLM"):
                    case Content::fourCC("SOUN"):
                    case Content::fourCC("ASPC"):
                    case Content::fourCC("SPEL"):
                    case Content::fourCC("TXST"):
                        break;
                    default:
                        return true;
                }
                TypeCount& count = mOut.mCounts[record.mType];
                ++count.mSeen;
                if ((record.mFlags & RecordFlags::Deleted) != 0)
                    ++count.mDeleted;
                if (mLimits.mSkipReferences
                    && (record.mType == Content::fourCC("REFR") || record.mType == Content::fourCC("ACHR")))
                {
                    if (record.mType == Content::fourCC("REFR"))
                        indexTeleportDoor(record);
                    return true;
                }
                if (count.mParsed >= mLimits.mMaxRecordsPerType)
                {
                    fail(record, "more than " + std::to_string(mLimits.mMaxRecordsPerType) + " records of one type");
                    return true;
                }

                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record,
                            "the record inflates to " + std::to_string(record.mUncompressedSize)
                                + " bytes, over the limit of " + std::to_string(mLimits.mMaxRecordBytes));
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(error, record.mType);
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record,
                        "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }

                mWhy.clear();
                mCurrentType = record.mType;
                bool ok = false;
                switch (record.mType)
                {
                    case Content::fourCC("WRLD"):
                        ok = readWorld(record, bytes);
                        break;
                    case Content::fourCC("CELL"):
                        ok = readCell(record, bytes);
                        break;
                    case Content::fourCC("REFR"):
                    case Content::fourCC("ACHR"):
                        ok = readRef(record, bytes);
                        break;
                    case Content::fourCC("STAT"):
                        ok = readStatic(record, bytes);
                        break;
                    case Content::fourCC("ACTI"):
                    case Content::fourCC("CONT"):
                    case Content::fourCC("DOOR"):
                    case Content::fourCC("FLOR"):
                    case Content::fourCC("FURN"):
                    case Content::fourCC("LIGH"):
                    case Content::fourCC("MSTT"):
                    case Content::fourCC("TREE"):
                    case Content::fourCC("ALCH"):
                    case Content::fourCC("AMMO"):
                    case Content::fourCC("APPA"):
                    case Content::fourCC("BOOK"):
                    case Content::fourCC("INGR"):
                    case Content::fourCC("KEYM"):
                    case Content::fourCC("SCRL"):
                    case Content::fourCC("SLGM"):
                    case Content::fourCC("TACT"):
                    case Content::fourCC("IDLM"):
                    case Content::fourCC("SOUN"):
                    case Content::fourCC("ASPC"):
                    case Content::fourCC("SPEL"):
                    case Content::fourCC("TXST"):
                        ok = readStatic(record, bytes);
                        break;
                    case Content::fourCC("NPC_"):
                        ok = readNpc(record, bytes);
                        break;
                    case Content::fourCC("ARMO"):
                        ok = readItem(record, bytes, ItemKind::Armor);
                        break;
                    case Content::fourCC("WEAP"):
                        ok = readItem(record, bytes, ItemKind::Weapon);
                        break;
                    case Content::fourCC("MISC"):
                        ok = readItem(record, bytes, ItemKind::Misc);
                        break;
                    default:
                        ok = readLeveled(record, bytes, record.mType == Content::fourCC("LVLN"));
                        break;
                }
                if (ok)
                    ++count.mParsed;
                else
                    fail(record, mWhy);
                return true;
            }

        private:
            // -- bookkeeping --

            void addError(Content::Error error, std::uint32_t type)
            {
                ++mOut.mCounts[type].mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)), record.mType);
            }

            void odd(const char* sub)
            {
                ++mOut.mOddSubrecords;
                ++mOut.mOddBy[tag(mCurrentType) + "." + sub];
            }

            void oddType(std::uint32_t sub)
            {
                ++mOut.mOddSubrecords;
                ++mOut.mOddBy[tag(mCurrentType) + "." + tag(sub)];
            }

            bool damaged(const SubReader& reader)
            {
                mWhy = reader.message();
                return false;
            }

            FormId resolved(std::uint32_t stored)
            {
                const std::optional<FormId> id = mResolver.resolve(stored);
                if (!id)
                {
                    ++mOut.mUnresolvedFormIds;
                    return kUnresolvedFormId;
                }
                return *id;
            }

            RecordCommon common(const Content::RawRecord& record)
            {
                RecordCommon c;
                c.mStoredId = record.mFormId;
                c.mId = resolved(record.mFormId);
                c.mFlags = record.mFlags;
                c.mOffset = record.mOffset;
                return c;
            }

            // -- field readers (each returns false and sets mWhy for a hostile value; a wrong size is "odd") --

            bool zstring(std::uint32_t sub, std::span<const std::byte> data, std::string& out)
            {
                if (data.size() > mLimits.mMaxStringBytes)
                {
                    mWhy = "subrecord " + tag(sub) + " is " + std::to_string(data.size())
                        + " bytes, over the string limit of " + std::to_string(mLimits.mMaxStringBytes);
                    return false;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                return true;
            }

            // FULL: a 4 byte id into the string tables in a localised plugin, else text. VERIFIED by the tool.
            bool lstring(std::uint32_t sub, std::span<const std::byte> data, LString& out)
            {
                if (mLocalized)
                {
                    if (data.size() != 4)
                    {
                        oddType(sub);
                        return true;
                    }
                    out.mPresent = true;
                    out.mLocalized = true;
                    out.mId = readLe<std::uint32_t>(data.data());
                    return true;
                }
                out.mPresent = true;
                return zstring(sub, data, out.mText);
            }

            bool formField(std::uint32_t sub, std::span<const std::byte> data, FormId& out)
            {
                if (data.size() != 4)
                {
                    oddType(sub);
                    return true;
                }
                out = resolved(readLe<std::uint32_t>(data.data()));
                return true;
            }

            bool floatField(std::uint32_t sub, std::span<const std::byte> data, float& out)
            {
                if (data.size() != 4)
                {
                    oddType(sub);
                    return true;
                }
                out = readLe<float>(data.data());
                return true;
            }

            bool formList(std::uint32_t sub, std::span<const std::byte> data, std::vector<FormId>& out)
            {
                if (data.size() % 4 != 0)
                {
                    oddType(sub);
                    return true;
                }
                const std::size_t n = data.size() / 4;
                if (out.size() + n > mLimits.mMaxEntriesPerList)
                {
                    mWhy = "more than " + std::to_string(mLimits.mMaxEntriesPerList) + " entries in " + tag(sub);
                    return false;
                }
                for (std::size_t i = 0; i < n; ++i)
                    out.push_back(resolved(readLe<std::uint32_t>(data.data() + i * 4)));
                return true;
            }

            // -- records --

            bool readWorld(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                WorldRecord w;
                w.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, w.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, w.mName);
                            break;
                        case Content::fourCC("WNAM"):
                            ok = formField(type, data, w.mParent);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mWorlds.push_back(std::move(w));
                return true;
            }

            bool readCell(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                CellRecord c;
                c.mCommon = common(record);
                c.mRecordSize = mPlugin.header().mRecordHeaderSize + record.mDataSize;
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, c.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, c.mName);
                            break;
                        case Content::fourCC("DATA"): // VERIFIED sizes 1 and 2 (tool)
                            if (data.size() == 1)
                                c.mFlags = std::to_integer<std::uint8_t>(data[0]);
                            else if (data.size() == 2)
                                c.mFlags = readLe<std::uint16_t>(data.data());
                            else
                                odd("DATA");
                            break;
                        case Content::fourCC("XCLC"): // x, y (i32), flags; VERIFIED size 12 (tool)
                            if (data.size() >= 8)
                            {
                                c.mGrid = CellGrid{ readLe<std::int32_t>(data.data()),
                                    readLe<std::int32_t>(data.data() + 4) };
                                if (data.size() != 12)
                                    odd("XCLC");
                            }
                            else
                                odd("XCLC");
                            break;
                        case Content::fourCC("XCLW"):
                        {
                            float height = 0;
                            if (data.size() == 4)
                            {
                                height = readLe<float>(data.data());
                                c.mWaterHeight = height;
                            }
                            else
                                odd("XCLW");
                            break;
                        }
                        case Content::fourCC("XLCN"):
                            ok = formField(type, data, c.mLocation);
                            break;
                        case Content::fourCC("XCLL"): // VERIFIED sizes 64 and 92 (tool); the 20 byte prefix is shared
                            if (data.size() == 64 || data.size() == 92)
                            {
                                const std::byte* p = data.data();
                                CellLightingData lighting;
                                for (std::size_t i = 0; i < 3; ++i)
                                {
                                    lighting.mAmbient[i] = static_cast<std::uint8_t>(p[i]);
                                    lighting.mDirectional[i] = static_cast<std::uint8_t>(p[4 + i]);
                                    lighting.mFogColor[i] = static_cast<std::uint8_t>(p[8 + i]);
                                }
                                lighting.mFogNear = readLe<float>(p + 12);
                                lighting.mFogFar = readLe<float>(p + 16);
                                c.mLighting = lighting;
                            }
                            else
                                odd("XCLL");
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                placeCell(c);
                const std::size_t index = mOut.mCells.size();
                mCellByStoredId[record.mFormId] = index;
                mOut.mCells.push_back(std::move(c));
                return true;
            }

            // Where the group stack says this CELL sits (UESP: CELL groups, types 2 to 5 and 1).
            void placeCell(CellRecord& c)
            {
                for (const Content::GroupInfo& g : mGroups)
                {
                    switch (g.mType)
                    {
                        case 1:
                            c.mWorld = resolved(g.mLabel);
                            break;
                        case 2:
                            c.mInteriorBlock = g.mLabel;
                            break;
                        case 3:
                            c.mInteriorSubBlock = g.mLabel;
                            break;
                        case 4:
                            c.mExteriorBlockLabel = g.mLabel;
                            break;
                        case 5:
                            c.mExteriorSubBlockLabel = g.mLabel;
                            break;
                        default:
                            break;
                    }
                }
                c.mWorldPersistentCell = !mGroups.empty() && mGroups.back().mType == 1;
            }

            bool readRef(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                PlacedRef r;
                r.mKind = record.mType == Content::fourCC("ACHR") ? RefKind::Actor : RefKind::Placed;
                r.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                bool haveOwner = false;
                bool haveRank = false;
                FormId owner = kNullFormId;
                std::int32_t rank = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, r.mCommon.mEditorId);
                            break;
                        case Content::fourCC("NAME"):
                            ok = formField(type, data, r.mBase);
                            break;
                        case Content::fourCC("DATA"): // position xyz, rotation xyz (f32); VERIFIED size 24 (tool),
                                                      // CONFIRMED by upstream components/esm4/loadrefr.cpp case DATA
                            if (data.size() >= 24)
                            {
                                const std::byte* p = data.data();
                                r.mPosition = { readLe<float>(p), readLe<float>(p + 4), readLe<float>(p + 8) };
                                r.mRotation = { readLe<float>(p + 12), readLe<float>(p + 16), readLe<float>(p + 20) };
                                r.mHasTransform = true;
                                if (data.size() != 24)
                                    odd("DATA");
                            }
                            else
                                odd("DATA");
                            break;
                        case Content::fourCC("XSCL"):
                            ok = floatField(type, data, r.mScale);
                            break;
                        // XOWN, XRNK, XLOC and XTEL (task 0099 N4). Sizes VERIFIED on the installed plugins
                        // (tools/skyrimse_records_check) and corroborated by upstream's ESM4 reader (cc9cii, from UESP,
                        // components/esm4/loadrefr.cpp: XOWN 4 bytes in Skyrim, XTEL 32, XLOC level + key positions;
                        // docs/research/0051-upstream-esm-audit.md), which is a second reading of the same source, not
                        // independent proof. Any other size is counted and decodes to nothing (never guessed); a
                        // repeated subrecord keeps the first and is counted.
                        case Content::fourCC("XOWN"):
                            if (data.size() != 4 || haveOwner)
                                odd("XOWN");
                            else
                            {
                                haveOwner = true;
                                owner = resolved(readLe<std::uint32_t>(data.data()));
                            }
                            break;
                        case Content::fourCC("XRNK"):
                            if (data.size() != 4 || haveRank)
                                odd("XRNK");
                            else
                            {
                                haveRank = true;
                                rank = static_cast<std::int32_t>(readLe<std::uint32_t>(data.data()));
                            }
                            break;
                        case Content::fourCC("XLOC"):
                            // level u8 @0, 3 padding bytes (garbage in the data, not read), key @4, flags u8 @8, 3
                            // padding, 8 zero bytes (not read)
                            if (data.size() != 20 || r.mLock)
                                odd("XLOC");
                            else
                            {
                                RefLock lock;
                                lock.mLevel = std::to_integer<std::uint8_t>(data[0]);
                                lock.mKey = resolved(readLe<std::uint32_t>(data.data() + 4));
                                lock.mFlags = std::to_integer<std::uint8_t>(data[8]);
                                r.mLock = lock;
                            }
                            break;
                        case Content::fourCC("XTEL"):
                            if (data.size() != 32 || r.mTeleport)
                                odd("XTEL");
                            else
                            {
                                const std::byte* q = data.data();
                                RefTeleport t;
                                t.mDoor = resolved(readLe<std::uint32_t>(q));
                                t.mPosition = { readLe<float>(q + 4), readLe<float>(q + 8), readLe<float>(q + 12) };
                                t.mRotation = { readLe<float>(q + 16), readLe<float>(q + 20), readLe<float>(q + 24) };
                                t.mFlags = readLe<std::uint32_t>(q + 28);
                                r.mTeleport = t;
                            }
                            break;
                        case Content::fourCC(
                            "XESP"): // parent form, flags, 3 unused; VERIFIED size 8 (tool), flag bits CONFIRMED by
                                     // upstream components/esm4/reference.hpp EnableParent (Flag_Inversed 0x1,
                                     // Flag_PopIn 0x2) and loadrefr.cpp case XESP (upstream reads the flags as a u32)
                            if (data.size() >= 5)
                            {
                                EnableParent e;
                                e.mParent = resolved(readLe<std::uint32_t>(data.data()));
                                e.mFlags = std::to_integer<std::uint8_t>(data[4]);
                                r.mEnableParent = e;
                                if (data.size() != 8)
                                    odd("XESP");
                            }
                            else
                                odd("XESP");
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                // The order of the subrecords is not part of the layout, so the owner and its rank are joined here.
                if (haveOwner)
                {
                    RefOwner o;
                    o.mOwner = owner;
                    if (haveRank)
                        o.mRank = rank;
                    r.mOwner = o;
                }
                else if (haveRank)
                    odd("XRNK"); // a rank with no owner to apply to

                // Which cell and which child group: the innermost cell-children group (type 6) names the cell.
                std::vector<PlacedRef>* target = nullptr;
                const Content::GroupInfo* cellGroup = nullptr;
                RefGroup kind = RefGroup::Other;
                for (auto it = mGroups.rbegin(); it != mGroups.rend(); ++it)
                {
                    if (it->mType == 6)
                    {
                        cellGroup = &*it;
                        break;
                    }
                }
                if (!mGroups.empty())
                {
                    switch (mGroups.back().mType)
                    {
                        case 8:
                            kind = RefGroup::Persistent;
                            break;
                        case 9:
                            kind = RefGroup::Temporary;
                            break;
                        case 10:
                            kind = RefGroup::VisibleDistant;
                            break;
                        default:
                            break;
                    }
                }
                r.mGroup = kind;
                if (cellGroup != nullptr)
                {
                    const auto it = mCellByStoredId.find(cellGroup->mLabel);
                    if (it != mCellByStoredId.end())
                    {
                        CellRecord& cell = mOut.mCells[it->second];
                        switch (kind)
                        {
                            case RefGroup::Persistent:
                                target = &cell.mPersistent;
                                break;
                            case RefGroup::Temporary:
                                target = &cell.mTemporary;
                                break;
                            case RefGroup::VisibleDistant:
                                target = &cell.mVisibleDistant;
                                break;
                            default:
                                target = &cell.mOther;
                                break;
                        }
                    }
                }
                if (target == nullptr)
                {
                    ++mOut.mRefsOutsideCells;
                    return true;
                }
                if (r.mTeleport && cellGroup != nullptr)
                {
                    const auto cellIt = mCellByStoredId.find(cellGroup->mLabel);
                    mOut.mTeleportRefCells.emplace_back(r.mCommon.mId, mOut.mCells[cellIt->second].mCommon.mId);
                }
                target->push_back(std::move(r));
                return true;
            }

            // The index pass (mSkipReferences) does not decode references, but the store needs the cell of every
            // teleport door to name a destination cell: a light scan of the subrecords for an XTEL of the right size.
            void indexTeleportDoor(const Content::RawRecord& record)
            {
                const Content::GroupInfo* cellGroup = nullptr;
                for (auto it = mGroups.rbegin(); it != mGroups.rend(); ++it)
                {
                    if (it->mType == 6)
                    {
                        cellGroup = &*it;
                        break;
                    }
                }
                if (cellGroup == nullptr)
                    return;
                const auto cellIt = mCellByStoredId.find(cellGroup->mLabel);
                if (cellIt == mCellByStoredId.end())
                    return;
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    Content::Error ignored;
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes
                        || !inflateRecord(record, mPlugin.name(), mInflated, ignored))
                        return; // the full parse reports a damaged record; the index just has no door for it
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                    return;
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                while (reader.next(type, data) == 1)
                {
                    if (type == Content::fourCC("XTEL") && data.size() == 32)
                    {
                        const std::optional<FormId> id = mResolver.resolve(record.mFormId);
                        if (id)
                            mOut.mTeleportRefCells.emplace_back(*id, mOut.mCells[cellIt->second].mCommon.mId);
                        return;
                    }
                }
            }

            // STAT, and the other placed types with a model (ModelRecord): the same three subrecords.
            bool readStatic(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                StaticRecord s;
                s.mCommon = common(record);
                const bool isLight = record.mType == Content::fourCC("LIGH");
                LightData light;
                bool hasLightData = false;
                float lightFade = 1.0f;
                bool hasLightFade = false;
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, s.mCommon.mEditorId);
                            break;
                        case Content::fourCC("DATA"): // LIGH: VERIFIED size 48 (tool), see LightData
                            if (!isLight)
                                break;
                            if (data.size() == 48)
                            {
                                const std::byte* p = data.data();
                                light.mTime = readLe<std::int32_t>(p);
                                light.mRadius = readLe<std::uint32_t>(p + 4);
                                for (std::size_t i = 0; i < 4; ++i)
                                    light.mColor[i] = static_cast<std::uint8_t>(p[8 + i]);
                                light.mFlags = readLe<std::uint32_t>(p + 12);
                                light.mFalloff = readLe<float>(p + 16);
                                light.mFov = readLe<float>(p + 20);
                                light.mNearClip = readLe<float>(p + 24);
                                light.mFlickerPeriod = readLe<float>(p + 28);
                                light.mFlickerIntensity = readLe<float>(p + 32);
                                light.mFlickerMovement = readLe<float>(p + 36);
                                light.mValue = readLe<std::uint32_t>(p + 40);
                                light.mWeight = readLe<float>(p + 44);
                                hasLightData = true;
                            }
                            else
                                odd("DATA");
                            break;
                        case Content::fourCC("FNAM"): // LIGH: VERIFIED size 4 (tool), the fade value
                            if (!isLight)
                                break;
                            if (data.size() == 4)
                            {
                                lightFade = readLe<float>(data.data());
                                hasLightFade = true;
                            }
                            else
                                odd("FNAM");
                            break;
                        case Content::fourCC("OBND"): // six i16; VERIFIED size 12 (tool)
                            if (data.size() == 12)
                            {
                                std::array<std::int16_t, 6> b{};
                                for (std::size_t i = 0; i < 6; ++i)
                                    b[i] = readLe<std::int16_t>(data.data() + i * 2);
                                s.mBounds = b;
                            }
                            else
                                odd("OBND");
                            break;
                        case Content::fourCC("MODL"):
                            ok = zstring(type, data, s.mModel);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                if (record.mType == Content::fourCC("STAT"))
                    mOut.mStatics.push_back(std::move(s));
                else
                {
                    ModelRecord model{ record.mType, std::move(s.mCommon), s.mBounds, std::move(s.mModel), std::nullopt };
                    if (hasLightData)
                    {
                        light.mFade = lightFade;
                        light.mHasFade = hasLightFade;
                        model.mLight = light;
                    }
                    mOut.mModels.push_back(std::move(model));
                }
                return true;
            }

            bool readNpc(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                NpcRecord n;
                n.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, n.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, n.mName);
                            break;
                        case Content::fourCC("RNAM"):
                            ok = formField(type, data, n.mRace);
                            break;
                        case Content::fourCC("CNAM"):
                            ok = formField(type, data, n.mClass);
                            break;
                        case Content::fourCC("ACBS"): // flags u32 @0, level u16 @8, calc min @10, calc max @12; size 24
                                                      // VERIFIED size (tool); offsets CONFIRMED by upstream
                                                      // components/esm4/actor.hpp ACBS_TES5 (flags, magicka u16,
                                                      // stamina u16, levelOrMult @8, calcMin @10, calcMax @12) and
                                                      // loadnpc.cpp case ACBS (24 bytes)
                            if (data.size() >= 14)
                            {
                                n.mHasConfig = true;
                                n.mFlags = readLe<std::uint32_t>(data.data());
                                n.mLevel = readLe<std::uint16_t>(data.data() + 8);
                                n.mCalcMin = readLe<std::uint16_t>(data.data() + 10);
                                n.mCalcMax = readLe<std::uint16_t>(data.data() + 12);
                                if (data.size() != 24)
                                    odd("ACBS");
                            }
                            else
                                odd("ACBS");
                            break;
                        case Content::fourCC("DNAM"): // 18 skills, 18 offsets, health, magicka, stamina u16; size 52
                                                      // VERIFIED (tool), offsets RECALLED-VERIFY
                            if (data.size() >= 42)
                            {
                                n.mHasStats = true;
                                for (std::size_t i = 0; i < 18; ++i)
                                    n.mSkills[i] = std::to_integer<std::uint8_t>(data[i]);
                                n.mHealth = readLe<std::uint16_t>(data.data() + 36);
                                n.mMagicka = readLe<std::uint16_t>(data.data() + 38);
                                n.mStamina = readLe<std::uint16_t>(data.data() + 40);
                                if (data.size() != 52)
                                    odd("DNAM");
                            }
                            else
                                odd("DNAM");
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mNpcs.push_back(std::move(n));
                return true;
            }

            bool readItem(const Content::RawRecord& record, std::span<const std::byte> bytes, ItemKind kind)
            {
                ItemRecord it;
                it.mKind = kind;
                it.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, it.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, it.mName);
                            break;
                        case Content::fourCC("MODL"):
                            if (kind == ItemKind::Armor)
                            {
                                // In an ARMO, MODL entries are armature (ARMA) forms; cc9cii's loadarmo.cpp does the
                                // same.
                                if (data.size() == 4)
                                {
                                    if (it.mArmatures.size() >= mLimits.mMaxEntriesPerList)
                                    {
                                        mWhy = "more than " + std::to_string(mLimits.mMaxEntriesPerList) + " armatures";
                                        return false;
                                    }
                                    it.mArmatures.push_back(resolved(readLe<std::uint32_t>(data.data())));
                                }
                                else
                                    odd("MODL");
                            }
                            else
                                ok = zstring(type, data, it.mModel);
                            break;
                        case Content::fourCC("MOD2"):
                            if (kind == ItemKind::Armor)
                                ok = zstring(type, data, it.mModel);
                            break;
                        case Content::fourCC("MOD4"):
                            if (kind == ItemKind::Armor)
                                ok = zstring(type, data, it.mFemaleModel);
                            break;
                        case Content::fourCC("KWDA"):
                            ok = formList(type, data, it.mKeywords);
                            break;
                        case Content::fourCC("DATA"):
                        {
                            const std::size_t expected = kind == ItemKind::Weapon ? 10 : 8;
                            if (data.size() >= 8)
                            {
                                it.mHasData = true;
                                it.mValue = readLe<std::int32_t>(data.data());
                                it.mWeight = readLe<float>(data.data() + 4);
                                if (kind == ItemKind::Weapon && data.size() >= 10)
                                    it.mDamage = readLe<std::uint16_t>(data.data() + 8);
                                if (data.size() != expected)
                                    odd("DATA");
                            }
                            else
                                odd("DATA");
                            break;
                        }
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mItems.push_back(std::move(it));
                return true;
            }

            bool readLeveled(const Content::RawRecord& record, std::span<const std::byte> bytes, bool npcList)
            {
                LeveledList l;
                l.mIsNpcList = npcList;
                l.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, l.mCommon.mEditorId);
                            break;
                        case Content::fourCC("LVLD"):
                            if (data.size() == 1)
                                l.mChanceNone = std::to_integer<std::uint8_t>(data[0]);
                            else
                                odd("LVLD");
                            break;
                        case Content::fourCC("LVLF"):
                            if (data.size() == 1)
                                l.mFlags = std::to_integer<std::uint8_t>(data[0]);
                            else
                                odd("LVLF");
                            break;
                        case Content::fourCC("LVLG"):
                            ok = formField(type, data, l.mGlobal);
                            break;
                        case Content::fourCC(
                            "LVLO"): // level u16, unused u16, form u32, count u16, unused u16; size 12 VERIFIED (tool)
                            if (data.size() == 12)
                            {
                                if (l.mEntries.size() >= mLimits.mMaxEntriesPerList)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxEntriesPerList)
                                        + " entries in a list";
                                    return false;
                                }
                                LeveledEntry e;
                                e.mLevel = readLe<std::uint16_t>(data.data());
                                e.mReference = resolved(readLe<std::uint32_t>(data.data() + 4));
                                e.mCount = readLe<std::uint16_t>(data.data() + 8);
                                l.mEntries.push_back(e);
                            }
                            else
                                odd("LVLO");
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mLeveledLists.push_back(std::move(l));
                return true;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const ParseLimits& mLimits;
            ParsedPlugin& mOut;
            bool mLocalized;
            std::vector<Content::GroupInfo> mGroups;
            std::unordered_map<std::uint32_t, std::size_t> mCellByStoredId;
            std::vector<std::byte> mInflated;
            std::string mWhy;
            std::uint32_t mCurrentType = 0;
        };
    }

    bool parseRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const ParseLimits& limits, ParsedPlugin& out)
    {
        out = ParsedPlugin{};
        out.mName = plugin.name();
        out.mHeader = plugin.header();
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Parser parser(plugin, resolver, limits, out);
        if (const std::optional<Content::Error> error = plugin.walk(parser))
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseRecordsFile(const std::filesystem::path& path, const LoadOrder& order, const ParseLimits& limits,
        ParsedPlugin& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, error);
        if (plugin == nullptr)
            return false;
        if (!isSkyrimSeHeader(plugin->header()))
        {
            error = makeError(plugin->name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        return parseRecords(*plugin, order, limits, out);
    }

    bool parseRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const ParseLimits& limits, ParsedPlugin& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!isSkyrimSeHeader(plugin->header()))
        {
            error = makeError(name, 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        return parseRecords(*plugin, order, limits, out);
    }
}

#include "game_skyrimse/music.hpp"

#include "exterior_common.hpp"
#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

// Layouts (VERIFIED = measured on the five installed Skyrim SE masters with an independent Python reader, aggregates in
// docs/tasks/0209-skyrim-music.md; RECALLED-VERIFY = a meaning the data does not settle):
//  MUSC (74): EDID; FNAM 4 (flags; VERIFIED 14 distinct values, every type with a ducking value has 0x20); PNAM 4 (u16
//   priority, u16 ducking; VERIFIED); WNAM 4 (float, the fade in seconds: 0, 3, 6, 9, 12 in the data); TNAM (n x 4,
//   MUST ids, 1 to 47, an id may repeat). Meanings of the flag bits RECALLED-VERIFY.
//  MUST (352): EDID; CNAM 4 (kind; VERIFIED three values); FLTV 4 (float, the length of a silent track or a palette);
//   DNAM 4 (float fade out); ANAM (zstring file, VERIFIED 330 names all ".wav"); BNAM (zstring, the finale); FNAM
//   (float array, cue points, 11 to 25 entries); LNAM 12 (float, float, u32; one record); CITC 4 + CTDA 32 each (the
//   conditions; the functions used are 18 GetCurrentTime, 149 GetIsCurrentWeather and 707 GetCombatTargetHasKeyword);
//   SNAM (palette members, n x 4; a zero separates groups: RECALLED-VERIFY, see the task).
//  CELL XCMO 4 (992 cells), XLCN 4, XCLR n x 4, DATA 1 or 2 bytes (bit 0 interior); LCTN PNAM 4, NAM1 4 (8 locations);
//  REGN RDMO 4 (53 uses; the section's RDAT is not read); WRLD ZNAM 4 (62 worlds); DOBJ DNAM 8-byte entries (code,
//  form), BTMS is the battle music type (VERIFIED: MUSCombat).

namespace game_skyrimse::music
{
    using namespace detail;

    namespace
    {
        constexpr std::uint32_t kParamResolvedFunctions[] = { 149, 707 };

        bool takesFormParam1(std::uint16_t function)
        {
            for (std::uint32_t f : kParamResolvedFunctions)
                if (f == function)
                    return true;
            return false;
        }

        class Walker final : public Content::Visitor
        {
        public:
            Walker(
                const Content::Plugin& plugin, const FormIdResolver& resolver, const MusicLimits& limits, Records& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onGroupBegin(const Content::GroupInfo& group) override
            {
                mGroups.push_back(group);
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
                switch (record.mType)
                {
                    case Content::fourCC("MUSC"):
                    case Content::fourCC("MUST"):
                    case Content::fourCC("CELL"):
                    case Content::fourCC("LCTN"):
                    case Content::fourCC("REGN"):
                    case Content::fourCC("WRLD"):
                    case Content::fourCC("DOBJ"):
                        break;
                    default:
                        return true;
                }
                TypeCount& count = mOut.mCounts[record.mType];
                ++count.mSeen;
                if ((record.mFlags & RecordFlags::Deleted) != 0)
                {
                    // A deleted record has no data worth reading; the earlier record stays (a limit).
                    ++count.mDeleted;
                    return true;
                }
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record inflates to " + std::to_string(record.mUncompressedSize) + " bytes");
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error), record.mType);
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record, "the record is " + std::to_string(record.mDataSize) + " bytes");
                    return true;
                }
                std::string why;
                bool ok = false;
                switch (record.mType)
                {
                    case Content::fourCC("MUSC"):
                        ok = readType(record, bytes, why);
                        break;
                    case Content::fourCC("MUST"):
                        ok = readTrack(record, bytes, why);
                        break;
                    case Content::fourCC("CELL"):
                        ok = readCell(record, bytes, why);
                        break;
                    case Content::fourCC("LCTN"):
                        ok = readLocation(record, bytes, why);
                        break;
                    case Content::fourCC("REGN"):
                        ok = readRegion(record, bytes, why);
                        break;
                    case Content::fourCC("WRLD"):
                        ok = readWorld(record, bytes, why);
                        break;
                    default:
                        ok = readDefaultObjects(bytes, why);
                        break;
                }
                if (ok)
                    ++count.mParsed;
                else
                    fail(record, why);
                return true;
            }

        private:
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

            FormId resolvedOwn(const Content::RawRecord& record) { return resolved(record.mFormId); }

            void odd() { ++mOut.mOddSubrecords; }

            bool form(std::span<const std::byte> data, FormId& out)
            {
                if (data.size() != 4)
                {
                    odd();
                    return true;
                }
                out = resolved(readLe<std::uint32_t>(data.data()));
                return true;
            }

            bool text(std::span<const std::byte> data, std::string& out, std::string& why)
            {
                if (data.size() > mLimits.mMaxStringBytes)
                {
                    why = "a string of " + std::to_string(data.size()) + " bytes";
                    return false;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                return true;
            }

            bool formList(std::span<const std::byte> data, std::vector<FormId>& out, std::string& why)
            {
                if (data.size() % 4 != 0)
                {
                    odd();
                    return true;
                }
                if (data.size() / 4 > mLimits.mMaxEntriesPerList)
                {
                    why = "a list of " + std::to_string(data.size() / 4) + " entries";
                    return false;
                }
                for (std::size_t i = 0; i < data.size(); i += 4)
                {
                    const std::uint32_t stored = readLe<std::uint32_t>(data.data() + i);
                    out.push_back(stored == 0 ? kNullFormId : resolved(stored));
                }
                return true;
            }

            bool readType(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                Type type;
                type.mId = resolvedOwn(record);
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                {
                    switch (sub)
                    {
                        case Content::fourCC("EDID"):
                            if (!text(data, type.mEditorId, why))
                                return false;
                            break;
                        case Content::fourCC("FNAM"):
                            if (data.size() == 4)
                                type.mFlags = readLe<std::uint32_t>(data.data());
                            else
                                odd();
                            break;
                        case Content::fourCC("PNAM"):
                            if (data.size() == 4)
                            {
                                type.mPriority = readLe<std::uint16_t>(data.data());
                                type.mDucking = readLe<std::uint16_t>(data.data() + 2);
                            }
                            else
                                odd();
                            break;
                        case Content::fourCC("WNAM"):
                            if (data.size() == 4)
                            {
                                const float fade = readLe<float>(data.data());
                                type.mFade = std::isfinite(fade) && fade > 0.f && fade < 600.f ? fade : 0.f;
                            }
                            else
                                odd();
                            break;
                        case Content::fourCC("TNAM"):
                            if (!formList(data, type.mTracks, why))
                                return false;
                            break;
                        default:
                            break;
                    }
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                mOut.mTypes.push_back(std::move(type));
                return true;
            }

            bool readTrack(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                Track track;
                track.mId = resolvedOwn(record);
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                {
                    switch (sub)
                    {
                        case Content::fourCC("EDID"):
                            if (!text(data, track.mEditorId, why))
                                return false;
                            break;
                        case Content::fourCC("CNAM"):
                            if (data.size() == 4)
                            {
                                const std::uint32_t kind = readLe<std::uint32_t>(data.data());
                                track.mKind = kind == kKindSingle ? TrackKind::Single
                                    : kind == kKindPalette        ? TrackKind::Palette
                                    : kind == kKindSilent         ? TrackKind::Silent
                                                                  : TrackKind::Unknown;
                            }
                            else
                                odd();
                            break;
                        case Content::fourCC("FLTV"):
                            track.mDuration = floatField(data);
                            break;
                        case Content::fourCC("DNAM"):
                            track.mFadeOut = floatField(data);
                            break;
                        case Content::fourCC("ANAM"):
                            if (!text(data, track.mFile, why))
                                return false;
                            break;
                        case Content::fourCC("BNAM"):
                            if (!text(data, track.mFinale, why))
                                return false;
                            break;
                        case Content::fourCC("FNAM"):
                            if (data.size() % 4 != 0 || data.size() / 4 > mLimits.mMaxEntriesPerList)
                                odd();
                            else
                                for (std::size_t i = 0; i < data.size(); i += 4)
                                    track.mCuePoints.push_back(readLe<float>(data.data() + i));
                            break;
                        case Content::fourCC("LNAM"):
                            if (data.size() == 12)
                            {
                                track.mLoopStart = readLe<float>(data.data());
                                track.mLoopEnd = readLe<float>(data.data() + 4);
                                track.mLoopCount = readLe<std::uint32_t>(data.data() + 8);
                            }
                            else
                                odd();
                            break;
                        case Content::fourCC("SNAM"):
                            if (!formList(data, track.mPalette, why))
                                return false;
                            break;
                        default:
                            break;
                    }
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                // The conditions: the same decoder the dialogue uses over the whole record; the form parameter of the
                // functions this data uses is a plugin-local id and is resolved here, through the plugin that holds the
                // record.
                track.mConditionsOk = conditions::decodeConditions(bytes, track.mConditions);
                for (survey::Ctda& c : track.mConditions)
                    if (takesFormParam1(c.mFunction))
                        c.mParam1 = c.mParam1 == 0 ? 0 : resolved(c.mParam1);
                mOut.mTracks.push_back(std::move(track));
                return true;
            }

            float floatField(std::span<const std::byte> data)
            {
                if (data.size() != 4)
                {
                    odd();
                    return 0.f;
                }
                const float value = readLe<float>(data.data());
                return std::isfinite(value) && value > 0.f && value < 1.0e6f ? value : 0.f;
            }

            FormId worldOfGroups() const
            {
                for (auto it = mGroups.rbegin(); it != mGroups.rend(); ++it)
                    if (it->mType == 1)
                    {
                        const std::optional<FormId> id = mResolver.resolve(it->mLabel);
                        return id ? *id : kUnresolvedFormId;
                    }
                return kNullFormId;
            }

            bool readCell(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                CellLink cell;
                cell.mCell = resolvedOwn(record);
                cell.mWorld = worldOfGroups();
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                {
                    switch (sub)
                    {
                        case Content::fourCC("EDID"):
                            if (!text(data, cell.mEditorId, why))
                                return false;
                            break;
                        case Content::fourCC("DATA"):
                            if (!data.empty())
                                cell.mInterior = (std::to_integer<unsigned>(data[0]) & 1u) != 0;
                            break;
                        case Content::fourCC("XCMO"):
                            form(data, cell.mMusic);
                            break;
                        case Content::fourCC("XLCN"):
                            form(data, cell.mLocation);
                            break;
                        case Content::fourCC("XCLR"):
                            if (!formList(data, cell.mRegions, why))
                                return false;
                            break;
                        default:
                            break;
                    }
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                // Only a cell that links something is kept (a cell with no music link and no region and no location
                // still matters for its world, so keep every cell: the table is a few tens of thousands of rows).
                if (cell.mInterior)
                    cell.mWorld = kNullFormId;
                mOut.mCells.push_back(std::move(cell));
                return true;
            }

            bool readLocation(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                LocationLink loc;
                loc.mId = resolvedOwn(record);
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                {
                    if (sub == Content::fourCC("PNAM"))
                        form(data, loc.mParent);
                    else if (sub == Content::fourCC("NAM1"))
                        form(data, loc.mMusic);
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                mOut.mLocations.push_back(loc);
                return true;
            }

            bool readRegion(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                RegionLink region;
                region.mId = resolvedOwn(record);
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                    if (sub == Content::fourCC("RDMO") && region.mMusic == kNullFormId)
                        form(data, region.mMusic);
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                mOut.mRegions.push_back(region);
                return true;
            }

            bool readWorld(const Content::RawRecord& record, std::span<const std::byte> bytes, std::string& why)
            {
                WorldLink world;
                world.mId = resolvedOwn(record);
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                {
                    if (sub == Content::fourCC("ZNAM"))
                        form(data, world.mMusic);
                    else if (sub == Content::fourCC("EDID") && !text(data, world.mEditorId, why))
                        return false;
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                mOut.mWorlds.push_back(std::move(world));
                return true;
            }

            bool readDefaultObjects(std::span<const std::byte> bytes, std::string& why)
            {
                SubReader reader(bytes);
                std::uint32_t sub = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(sub, data)) == 1)
                {
                    if (sub != Content::fourCC("DNAM"))
                        continue;
                    if (data.size() % 8 != 0)
                    {
                        odd();
                        continue;
                    }
                    for (std::size_t i = 0; i + 8 <= data.size(); i += 8)
                        if (readLe<std::uint32_t>(data.data() + i) == Content::fourCC("BTMS"))
                            mOut.mCombatType = resolved(readLe<std::uint32_t>(data.data() + i + 4));
                }
                if (status < 0)
                {
                    why = reader.message();
                    return false;
                }
                return true;
            }

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
                Content::Error error;
                error.mFile = mPlugin.name();
                error.mOffset = record.mOffset;
                error.mRecordType = record.mType;
                error.mMessage = std::move(message);
                addError(std::move(error), record.mType);
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const MusicLimits& mLimits;
            Records& mOut;
            std::vector<Content::GroupInfo> mGroups;
            std::vector<std::byte> mInflated;
        };

        template <class T, class Map>
        void put(Map& map, const std::vector<T>& records, FormId T::*id)
        {
            for (const T& r : records)
                map[r.*id] = r;
        }
    }

    bool parseMusicRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const MusicLimits& limits, Records& out)
    {
        out = Records();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            Content::Error error;
            error.mFile = plugin.name();
            error.mMessage = "this plugin is not a Skyrim Special Edition one";
            out.mWalkError = error;
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Walker walker(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(walker);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseMusicRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const MusicLimits& limits, Records& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (!plugin)
            return false;
        if (!parseMusicRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    // ---- MusicIndex ---------------------------------------------------------------------------------------------

    void MusicIndex::add(const Records& records)
    {
        // The walker skips deleted records, so a deletion in a later plugin does not remove the earlier record.
        put(mTypes, records.mTypes, &Type::mId);
        put(mTracks, records.mTracks, &Track::mId);
        put(mCells, records.mCells, &CellLink::mCell);
        put(mLocations, records.mLocations, &LocationLink::mId);
        put(mRegions, records.mRegions, &RegionLink::mId);
        put(mWorlds, records.mWorlds, &WorldLink::mId);
        if (records.mCombatType != kNullFormId && records.mCombatType != kUnresolvedFormId)
            mCombatType = records.mCombatType;
    }

    // ---- file names ---------------------------------------------------------------------------------------------

    std::string musicFilePath(std::string_view stored, const FileProbe& exists)
    {
        std::string s;
        s.reserve(stored.size());
        for (char c : stored)
        {
            if (c == '\\')
                c = '/';
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            if (static_cast<unsigned char>(c) < 0x20)
                return std::string();
            s.push_back(c);
        }
        while (!s.empty() && s.front() == '/')
            s.erase(s.begin());
        if (s.rfind("data/", 0) == 0)
            s.erase(0, 5);
        if (s.empty() || s.find("..") != std::string::npos || s.find(':') != std::string::npos)
            return std::string();
        const std::size_t slash = s.rfind('/');
        const std::size_t dot = s.rfind('.');
        const std::string stem
            = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? s.substr(0, dot) : s;
        const std::string candidates[2] = { stem + ".xwm", s };
        for (const std::string& candidate : candidates)
            if (!exists || exists(candidate))
                return candidate;
        return std::string();
    }

    // ---- the chooser --------------------------------------------------------------------------------------------

    namespace
    {
        constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

        std::uint64_t mix64(std::uint64_t x)
        {
            x += kGolden;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        constexpr int kMaxPaletteDepth = 3;
        constexpr std::size_t kMaxChain = 16;

        /// The world the evaluator asks: only the facts the music data uses.
        class MusicConditionWorld final : public conditions::ConditionWorld
        {
        public:
            explicit MusicConditionWorld(const ConditionFacts& facts)
                : mFacts(facts)
            {
            }

            conditions::Subject resolveRunOn(std::uint32_t, std::uint32_t) const override { return 1; }
            bool hasBaseId(conditions::Subject, std::uint32_t) const override { return false; }
            float questStage(std::uint32_t) const override { return 0.f; }
            bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
            bool inFaction(conditions::Subject, std::uint32_t) const override { return false; }
            float globalValue(std::uint32_t) const override { return 0.f; }
            conditions::Subject playerSubject() const override { return 1; }
            float gameHour() const override { return mFacts.mGameHour; }
            // The pack is not told the current weather (the engine's weather manager owns it): "is this weather" is
            // false, so a condition "weather is not X" holds and "weather is X" fails (a limit, in the task).
            bool isCurrentWeather(std::uint32_t) const override { return false; }
            // This session has no dragon to fight: the combat target has no keyword.
            bool combatTargetHasKeyword(conditions::Subject, std::uint32_t) const override { return false; }

        private:
            ConditionFacts mFacts;
        };
    }

    std::uint64_t musicRandom(std::uint64_t seed, std::uint32_t typeId, std::uint32_t pick, std::uint32_t salt)
    {
        return mix64(seed ^ mix64(static_cast<std::uint64_t>(typeId) + kGolden)
            ^ mix64((static_cast<std::uint64_t>(pick) << 20) + salt + 1));
    }

    PlaceType MusicChooser::placeType(FormId cellId) const
    {
        const CellLink* cell = mIndex.cell(cellId);
        if (cell == nullptr)
            return {};
        const auto valid = [this](FormId id) { return id != kNullFormId && mIndex.type(id) != nullptr; };
        if (valid(cell->mMusic))
            return { cell->mMusic, "cell" };
        FormId location = cell->mLocation;
        for (std::size_t step = 0; step < kMaxChain && location != kNullFormId; ++step)
        {
            const LocationLink* loc = mIndex.location(location);
            if (loc == nullptr)
                break;
            if (valid(loc->mMusic))
                return { loc->mMusic, "location" };
            location = loc->mParent;
        }
        FormId bestRegion = kNullFormId;
        std::uint16_t bestPriority = 0xFFFFu;
        for (FormId regionId : cell->mRegions)
        {
            const RegionLink* region = mIndex.region(regionId);
            if (region == nullptr || !valid(region->mMusic))
                continue;
            const Type* type = mIndex.type(region->mMusic);
            if (bestRegion == kNullFormId || type->mPriority < bestPriority)
            {
                bestRegion = region->mMusic;
                bestPriority = type->mPriority;
            }
        }
        if (bestRegion != kNullFormId)
            return { bestRegion, "region" };
        if (cell->mWorld != kNullFormId)
            if (const WorldLink* world = mIndex.world(cell->mWorld); world != nullptr && valid(world->mMusic))
                return { world->mMusic, "world" };
        return {};
    }

    bool MusicChooser::describe(
        FormId typeId, const char* reason, bool combat, openeden::sdk::game::MusicTypeDescription& out) const
    {
        const Type* type = mIndex.type(typeId);
        if (type == nullptr)
            return false;
        out = {};
        out.id = typeId;
        out.editorId = type->mEditorId;
        out.priority = type->mPriority;
        out.duckingCentiDb = type->mDucking;
        out.fadeSeconds = type->mFade;
        out.flags = type->mFlags;
        out.combat = combat;
        out.reason = reason;
        return true;
    }

    bool MusicChooser::expand(const Track& track, int depth, std::uint64_t random, const FileProbe& exists,
        openeden::sdk::game::MusicPlan& out) const
    {
        using namespace openeden::sdk::game;
        if (out.items.size() >= kMaxMusicPlanItems)
            return true;
        switch (track.mKind)
        {
            case TrackKind::Single:
            {
                const std::string path = musicFilePath(track.mFile, exists);
                if (path.empty())
                    return false;
                MusicItem item;
                item.kind = MusicItemKind::File;
                item.file = path;
                item.name = track.mEditorId;
                out.items.push_back(std::move(item));
                return true;
            }
            case TrackKind::Silent:
            {
                MusicItem item;
                item.kind = MusicItemKind::Silence;
                item.seconds = track.mDuration;
                item.name = track.mEditorId;
                out.items.push_back(std::move(item));
                return true;
            }
            case TrackKind::Palette:
            {
                if (depth >= kMaxPaletteDepth)
                    return false;
                // The groups of the palette: SNAM split at the zeros (RECALLED-VERIFY, see the task).
                std::vector<std::vector<FormId>> groups;
                std::vector<FormId> current;
                for (FormId id : track.mPalette)
                {
                    if (id == kNullFormId)
                    {
                        if (!current.empty())
                            groups.push_back(std::move(current));
                        current.clear();
                    }
                    else
                        current.push_back(id);
                }
                if (!current.empty())
                    groups.push_back(std::move(current));
                if (groups.empty())
                    return false;
                const std::vector<FormId>& group = groups[static_cast<std::size_t>(random % groups.size())];
                bool any = false;
                std::uint32_t k = 0;
                for (FormId id : group)
                {
                    const Track* member = mIndex.track(id);
                    if (member == nullptr)
                        continue;
                    if (expand(*member, depth + 1, mix64(random + (++k)), exists, out))
                        any = true;
                }
                return any;
            }
            case TrackKind::Unknown:
                break;
        }
        return false;
    }

    bool MusicChooser::plan(FormId typeId, const ConditionFacts& facts, std::uint64_t seed, std::uint32_t pick,
        const FileProbe& exists, openeden::sdk::game::MusicPlan& out)
    {
        out = {};
        const Type* type = mIndex.type(typeId);
        if (type == nullptr)
            return false;
        ++mPlans;
        const MusicConditionWorld world(facts);
        std::vector<const Track*> candidates;
        for (FormId id : type->mTracks)
        {
            const Track* track = mIndex.track(id);
            if (track == nullptr || track->mKind == TrackKind::Unknown || !track->mConditionsOk)
                continue;
            if (!mEvaluator.evaluate(track->mConditions, world))
            {
                ++mTracksRejectedByConditions;
                continue;
            }
            candidates.push_back(track);
        }
        if (candidates.empty())
            return false;
        const std::uint64_t random = musicRandom(seed, typeId, pick, 0);
        const bool inOrder = (type->mFlags & (TypeFlag::CycleTracks | TypeFlag::MaintainTrackOrder)) != 0;
        const std::size_t first = inOrder ? static_cast<std::size_t>(pick % candidates.size())
                                          : static_cast<std::size_t>(random % candidates.size());
        for (std::size_t n = 0; n < candidates.size(); ++n)
        {
            const Track& track = *candidates[(first + n) % candidates.size()];
            openeden::sdk::game::MusicPlan candidate;
            if (!expand(track, 0, mix64(random + n), exists, candidate) || candidate.items.empty())
            {
                ++mTracksWithoutFile;
                continue;
            }
            candidate.trackName = track.mEditorId;
            candidate.fadeOutSeconds = track.mFadeOut;
            candidate.finaleFile = track.mFinale.empty() ? std::string() : musicFilePath(track.mFinale, exists);
            out = std::move(candidate);
            return true;
        }
        return false;
    }

    // ---- the runtime --------------------------------------------------------------------------------------------

    MusicRuntime::MusicRuntime(std::vector<std::filesystem::path> plugins, LoadOrder order)
        : mPlugins(std::move(plugins))
        , mOrder(std::move(order))
    {
        mChooser = std::make_unique<MusicChooser>(mIndex);
    }

    MusicRuntime::~MusicRuntime()
    {
        if (mFuture.valid())
            mFuture.wait();
    }

    void MusicRuntime::start()
    {
        if (mStarted)
            return;
        mStarted = true;
        mFuture = std::async(std::launch::async, [this] { work(); });
    }

    void MusicRuntime::work()
    {
        const MusicLimits limits;
        for (const std::filesystem::path& path : mPlugins)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(path, error);
            if (file == nullptr)
            {
                ++mRecordErrors;
                continue;
            }
            Records records;
            (void)parseMusicRecords(*file, mOrder, limits, records);
            mRecordErrors += records.mErrors.size() + records.mErrorsDropped;
            mIndex.add(records);
        }
        mReady.store(true, std::memory_order_release);
    }

    bool MusicRuntime::type(FormId cell, bool inCombat, openeden::sdk::game::MusicTypeDescription& out)
    {
        if (!ready())
            return false;
        if (inCombat)
        {
            const FormId combat = mChooser->combatType();
            if (combat != kNullFormId && mChooser->describe(combat, "combat", true, out))
                return true;
        }
        const PlaceType place = mChooser->placeType(cell);
        if (place.mType == kNullFormId)
            return false;
        return mChooser->describe(place.mType, place.mReason, false, out);
    }

    bool MusicRuntime::plan(FormId typeId, const ConditionFacts& facts, std::uint64_t seed, std::uint32_t pick,
        const FileProbe& exists, openeden::sdk::game::MusicPlan& out)
    {
        return ready() && mChooser->plan(typeId, facts, seed, pick, exists, out);
    }
}

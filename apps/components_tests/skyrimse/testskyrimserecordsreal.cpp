// Task 0086, record loading first slice: the aggregate check on the player's own Skyrim SE plugins. It prints counts
// only (lines starting SKYRIMSE-RECORDS), never a record name, id or mod path, and writes nothing. Skipped when the
// environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required)
//   OPENEDEN_SKYRIMSE_MO2    optional: a Mod Organizer 2 instance folder (holding mods/ and profiles/); the plugins of
//                            the profile's loadorder.txt are parsed in that order
//   OPENEDEN_SKYRIMSE_PROFILE  optional: the profile name (default "Default")
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string envOr(const char* name)
    {
        const char* v = std::getenv(name);
        return v != nullptr ? v : "";
    }

    bool hasPluginExtension(const fs::path& p)
    {
        const std::string e = lower(p.extension().string());
        return e == ".esm" || e == ".esp" || e == ".esl";
    }

    // Owner, lock and teleport numbers (task 0099 stage N4): what the references say, and which ids they point at.
    struct Links
    {
        std::uint64_t mOwners = 0, mOwnersUnresolved = 0, mRanks = 0;
        std::uint64_t mLocks = 0, mKeys = 0, mKeysUnresolved = 0;
        std::uint64_t mTeleports = 0, mTeleportsNonFinite = 0, mTeleportsHuge = 0, mTeleportDoorsUnresolved = 0;
        std::map<std::string, std::uint64_t> mHistogram; // "lock level 25", "lock flags 4", "teleport flags 1", ...
        std::set<FormId> mOwnerIds, mKeyIds, mTargetIds;
        std::map<FormId, FormId> mTeleportOf; // door reference -> its destination door (later plugins win)
        std::uint64_t mIndexed = 0, mIndexMismatch = 0; // teleport doors in the index pass against the full parse
    };

    // Numbers kept across plugins.
    struct Totals
    {
        Links mLinks;
        std::uint64_t mPlugins = 0;
        std::uint64_t mRefused = 0;
        std::map<std::string, std::uint64_t> mRefusedWhy;
        std::uint64_t mWalkErrors = 0;
        std::map<std::string, std::uint64_t> mWalkWhy;
        std::map<std::uint32_t, TypeCount> mCounts;
        std::map<std::string, std::uint64_t> mOdd;
        std::map<std::string, std::uint64_t> mErrorKinds;
        std::uint64_t mUnresolved = 0;
        std::uint64_t mOutside = 0;
        std::uint64_t mRecords = 0;
        // cell layout checks
        std::uint64_t mInteriorCells = 0, mInteriorBlockMatch = 0, mInteriorSubMatch = 0;
        std::uint64_t mExteriorCells = 0, mExteriorWithGrid = 0, mBlockMatchYx = 0, mBlockMatchXy = 0, mSubMatchYx = 0,
                      mSubMatchXy = 0;
        std::uint64_t mCellsFlagInteriorButGrid = 0, mCellsGridButNotExterior = 0;
        std::uint64_t mRefsPersistentGroup = 0, mRefsPersistentGroupFlagged = 0, mRefsTemporaryFlagged = 0;
        std::uint64_t mRefsDisabled = 0, mRefsWithEnableParent = 0, mRefsScaled = 0, mRefsNoBase = 0;
        std::uint64_t mLightSelfRecords = 0, mLightSelfOutOfRange = 0;
        std::uint64_t mLightFlagMismatchExt = 0;
        std::uint64_t mNpcs = 0, mNpcsWithStats = 0, mNpcsFemale = 0;
        std::uint64_t mLeveledEntries = 0, mLeveledTotal = 0;
        std::uint64_t mNamesLocalized = 0, mNamesPlain = 0;
    };

    std::string errorKind(const std::string& m)
    {
        // Strip digits so equal kinds count together; messages hold tags and sizes, never names.
        std::string out;
        for (char c : m)
            if (!std::isdigit(static_cast<unsigned char>(c)))
                out.push_back(c);
        return out;
    }

    void accumulate(const ParsedPlugin& p, bool light, Totals& t)
    {
        t.mRecords += p.mRecordsWalked;
        for (const auto& [type, c] : p.mCounts)
        {
            TypeCount& sum = t.mCounts[type];
            sum.mSeen += c.mSeen;
            sum.mParsed += c.mParsed;
            sum.mFailed += c.mFailed;
            sum.mDeleted += c.mDeleted;
        }
        for (const auto& [k, v] : p.mOddBy)
            t.mOdd[k] += v;
        for (const Content::Error& e : p.mErrors)
            ++t.mErrorKinds[errorKind(e.mMessage)];
        t.mErrorKinds["(dropped over the cap)"] += p.mErrorsDropped;
        t.mUnresolved += p.mUnresolvedFormIds;
        t.mOutside += p.mRefsOutsideCells;

        for (const CellRecord& c : p.mCells)
        {
            if (c.interior())
            {
                ++t.mInteriorCells;
                // UESP: the block is the last decimal digit of the cell's form id, the sub-block the second last.
                const std::uint64_t local = c.mCommon.mStoredId & 0xFFFFFF;
                if (c.mInteriorBlock == static_cast<std::int64_t>(local % 10))
                    ++t.mInteriorBlockMatch;
                if (c.mInteriorSubBlock == static_cast<std::int64_t>((local / 10) % 10))
                    ++t.mInteriorSubMatch;
                if (c.mGrid)
                    ++t.mCellsFlagInteriorButGrid;
            }
            else
            {
                ++t.mExteriorCells;
                if (c.mGrid)
                {
                    ++t.mExteriorWithGrid;
                    // Block = grid / 32 (floor), sub-block = grid / 8 (floor); the label packs two int16.
                    const auto floorDiv
                        = [](std::int32_t a, std::int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
                    const auto pack = [](std::int32_t hi, std::int32_t lo) {
                        return (static_cast<std::uint32_t>(hi & 0xFFFF) << 16)
                            | static_cast<std::uint32_t>(lo & 0xFFFF);
                    };
                    const std::int32_t bx = floorDiv(c.mGrid->x, 32), by = floorDiv(c.mGrid->y, 32);
                    const std::int32_t sx = floorDiv(c.mGrid->x, 8), sy = floorDiv(c.mGrid->y, 8);
                    if (c.mExteriorBlockLabel == pack(by, bx))
                        ++t.mBlockMatchYx;
                    if (c.mExteriorBlockLabel == pack(bx, by))
                        ++t.mBlockMatchXy;
                    if (c.mExteriorSubBlockLabel == pack(sy, sx))
                        ++t.mSubMatchYx;
                    if (c.mExteriorSubBlockLabel == pack(sx, sy))
                        ++t.mSubMatchXy;
                }
            }
            if (c.mGrid && c.interior())
                ++t.mCellsGridButNotExterior;
            if (light && (c.mCommon.mStoredId >> 24) >= p.mHeader.mMasters.size())
            {
                ++t.mLightSelfRecords;
                const std::uint32_t low = c.mCommon.mStoredId & 0xFFFFFF;
                if (low < 0x800 || low > 0xFFF)
                    ++t.mLightSelfOutOfRange;
            }
            for (const PlacedRef& r : c.mPersistent)
            {
                ++t.mRefsPersistentGroup;
                if (r.persistent())
                    ++t.mRefsPersistentGroupFlagged;
            }
            for (const PlacedRef& r : c.mTemporary)
                if (r.persistent())
                    ++t.mRefsTemporaryFlagged;
            const auto refStats = [&](const std::vector<PlacedRef>& v) {
                Links& k = t.mLinks;
                const auto usable = [](FormId id) { return id != kNullFormId && id != kUnresolvedFormId; };
                for (const PlacedRef& r : v)
                {
                    if (r.mOwner)
                    {
                        ++k.mOwners;
                        if (r.mOwner->mRank)
                            ++k.mRanks;
                        if (usable(r.mOwner->mOwner))
                            k.mOwnerIds.insert(r.mOwner->mOwner);
                        else
                            ++k.mOwnersUnresolved;
                    }
                    if (r.mLock)
                    {
                        ++k.mLocks;
                        ++k.mHistogram["lock level " + std::to_string(r.mLock->mLevel)];
                        ++k.mHistogram["lock flags " + std::to_string(r.mLock->mFlags)];
                        if (usable(r.mLock->mKey))
                        {
                            ++k.mKeys;
                            k.mKeyIds.insert(r.mLock->mKey);
                        }
                        else if (r.mLock->mKey == kUnresolvedFormId)
                            ++k.mKeysUnresolved;
                    }
                    if (r.mTeleport)
                    {
                        const RefTeleport& tp = *r.mTeleport;
                        ++k.mTeleports;
                        ++k.mHistogram["teleport flags " + std::to_string(tp.mFlags)];
                        const float all[] = { tp.mPosition.x, tp.mPosition.y, tp.mPosition.z, tp.mRotation.x,
                            tp.mRotation.y, tp.mRotation.z };
                        bool finite = true, huge = false;
                        for (float f : all)
                        {
                            finite = finite && std::isfinite(f);
                            huge = huge || (std::isfinite(f) && std::abs(f) > 1.0e7f);
                        }
                        if (!finite)
                            ++k.mTeleportsNonFinite;
                        if (huge)
                            ++k.mTeleportsHuge;
                        if (usable(tp.mDoor))
                        {
                            k.mTargetIds.insert(tp.mDoor);
                            k.mTeleportOf[r.mCommon.mId] = tp.mDoor;
                        }
                        else
                            ++k.mTeleportDoorsUnresolved;
                    }
                    if (r.initiallyDisabled())
                        ++t.mRefsDisabled;
                    if (r.mEnableParent)
                        ++t.mRefsWithEnableParent;
                    if (r.mScale != 1.0f)
                        ++t.mRefsScaled;
                    if (r.mBase == kNullFormId)
                        ++t.mRefsNoBase;
                }
            };
            refStats(c.mPersistent);
            refStats(c.mTemporary);
            refStats(c.mVisibleDistant);
            refStats(c.mOther);
            (c.mName.mLocalized ? t.mNamesLocalized : t.mNamesPlain) += c.mName.mPresent ? 1 : 0;
        }
        for (const NpcRecord& n : p.mNpcs)
        {
            ++t.mNpcs;
            t.mNpcsWithStats += n.mHasStats ? 1 : 0;
            t.mNpcsFemale += n.female() ? 1 : 0;
            (n.mName.mLocalized ? t.mNamesLocalized : t.mNamesPlain) += n.mName.mPresent ? 1 : 0;
        }
        for (const ItemRecord& i : p.mItems)
            (i.mName.mLocalized ? t.mNamesLocalized : t.mNamesPlain) += i.mName.mPresent ? 1 : 0;
        for (const LeveledList& l : p.mLeveledLists)
        {
            ++t.mLeveledTotal;
            t.mLeveledEntries += l.mEntries.size();
        }
    }

    void print(const char* label, const Totals& t)
    {
        const auto line
            = [&](const std::string& what) { std::cout << "SKYRIMSE-RECORDS " << label << ' ' << what << '\n'; };
        line("plugins parsed " + std::to_string(t.mPlugins) + " refused " + std::to_string(t.mRefused) + " walk errors "
            + std::to_string(t.mWalkErrors) + " records walked " + std::to_string(t.mRecords));
        for (const auto& [k, v] : t.mRefusedWhy)
            line("refused: " + k + " x" + std::to_string(v));
        for (const auto& [k, v] : t.mWalkWhy)
            line("walk error: " + k + " x" + std::to_string(v));
        for (const auto& [type, c] : t.mCounts)
            line("type " + Content::fourCCToString(type) + " seen " + std::to_string(c.mSeen) + " parsed "
                + std::to_string(c.mParsed) + " failed " + std::to_string(c.mFailed) + " deleted "
                + std::to_string(c.mDeleted));
        for (const auto& [k, v] : t.mOdd)
            line("odd subrecord " + k + " x" + std::to_string(v));
        for (const auto& [k, v] : t.mErrorKinds)
            if (v != 0)
                line("error kind: " + k + " x" + std::to_string(v));
        line("unresolved form ids " + std::to_string(t.mUnresolved) + " refs outside cells "
            + std::to_string(t.mOutside));
        line("interior cells " + std::to_string(t.mInteriorCells) + " block label = id%10 "
            + std::to_string(t.mInteriorBlockMatch) + " sub-block = (id/10)%10 " + std::to_string(t.mInteriorSubMatch));
        line("exterior cells " + std::to_string(t.mExteriorCells) + " with XCLC " + std::to_string(t.mExteriorWithGrid)
            + " block label y|x " + std::to_string(t.mBlockMatchYx) + " x|y " + std::to_string(t.mBlockMatchXy)
            + " sub-block y|x " + std::to_string(t.mSubMatchYx) + " x|y " + std::to_string(t.mSubMatchXy));
        line("interior cells with XCLC " + std::to_string(t.mCellsFlagInteriorButGrid));
        line("refs in persistent groups " + std::to_string(t.mRefsPersistentGroup) + " with the persistent flag "
            + std::to_string(t.mRefsPersistentGroupFlagged) + "; temporary refs with the flag "
            + std::to_string(t.mRefsTemporaryFlagged));
        line("refs initially disabled " + std::to_string(t.mRefsDisabled) + " with enable parent "
            + std::to_string(t.mRefsWithEnableParent) + " scaled " + std::to_string(t.mRefsScaled) + " no base "
            + std::to_string(t.mRefsNoBase));
        line("light plugin self cells " + std::to_string(t.mLightSelfRecords) + " outside 0x800..0xFFF "
            + std::to_string(t.mLightSelfOutOfRange));
        line("npcs " + std::to_string(t.mNpcs) + " with DNAM " + std::to_string(t.mNpcsWithStats) + " female "
            + std::to_string(t.mNpcsFemale));
        line("leveled lists " + std::to_string(t.mLeveledTotal) + " entries " + std::to_string(t.mLeveledEntries));
        line("names localized " + std::to_string(t.mNamesLocalized) + " plain " + std::to_string(t.mNamesPlain));
    }

    // Finds a plugin by name: the game's Data folder first, then the MO2 mod folders.
    class PluginFinder
    {
    public:
        PluginFinder(const fs::path& data, const fs::path& mo2)
            : mData(data)
        {
            if (mo2.empty())
                return;
            std::error_code ec;
            for (const auto& mod : fs::directory_iterator(mo2 / "mods", ec))
            {
                if (!mod.is_directory(ec))
                    continue;
                for (const auto& f : fs::directory_iterator(mod.path(), ec))
                    if (f.is_regular_file(ec) && hasPluginExtension(f.path()))
                        mMods[lower(f.path().filename().string())] = f.path();
            }
        }

        fs::path find(const std::string& name) const
        {
            std::error_code ec;
            const fs::path inData = mData / name;
            if (fs::exists(inData, ec))
                return inData;
            const auto it = mMods.find(lower(name));
            return it != mMods.end() ? it->second : fs::path();
        }

    private:
        fs::path mData;
        std::map<std::string, fs::path> mMods;
    };

    // Walks the record headers of every plugin once more and says what the ids that owners, keys and teleports point
    // at really are (aggregates only). Needs the whole load order, which is why it runs after the parse.
    void crossCheck(
        const char* label, const std::vector<std::pair<std::string, fs::path>>& files, const LoadOrder& order, Links& k)
    {
        struct Walker final : Content::Visitor
        {
            const FormIdResolver* mResolver = nullptr;
            Links* mLinks = nullptr;
            std::map<FormId, std::uint32_t> mOwnerType, mKeyType, mTargetType;
            bool onRecord(const Content::RawRecord& r) override
            {
                const std::optional<FormId> id = mResolver->resolve(r.mFormId);
                if (!id)
                    return true;
                if (mLinks->mOwnerIds.count(*id))
                    mOwnerType[*id] = r.mType;
                if (mLinks->mKeyIds.count(*id))
                    mKeyType[*id] = r.mType;
                if (mLinks->mTargetIds.count(*id))
                    mTargetType[*id] = r.mType;
                return true;
            }
        } walker;
        walker.mLinks = &k;
        for (const auto& [name, path] : files)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(path, error);
            if (plugin == nullptr)
                continue;
            const FormIdResolver resolver(plugin->header(), name, order);
            walker.mResolver = &resolver;
            Content::WalkOptions options;
            options.mReadData = false;
            (void)plugin->walk(walker, options);
        }
        const auto line
            = [&](const std::string& what) { std::cout << "SKYRIMSE-RECORDS " << label << ' ' << what << '\n'; };
        const auto count = [&](const std::set<FormId>& ids, const std::map<FormId, std::uint32_t>& types,
                               std::initializer_list<std::uint32_t> good, std::uint64_t& missing,
                               std::map<std::string, std::uint64_t>& other) {
            std::uint64_t ok = 0;
            missing = 0;
            for (const FormId id : ids)
            {
                const auto it = types.find(id);
                if (it == types.end())
                {
                    ++missing;
                    continue;
                }
                bool isGood = false;
                for (const std::uint32_t g : good)
                    isGood = isGood || it->second == g;
                if (isGood)
                    ++ok;
                else
                    ++other[Content::fourCCToString(it->second)];
            }
            return ok;
        };
        std::uint64_t missing = 0;
        std::map<std::string, std::uint64_t> other;
        const std::uint64_t ownersOk = count(
            k.mOwnerIds, walker.mOwnerType, { Content::fourCC("NPC_"), Content::fourCC("FACT") }, missing, other);
        line("owners: " + std::to_string(k.mOwners) + " with rank " + std::to_string(k.mRanks) + " unresolved "
            + std::to_string(k.mOwnersUnresolved) + "; distinct owner forms " + std::to_string(k.mOwnerIds.size())
            + " NPC_ or FACT " + std::to_string(ownersOk) + " missing " + std::to_string(missing));
        for (const auto& [t, n] : other)
            line("owner form of type " + t + " x" + std::to_string(n));
        other.clear();
        const std::uint64_t keysOk = count(k.mKeyIds, walker.mKeyType, { Content::fourCC("KEYM") }, missing, other);
        line("locks: " + std::to_string(k.mLocks) + " with a key " + std::to_string(k.mKeys) + " key unresolved "
            + std::to_string(k.mKeysUnresolved) + "; distinct keys " + std::to_string(k.mKeyIds.size()) + " KEYM "
            + std::to_string(keysOk) + " missing " + std::to_string(missing));
        for (const auto& [t, n] : other)
            line("key form of type " + t + " x" + std::to_string(n));
        other.clear();
        const std::uint64_t targetsOk
            = count(k.mTargetIds, walker.mTargetType, { Content::fourCC("REFR") }, missing, other);
        line("teleports: " + std::to_string(k.mTeleports) + " non-finite " + std::to_string(k.mTeleportsNonFinite)
            + " past 1e7 " + std::to_string(k.mTeleportsHuge) + " door unresolved "
            + std::to_string(k.mTeleportDoorsUnresolved) + "; distinct destination doors "
            + std::to_string(k.mTargetIds.size()) + " REFR " + std::to_string(targetsOk) + " missing "
            + std::to_string(missing));
        for (const auto& [t, n] : other)
            line("destination of type " + t + " x" + std::to_string(n));
        // Does the destination door lead back? (Both ends of a pair carry an XTEL.)
        std::uint64_t reciprocal = 0, oneWay = 0, noXtel = 0;
        for (const auto& [door, target] : k.mTeleportOf)
        {
            const auto back = k.mTeleportOf.find(target);
            if (back == k.mTeleportOf.end())
                ++noXtel;
            else if (back->second == door)
                ++reciprocal;
            else
                ++oneWay;
        }
        line("teleport pairs: leads back " + std::to_string(reciprocal) + " leads elsewhere " + std::to_string(oneWay)
            + " destination has no XTEL " + std::to_string(noXtel) + " of " + std::to_string(k.mTeleportOf.size()));
        line("index pass: teleport doors found " + std::to_string(k.mIndexed) + " plugins where the index differs from the "
            "full parse " + std::to_string(k.mIndexMismatch));
        for (const auto& [h, n] : k.mHistogram)
            line(h + " x" + std::to_string(n));
    }

    // Parses `names` in order as one load order.
    void runLoadOrder(const char* label, const std::vector<std::string>& names, const PluginFinder& finder, Totals& t,
        bool resolveStrings, const fs::path& data)
    {
        // The load order: each plugin that exists, in the listed order, light by its header flag.
        LoadOrder order;
        std::vector<std::pair<std::string, fs::path>> files;
        std::set<std::string> seen;
        for (const std::string& name : names)
        {
            if (!seen.insert(lower(name)).second)
                continue;
            const fs::path path = finder.find(name);
            if (path.empty())
                continue;
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(path, error);
            if (plugin == nullptr)
            {
                ++t.mRefused;
                ++t.mRefusedWhy["cannot open: " + errorKind(error.mMessage)];
                continue;
            }
            const bool light = plugin->header().isLight();
            if (light != (lower(path.extension().string()) == ".esl"))
                ++t.mLightFlagMismatchExt;
            std::string why;
            if (!order.add(name, light, why))
            {
                ++t.mRefused;
                ++t.mRefusedWhy["load order: " + errorKind(why)];
                continue;
            }
            files.emplace_back(name, path);
        }
        std::cout << "SKYRIMSE-RECORDS " << label << " load order " << order.normalCount() << " full + "
                  << order.lightCount() << " light plugins; light flag differs from the .esl extension "
                  << t.mLightFlagMismatchExt << '\n';

        std::uint64_t stringsFound = 0, stringsMissingId = 0, stringsChecked = 0, tablesMissing = 0;
        for (const auto& [name, path] : files)
        {
            ParsedPlugin parsed;
            Content::Error error;
            ParseLimits limits;
            const bool ok = parseRecordsFile(path, order, limits, parsed, error);
            if (!ok && !parsed.mWalkError)
            {
                ++t.mRefused;
                ++t.mRefusedWhy[errorKind(error.mMessage)];
                continue;
            }
            ++t.mPlugins;
            if (!ok)
            {
                ++t.mWalkErrors;
                ++t.mWalkWhy[errorKind(parsed.mWalkError->mMessage)];
            }
            accumulate(parsed, order.isLight(name), t);
            {
                // The index pass (references skipped) must find the same teleport doors in the same cells.
                ParsedPlugin indexed;
                Content::Error indexError;
                ParseLimits skip;
                skip.mSkipReferences = true;
                (void)parseRecordsFile(path, order, skip, indexed, indexError);
                t.mLinks.mIndexed += indexed.mTeleportRefCells.size();
                if (indexed.mTeleportRefCells != parsed.mTeleportRefCells)
                    ++t.mLinks.mIndexMismatch;
            }

            if (resolveStrings && parsed.mHeader.isLocalized())
            {
                // The official masters keep their tables in Skyrim - Interface.bsa.
                StringTable table;
                std::string bytes, err;
                bool have = false;
                for (const char* bsa : { "Skyrim - Interface.bsa" })
                {
                    if (readArchiveEntry(
                            data / bsa, stringTablePath(name, "english", StringTableKind::Strings), bytes, err))
                    {
                        Content::Error tableError;
                        have = StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                            StringTableKind::Strings, name, table, tableError);
                        if (!have)
                            std::cout << "SKYRIMSE-RECORDS " << label
                                      << " string table refused: " << errorKind(tableError.mMessage) << '\n';
                    }
                }
                if (!have)
                {
                    ++tablesMissing;
                    continue;
                }
                stringsFound += table.size();
                const auto check = [&](LString s) {
                    if (!s.mPresent || !s.mLocalized)
                        return;
                    ++stringsChecked;
                    if (!lookupLString(s, table))
                        ++stringsMissingId;
                };
                for (const CellRecord& c : parsed.mCells)
                    check(c.mName);
                for (const NpcRecord& n : parsed.mNpcs)
                    check(n.mName);
                for (const ItemRecord& i : parsed.mItems)
                    check(i.mName);
                for (const WorldRecord& w : parsed.mWorlds)
                    check(w.mName);
            }
        }
        if (resolveStrings)
            std::cout << "SKYRIMSE-RECORDS " << label << " strings: table entries " << stringsFound << " names checked "
                      << stringsChecked << " id not in the table " << stringsMissingId << " plugins without a table "
                      << tablesMissing << '\n';
        crossCheck(label, files, order, t.mLinks);
    }

    // The sizes and ranges the decoders rely on, as measured on the installed plugins (task 0099 N4). A plugin outside
    // them is not an error of the data, but the decoders were written for these.
    void expectLinks(const Totals& t)
    {
        for (const char* key : { "REFR.XOWN", "REFR.XRNK", "REFR.XLOC", "REFR.XTEL", "ACHR.XOWN", "ACHR.XRNK" })
        {
            const auto it = t.mOdd.find(key);
            EXPECT_TRUE(it == t.mOdd.end()) << key << " odd x" << (it == t.mOdd.end() ? 0 : it->second);
        }
        EXPECT_EQ(t.mLinks.mTeleportsNonFinite, 0u);
        EXPECT_EQ(t.mLinks.mTeleportsHuge, 0u);
        EXPECT_EQ(t.mLinks.mIndexMismatch, 0u);
        EXPECT_GT(t.mLinks.mTeleports, 0u);
    }

    TEST(SkyrimSeRecordsRealData, theGamesOwnMasters)
    {
        const std::string dataEnv = envOr("OPENEDEN_SKYRIMSE_DATA");
        if (dataEnv.empty() || !fs::is_directory(dataEnv))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = dataEnv;
        const PluginFinder finder(data, fs::path());
        Totals t;
        runLoadOrder("game", { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" },
            finder, t, true, data);
        print("game", t);
        EXPECT_EQ(t.mPlugins, 5u);
        EXPECT_EQ(t.mWalkErrors, 0u);
        for (const auto& [type, c] : t.mCounts)
            EXPECT_EQ(c.mFailed, 0u) << Content::fourCCToString(type);
        expectLinks(t);
    }

    TEST(SkyrimSeRecordsRealData, theModList)
    {
        const std::string dataEnv = envOr("OPENEDEN_SKYRIMSE_DATA");
        const std::string mo2Env = envOr("OPENEDEN_SKYRIMSE_MO2");
        if (dataEnv.empty() || mo2Env.empty() || !fs::is_directory(mo2Env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA and OPENEDEN_SKYRIMSE_MO2 are not both set";
        std::string profile = envOr("OPENEDEN_SKYRIMSE_PROFILE");
        if (profile.empty())
            profile = "Default";
        std::ifstream list(fs::path(mo2Env) / "profiles" / profile / "loadorder.txt");
        if (!list)
            GTEST_SKIP() << "no loadorder.txt";
        std::vector<std::string> names;
        for (std::string line; std::getline(list, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] == '*')
                line.erase(0, 1);
            names.push_back(line);
        }
        const PluginFinder finder(dataEnv, mo2Env);
        Totals t;
        runLoadOrder("mods", names, finder, t, false, dataEnv);
        print("mods", t);
        EXPECT_GT(t.mPlugins, 0u);
        expectLinks(t);
    }
}

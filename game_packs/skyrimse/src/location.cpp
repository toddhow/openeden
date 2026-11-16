#include "game_skyrimse/location.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <cstring>

// Layout of LCTN: see the header (task 0156).

namespace game_skyrimse
{
    namespace
    {
        std::uint32_t le32(const std::byte* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }

        std::int16_t le16s(const std::byte* p)
        {
            std::int16_t v;
            std::memcpy(&v, p, 2);
            return v;
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

        class LocationParser final : public Content::Visitor
        {
        public:
            LocationParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const LocationLimits& limits,
                LocationRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("LCTN"))
                    return true;
                ++mOut.mCount.mSeen;
                if ((record.mFlags & RecordFlags::Deleted) != 0)
                    ++mOut.mCount.mDeleted;
                if (mOut.mCount.mParsed >= mLimits.mMaxRecords)
                {
                    fail(record, "too many LCTN records");
                    return true;
                }
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record inflates past the limit");
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error));
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record, "the record is over the size limit");
                    return true;
                }
                LocationRecord loc;
                loc.mCommon.mStoredId = record.mFormId;
                loc.mCommon.mId = resolved(record.mFormId);
                loc.mCommon.mFlags = record.mFlags;
                loc.mCommon.mOffset = record.mOffset;
                bool tooMany = false;
                const auto tally = [&](std::size_t entries) {
                    if (entries > mLimits.mMaxEntries)
                        tooMany = true;
                    return !tooMany;
                };
                const bool wellFormed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    switch (sub.mType)
                    {
                        case Content::fourCC("EDID"):
                        {
                            const char* b = reinterpret_cast<const char*>(sub.mData.data());
                            const void* nul = sub.mData.empty() ? nullptr : std::memchr(b, '\0', sub.mData.size());
                            loc.mCommon.mEditorId.assign(
                                b, nul != nullptr ? static_cast<const char*>(nul) : b + sub.mData.size());
                            break;
                        }
                        case Content::fourCC("FULL"):
                            // a 4 byte id into the string tables in a localised plugin, else the text (task 0217)
                            if (mPlugin.header().isLocalized())
                            {
                                if (sub.mData.size() != 4)
                                {
                                    ++mOut.mOddSubrecords;
                                    break;
                                }
                                loc.mName.mPresent = true;
                                loc.mName.mLocalized = true;
                                loc.mName.mId = le32(sub.mData.data());
                            }
                            else
                            {
                                const char* b = reinterpret_cast<const char*>(sub.mData.data());
                                const void* nul = sub.mData.empty() ? nullptr : std::memchr(b, 0, sub.mData.size());
                                loc.mName.mPresent = true;
                                loc.mName.mText.assign(
                                    b, nul != nullptr ? static_cast<const char*>(nul) : b + sub.mData.size());
                            }
                            break;
                        case Content::fourCC("PNAM"):
                            if (sub.mData.size() != 4)
                                ++mOut.mOddSubrecords;
                            else
                                loc.mParent = resolved(le32(sub.mData.data()));
                            break;
                        case Content::fourCC("KWDA"):
                            if (sub.mData.size() % 4 != 0)
                            {
                                ++mOut.mOddSubrecords;
                                break;
                            }
                            if (sub.mData.size() / 4 > mLimits.mMaxKeywords)
                            {
                                tooMany = true;
                                break;
                            }
                            for (std::size_t i = 0; i + 4 <= sub.mData.size(); i += 4)
                                loc.mKeywords.push_back(resolved(le32(sub.mData.data() + i)));
                            break;
                        // task 0211: the lists of exterior cells, special and persistent references and unique NPCs; the
                        // location (L*), actor (A*) and reference (R*) families are merged (see the header)
                        case Content::fourCC("LCEC"):
                        case Content::fourCC("ACEC"):
                        case Content::fourCC("RCEC"):
                        {
                            const std::size_t size = sub.mData.size();
                            if (size < 4 || (size - 4) % 4 != 0)
                            {
                                ++mOut.mOddSubrecords;
                                break;
                            }
                            if (!tally((size - 4) / 4))
                                break;
                            LocationCells cells;
                            cells.mWorld = resolved(le32(sub.mData.data()));
                            for (std::size_t i = 4; i + 4 <= size; i += 4)
                                cells.mGrids.push_back({ le16s(sub.mData.data() + i), le16s(sub.mData.data() + i + 2) });
                            loc.mCells.push_back(std::move(cells));
                            break;
                        }
                        case Content::fourCC("LCSR"):
                        case Content::fourCC("ACSR"):
                        {
                            const std::size_t size = sub.mData.size();
                            if (size % 16 != 0)
                            {
                                ++mOut.mOddSubrecords;
                                break;
                            }
                            if (!tally(loc.mSpecialRefs.size() + size / 16))
                                break;
                            for (std::size_t i = 0; i + 16 <= size; i += 16)
                            {
                                LocationSpecialRef r;
                                r.mRefType = resolved(le32(sub.mData.data() + i));
                                r.mRef = resolved(le32(sub.mData.data() + i + 4));
                                r.mWorld = resolved(le32(sub.mData.data() + i + 8));
                                r.mGrid = { le16s(sub.mData.data() + i + 12), le16s(sub.mData.data() + i + 14) };
                                loc.mSpecialRefs.push_back(r);
                            }
                            break;
                        }
                        case Content::fourCC("LCPR"):
                        case Content::fourCC("ACPR"):
                        case Content::fourCC("RCPR"):
                        {
                            const std::size_t size = sub.mData.size();
                            if (size % 12 != 0)
                            {
                                ++mOut.mOddSubrecords;
                                break;
                            }
                            if (!tally(loc.mPersistentRefs.size() + size / 12))
                                break;
                            for (std::size_t i = 0; i + 12 <= size; i += 12)
                            {
                                LocationPersistentRef r;
                                r.mRef = resolved(le32(sub.mData.data() + i));
                                r.mWorld = resolved(le32(sub.mData.data() + i + 4));
                                r.mGrid = { le16s(sub.mData.data() + i + 8), le16s(sub.mData.data() + i + 10) };
                                loc.mPersistentRefs.push_back(r);
                            }
                            break;
                        }
                        case Content::fourCC("LCUN"):
                        case Content::fourCC("ACUN"):
                        {
                            const std::size_t size = sub.mData.size();
                            if (size % 12 != 0)
                            {
                                ++mOut.mOddSubrecords;
                                break;
                            }
                            if (!tally(loc.mUniqueNpcs.size() + size / 12))
                                break;
                            for (std::size_t i = 0; i + 12 <= size; i += 12)
                            {
                                LocationUniqueNpc n;
                                n.mActor = resolved(le32(sub.mData.data() + i));
                                n.mRef = resolved(le32(sub.mData.data() + i + 4));
                                n.mLocation = resolved(le32(sub.mData.data() + i + 8));
                                loc.mUniqueNpcs.push_back(n);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                });
                if (!wellFormed)
                {
                    fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                    return true;
                }
                if (tooMany)
                {
                    fail(record, "too many keywords or list entries");
                    return true;
                }
                ++mOut.mCount.mParsed;
                mOut.mLocations.push_back(std::move(loc));
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

            void addError(Content::Error error)
            {
                ++mOut.mCount.mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const LocationLimits& mLimits;
            LocationRecords& mOut;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseLocationRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const LocationLimits& limits, LocationRecords& out)
    {
        out = LocationRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        LocationParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    void LocationIndex::add(LocationRecords records)
    {
        const std::uint32_t plugin = mAdded++;
        for (LocationRecord& l : records.mLocations)
        {
            l.mPlugin = plugin;
            if (l.mCommon.mId == kNullFormId || l.mCommon.mId == kUnresolvedFormId)
                continue;
            if (l.mCommon.deleted())
                mLocations.erase(l.mCommon.mId);
            else
                mLocations[l.mCommon.mId] = std::move(l);
        }
        mGridsDirty = true;
        mRefOwnersDirty = true;
    }

    std::vector<FormId> LocationIndex::ids() const
    {
        std::vector<FormId> out;
        out.reserve(mLocations.size());
        for (const auto& [id, record] : mLocations)
            out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }

    void LocationIndex::rebuildGrids() const
    {
        mGridOwner.clear();
        // the location with the fewest listed cells owns a square, ties to the lower id: visit in id order so that the
        // result does not depend on the hash map's order
        std::map<std::tuple<FormId, std::int32_t, std::int32_t>, std::pair<std::size_t, FormId>> best;
        for (const FormId id : ids())
        {
            const LocationRecord& l = mLocations.at(id);
            std::size_t total = 0;
            for (const LocationCells& c : l.mCells)
                total += c.mGrids.size();
            for (const LocationCells& c : l.mCells)
                for (const LocationGrid& g : c.mGrids)
                {
                    const auto key = std::make_tuple(c.mWorld, static_cast<std::int32_t>(g.mX), static_cast<std::int32_t>(g.mY));
                    const auto it = best.find(key);
                    if (it == best.end() || total < it->second.first)
                        best[key] = { total, id };
                }
        }
        for (const auto& [key, owner] : best)
            mGridOwner[key] = owner.second;
        mGridsDirty = false;
    }

    FormId LocationIndex::locationOfGrid(FormId world, std::int32_t x, std::int32_t y) const
    {
        if (mGridsDirty)
            rebuildGrids();
        const auto it = mGridOwner.find({ world, x, y });
        return it == mGridOwner.end() ? kNullFormId : it->second;
    }

    bool LocationIndex::gridOf(FormId location, FormId& world, std::int32_t& x, std::int32_t& y) const
    {
        const LocationRecord* l = this->location(location);
        if (l == nullptr)
            return false;
        for (const LocationCells& c : l->mCells)
            if (!c.mGrids.empty())
            {
                world = c.mWorld;
                x = c.mGrids.front().mX;
                y = c.mGrids.front().mY;
                return true;
            }
        return false;
    }

    std::vector<LocationSpecialRef> LocationIndex::specialRefsOfType(FormId location, FormId refType) const
    {
        std::vector<LocationSpecialRef> out;
        const LocationRecord* l = this->location(location);
        if (l == nullptr)
            return out;
        for (const LocationSpecialRef& r : l->mSpecialRefs)
            if (r.mRefType == refType)
                out.push_back(r);
        return out;
    }

    bool LocationIndex::hasRefType(FormId location, FormId refType) const
    {
        const LocationRecord* l = this->location(location);
        if (l == nullptr || refType == kNullFormId)
            return false;
        return std::any_of(l->mSpecialRefs.begin(), l->mSpecialRefs.end(),
            [refType](const LocationSpecialRef& r) { return r.mRefType == refType; });
    }

    const LocationRecord* LocationIndex::location(FormId id) const
    {
        const auto it = mLocations.find(id);
        return it == mLocations.end() ? nullptr : &it->second;
    }

    bool LocationIndex::isWithin(FormId current, FormId ancestor) const
    {
        if (current == kNullFormId || ancestor == kNullFormId)
            return false;
        FormId at = current;
        for (int step = 0; step < 64 && at != kNullFormId; ++step)
        {
            if (at == ancestor)
                return true;
            const LocationRecord* l = location(at);
            if (l == nullptr)
                return false;
            at = l->mParent;
        }
        return false;
    }

    bool LocationIndex::hasKeyword(FormId loc, FormId keyword) const
    {
        const LocationRecord* l = location(loc);
        return l != nullptr && keyword != kNullFormId
            && std::find(l->mKeywords.begin(), l->mKeywords.end(), keyword) != l->mKeywords.end();
    }

    FormId LocationIndex::ancestorWithKeyword(FormId location, FormId keyword) const
    {
        if (keyword == kNullFormId)
            return kNullFormId;
        FormId at = location;
        for (int step = 0; step < 64 && at != kNullFormId; ++step)
        {
            const LocationRecord* l = this->location(at);
            if (l == nullptr)
                return kNullFormId;
            if (hasKeyword(at, keyword))
                return at;
            at = l->mParent;
        }
        return kNullFormId;
    }

    FormId LocationIndex::descendantWithKeyword(FormId location, FormId keyword) const
    {
        if (location == kNullFormId || keyword == kNullFormId)
            return kNullFormId;
        FormId best = kNullFormId;
        int bestDepth = 0;
        for (const FormId id : ids())
        {
            if (id == location || !hasKeyword(id, keyword))
                continue;
            // the number of parent steps from id up to `location`
            int depth = 0;
            FormId at = id;
            bool inside = false;
            for (; depth < 64 && at != kNullFormId; ++depth)
            {
                if (at == location)
                {
                    inside = true;
                    break;
                }
                const LocationRecord* l = this->location(at);
                if (l == nullptr)
                    break;
                at = l->mParent;
            }
            if (inside && (best == kNullFormId || depth < bestDepth))
            {
                best = id;
                bestDepth = depth;
            }
        }
        return best;
    }

    void LocationIndex::rebuildRefOwners() const
    {
        mRefOwner.clear();
        const auto depthOf = [this](FormId id) {
            int depth = 0;
            for (FormId at = id; depth < 64 && at != kNullFormId;)
            {
                const LocationRecord* l = location(at);
                if (l == nullptr)
                    break;
                at = l->mParent;
                ++depth;
            }
            return depth;
        };
        std::unordered_map<FormId, int> depth;
        const auto offer = [&](FormId ref, FormId owner, int ownerDepth) {
            if (ref == kNullFormId || ref == kUnresolvedFormId)
                return;
            const auto it = mRefOwner.find(ref);
            if (it == mRefOwner.end() || ownerDepth > depth[ref])
            {
                mRefOwner[ref] = owner;
                depth[ref] = ownerDepth;
            }
        };
        for (const FormId id : ids()) // ascending: a tie keeps the lower id
        {
            const LocationRecord& l = mLocations.at(id);
            const int d = depthOf(id);
            for (const LocationSpecialRef& r : l.mSpecialRefs)
                offer(r.mRef, id, d);
            for (const LocationPersistentRef& r : l.mPersistentRefs)
                offer(r.mRef, id, d);
        }
        mRefOwnersDirty = false;
    }

    FormId LocationIndex::editorLocationOf(FormId reference) const
    {
        if (mRefOwnersDirty)
            rebuildRefOwners();
        const auto it = mRefOwner.find(reference);
        return it == mRefOwner.end() ? kNullFormId : it->second;
    }
}

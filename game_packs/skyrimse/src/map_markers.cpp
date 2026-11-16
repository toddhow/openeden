#include "game_skyrimse/map_markers.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// Layouts and decisions: see the header and docs/tasks/0201-skyrim-map-and-compass.md.

namespace game_skyrimse::map
{
    namespace
    {
        std::uint32_t le32(const std::byte* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }

        float lef(const std::byte* p)
        {
            float v;
            std::memcpy(&v, p, 4);
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

        class MarkerParser final : public Content::Visitor
        {
        public:
            MarkerParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const MarkerLimits& limits,
                MarkerRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onGroupBegin(const Content::GroupInfo& group) override
            {
                if (group.mType == 1) // world children: the label is the worldspace's stored id
                    mWorld = resolved(group.mLabel);
                return true;
            }

            bool onGroupEnd(const Content::GroupInfo& group) override
            {
                if (group.mType == 1)
                    mWorld = kNullFormId;
                return true;
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("REFR"))
                    return true;
                const bool deleted = (record.mFlags & RecordFlags::Deleted) != 0;
                std::span<const std::byte> bytes;
                if (!deleted)
                {
                    bytes = record.mData;
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
                }
                const FormId id = resolved(record.mFormId);
                if (id == kNullFormId || id == kUnresolvedFormId)
                    return true;
                Marker marker;
                marker.mRef = id;
                marker.mWorld = mWorld;
                marker.mDeleted = deleted;
                bool isMarker = false;
                if (!deleted)
                {
                    const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                        switch (sub.mType)
                        {
                            case Content::fourCC("XMRK"):
                                isMarker = true;
                                break;
                            case Content::fourCC("FNAM"):
                                if (sub.mData.size() != 1)
                                    ++mOut.mOddSubrecords;
                                else
                                    marker.mFlags = static_cast<std::uint8_t>(sub.mData[0]);
                                break;
                            case Content::fourCC("TNAM"):
                                if (sub.mData.size() != 2)
                                    ++mOut.mOddSubrecords;
                                else
                                    marker.mType = static_cast<std::uint8_t>(sub.mData[0]);
                                break;
                            case Content::fourCC("XLKR"):
                                // Task 0218: a link with a null keyword is the landing place of a fast travel (8 bytes:
                                // keyword, reference); a link with a keyword is another kind of link.
                                if (sub.mData.size() != 8)
                                    ++mOut.mOddSubrecords;
                                else if (le32(sub.mData.data()) == 0)
                                {
                                    const FormId link = resolved(le32(sub.mData.data() + 4));
                                    if (link != kUnresolvedFormId)
                                        marker.mLinked = link;
                                }
                                break;
                            case Content::fourCC("FULL"):
                                readName(sub.mData, marker.mName);
                                break;
                            case Content::fourCC("DATA"):
                                if (sub.mData.size() != 24)
                                    ++mOut.mOddSubrecords;
                                else
                                {
                                    marker.mX = lef(sub.mData.data());
                                    marker.mY = lef(sub.mData.data() + 4);
                                    marker.mZ = lef(sub.mData.data() + 8);
                                }
                                break;
                            default:
                                break;
                        }
                    });
                    if (!ok)
                    {
                        fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                        return true;
                    }
                    if (!isMarker)
                        return true;
                }
                else if (mOut.mMarkers.size() >= mLimits.mMaxMarkers)
                    return true;
                if (mOut.mMarkers.size() >= mLimits.mMaxMarkers)
                {
                    fail(record, "too many map markers");
                    return true;
                }
                mOut.mMarkers.push_back(std::move(marker));
                return true;
            }

        private:
            void readName(std::span<const std::byte> data, LString& out)
            {
                out.mPresent = true;
                if (mLocalized)
                {
                    if (data.size() != 4)
                    {
                        ++mOut.mOddSubrecords;
                        out.mPresent = false;
                        return;
                    }
                    out.mLocalized = true;
                    out.mId = le32(data.data());
                    return;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.mText.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                if (out.mText.size() > 256)
                    out.mText.resize(256);
            }

            FormId resolved(std::uint32_t stored)
            {
                if (stored == 0)
                    return kNullFormId;
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
                ++mOut.mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const MarkerLimits& mLimits;
            MarkerRecords& mOut;
            const bool mLocalized;
            FormId mWorld = kNullFormId;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseMarkers(
        const Content::Plugin& plugin, const LoadOrder& order, const MarkerLimits& limits, MarkerRecords& out)
    {
        out = MarkerRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        MarkerParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    // ---- the index -------------------------------------------------------------------------------------------------

    void MarkerIndex::add(MarkerRecords&& records)
    {
        const std::uint32_t plugin = mNextPlugin++;
        mMarkers.reserve(mMarkers.size() + records.mMarkers.size());
        for (Marker& m : records.mMarkers)
        {
            m.mPlugin = plugin;
            mMarkers.push_back(std::move(m));
        }
        records.mMarkers.clear();
    }

    void MarkerIndex::finish()
    {
        std::stable_sort(
            mMarkers.begin(), mMarkers.end(), [](const Marker& a, const Marker& b) { return a.mRef < b.mRef; });
        std::size_t out = 0;
        for (std::size_t i = 0; i < mMarkers.size();)
        {
            std::size_t j = i + 1;
            Marker merged = std::move(mMarkers[i]);
            for (; j < mMarkers.size() && mMarkers[j].mRef == merged.mRef; ++j)
                merged = std::move(mMarkers[j]); // the later plugin's record wins; a deleted one removes it
            if (!merged.mDeleted && merged.mWorld != kNullFormId && out < kMaxMarkers)
                mMarkers[out++] = std::move(merged);
            i = j;
        }
        mMarkers.resize(out);
        mMarkers.shrink_to_fit();
    }

    const Marker* MarkerIndex::find(FormId reference) const
    {
        const auto it = std::lower_bound(
            mMarkers.begin(), mMarkers.end(), reference, [](const Marker& m, FormId id) { return m.mRef < id; });
        return it != mMarkers.end() && it->mRef == reference ? &*it : nullptr;
    }

    bool MarkerIndex::hasWorld(FormId world) const
    {
        return std::any_of(mMarkers.begin(), mMarkers.end(), [world](const Marker& m) { return m.mWorld == world; });
    }

    // ---- discovery -------------------------------------------------------------------------------------------------

    bool Discovery::discovered(FormId marker) const
    {
        return std::binary_search(mFound.begin(), mFound.end(), marker);
    }

    std::size_t Discovery::visit(const MarkerIndex& index, FormId world, float x, float y, float radius)
    {
        return visit(index, std::span<const FormId>(&world, 1), x, y, radius);
    }

    std::size_t Discovery::visit(
        const MarkerIndex& index, std::span<const FormId> worlds, float x, float y, float radius)
    {
        if (!(radius >= 0.f) || !std::isfinite(x) || !std::isfinite(y))
            return 0;
        const double r2 = static_cast<double>(radius) * static_cast<double>(radius);
        std::size_t added = 0;
        for (const Marker& m : index.all())
        {
            if (std::find(worlds.begin(), worlds.end(), m.mWorld) == worlds.end())
                continue;
            const double dx = static_cast<double>(m.mX) - x;
            const double dy = static_cast<double>(m.mY) - y;
            if (dx * dx + dy * dy > r2 || discovered(m.mRef) || mFound.size() >= kMaxDiscovered)
                continue;
            mFound.insert(std::lower_bound(mFound.begin(), mFound.end(), m.mRef), m.mRef);
            ++added;
        }
        if (added > 0)
            ++mRevision;
        return added;
    }

    void Discovery::restore(std::vector<FormId> found)
    {
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        if (found.size() > kMaxDiscovered)
            found.resize(kMaxDiscovered);
        mFound = std::move(found);
        ++mRevision;
    }

    // ---- queries ---------------------------------------------------------------------------------------------------

    void query(const MarkerIndex& index, const Discovery& state, FormId world, float x, float y, float range,
        float hintRange, std::size_t skip, std::size_t count, std::vector<View>& out)
    {
        query(index, state, std::span<const FormId>(&world, 1), x, y, range, hintRange, skip, count, out);
    }

    void query(const MarkerIndex& index, const Discovery& state, std::span<const FormId> worlds, float x, float y,
        float range, float hintRange, std::size_t skip, std::size_t count, std::vector<View>& out)
    {
        out.clear();
        std::vector<View> all;
        const double range2 = static_cast<double>(range) * range;
        const double hint2 = static_cast<double>(hintRange) * hintRange;
        for (const Marker& m : index.all())
        {
            if (std::find(worlds.begin(), worlds.end(), m.mWorld) == worlds.end())
                continue;
            const double dx = static_cast<double>(m.mX) - x;
            const double dy = static_cast<double>(m.mY) - y;
            const double d2 = dx * dx + dy * dy;
            const bool shown = state.shown(m);
            if (!((shown && d2 <= range2) || (!shown && d2 <= hint2)))
                continue;
            View v;
            v.mMarker = &m;
            v.mDistance = static_cast<float>(std::sqrt(d2));
            v.mDiscovered = state.discovered(m.mRef) || (m.mFlags & MarkerFlags::Visible) != 0;
            all.push_back(v);
        }
        std::stable_sort(all.begin(), all.end(), [](const View& a, const View& b) {
            return a.mDistance != b.mDistance ? a.mDistance < b.mDistance : a.mMarker->mRef < b.mMarker->mRef;
        });
        for (std::size_t i = skip; i < all.size() && out.size() < count; ++i)
            out.push_back(all[i]);
    }

}

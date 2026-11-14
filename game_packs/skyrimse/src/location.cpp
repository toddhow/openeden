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
                    fail(record, "too many keywords");
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
        for (LocationRecord& l : records.mLocations)
        {
            if (l.mCommon.mId == kNullFormId || l.mCommon.mId == kUnresolvedFormId)
                continue;
            if (l.mCommon.deleted())
                mLocations.erase(l.mCommon.mId);
            else
                mLocations[l.mCommon.mId] = std::move(l);
        }
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
}

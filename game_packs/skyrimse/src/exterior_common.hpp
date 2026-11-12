#ifndef OPENEDEN_GAME_SKYRIMSE_EXTERIOR_COMMON_H
#define OPENEDEN_GAME_SKYRIMSE_EXTERIOR_COMMON_H

// Internal helpers shared by land.cpp and worldspace.cpp (task 0117): a subrecord walker and small field readers. Not
// installed, not part of the pack's interface.

#include "game_skyrimse/land.hpp"

#include <cstring>
#include <span>
#include <string>

namespace game_skyrimse::detail
{
    template <class T>
    T readLe(const std::byte* p)
    {
        T value;
        std::memcpy(&value, p, sizeof(T));
        return value;
    }

    inline std::string tag(std::uint32_t type)
    {
        return Content::fourCCToString(type);
    }

    /// Walks the subrecords of one record's (inflated) bytes. XXXX carries the 32 bit size of the next one. Every size
    /// is checked against the bytes that remain before a span is made.
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
                    return fail(tag(type) + ": the subrecord claims " + std::to_string(size) + " bytes but "
                        + std::to_string(mData.size() - mPos) + " remain in the record");
                if (type == Content::fourCC("XXXX"))
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

    /// Field readers over a DecodeContext. Each returns false with `why` set for a hostile value; a wrong size is
    /// counted in the notes and the field skipped.
    struct Fields
    {
        const DecodeContext& mContext;
        const char* mRecord; // "LAND"
        std::string& mWhy;

        void odd(std::uint32_t sub) const { mContext.mNotes.odd(mRecord, tag(sub).c_str()); }

        FormId resolved(std::uint32_t stored) const
        {
            const std::optional<FormId> id = mContext.mResolver.resolve(stored);
            if (!id)
            {
                ++mContext.mNotes.mUnresolvedFormIds;
                return kUnresolvedFormId;
            }
            return *id;
        }

        RecordCommon common(const Content::RawRecord& record) const
        {
            RecordCommon c;
            c.mStoredId = record.mFormId;
            c.mId = resolved(record.mFormId);
            c.mFlags = record.mFlags;
            c.mOffset = record.mOffset;
            return c;
        }

        bool zstring(std::uint32_t sub, std::span<const std::byte> data, std::string& out) const
        {
            if (data.size() > mContext.mLimits.mMaxStringBytes)
            {
                mWhy = tag(sub) + ": " + std::to_string(data.size()) + " bytes, over the string limit of "
                    + std::to_string(mContext.mLimits.mMaxStringBytes);
                return false;
            }
            const char* begin = reinterpret_cast<const char*>(data.data());
            const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
            out.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
            return true;
        }

        bool lstring(std::uint32_t sub, std::span<const std::byte> data, LString& out) const
        {
            if (mContext.mLocalized)
            {
                if (data.size() != 4)
                {
                    odd(sub);
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

        void form(std::uint32_t sub, std::span<const std::byte> data, FormId& out) const
        {
            if (data.size() != 4)
                odd(sub);
            else
                out = resolved(readLe<std::uint32_t>(data.data()));
        }
    };
}

#endif

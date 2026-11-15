#include "game_skyrimse/vmad.hpp"

#include <cstring>
#include <string>

// VMAD layout: see the header (task 0176).

namespace game_skyrimse::vmad
{
    namespace
    {
        class Reader
        {
        public:
            Reader(std::span<const std::byte> data, const Limits& limits)
                : mData(data)
                , mLimits(limits)
            {
            }

            [[nodiscard]] std::size_t offset() const { return mAt; }
            [[nodiscard]] bool atEnd() const { return mAt == mData.size(); }
            [[nodiscard]] const std::string& error() const { return mError; }

            bool fail(const std::string& what)
            {
                if (mError.empty())
                    mError = "offset " + std::to_string(mAt) + ": " + what;
                return false;
            }

            bool u8(std::uint8_t& v)
            {
                if (mData.size() - mAt < 1)
                    return fail("the subrecord ends inside a byte");
                v = static_cast<std::uint8_t>(mData[mAt++]);
                return true;
            }
            bool u16(std::uint16_t& v)
            {
                if (mData.size() - mAt < 2)
                    return fail("the subrecord ends inside a 16 bit number");
                v = static_cast<std::uint16_t>(
                    static_cast<std::uint8_t>(mData[mAt]) | (static_cast<std::uint8_t>(mData[mAt + 1]) << 8));
                mAt += 2;
                return true;
            }
            bool i16(std::int16_t& v)
            {
                std::uint16_t u;
                if (!u16(u))
                    return false;
                v = static_cast<std::int16_t>(u);
                return true;
            }
            bool u32(std::uint32_t& v)
            {
                if (mData.size() - mAt < 4)
                    return fail("the subrecord ends inside a 32 bit number");
                v = 0;
                for (int i = 3; i >= 0; --i)
                    v = (v << 8) | static_cast<std::uint8_t>(mData[mAt + static_cast<std::size_t>(i)]);
                mAt += 4;
                return true;
            }
            bool i32(std::int32_t& v)
            {
                std::uint32_t u;
                if (!u32(u))
                    return false;
                v = static_cast<std::int32_t>(u);
                return true;
            }
            bool f32(float& v)
            {
                std::uint32_t u;
                if (!u32(u))
                    return false;
                static_assert(sizeof(float) == sizeof(std::uint32_t));
                std::memcpy(&v, &u, 4);
                return true;
            }
            bool wstring(std::string& out, std::uint32_t maxBytes)
            {
                std::uint16_t n;
                if (!u16(n))
                    return false;
                if (n > maxBytes)
                    return fail("a name of " + std::to_string(n) + " bytes is over the limit");
                if (mData.size() - mAt < n)
                    return fail("a name of " + std::to_string(n) + " bytes runs past the end of the subrecord");
                out.assign(reinterpret_cast<const char*>(mData.data() + mAt), n);
                mAt += n;
                return true;
            }

            [[nodiscard]] const Limits& limits() const { return mLimits; }
            [[nodiscard]] std::size_t remaining() const { return mData.size() - mAt; }

        private:
            std::span<const std::byte> mData;
            const Limits& mLimits;
            std::size_t mAt = 0;
            std::string mError;
        };

        bool readObject(Reader& r, std::int16_t format, Value& v)
        {
            v.mKind = ValueKind::Object;
            std::uint16_t unused;
            if (format == 1)
                return r.u32(v.mForm) && r.i16(v.mAlias) && r.u16(unused);
            return r.u16(unused) && r.i16(v.mAlias) && r.u32(v.mForm);
        }

        bool readValue(Reader& r, std::int16_t format, std::uint8_t type, Value& v)
        {
            switch (type)
            {
                case 1:
                    return readObject(r, format, v);
                case 2:
                    v.mKind = ValueKind::String;
                    return r.wstring(v.mText, r.limits().mMaxTextBytes);
                case 3:
                    v.mKind = ValueKind::Int;
                    return r.i32(v.mInteger);
                case 4:
                    v.mKind = ValueKind::Float;
                    return r.f32(v.mNumber);
                case 5:
                {
                    v.mKind = ValueKind::Bool;
                    std::uint8_t b;
                    if (!r.u8(b))
                        return false;
                    v.mInteger = b != 0 ? 1 : 0;
                    return true;
                }
                default:
                    return r.fail("property value type " + std::to_string(type) + " is not known");
            }
        }

        // The smallest a value of the type can be, to refuse an array count the bytes left cannot hold.
        std::size_t minimumBytes(std::uint8_t type)
        {
            switch (type)
            {
                case 1:
                    return 8;
                case 2:
                    return 2;
                case 3:
                case 4:
                    return 4;
                default:
                    return 1;
            }
        }

        bool readProperty(Reader& r, std::int16_t format, Property& p)
        {
            if (!r.wstring(p.mName, r.limits().mMaxNameBytes) || !r.u8(p.mType) || !r.u8(p.mStatus))
                return false;
            if (p.mType >= 11 && p.mType <= 15)
            {
                p.mArray = true;
                std::uint32_t count;
                if (!r.u32(count))
                    return false;
                const std::uint8_t element = static_cast<std::uint8_t>(p.mType - 10);
                if (count > r.limits().mMaxArray)
                    return r.fail("an array of " + std::to_string(count) + " values is over the limit");
                if (static_cast<std::uint64_t>(count) * minimumBytes(element) > r.remaining())
                    return r.fail("an array of " + std::to_string(count) + " values runs past the end");
                p.mElements.resize(count);
                for (Value& v : p.mElements)
                    if (!readValue(r, format, element, v))
                        return false;
                return true;
            }
            return readValue(r, format, p.mType, p.mValue);
        }

        bool readScripts(Reader& r, std::int16_t format, std::uint16_t count, std::vector<Script>& out)
        {
            if (count > r.limits().mMaxScripts)
                return r.fail(std::to_string(count) + " scripts are over the limit");
            out.reserve(count);
            for (std::uint16_t i = 0; i < count; ++i)
            {
                Script s;
                std::uint16_t props;
                if (!r.wstring(s.mName, r.limits().mMaxNameBytes) || !r.u8(s.mStatus) || !r.u16(props))
                    return false;
                if (props > r.limits().mMaxProperties)
                    return r.fail(std::to_string(props) + " properties are over the limit");
                if (static_cast<std::size_t>(props) * 4 > r.remaining())
                    return r.fail(std::to_string(props) + " properties run past the end");
                s.mProperties.resize(props);
                out.push_back(std::move(s));
                for (Property& p : out.back().mProperties)
                    if (!readProperty(r, format, p))
                        return false;
            }
            return true;
        }

        bool readHeader(Reader& r, std::int16_t& version, std::int16_t& format, std::uint16_t& count)
        {
            if (!r.i16(version) || !r.i16(format) || !r.u16(count))
                return false;
            if (format != 1 && format != 2)
                return r.fail("object format " + std::to_string(format) + " is not known");
            return true;
        }

        bool readFragments(Reader& r, Vmad& v)
        {
            std::uint8_t marker;
            std::uint16_t count;
            if (!r.u8(marker) || !r.u16(count) || !r.wstring(v.mFragmentScript, r.limits().mMaxNameBytes))
                return false;
            v.mHasFragments = true;
            if (count > r.limits().mMaxFragments)
                return r.fail(std::to_string(count) + " fragments are over the limit");
            v.mFragments.reserve(count);
            for (std::uint16_t i = 0; i < count; ++i)
            {
                Fragment f;
                std::int16_t unknown16;
                std::uint8_t unknown8;
                if (!r.u16(f.mStage) || !r.i16(unknown16) || !r.i32(f.mLogEntry) || !r.u8(unknown8)
                    || !r.wstring(f.mScript, r.limits().mMaxNameBytes)
                    || !r.wstring(f.mFunction, r.limits().mMaxNameBytes))
                    return false;
                v.mFragments.push_back(std::move(f));
            }
            std::uint16_t aliases;
            if (!r.u16(aliases))
                return false;
            if (aliases > r.limits().mMaxAliases)
                return r.fail(std::to_string(aliases) + " aliases are over the limit");
            v.mAliases.reserve(aliases);
            for (std::uint16_t i = 0; i < aliases; ++i)
            {
                AliasScripts a;
                Value object;
                std::int16_t aliasVersion;
                std::int16_t aliasFormat;
                std::uint16_t scripts;
                if (!readObject(r, v.mObjectFormat, object))
                    return false;
                a.mQuest = object.mForm;
                a.mAlias = object.mAlias;
                if (!readHeader(r, aliasVersion, aliasFormat, scripts))
                    return false;
                const bool ok = readScripts(r, aliasFormat, scripts, a.mScripts);
                v.mAliases.push_back(std::move(a));
                if (!ok)
                    return false;
            }
            return true;
        }

        std::size_t resolveValue(Value& value, const std::function<std::uint32_t(std::uint32_t)>& resolve)
        {
            if (value.mKind != ValueKind::Object || value.mForm == 0)
                return 0;
            const std::uint32_t resolved = resolve(value.mForm);
            value.mForm = resolved;
            return resolved == 0 ? 1 : 0;
        }
    }

    ParseResult parse(std::span<const std::byte> bytes, bool withFragments, const Limits& limits)
    {
        ParseResult result;
        Reader r(bytes, limits);
        Vmad& v = result.mVmad;
        std::uint16_t count = 0;
        bool ok
            = readHeader(r, v.mVersion, v.mObjectFormat, count) && readScripts(r, v.mObjectFormat, count, v.mScripts);
        if (ok && withFragments && !r.atEnd())
            ok = readFragments(r, v);
        if (ok && !r.atEnd())
            ok = r.fail(std::to_string(r.remaining()) + " bytes are left after the last field");
        result.mOk = ok;
        if (!ok)
            result.mError = r.error();
        return result;
    }

    std::size_t resolveForms(std::vector<Script>& scripts, const std::function<std::uint32_t(std::uint32_t)>& resolve)
    {
        std::size_t missing = 0;
        for (Script& s : scripts)
            for (Property& p : s.mProperties)
            {
                missing += resolveValue(p.mValue, resolve);
                for (Value& e : p.mElements)
                    missing += resolveValue(e, resolve);
            }
        return missing;
    }

    std::size_t resolveForms(Vmad& vmad, const std::function<std::uint32_t(std::uint32_t)>& resolve)
    {
        std::size_t missing = resolveForms(vmad.mScripts, resolve);
        for (AliasScripts& a : vmad.mAliases)
        {
            if (a.mQuest != 0)
            {
                const std::uint32_t resolved = resolve(a.mQuest);
                if (resolved == 0)
                    ++missing;
                a.mQuest = resolved;
            }
            missing += resolveForms(a.mScripts, resolve);
        }
        return missing;
    }
}

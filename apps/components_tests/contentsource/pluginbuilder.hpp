#ifndef OPENEDEN_COMPONENTS_TESTS_CONTENTSOURCE_PLUGINBUILDER_H
#define OPENEDEN_COMPONENTS_TESTS_CONTENTSOURCE_PLUGINBUILDER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <components/contentsource/plugin.hpp>

// Generated plugins for the contentsource tests (task 0083). No game data: every byte here is written by the test.
namespace ContentTests
{
    class Bytes
    {
    public:
        Bytes& u8(std::uint8_t v)
        {
            mData.push_back(static_cast<char>(v));
            return *this;
        }
        Bytes& u16(std::uint16_t v) { return raw(&v, 2); }
        Bytes& u32(std::uint32_t v) { return raw(&v, 4); }
        Bytes& u64(std::uint64_t v) { return raw(&v, 8); }
        Bytes& f32(float v) { return raw(&v, 4); }
        Bytes& fourCC(const char (&name)[5]) { return u32(Content::fourCC(name)); }
        Bytes& text(const std::string& s)
        {
            mData += s;
            return *this;
        }
        Bytes& zstring(const std::string& s)
        {
            mData += s;
            mData.push_back('\0');
            return *this;
        }
        Bytes& fixed(const std::string& s, std::size_t size)
        {
            std::string copy = s;
            copy.resize(size, '\0');
            mData += copy;
            return *this;
        }
        Bytes& append(const Bytes& other)
        {
            mData += other.mData;
            return *this;
        }
        std::size_t size() const { return mData.size(); }
        const std::string& str() const { return mData; }
        std::string& str() { return mData; }

    private:
        Bytes& raw(const void* p, std::size_t n)
        {
            mData.append(static_cast<const char*>(p), n);
            return *this;
        }
        std::string mData;
    };

    // TES3 subrecord: name, u32 size, data.
    inline Bytes sub3(const char (&name)[5], const Bytes& data)
    {
        Bytes b;
        b.fourCC(name).u32(static_cast<std::uint32_t>(data.size())).append(data);
        return b;
    }

    // TES3 record: type, u32 size, u32 zero, u32 flags, then the subrecords.
    inline Bytes record3(const char (&type)[5], std::uint32_t flags, const Bytes& subs)
    {
        Bytes b;
        b.fourCC(type).u32(static_cast<std::uint32_t>(subs.size())).u32(0).u32(flags).append(subs);
        return b;
    }

    struct Master
    {
        std::string mName;
        std::uint64_t mSize;
    };

    inline Bytes tes3Header(float version, std::uint32_t type, const std::string& author, const std::string& desc,
        std::uint32_t records, const std::vector<Master>& masters)
    {
        Bytes hedr;
        hedr.f32(version).u32(type).fixed(author, 32).fixed(desc, 256).u32(records);
        Bytes subs = sub3("HEDR", hedr);
        for (const Master& m : masters)
        {
            subs.append(sub3("MAST", Bytes().zstring(m.mName)));
            subs.append(sub3("DATA", Bytes().u64(m.mSize)));
        }
        return record3("TES3", 0, subs);
    }

    // TES4 family subrecord: name, u16 size, data.
    inline Bytes sub4(const char (&name)[5], const Bytes& data)
    {
        Bytes b;
        b.fourCC(name).u16(static_cast<std::uint16_t>(data.size())).append(data);
        return b;
    }

    // TES4 family record. headerSize is 20 (Oblivion) or 24 (Fallout 3/NV, Skyrim, Fallout 4).
    inline Bytes record4(const char (&type)[5], std::uint32_t flags, std::uint32_t formId, const Bytes& data,
        std::uint32_t headerSize = 24, std::uint16_t formVersion = 44)
    {
        Bytes b;
        b.fourCC(type).u32(static_cast<std::uint32_t>(data.size())).u32(flags).u32(formId).u32(0);
        if (headerSize == 24)
        {
            b.u16(formVersion).u16(0);
        }
        b.append(data);
        return b;
    }

    // TES4 family group: GRUP, u32 size including this header, label, i32 type, stamp.
    inline Bytes group4(std::uint32_t label, std::int32_t type, const Bytes& children, std::uint32_t headerSize = 24)
    {
        Bytes b;
        b.fourCC("GRUP")
            .u32(static_cast<std::uint32_t>(headerSize + children.size()))
            .u32(label)
            .u32(static_cast<std::uint32_t>(type));
        b.u16(0).u16(0);
        if (headerSize == 24)
        {
            b.u16(0).u16(0);
        }
        b.append(children);
        return b;
    }

    inline Bytes tes4Header(float version, std::uint32_t flags, const std::string& author, const std::string& desc,
        std::uint32_t records, const std::vector<Master>& masters, std::uint32_t headerSize = 24,
        std::uint16_t formVersion = 44)
    {
        Bytes subs;
        subs.append(sub4("HEDR", Bytes().f32(version).u32(records).u32(0x800)));
        if (!author.empty())
        {
            subs.append(sub4("CNAM", Bytes().zstring(author)));
        }
        if (!desc.empty())
        {
            subs.append(sub4("SNAM", Bytes().zstring(desc)));
        }
        for (const Master& m : masters)
        {
            subs.append(sub4("MAST", Bytes().zstring(m.mName)));
            subs.append(sub4("DATA", Bytes().u64(m.mSize)));
        }
        return record4("TES4", flags, 0, subs, headerSize, formVersion);
    }
}

#endif

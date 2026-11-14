#include "packfile.hpp"

#include <algorithm>
#include <cstring>

namespace HavokPack
{
    namespace
    {
        // The facts below are logged, with how each was established, in docs/research/0054-provenance.md (F1 to F12).
        constexpr std::uint32_t Magic0 = 0x57E0E057;
        constexpr std::uint32_t Magic1 = 0x10C0C010;
        constexpr std::uint32_t SupportedFileVersion = 8;
        constexpr std::size_t HeaderSize = 64;
        constexpr std::size_t SectionHeaderSize = 48;
        constexpr std::size_t SectionNameSize = 19;
        constexpr std::uint32_t FixupEnd = 0xFFFFFFFFu;
        constexpr std::uint8_t ClassNameMarker = 9;

        std::string where(const std::string& what, std::size_t offset)
        {
            return what + " (offset " + std::to_string(offset) + ")";
        }

        std::uint32_t le32(const std::vector<std::uint8_t>& file, std::size_t at)
        {
            std::uint32_t v;
            std::memcpy(&v, file.data() + at, 4);
            return v;
        }
    }

    void Packfile::fail(const std::string& what, std::size_t offset) const
    {
        throw Error(where(what, offset));
    }

    Packfile Packfile::parse(std::span<const std::uint8_t> bytes, const Limits& limits)
    {
        if (bytes.size() > limits.mMaxFileBytes)
            throw Error(where("havok packfile too large (" + std::to_string(bytes.size()) + " bytes)", 0));
        if (bytes.size() < HeaderSize)
            throw Error(where("havok packfile is shorter than its header", bytes.size()));

        Packfile p;
        p.mLimits = limits;
        p.mFile.assign(bytes.begin(), bytes.end());
        const auto& f = p.mFile;
        const std::size_t size = f.size();

        if (le32(f, 0) != Magic0 || le32(f, 4) != Magic1)
            p.fail("not a havok packfile (bad magic)", 0);
        p.mFileVersion = le32(f, 12);
        if (p.mFileVersion != SupportedFileVersion)
            p.fail("unsupported packfile version " + std::to_string(p.mFileVersion), 12);
        if (f[16] != 8)
            p.fail("unsupported pointer size " + std::to_string(f[16]) + " (only the 64-bit layout is read)", 16);
        if (f[17] != 1)
            p.fail("unsupported endianness (only little endian is read)", 17);

        const std::uint32_t sectionCount = le32(f, 20);
        if (sectionCount == 0 || sectionCount > limits.mMaxSections)
            p.fail("bad section count " + std::to_string(sectionCount), 20);
        const std::uint32_t contentsSection = le32(f, 24);
        const std::uint32_t classNameSection = le32(f, 32);
        if (contentsSection >= sectionCount)
            p.fail("contents section index out of range", 24);
        if (classNameSection >= sectionCount)
            p.fail("class name section index out of range", 32);

        {
            std::size_t end = 40;
            while (end < 56 && f[end] != 0 && f[end] != 0xFF)
                ++end;
            p.mVersion.assign(reinterpret_cast<const char*>(f.data() + 40), end - 40);
        }

        if (HeaderSize + static_cast<std::size_t>(sectionCount) * SectionHeaderSize > size)
            p.fail("section table runs past the end of the file", HeaderSize);

        p.mSections.resize(sectionCount);
        for (std::uint32_t i = 0; i < sectionCount; ++i)
        {
            const std::size_t at = HeaderSize + static_cast<std::size_t>(i) * SectionHeaderSize;
            Section& s = p.mSections[i];
            std::size_t n = 0;
            while (n < SectionNameSize && f[at + n] != 0)
                ++n;
            s.mName.assign(reinterpret_cast<const char*>(f.data() + at), n);
            s.mDataStart = le32(f, at + 20);
            s.mLocalFixups = le32(f, at + 24);
            s.mGlobalFixups = le32(f, at + 28);
            s.mVirtualFixups = le32(f, at + 32);
            s.mExports = le32(f, at + 36);
            s.mImports = le32(f, at + 40);
            s.mEnd = le32(f, at + 44);
            s.mDataSize = s.mLocalFixups;
            if (static_cast<std::uint64_t>(s.mDataStart) + s.mEnd > size)
                p.fail("section '" + s.mName + "' runs past the end of the file", at + 20);
            if (!(s.mLocalFixups <= s.mGlobalFixups && s.mGlobalFixups <= s.mVirtualFixups
                    && s.mVirtualFixups <= s.mExports && s.mExports <= s.mImports && s.mImports <= s.mEnd))
                p.fail("section '" + s.mName + "' has fix-up tables out of order", at + 24);
        }

        // Class names: a 4 byte signature, the marker 9, then a NUL terminated name. The table ends where the marker
        // is missing (the remainder is padding).
        std::vector<std::pair<std::uint32_t, std::uint32_t>> classAt; // offset of the name -> index
        {
            const Section& cs = p.mSections[classNameSection];
            std::size_t q = 0;
            while (q + 5 <= cs.mDataSize && f[cs.mDataStart + q + 4] == ClassNameMarker)
            {
                std::size_t e = q + 5;
                while (e < cs.mDataSize && f[cs.mDataStart + e] != 0)
                    ++e;
                if (e >= cs.mDataSize)
                    p.fail("class name is not terminated", cs.mDataStart + q);
                if (e - (q + 5) > limits.mMaxClassNameLength)
                    p.fail("class name is too long", cs.mDataStart + q);
                if (p.mClassNames.size() >= limits.mMaxClasses)
                    p.fail("too many classes", cs.mDataStart + q);
                classAt.emplace_back(
                    static_cast<std::uint32_t>(q + 5), static_cast<std::uint32_t>(p.mClassNames.size()));
                p.mClassNames.emplace_back(
                    reinterpret_cast<const char*>(f.data() + cs.mDataStart + q + 5), e - (q + 5));
                q = e + 1;
            }
        }

        p.mPointers.resize(sectionCount);
        for (std::uint32_t si = 0; si < sectionCount; ++si)
        {
            const Section& s = p.mSections[si];
            const std::size_t base = s.mDataStart;

            // local fix-ups: (slot, target) in this section
            {
                std::uint32_t count = 0;
                for (std::size_t q = s.mLocalFixups; q + 8 <= s.mGlobalFixups; q += 8)
                {
                    const std::uint32_t src = le32(f, base + q);
                    if (src == FixupEnd)
                        break;
                    const std::uint32_t dst = le32(f, base + q + 4);
                    if (++count > limits.mMaxFixupsPerTable)
                        p.fail("too many local fix-ups", base + q);
                    if (static_cast<std::uint64_t>(src) + 8 > s.mDataSize || dst > s.mDataSize)
                        p.fail("local fix-up leaves the section", base + q);
                    p.mPointers[si].push_back({ src, { si, dst } });
                }
            }
            // global fix-ups: (slot, target section, target offset)
            {
                std::uint32_t count = 0;
                for (std::size_t q = s.mGlobalFixups; q + 12 <= s.mVirtualFixups; q += 12)
                {
                    const std::uint32_t src = le32(f, base + q);
                    if (src == FixupEnd)
                        break;
                    const std::uint32_t dsec = le32(f, base + q + 4);
                    const std::uint32_t dst = le32(f, base + q + 8);
                    if (++count > limits.mMaxFixupsPerTable)
                        p.fail("too many global fix-ups", base + q);
                    if (static_cast<std::uint64_t>(src) + 8 > s.mDataSize || dsec >= sectionCount
                        || dst > p.mSections[dsec].mDataSize)
                        p.fail("global fix-up leaves the section", base + q);
                    p.mPointers[si].push_back({ src, { dsec, dst } });
                }
            }
            std::sort(p.mPointers[si].begin(), p.mPointers[si].end(),
                [](const Fixup& a, const Fixup& b) { return a.mSource < b.mSource; });
            for (std::size_t i = 1; i < p.mPointers[si].size(); ++i)
                if (p.mPointers[si][i].mSource == p.mPointers[si][i - 1].mSource)
                    p.fail("two fix-ups for one pointer slot", base + p.mPointers[si][i].mSource);

            // virtual fix-ups: (object start, class names section, name offset)
            {
                std::uint32_t count = 0;
                for (std::size_t q = s.mVirtualFixups; q + 12 <= s.mExports; q += 12)
                {
                    const std::uint32_t src = le32(f, base + q);
                    if (src == FixupEnd)
                        break;
                    const std::uint32_t csec = le32(f, base + q + 4);
                    const std::uint32_t name = le32(f, base + q + 8);
                    if (++count > limits.mMaxFixupsPerTable || p.mObjects.size() >= limits.mMaxObjects)
                        p.fail("too many objects (virtual fix-ups)", base + q);
                    if (src >= s.mDataSize)
                        p.fail("virtual fix-up leaves the section", base + q);
                    if (csec != classNameSection)
                        p.fail("virtual fix-up names a section that holds no class names", base + q);
                    const auto it = std::lower_bound(classAt.begin(), classAt.end(), std::make_pair(name, 0u));
                    if (it == classAt.end() || it->first != name)
                        p.fail(
                            "virtual fix-up names no class (class name offset " + std::to_string(name) + ")", base + q);
                    p.mObjects.push_back({ si, src, 0, it->second });
                }
            }
        }

        std::sort(p.mObjects.begin(), p.mObjects.end(), [](const Object& a, const Object& b) {
            return a.mSection != b.mSection ? a.mSection < b.mSection : a.mOffset < b.mOffset;
        });
        for (std::size_t i = 0; i < p.mObjects.size(); ++i)
        {
            Object& o = p.mObjects[i];
            const bool nextSame = i + 1 < p.mObjects.size() && p.mObjects[i + 1].mSection == o.mSection;
            if (nextSame && p.mObjects[i + 1].mOffset == o.mOffset)
                p.fail("two objects at one place, listed twice", p.mSections[o.mSection].mDataStart + o.mOffset);
            const std::uint32_t end = nextSame ? p.mObjects[i + 1].mOffset : p.mSections[o.mSection].mDataSize;
            o.mSize = end - o.mOffset;
        }
        return p;
    }

    std::span<const std::uint8_t> Packfile::sectionData(std::uint32_t section) const
    {
        if (section >= mSections.size())
            fail("no such section " + std::to_string(section), 0);
        const Section& s = mSections[section];
        return { mFile.data() + s.mDataStart, s.mDataSize };
    }

    void Packfile::check(Ref at, std::size_t size) const
    {
        if (at.mSection >= mSections.size())
            fail("no such section " + std::to_string(at.mSection), 0);
        const Section& s = mSections[at.mSection];
        if (static_cast<std::uint64_t>(at.mOffset) + size > s.mDataSize)
            fail("read of " + std::to_string(size) + " bytes at section " + std::to_string(at.mSection) + " offset "
                    + std::to_string(at.mOffset) + " leaves the section's data",
                static_cast<std::size_t>(s.mDataStart) + at.mOffset);
    }

    const Object* Packfile::findObject(Ref at) const
    {
        const auto it = std::lower_bound(mObjects.begin(), mObjects.end(), at, [](const Object& o, const Ref& r) {
            return o.mSection != r.mSection ? o.mSection < r.mSection : o.mOffset < r.mOffset;
        });
        if (it == mObjects.end() || it->mSection != at.mSection || it->mOffset != at.mOffset)
            return nullptr;
        return &*it;
    }

    std::vector<const Object*> Packfile::objectsOfClass(std::string_view name) const
    {
        std::vector<const Object*> out;
        for (const Object& o : mObjects)
            if (mClassNames[o.mClass] == name)
                out.push_back(&o);
        return out;
    }

    std::optional<Ref> Packfile::pointerAt(Ref slot) const
    {
        check(slot, 8);
        const auto& list = mPointers[slot.mSection];
        const auto it = std::lower_bound(
            list.begin(), list.end(), slot.mOffset, [](const Fixup& a, std::uint32_t v) { return a.mSource < v; });
        if (it != list.end() && it->mSource == slot.mOffset)
            return it->mTarget;
        if (u64(slot) != 0)
            fail("pointer slot holds a value but has no fix-up",
                static_cast<std::size_t>(mSections[slot.mSection].mDataStart) + slot.mOffset);
        return std::nullopt;
    }

    ArrayRef Packfile::arrayAt(Ref slot) const
    {
        check(slot, 16);
        ArrayRef a;
        a.mData = pointerAt(slot);
        a.mSize = u32({ slot.mSection, slot.mOffset + 8 });
        if (!a.mData && a.mSize != 0)
            fail("array has elements but no data pointer",
                static_cast<std::size_t>(mSections[slot.mSection].mDataStart) + slot.mOffset);
        return a;
    }

    std::span<const std::uint8_t> Packfile::bytes(Ref at, std::size_t size) const
    {
        check(at, size);
        return { mFile.data() + mSections[at.mSection].mDataStart + at.mOffset, size };
    }

    std::uint8_t Packfile::u8(Ref at) const
    {
        return bytes(at, 1)[0];
    }

    std::uint16_t Packfile::u16(Ref at) const
    {
        std::uint16_t v;
        std::memcpy(&v, bytes(at, 2).data(), 2);
        return v;
    }

    std::uint32_t Packfile::u32(Ref at) const
    {
        std::uint32_t v;
        std::memcpy(&v, bytes(at, 4).data(), 4);
        return v;
    }

    std::uint64_t Packfile::u64(Ref at) const
    {
        std::uint64_t v;
        std::memcpy(&v, bytes(at, 8).data(), 8);
        return v;
    }

    float Packfile::f32(Ref at) const
    {
        float v;
        std::memcpy(&v, bytes(at, 4).data(), 4);
        return v;
    }

    std::string_view Packfile::cstring(Ref at) const
    {
        check(at, 1);
        const Section& s = mSections[at.mSection];
        const char* base = reinterpret_cast<const char*>(mFile.data() + s.mDataStart);
        const std::size_t room = std::min<std::size_t>(s.mDataSize - at.mOffset, mLimits.mMaxStringLength);
        for (std::size_t i = 0; i < room; ++i)
            if (base[at.mOffset + i] == 0)
                return { base + at.mOffset, i };
        fail("string is not terminated inside its section (or is too long)",
            static_cast<std::size_t>(s.mDataStart) + at.mOffset);
    }
}

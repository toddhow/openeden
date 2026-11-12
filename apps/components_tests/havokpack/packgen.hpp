#ifndef OPENEDEN_TESTS_HAVOKPACK_PACKGEN_H
#define OPENEDEN_TESTS_HAVOKPACK_PACKGEN_H

// Task 0119: our own generator of Havok packfiles for tests (file version 8, 64-bit pointers, little endian).
// The layout written here is the one the reader documents in docs/research/0054-provenance.md. No game file and no
// Havok SDK material is used: the bytes come from the structure the data analysis established.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace HavokPackTest
{
    class PackGen
    {
    public:
        // Adds an object of `size` bytes (rounded up to 16) to the data section and returns its offset.
        std::uint32_t addObject(const std::string& cls, std::size_t size)
        {
            while (mData.size() % 16 != 0)
                mData.push_back(0);
            const std::uint32_t at = static_cast<std::uint32_t>(mData.size());
            mData.resize(mData.size() + ((size + 15) / 16) * 16, 0);
            mVirtual.push_back({ at, classOffset(cls) });
            return at;
        }

        // Raw (non-object) bytes appended to the data section; returns their offset.
        std::uint32_t addBytes(const std::vector<std::uint8_t>& bytes)
        {
            while (mData.size() % 16 != 0)
                mData.push_back(0);
            const std::uint32_t at = static_cast<std::uint32_t>(mData.size());
            mData.insert(mData.end(), bytes.begin(), bytes.end());
            return at;
        }

        std::uint32_t addCString(const std::string& s)
        {
            std::vector<std::uint8_t> b(s.begin(), s.end());
            b.push_back(0);
            return addBytes(b);
        }

        void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(&mData[at], &v, 4); }
        void putF32(std::uint32_t at, float v) { std::memcpy(&mData[at], &v, 4); }
        void put16(std::uint32_t at, std::uint16_t v) { std::memcpy(&mData[at], &v, 2); }

        // A pointer slot at `slot` that points to `target` in the data section.
        void pointer(std::uint32_t slot, std::uint32_t target) { mLocal.push_back({ slot, target }); }
        // A pointer slot that points to a place in section `section` (0 classnames, 1 types, 2 data).
        void globalPointer(std::uint32_t slot, std::uint32_t section, std::uint32_t target)
        {
            mGlobal.push_back({ slot, section, target });
        }
        // Array member at `slot`: pointer, size, capacity with the "don't deallocate" flag.
        void array(std::uint32_t slot, std::uint32_t target, std::uint32_t size)
        {
            pointer(slot, target);
            put32(slot + 8, size);
            put32(slot + 12, size | 0x80000000u);
        }

        std::vector<std::uint8_t> build() const
        {
            std::vector<std::uint8_t> names;
            for (const auto& c : mClasses)
            {
                const std::uint32_t sig = 0x1000u + static_cast<std::uint32_t>(c.size());
                for (int i = 0; i < 4; ++i)
                    names.push_back(static_cast<std::uint8_t>(sig >> (8 * i)));
                names.push_back(9);
                names.insert(names.end(), c.begin(), c.end());
                names.push_back(0);
            }
            while (names.size() % 16 != 0)
                names.push_back(0xFF);

            std::vector<std::uint8_t> loc, glob, virt;
            for (const auto& l : mLocal)
            {
                push32(loc, l.src);
                push32(loc, l.dst);
            }
            for (const auto& g : mGlobal)
            {
                push32(glob, g.src);
                push32(glob, g.section);
                push32(glob, g.dst);
            }
            for (const auto& v : mVirtual)
            {
                push32(virt, v.src);
                push32(virt, 0); // the class names section
                push32(virt, v.cls);
            }
            terminate(loc, 8);
            terminate(glob, 12);
            terminate(virt, 12);

            const std::uint32_t headerSize = 64 + 3 * 48;
            const std::uint32_t namesStart = headerSize;
            const std::uint32_t namesEnd = static_cast<std::uint32_t>(names.size());
            const std::uint32_t typesStart = namesStart + namesEnd;
            const std::uint32_t dataStart = typesStart;
            const std::uint32_t dataSize = static_cast<std::uint32_t>(mData.size());

            std::vector<std::uint8_t> out;
            push32(out, 0x57E0E057);
            push32(out, 0x10C0C010);
            push32(out, 0);
            push32(out, 8);
            out.push_back(8); // pointer size
            out.push_back(1); // little endian
            out.push_back(0);
            out.push_back(1);
            push32(out, 3);
            push32(out, 2); // contents section
            push32(out, mRootOffset);
            push32(out, 0); // class names section
            push32(out, mRootClassOffset);
            const std::string v = "hk_2010.2.0-r1";
            for (std::size_t i = 0; i < 16; ++i)
                out.push_back(i < v.size() ? static_cast<std::uint8_t>(v[i]) : (i == 15 ? 0xFF : 0));
            push32(out, 0);
            push32(out, 0xFFFFFFFF);

            section(out, "__classnames__", namesStart, namesEnd, namesEnd, namesEnd, namesEnd, namesEnd, namesEnd);
            section(out, "__types__", typesStart, 0, 0, 0, 0, 0, 0);
            const std::uint32_t l = dataSize;
            const std::uint32_t g = l + static_cast<std::uint32_t>(loc.size());
            const std::uint32_t vv = g + static_cast<std::uint32_t>(glob.size());
            const std::uint32_t e = vv + static_cast<std::uint32_t>(virt.size());
            section(out, "__data__", dataStart, l, g, vv, e, e, e);

            out.insert(out.end(), names.begin(), names.end());
            out.insert(out.end(), mData.begin(), mData.end());
            out.insert(out.end(), loc.begin(), loc.end());
            out.insert(out.end(), glob.begin(), glob.end());
            out.insert(out.end(), virt.begin(), virt.end());
            return out;
        }

        std::vector<std::uint8_t> mData;

    private:
        struct Local
        {
            std::uint32_t src, dst;
        };
        struct Global
        {
            std::uint32_t src, section, dst;
        };
        struct Virt
        {
            std::uint32_t src, cls;
        };

        std::uint32_t classOffset(const std::string& cls)
        {
            std::uint32_t at = 0;
            for (const auto& c : mClasses)
            {
                if (c == cls)
                    return at + 5;
                at += 5 + static_cast<std::uint32_t>(c.size()) + 1;
            }
            mClasses.push_back(cls);
            return at + 5;
        }

        static void push32(std::vector<std::uint8_t>& v, std::uint32_t x)
        {
            for (int i = 0; i < 4; ++i)
                v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
        }

        // The table ends with 0xFFFFFFFF in the first word of an entry, then 0xFF padding to a multiple of 16.
        static void terminate(std::vector<std::uint8_t>& v, std::size_t entry)
        {
            const std::size_t n = entry; // one whole terminating entry
            for (std::size_t i = 0; i < n; ++i)
                v.push_back(0xFF);
            while (v.size() % 16 != 0)
                v.push_back(0xFF);
        }

        static void section(std::vector<std::uint8_t>& out, const std::string& name, std::uint32_t start,
            std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f)
        {
            for (std::size_t i = 0; i < 19; ++i)
                out.push_back(i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0);
            out.push_back(0xFF);
            for (std::uint32_t x : { start, a, b, c, d, e, f })
                push32(out, x);
        }

        std::vector<std::string> mClasses;
        std::vector<Local> mLocal;
        std::vector<Global> mGlobal;
        std::vector<Virt> mVirtual;
        std::uint32_t mRootOffset = 0;
        std::uint32_t mRootClassOffset = 0x4b;
    };
}

#endif

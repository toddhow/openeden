#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_PACKFILE_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_PACKFILE_H

// Task 0119 (research note 0054): a reader for the Havok "packfile" container as Skyrim Special Edition stores its
// .hkx files (file version 8, content version "hk_2010.2.0-r1", 64-bit pointers, little endian).
//
// Clean-room: every layout fact in this library was established from the player's own files or by reasoning, and is
// logged in docs/research/0054-provenance.md. No Havok SDK header, source or derived code was used.
//
// Formats layer, standard library only. The library knows containers, sections, fix-ups, class names and objects. It
// does not know what any class contains; see skeleton.hpp and animation.hpp for those.
//
// Ownership: a Packfile owns a copy of the file bytes. Every view it returns points into that copy and lives as long
// as the Packfile. Thread safety: a parsed Packfile is immutable, so concurrent reads are safe.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace HavokPack
{
    // Every refusal is one of these, with the place in the file in the text ("... (offset 123)").
    class Error : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Caps checked before anything is allocated. The defaults are far above the real files (the largest of the 7,699
    // installed files is well below 1 MiB) and far below anything that could exhaust memory.
    struct Limits
    {
        std::size_t mMaxFileBytes = 64u * 1024u * 1024u;
        std::uint32_t mMaxSections = 8;
        std::uint32_t mMaxClasses = 4096;
        std::uint32_t mMaxClassNameLength = 256;
        std::uint32_t mMaxFixupsPerTable = 1u << 21;
        std::uint32_t mMaxObjects = 1u << 20;
        std::uint32_t mMaxStringLength = 1u << 16;
    };

    // A place inside a section's data.
    struct Ref
    {
        std::uint32_t mSection = 0;
        std::uint32_t mOffset = 0;
        friend bool operator==(const Ref&, const Ref&) = default;
    };

    struct Section
    {
        std::string mName;
        std::uint32_t mDataStart = 0; // absolute offset of the section's data in the file
        std::uint32_t mDataSize = 0; // bytes of data (the local fix-up table starts right after them)
        std::uint32_t mLocalFixups = 0; // offsets below are relative to mDataStart
        std::uint32_t mGlobalFixups = 0;
        std::uint32_t mVirtualFixups = 0;
        std::uint32_t mExports = 0;
        std::uint32_t mImports = 0;
        std::uint32_t mEnd = 0;
    };

    // An object: the start of a record with a virtual fix-up, which names its class.
    struct Object
    {
        std::uint32_t mSection = 0;
        std::uint32_t mOffset = 0;
        std::uint32_t mSize = 0; // up to the next object of the section, or to the end of its data
        std::uint32_t mClass = 0; // index into classNames()
    };

    // An array member as the 64-bit layout stores it: pointer (8), size (4), capacity and flags (4).
    struct ArrayRef
    {
        std::optional<Ref> mData;
        std::uint32_t mSize = 0;
    };

    class Packfile
    {
    public:
        // Reads and checks a whole file. Throws Error.
        static Packfile parse(std::span<const std::uint8_t> bytes, const Limits& limits = {});

        const std::string& versionString() const { return mVersion; }
        std::uint32_t fileVersion() const { return mFileVersion; }
        const std::vector<Section>& sections() const { return mSections; }
        const std::vector<std::string>& classNames() const { return mClassNames; }
        // Sorted by (section, offset).
        const std::vector<Object>& objects() const { return mObjects; }
        std::string_view className(const Object& object) const { return mClassNames[object.mClass]; }
        std::span<const std::uint8_t> sectionData(std::uint32_t section) const;

        // The object that starts exactly at the place, if any.
        const Object* findObject(Ref at) const;
        // Objects of a class, in file order.
        std::vector<const Object*> objectsOfClass(std::string_view name) const;

        // The pointer stored in the 8 bytes at `slot`: the target named by a local or global fix-up, or nothing for
        // a null pointer. A slot with no fix-up that holds a non-zero value is an error (the file is inconsistent).
        std::optional<Ref> pointerAt(Ref slot) const;
        ArrayRef arrayAt(Ref slot) const;

        // Bounds-checked reads. Each throws Error with the place if the bytes are not inside the section's data.
        std::uint8_t u8(Ref at) const;
        std::uint16_t u16(Ref at) const;
        std::uint32_t u32(Ref at) const;
        std::uint64_t u64(Ref at) const;
        float f32(Ref at) const;
        // The bytes [at, at + size), inside the section's data.
        std::span<const std::uint8_t> bytes(Ref at, std::size_t size) const;
        // A NUL-terminated string that ends inside the section's data and is at most Limits::mMaxStringLength long.
        std::string_view cstring(Ref at) const;

    private:
        struct Fixup
        {
            std::uint32_t mSource = 0;
            Ref mTarget;
        };

        [[noreturn]] void fail(const std::string& what, std::size_t offset) const;
        void check(Ref at, std::size_t size) const;

        std::vector<std::uint8_t> mFile;
        Limits mLimits;
        std::uint32_t mFileVersion = 0;
        std::string mVersion;
        std::vector<Section> mSections;
        std::vector<std::string> mClassNames;
        std::vector<Object> mObjects;
        // Per section, sorted by source offset; local and global fix-ups together.
        std::vector<std::vector<Fixup>> mPointers;
    };
}

#endif

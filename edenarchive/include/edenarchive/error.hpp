// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_ERROR_HPP
#define EDENARCHIVE_ERROR_HPP

#include <cstdint>
#include <string>

namespace EdenArchive
{
    /// The error classes of section 14.1 of the specification (docs/spec/eden-archive-format-v0.1.md), plus Io for
    /// failures of the operating system that the specification does not describe (cannot open, short read).
    enum class ErrorClass : std::uint8_t
    {
        None = 0,
        Magic, // E_MAGIC
        Version, // E_VERSION
        Truncated, // E_TRUNCATED
        Checksum, // E_CHECKSUM
        Range, // E_RANGE
        Overlap, // E_OVERLAP
        Order, // E_ORDER
        Duplicate, // E_DUPLICATE
        Name, // E_NAME
        Limit, // E_LIMIT
        Index, // E_INDEX
        Unsupported, // E_UNSUPPORTED
        Codec, // E_CODEC
        Hash, // E_HASH
        Io, // E_IO
    };

    /// "E_MAGIC", "E_RANGE" and so on; "OK" for None.
    const char* errorClassName(ErrorClass errorClass) noexcept;

    /// A located error. Success is mClass == ErrorClass::None. Tests compare the class and the offset, never the text.
    struct Error
    {
        ErrorClass mClass = ErrorClass::None;
        /// Absolute file offset of the offending field; 0 when there is none.
        std::uint64_t mOffset = 0;
        /// The field named as in the tables of the specification, for example "chunk[2].offset".
        std::string mField;
        std::string mMessage;

        explicit operator bool() const noexcept { return mClass != ErrorClass::None; }
        bool ok() const noexcept { return mClass == ErrorClass::None; }

        /// "<file>: E_RANGE at offset 0x4198, field chunk[0].offset: <message>"; the file part is left out when empty.
        std::string describe(const std::string& file = {}) const;
    };
}

#endif

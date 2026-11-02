// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_SRC_UTF8_HPP
#define EDENARCHIVE_SRC_UTF8_HPP

#include <string>
#include <string_view>

#include <edenarchive/error.hpp>

namespace EdenArchive
{
    /// True if the bytes are well-formed UTF-8 (RFC 3629): no overlong forms, no surrogates, nothing above U+10FFFF.
    bool isValidUtf8(std::string_view text) noexcept;

    /// Checks a stored path against section 7.3.1. Returns ErrorClass::None when it is acceptable, ErrorClass::Limit
    /// for the length limits of rule 1 (empty, over 1024 bytes, a component over 255 bytes), ErrorClass::Name for
    /// every other rule; `reason` then says which.
    ErrorClass checkStoredPath(std::string_view path, std::string& reason);

    /// The comparison of section 7.3.2: bytewise as unsigned values on the common length, the shorter first.
    /// Returns -1, 0 or 1.
    int comparePaths(std::string_view a, std::string_view b) noexcept;
}

#endif

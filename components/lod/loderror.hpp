#ifndef OPENMW_COMPONENTS_LOD_LODERROR_H
#define OPENMW_COMPONENTS_LOD_LODERROR_H

#include <cstdint>
#include <string>

namespace Lod
{
    /// A located error of the tile code: which field, at which byte of the payload (or file), and why. Success is
    /// an empty mField. Tests compare the field name and offset, never the message text.
    struct LodError
    {
        std::string mField;
        std::uint64_t mOffset = 0;
        std::string mMessage;

        bool ok() const noexcept { return mField.empty(); }
        /// True when this is a failure, like EdenArchive::Error.
        explicit operator bool() const noexcept { return !mField.empty(); }
        std::string describe() const { return mField + " at offset " + std::to_string(mOffset) + ": " + mMessage; }
    };
}

#endif

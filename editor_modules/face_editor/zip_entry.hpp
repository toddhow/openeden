#pragma once

// Reads ONE named entry out of a zip file (task 0147 slice 3): a NuGet package is a zip, and the face editor takes only
// DirectML.dll from the pinned Microsoft.AI.DirectML package. Stored (0) and deflated (8) entries only; no zip64, no
// encryption, no multi-disk archive. Hostile input: every offset and size is checked against the file, the inflated
// size must equal the central directory's, the CRC-32 must match, and the entry is capped by the caller.
// Written for this project from the zip file format description (PKWARE APPNOTE, public); zlib inflates.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace face_editor
{
    /// Exact (case sensitive) entry name. False with \a error set when the file is not a usable zip, has no such entry,
    /// the entry is larger than \a maxBytes, uses an unsupported feature, or fails its checks.
    bool readZipEntry(const std::filesystem::path& zipPath, std::string_view entryName, std::uint64_t maxBytes,
        std::vector<char>& out, std::string& error);
}

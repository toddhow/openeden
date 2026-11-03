#ifndef OPENEDEN_COMPONENTS_BODY_BODYPACK_H
#define OPENEDEN_COMPONENTS_BODY_BODYPACK_H

// OpenEden AdvancedBodies, stage B1 (task 0063, decision D1): a body package as a folder (the source) or as one
// .edenpak (the packed form, written with the Eden Archive library). Packing uses generic, path-keyed, store-only
// entries (entry type 0, no key, no provenance), the library's default chunk size, and no pack flags; the Eden
// Archive format is not changed. The package digest is the pack's manifest digest (spec section 13.5), which does not
// depend on the compressor: a folder and its pack give the same digest.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Body
{
    /// The files of a package, keyed by their package-relative path with '/' separators. std::map keeps them sorted.
    struct PackageFiles
    {
        std::map<std::string, std::vector<std::uint8_t>> mFiles;
    };

    namespace PackLimits
    {
        inline constexpr std::size_t sMaxFiles = 4096;
        inline constexpr std::uint64_t sMaxTotalBytes = std::uint64_t{ 512 } << 20;
    }

    /// Reads every regular file under `folder` (recursively). Paths must pass isSafePackagePath, else an error.
    /// Symlinks are skipped. On failure returns nullopt and sets `error` (one line).
    std::optional<PackageFiles> readPackageFolder(const std::filesystem::path& folder, std::string& error);

    /// Writes the files as a .edenpak at `output` (written to a temporary name and renamed, by the library).
    bool writePackageFile(const PackageFiles& files, const std::filesystem::path& output, std::string& error);

    /// Opens a .edenpak and reads every path-keyed entry (hashes are verified by the library). Entry count and total
    /// size are capped by PackLimits.
    std::optional<PackageFiles> readPackageFile(const std::filesystem::path& pack, std::string& error);

    /// The package digest as lowercase hex: the manifest digest of the pack that writePackageFile would write.
    std::string packageDigest(const PackageFiles& files, std::string& error);
}

#endif

#pragma once

// DirectML for the face editor's GPU path, downloadable on request (task 0147 slice 3, toddhow 2026-11-12: "optional,
// we can make it downloadable in the settings"). Nothing of DirectML ships with Eden Studio. The author clicks
// Face > Install DirectML..., reads the size and the licence in the confirmation, and only then does Studio fetch the
// pinned Microsoft.AI.DirectML NuGet package, check its SHA-512, and keep DirectML.dll from it in the user data folder
// (<user data>/openeden/directml/<version>/DirectML.dll). The generator loads that DLL by its full path.
//
// The package source is a URL: https (WinHTTP, Windows), http only to the machine itself (tests), or file:/// (tests
// and offline copies). Everything else is refused. Standard library only except the WinHTTP part.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace face_editor
{
    struct DirectMlPackage
    {
        std::string mVersion; ///< "1.15.4"
        std::string mUrl; ///< where the package is fetched from
        std::string mSha512; ///< 128 lower case hex characters
        std::uint64_t mSize = 0; ///< bytes; the download is refused beyond it and when it ends short
        std::string mEntry; ///< the entry kept: "bin/x64-win/DirectML.dll"
        std::uint64_t mMaxEntryBytes = 0; ///< the cap on the extracted DLL
    };

    /// Microsoft.AI.DirectML 1.15.4, the version ONNX Runtime 1.24.4's DirectML build declares (docs/dependencies.md).
    const DirectMlPackage& pinnedDirectMlPackage();

    /// <user data>/openeden/directml/<version>/DirectML.dll for \a package.
    std::filesystem::path directMlDllPath(const std::filesystem::path& userData, const DirectMlPackage& package);
    /// The pinned package's path.
    std::filesystem::path directMlDllPath(const std::filesystem::path& userData);
    /// Whether the file is there (not that it loads).
    bool directMlInstalled(const std::filesystem::path& userData);

    /// What the confirmation dialog says before anything is downloaded: size, what is kept and where, and the licence.
    std::string directMlConfirmationText(const std::filesystem::path& userData);
    std::string directMlConfirmationText(const std::filesystem::path& userData, const DirectMlPackage& package);

    /// Whether \a package's DLL is installed.
    bool directMlInstalled(const std::filesystem::path& userData, const DirectMlPackage& package);

    /// \a progress gets a fraction in [0, 1] and returns false to cancel.
    using InstallProgress = std::function<bool(double fraction, std::string_view message)>;

    /// Fetches \a package into \a destDir (created), checks size and SHA-512, extracts \a package.mEntry's file name
    /// into \a destDir and removes the downloaded package. All or nothing: on any failure (including a cancel, "cancelled")
    /// nothing is left in \a destDir that was not there before, and an existing DirectML.dll is not touched.
    bool installDirectMl(const DirectMlPackage& package, const std::filesystem::path& destDir,
        const InstallProgress& progress, std::string& error);
}

#pragma once

// The Audio2Face models folder (task 0147, decisions of toddhow 2026-11-03 and 2026-11-12): people download the models
// themselves; Eden Studio keeps a folder `<eden-studio user data>/models/audio2face/<variant>/`, writes a README there
// on first use, lists the variants it finds and checks them against the file sizes and SHA-256 recorded below (taken
// from the downloads of 2026-11-12, docs/tasks/0071-facial-speech-animation.md). No model file is ever copied into the
// repository or shipped. Pure: standard library only.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace face_editor
{
    struct ExpectedFile
    {
        std::string mName;
        std::uint64_t mSize = 0;
        std::string mSha256; ///< lower case hex; empty = not recorded (size only)
    };

    struct VariantSpec
    {
        std::string mId; ///< the folder name
        std::string mLabel;
        std::string mSource; ///< where the user downloads it
        std::string mLicence;
        bool mOptional = false; ///< Audio2Emotion
        std::vector<ExpectedFile> mFiles;
    };

    enum class VariantState
    {
        Missing, ///< no folder
        Incomplete, ///< a required file is absent
        WrongSize, ///< a file has another size than recorded
        Present, ///< every file exists with the recorded size (hashes not yet verified)
        Verified, ///< ... and the recorded hashes match
        HashMismatch
    };

    struct VariantStatus
    {
        std::string mId;
        std::string mLabel;
        VariantState mState = VariantState::Missing;
        std::string mDetail; ///< one line: what is missing or wrong
    };

    /// v3.0 (Mark identity), v2.3 Mark, Audio2Emotion, in that order.
    const std::vector<VariantSpec>& knownVariants();

    /// The folder Studio uses: <userData>/models/audio2face.
    std::filesystem::path modelsRoot(const std::filesystem::path& userData);

    /// Looks at each known variant under \a root. Sizes only; never reads a file's contents. Never throws.
    std::vector<VariantStatus> scanModels(const std::filesystem::path& root);

    /// Hashes the files with a recorded SHA-256 (streams them; slow for 1.2 GB: call from a background task). \a
    /// progress gets a fraction in [0, 1] and returns false to cancel. Returns the status with Verified or
    /// HashMismatch; a cancelled run returns Present.
    VariantStatus verifyVariant(
        const std::filesystem::path& root, const VariantSpec& spec, const std::function<bool(double)>& progress);

    /// Text of the README Studio writes: which pages to visit, which licence is accepted, which files go where, with
    /// their names, sizes and hashes.
    std::string readmeText();

    /// Creates the root and writes README.txt there if it is not there yet (an existing file is left alone, so an
    /// author's edits stay). Returns false with \a error set when the folder cannot be created or written.
    bool ensureModelsFolder(const std::filesystem::path& root, std::string& error);

    std::string stateText(VariantState state);
}

// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_VALIDATOR_HPP
#define EDENARCHIVE_VALIDATOR_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "error.hpp"
#include "source.hpp"

namespace EdenArchive
{
    struct ValidateOptions
    {
        /// Also check the canonical form (section 17): zero padding, reserved bytes and gaps, default chunking, no
        /// extension blocks, the manifest digest, the file/directory name rule, and so on.
        bool mCanonical = true;
        /// Decode and hash every chunk and every entry (otherwise only the structure is checked).
        bool mVerifyContent = true;
        /// Budget of decoded bytes (section 15.3); exceeding it is a finding of class Limit.
        std::uint64_t mDecodeBudget = std::uint64_t{ 1 } << 36;
        /// Stop collecting after this many findings.
        std::size_t mMaxFindings = 100;
    };

    struct ValidationReport
    {
        /// False when the pack could not be opened; the open error is then the first and only finding.
        bool mOpened = false;
        std::vector<Error> mFindings;
        std::uint64_t mBytesDecoded = 0;
        bool valid() const noexcept { return mOpened && mFindings.empty(); }
    };

    /// Opens the source with every check of section 14.2 (digest on), then checks the rest. Never throws.
    ValidationReport validate(std::shared_ptr<const Source> source, const ValidateOptions& options) noexcept;
}

#endif

// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// The conformance corpus as data: every case with its bytes and the result the specification requires. The corpus
// generator (edenarchive/corpus/corpusgen.cpp) writes these cases to files and the unit tests run them directly, so
// both always use the same list. The expected results come from the specification alone (sections 14.1 and 14.2 for
// the class, Annex C for the first twenty invalid packs).
#ifndef EDENARCHIVE_TESTSUPPORT_CORPUSCASES_HPP
#define EDENARCHIVE_TESTSUPPORT_CORPUSCASES_HPP

#include <optional>
#include <string>
#include <vector>

#include "eautil.hpp"

namespace EdenTest
{
    /// What an entry of a valid pack must look like when read.
    struct ExpectedEntry
    {
        std::optional<std::string> mPath;
        std::optional<std::uint64_t> mKey;
        std::uint16_t mType = 0;
        Bytes mContent;
        std::uint32_t mChunkCount = 0;
        /// False for the one entry of a read-time failure case whose read must fail (mContent is then empty).
        bool mReadable = true;
        bool mHasProvenance = false;
        std::string mToolId;
        Bytes mParameters;
    };

    enum class Outcome
    {
        Valid, // the pack opens and every entry reads
        InvalidAtOpen, // Archive::open must fail
        InvalidAtRead, // the pack opens, and reading entry mFailingEntry must fail
    };

    enum class CanonicalResult
    {
        Pass, // the canonical validator finds nothing
        Fail, // the canonical validator reports at least one finding (non-canonical mode finds none)
        Unspecified, // the specification does not say; the tests do not check the canonical validator
    };

    struct CorpusCase
    {
        /// Letters and digits only (it becomes a file name).
        std::string mId;
        Bytes mBytes;
        Outcome mOutcome = Outcome::Valid;
        /// Names as in section 14.1 ("E_RANGE"). One of them must be the class. Empty means the specification does not
        /// fix the class: the pack must only be rejected.
        std::vector<std::string> mClasses;
        /// The absolute file offset of the offending field, only where the specification makes it unambiguous.
        std::optional<std::uint64_t> mOffset;
        /// No spaces.
        std::string mDescription;
        /// For InvalidAtRead: the entry whose read fails; every other entry in mEntries must still read.
        std::size_t mFailingEntry = 0;
        /// For InvalidAtRead: false when only reading the whole entry fails (a bad content hash), while a range read
        /// of the same bytes succeeds.
        bool mRangeFails = true;
        /// In entry order. Filled for Valid and InvalidAtRead cases.
        std::vector<ExpectedEntry> mEntries;
        CanonicalResult mCanonical = CanonicalResult::Pass;
        /// For CanonicalResult::Fail: classes the specification names for the finding (empty: any class).
        std::vector<std::string> mCanonicalClasses;
        /// True: the validator-only case of 7.3.2 (the reader accepts it, the canonical validator must reject it).
        bool mReaderMayReject = false;
        /// True when some chunk was made by libzstd: the bytes then depend on the zstd version and CPU (specification
        /// 17.3), so a regenerated file is compared by content and manifest digest, not byte for byte.
        bool mUsesRealZstd = false;

        bool opens() const { return mOutcome != Outcome::InvalidAtOpen; }
    };

    /// All cases, built once. The order is fixed.
    const std::vector<CorpusCase>& corpusCases();

    /// The line of expected.txt for a case: "<id> <valid|invalid> <class or -> <open|read> <offset or -> <description>".
    std::string expectedLine(const CorpusCase& corpusCase);

    /// The whole expected.txt (one line per case, in case order, each ending in a newline character).
    std::string expectedText();
}

#endif

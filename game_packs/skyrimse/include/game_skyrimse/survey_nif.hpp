#ifndef OPENEDEN_GAME_SKYRIMSE_SURVEY_NIF_H
#define OPENEDEN_GAME_SKYRIMSE_SURVEY_NIF_H

// The Skyrim SE data survey, part 3: NIF block types (task 0116). Reads only the header of a NIF 20.2.0.7 file (the
// block type table and each block's type index), so it counts types without parsing blocks and never throws. Which
// types the engine's reader can parse is found by running that reader (the gated tool does), not by a list kept here.
//
// The header layout is the one components/nif/niffile.cpp reads (header line, version, endianness, user version, block
// count, the Bethesda stream header when the user version is 3 or more, the block type table, one type index per
// block).

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse::survey
{
    struct NifHeader
    {
        bool mOk = false;
        std::string mError;
        std::uint32_t mVersion = 0; ///< packed, 0x14020007 for 20.2.0.7
        std::uint32_t mUserVersion = 0;
        std::uint32_t mStreamVersion = 0;
        std::vector<std::string> mTypeNames;
        std::vector<std::uint16_t> mBlockTypes; ///< one index into mTypeNames per block
    };

    /// Caps: a header asking for more blocks or types than the bytes can hold is refused with a message.
    [[nodiscard]] NifHeader readNifHeader(std::span<const std::byte> data);

    class NifSurvey
    {
    public:
        void add(const NifHeader& header);
        /// A file the engine's reader refused, with the reader's message ("Unknown record type X" puts X in
        /// mUnsupportedTypes).
        void noteReaderFailure(const std::string& message);

        std::uint64_t mFiles = 0;
        std::uint64_t mHeaderFailures = 0;
        std::uint64_t mBlocks = 0;
        std::uint64_t mReaderFailures = 0;
        std::map<std::string, std::uint64_t> mBlocksByType;
        std::map<std::string, std::uint64_t> mFilesByType;
        std::map<std::string, std::uint64_t> mVersions; ///< "20.2.0.7 user 12 stream 100"
        std::map<std::string, std::uint64_t> mHeaderErrors;
        std::map<std::string, std::uint64_t> mReaderFailureKinds;
        std::set<std::string> mUnsupportedTypes;
    };
}

#endif

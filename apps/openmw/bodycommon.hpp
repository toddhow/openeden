#ifndef OPENMW_BODYCOMMON_H
#define OPENMW_BODYCOMMON_H

// OpenEden AdvancedBodies, merged legacy body (task 0069): what the body analysis (`--analyse-bodies`,
// bodyanalyser.cpp) and the body bake and check (`--bake-bodies`, `--check-bodies`, bodybaker.cpp) share, so that the
// two cannot drift apart: the list of slots of a body, the unit (race and sex), and the reading of one NIF file from
// the VFS with its size, digest and shapes. Neither tool changes the game or the data.

#include <components/bodynif/skinnedshape.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/nif/niffile.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VFS
{
    class Manager;
}

namespace OMW::BodyCommon
{
    struct SlotDef
    {
        ESM::PartReferenceType mType;
        const char* mName;
    };

    // The slots of a body from the neck down (research note 0032 section 1); the head is a part of its own (decision
    // H14). The slot number of a region is the engine's part type.
    inline constexpr SlotDef slotDefs[] = {
        { ESM::PRT_Neck, "neck" },
        { ESM::PRT_Cuirass, "cuirass" },
        { ESM::PRT_Groin, "groin" },
        { ESM::PRT_RUpperarm, "rupperarm" },
        { ESM::PRT_LUpperarm, "lupperarm" },
        { ESM::PRT_RForearm, "rforearm" },
        { ESM::PRT_LForearm, "lforearm" },
        { ESM::PRT_RWrist, "rwrist" },
        { ESM::PRT_LWrist, "lwrist" },
        { ESM::PRT_RHand, "rhand" },
        { ESM::PRT_LHand, "lhand" },
        { ESM::PRT_RLeg, "rleg" },
        { ESM::PRT_LLeg, "lleg" },
        { ESM::PRT_RKnee, "rknee" },
        { ESM::PRT_LKnee, "lknee" },
        { ESM::PRT_RAnkle, "rankle" },
        { ESM::PRT_LAnkle, "lankle" },
        { ESM::PRT_RFoot, "rfoot" },
        { ESM::PRT_LFoot, "lfoot" },
        { ESM::PRT_Tail, "tail" },
    };

    /// One body: a race and a sex.
    struct Unit
    {
        const ESM::Race* mRace = nullptr;
        bool mFemale = false;
    };

    /// A body part file is a few hundred kilobytes (the largest stock one is under one megabyte); a file over this is a
    /// mod's mistake or an attack and is refused before it is read into memory twice.
    inline constexpr std::uint64_t maxNifBytes = 32ull * 1024 * 1024;

    /// One NIF file read from the VFS: its size and the digest of its bytes (a loose file has no archive digest, so
    /// the bytes are hashed), and the parsed records. mError is non-empty when the file cannot be used.
    struct ParsedFile
    {
        std::unique_ptr<Nif::NIFFile> mFile;
        std::uint64_t mBytes = 0;
        std::string mDigest; // lowercase hex BLAKE3 of the bytes; empty when the file could not be read
        std::string mError;
    };

    /// Reads `path` (normalised by the caller), at most maxNifBytes. Never throws.
    ParsedFile parseFile(const VFS::Manager& vfs, const std::string& path);

    /// The shapes the skin filter selects from every root of a parsed file; reader errors are appended to `errors`.
    /// `fileShapes`, when given, gets the number of shapes the file holds without a filter.
    std::vector<BodyNif::Shape> shapesOf(const ParsedFile& parsed, const std::string& filter,
        const BodyNif::Placement& placement, std::vector<std::string>& errors, std::size_t* fileShapes = nullptr);
}

#endif

#ifndef OPENEDEN_COMPONENTS_BODYNIF_ANALYSISREPORT_H
#define OPENEDEN_COMPONENTS_BODYNIF_ANALYSISREPORT_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage a): the data model of the stock body analysis and its
// two renderings, a canonical JSON and a text. Pure code: the engine adapter (apps/openmw/bodyanalyser.cpp) fills
// the structs from the game data; nothing here knows the ESM or the VFS.
//
// Determinism: the output depends only on the structs (never on a clock, a path of the machine or an address); the
// numbers are written with a locale-independent shortest form of at most 6 significant digits. Raw reports name
// records and models of the user's own game data and stay out of the repository; the aggregate table is what a
// document may quote.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "seamanalysis.hpp"

namespace BodyNif
{
    struct SlotReport
    {
        std::string mSlot; // "cuirass", "rforearm", ...
        std::string mRecord; // the body part record id, empty when the slot has none
        std::string mModel; // the record's model path, empty when none
        std::uint64_t mFileBytes = 0;
        bool mRead = false; // the file was read and held shapes for this slot
        std::vector<std::string> mErrors;
        PartStats mStats;
        std::vector<std::string> mShapeNames;
        std::vector<std::string> mBoneNames; // distinct bone names of the slot's shapes, sorted
        std::vector<std::string> mMaterials; // "texture|a" (alpha test) "|b" (blend) or "|-"; sorted, unique
        bool mAllIdentity = true; // every shape's node and skin transform is the identity
        bool mAnyMirrored = false;
        bool mPlaced = false; // a rigid part brought into the skeleton's space by its attach bone
        std::size_t mFileShapes = 0; // shapes the file holds in all (before the skin filter), 0 when unread
    };

    struct SeamEntry
    {
        std::string mLabel; // the generic seam name: "chest-upperarm" for both sides
        std::string mSlotA;
        std::string mSlotB;
        SeamReport mReport;
    };

    struct HeadEntry
    {
        std::string mRecord;
        std::string mModel;
        bool mRead = false;
        PartStats mStats;
        SeamReport mNeckSeam;
    };

    struct UnitReport
    {
        std::string mRace;
        std::string mSkeleton; // the skeleton file whose bones place the rigid parts
        std::size_t mSkeletonBones = 0;
        bool mFemale = false;
        bool mBeast = false;
        bool mPlayable = false;
        std::vector<SlotReport> mSlots;
        std::vector<SeamEntry> mSeams;
        std::vector<HeadEntry> mHeads;
        // Totals over the slots that were read (the vertices of the same file read twice for left and right count
        // once per slot, as a merge would count them).
        std::size_t mVertices = 0;
        std::size_t mTriangles = 0;
        std::size_t mBones = 0; // distinct bone names
        std::size_t mMaterials = 0; // distinct material keys
        std::uint64_t mFileBytes = 0; // distinct files
        std::size_t mFiles = 0;
        std::size_t mRigidParts = 0;
        std::size_t mSkinnedParts = 0;
    };

    struct AnalysisReport
    {
        std::vector<UnitReport> mUnits; // in the order given: the caller sorts
    };

    /// Sums the per-slot figures into the totals of a unit (call after the slots are filled).
    void computeTotals(UnitReport& unit);

    /// One row of the go/no-go table: a generic seam over the units of one group.
    struct SeamAggregate
    {
        std::string mLabel;
        std::size_t mSeams = 0; // seams (units x sides) of this label
        std::size_t mTouching = 0; // of them, how many have pairs
        std::size_t mPairs = 0;
        std::array<std::size_t, 6> mWithin{}; // pooled ladder counts
        double mMinDistance = 0; // smallest of the seams' minimum distances
        double mMedianDistance = 0; // median over seams of their median distance
        double mMaxDistance = 0; // largest of the seams' maximum distances
        double mMinGap = 0; // signed gap: smallest minimum, median of medians, largest maximum
        double mMedianGap = 0;
        double mMaxGap = 0;
        std::size_t mOverlapPairs = 0;
        std::size_t mGapPairs = 0;
        std::size_t mMeetingPairs = 0;
        double mMaxWeightDisagreement = 0;
        std::size_t mBoneSetDiffers = 0;
        std::size_t mUvDiffers = 0;
        double mMaxNormalAngle = 0;
        // The all-vertex pairing (see SeamReport): seams with any pair, pooled ladder counts, and the smallest and
        // median (over seams) distance.
        std::size_t mAnyTouching = 0;
        std::size_t mAnyPairs = 0;
        std::array<std::size_t, 6> mAnyWithin{};
        double mMinAnyDistance = 0;
        double mMedianAnyDistance = 0;
    };

    enum class Group
    {
        Humanoid, // not beast
        Beast,
    };

    /// Aggregates the seams of the units of one group by label, labels in order of first appearance. The head seam is
    /// included as "head-neck", one entry per head record.
    std::vector<SeamAggregate> aggregateSeams(const AnalysisReport& report, Group group);

    /// The canonical JSON (keys in a fixed order, two-space indent, one trailing newline).
    std::string toJson(const AnalysisReport& report);

    /// The text report: one section per unit, then the aggregate tables of both groups.
    std::string toText(const AnalysisReport& report);
}

#endif

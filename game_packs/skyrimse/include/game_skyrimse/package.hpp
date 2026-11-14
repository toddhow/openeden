#ifndef OPENEDEN_GAME_SKYRIMSE_PACKAGE_H
#define OPENEDEN_GAME_SKYRIMSE_PACKAGE_H

// Skyrim SE AI packages (task 0135 stage S5; docs/design/skyrim-se-implementation-plan.md section 4.19). Pack side
// only: decode PACK (flags, type, preferred speed, schedule, conditions, template, procedure names, locations),
// merge a load order, and classify what a package makes an actor DO. Plain structs, no engine type.
//
// Layout status. VERIFIED = checked on the five installed masters (7,611 PACK; aggregates in the task log).
// RECALLED-VERIFY = names from xEdit's TES5 definitions (allowed by the decision 0014 amendment), not settled by data.
//
//  - Subrecord order (VERIFIED): EDID, PKDT, PSDT, CTDA* (+ CIS1/CIS2), [QNAM], [VMAD], PKCU, then the data inputs
//    (ANAM type string followed by its value: Bool/Int/Float/ObjectList CNAM, Location PLDT, TargetSelector PTDA, Topic
//    PDTO), UNAM input ids, XNAM, then the procedure tree (POBA/POEA/POCA ... PRCB, PNAM, FNAM, PKC2, PFO2, CITC with
//    their CTDA). The CTDA BEFORE PKCU are the package's conditions; the CTDA after it are branch conditions
//    (VERIFIED: the CITC counts add up to the later CTDA in 7,611 of 7,611 records).
//  - PKDT 12 bytes (VERIFIED size on 7,611): u32 general flags, u8 package type (18 package 7,417; 19 template 126),
//    u8 interrupt override, u8 preferred speed (0..3, 255 once; RECALLED-VERIFY 0 walk, 1 jog, 2 run, 3 fast walk),
//    u8 unknown, u16 interrupt flags, u16 unknown.
//  - PSDT 12 bytes (VERIFIED size): s8 month (-1 in 7,543 of 7,543 that read), s8 day of week (-1 in 7,497; 0..10
//    otherwise: RECALLED-VERIFY 0 Sunday .. 6 Saturday, 7 weekdays, 8 weekends, 9 Mon/Wed/Fri, 10 Tue/Thu), u8 date
//    (0), s8 hour (-1 any, else 0..23), s8 minute (-1 = on the hour), three bytes of garbage (never read), s32 duration
//    in minutes. Hour -1 with a duration (23 packages) is a scene timer, not a window: always in schedule. An hour with
//    duration 0 (7 packages) is read as a one hour window (RECALLED-VERIFY, a decision).
//  - PKCU 12 bytes: u32 data input count, form id of the template package (0 for none), u32 version.
//  - PLDT 12 bytes (VERIFIED size): s32 type, u32 value, s32 radius. Types seen: 0 near reference (value = a placed
//    reference), 1 in cell, 2 near package start location, 3 near editor location, 6 near linked reference, 8 alias
//    (reference), 9 alias (location), 11, 12 near self (RECALLED-VERIFY names). The Mare's NPCs use 0 and 3.
//  - PNAM (procedure names): the alphabetic ones are the procedure kinds of the tree (Travel, Sandbox, Sleep, Eat, Sit,
//    Patrol, Guard, Follow, Wander, ...); non-alphabetic PNAM are padding. A package built from a template
//    (PKCU template != 0) does its template's procedures.

#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/survey.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace PackageLocationType
    {
        inline constexpr std::int32_t NearReference = 0;
        inline constexpr std::int32_t InCell = 1;
        inline constexpr std::int32_t NearPackageStart = 2;
        inline constexpr std::int32_t NearEditorLocation = 3;
        inline constexpr std::int32_t ObjectId = 4;
        inline constexpr std::int32_t ObjectType = 5;
        inline constexpr std::int32_t NearLinkedReference = 6;
        inline constexpr std::int32_t AtPackageLocation = 7;
        inline constexpr std::int32_t AliasReference = 8;
        inline constexpr std::int32_t AliasLocation = 9;
        inline constexpr std::int32_t NearSelf = 12;
    }

    /// PTDA target types (stage S6f; names RECALLED-VERIFY from xEdit, the data's counts fit: Patrol targets are 296
    /// type 0 and 101 type 3, Follow 95 type 0 and 60 type 4, Escort 59 type 0).
    namespace PackageTargetType
    {
        inline constexpr std::int32_t SpecificReference = 0;
        inline constexpr std::int32_t ObjectId = 1;
        inline constexpr std::int32_t ObjectType = 2;
        inline constexpr std::int32_t LinkedReference = 3;
        inline constexpr std::int32_t ReferenceAlias = 4;
        inline constexpr std::int32_t InterruptData = 5;
        inline constexpr std::int32_t Self = 6;
    }

    /// PTDA, 12 bytes (VERIFIED size): s32 type, u32 value, s32 count. The value is a form id for types 0, 1 and 3 (for
    /// 3 the keyword of the link, 0 = the unkeyworded one), a number otherwise.
    struct PackageTarget
    {
        std::int32_t mType = 0;
        std::uint32_t mValue = 0;
        std::int32_t mCount = 0;
    };

    inline constexpr std::size_t kMaxPackageTargets = 16;

    /// PKDT general flag bit 13 (stage S6f): the package sets its own preferred speed. VERIFIED by the data: where it
    /// is clear the speed byte is 2 in 5,499 of 5,600 Travel packages (a placeholder), where it is set the byte spreads
    /// over 0 to 3 (108, 274, 607, 343). The bit's name is not known (RECALLED-VERIFY: xEdit has no name I recall).
    inline constexpr std::uint32_t kPreferredSpeedFlag = 1u << 13;

    /// What a package makes an actor DO beyond the classification (stage S6f). Only for Stand and Unsupported packages:
    /// an actor that sleeps, eats or sandboxes does that first.
    enum class PackageActivity : std::uint8_t
    {
        None,
        Patrol, ///< a patrol procedure: walk a chain of linked markers, idling at each
        Follow, ///< a Follow or FollowTo procedure: stay near a target actor
        Escort, ///< recognised, not run (the target follows the actor to a place)
    };

    /// What an actor does while a package runs (S5 implements the first four; the rest stand where they are told).
    enum class PackageBehavior : std::uint8_t
    {
        Stand, ///< hold position near the location (Sit, Hold position, Wait, Guard, Patrol, Follow, ... : not built)
        Travel, ///< go to the location and stay
        Sandbox, ///< wander to random points within the radius and pause there
        Wander, ///< as Sandbox (a Wander procedure without a Sandbox one)
        Sleep, ///< stand at the location (furniture use is not built)
        Eat, ///< stand at the location (furniture use is not built)
        Unsupported, ///< a procedure kind with no behaviour yet; runs as Stand and is counted
    };

    /// The behaviour of a list of procedure names (the alphabetic PNAM of a package or of its template). Precedence:
    /// ForceGreet/DialogueActivate (Stand) before Sleep before Eat before Sandbox before Wander before Travel; only
    /// Travel names give Travel; no name gives Stand. Names are compared case-insensitively.
    [[nodiscard]] PackageBehavior classifyProcedures(std::span<const std::string> names);

    /// Stage S6f: the activity of a list of procedure names, or None when a sleep, eat, sandbox, wander, force-greet or
    /// dialogue procedure is in it (those are classifyProcedures' business). Patrol before Follow before Escort.
    [[nodiscard]] PackageActivity classifyActivity(std::span<const std::string> names);

    struct PackageSchedule
    {
        std::int8_t mMonth = -1;
        std::int8_t mDayOfWeek = -1;
        std::uint8_t mDate = 0;
        std::int8_t mHour = -1;
        std::int8_t mMinute = -1;
        std::int32_t mDurationMinutes = 0;
    };

    struct PackageLocation
    {
        std::int32_t mType = 0;
        /// A form id (load-order, kUnresolvedFormId when it cannot be resolved) for the form-valued types 0, 1, 4, 6;
        /// the raw number for the others.
        std::uint32_t mValue = 0;
        std::int32_t mRadius = 0;
    };

    /// The most Bool data inputs kept for one package.
    inline constexpr std::size_t kMaxPackageBools = 64;

    /// The Sandbox template's Bool inputs, in order (task 0135 stage S6b). RECALLED-VERIFY (the Creation Kit's order,
    /// not settled by data): 0 allow eating, 1 allow sleeping, 2 allow conversation, 3 allow idle markers, 4 allow
    /// sitting. The data agrees in kind: the 145 most common pattern allows everything but eating; mine-work packages
    /// allow only idle markers and sitting (0,0,0,1,1,...). A package with fewer inputs allows nothing it does not
    /// list.
    namespace SandboxBool
    {
        inline constexpr std::size_t Eating = 0;
        inline constexpr std::size_t Sleeping = 1;
        inline constexpr std::size_t Sitting = 4;
    }

    struct PackageRecord
    {
        RecordCommon mCommon;
        std::uint32_t mFlags = 0; ///< PKDT general flags (meanings not decoded)
        std::uint8_t mType = 0; ///< 18 package, 19 template
        std::uint8_t mPreferredSpeed = 0;
        std::uint16_t mInterruptFlags = 0;
        bool mHasPkdt = false;
        PackageSchedule mSchedule;
        std::vector<survey::Ctda> mConditions; ///< the package's own (before PKCU)
        bool mConditionsOk = true; ///< false: a CTDA of the list was damaged
        FormId mTemplate = kNullFormId;
        std::vector<std::string> mProcedures; ///< alphabetic PNAM, in tree order
        std::vector<PackageLocation> mLocations; ///< PLDT, in order (the first is the package's main location)
        std::vector<PackageTarget> mTargets; ///< PTDA, in order (stage S6f)
        std::vector<std::uint8_t> mBools; ///< the Bool data inputs (ANAM "Bool" + 1 byte CNAM), in order
        std::uint32_t mPlugin = 0; ///< index into PackageIndex's plugins: the plugin whose record won
    };

    struct PackageLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxConditions = 4096;
        std::uint32_t mMaxLocations = 256;
        std::uint32_t mMaxProcedures = 4096;
        std::uint32_t mMaxStringBytes = 64 * 1024;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    struct PackageRecords
    {
        std::string mName;
        std::vector<PackageRecord> mPackages;
        std::map<std::uint32_t, TypeCount> mCounts;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy; ///< "PACK.SUB" -> count
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parsePackageRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const PackageLimits& limits, PackageRecords& out);

    [[nodiscard]] bool parsePackageRecordsBytes(const std::string& bytes, const std::string& name,
        const LoadOrder& order, const PackageLimits& limits, PackageRecords& out, Content::Error& error);

    /// The packages of a whole load order: a later record replaces an earlier one with the same form id (a deleted one
    /// is removed).
    class PackageIndex
    {
    public:
        void add(PackageRecords records, FormIdResolver resolver);

        [[nodiscard]] const PackageRecord* package(FormId id) const;
        [[nodiscard]] std::size_t packageCount() const { return mPackages.size(); }
        [[nodiscard]] std::vector<FormId> packageIds() const; ///< sorted
        [[nodiscard]] const FormIdResolver& resolverOf(const PackageRecord& package) const
        {
            return mResolvers[package.mPlugin];
        }

        /// What the package does: the classification of its own procedures, or of its template's when it has a
        /// template (followed up to 8 links; a missing or cyclic template gives Stand).
        [[nodiscard]] PackageBehavior behaviorOf(const PackageRecord& package) const;

        /// Stage S6f: the activity of the package's own procedures, or its template's (same walk as behaviorOf).
        [[nodiscard]] PackageActivity activityOf(const PackageRecord& package) const;
        /// Stage S6f: the first target of the package, or of its template when it has none of its own.
        [[nodiscard]] bool targetOf(const PackageRecord& package, PackageTarget& out) const;

    private:
        std::vector<FormIdResolver> mResolvers;
        std::unordered_map<FormId, PackageRecord> mPackages;
    };
}

#endif

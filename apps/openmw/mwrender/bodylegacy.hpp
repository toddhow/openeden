#ifndef OPENEDEN_APPS_OPENMW_MWRENDER_BODYLEGACY_H
#define OPENEDEN_APPS_OPENMW_MWRENDER_BODYLEGACY_H

// OpenEden AdvancedBodies, stage B1 (task 0063): the legacy adapter. It describes Morrowind's default human body
// parts (what MWRender::getBodyParts, getHeadModel and getHairModel select for a race and sex) as a Body::BodyPackage,
// in memory only. It CALLS the engine's selection functions and never copies their logic, so it cannot disagree
// with the game. This is the only place in the body system that names ESM records (design doc section 2).
//
// Units: Morrowind units are converted at ONE function, toBodyUnits.
// Threading: pure functions of their arguments; called on the main thread by the `bodycheck` console command.
// The package is never written to a file here and no mesh is copied.

#include <components/body/bodycontract.hpp>
#include <components/body/bodypackage.hpp>
#include <components/body/bodyvalidate.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadbody.hpp>

#include <optional>
#include <string>
#include <vector>

#include "../mwworld/store.hpp"

namespace MWRender
{
    /// Morrowind units to metres (Constants::UnitsPerMeter).
    double toBodyUnits(double morrowindUnits);

    /// Notes the adapter makes (the "authored / derived / missing" labels: here "legacyFallback"). Codes:
    ///   LEGACY-BEAST-001   warning  beast race: another skeleton, not supported yet (mSupported is false)
    ///   LEGACY-MISSING-001 info     no body part record for this slot (mPart names it); armour or nothing covers it
    ///   LEGACY-FALLBACK-001 info    the chosen record is a male part standing in for a female one (mPart)
    ///   LEGACY-HEAD-001    info     head record chosen: the first suitable one in store order (message names its id)
    ///   LEGACY-HEAD-002    info     no suitable head record in the store
    ///   LEGACY-HAIR-001    info     hair record chosen, as above
    ///   LEGACY-HAIR-002    info     no suitable hair record in the store
    ///   LEGACY-MESH-001    info     no mesh summary available for this part (mPart)
    ///   LEGACY-AGE-001     info     Morrowind records carry no age class: the package is "unspecified" and so never
    ///                               eligible for a restricted tier
    struct LegacyNote
    {
        std::string mCode;
        std::string mPart; // the part type name, or empty
        std::string mMessage;
        friend bool operator==(const LegacyNote&, const LegacyNote&) = default;
    };

    struct LegacyOptions
    {
        /// Gives each part's mesh summary. May be null; the parts then have none (LEGACY-MESH-001).
        const Body::MeshSource* mMeshSource = nullptr;
        /// The skeleton profile to bind: inline when set, else mProfilePath is written to the binding.
        std::optional<opas::anim::SkeletonProfile> mProfile;
        std::string mProfilePath = "data/profiles/humanoid-to-eden.skeletonprofile.json";
        /// True for an ESM::Race with the Beast flag (the caller knows the race record).
        bool mBeast = false;
    };

    struct LegacyBody
    {
        Body::BodyPackage mPackage;
        std::vector<LegacyNote> mNotes; // in a fixed order: slot order, then head and hair, then package-wide notes
        bool mSupported = true; // false for a beast race
    };

    /// Builds the package for a race and sex, third person only. Part types name the slot: "head", "hair", "neck",
    /// "cuirass", "groin", "rhand", "lhand", "rwrist", "lwrist", "rforearm", "lforearm", "rupperarm", "lupperarm",
    /// "rfoot", "lfoot", "rankle", "lankle", "rknee", "lknee", "rleg", "lleg", "tail". A mesh's id and partType are
    /// that name, its path the record's model, its attachBone MWAnim::sPartBones' entry, its role skin (head: head,
    /// hair: hair). The package id is "morrowind." + lowercase race id + ".male" / ".female" with characters that are
    /// not valid in a package id replaced by '_'.
    LegacyBody buildLegacyBody(
        const ESM::RefId& race, bool female, const MWWorld::Store<ESM::BodyPart>& store, const LegacyOptions& options);

    /// Additive (stage e): the lines the `bodycheck` console command prints and logs: the package id, the notes and the
    /// validator's findings, then the count line. Pure function.
    std::vector<std::string> describeLegacyCheck(const LegacyBody& body, const Body::Report& report);
}

#endif

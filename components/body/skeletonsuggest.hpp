#ifndef OPENEDEN_COMPONENTS_BODY_SKELETONSUGGEST_H
#define OPENEDEN_COMPONENTS_BODY_SKELETONSUGGEST_H

// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage c): the suggestion engine. A pure function that reads a
// source skeleton (and optionally how much skin weight each bone carries) and writes a SkeletonMap whose extra entries
// are SUGGESTIONS: accepted = false, each with the heuristic that proposed it and a confidence. Nothing is applied
// silently: the consumer (skeletonmapping.hpp) refuses an unaccepted map; acceptAllSuggestions() is the explicit act.
//
// ROLES. The pack of a game gives the engine its own role table (role, the game's native bone name, aliases). A role is
// resolved in this order: the native name (compared by normalisedBoneName), an alias, then a token match: the bone's
// name is split into lower-case tokens (camelCase, digits, ' ', '_', '.', '-', ':' and the contents of "[...]" are
// separators; the noise tokens bip01, bip02, npc, cme, def, deform, jnt, bone, skel, b are dropped), a side token
// (l, left / r, right) is read, and the remaining tokens are looked up in a small vocabulary (upperarm, forearm, hand,
// thigh, calf and shin, foot, pelvis, spine with its digit, neck, head, clavicle, toe). Finger and tail roles are NOT
// resolved by tokens (the vocabulary has no role entries for them): a pack's role table names them, and without one
// finger bones get the name.finger fold and tail bones the name.tail secondary vote below. Only a bone that matches ONE
// role is taken; two bones for one role leave the role unresolved and are reported in mNotes (the candidates in name
// order). A CORE role with no bone is written to the map's "missingRoles" (BODY-MAP-024 refuses the map until a person
// lists its bone), and while one is missing a weighted bone with no name vote is "ask", never a fold: it may be the
// missing role (review fix 2).
//
// EXTRA BONES get votes. Each vote has an action, the details of the action, a heuristic id and a confidence:
//   name.soft-tissue  (high)    breast, boob, bust: secondary breast; belly, stomach, tummy: secondary belly; butt,
//                               glute, buttock, rump: secondary butt; genital, penis, scrotum, testicle: secondary
//                               other
//   name.soft-tissue-weak (medium) pec, pectoral: secondary breast
//   name.cloth (high)           cloth, cape, cloak, skirt, robe, scarf, tabard, hem, coat, sleeve: secondary cloth
//   name.hair (high)            hair, ponytail, braid, bang: secondary hair
//   name.tail (high)            tail: secondary tail
//   name.twist (high)           twist, roll: fold to the nearest mapped ancestor
//   name.face (high)            jaw, eye, eyeball, eyelid, lid, brow, lip, cheek, tongue, mouth, nose, teeth, tooth,
//                               facial: fold to Head (toddhow, answer 6: face bones fold to Head for now)
//   name.finger (high)          a finger token when the role table has no finger roles: fold to the nearest ancestor
//   name.attach (high)          weapon, sword, dagger, axe, mace, bow, staff, shield, quiver, sheath, scabbard,
//   holster,
//                               backpack, torch, camera, magic, anim, animobject: attach, with an attach point
//   name.helper (medium)        ik, helper, dummy, target, pole, marker, null, aim, look, handle: drop when the bone
//                               is KNOWN to carry no weight and has no children, attach (point "node") when it has
//                               children but no weight, fold to the nearest mapped ancestor when it carries weight,
//                               and "ask" when the weights are unknown (mHasMasses false; review fix 5)
//   weight.deform (medium)      a bone with NO name vote that carries weight (mass at least sWeightMassThreshold): fold
//   to
//                               the nearest mapped ancestor, so what it deforms still moves with the body
//   hierarchy.leaf (medium)     a bone with NO name vote, no weight, no children: drop
//   hierarchy.transform (medium) a bone with NO name vote, no weight, children: attach (point "node", a plain
//   transform) rest.coincident (low)       only when the weights are unknown (mHasMasses false) and no name votes: the
//   bone's rest
//                               position is within sCoincidentFraction of the skeleton's height of its nearest mapped
//                               ancestor's: fold
// Weight, hierarchy and rest votes are FALLBACKS: they vote only when no name vote exists, so a name hint is never
// argued with by a weight statistic. The highest confidence wins; two top votes that disagree (action, kind, attach
// point or fold target) make the entry action "ask" (accepted = false, never valid until a person decides); no vote at
// all is also "ask". A high name vote beats a medium name vote that disagrees.
//
// ORDER. The map's roles are in canonicalRoles() order; extra entries are sorted by (depth in the source skeleton, then
// normalised name, then name), so the result is the same whatever order the source bones are listed in.
// Ownership: values. No clock, randomness or threads.

#include "bodypackage.hpp"
#include "skeletonmap.hpp"

#include <string>
#include <vector>

namespace Body
{
    /// How much weight a bone must carry (share of the total skin weight) to count as a deform bone.
    /// A bone that carries less is treated as carrying none. 1e-4 is one hundredth of a percent of the body's weight.
    inline constexpr double sWeightMassThreshold = sMapWeightMassThreshold;
    /// A rest position this close to its ancestor's, as a fraction of the skeleton's height, is "coincident".
    inline constexpr double sCoincidentFraction = 0.005;

    /// What a game's compatibility pack says about its native skeleton for one role.
    struct RoleBinding
    {
        std::string mRole; // a canonical role
        std::string mBone; // the game's native bone name
        std::vector<std::string> mAliases;
    };

    struct SuggestInput
    {
        std::string mName; // the map's name
        std::string mProfile; // "humanoid-to-eden"
        std::vector<RestBone> mSkeleton; // the source skeleton (parents before children)
        std::vector<BoneWeightMass> mMasses; // optional: bones missing here carry no weight
        bool mHasMasses = false; // false: no weight information at all (the weight votes are skipped)
        std::vector<RoleBinding> mRoles; // the pack's role table (may be empty: tokens only)
    };

    struct SuggestResult
    {
        SkeletonMap mMap; // extra entries accepted = false; roles accepted by construction
        std::vector<std::string> mMissingRoles; // roles with no bone, in canonical order
        std::vector<std::string> mNotes; // ambiguities that did not fit the map, in order
    };

    /// Builds a map for the skeleton. An invalid skeleton (validateSourceSkeleton has errors) gives an empty map and a
    /// note.
    SuggestResult suggestSkeletonMap(const SuggestInput& input);

    struct AcceptResult
    {
        std::size_t mAccepted = 0;
        std::size_t mRemainingAsks = 0; // "ask" entries stay unaccepted
    };

    /// The explicit act: marks every non-ask extra entry as accepted. Does not change anything else.
    AcceptResult acceptAllSuggestions(SkeletonMap& map);

    /// The tokens of a bone name as the vocabulary sees them (exposed for tests).
    std::vector<std::string> boneNameTokens(std::string_view name);

    /// True for a token the role match ignores (bip01, bip02, npc, cme, ...: the noise list above). Exposed for the
    /// measurement of task 0070 stage a (skeletonstats.hpp), so both use one list.
    bool isNoiseBoneToken(std::string_view token);
}

#endif

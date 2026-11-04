#include "skeletonsuggest.hpp"

#include "skeletonmapping.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Body
{
    namespace
    {
        // ---- Tokens ------------------------------------------------------------------------------------------------

        bool isUpper(char c)
        {
            return c >= 'A' && c <= 'Z';
        }

        bool isLowerOrDigit(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        }

        char lower(char c)
        {
            return isUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool isSeparator(char c)
        {
            return c == ' ' || c == '_' || c == '-' || c == '.' || c == ':' || c == '/' || c == ',' || c == '('
                || c == ')' || c == '\t';
        }

        // ---- The vocabulary -----------------------------------------------------------------------------------------

        // A token matches a stem when it starts with it (stems of four or more characters) or equals it (shorter stems,
        // which would otherwise match unrelated words).
        bool matchesStem(std::string_view token, std::string_view stem)
        {
            if (stem.size() >= 4)
            {
                return token.size() >= stem.size() && token.substr(0, stem.size()) == stem;
            }
            return token == stem;
        }

        bool matchesAny(const std::vector<std::string>& tokens, std::initializer_list<std::string_view> stems)
        {
            for (const std::string& token : tokens)
            {
                for (const std::string_view stem : stems)
                {
                    if (matchesStem(token, stem))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        bool tokenIs(const std::vector<std::string>& tokens, std::string_view word)
        {
            return std::find(tokens.begin(), tokens.end(), word) != tokens.end();
        }

        const std::set<std::string_view>& noiseTokens()
        {
            static const std::set<std::string_view> noise
                = { "bip01", "bip02", "npc", "cme", "def", "deform", "jnt", "bone", "skel", "b", "mixamorig" };
            return noise;
        }

        // The role a bone name stands for by its tokens ("" when none).
        std::string roleByTokens(const std::vector<std::string>& allTokens)
        {
            enum class Side
            {
                None,
                Left,
                Right,
            };
            Side side = Side::None;
            bool sideSeen = false;
            std::string joined;
            for (const std::string& token : allTokens)
            {
                if (noiseTokens().count(token) != 0)
                {
                    continue;
                }
                const bool left = token == "l" || token == "left" || token == "lf";
                const bool right = token == "r" || token == "right" || token == "rt";
                if ((left || right) && !sideSeen)
                {
                    side = left ? Side::Left : Side::Right;
                    sideSeen = true;
                    continue;
                }
                joined += token;
            }
            struct Entry
            {
                std::string_view mKey;
                std::string_view mBase;
                bool mSided;
            };
            static const std::array<Entry, 32> vocabulary = { {
                { "pelvis", "Pelvis", false },
                { "hips", "Pelvis", false },
                { "hip", "Pelvis", false },
                { "spine", "Spine", false },
                { "spine0", "Spine", false },
                { "spine1", "Chest", false },
                { "chest", "Chest", false },
                { "spine2", "Spine2", false },
                { "neck", "Neck", false },
                { "head", "Head", false },
                { "upperarm", "UpperArm", true },
                { "uparm", "UpperArm", true },
                { "arm", "UpperArm", true },
                { "forearm", "Forearm", true },
                { "lowerarm", "Forearm", true },
                { "lowarm", "Forearm", true },
                { "hand", "Hand", true },
                { "thigh", "Thigh", true },
                { "upperleg", "Thigh", true },
                { "upleg", "Thigh", true },
                { "calf", "Shin", true },
                { "shin", "Shin", true },
                { "lowerleg", "Shin", true },
                { "lowleg", "Shin", true },
                { "leg", "Shin", true },
                { "foot", "Foot", true },
                { "toe", "Toes", true },
                { "toes", "Toes", true },
                { "toebase", "Toes", true },
                { "clavicle", "Clavicle", true },
                { "collar", "Clavicle", true },
                { "shoulder", "Clavicle", true },
            } };
            for (const Entry& entry : vocabulary)
            {
                if (entry.mKey != joined)
                {
                    continue;
                }
                if (entry.mSided)
                {
                    if (side == Side::None)
                    {
                        return {};
                    }
                    return std::string(side == Side::Left ? "Left" : "Right") + std::string(entry.mBase);
                }
                return side == Side::None ? std::string(entry.mBase) : std::string();
            }
            return {};
        }

        // ---- Votes -------------------------------------------------------------------------------------------------

        struct Vote
        {
            MapAction mAction = MapAction::Ask;
            std::optional<SecondaryKind> mKind;
            FoldRule mRule = FoldRule::NearestMappedAncestor;
            std::string mTo;
            std::string mAttachPoint;
            std::string mReason;
            std::string mHeuristic;
            MapConfidence mConfidence = MapConfidence::Low;

            bool sameDecision(const Vote& other) const
            {
                return mAction == other.mAction && mKind == other.mKind && mRule == other.mRule && mTo == other.mTo
                    && mAttachPoint == other.mAttachPoint;
            }
        };

        Vote secondaryVote(SecondaryKind kind, const char* heuristic, MapConfidence confidence)
        {
            Vote vote;
            vote.mAction = MapAction::Secondary;
            vote.mKind = kind;
            vote.mHeuristic = heuristic;
            vote.mConfidence = confidence;
            return vote;
        }

        Vote foldVote(const char* heuristic, MapConfidence confidence, std::string to = {})
        {
            Vote vote;
            vote.mAction = MapAction::Fold;
            vote.mRule = to.empty() ? FoldRule::NearestMappedAncestor : FoldRule::Named;
            vote.mTo = std::move(to);
            vote.mHeuristic = heuristic;
            vote.mConfidence = confidence;
            return vote;
        }

        Vote attachVote(std::string point, const char* heuristic, MapConfidence confidence)
        {
            Vote vote;
            vote.mAction = MapAction::Attach;
            vote.mAttachPoint = std::move(point);
            vote.mHeuristic = heuristic;
            vote.mConfidence = confidence;
            return vote;
        }

        Vote dropVote(std::string reason, const char* heuristic, MapConfidence confidence)
        {
            Vote vote;
            vote.mAction = MapAction::Drop;
            vote.mReason = std::move(reason);
            vote.mHeuristic = heuristic;
            vote.mConfidence = confidence;
            return vote;
        }

        std::string attachPointFor(const std::vector<std::string>& tokens)
        {
            for (const char* sub : { "sword", "dagger", "axe", "mace", "bow", "staff" })
            {
                if (matchesAny(tokens, { std::string_view(sub) }))
                {
                    return std::string("weapon.") + sub;
                }
            }
            if (matchesAny(tokens, { "weapon" }))
            {
                if (tokenIs(tokens, "back"))
                {
                    return "weapon.back";
                }
                if (tokenIs(tokens, "hip"))
                {
                    return "weapon.hip";
                }
                return "weapon";
            }
            if (matchesAny(tokens, { "shield" }))
            {
                return tokenIs(tokens, "back") ? "shield.back" : "shield";
            }
            if (matchesAny(tokens, { "sheath", "scabbard", "holster" }))
            {
                return "weapon.sheath";
            }
            if (matchesAny(tokens, { "quiver" }))
            {
                return "quiver";
            }
            if (matchesAny(tokens, { "backpack" }))
            {
                return "backpack";
            }
            if (matchesAny(tokens, { "torch" }))
            {
                return "torch";
            }
            if (matchesAny(tokens, { "camera" }))
            {
                return "camera";
            }
            if (matchesAny(tokens, { "magic" }))
            {
                return "magic.effects";
            }
            if (matchesAny(tokens, { "anim" }))
            {
                return "animobject";
            }
            return {};
        }

        struct BoneFacts
        {
            bool mHasWeight = false;
            bool mHasChildren = false;
            bool mUnderHand = false; // an ancestor is a hand role bone
        };

        // The votes of the NAME of a bone, in the fixed order of the header.
        std::vector<Vote> nameVotes(
            const std::vector<std::string>& tokens, const BoneFacts& facts, bool fingerRolesExist, bool headResolved)
        {
            std::vector<Vote> votes;
            for (const std::string& token : tokens)
            {
                if (matchesStem(token, "breast") || matchesStem(token, "boob") || matchesStem(token, "bust"))
                {
                    votes.push_back(secondaryVote(SecondaryKind::Breast, "name.soft-tissue", MapConfidence::High));
                }
                else if (matchesStem(token, "belly") || matchesStem(token, "stomach") || matchesStem(token, "tummy"))
                {
                    votes.push_back(secondaryVote(SecondaryKind::Belly, "name.soft-tissue", MapConfidence::High));
                }
                else if (matchesStem(token, "butt") || matchesStem(token, "glute") || matchesStem(token, "rump"))
                {
                    votes.push_back(secondaryVote(SecondaryKind::Butt, "name.soft-tissue", MapConfidence::High));
                }
                else if (matchesStem(token, "genital") || matchesStem(token, "scrot") || matchesStem(token, "testic")
                    || matchesStem(token, "penis"))
                {
                    votes.push_back(secondaryVote(SecondaryKind::Other, "name.soft-tissue", MapConfidence::High));
                }
                else if (matchesStem(token, "pec") || matchesStem(token, "pectoral"))
                {
                    votes.push_back(
                        secondaryVote(SecondaryKind::Breast, "name.soft-tissue-weak", MapConfidence::Medium));
                }
            }
            if (matchesAny(
                    tokens, { "cloth", "cape", "cloak", "skirt", "robe", "scarf", "tabard", "hem", "coat", "sleeve" }))
            {
                votes.push_back(secondaryVote(SecondaryKind::Cloth, "name.cloth", MapConfidence::High));
            }
            if (matchesAny(tokens, { "hair", "ponytail", "braid", "bang" }))
            {
                votes.push_back(secondaryVote(SecondaryKind::Hair, "name.hair", MapConfidence::High));
            }
            if (matchesAny(tokens, { "tail" }))
            {
                votes.push_back(secondaryVote(SecondaryKind::Tail, "name.tail", MapConfidence::High));
            }
            if (matchesAny(tokens, { "twist", "roll" }))
            {
                votes.push_back(foldVote("name.twist", MapConfidence::High));
            }
            if (matchesAny(tokens,
                    { "jaw", "eye", "eyeball", "eyelid", "eyebrow", "lid", "brow", "lip", "lips", "cheek", "tongue",
                        "mouth", "nose", "teeth", "tooth", "facial" }))
            {
                votes.push_back(foldVote("name.face", MapConfidence::High, headResolved ? "Head" : ""));
            }
            if (!fingerRolesExist && facts.mUnderHand
                && matchesAny(tokens, { "thumb", "index", "middle", "ring", "pinky", "pinkie", "finger" }))
            {
                votes.push_back(foldVote("name.finger", MapConfidence::High));
            }
            if (const std::string point = attachPointFor(tokens); !point.empty())
            {
                votes.push_back(attachVote(point, "name.attach", MapConfidence::High));
            }
            if (matchesAny(
                    tokens, { "ik", "helper", "dummy", "target", "pole", "marker", "null", "aim", "look", "handle" }))
            {
                if (facts.mHasWeight)
                {
                    votes.push_back(foldVote("name.helper", MapConfidence::Medium));
                }
                else if (facts.mHasChildren)
                {
                    votes.push_back(attachVote("node", "name.helper", MapConfidence::Medium));
                }
                else
                {
                    votes.push_back(
                        dropVote("helper bone without weights or children", "name.helper", MapConfidence::Medium));
                }
            }
            return votes;
        }

        double length(const std::array<double, 3>& a, const std::array<double, 3>& b)
        {
            return std::sqrt(
                (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        }
    }

    std::vector<std::string> boneNameTokens(std::string_view name)
    {
        // Bracket contents are removed.
        std::string text;
        text.reserve(name.size());
        bool inBracket = false;
        for (const char c : name)
        {
            if (c == '[')
            {
                inBracket = true;
                continue;
            }
            if (c == ']')
            {
                inBracket = false;
                continue;
            }
            if (!inBracket)
            {
                text += c;
            }
        }
        std::vector<std::string> tokens;
        std::string current;
        const auto flush = [&] {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current.clear();
            }
        };
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (isSeparator(c))
            {
                flush();
                continue;
            }
            if (isUpper(c) && !current.empty())
            {
                const char previous = text[i - 1];
                const bool afterLower = isLowerOrDigit(previous);
                const bool endsUpperRun
                    = isUpper(previous) && i + 1 < text.size() && text[i + 1] >= 'a' && text[i + 1] <= 'z';
                if (afterLower || endsUpperRun)
                {
                    flush();
                }
            }
            current += lower(c);
        }
        flush();
        return tokens;
    }

    SuggestResult suggestSkeletonMap(const SuggestInput& input)
    {
        SuggestResult result;
        result.mMap.mName = input.mName;
        result.mMap.mProfile = input.mProfile;
        const std::vector<RestBone>& skeleton = input.mSkeleton;

        const MapReport sourceReport = validateSourceSkeleton(skeleton);
        if (sourceReport.hasErrors())
        {
            for (const MapFinding& f : sourceReport.mFindings)
            {
                if (f.mSeverity == MapSeverity::Error)
                {
                    result.mNotes.push_back(
                        "the source skeleton is invalid: " + f.mId + " " + f.mPointer + ": " + f.mMessage);
                    break;
                }
            }
            return result;
        }
        result.mMap.mSource.mBoneCount = static_cast<std::uint32_t>(skeleton.size());
        result.mMap.mSource.mDigest = skeletonDigest(skeleton);

        const std::size_t count = skeleton.size();
        std::map<std::string, std::size_t> indexByName;
        std::map<std::string, std::size_t> indexByNormalised;
        for (std::size_t i = 0; i < count; ++i)
        {
            indexByName.emplace(skeleton[i].mName, i);
            indexByNormalised.emplace(normalisedBoneName(skeleton[i].mName), i);
        }

        // ---- roles ----
        const std::vector<RoleInfo>& canonical = canonicalRoles();
        std::map<std::string, const RoleBinding*> tableByRole;
        for (const RoleBinding& binding : input.mRoles)
        {
            if (findRole(binding.mRole) == nullptr)
            {
                result.mNotes.push_back("the role table names " + binding.mRole + ", which is not a canonical role");
                continue;
            }
            tableByRole.emplace(binding.mRole, &binding);
        }
        std::map<std::string, std::size_t> boneOfRole; // role -> source index
        std::set<std::size_t> roleBones;
        const auto claim = [&](const std::string& role, std::size_t index) {
            if (roleBones.count(index) != 0)
            {
                result.mNotes.push_back(
                    "the bone " + skeleton[index].mName + " matches more than one role; it stays with the first");
                return false;
            }
            boneOfRole[role] = index;
            roleBones.insert(index);
            return true;
        };
        const auto findBone = [&](const std::string& name) -> std::optional<std::size_t> {
            if (const auto exact = indexByName.find(name); exact != indexByName.end())
            {
                return exact->second;
            }
            if (const auto normal = indexByNormalised.find(normalisedBoneName(name)); normal != indexByNormalised.end())
            {
                return normal->second;
            }
            return std::nullopt;
        };
        for (const RoleInfo& info : canonical)
        {
            const auto entry = tableByRole.find(std::string(info.mName));
            if (entry == tableByRole.end())
            {
                continue;
            }
            std::optional<std::size_t> found = findBone(entry->second->mBone);
            for (std::size_t a = 0; !found && a < entry->second->mAliases.size(); ++a)
            {
                found = findBone(entry->second->mAliases[a]);
            }
            if (found)
            {
                claim(std::string(info.mName), *found);
            }
        }
        // Token matching for the roles still unresolved: the roles of the pack's table, or with no table the core roles
        // plus clavicles, a third spine and toes.
        std::map<std::size_t, std::string> keyOfBone;
        for (std::size_t i = 0; i < count; ++i)
        {
            keyOfBone[i] = roleByTokens(boneNameTokens(skeleton[i].mName));
        }
        for (const RoleInfo& info : canonical)
        {
            const std::string role(info.mName);
            if (boneOfRole.count(role) != 0)
            {
                continue;
            }
            const bool tokenRole = input.mRoles.empty()
                ? (!info.mOptional || role == "LeftClavicle" || role == "RightClavicle" || role == "Spine2"
                    || role == "LeftToes" || role == "RightToes")
                : tableByRole.count(role) != 0;
            if (!tokenRole)
            {
                continue;
            }
            std::vector<std::size_t> candidates;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (roleBones.count(i) == 0 && keyOfBone[i] == role)
                {
                    candidates.push_back(i);
                }
            }
            if (candidates.size() == 1)
            {
                claim(role, candidates[0]);
            }
            else if (candidates.size() > 1)
            {
                std::string names;
                for (const std::size_t c : candidates)
                {
                    names += (names.empty() ? "" : ", ") + skeleton[c].mName;
                }
                result.mNotes.push_back("the role " + role + " is ambiguous: " + names);
            }
        }
        for (const RoleInfo& info : canonical)
        {
            const std::string role(info.mName);
            const auto bone = boneOfRole.find(role);
            if (bone != boneOfRole.end())
            {
                MapRole entry;
                entry.mRole = role;
                entry.mBone = skeleton[bone->second].mName;
                if (const auto table = tableByRole.find(role); table != tableByRole.end())
                {
                    entry.mAliases = table->second->mAliases;
                }
                result.mMap.mRoles.push_back(std::move(entry));
            }
            else if (!info.mOptional || tableByRole.count(role) != 0)
            {
                result.mMissingRoles.push_back(role);
            }
        }
        const bool fingerRolesExist = std::any_of(tableByRole.begin(), tableByRole.end(), [](const auto& entry) {
            return entry.first.find("Thumb") != std::string::npos || entry.first.find("Index") != std::string::npos
                || entry.first.find("Middle") != std::string::npos || entry.first.find("Ring") != std::string::npos
                || entry.first.find("Pinky") != std::string::npos;
        });
        const bool headResolved = boneOfRole.count("Head") != 0;

        // ---- facts about each bone ----
        std::map<std::string, double> mass;
        for (const BoneWeightMass& m : input.mMasses)
        {
            mass[m.mBone] += m.mMass;
        }
        std::vector<bool> hasChildren(count, false);
        std::vector<std::size_t> depth(count, 0);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (skeleton[i].mParent >= 0)
            {
                hasChildren[static_cast<std::size_t>(skeleton[i].mParent)] = true;
                depth[i] = depth[static_cast<std::size_t>(skeleton[i].mParent)] + 1;
            }
        }
        std::set<std::size_t> handBones;
        for (const char* hand : { "LeftHand", "RightHand" })
        {
            if (const auto it = boneOfRole.find(hand); it != boneOfRole.end())
            {
                handBones.insert(it->second);
            }
        }
        const std::vector<Mat4> world = worldRestMatrices(skeleton);
        double extent = 0;
        std::vector<std::array<double, 3>> position(count);
        if (world.size() == count)
        {
            std::array<double, 3> low{ 1e300, 1e300, 1e300 };
            std::array<double, 3> high{ -1e300, -1e300, -1e300 };
            for (std::size_t i = 0; i < count; ++i)
            {
                position[i] = transformPoint({ 0, 0, 0 }, world[i]);
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    low[axis] = std::min(low[axis], position[i][axis]);
                    high[axis] = std::max(high[axis], position[i][axis]);
                }
            }
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                extent = std::max(extent, high[axis] - low[axis]);
            }
        }

        // ---- extra bones, shallow first ----
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (roleBones.count(i) == 0)
            {
                order.push_back(i);
            }
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (depth[a] != depth[b])
            {
                return depth[a] < depth[b];
            }
            const std::string na = normalisedBoneName(skeleton[a].mName);
            const std::string nb = normalisedBoneName(skeleton[b].mName);
            if (na != nb)
            {
                return na < nb;
            }
            return skeleton[a].mName < skeleton[b].mName;
        });

        std::map<std::size_t, std::string> secondaryName; // decided secondary bones, for parents
        std::map<std::string, std::string> roleOfBone;
        for (const auto& [role, index] : boneOfRole)
        {
            roleOfBone[skeleton[index].mName] = role;
        }
        const std::string rootRole = boneOfRole.count("Pelvis") != 0
            ? "Pelvis"
            : (result.mMap.mRoles.empty() ? std::string() : result.mMap.mRoles.front().mRole);

        for (const std::size_t index : order)
        {
            const RestBone& bone = skeleton[index];
            // Nearest mapped ancestor: a role bone (its role name) or a decided secondary bone (its own name), plus
            // whether an ancestor is a hand.
            std::string mappedParent;
            std::optional<std::size_t> mappedParentIndex;
            BoneFacts facts;
            const auto massIt = mass.find(bone.mName);
            facts.mHasWeight = massIt != mass.end() && massIt->second >= sWeightMassThreshold;
            facts.mHasChildren = hasChildren[index];
            for (int up = bone.mParent; up >= 0; up = skeleton[static_cast<std::size_t>(up)].mParent)
            {
                const auto u = static_cast<std::size_t>(up);
                if (handBones.count(u) != 0)
                {
                    facts.mUnderHand = true;
                }
                if (mappedParent.empty())
                {
                    if (roleBones.count(u) != 0)
                    {
                        mappedParent = roleOfBone[skeleton[u].mName];
                        mappedParentIndex = u;
                    }
                    else if (const auto sec = secondaryName.find(u); sec != secondaryName.end())
                    {
                        mappedParent = sec->second;
                        mappedParentIndex = u;
                    }
                }
            }

            std::vector<Vote> votes = nameVotes(boneNameTokens(bone.mName), facts, fingerRolesExist, headResolved);
            if (votes.empty() && input.mHasMasses)
            {
                if (facts.mHasWeight)
                {
                    votes.push_back(foldVote("weight.deform", MapConfidence::Medium));
                }
                else if (!facts.mHasChildren)
                {
                    votes.push_back(dropVote("no weights, no children", "hierarchy.leaf", MapConfidence::Medium));
                }
                else
                {
                    votes.push_back(attachVote("node", "hierarchy.transform", MapConfidence::Medium));
                }
            }
            if (votes.empty() && !input.mHasMasses && mappedParentIndex && extent > 0)
            {
                if (length(position[index], position[*mappedParentIndex]) <= sCoincidentFraction * extent)
                {
                    votes.push_back(foldVote("rest.coincident", MapConfidence::Low));
                }
            }

            MapExtra entry;
            entry.mBone = bone.mName;
            entry.mAccepted = false;
            if (votes.empty())
            {
                entry.mAction = MapAction::Ask;
                entry.mSuggested = MapSuggestion{ "none", MapConfidence::Low };
            }
            else
            {
                MapConfidence top = MapConfidence::Low;
                for (const Vote& v : votes)
                {
                    top = std::max(top, v.mConfidence);
                }
                std::vector<const Vote*> best;
                for (const Vote& v : votes)
                {
                    if (v.mConfidence == top)
                    {
                        best.push_back(&v);
                    }
                }
                const bool agree
                    = std::all_of(best.begin(), best.end(), [&](const Vote* v) { return v->sameDecision(*best[0]); });
                if (!agree)
                {
                    entry.mAction = MapAction::Ask;
                    entry.mSuggested = MapSuggestion{ "ambiguous", top };
                }
                else
                {
                    const Vote& chosen = *best[0];
                    entry.mAction = chosen.mAction;
                    entry.mSuggested = MapSuggestion{ chosen.mHeuristic, chosen.mConfidence };
                    switch (chosen.mAction)
                    {
                        case MapAction::Secondary:
                            entry.mParent = mappedParent.empty() ? rootRole : mappedParent;
                            entry.mKind = chosen.mKind;
                            break;
                        case MapAction::Fold:
                            entry.mRule = chosen.mRule;
                            entry.mTo = chosen.mTo;
                            break;
                        case MapAction::Attach:
                            entry.mParent = mappedParent.empty() ? rootRole : mappedParent;
                            entry.mAttachPoint = chosen.mAttachPoint;
                            break;
                        case MapAction::Drop:
                            entry.mReason = chosen.mReason;
                            break;
                        case MapAction::Ask:
                            break;
                    }
                    if ((entry.mAction == MapAction::Secondary || entry.mAction == MapAction::Attach)
                        && entry.mParent.empty())
                    {
                        // No mapped bone to hang it from: a person decides.
                        entry = MapExtra{};
                        entry.mBone = bone.mName;
                        entry.mAction = MapAction::Ask;
                        entry.mSuggested = MapSuggestion{ "no-parent", chosen.mConfidence };
                    }
                    else if (entry.mAction == MapAction::Secondary)
                    {
                        secondaryName[index] = bone.mName;
                    }
                }
            }
            result.mMap.mExtra.push_back(std::move(entry));
        }
        return result;
    }

    AcceptResult acceptAllSuggestions(SkeletonMap& map)
    {
        AcceptResult result;
        for (MapExtra& entry : map.mExtra)
        {
            if (entry.mAction == MapAction::Ask)
            {
                ++result.mRemainingAsks;
            }
            else if (!entry.mAccepted)
            {
                entry.mAccepted = true;
                ++result.mAccepted;
            }
        }
        return result;
    }
}

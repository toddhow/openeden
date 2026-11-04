#include "skeletonstats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Body
{
    namespace
    {
        constexpr double sRadiansToDegrees = 180.0 / 3.14159265358979323846;
        constexpr std::size_t sMaxLabelBytes = 64;
        constexpr const char* sProfile = "humanoid-to-eden";

        // The side tokens of the suggestion engine's role match (skeletonsuggest.cpp, roleByTokens).
        bool sideOf(const std::string& token, char& side)
        {
            if (token == "l" || token == "left" || token == "lf")
            {
                side = 'l';
                return true;
            }
            if (token == "r" || token == "right" || token == "rt")
            {
                side = 'r';
                return true;
            }
            return false;
        }

        bool isDigit(char c)
        {
            return c >= '0' && c <= '9';
        }

        // Fixed-format numbers; "-0.000" is written as "0.000" so a sign of zero never makes two reports differ.
        std::string fixed(double value, int decimals)
        {
            if (!std::isfinite(value))
            {
                return "nan";
            }
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            std::string text(buffer);
            if (!text.empty() && text[0] == '-' && text.find_first_not_of("-0.") == std::string::npos)
            {
                text.erase(0, 1);
            }
            return text;
        }

        std::string percent(std::size_t part, std::size_t whole)
        {
            if (whole == 0)
            {
                return "-";
            }
            return fixed(100.0 * static_cast<double>(part) / static_cast<double>(whole), 1) + "%";
        }

        std::string joined(const std::vector<std::string>& values)
        {
            if (values.empty())
            {
                return "-";
            }
            std::string text;
            for (const std::string& value : values)
            {
                text += (text.empty() ? "" : ", ") + value;
            }
            return text;
        }

        std::string facts(const std::vector<std::pair<std::string, std::string>>& values)
        {
            std::string text;
            for (const auto& [key, value] : values)
            {
                text += (text.empty() ? "" : " | ") + key + " " + value;
            }
            return text;
        }

        std::string quantileLine(const Quantiles& q, int decimals)
        {
            return "n " + std::to_string(q.mCount) + " | min " + fixed(q.mMin, decimals) + " | p25 "
                + fixed(q.mP25, decimals) + " | median " + fixed(q.mMedian, decimals) + " | p75 "
                + fixed(q.mP75, decimals) + " | max " + fixed(q.mMax, decimals);
        }

        double length(const std::array<double, 3>& v)
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        double distance(const std::array<double, 3>& a, const std::array<double, 3>& b)
        {
            return length({ a[0] - b[0], a[1] - b[1], a[2] - b[2] });
        }

        // The role an ambiguity note of suggestSkeletonMap names ("the role <Role> is ambiguous: ...").
        std::string ambiguousRoleOf(const std::string& note)
        {
            static const std::string prefix = "the role ";
            static const std::string suffix = " is ambiguous";
            if (note.rfind(prefix, 0) != 0)
            {
                return {};
            }
            const std::size_t end = note.find(suffix, prefix.size());
            if (end == std::string::npos)
            {
                return {};
            }
            return note.substr(prefix.size(), end - prefix.size());
        }

        int confidenceRank(MapConfidence confidence)
        {
            switch (confidence)
            {
                case MapConfidence::High:
                    return 0;
                case MapConfidence::Medium:
                    return 1;
                case MapConfidence::Low:
                    return 2;
            }
            return 3;
        }

        std::string decisionText(const MapExtra& entry)
        {
            std::string text = toString(entry.mAction);
            if (entry.mKind)
            {
                text += std::string("/") + toString(*entry.mKind);
            }
            if (entry.mSuggested)
            {
                text += " " + entry.mSuggested->mHeuristic + " " + toString(entry.mSuggested->mConfidence);
            }
            return text;
        }

        // The strict verdict on a source skeleton and, when it fails only on colliding names (BODY-MAP-022), the
        // relaxed copy the suggestions are measured on.
        struct Verdict
        {
            std::vector<std::string> mRules; // error rule ids, sorted, each once
            bool mRelaxed = false;
            std::size_t mRenamed = 0;
            std::size_t mExactKeyDuplicates = 0;
            std::size_t mNormalisedOnlyCollisions = 0;
            std::vector<RestBone> mMeasured; // the skeleton the suggestions run on
        };

        constexpr const char* sCollisionRule = "BODY-MAP-022";

        Verdict judge(const std::vector<RestBone>& skeleton)
        {
            Verdict verdict;
            std::set<std::string> rules;
            for (const MapFinding& finding : validateSourceSkeleton(skeleton).mFindings)
            {
                if (finding.mSeverity == MapSeverity::Error)
                {
                    rules.insert(finding.mId);
                }
            }
            verdict.mRules.assign(rules.begin(), rules.end());
            // Each later bone of a colliding name gets "~<n>" ('~' is not a separator of boneNameTokens, so the stems
            // and the vocabulary see the same words).
            std::vector<RestBone> relaxed = skeleton;
            std::set<std::string> keys;
            std::map<std::string, std::size_t> seen;
            for (RestBone& bone : relaxed)
            {
                const bool exact = !keys.insert(engineBoneKey(bone.mName)).second;
                const std::size_t times = ++seen[normalisedBoneName(bone.mName)];
                if (times > 1)
                {
                    ++(exact ? verdict.mExactKeyDuplicates : verdict.mNormalisedOnlyCollisions);
                    bone.mName += "~" + std::to_string(times);
                    ++verdict.mRenamed;
                }
            }
            verdict.mMeasured = skeleton;
            if (verdict.mRules == std::vector<std::string>{ sCollisionRule } && verdict.mRenamed > 0
                && !validateSourceSkeleton(relaxed).hasErrors())
            {
                verdict.mRelaxed = true;
                verdict.mMeasured = std::move(relaxed);
            }
            return verdict;
        }

        SuggestionStats summarise(
            const SuggestResult& result, const Verdict& verdict, const std::map<std::string, double>& shareOfBone)
        {
            SuggestionStats s;
            s.mValid = verdict.mRules.empty();
            s.mInvalidRules = verdict.mRules;
            s.mRelaxed = verdict.mRelaxed;
            s.mRenamed = verdict.mRelaxed ? verdict.mRenamed : 0;
            for (const MapRole& role : result.mMap.mRoles)
            {
                const RoleInfo* info = findRole(role.mRole);
                if (info != nullptr && !info->mOptional)
                {
                    ++s.mCoreRoles;
                }
                else
                {
                    ++s.mOptionalRoles;
                }
            }
            s.mMissingCoreRoles = result.mMap.mMissingRoles;
            std::set<std::string> ambiguous;
            for (const std::string& note : result.mNotes)
            {
                if (const std::string role = ambiguousRoleOf(note); !role.empty())
                {
                    ambiguous.insert(role);
                }
            }
            for (const RoleInfo& info : canonicalRoles())
            {
                if (ambiguous.count(std::string(info.mName)) != 0)
                {
                    s.mAmbiguousRoles.emplace_back(info.mName);
                }
            }
            s.mNotes = result.mNotes.size();
            using Key = std::tuple<std::string, int, int, int>; // heuristic, confidence rank, action, kind (-1 none)
            std::map<Key, HeuristicCount> counts;
            for (const MapExtra& entry : result.mMap.mExtra)
            {
                ++s.mExtra;
                const MapConfidence confidence = entry.mSuggested ? entry.mSuggested->mConfidence : MapConfidence::Low;
                if (entry.mAction == MapAction::Ask)
                {
                    ++s.mAsk;
                }
                else if (confidence == MapConfidence::Medium || confidence == MapConfidence::High)
                {
                    ++s.mMediumOrHigher;
                }
                const std::string heuristic = entry.mSuggested ? entry.mSuggested->mHeuristic : std::string();
                const Key key{ heuristic, confidenceRank(confidence), static_cast<int>(entry.mAction),
                    entry.mKind ? static_cast<int>(*entry.mKind) : -1 };
                HeuristicCount& count = counts[key];
                count.mHeuristic = heuristic;
                count.mConfidence = confidence;
                count.mAction = entry.mAction;
                count.mKind = entry.mKind;
                ++count.mCount;
                if (const auto share = shareOfBone.find(entry.mBone); share != shareOfBone.end())
                {
                    count.mShare += share->second;
                }
            }
            for (const auto& [key, count] : counts)
            {
                s.mHeuristics.push_back(count);
            }
            return s;
        }

        std::vector<std::pair<std::string, std::size_t>> sortedCounts(const std::map<std::string, std::size_t>& counts)
        {
            std::vector<std::pair<std::string, std::size_t>> sorted(counts.begin(), counts.end());
            std::stable_sort(
                sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            return sorted;
        }

        void writeSuggestions(std::string& out, const SuggestionStats& s, const std::string& indent, bool withShares)
        {
            out += indent + "valid source skeleton " + std::string(s.mValid ? "yes" : "no");
            if (!s.mValid)
            {
                out += " (" + joined(s.mInvalidRules) + ")";
            }
            if (s.mRelaxed)
            {
                out += " | measured relaxed: " + std::to_string(s.mRenamed) + " renamed";
            }
            out += " | medium+ " + std::to_string(s.mMediumOrHigher) + " of " + std::to_string(s.mExtra) + " ("
                + percent(s.mMediumOrHigher, s.mExtra) + ") | ask " + std::to_string(s.mAsk) + "\n";
            for (const HeuristicCount& h : s.mHeuristics)
            {
                out += indent + "  " + (h.mHeuristic.empty() ? std::string("(none)") : h.mHeuristic) + " "
                    + toString(h.mConfidence) + " " + toString(h.mAction);
                if (h.mKind)
                {
                    out += std::string("/") + toString(*h.mKind);
                }
                out += " " + std::to_string(h.mCount);
                if (withShares)
                {
                    out += " share " + fixed(h.mShare, 6);
                }
                out += "\n";
            }
        }

        void writeRest(std::string& out, const std::vector<RoleRestDifference>& rest, bool valid, std::size_t errors,
            bool needsInverseBind)
        {
            out += "  valid " + std::string(valid ? "yes" : "no") + " | errors " + std::to_string(errors)
                + " | inverse-bind correction needed " + (needsInverseBind ? "yes" : "no") + "\n";
            if (!rest.empty())
            {
                out += "  role | translation delta m | length ratio | rotation deg\n";
            }
            for (const RoleRestDifference& d : rest)
            {
                out += "  " + d.mRole + " | " + fixed(d.mTranslationDelta, 4) + " | " + fixed(d.mLengthRatio, 4) + " | "
                    + fixed(d.mRotationAngle * sRadiansToDegrees, 2) + "\n";
            }
        }

        double maxAngleDegrees(const std::vector<RoleRestDifference>& rest)
        {
            double largest = 0;
            for (const RoleRestDifference& d : rest)
            {
                largest = std::max(largest, d.mRotationAngle * sRadiansToDegrees);
            }
            return largest;
        }

        bool validLabel(const std::string& label)
        {
            if (label.empty() || label.size() > sMaxLabelBytes)
            {
                return false;
            }
            for (const char c : label)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '.' || c == '_'
                    || c == '-';
                if (!ok)
                {
                    return false;
                }
            }
            return true;
        }

        std::vector<std::string> splitTabs(const std::string& line)
        {
            std::vector<std::string> fields;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t tab = line.find('\t', start);
                fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
                if (tab == std::string::npos)
                {
                    break;
                }
                start = tab + 1;
            }
            return fields;
        }
    }

    std::string boneComparisonKey(std::string_view name)
    {
        std::string side;
        std::vector<std::string> rest;
        for (const std::string& token : boneNameTokens(name))
        {
            if (isNoiseBoneToken(token))
            {
                continue;
            }
            char s = 0;
            if (side.empty() && sideOf(token, s))
            {
                side = std::string(1, s);
                continue;
            }
            rest.push_back(token);
        }
        if (rest.empty())
        {
            return {};
        }
        std::string key = side;
        for (const std::string& token : rest)
        {
            key += (key.empty() ? "" : "_") + token;
        }
        return key;
    }

    std::vector<std::string> boneTokenStems(std::string_view name)
    {
        std::vector<std::string> stems;
        for (const std::string& token : boneNameTokens(name))
        {
            if (isNoiseBoneToken(token))
            {
                continue;
            }
            std::string stem;
            char s = 0;
            if (sideOf(token, s))
            {
                stem = std::string(1, s);
            }
            else
            {
                std::size_t end = token.size();
                while (end > 0 && isDigit(token[end - 1]))
                {
                    --end;
                }
                stem = end == 0 ? std::string("#") : token.substr(0, end);
                if (isNoiseBoneToken(stem))
                {
                    continue; // "bone01" says no more than "bone"
                }
            }
            if (std::find(stems.begin(), stems.end(), stem) == stems.end())
            {
                stems.push_back(std::move(stem));
            }
        }
        return stems;
    }

    Quantiles quantiles(std::vector<double> values)
    {
        Quantiles q;
        if (values.empty())
        {
            return q;
        }
        std::sort(values.begin(), values.end());
        const std::size_t n = values.size();
        q.mCount = n;
        q.mMin = values.front();
        q.mMax = values.back();
        // Nearest rank: index ceil(p * n) - 1, in integers.
        q.mP25 = values[(n + 3) / 4 - 1];
        q.mMedian = values[(n + 1) / 2 - 1];
        q.mP75 = values[(3 * n + 3) / 4 - 1];
        return q;
    }

    SkeletonStats computeSkeletonStats(const SkeletonStatInput& input)
    {
        SkeletonStats stats;
        stats.mLabel = input.mLabel;
        stats.mBaseLabel = input.mBaseLabel;
        stats.mFacts = input.mFacts;
        const std::vector<RestBone>& skeleton = input.mSkeleton;
        const std::size_t count = skeleton.size();
        stats.mBones = count;

        std::vector<std::size_t> depth(count, 0);
        std::vector<bool> hasChildren(count, false);
        for (std::size_t i = 0; i < count; ++i)
        {
            const int parent = skeleton[i].mParent;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
            {
                depth[i] = depth[static_cast<std::size_t>(parent)] + 1;
                hasChildren[static_cast<std::size_t>(parent)] = true;
            }
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            stats.mMaxDepth = std::max(stats.mMaxDepth, depth[i]);
            if (!hasChildren[i])
            {
                ++stats.mLeaves;
            }
        }
        const std::vector<Mat4> world = worldRestMatrices(skeleton);
        std::vector<std::array<double, 3>> position;
        if (world.size() == count && count > 0)
        {
            double low = 1e300;
            double high = -1e300;
            for (const Mat4& m : world)
            {
                position.push_back(transformPoint({ 0, 0, 0 }, m));
                low = std::min(low, position.back()[2]);
                high = std::max(high, position.back()[2]);
            }
            stats.mHeight = high - low;
        }

        // The base set.
        std::set<std::string> baseKeys;
        for (const RestBone& bone : input.mBase)
        {
            if (std::string key = boneComparisonKey(bone.mName); !key.empty())
            {
                baseKeys.insert(std::move(key));
            }
        }
        stats.mBaseBones = input.mBase.size();
        std::vector<bool> inBase(count, false);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::string key = boneComparisonKey(skeleton[i].mName);
            inBase[i] = !key.empty() && baseKeys.count(key) != 0;
            if (inBase[i])
            {
                ++stats.mInBase;
            }
        }
        stats.mNotInBase = count - stats.mInBase;

        // The strict verdict, and the skeleton the suggestions are measured on (relaxed when only names collide).
        const Verdict verdict = judge(skeleton);
        stats.mExactKeyDuplicates = verdict.mExactKeyDuplicates;
        stats.mNormalisedOnlyCollisions = verdict.mNormalisedOnlyCollisions;
        const std::vector<RestBone>& measured = verdict.mMeasured;

        // The suggestion from the skeleton alone: tokens only, and with the role table when there is one. The table's
        // map (else the tokens') is the ANALYSIS map: it defines the extra bones.
        SuggestInput suggest;
        suggest.mName = input.mLabel;
        suggest.mProfile = sProfile;
        suggest.mSkeleton = measured;
        const SuggestResult skeletonOnly = suggestSkeletonMap(suggest);
        stats.mSuggestion = summarise(skeletonOnly, verdict, {});
        SuggestInput analysisInput = suggest;
        SuggestResult withTable;
        stats.mHasRoleTable = !input.mRoles.empty();
        if (stats.mHasRoleTable)
        {
            analysisInput.mRoles = input.mRoles;
            withTable = suggestSkeletonMap(analysisInput);
            stats.mSuggestionWithTable = summarise(withTable, verdict, {});
        }
        const SuggestResult& analysis = stats.mHasRoleTable ? withTable : skeletonOnly;
        std::map<std::string, std::string> roleOfBone;
        for (const MapRole& role : analysis.mMap.mRoles)
        {
            roleOfBone[role.mBone] = role.mRole;
        }
        std::vector<bool> isRole(count, false);
        for (std::size_t i = 0; i < count; ++i)
        {
            isRole[i] = roleOfBone.count(measured[i].mName) != 0;
            if (!isRole[i])
            {
                ++stats.mExtra;
            }
        }
        const auto parentIsRole = [&](std::size_t i) {
            const int parent = skeleton[i].mParent;
            return parent >= 0 && static_cast<std::size_t>(parent) < i && isRole[static_cast<std::size_t>(parent)];
        };
        const auto atParent = [&](std::size_t i) {
            return skeleton[i].mParent >= 0 && length(skeleton[i].mTranslation) < sZeroOffsetM;
        };

        // The bones on the way from the root to a role bone (the role bones included).
        std::vector<bool> onRolePath(count, false);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!isRole[i])
            {
                continue;
            }
            for (int up = static_cast<int>(i); up >= 0 && !onRolePath[static_cast<std::size_t>(up)];
                 up = skeleton[static_cast<std::size_t>(up)].mParent)
            {
                onRolePath[static_cast<std::size_t>(up)] = true;
                if (skeleton[static_cast<std::size_t>(up)].mParent >= up)
                {
                    break;
                }
            }
        }
        // Untrusted rest values on that way.
        if (input.mUntrusted.size() == count)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (input.mUntrusted[i] && onRolePath[i])
                {
                    ++stats.mUntrustedOnRolePath;
                }
            }
        }

        // Extra bones: stems, offsets, distances.
        std::map<std::string, std::size_t> stems;
        std::vector<double> parentOffsets;
        std::vector<double> ancestorDistances;
        std::vector<double> ancestorOf(count, -1);
        for (std::size_t i = 0; i < count; ++i)
        {
            bool roleAbove = false;
            for (int up = skeleton[i].mParent; up >= 0 && static_cast<std::size_t>(up) < i;
                 up = skeleton[static_cast<std::size_t>(up)].mParent)
            {
                if (isRole[static_cast<std::size_t>(up)])
                {
                    roleAbove = true;
                    if (position.size() == count)
                    {
                        ancestorOf[i] = distance(position[i], position[static_cast<std::size_t>(up)]);
                    }
                    break;
                }
            }
            if (isRole[i])
            {
                continue;
            }
            if (roleAbove && onRolePath[i])
            {
                ++stats.mExtraOnRoleChain;
            }
            for (const std::string& stem : boneTokenStems(skeleton[i].mName))
            {
                ++stems[stem];
            }
            if (skeleton[i].mParent >= 0)
            {
                parentOffsets.push_back(length(skeleton[i].mTranslation));
            }
            if (parentIsRole(i))
            {
                ++stats.mExtraUnderRole;
            }
            if (atParent(i))
            {
                ++stats.mExtraZeroOffset;
            }
            if (ancestorOf[i] >= 0)
            {
                ancestorDistances.push_back(ancestorOf[i]);
            }
        }
        stats.mTokens = sortedCounts(stems);
        stats.mParentOffset = quantiles(parentOffsets);
        stats.mAncestorDistance = quantiles(ancestorDistances);

        // The source bones by the engine's key (the first of a key wins).
        std::map<std::string, std::size_t> sourceByKey;
        for (std::size_t i = 0; i < count; ++i)
        {
            sourceByKey.emplace(engineBoneKey(skeleton[i].mName), i);
        }

        // Body sets.
        std::vector<std::vector<double>> sharePerSet;
        std::vector<SuggestResult> setSuggestions;
        for (const BodySetInput& set : input.mBodySets)
        {
            MassStats m;
            m.mLabel = set.mLabel;
            m.mFacts = set.mFacts;
            std::map<std::string, double> byKey;
            for (const BoneWeightMass& mass : set.mMasses)
            {
                if (std::isfinite(mass.mMass) && mass.mMass > 0)
                {
                    byKey[engineBoneKey(mass.mBone)] += mass.mMass;
                }
            }
            double total = 0;
            for (const auto& [key, mass] : byKey)
            {
                total += mass;
            }
            m.mTotal = total;
            std::vector<double> shares(count, 0.0);
            double missing = 0;
            for (const auto& [key, mass] : byKey)
            {
                ++m.mSkinBones;
                const auto source = sourceByKey.find(key);
                if (source == sourceByKey.end())
                {
                    ++m.mSkinBonesMissing;
                    missing += mass;
                    continue;
                }
                const double share = total > 0 ? mass / total : 0.0;
                const std::size_t bone = source->second;
                shares[bone] += share;
                // Summed in key order, so the result does not depend on the order of the source bones.
                (isRole[bone] ? m.mRoleShare : m.mExtraShare) += share;
                if (!isRole[bone] && parentIsRole(bone))
                {
                    m.mShareUnderRole += share;
                    if (atParent(bone))
                    {
                        m.mShareUnderRoleZeroOffset += share;
                    }
                }
                if (!isRole[bone])
                {
                    const double d = ancestorOf[bone];
                    const std::size_t bin = d < 0 ? 4 : (d < 0.01 ? 0 : (d < 0.05 ? 1 : (d < 0.15 ? 2 : 3)));
                    m.mShareByAncestorDistance[bin] += share;
                }
            }
            m.mMissingShare = total > 0 ? missing / total : 0.0;
            std::vector<double> extraShares;
            std::map<std::string, double> shareOfBone;
            SuggestInput withMasses = analysisInput;
            withMasses.mHasMasses = true;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (shares[i] > 0)
                {
                    withMasses.mMasses.push_back({ measured[i].mName, shares[i] });
                    shareOfBone[measured[i].mName] = shares[i];
                }
                if (isRole[i])
                {
                    continue;
                }
                const double share = shares[i];
                std::size_t bin = 0;
                if (share >= 1e-2)
                {
                    bin = 4;
                }
                else if (share >= 1e-3)
                {
                    bin = 3;
                }
                else if (share >= sMapWeightMassThreshold)
                {
                    bin = 2;
                }
                else if (share > 0)
                {
                    bin = 1;
                }
                ++m.mExtraMassBins[bin];
                if (share > 0)
                {
                    ++m.mExtraWithAnyMass;
                    extraShares.push_back(share);
                }
                if (share >= sMapWeightMassThreshold)
                {
                    ++m.mExtraWithMass;
                }
            }
            m.mExtraMass = quantiles(extraShares);
            const SuggestResult result = suggestSkeletonMap(withMasses);
            m.mSuggestion = summarise(result, verdict, shareOfBone);
            stats.mBodySets.push_back(std::move(m));
            sharePerSet.push_back(std::move(shares));
            setSuggestions.push_back(result);
        }

        // Rest differences against the base.
        if (!input.mBase.empty())
        {
            const RestComparison comparison = compareRest(analysis.mMap, measured, input.mBase, input.mBaseRoles);
            const RestPlan plan = planRest(comparison);
            stats.mRest = comparison.mDifferences;
            stats.mRestErrors = comparison.mErrors.size();
            stats.mRestValid = plan.mValid;
            stats.mNeedsInverseBind = plan.mNeedsInverseBindCorrection;
        }

        // Detail, in source order.
        const auto suggestionOf = [](const SuggestResult& result, const std::string& bone, const std::string& role) {
            if (!role.empty())
            {
                return "role " + role;
            }
            for (const MapExtra& entry : result.mMap.mExtra)
            {
                if (entry.mBone == bone)
                {
                    return decisionText(entry);
                }
            }
            return std::string("-");
        };
        for (std::size_t i = 0; i < count; ++i)
        {
            BoneDetail detail;
            detail.mName = skeleton[i].mName;
            const int parent = skeleton[i].mParent;
            detail.mParent = parent >= 0 && static_cast<std::size_t>(parent) < count
                ? skeleton[static_cast<std::size_t>(parent)].mName
                : std::string();
            detail.mDepth = depth[i];
            if (const auto role = roleOfBone.find(measured[i].mName); role != roleOfBone.end())
            {
                detail.mRole = role->second;
            }
            detail.mInBase = inBase[i];
            detail.mParentOffset = length(skeleton[i].mTranslation);
            detail.mAncestorDistance = ancestorOf[i];
            for (const std::vector<double>& shares : sharePerSet)
            {
                detail.mShares.push_back(shares[i]);
            }
            // The tokens-only decision of a bone the table makes a role is still the tokens' own.
            const auto tokenRole = [&]() -> std::string {
                for (const MapRole& role : skeletonOnly.mMap.mRoles)
                {
                    if (role.mBone == measured[i].mName)
                    {
                        return role.mRole;
                    }
                }
                return {};
            };
            detail.mSuggestions.push_back(suggestionOf(skeletonOnly, measured[i].mName, tokenRole()));
            if (stats.mHasRoleTable)
            {
                detail.mSuggestions.push_back(suggestionOf(withTable, measured[i].mName, detail.mRole));
            }
            for (const SuggestResult& result : setSuggestions)
            {
                detail.mSuggestions.push_back(suggestionOf(result, measured[i].mName, detail.mRole));
            }
            stats.mDetail.push_back(std::move(detail));
        }
        return stats;
    }

    RestPairStats compareSkeletonRests(const std::string& firstLabel, const std::vector<RestBone>& first,
        const std::string& secondLabel, const std::vector<RestBone>& second, const std::vector<RoleBinding>& firstRoles,
        const std::vector<RoleBinding>& secondRoles)
    {
        RestPairStats pair;
        pair.mFirst = firstLabel;
        pair.mSecond = secondLabel;
        const Verdict verdictA = judge(first);
        const Verdict verdictB = judge(second);
        SuggestInput a;
        a.mName = firstLabel;
        a.mProfile = sProfile;
        a.mSkeleton = verdictA.mMeasured;
        a.mRoles = firstRoles;
        SuggestInput b;
        b.mName = secondLabel;
        b.mProfile = sProfile;
        b.mSkeleton = verdictB.mMeasured;
        b.mRoles = secondRoles;
        const SuggestResult mapA = suggestSkeletonMap(a);
        const SuggestResult mapB = suggestSkeletonMap(b);
        std::vector<RoleBinding> rolesB;
        for (const MapRole& role : mapB.mMap.mRoles)
        {
            rolesB.push_back({ role.mRole, role.mBone, {} });
        }
        pair.mRelaxed = verdictA.mRelaxed || verdictB.mRelaxed;
        const RestComparison comparison = compareRest(mapA.mMap, verdictA.mMeasured, verdictB.mMeasured, rolesB);
        const RestPlan plan = planRest(comparison);
        pair.mRest = comparison.mDifferences;
        pair.mErrors = comparison.mErrors.size();
        pair.mValid = plan.mValid;
        pair.mNeedsInverseBind = plan.mNeedsInverseBindCorrection;
        return pair;
    }

    std::string writeSkeletonStatsReport(
        const std::vector<SkeletonStats>& skeletons, const std::vector<RestPairStats>& pairs)
    {
        std::string out = "openeden.skelstat 1\n";
        out += "units: lengths in metres, angles in degrees, weight as shares of a body set's total skin weight\n\n";

        out += "summary (the analysis map is the role table's where a skeleton has one, else the tokens'; medium+ is "
               "the share of extra bones with a suggestion of medium or high confidence that is not ask)\n";
        out += "| skeleton | base | strict verdict | bones | depth | height m | in base | extra | core roles (tokens) "
               "| core roles (table) | medium+ (skeleton, analysis map) | medium+ (first body set) | extra share "
               "| share under a role bone | missing skin bones | rest plan | largest role angle deg |\n";
        out += "|";
        for (int column = 0; column < 17; ++column)
        {
            out += " --- |";
        }
        out += "\n";
        for (const SkeletonStats& s : skeletons)
        {
            std::string firstSet = "-";
            std::string extraShare = "-";
            std::string underRole = "-";
            std::string missing = "-";
            if (!s.mBodySets.empty())
            {
                const MassStats& m = s.mBodySets.front();
                firstSet = percent(m.mSuggestion.mMediumOrHigher, m.mSuggestion.mExtra);
                extraShare = fixed(m.mExtraShare, 4);
                underRole = fixed(m.mShareUnderRole, 4);
                missing = std::to_string(m.mSkinBonesMissing);
            }
            const SuggestionStats& analysis = s.mHasRoleTable ? s.mSuggestionWithTable : s.mSuggestion;
            std::string verdict = s.mSuggestion.mValid ? std::string("valid") : joined(s.mSuggestion.mInvalidRules);
            if (s.mSuggestion.mRelaxed)
            {
                verdict += " (relaxed)";
            }
            const std::string plan = !s.mRestValid ? "invalid" : (s.mNeedsInverseBind ? "inverse-bind" : "tier-1");
            out += "| " + s.mLabel + " | " + s.mBaseLabel + " | " + verdict + " | " + std::to_string(s.mBones) + " | "
                + std::to_string(s.mMaxDepth) + " | " + fixed(s.mHeight, 3) + " | " + std::to_string(s.mInBase) + " | "
                + std::to_string(s.mExtra) + " | " + std::to_string(s.mSuggestion.mCoreRoles) + "/17 | "
                + (s.mHasRoleTable ? std::to_string(s.mSuggestionWithTable.mCoreRoles) + "/17" : std::string("-"))
                + " | " + percent(analysis.mMediumOrHigher, analysis.mExtra) + " | " + firstSet + " | " + extraShare
                + " | " + underRole + " | " + missing + " | " + plan + " | " + fixed(maxAngleDegrees(s.mRest), 1)
                + " |\n";
        }

        for (const SkeletonStats& s : skeletons)
        {
            out += "\n== skeleton " + s.mLabel + " (base " + s.mBaseLabel + ")\n";
            if (!s.mFacts.empty())
            {
                out += "reader: " + facts(s.mFacts) + "\n";
            }
            out += "bones " + std::to_string(s.mBones) + " | depth " + std::to_string(s.mMaxDepth) + " | leaves "
                + std::to_string(s.mLeaves) + " | height " + fixed(s.mHeight, 4) + " m\n";
            out += "base set: base bones " + std::to_string(s.mBaseBones) + " | in base " + std::to_string(s.mInBase)
                + " | not in base " + std::to_string(s.mNotInBase) + "\n";
            out += "name collisions: same engine key " + std::to_string(s.mExactKeyDuplicates)
                + " | equal only after normalisation " + std::to_string(s.mNormalisedOnlyCollisions) + "\n";
            const auto rolesLine = [&](const char* title, const SuggestionStats& r) {
                out += std::string(title) + ": core " + std::to_string(r.mCoreRoles) + "/17 | optional "
                    + std::to_string(r.mOptionalRoles) + " | missing core " + joined(r.mMissingCoreRoles)
                    + " | ambiguous " + joined(r.mAmbiguousRoles) + " | notes " + std::to_string(r.mNotes) + "\n";
            };
            rolesLine("roles (tokens only)", s.mSuggestion);
            if (s.mHasRoleTable)
            {
                rolesLine("roles (pack table)", s.mSuggestionWithTable);
            }
            out += "extra bones " + std::to_string(s.mExtra) + " (by the " + (s.mHasRoleTable ? "pack table" : "tokens")
                + ") | under a role bone " + std::to_string(s.mExtraUnderRole) + " | between role bones "
                + std::to_string(s.mExtraOnRoleChain) + " | at the parent's position "
                + std::to_string(s.mExtraZeroOffset) + " | untrusted rest values on a role path "
                + std::to_string(s.mUntrustedOnRolePath) + "\n";
            std::string tokens;
            for (const auto& [stem, n] : s.mTokens)
            {
                tokens += (tokens.empty() ? "" : ", ") + stem + " " + std::to_string(n);
            }
            out += "tokens of the extra bones: " + (tokens.empty() ? std::string("-") : tokens) + "\n";
            out += "rest offset to the parent (m): " + quantileLine(s.mParentOffset, 4) + "\n";
            out += "distance to the nearest role ancestor (m): " + quantileLine(s.mAncestorDistance, 4) + "\n";
            out += "suggestions from the skeleton alone (tokens only):\n";
            writeSuggestions(out, s.mSuggestion, "  ", false);
            if (s.mHasRoleTable)
            {
                out += "suggestions from the skeleton alone (pack table):\n";
                writeSuggestions(out, s.mSuggestionWithTable, "  ", false);
            }
            for (const MassStats& m : s.mBodySets)
            {
                out += "body set " + m.mLabel + "\n";
                if (!m.mFacts.empty())
                {
                    out += "  reader: " + facts(m.mFacts) + "\n";
                }
                out += "  total weight " + fixed(m.mTotal, 3) + " | skin bones " + std::to_string(m.mSkinBones)
                    + " | not in the skeleton " + std::to_string(m.mSkinBonesMissing) + " (share "
                    + fixed(m.mMissingShare, 6) + ") | on role bones " + fixed(m.mRoleShare, 6) + " | on extra bones "
                    + fixed(m.mExtraShare, 6) + " | of that under a role bone " + fixed(m.mShareUnderRole, 6)
                    + " (at its position " + fixed(m.mShareUnderRoleZeroOffset, 6) + ")\n";
                out += "  extra bones with weight: any " + std::to_string(m.mExtraWithAnyMass) + " | at least 1e-4 "
                    + std::to_string(m.mExtraWithMass) + " | by share: 0 " + std::to_string(m.mExtraMassBins[0])
                    + " | below 1e-4 " + std::to_string(m.mExtraMassBins[1]) + " | below 1e-3 "
                    + std::to_string(m.mExtraMassBins[2]) + " | below 1e-2 " + std::to_string(m.mExtraMassBins[3])
                    + " | 1e-2 or more " + std::to_string(m.mExtraMassBins[4]) + "\n";
                out += "  shares of the extra bones with weight: " + quantileLine(m.mExtraMass, 6) + "\n";
                const auto& bins = m.mShareByAncestorDistance;
                out += "  weight on extra bones by rest distance to the nearest role ancestor: below 0.01 m "
                    + fixed(bins[0], 6) + " | below 0.05 m " + fixed(bins[1], 6) + " | below 0.15 m "
                    + fixed(bins[2], 6) + " | 0.15 m or more " + fixed(bins[3], 6) + " | no role ancestor "
                    + fixed(bins[4], 6) + "\n";
                out += std::string("  suggestions with these weights (") + (s.mHasRoleTable ? "pack table" : "tokens")
                    + "):\n";
                writeSuggestions(out, m.mSuggestion, "    ", true);
            }
            out += "rest against the base (each role's rest in its parent role's frame, analysis map):\n";
            writeRest(out, s.mRest, s.mRestValid, s.mRestErrors, s.mNeedsInverseBind);
        }

        for (const RestPairStats& p : pairs)
        {
            out += "\n== pair " + p.mFirst + " / " + p.mSecond + " (the first's roles in the second's frames"
                + (p.mRelaxed ? "; measured relaxed" : "") + ")\n";
            writeRest(out, p.mRest, p.mValid, p.mErrors, p.mNeedsInverseBind);
        }
        return out;
    }

    std::string writeSkeletonStatsDetail(const std::vector<SkeletonStats>& skeletons)
    {
        std::string out = "openeden.skelstat.detail 1\n";
        out += "WORK FOLDER ONLY: names the bones of the files read. Never commit.\n";
        for (const SkeletonStats& s : skeletons)
        {
            out += "\n== skeleton " + s.mLabel + "\n";
            out += "name\tparent\tdepth\trole\tin base\tparent offset m\trole ancestor m";
            for (const MassStats& m : s.mBodySets)
            {
                out += "\tshare " + m.mLabel;
            }
            out += "\tsuggestion (tokens)";
            if (s.mHasRoleTable)
            {
                out += "\tsuggestion (pack table)";
            }
            for (const MassStats& m : s.mBodySets)
            {
                out += "\tsuggestion " + m.mLabel;
            }
            out += "\n";
            for (const BoneDetail& d : s.mDetail)
            {
                out += d.mName + "\t" + d.mParent + "\t" + std::to_string(d.mDepth) + "\t"
                    + (d.mRole.empty() ? std::string("-") : d.mRole) + "\t" + (d.mInBase ? "yes" : "no") + "\t"
                    + fixed(d.mParentOffset, 4) + "\t"
                    + (d.mAncestorDistance < 0 ? "-" : fixed(d.mAncestorDistance, 4));
                for (const double share : d.mShares)
                {
                    out += "\t" + fixed(share, 6);
                }
                for (const std::string& suggestion : d.mSuggestions)
                {
                    out += "\t" + suggestion;
                }
                out += "\n";
            }
        }
        return out;
    }

    SkelStatManifestResult parseSkelStatManifest(std::string_view text)
    {
        SkelStatManifestResult result;
        SkelStatManifest manifest;
        std::set<std::string> bases;
        std::set<std::string> skeletons;
        std::map<std::string, std::size_t> bodySets; // label -> index
        std::set<std::pair<std::string, std::string>> roles; // (skeleton, role)
        const auto fail = [&](std::size_t line, const std::string& message) {
            result.mError = "line " + std::to_string(line) + ": " + message;
            return result;
        };
        const auto taken = [&](const std::string& label) {
            return bases.count(label) != 0 || skeletons.count(label) != 0 || bodySets.count(label) != 0;
        };
        std::size_t lineNumber = 0;
        std::size_t start = 0;
        while (start <= text.size())
        {
            const std::size_t end = text.find('\n', start);
            std::string line(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            start = end == std::string_view::npos ? text.size() + 1 : end + 1;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty() || line.front() == '#' || line.find_first_not_of(" \t") == std::string::npos)
            {
                continue;
            }
            const std::vector<std::string> fields = splitTabs(line);
            const std::string& kind = fields[0];
            const auto expectFields = [&](std::size_t n) { return fields.size() == n; };
            for (std::size_t f = 1; f < fields.size(); ++f)
            {
                if (fields[f].empty())
                {
                    return fail(lineNumber, "field " + std::to_string(f + 1) + " is empty");
                }
            }
            if (kind == "base")
            {
                if (!expectFields(3))
                {
                    return fail(lineNumber, "base needs a label and a path");
                }
                if (!validLabel(fields[1]))
                {
                    return fail(lineNumber, "the label must be 1 to 64 characters of A-Z, a-z, 0-9, '.', '_', '-'");
                }
                if (taken(fields[1]))
                {
                    return fail(lineNumber, "the label " + fields[1] + " is used already");
                }
                bases.insert(fields[1]);
                manifest.mBases.push_back({ fields[1], fields[2] });
            }
            else if (kind == "skeleton")
            {
                if (!expectFields(4))
                {
                    return fail(lineNumber, "skeleton needs a label, a base label and a path");
                }
                if (!validLabel(fields[1]))
                {
                    return fail(lineNumber, "the label must be 1 to 64 characters of A-Z, a-z, 0-9, '.', '_', '-'");
                }
                if (taken(fields[1]))
                {
                    return fail(lineNumber, "the label " + fields[1] + " is used already");
                }
                if (bases.count(fields[2]) == 0)
                {
                    return fail(lineNumber, "no base " + fields[2] + " is declared before this line");
                }
                skeletons.insert(fields[1]);
                manifest.mSkeletons.push_back({ fields[1], fields[2], fields[3] });
            }
            else if (kind == "bodyset")
            {
                if (!expectFields(4))
                {
                    return fail(lineNumber, "bodyset needs a label, a skeleton label and a path");
                }
                if (!validLabel(fields[1]))
                {
                    return fail(lineNumber, "the label must be 1 to 64 characters of A-Z, a-z, 0-9, '.', '_', '-'");
                }
                if (skeletons.count(fields[2]) == 0)
                {
                    return fail(lineNumber, "no skeleton " + fields[2] + " is declared before this line");
                }
                if (const auto existing = bodySets.find(fields[1]); existing != bodySets.end())
                {
                    SkelStatManifest::BodySet& set = manifest.mBodySets[existing->second];
                    if (set.mSkeleton != fields[2])
                    {
                        return fail(
                            lineNumber, "the body set " + fields[1] + " belongs to the skeleton " + set.mSkeleton);
                    }
                    set.mPaths.push_back(fields[3]);
                    continue;
                }
                if (taken(fields[1]))
                {
                    return fail(lineNumber, "the label " + fields[1] + " is used already");
                }
                bodySets[fields[1]] = manifest.mBodySets.size();
                manifest.mBodySets.push_back({ fields[1], fields[2], { fields[3] } });
            }
            else if (kind == "compare")
            {
                if (!expectFields(3))
                {
                    return fail(lineNumber, "compare needs two skeleton labels");
                }
                for (std::size_t f = 1; f < 3; ++f)
                {
                    if (skeletons.count(fields[f]) == 0)
                    {
                        return fail(lineNumber, "no skeleton " + fields[f] + " is declared before this line");
                    }
                }
                manifest.mCompares.emplace_back(fields[1], fields[2]);
            }
            else if (kind == "role")
            {
                if (!expectFields(4))
                {
                    return fail(lineNumber, "role needs a skeleton label, a canonical role and a bone name");
                }
                if (skeletons.count(fields[1]) == 0)
                {
                    return fail(lineNumber, "no skeleton " + fields[1] + " is declared before this line");
                }
                if (findRole(fields[2]) == nullptr)
                {
                    return fail(lineNumber, fields[2] + " is not a canonical role");
                }
                if (!roles.insert({ fields[1], fields[2] }).second)
                {
                    return fail(lineNumber, "the role " + fields[2] + " is listed twice for " + fields[1]);
                }
                manifest.mRoles.push_back({ fields[1], fields[2], fields[3] });
            }
            else
            {
                return fail(lineNumber, "unknown kind of line (base, skeleton, bodyset, compare or role)");
            }
        }
        result.mValue = std::move(manifest);
        return result;
    }
}

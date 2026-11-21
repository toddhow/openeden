// Task 0298: the order of quests when their alias packages are tried (Civil War: CW00A's forced greeting before CW's
// sandbox).
#ifndef OPENEDEN_GAME_SKYRIMSE_QUEST_PRIORITY_HPP
#define OPENEDEN_GAME_SKYRIMSE_QUEST_PRIORITY_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    struct PackageRecord;

    /// The order in which the packages that quest aliases give one actor are tried: the quest with the higher priority
    /// first (QUST DATA: CW00A has 71 and CW 70, so the forced greeting of CW00A is ahead of CW's map table sandbox),
    /// equal priorities by quest id so the order never depends on a hash. RECALLED-VERIFY: the data's numbers fit.
    [[nodiscard]] inline bool questPriorityBefore(
        std::uint8_t priorityA, std::uint32_t idA, std::uint8_t priorityB, std::uint32_t idB)
    {
        if (priorityA != priorityB)
            return priorityA > priorityB;
        return idA < idB;
    }

    /// Task 0334: the packages ONE quest's aliases give one placement, as the world view finds them: `mForced` for an
    /// alias a script (or an external alias) filled (the dynamic lists), else an alias filled with the placement
    /// itself.
    struct AliasPackageGroup
    {
        std::uint32_t mQuest = 0;
        std::uint8_t mPriority = 0;
        bool mForced = false;
        std::vector<const PackageRecord*> mPackages;
    };

    /// The packages of every group in the order they are tried: the quest with the higher priority first, whatever kind
    /// of alias gives them (Serana's follow packages come from the specific reference alias of DLC1NPCMentalModel,
    /// priority 80, and must be tried before DLC1VQ02's `SeranaAbandoned`, priority 70, which an external alias gives);
    /// at the same priority a forced alias first (the order before this task), then by quest id. The order inside a
    /// group is kept.
    [[nodiscard]] inline std::vector<const PackageRecord*> orderAliasPackageGroups(
        std::vector<AliasPackageGroup> groups)
    {
        std::stable_sort(groups.begin(), groups.end(), [](const AliasPackageGroup& a, const AliasPackageGroup& b) {
            if (a.mPriority != b.mPriority)
                return a.mPriority > b.mPriority;
            if (a.mForced != b.mForced)
                return a.mForced;
            return a.mQuest < b.mQuest;
        });
        std::vector<const PackageRecord*> out;
        for (const AliasPackageGroup& group : groups)
            out.insert(out.end(), group.mPackages.begin(), group.mPackages.end());
        return out;
    }
    /// Task 0341 (`side quests`): the packages of the groups that an actor's UNIQUE-actor aliases give (`uniqueGroups`:
    /// they stand in front of `list`, `frontCount` packages) take part in the one priority order with the groups of the
    /// other aliases, then the rest of `list` (the actor's own packages) follows. Before, the unique-actor packages
    /// always stood BEHIND a forced or specific-reference alias's: a world interaction quest (WITavern, priority 40)
    /// that took the barmaid Saadia as its server kept her in its sandbox while her own quest MS08 wanted her
    /// elsewhere.
    [[nodiscard]] inline std::vector<const PackageRecord*> orderWithUniqueActorGroups(
        const std::vector<const PackageRecord*>& list, std::size_t frontCount,
        std::vector<AliasPackageGroup> uniqueGroups, std::vector<AliasPackageGroup> otherGroups)
    {
        for (AliasPackageGroup& group : uniqueGroups)
            otherGroups.push_back(std::move(group));
        std::vector<const PackageRecord*> out = orderAliasPackageGroups(std::move(otherGroups));
        if (frontCount < list.size())
            out.insert(out.end(), list.begin() + static_cast<std::ptrdiff_t>(frontCount), list.end());
        return out;
    }
}

#endif

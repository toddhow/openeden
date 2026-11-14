#include "game_skyrimse/actor_mechanics.hpp"

#include "game_skyrimse/appearance.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        namespace sdkgame = openeden::sdk::game;

        // The level the pick for leveled templates uses: the SAME as the appearance's (task 0121), so the numbers
        // belong to the leveled entry that is drawn. The pick therefore ignores the player's level (a limit, see the
        // task log).
        constexpr std::uint16_t kTemplateLevel = kAppearanceLevel;

        const char* resolveMessage(ActorIndex::ResolveStatus status)
        {
            switch (status)
            {
                case ActorIndex::ResolveStatus::UnknownNpc:
                    return "the form is not a known NPC_";
                case ActorIndex::ResolveStatus::Cycle:
                    return "the NPC's template chain leads back to itself";
                case ActorIndex::ResolveStatus::TooDeep:
                    return "the NPC's template chain is too deep";
                case ActorIndex::ResolveStatus::BrokenLink:
                    return "the NPC's template chain names something that is not an NPC_ or a usable leveled list";
                case ActorIndex::ResolveStatus::Ok:
                    break;
            }
            return "";
        }

        sdkgame::ActorRelation reactionOf(std::uint32_t stored)
        {
            switch (stored)
            {
                case CombatReaction::Enemy:
                    return sdkgame::ActorRelation::Enemy;
                case CombatReaction::Ally:
                    return sdkgame::ActorRelation::Ally;
                case CombatReaction::Friend:
                    return sdkgame::ActorRelation::Friend;
                default:
                    return sdkgame::ActorRelation::Neutral;
            }
        }

        // What faction `from` says about faction `to` (the last XNAM naming it wins); Neutral when it says nothing.
        sdkgame::ActorRelation statedRelation(const ActorIndex& index, FormId from, FormId to)
        {
            const FactionRecord* record = index.faction(from);
            if (record == nullptr)
                return sdkgame::ActorRelation::Neutral;
            sdkgame::ActorRelation result = sdkgame::ActorRelation::Neutral;
            for (const FactionRelation& relation : record->mRelations)
            {
                if (relation.mOther == to)
                    result = reactionOf(relation.mReaction);
            }
            return result;
        }

        std::uint8_t clampSkill(std::uint8_t v)
        {
            return v;
        }
    }

    LevelGain levelGainOf(const ClassRecord* classRecord)
    {
        LevelGain gain;
        if (classRecord == nullptr || !classRecord->mHasData)
            return gain;
        const float sum = static_cast<float>(classRecord->mHealthWeight) + classRecord->mMagickaWeight
            + classRecord->mStaminaWeight;
        if (sum <= 0.f)
            return gain;
        gain.mHealth = 5.f + 10.f * classRecord->mHealthWeight / sum;
        gain.mMagicka = 10.f * classRecord->mMagickaWeight / sum;
        gain.mStamina = 10.f * classRecord->mStaminaWeight / sum;
        return gain;
    }

    bool computeActorStats(const ActorIndex& index, FormId npc, std::uint32_t variation, std::uint16_t playerLevel,
        sdkgame::ActorStats& out, std::string& error)
    {
        out = {};
        ActorIndex::Resolved resolved;
        const ActorIndex::ResolveStatus status = index.resolve(npc, kTemplateLevel, variation, resolved);
        if (status != ActorIndex::ResolveStatus::Ok)
        {
            error = resolveMessage(status);
            return false;
        }
        const ActorNpc* own = index.npc(npc);
        const ActorNpc* stats = index.npc(resolved.mStatsFrom);
        if (own == nullptr || stats == nullptr)
        {
            error = "the NPC has no stats record";
            return false;
        }
        const ActorNpc* base = index.npc(resolved.mBaseDataFrom);
        if (base == nullptr)
            base = own;
        const ActorNpc* traits = index.npc(resolved.mTraitsFrom);
        if (traits == nullptr)
            traits = own;

        const bool scales = (stats->mFlags & NpcFlags::PlayerLevelMult) != 0;
        const std::uint32_t floorLevel = std::max<std::uint32_t>(scales ? stats->mCalcMin : stats->mLevel, 1u);
        std::uint32_t level = floorLevel;
        if (scales)
        {
            if (playerLevel == 0)
                playerLevel = kStandInPlayerLevel;
            const double wanted = std::round(static_cast<double>(playerLevel) * stats->mLevel / kLevelMultiplierScale);
            level = static_cast<std::uint32_t>(std::clamp(wanted, 0.0, static_cast<double>(kMaxActorLevel)));
            level = std::max(level, floorLevel);
            if (stats->mCalcMax != 0)
                level = std::min<std::uint32_t>(level, std::max<std::uint32_t>(stats->mCalcMax, floorLevel));
        }
        level = std::min<std::uint32_t>(level, kMaxActorLevel);
        out.level = static_cast<std::uint16_t>(level);
        out.scalesWithPlayer = scales;

        const RaceRecord* race = index.race(resolved.mRace);
        const LevelGain gain = levelGainOf(index.classRecord(resolved.mClass));
        const float extraLevels = static_cast<float>(static_cast<std::int64_t>(level) - floorLevel);
        // The stored totals are signed 16 bit: a race with no magicka and an offset of -25 stores -25 (65511 as an
        // unsigned number: dunTransmogrifyHare, EncChicken). A negative total is 0 (not race start + offset, which is
        // the same negative number); a stored 0 means the record carries none (templates, ghosts): race start + offset.
        const auto attribute = [&](std::uint16_t raw, float raceStart, std::int16_t offset, float perLevel) {
            const std::int16_t stored = static_cast<std::int16_t>(raw);
            const float total = stored < 0 ? 0.f : stored > 0 ? static_cast<float>(stored) : raceStart + offset;
            return std::max(0.f, total + extraLevels * perLevel);
        };
        const float startHealth = race != nullptr ? race->mStartHealth : 0.f;
        const float startMagicka = race != nullptr ? race->mStartMagicka : 0.f;
        const float startStamina = race != nullptr ? race->mStartStamina : 0.f;
        out.health = attribute(stats->mHasStats ? stats->mHealth : 0, startHealth, stats->mHealthOffset, gain.mHealth);
        out.magicka
            = attribute(stats->mHasStats ? stats->mMagicka : 0, startMagicka, stats->mMagickaOffset, gain.mMagicka);
        out.stamina
            = attribute(stats->mHasStats ? stats->mStamina : 0, startStamina, stats->mStaminaOffset, gain.mStamina);
        if (stats->mHasStats)
        {
            for (std::size_t i = 0; i < out.skills.size() && i < stats->mSkills.size(); ++i)
                out.skills[i] = clampSkill(stats->mSkills[i]);
        }
        out.female = traits->female();
        out.essential = (base->mFlags & NpcFlags::Essential) != 0;
        out.unique = (base->mFlags & NpcFlags::Unique) != 0;
        out.protectedActor = (base->mFlags & NpcFlags::Protected) != 0;
        out.invulnerable = (base->mFlags & NpcFlags::Invulnerable) != 0;
        error.clear();
        return true;
    }

    sdkgame::ActorRelation relationOfFactions(
        const ActorIndex& index, const std::vector<FactionMembership>& a, const std::vector<FactionMembership>& b)
    {
        bool enemy = false;
        bool ally = false;
        bool friendly = false;
        const auto note = [&](sdkgame::ActorRelation relation) {
            enemy = enemy || relation == sdkgame::ActorRelation::Enemy;
            ally = ally || relation == sdkgame::ActorRelation::Ally;
            friendly = friendly || relation == sdkgame::ActorRelation::Friend;
        };
        for (const FactionMembership& fa : a)
        {
            if (fa.mFaction == kNullFormId || fa.mFaction == kUnresolvedFormId)
                continue;
            for (const FactionMembership& fb : b)
            {
                if (fb.mFaction == kNullFormId || fb.mFaction == kUnresolvedFormId)
                    continue;
                if (fa.mFaction == fb.mFaction)
                {
                    ally = true; // the members of one faction stand together
                    continue;
                }
                note(statedRelation(index, fa.mFaction, fb.mFaction));
                note(statedRelation(index, fb.mFaction, fa.mFaction));
            }
        }
        if (enemy)
            return sdkgame::ActorRelation::Enemy;
        if (ally)
            return sdkgame::ActorRelation::Ally;
        if (friendly)
            return sdkgame::ActorRelation::Friend;
        return sdkgame::ActorRelation::Neutral;
    }

    bool wouldAttackOnSight(sdkgame::ActorAggression aggression, sdkgame::ActorRelation relation)
    {
        switch (aggression)
        {
            case sdkgame::ActorAggression::Aggressive:
                return relation == sdkgame::ActorRelation::Enemy;
            case sdkgame::ActorAggression::VeryAggressive:
                return relation == sdkgame::ActorRelation::Enemy || relation == sdkgame::ActorRelation::Neutral;
            case sdkgame::ActorAggression::Frenzied:
                return true;
            case sdkgame::ActorAggression::Unaggressive:
                break;
        }
        return false;
    }

    bool computeActorDisposition(const ActorIndex& index, FormId npc, std::uint32_t variation,
        const MechanicsOptions& options, sdkgame::ActorDisposition& out, std::string& error)
    {
        out = {};
        ActorIndex::Resolved resolved;
        const ActorIndex::ResolveStatus status = index.resolve(npc, kTemplateLevel, variation, resolved);
        if (status != ActorIndex::ResolveStatus::Ok)
        {
            error = resolveMessage(status);
            return false;
        }
        std::vector<FactionMembership> player;
        player.reserve(options.mPlayerFactions.size());
        for (FormId faction : options.mPlayerFactions)
            player.push_back({ faction, 0 });
        out.relationToPlayer = relationOfFactions(index, resolved.mFactions, player);
        if (const ActorNpc* aiOwner = index.npc(resolved.mAiFrom))
        {
            if (aiOwner->mAi)
                out.aggression
                    = static_cast<sdkgame::ActorAggression>(std::min<std::uint8_t>(aiOwner->mAi->mAggression, 3));
        }
        out.hostileOnSight = wouldAttackOnSight(out.aggression, out.relationToPlayer);
        error.clear();
        return true;
    }

    sdkgame::ActorRelation relationBetweenActors(
        const ActorIndex& index, FormId a, std::uint32_t variationA, FormId b, std::uint32_t variationB)
    {
        ActorIndex::Resolved ra;
        ActorIndex::Resolved rb;
        if (index.resolve(a, kTemplateLevel, variationA, ra) != ActorIndex::ResolveStatus::Ok
            || index.resolve(b, kTemplateLevel, variationB, rb) != ActorIndex::ResolveStatus::Ok)
            return sdkgame::ActorRelation::Neutral;
        return relationOfFactions(index, ra.mFactions, rb.mFactions);
    }
}

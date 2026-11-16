#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cmath>
#include <string>

// Task 0241: the numbers a held ability or a timed shrine blessing gives the player (ability_numbers.hpp): the three
// maximums, the regeneration rates (combat_regen.cpp reads the sums), the resistances and weaknesses (resistOf and
// weaknessOf read them), the carry weight (the pack gives it to the inventory), and an Invisibility effect. Player
// only: an NPC's abilities keep the old rules. Nothing here allocates per frame: applyAbilityNumbers runs when a spell list
// changes, flushAbilityVitals sends at most one report when a Fortify Health starts or ends.
namespace game_skyrimse
{
    namespace
    {
        std::string num(float v)
        {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(v));
            return buffer;
        }
    }

    void SkyrimCombat::setPlayerExtraPerks(std::vector<FormId> perks)
    {
        std::sort(perks.begin(), perks.end());
        perks.erase(std::unique(perks.begin(), perks.end()), perks.end());
        if (perks == mExtraPerks)
            return;
        mExtraPerks = std::move(perks);
        if (mTrace)
            trace("COMBAT player extra perks: " + std::to_string(mExtraPerks.size()));
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || !player->mHasProfile)
            return; // preparePerks adds them when the profile is made
        FighterProfile& profile = player->mProfile;
        profile.mAbilityDamageResist = 0.f;
        profile.mHasWeaponSpeedAbility = false;
        profile.mAbilityWeaponSpeed = 1.f;
        preparePerks(*player);
    }

    AbilityNumbers SkyrimCombat::playerAbilityNumbers() const
    {
        const Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        return player != nullptr && player->mHasAbilities ? player->mAbilities : AbilityNumbers();
    }

    float SkyrimCombat::playerResistPercent(std::int32_t resistValue)
    {
        const Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        return player != nullptr ? resistOf(*player, resistValue) : 0.f;
    }

    float SkyrimCombat::playerWeaknessPercent(std::int32_t resistValue)
    {
        const Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        return player != nullptr ? weaknessOf(*player, resistValue) : 0.f;
    }

    bool SkyrimCombat::playerInvisible() const
    {
        const Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        return player != nullptr && statusActive(player->mStatus.mInvisibleUntil, mClock);
    }

    // Called by preparePerks once the player's spell and perk lists are worked out (also when they are empty: the
    // numbers of a spell that is gone must go).
    void SkyrimCombat::applyAbilityNumbers(Fighter& fighter, const PerkLibrary& library)
    {
        if (!fighter.mSubject.player || !fighter.mHasProfile)
            return;
        FighterProfile& profile = fighter.mProfile;
        std::vector<FormId> held = profile.mSpells;
        collectPerkAbilities(library, profile.mPerks, held);
        const AbilityNumbers numbers = sumAbilityNumbers(library, held);
        const bool changed = !(numbers == fighter.mAbilities);
        fighter.mAbilities = numbers;
        fighter.mHasAbilities = numbers.any();
        if (changed)
        {
            ++mAbilityRevision;
            if (mTrace)
                trace("COMBAT player ability numbers: vitals " + num(numbers.mVital[0]) + " " + num(numbers.mVital[1])
                    + " " + num(numbers.mVital[2]) + " carry " + num(numbers.mCarry) + " absorb " + num(numbers.mAbsorb)
                    + " resist " + num(numbers.mResist[0]) + " " + num(numbers.mResist[1]) + " "
                    + num(numbers.mResist[2]) + " " + num(numbers.mResist[3]) + " " + num(numbers.mResist[4]) + " "
                    + num(numbers.mResist[5]) + " regen rate " + num(numbers.mRegenRate[0]) + " "
                    + num(numbers.mRegenRate[1]) + " " + num(numbers.mRegenRate[2]) + " mult "
                    + num(numbers.mRegenMult[0]) + " " + num(numbers.mRegenMult[1]) + " " + num(numbers.mRegenMult[2]));
        }
        // The Fortify share of the three maximums: a gain also raises the current value by the same amount (the game's
        // modifier is added to both), a loss lowers it; health is the engine's, so its change is reported later.
        float* maximums[3] = { &profile.mHealthMax, &profile.mMagickaMax, &profile.mStaminaMax };
        float* currents[3] = { &fighter.mHealth, &fighter.mMagicka, &fighter.mStamina };
        for (int i = 0; i < 3; ++i)
        {
            const float delta = numbers.mVital[i] - mAppliedVital[i];
            if (delta == 0.f)
                continue;
            *maximums[i] = std::max(0.f, *maximums[i] + delta);
            *currents[i] = std::clamp(*currents[i] + delta, 0.f, *maximums[i]);
            mAppliedVital[i] = numbers.mVital[i];
            if (i == 0)
            {
                mPendingHealthDelta += delta;
                mVitalReportPending = true;
            }
        }
    }

    // The engine's health maximum follows a Fortify Health: the same zero-damage report as a level up's gain.
    void SkyrimCombat::flushAbilityVitals(openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (!mVitalReportPending)
            return;
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || !player->mReady || !player->mHasProfile || player->mDead)
        {
            if (player != nullptr && player->mDead)
            {
                mVitalReportPending = false;
                mPendingHealthDelta = 0.f;
            }
            return;
        }
        openeden::sdk::game::SubjectState state;
        if (!writer.subjectState(openeden::sdk::game::CombatSubject::ofPlayer(), state) || !(state.healthMax > 0.f))
            return; // the engine has no health for the player yet: keep what is owed
        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = true;
        report.victimIsPlayer = true;
        report.amount = 0.f;
        report.healthMax = player->mProfile.mHealthMax;
        report.healthAfter
            = std::clamp(state.health + mPendingHealthDelta, std::min(1.f, report.healthMax), report.healthMax);
        report.x = state.x;
        report.y = state.y;
        report.z = state.z;
        if (writer.reportDamage(report))
        {
            player->mHealth = report.healthAfter;
            mVitalReportPending = false;
            mPendingHealthDelta = 0.f;
        }
    }
}

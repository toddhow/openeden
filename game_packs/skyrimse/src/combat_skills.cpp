#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cmath>

// The combat's side of the player's skills (task 0200): what a fight reports to the progression, and the progression's
// numbers in the player's fighter profile. Everything here is inert until a hook or a progression is set.

namespace game_skyrimse
{
    namespace
    {
        constexpr std::uint32_t kArmourTypeLight = 0;
        constexpr std::uint32_t kArmourTypeHeavy = 1;
    }

    void SkyrimCombat::setPlayerProgression(const progression::PlayerProgression* progression)
    {
        mProgression = progression;
        mProgressionSynced = 0;
    }

    void SkyrimCombat::reportSkillUse(std::size_t slot, float amount)
    {
        if (mSkillHook && slot < progression::kSkillCount && std::isfinite(amount) && amount > 0.f)
            mSkillHook(slot, amount);
    }

    // A blow landed. The player's weapon skill gets the damage it dealt; a player who is hit trains Block with what
    // the block took off and the armour skill of what is worn (heavy when any main piece is heavy, else light) with
    // the damage taken. RECALLED-VERIFY amounts (task 0200).
    void SkyrimCombat::reportHitSkills(const Fighter& attacker, const Fighter& victim, const WeaponRecord* weapon,
        float blow, float taken, bool blocked)
    {
        if (attacker.mSubject.player && weapon != nullptr && weapon->skillSlot() < progression::kSkillCount)
            reportSkillUse(weapon->skillSlot(), taken);
        if (!victim.mSubject.player)
            return;
        if (blocked)
            reportSkillUse(progression::Slot::Block, blow - taken);
        bool heavy = false;
        bool light = false;
        for (const ArmourPiece& piece : victim.mProfile.mPieces)
        {
            if ((piece.mSlotMask & ArmourSlot::MainPieces) == 0)
                continue;
            heavy = heavy || piece.mType == kArmourTypeHeavy;
            light = light || piece.mType == kArmourTypeLight;
        }
        if (heavy || light)
            reportSkillUse(heavy ? progression::Slot::HeavyArmor : progression::Slot::LightArmor, taken);
    }

    // A spell was cast or a held spell paid for a step: the school of its first effect with one gets the magicka.
    void SkyrimCombat::reportSpellUse(const Fighter& caster, const SpellRecord& spell, float magicka)
    {
        if (!caster.mSubject.player || !mSkillHook || mData == nullptr)
            return;
        for (const SpellEffect& effect : spell.mEffects)
        {
            const MagicEffectRecord* record = mData->effectById(effect.mEffect);
            if (record == nullptr)
                continue;
            const std::size_t slot = progression::slotOfMagicSkillValue(record->mAssociatedSkill);
            if (slot < progression::kSkillCount)
            {
                reportSkillUse(slot, magicka);
                return;
            }
        }
    }

    // The progression's numbers become the player's profile: skills, level, the three maximums (a gain also fills the
    // current stamina and magicka, and the engine's health through a report that takes none off) and the perks bought.
    void SkyrimCombat::syncProgression(openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (mProgression == nullptr || mProgression->revision() == mProgressionSynced)
            return;
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || !ready(*player) || !player->mHasProfile)
            return;
        if (!mHaveStandProfile)
        {
            mStandProfile = player->mProfile;
            mHaveStandProfile = true;
        }
        const progression::PlayerProgression& p = *mProgression;
        mProgressionSynced = p.revision();
        FighterProfile& profile = player->mProfile;
        const float oldHealthMax = profile.mHealthMax;
        const float oldStaminaMax = profile.mStaminaMax;
        const float oldMagickaMax = profile.mMagickaMax;
        for (std::size_t s = 0; s < progression::kSkillCount; ++s)
            profile.mSkills[s] = p.skill(s);
        profile.mLevel = p.level();
        profile.mHealthMax = mStandProfile.mHealthMax + p.vitalBonus(progression::Vital::Health);
        profile.mMagickaMax = mStandProfile.mMagickaMax + p.vitalBonus(progression::Vital::Magicka);
        profile.mStaminaMax = mStandProfile.mStaminaMax + p.vitalBonus(progression::Vital::Stamina);
        player->mStamina
            = std::min(profile.mStaminaMax, player->mStamina + std::max(0.f, profile.mStaminaMax - oldStaminaMax));
        player->mMagicka
            = std::min(profile.mMagickaMax, player->mMagicka + std::max(0.f, profile.mMagickaMax - oldMagickaMax));
        // perks: the stand-in's own, the bought ones, the test settings' (preparePerks adds them and sorts)
        profile.mPerks = mStandProfile.mPerks;
        const std::vector<FormId> bought = p.ownedPerks();
        profile.mPerks.insert(profile.mPerks.end(), bought.begin(), bought.end());
        // preparePerks sums the abilities of the perks and spells again from nothing (the player's base profile has no
        // ability of its own): leaving the first call's sums would count them twice
        profile.mAbilityDamageResist = 0.f;
        profile.mHasWeaponSpeedAbility = false;
        profile.mAbilityWeaponSpeed = 1.f;
        preparePerks(*player);
        applyPlayerEquipment(); // armour scales with the armour skills
        mValueModsApplied = {}; // task 0240: the maxima above were written afresh, so nothing is applied on them
        mValueModsDirty = true;
        applyPlayerValueModifiers();
        const float gained = profile.mHealthMax - oldHealthMax;
        if (gained > 0.f)
        {
            openeden::sdk::game::SubjectState state;
            if (writer.subjectState(openeden::sdk::game::CombatSubject::ofPlayer(), state) && state.healthMax > 0.f)
            {
                openeden::sdk::game::DamageReport report;
                report.attackerIsPlayer = true;
                report.victimIsPlayer = true;
                report.amount = 0.f;
                report.healthMax = std::max(state.healthMax, profile.mHealthMax);
                report.healthAfter = std::min(report.healthMax, state.health + gained);
                report.x = state.x;
                report.y = state.y;
                report.z = state.z;
                if (writer.reportDamage(report))
                    player->mHealth = report.healthAfter;
            }
        }
        if (mTrace)
            trace("COMBAT progression synced: level " + std::to_string(p.level()) + " health max "
                + std::to_string(static_cast<int>(profile.mHealthMax)) + " magicka max "
                + std::to_string(static_cast<int>(profile.mMagickaMax)) + " stamina max "
                + std::to_string(static_cast<int>(profile.mStaminaMax)) + " perks "
                + std::to_string(profile.mPerks.size()));
    }
}

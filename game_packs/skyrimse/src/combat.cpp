#include <game_skyrimse/combat.hpp>

#include <game_skyrimse/combat_crowd.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game_skyrimse
{
    float fighterBowDrawSpeed(const FighterProfile& profile, float weaponSpeed, float testBonus)
    {
        const float speed = bowDrawSpeed(weaponSpeed, testBonus);
        if (!profile.mHasWeaponSpeedAbility || !std::isfinite(profile.mAbilityWeaponSpeed))
            return speed;
        return std::clamp(speed * profile.mAbilityWeaponSpeed, 0.1f, 10.f);
    }

    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::ClipSlot;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::SweepHit;
        using openeden::sdk::game::SweepTarget;

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        }

        // Clip names of Skyrim - Animations.bsa, meshes/actors/character/animations (read from the listing; events in
        // animationdata/defaultmale.txt: AttackRight has preHitFrame 0.233, weaponSwing 0.333, HitFrame 0.367,
        // attackStop 1.287 playback seconds). Case as in the sidecar's generator names, the file system of the VFS
        // ignores case.
        constexpr SlotTable::Entry sHumanoid[] = {
            { ClipSlot::Attack, kWeaponAnim1HSword, "1HM_AttackRight" },
            { ClipSlot::AttackLeft, kWeaponAnim1HSword, "1HM_AttackLeft" },
            { ClipSlot::PowerAttack, kWeaponAnim1HSword, "1HM_AttackPower" },
            { ClipSlot::BlockHit, kWeaponAnim1HSword, "1HM_BlockHit" },
            { ClipSlot::Bash, kWeaponAnim1HSword, "1HM_BlockBash" },
            { ClipSlot::Recoil, kWeaponAnim1HSword, "1HM_RecoilRight" },
            { ClipSlot::Stagger, kWeaponAnim1HSword, "1HM_StaggerBackMedium" },
        };

        std::string fmt(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(value));
            return buffer;
        }

        std::string name(const CombatSubject& subject)
        {
            if (subject.player)
                return "player";
            return "actor" + std::to_string(subject.actor.reference) + "/" + std::to_string(subject.actor.session);
        }
    }

    // Task 0212: the wolf's clips, from the profile (creatures.hpp) so the names have one source.
    SlotTable SlotTable::wolf()
    {
        static const Entry sWolfEntries[] = {
            { ClipSlot::Attack, kWeaponAnimHandToHand, wolfProfile().mAttack },
            { ClipSlot::PowerAttack, kWeaponAnimHandToHand, wolfProfile().mPower },
            { ClipSlot::Recoil, kWeaponAnimHandToHand, wolfProfile().mRecoil },
            { ClipSlot::Stagger, kWeaponAnimHandToHand, wolfProfile().mStagger },
            { ClipSlot::Death, kWeaponAnimHandToHand, wolfProfile().mDeath },
        };
        SlotTable table;
        table.mEntries = sWolfEntries;
        table.mCount = std::size(sWolfEntries);
        return table;
    }

    std::string_view SkyrimCombat::clipOf(const Fighter& fighter, ClipSlot slot) const
    {
        if (fighter.mProfile.mCreature != nullptr)
            return creatureClipFor(*fighter.mProfile.mCreature, slot); // task 0216: the row of its own creature
        return mSlots.clipFor(slot, kWeaponAnim1HSword);
    }

    SlotTable SlotTable::humanoid()
    {
        SlotTable table;
        table.mEntries = sHumanoid;
        table.mCount = std::size(sHumanoid);
        return table;
    }

    // Task 0178 (stage S5-6): the bow's clips. Names from the listing of Skyrim - Animations.bsa (`bow_drawlight`,
    // `bow_release`); events from animationdata/defaultmale.txt: Bow_DrawLight has bowDrawStart 0.8 and bowDrawn
    // 1.667 playback seconds, Bow_Release has arrowRelease 0.033.
    namespace
    {
        constexpr SlotTable::Entry sBow[] = {
            { ClipSlot::BowDraw, kWeaponAnimBow, "Bow_DrawLight" },
            { ClipSlot::BowRelease, kWeaponAnimBow, "Bow_Release" },
        };
    }

    // Task 0214: the melee clips of every weapon class. Names from the listing of Skyrim - Animations.bsa; the sidecar
    // generators are `2HM_*`, `2HW_*NPC` (attack right and left), `shd_BlockHit` and `Shd_Bash` (see the task document
    // and SkyrimClipMeta).
    namespace
    {
        constexpr SlotTable::Entry sGear[] = {
            { ClipSlot::Attack, kClipClassOneHanded, "1HM_AttackRight" },
            { ClipSlot::AttackLeft, kClipClassOneHanded, "1HM_AttackLeft" },
            { ClipSlot::PowerAttack, kClipClassOneHanded, "1HM_AttackPower" },
            { ClipSlot::BlockHit, kClipClassOneHanded, "1HM_BlockHit" },
            { ClipSlot::Bash, kClipClassOneHanded, "1HM_BlockBash" },
            { ClipSlot::Recoil, kClipClassOneHanded, "1HM_RecoilRight" },
            { ClipSlot::Stagger, kClipClassOneHanded, "1HM_StaggerBackMedium" },
            { ClipSlot::BlockStart, kClipClassOneHanded, "1HM_BlockIdle" },
            { ClipSlot::Attack, kClipClassTwoHandedSword, "2HM_AttackRight" },
            { ClipSlot::AttackLeft, kClipClassTwoHandedSword, "2HM_AttackLeft" },
            { ClipSlot::PowerAttack, kClipClassTwoHandedSword, "2HM_AttackPower" },
            { ClipSlot::BlockHit, kClipClassTwoHandedSword, "2HM_BlockHit" },
            { ClipSlot::Bash, kClipClassTwoHandedSword, "2HM_BlockBash" },
            { ClipSlot::Recoil, kClipClassTwoHandedSword, "2HM_RecoilRight" },
            { ClipSlot::Stagger, kClipClassTwoHandedSword, "2HM_StaggerBackMedium" },
            { ClipSlot::BlockStart, kClipClassTwoHandedSword, "2HM_BlockIdle" },
            { ClipSlot::Attack, kClipClassTwoHandedAxe, "2HW_AttackRight" },
            { ClipSlot::AttackLeft, kClipClassTwoHandedAxe, "2HW_AttackLeft" },
            { ClipSlot::PowerAttack, kClipClassTwoHandedAxe, "2HW_AttackPower" },
            { ClipSlot::BlockHit, kClipClassTwoHandedAxe, "2HW_BlockHit" },
            { ClipSlot::Bash, kClipClassTwoHandedAxe, "2HW_BlockBash" },
            { ClipSlot::Recoil, kClipClassTwoHandedAxe, "2HW_RecoilRight" },
            { ClipSlot::Stagger, kClipClassTwoHandedAxe, "2HW_StaggerBackMedium" },
            { ClipSlot::BlockStart, kClipClassTwoHandedAxe, "2HW_BlockIdle" },
            { ClipSlot::BlockHit, kClipClassShield, "SHD_BlockHit" },
            { ClipSlot::Bash, kClipClassShield, "SHD_BlockBash" },
            { ClipSlot::BlockStart, kClipClassShield, "SHD_BlockIdle" },
        };
    }

    SlotTable SlotTable::gear()
    {
        SlotTable table;
        table.mEntries = sGear;
        table.mCount = std::size(sGear);
        return table;
    }

    const WeaponRecord* SkyrimCombat::perkWeaponFor(const Fighter& fighter) const
    {
        return mRealGear ? weaponFor(fighter) : mWeapon;
    }

    std::uint8_t SkyrimCombat::clipClassOf(const Fighter& fighter) const
    {
        if (!mRealGear)
            return kWeaponAnim1HSword;
        const WeaponRecord* weapon = weaponFor(fighter);
        return weapon != nullptr ? meleeClipClass(weapon->mAnimType) : kClipClassOneHanded;
    }

    bool SkyrimCombat::hasShield(const Fighter& fighter) const
    {
        if (!mRealGear)
            return false;
        const WeaponRecord* weapon = weaponFor(fighter);
        if (weapon != nullptr && isTwoHanded(weapon->mAnimType))
            return false;
        if (fighter.mSubject.player)
        {
            for (const ArmourPiece& piece : mEquip.mPieces)
                if (isShield(piece))
                    return true;
            return false;
        }
        return fighter.mGear.mKnown && fighter.mGear.mHasShield;
    }

    std::string_view SkyrimCombat::meleeClip(ClipSlot slot, const Fighter& fighter, bool shieldSlot) const
    {
        if (fighter.mProfile.mCreature != nullptr)
            return clipOf(fighter, slot);
        if (!mRealGear)
            return mSlots.clipFor(slot, kWeaponAnim1HSword);
        const std::uint8_t cls = shieldSlot && hasShield(fighter) ? kClipClassShield : clipClassOf(fighter);
        return mGearSlots.clipFor(slot, cls);
    }

    float SkyrimCombat::shieldBlockBonus(const Fighter& fighter) const
    {
        if (!hasShield(fighter))
            return 0.f;
        float rating = 0.f;
        if (fighter.mSubject.player)
        {
            for (const ArmourPiece& piece : mEquip.mPieces)
                if (isShield(piece))
                    rating = std::max(rating, piece.mRating);
        }
        else
            rating = fighter.mGear.mShield.mRating;
        return std::isfinite(rating) && rating > 0.f ? rating * kShieldBlockPerRating : 0.f;
    }

    // Looked up once, when the fighter is first used. An NPC with a shield or inventory armour has them counted in
    // its mitigation from then on.
    void SkyrimCombat::applyRealGear(Fighter& fighter)
    {
        if (!mRealGear || fighter.mSubject.player || mData == nullptr)
            return;
        FighterGear gear;
        if (fighter.mHasProfile && mData->fighterGear(fighter.mBase, fighter.mProfile.mPieces, gear))
            fighter.mGear = std::move(gear);
        fighter.mGear.mKnown = true;
        std::vector<ArmourPiece> pieces = fighter.mProfile.mPieces;
        const std::size_t before = pieces.size();
        pieces.insert(pieces.end(), fighter.mGear.mExtraPieces.begin(), fighter.mGear.mExtraPieces.end());
        if (hasShield(fighter))
            pieces.push_back(fighter.mGear.mShield);
        if (pieces.size() != before)
        {
            fighter.mProfile.mPieces = pieces;
            fighter.mProfile.mMitigationPercent
                = armourOf(pieces, fighter.mProfile.mSkills, mCombatSettings).mMitigationPercent;
        }
        if (mTrace)
        {
            const WeaponRecord* weapon = weaponFor(fighter);
            trace("COMBAT gear " + name(fighter.mSubject) + " weapon "
                + (weapon != nullptr ? weapon->mCommon.mEditorId + " type " + std::to_string(weapon->mAnimType)
                                     : std::string("none"))
                + " class " + std::to_string(clipClassOf(fighter)) + " shield " + (hasShield(fighter) ? "yes" : "no")
                + " extra armour " + std::to_string(fighter.mGear.mExtraPieces.size()) + " mitigation "
                + fmt(fighter.mProfile.mMitigationPercent));
        }
    }

    SlotTable SlotTable::bow()
    {
        SlotTable table;
        table.mEntries = sBow;
        table.mCount = std::size(sBow);
        return table;
    }

    bool SkyrimCombat::setTestAmmo(std::string_view editorId)
    {
        mAmmoLooked = false;
        mAmmo = nullptr;
        mProjectile = nullptr;
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestAmmo.clear();
            return false;
        }
        mTestAmmo = lowerAscii(editorId);
        return true;
    }

    void SkyrimCombat::setTestBowSpeedBonus(float bonus)
    {
        mBowSpeedBonus = std::isfinite(bonus) ? std::max(bonus, -0.9f) : 0.f;
    }

    void SkyrimCombat::setTestPlayerBow(float holdSeconds)
    {
        mPlayerBowHold = (std::isfinite(holdSeconds) && holdSeconds > 0.f) ? holdSeconds : 0.f;
    }

    void SkyrimCombat::onProjectileResult(const openeden::sdk::game::ProjectileResult& result)
    {
        if (mProjectileResults.size() < mProjectileResults.capacity())
            mProjectileResults.push_back(result);
    }

    void SkyrimCombat::onPlayerAttackEdge(bool pressed)
    {
        if (mPlayerEdges.size() < mPlayerEdges.capacity())
            mPlayerEdges.push_back(pressed);
        if (!mTestPlayerSpell.empty() && mCastEdges.size() < mCastEdges.capacity())
            mCastEdges.push_back(pressed); // task 0186: the attack control casts
    }

    SkyrimCombat::SkyrimCombat()
        : mSlots(SlotTable::humanoid())
    {
        mFighters.reserve(256);
        mBowSlots = SlotTable::bow();
        mWolfSlots = SlotTable::wolf();
        mGearSlots = SlotTable::gear();
        mMagicSlots = SlotTable::magic();
        mCasts.reserve(kMaxCasts);
        mConcentrations.reserve(kMaxConcentrations);
        mSpellShots.reserve(kMaxSpellShots);
        mCastEdges.reserve(8);
        mTestCasters.reserve(kMaxTestCasters);
        mActorNames.reserve(256);
        mPendingBow.reserve(kMaxConcurrentAttacks);
        mShots.reserve(kMaxShotsRemembered);
        mProjectileResults.reserve(kMaxShotsRemembered);
        mPlayerEdges.reserve(8);
        mPendingHits.reserve(kMaxConcurrentAttacks);
        mAwaiting.reserve(kMaxConcurrentAttacks);
        mDeliveries.reserve(kMaxConcurrentAttacks);
        Fighter player;
        player.mSubject = CombatSubject::ofPlayer();
        mFighters.push_back(player);
    }

    void SkyrimCombat::setData(CombatData* data)
    {
        mData = data;
        mHaveSettings = false;
        mWeaponLooked = false;
        mWeapon = nullptr;
        mAmmoLooked = false;
        mAmmo = nullptr;
        mProjectile = nullptr;
        mPlayerDrawTime = 0.f;
        mHaveMagicSettings = false;
        mPlayerSpell = nullptr;
        mPlayerSpellLooked = false;
        for (TestCaster& tc : mTestCasters)
        {
            tc.mSpell = nullptr;
            tc.mLooked = false;
        }
        mHaveTiming[0] = mHaveTiming[1] = false;
        for (Fighter& f : mFighters)
            f.mReady = false;
    }

    bool SkyrimCombat::setTestSwing(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestSwing.clear();
            return false;
        }
        mTestSwing = lowerAscii(editorId);
        return true;
    }

    bool SkyrimCombat::setTestHostile(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestHostile.clear();
            return false;
        }
        mTestHostile = lowerAscii(editorId);
        return true;
    }

    void SkyrimCombat::setTestCrowd(int hostile, int duels, float delaySeconds)
    {
        mTestCrowd = hostile > 0 ? static_cast<std::size_t>(hostile) : 0;
        mTestDuels = duels > 0 ? static_cast<std::size_t>(duels) : 0;
        mCrowdDelay = (std::isfinite(delaySeconds) && delaySeconds >= 0.f) ? delaySeconds : kDefaultCrowdDelay;
        mCrowdChosen = false;
        mOpponents.clear();
        mOpponents.reserve(2 * std::min<std::size_t>(mTestDuels, Crowd::kMaxPicked / 2));
    }

    bool SkyrimCombat::setTestWeapon(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestWeapon.clear();
            return false;
        }
        mTestWeapon = lowerAscii(editorId);
        mWeaponLooked = false;
        mWeapon = nullptr;
        return true;
    }

    // ---- task 0225: the abilities of the race the player chose
    void SkyrimCombat::setPlayerRaceAbilities(std::vector<FormId> chosen, std::vector<FormId> standIn)
    {
        mRaceAbilities = std::move(chosen);
        mStandRaceAbilities = std::move(standIn);
        mRaceAbilitiesSet = true;
        if (mTrace)
            trace("COMBAT player race abilities: " + std::to_string(mRaceAbilities.size()) + " chosen, "
                + std::to_string(mStandRaceAbilities.size()) + " of the stand-in's race left out");
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || !player->mHasProfile)
            return; // the profile is not made yet: preparePerks applies the abilities when it is
        // preparePerks sums the abilities again from nothing (the player's base profile has no ability of its own)
        FighterProfile& profile = player->mProfile;
        profile.mAbilityDamageResist = 0.f;
        profile.mHasWeaponSpeedAbility = false;
        profile.mAbilityWeaponSpeed = 1.f;
        preparePerks(*player);
    }

    // ---- task 0237: stones and blessings
    void SkyrimCombat::setPlayerExtraAbilities(std::vector<FormId> spells)
    {
        if (spells == mExtraAbilities)
            return;
        mExtraAbilities = std::move(spells);
        if (mTrace)
            trace("COMBAT player extra abilities: " + std::to_string(mExtraAbilities.size()));
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || !player->mHasProfile)
            return; // preparePerks applies them when the profile is made
        FighterProfile& profile = player->mProfile;
        profile.mAbilityDamageResist = 0.f;
        profile.mHasWeaponSpeedAbility = false;
        profile.mAbilityWeaponSpeed = 1.f;
        preparePerks(*player);
    }

    // ---- task 0184: what the player wields and wears, and a potion's health
    void SkyrimCombat::setPlayerEquipment(PlayerEquipment equipment)
    {
        mEquip = std::move(equipment);
        mEquipDirty = true;
        applyPlayerEquipment();
    }

    bool SkyrimCombat::queuePlayerHeal(float amount)
    {
        if (mData == nullptr || !std::isfinite(amount) || amount <= 0.f)
            return false;
        mPendingHeal += std::min(amount, kMaxPlayerHeal);
        return true;
    }

    float SkyrimCombat::playerMitigationPercent() const
    {
        const Fighter* player = find(CombatSubject::ofPlayer());
        return player != nullptr && player->mReady && player->mHasProfile ? player->mProfile.mMitigationPercent : 0.f;
    }

    // The player's armour: the pieces equipped (armourOf with the stand-in's skills), else the stand-in's own number.
    // Applied when the player's data is read and whenever the equipment changes.
    void SkyrimCombat::applyPlayerEquipment()
    {
        Fighter* player = find(CombatSubject::ofPlayer());
        if (player == nullptr || !player->mReady || !player->mHasProfile)
            return; // ready() applies it when the profile is fetched
        mEquipDirty = false;
        if (mEquip.mPieces.empty())
        {
            player->mProfile.mMitigationPercent = mBaseMitigation;
            player->mProfile.mPieces = mBasePieces;
            return;
        }
        if (!mHaveSettings)
            loadSettings();
        const ArmourResult armour = armourOf(mEquip.mPieces, player->mProfile.mSkills, mCombatSettings);
        player->mProfile.mMitigationPercent = armour.mMitigationPercent;
        player->mProfile.mPieces = mEquip.mPieces;
        trace("COMBAT player armour " + std::to_string(mEquip.mPieces.size()) + " pieces: rating " + fmt(armour.mRating)
            + " takes " + fmt(armour.mMitigationPercent) + "% off");
    }

    // Task 0214: the player's swing timing is per clip class; a change of weapon class forgets the old numbers.
    void SkyrimCombat::syncPlayerTimingClass(const Fighter& player)
    {
        const std::uint8_t cls = clipClassOf(player);
        if (cls == mTimingClass)
            return;
        mTimingClass = cls;
        mHaveTiming[0] = mHaveTiming[1] = false;
        mPlayerTimingKicked = false;
    }

    // The weapon a fighter swings: the player's equipped melee weapon, else the test weapon (as before).
    const WeaponRecord* SkyrimCombat::weaponFor(const Fighter& fighter) const
    {
        if (fighter.mSubject.player && mEquip.mWeapon != nullptr && mEquip.mWeapon->isMelee())
            return mEquip.mWeapon;
        // Task 0214: an NPC's own weapon, unless the test weapon overrides it.
        if (mRealGear && mWeapon == nullptr && !fighter.mSubject.player && fighter.mGear.mKnown)
            return fighter.mGear.mWeapon;
        return mWeapon;
    }

    // A queued heal reaches the engine's health: a report that takes none off and sets the health after it. It waits
    // while the engine has no health for the player.
    void SkyrimCombat::applyPlayerHeal(openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (!(mPendingHeal > 0.f))
            return;
        openeden::sdk::game::SubjectState state;
        if (!writer.subjectState(CombatSubject::ofPlayer(), state) || !(state.healthMax > 0.f))
            return;
        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = true;
        report.victimIsPlayer = true;
        report.amount = 0.f;
        report.healthMax = state.healthMax;
        report.healthAfter = std::clamp(state.health + mPendingHeal, 0.f, state.healthMax);
        report.x = state.x;
        report.y = state.y;
        report.z = state.z;
        if (writer.reportDamage(report))
        {
            if (Fighter* player = find(CombatSubject::ofPlayer()))
                player->mHealth = report.healthAfter;
            trace("COMBAT heal player +" + fmt(mPendingHeal) + " health " + fmt(state.health) + " -> "
                + fmt(report.healthAfter) + " of " + fmt(state.healthMax));
        }
        mPendingHeal = 0.f; // applied, or refused for good by the engine
    }

    void SkyrimCombat::setTestPowerEvery(int n)
    {
        mPowerEvery = n > 0 ? n : 0;
    }

    void SkyrimCombat::setTestBashEvery(int n)
    {
        mBashEvery = n > 0 ? n : 0;
    }

    bool SkyrimCombat::setTestBlock(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestBlock.clear();
            return false;
        }
        mTestBlock = lowerAscii(editorId);
        return true;
    }

    void SkyrimCombat::loadSettings()
    {
        mCombatSettings = mData->settings();
        mBlockSettings = mData->blockSettings();
        mHaveSettings = true;
        CombatData* const data = mData; // task 0193: the sound records and the VFS probe
        mSounds.bind(mData->soundLibrary(), [data](const std::string& path) { return data->soundFileExists(path); });
    }

    // ---- perks (task 0179, stage S5-7) -----------------------------------------------------------------------------

    namespace
    {
        // The actor values the perk conditions read from a fighter: the skills from 6, then the three vitals.
        constexpr std::uint32_t kPerkSkillBase = 6;
        constexpr std::uint32_t kPerkHealthValue = 24, kPerkMagickaValue = 25, kPerkStaminaValue = 26;

        std::vector<std::string> splitNames(const std::string& list)
        {
            std::vector<std::string> out;
            std::string current;
            auto flush = [&] {
                std::size_t b = 0, e = current.size();
                while (b < e && (current[b] == ' ' || current[b] == '\t'))
                    ++b;
                while (e > b && (current[e - 1] == ' ' || current[e - 1] == '\t'))
                    --e;
                if (e > b)
                    out.push_back(current.substr(b, e - b));
                current.clear();
            };
            for (const char c : list)
            {
                if (c == ',')
                    flush();
                else
                    current.push_back(c);
            }
            flush();
            return out;
        }
    }

    void SkyrimCombat::resolveTestPerks()
    {
        mPerksResolved = true;
        const std::pair<const std::string*, std::vector<FormId>*> lists[]
            = { { &mTestPlayerPerks, &mPlayerTestPerks }, { &mTestNpcPerks, &mNpcTestPerks } };
        for (const auto& [text, ids] : lists)
        {
            ids->clear();
            for (const std::string& n : splitNames(*text))
            {
                const FormId id = mData->perkByEditorId(n);
                if (id == kNullFormId)
                    trace("COMBAT perk '" + n + "' is not a PERK of the data: skipped");
                else
                    ids->push_back(id);
            }
        }
    }

    void SkyrimCombat::preparePerks(Fighter& fighter)
    {
        const PerkLibrary* library = mUsePerks && mData != nullptr ? mData->perkLibrary() : nullptr;
        if (library == nullptr || !fighter.mHasProfile)
            return;
        if (!mPerksResolved)
            resolveTestPerks();
        FighterProfile& p = fighter.mProfile;
        if (fighter.mSubject.player && mRaceAbilitiesSet)
        {
            // task 0225: the abilities of the race the player chose replace those of the stand-in's own race
            p.mSpells.erase(std::remove_if(p.mSpells.begin(), p.mSpells.end(),
                                [this](FormId id) {
                                    return std::find(mStandRaceAbilities.begin(), mStandRaceAbilities.end(), id)
                                        != mStandRaceAbilities.end();
                                }),
                p.mSpells.end());
            for (const FormId id : mRaceAbilities)
                if (std::find(p.mSpells.begin(), p.mSpells.end(), id) == p.mSpells.end())
                    p.mSpells.push_back(id);
        }
        if (fighter.mSubject.player)
        {
            // task 0237: the stones' and blessings' spells replace the ones the last call added
            p.mSpells.erase(std::remove_if(p.mSpells.begin(), p.mSpells.end(),
                                [this](FormId id) {
                                    return std::find(mAppliedExtra.begin(), mAppliedExtra.end(), id)
                                        != mAppliedExtra.end();
                                }),
                p.mSpells.end());
            mAppliedExtra.clear();
            for (const FormId id : mExtraAbilities)
                if (std::find(p.mSpells.begin(), p.mSpells.end(), id) == p.mSpells.end())
                {
                    p.mSpells.push_back(id);
                    mAppliedExtra.push_back(id);
                }
        }
        const std::vector<FormId>& extra = fighter.mSubject.player ? mPlayerTestPerks : mNpcTestPerks;
        p.mPerks.insert(p.mPerks.end(), extra.begin(), extra.end());
        if (fighter.mSubject.player)
        {
            // task 0241: the perks Actor.AddPerk gave the player replace the ones the last call added
            p.mPerks.erase(std::remove_if(p.mPerks.begin(), p.mPerks.end(),
                               [this](FormId id) {
                                   return std::find(mAppliedExtraPerks.begin(), mAppliedExtraPerks.end(), id)
                                       != mAppliedExtraPerks.end();
                               }),
                p.mPerks.end());
            mAppliedExtraPerks.clear();
            for (const FormId id : mExtraPerks)
                if (std::find(p.mPerks.begin(), p.mPerks.end(), id) == p.mPerks.end())
                {
                    p.mPerks.push_back(id);
                    mAppliedExtraPerks.push_back(id);
                }
        }
        std::sort(p.mPerks.begin(), p.mPerks.end());
        p.mPerks.erase(std::unique(p.mPerks.begin(), p.mPerks.end()), p.mPerks.end());
        applyAbilityNumbers(fighter, *library); // task 0241: the held abilities' and blessings' numbers (player only)
        if (p.mPerks.empty() && p.mSpells.empty())
            return;
        std::vector<FormId> spells = p.mSpells;
        collectPerkAbilities(*library, p.mPerks, spells);
        bool found = false;
        const float resist
            = abilityModifier(*library, spells, static_cast<std::int32_t>(ActorValueIndex::DamageResist), found);
        if (found)
            p.mAbilityDamageResist += resist;
        const float speed
            = abilityModifier(*library, spells, static_cast<std::int32_t>(ActorValueIndex::WeaponSpeedMult), found);
        if (found)
        {
            p.mHasWeaponSpeedAbility = true;
            p.mAbilityWeaponSpeed = speed;
        }
        fighter.mPerkValues.clear();
        for (std::size_t i = 0; i < p.mSkills.size(); ++i)
            fighter.mPerkValues.emplace_back(
                kPerkSkillBase + static_cast<std::uint32_t>(i), static_cast<float>(p.mSkills[i]));
        fighter.mPerkValues.emplace_back(kPerkHealthValue, p.mHealthMax);
        fighter.mPerkValues.emplace_back(kPerkMagickaValue, 0.f);
        fighter.mPerkValues.emplace_back(kPerkStaminaValue, p.mStaminaMax);
        fighter.mWornKeywords.clear();
        for (const ArmourPiece& piece : p.mPieces)
            fighter.mWornKeywords.insert(fighter.mWornKeywords.end(), piece.mKeywords.begin(), piece.mKeywords.end());
        if (!p.mPerks.empty() || p.mAbilityDamageResist != 0.f || p.mHasWeaponSpeedAbility)
            trace("COMBAT perks " + name(fighter.mSubject) + ": " + std::to_string(p.mPerks.size()) + " perks, "
                + std::to_string(spells.size()) + " abilities, damage resist " + fmt(p.mAbilityDamageResist)
                + (p.mHasWeaponSpeedAbility ? ", weapon speed x " + fmt(p.mAbilityWeaponSpeed) : std::string()));
    }

    bool SkyrimCombat::perksMatter(const Fighter& fighter) const
    {
        if (!mUsePerks || mData == nullptr || !fighter.mHasProfile)
            return false;
        const FighterProfile& p = fighter.mProfile;
        return !p.mPerks.empty() || p.mAbilityDamageResist != 0.f || p.mHasWeaponSpeedAbility;
    }

    PerkParticipant SkyrimCombat::actorParticipant(const Fighter& fighter, bool power) const
    {
        const FighterProfile& p = fighter.mProfile;
        PerkParticipant out;
        out.mKind = PerkParticipant::Kind::Actor;
        out.mForm = p.mBase;
        out.mRace = p.mRace;
        out.mFemale = p.mFemale;
        out.mLevel = p.mLevel;
        out.mPerks = &p.mPerks;
        out.mKeywords = &p.mKeywords;
        out.mFactions = &p.mFactions;
        out.mWornKeywords = &fighter.mWornKeywords;
        out.mValues = &fighter.mPerkValues;
        const WeaponRecord* const held = perkWeaponFor(fighter);
        out.mRightHandType = held != nullptr ? held->mAnimType : std::uint8_t(0);
        out.mBlocking = isBlocking(fighter);
        out.mPowerAttacking = power;
        return out;
    }

    PerkParticipant SkyrimCombat::weaponParticipant(const WeaponRecord* weapon) const
    {
        PerkParticipant out;
        if (weapon == nullptr)
            return out;
        out.mKind = PerkParticipant::Kind::Weapon;
        out.mForm = weapon->mCommon.mId;
        out.mKeywords = &weapon->mKeywords;
        out.mAnimType = weapon->mAnimType;
        return out;
    }

    // The damage, the mitigation and the stagger of one blow through the perks of both sides. Nothing changes (and
    // nothing is traced) unless a perk effect applied.
    void SkyrimCombat::applyPerksToHit(Fighter& attacker, Fighter& victim, bool power, MeleeHit& hit)
    {
        const bool attackerMatters = perksMatter(attacker);
        const bool victimMatters = perksMatter(victim);
        if (!attackerMatters && !victimMatters)
            return;
        const PerkLibrary* library = mData->perkLibrary();
        if (library == nullptr)
            return;
        if (!mPerkEngine)
            mPerkEngine = std::make_unique<PerkEngine>(*library);
        PerkEngine& engine = *mPerkEngine;
        const PerkParticipant a = actorParticipant(attacker, power);
        const PerkParticipant v = actorParticipant(victim, false);
        const WeaponRecord* const held = perkWeaponFor(attacker); // task 0214
        const PerkParticipant w = weaponParticipant(held);
        PerkQuery attackQuery; // owner = the attacker: [Owner, Weapon, Target]
        attackQuery.set(PerkRole::Owner, &a);
        attackQuery.set(PerkRole::Target, &v);
        if (held != nullptr)
            attackQuery.set(PerkRole::Weapon, &w);
        PerkQuery defendQuery; // owner = the victim: [Owner, Attacker, Weapon]
        defendQuery.set(PerkRole::Owner, &v);
        defendQuery.set(PerkRole::Attacker, &a);
        if (held != nullptr)
            defendQuery.set(PerkRole::Weapon, &w);
        auto step = [&](std::uint8_t entry, const std::vector<FormId>& perks, const PerkQuery& query, float& value,
                        const char* what) {
            if (!engine.hasEntryPoint(perks, entry))
                return false;
            const EntryOutcome outcome = engine.apply(entry, perks, query, value);
            if (outcome.mApplied == 0)
                return false;
            trace("COMBAT perk " + std::string(what) + " (entry point " + std::to_string(entry) + ") "
                + std::to_string(outcome.mApplied) + " effects: " + fmt(value) + " -> " + fmt(outcome.mValue));
            value = outcome.mValue;
            mPerkEffectsApplied += outcome.mApplied;
            return true;
        };
        float before = hit.mDamageBefore;
        bool damageChanged = false;
        if (attackerMatters)
        {
            if (held != nullptr && held->mDamage > 0)
            {
                const float base = static_cast<float>(held->mDamage);
                float newBase = base;
                if (step(PerkEntry::CalculateWeaponDamage, attacker.mProfile.mPerks, attackQuery, newBase,
                        "weapon damage"))
                {
                    before *= newBase / base;
                    damageChanged = true;
                }
            }
            damageChanged
                |= step(PerkEntry::ModAttackDamage, attacker.mProfile.mPerks, attackQuery, before, "attack damage");
            if (power)
                damageChanged |= step(PerkEntry::ModPowerAttackDamage, attacker.mProfile.mPerks, attackQuery, before,
                    "power attack damage");
        }
        if (victimMatters)
            damageChanged
                |= step(PerkEntry::ModIncomingDamage, victim.mProfile.mPerks, defendQuery, before, "incoming damage");
        bool mitigationChanged = false;
        float mitigation = victim.mProfile.mMitigationPercent;
        if (victimMatters)
            mitigation = perkMitigation(victim, mitigationChanged);
        if (damageChanged || mitigationChanged)
        {
            before = std::isfinite(before) ? std::max(0.f, before) : 0.f;
            const float m = std::clamp(mitigation, 0.f, mCombatSettings.mMaxArmorRating);
            hit.mDamageBefore = before;
            hit.mDamageAfter = std::clamp(before * (1.f - m / 100.f), 0.f, before);
            trace("COMBAT perk damage before armour " + fmt(hit.mDamageBefore) + " after armour "
                + fmt(hit.mDamageAfter) + " (mitigation " + fmt(m) + "%)");
        }
        float stagger = hit.mStagger;
        if (attackerMatters)
        {
            PerkQuery staggerQuery;
            staggerQuery.set(PerkRole::Owner, &a);
            staggerQuery.set(PerkRole::Target, &v);
            step(PerkEntry::ModTargetStagger, attacker.mProfile.mPerks, staggerQuery, stagger, "target stagger");
        }
        if (victimMatters)
        {
            PerkQuery incoming;
            incoming.set(PerkRole::Owner, &v);
            incoming.set(PerkRole::Attacker, &a);
            step(PerkEntry::ModIncomingStagger, victim.mProfile.mPerks, incoming, stagger, "incoming stagger");
        }
        hit.mStagger = std::max(0.f, stagger);
    }

    // The victim's mitigation percent through ModArmorRating (per worn piece) and DamageResist abilities.
    float SkyrimCombat::perkMitigation(const Fighter& victim, bool& changed)
    {
        const FighterProfile& p = victim.mProfile;
        changed = false;
        float mitigation = p.mMitigationPercent;
        PerkEngine& engine = *mPerkEngine;
        if (!p.mPieces.empty() && engine.hasEntryPoint(p.mPerks, PerkEntry::ModArmorRating))
        {
            const PerkParticipant owner = actorParticipant(victim, false);
            std::vector<ArmourPiece> pieces = p.mPieces;
            bool any = false;
            for (ArmourPiece& piece : pieces)
            {
                PerkParticipant armour;
                armour.mKind = PerkParticipant::Kind::Armour;
                armour.mForm = piece.mForm;
                armour.mKeywords = &piece.mKeywords;
                PerkQuery query;
                query.set(PerkRole::Owner, &owner);
                query.set(PerkRole::Armour, &armour);
                const EntryOutcome outcome = engine.apply(PerkEntry::ModArmorRating, p.mPerks, query, piece.mRating);
                if (outcome.mApplied > 0)
                {
                    piece.mRating = std::max(0.f, outcome.mValue);
                    mPerkEffectsApplied += outcome.mApplied;
                    any = true;
                }
            }
            if (any)
            {
                mitigation = armourOf(pieces, p.mSkills, mCombatSettings).mMitigationPercent;
                changed = true;
            }
        }
        if (p.mAbilityDamageResist != 0.f)
        {
            mitigation = std::min(mCombatSettings.mMaxArmorRating,
                mitigation + p.mAbilityDamageResist * mCombatSettings.mArmorScalingFactor);
            changed = true;
        }
        return mitigation;
    }

    bool SkyrimCombat::isBlocker(ActorHandle handle) const
    {
        return std::find(mBlockers.begin(), mBlockers.end(), handle) != mBlockers.end();
    }

    // A named blocker blocks whenever it is alive, free (not swinging, not staggered) and its data is read.
    bool SkyrimCombat::isBlocking(const Fighter& fighter) const
    {
        return !fighter.mSubject.player && (isBlocker(fighter.mSubject.actor) || aiGuardUp(fighter.mSubject.actor))
            && fighter.mReady && fighter.mHasProfile && !fighter.mDead && !fighter.mSwinging && !fighter.mStaggered;
    }

    void SkyrimCombat::setTestPlayerSwing(float seconds)
    {
        mPlayerAutoPeriod = (std::isfinite(seconds) && seconds > 0.f) ? seconds : 0.f;
    }

    SkyrimCombat::Fighter* SkyrimCombat::find(CombatSubject subject)
    {
        for (Fighter& f : mFighters)
            if (f.mSubject == subject)
                return &f;
        return nullptr;
    }

    const SkyrimCombat::Fighter* SkyrimCombat::find(CombatSubject subject) const
    {
        for (const Fighter& f : mFighters)
            if (f.mSubject == subject)
                return &f;
        return nullptr;
    }

    bool SkyrimCombat::fighterView(CombatSubject subject, FighterView& out) const
    {
        out = {};
        const Fighter* f = find(subject);
        if (f == nullptr || !f->mReady || !f->mHasProfile)
            return false;
        out.mHealth = f->mHealth;
        out.mHealthMax = f->mProfile.mHealthMax;
        out.mStamina = f->mStamina;
        out.mStaminaMax = f->mProfile.mStaminaMax;
        out.mDead = f->mDead;
        out.mStaggered = f->mStaggered;
        out.mBlocking = isBlocking(*f);
        out.mMagicka = f->mMagicka;
        out.mMagickaMax = f->mProfile.mMagickaMax;
        out.mWardPower = f->mWardPower;
        out.mDemoralized = f->mDemoralizedUntil >= 0.f;
        out.mWeaponClass = mRealGear ? clipClassOf(*f) : std::uint8_t(0); // task 0214
        out.mHasShield = hasShield(*f);
        out.mLevel = f->mProfile.mLevel;
        out.mEngaged = isEngagedNow(*f);
        return true;
    }

    bool SkyrimCombat::isEngagedNow(const Fighter& f) const
    {
        if (f.mSubject.player)
            return false;
        if (mClock < f.mCombatUntil)
            return true;
        for (const AiActor& actor : mAiActors)
            if (actor.mHandle == f.mSubject.actor && !actor.mGiveUp)
                return true;
        for (const ActorHandle& handle : mHostiles)
            if (handle == f.mSubject.actor)
                return true;
        return false;
    }

    void SkyrimCombat::forEachFighter(const FighterVisitor& visit) const
    {
        FighterView view;
        for (const Fighter& f : mFighters)
        {
            if (f.mSubject.player || !fighterView(f.mSubject, view))
                continue;
            visit(f.mSubject, view);
        }
    }

    bool SkyrimCombat::ready(Fighter& fighter)
    {
        if (mData == nullptr)
            return false;
        if (!mHaveSettings)
            loadSettings();
        if (!mWeaponLooked)
        {
            mWeaponLooked = true;
            mWeapon = mTestWeapon.empty() ? nullptr : mData->weaponByEditorId(mTestWeapon);
        }
        if (!mAmmoLooked)
        {
            mAmmoLooked = true;
            mAmmo = mTestAmmo.empty() ? nullptr : mData->ammoByEditorId(mTestAmmo);
            mProjectile = mAmmo != nullptr ? mData->projectileOf(*mAmmo) : nullptr;
            mProjectileSettings = mData->projectileSettings();
            if (!mTestAmmo.empty())
                trace("COMBAT ammo '" + mTestAmmo + "' " + (mAmmo != nullptr ? "found" : "NOT found") + ", projectile "
                    + (mProjectile != nullptr ? "found (speed " + fmt(mProjectile->mSpeed) + " gravity "
                                + fmt(mProjectile->mGravity) + " range " + fmt(mProjectile->mRange) + " type "
                                + std::to_string(mProjectile->mType) + " model " + mProjectile->mModel + ")"
                                              : std::string("NOT found"))
                    + ", weapon " + (mWeapon != nullptr ? "found" : "NOT found"));
        }
        if (!fighter.mReady)
        {
            fighter.mReady = true;
            fighter.mHasProfile = fighter.mSubject.player
                ? true
                : mData->fighterFor(fighter.mBase, static_cast<std::uint32_t>(fighter.mSubject.actor.reference),
                    fighter.mProfile);
            if (mTrace && fighter.mHasProfile && fighter.mProfile.mCreature != nullptr)
                trace("COMBAT creature " + name(fighter.mSubject) + " is a "
                    + std::string(fighter.mProfile.mCreature->mName));
            if (fighter.mSubject.player)
            {
                fighter.mProfile = mData->playerFighter();
                mBaseMitigation = fighter.mProfile.mMitigationPercent; // task 0184: the stand-in's, to restore
                mBasePieces = fighter.mProfile.mPieces;
                mBaseMitigationKnown = true;
            }
            applyRealGear(fighter); // task 0214 (nothing with the setting off)
            fighter.mHealth = fighter.mProfile.mHealthMax;
            fighter.mStamina = fighter.mProfile.mStaminaMax;
            fighter.mMagicka = fighter.mProfile.mMagickaMax;
            if (fighter.mSubject.player && mEquipDirty)
                applyPlayerEquipment();
            preparePerks(fighter);
        }
        return fighter.mHasProfile;
    }

    void SkyrimCombat::onActorAdded(const openeden::sdk::game::ActorInfo& info)
    {
        if (info.handle.valid())
        {
            // The same placement again with a new session replaces the old fighter (its state starts again).
            std::erase_if(mFighters, [&](const Fighter& f) {
                return !f.mSubject.player && f.mSubject.actor.reference == info.handle.reference;
            });
            if (mFighters.size() < kMaxCombatFighters)
            {
                Fighter fighter;
                fighter.mSubject = CombatSubject::of(info.handle);
                fighter.mBase = info.base;
                mFighters.push_back(fighter);
            }
        }
        const std::string id = lowerAscii(info.editorId);
        if (!info.handle.valid())
            return;
        // Task 0186: the names of the actors seen, so a test caster can be named before or after the actor appears.
        std::erase_if(mActorNames, [&](const auto& n) { return n.first.reference == info.handle.reference; });
        if (mActorNames.size() < kMaxCombatFighters)
            mActorNames.emplace_back(info.handle, id);
        for (TestCaster& tc : mTestCasters)
            if (tc.mActor == id)
                tc.mHandle = info.handle;
        if (!mTestSwing.empty() && id == mTestSwing)
        {
            std::erase_if(mSwingers, [&](const ActorHandle& h) { return h.reference == info.handle.reference; });
            mSwingers.push_back(info.handle);
        }
        if (!mTestBlock.empty() && id == mTestBlock)
        {
            std::erase_if(mBlockers, [&](const ActorHandle& h) { return h.reference == info.handle.reference; });
            mBlockers.push_back(info.handle);
        }
        if (!mTestHostile.empty() && id == mTestHostile)
        {
            std::erase_if(mHostiles, [&](const ActorHandle& h) { return h.reference == info.handle.reference; });
            mHostiles.push_back(info.handle);
        }
    }

    void SkyrimCombat::onActorRemoved(ActorHandle handle)
    {
        std::erase(mSwingers, handle);
        std::erase(mHostiles, handle);
        std::erase_if(mOpponents, [&](const auto& pair) { return pair.first == handle; });
        std::erase(mBlockers, handle);
        aiForget(handle); // task 0190
        cancelCastsOf(CombatSubject::of(handle)); // task 0186
        mEffects.removeCaster(CombatSubject::of(handle));
        std::erase_if(mActorNames, [&](const auto& n) { return n.first == handle; });
        for (TestCaster& tc : mTestCasters)
            if (tc.mHandle == handle)
                tc.mHandle = {};
        std::erase_if(mFighters, [&](const Fighter& f) { return !f.mSubject.player && f.mSubject.actor == handle; });
        // Nothing of the actor stays in a table: a result that arrives later is refused, a hit that was waiting is
        // gone.
        const CombatSubject gone = CombatSubject::of(handle);
        std::erase_if(mPendingHits, [&](const PendingHit& p) { return p.mSubject == gone; });
        std::erase_if(mAwaiting, [&](const PendingHit& p) { return p.mSubject == gone; });
        std::erase_if(mDeliveries, [&](const SweepDelivery& d) { return d.mWho.mSubject == gone; });
        std::erase_if(mPendingBow, [&](const PendingBow& p) { return p.mSubject == gone; });
    }

    void SkyrimCombat::onPlayerAttack(bool power)
    {
        if (!mTestPlayerSpell.empty())
            return; // task 0186: with a spell chosen the attack control casts (its edges say when)
        mPlayerAttackPending = true;
        mPlayerPower = power;
    }

    void SkyrimCombat::onClipEvent(const openeden::sdk::game::ClipEvent& event)
    {
        ++mEventsSeen;
        soundEvent(event); // task 0193
        if (event.slot == ClipSlot::Cast && event.kind == openeden::sdk::game::ClipEventKind::SpellFire)
        {
            // Task 0186: the cast clip lets the spell go; combatUpdate fires it.
            for (Cast& c : mCasts)
                if (c.mToken == event.token && c.mCaster == event.subject)
                    c.mFireSeen = true;
            return;
        }
        // Task 0178: the bow's two events are acted on in the next combatUpdate (a writer is not valid here).
        const bool bowDrawn
            = event.slot == ClipSlot::BowDraw && event.kind == openeden::sdk::game::ClipEventKind::BowDrawn;
        const bool bowRelease
            = event.slot == ClipSlot::BowRelease && event.kind == openeden::sdk::game::ClipEventKind::BowRelease;
        if (bowDrawn || bowRelease)
        {
            if (mPendingBow.size() < mPendingBow.capacity())
                mPendingBow.push_back({ event.subject, event.token, bowRelease });
            return;
        }
        const bool attackSlot = event.slot == ClipSlot::Attack || event.slot == ClipSlot::PowerAttack
            || event.slot == ClipSlot::AttackLeft || event.slot == ClipSlot::Bash;
        if (mData == nullptr || event.kind != openeden::sdk::game::ClipEventKind::Hit || !attackSlot)
        {
            if (event.kind == openeden::sdk::game::ClipEventKind::Hit)
                ++mRefusedEvents; // a hit with no rule to act on it
            return;
        }
        if (mPendingHits.size() < mPendingHits.capacity())
            mPendingHits.push_back(
                { event.subject, event.token, event.slot == ClipSlot::PowerAttack, 0, event.slot == ClipSlot::Bash });
        else if (!mWarnedHitsFull)
        {
            // Task 0192: more blows in one frame than the table holds (the bridge plays at most 64 clips, so this is
            // a sign of a table sized below that): said once, never in silence.
            mWarnedHitsFull = true;
            const std::string line = "COMBAT more hit events in one frame than the table holds; blows are dropped";
            if (mWarning)
                mWarning(line);
            trace(line);
        }
    }

    void SkyrimCombat::onClipFinished(CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how)
    {
        ++mClipsFinished;
        // Task 0186: a cast clip that ended without its event let the spell go at its end (like the bow's draw); one
        // that was interrupted or failed lets nothing go.
        std::erase_if(mCasts, [&](const Cast& c) {
            return c.mToken == token && c.mCaster == subject && !c.mFireSeen
                && how != openeden::sdk::game::ClipEnd::Completed;
        });
        for (Cast& c : mCasts)
            if (c.mToken == token && c.mCaster == subject && how == openeden::sdk::game::ClipEnd::Completed)
                c.mFireSeen = true;
        if (how == openeden::sdk::game::ClipEnd::Completed)
            ++mClipsCompleted;
        if (Fighter* f = find(subject); f != nullptr)
        {
            if (f->mSwinging && f->mSwingToken == token)
                f->mSwinging = false;
            if (f->mStaggered && f->mStaggerToken == token)
                f->mStaggered = false;
            if (f->mBow != Fighter::Bow::None && f->mBowToken == token)
            {
                // Measured in game: the draw clip's bowDrawn event lies a hair past the clip's own length (1.66667 s
                // against a shorter duration), so the core never delivers it; a draw clip that COMPLETES has drawn.
                if (f->mBow == Fighter::Bow::Drawing && how == openeden::sdk::game::ClipEnd::Completed
                    && mPendingBow.size() < mPendingBow.capacity())
                    mPendingBow.push_back({ subject, token, false });
                else
                    f->mBow = Fighter::Bow::None; // replaced by a hit reaction, failed, or the release clip ended
            }
        }
    }

    void SkyrimCombat::onSweepResult(
        CombatSubject subject, std::uint32_t token, const SweepHit* hits, std::size_t count)
    {
        const auto waiting = std::find_if(mAwaiting.begin(), mAwaiting.end(),
            [&](const PendingHit& p) { return p.mSubject == subject && p.mToken == token; });
        if (waiting == mAwaiting.end())
        {
            ++mRefusedSweepResults;
            return;
        }
        const PendingHit who = *waiting;
        mAwaiting.erase(waiting); // gone whether or not there is room to deliver (a refused result must not leak)
        if (mDeliveries.size() >= mDeliveries.capacity())
        {
            ++mRefusedSweepResults;
            return;
        }
        SweepDelivery delivery;
        delivery.mWho = who;
        delivery.mCount = std::min(count, openeden::sdk::game::kMaxSweepHits);
        for (std::size_t i = 0; i < delivery.mCount && hits != nullptr; ++i)
            delivery.mHits[i] = hits[i];
        mDeliveries.push_back(delivery);
    }

    void SkyrimCombat::regenerate(float seconds)
    {
        const bool anyRate = anyRegenEffect(); // task 0238
        applyPlayerValueModifiers(); // task 0240
        for (Fighter& f : mFighters)
        {
            if (!f.mReady || !f.mHasProfile || f.mDead)
                continue;
            const bool inCombat = mClock < f.mCombatUntil;
            const float healthRate = regenRateFor(f, 0, f.mProfile.mHealthRegenPercent, anyRate);
            if (!f.mSubject.player)
                f.mHealth = std::min(f.mProfile.mHealthMax,
                    f.mHealth
                        + regenAmount(f.mProfile.mHealthMax, healthRate, mCombatSettings.mCombatHealthRegenRateMult,
                            inCombat, seconds));
            else if (anyRate || (f.mHasAbilities && f.mAbilities.hasRegen()))
                // the engine regenerates the player's health; only what the effects and abilities add is ours
                addPlayerHealthRegen(regenAmount(f.mProfile.mHealthMax, healthRate - f.mProfile.mHealthRegenPercent,
                    mCombatSettings.mCombatHealthRegenRateMult, inCombat, seconds));
            f.mStamina = std::min(f.mProfile.mStaminaMax,
                f.mStamina
                    + regenAmount(f.mProfile.mStaminaMax,
                        regenRateFor(f, 2, f.mProfile.mStaminaRegenPercent, anyRate),
                        mCombatSettings.mCombatStaminaRegenRateMult, inCombat, seconds));
            // Task 0186: magicka waits after a spend and while a spell is held (fDamagedMagickaRegenDelay).
            if (f.mProfile.mMagickaMax > 0.f && mClock >= f.mMagickaRegenAt && !isBusyConcentrating(f.mSubject))
                f.mMagicka = std::min(f.mProfile.mMagickaMax,
                    f.mMagicka
                        + regenAmount(f.mProfile.mMagickaMax,
                            regenRateFor(f, 1, f.mProfile.mMagickaRegenPercent, anyRate),
                            mCombatSettings.mCombatMagickaRegenRateMult, inCombat, seconds));
        }
        traceRegenRates(anyRate); // task 0238: one line a second per fighter with a rate effect, only with a trace
    }

    // ---- task 0210: waiting and sleeping.
    void SkyrimCombat::requestRest(float seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0.f)
            return;
        mRestSeconds = std::min(kMaxRestSeconds, mRestSeconds + std::min(seconds, kMaxRestSeconds));
    }

    bool SkyrimCombat::playerInCombat() const
    {
        const Fighter* player = find(CombatSubject::ofPlayer());
        return player != nullptr && mClock < player->mCombatUntil;
    }

    bool SkyrimCombat::enemiesNear() const
    {
        for (const AiActor& actor : mAiActors)
        {
            const Fighter* f = find(CombatSubject::of(actor.mHandle));
            if (f != nullptr && !f->mDead && !actor.mGiveUp && actor.mTarget.player)
                return true;
        }
        for (const ActorHandle& handle : mHostiles)
        {
            const Fighter* f = find(CombatSubject::of(handle));
            if (f != nullptr && !f->mDead)
                return true;
        }
        return false;
    }

    void SkyrimCombat::applyRest(CombatRequestWriter& writer)
    {
        if (!(mRestSeconds > 0.f))
            return;
        const float seconds = mRestSeconds;
        mRestSeconds = 0.f;
        ++mRestsApplied;
        float playerGain = 0.f;
        for (Fighter& f : mFighters)
        {
            if (!f.mReady || !f.mHasProfile || f.mDead)
                continue;
            const float health = regenAmount(f.mProfile.mHealthMax,
                regenRateFor(f, 0, f.mProfile.mHealthRegenPercent, false), mCombatSettings.mCombatHealthRegenRateMult,
                false, seconds); // task 0241: the abilities' rates count in a rest
            if (f.mSubject.player)
                playerGain = health;
            else
                f.mHealth = std::min(f.mProfile.mHealthMax, f.mHealth + health);
            f.mStamina = std::min(f.mProfile.mStaminaMax,
                f.mStamina
                    + regenAmount(f.mProfile.mStaminaMax, regenRateFor(f, 2, f.mProfile.mStaminaRegenPercent, false),
                        mCombatSettings.mCombatStaminaRegenRateMult, false, seconds));
            if (f.mProfile.mMagickaMax > 0.f)
                f.mMagicka = std::min(f.mProfile.mMagickaMax,
                    f.mMagicka
                        + regenAmount(f.mProfile.mMagickaMax,
                            regenRateFor(f, 1, f.mProfile.mMagickaRegenPercent, false),
                            mCombatSettings.mCombatMagickaRegenRateMult, false, seconds));
        }
        // The player's health is the engine's: the same report that takes none off as a level up's gain.
        Fighter* player = find(CombatSubject::ofPlayer());
        if (player == nullptr || !player->mReady || !player->mHasProfile || player->mDead || !(playerGain > 0.f))
            return;
        openeden::sdk::game::SubjectState state;
        if (!writer.subjectState(CombatSubject::ofPlayer(), state) || !(state.healthMax > 0.f))
            return;
        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = true;
        report.victimIsPlayer = true;
        report.amount = 0.f;
        report.healthMax = std::max(state.healthMax, player->mProfile.mHealthMax);
        report.healthAfter = std::min(report.healthMax, std::max(state.health, 0.f) + playerGain);
        if (!(report.healthAfter > state.health))
            return; // already at its maximum: nothing to send
        report.x = state.x;
        report.y = state.y;
        report.z = state.z;
        if (writer.reportDamage(report))
            player->mHealth = report.healthAfter;
    }

    void SkyrimCombat::beginAttack(CombatRequestWriter& writer, Fighter& fighter, AttackKind kind)
    {
        mSounds.weaponDraw(writer, fighter.mSubject, weaponFor(fighter)); // task 0193: once per fighter
        const bool power = kind == AttackKind::Power;
        if (fighter.mSubject.player)
        {
            if (mPlayerSwingActive || kind == AttackKind::Bash) // the player has no bash (there is no input for it)
                return;
            // The player's clip is never played (no first-person arms): its timing is the sidecar's, asked for by the
            // clip the slot would play. Until it is ready there is no swing (stage S5-3b: no fallback number).
            syncPlayerTimingClass(fighter);
            const std::string_view playerClip
                = meleeClip(power ? ClipSlot::PowerAttack : ClipSlot::Attack, fighter, false);
            const int timingSlot = power ? 1 : 0;
            openeden::sdk::game::ClipTiming timing;
            // Once known the timing is kept: the engine is not asked again (review fix of task 0171).
            if (mHaveTiming[timingSlot])
                timing = mKnownTiming[timingSlot];
            else if (!playerClip.empty() && writer.clipTiming(playerClip, false, timing) && timing.hasHit
                && timing.length > 0.f)
            {
                mKnownTiming[timingSlot] = timing;
                mHaveTiming[timingSlot] = true;
            }
            if (playerClip.empty() || !timing.hasHit || !(timing.length > 0.f))
            {
                if (mPlayerTimingSince < 0.f)
                    mPlayerTimingSince = mClock;
                else if (mClock - mPlayerTimingSince >= kPlayerTimingTimeoutSeconds && mPlayerTimingTimeouts == 0)
                {
                    ++mPlayerTimingTimeouts;
                    const std::string line = "COMBAT player swing timing never arrived for '" + std::string(playerClip)
                        + "': the player cannot swing";
                    if (mWarning)
                        mWarning(line);
                    trace(line);
                }
                return;
            }
            mPlayerTimingSince = -1.f;
            mPlayerHitTime = timing.hitTime;
            mPlayerSwingLength = timing.length;
            mPlayerSwingActive = true;
            mPlayerHitDone = false;
            mPlayerSwingStart = mClock;
            mPlayerSwingToken = mNextToken++;
            ++mPlayerSwingCount; // task 0222: the core draws the swing on the player's body
            mPlayerSwingClip.assign(playerClip);
            mPlayerPower = power;
            mSounds.queueSwing(fighter.mSubject, weaponFor(fighter)); // task 0193: the player has no clip event
            ++mSwingsRequested;
            if (mTrace)
                trace("COMBAT swing player token " + std::to_string(mPlayerSwingToken) + (power ? " power" : "")
                    + " hit at " + std::to_string(mPlayerHitTime) + " length " + std::to_string(mPlayerSwingLength));
            return;
        }
        if (fighter.mDead || fighter.mSwinging || fighter.mStaggered || isParalyzed(fighter)) // task 0233
            return;
        const ClipSlot slot = kind == AttackKind::Bash ? ClipSlot::Bash
            : power                                    ? ClipSlot::PowerAttack
                                                       : ClipSlot::Attack;
        const std::string_view clip = meleeClip(slot, fighter, kind == AttackKind::Bash);
        if (clip.empty())
            return;
        openeden::sdk::game::ClipRequest request;
        request.slot = slot;
        request.clip = clip;
        request.clipPerSex = false;
        request.blendSeconds = 0.1f;
        request.token = mNextToken++;
        if (writer.playClip(fighter.mSubject, request))
        {
            fighter.mSwinging = true;
            fighter.mSwingToken = request.token;
            ++mSwingsRequested;
            if (mTrace)
                trace("COMBAT swing " + name(fighter.mSubject) + " token " + std::to_string(request.token)
                    + (kind == AttackKind::Bash ? " bash"
                            : power             ? " power"
                                                : ""));
        }
    }

    void SkyrimCombat::chooseCrowd(CombatRequestWriter& writer)
    {
        // Task 0192: once, at the delay; the actors then known and alive are ranked by their distance to the player.
        mCrowdChosen = true;
        openeden::sdk::game::SubjectState player;
        if (!writer.subjectState(CombatSubject::ofPlayer(), player))
            return;
        std::vector<Crowd::Candidate> candidates;
        candidates.reserve(mFighters.size());
        for (const Fighter& f : mFighters)
        {
            openeden::sdk::game::SubjectState where;
            if (f.mSubject.player || f.mDead || !writer.subjectState(f.mSubject, where))
                continue;
            candidates.push_back({ f.mSubject.actor, where.x, where.y });
        }
        const Crowd::Choice choice = Crowd::choose(candidates, player.x, player.y, mTestCrowd, mTestDuels);
        mHostiles.reserve(mHostiles.size() + choice.mHostile.size() + choice.mOpponents.size());
        for (const ActorHandle& h : choice.mHostile)
            if (std::find(mHostiles.begin(), mHostiles.end(), h) == mHostiles.end())
                mHostiles.push_back(h);
        for (const auto& pair : choice.mOpponents)
        {
            if (std::find(mHostiles.begin(), mHostiles.end(), pair.first) == mHostiles.end())
                mHostiles.push_back(pair.first);
            mOpponents.push_back(pair);
        }
        for (const ActorHandle& h : choice.mHostile)
            trace("COMBAT crowd hostile " + name(CombatSubject::of(h)));
        for (std::size_t i = 0; i + 1 < choice.mOpponents.size(); i += 2)
            trace("COMBAT crowd duel " + name(CombatSubject::of(choice.mOpponents[i].first)) + " against "
                + name(CombatSubject::of(choice.mOpponents[i].second)));
        trace("COMBAT crowd chosen: " + std::to_string(choice.mHostile.size()) + " hostile to the player, "
            + std::to_string(choice.mOpponents.size() / 2) + " duels, of " + std::to_string(candidates.size())
            + " actors");
    }

    bool SkyrimCombat::mayHit(const Fighter& attacker, const Fighter& victim) const
    {
        if (factions())
        {
            // Task 0197: the real relation. The crowd rule below stays for the runs without the factions rules.
            if (attacker.mSubject.player)
                return true;
            const AiActor* engaged = aiFind(attacker.mSubject.actor);
            const bool isTarget = engaged != nullptr && engaged->mTarget == victim.mSubject;
            return Factions::mayHit(relationOf(attacker, victim), isTarget);
        }
        if (!crowdOn() || attacker.mSubject.player)
            return true;
        for (const auto& pair : mOpponents)
            if (pair.first == attacker.mSubject.actor)
                return !victim.mSubject.player && victim.mSubject.actor == pair.second;
        if (std::find(mHostiles.begin(), mHostiles.end(), attacker.mSubject.actor) != mHostiles.end())
            return victim.mSubject.player;
        return true;
    }

    void SkyrimCombat::startHostiles(CombatRequestWriter& writer)
    {
        if (mHostiles.empty() || mAiOn) // with the AI on the named hostiles are engaged by runAi
            return;
        openeden::sdk::game::SubjectState player;
        bool havePlayer = false;
        for (const ActorHandle& handle : mHostiles)
        {
            Fighter* f = find(CombatSubject::of(handle));
            if (f == nullptr || f->mDead || f->mSwinging || f->mStaggered || f->mBow != Fighter::Bow::None
                || mClock < f->mNextAttack || !ready(*f))
                continue;
            // Task 0192: a duelist fights its one opponent, everybody else the player.
            const ActorHandle* opponent = nullptr;
            for (const auto& pair : mOpponents)
                if (pair.first == handle)
                    opponent = &pair.second;
            openeden::sdk::game::SubjectState rival;
            if (opponent != nullptr)
            {
                const Fighter* other = find(CombatSubject::of(*opponent));
                if (other == nullptr || other->mDead || !writer.subjectState(other->mSubject, rival))
                    continue;
            }
            else if (!havePlayer)
            {
                havePlayer = writer.subjectState(CombatSubject::ofPlayer(), player);
                if (!havePlayer)
                    return;
            }
            const openeden::sdk::game::SubjectState& target = opponent != nullptr ? rival : player;
            openeden::sdk::game::SubjectState self;
            if (!writer.subjectState(f->mSubject, self))
                continue;
            const double dx = target.x - self.x;
            const double dy = target.y - self.y;
            if (opponent != nullptr && mWeapon != nullptr && mWeapon->mAnimType == kWeaponAnimBow)
                continue; // duels are melee only: arrows are aimed at the player
            if (mWeapon != nullptr && mWeapon->mAnimType == kWeaponAnimBow)
            {
                // Task 0178: an archer shoots from afar (up to the bow's maximum range) and does not swing.
                const double range = mWeapon->mMaxRange > 0.f ? mWeapon->mMaxRange : kBowDefaultRange;
                if (!bowReady() || std::sqrt(dx * dx + dy * dy) > range)
                    continue;
                writer.faceHeading(f->mSubject, static_cast<float>(std::atan2(dx, dy)));
                beginBowDraw(writer, *f);
                continue;
            }
            const float reach = meleeReachUnits(weaponFor(*f), f->mProfile.mUnarmedReach, mCombatSettings);
            // The sweep reaches `reach` from the actor to the player's capsule: close enough when the centres are
            // within that and a body radius, less a little so a hit is certain.
            if (std::sqrt(dx * dx + dy * dy) > static_cast<double>(reach + kBodyRadius - 4.f))
                continue;
            writer.faceHeading(f->mSubject, static_cast<float>(std::atan2(dx, dy)));
            f->mNextAttack = mClock + kHostileAttackPeriod;
            // The every-N rules (task 0172): counts per actor from 1, a bash wins on a shared number.
            ++f->mAttackCount;
            AttackKind kind = AttackKind::Normal;
            if (mBashEvery > 0 && f->mAttackCount % static_cast<std::uint32_t>(mBashEvery) == 0)
                kind = AttackKind::Bash;
            else if (mPowerEvery > 0 && f->mAttackCount % static_cast<std::uint32_t>(mPowerEvery) == 0)
                kind = AttackKind::Power;
            beginAttack(writer, *f, kind);
        }
    }

    void SkyrimCombat::runPlayerSwing(CombatRequestWriter& writer)
    {
        if (mData == nullptr)
            return;
        Fighter& player = mFighters.front();
        if (!ready(player))
            return;
        if (mWeapon != nullptr && mWeapon->mAnimType == kWeaponAnimBow)
        {
            // Task 0178: with a bow the attack control draws and shoots (runPlayerBow); there is no melee swing.
            mPlayerAttackPending = false;
            return;
        }
        syncPlayerTimingClass(player);
        if (!mPlayerTimingKicked)
        {
            // Start reading the sidecars of the player's attack clips now, long before the first swing needs them (the
            // answer is ignored here; the swing asks again). Reading is on the core's worker.
            mPlayerTimingKicked = true;
            for (const ClipSlot slot : { ClipSlot::Attack, ClipSlot::PowerAttack })
            {
                const std::string_view clip = meleeClip(slot, player, false);
                openeden::sdk::game::ClipTiming timing;
                const int timingSlot = slot == ClipSlot::PowerAttack ? 1 : 0;
                if (!clip.empty() && writer.clipTiming(clip, false, timing) && timing.hasHit && timing.length > 0.f)
                {
                    mKnownTiming[timingSlot] = timing;
                    mHaveTiming[timingSlot] = true;
                }
            }
        }
        // A press while a swing runs is dropped (no buffering in this stage).
        const bool pressed = mPlayerAttackPending;
        mPlayerAttackPending = false;
        if (!mPlayerSwingActive)
        {
            bool start = false;
            bool power = false;
            bool automatic = false;
            if (pressed)
            {
                start = true;
                power = mPlayerPower;
            }
            else if (mPlayerAutoPeriod > 0.f && mClock >= mNextPlayerAuto)
            {
                start = true;
                mNextPlayerAuto = mClock + mPlayerAutoPeriod;
                automatic = true;
            }
            if (start)
            {
                beginAttack(writer, player, power ? AttackKind::Power : AttackKind::Normal);
                // An automatic swing whose timing is not ready yet asks again in the next frame, not a period later.
                if (automatic && !mPlayerSwingActive)
                    mNextPlayerAuto = mClock;
            }
        }
        if (mPlayerSwingActive)
        {
            const float elapsed = mClock - mPlayerSwingStart;
            if (!mPlayerHitDone && elapsed >= mPlayerHitTime)
            {
                mPlayerHitDone = true;
                processHit(writer, { player.mSubject, mPlayerSwingToken, mPlayerPower });
            }
            if (elapsed >= mPlayerSwingLength)
                mPlayerSwingActive = false;
        }
    }

    void SkyrimCombat::processHit(CombatRequestWriter& writer, const PendingHit& hit)
    {
        Fighter* attacker = find(hit.mSubject);
        if (attacker == nullptr || attacker->mDead || !ready(*attacker))
            return;
        if (mAwaiting.size() >= mAwaiting.capacity())
        {
            // No room to remember the sweep: the swing does not land (nothing is spent) and the table is reported once.
            if (!mWarnedAwaitingFull)
            {
                mWarnedAwaitingFull = true;
                const std::string line = "COMBAT sweeps waiting for their result fill the table; hits are dropped";
                if (mWarning)
                    mWarning(line);
                trace(line);
            }
            return;
        }
        const WeaponRecord* const swung = weaponFor(*attacker); // task 0184
        MeleeInput input;
        input.mWeapon = swung;
        input.mUnarmedDamage = attacker->mProfile.mUnarmedDamage;
        input.mPowerAttack = hit.mPower;
        if (swung != nullptr && swung->skillSlot() < attacker->mProfile.mSkills.size())
            input.mAttackerSkill = attacker->mProfile.mSkills[swung->skillSlot()];
        const MeleeHit cost = computeMeleeHit(input, mCombatSettings);
        float staminaCost = hit.mBash ? bashStaminaCost(mBlockSettings) : cost.mStaminaCost;
        if (hit.mPower && !hit.mBash && perksMatter(*attacker) && mData->perkLibrary() != nullptr)
        {
            // Task 0179: ModPowerAttackStamina [owner, weapon].
            if (!mPerkEngine)
                mPerkEngine = std::make_unique<PerkEngine>(*mData->perkLibrary());
            const PerkParticipant a = actorParticipant(*attacker, true);
            const WeaponRecord* const held = perkWeaponFor(*attacker);
            const PerkParticipant w = weaponParticipant(held);
            PerkQuery query;
            query.set(PerkRole::Owner, &a);
            if (held != nullptr)
                query.set(PerkRole::Weapon, &w);
            if (mPerkEngine->hasEntryPoint(attacker->mProfile.mPerks, PerkEntry::ModPowerAttackStamina))
            {
                const EntryOutcome outcome = mPerkEngine->apply(
                    PerkEntry::ModPowerAttackStamina, attacker->mProfile.mPerks, query, staminaCost);
                if (outcome.mApplied > 0)
                {
                    trace("COMBAT perk power attack stamina " + fmt(staminaCost) + " -> " + fmt(outcome.mValue));
                    staminaCost = std::max(0.f, outcome.mValue);
                    mPerkEffectsApplied += outcome.mApplied;
                }
            }
        }
        attacker->mStamina = std::max(0.f, attacker->mStamina - staminaCost);
        attacker->mCombatUntil = mClock + kCombatLingerSeconds;
        openeden::sdk::game::SubjectState where;
        if (!writer.subjectState(attacker->mSubject, where))
            return;
        openeden::sdk::game::SweepRequest request;
        request.token = hit.mToken;
        request.x = where.x;
        request.y = where.y;
        request.z = where.z;
        request.heading = where.heading;
        request.reach = meleeReachUnits(swung, attacker->mProfile.mUnarmedReach, mCombatSettings);
        request.halfAngle = kStrikeHalfAngle;
        request.heightBelow = kSweepHeightBelow;
        request.heightAbove = kSweepHeightAbove;
        if (mTrace)
            trace("COMBAT hitframe " + name(attacker->mSubject) + " token " + std::to_string(hit.mToken) + " stamina "
                + fmt(attacker->mStamina) + " of " + fmt(attacker->mProfile.mStaminaMax) + " reach "
                + fmt(request.reach)
                + (hit.mBash ? " bash" : ""));
        if (writer.requestSweep(attacker->mSubject, request))
        {
            PendingHit asked = hit;
            asked.mAskedAt = mUpdates;
            mAwaiting.push_back(asked); // room was checked above
        }
    }

    void SkyrimCombat::applyHit(CombatRequestWriter& writer, Fighter& attacker, bool power, bool bash,
        const SweepHit& hit, Fighter& victim, const MeleeHit* preset)
    {
        const WeaponRecord* const swung = weaponFor(attacker); // task 0184
        MeleeInput input;
        input.mWeapon = swung;
        input.mUnarmedDamage = attacker.mProfile.mUnarmedDamage;
        input.mPowerAttack = power;
        input.mTargetMitigationPercent = victim.mProfile.mMitigationPercent;
        if (swung != nullptr && swung->skillSlot() < attacker.mProfile.mSkills.size())
            input.mAttackerSkill = attacker.mProfile.mSkills[swung->skillSlot()];
        MeleeHit result = preset != nullptr ? *preset : computeMeleeHit(input, mCombatSettings);
        if (!bash)
            applyPerksToHit(attacker, victim, power, result);
        const float sneakMult = bash ? 1.f : sneakAttackFor(attacker, victim, swung, preset != nullptr); // task 0227
        if (sneakMult > 1.f)
        {
            result.mDamageBefore *= sneakMult;
            result.mDamageAfter *= sneakMult;
        }

        if (!bash)
            applySkillPercent(attacker, swung, result); // task 0240
        // A bash deals no damage and no block stops it; a blow on a blocker goes through the block formula (task 0172).
        float damage = bash ? 0.f : result.mDamageAfter;
        bool blocked = false;
        bool broken = false;
        std::string blockNote;
        if (!bash && isBlocking(victim))
        {
            BlockInput blockInput;
            blockInput.mDamage = damage;
            blockInput.mBlockerSkill = victim.mProfile.mSkills[kBlockSkillSlot];
            blockInput.mPowerAttack = power;
            blockInput.mShieldBonus = shieldBlockBonus(victim); // task 0214
            blockInput.mBlockerStamina = victim.mStamina;
            if (perksMatter(victim) && mPerkEngine
                && mPerkEngine->hasEntryPoint(victim.mProfile.mPerks, PerkEntry::ModPercentBlocked))
            {
                // Task 0179: ModPercentBlocked [owner] scales the fraction a block takes off.
                const PerkParticipant owner = actorParticipant(victim, false);
                PerkQuery query;
                query.set(PerkRole::Owner, &owner);
                const EntryOutcome outcome
                    = mPerkEngine->apply(PerkEntry::ModPercentBlocked, victim.mProfile.mPerks, query, 1.f);
                if (outcome.mApplied > 0)
                {
                    blockInput.mFractionScale = outcome.mValue;
                    mPerkEffectsApplied += outcome.mApplied;
                    trace("COMBAT perk block fraction scale " + fmt(outcome.mValue));
                }
            }
            const BlockResult block = computeBlock(blockInput, mBlockSettings);
            victim.mStamina = std::max(0.f, victim.mStamina - block.mStaminaDrain);
            damage = block.mDamageTaken;
            blocked = block.mBlocked;
            broken = block.mBroken;
            blockNote = std::string(blocked ? " blocked fraction " : " block broken fraction ") + fmt(block.mFraction)
                + " stamina " + fmt(victim.mStamina);
        }

        float current = victim.mHealth;
        float maximum = victim.mProfile.mHealthMax;
        if (victim.mSubject.player)
        {
            // The player's health is the engine's: read it when the engine has one.
            openeden::sdk::game::SubjectState state;
            if (writer.subjectState(victim.mSubject, state) && state.healthMax > 0.f)
            {
                current = state.health;
                maximum = state.healthMax;
            }
        }
        const float after = std::max(0.f, current - damage);
        victim.mHealth = after;
        if (mSkillHook && !bash)
            reportHitSkills(attacker, victim, swung, result.mDamageAfter, damage, blocked); // task 0200
        victim.mCombatUntil = mClock + kCombatLingerSeconds;
        ++mHitsResolved;
        aiProvoke(attacker, victim); // task 0190: an NPC the player hurt fights back (or flees)

        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = attacker.mSubject.player;
        report.attacker = attacker.mSubject.actor;
        report.victimIsPlayer = victim.mSubject.player;
        report.victim = victim.mSubject.actor;
        report.amount = damage;
        report.healthAfter = after;
        report.healthMax = maximum;
        report.x = hit.x;
        report.y = hit.y;
        report.z = hit.z;
        writer.reportDamage(report);
        if (sneakMult > 1.f && mSneakDone)
            mSneakDone(victim.mSubject.actor.reference, sneakMult); // task 0227
        soundHit(writer, attacker, victim, blocked, bash); // task 0193
        if (!blocked && !bash)
            applyPoisonOnHit(writer, attacker, victim, hit); // task 0229
        if (!blocked && !bash && after > 0.f)
            notifyPlayerHit(attacker, victim); // task 0240
        if (mTrace)
            trace("COMBAT damage " + name(attacker.mSubject) + " -> " + name(victim.mSubject) + " before armour "
                + fmt(result.mDamageBefore) + " after armour " + fmt(result.mDamageAfter) + " (mitigation "
                + fmt(victim.mProfile.mMitigationPercent) + "%) health " + fmt(current) + " -> " + fmt(after) + " of "
                + fmt(maximum) + (power ? " power" : "") + (bash ? " bash" : "") + blockNote);

        if (after > 0.f)
        {
            if (!victim.mSubject.player)
            {
                // What the victim does: a held block plays the block-hit clip, a bash, a broken block or a heavy enough
                // blow staggers it, anything else makes it recoil (S5-3).
                const bool staggers
                    = bash || broken || (!blocked && causesStagger(result.mStagger, power, mBlockSettings));
                const ClipSlot slot = blocked ? ClipSlot::BlockHit : staggers ? ClipSlot::Stagger : ClipSlot::Recoil;
                const std::string_view clip = meleeClip(slot, victim, slot == ClipSlot::BlockHit);
                if (!clip.empty() && !isParalyzed(victim)) // task 0233: a held body keeps its pose
                {
                    openeden::sdk::game::ClipRequest request;
                    request.slot = slot;
                    request.clip = clip;
                    request.blendSeconds = 0.05f;
                    request.token = mNextToken++;
                    if (writer.playClip(victim.mSubject, request))
                    {
                        victim.mSwinging = false; // the reaction replaced its swing
                        victim.mStaggered = slot == ClipSlot::Stagger;
                        victim.mStaggerToken = request.token;
                        if (mTrace)
                            trace("COMBAT reaction " + name(victim.mSubject) + " clip " + std::string(clip));
                    }
                }
            }
            return;
        }
        if (victim.mDead)
            return;
        victim.mDead = true;
        victim.mStaggered = false;
        if (mTrace)
            trace("COMBAT death " + name(victim.mSubject));
        if (!victim.mSubject.player)
        {
            // The stagger-back clip lays the body down; as a Death-slot clip the core holds its last pose until the
            // ragdoll (stage S5-5) exists. (The table has no Death entry: its test pins that, so the Stagger entry is
            // used.)
            // Task 0212: a creature has a Death clip of its own.
            std::string_view clip = clipOf(victim, ClipSlot::Death);
            if (clip.empty())
                clip = meleeClip(ClipSlot::Stagger, victim, false);
            if (!clip.empty())
            {
                openeden::sdk::game::ClipRequest request;
                request.slot = ClipSlot::Death;
                request.clip = clip;
                request.blendSeconds = 0.05f;
                request.token = mNextToken++;
                (void)writer.playClip(victim.mSubject, request);
            }
            victim.mSwinging = false;
            (void)writer.reportDeath(victim.mSubject.actor);
        }
    }

    void SkyrimCombat::resolveSweep(CombatRequestWriter& writer, const SweepDelivery& delivery)
    {
        Fighter* attacker = find(delivery.mWho.mSubject);
        if (attacker == nullptr || !ready(*attacker))
            return;
        for (std::size_t i = 0; i < delivery.mCount; ++i)
        {
            const SweepHit& hit = delivery.mHits[i];
            if (hit.target == SweepTarget::World)
                continue;
            Fighter* victim = hit.target == SweepTarget::Player ? find(CombatSubject::ofPlayer())
                                                                : find(CombatSubject::of(hit.actor));
            if (victim == nullptr || victim == attacker || victim->mDead || !ready(*victim)
                || !mayHit(*attacker, *victim))
                continue;
            applyHit(writer, *attacker, delivery.mWho.mPower, delivery.mWho.mBash, hit, *victim);
            return; // one target a swing: the nearest valid one
        }
        if (mTrace)
            trace("COMBAT miss " + name(attacker->mSubject) + " token " + std::to_string(delivery.mWho.mToken));
    }

    bool SkyrimCombat::bowReady()
    {
        return mWeapon != nullptr && mWeapon->mAnimType == kWeaponAnimBow && mAmmo != nullptr && mAmmo->mHasData
            && mProjectile != nullptr && mProjectile->mHasData;
    }

    void SkyrimCombat::beginBowDraw(CombatRequestWriter& writer, Fighter& fighter)
    {
        if (isParalyzed(fighter)) // task 0233
            return;
        const std::string_view clip = mBowSlots.clipFor(ClipSlot::BowDraw, kWeaponAnimBow);
        if (clip.empty())
            return;
        fighter.mNextAttack = mClock + kHostileAttackPeriod;
        openeden::sdk::game::ClipRequest request;
        request.slot = ClipSlot::BowDraw;
        request.clip = clip;
        request.clipPerSex = false;
        request.blendSeconds = 0.1f;
        request.token = mNextToken++;
        if (!writer.playClip(fighter.mSubject, request))
            return;
        const float speed = fighterBowDrawSpeed(fighter.mProfile, mWeapon->mSpeed, mBowSpeedBonus);
        (void)writer.setClipSpeed(fighter.mSubject, speed);
        fighter.mBow = Fighter::Bow::Drawing;
        fighter.mBowToken = request.token;
        mSounds.weaponDraw(writer, fighter.mSubject, mWeapon); // task 0193: the bow comes up, once
        mSounds.bowNock(writer, fighter.mSubject); // task 0193
        if (mTrace)
            trace("COMBAT bow draw " + name(fighter.mSubject) + " token " + std::to_string(request.token) + " speed "
                + fmt(speed));
    }

    void SkyrimCombat::releaseBow(CombatRequestWriter& writer, Fighter& fighter, std::uint32_t)
    {
        const std::string_view clip = mBowSlots.clipFor(ClipSlot::BowRelease, kWeaponAnimBow);
        openeden::sdk::game::ClipRequest request;
        request.slot = ClipSlot::BowRelease;
        request.clip = clip;
        request.clipPerSex = false;
        request.blendSeconds = 0.05f;
        request.token = mNextToken++;
        if (clip.empty() || !writer.playClip(fighter.mSubject, request))
        {
            fighter.mBow = Fighter::Bow::None;
            return;
        }
        fighter.mBow = Fighter::Bow::Releasing;
        fighter.mBowToken = request.token;
        if (mTrace)
            trace("COMBAT bow release " + name(fighter.mSubject) + " token " + std::to_string(request.token));
    }

    bool SkyrimCombat::launchArrow(
        CombatRequestWriter& writer, Fighter& archer, float draw, const openeden::sdk::game::SubjectState* target)
    {
        openeden::sdk::game::SubjectState self;
        if (!bowReady() || !writer.subjectState(archer.mSubject, self))
            return false;
        const float fraction = std::isfinite(draw) ? std::clamp(draw, 0.f, 1.f) : 0.f;
        const float speed = mProjectile->mSpeed
            * (fraction < 1.f ? kMinDrawSpeedFraction + (1.f - kMinDrawSpeedFraction) * fraction : 1.f);
        const float gravity = std::max(0.f, mProjectile->mGravity) * kProjectileGravityUnits;
        openeden::sdk::game::ProjectileRequest request;
        request.token = mNextToken++;
        if (target != nullptr)
        {
            // An NPC aims: toward the target's chest, the elevation that makes the arc reach it.
            const double dx = target->x - self.x;
            const double dy = target->y - self.y;
            const double h0 = std::atan2(dx, dy);
            const double ox = self.x + std::sin(h0) * static_cast<double>(kBowMuzzleForward);
            const double oy = self.y + std::cos(h0) * static_cast<double>(kBowMuzzleForward);
            const double oz = self.z + static_cast<double>(kBowReleaseHeight);
            const double ax = target->x - ox;
            const double ay = target->y - oy;
            const double horizontal = std::sqrt(ax * ax + ay * ay);
            const double rise = target->z + static_cast<double>(kBowAimHeight) - oz;
            request.x = ox;
            request.y = oy;
            request.z = oz;
            request.heading = static_cast<float>(std::atan2(ax, ay));
            request.pitch = solveLaunchPitch(static_cast<float>(horizontal), static_cast<float>(rise), speed, gravity)
                                .mPitch;
        }
        else
        {
            // The player aims with its look direction, from its eye.
            request.x = self.x + std::sin(static_cast<double>(self.heading)) * static_cast<double>(kBowMuzzleForward);
            request.y = self.y + std::cos(static_cast<double>(self.heading)) * static_cast<double>(kBowMuzzleForward);
            request.z = self.z + static_cast<double>(kPlayerEyeHeight);
            request.heading = self.heading;
            request.pitch = self.pitch;
        }
        request.speed = speed;
        request.gravity = gravity;
        request.range = mProjectile->mRange;
        request.stickDepth = mProjectileSettings.mMissileStickDepth;
        request.stickSeconds = mProjectileSettings.mAttachedArrowAgeMax;
        const std::string model = projectileModelPath(mProjectile->mModel);
        request.model = model;
        if (!writer.launchProjectile(archer.mSubject, request))
            return false;
        if (mShots.size() >= kMaxShotsRemembered)
            mShots.erase(mShots.begin()); // an arrow whose end never came (its owner left): the oldest is forgotten
        mShots.push_back({ request.token, archer.mSubject, fraction });
        ++mArrowsLaunched;
        mSounds.bowRelease(writer, archer.mSubject, mWeapon); // task 0193
        if (mTrace)
            trace("COMBAT arrow launch " + name(archer.mSubject) + " token " + std::to_string(request.token) + " from "
                + fmt(static_cast<float>(request.x)) + " " + fmt(static_cast<float>(request.y)) + " "
                + fmt(static_cast<float>(request.z)) + " heading " + fmt(request.heading) + " pitch "
                + fmt(request.pitch)
                + " speed " + fmt(request.speed) + " gravity " + fmt(request.gravity) + " draw " + fmt(fraction));
        return true;
    }

    void SkyrimCombat::resolveProjectile(
        CombatRequestWriter& writer, const openeden::sdk::game::ProjectileResult& result)
    {
        if (resolveSpellShot(writer, result))
            return; // task 0186: a spell's projectile
        const auto shot = std::find_if(mShots.begin(), mShots.end(),
            [&](const Shot& s) { return s.mToken == result.token && s.mOwner == result.owner; });
        if (shot == mShots.end())
            return; // not one of ours (or already told)
        const Shot launched = *shot;
        mShots.erase(shot);
        ++mProjectileResultsSeen;
        if (mTrace)
            trace("COMBAT arrow ended " + name(result.owner) + " token " + std::to_string(result.token) + " fate "
                + std::to_string(static_cast<int>(result.fate)) + " at " + fmt(static_cast<float>(result.x)) + " "
                + fmt(static_cast<float>(result.y)) + " " + fmt(static_cast<float>(result.z)) + " after "
                + fmt(result.flightSeconds) + " s, " + fmt(result.distance) + " units");
        if (result.fate != openeden::sdk::game::ProjectileFate::HitActor
            && result.fate != openeden::sdk::game::ProjectileFate::HitPlayer)
            return;
        Fighter* archer = find(result.owner);
        Fighter* victim = result.fate == openeden::sdk::game::ProjectileFate::HitPlayer
            ? find(CombatSubject::ofPlayer())
            : find(CombatSubject::of(result.actor));
        if (archer == nullptr || victim == nullptr || archer == victim || victim->mDead || !ready(*archer)
            || !ready(*victim) || !bowReady())
            return;
        if (!mayHit(*archer, *victim))
            return; // task 0192: an arrow that ends in a friend of the archer (a crowd test) hurts nobody
        BowInput input;
        input.mBow = mWeapon;
        input.mAmmo = mAmmo;
        input.mDrawFraction = launched.mDraw;
        input.mTargetMitigationPercent = victim->mProfile.mMitigationPercent;
        if (mWeapon->skillSlot() < archer->mProfile.mSkills.size())
            input.mArcherySkill = archer->mProfile.mSkills[mWeapon->skillSlot()];
        const BowHit bow = computeBowHit(input, mCombatSettings);
        MeleeHit preset;
        preset.mDamageBefore = bow.mDamageBefore;
        preset.mDamageAfter = bow.mDamageAfter;
        preset.mStagger = bow.mStagger;
        SweepHit where;
        where.target = result.fate == openeden::sdk::game::ProjectileFate::HitPlayer ? SweepTarget::Player
                                                                                     : SweepTarget::Actor;
        where.actor = result.actor;
        where.x = result.x;
        where.y = result.y;
        where.z = result.z;
        where.distance = result.distance;
        archer->mCombatUntil = mClock + kCombatLingerSeconds;
        const bool alreadyDead = victim->mDead;
        applyHit(writer, *archer, false, false, where, *victim, &preset);
        // The arrow may end in the victim's inventory (iArrowInventoryChance percent). There is no inventory yet
        // (stage S5-9): it is counted per fighter.
        if (!alreadyDead && result.fate == openeden::sdk::game::ProjectileFate::HitActor)
        {
            const std::uint64_t seed = (static_cast<std::uint64_t>(result.token) << 32) ^ result.actor.reference;
            float chance = mProjectileSettings.mArrowInventoryChance;
            if (perksMatter(*archer) && mData->perkLibrary() != nullptr)
            {
                // Task 0179: ModRecoverArrowChance [owner].
                if (!mPerkEngine)
                    mPerkEngine = std::make_unique<PerkEngine>(*mData->perkLibrary());
                if (mPerkEngine->hasEntryPoint(archer->mProfile.mPerks, PerkEntry::ModRecoverArrowChance))
                {
                    const PerkParticipant owner = actorParticipant(*archer, false);
                    PerkQuery query;
                    query.set(PerkRole::Owner, &owner);
                    const EntryOutcome outcome
                        = mPerkEngine->apply(PerkEntry::ModRecoverArrowChance, archer->mProfile.mPerks, query, chance);
                    if (outcome.mApplied > 0)
                    {
                        chance = outcome.mValue;
                        mPerkEffectsApplied += outcome.mApplied;
                        trace("COMBAT perk arrow recover chance " + fmt(chance));
                    }
                }
            }
            if (rollPercent(seed) < chance)
            {
                ++victim->mArrowsKept;
                ++mArrowsKept;
                trace("COMBAT arrow kept by " + name(victim->mSubject) + " (inventory "
                    + std::to_string(victim->mArrowsKept) + ")");
            }
        }
    }

    void SkyrimCombat::runPlayerBow(CombatRequestWriter& writer)
    {
        Fighter& player = mFighters.front();
        if (!ready(player) || mWeapon == nullptr || mWeapon->mAnimType != kWeaponAnimBow || !bowReady())
        {
            mPlayerEdges.clear();
            mPlayerDrawing = false;
            return;
        }
        // The draw clip's bowDrawn time: the read starts with the first ask and arrives a few frames later.
        if (!(mPlayerDrawTime > 0.f))
        {
            float time = 0.f;
            const std::string_view clip = mBowSlots.clipFor(ClipSlot::BowDraw, kWeaponAnimBow);
            if (!clip.empty() && writer.clipEventTime(clip, false, openeden::sdk::game::ClipEventKind::BowDrawn, time)
                && time > 0.f)
                mPlayerDrawTime = time;
        }
        const float drawSeconds
            = mPlayerDrawTime / fighterBowDrawSpeed(player.mProfile, mWeapon->mSpeed, mBowSpeedBonus);
        // The automatic test: press, hold, release, every kPlayerBowPeriod seconds.
        if (mPlayerBowHold > 0.f && !mPlayerDrawing && mPlayerDrawTime > 0.f && mPlayerEdges.empty()
            && mClock >= mNextPlayerBow)
        {
            mPlayerEdges.push_back(true);
            mNextPlayerBow = mClock + kPlayerBowPeriod;
            mPlayerAutoReleaseAt = mClock + mPlayerBowHold;
        }
        if (mPlayerDrawing && mPlayerAutoReleaseAt >= 0.f && mClock >= mPlayerAutoReleaseAt)
        {
            mPlayerEdges.push_back(false);
            mPlayerAutoReleaseAt = -1.f;
        }
        for (const bool pressed : mPlayerEdges)
        {
            if (pressed)
            {
                if (mPlayerDrawing)
                    continue;
                if (!(mPlayerDrawTime > 0.f))
                {
                    trace("COMBAT player bow press ignored: the draw time has not arrived");
                    continue;
                }
                mPlayerDrawing = true;
                mPlayerDrawStart = mClock;
                mSounds.weaponDraw(writer, player.mSubject, mWeapon); // task 0193
                mSounds.bowNock(writer, player.mSubject); // task 0193
                trace("COMBAT player bow draw at " + fmt(mClock) + " full draw takes " + fmt(drawSeconds) + " s");
                continue;
            }
            if (!mPlayerDrawing)
                continue;
            mPlayerDrawing = false;
            const float held = mClock - mPlayerDrawStart;
            if (held < kMinPullSeconds)
            {
                trace("COMBAT player bow cancelled after " + fmt(held) + " s");
                continue;
            }
            const float fraction = drawSeconds > 0.f ? std::min(1.f, held / drawSeconds) : 1.f;
            (void)launchArrow(writer, player, fraction, nullptr);
        }
        mPlayerEdges.clear();
    }

    void SkyrimCombat::combatUpdate(float seconds, CombatRequestWriter& writer)
    {
        ++mUpdates;
        const float step = (std::isfinite(seconds) && seconds > 0.f) ? seconds : 0.f;
        const bool waiting = mData != nullptr && !mData->combatReady();
        // Task 0183: the combat clock (test schedules, regeneration, linger times) counts only the updates on which
        // the data was ready. How many updates a worker needs to read the records differs from run to run, so a clock
        // that also counted the waiting ones moved every schedule and every regeneration step with it.
        if (!waiting)
            mClock += step;
        if (waiting)
        {
            // The data is still being read on a worker: no swing, no hit this update, and nothing waits for later.
            ++mWaitingForData;
            mPendingHits.clear();
            mAwaiting.clear();
            mDeliveries.clear();
            mPendingBow.clear();
            mProjectileResults.clear();
            mPlayerEdges.clear();
            mCastEdges.clear();
            mShoutEdges.clear();
            mPlayerAttackPending = false;
        }
        else if (mData != nullptr)
        {
            if (!mHaveSettings)
                loadSettings();
            mSounds.flush(writer); // task 0193: the swings and clip sounds the events asked for
            // A sweep whose result never came (the engine drops one when its attacker has gone) is forgotten.
            std::erase_if(mAwaiting, [&](const PendingHit& p) { return mUpdates - p.mAskedAt > kSweepAnswerUpdates; });
            regenerate(step);
            applyRest(writer); // task 0210: does nothing unless a rest was queued
            flushPlayerHealthRegen(writer); // task 0238: the rate effects' share of the player's health
            flushAbilityVitals(writer); // task 0241: the engine's health maximum follows a Fortify Health
            syncProgression(writer); // task 0200
            applyPlayerHeal(writer); // task 0184
            updateMagic(writer, step); // task 0186: ticks, casts that go, the player's and the test casters' casts
            // Results cast last frame, then the hits the clips reached since.
            for (const SweepDelivery& delivery : mDeliveries)
                resolveSweep(writer, delivery);
            mDeliveries.clear();
            // processHit may push to mAwaiting but never to mPendingHits, so iterating a copy of the count is enough.
            for (std::size_t i = 0; i < mPendingHits.size(); ++i)
                processHit(writer, mPendingHits[i]);
            mPendingHits.clear();
            // Task 0178: how the arrows ended, then the bow events (draw done: release; release: the arrow flies).
            for (std::size_t i = 0; i < mProjectileResults.size(); ++i)
                resolveProjectile(writer, mProjectileResults[i]);
            mProjectileResults.clear();
            for (std::size_t i = 0; i < mPendingBow.size(); ++i)
            {
                const PendingBow pending = mPendingBow[i];
                Fighter* f = find(pending.mSubject);
                if (f == nullptr || f->mDead || f->mBowToken != pending.mToken)
                    continue;
                if (!pending.mRelease && f->mBow == Fighter::Bow::Drawing)
                    releaseBow(writer, *f, pending.mToken);
                else if (pending.mRelease && f->mBow == Fighter::Bow::Releasing)
                {
                    f->mBow = Fighter::Bow::None;
                    openeden::sdk::game::SubjectState target;
                    if (writer.subjectState(f->mAimTarget, target)) // task 0197: the player unless the AI chose
                        (void)launchArrow(writer, *f, 1.f, &target);
                }
            }
            mPendingBow.clear();
            // The named blockers' data is read even before a blow reaches them (their blocking state is then visible).
            for (const ActorHandle& handle : mBlockers)
                if (Fighter* f = find(CombatSubject::of(handle)))
                    (void)ready(*f);
            if (crowdOn() && !mCrowdChosen && mClock >= mCrowdDelay)
                chooseCrowd(writer);
            runStatuses(writer); // task 0233: paralysis, frenzy and fear end and drive here; inert with none running
            runAi(writer); // task 0190: does nothing unless `combat ai` is on, which also stops startHostiles
            startHostiles(writer);
            runPlayerSwing(writer);
            runPlayerBow(writer);
        }
        if (mSwingers.empty())
            return;
        if (mClock < mNextSwing)
            return;
        mNextSwing = mClock + kTestSwingPeriod;
        const std::string_view clip = mSlots.clipFor(ClipSlot::Attack, kWeaponAnim1HSword);
        for (const ActorHandle& handle : mSwingers)
        {
            openeden::sdk::game::ClipRequest request;
            request.slot = ClipSlot::Attack;
            request.clip = clip;
            request.clipPerSex = false;
            request.blendSeconds = 0.1f;
            request.token = mNextToken++;
            if (writer.playClip(CombatSubject::of(handle), request))
            {
                ++mSwingsRequested;
                if (Fighter* f = find(CombatSubject::of(handle)))
                {
                    f->mSwinging = true;
                    f->mSwingToken = request.token;
                }
            }
        }
    }
}

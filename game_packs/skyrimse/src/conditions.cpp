#include <game_skyrimse/conditions.hpp>

#include <array>
#include <limits>
#include <string>

namespace game_skyrimse::conditions
{
    namespace
    {
        // "CTDA" as stored (little endian).
        constexpr std::uint32_t kCtda = 'C' | ('T' << 8) | ('D' << 16) | (static_cast<std::uint32_t>('A') << 24);
        constexpr std::uint8_t kFlagOr = 0x01;
        constexpr std::uint8_t kFlagUseGlobal = 0x04;
        constexpr std::uint8_t kFlagSwapSubjectAndTarget = 0x10;
        constexpr std::uint32_t kCis1 = 'C' | ('I' << 8) | ('S' << 16) | (static_cast<std::uint32_t>('1') << 24);
        constexpr std::uint32_t kCis2 = 'C' | ('I' << 8) | ('S' << 16) | (static_cast<std::uint32_t>('2') << 24);

        // A zero-terminated string subrecord; anything after the first NUL is dropped.
        std::string cString(std::span<const std::byte> data)
        {
            std::string text(reinterpret_cast<const char*>(data.data()), data.size());
            const std::size_t nul = text.find('\0');
            if (nul != std::string::npos)
                text.resize(nul);
            return text;
        }

        float getIsId(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 && world.hasBaseId(subject, c.mParam1) ? 1.f : 0.f;
        }

        float getStage(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.questStage(c.mParam1);
        }

        float getStageDone(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.questStageDone(c.mParam1, c.mParam2) ? 1.f : 0.f;
        }

        float getInFaction(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 && world.inFaction(subject, c.mParam1) ? 1.f : 0.f;
        }

        float getGlobalValue(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.globalValue(c.mParam1);
        }

        // The functions added by the "condnames" follow-up. Names and parameter types: xEdit's table
        // (survey_names_xedit.cpp); the meanings are RECALLED-VERIFY except where the data fixes them (note 0055
        // section 2.5). A function that asks about the subject answers its "absent" value when the condition runs on
        // nobody, without asking the world.
        float asFloat(bool value)
        {
            return value ? 1.f : 0.f;
        }

        // 566 GetIsAliasRef(alias)
        float getIsAliasRef(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isAliasRef(subject, c.mParam1));
        }

        // 426 GetIsVoiceType(voice type or form list)
        float getIsVoiceType(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasVoiceType(subject, c.mParam1));
        }

        // 629 GetVMQuestVariable(quest, "variable"): the name is the CIS2 string, parameter 2 is not a value.
        float getVMQuestVariable(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.questVariable(c.mParam1, c.mString2);
        }

        // 359 GetInCurrentLoc(location)
        float getInCurrentLoc(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inLocation(subject, c.mParam1));
        }

        // 77 GetRandomPercent
        float getRandomPercent(const survey::Ctda&, Subject, const ConditionWorld& world)
        {
            return world.randomPercent();
        }

        // 47 GetItemCount(item)
        float getItemCount(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.itemCount(subject, c.mParam1) : 0.f;
        }

        // 67 GetInCell(cell)
        float getInCell(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inCell(subject, c.mParam1));
        }

        // 14 GetActorValue(actor value)
        float getActorValue(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.actorValue(subject, c.mParam1) : 0.f;
        }

        // 70 GetIsSex(sex): 0 male, 1 female (xEdit's sex enum)
        float getIsSex(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.sexOf(subject) == static_cast<int>(c.mParam1));
        }

        // 131 GetPCIsSex(sex): the same for the player, whatever the condition runs on
        float getPCIsSex(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            const Subject player = world.playerSubject();
            return asFloat(player != 0 && world.sexOf(player) == static_cast<int>(c.mParam1));
        }

        // 46 GetDead
        float getDead(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isDead(subject));
        }

        // 249 IsInDialogueWithPlayer
        float isInDialogueWithPlayer(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inDialogueWithPlayer(subject));
        }

        // 579 GetEquippedShout(shout)
        float getEquippedShout(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasEquippedShout(subject, c.mParam1));
        }

        // 543 GetQuestCompleted(quest)
        float getQuestCompleted(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return asFloat(world.questCompleted(c.mParam1));
        }

        // 560 HasKeyword(keyword)
        float hasKeyword(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasKeyword(subject, c.mParam1));
        }

        // 73 GetFactionRank(faction): -1 for a non-member (RECALLED-VERIFY)
        float getFactionRank(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.factionRank(subject, c.mParam1) : -1.f;
        }

        // 562 LocationHasKeyword(keyword)
        float locationHasKeyword(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.locationHasKeyword(subject, c.mParam1));
        }

        // 403 GetRelationshipRank(target): nobody as subject is neutral (toddhow, 2026-11-11)
        float getRelationshipRank(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.relationshipRank(subject, c.mParam1) : 0.f;
        }

        // 606 GetKeywordDataForLocation(location, keyword): names its location, so no subject (toddhow, 2026-11-11)
        float getKeywordDataForLocation(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.keywordDataForLocation(c.mParam1, c.mParam2);
        }

        // 69 GetIsRace(race), 35 GetDisabled, 286 IsSneaking, 56 GetQuestRunning(quest) (toddhow, 2026-11-11)
        float getIsRace(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isRace(subject, c.mParam1));
        }

        float getDisabled(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isDisabled(subject));
        }

        float isSneaking(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isSneaking(subject));
        }

        float getQuestRunning(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return asFloat(world.questRunning(c.mParam1));
        }

        // 310 GetInWorldspace(worldspace): exteriors only; the world answers false in an interior (CK wiki, 2026-11-11)
        float getInWorldspace(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inWorldspace(subject, c.mParam1));
        }

        // 130 GetPCIsRace(race): GetIsRace of the player, whatever the condition runs on (CK wiki, 2026-11-11)
        float getPCIsRace(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            const Subject player = world.playerSubject();
            return asFloat(player != 0 && world.isRace(player, c.mParam1));
        }

        // 182 GetEquipped(object) (CK wiki, 2026-11-11)
        float getEquipped(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isEquipped(subject, c.mParam1));
        }

        // 1 GetDistance(reference): nobody as subject is infinitely far (CK wiki, toddhow 2026-11-11)
        float getDistance(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.distanceTo(subject, c.mParam1) : std::numeric_limits<float>::max();
        }

        // 161 GetIsCurrentPackage(package) (CK wiki, 2026-11-11)
        float getIsCurrentPackage(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.runsPackage(subject, c.mParam1));
        }

        // 84 GetDeadCount(actor base): names its base, so no subject (CK wiki, 2026-11-11)
        float getDeadCount(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.deadCount(c.mParam1);
        }

        // 255 GetOffersServicesNow (CK wiki, toddhow 2026-11-11)
        float getOffersServicesNow(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.offersServicesNow(subject));
        }

        // 149 GetIsCurrentWeather(weather): names its weather, so no subject (CK wiki, toddhow 2026-11-11)
        float getIsCurrentWeather(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return asFloat(world.isCurrentWeather(c.mParam1));
        }

        // 300 IsInInterior (CK wiki, toddhow 2026-11-11)
        float isInInterior(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inInterior(subject));
        }

        // 314 IsActorAVictim: one victim at a time (CK wiki, toddhow 2026-11-11)
        float isActorAVictim(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.crimeVictim() == subject);
        }

        // 145 IsTrespassing (CK wiki, toddhow 2026-11-11)
        float isTrespassing(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isTrespassing(subject));
        }

        // 48 GetGold (CK wiki, toddhow 2026-11-11)
        float getGold(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.goldOf(subject) : 0.f;
        }

        // 605 LocAliasIsLocation(alias, location): names its alias, so no subject (toddhow, 2026-11-11)
        float locAliasIsLocation(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return asFloat(world.locAliasIs(c.mParam1, c.mParam2));
        }

        // 360 GetInCurrentLocAlias(alias) (toddhow, 2026-11-11)
        float getInCurrentLocAlias(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inLocationAlias(subject, c.mParam1));
        }

        // 453 GetPlayerTeammate (toddhow, 2026-11-11)
        float getPlayerTeammate(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isPlayerTeammate(subject));
        }

        // 596 SpellHasKeyword(casting source, keyword) (toddhow, 2026-11-11)
        float spellHasKeyword(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.spellHasKeyword(subject, c.mParam1, c.mParam2));
        }

        // 277 GetBaseActorValue(actor value) (CK wiki, toddhow 2026-11-11)
        float getBaseActorValue(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.baseActorValue(subject, c.mParam1) : 0.f;
        }

        // 50 GetTalkedToPC (CK wiki, toddhow 2026-11-11)
        float getTalkedToPC(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.talkedToPlayer(subject));
        }

        // 655 GetIntimidateSuccess (CK wiki, toddhow 2026-11-11)
        float getIntimidateSuccess(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.playerCanIntimidate(subject));
        }

        // 66 GetShouldAttack(target) (CK wiki, toddhow 2026-11-11)
        float getShouldAttack(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.attackDesire(subject, c.mParam1) : 0.f;
        }

        // 640 GetActorValuePercent(actor value): 0 to 1 (CK wiki, toddhow 2026-11-11)
        float getActorValuePercent(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.actorValuePercent(subject, c.mParam1) : 0.f;
        }

        // 459 GetCrimeGold(faction, optional): with no faction, the subject's crime faction (CK wiki, 2026-11-11)
        float getCrimeGold(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return c.mParam1 != 0 || subject != 0 ? world.crimeGold(subject, c.mParam1) : 0.f;
        }

        // 289 IsInCombat(flag) (CK wiki, toddhow 2026-11-11)
        float isInCombat(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inCombat(subject, c.mParam1 != 0));
        }

        // 49 GetSleeping: state 0 to 4 (CK wiki, toddhow 2026-11-11)
        float getSleeping(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.sleepState(subject) : 0.f;
        }

        // 264 HasSpell(spell or shout): known, not merely in effect (Papyrus Actor.HasSpell, toddhow 2026-11-11)
        float hasSpell(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.knowsSpell(subject, c.mParam1));
        }

        // 654 GetBribeSuccess: meaning inferred from GetIntimidateSuccess (CK dialog, toddhow 2026-11-11)
        float getBribeSuccess(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.playerCanBribe(subject));
        }

        // 144 GetTrespassWarningLevel: counts on the warner (CK wiki, toddhow 2026-11-11)
        float getTrespassWarningLevel(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.trespassWarnings(subject) : 0.f;
        }

        // 248 IsScenePlaying(scene): no subject (CK wiki, toddhow 2026-11-11)
        float isScenePlaying(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return asFloat(world.scenePlaying(c.mParam1));
        }

        // 101 IsWeaponMagicOut (CK wiki, toddhow 2026-11-11)
        float isWeaponMagicOut(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.weaponOrMagicOut(subject));
        }

        // 80 GetLevel (CK wiki, toddhow 2026-11-11)
        float getLevel(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.levelOf(subject) : 0.f;
        }

        // 214 HasMagicEffect(effect): "having", not "being affected by" (CK wiki, toddhow 2026-11-11)
        float hasMagicEffect(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasMagicEffect(subject, c.mParam1));
        }

        // 580 IsBleedingOut (CK wiki, toddhow 2026-11-11)
        float isBleedingOut(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isBleedingOut(subject));
        }

        // 624 GetInContainer(reference): meaning inferred from the name (CK dialog, toddhow 2026-11-11)
        float getInContainer(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inContainer(subject, c.mParam1));
        }

        // 61 GetAlarmed (CK wiki, toddhow 2026-11-11)
        float getAlarmed(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isAlarmed(subject));
        }

        // 136 GetIsReference(reference) (CK wiki, toddhow 2026-11-11)
        float getIsReference(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isReference(subject, c.mParam1));
        }

        // 159 GetSitting: state 0 to 4, 11 to 14 (CK wiki, toddhow 2026-11-11)
        float getSitting(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.sitState(subject) : 0.f;
        }

        // 237 GetIsGhost (CK wiki, toddhow 2026-11-11)
        float getIsGhost(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isGhost(subject));
        }

        // 630 GetVMScriptVariable(object, "variable"): shape as 629, RECALLED-VERIFY (CK wiki, toddhow 2026-11-11)
        float getVMScriptVariable(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return world.scriptVariable(c.mParam1, c.mString2);
        }

        // 258 HasAssociationType(actor, association type) (CK wiki, toddhow 2026-11-11)
        float hasAssociationType(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasAssociation(subject, c.mParam1, c.mParam2));
        }

        // 635 IsInFavorState (CK wiki, toddhow 2026-11-11: doing something the player asked)
        float isInFavorStateFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inFavorState(subject));
        }

        // 18 GetCurrentTime: hour as a decimal, no subject (CK wiki, toddhow 2026-11-11)
        float getCurrentTime(const survey::Ctda&, Subject, const ConditionWorld& world)
        {
            return world.gameHour();
        }

        // 610 LocAliasHasKeyword(location alias, keyword): meaning inferred (CK dialog, toddhow 2026-11-11)
        float locAliasHasKeyword(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            return asFloat(world.locAliasHasKeyword(c.mParam1, c.mParam2));
        }

        // 288 GetFriendHit(target actor) (CK wiki, toddhow 2026-11-11)
        float getFriendHit(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.friendHits(subject, c.mParam1) : 0.f;
        }

        // 415 GetGroupMemberCount = GetCombatGroupMemberCount (CK wiki, toddhow 2026-11-11)
        float getGroupMemberCount(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.combatGroupSize(subject) : 0.f;
        }

        // 594 GetIsFlying (CK wiki, toddhow 2026-11-11: flying, 0 once the landing animation ends)
        float getIsFlyingFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isFlying(subject));
        }

        // 125 IsGuard (CK wiki, toddhow 2026-11-11: the NPC's class is marked as a Guard class)
        float isGuardFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isGuardClass(subject));
        }

        // 274 IsSmallBump (CK wiki, toddhow 2026-11-11: an actor walked into the subject; false if they ran in and caused a stagger)
        float isSmallBumpFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isSmallBump(subject));
        }

        // 223 IsSpellTarget(magic item) (CK wiki, toddhow 2026-11-11)
        float isSpellTarget(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isSpellTarget(subject, c.mParam1));
        }

        // 638 IsInFriendStateWithPlayer (CK wiki, toddhow 2026-11-11: an actor that is bribed by the player, intimidated by the player, or whose relationship rank with the player is at least the iRelationshipFriendValue game setting)
        float isInFriendStateWithPlayerFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inFriendStateWithPlayer(subject));
        }

        // 25 IsMoving: direction 0 to 4 (CK wiki, toddhow 2026-11-11)
        float isMoving(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.moveDirection(subject) : 0.f;
        }

        // 449 GetFactionRelation(target actor): 0 to 3 (CK wiki, toddhow 2026-11-11)
        float getFactionRelation(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.factionRelation(subject, c.mParam1) : 0.f;
        }

        // 79 GetQuestVariable(quest, variable): deprecated, "will not work in current versions" (CK wiki, toddhow
        // 2026-11-11); Skyrim quests have no ObScript variables, so the answer is 0 (INFERRED from that note).
        float getQuestVariable(const survey::Ctda&, Subject, const ConditionWorld&)
        {
            return 0.f;
        }

        // 448 HasPerk(perk), 430 GetHealthPercentage, 491 GetMapMarkerVisible, 261 HasParentRelationship(actor)
        // (CK wiki, toddhow 2026-11-11)
        float hasPerk(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasPerk(subject, c.mParam1));
        }

        float getHealthPercentage(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.healthFraction(subject) : 0.f;
        }

        float getMapMarkerVisible(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.mapMarkerState(subject) : 0.f;
        }

        float hasParentRelationship(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isParentOf(subject, c.mParam1));
        }

        // 432 GetIsObjectType(object class) (CK wiki, toddhow 2026-11-11)
        float getIsObjectType(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isObjectType(subject, c.mParam1));
        }

        // 497 CanPayCrimeGold(faction, optional), 513 IsCombatTarget(actor), 707 GetCombatTargetHasKeyword(keyword)
        // (CK wiki, toddhow 2026-11-11)
        float canPayCrimeGold(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.canPayCrimeGold(subject, c.mParam1));
        }

        float isCombatTarget(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasCombatTarget(subject, c.mParam1));
        }

        float getCombatTargetHasKeyword(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.combatTargetHasKeyword(subject, c.mParam1));
        }

        // 499 GetDaysInJail (CK wiki, toddhow 2026-11-11)
        float getDaysInJail(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.daysInJail(subject) : 0.f;
        }

        // 698 IsAllowedToFly (CK wiki, toddhow 2026-11-11: false if not an actor, its race cannot fly, the Injured graph variable is set, allow-flying is cleared, flight is blocked, or health is exactly 0)
        float isAllowedToFlyFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isAllowedToFly(subject));
        }

        // 700 IsCommandedActor (CK wiki, toddhow 2026-11-11: raised as a zombie or summoned by a summoning spell)
        float isCommandedActorFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isCommandedActor(subject));
        }

        // 402 IsBribedbyPlayer (CK wiki, toddhow 2026-11-11: bribed by the player; the original compares a date stamp with the current date)
        float isBribedbyPlayerFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isBribedByPlayer(subject));
        }

        // 32 GetInSameCell(reference) (CK wiki, toddhow 2026-11-11)
        float getInSameCell(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.inSameCell(subject, c.mParam1));
        }

        // 375 GetCrimeGoldViolent(faction, optional), 132 GetPCInFaction(faction), 372 IsInList, 398 IsWeaponInList,
        // 444 GetInCurrentLocFormList (CK wiki, toddhow 2026-11-11)
        float getCrimeGoldViolent(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return c.mParam1 != 0 || subject != 0 ? world.crimeGoldViolent(subject, c.mParam1) : 0.f;
        }

        float getPCInFaction(const survey::Ctda& c, Subject, const ConditionWorld& world)
        {
            const Subject player = world.playerSubject();
            return asFloat(player != 0 && world.inFaction(player, c.mParam1));
        }

        float isInList(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.baseInList(subject, c.mParam1));
        }

        float isWeaponInList(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.weaponInList(subject, c.mParam1));
        }

        float getInCurrentLocFormList(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.locationInList(subject, c.mParam1));
        }

        // 590 IsInScene (CK condition dialog, toddhow 2026-11-11: no parameters, runs on the subject; meaning INFERRED from the name: the subject is taking part in a scene)
        float isInSceneFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isInScene(subject));
        }

        // 266 IsPleasant: weather, no subject (CK wiki, toddhow 2026-11-11)
        float isPleasant(const survey::Ctda&, Subject, const ConditionWorld& world)
        {
            return world.weatherPleasantness();
        }

        // 5 GetLocked (CK wiki, toddhow 2026-11-11: the object is locked)
        float getLockedFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isLocked(subject));
        }

        // 657 GetArrestingActor (CK condition dialog, toddhow 2026-11-11: no parameters, runs on the subject; meaning INFERRED from the name: the subject is the actor making the arrest)
        float getArrestingActorFn(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.isArrestingActor(subject));
        }

        // 263 IsWeaponOut, 651 GetKeywordDataForCurrentLocation, 682 WornHasKeyword, 75 IsSnowing (CK wiki, toddhow
        // 2026-11-11)
        float isWeaponOut(const survey::Ctda&, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.weaponOutState(subject) : 0.f;
        }

        float getKeywordDataForCurrentLocation(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return subject != 0 ? world.keywordDataForCurrentLocation(subject, c.mParam1) : 0.f;
        }

        float wornHasKeyword(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.wornHasKeyword(subject, c.mParam1));
        }

        float isSnowing(const survey::Ctda&, Subject, const ConditionWorld& world)
        {
            return world.snowiness();
        }

        // 376 GetCrimeGoldNonviolent, 45 GetDetected(target), 62 IsRaining, 561 HasRefType (CK wiki, toddhow
        // 2026-11-11)
        float getCrimeGoldNonviolent(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return c.mParam1 != 0 || subject != 0 ? world.crimeGoldNonviolent(subject, c.mParam1) : 0.f;
        }

        float getDetected(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.detects(subject, c.mParam1));
        }

        float isRaining(const survey::Ctda&, Subject, const ConditionWorld& world)
        {
            return world.raininess();
        }

        float hasRefType(const survey::Ctda& c, Subject subject, const ConditionWorld& world)
        {
            return asFloat(subject != 0 && world.hasRefType(subject, c.mParam1));
        }

        struct Entry
        {
            std::uint16_t mIndex;
            ConditionFunction mFunction;
        };

        // Names: xEdit's table (survey_names_xedit.cpp), the first five also upstream OpenMW's (survey_names.cpp).
        // Sorted by index.
        constexpr std::array<Entry, 110> sTable = { {
            { 1, getDistance },
            { 5, getLockedFn },
            { 14, getActorValue },
            { 18, getCurrentTime },
            { 25, isMoving },
            { 32, getInSameCell },
            { 35, getDisabled },
            { 45, getDetected },
            { 46, getDead },
            { 47, getItemCount },
            { 48, getGold },
            { 49, getSleeping },
            { 50, getTalkedToPC },
            { 56, getQuestRunning },
            { 58, getStage },
            { 59, getStageDone },
            { 61, getAlarmed },
            { 62, isRaining },
            { 66, getShouldAttack },
            { 67, getInCell },
            { 69, getIsRace },
            { 70, getIsSex },
            { 71, getInFaction },
            { 72, getIsId },
            { 73, getFactionRank },
            { 74, getGlobalValue },
            { 75, isSnowing },
            { 77, getRandomPercent },
            { 79, getQuestVariable },
            { 80, getLevel },
            { 84, getDeadCount },
            { 101, isWeaponMagicOut },
            { 125, isGuardFn },
            { 130, getPCIsRace },
            { 131, getPCIsSex },
            { 132, getPCInFaction },
            { 136, getIsReference },
            { 144, getTrespassWarningLevel },
            { 145, isTrespassing },
            { 149, getIsCurrentWeather },
            { 159, getSitting },
            { 161, getIsCurrentPackage },
            { 182, getEquipped },
            { 214, hasMagicEffect },
            { 223, isSpellTarget },
            { 237, getIsGhost },
            { 248, isScenePlaying },
            { 249, isInDialogueWithPlayer },
            { 255, getOffersServicesNow },
            { 258, hasAssociationType },
            { 261, hasParentRelationship },
            { 263, isWeaponOut },
            { 264, hasSpell },
            { 266, isPleasant },
            { 274, isSmallBumpFn },
            { 277, getBaseActorValue },
            { 286, isSneaking },
            { 288, getFriendHit },
            { 289, isInCombat },
            { 300, isInInterior },
            { 310, getInWorldspace },
            { 314, isActorAVictim },
            { 359, getInCurrentLoc },
            { 360, getInCurrentLocAlias },
            { 372, isInList },
            { 375, getCrimeGoldViolent },
            { 376, getCrimeGoldNonviolent },
            { 398, isWeaponInList },
            { 402, isBribedbyPlayerFn },
            { 403, getRelationshipRank },
            { 415, getGroupMemberCount },
            { 426, getIsVoiceType },
            { 430, getHealthPercentage },
            { 432, getIsObjectType },
            { 444, getInCurrentLocFormList },
            { 448, hasPerk },
            { 449, getFactionRelation },
            { 453, getPlayerTeammate },
            { 459, getCrimeGold },
            { 491, getMapMarkerVisible },
            { 497, canPayCrimeGold },
            { 499, getDaysInJail },
            { 513, isCombatTarget },
            { 543, getQuestCompleted },
            { 560, hasKeyword },
            { 561, hasRefType },
            { 562, locationHasKeyword },
            { 566, getIsAliasRef },
            { 579, getEquippedShout },
            { 580, isBleedingOut },
            { 590, isInSceneFn },
            { 594, getIsFlyingFn },
            { 596, spellHasKeyword },
            { 605, locAliasIsLocation },
            { 606, getKeywordDataForLocation },
            { 610, locAliasHasKeyword },
            { 624, getInContainer },
            { 629, getVMQuestVariable },
            { 630, getVMScriptVariable },
            { 635, isInFavorStateFn },
            { 638, isInFriendStateWithPlayerFn },
            { 640, getActorValuePercent },
            { 651, getKeywordDataForCurrentLocation },
            { 654, getBribeSuccess },
            { 655, getIntimidateSuccess },
            { 657, getArrestingActorFn },
            { 682, wornHasKeyword },
            { 698, isAllowedToFlyFn },
            { 700, isCommandedActorFn },
            { 707, getCombatTargetHasKeyword },
        } };
    }

    bool decodeConditions(std::span<const std::byte> recordData, std::vector<survey::Ctda>& out)
    {
        bool bad = false;
        bool lastBad = false;
        const bool wellFormed = survey::forEachSubrecord(recordData, [&](const survey::SubrecordView& sub) {
            if (sub.mType == kCis1 || sub.mType == kCis2)
            {
                // The string form of parameter 1 or 2 of the CTDA before it (xEdit, wbConditions).
                if (!out.empty() && !lastBad)
                    (sub.mType == kCis1 ? out.back().mString1 : out.back().mString2) = cString(sub.mData);
                return;
            }
            if (sub.mType != kCtda)
                return;
            survey::Ctda c;
            lastBad = !survey::decodeCtda(sub.mData, c);
            if (!lastBad)
                out.push_back(c);
            else
                bad = true;
        });
        return wellFormed && !bad;
    }

    ConditionFunction findFunction(std::uint16_t index)
    {
        for (const Entry& e : sTable)
            if (e.mIndex == index)
                return e.mFunction;
        return nullptr;
    }

    bool compareValues(std::uint8_t op, float lhs, float rhs, bool& valid)
    {
        valid = true;
        switch (op)
        {
            case 0:
                return lhs == rhs;
            case 1:
                return lhs != rhs;
            case 2:
                return lhs > rhs;
            case 3:
                return lhs >= rhs;
            case 4:
                return lhs < rhs;
            case 5:
                return lhs <= rhs;
            default:
                valid = false;
                return false;
        }
    }

    bool Evaluator::evaluate(std::span<const survey::Ctda> list, const ConditionWorld& world)
    {
        ++mLists;
        bool failedClosed = false;
        bool allClausesTrue = true;
        bool clauseTrue = false;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const survey::Ctda& c = list[i];
            ++mConditions;
            bool result = false;
            if (const ConditionFunction function = findFunction(c.mFunction))
            {
                if ((c.mFlags & kFlagSwapSubjectAndTarget) != 0)
                {
                    // Not applied (the world has no "target" to swap with): the list fails closed, counted.
                    ++mConditionsSwapped;
                    failedClosed = true;
                }
                else
                {
                    const Subject subject = world.resolveSubject(c);
                    const float lhs = function(c, subject, world);
                    const float rhs
                        = (c.mFlags & kFlagUseGlobal) != 0 ? world.globalValue(c.mComparisonBits) : c.mComparison;
                    bool valid = true;
                    result = compareValues(c.mCompare, lhs, rhs, valid);
                    if (!valid)
                    {
                        ++mConditionsBadCompare;
                        failedClosed = true;
                    }
                }
            }
            else
            {
                ++mConditionsUnknown;
                if (mUnknownFunctions[c.mFunction]++ == 0 && mLog)
                    mLog("conditions: unknown function " + survey::functionName(c.mFunction)
                        + ", lists using it evaluate to false");
                failedClosed = true;
            }
            clauseTrue = clauseTrue || result;
            if ((c.mFlags & kFlagOr) == 0 || i + 1 == list.size())
            {
                allClausesTrue = allClausesTrue && clauseTrue;
                clauseTrue = false;
            }
        }
        if (failedClosed)
            ++mListsFailedClosed;
        return !failedClosed && allClausesTrue;
    }
}

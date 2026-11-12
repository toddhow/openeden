// Names for the condition function indices that the survey met (task 0116, research note 0055).
//
// SOURCE: upstream OpenMW, components/esm4/script.hpp, enum ESM4::FunctionIndices (file last changed in commit
// 9889b225b155951b1c6effc896e6857f95886c33, read at upstream master 97722d48f98f14b323c3880a698e8ce81babf7b3).
// That file is Copyright (C) 2020-2021 cc9cii <cc9c@iinet.net.au>, under the zlib-style licence stated in its header
// (use, alteration and redistribution are free; the origin must not be misrepresented; altered versions must be
// plainly marked; the notice may not be removed). Its header says the information comes from Tes4Mod:Mod_File_Format,
// Tes5Mod:File_Formats and the FO3/FNV documentation of the UESP and TES5Edit wikis.
// ALTERED: only the name and index pairs below are taken (the "FUN_" prefix dropped), the 90 of them that are at most
// 330 and occur in the five Skyrim SE masters. This is not a copy of the file. Nothing is taken from xEdit, the
// Creation Kit wiki or CommonLib.
//
// STATUS: every name is RECALLED-VERIFY. The upstream table is the Oblivion/FO3/FNV numbering. In the survey's data
// its parameter shapes agree with Skyrim SE for indices up to about 330 (for example 72 takes an NPC_ form, 58 a QUST,
// 71 a FACT, 74 a GLOB, 69 a RACE, 67 a CELL, 310 a WRLD, 77 and 18 take none) and disagree above (362 and 403 take a
// keyword and an object where the table has none, and Skyrim's voice type and perk functions sit one below the
// table's), so no name above 330 is given.
#include <game_skyrimse/survey.hpp>

#include <algorithm>
#include <iterator>

namespace game_skyrimse::survey
{
    namespace
    {
        struct NamedFunction
        {
            std::uint16_t mIndex;
            std::string_view mName;
        };

        constexpr NamedFunction sNames[] = {
            { 1, "GetDistance" },
            { 5, "GetLocked" },
            { 6, "GetPos" },
            { 14, "GetActorValue" },
            { 18, "GetCurrentTime" },
            { 25, "IsMoving" },
            { 27, "GetLineOfSight" },
            { 32, "GetIsInSameCell" },
            { 35, "GetDisabled" },
            { 36, "GetMenuMode" },
            { 39, "GetDisease" },
            { 41, "GetClothingValue" },
            { 45, "GetDetected" },
            { 46, "GetDead" },
            { 47, "GetItemCount" },
            { 48, "GetGold" },
            { 49, "GetSleeping" },
            { 50, "GetTalkedToPC" },
            { 56, "GetQuestRunning" },
            { 58, "GetStage" },
            { 59, "GetStageDone" },
            { 61, "GetAlarmed" },
            { 62, "IsRaining" },
            { 65, "GetLockLevel" },
            { 66, "GetShouldAttack" },
            { 67, "GetInCell" },
            { 69, "GetIsRace" },
            { 70, "GetIsSex" },
            { 71, "GetInFaction" },
            { 72, "GetIsID" },
            { 73, "GetFactionRank" },
            { 74, "GetGlobalValue" },
            { 75, "IsSnowing" },
            { 77, "GetRandomPercent" },
            { 79, "GetQuestVariable" },
            { 80, "GetLevel" },
            { 84, "GetDeadCount" },
            { 91, "GetIsAlerted" },
            { 99, "GetHeadingAngle" },
            { 101, "IsWeaponOut" },
            { 102, "IsTorchOut" },
            { 103, "IsShieldOut" },
            { 108, "GetWeaponAnimType" },
            { 109, "IsWeaponSkillType" },
            { 123, "IsGreetingPlayer" },
            { 125, "IsGuard" },
            { 127, "HasBeenEaten" },
            { 130, "GetPCIsRace" },
            { 131, "GetPCIsSex" },
            { 132, "GetPCInFaction" },
            { 136, "GetIsReference" },
            { 141, "IsTalking" },
            { 143, "GetCurrentAIProcedure" },
            { 144, "GetTrespassWarningLevel" },
            { 145, "IsTrespassing" },
            { 149, "GetIsCurrentWeather" },
            { 157, "GetOpenState" },
            { 159, "GetSitting" },
            { 161, "GetIsCurrentPackage" },
            { 162, "IsCurrentFurnitureRef" },
            { 163, "IsCurrentFurnitureObj" },
            { 170, "GetDayofWeek" },
            { 180, "GetDetectionLevel" },
            { 182, "GetEquipped" },
            { 185, "IsSwimming" },
            { 203, "GetDestroyed" },
            { 214, "HasMagicEffect" },
            { 223, "IsSpellTarget" },
            { 226, "GetSandman" },
            { 227, "GetCannibal" },
            { 237, "GetIsGhost" },
            { 244, "GetRestrained" },
            { 247, "GetIsUsedItemType" },
            { 255, "GetOffersServicesNow" },
            { 258, "GetUsedItemLevel" },
            { 259, "GetUsedItemActivate" },
            { 264, "GetBarterGold" },
            { 266, "IsPleasant" },
            { 274, "GetArmorRatingUpperBody" },
            { 277, "GetBaseActorValue" },
            { 278, "IsOwner" },
            { 286, "IsSneaking" },
            { 288, "GetFriendHit" },
            { 289, "IsInCombat" },
            { 300, "IsInInterior" },
            { 310, "GetInWorldspace" },
            { 313, "IsActorEvil" },
            { 314, "IsActorAVictim" },
            { 323, "WhichServiceMenu" },
            { 327, "IsRidingHorse" },
        };
    }

    std::string_view upstreamFunctionName(std::uint16_t index)
    {
        const auto* end = std::end(sNames);
        const auto* it = std::lower_bound(std::begin(sNames), end, index,
            [](const NamedFunction& entry, std::uint16_t value) { return entry.mIndex < value; });
        return it != end && it->mIndex == index ? it->mName : std::string_view();
    }
}

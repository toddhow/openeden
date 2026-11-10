#include <game_morrowind/rules.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>

#include <apps/openmw/mwbase/dialoguemanager.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/socialrules.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/livecellref.hpp>

// Morrowind's social rules (task 0098 move 6): moved from MWMechanics::MechanicsManager::getDerivedDisposition,
// getBarterOffer and getPersuasionDispositionChange (apps/openmw/mwmechanics/mechanicsmanagerimp.cpp) with the
// arithmetic unchanged. The function-local statics keep their GMST values for the process, as before.
namespace MWMechanics::Morrowind
{
    namespace
    {
        void getPersuasionRatings(
            const MWMechanics::NpcStats& stats, float& rating1, float& rating2, float& rating3, bool player)
        {
            const MWWorld::Store<ESM::GameSetting>& gmst
                = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

            float persTerm = stats.getAttribute(ESM::Attribute::Personality).getModified()
                / gmst.find("fPersonalityMod")->mValue.getFloat();
            float luckTerm
                = stats.getAttribute(ESM::Attribute::Luck).getModified() / gmst.find("fLuckMod")->mValue.getFloat();
            float repTerm = stats.getReputation() * gmst.find("fReputationMod")->mValue.getFloat();
            float fatigueTerm = stats.getFatigueTerm();
            float levelTerm = stats.getLevel() * gmst.find("fLevelMod")->mValue.getFloat();

            rating1
                = (repTerm + luckTerm + persTerm + stats.getSkill(ESM::Skill::Speechcraft).getModified()) * fatigueTerm;

            if (player)
            {
                rating2 = rating1 + levelTerm;
                rating3 = (stats.getSkill(ESM::Skill::Mercantile).getModified() + luckTerm + persTerm) * fatigueTerm;
            }
            else
            {
                rating2 = (levelTerm + repTerm + luckTerm + persTerm
                              + stats.getSkill(ESM::Skill::Speechcraft).getModified())
                    * fatigueTerm;
                rating3 = (stats.getSkill(ESM::Skill::Mercantile).getModified() + repTerm + luckTerm + persTerm)
                    * fatigueTerm;
            }
        }

        class MorrowindSocialRules final : public SocialRules
        {
        public:
            int derivedDisposition(const MWWorld::Ptr& ptr, bool clamp) override
            {
                const MWMechanics::NpcStats& npcStats = ptr.getClass().getNpcStats(ptr);
                float x = static_cast<float>(npcStats.getBaseDisposition() + npcStats.getCrimeDispositionModifier());

                MWWorld::LiveCellRef<ESM::NPC>* npc = ptr.get<ESM::NPC>();
                MWWorld::Ptr playerPtr = getPlayer();
                MWWorld::LiveCellRef<ESM::NPC>* player = playerPtr.get<ESM::NPC>();
                const MWMechanics::NpcStats& playerStats = playerPtr.getClass().getNpcStats(playerPtr);

                const MWWorld::Store<ESM::GameSetting>& gmst
                    = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
                static const float fDispRaceMod = gmst.find("fDispRaceMod")->mValue.getFloat();
                if (npc->mBase->mRace == player->mBase->mRace)
                    x += fDispRaceMod;

                static const float fDispPersonalityMult = gmst.find("fDispPersonalityMult")->mValue.getFloat();
                static const float fDispPersonalityBase = gmst.find("fDispPersonalityBase")->mValue.getFloat();
                x += fDispPersonalityMult
                    * (playerStats.getAttribute(ESM::Attribute::Personality).getModified() - fDispPersonalityBase);

                float reaction = 0;
                int rank = 0;
                const ESM::RefId& npcFaction = ptr.getClass().getPrimaryFaction(ptr);

                if (playerStats.getFactionRanks().find(npcFaction) != playerStats.getFactionRanks().end())
                {
                    if (!playerStats.getExpelled(npcFaction))
                    {
                        // faction reaction towards itself. yes, that exists
                        reaction
                            = static_cast<float>(MWBase::Environment::get().getDialogueManager()->getFactionReaction(
                                npcFaction, npcFaction));

                        rank = playerStats.getFactionRanks().find(npcFaction)->second;
                    }
                }
                else if (!npcFaction.empty())
                {
                    auto playerFactionIt = playerStats.getFactionRanks().begin();
                    for (; playerFactionIt != playerStats.getFactionRanks().end(); ++playerFactionIt)
                    {
                        const ESM::RefId& itFaction = playerFactionIt->first;

                        // Ignore the faction, if a player was expelled from it.
                        if (playerStats.getExpelled(itFaction))
                            continue;

                        int itReaction = MWBase::Environment::get().getDialogueManager()->getFactionReaction(
                            npcFaction, itFaction);
                        if (playerFactionIt == playerStats.getFactionRanks().begin() || itReaction < reaction)
                        {
                            reaction = static_cast<float>(itReaction);
                            rank = playerFactionIt->second;
                        }
                    }
                }
                else
                {
                    reaction = 0;
                    rank = 0;
                }

                static const float fDispFactionRankMult = gmst.find("fDispFactionRankMult")->mValue.getFloat();
                static const float fDispFactionRankBase = gmst.find("fDispFactionRankBase")->mValue.getFloat();
                static const float fDispFactionMod = gmst.find("fDispFactionMod")->mValue.getFloat();
                x += (fDispFactionRankMult * rank + fDispFactionRankBase) * fDispFactionMod * reaction;

                static const float fDispCrimeMod = gmst.find("fDispCrimeMod")->mValue.getFloat();
                static const float fDispDiseaseMod = gmst.find("fDispDiseaseMod")->mValue.getFloat();
                x -= fDispCrimeMod * playerStats.getBounty();
                if (playerStats.hasCommonDisease() || playerStats.hasBlightDisease())
                    x += fDispDiseaseMod;

                static const float fDispWeaponDrawn = gmst.find("fDispWeaponDrawn")->mValue.getFloat();
                if (playerStats.getDrawState() == MWMechanics::DrawState::Weapon)
                    x += fDispWeaponDrawn;

                x += ptr.getClass()
                         .getCreatureStats(ptr)
                         .getMagicEffects()
                         .getOrDefault(ESM::MagicEffect::Charm)
                         .getMagnitude();

                if (clamp)
                    return std::clamp(static_cast<int>(x), 0, 100); //, normally clamped to [0..100] when used
                return static_cast<int>(x);
            }

            int barterOffer(const MWWorld::Ptr& ptr, int basePrice, bool buying) override
            {
                // Make sure zero base price items/services can't be bought/sold for 1 gold
                // and return the intended base price for creature merchants
                if (basePrice == 0 || ptr.getType() == ESM::Creature::sRecordId)
                    return basePrice;

                const MWMechanics::NpcStats& sellerStats = ptr.getClass().getNpcStats(ptr);

                MWWorld::Ptr playerPtr = getPlayer();
                const MWMechanics::NpcStats& playerStats = playerPtr.getClass().getNpcStats(playerPtr);

                // I suppose the temporary disposition change (second param to getDerivedDisposition()) _has_ to be
                // considered here, otherwise one would get different prices when exiting and re-entering the dialogue
                // window...
                int clampedDisposition = derivedDisposition(ptr, true);
                float a = std::min(playerPtr.getClass().getSkill(playerPtr, ESM::Skill::Mercantile), 100.f);
                float b = std::min(0.1f * playerStats.getAttribute(ESM::Attribute::Luck).getModified(), 10.f);
                float c = std::min(0.2f * playerStats.getAttribute(ESM::Attribute::Personality).getModified(), 10.f);
                float d = std::min(ptr.getClass().getSkill(ptr, ESM::Skill::Mercantile), 100.f);
                float e = std::min(0.1f * sellerStats.getAttribute(ESM::Attribute::Luck).getModified(), 10.f);
                float f = std::min(0.2f * sellerStats.getAttribute(ESM::Attribute::Personality).getModified(), 10.f);
                float pcTerm = (clampedDisposition - 50 + a + b + c) * playerStats.getFatigueTerm();
                float npcTerm = (d + e + f) * sellerStats.getFatigueTerm();
                float buyTerm = 0.01f * (100 - 0.5f * (pcTerm - npcTerm));
                float sellTerm = 0.01f * (50 - 0.5f * (npcTerm - pcTerm));
                int offerPrice = int(basePrice * (buying ? buyTerm : sellTerm));
                return std::max(1, offerPrice);
            }

            void persuasionDispositionChange(const MWWorld::Ptr& npc, MWBase::MechanicsManager::PersuasionType type,
                bool& success, int& tempChange, int& permChange) override
            {
                const MWWorld::Store<ESM::GameSetting>& gmst
                    = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

                MWMechanics::NpcStats& npcStats = npc.getClass().getNpcStats(npc);

                MWWorld::Ptr playerPtr = getPlayer();
                const MWMechanics::NpcStats& playerStats = playerPtr.getClass().getNpcStats(playerPtr);

                float npcRating1, npcRating2, npcRating3;
                getPersuasionRatings(npcStats, npcRating1, npcRating2, npcRating3, false);

                float playerRating1, playerRating2, playerRating3;
                getPersuasionRatings(playerStats, playerRating1, playerRating2, playerRating3, true);

                const int currentDisposition = derivedDisposition(npc, true);

                float d = 1 - 0.02f * abs(currentDisposition - 50);
                float target1 = d * (playerRating1 - npcRating1 + 50);
                float target2 = d * (playerRating2 - npcRating2 + 50);

                float bribeMod;
                if (type == MWBase::MechanicsManager::PT_Bribe10)
                    bribeMod = gmst.find("fBribe10Mod")->mValue.getFloat();
                else if (type == MWBase::MechanicsManager::PT_Bribe100)
                    bribeMod = gmst.find("fBribe100Mod")->mValue.getFloat();
                else
                    bribeMod = gmst.find("fBribe1000Mod")->mValue.getFloat();

                float target3 = d * (playerRating3 - npcRating3 + 50) + bribeMod;

                const float iPerMinChance = gmst.find("iPerMinChance")->mValue.getFloat();
                const float iPerMinChange = gmst.find("iPerMinChange")->mValue.getFloat();
                const float fPerDieRollMult = gmst.find("fPerDieRollMult")->mValue.getFloat();
                const float fPerTempMult = gmst.find("fPerTempMult")->mValue.getFloat();

                float x = 0;
                float y = 0;

                auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                int roll = Misc::Rng::roll0to99(prng);

                if (type == MWBase::MechanicsManager::PT_Admire)
                {
                    target1 = std::max(iPerMinChance, target1);
                    success = (roll <= target1);
                    float c = floor(fPerDieRollMult * (target1 - roll));
                    x = success ? std::max(iPerMinChange, c) : c;
                }
                else if (type == MWBase::MechanicsManager::PT_Intimidate)
                {
                    target2 = std::max(iPerMinChance, target2);

                    success = (roll <= target2);

                    float r;
                    if (roll != target2)
                        r = floor(target2 - roll);
                    else
                        r = 1;

                    if (roll <= target2)
                    {
                        float s = floor(r * fPerDieRollMult * fPerTempMult);

                        const int flee = npcStats.getAiSetting(MWMechanics::AiSetting::Flee).getBase();
                        const int fight = npcStats.getAiSetting(MWMechanics::AiSetting::Fight).getBase();
                        npcStats.setAiSetting(
                            MWMechanics::AiSetting::Flee, std::clamp(flee + int(std::max(iPerMinChange, s)), 0, 100));
                        npcStats.setAiSetting(MWMechanics::AiSetting::Fight,
                            std::clamp(fight + int(std::min(-iPerMinChange, -s)), 0, 100));
                    }

                    float c = -std::abs(floor(r * fPerDieRollMult));
                    if (success)
                    {
                        if (std::abs(c) < iPerMinChange)
                        {
                            // Deviating from Morrowind here: it doesn't increase disposition on marginal wins,
                            // which seems to be a bug (MCP fixes it too).
                            // Original logic: x = 0, y = -iPerMinChange
                            x = iPerMinChange;
                            y = x; // This goes unused.
                        }
                        else
                        {
                            x = -floor(c * fPerTempMult);
                            y = c;
                        }
                    }
                    else
                    {
                        x = floor(c * fPerTempMult);
                        y = c;
                    }
                }
                else if (type == MWBase::MechanicsManager::PT_Taunt)
                {
                    target1 = std::max(iPerMinChance, target1);
                    success = (roll <= target1);

                    float c = std::abs(floor(target1 - roll));

                    if (success)
                    {
                        float s = c * fPerDieRollMult * fPerTempMult;
                        const int flee = npcStats.getAiSetting(AiSetting::Flee).getBase();
                        const int fight = npcStats.getAiSetting(AiSetting::Fight).getBase();
                        npcStats.setAiSetting(
                            AiSetting::Flee, std::clamp(flee + std::min(-int(iPerMinChange), int(-s)), 0, 100));
                        npcStats.setAiSetting(
                            AiSetting::Fight, std::clamp(fight + std::max(int(iPerMinChange), int(s)), 0, 100));
                    }
                    x = floor(-c * fPerDieRollMult);

                    if (success && std::abs(x) < iPerMinChange)
                        x = -iPerMinChange;
                }
                else // Bribe
                {
                    target3 = std::max(iPerMinChance, target3);
                    success = (roll <= target3);
                    float c = floor((target3 - roll) * fPerDieRollMult);

                    x = success ? std::max(iPerMinChange, c) : c;
                }

                if (type == MWBase::MechanicsManager::PT_Intimidate)
                {
                    tempChange = int(x);
                    if (currentDisposition + tempChange > 100)
                        tempChange = 100 - currentDisposition;
                    else if (currentDisposition + tempChange < 0)
                        tempChange = -currentDisposition;
                    permChange = success ? -int(tempChange / fPerTempMult) : int(y);
                }
                else
                {
                    tempChange = int(x * fPerTempMult);
                    if (currentDisposition + tempChange > 100)
                        tempChange = 100 - currentDisposition;
                    else if (currentDisposition + tempChange < 0)
                        tempChange = -currentDisposition;
                    permChange = int(tempChange / fPerTempMult);
                }
            }
        };
    }
}

namespace game_morrowind
{
    std::shared_ptr<MWMechanics::SocialRules> makeSocialRules()
    {
        return std::make_shared<MWMechanics::Morrowind::MorrowindSocialRules>();
    }
}

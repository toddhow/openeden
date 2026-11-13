#include "game_skyrimse/speech.hpp"

#include <algorithm>
#include <utility>

namespace game_skyrimse
{
    namespace
    {
        // A response whose fragment script refers to one of these functions is that service. The first name found in
        // the order of this table wins. Names are lower case, as the .pex string table is compared lower-cased.
        // SETTLED BY DATA (task 0155 stage log): which of these occur in the fragment scripts of the five masters.
        struct ServiceName
        {
            std::string_view mFunction;
            std::string_view mService;
        };

        constexpr ServiceName kServices[] = {
            { "showbartermenu", "barter" },
            { "showgiftmenu", "gift" },
            { "showtrainingmenu", "training" },
            // The inn rooms' fragments call into the quest script RentRoomScript (4 scripts in the five masters).
            { "rentroomscript", "rent a room" },
        };
    }

    std::optional<std::size_t> skillSlotOfActorValue(std::uint32_t actorValue)
    {
        if (actorValue < kAvOneHanded || actorValue > kAvEnchanting)
            return std::nullopt;
        return static_cast<std::size_t>(actorValue - kAvOneHanded);
    }

    float actorValueOfPlayer(const PlayerStandIn& player, std::uint32_t actorValue)
    {
        if (const std::optional<std::size_t> slot = skillSlotOfActorValue(actorValue))
            return static_cast<float>(player.mSkills[*slot]);
        switch (actorValue)
        {
            case kAvHealth:
                return player.mHealth;
            case kAvMagicka:
                return player.mMagicka;
            case kAvStamina:
                return player.mStamina;
            default:
                return 0.f;
        }
    }

    float actorValueOfActor(const openeden::sdk::game::ActorStats& stats, std::uint32_t actorValue)
    {
        if (const std::optional<std::size_t> slot = skillSlotOfActorValue(actorValue))
            return static_cast<float>(stats.skills[*slot]);
        switch (actorValue)
        {
            case kAvHealth:
                return stats.health;
            case kAvMagicka:
                return stats.magicka;
            case kAvStamina:
                return stats.stamina;
            default:
                return 0.f;
        }
    }

    bool intimidateSucceeds(std::uint16_t playerLevel, std::uint16_t speakerLevel)
    {
        return playerLevel >= speakerLevel;
    }

    bool bribeSucceeds(float playerGold, std::uint16_t speakerLevel)
    {
        return playerGold >= kBribeGoldPerLevel * static_cast<float>(std::max<std::uint16_t>(speakerLevel, 1));
    }

    std::string classifyService(const std::vector<std::string>& referencedNames)
    {
        for (const ServiceName& service : kServices)
        {
            for (const std::string& name : referencedNames)
            {
                if (name == service.mFunction)
                    return std::string(service.mService);
            }
        }
        return std::string();
    }
}

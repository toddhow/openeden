#include <game_skyrimse/combat.hpp>

namespace game_skyrimse
{
    void SkyrimCombat::onClipEvent(const openeden::sdk::game::ClipEvent& event)
    {
        (void)event;
        ++mRefusedEvents;
    }

    void SkyrimCombat::onSweepResult(openeden::sdk::game::CombatSubject subject, std::uint32_t token,
        const openeden::sdk::game::SweepHit* hits, std::size_t count)
    {
        (void)subject;
        (void)token;
        (void)hits;
        (void)count;
        ++mRefusedSweepResults;
    }

    void SkyrimCombat::combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer)
    {
        (void)seconds;
        (void)writer;
        ++mUpdates;
    }
}

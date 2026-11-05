#include "terrainfade.hpp"

#include <cassert>

namespace Lod
{
    bool terrainTileIsSolid(const TerrainFadeState& state)
    {
        return state.mFade.mPhase == FadePhase::Solid || state.mFade.mPhase == FadePhase::WaitingForLive;
    }

    namespace
    {
        void updateCovers(TerrainFadeState& state)
        {
            state.mCovers = terrainTileIsSolid(state);
        }

        // Steps one tile and reports the start of a fade out or its end.
        TerrainFadeEvent stepOne(TerrainFadeState& state, bool wanted, float seconds, float fadeSeconds)
        {
            const FadePhase before = state.mFade.mPhase;
            // `liveReady` is true: when the tile is not wanted the caller has already decided that it may go.
            stepTileFade(state.mFade, wanted, true, seconds, fadeSeconds);
            if (state.mFade.mPhase == FadePhase::WaitingForLive)
            {
                // A tile that may go does not wait a frame for the live terrain: step it into the fade out now.
                stepTileFade(state.mFade, wanted, true, seconds, fadeSeconds);
            }
            const FadePhase after = state.mFade.mPhase;
            if (after == FadePhase::Hidden)
            {
                // Hidden after being drawn means a fade out has just ended.
                if (before != FadePhase::Hidden)
                {
                    state = TerrainFadeState{};
                    return TerrainFadeEvent::Finished;
                }
                return TerrainFadeEvent::None;
            }
            if (after == FadePhase::FadingOut && before != FadePhase::FadingOut)
                return TerrainFadeEvent::StartedLeaving;
            return TerrainFadeEvent::None;
        }
    }

    void stepTerrainFades(std::span<TerrainFadeState> states, std::span<const TerrainFadeInput> inputs,
        std::span<TerrainFadeEvent> events, float seconds, float fadeSeconds)
    {
        assert(states.size() == inputs.size() && states.size() == events.size());
        assert(fadeSeconds > 0.f);
        const std::size_t count = states.size();
        for (TerrainFadeEvent& event : events)
            event = TerrainFadeEvent::None;

        // First the tiles the selection chooses and the refined ones: a tile that covers others is stepped before them.
        for (std::size_t i = 0; i < count; ++i)
        {
            TerrainFadeState& state = states[i];
            const TerrainFadeInput& input = inputs[i];
            if (!input.mBuilt)
            {
                state = TerrainFadeState{};
                continue;
            }
            if (input.mChoice == TileChoice::Covered)
                continue;
            bool wanted = input.mChoice == TileChoice::Tile;
            if (!wanted)
            {
                // Live, or a choice that was never made (Unset): the tile goes unless something depends on it.
                const RefinedGate gate = input.mChoice == TileChoice::Live ? input.mGate : RefinedGate::ToLive;
                if (gate == RefinedGate::Covered)
                {
                    state = TerrainFadeState{};
                    events[i] = TerrainFadeEvent::Remove;
                    continue;
                }
                wanted = gate == RefinedGate::Stay;
            }
            events[i] = stepOne(state, wanted, seconds, fadeSeconds);
            updateCovers(state);
        }
        // Then the tiles under a drawn ancestor: they stay until the ancestor is solid, then they go at once.
        for (std::size_t i = 0; i < count; ++i)
        {
            const TerrainFadeInput& input = inputs[i];
            if (!input.mBuilt || input.mChoice != TileChoice::Covered)
                continue;
            TerrainFadeState& state = states[i];
            std::int32_t ancestor = input.mParent;
            while (ancestor >= 0 && inputs[static_cast<std::size_t>(ancestor)].mChoice != TileChoice::Tile)
                ancestor = inputs[static_cast<std::size_t>(ancestor)].mParent;
            if (ancestor >= 0 && inputs[static_cast<std::size_t>(ancestor)].mBuilt
                && terrainTileIsSolid(states[static_cast<std::size_t>(ancestor)]))
            {
                state = TerrainFadeState{};
                events[i] = TerrainFadeEvent::Remove;
                continue;
            }
            // Not yet: it stays wanted (a tile that was fading in goes on and reaches solid).
            events[i] = stepOne(state, true, seconds, fadeSeconds);
            updateCovers(state);
        }
    }
}

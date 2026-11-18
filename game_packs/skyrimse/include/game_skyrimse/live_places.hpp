// Task 0313: where actors are NOW, for the Papyrus host. A script's GetDistance and GetPosition read the data's placement
// of a reference, which for a dead dragon that fell (or any actor that walked) is not where it is. The first ask for a
// reference watches it; the place is read from the core once an update from then on (a handful of references, never
// every actor). Off by default; the cell source owns one and the pack turns it on with `main quest whiterun`.
#pragma once

#include <openeden/game/actor_rules.hpp>

#include <cstddef>
#include <map>

namespace game_skyrimse
{
    class LivePlaces
    {
    public:
        static constexpr std::size_t kMaxWatched = 64;

        void setOn(bool on) { mOn = on; }
        [[nodiscard]] bool on() const { return mOn; }
        [[nodiscard]] std::size_t watched() const { return mPlaces.size(); }

        /// The last place read for the reference; marks it watched on the first ask. False while off, while no place
        /// has been read yet (the next update reads it), for an unknown actor and once 64 references are watched.
        [[nodiscard]] bool placeOf(openeden::sdk::game::WorldId reference, double& x, double& y, double& z)
        {
            if (!mOn)
                return false;
            auto found = mPlaces.find(reference);
            if (found == mPlaces.end())
            {
                if (mPlaces.size() >= kMaxWatched)
                    return false;
                found = mPlaces.emplace(reference, Place{}).first;
            }
            if (!found->second.mKnown)
                return false;
            x = found->second.mX;
            y = found->second.mY;
            z = found->second.mZ;
            return true;
        }

        /// Reads the places of the watched references. `handleOf` is the actor table's lookup (an invalid handle: not an
        /// actor, or not loaded: the last place read is kept).
        template <class HandleOf>
        void update(HandleOf&& handleOf, const openeden::sdk::game::ActorRequestWriter& writer)
        {
            if (!mOn)
                return;
            for (auto& [reference, place] : mPlaces)
            {
                const openeden::sdk::game::ActorHandle handle = handleOf(reference);
                if (!handle.valid())
                    continue;
                double x = 0.0, y = 0.0, z = 0.0;
                if (!writer.actorPosition(handle, x, y, z))
                    continue;
                place.mX = x;
                place.mY = y;
                place.mZ = z;
                place.mKnown = true;
            }
        }

    private:
        struct Place
        {
            double mX = 0.0, mY = 0.0, mZ = 0.0;
            bool mKnown = false;
        };
        bool mOn = false;
        std::map<openeden::sdk::game::WorldId, Place> mPlaces;
    };
}

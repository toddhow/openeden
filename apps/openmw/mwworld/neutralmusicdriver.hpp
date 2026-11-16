#ifndef OPENMW_MWWORLD_NEUTRALMUSICDRIVER_H
#define OPENMW_MWWORLD_NEUTRALMUSICDRIVER_H

// Task 0209: ties the NeutralMusicDirector to a session: finds the game pack's MusicSource, the player's cell and the
// sound manager. Only made with [Game] neutral world and a pack that has its music switched on.

#include <memory>

namespace MWWorld
{
    class Ptr;

    class NeutralMusicDriver
    {
    public:
        /// Null when the session has no game pack with music on.
        static std::unique_ptr<NeutralMusicDriver> make();

        ~NeutralMusicDriver();
        NeutralMusicDriver(const NeutralMusicDriver&) = delete;
        NeutralMusicDriver& operator=(const NeutralMusicDriver&) = delete;

        /// Main thread, once a frame the world is not paused. `gameHour` is the game's hour of the day.
        void update(float dt, const Ptr& player, float gameHour);

    private:
        struct Impl;
        explicit NeutralMusicDriver(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };
}

#endif

#ifndef OPENMW_MWWORLD_NEUTRALPLAYERNAME_H
#define OPENMW_MWWORLD_NEUTRALPLAYERNAME_H

// Task 0228: the name a game pack's player chose goes into the engine's player record (the save list, the HUD and the
// dialogue read it there). The pack's PlayerIdentitySource says the name and a revision; the core asks every frame and
// acts only when the revision changed.

#include <cstdint>
#include <memory>
#include <string>

#include <openeden/game/player_identity.hpp>

namespace MWWorld
{
    /// The decision, with no engine type: true with `name` when the source's revision is new and not 0.
    class NeutralPlayerNameSync
    {
    public:
        [[nodiscard]] bool poll(openeden::sdk::game::PlayerIdentitySource& source, std::string& name);

    private:
        std::uint32_t mSeen = 0;
    };

    /// Finds the game pack's source and writes the name through the mechanics manager. Only made with [Game] neutral
    /// world; null otherwise.
    class NeutralPlayerNameDriver
    {
    public:
        static std::unique_ptr<NeutralPlayerNameDriver> make();
        ~NeutralPlayerNameDriver();
        NeutralPlayerNameDriver(const NeutralPlayerNameDriver&) = delete;
        NeutralPlayerNameDriver& operator=(const NeutralPlayerNameDriver&) = delete;

        /// Main thread, every frame (also while the world is paused: the creation window pauses it).
        void update();

    private:
        NeutralPlayerNameDriver() = default;
        NeutralPlayerNameSync mSync;
        std::string mGameId;
        std::uint32_t mLookupFrames = 0;
    };
}

#endif

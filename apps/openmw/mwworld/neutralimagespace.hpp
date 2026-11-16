#ifndef OPENMW_MWWORLD_NEUTRALIMAGESPACE_H
#define OPENMW_MWWORLD_NEUTRALIMAGESPACE_H

// Task 0257: a game pack's effects can change the picture (Skyrim's Night Eye). The pack's PlayerImageSpaceSource says
// the multiply and add of each value; the core polls it every frame and hands the renderer a
// Render::ImageSpace::Modifier (which `Render::ImageSpace::combine` lays over the weather's image space). With no active
// modifier the renderer is told "inactive", which is what it holds without this feature.

#include <memory>
#include <string>

#include <components/render/imagespacemath.hpp>

#include <openeden/game/player_image_space.hpp>

namespace MWRender
{
    class RenderManager;
}

namespace MWWorld
{
    /// The conversion, with no engine type: a source that has nothing (false), or says `active` false, gives the
    /// inactive modifier. A value that is not a number is left for `combine`, which treats it as the neutral one.
    [[nodiscard]] Render::ImageSpace::Modifier neutralModifierFrom(openeden::sdk::game::PlayerImageSpaceSource& source);

    /// Finds the game pack's source and tells the renderer. Only made with [Game] neutral world and
    /// [Game] neutral image space effects; null otherwise.
    class NeutralImageSpaceDriver
    {
    public:
        static std::unique_ptr<NeutralImageSpaceDriver> make();
        ~NeutralImageSpaceDriver();
        NeutralImageSpaceDriver(const NeutralImageSpaceDriver&) = delete;
        NeutralImageSpaceDriver& operator=(const NeutralImageSpaceDriver&) = delete;

        /// Main thread, every frame (also while the world is paused).
        void update(MWRender::RenderManager& rendering);

    private:
        NeutralImageSpaceDriver() = default;
        std::string mGameId;
        unsigned mLookupFrames = 0;
        bool mWasActive = false;
    };
}

#endif

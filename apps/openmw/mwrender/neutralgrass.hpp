#ifndef OPENEDEN_MWRENDER_NEUTRALGRASS_H
#define OPENEDEN_MWRENDER_NEUTRALGRASS_H

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>

#include <openeden/game/grass_source.hpp>

#include "groundcoverchunks.hpp"
#include "groundcoverinstances.hpp"

namespace vsg
{
    class OperationThreads;
}

namespace MWRender
{
    /// Skyrim's grass blade textures are authored about twice as bright as the ground textures they stand on
    /// (task 0153 follow-up, measured on the installed data: over the 31 pairs of a ground texture and a grass model
    /// its LTEX names, the median of the ground texture's mean luminance divided by the grass texture's opaque
    /// luminance is 0.51, mean 0.53), and the grass models' vertex colours already dim them to about 0.8 (mean over
    /// the six models traced). The lighting is the same for both, so the model reader multiplies each tuft's vertex
    /// colours and material by 0.51 / 0.8, rounded, to bring the grass to the ground's tone. 1 changes nothing.
    inline constexpr float kGrassAlbedoScale = 0.65f;
    float grassAlbedoScale();
    /// makeGroundcoverContext's context with the grass albedo scale applied through a Pipeline::Override materialData
    /// hook.
    MWAnim::Context makeNeutralGrassContext(const MWAnim::Context& base);

    /// The tufts of one grid square as groundcover instances (task 0153): one list per model path, in the pack's order,
    /// each with the tuft's world position, its yaw as the Euler rotation about Z and its scale. Tufts naming a model
    /// the cell does not list, or with a position that is not finite, are dropped. Pure.
    GroundcoverInstances groundcoverFromGrass(const openeden::sdk::game::GrassCell& cell);

    /**
     * @brief Grass on a game pack's exterior (task 0153, `[Game] neutral grass`): the pack places the tufts of each
     * grid square (openeden::sdk::game::GrassSource), and this draws them through groundcover's instanced path
     * (buildGroundcoverChunk: wind and stomp in the vertex stage, fade beyond `[Groundcover] rendering distance`).
     *
     * One chunk per grid square within the rendering distance of the camera, found again after the camera has moved an
     * eighth of a square. The pack is asked on the MAIN thread (a source is main thread only), at most
     * kCellsPerUpdate squares per update, nearest first; the models are read and assembled on this object's own worker
     * thread, then compiled and shown by a later update (the chunk tracker of Groundcover, with a cache of built
     * chunks). Chunks leaving the scene go through the compile context's detach.
     *
     * Owned by RenderManager; every member function runs on the main thread. The source must outlive the calls to
     * update(); the destructor and waitForBuilds() wait for the worker.
     */
    class NeutralGrass
    {
    public:
        /// The squares asked of the pack in one update.
        static constexpr std::size_t kCellsPerUpdate = 1;

        struct Stats
        {
            std::uint64_t mCellsAsked = 0; ///< squares the pack was asked for
            std::uint64_t mCellsWithGrass = 0;
            std::uint64_t mTufts = 0; ///< instances handed to the renderer
            std::uint64_t mTuftsRequested = 0; ///< the pack's upper bound before its ground test and cap
            double mPackMilliseconds = 0.0; ///< main thread time in grassForCell
            double mWorstPackMilliseconds = 0.0;
        };

        NeutralGrass(const MWAnim::Context& context, openeden::sdk::game::GrassSource& source,
            openeden::sdk::game::WorldId world, float cellSize, float renderingDistance);
        ~NeutralGrass();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        void update(const vsg::vec3& cameraPosition);
        /// Hides every chunk (in interiors) and keeps the built ones for the next update().
        void hide();
        void waitForBuilds();

        const Stats& stats() const { return mStats; }

    private:
        struct ChunkBuild;
        using Tracker = GroundcoverChunkTracker<vsg::ref_ptr<vsg::Node>>;

        bool collectBuilds();
        void askPack(const vsg::vec2& camera);
        void setShownChunks(vsg::Group::Children chunks);

        MWAnim::Context mContext;
        openeden::sdk::game::GrassSource& mSource;
        openeden::sdk::game::WorldId mWorld;
        float mCellSize;
        float mRenderingDistance;
        vsg::ref_ptr<vsg::OperationThreads> mThreads;
        vsg::ref_ptr<vsg::Group> mRoot;
        Tracker mTracker;
        std::vector<std::pair<int, int>> mWanted; // sorted
        std::deque<std::pair<int, int>> mQueue; // squares to ask the pack for, nearest first
        std::vector<vsg::ref_ptr<ChunkBuild>> mBuilds;
        vsg::vec2 mLastUpdatePosition;
        bool mHasUpdated = false;
        Stats mStats;
    };
}

#endif

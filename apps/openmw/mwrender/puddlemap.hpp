#ifndef OPENMW_MWRENDER_PUDDLEMAP_H
#define OPENMW_MWRENDER_PUDDLEMAP_H

// OpenEden (task 0095, stage W-d): the run-time side of the baked puddle map. Opens <user data>/openeden/puddles/
// puddles.edenpak on a worker thread, keeps the 7 x 7 cells round the camera loaded (Puddle::PuddleWindow: cells baked
// from other land or with other settings are ignored, with one warning) and packs them into the words the terrain
// shader reads (components/puddle/puddlegpu.hpp, layout in components/render/puddlemapgpu.hpp). The render thread
// tells it the camera's cell each frame (a cheap call) and takes a finished buffer when there is one.
//
// Ownership: RenderManager owns one of these (a unique_ptr) while `wetness puddle map` is on; the destructor stops and
// joins the worker. Threads: the constructor, `setCamera` and `takeNewest` are for the render (main) thread; the worker
// reads the pack and the land (Terrain::Storage through StorageHeights, which the terrain's own worker threads use
// too) and writes only its own buffer; the two sides meet under one mutex. Nothing allocates per frame: `takeNewest`
// copies into the GPU buffer's memory, and only when a new window was packed.

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MWWorld
{
    class ESMStore;
}

namespace VFS
{
    class Manager;
}

namespace MWRender
{
    class TerrainStorage;

    class PuddleMapRuntime
    {
    public:
        struct Setup
        {
            std::filesystem::path mPackPath;
            TerrainStorage* mStorage = nullptr;
            const MWWorld::ESMStore* mStore = nullptr;
            /// Needed for a pack baked with static surfaces: the staleness check hashes the models the references name.
            const VFS::Manager* mVfs = nullptr;
            /// Water in world units of depth that fully wet ground has had, and what the ground absorbs first.
            float mSupply = 0.5f;
            float mAbsorption = 0.1f;
            /// A Render::PuddleMap::DebugMode.
            std::uint32_t mDebugMode = 0;
            /// `wetness puddle static noise` (stage W-c).
            bool mKeepStaticNoise = true;
        };

        explicit PuddleMapRuntime(Setup setup);
        ~PuddleMapRuntime();
        PuddleMapRuntime(const PuddleMapRuntime&) = delete;
        PuddleMapRuntime& operator=(const PuddleMapRuntime&) = delete;

        /// The cell the camera is in (render thread, every frame; the worker is woken only when it changes).
        void setCamera(std::int32_t cellX, std::int32_t cellY);

        /// If the worker has packed a window since the last call, copies it into `words` (at most `count` words) and
        /// returns true; the caller then marks the GPU buffer dirty.
        bool takeNewest(std::uint32_t* words, std::size_t count);

        /// True once the worker has opened a usable pack (for the log and the tests).
        bool packLoaded() const;

    private:
        void run();

        Setup mSetup;
        mutable std::mutex mMutex;
        std::condition_variable mWake;
        bool mStop = false;
        bool mHaveRequest = false;
        std::int32_t mRequestX = 0;
        std::int32_t mRequestY = 0;
        std::vector<std::uint32_t> mReady;
        bool mReadyNew = false;
        bool mPackLoaded = false;
        std::thread mThread;
    };
}

#endif

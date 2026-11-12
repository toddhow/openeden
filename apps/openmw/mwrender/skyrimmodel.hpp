#ifndef OPENMW_MWRENDER_SKYRIMMODEL_HPP
#define OPENMW_MWRENDER_SKYRIMMODEL_HPP

// Task 0110 (neutral world stage N3a-4): the second half of drawing a Skyrim SE model in the game. It lives in the host
// layer because it needs both the format (components/nifscene) and the renderer (components/pipeline, vsgutil), and the
// layer rules keep those apart below it. A FlatModel (components/nifscene/flatten.hpp: static shapes with their
// materials reduced to a key) becomes a VSG scene graph through the engine's own Pipeline::Builder: one StateGroup per
// shape (graphics pipeline from the key, a descriptor set with the diffuse texture read through the VFS and a
// material), one VertexIndexDraw with 32-bit indices, a DepthSorted wrapper for blended shapes, and the usual cull
// nodes. Skinned shapes, animation, effects and every map but the diffuse one are not part of this slice (see
// components/nifscene/flatten.hpp for the policy).
//
// Threads: pure with respect to engine state. It reads the VFS through the image options, shares objects through
// vsg::SharedObjects (thread safe) and creates pipelines through Pipeline::Builder (the same call the Morrowind reader
// makes from its loader threads), so it may run on any thread the Morrowind NIF reader runs on.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <vsg/core/Data.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Node.h>
#include <vsg/utils/SharedObjects.h>

#include <components/nifscene/flatten.hpp>
#include <components/nifscene/skinned.hpp>
#include <components/vsgadapters/nif/nif.hpp>

namespace Pipeline
{
    class Builder;
}

namespace MWRender::SkyrimModel
{
    struct BuildContext
    {
        const Pipeline::Builder& mBuilder;
        vsg::ref_ptr<const vsg::Options> mImageOptions; // the texture options (VFS, "textures" search path)
        vsg::ref_ptr<vsg::SharedObjects> mSharedObjects; // may be null
        int mDepthSortedBin = 0;
        std::string mFilename; // diagnostics only
    };

    /// What building one model did.
    struct BuildResult
    {
        vsg::ref_ptr<vsg::Node> mNode; // null when the model draws nothing
        std::uint32_t mTexturesLoaded = 0; // shapes whose diffuse texture was read
        std::uint32_t mTexturesMissing = 0; // shapes whose diffuse texture could not be read (drawn untextured)
        std::uint32_t mDraws = 0; // StateGroups made
    };

    BuildResult buildNode(const NifScene::Flat::FlatModel& model, const BuildContext& context);

    /// The skinned shapes of a model (task 0121, N5-2): one draw per shape, each with its own Anim::Skin (bones named
    /// as the file names them) and bone matrix buffer, under a plain group. The result has NO Anim::Meta: the caller
    /// that assembles the actor puts Skins | Controllers | DynamicBounds on the root. Main thread (the bone matrix
    /// buffers and descriptor sets are the actor's own and are not shared through SharedObjects).
    BuildResult buildSkinnedNode(const NifScene::Skinned::SkinnedModel& model, const BuildContext& context);

    /// The scene reader the host hands to the renderer's NIF reader (vsgAdapters::nif::sceneReader) when the neutral
    /// world is on: accepts Skyrim SE files (header 20.2.0.7), reads them as a Nif::Scene, flattens and builds them.
    /// The references must outlive the reader (the renderer's Pipeline::Builder and the texture options).
    std::shared_ptr<const vsgAdapters::nif::SceneReader> makeSceneReader(
        const Pipeline::Builder& builder, vsg::ref_ptr<const vsg::Options> imageOptions, int depthSortedBin);

    /// Process-wide totals for the log line of a neutral cell. Relaxed atomics: a count, not a synchronisation.
    struct Counters
    {
        std::atomic<std::uint64_t> mRequests{ 0 }; // objects that asked for a model (Neutral::insertObjectRendering)
        std::atomic<std::uint64_t> mModels{ 0 }; // models built from a scene (not cache hits: the cache is VSG's)
        std::atomic<std::uint64_t> mModelsEmpty{ 0 }; // of those, models that draw nothing
        std::atomic<std::uint64_t> mDraws{ 0 };
        std::atomic<std::uint64_t> mTriangles{ 0 };
        std::atomic<std::uint64_t> mVertices{ 0 };
        std::atomic<std::uint64_t> mTexturesLoaded{ 0 };
        std::atomic<std::uint64_t> mTexturesMissing{ 0 };
        std::atomic<std::uint64_t> mSkippedSkinned{ 0 };
        std::atomic<std::uint64_t> mSkippedEffect{ 0 };
        std::atomic<std::uint64_t> mSkippedNoMaterial{ 0 };
        std::atomic<std::uint64_t> mSkippedHidden{ 0 };
        std::atomic<std::uint64_t> mDroppedTriangles{ 0 };
        std::atomic<std::uint64_t> mDoubleSidedShapes{ 0 };
        std::atomic<std::uint64_t> mNormalMaps{ 0 }; // task 0130: normal maps bound
        std::atomic<std::uint64_t> mGlowMaps{ 0 };
        std::atomic<std::uint64_t> mNormalMapsNoTangents{ 0 };
        std::atomic<std::uint64_t> mNormalMapsModelSpace{ 0 };
        std::atomic<std::uint64_t> mFailed{ 0 }; // models whose read threw
    };

    Counters& counters();

    /// "Skyrim models: 213 built (4 empty), 1,210 draws, ...": one line for the log.
    std::string describeCounters();
}

#endif

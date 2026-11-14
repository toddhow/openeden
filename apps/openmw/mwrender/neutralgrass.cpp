#include "neutralgrass.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include <vsg/threading/OperationThreads.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/options.hpp>
#include <components/pipeline/override.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/operation.hpp>

#include "groundcoverbuild.hpp"

namespace MWRender
{
    namespace
    {
        // Built chunks kept after their square leaves range, so walking back does not rebuild them (as Groundcover).
        constexpr std::size_t sCachedChunks = 48;
        // The camera moves this share of a square before the squares in range are worked out again.
        constexpr float sUpdateShareOfCell = 0.125f;
        // The longest (x, y) of a tilt normal the shader is given: sin of about 64 degrees.
        constexpr float kMaxTiltLength = 0.9f;

        // OPENEDEN_NEUTRAL_GRASS_ALBEDO=<scale> (a developer aid, read once) replaces grassAlbedoScale() for a look.
        float albedoOverride()
        {
            static const float value = [] {
                const char* text = std::getenv("OPENEDEN_NEUTRAL_GRASS_ALBEDO");
                return text ? static_cast<float>(std::atof(text)) : -1.f;
            }();
            return value;
        }
    }

    float grassAlbedoScale()
    {
        const float override_ = albedoOverride();
        return override_ >= 0.f ? override_ : kGrassAlbedoScale;
    }

    MWAnim::Context makeNeutralGrassContext(const MWAnim::Context& base)
    {
        MWAnim::Context result = makeGroundcoverContext(base);
        const float scale = grassAlbedoScale();
        auto nodeOptions = vsg::Options::create(*result.nodeOptions);
        auto tint = vsg::ref_ptr{ new Pipeline::Override };
        // Each tuft carries a colour and a sway speed (the third per-instance input, task 0153 follow-up 2).
        tint->pipelineOptions = [](Pipeline::Options& options) {
            if (options.shader.hasMode(Pipeline::Mode::INSTANCE))
                options.shader.addMode(Pipeline::Mode::INSTANCE_TINT);
        };
        if (scale != 1.f)
        {
            // The Skyrim model reader multiplies the shape's vertex colours (and its material) by this colour.
            tint->materialData = [scale](Pipeline::Data::Material& material) {
                for (int c = 0; c < 3; ++c)
                {
                    material.diffuse[c] *= scale;
                    material.ambient[c] *= scale;
                }
            };
        }
        tint->composite(*nodeOptions);
        result.nodeOptions = nodeOptions;
        return result;
    }

    GroundcoverInstances groundcoverFromGrass(const openeden::sdk::game::GrassCell& cell)
    {
        GroundcoverInstances out;
        std::vector<GroundcoverInstances::iterator> byModel(cell.models.size(), out.end());
        for (const openeden::sdk::game::GrassInstance& tuft : cell.instances)
        {
            if (tuft.model >= cell.models.size() || !std::isfinite(tuft.x) || !std::isfinite(tuft.y)
                || !std::isfinite(tuft.z) || !std::isfinite(tuft.yaw) || !std::isfinite(tuft.scale)
                || !std::isfinite(tuft.tiltX) || !std::isfinite(tuft.tiltY) || !std::isfinite(tuft.tintR)
                || !std::isfinite(tuft.tintG) || !std::isfinite(tuft.tintB) || !std::isfinite(tuft.waveSpeed))
                continue;
            auto& slot = byModel[tuft.model];
            if (slot == out.end())
            {
                const VFS::Path::Normalized path(cell.models[tuft.model].modelPath);
                slot = out.try_emplace(path).first;
            }
            GroundcoverInstance instance;
            instance.position.pos[0] = tuft.x;
            instance.position.pos[1] = tuft.y;
            instance.position.pos[2] = tuft.z;
            // With INSTANCE_TINT the rotation input is (tilt x, tilt y, yaw): the ground normal's x and y, cut to a
            // length below 1 (at most 64 degrees of tilt) so the shader's normal z stays real.
            float tiltX = tuft.tiltX;
            float tiltY = tuft.tiltY;
            const float tiltLength = std::sqrt(tiltX * tiltX + tiltY * tiltY);
            if (tiltLength > kMaxTiltLength)
            {
                tiltX *= kMaxTiltLength / tiltLength;
                tiltY *= kMaxTiltLength / tiltLength;
            }
            instance.position.rot[0] = tiltX;
            instance.position.rot[1] = tiltY;
            instance.position.rot[2] = tuft.yaw;
            instance.scale = tuft.scale;
            instance.tinted = true;
            instance.tint = vsg::vec4(tuft.tintR, tuft.tintG, tuft.tintB, tuft.waveSpeed);
            slot->second.push_back(instance);
        }
        return out;
    }

    // One square's chunk being built on the worker; the main thread reads chunk only once done is set.
    struct NeutralGrass::ChunkBuild : public vsgUtil::Operation
    {
        const MWAnim::Context& context;
        std::pair<int, int> cell;
        GroundcoverInstances instances;
        vsg::vec3 origin;
        vsg::ref_ptr<vsg::Node> chunk;

        ChunkBuild(const MWAnim::Context& in_context, const std::pair<int, int>& in_cell,
            GroundcoverInstances in_instances, const vsg::vec3& in_origin)
            : context(in_context)
            , cell(in_cell)
            , instances(std::move(in_instances))
            , origin(in_origin)
        {
        }

        void operate() override
        {
            try
            {
                chunk = buildGroundcoverChunk(context, instances, origin);
            }
            catch (const std::exception& e)
            {
                // The square is then treated as having no grass, and not built again.
                Log(Debug::Error) << "Failed to build neutral grass for square " << cell.first << ", " << cell.second
                                  << ": " << e.what();
            }
            instances.clear();
        }
    };

    NeutralGrass::NeutralGrass(const MWAnim::Context& context, openeden::sdk::game::GrassSource& source,
        openeden::sdk::game::WorldId world, float cellSize, float renderingDistance)
        : mContext(makeNeutralGrassContext(context))
        , mSource(source)
        , mWorld(world)
        , mCellSize(cellSize)
        , mRenderingDistance(renderingDistance)
        , mThreads(vsg::OperationThreads::create(1))
        , mRoot(vsg::Group::create())
        , mTracker(sCachedChunks)
    {
    }

    NeutralGrass::~NeutralGrass()
    {
        waitForBuilds();
        setShownChunks({});
        Log(Debug::Info) << "Neutral grass: asked the pack for " << mStats.mCellsAsked << " squares (" << mStats.mCellsWithGrass
                         << " with grass), " << mStats.mTufts << " tufts of " << mStats.mTuftsRequested
                         << " asked, pack time " << mStats.mPackMilliseconds << " ms (worst "
                         << mStats.mWorstPackMilliseconds << " ms)";
    }

    void NeutralGrass::waitForBuilds()
    {
        for (const auto& build : mBuilds)
            build->wait();
    }

    void NeutralGrass::hide()
    {
        setShownChunks({});
        mHasUpdated = false;
    }

    void NeutralGrass::update(const vsg::vec3& cameraPosition)
    {
        bool changed = collectBuilds();

        const vsg::vec2 camera(cameraPosition.x, cameraPosition.y);
        if (!mHasUpdated || vsg::length(camera - mLastUpdatePosition) >= mCellSize * sUpdateShareOfCell)
        {
            changed = true;
            mHasUpdated = true;
            mLastUpdatePosition = camera;
            mWanted = groundcoverCellsInRange(camera.x, camera.y, mRenderingDistance, mCellSize);
            const std::vector<std::pair<int, int>> toBuild = mTracker.setWanted(mWanted);
            mQueue.insert(mQueue.end(), toBuild.begin(), toBuild.end());
            // Nearest first: the square under the camera must not wait behind the ring.
            const auto distance = [&](const std::pair<int, int>& c) {
                const float dx = (static_cast<float>(c.first) + 0.5f) * mCellSize - camera.x;
                const float dy = (static_cast<float>(c.second) + 0.5f) * mCellSize - camera.y;
                return dx * dx + dy * dy;
            };
            std::sort(mQueue.begin(), mQueue.end(),
                [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
        }

        askPack(camera);

        if (!changed)
            return;
        vsg::Group::Children chunks;
        for (const auto& [cell, chunk] : mTracker.shown())
            if (chunk)
                chunks.push_back(chunk);
        setShownChunks(std::move(chunks));
    }

    void NeutralGrass::askPack(const vsg::vec2&)
    {
        for (std::size_t asked = 0; asked < kCellsPerUpdate && !mQueue.empty();)
        {
            const std::pair<int, int> cell = mQueue.front();
            mQueue.pop_front();
            if (!mTracker.isPending(cell))
                continue; // forgotten, or finished by another path
            if (!std::binary_search(mWanted.begin(), mWanted.end(), cell))
            {
                mTracker.forget(cell); // the camera moved on before its turn
                continue;
            }
            ++asked;
            openeden::sdk::game::GrassCell grass;
            bool ok = false;
            {
                Misc::LoadTrace::Scope trace("neutralgrass.pack");
                const auto start = std::chrono::steady_clock::now();
                ok = mSource.grassForCell(mWorld, cell.first, cell.second, grass);
                const double ms
                    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                mStats.mPackMilliseconds += ms;
                mStats.mWorstPackMilliseconds = std::max(mStats.mWorstPackMilliseconds, ms);
            }
            ++mStats.mCellsAsked;
            GroundcoverInstances instances;
            if (ok)
                instances = groundcoverFromGrass(grass);
            if (instances.empty())
            {
                mTracker.finish(cell, {});
                continue;
            }
            ++mStats.mCellsWithGrass;
            mStats.mTufts += grass.instances.size();
            mStats.mTuftsRequested += grass.requested;
            Log(Debug::Verbose) << "Neutral grass: square (" << cell.first << ", " << cell.second << ") has "
                                << grass.instances.size() << " tufts of " << grass.models.size() << " models";
            // The chunk is placed at the middle of its square, as Groundcover's are.
            const vsg::vec3 origin((static_cast<float>(cell.first) + 0.5f) * mCellSize,
                (static_cast<float>(cell.second) + 0.5f) * mCellSize, 0.f);
            auto build = vsg::ref_ptr{ new ChunkBuild(mContext, cell, std::move(instances), origin) };
            mBuilds.push_back(build);
            mThreads->add(build);
        }
    }

    bool NeutralGrass::collectBuilds()
    {
        return std::erase_if(mBuilds, [this](const vsg::ref_ptr<ChunkBuild>& build) {
            if (!build->done)
                return false;
            vsg::ref_ptr<vsg::Node> chunk = build->chunk;
            if (!chunk)
                Log(Debug::Warning) << "Neutral grass: square (" << build->cell.first << ", " << build->cell.second
                                    << ") built no chunk (no model could be read)";
            else if (!mContext.compileContext->compile(chunk))
            {
                Log(Debug::Warning) << "Neutral grass: square (" << build->cell.first << ", " << build->cell.second
                                    << ") could not be compiled";
                chunk = {};
            }
            mTracker.finish(build->cell, chunk);
            return true;
        }) > 0;
    }

    void NeutralGrass::setShownChunks(vsg::Group::Children chunks)
    {
        for (const auto& child : mRoot->children)
            if (std::find(chunks.begin(), chunks.end(), child) == chunks.end())
                mContext.compileContext->detach(child);
        mRoot->children = std::move(chunks);
    }
}

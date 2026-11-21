#include "pointshadows.hpp"

#include <algorithm>
#include <cstdlib>

#include <components/animation/pointshadowcarrier.hpp>
#include <components/debug/debuglog.hpp>
#include <components/render/pointshadowmath.hpp>
#include <components/settings/values.hpp>

namespace MWRender
{
    namespace
    {
        vsg::dvec3 xyz(const vsg::dvec4& v)
        {
            return vsg::dvec3(v.x, v.y, v.z);
        }

        // The filter's radius in texels: half a texel more than the one-texel hardware footprint, a soft edge without
        // blur (research note 0039, section 8).
        constexpr double pcfRadiusTexels = 1.5;
        // Frames a holder keeps its slot while its light is not in the collected list.
        constexpr std::uint32_t missingGraceFrames = 16;
    }

    PointShadows::PointShadows(vsg::Context& ctx, std::size_t maxLights)
        : mMaps(ctx, Settings::shadows().mPointLightShadowCount,
            static_cast<std::uint32_t>(Settings::shadows().mPointLightShadowResolution), maxLights)
        , mMaxLights(maxLights)
    {
        const std::string memoryWarning
            = Render::PointShadow::memoryWarningText(mMaps.slotCount, static_cast<int>(mMaps.resolution));
        if (!memoryWarning.empty())
            Log(Debug::Warning) << memoryWarning;
        mSelection.maxSlots = mMaps.slotCount;
        mSlots.assign(static_cast<std::size_t>(mMaps.slotCount), Render::PointShadow::Slot{});
        mMissing.assign(static_cast<std::size_t>(mMaps.slotCount), 0u);
        mGraphs.resize(static_cast<std::size_t>(mMaps.slotCount));
        mCacheEnabled = Settings::shadows().mPointLightShadowCache;
        mCacheParams.facesPerFrame = Settings::shadows().mPointLightShadowRefreshFaces;
        mCacheParams.refreshFrames = static_cast<std::uint32_t>(Settings::shadows().mPointLightShadowRefreshFrames);
        if (mCacheEnabled)
        {
            Log(Debug::Info) << "Shadows: point light shadow cache on, at most " << mCacheParams.facesPerFrame
                             << " stale faces a frame, a face is redrawn at the latest after "
                             << mCacheParams.refreshFrames << " frames";
        }
        mGpu.resolution = mMaps.resolution;
        mGpu.tanHalf = mMaps.tanHalf;
        mGpu.pcfRadiusTexels = pcfRadiusTexels;
        mGpu.taps = Settings::shadows().mPointLightShadowFilterTaps;
        mGpu.biasScale = Settings::shadows().mPointLightShadowBias;
        mCandidates.reserve(maxLights);
        mEntries.reserve(Render::PointShadow::maxSlots);
        mPacked.reserve(Render::PointShadow::bufferVec4s(maxLights));
        mSlotPositions.reserve(Render::PointShadow::maxSlots);
    }

    void PointShadows::setGraph(int slot, vsg::ref_ptr<vsg::CommandGraph> graph)
    {
        mGraphs[static_cast<std::size_t>(slot)] = std::move(graph);
    }

    void PointShadows::setSlotTimer(int slot, vsg::ref_ptr<vsg::Node> begin, vsg::ref_ptr<vsg::Node> end)
    {
        const std::size_t k = static_cast<std::size_t>(slot);
        if (mTimerBegin.size() <= k)
        {
            mTimerBegin.resize(k + 1);
            mTimerEnd.resize(k + 1);
        }
        mTimerBegin[k] = std::move(begin);
        mTimerEnd[k] = std::move(end);
    }

    int PointShadows::usedSlots() const
    {
        return static_cast<int>(std::count_if(mSlots.begin(), mSlots.end(), [](const auto& s) { return s.used; }));
    }

    void PointShadows::update(
        const vsg::dmat4& viewMatrix, const std::vector<View::LightSnapshot>& lights, std::uint32_t worldEpoch)
    {
        // The lights' eye positions belong to the view of the frame that collected them.
        const vsg::dmat4 inverseView = vsg::inverse(mHaveListView ? mListView : viewMatrix);
        const vsg::dvec3 camera = xyz(vsg::inverse(viewMatrix) * vsg::dvec4(0.0, 0.0, 0.0, 1.0));
        mListView = viewMatrix;
        mHaveListView = true;

        // Candidates in the light buffer's order. The id (the node's address, never followed) only recognises the same
        // light from one frame to the next.
        mCandidates.clear();
        const std::size_t count = std::min(lights.size(), mMaxLights);
        for (std::size_t i = 0; i < count; ++i)
        {
            const View::LightSnapshot& light = lights[i];
            Render::PointShadow::Candidate c;
            c.id = static_cast<std::uint64_t>(light.id);
            c.position = xyz(inverseView * vsg::dvec4(light.eyePosition, 1.0));
            c.radius = light.intensity;
            // Darkness lights have a negative colour and cast nothing.
            // Task 0355 stage g: and a light whose game record says it casts no shadow does not take a slot.
            c.casts = Render::PointShadow::lightMayCast(light.color.r, light.color.g, light.color.b, light.shadowRule);
            mCandidates.push_back(c);
        }
        // A holder whose light the last traversal did not see (it was culled for a moment) keeps its slot for a few
        // frames, with the position it had.
        for (std::size_t k = 0; k < mSlots.size(); ++k)
        {
            const Render::PointShadow::Slot& slot = mSlots[k];
            if (!slot.used)
            {
                mMissing[k] = 0;
                continue;
            }
            const bool seen = std::any_of(mCandidates.begin(), mCandidates.begin() + static_cast<std::ptrdiff_t>(count),
                [&](const Render::PointShadow::Candidate& c) { return c.id == slot.id; });
            if (seen)
                mMissing[k] = 0;
            else if (++mMissing[k] <= missingGraceFrames)
            {
                Render::PointShadow::Candidate c;
                c.id = slot.id;
                c.position = slot.position;
                c.radius = slot.radius;
                mCandidates.push_back(c);
            }
        }

        Render::PointShadow::selectInto(mCandidates, camera, mSlots, mSelection, mResult, mScratch);
        for (std::size_t k = 0; k < mResult.slots.size(); ++k)
        {
            const Render::PointShadow::Slot& now = mResult.slots[k];
            const Render::PointShadow::Slot& before = mSlots[k];
            if (now.used && (!before.used || before.id != now.id))
            {
                mMissing[k] = 0;
                Log(Debug::Verbose) << "Point light shadow: slot " << k << " takes the light at " << now.position.x
                                    << " " << now.position.y << " " << now.position.z << ", radius " << now.radius;
            }
            else if (!now.used && before.used)
                Log(Debug::Verbose) << "Point light shadow: slot " << k << " is free";
        }
        mSlots = mResult.slots;
        // Task 0362: which faces each slot's light needs (a spot's cone, a hemisphere's half space). A holder the last
        // traversal did not see keeps its mask.
        mSlotMask.resize(mSlots.size(), Render::PointShadow::allFacesMask);
        for (std::size_t k = 0; k < mSlots.size(); ++k)
            if (!mSlots[k].used)
                mSlotMask[k] = Render::PointShadow::allFacesMask;
        for (std::size_t i = 0; i < count && i < mResult.slotOfCandidate.size(); ++i)
        {
            const int k = mResult.slotOfCandidate[i];
            if (k < 0)
                continue;
            const View::LightSnapshot& light = lights[i];
            const vsg::dvec4 worldAxis = inverseView * vsg::dvec4(light.shadowAxisEye, 0.0);
            const std::uint8_t mask = Render::PointShadow::faceMask(
                light.shadowShape, vsg::dvec3(worldAxis.x, worldAxis.y, worldAxis.z), light.shadowConeCos);
            if (mask != mSlotMask[static_cast<std::size_t>(k)])
                Log(Debug::Verbose) << "Point light shadow: slot " << k << " draws "
                                    << Render::PointShadow::faceCount(mask) << " of 6 faces (shape "
                                    << static_cast<int>(light.shadowShape) << ", mask 0x" << std::hex
                                    << static_cast<int>(mask) << std::dec << ", world axis " << worldAxis.x << " "
                                    << worldAxis.y << " " << worldAxis.z << ", cone cos " << light.shadowConeCos
                                    << ")";
            mSlotMask[static_cast<std::size_t>(k)] = mask;
        }
        // Developer aid (task 0362): OPENEDEN_POINT_SHADOW_ALL_FACES=1 draws all six faces of every slot again, to
        // compare the face count and the cost with the cone and half space masks.
        static const bool allFaces = std::getenv("OPENEDEN_POINT_SHADOW_ALL_FACES") != nullptr;
        if (allFaces)
            std::fill(mSlotMask.begin(), mSlotMask.end(), Render::PointShadow::allFacesMask);
        // Which light holds which slot, for the models that carry a light (Anim::Transform::setPointShadowCarrier).
        for (int k = 0; k < Anim::pointShadowSlotCount; ++k)
        {
            const bool held = k < mMaps.slotCount && mSlots[static_cast<std::size_t>(k)].used;
            Anim::setPointShadowOwner(k, held ? mSlots[static_cast<std::size_t>(k)].id : 0);
        }

        // Which faces are drawn this frame.
        mFacesDrawn = 0;
        mFacesPossible = 0;
        if (mCacheEnabled)
        {
            mCacheLights.resize(mSlots.size());
            for (std::size_t k = 0; k < mSlots.size(); ++k)
            {
                Render::PointShadow::CacheLight& c = mCacheLights[k];
                c.used = mSlots[k].used;
                c.id = mSlots[k].id;
                c.position = mSlots[k].position;
                c.radius = mSlots[k].radius;
                c.faceMask = mSlotMask[k];
            }
            mMotion.end(mDisturbances);
            mCache.step(mCacheLights, mDisturbances, worldEpoch + mExtraEpoch, mCacheParams, mDraw);
            if (++mCacheLogCounter % 120 == 0)
            {
                Log(Debug::Verbose) << "Point light shadow cache: " << mCache.lastHardSlots()
                                    << " slots redrawn whole, " << mCache.lastPendingFaces() << " stale faces waiting, "
                                    << mDisturbances.size() << " disturbances, epoch " << worldEpoch << "+"
                                    << mExtraEpoch;
            }
        }

        mSlotPositions.clear();
        mEntries.clear();
        for (int k = 0; k < mMaps.slotCount; ++k)
        {
            const Render::PointShadow::Slot& slot = mSlots[static_cast<std::size_t>(k)];
            auto& graph = mGraphs[static_cast<std::size_t>(k)];
            if (!slot.used)
            {
                mSlotPositions.emplace_back(0.f, 0.f, 0.f, 0.f);
                if (graph)
                    graph->children.clear();
                continue;
            }
            const double far = Render::PointShadow::farMargin * Render::PointShadow::reach(slot.radius);
            mMaps.setSlotLight(k, slot.position, far);
            const vsg::dvec3 viewPosition = xyz(viewMatrix * vsg::dvec4(slot.position, 1.0));
            mSlotPositions.emplace_back(static_cast<float>(viewPosition.x), static_cast<float>(viewPosition.y),
                static_cast<float>(viewPosition.z), static_cast<float>(far));
            mEntries.push_back(
                { static_cast<std::uintptr_t>(slot.id), k, slot.fade, mSlotMask[static_cast<std::size_t>(k)] });
            if (graph)
            {
                graph->children.clear();
                const bool timed
                    = static_cast<std::size_t>(k) < mTimerBegin.size() && mTimerBegin[static_cast<std::size_t>(k)];
                if (timed)
                    graph->children.push_back(mTimerBegin[static_cast<std::size_t>(k)]);
                for (int f = 0; f < Render::PointShadow::facesPerLight; ++f)
                {
                    ++mFacesPossible;
                    if (!((mSlotMask[static_cast<std::size_t>(k)] >> f) & 1))
                        continue; // outside the light's cone or half space: not drawn, and the shader never reads it
                    if (mCacheEnabled && !mDraw[static_cast<std::size_t>(k * Render::PointShadow::facesPerLight + f)])
                        continue;
                    ++mFacesDrawn;
                    graph->children.push_back(mMaps.renderGraph(k, f));
                }
                if (timed)
                    graph->children.push_back(mTimerEnd[static_cast<std::size_t>(k)]);
            }
        }

        // The header and the slots; the lights' entries are written by View::CollectLights::pack, in the loop that
        // writes the lights, so that they match the lights of the frame about to be recorded.
        mGpu.slotCount = usedSlots();
        Render::PointShadow::pack(mGpu, mMaxLights, mNoSlots, mNoFades, mSlotPositions, mPacked);
        auto data = mMaps.data();
        std::copy(mPacked.begin(), mPacked.end(), data->begin());
        data->dirty();
    }
}

#include "pointshadows.hpp"

#include <algorithm>

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
        mSelection.maxSlots = mMaps.slotCount;
        mSlots.assign(static_cast<std::size_t>(mMaps.slotCount), Render::PointShadow::Slot{});
        mMissing.assign(static_cast<std::size_t>(mMaps.slotCount), 0u);
        mGraphs.resize(static_cast<std::size_t>(mMaps.slotCount));
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

    int PointShadows::usedSlots() const
    {
        return static_cast<int>(std::count_if(mSlots.begin(), mSlots.end(), [](const auto& s) { return s.used; }));
    }

    void PointShadows::update(const vsg::dmat4& viewMatrix, const std::vector<View::LightSnapshot>& lights)
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
            c.casts = light.color.r >= 0.f && light.color.g >= 0.f && light.color.b >= 0.f
                && (light.color.r > 0.f || light.color.g > 0.f || light.color.b > 0.f);
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
            mEntries.push_back({ static_cast<std::uintptr_t>(slot.id), k, slot.fade });
            if (graph)
            {
                graph->children.clear();
                for (int f = 0; f < Render::PointShadow::facesPerLight; ++f)
                    graph->children.push_back(mMaps.renderGraph(k, f));
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

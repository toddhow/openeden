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
    }

    PointShadows::PointShadows(vsg::Context& ctx, std::size_t maxLights)
        : mMaps(ctx, Settings::shadows().mPointLightShadowCount,
            static_cast<std::uint32_t>(Settings::shadows().mPointLightShadowResolution), maxLights)
        , mMaxLights(maxLights)
    {
        mSelection.maxSlots = mMaps.slotCount;
        mSlots.assign(static_cast<std::size_t>(mMaps.slotCount), Render::PointShadow::Slot{});
        mGraphs.resize(static_cast<std::size_t>(mMaps.slotCount));
        mGpu.resolution = mMaps.resolution;
        mGpu.tanHalf = mMaps.tanHalf;
        mGpu.pcfRadiusTexels = pcfRadiusTexels;
        mGpu.taps = Settings::shadows().mPointLightShadowFilterTaps;
        mGpu.biasScale = Settings::shadows().mPointLightShadowBias;
        mCandidates.reserve(maxLights);
        mSlotOfLight.reserve(maxLights);
        mFadeOfLight.reserve(maxLights);
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

    void PointShadows::update(const vsg::dmat4& viewMatrix, const PointLights& lights)
    {
        using namespace Render::PointShadow;
        // The lights' eye positions belong to the view of the frame that collected them.
        const vsg::dmat4 inverseView = vsg::inverse(mHaveListView ? mListView : viewMatrix);
        const vsg::dvec3 camera = xyz(vsg::inverse(viewMatrix) * vsg::dvec4(0.0, 0.0, 0.0, 1.0));
        mListView = viewMatrix;
        mHaveListView = true;

        // Candidates in the light buffer's order (CollectLights::pack writes them in this order and stops at
        // maxLights), so a candidate's index is its light's index in the buffer.
        mCandidates.clear();
        const std::size_t count = std::min(lights.size(), mMaxLights);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& [modelView, light] = lights[i];
            Candidate c;
            // The pointer only recognises the same light from one frame to the next (never an order).
            c.id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(light));
            const vsg::dvec3 eye = xyz(modelView * vsg::dvec4(light->position, 1.0));
            c.position = xyz(inverseView * vsg::dvec4(eye, 1.0));
            c.radius = light->intensity;
            // Darkness lights have a negative colour and cast nothing.
            c.casts = light->color.r >= 0.f && light->color.g >= 0.f && light->color.b >= 0.f
                && (light->color.r > 0.f || light->color.g > 0.f || light->color.b > 0.f);
            mCandidates.push_back(c);
        }

        const Selection selection = select(mCandidates, camera, mSlots, mSelection);
        for (std::size_t k = 0; k < selection.slots.size(); ++k)
        {
            const Slot& now = selection.slots[k];
            const Slot& before = mSlots[k];
            if (now.used && (!before.used || before.id != now.id))
                Log(Debug::Info) << "Point light shadow: slot " << k << " takes the light at " << now.position.x << " "
                                 << now.position.y << " " << now.position.z << ", radius " << now.radius;
            else if (!now.used && before.used)
                Log(Debug::Info) << "Point light shadow: slot " << k << " is free";
        }
        mSlots = selection.slots;

        mSlotOfLight.assign(count, -1);
        mFadeOfLight.assign(count, 0.f);
        for (std::size_t i = 0; i < count; ++i)
        {
            const int slot = selection.slotOfCandidate[i];
            mSlotOfLight[i] = slot;
            if (slot >= 0)
                mFadeOfLight[i] = mSlots[static_cast<std::size_t>(slot)].fade;
        }

        mSlotPositions.clear();
        for (int k = 0; k < mMaps.slotCount; ++k)
        {
            const Slot& slot = mSlots[static_cast<std::size_t>(k)];
            auto& graph = mGraphs[static_cast<std::size_t>(k)];
            if (!slot.used)
            {
                mSlotPositions.emplace_back(0.f, 0.f, 0.f, 0.f);
                if (graph)
                    graph->children.clear();
                continue;
            }
            const double far = farMargin * reach(slot.radius);
            mMaps.setSlotLight(k, slot.position, far);
            const vsg::dvec3 viewPosition = xyz(viewMatrix * vsg::dvec4(slot.position, 1.0));
            mSlotPositions.emplace_back(static_cast<float>(viewPosition.x), static_cast<float>(viewPosition.y),
                static_cast<float>(viewPosition.z), static_cast<float>(far));
            if (graph)
            {
                graph->children.clear();
                for (int f = 0; f < facesPerLight; ++f)
                    graph->children.push_back(mMaps.renderGraph(k, f));
            }
        }

        mGpu.slotCount = usedSlots();
        pack(mGpu, mMaxLights, mSlotOfLight, mFadeOfLight, mSlotPositions, mPacked);
        auto data = mMaps.data();
        std::copy(mPacked.begin(), mPacked.end(), data->begin());
        data->dirty();
    }
}

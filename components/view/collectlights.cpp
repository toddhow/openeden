#include "collectlights.hpp"
#include <vsg/lighting/PointLight.h>

#include <cstdlib>

#include <components/debug/debuglog.hpp>

#include <components/render/hdrmath.hpp>
#include <components/render/lightfade.hpp>

namespace View
{
    CollectLights::CollectLights(size_t maxLights)
        : vsg::ViewDependentState(0)
    {
        mData = vsg::vec4Array::create(maxLights * 2); // vsgopenmw-fixme(dont-repeat-yourself(light-data-size))
        mData->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA_TRANSFER_AFTER_RECORD;
    }

    void CollectLights::setShadowTarget(vsg::ref_ptr<vsg::vec4Array> data, std::size_t entryOffset)
    {
        mShadowData = std::move(data);
        mShadowEntryOffset = entryOffset;
        mSnapshots.reserve(mData->size() / 2);
        mShadowEntries.reserve(8);
    }

    void CollectLights::pack() const
    {
        auto light_itr = mData->begin();
        size_t maxLights = mData->size() / 2;
        size_t lightCount = std::min(maxLights, pointLights.size());
        for (size_t i = 0; i < lightCount; ++i)
        {
            auto& l = pointLights[i];
            auto& mv = l.first;
            auto& light = l.second;
            auto eye_position = vsg::vec3(mv * light->position);
            // Darkness lights have negative colours; Render::Hdr::toLinear keeps their sign.
            vsg::vec3 color(light->color.r, light->color.g, light->color.b);
            if (mLinear)
                color = Render::Hdr::toLinear(color);
            // Task 0355 stage j: a game pack's interior fades its lights by their distance from the eye. After the
            // conversion, so the factor scales the LINEAR intensity. Off (both 0), no operation at all.
            if (mFadeEnd > 0.f)
                color *= Render::LightFade::factor(vsg::length(eye_position), mFadeBegin, mFadeEnd);
            (*light_itr++).set(color.x, color.y, color.z, light->intensity);
            (*light_itr++).set(eye_position.x, eye_position.y, eye_position.z, 0);
        }
        mData->at(1).w = static_cast<float>(lightCount);
        // OPENEDEN_LIGHT_COUNT_TRACE=1 (developer aid, task 0355): the lights this traversal collected, every 120 packs.
        static const bool countTrace = std::getenv("OPENEDEN_LIGHT_COUNT_TRACE") != nullptr;
        if (countTrace)
        {
            static std::size_t packs = 0;
            if (packs++ % 120 == 0)
                Log(Debug::Info) << "NLIGHTCOUNT collected " << pointLights.size() << " packed " << lightCount;
            if (packs % 120 == 1)
                for (std::size_t i = 0; i < lightCount && i < 6; ++i)
                {
                    const auto& light = pointLights[i].second;
                    const vsg::vec3 e = vsg::vec3(pointLights[i].first * light->position);
                    Log(Debug::Info) << "NLIGHTCOUNT light " << i << " colour " << light->color.r << "," << light->color.g
                                     << "," << light->color.b << " radius " << light->intensity << " eye " << e.x << ","
                                     << e.y << "," << e.z;
                }
        }
        if (mShadowData)
        {
            mSnapshots.clear();
            for (std::size_t i = 0; i < lightCount; ++i)
            {
                const auto& l = pointLights[i];
                const auto& light = l.second;
                LightSnapshot snapshot;
                snapshot.id = reinterpret_cast<std::uintptr_t>(light);
                snapshot.eyePosition = l.first * light->position;
                snapshot.color = light->color;
                snapshot.intensity = light->intensity;
                mSnapshots.push_back(snapshot);
                float slot = -1.f;
                float fade = 0.f;
                for (const ShadowEntry& entry : mShadowEntries)
                {
                    if (entry.id == snapshot.id)
                    {
                        slot = static_cast<float>(entry.slot);
                        fade = entry.fade;
                        break;
                    }
                }
                if (mShadowEntryOffset + i < mShadowData->size())
                    mShadowData->at(mShadowEntryOffset + i) = vsg::vec4(slot, fade, 0.f, 0.f);
            }
            mShadowData->dirty();
        }
        // if (maxLights < pointLights.size())
        //     std::cerr << "maxLights < pointLights.size()" << std::endl;
        mData->dirty();
    }
}

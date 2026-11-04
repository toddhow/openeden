#include "collectlights.hpp"
#include <vsg/lighting/PointLight.h>

#include <components/render/hdrmath.hpp>

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
            (*light_itr++).set(color.x, color.y, color.z, light->intensity);
            (*light_itr++).set(eye_position.x, eye_position.y, eye_position.z, 0);
        }
        mData->at(1).w = static_cast<float>(lightCount);
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

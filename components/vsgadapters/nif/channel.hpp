#ifndef VSGOPENMW_VSGADAPTERS_NIF_CHANNEL_H
#define VSGOPENMW_VSGADAPTERS_NIF_CHANNEL_H

#include <algorithm>
#include <cassert>
#include <vector>

#include <components/animation/channel.hpp>
#include <components/nif/data.hpp>

namespace vsgAdapters
{
    struct FlipChannel : public Anim::Channel<size_t>
    {
        FlipChannel(float d, size_t n)
            : delta(d)
            , numTextures(n)
        {
        }
        size_t value(float time) const override { return static_cast<size_t>(time / delta) % numTextures; }
        float delta;
        size_t numTextures;
    };

    struct VisChannel : public Anim::Channel<bool>
    {
        VisChannel(std::shared_ptr<std::vector<std::pair<float, bool>>> d)
            : data(d)
        {
            assert(!data->empty());
        }
        std::shared_ptr<std::vector<std::pair<float, bool>>> data;
        bool value(float time) const override
        {
            auto iter = std::upper_bound(data->begin(), data->end(), time,
                [](float t, const std::pair<float, bool>& elem) { return t < elem.first; });
            if (iter != data->begin())
                --iter;
            return iter->second;
        }
    };
}

#endif

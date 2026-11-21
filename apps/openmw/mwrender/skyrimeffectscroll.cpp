#include "skyrimeffectscroll.hpp"

#include <algorithm>
#include <cmath>

#include <components/nifscene/uvanim.hpp>

namespace MWRender::SkyrimEffectScroll
{
    namespace
    {
        bool same(const vsg::mat4& a, const vsg::mat4& b)
        {
            for (std::uint32_t c = 0; c < 4; ++c)
                for (std::uint32_t r = 0; r < 4; ++r)
                    if (a(c, r) != b(c, r))
                        return false;
            return true;
        }
    }

    vsg::mat4 matrixAt(const NifScene::UvAnimation& animation, double clock)
    {
        const std::array<float, 16> values = NifScene::UvAnim::matrixAt(animation, clock);
        vsg::mat4 m;
        for (std::uint32_t c = 0; c < 4; ++c)
            for (std::uint32_t r = 0; r < 4; ++r)
                m(c, r) = values[c * 4 + r];
        return m;
    }

    void Driver::add(
        const vsg::ref_ptr<vsg::mat4Value>& uniform, std::shared_ptr<const NifScene::UvAnimation> animation)
    {
        if (!uniform || !animation)
            return;
        std::lock_guard lock(mMutex);
        uniform->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        uniform->value() = matrixAt(*animation, mSeconds);
        mEntries.push_back({ vsg::observer_ptr<vsg::mat4Value>(uniform), std::move(animation) });
    }

    void Driver::update(double seconds)
    {
        std::lock_guard lock(mMutex);
        if (std::isfinite(seconds) && seconds > 0.0)
            mSeconds += seconds;
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            vsg::ref_ptr<vsg::mat4Value> uniform = it->mUniform.ref_ptr();
            if (!uniform)
            {
                it = mEntries.erase(it);
                continue;
            }
            const vsg::mat4 next = matrixAt(*it->mAnimation, mSeconds);
            if (!same(uniform->value(), next))
            {
                uniform->value() = next;
                uniform->dirty();
            }
            ++it;
        }
    }

    double Driver::clockSeconds() const
    {
        std::lock_guard lock(mMutex);
        return mSeconds;
    }

    std::size_t Driver::liveCount() const
    {
        std::lock_guard lock(mMutex);
        return static_cast<std::size_t>(std::count_if(
            mEntries.begin(), mEntries.end(), [](const Entry& e) { return e.mUniform.ref_ptr() != nullptr; }));
    }
}

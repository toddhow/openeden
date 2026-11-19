#ifndef OPENMW_MWRENDER_FOGMANAGER_H
#define OPENMW_MWRENDER_FOGMANAGER_H

#include <vsg/maths/vec4.h>

#include <components/render/fogcurve.hpp>

namespace MWWorld
{
    class Cell;
}

namespace MWRender
{
    class FogManager
    {
    public:
        FogManager();

        void configure(float viewDistance, const MWWorld::Cell& cell);
        void configure(float viewDistance, float fogDepth, float underwaterFog, float dlFactor, float dlOffset,
            const vsg::vec4& color);

        vsg::vec4 getFogColor(bool isUnderwater) const;
        float getFogStart(bool isUnderwater) const;
        float getFogEnd(bool isUnderwater) const;
        /// Task 0355 (follow-up): the power and maximum of the land fog; the plain linear fog (1, 1) for everything but
        /// a game pack's interior, and never under water.
        Render::FogCurve::Curve getFogCurve(bool isUnderwater) const
        {
            return isUnderwater ? Render::FogCurve::Curve{} : mLandFogCurve;
        }

    private:
        float mLandFogStart;
        float mLandFogEnd;
        float mUnderwaterFogStart;
        float mUnderwaterFogEnd;
        Render::FogCurve::Curve mLandFogCurve;
        vsg::vec4 mFogColor;
        vsg::vec4 mUnderwaterColor;
        float mUnderwaterWeight;
        float mUnderwaterIndoorFog;
    };
}

#endif

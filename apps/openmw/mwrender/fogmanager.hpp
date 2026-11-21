#ifndef OPENMW_MWRENDER_FOGMANAGER_H
#define OPENMW_MWRENDER_FOGMANAGER_H

#include <vsg/maths/vec3.h>
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
        /// Task 0362 item 1: `otherColour` (when not null) is the fog's second colour: `color` stays the colour the scene
        /// uses everywhere (the background, water, particles ...); `otherIsEnd` says the other colour is the fog's far end
        /// (then `color` is where it starts) or its start (then `color` is the far end). In the object and terrain
        /// shaders the fog goes from one to the other with its linear share (lib/view/fog.glsl).
        void configure(float viewDistance, float fogDepth, float underwaterFog, float dlFactor, float dlOffset,
            const vsg::vec4& color, const vsg::vec3* otherColour = nullptr, bool otherIsEnd = false);

        vsg::vec4 getFogColor(bool isUnderwater) const;
        /// The fog's second colour (never under water); null when it has one colour. See configure.
        const vsg::vec3* getFogOtherColor(bool isUnderwater) const
        {
            return (!isUnderwater && mHasFogOtherColor) ? &mFogOtherColor : nullptr;
        }
        bool getFogOtherIsEnd() const { return mFogOtherIsEnd; }
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
        bool mHasFogOtherColor = false;
        bool mFogOtherIsEnd = false;
        vsg::vec3 mFogOtherColor{ 0.f, 0.f, 0.f };
        vsg::vec4 mUnderwaterColor;
        float mUnderwaterWeight;
        float mUnderwaterIndoorFog;
    };
}

#endif

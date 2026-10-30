#include "detourdebugdraw.hpp"

namespace SceneUtil
{
    vsg::vec4 colourFromRGBA(unsigned int value)
    {
        const auto component
            = [&](unsigned int shift) { return static_cast<float>((value >> shift) & 0xFFu) / 255.0f; };
        return vsg::vec4(component(0), component(8), component(16), component(24));
    }

    DebugDraw::DebugDraw(DebugPrimitiveSink& sink, const vsg::vec3& shift, float recastInvertedScaleFactor)
        : mSink(sink)
        , mShift(shift)
        , mRecastInvertedScaleFactor(recastInvertedScaleFactor)
    {
    }

    void DebugDraw::depthMask(bool) {}

    void DebugDraw::texture(bool) {}

    void DebugDraw::begin(duDebugDrawPrimitives prim, float)
    {
        mPrimitive = prim;
        mPendingCount = 0;
    }

    void DebugDraw::vertex(const float* pos, unsigned color)
    {
        vertex(pos[0], pos[1], pos[2], color);
    }

    void DebugDraw::vertex(const float x, const float y, const float z, unsigned color)
    {
        // Recast is Y up; the world is Z up.
        const vsg::vec3 position = vsg::vec3(x, z, y) * mRecastInvertedScaleFactor + mShift;
        mPending[mPendingCount++] = Vertex{ position, colourFromRGBA(color) };

        switch (mPrimitive)
        {
            case DU_DRAW_POINTS:
            {
                const Vertex& v = mPending[0];
                for (int axis = 0; axis < 3; ++axis)
                {
                    vsg::vec3 offset(0, 0, 0);
                    offset[axis] = sPointCrossHalfSize;
                    mSink.addLine(v.mPosition - offset, v.mPosition + offset, v.mColor);
                }
                mPendingCount = 0;
                break;
            }
            case DU_DRAW_LINES:
                if (mPendingCount == 2)
                {
                    mSink.addLine(mPending[0].mPosition, mPending[1].mPosition, mPending[0].mColor);
                    mPendingCount = 0;
                }
                break;
            case DU_DRAW_TRIS:
                if (mPendingCount == 3)
                {
                    mSink.addTriangle(
                        mPending[0].mPosition, mPending[1].mPosition, mPending[2].mPosition, mPending[0].mColor);
                    mPendingCount = 0;
                }
                break;
            case DU_DRAW_QUADS:
                if (mPendingCount == 4)
                {
                    mSink.addTriangle(
                        mPending[0].mPosition, mPending[1].mPosition, mPending[2].mPosition, mPending[0].mColor);
                    mSink.addTriangle(
                        mPending[0].mPosition, mPending[2].mPosition, mPending[3].mPosition, mPending[0].mColor);
                    mPendingCount = 0;
                }
                break;
        }
    }

    void DebugDraw::vertex(const float* pos, unsigned color, const float* uv)
    {
        vertex(pos[0], pos[1], pos[2], color, uv[0], uv[1]);
    }

    void DebugDraw::vertex(const float x, const float y, const float z, unsigned color, const float, const float)
    {
        vertex(x, y, z, color);
    }

    void DebugDraw::end()
    {
        // An incomplete primitive is dropped, as glDrawArrays did in the OSG build.
        mPendingCount = 0;
    }
}

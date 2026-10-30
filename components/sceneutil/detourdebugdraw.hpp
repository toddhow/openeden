#ifndef OPENMW_COMPONENTS_SCENEUTIL_DETOURDEBUGDRAW_H
#define OPENMW_COMPONENTS_SCENEUTIL_DETOURDEBUGDRAW_H

#include <DebugDraw.h>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <array>

namespace SceneUtil
{
    /// Receives debug primitives in world coordinates (OpenEden task 0011). The renderer's debug geometry implements
    /// it; so can a recorder that keeps primitives to replay later.
    class DebugPrimitiveSink
    {
    public:
        virtual ~DebugPrimitiveSink() = default;

        virtual void addLine(const vsg::vec3& from, const vsg::vec3& to, const vsg::vec4& color) = 0;

        virtual void addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color)
            = 0;
    };

    /// Converts a packed duRGBA colour (red in the low byte) to a colour with components in [0, 1].
    vsg::vec4 colourFromRGBA(unsigned int value);

    /**
     * @brief Recast's debug draw interface over a DebugPrimitiveSink, with no renderer types (OpenEden task 0011;
     * the OSG build built osg::Geometry here).
     *
     * Vertices arrive in Recast coordinates (Y up, scaled by the recast scale factor); each is converted to world
     * coordinates by swapping Y and Z, scaling by recastInvertedScaleFactor and adding shift. Lines and triangles
     * pass through; quads become two triangles; points become small three-axis crosses, since Vulkan has no point
     * size without shader support (the OSG build drew them as one-pixel points). Allocates nothing.
     */
    class DebugDraw : public duDebugDraw
    {
    public:
        /// Half the length of each arm of the cross drawn for a point, in world units.
        static constexpr float sPointCrossHalfSize = 2.0f;

        explicit DebugDraw(DebugPrimitiveSink& sink, const vsg::vec3& shift, float recastInvertedScaleFactor);

        void depthMask(bool state) override;

        void texture(bool state) override;

        void begin(duDebugDrawPrimitives prim, float size) override;

        void vertex(const float* pos, unsigned int color) override;

        void vertex(const float x, const float y, const float z, unsigned int color) override;

        void vertex(const float* pos, unsigned int color, const float* uv) override;

        void vertex(
            const float x, const float y, const float z, unsigned int color, const float u, const float v) override;

        void end() override;

    private:
        struct Vertex
        {
            vsg::vec3 mPosition;
            vsg::vec4 mColor;
        };

        DebugPrimitiveSink& mSink;
        vsg::vec3 mShift;
        float mRecastInvertedScaleFactor;
        duDebugDrawPrimitives mPrimitive = DU_DRAW_POINTS;
        std::array<Vertex, 4> mPending;
        std::size_t mPendingCount = 0;
    };
}

#endif

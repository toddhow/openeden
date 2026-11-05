#ifndef CSM_MODULE_OVERLAYBUFFER_H
#define CSM_MODULE_OVERLAYBUFFER_H

#include <cstddef>
#include <string_view>
#include <vector>

#include <openeden/editor_module/types.hpp>

#include <apps/opencs/model/physics/debuglinedrawer.hpp>

namespace CSMModule
{
    /// Most lines one drawing may hold: the overlay geometry of a 3D view holds 65,536 vertices
    /// (apps/opencs/view/render/physicshub.cpp, cLineVertexCapacity), two per line.
    inline constexpr std::size_t kMaxOverlayLines = 32768;
    /// Most filled triangles one drawing may hold (API 1.1). Three vertices of 28 bytes each: about 5 MB.
    inline constexpr std::size_t kMaxOverlayTriangles = 60000;
    /// A coordinate further than this from the module's origin is refused: it cannot be a body, a rig or a gizmo, and
    /// converted to the world's unit it would lose all its precision as a float.
    inline constexpr double kMaxOverlayCoordinateMetres = 1.0e6;

    /// One filled triangle in the world's unit, as the renderer is given it.
    struct OverlayTriangle
    {
        float mVertices[3][3];
        float mColour[4];
        bool mAlwaysOnTop = false;
    };

    /// What one drawing of the modules' gizmos came to.
    struct OverlayData
    {
        std::vector<CSMPhysics::DebugLine> mLines;
        std::vector<OverlayTriangle> mTriangles;
        std::size_t mRefused = 0; ///< Primitives with a coordinate or colour that is not finite, or out of reach.
        std::size_t mDropped = 0; ///< Valid primitives past kMaxOverlayLines / kMaxOverlayTriangles.
    };

    /// The overlay sink Eden Studio gives to the modules' gizmos (task 0031; API 1.1 adds triangles). It stores what is
    /// drawn as lines and triangles in the world's unit, moved by an origin: this is the one place where the modules'
    /// meters become world units. It is the host's defence against a module's hostile or broken data: a primitive with
    /// a non-finite or far-out coordinate, or a non-finite colour, is refused and counted; colour components are
    /// clamped to [0, 1]; past the caps further primitives are dropped and counted. No exception, no allocation beyond
    /// the two vectors (which never grow past the caps). Text labels are not drawn (the debug geometry has no text).
    ///
    /// Ownership: owns its data. Main thread only.
    class OverlayBuffer final : public openeden::editor::OverlaySink
    {
    public:
        /// \param origin the world position that module coordinates are relative to (three floats).
        OverlayBuffer(const float origin[3], double unitsPerMeter);

        void line(const openeden::editor::Vec3& a, const openeden::editor::Vec3& b,
            const openeden::editor::Colour& colour) override;
        void point(const openeden::editor::Vec3& p, const openeden::editor::Colour& colour, float sizePixels) override;
        void label(
            const openeden::editor::Vec3& p, std::string_view text, const openeden::editor::Colour& colour) override;
        void triangle(const openeden::editor::Vec3& a, const openeden::editor::Vec3& b, const openeden::editor::Vec3& c,
            const openeden::editor::Colour& colour, openeden::editor::OverlayDepth depth) override;

        const OverlayData& data() const { return mData; }
        OverlayData take() { return std::move(mData); }

    private:
        bool accepts(const openeden::editor::Vec3& p) const;
        bool accepts(const openeden::editor::Colour& c) const;
        void convert(const openeden::editor::Vec3& meters, float out[3]) const;
        void addLine(
            const openeden::editor::Vec3& a, const openeden::editor::Vec3& b, const openeden::editor::Colour& colour);

        OverlayData mData;
        double mOrigin[3];
        double mUnitsPerMeter;
    };
}

#endif

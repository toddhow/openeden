#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

#include <vsg/maths/transform.h>

#include <apps/opencs/view/render/selectionmarkergeometry.hpp>

// Written from files/opencs/selectionmarker.osgt, which this geometry replaces: the sizes are that file's bounds after
// its transforms (a factor of 253.52 for the move and scale parts and of 1 for the rotate rings), its triangle counts
// and its material colours; and from objectmarker.cpp and SceneWidget::update for the rest.
namespace
{
    namespace Marker = CSVRender::SelectionMarkerGeometry;
    constexpr float sTolerance = 2e-3f;

    struct Box
    {
        vsg::vec3 mMin{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max() };
        vsg::vec3 mMax{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest() };
    };

    Box boxOf(const std::vector<vsg::vec3>& vertices)
    {
        Box box;
        for (const auto& v : vertices)
            for (int i = 0; i < 3; ++i)
            {
                box.mMin[i] = std::min(box.mMin[i], v[i]);
                box.mMax[i] = std::max(box.mMax[i], v[i]);
            }
        return box;
    }

    const Marker::Part& find(const std::vector<Marker::Part>& parts, const std::string& name, bool alpha = false)
    {
        const auto it = std::find_if(parts.begin(), parts.end(),
            [&](const Marker::Part& part) { return part.mName == name && part.mAlpha == alpha; });
        if (it == parts.end())
            throw std::runtime_error("no part " + name);
        return *it;
    }

    void expectBox(const Marker::Part& part, const vsg::vec3& min, const vsg::vec3& max)
    {
        const Box box = boxOf(part.mVertices);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(box.mMin[i], min[i], sTolerance) << part.mName << " axis " << i;
            EXPECT_NEAR(box.mMax[i], max[i], sTolerance) << part.mName << " axis " << i;
        }
    }

    TEST(SelectionMarkerGeometryTest, moveHasThreeArrowsAndThreeWallsWithPanes)
    {
        const auto parts = Marker::moveParts();
        EXPECT_EQ(parts.size(), 9u);
        for (const char* name : { "X_Axis", "Y_Axis", "Z_Axis" })
            EXPECT_EQ(find(parts, name).mVertices.size(), 288u) << name;
        for (const char* name : { "X_Wall", "Y_Wall", "Z_Wall" })
        {
            EXPECT_EQ(find(parts, name).mVertices.size(), 48u) << name;
            EXPECT_EQ(find(parts, name, true).mVertices.size(), 12u) << name;
        }
    }

    TEST(SelectionMarkerGeometryTest, arrowsHaveTheOldFilesBounds)
    {
        const auto parts = Marker::moveParts();
        expectBox(find(parts, "X_Axis"), { 0.3803f, -0.1318f, -0.1318f }, { 2.5327f, 0.1318f, 0.1318f });
        expectBox(find(parts, "Y_Axis"), { -0.1318f, 0.3803f, -0.1318f }, { 0.1318f, 2.5327f, 0.1318f });
        expectBox(find(parts, "Z_Axis"), { -0.1318f, -0.1318f, 0.3803f }, { 0.1318f, 0.1318f, 2.5327f });
    }

    TEST(SelectionMarkerGeometryTest, wallsLieAcrossTheirAxisAndPanesInsideFrames)
    {
        const auto parts = Marker::moveParts();
        expectBox(find(parts, "Z_Wall"), { 1.1408f, 1.1408f, 0 }, { 1.3944f, 1.3944f, 0 });
        expectBox(find(parts, "Z_Wall", true), { 1.1662f, 1.1662f, 0 }, { 1.3690f, 1.3690f, 0 });
        expectBox(find(parts, "X_Wall"), { 0, 1.1408f, 1.1408f }, { 0, 1.3944f, 1.3944f });
        expectBox(find(parts, "Y_Wall"), { 1.1408f, 0, 1.1408f }, { 1.3944f, 0, 1.3944f });
    }

    TEST(SelectionMarkerGeometryTest, scalePartsEndInACube)
    {
        const auto parts = Marker::scaleParts();
        EXPECT_EQ(parts.size(), 9u);
        EXPECT_EQ(find(parts, "X_Axis_Scale").mVertices.size(), 180u);
        expectBox(find(parts, "X_Axis_Scale"), { 0.3803f, -0.1183f, -0.1183f }, { 2.2361f, 0.1183f, 0.1183f });
        expectBox(find(parts, "Z_Axis_Scale"), { -0.1183f, -0.1183f, 0.3803f }, { 0.1183f, 0.1183f, 2.2361f });
        expectBox(find(parts, "Y_Wall_Scale"), { 1.1408f, 0, 1.1408f }, { 1.3944f, 0, 1.3944f });
    }

    TEST(SelectionMarkerGeometryTest, rotateRingsAreToriAcrossTheirAxis)
    {
        const auto parts = Marker::rotateParts();
        EXPECT_EQ(parts.size(), 3u);
        EXPECT_EQ(find(parts, "X_Axis_Rot").mVertices.size(), 3456u);
        expectBox(find(parts, "X_Axis_Rot"), { -0.049f, -1.998f, -1.998f }, { 0.049f, 1.998f, 1.998f });
        expectBox(find(parts, "Z_Axis_Rot"), { -1.998f, -1.998f, -0.049f }, { 1.998f, 1.998f, 0.049f });
    }

    TEST(SelectionMarkerGeometryTest, partsNameTheirAxis)
    {
        for (const auto& parts : { Marker::moveParts(), Marker::scaleParts(), Marker::rotateParts() })
            for (const auto& part : parts)
                EXPECT_EQ(part.mAxis, part.mName[0] - 'X') << part.mName;
    }

    TEST(SelectionMarkerGeometryTest, solidPartsFaceOutwards)
    {
        // The picking rule counts a triangle only from its front. An arrow's triangles face away from its axis or along
        // it.
        for (const char* name : { "X_Axis", "Y_Axis", "Z_Axis" })
        {
            const auto parts = Marker::moveParts();
            const auto& vertices = find(parts, name).mVertices;
            const int axis = name[0] - 'X';
            int outward = 0;
            for (std::size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
                const vsg::vec3 normal = vsg::cross(vertices[i + 1] - vertices[i], vertices[i + 2] - vertices[i]);
                vsg::vec3 centre = (vertices[i] + vertices[i + 1] + vertices[i + 2]) / 3.f;
                centre[axis] = 0;
                vsg::vec3 side = normal;
                side[axis] = 0;
                if (vsg::dot(side, centre) >= -1e-6f)
                    ++outward;
            }
            EXPECT_EQ(outward, static_cast<int>(vertices.size() / 3)) << name;
        }
    }

    TEST(SelectionMarkerGeometryTest, coloursAreTheOldMaterialsAtAQuarterUnlessHighlighted)
    {
        const vsg::vec4 red = Marker::axisColour(0, false, false);
        EXPECT_NEAR(red.r, 0.8f / 4, 1e-5f);
        EXPECT_NEAR(red.g, 0.123281f / 4, 1e-5f);
        EXPECT_NEAR(red.b, 0.11568f / 4, 1e-5f);
        EXPECT_FLOAT_EQ(red.a, 1.f);
        const vsg::vec4 green = Marker::axisColour(1, true, false);
        EXPECT_NEAR(green.r, 0.190456f, 1e-5f);
        EXPECT_NEAR(green.g, 0.8f, 1e-5f);
        const vsg::vec4 blue = Marker::axisColour(2, true, true);
        EXPECT_NEAR(blue.b, 0.8f, 1e-5f);
        // The old file's pane blends by a constant of 0.5.
        EXPECT_FLOAT_EQ(blue.a, 0.5f);
    }

    TEST(SelectionMarkerGeometryTest, ringClipKeepsOnlyTheHalfFacingTheEye)
    {
        const auto parts = Marker::rotateParts();
        const auto& full = find(parts, "X_Axis_Rot").mVertices;
        std::vector<vsg::vec3> clipped(full.size());
        Marker::clipRing(full, vsg::vec3(0, 0, 1), clipped);
        std::size_t kept = 0;
        for (std::size_t i = 0; i + 2 < clipped.size(); i += 3)
        {
            const bool collapsed = clipped[i] == clipped[i + 1] && clipped[i] == clipped[i + 2];
            if (collapsed)
                continue;
            ++kept;
            // What stays is what the old clip plane left, whole triangles at its edge.
            EXPECT_GE(clipped[i].z + clipped[i + 1].z + clipped[i + 2].z, 0.f);
        }
        EXPECT_GT(kept, full.size() / 3 * 2 / 5);
        EXPECT_LT(kept, full.size() / 3 * 3 / 5);
    }

    TEST(SelectionMarkerGeometryTest, flipFollowsTheOldQuadrantRule)
    {
        // SceneWidget::update: flip when the camera is neither in the front right quadrant (both above 0.1) nor
        // anywhere but behind (either at or above 1).
        EXPECT_FALSE(Marker::cameraWantsFlip({ 100, 100, 50 }));
        EXPECT_TRUE(Marker::cameraWantsFlip({ -100, -100, 50 }));
        EXPECT_FALSE(Marker::cameraWantsFlip({ -100, 100, 50 }));
        EXPECT_FALSE(Marker::cameraWantsFlip({ 100, -100, 50 }));
    }

    TEST(SelectionMarkerGeometryTest, sizeGrowsWithDistanceAndNeverShrinksBelowOne)
    {
        EXPECT_FLOAT_EQ(Marker::distanceScale(750.f), 10.f);
        EXPECT_FLOAT_EQ(Marker::distanceScale(10.f), 1.f);
    }

    TEST(SelectionMarkerGeometryTest, hitBehindTheMarkerOnlyCountsForRotate)
    {
        const vsg::dvec3 marker(0, 0, 0);
        const vsg::dvec3 forward(0, 1, 0);
        EXPECT_TRUE(Marker::hitBehind(vsg::dvec3(0, 5, 0), marker, forward));
        EXPECT_FALSE(Marker::hitBehind(vsg::dvec3(0, -5, 0), marker, forward));
    }

    TEST(SelectionMarkerGeometryTest, outlineIsTwelveEdgesOfTheBox)
    {
        const auto lines = Marker::boxLines({ -1, -2, -3 }, { 1, 2, 3 });
        EXPECT_EQ(lines.size(), 24u);
        for (std::size_t i = 0; i + 1 < lines.size(); i += 2)
        {
            int differences = 0;
            for (int axis = 0; axis < 3; ++axis)
                if (lines[i][axis] != lines[i + 1][axis])
                    ++differences;
            EXPECT_EQ(differences, 1);
        }
    }
}

// Tests for the renderer-free navmesh debug draw (OpenEden task 0011), written from the task's specification.

#include <components/sceneutil/detourdebugdraw.hpp>

#include <DebugDraw.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <array>
#include <vector>

namespace
{
    using namespace testing;
    using namespace SceneUtil;

    constexpr float sTolerance = 1e-6f;

    struct RecordedLine
    {
        vsg::vec3 mFrom;
        vsg::vec3 mTo;
        vsg::vec4 mColor;
    };

    struct RecordedTriangle
    {
        vsg::vec3 mA;
        vsg::vec3 mB;
        vsg::vec3 mC;
        vsg::vec4 mColor;
    };

    enum class Kind
    {
        Line,
        Triangle,
    };

    class RecordingSink final : public DebugPrimitiveSink
    {
    public:
        std::vector<RecordedLine> mLines;
        std::vector<RecordedTriangle> mTriangles;
        std::vector<Kind> mOrder;

        void addLine(const vsg::vec3& from, const vsg::vec3& to, const vsg::vec4& color) override
        {
            mLines.push_back({ from, to, color });
            mOrder.push_back(Kind::Line);
        }

        void addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color) override
        {
            mTriangles.push_back({ a, b, c, color });
            mOrder.push_back(Kind::Triangle);
        }

        std::size_t total() const { return mOrder.size(); }
    };

    void expectVec3Near(const vsg::vec3& actual, const vsg::vec3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, sTolerance);
        EXPECT_NEAR(actual.y, expected.y, sTolerance);
        EXPECT_NEAR(actual.z, expected.z, sTolerance);
    }

    void expectVec4Near(const vsg::vec4& actual, const vsg::vec4& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, sTolerance);
        EXPECT_NEAR(actual.y, expected.y, sTolerance);
        EXPECT_NEAR(actual.z, expected.z, sTolerance);
        EXPECT_NEAR(actual.w, expected.w, sTolerance);
    }

    struct SceneUtilDetourDebugDrawTest : Test
    {
        RecordingSink mSink;
        // Values chosen so the conversion arithmetic is exact in float.
        const vsg::vec3 mShift{ 10.0f, 20.0f, 30.0f };
        const float mScale = 2.0f;
        DebugDraw mDraw{ mSink, mShift, mScale };

        const unsigned mRed = duRGBA(255, 0, 0, 255);
        const unsigned mGreen = duRGBA(0, 255, 0, 255);
        const unsigned mBlue = duRGBA(0, 0, 255, 255);
        const unsigned mWhite = duRGBA(255, 255, 255, 255);

        // World position for a Recast vertex: Y and Z swapped, scaled, then shifted.
        vsg::vec3 world(float x, float y, float z) const { return vsg::vec3(x, z, y) * mScale + mShift; }
    };

    TEST(SceneUtilColourFromRGBATest, convertsPackedComponentsToUnitRange)
    {
        const vsg::vec4 colour = colourFromRGBA(duRGBA(255, 0, 51, 102));
        expectVec4Near(colour, vsg::vec4(1.0f, 0.0f, 51.0f / 255.0f, 102.0f / 255.0f));
    }

    TEST(SceneUtilColourFromRGBATest, redIsLowestByteAndAlphaHighest)
    {
        const vsg::vec4 colour = colourFromRGBA(0xFF000000u | 0x000000FFu);
        EXPECT_EQ(colour.x, 1.0f);
        EXPECT_EQ(colour.y, 0.0f);
        EXPECT_EQ(colour.z, 0.0f);
        EXPECT_EQ(colour.w, 1.0f);
    }

    TEST(SceneUtilColourFromRGBATest, distinctComponents)
    {
        const vsg::vec4 colour = colourFromRGBA(duRGBA(10, 20, 30, 40));
        expectVec4Near(colour, vsg::vec4(10.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 40.0f / 255.0f));
    }

    TEST(SceneUtilColourFromRGBATest, zeroIsTransparentBlack)
    {
        const vsg::vec4 colour = colourFromRGBA(0u);
        EXPECT_EQ(colour.x, 0.0f);
        EXPECT_EQ(colour.y, 0.0f);
        EXPECT_EQ(colour.z, 0.0f);
        EXPECT_EQ(colour.w, 0.0f);
    }

    TEST_F(SceneUtilDetourDebugDrawTest, lineVerticesAreSwappedScaledAndShifted)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(1.0f, 2.0f, 3.0f, mRed);
        mDraw.vertex(-4.0f, 5.5f, 0.25f, mRed);
        mDraw.end();

        ASSERT_EQ(mSink.mLines.size(), 1u);
        EXPECT_TRUE(mSink.mTriangles.empty());
        const RecordedLine& line = mSink.mLines[0];
        EXPECT_EQ(line.mFrom, vsg::vec3(12.0f, 26.0f, 34.0f));
        EXPECT_EQ(line.mTo, vsg::vec3(2.0f, 20.5f, 41.0f));
        EXPECT_EQ(line.mFrom, world(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(line.mTo, world(-4.0f, 5.5f, 0.25f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, conversionUsesGivenShiftAndScale)
    {
        RecordingSink sink;
        DebugDraw draw(sink, vsg::vec3(-1.0f, 0.5f, 100.0f), 0.5f);
        draw.begin(DU_DRAW_LINES, 1.0f);
        draw.vertex(4.0f, 8.0f, 16.0f, mRed);
        draw.vertex(0.0f, 0.0f, 0.0f, mRed);
        draw.end();

        ASSERT_EQ(sink.mLines.size(), 1u);
        EXPECT_EQ(sink.mLines[0].mFrom, vsg::vec3(1.0f, 8.5f, 104.0f));
        EXPECT_EQ(sink.mLines[0].mTo, vsg::vec3(-1.0f, 0.5f, 100.0f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, lineUsesFirstVertexColour)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, duRGBA(255, 0, 51, 102));
        mDraw.vertex(1.0f, 1.0f, 1.0f, mGreen);
        mDraw.end();

        ASSERT_EQ(mSink.mLines.size(), 1u);
        expectVec4Near(mSink.mLines[0].mColor, colourFromRGBA(duRGBA(255, 0, 51, 102)));
        expectVec4Near(mSink.mLines[0].mColor, vsg::vec4(1.0f, 0.0f, 0.2f, 0.4f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, everyTwoLineVerticesMakeOneLine)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(2.0f, 0.0f, 0.0f, mGreen);
        mDraw.vertex(3.0f, 0.0f, 0.0f, mBlue);
        mDraw.end();

        ASSERT_EQ(mSink.mLines.size(), 2u);
        EXPECT_EQ(mSink.mLines[0].mFrom, world(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mLines[0].mTo, world(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mLines[1].mFrom, world(2.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mLines[1].mTo, world(3.0f, 0.0f, 0.0f));
        expectVec4Near(mSink.mLines[0].mColor, colourFromRGBA(mRed));
        expectVec4Near(mSink.mLines[1].mColor, colourFromRGBA(mGreen));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, nothingIsEmittedBeforeLineIsComplete)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        EXPECT_EQ(mSink.total(), 0u);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        EXPECT_EQ(mSink.mLines.size(), 1u);
        mDraw.vertex(2.0f, 0.0f, 0.0f, mRed);
        EXPECT_EQ(mSink.mLines.size(), 1u);
        mDraw.end();
    }

    TEST_F(SceneUtilDetourDebugDrawTest, trianglesFromEveryThreeVertices)
    {
        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mGreen);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mBlue);
        mDraw.vertex(2.0f, 1.0f, 0.0f, mBlue);
        mDraw.vertex(3.0f, 1.0f, 0.0f, mRed);
        mDraw.vertex(2.0f, 1.0f, 1.0f, mGreen);
        mDraw.end();

        EXPECT_TRUE(mSink.mLines.empty());
        ASSERT_EQ(mSink.mTriangles.size(), 2u);
        const RecordedTriangle& first = mSink.mTriangles[0];
        EXPECT_EQ(first.mA, world(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(first.mB, world(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(first.mC, world(0.0f, 0.0f, 1.0f));
        expectVec4Near(first.mColor, colourFromRGBA(mRed));
        const RecordedTriangle& second = mSink.mTriangles[1];
        EXPECT_EQ(second.mA, world(2.0f, 1.0f, 0.0f));
        EXPECT_EQ(second.mB, world(3.0f, 1.0f, 0.0f));
        EXPECT_EQ(second.mC, world(2.0f, 1.0f, 1.0f));
        expectVec4Near(second.mColor, colourFromRGBA(mBlue));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, nothingIsEmittedBeforeTriangleIsComplete)
    {
        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        EXPECT_EQ(mSink.total(), 0u);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mRed);
        EXPECT_EQ(mSink.mTriangles.size(), 1u);
        mDraw.end();
        EXPECT_EQ(mSink.total(), 1u);
    }

    TEST_F(SceneUtilDetourDebugDrawTest, quadBecomesTwoTrianglesWithFirstVertexColour)
    {
        mDraw.begin(DU_DRAW_QUADS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mGreen);
        mDraw.vertex(1.0f, 0.0f, 1.0f, mBlue);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mWhite);
        mDraw.end();

        EXPECT_TRUE(mSink.mLines.empty());
        ASSERT_EQ(mSink.mTriangles.size(), 2u);
        const vsg::vec3 v0 = world(0.0f, 0.0f, 0.0f);
        const vsg::vec3 v1 = world(1.0f, 0.0f, 0.0f);
        const vsg::vec3 v2 = world(1.0f, 0.0f, 1.0f);
        const vsg::vec3 v3 = world(0.0f, 0.0f, 1.0f);
        EXPECT_EQ(mSink.mTriangles[0].mA, v0);
        EXPECT_EQ(mSink.mTriangles[0].mB, v1);
        EXPECT_EQ(mSink.mTriangles[0].mC, v2);
        EXPECT_EQ(mSink.mTriangles[1].mA, v0);
        EXPECT_EQ(mSink.mTriangles[1].mB, v2);
        EXPECT_EQ(mSink.mTriangles[1].mC, v3);
        expectVec4Near(mSink.mTriangles[0].mColor, colourFromRGBA(mRed));
        expectVec4Near(mSink.mTriangles[1].mColor, colourFromRGBA(mRed));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, nothingIsEmittedBeforeQuadIsComplete)
    {
        mDraw.begin(DU_DRAW_QUADS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 1.0f, mRed);
        EXPECT_EQ(mSink.total(), 0u);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mRed);
        EXPECT_EQ(mSink.mTriangles.size(), 2u);
        mDraw.end();
        EXPECT_EQ(mSink.total(), 2u);
    }

    TEST_F(SceneUtilDetourDebugDrawTest, twoQuadsMakeFourTriangles)
    {
        mDraw.begin(DU_DRAW_QUADS, 1.0f);
        for (int i = 0; i < 2; ++i)
        {
            const float o = static_cast<float>(i) * 4.0f;
            const unsigned colour = i == 0 ? mRed : mGreen;
            mDraw.vertex(o, 0.0f, 0.0f, colour);
            mDraw.vertex(o + 1.0f, 0.0f, 0.0f, mBlue);
            mDraw.vertex(o + 1.0f, 0.0f, 1.0f, mBlue);
            mDraw.vertex(o, 0.0f, 1.0f, mBlue);
        }
        mDraw.end();

        ASSERT_EQ(mSink.mTriangles.size(), 4u);
        EXPECT_EQ(mSink.mTriangles[2].mA, world(4.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mTriangles[3].mC, world(4.0f, 0.0f, 1.0f));
        expectVec4Near(mSink.mTriangles[2].mColor, colourFromRGBA(mGreen));
        expectVec4Near(mSink.mTriangles[3].mColor, colourFromRGBA(mGreen));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, pointBecomesThreeAxisCross)
    {
        mDraw.begin(DU_DRAW_POINTS, 5.0f);
        mDraw.vertex(1.0f, 2.0f, 3.0f, mRed);
        mDraw.end();

        EXPECT_TRUE(mSink.mTriangles.empty());
        ASSERT_EQ(mSink.mLines.size(), 3u);
        const vsg::vec3 p = world(1.0f, 2.0f, 3.0f);
        const float h = DebugDraw::sPointCrossHalfSize;
        const std::array<vsg::vec3, 3> offsets{
            vsg::vec3(h, 0.0f, 0.0f),
            vsg::vec3(0.0f, h, 0.0f),
            vsg::vec3(0.0f, 0.0f, h),
        };
        for (std::size_t i = 0; i < 3; ++i)
        {
            SCOPED_TRACE(i);
            expectVec3Near(mSink.mLines[i].mFrom, p - offsets[i]);
            expectVec3Near(mSink.mLines[i].mTo, p + offsets[i]);
            expectVec4Near(mSink.mLines[i].mColor, colourFromRGBA(mRed));
        }
    }

    TEST_F(SceneUtilDetourDebugDrawTest, eachPointMakesItsOwnCrossWithItsOwnColour)
    {
        mDraw.begin(DU_DRAW_POINTS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        EXPECT_EQ(mSink.mLines.size(), 3u);
        mDraw.vertex(10.0f, 0.0f, 0.0f, mGreen);
        mDraw.end();

        ASSERT_EQ(mSink.mLines.size(), 6u);
        const vsg::vec3 p = world(10.0f, 0.0f, 0.0f);
        const float h = DebugDraw::sPointCrossHalfSize;
        expectVec3Near(mSink.mLines[3].mFrom, p - vsg::vec3(h, 0.0f, 0.0f));
        expectVec3Near(mSink.mLines[3].mTo, p + vsg::vec3(h, 0.0f, 0.0f));
        expectVec3Near(mSink.mLines[5].mFrom, p - vsg::vec3(0.0f, 0.0f, h));
        expectVec3Near(mSink.mLines[5].mTo, p + vsg::vec3(0.0f, 0.0f, h));
        for (std::size_t i = 0; i < 3; ++i)
            expectVec4Near(mSink.mLines[i].mColor, colourFromRGBA(mRed));
        for (std::size_t i = 3; i < 6; ++i)
            expectVec4Near(mSink.mLines[i].mColor, colourFromRGBA(mGreen));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, sizeArgumentOfBeginIsIgnored)
    {
        RecordingSink otherSink;
        DebugDraw other(otherSink, mShift, mScale);

        mDraw.begin(DU_DRAW_POINTS, 1.0f);
        other.begin(DU_DRAW_POINTS, 25.0f);
        mDraw.vertex(1.0f, 2.0f, 3.0f, mRed);
        other.vertex(1.0f, 2.0f, 3.0f, mRed);
        mDraw.end();
        other.end();

        mDraw.begin(DU_DRAW_LINES, 0.5f);
        other.begin(DU_DRAW_LINES, 8.0f);
        mDraw.vertex(1.0f, 2.0f, 3.0f, mRed);
        other.vertex(1.0f, 2.0f, 3.0f, mRed);
        mDraw.vertex(4.0f, 5.0f, 6.0f, mRed);
        other.vertex(4.0f, 5.0f, 6.0f, mRed);
        mDraw.end();
        other.end();

        ASSERT_EQ(mSink.mLines.size(), otherSink.mLines.size());
        ASSERT_EQ(mSink.mLines.size(), 4u);
        for (std::size_t i = 0; i < mSink.mLines.size(); ++i)
        {
            EXPECT_EQ(mSink.mLines[i].mFrom, otherSink.mLines[i].mFrom);
            EXPECT_EQ(mSink.mLines[i].mTo, otherSink.mLines[i].mTo);
            EXPECT_EQ(mSink.mLines[i].mColor, otherSink.mLines[i].mColor);
        }
    }

    TEST_F(SceneUtilDetourDebugDrawTest, danglingLineVertexIsDroppedAtEnd)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(1.0f, 2.0f, 3.0f, mRed);
        mDraw.end();
        EXPECT_EQ(mSink.total(), 0u);

        // The dropped vertex must not pair with a vertex of the next primitive.
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(4.0f, 0.0f, 0.0f, mGreen);
        mDraw.vertex(5.0f, 0.0f, 0.0f, mGreen);
        mDraw.end();

        ASSERT_EQ(mSink.mLines.size(), 1u);
        EXPECT_EQ(mSink.mLines[0].mFrom, world(4.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mLines[0].mTo, world(5.0f, 0.0f, 0.0f));
        expectVec4Near(mSink.mLines[0].mColor, colourFromRGBA(mGreen));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, incompleteTriangleAndQuadAreDroppedAtEnd)
    {
        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.end();

        mDraw.begin(DU_DRAW_QUADS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 1.0f, mRed);
        mDraw.end();

        EXPECT_EQ(mSink.total(), 0u);
    }

    TEST_F(SceneUtilDetourDebugDrawTest, beginDiscardsIncompletePrimitiveWithoutEnd)
    {
        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(9.0f, 9.0f, 9.0f, mRed);
        mDraw.vertex(8.0f, 8.0f, 8.0f, mRed);

        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mGreen);
        EXPECT_EQ(mSink.total(), 0u);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mGreen);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mGreen);
        mDraw.end();

        ASSERT_EQ(mSink.mTriangles.size(), 1u);
        EXPECT_TRUE(mSink.mLines.empty());
        EXPECT_EQ(mSink.mTriangles[0].mA, world(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mTriangles[0].mB, world(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mTriangles[0].mC, world(0.0f, 0.0f, 1.0f));
        expectVec4Near(mSink.mTriangles[0].mColor, colourFromRGBA(mGreen));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, beginWithDifferentPrimitiveDiscardsPendingVertices)
    {
        mDraw.begin(DU_DRAW_QUADS, 1.0f);
        mDraw.vertex(9.0f, 9.0f, 9.0f, mRed);
        mDraw.vertex(8.0f, 8.0f, 8.0f, mRed);
        mDraw.vertex(7.0f, 7.0f, 7.0f, mRed);

        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mBlue);
        EXPECT_EQ(mSink.total(), 0u);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mBlue);
        mDraw.end();

        EXPECT_TRUE(mSink.mTriangles.empty());
        ASSERT_EQ(mSink.mLines.size(), 1u);
        EXPECT_EQ(mSink.mLines[0].mFrom, world(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mLines[0].mTo, world(1.0f, 0.0f, 0.0f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, pointerVertexOverloadMatchesScalarOverload)
    {
        RecordingSink otherSink;
        DebugDraw other(otherSink, mShift, mScale);
        const float a[3] = { 1.0f, 2.0f, 3.0f };
        const float b[3] = { -4.0f, 5.5f, 0.25f };
        const float c[3] = { 6.0f, -1.0f, 2.0f };

        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(a, mRed);
        mDraw.vertex(b, mGreen);
        mDraw.vertex(c, mBlue);
        mDraw.end();

        other.begin(DU_DRAW_TRIS, 1.0f);
        other.vertex(a[0], a[1], a[2], mRed);
        other.vertex(b[0], b[1], b[2], mGreen);
        other.vertex(c[0], c[1], c[2], mBlue);
        other.end();

        ASSERT_EQ(mSink.mTriangles.size(), 1u);
        ASSERT_EQ(otherSink.mTriangles.size(), 1u);
        EXPECT_EQ(mSink.mTriangles[0].mA, otherSink.mTriangles[0].mA);
        EXPECT_EQ(mSink.mTriangles[0].mB, otherSink.mTriangles[0].mB);
        EXPECT_EQ(mSink.mTriangles[0].mC, otherSink.mTriangles[0].mC);
        EXPECT_EQ(mSink.mTriangles[0].mColor, otherSink.mTriangles[0].mColor);
        EXPECT_EQ(mSink.mTriangles[0].mA, world(1.0f, 2.0f, 3.0f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, texturedScalarVertexOverloadIgnoresUv)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(1.0f, 2.0f, 3.0f, mRed, 0.75f, 0.125f);
        mDraw.vertex(-4.0f, 5.5f, 0.25f, mGreen, 100.0f, -3.0f);
        mDraw.end();

        ASSERT_EQ(mSink.mLines.size(), 1u);
        EXPECT_EQ(mSink.mLines[0].mFrom, world(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(mSink.mLines[0].mTo, world(-4.0f, 5.5f, 0.25f));
        expectVec4Near(mSink.mLines[0].mColor, colourFromRGBA(mRed));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, texturedPointerVertexOverloadIgnoresUv)
    {
        const float a[3] = { 1.0f, 2.0f, 3.0f };
        const float b[3] = { 0.0f, 0.0f, 0.0f };
        const float c[3] = { 1.0f, 0.0f, 1.0f };
        const float d[3] = { 0.0f, 0.0f, 1.0f };
        const float uv[2] = { 0.5f, 0.25f };

        mDraw.begin(DU_DRAW_QUADS, 1.0f);
        mDraw.vertex(a, mWhite, uv);
        mDraw.vertex(b, mRed, uv);
        mDraw.vertex(c, mRed, uv);
        mDraw.vertex(d, mRed, uv);
        mDraw.end();

        ASSERT_EQ(mSink.mTriangles.size(), 2u);
        EXPECT_EQ(mSink.mTriangles[0].mA, world(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(mSink.mTriangles[0].mB, world(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mTriangles[0].mC, world(1.0f, 0.0f, 1.0f));
        EXPECT_EQ(mSink.mTriangles[1].mA, world(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(mSink.mTriangles[1].mB, world(1.0f, 0.0f, 1.0f));
        EXPECT_EQ(mSink.mTriangles[1].mC, world(0.0f, 0.0f, 1.0f));
        expectVec4Near(mSink.mTriangles[0].mColor, colourFromRGBA(mWhite));
        expectVec4Near(mSink.mTriangles[1].mColor, colourFromRGBA(mWhite));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, overloadsCanBeMixedWithinOnePrimitive)
    {
        const float b[3] = { 1.0f, 0.0f, 0.0f };
        const float uv[2] = { 0.0f, 1.0f };

        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed, 0.0f, 0.0f);
        mDraw.vertex(b, mGreen);
        mDraw.vertex(b, mBlue, uv);
        EXPECT_EQ(mSink.mTriangles.size(), 1u);
        mDraw.end();

        ASSERT_EQ(mSink.mTriangles.size(), 1u);
        EXPECT_EQ(mSink.mTriangles[0].mA, world(0.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mTriangles[0].mB, world(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(mSink.mTriangles[0].mC, world(1.0f, 0.0f, 0.0f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, depthMaskAndTextureHaveNoEffect)
    {
        RecordingSink otherSink;
        DebugDraw other(otherSink, mShift, mScale);

        mDraw.depthMask(false);
        mDraw.texture(true);
        EXPECT_EQ(mSink.total(), 0u);
        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.depthMask(true);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.texture(false);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mRed);
        mDraw.end();

        other.begin(DU_DRAW_TRIS, 1.0f);
        other.vertex(0.0f, 0.0f, 0.0f, mRed);
        other.vertex(1.0f, 0.0f, 0.0f, mRed);
        other.vertex(0.0f, 0.0f, 1.0f, mRed);
        other.end();

        ASSERT_EQ(mSink.mTriangles.size(), 1u);
        ASSERT_EQ(otherSink.mTriangles.size(), 1u);
        EXPECT_TRUE(mSink.mLines.empty());
        EXPECT_EQ(mSink.mTriangles[0].mA, otherSink.mTriangles[0].mA);
        EXPECT_EQ(mSink.mTriangles[0].mB, otherSink.mTriangles[0].mB);
        EXPECT_EQ(mSink.mTriangles[0].mC, otherSink.mTriangles[0].mC);
        EXPECT_EQ(mSink.mTriangles[0].mColor, otherSink.mTriangles[0].mColor);
    }

    TEST_F(SceneUtilDetourDebugDrawTest, drawsThroughBaseInterface)
    {
        duDebugDraw& base = mDraw;
        base.begin(DU_DRAW_LINES);
        base.vertex(1.0f, 2.0f, 3.0f, mRed);
        base.vertex(4.0f, 5.0f, 6.0f, mRed);
        base.end();

        ASSERT_EQ(mSink.mLines.size(), 1u);
        EXPECT_EQ(mSink.mLines[0].mFrom, world(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(mSink.mLines[0].mTo, world(4.0f, 5.0f, 6.0f));
    }

    TEST_F(SceneUtilDetourDebugDrawTest, primitivesReachSinkInSubmissionOrder)
    {
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.end();
        mDraw.begin(DU_DRAW_TRIS, 1.0f);
        mDraw.vertex(0.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(1.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(0.0f, 0.0f, 1.0f, mRed);
        mDraw.end();
        mDraw.begin(DU_DRAW_LINES, 1.0f);
        mDraw.vertex(2.0f, 0.0f, 0.0f, mRed);
        mDraw.vertex(3.0f, 0.0f, 0.0f, mRed);
        mDraw.end();

        EXPECT_THAT(mSink.mOrder, ElementsAre(Kind::Line, Kind::Triangle, Kind::Line));
    }
}

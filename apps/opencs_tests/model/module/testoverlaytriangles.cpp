// Editor Module API 1.1, filled overlay triangles (task 0031 follow-up): the version gate, the sink's default, and
// CSMModule::OverlayBuffer, the sink Eden Studio gives to gizmos. Written from the request alone, before the code.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"
#include "apps/opencs/model/module/overlaybuffer.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;

        constexpr double cNaN = std::numeric_limits<double>::quiet_NaN();
        constexpr double cInf = std::numeric_limits<double>::infinity();
        constexpr float cOrigin[3] = { 100.f, 200.f, 300.f };
        constexpr double cUnits = 70.0;

        // A sink as an API 1.0 test would write it: it overrides the three original functions only.
        class OldSink final : public OverlaySink
        {
        public:
            void line(const Vec3&, const Vec3&, const Colour&) override { ++mLines; }
            void point(const Vec3&, const Colour&, float) override {}
            void label(const Vec3&, std::string_view, const Colour&) override {}
            int mLines = 0;
        };

        class NullContext final : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view) override {}
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return nullptr; }
        };

        class EmptyModule final : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar&) override {}
            void shutdown() override {}
        };

        EditorModule* createEmpty()
        {
            return new EmptyModule();
        }

        void destroyEmpty(EditorModule* module)
        {
            delete module;
        }

        EditorModuleDescriptor descriptorFor(openeden::sdk::Version version)
        {
            return EditorModuleDescriptor{ version, "overlaytest", &createEmpty, &destroyEmpty };
        }

        const Colour cRed{ 1.f, 0.f, 0.f, 0.5f };

        void addTriangle(OverlayBuffer& buffer, OverlayDepth depth = OverlayDepth::Tested)
        {
            buffer.triangle(Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, cRed, depth);
        }

        TEST(EditorApi11, versionIsOneDotTwoAndOldModulesStillLoad)
        {
            EXPECT_EQ(kApiVersion, (openeden::sdk::Version{ 1, 2 }));
            EXPECT_TRUE(openeden::sdk::isCompatible(openeden::sdk::Version{ 1, 0 }, kApiVersion));
            EXPECT_TRUE(openeden::sdk::isCompatible(openeden::sdk::Version{ 1, 1 }, kApiVersion));
            EXPECT_FALSE(openeden::sdk::isCompatible(openeden::sdk::Version{ 1, 3 }, kApiVersion));
        }

        TEST(EditorApi11, aSinkWrittenForOneDotZeroKeepsWorkingAndIgnoresTriangles)
        {
            OldSink sink;
            sink.line(Vec3{}, Vec3{ 1, 1, 1 }, Colour{});
            sink.triangle(Vec3{}, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, Colour{}, OverlayDepth::AlwaysOnTop);
            EXPECT_EQ(sink.mLines, 1);
        }

        class ApiGateTest : public ::testing::Test
        {
        protected:
            ModuleRegistry mRegistry;
            NullContext mContext;
        };

        TEST_F(ApiGateTest, aOneDotOneModuleLoadsOnAOneDotOneHost)
        {
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d = descriptorFor({ 1, 1 });
            host.loadStatic("overlaytest", d, ModuleSource::Origin::Core);
            ASSERT_EQ(host.modules().size(), 1u);
            EXPECT_EQ(host.modules()[0].mState, ModuleInfo::State::Loaded) << host.modules()[0].mMessage;
        }

        TEST_F(ApiGateTest, aOneDotZeroModuleStillLoadsOnAOneDotOneHost)
        {
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d = descriptorFor({ 1, 0 });
            host.loadStatic("overlaytest", d, ModuleSource::Origin::Core);
            ASSERT_EQ(host.modules().size(), 1u);
            EXPECT_EQ(host.modules()[0].mState, ModuleInfo::State::Loaded) << host.modules()[0].mMessage;
        }

        TEST_F(ApiGateTest, aOneDotOneModuleIsRefusedByAOneDotZeroHostWithAMessageNamingBothVersions)
        {
            ModuleHost host(mRegistry, mContext);
            host.setProvidedApiVersion({ 1, 0 });
            const EditorModuleDescriptor d = descriptorFor({ 1, 1 });
            host.loadStatic("overlaytest", d, ModuleSource::Origin::Core);
            ASSERT_EQ(host.modules().size(), 1u);
            EXPECT_EQ(host.modules()[0].mState, ModuleInfo::State::Failed);
            EXPECT_NE(host.modules()[0].mMessage.find("1.1"), std::string::npos) << host.modules()[0].mMessage;
            EXPECT_NE(host.modules()[0].mMessage.find("provides 1.0"), std::string::npos) << host.modules()[0].mMessage;
        }

        TEST_F(ApiGateTest, aOneDotZeroModuleLoadsOnAOneDotZeroHost)
        {
            ModuleHost host(mRegistry, mContext);
            host.setProvidedApiVersion({ 1, 0 });
            const EditorModuleDescriptor d = descriptorFor({ 1, 0 });
            host.loadStatic("overlaytest", d, ModuleSource::Origin::Core);
            ASSERT_EQ(host.modules().size(), 1u);
            EXPECT_EQ(host.modules()[0].mState, ModuleInfo::State::Loaded) << host.modules()[0].mMessage;
        }

        // ---- OverlayBuffer ---------------------------------------------------------------------------------

        TEST(OverlayBuffer, aTriangleBecomesWorldUnitsMovedByTheOrigin)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            buffer.triangle(Vec3{ 1, 2, 3 }, Vec3{ 2, 2, 3 }, Vec3{ 1, 3, 3 }, Colour{ 0.1f, 0.2f, 0.3f, 0.4f },
                OverlayDepth::Tested);
            ASSERT_EQ(buffer.data().mTriangles.size(), 1u);
            const OverlayTriangle& t = buffer.data().mTriangles[0];
            EXPECT_FLOAT_EQ(t.mVertices[0][0], 100.f + 70.f);
            EXPECT_FLOAT_EQ(t.mVertices[0][1], 200.f + 140.f);
            EXPECT_FLOAT_EQ(t.mVertices[0][2], 300.f + 210.f);
            EXPECT_FLOAT_EQ(t.mVertices[1][0], 100.f + 140.f);
            EXPECT_FLOAT_EQ(t.mVertices[2][1], 200.f + 210.f);
            EXPECT_FLOAT_EQ(t.mColour[0], 0.1f);
            EXPECT_FLOAT_EQ(t.mColour[3], 0.4f);
            EXPECT_FALSE(t.mAlwaysOnTop);
            EXPECT_TRUE(buffer.data().mLines.empty());
        }

        TEST(OverlayBuffer, theDepthModeIsKept)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            addTriangle(buffer, OverlayDepth::Tested);
            addTriangle(buffer, OverlayDepth::AlwaysOnTop);
            ASSERT_EQ(buffer.data().mTriangles.size(), 2u);
            EXPECT_FALSE(buffer.data().mTriangles[0].mAlwaysOnTop);
            EXPECT_TRUE(buffer.data().mTriangles[1].mAlwaysOnTop);
        }

        TEST(OverlayBuffer, aNonFiniteCoordinateRefusesTheTriangleAndIsCounted)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            buffer.triangle(Vec3{ cNaN, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, cRed, OverlayDepth::Tested);
            buffer.triangle(Vec3{ 0, 0, 0 }, Vec3{ 1, cInf, 0 }, Vec3{ 0, 1, 0 }, cRed, OverlayDepth::Tested);
            buffer.triangle(Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, -cInf }, cRed, OverlayDepth::Tested);
            EXPECT_TRUE(buffer.data().mTriangles.empty());
            EXPECT_EQ(buffer.data().mRefused, 3u);
            EXPECT_EQ(buffer.data().mDropped, 0u);
        }

        TEST(OverlayBuffer, aCoordinateBeyondTheReachRefusesTheTriangle)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            buffer.triangle(Vec3{ kMaxOverlayCoordinateMetres * 2, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, cRed,
                OverlayDepth::Tested);
            buffer.triangle(Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1e300, 0 }, cRed, OverlayDepth::Tested);
            EXPECT_TRUE(buffer.data().mTriangles.empty());
            EXPECT_EQ(buffer.data().mRefused, 2u);
        }

        TEST(OverlayBuffer, aNonFiniteColourRefusesAndAnOutOfRangeColourIsClamped)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            const float nan = std::numeric_limits<float>::quiet_NaN();
            buffer.triangle(
                Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, Colour{ nan, 0, 0, 1 }, OverlayDepth::Tested);
            EXPECT_TRUE(buffer.data().mTriangles.empty());
            EXPECT_EQ(buffer.data().mRefused, 1u);
            buffer.triangle(Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 }, Colour{ 2.f, -1.f, 0.5f, 7.f },
                OverlayDepth::Tested);
            ASSERT_EQ(buffer.data().mTriangles.size(), 1u);
            const OverlayTriangle& t = buffer.data().mTriangles[0];
            EXPECT_FLOAT_EQ(t.mColour[0], 1.f);
            EXPECT_FLOAT_EQ(t.mColour[1], 0.f);
            EXPECT_FLOAT_EQ(t.mColour[2], 0.5f);
            EXPECT_FLOAT_EQ(t.mColour[3], 1.f);
        }

        TEST(OverlayBuffer, trianglesPastTheCapAreDroppedAndCounted)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            for (std::size_t i = 0; i < kMaxOverlayTriangles + 25; ++i)
                addTriangle(buffer);
            EXPECT_EQ(buffer.data().mTriangles.size(), kMaxOverlayTriangles);
            EXPECT_EQ(buffer.data().mDropped, 25u);
            EXPECT_EQ(buffer.data().mRefused, 0u);
        }

        TEST(OverlayBuffer, linesAreConvertedCappedAndCheckedToo)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            buffer.line(Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Colour{ 0.f, 1.f, 0.f, 1.f });
            ASSERT_EQ(buffer.data().mLines.size(), 1u);
            EXPECT_FLOAT_EQ(buffer.data().mLines[0].mFrom[0], 100.f);
            EXPECT_FLOAT_EQ(buffer.data().mLines[0].mTo[0], 170.f);
            EXPECT_FLOAT_EQ(buffer.data().mLines[0].mColour[1], 1.f);
            buffer.line(Vec3{ cNaN, 0, 0 }, Vec3{ 1, 0, 0 }, cRed);
            EXPECT_EQ(buffer.data().mLines.size(), 1u);
            EXPECT_EQ(buffer.data().mRefused, 1u);
            for (std::size_t i = 0; i < kMaxOverlayLines + 4; ++i)
                buffer.line(Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, cRed);
            EXPECT_EQ(buffer.data().mLines.size(), kMaxOverlayLines);
            EXPECT_EQ(buffer.data().mDropped, 5u);
        }

        TEST(OverlayBuffer, aPointIsThreeCrossingLinesAndLabelsDrawNothing)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            buffer.point(Vec3{ 1, 1, 1 }, cRed, 6.f);
            EXPECT_EQ(buffer.data().mLines.size(), 3u);
            buffer.label(Vec3{ 1, 1, 1 }, "text", cRed);
            EXPECT_EQ(buffer.data().mLines.size(), 3u);
            EXPECT_TRUE(buffer.data().mTriangles.empty());
            buffer.point(Vec3{ cNaN, 0, 0 }, cRed, 6.f);
            EXPECT_EQ(buffer.data().mLines.size(), 3u);
            EXPECT_EQ(buffer.data().mRefused, 1u);
        }

        TEST(OverlayBuffer, takeMovesTheDataOut)
        {
            OverlayBuffer buffer(cOrigin, cUnits);
            addTriangle(buffer);
            const OverlayData taken = buffer.take();
            EXPECT_EQ(taken.mTriangles.size(), 1u);
        }
    }
}

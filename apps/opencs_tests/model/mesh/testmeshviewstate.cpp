#include "apps/opencs/model/mesh/meshviewstate.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace CSMMesh
{
    namespace
    {
        constexpr std::array<ViewMode, 5> sAllModes{ ViewMode::Textured, ViewMode::Wireframe, ViewMode::Flat,
            ViewMode::VertexColours, ViewMode::Normals };
        constexpr std::array<LightingPreset, 3> sAllLighting{ LightingPreset::Headlight, LightingPreset::Sun,
            LightingPreset::Flat };

        TEST(CSMMeshMeshViewStateTest, defaultsMatchTheSpec)
        {
            const MeshViewState state;
            EXPECT_EQ(state.mode, ViewMode::Textured);
            EXPECT_EQ(state.lighting, LightingPreset::Headlight);
            EXPECT_FALSE(state.showCollision);
            EXPECT_FALSE(state.showBounds);
            EXPECT_FALSE(state.showSkeleton);
            EXPECT_FALSE(state.showNodeAxes);
            EXPECT_TRUE(state.showGrid);
            EXPECT_FLOAT_EQ(state.background[0], 0.16f);
            EXPECT_FLOAT_EQ(state.background[1], 0.18f);
            EXPECT_FLOAT_EQ(state.background[2], 0.22f);
        }

        TEST(CSMMeshMeshViewStateTest, viewModeNamesAreNonEmptyDistinctAndRoundTrip)
        {
            std::array<std::string, 5> names;
            for (std::size_t i = 0; i < sAllModes.size(); ++i)
            {
                const char* name = viewModeName(sAllModes[i]);
                ASSERT_NE(name, nullptr);
                names[i] = name;
                EXPECT_FALSE(names[i].empty());
                const auto parsed = parseViewMode(names[i]);
                ASSERT_TRUE(parsed.has_value()) << names[i];
                EXPECT_EQ(*parsed, sAllModes[i]);
            }
            for (std::size_t i = 0; i < names.size(); ++i)
                for (std::size_t j = i + 1; j < names.size(); ++j)
                    EXPECT_NE(names[i], names[j]);
        }

        TEST(CSMMeshMeshViewStateTest, parseViewModeAcceptsTheSpecNamesInAnyCase)
        {
            EXPECT_EQ(parseViewMode("textured"), ViewMode::Textured);
            EXPECT_EQ(parseViewMode("wireframe"), ViewMode::Wireframe);
            EXPECT_EQ(parseViewMode("flat"), ViewMode::Flat);
            EXPECT_EQ(parseViewMode("vertexcolours"), ViewMode::VertexColours);
            EXPECT_EQ(parseViewMode("normals"), ViewMode::Normals);

            EXPECT_EQ(parseViewMode("TEXTURED"), ViewMode::Textured);
            EXPECT_EQ(parseViewMode("WireFrame"), ViewMode::Wireframe);
            EXPECT_EQ(parseViewMode("VertexColours"), ViewMode::VertexColours);
            EXPECT_EQ(parseViewMode("NORMALS"), ViewMode::Normals);
        }

        TEST(CSMMeshMeshViewStateTest, parseViewModeRejectsUnknownText)
        {
            EXPECT_FALSE(parseViewMode("").has_value());
            EXPECT_FALSE(parseViewMode("shaded").has_value());
            EXPECT_FALSE(parseViewMode("texture").has_value());
            EXPECT_FALSE(parseViewMode(" flat").has_value());
        }

        TEST(CSMMeshMeshViewStateTest, lightingNamesRoundTripAndParseCaseInsensitively)
        {
            for (LightingPreset preset : sAllLighting)
            {
                const char* name = lightingPresetName(preset);
                ASSERT_NE(name, nullptr);
                EXPECT_FALSE(std::string_view(name).empty());
                const auto parsed = parseLightingPreset(name);
                ASSERT_TRUE(parsed.has_value());
                EXPECT_EQ(*parsed, preset);
            }
            EXPECT_EQ(parseLightingPreset("headlight"), LightingPreset::Headlight);
            EXPECT_EQ(parseLightingPreset("sun"), LightingPreset::Sun);
            EXPECT_EQ(parseLightingPreset("flat"), LightingPreset::Flat);
            EXPECT_EQ(parseLightingPreset("HEADLIGHT"), LightingPreset::Headlight);
            EXPECT_EQ(parseLightingPreset("Sun"), LightingPreset::Sun);
            EXPECT_FALSE(parseLightingPreset("").has_value());
            EXPECT_FALSE(parseLightingPreset("moon").has_value());
        }

        TEST(CSMMeshMeshViewStateTest, onlyTexturedDoesNotHideTheTexturedModel)
        {
            EXPECT_FALSE(hidesTexturedModel(ViewMode::Textured));
            EXPECT_TRUE(hidesTexturedModel(ViewMode::Wireframe));
            EXPECT_TRUE(hidesTexturedModel(ViewMode::Flat));
            EXPECT_TRUE(hidesTexturedModel(ViewMode::VertexColours));
            EXPECT_TRUE(hidesTexturedModel(ViewMode::Normals));
        }

        TEST(CSMMeshMeshViewStateTest, triangleOverlayIsNeededForFlatAndVertexColoursOnly)
        {
            EXPECT_FALSE(needsTriangleOverlay(ViewMode::Textured));
            EXPECT_FALSE(needsTriangleOverlay(ViewMode::Wireframe));
            EXPECT_TRUE(needsTriangleOverlay(ViewMode::Flat));
            EXPECT_TRUE(needsTriangleOverlay(ViewMode::VertexColours));
            EXPECT_FALSE(needsTriangleOverlay(ViewMode::Normals));
        }

        TEST(CSMMeshMeshViewStateTest, lineOverlayFollowsModeAndHelperToggles)
        {
            MeshViewState state; // textured, grid on, everything else off
            EXPECT_FALSE(needsLineOverlay(state));

            state.mode = ViewMode::Wireframe;
            EXPECT_TRUE(needsLineOverlay(state));
            state.mode = ViewMode::Normals;
            EXPECT_TRUE(needsLineOverlay(state));
            state.mode = ViewMode::Flat;
            EXPECT_FALSE(needsLineOverlay(state));
            state.mode = ViewMode::VertexColours;
            EXPECT_FALSE(needsLineOverlay(state));

            for (int which = 0; which < 4; ++which)
            {
                MeshViewState helper;
                switch (which)
                {
                    case 0:
                        helper.showCollision = true;
                        break;
                    case 1:
                        helper.showBounds = true;
                        break;
                    case 2:
                        helper.showSkeleton = true;
                        break;
                    default:
                        helper.showNodeAxes = true;
                        break;
                }
                EXPECT_TRUE(needsLineOverlay(helper)) << which;
                helper.mode = ViewMode::Flat;
                EXPECT_TRUE(needsLineOverlay(helper)) << which;
            }
        }

        TEST(CSMMeshMeshViewStateTest, gridAloneDoesNotNeedALineOverlay)
        {
            MeshViewState state;
            state.showGrid = true;
            EXPECT_FALSE(needsLineOverlay(state));
            state.showGrid = false;
            EXPECT_FALSE(needsLineOverlay(state));
        }
    }
}

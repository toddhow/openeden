// Task 0031, stage A: this translation unit includes only types.hpp, so the header must be self-contained.
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

TEST(EditorModuleHeaderTypes, selfContainedAndDefaults)
{
    const openeden::editor::Vec3 v;
    EXPECT_EQ(v.x, 0.0);
    EXPECT_EQ(v.y, 0.0);
    EXPECT_EQ(v.z, 0.0);

    const openeden::editor::Colour c;
    EXPECT_EQ(c.r, 1.0f);
    EXPECT_EQ(c.a, 1.0f);

    const openeden::editor::PropertyDesc desc;
    EXPECT_EQ(desc.type, openeden::editor::PropertyType::Float);
    EXPECT_FALSE(desc.readOnly);

    EXPECT_EQ(openeden::editor::kApiVersion.major, 1);
    EXPECT_EQ(openeden::editor::kApiVersion.minor, 1);
}

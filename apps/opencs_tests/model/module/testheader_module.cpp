// Task 0031, stage A: this translation unit includes only module.hpp, so the header must be self-contained.
#include <openeden/editor_module/module.hpp>

#include <gtest/gtest.h>

#include <string_view>

TEST(EditorModuleHeaderModule, selfContainedAndEntryPointName)
{
    EXPECT_EQ(std::string_view(openeden::editor::kEditorModuleEntryPoint), "openeden_editor_module_descriptor");
    const openeden::editor::EditorModuleDescriptor descriptor{};
    EXPECT_EQ(descriptor.create, nullptr);
    EXPECT_EQ(descriptor.destroy, nullptr);
}

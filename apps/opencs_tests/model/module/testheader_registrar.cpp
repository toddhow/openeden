// Task 0031, stage A: this translation unit includes only registrar.hpp, so the header must be self-contained.
#include <openeden/editor_module/registrar.hpp>

#include <gtest/gtest.h>

TEST(EditorModuleHeaderRegistrar, selfContainedAndDefaults)
{
    const openeden::editor::Registration registration;
    EXPECT_TRUE(registration.ok);
    EXPECT_TRUE(registration.error.empty());

    const openeden::editor::PanelDesc panel;
    EXPECT_FALSE(static_cast<bool>(panel.source));

    const openeden::editor::CommandDesc command;
    EXPECT_FALSE(static_cast<bool>(command.handler));
    EXPECT_FALSE(static_cast<bool>(command.enabledWhen));
}

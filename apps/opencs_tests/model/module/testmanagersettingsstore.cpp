// Task 0091, stage e: the editor's settings store over Settings::Manager's user settings. Written with the code (it is
// a five-line adapter); what it pins is the section and key layout of eden-studio.cfg: `[module.<id>]` and the
// setting's id.
#include "apps/opencs/model/module/managersettingsstore.hpp"

#include <components/settings/settings.hpp>

#include <gtest/gtest.h>

namespace
{
    TEST(CSMManagerSettingsStore, valuesLiveInTheUserSettingsUnderTheModuleSection)
    {
        CSMModule::ManagerSettingsStore store;
        EXPECT_FALSE(store.get("module.testmgr", "testmgr.key").has_value());
        store.set("module.testmgr", "testmgr.key", "value one");
        EXPECT_EQ(store.get("module.testmgr", "testmgr.key"), std::optional<std::string>("value one"));
        EXPECT_EQ(Settings::Manager::getString("testmgr.key", "module.testmgr"), "value one");
        store.set("module.testmgr", "testmgr.key", "two");
        EXPECT_EQ(Settings::Manager::getString("testmgr.key", "module.testmgr"), "two");
        Settings::Manager::mUserSettings.erase(
            std::make_pair(std::string("module.testmgr"), std::string("testmgr.key")));
    }

    TEST(CSMManagerSettingsStore, aModuleSettingsRoundTripsThroughIt)
    {
        CSMModule::ManagerSettingsStore store;
        CSMModule::ModuleSettings settings;
        settings.setStore(&store);
        openeden::editor::SettingDesc desc;
        desc.property.id = "testmgr2.flag";
        desc.property.type = openeden::editor::PropertyType::Bool;
        desc.defaultValue = true;
        ASSERT_TRUE(settings.add("testmgr2", desc).ok);
        EXPECT_EQ(Settings::Manager::getString("testmgr2.flag", "module.testmgr2"), "true");
        settings.set("testmgr2.flag", openeden::editor::PropertyValue(false));
        EXPECT_EQ(Settings::Manager::getString("testmgr2.flag", "module.testmgr2"), "false");
        Settings::Manager::mUserSettings.erase(
            std::make_pair(std::string("module.testmgr2"), std::string("testmgr2.flag")));
    }
}

// Task 0361d: a game pack (neutral) session starts with [GUI] rmlui on and the RmlUi debug panel off, so the pack's own
// HUD shows with no hand settings; Morrowind and explicit user values are untouched
// (components/settings/neutraldefaults.hpp).
#include <gtest/gtest.h>

#include <components/settings/neutraldefaults.hpp>

namespace
{
    using Settings::CategorySettingValueMap;
    namespace ND = Settings::NeutralDefaults;

    CategorySettingValueMap shipped()
    {
        CategorySettingValueMap m;
        m[{ "Rendering", "hdr" }] = "false";
        m[{ "GUI", "rmlui" }] = "false";
        m[{ "GUI", "rmlui debug panel" }] = "true";
        m[{ "Game", "neutral world" }] = "false";
        return m;
    }

    std::string get(const CategorySettingValueMap& m, std::string_view cat, std::string_view key)
    {
        return m.find(std::make_pair(cat, key))->second;
    }

    TEST(NeutralDefaultsHud, aNeutralSessionGetsTheOverlayOnAndItsDebugPanelOff)
    {
        const CategorySettingValueMap original = shipped();
        CategorySettingValueMap defaults = original;
        CategorySettingValueMap user;
        user[{ "Game", "neutral world" }] = "true";
        ND::apply(defaults, user, original);
        EXPECT_EQ(get(defaults, "GUI", "rmlui"), "true");
        EXPECT_EQ(get(defaults, "GUI", "rmlui debug panel"), "false");
    }

    TEST(NeutralDefaultsHud, morrowindKeepsBoth)
    {
        const CategorySettingValueMap original = shipped();
        CategorySettingValueMap defaults = original;
        ND::apply(defaults, CategorySettingValueMap{}, original);
        EXPECT_EQ(get(defaults, "GUI", "rmlui"), "false");
        EXPECT_EQ(get(defaults, "GUI", "rmlui debug panel"), "true");
    }

    TEST(NeutralDefaultsHud, anExplicitUserValueWinsEitherWay)
    {
        const CategorySettingValueMap original = shipped();
        CategorySettingValueMap defaults = original;
        CategorySettingValueMap user;
        user[{ "Game", "neutral world" }] = "true";
        user[{ "GUI", "rmlui" }] = "false";
        user[{ "GUI", "rmlui debug panel" }] = "true";
        ND::apply(defaults, user, original);
        EXPECT_EQ(get(defaults, "GUI", "rmlui"), "false");
        EXPECT_EQ(get(defaults, "GUI", "rmlui debug panel"), "true");
    }

    TEST(NeutralDefaultsHud, aChangedDefaultFromAnotherFolderCountsAsExplicit)
    {
        const CategorySettingValueMap original = shipped();
        CategorySettingValueMap defaults = original;
        defaults[{ "GUI", "rmlui debug panel" }] = "true";
        defaults[{ "GUI", "rmlui" }] = "false";
        defaults[{ "GUI", "rmlui" }] = "false";
        CategorySettingValueMap changed = original;
        changed[{ "GUI", "rmlui debug panel" }] = "false"; // pretend the shipped file said false
        CategorySettingValueMap user;
        user[{ "Game", "neutral world" }] = "true";
        ND::apply(defaults, user, changed);
        EXPECT_EQ(get(defaults, "GUI", "rmlui debug panel"), "true");
    }
}

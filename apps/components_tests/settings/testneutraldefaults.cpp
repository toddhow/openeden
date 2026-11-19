// Task 0355 (follow-up): a game pack session starts with [Rendering] hdr on; Morrowind default and an explicit user
// value are untouched (components/settings/neutraldefaults.hpp).
#include <gtest/gtest.h>

#include <components/settings/neutraldefaults.hpp>

namespace
{
    using Settings::CategorySettingValueMap;
    namespace ND = Settings::NeutralDefaults;

    CategorySettingValueMap shipped(bool neutral)
    {
        CategorySettingValueMap m;
        m[{ "Rendering", "hdr" }] = "false";
        m[{ "Game", "neutral world" }] = neutral ? "true" : "false";
        return m;
    }

    std::string hdr(const CategorySettingValueMap& m)
    {
        return m.find(std::make_pair(std::string_view("Rendering"), std::string_view("hdr")))->second;
    }

    TEST(NeutralDefaults, aNeutralSessionWithNoUserValueGetsHdrOn)
    {
        const CategorySettingValueMap original = shipped(false);
        CategorySettingValueMap defaults = original;
        CategorySettingValueMap user;
        user[{ "Game", "neutral world" }] = "true"; // the user file turns the neutral world on
        ND::apply(defaults, user, original);
        EXPECT_EQ(hdr(defaults), "true");
        EXPECT_TRUE(user.find(std::make_pair(std::string_view("Rendering"), std::string_view("hdr"))) == user.end())
            << "nothing is written to the user settings (they are saved at exit)";
    }

    TEST(NeutralDefaults, morrowindKeepsItsDefault)
    {
        const CategorySettingValueMap original = shipped(false);
        CategorySettingValueMap defaults = original;
        ND::apply(defaults, {}, original);
        EXPECT_EQ(hdr(defaults), "false");
        CategorySettingValueMap user;
        user[{ "Game", "neutral world" }] = "false";
        ND::apply(defaults, user, original);
        EXPECT_EQ(hdr(defaults), "false");
    }

    TEST(NeutralDefaults, anExplicitUserValueWinsEitherWay)
    {
        for (const char* value : { "false", "true" })
        {
            const CategorySettingValueMap original = shipped(false);
            CategorySettingValueMap defaults = original;
            CategorySettingValueMap user;
            user[{ "Game", "neutral world" }] = "true";
            user[{ "Rendering", "hdr" }] = value;
            ND::apply(defaults, user, original);
            EXPECT_EQ(hdr(defaults), "false") << "the default is not touched, the user value is read first";
            EXPECT_FALSE(ND::hdrDefaultApplies(defaults, user, original)) << value;
        }
    }

    TEST(NeutralDefaults, aValueFromAnotherConfigFolderCountsAsExplicit)
    {
        const CategorySettingValueMap original = shipped(false);
        CategorySettingValueMap defaults = original;
        defaults[{ "Game", "neutral world" }] = "true";
        EXPECT_TRUE(ND::hdrDefaultApplies(defaults, {}, original)) << "unchanged from the shipped value";
        CategorySettingValueMap other = original;
        other[{ "Rendering", "hdr" }] = "true"; // overridden by an additional settings.cfg
        other[{ "Game", "neutral world" }] = "true";
        EXPECT_FALSE(ND::hdrDefaultApplies(other, {}, original)) << "a changed default is someone's choice";
    }

    TEST(NeutralDefaults, theNeutralWorldSettingReadsUserBeforeDefault)
    {
        CategorySettingValueMap defaults = shipped(true);
        CategorySettingValueMap user;
        EXPECT_TRUE(ND::neutralWorldOn(defaults, user));
        user[{ "Game", "neutral world" }] = "false";
        EXPECT_FALSE(ND::neutralWorldOn(defaults, user));
        EXPECT_FALSE(ND::neutralWorldOn({}, {})) << "no line anywhere is off";
    }
}
